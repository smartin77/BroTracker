#pragma once
#include "note.h"

namespace BroTracker {
constexpr unsigned kSampleRootDefault=60, kSampleFineTuneMaximum=99;
struct SampleTuning {
    unsigned root=0, cents=0; // Root zero denotes absent metadata in protocol/UI.
    constexpr bool Valid() const noexcept {
        return IsPitchedPatternNote(root) && cents<=kSampleFineTuneMaximum;
    }
    constexpr bool operator==(SampleTuning other) const noexcept {
        return root==other.root && cents==other.cents;
    }
    constexpr bool operator!=(SampleTuning other) const noexcept { return !(*this==other); }
};
constexpr SampleTuning kDefaultSampleTuning{kSampleRootDefault,0};
constexpr SampleTuning ResolveSampleTuning(SampleTuning tuning) noexcept {
    return {tuning.root?tuning.root:kSampleRootDefault,tuning.cents};
}

// Rounded Q0.32 deltas, round((2^(cents/1200) - 1) * 2^32).
constexpr std::uint32_t kSampleFineTuneDeltas[]={
    0,2481587,4964608,7449063,9934954,12422282,14911046,17401249,19892890,22385971,
    24880492,27376455,29873860,32372708,34873000,37374736,39877918,42382546,44888621,47396144,
    49905116,52415538,54927410,57440733,59955509,62471738,64989420,67508558,70029150,72551200,
    75074706,77599670,80126094,82653977,85183320,87714125,90246393,92780123,95315318,97851977,
    100390102,102929693,105470752,108013279,110557275,113102741,115649677,118198085,120747966,123299320,
    125852148,128406451,130962230,133519486,136078219,138638431,141200121,143763292,146327944,148894078,
    151461695,154030795,156601379,159173449,161747004,164322047,166898578,169476597,172056106,174637105,
    177219596,179803578,182389054,184976024,187564488,190154448,192745904,195338857,197933309,200529260,
    203126711,205725662,208326115,210928071,213531530,216136494,218742962,221350936,223960418,226571407,
    229183904,231797911,234413429,237030457,239648998,242269052,244890619,247513702,250138300,252764414,
};
inline std::uint64_t ApplySampleFineTune(std::uint64_t step,unsigned cents) noexcept {
    const auto delta=kSampleFineTuneDeltas[cents]; // Caller validates the binding.
    const auto low=std::uint64_t(std::uint32_t(step))*delta+(std::uint64_t{1}<<31);
    return step+(step>>32)*delta+(low>>32);
}
}
