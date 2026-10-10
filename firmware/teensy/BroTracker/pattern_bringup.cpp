#ifdef BROTRACKER_PATTERN_BRINGUP
#include "pattern_bringup.h"
#include <Arduino.h>
#include <Audio.h>
#include <musical_tick_cursor.h> // PlatformIO discovers the existing scheduler library.
#include "bringup/pattern_control.h"
#include "bringup/pattern_telemetry.h"
#include "bringup/pattern_snapshot.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace BroTracker
{
namespace
{
    static_assert(AUDIO_BLOCK_SAMPLES == kNativeRatePatternFrameCapacity,
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
    const RealtimePattern kPattern = [] {
        RealtimePattern pattern;
        pattern.active_rows = 16; pattern.active_channels = 2;
        pattern.cells[0][0] = {60,0};
        pattern.cells[2][1] = {62,1};
        pattern.cells[4][0] = {60,kNoInstrumentUpdate};
        pattern.cells[5][0] = {60,kNoInstrumentUpdate};
        pattern.cells[6][1] = {NOTE_OFF,kNoInstrumentUpdate};
        pattern.cells[8][0] = {60,0}; pattern.cells[8][1] = {62,1};
        pattern.cells[10][0] = {NOTE_OFF,kNoInstrumentUpdate};
        pattern.cells[12][1] = {62,kNoInstrumentUpdate};
        pattern.cells[13][1] = {62,kNoInstrumentUpdate};
        pattern.cells[14][0] = {NOTE_OFF,kNoInstrumentUpdate};
        pattern.cells[14][1] = {NOTE_OFF,kNoInstrumentUpdate};
        return pattern;
    }();
    const NativeRateSampleBindings kBindings = [] {
        NativeRateSampleBindings bindings{}; bindings.count = 2;
        bindings.bindings[0] = {0,60,{kSampleA.data(),kSampleA.size(),44100}};
        bindings.bindings[1] = {1,62,{kSampleB.data(),kSampleB.size(),44100}};
        return bindings;
    }();
    PatternBringUpControl g_control; // Includes the statically allocated core player/scratch.
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
            if (!initialized_)
            {
                (void)g_control.InitializeAudio(kTempoHundredths,44100,kPattern,kBindings);
                initialized_ = true;
            }
            audio_block_t* block = allocate();
            if (!block)
            {
                g_control.AudioBlock(nullptr,AUDIO_BLOCK_SAMPLES,false);
                return; // Fail-stop: no timeline advance and no stale block transmitted.
            }
            g_control.AudioBlock(block->data,AUDIO_BLOCK_SAMPLES,true);
            transmit(block);
            release(block);
        }
    private:
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
    struct TxLine { char data[96]{}; std::size_t length = 0, offset = 0; };
    TxLine g_tx[kTxCapacity];
    std::size_t g_tx_head = 0, g_tx_count = 0;
    PatternTelemetrySender g_telemetry;
    PatternSnapshotSender g_pattern_transfer(kPattern, kTempoHundredths);
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
    void QueueStatus(bool advertise_snapshot)
    {
        const auto status = Snapshot();
        (void)QueueLine(status.fault != PatternFault::None ? "BTTEST1 STATE ERROR\n" :
            status.running ? "BTTEST1 STATE PLAYING\n" :
            status.transport == PatternTransportState::Paused ? "BTTEST1 STATE PAUSED\n" : "BTTEST1 STATE IDLE\n");
        g_protocol_connected = true;
        OfferPosition(status);
        if (advertise_snapshot) {
            (void)QueueLine("BTPATTERN1 SNAPCAP 1\n");
            (void)QueueLine("BTPATTERN1 TRANSPORTCAP 1\n");
        }
    }
    void FlushSerial()
    {
        const int available = Serial.availableForWrite();
        if (available <= 0) return;
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
        static char line[64]; static std::size_t used = 0;
        static bool overflow = false;
        static std::uint32_t reported_failures = 0;
        if (!Serial)
        {
            used = 0; overflow = false;
            g_tx_head = g_tx_count = 0;
            g_telemetry.Clear();
            g_pattern_transfer.Clear();
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
            (void)QueueLine(applied.result != PatternPlayerStatus::Success ? "BTTEST1 ERROR transport\n" :
                applied.request == PatternRequest::Start ? "BTTEST1 STARTED\n" :
                applied.request == PatternRequest::Pause ? "BTTEST1 PAUSED\n" :
                applied.request == PatternRequest::Continue ? "BTTEST1 CONTINUED\n" : "BTTEST1 STOPPED\n");
            // Replace unsent pre-command telemetry; partial older lines finish
            // BEFORE the ACK, so the host's pending-command barrier rejects them.
            if (g_protocol_connected) OfferPosition(Snapshot());
        }
        const auto status = Snapshot();
        const auto now = static_cast<std::uint32_t>(millis());
        g_pattern_transfer.Tick(now);
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
        for (unsigned int budget = 0; budget < 128 && g_tx_count + 3 <= kTxCapacity; ++budget)
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
            if (overflow) (void)QueueLine(!std::strncmp(line, "BTPATTERN1 GET", 14) ?
                "BTPATTERN1 SNAPERR 1 0\n" : "BTTEST1 ERROR line-too-long\n");
            else if (!std::strcmp(line,"BTTEST1 HELLO") || !std::strcmp(line,"BTTEST1 STATUS"))
                QueueStatus(!std::strcmp(line,"BTTEST1 HELLO"));
            else if (!std::strncmp(line, "BTPATTERN1 GET", 14))
            {
                std::uint32_t id = 0;
                if (!ParseSnapshotRequest(line, used, id) || !g_pattern_transfer.Begin(id, now))
                {
                    char error[64];
                    std::snprintf(error, sizeof(error), "BTPATTERN1 SNAPERR 1 %" PRIu32 "\n", id);
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
    }
}
    void PatternBringUpPlatformInit()
    {
        Serial.begin(115200);
        pinMode(LED_BUILTIN,OUTPUT); digitalWrite(LED_BUILTIN,LOW);
        // No SD/log/time-sync initialization in this RAM-only opt-in path.
        // All globals are constructed; audio owner configures on its first update.
        AudioMemory(8);
        { InterruptGuard guard; g_audio_ready = true; }
    }
    void PatternBringUpKernelInit() {}
    void PatternBringUpKernelRun() { ServiceSerial(); }
}
#endif
