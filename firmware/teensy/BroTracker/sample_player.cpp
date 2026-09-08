#include "sample_player.h"

#include <utility>

namespace BroTracker
{
    SamplePlayer::SamplePlayer()
        : AudioStream(0, nullptr)
    {
    }

    void SamplePlayer::SetSample(const Sample* sample)
    {
        sample_ = sample;
        position_ = 0;
        playing_ = false;
    }

    void SamplePlayer::Play()
    {
        position_ = 0;
        playing_ = sample_ != nullptr && sample_->IsValid();
    }

    void SamplePlayer::SetStream(WavStreamInfo&& info)
    {
        stream_info_ = std::move(info);
        stream_open_ = static_cast<bool>(stream_info_.file);
        stream_state_ = StreamState::Idle;
        ring_write_index_ = 0;
        ring_read_index_ = 0;
        stream_frames_read_ = 0;
        stream_eof_ = false;
        stream_underrun_count_ = 0;
    }

    void SamplePlayer::PlayStream()
    {
        if (!stream_open_)
            return;

        stream_info_.file.seek(stream_info_.data_chunk_offset);
        ring_write_index_ = 0;
        ring_read_index_ = 0;
        stream_frames_read_ = 0;
        stream_eof_ = false;
        stream_underrun_count_ = 0;
        stream_state_ = StreamState::Priming;
    }

    std::uint32_t SamplePlayer::ReadFrames(std::int16_t* dest, std::uint32_t frame_count)
    {
        if (frame_count == 0)
            return 0;

        const std::size_t bytes_wanted = static_cast<std::size_t>(frame_count) * sizeof(std::int16_t);
        const int bytes_read = stream_info_.file.read(reinterpret_cast<std::uint8_t*>(dest), bytes_wanted);
        if (bytes_read <= 0)
            return 0;

        return static_cast<std::uint32_t>(bytes_read) / sizeof(std::int16_t);
    }

    std::uint32_t SamplePlayer::ReadIntoRingBuffer(std::uint32_t frames_wanted)
    {
        // Refill may wrap around the end of the ring buffer, so the read is
        // split into at most two contiguous SD reads.
        const std::uint32_t write_pos = ring_write_index_ & kRingBufferMask;
        const std::uint32_t contiguous = kRingBufferFrames - write_pos;
        const std::uint32_t frames_first = frames_wanted < contiguous ? frames_wanted : contiguous;
        const std::uint32_t frames_second = frames_wanted - frames_first;

        std::uint32_t frames_read = ReadFrames(&ring_buffer_[write_pos], frames_first);
        if (frames_read < frames_first)
        {
            stream_eof_ = true;
            return frames_read;
        }

        if (frames_second > 0)
        {
            const std::uint32_t second_read = ReadFrames(&ring_buffer_[0], frames_second);
            frames_read += second_read;
            if (second_read < frames_second)
                stream_eof_ = true;
        }

        return frames_read;
    }

    void SamplePlayer::ServiceStreaming()
    {
        if (!stream_open_ || stream_state_ == StreamState::Idle || stream_state_ == StreamState::Finished)
            return;

        if (!stream_eof_)
        {
            const std::uint32_t buffered = ring_write_index_ - ring_read_index_;
            const std::uint32_t free_space = kRingBufferFrames - buffered;

            if (free_space >= kRefillChunkFrames)
            {
                const std::uint32_t frames_remaining_in_file =
                    stream_info_.frame_count - stream_frames_read_;
                const std::uint32_t frames_to_read = frames_remaining_in_file < kRefillChunkFrames
                    ? frames_remaining_in_file : kRefillChunkFrames;

                if (frames_to_read == 0)
                {
                    stream_eof_ = true;
                }
                else
                {
                    const std::uint32_t frames_read = ReadIntoRingBuffer(frames_to_read);
                    stream_frames_read_ += frames_read;
                    ring_write_index_ += frames_read;

                    if (stream_frames_read_ >= stream_info_.frame_count)
                        stream_eof_ = true;
                }
            }
        }

        if (stream_state_ == StreamState::Priming)
        {
            const std::uint32_t buffered = ring_write_index_ - ring_read_index_;
            if (buffered >= kRefillChunkFrames || stream_eof_)
                stream_state_ = StreamState::Playing;
        }
    }

    void SamplePlayer::update()
    {
        audio_block_t* block = allocate();
        if (block == nullptr)
            return;

        if (stream_state_ == StreamState::Playing)
        {
            const std::uint32_t available = ring_write_index_ - ring_read_index_;
            const std::uint32_t frames_to_copy = available < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES)
                ? available : static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES);

            std::uint32_t i = 0;
            for (; i < frames_to_copy; ++i, ++ring_read_index_)
                block->data[i] = ring_buffer_[ring_read_index_ & kRingBufferMask];

            for (; i < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES); ++i)
                block->data[i] = 0;

            if (frames_to_copy < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES))
            {
                // Clean end of stream vs. SD not keeping up: only the latter
                // is an underrun.
                if (stream_eof_ && available == frames_to_copy)
                    stream_state_ = StreamState::Finished;
                else
                    ++stream_underrun_count_;
            }
        }
        else if (playing_ && sample_ != nullptr)
        {
            int i = 0;
            for (; i < AUDIO_BLOCK_SAMPLES && position_ < sample_->frame_count; ++i, ++position_)
                block->data[i] = sample_->data[position_];

            for (; i < AUDIO_BLOCK_SAMPLES; ++i)
                block->data[i] = 0;

            if (position_ >= sample_->frame_count)
                playing_ = false;
        }
        else
        {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
                block->data[i] = 0;
        }

        transmit(block);
        release(block);
    }
}
