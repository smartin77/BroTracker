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
    //   - Legacy in-RAM playback via SetSample()/Play(), retained for the
    //     current platform.cpp wiring. No looping, no sample-rate
    //     conversion.
    //
    //   - SD-streamed playback via SetStream()/PlayStream(), which consumes
    //     PCM data from a bounded ring buffer filled by ServiceStreaming().
    //     All SD I/O happens in ServiceStreaming(), called from a
    //     non-realtime context; update() only ever reads already-buffered
    //     PCM data, never the SD card.
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
        // not start playback; call PlayStream() to arm it.
        void SetStream(WavStreamInfo&& info);

        // Restarts stream playback from the first PCM frame. Playback does
        // not begin until ServiceStreaming() has buffered an initial
        // refill chunk (head cache).
        void PlayStream();

        bool IsStreamPlaying() const { return stream_state_ == StreamState::Playing; }

        // Refills the streaming ring buffer from SD in bounded chunks.
        // Must be called periodically from a non-realtime context (e.g.
        // KernelRun()). Must never be called from update() or any other
        // realtime/audio context.
        void ServiceStreaming();

        // Number of audio blocks output with less than a full block of
        // buffered stream data available (excluding clean end-of-stream).
        std::uint32_t StreamUnderrunCount() const { return stream_underrun_count_; }

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
            Playing,
            Finished,
        };

        // 8 KiB ring buffer, refilled in 4 KiB chunks, per the SD streaming
        // benchmark configuration (see docs/REFERENCE_SD_CARD.md).
        static constexpr std::uint32_t kRingBufferFrames = 4096;
        static constexpr std::uint32_t kRingBufferMask = kRingBufferFrames - 1;
        static constexpr std::uint32_t kRefillChunkFrames = 2048;

        std::uint32_t ReadFrames(std::int16_t* dest, std::uint32_t frame_count);
        std::uint32_t ReadIntoRingBuffer(std::uint32_t frames_wanted);

        WavStreamInfo stream_info_;
        bool stream_open_ = false;

        // ring_write_index_ is written only by ServiceStreaming(); ring_read_index_
        // is written only by update(). Each side only reads the other's index,
        // making this a single-producer/single-consumer ring buffer without
        // needing an atomic type.
        volatile StreamState stream_state_ = StreamState::Idle;
        volatile std::uint32_t ring_write_index_ = 0;
        volatile std::uint32_t ring_read_index_ = 0;
        volatile bool stream_eof_ = false;

        std::uint32_t stream_frames_read_ = 0;
        std::uint32_t stream_underrun_count_ = 0;

        std::int16_t ring_buffer_[kRingBufferFrames];
    };
}

