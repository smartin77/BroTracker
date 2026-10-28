#pragma once

#include "core/playback/native_rate_pattern_player.h"
#include "pattern_performance.h"
#include "core/audio/master_volume.h"
#include <type_traits>
#include <utility>

namespace BroTracker
{
    template<class T,class=void> struct SupportsChannelMute : std::false_type {};
    template<class T> struct SupportsChannelMute<T,std::void_t<decltype(std::declval<T&>().SetMuteMask(0u))>> : std::true_type {};
    constexpr std::size_t kPatternRequestCapacity = 8;
    enum class PatternRequest { Start, Stop, Pause, Continue, Replace, Preview, PreviewApplied, PreviewSuperseded, Gains, Tempo, MasterVolume };
    enum class PatternRequestStatus { Accepted, Full, InvalidRequest, InvalidGain, Identity, InvalidTempo };
    enum class PatternFault { None, Configuration, Render, Allocation };
    struct PatternAppliedRequest
    {
        PatternRequest request = PatternRequest::Stop;
        PatternPlayerStatus result = PatternPlayerStatus::Success;
        std::uint32_t publication_id = 0;
        std::uint32_t tempo = 0;
    };
    struct PatternBringUpStatus
    {
        // One audio-owner publication: applied transport, consumed position and
        // dimensions belong together. Copy only under platform interrupt exclusion.
        PatternPerformance performance;
        Pcm16ChannelGains gains;
        unsigned volume_percent=100;std::uint32_t volume_current=65536,applied_volume_id=0,next_volume_id=1;
        unsigned mute_mask=0; std::uint32_t applied_mute_id=0,next_mute_id=1;
        std::uint32_t applied_gain_id = 0, next_gain_id = 1;
        std::uint32_t tempo=0,published_tempo=0,settled_tempo_id=0,applied_tempo_id=0,pending_tempo_id=0,next_tempo_id=1;
        bool running = false;
        PatternTransportState transport = PatternTransportState::Stopped;
        std::uint64_t next_sample = 0;
        ConsumedPatternPosition position;
        std::uint32_t active_rows = 0, active_channels = 0;
        PatternFault fault = PatternFault::None;
        PatternFault last_fault = PatternFault::None;
        PatternPlayerStatus player_error = PatternPlayerStatus::Success;
        std::uint32_t failures = 0;
        std::uint32_t allocation_failures = 0;
        std::uint32_t published_id = 0, working_id = 0, pending_preview_id = 0;
    };

    // Portable bring-up policy, not the final transport/protocol. No locks inside:
    // platform MUST exclude the audio owner during every main-loop Submit,
    // TakeApplied and Snapshot call, using a real critical section with compiler
    // memory barriers. Audio methods run in the single audio owner only. No
    // volatile-based synchronization, callbacks, allocation, I/O or clock policy.
    // Fixed request AND acknowledgement FIFOs preserve accepted order. If the
    // acknowledgement FIFO is full, leave requests pending rather than losing
    // acknowledgements. Full request FIFO explicitly rejects without overwriting.
    template<class Player, class Bindings>
    class BasicPatternBringUpControl
    {
    public:
        [[nodiscard]] PatternRequestStatus SubmitMute(unsigned mask,std::uint32_t id) noexcept {
            if(!SupportsChannelMute<Player>::value || mask>255 || !id || id<=highest_mute_id_) return PatternRequestStatus::InvalidRequest;
            pending_mute_mask_=mask; pending_mute_id_=highest_mute_id_=id;
            return PatternRequestStatus::Accepted;
        }
        void OutputFailure() noexcept { Fail(PatternFault::Render,PatternPlayerStatus::ComponentError); Publish(); }
        Player& OutputPlayer() noexcept { return player_; } // Platform audio owner only.
        // Audio owner only; main loop snapshots this under the existing exclusion.
        void RecordPerformance(std::uint32_t begin, std::uint32_t end, bool initialization,
            std::uint32_t budget) noexcept { RecordPatternCycles(status_.performance,begin,end,initialization,budget); }
        [[nodiscard]] PatternRequestStatus Submit(PatternRequest request) noexcept
        {
            if (request != PatternRequest::Start && request != PatternRequest::Stop &&
                request != PatternRequest::Pause && request != PatternRequest::Continue)
                return PatternRequestStatus::InvalidRequest;
            if (request_count_ == kPatternRequestCapacity) return PatternRequestStatus::Full;
            if(request==PatternRequest::Stop) tempo_cancel_through_=highest_tempo_id_;
            requests_[(request_head_ + request_count_) % kPatternRequestCapacity] = {request, nullptr, 0};
            ++request_count_;
            return PatternRequestStatus::Accepted;
        }
        // Main-loop critical section. Storage must stay frozen until TakeApplied
        // returns this ID. One outstanding replacement; FIFO order with transport.
        [[nodiscard]] PatternRequestStatus SubmitPattern(const RealtimePattern& pattern,
            std::uint32_t id, std::uint32_t tempo=0) noexcept {
            if(tempo && !ValidInteractiveTempo(tempo)) return PatternRequestStatus::InvalidTempo;
            if (!id) return PatternRequestStatus::InvalidRequest;
            if (publication_pending_ || request_count_ == kPatternRequestCapacity) return PatternRequestStatus::Full;
            requests_[(request_head_ + request_count_) % kPatternRequestCapacity] = {PatternRequest::Replace, &pattern, id};
            requests_[(request_head_ + request_count_) % kPatternRequestCapacity].tempo=tempo;
            ++request_count_; publication_pending_ = true;
            return PatternRequestStatus::Accepted;
        }
        // Same frozen ownership as publication, but replacement may stage while Playing.
        [[nodiscard]] PatternRequestStatus SubmitPreview(const RealtimePattern& pattern,
            std::uint32_t id) noexcept {
            if (!id) return PatternRequestStatus::InvalidRequest;
            if (publication_pending_ || request_count_ == kPatternRequestCapacity) return PatternRequestStatus::Full;
            requests_[(request_head_ + request_count_) % kPatternRequestCapacity] = {PatternRequest::Preview, &pattern, id};
            ++request_count_; publication_pending_ = true;
            return PatternRequestStatus::Accepted;
        }
        [[nodiscard]] PatternRequestStatus SubmitVolume(unsigned percent,std::uint32_t id) noexcept {
            if(percent>100)return PatternRequestStatus::InvalidGain;
            if(!id || id<=highest_volume_id_)return PatternRequestStatus::Identity;
            if(request_count_==kPatternRequestCapacity)return PatternRequestStatus::Full;
            auto& entry=requests_[(request_head_+request_count_)%kPatternRequestCapacity];
            entry={};entry.request=PatternRequest::MasterVolume;entry.id=id;entry.tempo=percent;
            ++request_count_;highest_volume_id_=id;return PatternRequestStatus::Accepted;
        }
        // Copy complete metadata into the FIFO under the platform critical section.
        // Accepted identities strictly increase for this boot; queue rejection does
        // not consume an ID. No HELLO/disconnect resets or automatic replay.
        [[nodiscard]] PatternRequestStatus SubmitGains(const Pcm16ChannelGains& gains,
            std::uint32_t id) noexcept {
            for(const auto gain:gains.channels)
                if(gain>kPcm16UnityGain) return PatternRequestStatus::InvalidGain;
            if(!id || id<=highest_gain_id_) return PatternRequestStatus::Identity;
            if(request_count_==kPatternRequestCapacity) return PatternRequestStatus::Full;
            requests_[(request_head_+request_count_)%kPatternRequestCapacity]={PatternRequest::Gains,nullptr,id,gains};
            ++request_count_; highest_gain_id_=id; return PatternRequestStatus::Accepted;
        }
        [[nodiscard]] PatternRequestStatus SubmitTempo(std::uint32_t value,std::uint32_t id) noexcept {
            if(!ValidInteractiveTempo(value)) return PatternRequestStatus::InvalidTempo;
            if(!id || id<=highest_tempo_id_) return PatternRequestStatus::Identity;
            if(request_count_==kPatternRequestCapacity) return PatternRequestStatus::Full;
            auto& entry=requests_[(request_head_+request_count_)%kPatternRequestCapacity];
            entry={}; entry.request=PatternRequest::Tempo; entry.id=id; entry.tempo=value;
            ++request_count_; highest_tempo_id_=id; return PatternRequestStatus::Accepted;
        }
        bool TakeApplied(PatternAppliedRequest& output) noexcept
        {
            if (applied_count_ == 0) {
                if (!deferred_notification_) return false;
                output = notification_; deferred_notification_ = false; return true;
            }
            output = applied_[applied_head_];
            applied_head_ = (applied_head_ + 1) % kPatternRequestCapacity;
            --applied_count_;
            if (output.request == PatternRequest::Replace || output.request == PatternRequest::Preview) publication_pending_ = false;
            return true;
        }
        PatternBringUpStatus Snapshot() const noexcept {
            auto result=status_;
            result.next_mute_id=highest_mute_id_==UINT32_MAX?0:highest_mute_id_+1;
            result.next_tempo_id=highest_tempo_id_==UINT32_MAX?0:highest_tempo_id_+1;
            if(highest_tempo_id_>result.settled_tempo_id) result.pending_tempo_id=highest_tempo_id_;
            result.next_volume_id=highest_volume_id_==UINT32_MAX?0:highest_volume_id_+1;
            result.next_gain_id=highest_gain_id_==UINT32_MAX ? 0 : highest_gain_id_+1;
            return result;
        }
        // Main context under the same interrupt exclusion as Snapshot. A stopped
        // status alone is insufficient while queued requests/ACKs can still mutate
        // the baseline. External MIDI/SD readiness is checked by the platform.
        bool StorageReady(bool allow_paused=false) const noexcept {
            return status_.tempo && (status_.transport==PatternTransportState::Stopped || (allow_paused && status_.transport==PatternTransportState::Paused)) &&
                status_.fault==PatternFault::None && !request_count_ && !applied_count_ &&
                !deferred_notification_ && !publication_pending_ && !pending_mute_id_ &&
                (!status_.pending_preview_id || (allow_paused && status_.transport==PatternTransportState::Paused)) && !status_.pending_tempo_id;
        }

        // Audio owner only, called once before accepting the first audio block.
        // Never configures/restarts from HELLO, reconnect or the main loop.
        [[nodiscard]] PatternPlayerStatus InitializeAudio(std::uint32_t tempo_hundredths,
            std::uint32_t rate, const RealtimePattern& pattern,
            const Bindings& bindings,std::uint32_t published_tempo=0) noexcept
        {
            volume_.Configure(rate);
            const auto result = player_.Configure(tempo_hundredths, rate, pattern, bindings);
            if (result != PatternPlayerStatus::Success) Fail(PatternFault::Configuration, result);
            if(result==PatternPlayerStatus::Success) {status_.applied_gain_id=0; status_.published_tempo=published_tempo?published_tempo:tempo_hundredths;}
            Publish();
            return result;
        }

        // Audio owner only. Apply at most eight requests at the block boundary,
        // then render exactly once. Each replacement adds <=63 logical rows of
        // validation/reconstruction, <=8 events and <=16 binding comparisons per
        // event; a playing preview application adds <=31 rows during Render.
        // Caller supplies <=128 frames and valid writable
        // output when storage_available. PCM lifetime/non-overlap follow the player.
        // Rejection zeros the entire available span, Stops and latches an error;
        // subsequent blocks stay silent, never retrying a frozen position. Explicit
        // successful START clears the latch; STOP alone retains error evidence.
        // Allocation failure stops/resets time (no rendering or advancement),
        // increments a saturating diagnostic counter, and transmits no block.
        // This is an explicit bring-up fail-stop policy, not a final underrun policy.
        void AudioBlock(std::int16_t* output, std::size_t count, bool storage_available) noexcept
        {
            // Separate latest slot applies before the transport/publication FIFO.
            if constexpr(SupportsChannelMute<Player>::value) if(pending_mute_id_) {
                const auto result=player_.SetMuteMask(pending_mute_mask_);
                if(result==PatternPlayerStatus::Success) { status_.mute_mask=pending_mute_mask_; status_.applied_mute_id=pending_mute_id_; }
                else Fail(PatternFault::Render,result);
                pending_mute_id_=0;
            }
            for (std::size_t budget = 0; budget < kPatternRequestCapacity &&
                request_count_ != 0 && !deferred_notification_ && applied_count_ < kPatternRequestCapacity; ++budget)
            {
                const auto entry = requests_[request_head_];
                const std::size_t needed = entry.request == PatternRequest::Preview || entry.request == PatternRequest::Replace
                    ? 3 : status_.pending_preview_id ? 2 : 1;
                if (kPatternRequestCapacity - applied_count_ < needed) break;
                const auto request = entry.request;
                request_head_ = (request_head_ + 1) % kPatternRequestCapacity;
                --request_count_;
                PatternPlayerStatus result = PatternPlayerStatus::Success;
                if(request==PatternRequest::Tempo) {
                    if(entry.id<=tempo_cancel_through_) { player_.CancelTempo(); status_.pending_tempo_id=0; status_.settled_tempo_id=entry.id; }
                    else {
                        result=player_.RequestTempo(entry.tempo);
                        if(result==PatternPlayerStatus::Success) status_.pending_tempo_id=entry.id;
                        else status_.settled_tempo_id=entry.id;
                    }
                    CompleteTempo(); continue;
                }
                if(request==PatternRequest::MasterVolume) {
                    (void)volume_.Set(entry.tempo);status_.applied_volume_id=entry.id;
                }
                else if (request == PatternRequest::Gains) {
                    result=player_.SetChannelGains(entry.gains);
                    if(result==PatternPlayerStatus::Success) status_.applied_gain_id=entry.id;
                }
                else if (request == PatternRequest::Preview) {
                    result = player_.ReplaceWorkingPattern(*entry.pattern);
                    if (result == PatternPlayerStatus::Success) {
                        SupersedePreview();
                        status_.pending_preview_id = entry.id;
                    }
                }
                else if (request == PatternRequest::Replace) {
                    result = player_.GetTransportState() == PatternTransportState::Paused
                        ? player_.ReplacePausedPattern(*entry.pattern) : player_.ReplacePattern(*entry.pattern);
                    if (result == PatternPlayerStatus::Success) {
                        if(entry.tempo) (void)player_.RequestTempo(entry.tempo);
                        status_.published_tempo=player_.GetTempo();
                        SupersedePreview(); status_.published_id = entry.id; status_.working_id = 0; // Published revision, separate ID domain.
                    }
                }
                else if (request == PatternRequest::Start)
                {
                    if(status_.pending_tempo_id) { status_.settled_tempo_id=status_.pending_tempo_id; status_.pending_tempo_id=0; }
                    result = player_.Start();
                    if (result == PatternPlayerStatus::Success)
                    {
                        status_.fault = PatternFault::None;
                    }
                    else Fail(PatternFault::Configuration, result);
                }
                else if (request == PatternRequest::Stop) {
                    player_.Stop();
                    if(status_.pending_tempo_id) status_.settled_tempo_id=status_.pending_tempo_id;
                    status_.pending_tempo_id=0;
                }
                else if (status_.fault != PatternFault::None) result = PatternPlayerStatus::InvalidTransportState;
                else if (request == PatternRequest::Pause) result = player_.Pause();
                else result = player_.Continue();
                applied_[(applied_head_ + applied_count_) % kPatternRequestCapacity] = {request,result,entry.id,player_.GetTempo()};
                ++applied_count_;
                CompletePreview();
            }
            if (!storage_available)
            {
                Increment(status_.allocation_failures);
                Fail(PatternFault::Allocation, PatternPlayerStatus::Success);
            }
            else if (status_.fault == PatternFault::None)
            {
                const auto result = player_.Render(output, count);
                if (result != PatternPlayerStatus::Success)
                {
                    Fail(PatternFault::Render, result);
                    Zero(output, count);
                }
            }
            else Zero(output, count);
            if(storage_available && output) volume_.Process(output,count); // Combined PCM, exactly once before every route.
            CompletePreview();
            CompleteTempo();
            Publish();
        }
    private:
        void CompleteTempo() noexcept {
            if(status_.pending_tempo_id && !player_.HasPendingTempo()) {
                status_.settled_tempo_id=status_.applied_tempo_id=status_.pending_tempo_id;
                status_.pending_tempo_id=0;
            }
        }
        // One completion debt slot covers application when the ACK FIFO fills.
        // While owed, no new requests are applied; rendering/transport timeline continues.
        void Notify(PatternRequest kind, std::uint32_t id) noexcept {
            PatternAppliedRequest event{kind,PatternPlayerStatus::Success,id};
            if (applied_count_ < kPatternRequestCapacity) {
                applied_[(applied_head_+applied_count_)%kPatternRequestCapacity] = event; ++applied_count_;
            } else { notification_=event; deferred_notification_=true; }
        }
        void SupersedePreview() noexcept {
            if (status_.pending_preview_id) Notify(PatternRequest::PreviewSuperseded,status_.pending_preview_id);
            status_.pending_preview_id=0;
        }
        void CompletePreview() noexcept {
            if (status_.pending_preview_id && !player_.HasPendingWorkingPattern()) {
                status_.working_id=status_.pending_preview_id;
                status_.pending_preview_id=0;
                Notify(PatternRequest::PreviewApplied,status_.working_id);
            }
        }
        static void Increment(std::uint32_t& value) noexcept { if (value != UINT32_MAX) ++value; }
        static void Zero(std::int16_t* output, std::size_t count) noexcept
        { for (std::size_t i = 0; i < count; ++i) output[i] = 0; }
        void Fail(PatternFault fault, PatternPlayerStatus error) noexcept
        {
            player_.Stop();
            if(status_.pending_tempo_id) status_.settled_tempo_id=status_.pending_tempo_id;
            status_.pending_tempo_id=0;
            status_.fault = fault;
            status_.last_fault = fault;
            status_.player_error = error;
            Increment(status_.failures);
        }
        void Publish() noexcept
        {
            status_.volume_percent=volume_.Percent();status_.volume_current=volume_.Current();
            status_.tempo=player_.GetTempo();
            status_.gains = player_.GetChannelGains();
            status_.running = player_.IsRunning();
            status_.transport = player_.GetTransportState();
            status_.next_sample = player_.GetNextSamplePosition();
            status_.position = player_.GetPlaybackPosition();
            status_.active_rows = player_.GetActiveRows();
            status_.active_channels = player_.GetActiveChannels();
        }
        unsigned pending_mute_mask_=0;
        std::uint32_t pending_mute_id_=0,highest_mute_id_=0;
        MasterVolume volume_;std::uint32_t highest_volume_id_=0;
        Player player_;
        PatternBringUpStatus status_;
        struct RequestEntry { PatternRequest request = PatternRequest::Stop; const RealtimePattern* pattern = nullptr; std::uint32_t id = 0; Pcm16ChannelGains gains; std::uint32_t tempo=0; };
        RequestEntry requests_[kPatternRequestCapacity]{};
        std::uint32_t highest_gain_id_ = 0, highest_tempo_id_=0, tempo_cancel_through_=0;
        bool publication_pending_ = false;
        bool deferred_notification_ = false;
        PatternAppliedRequest notification_;
        PatternAppliedRequest applied_[kPatternRequestCapacity]{};
        std::size_t request_head_ = 0, request_count_ = 0;
        std::size_t applied_head_ = 0, applied_count_ = 0;
    };
    // Existing default/API stays native-rate. Only the chosen specialization owns a player.
    using PatternBringUpControl = BasicPatternBringUpControl<NativeRatePatternPlayer, NativeRateSampleBindings>;
}
