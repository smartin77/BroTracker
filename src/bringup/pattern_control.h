#pragma once

#include "core/playback/native_rate_pattern_player.h"

namespace BroTracker
{
    constexpr std::size_t kPatternRequestCapacity = 8;
    enum class PatternRequest { Start, Stop, Pause, Continue };
    enum class PatternRequestStatus { Accepted, Full, InvalidRequest };
    enum class PatternFault { None, Configuration, Render, Allocation };
    struct PatternAppliedRequest
    {
        PatternRequest request = PatternRequest::Stop;
        PatternPlayerStatus result = PatternPlayerStatus::Success;
    };
    struct PatternBringUpStatus
    {
        // One audio-owner publication: applied transport, consumed position and
        // dimensions belong together. Copy only under platform interrupt exclusion.
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
    };

    // Portable bring-up policy, not the final transport/protocol. No locks inside:
    // platform MUST exclude the audio owner during every main-loop Submit,
    // TakeApplied and Snapshot call, using a real critical section with compiler
    // memory barriers. Audio methods run in the single audio owner only. No
    // volatile-based synchronization, callbacks, allocation, I/O or clock policy.
    // Fixed request AND acknowledgement FIFOs preserve accepted order. If the
    // acknowledgement FIFO is full, leave requests pending rather than losing
    // acknowledgements. Full request FIFO explicitly rejects without overwriting.
    class PatternBringUpControl
    {
    public:
        [[nodiscard]] PatternRequestStatus Submit(PatternRequest request) noexcept
        {
            if (request != PatternRequest::Start && request != PatternRequest::Stop &&
                request != PatternRequest::Pause && request != PatternRequest::Continue)
                return PatternRequestStatus::InvalidRequest;
            if (request_count_ == kPatternRequestCapacity) return PatternRequestStatus::Full;
            requests_[(request_head_ + request_count_) % kPatternRequestCapacity] = request;
            ++request_count_;
            return PatternRequestStatus::Accepted;
        }
        bool TakeApplied(PatternAppliedRequest& output) noexcept
        {
            if (applied_count_ == 0) return false;
            output = applied_[applied_head_];
            applied_head_ = (applied_head_ + 1) % kPatternRequestCapacity;
            --applied_count_;
            return true;
        }
        PatternBringUpStatus Snapshot() const noexcept { return status_; }

        // Audio owner only, called once before accepting the first audio block.
        // Never configures/restarts from HELLO, reconnect or the main loop.
        [[nodiscard]] PatternPlayerStatus InitializeAudio(std::uint32_t tempo_hundredths,
            std::uint32_t rate, const RealtimePattern& pattern,
            const NativeRateSampleBindings& bindings) noexcept
        {
            const auto result = player_.Configure(tempo_hundredths, rate, pattern, bindings);
            if (result != PatternPlayerStatus::Success) Fail(PatternFault::Configuration, result);
            Publish();
            return result;
        }

        // Audio owner only. Apply at most eight requests at the block boundary,
        // then render exactly once. Caller supplies <=128 frames and valid writable
        // output when storage_available. PCM lifetime/non-overlap follow the player.
        // Rejection zeros the entire available span, Stops and latches an error;
        // subsequent blocks stay silent, never retrying a frozen position. Explicit
        // successful START clears the latch; STOP alone retains error evidence.
        // Allocation failure stops/resets time (no rendering or advancement),
        // increments a saturating diagnostic counter, and transmits no block.
        // This is an explicit bring-up fail-stop policy, not a final underrun policy.
        void AudioBlock(std::int16_t* output, std::size_t count, bool storage_available) noexcept
        {
            for (std::size_t budget = 0; budget < kPatternRequestCapacity &&
                request_count_ != 0 && applied_count_ < kPatternRequestCapacity; ++budget)
            {
                const auto request = requests_[request_head_];
                request_head_ = (request_head_ + 1) % kPatternRequestCapacity;
                --request_count_;
                PatternPlayerStatus result = PatternPlayerStatus::Success;
                if (request == PatternRequest::Start)
                {
                    result = player_.Start();
                    if (result == PatternPlayerStatus::Success)
                    {
                        status_.fault = PatternFault::None;
                    }
                    else Fail(PatternFault::Configuration, result);
                }
                else if (request == PatternRequest::Stop) player_.Stop();
                else if (status_.fault != PatternFault::None) result = PatternPlayerStatus::InvalidTransportState;
                else if (request == PatternRequest::Pause) result = player_.Pause();
                else result = player_.Continue();
                applied_[(applied_head_ + applied_count_) % kPatternRequestCapacity] = {request,result};
                ++applied_count_;
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
            Publish();
        }
    private:
        static void Increment(std::uint32_t& value) noexcept { if (value != UINT32_MAX) ++value; }
        static void Zero(std::int16_t* output, std::size_t count) noexcept
        { for (std::size_t i = 0; i < count; ++i) output[i] = 0; }
        void Fail(PatternFault fault, PatternPlayerStatus error) noexcept
        {
            player_.Stop();
            status_.fault = fault;
            status_.last_fault = fault;
            status_.player_error = error;
            Increment(status_.failures);
        }
        void Publish() noexcept
        {
            status_.running = player_.IsRunning();
            status_.transport = player_.GetTransportState();
            status_.next_sample = player_.GetNextSamplePosition();
            status_.position = player_.GetPlaybackPosition();
            status_.active_rows = player_.GetActiveRows();
            status_.active_channels = player_.GetActiveChannels();
        }
        NativeRatePatternPlayer player_;
        PatternBringUpStatus status_;
        PatternRequest requests_[kPatternRequestCapacity]{};
        PatternAppliedRequest applied_[kPatternRequestCapacity]{};
        std::size_t request_head_ = 0, request_count_ = 0;
        std::size_t applied_head_ = 0, applied_count_ = 0;
    };
}
