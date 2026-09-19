#pragma once

#include <Audio.h>

#include "sample.h"
#include "wav_loader.h"

namespace BroTracker
{
    // AudioStream sample player.
    //
    // Supports two independent playback modes:
    //
    //   - Legacy in-RAM playback via SetSample()/Play(). No looping or
    //     sample-rate conversion.
    //
    //   - SD-streamed playback via SetStream()/PlayStream(), which consumes
    //     PCM data from a bounded ring buffer filled by ServiceStreaming().
    //     All SD I/O stays in non-realtime stream setup, restart, and
    //     servicing operations; update() only reads already-buffered PCM
    //     data, never the SD card.
    //
    // If a stream is actively playing, it takes priority in update() over
    // the legacy in-RAM sample.
    class SamplePlayer : public AudioStream
    {
    public:
        SamplePlayer();

        virtual void update() override;

        // Sample must outlive the player, or be cleared via SetSample(nullptr)
        // before it is freed. Does not take ownership.
        void SetSample(const Sample* sample);

        // Restart playback from the first frame.
        void Play();

        bool IsPlaying() const { return playing_; }

        // Associates a WAV PCM stream (see OpenWavPcmStream()) with this
        // player. Takes ownership of the open file handle in `info`. Does
        // not start playback; call PlayStream() to arm it. May close the
        // previous file; call only from a non-realtime context.
        void SetStream(WavStreamInfo&& info);

        // Associates a WAV PCM stream with the next slot and arms it for
        // prebuffering. May close the previous next-slot file; call only
        // from a non-realtime context.
        void SetNextStream(WavStreamInfo&& info);

        // Restarts stream playback from the first PCM frame. Playback does
        // not begin until ServiceStreaming() has buffered an initial
        // refill chunk (head cache), followed by StartStream(). Seeks the
        // file; call only from a non-realtime context.
        void PlayStream();

        // Starts playback of an already primed stream.
        //
        // The stream must be in Primed state. This operation only changes the
        // playback state; it performs no SD I/O.
        void StartStream();

        bool IsStreamPlaying() const { return current_stream_.state == StreamState::Playing; }
        bool IsStreamPrimed() const { return current_stream_.state == StreamState::Primed; }
        bool IsNextStreamPrimed() const { return next_stream_.state == StreamState::Primed; }

        // Refills the streaming ring buffer from SD in bounded chunks.
        // Must be called periodically from a non-realtime context (e.g.
        // KernelRun()). Must never be called from update() or any other
        // realtime/audio context.
        void ServiceStreaming();

        // Number of audio blocks output with less than a full block of
        // buffered stream data available (excluding clean end-of-stream).
        std::uint32_t StreamUnderrunCount() const
        {
            return current_stream_.underrun_count;
        }

        // Diagnostic snapshot captured from the first streaming audio block
        // that contained buffered frames. Does not affect playback.
        struct StreamDiagnostics
        {
            bool captured = false;
            bool had_nonzero_sample = false;
            std::int16_t first_sample = 0;
            std::int16_t min_sample = 0;
            std::int16_t max_sample = 0;
        };

        StreamDiagnostics GetStreamDiagnostics() const { return current_stream_.diagnostics; }

    private:
        // Legacy in-RAM sample playback.
        const Sample* sample_ = nullptr;
        std::uint32_t position_ = 0;
        bool playing_ = false;

        // Streaming playback state.
        enum class StreamState : std::uint8_t
        {
            Idle,
            Priming,
            Primed,
            Playing,
            Finished,
        };

        // 8 KiB ring buffer, refilled in 4 KiB chunks, per the SD streaming
        // benchmark configuration (see docs/REFERENCE_SD_CARD.md).
        static constexpr std::uint32_t kRingBufferFrames = 4096;
        static constexpr std::uint32_t kRingBufferMask = kRingBufferFrames - 1;
        static constexpr std::uint32_t kRefillChunkFrames = 2048;

        struct StreamSlot
        {
            WavStreamInfo info;
            bool open = false;

            // During playback, servicing writes the write index and update()
            // writes the read index; each side only reads the other index.
            volatile StreamState state = StreamState::Idle;
            volatile std::uint32_t ring_write_index = 0;
            volatile std::uint32_t ring_read_index = 0;
            volatile bool eof = false;

            std::uint32_t frames_read = 0;
            std::uint32_t underrun_count = 0;

            StreamDiagnostics diagnostics;

            std::int16_t ring_buffer[kRingBufferFrames];
        };

        std::uint32_t ReadFrames(
            StreamSlot& slot,
            std::int16_t* dest,
            std::uint32_t frame_count);

        std::uint32_t ReadIntoRingBuffer(
            StreamSlot& slot,
            std::uint32_t frames_wanted);

        // Resets playback state without closing or replacing the owned file.
        void ResetSlot(StreamSlot& slot);
        // File release and servicing must run only in non-realtime contexts.
        void ReleaseSlot(StreamSlot& slot);
        void ServiceSlot(StreamSlot& slot);

        StreamSlot current_stream_;
        StreamSlot next_stream_;
    };
}

