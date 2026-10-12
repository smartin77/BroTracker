#include "musical_timing.h"

#include <limits>

namespace BroTracker
{
    TickToSampleStatus TickToSamplePosition(
        std::uint64_t tick_index,
        std::uint32_t tempo_hundredths,
        std::uint32_t sample_rate_hz,
        std::uint64_t& sample_position) noexcept
    {
        if (tempo_hundredths == 0 || sample_rate_hz == 0)
            return TickToSampleStatus::InvalidConfiguration;

        constexpr std::uint64_t kSecondsPerMinuteTimesTempoScale = 60 * 100;
        const std::uint64_t numerator =
            std::uint64_t{sample_rate_hz} * kSecondsPerMinuteTimesTempoScale;
        const std::uint64_t denominator =
            std::uint64_t{tempo_hundredths} * kTicksPerQuarterNote;
        constexpr auto kMaximumPosition = std::numeric_limits<std::uint64_t>::max();

        // Process the tick bits from most to least significant. For each prefix p,
        // maintain p * numerator = quotient * denominator + remainder. This
        // avoids materializing the potentially overflowing full tick product.
        // With uint32_t configuration, numerator < 2^45 and denominator < 2^41;
        // 2 * remainder + numerator fits uint64_t because remainder < denominator.
        std::uint64_t quotient = 0;
        std::uint64_t remainder = 0;
        for (unsigned int bit = 64; bit != 0; --bit)
        {
            const std::uint64_t next = remainder * 2 +
                (((tick_index >> (bit - 1)) & 1u) ? numerator : 0);
            const std::uint64_t carry = next / denominator;
            if (quotient > (kMaximumPosition - carry) / 2)
                return TickToSampleStatus::Overflow;
            quotient = quotient * 2 + carry;
            remainder = next % denominator;
        }

        sample_position = quotient;
        return TickToSampleStatus::Success;
    }
}
