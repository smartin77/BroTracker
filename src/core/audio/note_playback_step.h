#pragma once
#include "core/note.h"
#include "variable_rate_sample_voice.h"
#include "note_playback_steps.h"
namespace BroTracker {
enum class NotePlaybackStepStatus { Success, InvalidTargetNote, InvalidReferenceNote };
// Equal-tempered ratio 2^((target-reference)/12), quantized once to nearest
// positive Q32.32 (ties upward). Explicit supported pattern pitches 24..127 only;
// reject empty/OFF/other values before subtraction. Failure preserves output.
// Full interval range [-103,103], no octave restriction. Exact unity and all
// supported whole octaves are exact powers of two. Max absolute ratio error is
// < 2^-33 (half Q32 LSB); max relative error < 4.5e-8 (0.045 ppm) over this range.
// Checked-in constants generated via exact integer twelfth-root/halfway comparison
// by tools/generate_note_playback_steps.py. No runtime float, pow, allocation, I/O
// or clock. One validation and lookup; values fit positive uint64 without overflow.
// Reference pitch is caller metadata/playback anchor, never inferred from duration
// or BPM; existing beeper bindings are not a claim of physical tuning.
// Whole-note interval mapping only. Fractional-semitone tuning and source/output
// rate conversion are separate future composition layers; do not round detune to
// a note or silently apply either here. No playback integration in this primitive.
[[nodiscard]] inline NotePlaybackStepStatus NoteToPlaybackStep(unsigned target_note,
    unsigned reference_note,std::uint64_t& output) noexcept {
    static_assert(kPatternNoteMinimum==24 && kPatternNoteMaximum==127, "Regenerate note step range if pitch bounds change");
    static_assert(Detail::kNotePlaybackSteps[103]==kSourceFrameStepUnity, "Zero interval must be exact unity");
    if(!IsPitchedPatternNote(target_note)) return NotePlaybackStepStatus::InvalidTargetNote;
    if(!IsPitchedPatternNote(reference_note)) return NotePlaybackStepStatus::InvalidReferenceNote;
    const int interval=static_cast<int>(target_note)-static_cast<int>(reference_note);
    output=Detail::kNotePlaybackSteps[interval+103];
    return NotePlaybackStepStatus::Success;
}
}
