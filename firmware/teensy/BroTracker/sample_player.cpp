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
        StreamSlot& slot = current_stream_;

        if (slot.open)
            ReleaseSlot(slot);

        slot.info = std::move(info);
        slot.open = static_cast<bool>(slot.info.file);
        ResetSlot(slot);
    }

    void SamplePlayer::SetNextStream(WavStreamInfo&& info)
    {
        StreamSlot& slot = next_stream_;

        if (slot.open)
            ReleaseSlot(slot);

        slot.info = std::move(info);
        slot.open = static_cast<bool>(slot.info.file);
        ResetSlot(slot);

        if (!slot.open)
            return;

        slot.info.file.seek(slot.info.data_chunk_offset);
        slot.state = StreamState::Priming;
    }

    void SamplePlayer::PlayStream()
    {
        StreamSlot& slot = current_stream_;

        if (!slot.open)
            return;

        slot.info.file.seek(slot.info.data_chunk_offset);
        ResetSlot(slot);
        slot.state = StreamState::Priming;
    }

    void SamplePlayer::StartStream()
    {
        StreamSlot& slot = current_stream_;

        if (!slot.open)
            return;

        if (slot.state == StreamState::Primed)
            slot.state = StreamState::Playing;
    }

    std::uint32_t SamplePlayer::ReadFrames(
        StreamSlot& slot,
        std::int16_t* dest,
        std::uint32_t frame_count)
    {
        if (frame_count == 0)
            return 0;

        const std::size_t bytes_wanted = static_cast<std::size_t>(frame_count) * sizeof(std::int16_t);
        const int bytes_read = slot.info.file.read(reinterpret_cast<std::uint8_t*>(dest), bytes_wanted);

        if (bytes_read <= 0)
            return 0;

        return static_cast<std::uint32_t>(bytes_read) / sizeof(std::int16_t);
    }

    std::uint32_t SamplePlayer::ReadIntoRingBuffer(
        StreamSlot& slot,
        std::uint32_t frames_wanted)
    {
        // Refill may wrap around the end of the ring buffer, so the read is
        // split into at most two contiguous SD reads.
        const std::uint32_t write_pos = slot.ring_write_index & kRingBufferMask;
        const std::uint32_t contiguous = kRingBufferFrames - write_pos;
        const std::uint32_t frames_first = frames_wanted < contiguous ? frames_wanted : contiguous;
        const std::uint32_t frames_second = frames_wanted - frames_first;

        std::uint32_t frames_read = ReadFrames(slot, &slot.ring_buffer[write_pos], frames_first);
        if (frames_read < frames_first)
        {
            slot.eof = true;
            return frames_read;
        }

        if (frames_second > 0)
        {
            const std::uint32_t second_read = ReadFrames(slot, &slot.ring_buffer[0], frames_second);
            frames_read += second_read;
            if (second_read < frames_second)
                slot.eof = true;
        }

        return frames_read;
    }

    void SamplePlayer::ResetSlot(StreamSlot& slot)
    {
        slot.state = StreamState::Idle;
        slot.ring_write_index = 0;
        slot.ring_read_index = 0;
        slot.frames_read = 0;
        slot.eof = false;
        slot.underrun_count = 0;
        slot.diagnostics = StreamDiagnostics{};
    }

    void SamplePlayer::ReleaseSlot(StreamSlot& slot)
    {
        if (slot.info.file)
            slot.info.file.close();

        slot.open = false;
        slot.state = StreamState::Idle;
    }

    void SamplePlayer::ServiceStreaming()
    {
        ServiceSlot(current_stream_);
        ServiceSlot(next_stream_);
    }

    void SamplePlayer::ServiceSlot(StreamSlot& slot)
    {
        if (!slot.open || slot.state == StreamState::Idle)
            return;

        if (slot.state == StreamState::Finished)
        {
            // File close/release is SD I/O and must happen here, never in
            // update(); this runs once because it clears slot.open.
            ReleaseSlot(slot);
            return;
        }

        if (!slot.eof)
        {
            const std::uint32_t buffered = slot.ring_write_index - slot.ring_read_index;
            const std::uint32_t free_space = kRingBufferFrames - buffered;

            if (free_space >= kRefillChunkFrames)
            {
                const std::uint32_t frames_remaining_in_file =
                    slot.info.frame_count - slot.frames_read;
                const std::uint32_t frames_to_read = frames_remaining_in_file < kRefillChunkFrames
                    ? frames_remaining_in_file : kRefillChunkFrames;

                if (frames_to_read == 0)
                {
                    slot.eof = true;
                }
                else
                {
                    const std::uint32_t frames_read = ReadIntoRingBuffer(slot, frames_to_read);
                    slot.frames_read += frames_read;
                    slot.ring_write_index += frames_read;

                    if (slot.frames_read >= slot.info.frame_count)
                        slot.eof = true;
                }
            }
        }

        if (slot.state == StreamState::Priming)
        {
            const std::uint32_t buffered = slot.ring_write_index - slot.ring_read_index;

            if (buffered >= kRefillChunkFrames || slot.eof)
                slot.state = StreamState::Primed;
        }
    }

    void SamplePlayer::update()
    {
        StreamSlot& slot = current_stream_;

        audio_block_t* block = allocate();
        if (block == nullptr)
            return;

        if (slot.state == StreamState::Playing)
        {
            const std::uint32_t available = slot.ring_write_index - slot.ring_read_index;
            const std::uint32_t frames_to_copy = available < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES)
                ? available : static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES);

            std::uint32_t i = 0;
            for (; i < frames_to_copy; ++i, ++slot.ring_read_index)
                block->data[i] = slot.ring_buffer[slot.ring_read_index & kRingBufferMask];

            for (; i < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES); ++i)
                block->data[i] = 0;

            // One-shot diagnostic: inspect (never modify) the first block
            // that actually contains buffered frames.
            if (!slot.diagnostics.captured && frames_to_copy > 0)
            {
                std::int16_t min_sample = block->data[0];
                std::int16_t max_sample = block->data[0];
                bool had_nonzero = false;

                for (std::uint32_t j = 0; j < frames_to_copy; ++j)
                {
                    const std::int16_t sample_value = block->data[j];
                    if (sample_value != 0)
                        had_nonzero = true;
                    if (sample_value < min_sample)
                        min_sample = sample_value;
                    if (sample_value > max_sample)
                        max_sample = sample_value;
                }

                slot.diagnostics.first_sample = block->data[0];
                slot.diagnostics.min_sample = min_sample;
                slot.diagnostics.max_sample = max_sample;
                slot.diagnostics.had_nonzero_sample = had_nonzero;
                slot.diagnostics.captured = true;
            }

            if (frames_to_copy < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES))
            {
                // Clean end of stream vs. SD not keeping up: only the latter
                // is an underrun.
                if (slot.eof && available == frames_to_copy)
                    slot.state = StreamState::Finished;
                else
                    ++slot.underrun_count;
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
