#pragma once
#include "pattern_control.h"
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
#include "variable_rate_player_adapter.h"
#endif
namespace BroTracker {
// Build-time selection only. No secondary player, runtime dispatch or allocation.
#ifdef BROTRACKER_PATTERN_VARIABLE_RATE
using SelectedPatternBindings = VariableRateSampleBindings;
using SelectedPatternPlayer = VariableRateBringUpPlayer;
using SelectedPatternBringUpControl = BasicPatternBringUpControl<SelectedPatternPlayer, SelectedPatternBindings>;
constexpr std::size_t kSelectedPatternFrameCapacity = kVariableRatePatternFrameCapacity;
#else
using SelectedPatternBindings = NativeRateSampleBindings;
using SelectedPatternPlayer = NativeRatePatternPlayer;
using SelectedPatternBringUpControl = PatternBringUpControl;
constexpr std::size_t kSelectedPatternFrameCapacity = kNativeRatePatternFrameCapacity;
#endif
}
