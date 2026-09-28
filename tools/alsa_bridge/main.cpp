// Temporary standalone ArkOS USB capture -> speaker playback experiment.
// Not part of the tracker UI, firmware, or final audio architecture.
#include <alsa/asoundlib.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <stdexcept>

namespace
{
constexpr unsigned int kRate = 44100;
constexpr unsigned int kChannels = 2;
constexpr unsigned int kFrameBytes = 4; // Two S16_LE samples; copy bytes unchanged.
constexpr snd_pcm_uframes_t kQueueFrames = 8192;
constexpr snd_pcm_uframes_t kChunkFrames = 256;
volatile std::sig_atomic_t stopped = 0;
void OnSignal(int) { stopped = 1; }

void Check(int result, const char* operation)
{
    if (result >= 0) return;
    std::fprintf(stderr, "%s: %s (%d)\n", operation, snd_strerror(result), result);
    if (result == -EBUSY)
        std::fprintf(stderr, "Device busy: release it manually (e.g. EmulationStation). "
                             "This tool does not stop other applications.\n");
    throw std::runtime_error(operation);
}

struct Pcm
{
    snd_pcm_t* handle = nullptr;
    ~Pcm()
    {
        if (handle)
        {
            snd_pcm_drop(handle); // Do not wait for queued audio on Ctrl+C.
            snd_pcm_close(handle);
        }
    }
    void Open(const char* device, snd_pcm_stream_t direction)
    {
        const char* label = direction == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture";
        std::fprintf(stderr, "Opening %s device: %s\n", label, device);
        Check(snd_pcm_open(&handle, device, direction, SND_PCM_NONBLOCK), label);
        snd_pcm_hw_params_t* hw;
        snd_pcm_hw_params_alloca(&hw);
        Check(snd_pcm_hw_params_any(handle, hw), "hw_params_any");
        Check(snd_pcm_hw_params_set_access(handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED), "interleaved access");
        Check(snd_pcm_hw_params_set_format(handle, hw, SND_PCM_FORMAT_S16_LE), "S16_LE format");
        Check(snd_pcm_hw_params_set_channels(handle, hw, kChannels), "stereo channels");
        // Reject a different application rate rather than silently changing pitch.
        Check(snd_pcm_hw_params_set_rate(handle, hw, kRate, 0), "44100 Hz rate");
        snd_pcm_uframes_t period = kChunkFrames, buffer = 2048;
        int dir = 0;
        Check(snd_pcm_hw_params_set_period_size_near(handle, hw, &period, &dir), "period size");
        Check(snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buffer), "buffer size");
        Check(snd_pcm_hw_params(handle, hw), "apply hardware parameters");
        Check(snd_pcm_hw_params_current(handle, hw), "read negotiated parameters");
        Check(snd_pcm_hw_params_get_period_size(hw, &period, &dir), "read period size");
        Check(snd_pcm_hw_params_get_buffer_size(hw, &buffer), "read buffer size");
        unsigned int rate = 0, channels = 0;
        snd_pcm_format_t format;
        Check(snd_pcm_hw_params_get_rate(hw, &rate, &dir), "read rate");
        Check(snd_pcm_hw_params_get_channels(hw, &channels), "read channels");
        Check(snd_pcm_hw_params_get_format(hw, &format), "read format");
        if (rate != kRate || channels != kChannels || format != SND_PCM_FORMAT_S16_LE || buffer < 2)
            throw std::runtime_error("unexpected negotiated format/buffer");
        snd_pcm_sw_params_t* sw;
        snd_pcm_sw_params_alloca(&sw);
        Check(snd_pcm_sw_params_current(handle, sw), "sw_params_current");
        Check(snd_pcm_sw_params_set_avail_min(handle, sw, 1), "avail_min");
        const auto threshold = direction == SND_PCM_STREAM_PLAYBACK ? buffer / 2 : 1;
        Check(snd_pcm_sw_params_set_start_threshold(handle, sw, threshold), "start threshold");
        Check(snd_pcm_sw_params_set_stop_threshold(handle, sw, buffer), "stop threshold");
        Check(snd_pcm_sw_params(handle, sw), "apply software parameters");
        Check(snd_pcm_prepare(handle), "prepare");
        std::fprintf(stderr,
            "%s: %s, %u channels, %u Hz; period=%lu frames, buffer=%lu frames, "
            "start_threshold=%lu frames\n",
            label, snd_pcm_format_name(format), channels, rate,
            static_cast<unsigned long>(period), static_cast<unsigned long>(buffer),
            static_cast<unsigned long>(threshold));
        // Includes the slave setup for plug devices: negotiated application
        // parameters alone need not describe the physical hardware format.
        snd_output_t* output = nullptr;
        Check(snd_output_stdio_attach(&output, stderr, 0), "attach ALSA dump");
        snd_pcm_dump(handle, output);
        snd_output_close(output);
    }
};

struct Bridge
{
    Pcm capture, playback;
    std::array<std::uint8_t, kQueueFrames * kFrameBytes> samples{};
    snd_pcm_uframes_t head = 0, queued = 0, high_water = 0;
    unsigned long long captured = 0, played = 0, discarded = 0;
    unsigned int overruns = 0, underruns = 0, suspends = 0;

    ~Bridge()
    {
        std::fprintf(stderr,
            "Summary: captured=%llu frames, submitted=%llu frames, "
            "capture_overruns=%u, playback_underruns=%u, suspends=%u, "
            "queue_high_water=%lu/%lu frames, queue_discarded=%llu frames, "
            "queue_remaining=%lu frames\n",
            captured, played, overruns, underruns, suspends,
            static_cast<unsigned long>(high_water), static_cast<unsigned long>(kQueueFrames),
            discarded, static_cast<unsigned long>(queued));
    }

    bool Retry(snd_pcm_sframes_t result, bool reading)
    {
        if (result >= 0 || result == -EAGAIN || result == -EINTR) return false;
        if (result != -EPIPE && result != -ESTRPIPE)
            Check(static_cast<int>(result), reading ? "capture read" : "playback write");
        // Reset the pair together after a discontinuity. No stale queued PCM
        // is replayed, and no blocking resume/recovery loop delays Ctrl+C.
        const bool capture_xrun = (reading && result == -EPIPE) ||
            snd_pcm_state(capture.handle) == SND_PCM_STATE_XRUN;
        const bool playback_xrun = (!reading && result == -EPIPE) ||
            snd_pcm_state(playback.handle) == SND_PCM_STATE_XRUN;
        overruns += capture_xrun;
        underruns += playback_xrun;
        suspends += result == -ESTRPIPE;
        std::fprintf(stderr,
            "Discontinuity: %s %s; capture_overruns=%u playback_underruns=%u "
            "suspends=%u; discarding %lu queued frames and resetting both PCMs\n",
            reading ? "capture" : "playback", snd_strerror(static_cast<int>(result)),
            overruns, underruns, suspends, static_cast<unsigned long>(queued));
        Check(snd_pcm_drop(capture.handle), "drop capture");
        Check(snd_pcm_drop(playback.handle), "drop playback");
        discarded += queued;
        queued = head = 0;
        Check(snd_pcm_prepare(capture.handle), "recover capture");
        Check(snd_pcm_prepare(playback.handle), "recover playback");
        if (!stopped) Check(snd_pcm_start(capture.handle), "restart capture");
        return true;
    }

    void Run()
    {
        Check(snd_pcm_start(capture.handle), "start capture");
        while (!stopped)
        {
            bool progress = false;
            if (queued < kQueueFrames)
            {
                const auto tail = (head + queued) % kQueueFrames;
                const auto frames = std::min({kChunkFrames, kQueueFrames - queued, kQueueFrames - tail});
                const auto result = snd_pcm_readi(capture.handle, samples.data() + tail * kFrameBytes, frames);
                if (Retry(result, true)) continue;
                if (result > 0)
                {
                    queued += result;
                    captured += result;
                    high_water = std::max(high_water, queued);
                    progress = true;
                }
            }
            if (stopped) break;
            if (queued > 0)
            {
                const auto frames = std::min({kChunkFrames, queued, kQueueFrames - head});
                const auto result = snd_pcm_writei(playback.handle, samples.data() + head * kFrameBytes, frames);
                if (Retry(result, false)) continue;
                if (result > 0)
                {
                    head = (head + result) % kQueueFrames;
                    queued -= result;
                    played += result;
                    progress = true;
                }
            }
            // Nonblocking ALSA + bounded idle wait keeps signals responsive.
            // A full queue applies backpressure; hardware loss is reported as
            // a capture overrun on the subsequent read, never hidden by growth.
            if (!progress) poll(nullptr, 0, 1);
        }
    }
};
}

int main(int argc, char** argv)
{
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc != 3 || (argc > 1 && std::strcmp(argv[1], "--help") == 0))
    {
        std::fprintf(stderr, "Usage: %s CAPTURE_DEVICE PLAYBACK_DEVICE\n"
            "Temporary stereo S16_LE 44100 Hz bridge. Use ALSA names, preferably CARD IDs.\n"
            "Ctrl+C stops without draining. Devices are never selected implicitly.\n", argv[0]);
        return argc == 2 && std::strcmp(argv[1], "--help") == 0 ? 0 : 2;
    }
    struct sigaction action{};
    action.sa_handler = OnSignal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
    {
        std::perror("sigaction");
        return 1;
    }
    try
    {
        Bridge bridge;
        // Report a busy output before taking ownership of the USB capture PCM.
        bridge.playback.Open(argv[2], SND_PCM_STREAM_PLAYBACK);
        if (!stopped) bridge.capture.Open(argv[1], SND_PCM_STREAM_CAPTURE);
        std::fprintf(stderr, "Queue capacity: %lu frames (%lu bytes). "
            "Buffer settings are NOT measured end-to-end latency.\n",
            static_cast<unsigned long>(kQueueFrames),
            static_cast<unsigned long>(kQueueFrames * kFrameBytes));
        if (!stopped) bridge.Run();
        std::fprintf(stderr, "Signal received; closing audio devices.\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Bridge failed: %s\n", error.what());
        return 1;
    }
}
