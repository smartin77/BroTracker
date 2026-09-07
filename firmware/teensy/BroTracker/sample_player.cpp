#include "sample_player.h"

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

    void SamplePlayer::update()
    {
        audio_block_t* block = allocate();
        if (block == nullptr)
            return;

        if (playing_ && sample_ != nullptr)
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
