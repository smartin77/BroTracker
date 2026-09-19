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
        slot.playback_rate_q16 = kPlaybackRateOne;
    }

    bool SamplePlayer::SetStreamPlaybackRate(float rate)
    {
        StreamSlot& slot = current_stream_;

        if (!slot.open ||
            (slot.state != StreamState::Idle && slot.state != StreamState::Primed) ||
            !(rate > 0.0f) || rate > kMaximumPlaybackRate)
        {
            return false;
        }

        const float scaled_rate = rate * static_cast<float>(kPlaybackRateOne);
        const std::uint32_t rate_q16 = static_cast<std::uint32_t>(scaled_rate + 0.5f);

        if (rate_q16 == 0 || rate_q16 > 2u * kPlaybackRateOne)
            return false;

        slot.playback_rate_q16 = rate_q16;
        return true;
    }

    void SamplePlayer::SetNextStream(WavStreamInfo&& info)
    {
        StreamSlot& slot = next_stream_;

        if (slot.open)
            ReleaseSlot(slot);

        slot.info = std::move(info);
        slot.open = static_cast<bool>(slot.info.file);
        ResetSlot(slot);
        slot.playback_rate_q16 = kPlaybackRateOne;

        if (!slot.open)
            return;

        slot.info.file.seek(slot.info.data_chunk_offset);
        slot.state = StreamState::Priming;
    }

    bool SamplePlayer::PromoteNextStream()
    {
        if (!next_stream_.open || next_stream_.state != StreamState::Primed)
            return false;

        // Publish Idle first so update() cannot consume the current slot
        // while its file and buffered state are being replaced.
        current_stream_.state = StreamState::Idle;
        ReleaseSlot(current_stream_);

        current_stream_.info = std::move(next_stream_.info);
        current_stream_.open = next_stream_.open;
        current_stream_.ring_write_index = next_stream_.ring_write_index;
        current_stream_.ring_read_index = next_stream_.ring_read_index;
        current_stream_.eof = next_stream_.eof;
        current_stream_.frames_read = next_stream_.frames_read;
        current_stream_.underrun_count = next_stream_.underrun_count;
        current_stream_.playback_rate_q16 = next_stream_.playback_rate_q16;
        current_stream_.source_fraction_q16 = next_stream_.source_fraction_q16;
        current_stream_.diagnostics = next_stream_.diagnostics;

        for (std::uint32_t i = 0; i < kRingBufferFrames; ++i)
            current_stream_.ring_buffer[i] = next_stream_.ring_buffer[i];

        next_stream_.open = false;
        ResetSlot(next_stream_);
        next_stream_.playback_rate_q16 = kPlaybackRateOne;

        // State is published last. StartStream() can now enter Playing
        // without another seek, refill, or priming cycle.
        current_stream_.state = StreamState::Primed;
        return true;
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
        slot.source_fraction_q16 = 0;
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
            std::uint32_t frames_generated = 0;
            bool reached_eof = false;

            while (frames_generated < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES))
            {
                const std::uint32_t available =
                    slot.ring_write_index - slot.ring_read_index;

                if (available == 0)
                {
                    reached_eof = slot.eof;
                    break;
                }

                const std::uint32_t next_source_position =
                    slot.source_fraction_q16 + slot.playback_rate_q16;
                const std::uint32_t source_frames_to_advance =
                    next_source_position >> kPlaybackRateFractionBits;

                // Until EOF is known, keep every source frame required for
                // this output sample in the ring. A refill can then resume
                // without losing the fractional source position.
                if (!slot.eof && source_frames_to_advance > available)
                    break;

                block->data[frames_generated] =
                    slot.ring_buffer[slot.ring_read_index & kRingBufferMask];
                ++frames_generated;

                slot.source_fraction_q16 =
                    next_source_position & kPlaybackRateFractionMask;

                if (slot.eof && source_frames_to_advance >= available)
                {
                    // The current sample is valid, but the requested advance
                    // reaches or passes the final buffered source frame.
                    slot.ring_read_index += available;
                    reached_eof = true;
                    break;
                }

                slot.ring_read_index += source_frames_to_advance;
            }

            for (std::uint32_t i = frames_generated;
                 i < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES);
                 ++i)
            {
                block->data[i] = 0;
            }

            // One-shot diagnostic: inspect (never modify) the first block
            // that actually contains buffered frames.
            if (!slot.diagnostics.captured && frames_generated > 0)
            {
                std::int16_t min_sample = block->data[0];
                std::int16_t max_sample = block->data[0];
                bool had_nonzero = false;

                for (std::uint32_t j = 0; j < frames_generated; ++j)
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

            if (reached_eof)
            {
                slot.state = StreamState::Finished;
            }
            else if (frames_generated < static_cast<std::uint32_t>(AUDIO_BLOCK_SAMPLES))
            {
                // Count an underrun only when a complete output block could
                // not be generated and the stream did not end cleanly.
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
