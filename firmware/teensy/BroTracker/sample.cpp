#include "sample.h"

#include <cstddef>

namespace BroTracker
{
    void FreeSample(Sample& sample)
    {
        delete[] sample.data;
        sample.data = nullptr;
        sample.frame_count = 0;
    }
}
