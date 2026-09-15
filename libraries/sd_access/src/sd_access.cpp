#include "sd_access.h"

namespace BroTracker
{
namespace SdAccess
{
namespace
{
    // Single shared ownership state for the whole SD device (D0046).
    // Not thread-safe by design: BroTracker's SD access is confined to
    // non-realtime, non-interrupt execution contexts (never the audio
    // callback), so plain counters are sufficient to make the exclusivity
    // rule mechanically enforced instead of relying on convention.
    unsigned int g_reader_count = 0;
    bool g_writer_active = false;
}

    bool AcquireRead()
    {
        if (g_writer_active)
            return false;

        ++g_reader_count;
        return true;
    }

    void ReleaseRead()
    {
        if (g_reader_count > 0)
            --g_reader_count;
    }

    bool AcquireWrite()
    {
        if (g_writer_active || g_reader_count > 0)
            return false;

        g_writer_active = true;
        return true;
    }

    void ReleaseWrite()
    {
        g_writer_active = false;
    }
}
}
