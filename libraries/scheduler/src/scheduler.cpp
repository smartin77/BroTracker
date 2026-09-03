#include "scheduler.h"

namespace BroTracker
{
    bool Scheduler::Initialize()
    {
        position_ = 0;
        return true;
    }

    void Scheduler::Reset()
    {
        position_ = 0;
    }

    void Scheduler::AdvanceSamples(std::uint32_t sample_count)
    {
        position_ += sample_count;
    }

    std::uint64_t Scheduler::GetPosition() const
    {
        return position_;
    }
}
