#ifdef BROTRACKER_PATTERN_BRINGUP
#include "pattern_bringup.h"
#include <Arduino.h>
#include <Audio.h>
#include <musical_tick_cursor.h> // PlatformIO discovers the existing scheduler library.
#include "bringup/pattern_player_selection.h"
#include "bringup/pattern_fixture.h"
#include "bringup/pattern_telemetry.h"
#include "bringup/pattern_snapshot.h"
#include "bringup/pattern_publication.h"
#include "bringup/pattern_preview.h"
#include "bringup/pattern_gains.h"
#ifdef BROTRACKER_PATTERN_SD_PREPARE
#include "sd_wav_preparation.h"
#include "sd_wav_inspection.h"
#endif
#include <array>
#include <cstdio>
#include <cstring>

namespace BroTracker
{
namespace
{
    static_assert(AUDIO_BLOCK_SAMPLES == kSelectedPatternFrameCapacity,
        "Pattern bring-up requires the current 128-frame Teensy audio blocks");
    // Short, low-amplitude, immutable diagnostic PCM fixtures, not a synth engine.
    // Integer compile-time square/triangle bursts taper to zero; no audio-time synthesis.
    constexpr std::array<std::int16_t, 8192> MakeSample(bool triangle)
    {
        std::array<std::int16_t, 8192> pcm{};
        for (std::size_t i = 0; i < pcm.size(); ++i)
        {
            const int phase = static_cast<int>(i % (triangle ? 64 : 100));
            const int wave = triangle ? (phase < 32 ? phase * 32 - 512 : 1536 - phase * 32) :
                (phase < 50 ? 600 : -600);
            pcm[i] = static_cast<std::int16_t>(wave * static_cast<int>(pcm.size() - 1 - i) /
                static_cast<int>(pcm.size() - 1));
        }
        return pcm;
    }
    constexpr auto kSampleA = MakeSample(false);
    constexpr auto kSampleB = MakeSample(true);
    constexpr std::uint32_t kTempoHundredths = 12753;
    const RealtimePattern kPattern = MakeBeeperPattern();
    const SelectedPatternBindings kBindings = [] {
        SelectedPatternBindings bindings{}; bindings.count = 2;
        bindings.bindings[0] = {0,60,{kSampleA.data(),kSampleA.size(),44100}};
        bindings.bindings[1] = {1,62,{kSampleB.data(),kSampleB.size(),44100}};
        return bindings;
    }();
    SelectedPatternBringUpControl g_control; // Includes the statically allocated core player/scratch.
#ifdef BROTRACKER_PATTERN_SD_PREPARE
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
    constexpr std::size_t kPreparedFrameCapacity=kDevelopmentManualWavFrameCapacity;
    DMAMEM alignas(32) static std::int16_t g_prepared_pcm3[kPreparedFrameCapacity];
    SdWavPreparation g_preparation3;
    SdPreparationReport g_preparation_report3;
#else
    constexpr std::size_t kPreparedFrameCapacity=131072;
#endif
    DMAMEM alignas(32) static std::int16_t g_prepared_pcm[kPreparedFrameCapacity];
    SdWavPreparation g_preparation; // Main-owned immutable after setup; optionally retained by voices.
    SdPreparationReport g_preparation_report;
    DevelopmentWavInspection g_wav_inspection;
    WavInspectionReport g_wav_inspection_report;
#endif
    RealtimePattern g_active_pattern = kPattern; // Main readback source; updated after audio acknowledgement.
    SelectedPatternBindings g_selected_bindings = kBindings; // Frozen before audio activation.
    bool g_audio_ready = false; // Published under InterruptGuard after AudioMemory.

    // Save/restore PRIMASK, including callers with interrupts already disabled.
    // Memory clobbers prevent compiler reordering across the handoff. Main-loop
    // sections only copy a small status or one fixed FIFO entry; no I/O inside.
    class InterruptGuard
    {
    public:
        InterruptGuard() noexcept
        { asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask_) :: "memory"); }
        ~InterruptGuard() noexcept
        { asm volatile("msr primask, %0" :: "r"(mask_) : "memory"); }
    private:
        std::uint32_t mask_;
    };
    class PatternAudioSource : public AudioStream
    {
    public:
        PatternAudioSource() : AudioStream(0,nullptr) {}
        void update() override
        {
            // MQS may schedule updates during static construction, before setup.
            // Do not configure, allocate or latch false failures until memory is ready.
            if (!g_audio_ready) return;
#ifdef BROTRACKER_PATTERN_PERF
            const std::uint32_t begin=ARM_DWT_CYCCNT;
            const bool first=!initialized_;
#endif
            if (!initialized_)
            {
                (void)g_control.InitializeAudio(kTempoHundredths,44100,g_active_pattern,g_selected_bindings);
                initialized_ = true;
            }
            audio_block_t* block = allocate();
            if (!block)
            {
                g_control.AudioBlock(nullptr,AUDIO_BLOCK_SAMPLES,false);
#ifdef BROTRACKER_PATTERN_PERF
                RecordCost(begin,first);
#endif
                return; // Fail-stop: no timeline advance and no stale block transmitted.
            }
            g_control.AudioBlock(block->data,AUDIO_BLOCK_SAMPLES,true);
            transmit(block);
            release(block);
#ifdef BROTRACKER_PATTERN_PERF
            RecordCost(begin,first);
#endif
        }
    private:
#ifdef BROTRACKER_PATTERN_PERF
        static void RecordCost(std::uint32_t begin, bool first) noexcept {
            // Budget calculation/counter publication follows the measured end.
            const auto end=ARM_DWT_CYCCNT;
            std::uint32_t budget=0;
            (void)PatternCycleBudget(F_CPU_ACTUAL,AUDIO_BLOCK_SAMPLES,AUDIO_SAMPLE_RATE_EXACT,budget);
            g_control.RecordPerformance(begin,end,first,budget);
        }
#endif
        bool initialized_ = false;
    };
    PatternAudioSource g_source;
    AudioOutputMQS g_mqs;
    AudioConnection g_to_mqs(g_source,0,g_mqs,0);
#if defined(AUDIO_INTERFACE)
    AudioOutputUSB g_usb;
    AudioConnection g_to_usb_left(g_source,0,g_usb,0);
    AudioConnection g_to_usb_right(g_source,0,g_usb,1);
#endif

    // Main-loop-owned TX FIFO. Never block on USB: only write available bytes,
    // at most 64 per service. RX/ack extraction pauses when this queue is full.
    constexpr std::size_t kTxCapacity = 16;
    static_assert(kTxCapacity >= kPatternDiscoveryReplyCount, "Discovery must fit TX FIFO");
    static_assert(kPatternGainLineCapacity <= kPatternSnapshotLineCapacity, "Gain RX must fit line storage");
    struct TxLine { char data[96]{}; std::size_t length = 0, offset = 0; };
    TxLine g_tx[kTxCapacity];
    std::size_t g_tx_head = 0, g_tx_count = 0;
    PatternTelemetrySender g_telemetry;
    PatternPublication g_publication, g_preview;
    PatternPreviewReadback g_working;
    RealtimePattern g_snapshot_source; // Frozen per readback, never audio-owned.
    PatternSnapshotSender g_pattern_transfer(g_snapshot_source, kTempoHundredths);
    PatternOutputMux g_output_mux(g_telemetry, g_pattern_transfer);
    bool g_protocol_connected = false;
    std::uint32_t g_last_position_ms = 0;
    bool QueueLine(const char* text)
    {
        if (g_tx_count == kTxCapacity) return false;
        auto& line = g_tx[(g_tx_head + g_tx_count) % kTxCapacity];
        line.length = std::strlen(text); // Only fixed literals/bounded snprintf results.
        if (line.length >= sizeof(line.data)) return false;
        std::memcpy(line.data,text,line.length); line.offset = 0;
        ++g_tx_count;
        return true;
    }
    PatternBringUpStatus Snapshot()
    { InterruptGuard guard; return g_control.Snapshot(); }
    void OfferPosition(const PatternBringUpStatus& status)
    {
        const auto& p = status.position;
        g_telemetry.Offer({status.running, p.valid, p.absolute_tick, p.pattern_row,
            p.loop_index, status.active_rows, status.active_channels,
            status.transport == PatternTransportState::Paused, 2});
    }
    void QueueGains(const PatternBringUpStatus& status) {
        char line[kPatternGainLineCapacity];
        if(FormatPatternGainState(status.gains,status.applied_gain_id,status.next_gain_id,line,sizeof(line)))
            (void)QueueLine(line);
    }
    void QueueStatus(bool advertise_snapshot)
    {
        const auto status = Snapshot();
        (void)QueueLine(status.fault != PatternFault::None ? "BTTEST1 STATE ERROR\n" :
            status.running ? "BTTEST1 STATE PLAYING\n" :
            status.transport == PatternTransportState::Paused ? "BTTEST1 STATE PAUSED\n" : "BTTEST1 STATE IDLE\n");
        g_protocol_connected = true;
        OfferPosition(status);
        if (advertise_snapshot) {
            (void)QueueLine("BTPATTERN1 SNAPCAP 2\n");
            (void)QueueLine("BTPATTERN1 SNAPCAP 1\n");
            (void)QueueLine("BTPATTERN1 TRANSPORTCAP 1\n");
            (void)QueueLine("BTPUBLISH1 CAP 1\n");
            (void)QueueLine("BTPUBLISH1 PAUSEDCAP 1\n");
        }
        if (advertise_snapshot) (void)QueueLine("BTPREVIEW1 CAP 1\n");
        if (advertise_snapshot) (void)QueueLine("BTGAIN1 CAP 1\n");
        QueueGains(status);
        char preview_discovery[96];
        if (FormatPreviewDiscovery(g_preview,preview_discovery,sizeof(preview_discovery))) (void)QueueLine(preview_discovery);
        if (FormatPreviewState(status.published_id,status.working_id,status.pending_preview_id,
            preview_discovery,sizeof(preview_discovery))) (void)QueueLine(preview_discovery);
        // Main-loop discovery; no audio mutation. STATUS refreshes discovery too.
        char discovery[64];
        if (g_publication.FormatDiscovery(discovery,sizeof(discovery))) (void)QueueLine(discovery);
    }
    void PublicationError(std::uint32_t id, PublicationStatus reason, bool preview=false)
    {
        char error[96];
        std::snprintf(error,sizeof(error),"%s ERROR 1 %" PRIu32 " %u\n", preview ? "BTPREVIEW1" : "BTPUBLISH1", id, unsigned(reason));
        (void)QueueLine(error);
    }
    const char* ReplacementRejection(PatternPlayerStatus status)
    {
        switch (status) {
        case PatternPlayerStatus::InvalidTransportState: return "playing-or-paused";
        case PatternPlayerStatus::NotConfigured: return "not-configured";
        case PatternPlayerStatus::MissingInstrument: return "missing-instrument";
        case PatternPlayerStatus::UnknownInstrument: return "unknown-instrument";
        case PatternPlayerStatus::UnsupportedPitch: return "unsupported-pitch";
        default: return "invalid-pattern";
        }
    }
    void FlushSerial()
    {
        const int available = Serial.availableForWrite();
        if (available <= 0) return;
#ifdef BROTRACKER_PATTERN_SD_PREPARE
        if(g_wav_inspection_report.Partial() || (g_wav_inspection_report.HasData() && !g_tx_count &&
            !g_telemetry.HasData() && !g_pattern_transfer.HasData() && !g_preparation_report.HasData()
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
            && !g_preparation_report3.HasData()
#endif
            )) {
            std::size_t remaining=0; const char* data=g_wav_inspection_report.Data(remaining);
            const auto count=std::min<std::size_t>(remaining,std::min(available,64));
            g_wav_inspection_report.Consume(Serial.write(reinterpret_cast<const std::uint8_t*>(data),count));
            return;
        }
        // Finish a partial diagnostic, otherwise ACKs and existing partial lines win.
        if(g_preparation_report.Partial() || (g_preparation_report.HasData() && !g_tx_count &&
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
            !g_preparation_report3.Partial() &&
#endif
            !g_telemetry.Partial() && !g_pattern_transfer.Partial())) {
            std::size_t remaining=0; const char* data=g_preparation_report.Data(remaining);
            const auto count=std::min<std::size_t>(remaining,std::min(available,64));
            g_preparation_report.Consume(Serial.write(reinterpret_cast<const std::uint8_t*>(data),count));
            return;
        }
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
        if(g_preparation_report3.Partial() || (g_preparation_report3.HasData() && !g_tx_count &&
            !g_telemetry.HasData() && !g_pattern_transfer.HasData())) {
            std::size_t remaining=0; const char* data=g_preparation_report3.Data(remaining);
            const auto count=std::min<std::size_t>(remaining,std::min(available,64));
            g_preparation_report3.Consume(Serial.write(reinterpret_cast<const std::uint8_t*>(data),count));
            return;
        }
#endif
#endif
        const auto lane = g_output_mux.Select(g_tx_count != 0,
            g_tx_count != 0 && g_tx[g_tx_head].offset != 0);
        if (lane == PatternOutputLane::None) return;
        // ACK priority and fair position/snapshot lines, never interleaved. No
        // transfer line enters the command FIFO or touches mutable player state.
        if (lane != PatternOutputLane::Command)
        {
            std::size_t count;
            const char* data = lane == PatternOutputLane::Position ?
                g_telemetry.Data(count) : g_pattern_transfer.Data(count);
            if (count > 64) count = 64;
            if (count > static_cast<std::size_t>(available)) count = available;
            if (count) g_output_mux.Consume(lane, Serial.write(
                reinterpret_cast<const std::uint8_t*>(data), count));
            return;
        }
        auto& line = g_tx[g_tx_head];
        std::size_t count = line.length - line.offset;
        if (count > 64) count = 64;
        if (count > static_cast<std::size_t>(available)) count = available;
        const auto written = Serial.write(reinterpret_cast<const std::uint8_t*>(line.data + line.offset),count);
        line.offset += written;
        if (line.offset == line.length) { g_tx_head = (g_tx_head + 1) % kTxCapacity; --g_tx_count; }
    }
    void ServiceSerial()
    {
        static char line[kPatternSnapshotLineCapacity]; static std::size_t used = 0;
        static bool overflow = false;
        static std::uint32_t reported_failures = 0;
        if (!Serial)
        {
            used = 0; overflow = false;
            g_tx_head = g_tx_count = 0;
#ifdef BROTRACKER_PATTERN_SD_PREPARE
            g_preparation_report.Reset();
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
            g_preparation_report3.Reset();
#endif
            g_wav_inspection_report.Reset();
#endif
            g_telemetry.Clear();
            g_pattern_transfer.Clear();
            g_publication.NewSession();
            g_preview.NewSession();
            g_protocol_connected = false;
            // Never queues START on reconnect. Already accepted requests retain
            // their order; HELLO reports applied state, not a reconnect restart.
            return;
        }
        for (std::size_t budget = 0; budget < kPatternRequestCapacity && g_tx_count < kTxCapacity; ++budget)
        {
            PatternAppliedRequest applied;
            bool ready;
            { InterruptGuard guard; ready = g_control.TakeApplied(applied); }
            if (!ready) break;
            if (applied.request == PatternRequest::Preview || applied.request == PatternRequest::PreviewApplied ||
                applied.request == PatternRequest::PreviewSuperseded) {
                char reply[96];
                const char* phase="REJECTED";
                if (applied.request == PatternRequest::Preview) {
                    const bool ok=applied.result==PatternPlayerStatus::Success;
                    (void)g_working.Accepted(g_preview,applied.publication_id,ok);
                    phase=ok ? "ACCEPTED" : "REJECTED";
                } else if (applied.request == PatternRequest::PreviewApplied) {
                    phase=g_working.Applied(applied.publication_id) ? "APPLIED" : "HANDOFFERROR";
                } else { g_working.Superseded(applied.publication_id); phase="SUPERSEDED"; }
                if (applied.result == PatternPlayerStatus::Success)
                    std::snprintf(reply,sizeof(reply),"BTPREVIEW1 %s 1 %" PRIu32 "\n",phase,applied.publication_id);
                else std::snprintf(reply,sizeof(reply),"BTPREVIEW1 REJECTED 1 %" PRIu32 " %s\n",
                    applied.publication_id,ReplacementRejection(applied.result));
                (void)QueueLine(reply);
            }
            else if (applied.request == PatternRequest::Gains) {
                char reply[64];
                std::snprintf(reply,sizeof(reply),"BTGAIN1 %s 1 %" PRIu32 "\n",
                    applied.result==PatternPlayerStatus::Success ? "APPLIED" : "REJECTED",applied.publication_id);
                (void)QueueLine(reply);
            }
            else if (applied.request == PatternRequest::Replace)
            {
                // Audio has finished reading frozen staging. Synchronize the
                // main-owned readback source OUTSIDE interrupt exclusion, BEFORE
                // APPLIED and before accepting GET/another publication.
                const bool synchronized = g_publication.Complete(applied.publication_id,
                    applied.result == PatternPlayerStatus::Success,g_active_pattern);
                char reply[96];
                if (synchronized && applied.result == PatternPlayerStatus::Success) {
                    g_working.Published(g_active_pattern,applied.publication_id);
                    std::snprintf(reply,sizeof(reply),"BTPUBLISH1 APPLIED 1 %" PRIu32 "\n",applied.publication_id);
                } else std::snprintf(reply,sizeof(reply),"BTPUBLISH1 REJECTED 1 %" PRIu32 " %s\n",
                    applied.publication_id,synchronized ? ReplacementRejection(applied.result) : "handoff-error");
                (void)QueueLine(reply);
            }
            else {
                (void)QueueLine(applied.result != PatternPlayerStatus::Success ? "BTTEST1 ERROR transport\n" :
                    applied.request == PatternRequest::Start ? "BTTEST1 STARTED\n" :
                    applied.request == PatternRequest::Pause ? "BTTEST1 PAUSED\n" :
                    applied.request == PatternRequest::Continue ? "BTTEST1 CONTINUED\n" : "BTTEST1 STOPPED\n");
            }
            // Replace unsent pre-command telemetry; partial older lines finish
            // BEFORE the ACK, so the host's pending-command barrier rejects them.
            if (g_protocol_connected) OfferPosition(Snapshot());
        }
        const auto status = Snapshot();
        const auto now = static_cast<std::uint32_t>(millis());
        g_pattern_transfer.Tick(now);
        // Preserve timeout notifications under TX backpressure (do not Tick/drop).
        if (g_tx_count < kTxCapacity && g_publication.Tick(now))
            PublicationError(g_publication.Identity(),PublicationStatus::Timeout);
        if (g_tx_count < kTxCapacity && g_preview.Tick(now))
            PublicationError(g_preview.Identity(),PublicationStatus::Timeout,true);
        if (g_protocol_connected && now - g_last_position_ms >= 50)
        {
            OfferPosition(status);
            g_last_position_ms = now;
        }
        if (status.failures != reported_failures && g_tx_count < kTxCapacity)
        {
            char error[96];
            // A historical fault recovered by explicit START is diagnostic text,
            // not a false ERROR state overriding the authoritative running status.
            std::snprintf(error,sizeof(error),"%s fault=%u code=%u alloc=%lu\n",
                status.fault == PatternFault::None ? "BTPATTERN1 RECOVERED" : "BTTEST1 ERROR pattern",
                static_cast<unsigned>(status.last_fault),static_cast<unsigned>(status.player_error),
                static_cast<unsigned long>(status.allocation_failures));
            if (QueueLine(error)) reported_failures = status.failures;
        }
        // Bounded RX and at most one complete protocol line per service. Queue
        // saturation backpressures RX; request overflow receives explicit ERROR.
        for (unsigned int budget = 0; budget < 128 && g_tx_count + kPatternDiscoveryReplyCount <= kTxCapacity; ++budget)
        {
            const int raw = Serial.read();
            if (raw < 0) break;
            const char c = static_cast<char>(raw);
            if (c != '\n')
            {
                if (c == '\0') overflow = true;
                else if (used + 1 < sizeof(line)) line[used++] = c;
                else overflow = true;
                continue;
            }
            if (used && line[used - 1] == '\r') --used;
            line[used] = '\0';
            if (overflow) {
                if (!std::strncmp(line,"BTGAIN1",7)) (void)QueueLine("BTGAIN1 ERROR 1 0 malformed\n");
                else if (!std::strncmp(line,"BTPUBLISH1",10) || !std::strncmp(line,"BTPREVIEW1",10)) {
                    const bool preview=!std::strncmp(line,"BTPREVIEW1",10);
                    (preview ? g_preview : g_publication).Cancel(); PublicationError(0,PublicationStatus::Malformed,preview);
                } else (void)QueueLine(!std::strncmp(line, "BTPATTERN1 GET", 14) ?
                    "BTPATTERN1 SNAPERR 1 0\n" : "BTTEST1 ERROR line-too-long\n");
            }
            else if (!std::strcmp(line,"BTTEST1 HELLO") || !std::strcmp(line,"BTTEST1 STATUS")) {
                if (!std::strcmp(line,"BTTEST1 HELLO")) { g_publication.NewSession(); g_preview.NewSession(); }
                QueueStatus(!std::strcmp(line,"BTTEST1 HELLO"));
#ifdef BROTRACKER_PATTERN_SD_PREPARE
                if(!std::strcmp(line,"BTTEST1 HELLO")) {
                    (void)g_preparation_report.Offer(g_preparation);
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
                    (void)g_preparation_report3.Offer(g_preparation3,kDevelopmentWavPaths[2]);
#endif
                    g_wav_inspection_report.Offer(g_wav_inspection);
                }
#endif
            }
            else if (!std::strncmp(line,"BTGAIN1",7)) {
                if(!std::strcmp(line,"BTGAIN1 GET 1")) QueueGains(Snapshot());
                else {
                    PatternGainMessage message;
                    PatternRequestStatus accepted=PatternRequestStatus::InvalidRequest;
                    const bool parsed=ParsePatternGains(line,used,message);
                    if(parsed && g_protocol_connected) {
                        InterruptGuard guard; accepted=g_control.SubmitGains(message.gains,message.id);
                    }
                    char reply[96];
                    if(accepted==PatternRequestStatus::Accepted)
                        std::snprintf(reply,sizeof(reply),"BTGAIN1 SUBMITTED 1 %" PRIu32 "\n",message.id);
                    else std::snprintf(reply,sizeof(reply),"BTGAIN1 ERROR 1 %" PRIu32 " %s\n",parsed?message.id:0,
                        !parsed ? "malformed" : !g_protocol_connected ? "not-connected" :
                        accepted==PatternRequestStatus::Full ? "queue-full" : accepted==PatternRequestStatus::Identity ? "identity" : "invalid-gain");
                    (void)QueueLine(reply);
                }
            }
            else if (!std::strncmp(line,"BTPREVIEW1 GET 1 ",17)) {
                std::uint32_t fields[2]{};
                const auto latest=Snapshot();
                const bool coherent=g_working.working_id==latest.working_id && g_working.published_id==latest.published_id;
                if (!SnapshotFields(line,used,"BTPREVIEW1 GET 1 ",fields,2) || !fields[0] || fields[1]>1 ||
                    !g_protocol_connected || !coherent || g_preview.Busy() || g_publication.Busy() || g_pattern_transfer.HasData() ||
                    (fields[1]==1 && g_working.pending_id!=latest.pending_preview_id) || (fields[1]==1 && !g_working.pending_id)) {
                    (void)QueueLine("BTPREVIEW1 READERR 1\n");
                } else {
                    g_snapshot_source=fields[1] ? g_working.pending : g_working.working;
                    (void)g_pattern_transfer.SetWorkingNamespace(true);
                    (void)g_pattern_transfer.Begin(fields[0],now,2);
                    char label[96];
                    std::snprintf(label,sizeof(label),"BTPREVIEW1 READ 1 %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
                        fields[0],fields[1],g_working.published_id,fields[1]?g_working.pending_id:g_working.working_id);
                    (void)QueueLine(label);
                }
            }
            else if (!std::strncmp(line,"BTPUBLISH1",10) || !std::strncmp(line,"BTPREVIEW1",10))
            {
                const bool preview=!std::strncmp(line,"BTPREVIEW1",10);
                auto& transfer=preview ? g_preview : g_publication;
                const auto& other=preview ? g_publication : g_preview;
                PublicationMessage message;
                if (!ParsePublication(line,used,message,preview)) {
                    transfer.Cancel(); PublicationError(0,PublicationStatus::Malformed,preview);
                } else if (!g_protocol_connected) PublicationError(message.fields[0],PublicationStatus::NotConnected,preview);
                else {
                    const auto result = transfer.Accept(message,now,g_pattern_transfer.HasData() || other.Busy());
                    if (result == PublicationStatus::Ready) {
                        (void)transfer.Freeze();
                        PatternRequestStatus accepted;
                        { InterruptGuard guard; accepted = preview ? g_control.SubmitPreview(*transfer.Frozen(),transfer.Identity()) : g_control.SubmitPattern(*transfer.Frozen(),transfer.Identity()); }
                        if (accepted != PatternRequestStatus::Accepted) {
                            transfer.Finish(); PublicationError(message.fields[0],PublicationStatus::QueueFull,preview);
                        } else {
                            char reply[64];
                            std::snprintf(reply,sizeof(reply),"%s SUBMITTED 1 %" PRIu32 "\n",preview?"BTPREVIEW1":"BTPUBLISH1",message.fields[0]);
                            (void)QueueLine(reply); // Not proof of application; never automatically retries.
                        }
                    } else if (result != PublicationStatus::Receiving) PublicationError(message.fields[0],result,preview);
                    else if (preview && message.kind == PublicationKind::Begin) {
                        char receipt[64];
                        std::snprintf(receipt,sizeof(receipt),"BTPREVIEW1 RECEIVING 1 %" PRIu32 "\n",message.fields[0]);
                        (void)QueueLine(receipt);
                    }
                }
            }
            else if (!std::strncmp(line, "BTPATTERN1 GET", 14))
            {
                std::uint32_t id = 0; unsigned version = 1;
                const bool parsed=ParseSnapshotRequest(line, used, id, version);
                if (parsed && !g_pattern_transfer.HasData() && !g_publication.Busy() && !g_preview.Busy()) {
                    g_snapshot_source=g_active_pattern;
                    (void)g_pattern_transfer.SetWorkingNamespace(false);
                }
                if (!parsed || !g_pattern_transfer.Begin(id, now, version,g_publication.Busy() || g_preview.Busy()))
                {
                    char error[64];
                    std::snprintf(error, sizeof(error), "BTPATTERN1 SNAPERR %u %" PRIu32 "\n", version, id);
                    (void)QueueLine(error); // Snapshot errors do not change transport.
                }
            }
            else if (!std::strcmp(line,"BTTEST1 START") || !std::strcmp(line,"BTTEST1 STOP") ||
                !std::strcmp(line,"BTTEST1 PAUSE") || !std::strcmp(line,"BTTEST1 CONTINUE"))
            {
                const auto request = !std::strcmp(line,"BTTEST1 START") ? PatternRequest::Start :
                    !std::strcmp(line,"BTTEST1 PAUSE") ? PatternRequest::Pause :
                    !std::strcmp(line,"BTTEST1 CONTINUE") ? PatternRequest::Continue : PatternRequest::Stop;
                PatternRequestStatus accepted;
                { InterruptGuard guard; accepted = g_control.Submit(request); }
                if (accepted != PatternRequestStatus::Accepted) (void)QueueLine("BTTEST1 ERROR request-overflow\n");
            }
            else (void)QueueLine("BTTEST1 ERROR command\n");
            used = 0; overflow = false;
            break;
        }
        FlushSerial();
#ifdef BROTRACKER_PATTERN_PERF
        static std::uint32_t last_performance_ms=0;
        if(g_protocol_connected && now-last_performance_ms>=1000) {
            last_performance_ms=now; // Skip this interval, never enqueue/backlog.
            if(!g_tx_count && !g_telemetry.Partial() && !g_pattern_transfer.HasData() && Serial.available()==0
#ifdef BROTRACKER_PATTERN_SD_PREPARE
                && !g_preparation_report.Partial() && !g_wav_inspection_report.Partial()
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
                && !g_preparation_report3.Partial()
#endif
#endif
                ) {
                char line[kPatternPerformanceLineCapacity]; std::size_t length=0;
                if(FormatPatternPerformance(status.performance,line,sizeof(line),length) &&
                    Serial.availableForWrite()>=static_cast<int>(length))
                    Serial.write(reinterpret_cast<const std::uint8_t*>(line),length);
            }
        }
#endif
    }
}
    void PatternBringUpPlatformInit()
    {
        Serial.begin(115200);
        pinMode(LED_BUILTIN,OUTPUT); digitalWrite(LED_BUILTIN,LOW);
        // Optional SD preparation is boot-only; no logging/time-sync initialization.
        // All globals are constructed; audio owner configures on its first update.
#ifdef BROTRACKER_PATTERN_PERF
        // Enable the supported DWT counter without resetting a shared core counter.
        ARM_DEMCR |= ARM_DEMCR_TRCENA;
        ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
#endif
#ifdef BROTRACKER_PATTERN_SD_PREPARE
        // Early callbacks return while g_audio_ready is false. No retries after activation.
        const bool sd_ready=SD.begin(BUILTIN_SDCARD);
        g_wav_inspection.InspectOnce(sd_ready);
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
        PrepareDevelopmentWavs(sd_ready,g_preparation,g_prepared_pcm,kPreparedFrameCapacity,
            g_preparation3,g_prepared_pcm3,kPreparedFrameCapacity);
#else
        g_preparation.Prepare(sd_ready,"Samples/test2.wav",g_prepared_pcm,kPreparedFrameCapacity);
#endif
#endif
#ifdef BROTRACKER_PATTERN_SD_PLAYBACK
        // Preparation failure leaves an invalid view and the working beeper fixture.
        const auto selected = SelectPatternBringUpFixture(kBindings,g_preparation.view,true);
        g_active_pattern = selected.pattern;
        g_selected_bindings = selected.bindings;
#endif
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
        // Eight-channel development fixture; pitched sequences need variable-rate mapping.
        // The existing prepared file is test2.wav (raw ID 3, displayed 04).
        const auto development=SelectDevelopmentFixture(kBindings,g_preparation.view,g_preparation3.view);
        g_active_pattern=development.pattern; g_selected_bindings=development.bindings;
#endif
        g_working.working=g_active_pattern;
        AudioMemory(8);
        { InterruptGuard guard; g_audio_ready = true; }
    }
    void PatternBringUpKernelInit() {}
    void PatternBringUpKernelRun() { ServiceSerial(); }
}
#endif
