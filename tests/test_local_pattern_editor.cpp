#include "test_framework.h"
#include "ui/local_pattern_editor.h"
#include "ui/editor_repeat.h"
#include "ui/editor_step_input.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "ui/instrument_formatter.h"
#include "ui/handheld_control_hints.h"
#include <cstring>

namespace {
std::optional<LivePlaybackView> ConnectedDraft() {
    LivePlaybackView live;
    live.telemetry_available = true;
    live.position.rows = 16; live.position.channels = 2;
    BroTracker::DevicePatternSnapshot snapshot;
    snapshot.transfer_id = 1; snapshot.tempo_hundredths = 12753;
    snapshot.pattern.active_channels = 2;
    snapshot.pattern.cells[0][0].note = 60;
    snapshot.pattern.cells[0][0].instrument = 1;
    live.device_pattern = snapshot;
    return live;
}
}
TEST_CASE(BpmPendingIndicationChangesOnlyNumberColorAndClearsWithExistingState) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    auto live = ConnectedDraft();
    live->tempo_hundredths = 12505; // Keep hidden hundredths and nearest-tenth formatting.
    RenderMainScreen(fb, tune, preview, live);
    const Framebuffer white = fb;
    Framebuffer expected = white;
    // Existing fixed-cell centering: label at 242, five-character number at 266.
    DrawFixedText(expected, 266, 9, "125.1", Color{255, 160, 64});
    CHECK(std::memcmp(white.PixelData(), expected.PixelData(), 640 * 26 * sizeof(Color)) != 0);

    for (int indication = 0; indication < 2; ++indication) {
        live->tempo_pending = true;
        RenderMainScreen(fb, tune, preview, live);
        // Includes label, spacing and trailing area: no asterisk, no recentering,
        // and no other header text changes color.
        CHECK(std::memcmp(fb.PixelData(), expected.PixelData(), 640 * 26 * sizeof(Color)) == 0);
        RenderMainScreen(fb, tune, preview, live);
        CHECK(std::memcmp(fb.PixelData(), expected.PixelData(), 640 * 26 * sizeof(Color)) == 0);
        live->tempo_pending = false; // Existing indication expiry; no new UI timer.
        RenderMainScreen(fb, tune, preview, live);
        CHECK(std::memcmp(fb.PixelData(), white.PixelData(), 640 * 26 * sizeof(Color)) == 0);
    }
    live = ConnectedDraft(); // Synchronization/reconnect without pending indication.
    live->tempo_hundredths = 12505;
    RenderMainScreen(fb, tune, preview, live);
    CHECK(std::memcmp(fb.PixelData(), white.PixelData(), 640 * 26 * sizeof(Color)) == 0);
}
TEST_CASE(LocalEditorNavigationAndRawFields) {
    LocalPatternEditor editor;
    CHECK(!editor.Apply(EditorAction::Increment));
    auto live = ConnectedDraft(); editor.Sync(live);
    CHECK(editor.Ready()); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Up)); CHECK(editor.Cursor().header); CHECK(!editor.Apply(EditorAction::Left));
    CHECK(editor.Apply(EditorAction::Down)); CHECK(!editor.Cursor().header);
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Dirty());
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK(!editor.Dirty());
    CHECK(!editor.Apply(EditorAction::SetValue, 128));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 60);
    CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_OFF);
    CHECK(editor.Apply(EditorAction::Clear));
    CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_EMPTY);
    CHECK(editor.Apply(EditorAction::Right));
    CHECK(!editor.Apply(EditorAction::NoteOff));
    CHECK(!editor.Apply(EditorAction::SetValue, 255));
    CHECK(!editor.Apply(EditorAction::SetValue, 254));
    CHECK(editor.Apply(EditorAction::SetValue, 98));
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 98);
    CHECK(editor.Apply(EditorAction::Clear));
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 255);
    CHECK(editor.Apply(EditorAction::SetValue, 7)); // instrument-only cell
    CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_EMPTY);
    CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK_EQ(editor.Cursor().field, EditField::Instrument);
    CHECK(editor.Apply(EditorAction::Right)); CHECK_EQ(editor.Cursor().channel, 1);
    CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::NextChannel));
    CHECK_EQ(editor.Cursor().channel, 0); CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::PreviousChannel));
    for (unsigned i = 0; i < 15; ++i) CHECK(editor.Apply(EditorAction::Down));
    CHECK(!editor.Apply(EditorAction::Down));
    CHECK(!editor.Apply(static_cast<EditorAction>(999)));
    CHECK_EQ(editor.Cursor().row, 15); CHECK_EQ(editor.Cursor().channel, 1);
}
TEST_CASE(LocalEditorConnectionTelemetryAndDisplayIndependence) {
    LocalPatternEditor editor;
    auto live = ConnectedDraft();
    auto incomplete = live; incomplete->device_pattern.reset();
    editor.Sync(incomplete); CHECK(!editor.Ready());
    auto mismatch = live; mismatch->position.channels = 3;
    editor.Sync(mismatch); CHECK(!editor.Ready());
    auto invalid = live; invalid->device_pattern->pattern.cells[0][0].note = 128;
    editor.Sync(invalid); CHECK(!editor.Ready());
    editor.Sync(live);
    CHECK(editor.Apply(EditorAction::SetValue, 72));
    CHECK(editor.Apply(EditorAction::Down));
    live->fresh = true; live->position.running = true; live->position.valid = true;
    live->position.tick = 96 * 8; live->position.row = 8;
    editor.Sync(live);
    CHECK_EQ(editor.Cursor().row, 1); CHECK_EQ(editor.Draft()->cells[0][0].note, 72);
    CHECK_EQ(live->device_pattern->pattern.cells[0][0].note, 60);
    const auto display = ResolvePatternDisplay(live);
    CHECK(display.playback_highlight); CHECK_EQ(display.row, 8);
    CHECK(display.row != editor.Cursor().row);
    live->device_pattern->pattern.cells[0][0].note = 90;
    editor.Sync(live); CHECK_EQ(editor.Draft()->cells[0][0].note, 72);
    editor.Sync(std::nullopt); CHECK(!editor.Ready()); CHECK(!editor.Dirty());
    CHECK(editor.Draft() == nullptr); CHECK(!editor.Apply(EditorAction::Restore));
    editor.Sync(incomplete); CHECK(!editor.Ready());
    editor.Sync(live); CHECK_EQ(editor.Draft()->cells[0][0].note, 90);
    CHECK_EQ(editor.Cursor().row, 1); CHECK(!editor.Dirty());
}
TEST_CASE(LocalEditorFramebufferUsesDraftAndSeparateFieldCursor) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    auto live = ConnectedDraft();
    live->fresh = true; live->position.running = true; live->position.valid = true;
    live->position.tick = 96 * 8; live->position.row = 8;
    LocalPatternEditor editor; editor.Sync(live);
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    RenderMainScreen(fb, tune, preview, live, &editor);
    CHECK_EQ(fb.PixelData()[62 * 640 + 36].r, 255); // Orange note-field border.
    CHECK_EQ(fb.PixelData()[(62 + 8 * 13) * 640 + 3].g, 30); // Neutral grey playback row.
    const std::vector<Color> before(fb.PixelData(), fb.PixelData() + 640 * 480);
    CHECK(editor.Apply(EditorAction::SetValue, 72));
    RenderMainScreen(fb, tune, preview, live, &editor);
    bool changed = false;
    for (unsigned y = 64; y < 73; ++y)
        for (unsigned x = 38; x < 56; ++x)
            changed |= std::memcmp(&before[y * 640 + x], &fb.PixelData()[y * 640 + x], sizeof(Color)) != 0;
    CHECK(changed); // Draft glyph, not only dirty status, changed.
    CHECK_EQ(live->device_pattern->pattern.cells[0][0].note, 60);
    CHECK(editor.Apply(EditorAction::Right));
    RenderMainScreen(fb, tune, preview, live, &editor);
    CHECK_EQ(fb.PixelData()[62 * 640 + 60].r, 255); // Instrument-field border.
    live->position.tick = 96 * 9; live->position.row = 9;
    editor.Sync(live);
    RenderMainScreen(fb, tune, preview, live, &editor);
    CHECK_EQ(fb.PixelData()[62 * 640 + 60].r, 255); // Cursor stays at row zero.
    CHECK_EQ(fb.PixelData()[(62 + 9 * 13) * 640 + 3].g, 30);
    CHECK_EQ(editor.Draft()->cells[0][0].note, 72);
}
TEST_CASE(LocalEditorNoteInitializationOctaveAndLimits) {
    LocalPatternEditor editor; editor.Sync(ConnectedDraft());
    CHECK(!editor.Apply(EditorAction::SetValue, 23));
    CHECK(editor.Apply(EditorAction::Clear));
    CHECK(editor.Apply(EditorAction::Increment));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 60);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK_EQ(editor.Draft()->cells[0][0].note, 59);
    CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK(editor.Apply(EditorAction::Decrement));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 127);
    CHECK(editor.Apply(EditorAction::SetValue,24));
    for (unsigned i = 0; i < 12; ++i) CHECK(editor.Apply(EditorAction::Increment));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 36);
    CHECK(editor.Apply(EditorAction::SetValue, 127));
    CHECK(editor.Apply(EditorAction::Increment)); CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_OFF);
    CHECK(editor.Apply(EditorAction::Increment)); CHECK_EQ(editor.Draft()->cells[0][0].note, 24);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(editor.Apply(EditorAction::Right));
    CHECK_EQ(editor.Cursor().channel, 1); CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::Left)); CHECK_EQ(editor.Cursor().channel, 0);
    CHECK(editor.Apply(EditorAction::Left)); CHECK(!editor.Apply(EditorAction::Left));
    CHECK_EQ(editor.Cursor().channel, 0);
    CHECK(editor.Apply(EditorAction::PreviousChannel)); CHECK_EQ(editor.Cursor().channel, 1);
    CHECK(editor.Apply(EditorAction::NextChannel)); CHECK_EQ(editor.Cursor().channel, 0);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(editor.Apply(EditorAction::Clear));
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK_EQ(editor.Draft()->cells[0][0].instrument, 0);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK_EQ(editor.Draft()->cells[0][0].instrument,255);
    CHECK(!editor.Apply(EditorAction::SetValue,254));
}
TEST_CASE(LocalEditorHorizontalEdgesAndTabAlwaysSelectsNote) {
    LocalPatternEditor editor; editor.Sync(ConnectedDraft());
    CHECK(editor.Apply(EditorAction::Down));
    CHECK(!editor.Apply(EditorAction::Left));
    CHECK(editor.Apply(EditorAction::Right)); CHECK_EQ(editor.Cursor().field, EditField::Instrument);
    CHECK(editor.Apply(EditorAction::Right)); CHECK_EQ(editor.Cursor().channel, 1);
    CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(!editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::NextChannel)); CHECK_EQ(editor.Cursor().channel, 0);
    CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(editor.Apply(EditorAction::PreviousChannel));
    CHECK_EQ(editor.Cursor().channel, 1); CHECK_EQ(editor.Cursor().field, EditField::Note);
    CHECK(editor.Apply(EditorAction::Left)); CHECK_EQ(editor.Cursor().channel, 0);
    CHECK_EQ(editor.Cursor().field, EditField::Instrument); CHECK_EQ(editor.Cursor().row, 1);
}
TEST_CASE(LocalEditorOffTimingIsSeparateTransactionalMetadata) {
    auto live = ConnectedDraft();
    live->device_pattern->pattern.cells[0][0] = {NOTE_OFF, 7};
    LocalPatternEditor editor; editor.Sync(live);
    CHECK(!editor.Dirty()); CHECK(!editor.OffTiming(16, 0)); CHECK(!editor.OffTiming(0, 2));
    CHECK(editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::Decrement));
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::EndOfPosition);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Dirty());
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::EndOfPosition);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 7);
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(!editor.Apply(EditorAction::SetValue, 12));
    CHECK(!editor.Apply(EditorAction::NoteOff));
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::EndOfPosition);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Apply(EditorAction::Clear)); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::Arrival);
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Apply(EditorAction::Left));
    CHECK(editor.Apply(EditorAction::Increment)); // OFF note -> C-0, not timing -> instrument.
    CHECK_EQ(editor.Draft()->cells[0][0].note, 24); CHECK_EQ(editor.Draft()->cells[0][0].instrument, 7);
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::Arrival);
    CHECK(editor.Apply(EditorAction::Clear)); CHECK(editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::Increment)); // Instrument-only update.
    CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_EMPTY); CHECK_EQ(editor.Draft()->cells[0][0].instrument, 0);
    CHECK(editor.Apply(EditorAction::Left)); CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::Arrival);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 0);
    CHECK_EQ(live->device_pattern->pattern.cells[0][0].instrument, 7);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(editor.Apply(EditorAction::Increment));
    editor.Sync(std::nullopt); CHECK(!editor.OffTiming(0, 0));
    editor.Sync(live); CHECK(!editor.Dirty()); CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::Arrival);
}
TEST_CASE(LocalEditorRendersBothOffTimingGlyphs) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    auto live = ConnectedDraft(); live->device_pattern->pattern.cells[0][0] = {NOTE_OFF, 7};
    LocalPatternEditor editor; editor.Sync(live); CHECK(editor.Apply(EditorAction::Right));
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    RenderMainScreen(fb, tune, preview, live, &editor);
    const std::vector<Color> arrival(fb.PixelData(), fb.PixelData() + 640 * 480);
    CHECK(editor.Apply(EditorAction::Increment)); RenderMainScreen(fb, tune, preview, live, &editor);
    bool changed = false;
    for (unsigned y = 64; y < 73; ++y) for (unsigned x = 62; x < 68; ++x)
        changed |= std::memcmp(&arrival[y * 640 + x], &fb.PixelData()[y * 640 + x], sizeof(Color)) != 0;
    CHECK(changed); CHECK_EQ(editor.Draft()->cells[0][0].instrument, 7);
    CHECK(editor.Apply(EditorAction::Decrement)); RenderMainScreen(fb, tune, preview, live, &editor);
    CHECK(std::memcmp(arrival.data(), fb.PixelData(), arrival.size() * sizeof(Color)) == 0);
}
TEST_CASE(EditorRepeatTimingAccelerationStallsAndCancellation) {
    LocalPatternEditor editor; editor.Sync(ConnectedDraft());
    EditorRepeat repeat;
    CHECK(repeat.Begin(EditorAction::Increment, 0, editor));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 61);
    CHECK(!repeat.Tick(399, true, editor)); CHECK(repeat.Tick(400, true, editor));
    CHECK(!repeat.Tick(499, true, editor)); CHECK(repeat.Tick(500, true, editor));
    CHECK(repeat.Tick(900, true, editor)); // Only one action despite missed intervals.
    CHECK(!repeat.Tick(999, true, editor)); CHECK(repeat.Tick(1000, true, editor));
    CHECK(!repeat.Tick(1049, true, editor)); CHECK(repeat.Tick(1050, true, editor));
    CHECK(repeat.Tick(1950, true, editor)); CHECK(!repeat.Tick(1999, true, editor));
    CHECK(repeat.Tick(2000, true, editor)); CHECK(!repeat.Tick(2024, true, editor));
    CHECK(repeat.Tick(2025, true, editor));
    const auto previous = editor.Draft()->cells[0][0].note;
    CHECK(repeat.Tick(5000, true, editor));
    CHECK_EQ(editor.Draft()->cells[0][0].note, previous + 1);
    CHECK(!repeat.Tick(5000, true, editor)); CHECK(!repeat.Tick(5001, true, editor));
    CHECK(repeat.Begin(EditorAction::Decrement, 5010, editor));
    repeat.Release(EditorAction::Increment); // Old key release cannot cancel opposite key.
    CHECK(!repeat.Tick(5409, true, editor)); CHECK(repeat.Tick(5410, true, editor));
    repeat.Release(EditorAction::Decrement); CHECK(!repeat.Tick(6000, true, editor));
    CHECK(repeat.Begin(EditorAction::Increment, 6100, editor));
    CHECK(!repeat.Tick(6500, false, editor)); CHECK(!repeat.Tick(7000, true, editor));
    CHECK(repeat.Begin(EditorAction::Increment, UINT32_MAX - 100, editor));
    CHECK(!repeat.Tick(298, true, editor)); CHECK(repeat.Tick(299, true, editor));
    editor.Sync(std::nullopt); CHECK(!repeat.Tick(700, true, editor));
    editor.Sync(ConnectedDraft()); CHECK(!repeat.Tick(900, true, editor));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 60);
}
TEST_CASE(EditorRepeatRenewsPauseAtBothPitchedEndpoints) {
    for(const auto action : {EditorAction::Increment,EditorAction::Decrement}) {
        LocalPatternEditor editor; editor.Sync(ConnectedDraft()); EditorRepeat repeat;
        const unsigned endpoint=action==EditorAction::Increment?127:24;
        const unsigned before=action==EditorAction::Increment?126:25;
        const unsigned wrapped=action==EditorAction::Increment?24:127;
        CHECK(editor.Apply(EditorAction::SetValue,before)); CHECK(repeat.Begin(action,0,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,endpoint);
        CHECK(!repeat.Tick(399,true,editor)); CHECK(repeat.Tick(400,true,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_OFF);
        CHECK(!repeat.Tick(799,true,editor)); CHECK(repeat.Tick(800,true,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,wrapped);
        CHECK(repeat.Tick(900,true,editor));
        CHECK(editor.Apply(EditorAction::SetValue,before));
        CHECK(repeat.Tick(2100,true,editor)); CHECK_EQ(editor.Draft()->cells[0][0].note,endpoint);
        CHECK(!repeat.Tick(2499,true,editor)); CHECK(repeat.Tick(2500,true,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_OFF);
        CHECK(!repeat.Tick(2899,true,editor)); CHECK(repeat.Tick(2900,true,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,wrapped);
        repeat.Release(action); CHECK(editor.Apply(EditorAction::SetValue,endpoint));
        CHECK(repeat.Begin(action,3000,editor)); CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_OFF);
        CHECK(!repeat.Tick(3399,true,editor)); CHECK(repeat.Tick(3400,true,editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note,wrapped);
    }
}

TEST_CASE(PatternInformationStaysInsidePaddedRightPanel) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    auto live = ConnectedDraft(); LocalPatternEditor editor; editor.Sync(live);
    // Status-only changes must never paint the pattern, headers or navigation.
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        auto before = live; auto after = live;
        const LocalPatternEditor* draft = nullptr;
        if (scenario == 0) {
            before->fresh = after->fresh = true;
            before->position.running = true; after->position.paused = true;
        } else if (scenario == 1) {
            before->fresh = true; after->fresh = false;
        } else if (scenario == 2) {
            before->device_pattern.reset(); after->device_pattern.reset();
            before->pattern_loading = true; after->pattern_loading = false;
        } else if (scenario == 3) {
            draft = &editor; // Read-only versus local-draft / NOT SENT label.
        } else {
            before->fresh = after->fresh = true;
            before->position.valid = after->position.valid = true;
            after->position.loop = UINT64_MAX; // Longest loop label must wrap safely.
        }
        RenderMainScreen(fb, tune, preview, before);
        const std::vector<Color> original(fb.PixelData(), fb.PixelData() + 640 * 480);
        RenderMainScreen(fb, tune, preview, after, draft);
        unsigned changed = 0;
        for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x) {
            if (std::memcmp(&original[y * 640 + x], &fb.PixelData()[y * 640 + x], sizeof(Color)) == 0) continue;
            // Draft also adds the field cursor, intentionally preserved.
            if (scenario == 3 && x < 454 && y >= 62 && y < 75) continue;
            CHECK(x >= 466 && x < 631); CHECK(y >= 38 && y < 442); ++changed;
        }
        CHECK(changed > 0);
        // Former information-label rows contain only padded empty cells; row numbers
        // remain outside this cell-area check.
        for (unsigned y = 290; y < 340; ++y) for (unsigned x = 30; x < 453; ++x) {
            const auto& pixel = fb.PixelData()[y * 640 + x];
            CHECK(!(pixel.r == 255 && pixel.g == 255 && pixel.b == 255));
            CHECK(!(pixel.r == 130 && pixel.g == 130 && pixel.b == 196));
        }
    }
}
TEST_CASE(WrappedPanelTextClipsAndSplitsLongWords) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(40, 40); fb.Clear(Color{0, 0, 0});
    CHECK_EQ(DrawWrappedFixedText(fb, 8, 8, 13, 15,
        "LONGWORD MORE WORDS", Color{255, 255, 255}), 15);
    unsigned painted = 0;
    for (unsigned y = 0; y < 40; ++y) for (unsigned x = 0; x < 40; ++x) {
        if (fb.PixelData()[y * 40 + x].r == 0) continue;
        CHECK(x >= 8 && x < 21); CHECK(y >= 8 && y < 23); ++painted;
    }
    CHECK(painted > 0);
}

TEST_CASE(HostConnectionControlsAndPendingMessagesStayInPanel) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    for (const auto& live : {std::optional<LivePlaybackView>{}, ConnectedDraft()}) {
        RenderMainScreen(fb, tune, preview, live);
        const std::vector<Color> baseline(fb.PixelData(), fb.PixelData() + 640 * 480);
        for (const auto* status : {"Waiting for Teensy", "Connected - IDLE", "Connected - PLAYING",
            "Connected - PAUSED", "ERROR: transport rejected"}) {
            for (const auto* hints : {"Space: START/PAUSE/CONTINUE | Enter: STOP | Ctrl+X: EXIT",
                "STOP pending; please wait", "STOP pending, then EXIT",
                kArkOSEditorHints, kArkOSLegacyEditorHints}) {
                RenderMainScreen(fb, tune, preview, live, nullptr, HostPanelInformation{status, hints});
                unsigned changed = 0;
                for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x) {
                    if (std::memcmp(&baseline[y * 640 + x], &fb.PixelData()[y * 640 + x], sizeof(Color)) == 0) continue;
                    CHECK(x >= 466 && x < 631); CHECK(y >= 38 && y < 442); ++changed;
                }
                CHECK(changed > 0);
                // Both supplied strings are drawn, including wrapped hints.
                unsigned hint_pixels = 0;
                for (unsigned y = 58; y < 94; ++y) for (unsigned x = 466; x < 631; ++x)
                    hint_pixels += fb.PixelData()[y * 640 + x].r == 255;
                CHECK(hint_pixels > 0);
            }
        }
    }
}
TEST_CASE(DecimalOneBasedRowHeaderDoesNotDependOnDeviceData) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    RenderPreviewScreen(fb, tune, preview);
    const std::vector<Color> baseline(fb.PixelData(), fb.PixelData() + 640 * 480);
    auto live = ConnectedDraft();
    for (unsigned available = 0; available < 2; ++available) {
        if (!available) live->device_pattern.reset(); else live = ConnectedDraft();
        RenderMainScreen(fb, tune, preview, live);
        for (unsigned y = 38; y < 46; ++y) for (unsigned x = 2; x < 28; ++x)
            CHECK(std::memcmp(&baseline[y * 640 + x], &fb.PixelData()[y * 640 + x], sizeof(Color)) == 0);
    }
}

TEST_CASE(LiveGreyRowAndFontArrowFollowReportedPositionOnly) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    auto live = ConnectedDraft();
    live->fresh = true; live->position.running = true; live->position.valid = true;
    live->position.tick = 12 * 96; live->position.row = 12;
    LocalPatternEditor editor; editor.Sync(live);
    Framebuffer fb(640, 480), expected(640, 480); Tune tune; Pattern preview;
    // Compare the marker bitmap with the same actual font glyph used in preview.
    expected.Clear(Color{30, 30, 30});
    DrawFixedText(expected, 23, 220, "\xC2\xA6", Color{192, 192, 192});
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto view = live;
        if (mode == 1) { view->position.running = false; view->position.paused = true; view->position.version = 2; }
        if (mode == 2) view->fresh = false;
        if (mode == 3) view->position.valid = false;
        if (mode == 4) view->position.running = false;
        if (mode == 5) view->telemetry_available = false;
        RenderMainScreen(fb, tune, preview, view, &editor);
        const bool visible = mode < 2;
        const auto& background = fb.PixelData()[218 * 640 + 32];
        CHECK_EQ(background.r, visible ? 30 : 16);
        CHECK_EQ(background.g, background.r); CHECK_EQ(background.b, background.r);
        unsigned markers = 0;
        for (unsigned y = 220; y < 229; ++y) for (unsigned x = 23; x < 28; ++x) {
            const auto& pixel = fb.PixelData()[y * 640 + x];
            if (pixel.r == 192 && pixel.g == 192 && pixel.b == 192) ++markers;
            if (visible) CHECK(std::memcmp(&pixel, &expected.PixelData()[y * 640 + x], sizeof(Color)) == 0);
        }
        CHECK_EQ(markers > 0, visible);
        // Edit cursor remains at row zero, never replacing the playback marker.
        if (mode != 5) CHECK_EQ(fb.PixelData()[62 * 640 + 36].r, 255);
    }
}

TEST_CASE(EditorUsesActualCoreTimingBaselineAndRestore) {
    auto live=ConnectedDraft(); live->device_pattern->pattern.cells[0][0]={NOTE_OFF,7};
    live->device_pattern->pattern.note_off_timing[0][0]=BroTracker::NoteOffTiming::EndOfPosition;
    LocalPatternEditor editor; editor.Sync(live); CHECK(!editor.Dirty());
    CHECK_EQ(editor.OffTiming(0,0),BroTracker::NoteOffTiming::EndOfPosition);
    CHECK_EQ(editor.Draft()->note_off_timing[0][0],BroTracker::NoteOffTiming::EndOfPosition);
    CHECK(editor.Apply(EditorAction::Right)); CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.Dirty());
    CHECK_EQ(editor.OffTiming(0,0),BroTracker::NoteOffTiming::Arrival);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Clear)); CHECK(editor.Dirty());
    CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK_EQ(editor.OffTiming(0,0),BroTracker::NoteOffTiming::EndOfPosition);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument,7);
    editor.Sync(std::nullopt); live->device_pattern->pattern.note_off_timing[0][0]=static_cast<BroTracker::NoteOffTiming>(2);
    editor.Sync(live); CHECK(!editor.Ready());
}

TEST_CASE(NoteDeleteAtomicallyClearsInstrumentAndOffTiming) {
    for (const auto note : {Note(60), Note(NOTE_OFF), Note(NOTE_EMPTY)}) {
        auto live=ConnectedDraft(); live->device_pattern->pattern.cells[0][0]={note,0};
        live->device_pattern->pattern.note_off_timing[0][0]=BroTracker::NoteOffTiming::EndOfPosition;
        LocalPatternEditor editor; editor.Sync(live);
        CHECK(editor.Apply(EditorAction::Clear));
        CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_EMPTY);
        CHECK_EQ(editor.Draft()->cells[0][0].instrument,BroTracker::kNoInstrumentUpdate);
        CHECK_EQ(editor.Draft()->note_off_timing[0][0],BroTracker::NoteOffTiming::Arrival);
        CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
        CHECK(editor.Apply(EditorAction::SetValue,60)); CHECK(editor.Apply(EditorAction::Right));
        CHECK(editor.Apply(EditorAction::Clear)); CHECK_EQ(editor.Draft()->cells[0][0].note,60);
        CHECK_EQ(editor.Draft()->cells[0][0].instrument,255);
    }
}

TEST_CASE(OneBasedInstrumentDisplayAndRawEditorBounds) {
    CHECK_EQ(FormatInstrument(0),"01"); CHECK_EQ(FormatInstrument(1),"02");
    CHECK_EQ(FormatInstrument(255),"--"); CHECK_EQ(FormatInstrument(98),"99");
    CHECK_EQ(FormatInstrument(99),"100"); CHECK_EQ(FormatInstrument(254),"255");
    for(const auto action : {EditorAction::Increment,EditorAction::Decrement}) {
        auto live=ConnectedDraft(); live->device_pattern->pattern.cells[0][0].instrument=255;
        LocalPatternEditor editor; editor.Sync(live); CHECK(editor.Apply(EditorAction::Right));
        CHECK(editor.Apply(action)); CHECK_EQ(editor.Draft()->cells[0][0].instrument,0);
        CHECK_EQ(FormatInstrument(editor.Draft()->cells[0][0].instrument),"01");
        CHECK(editor.Apply(EditorAction::Decrement)); CHECK_EQ(editor.Draft()->cells[0][0].instrument,255);
        CHECK(editor.Apply(EditorAction::Increment)); CHECK_EQ(editor.Draft()->cells[0][0].instrument,0);
        CHECK(editor.Apply(EditorAction::Increment)); CHECK_EQ(editor.Draft()->cells[0][0].instrument,1);
        CHECK_EQ(FormatInstrument(editor.Draft()->cells[0][0].instrument),"02");
        CHECK(!editor.Apply(EditorAction::SetValue,254));
        CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
        CHECK_EQ(FormatInstrument(editor.Draft()->cells[0][0].instrument),"--");
    }
}

TEST_CASE(PatternDerivedEntrySkipsSpecialCellsAndStaysInChannel) {
    for(const auto direction : {EditorAction::Increment,EditorAction::Decrement}) {
        auto live=ConnectedDraft(); live->device_pattern->version=2;
        auto& pattern=live->device_pattern->pattern;
        pattern.cells[0][0]={60,1}; pattern.cells[1][0]={72,255};
        pattern.cells[2][0]={NOTE_OFF,7}; pattern.cells[3][0]={NOTE_EMPTY,9};
        pattern.cells[0][1]={84,255}; pattern.cells[15][1]={90,255};
        LocalPatternEditor editor; editor.Sync(live);
        for(unsigned i=0;i<4;++i) CHECK(editor.Apply(EditorAction::Down));
        CHECK(editor.Apply(direction)); CHECK_EQ(editor.Draft()->cells[4][0].note,72);
        CHECK_EQ(editor.Draft()->cells[4][0].instrument,255);
        CHECK(editor.Apply(direction)); CHECK_EQ(editor.Draft()->cells[4][0].note,direction==EditorAction::Increment ? 73 : 71);
        CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
        CHECK(editor.Apply(direction)); CHECK_EQ(editor.Draft()->cells[4][0].note,72);
        auto accepted=*editor.Baseline(); accepted.pattern=*editor.Draft(); CHECK(editor.Rebase(accepted));
        CHECK(editor.Apply(EditorAction::Down)); CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[5][0].note,72);
        CHECK(editor.Apply(EditorAction::NextChannel)); CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[5][1].note,84);
        CHECK(editor.Apply(EditorAction::Restore));
        for(unsigned i=0;i<5;++i) CHECK(editor.Apply(EditorAction::Up));
        CHECK(editor.Apply(EditorAction::Clear)); CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[0][1].note,60); // No other channel or end-of-pattern wrap.
    }
}
TEST_CASE(PatternDerivedEntryReflectsDraftDeletionAndExistingPitchChanges) {
    for(const auto direction : {EditorAction::Increment,EditorAction::Decrement}) {
        LocalPatternEditor editor; editor.Sync(ConnectedDraft());
        CHECK(editor.Apply(direction)); const auto pitch=direction==EditorAction::Increment ? 61 : 59;
        CHECK(editor.Apply(EditorAction::Down)); CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[1][0].note,pitch);
        CHECK(editor.Apply(EditorAction::Clear)); CHECK(editor.Apply(EditorAction::Up));
        CHECK(editor.Apply(EditorAction::Clear)); CHECK(editor.Apply(EditorAction::Down));
        CHECK(editor.Apply(direction)); CHECK_EQ(editor.Draft()->cells[1][0].note,60);
        CHECK(editor.Apply(EditorAction::NoteOff)); CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[1][0].note,direction==EditorAction::Increment?24:127); // OFF exits in the logical direction.
    }
}
TEST_CASE(PatternDerivedHeldEntryThenDirectionalRepeat) {
    for(const auto direction : {EditorAction::Increment,EditorAction::Decrement}) {
        auto live=ConnectedDraft(); live->device_pattern->pattern.cells[0][0].note=72;
        LocalPatternEditor editor; editor.Sync(live); CHECK(editor.Apply(EditorAction::Down));
        EditorRepeat repeat; CHECK(repeat.Begin(direction,0,editor));
        CHECK_EQ(editor.Draft()->cells[1][0].note,72);
        CHECK(!repeat.Tick(399,true,editor)); CHECK(repeat.Tick(400,true,editor));
        CHECK_EQ(editor.Draft()->cells[1][0].note,direction==EditorAction::Increment ? 73 : 71);
        CHECK(!repeat.Tick(499,true,editor)); CHECK(repeat.Tick(500,true,editor));
        CHECK_EQ(editor.Draft()->cells[1][0].note,direction==EditorAction::Increment ? 74 : 70);
    }
}

TEST_CASE(EditorHistoryAtomicDeleteRestoreAndBaseline) {
    LocalPatternEditor editor; auto live=ConnectedDraft(); editor.Sync(live);
    CHECK(!editor.CanUndo());
    CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK(editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::Increment));
    CHECK(editor.Apply(EditorAction::Left));
    const auto cursor=editor.Cursor();
    CHECK(editor.Apply(EditorAction::Clear));
    CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_EMPTY);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument,255);
    CHECK(editor.Undo());
    CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_OFF);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument,1);
    CHECK(editor.OffTiming(0,0)==BroTracker::NoteOffTiming::EndOfPosition);
    CHECK_EQ(editor.Cursor().row,cursor.row); CHECK(editor.Cursor().field==cursor.field);
    CHECK(editor.Redo()); CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK(editor.Undo()); CHECK(editor.Dirty());
    CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_EMPTY);
    CHECK(editor.Redo()); CHECK(!editor.Dirty());
    CHECK(editor.Apply(EditorAction::Increment));
    auto published=*editor.Baseline(); published.version=2; published.pattern=*editor.Draft();
    CHECK(editor.Rebase(published)); CHECK(!editor.Dirty());
    CHECK(editor.Undo()); CHECK(editor.Dirty()); CHECK_EQ(editor.Draft()->cells[0][0].note,60);
    CHECK(editor.Redo()); CHECK(!editor.Dirty());
    // Earlier Restore retains its captured target despite the changed baseline.
    CHECK(editor.Undo()); CHECK(editor.Undo()); CHECK_EQ(editor.Draft()->cells[0][0].note,NOTE_EMPTY);
    CHECK(editor.Redo()); CHECK_EQ(editor.Draft()->cells[0][0].note,60);
}
TEST_CASE(EditorHistoryWholePatternRestoreAndBranching) {
    LocalPatternEditor editor; editor.Sync(ConnectedDraft());
    CHECK(editor.Apply(EditorAction::Increment));
    CHECK(editor.Apply(EditorAction::Down)); CHECK(editor.Apply(EditorAction::Increment));
    CHECK(editor.Apply(EditorAction::NextChannel)); CHECK(editor.Apply(EditorAction::Right));
    CHECK(editor.Apply(EditorAction::Increment));
    const auto changed=*editor.Draft();
    CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK(editor.Undo()); CHECK(BroTracker::SamePattern(changed,*editor.Draft()));
    CHECK(editor.Redo()); CHECK(!editor.Dirty()); CHECK(editor.Undo());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(!editor.CanRedo());
    editor.SetLocked(true); CHECK(!editor.CanUndo()); CHECK(!editor.Undo()); CHECK(!editor.Redo());
    CHECK(!editor.Apply(EditorAction::Restore)); CHECK(editor.Apply(EditorAction::Up));
    editor.SetLocked(false); CHECK(editor.CanUndo());
    editor.ResetContext(); CHECK(!editor.Ready()); CHECK(!editor.CanUndo());
    editor.Sync(ConnectedDraft()); CHECK(!editor.CanUndo());
}
TEST_CASE(EditorHistoryCapacityNoOpsAndRecovery) {
    LocalPatternEditor editor; auto live=ConnectedDraft(); editor.Sync(live);
    CHECK(editor.Apply(EditorAction::SetValue,60)); CHECK(!editor.CanUndo());
    CHECK(editor.Apply(EditorAction::Down)); CHECK(!editor.CanUndo());
    for(unsigned i=0;i<70;++i) CHECK(editor.Apply(EditorAction::Increment));
    unsigned undone=0; while(editor.Undo()) ++undone;
    CHECK_EQ(undone,64u); CHECK_EQ(editor.Draft()->cells[1][0].note,65);
    for(unsigned i=0;i<64;++i) { CHECK(editor.Redo()); }
    CHECK(!editor.CanRedo());
    const auto unchanged=*editor.Draft(); CHECK(editor.RecoverDraft(unchanged)); CHECK(editor.CanUndo());
    // Preview readback never changes baseline or dirty comparison.
    CHECK(editor.Dirty()); CHECK_EQ(editor.Baseline()->pattern.cells[1][0].note,NOTE_EMPTY);
    auto different=unchanged; different.cells[1][0].note=60;
    CHECK(editor.RecoverDraft(different)); CHECK(!editor.CanUndo());
    CHECK(editor.Apply(EditorAction::Increment)); CHECK(editor.CanUndo());
    editor.Sync(std::nullopt); CHECK(!editor.Ready()); CHECK(!editor.CanUndo()); CHECK(!editor.Undo());
    // Even an identical snapshot cannot prove context continuity in this protocol.
    editor.Sync(live); CHECK(!editor.CanUndo()); CHECK(!editor.CanRedo());
}

TEST_CASE(EditorHistoryFullCapacityTransaction) {
    auto live=ConnectedDraft(); live->position.channels=8;
    live->device_pattern->pattern.active_channels=8;
    LocalPatternEditor editor; editor.Sync(live);
    const auto baseline=*editor.Draft();
    for(unsigned r=0;r<16;++r) {
        for(unsigned c=0;c<8;++c) {
            CHECK(editor.Apply(EditorAction::SetValue,70));
            CHECK(editor.Apply(EditorAction::NextChannel));
        }
        if(r<15) CHECK(editor.Apply(EditorAction::Down));
    }
    const auto modified=*editor.Draft();
    CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty());
    CHECK(editor.Undo()); CHECK(BroTracker::SamePattern(*editor.Draft(),modified));
    CHECK(editor.Redo()); CHECK(BroTracker::SamePattern(*editor.Draft(),baseline));
    CHECK_EQ(sizeof(PatternEditHistory::Change),8u);
}

TEST_CASE(SharedGainPresentationContainedAndNeverPaintsMuteOrPattern) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Framebuffer fb(640,480); Tune tune; Pattern preview; auto live=ConnectedDraft();
    LocalPatternEditor editor; editor.Sync(live);
    HostPanelInformation panel{"Connected - PLAYING","STOP pending, then EXIT","Preview synchronized"};
    RenderMainScreen(fb,tune,preview,live,&editor,panel);
    const std::vector<Color> before(fb.PixelData(),fb.PixelData()+640*480);
    live->gains_supported=true; BroTracker::HostGainState applied; applied.applied_id=UINT32_MAX;
    applied.gains.channels[2]=0; live->channel_gains=applied;
    BroTracker::Pcm16ChannelGains desired; desired.channels[2]=12345; live->desired_gains=desired;
    for(auto status:{"Channel gains pending","Channel gains submitted; awaiting application","Channel gains rejected"}) {
        live->gain_status=status; RenderMainScreen(fb,tune,preview,live,&editor,panel);
        unsigned changed=0;
        for(unsigned y=0;y<480;++y) for(unsigned x=0;x<640;++x) {
            if(!std::memcmp(&before[y*640+x],&fb.PixelData()[y*640+x],sizeof(Color))) continue;
            CHECK(x>=466 && x<631); CHECK(y>=38 && y<442); ++changed;
        }
        CHECK(changed>0); CHECK(!editor.Dirty());
    }
    live->channel_gains.reset(); live->desired_gains.reset(); live->gain_status="Channel gains awaiting state";
    RenderMainScreen(fb,tune,preview,live,&editor,panel); CHECK(!editor.CanUndo());
}

TEST_CASE(EmptyNoteC3FallbackPreservesInstrumentAndHistory) {
    for (const auto direction : {EditorAction::Increment, EditorAction::Decrement}) {
        auto live = ConnectedDraft();
        auto& pattern = live->device_pattern->pattern;
        pattern.cells[0][0] = {NOTE_OFF, 7};
        pattern.cells[1][0] = {NOTE_EMPTY, 9};
        pattern.cells[3][0] = {NOTE_EMPTY, 5};
        pattern.cells[0][1] = {84, 1}; // Other channels cannot provide a pitch.
        LocalPatternEditor editor; editor.Sync(live);
        for (unsigned row = 0; row < 3; ++row) CHECK(editor.Apply(EditorAction::Down));
        const auto cursor = editor.Cursor();
        CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[3][0].note, 60);
        CHECK_EQ(editor.Draft()->cells[3][0].instrument, 5);
        CHECK(editor.Undo());
        CHECK_EQ(editor.Draft()->cells[3][0].note, NOTE_EMPTY);
        CHECK_EQ(editor.Draft()->cells[3][0].instrument, 5);
        CHECK(!editor.Dirty());
        CHECK(editor.Redo());
        CHECK_EQ(editor.Draft()->cells[3][0].note, 60);
        CHECK_EQ(editor.Cursor().row, cursor.row);
        CHECK_EQ(editor.Cursor().channel, cursor.channel);
        CHECK(editor.Cursor().field == cursor.field);
        CHECK(editor.Apply(direction));
        CHECK_EQ(editor.Draft()->cells[3][0].note,
                 direction == EditorAction::Increment ? 61 : 59);
        CHECK_EQ(editor.Draft()->cells[3][0].instrument, 5);
        CHECK(editor.Apply(EditorAction::Restore));
        EditorRepeat repeat;
        CHECK(repeat.Begin(direction, 0, editor));
        CHECK_EQ(editor.Draft()->cells[3][0].note, 60);
        CHECK(!repeat.Tick(399, true, editor));
        CHECK(repeat.Tick(400, true, editor));
        CHECK_EQ(editor.Draft()->cells[3][0].note,
                 direction == EditorAction::Increment ? 61 : 59);
    }
}

TEST_CASE(EditorOffCyclesPreserveInstrumentAndUndoTiming) {
    LocalPatternEditor e; e.Sync(ConnectedDraft());
    CHECK(e.Apply(EditorAction::Right)); CHECK(e.Apply(EditorAction::SetValue,17)); CHECK(e.Apply(EditorAction::Left));
    CHECK(e.Apply(EditorAction::SetValue,24));
    for(unsigned note=25;note<=127;++note) {CHECK(e.Apply(EditorAction::Increment)); CHECK_EQ(e.Draft()->cells[0][0].note,note);}
    CHECK(e.Apply(EditorAction::Increment)); CHECK_EQ(e.Draft()->cells[0][0].note,NOTE_OFF);
    CHECK_EQ(e.Draft()->cells[0][0].instrument,17); CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::Arrival);
    CHECK(e.Apply(EditorAction::Right)); CHECK(e.Apply(EditorAction::Increment)); CHECK(e.Apply(EditorAction::Left));
    CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::EndOfPosition);
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].note,127);
    CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::Arrival); CHECK(e.Undo());
    CHECK_EQ(e.Draft()->cells[0][0].note,NOTE_OFF); CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::EndOfPosition);
    CHECK(e.Redo()); CHECK_EQ(e.Draft()->cells[0][0].note,127);
    for(unsigned note=126;note>=24;--note) {CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].note,note);}
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].note,NOTE_OFF);
    CHECK(e.Apply(EditorAction::Increment)); CHECK_EQ(e.Draft()->cells[0][0].note,24);
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].note,NOTE_OFF);
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].note,127);
    CHECK_EQ(e.Draft()->cells[0][0].instrument,17);
}
TEST_CASE(SharedValueStepDirectionAppliesOnceAndRepeatsThroughOff) {
    for(auto direction : {BroTracker::ValueStepDirection::Normal,BroTracker::ValueStepDirection::Reversed}) {
        for(auto request : {EditorAction::Increment,EditorAction::Decrement}) {
            LocalPatternEditor e; e.Sync(ConnectedDraft()); EditorStepInput input(direction);
            const auto logical=ResolveEditorStep(request,EditField::Note,direction);
            const auto endpoint=logical==EditorAction::Increment?127:24;
            const auto next=logical==EditorAction::Increment?24:127;
            CHECK(e.Apply(EditorAction::SetValue,endpoint)); CHECK(input.Begin(request,0,e));
            CHECK_EQ(e.Draft()->cells[0][0].note,NOTE_OFF);
            CHECK(!input.Tick(399,true,e)); CHECK(input.Tick(400,true,e)); CHECK_EQ(e.Draft()->cells[0][0].note,next);
            input.Release(request); CHECK(!input.Tick(1000,true,e));
            CHECK(e.Apply(EditorAction::Clear)); CHECK(input.Begin(request,1100,e));
            CHECK_EQ(e.Draft()->cells[0][0].note,60); input.Cancel();
            CHECK(e.Apply(EditorAction::SetValue,74)); CHECK(e.Apply(EditorAction::Down)); CHECK(e.Apply(EditorAction::Clear));
            CHECK(input.Begin(request,1200,e)); CHECK_EQ(e.Draft()->cells[1][0].note,74);
            CHECK_EQ(e.Draft()->cells[1][0].instrument,255); input.Cancel();
            CHECK(e.Apply(EditorAction::Right)); CHECK(input.Begin(EditorAction::Increment,1300,e));
            CHECK_EQ(e.Draft()->cells[1][0].instrument,0); input.Cancel();
            CHECK(input.Begin(EditorAction::Increment,1400,e)); CHECK_EQ(e.Draft()->cells[1][0].instrument,direction==BroTracker::ValueStepDirection::Normal ? 1 : 255); input.Cancel();
            CHECK(e.Apply(EditorAction::Left)); CHECK(e.Apply(EditorAction::NoteOff)); CHECK(e.Apply(EditorAction::Right));
            CHECK(input.Begin(request,1500,e)); CHECK(e.OffTiming(1,0)==LocalNoteOffTiming::EndOfPosition);
            input.Cancel(); CHECK_EQ(e.Draft()->cells[1][0].instrument,direction==BroTracker::ValueStepDirection::Normal ? 1 : 255);
        }
    }
}

TEST_CASE(NumericInstrumentCycleEntryNavigationHistoryAndHighDeviceIds) {
    for(const auto action:{EditorAction::Increment,EditorAction::Decrement}) {
        LocalPatternEditor e; e.Sync(ConnectedDraft()); CHECK(e.Apply(EditorAction::Right));
        CHECK(e.Apply(EditorAction::Clear)); CHECK(e.Apply(action)); CHECK_EQ(e.Draft()->cells[0][0].instrument,0);
        CHECK_EQ(e.Draft()->cells[0][0].note,60);
        CHECK(e.Apply(EditorAction::SetValue,action==EditorAction::Increment ? 98 : 0));
        CHECK(e.Apply(action)); CHECK_EQ(e.Draft()->cells[0][0].instrument,255);
        CHECK(e.Apply(action)); CHECK_EQ(e.Draft()->cells[0][0].instrument,action==EditorAction::Increment ? 0 : 98);
        CHECK(e.Undo()); CHECK_EQ(e.Draft()->cells[0][0].instrument,255); CHECK(e.Redo());
        CHECK(e.Apply(EditorAction::SetValue,0)); CHECK(e.Apply(EditorAction::Decrement));
        CHECK(e.Apply(EditorAction::Left)); CHECK(e.Apply(EditorAction::Right));
        CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].instrument,0);
        CHECK(e.Apply(EditorAction::Clear)); CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].instrument,0);
        CHECK_EQ(e.Draft()->cells[0][0].note,60);
    }
    auto live=ConnectedDraft(); live->device_pattern->pattern.cells[0][0].instrument=254;
    LocalPatternEditor high; high.Sync(live); CHECK(high.Apply(EditorAction::Right));
    CHECK(!high.Apply(EditorAction::Increment)); CHECK(!high.Apply(EditorAction::Decrement));
    CHECK(!high.Apply(EditorAction::SetValue,99)); CHECK(!high.Dirty());
    CHECK_EQ(high.Draft()->cells[0][0].instrument,254); CHECK_EQ(FormatInstrument(254),"255");
    CHECK(high.Apply(EditorAction::Clear)); CHECK(high.Undo()); CHECK_EQ(high.Draft()->cells[0][0].instrument,254);
    CHECK(high.Apply(EditorAction::Restore)); CHECK_EQ(high.Draft()->cells[0][0].instrument,254);
}
TEST_CASE(NumericInstrumentDirectionHoldsBoundaryPauseAndOffTiming) {
    for(const auto direction:{BroTracker::ValueStepDirection::Normal,BroTracker::ValueStepDirection::Reversed})
    for(const auto request:{EditorAction::Increment,EditorAction::Decrement}) {
        LocalPatternEditor e; e.Sync(ConnectedDraft()); CHECK(e.Apply(EditorAction::Right));
        const auto logical=ResolveEditorStep(request,EditField::Instrument,direction);
        const bool increase=logical==EditorAction::Increment;
        CHECK(e.Apply(EditorAction::SetValue,increase ? 97 : 1));
        EditorStepInput input(direction); CHECK(input.Begin(request,0,e));
        CHECK_EQ(e.Draft()->cells[0][0].instrument,increase ? 98 : 0);
        CHECK(!input.Tick(399,true,e)); CHECK(input.Tick(400,true,e)); CHECK_EQ(e.Draft()->cells[0][0].instrument,255);
        CHECK(!input.Tick(799,true,e)); CHECK(input.Tick(800,true,e)); CHECK_EQ(e.Draft()->cells[0][0].instrument,increase ? 0 : 98);
        CHECK(!input.Tick(899,true,e)); CHECK(input.Tick(900,true,e)); CHECK_EQ(e.Draft()->cells[0][0].instrument,increase ? 1 : 97);
        input.Cancel(); CHECK(e.Apply(EditorAction::Left)); CHECK(e.Apply(EditorAction::NoteOff)); CHECK(e.Apply(EditorAction::Right));
        const auto instrument=e.Draft()->cells[0][0].instrument;
        CHECK(input.Begin(request,1000,e)); CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::EndOfPosition);
        CHECK(input.Tick(1400,true,e)); CHECK(e.OffTiming(0,0)==LocalNoteOffTiming::Arrival);
        CHECK_EQ(e.Draft()->cells[0][0].instrument,instrument);
    }
}

TEST_CASE(InstrumentCycleMatchingReadbackPreservesInteractionButReplacementResets) {
    LocalPatternEditor e; e.Sync(ConnectedDraft()); CHECK(e.Apply(EditorAction::Right));
    CHECK(e.Apply(EditorAction::SetValue,0)); CHECK(e.Apply(EditorAction::Decrement));
    const auto empty=*e.Draft(); CHECK(e.RecoverDraft(empty));
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].instrument,98);
    CHECK(e.RecoverDraft(empty)); CHECK(e.Apply(EditorAction::Decrement));
    CHECK_EQ(e.Draft()->cells[0][0].instrument,0);
    CHECK(e.Apply(EditorAction::Decrement)); CHECK(e.Apply(EditorAction::Increment));
    CHECK_EQ(e.Draft()->cells[0][0].instrument,0); // Opposite direction leaves cycled empty correctly.
    CHECK(e.Apply(EditorAction::Decrement)); CHECK(!e.Apply(EditorAction::SetValue,99));
    CHECK(e.Apply(EditorAction::Decrement)); CHECK_EQ(e.Draft()->cells[0][0].instrument,98);
    e.Sync(std::nullopt); e.Sync(ConnectedDraft()); CHECK_EQ(e.Cursor().field,EditField::Instrument);
    CHECK(e.Apply(EditorAction::Clear)); CHECK(e.Apply(EditorAction::Decrement));
    CHECK_EQ(e.Draft()->cells[0][0].instrument,0);
}

TEST_CASE(BeatNavigationUsesHighlightedRowsVisibleBoundsAndPreservesEditorData) {
    CHECK(PatternGrid::IsBeatRow(0)); CHECK(PatternGrid::IsBeatRow(4));
    CHECK(!PatternGrid::IsBeatRow(3));
    for (unsigned length : {1u, 2u, 4u, 5u, 6u, 8u, 9u, 16u, 31u, 32u, 33u, 64u}) {
        auto live = ConnectedDraft();
        live->position.rows = length; live->device_pattern->pattern.active_rows = length;
        for (unsigned origin = 0; origin < length; ++origin) {
            LocalPatternEditor editor; editor.Sync(live); CHECK(editor.Ready());
            for (unsigned i = 0; i < origin; ++i) CHECK(editor.Apply(EditorAction::Down));
            CHECK(editor.Apply(EditorAction::NextChannel)); CHECK(editor.Apply(EditorAction::Right));
            const auto before = *editor.Draft(); const auto cursor = editor.Cursor();
            for (const bool forward : {false, true}) {
                // Independent row-by-row oracle, using the renderer's highlight predicate.
                auto expected = origin;
                if (forward) {
                    for (unsigned r = origin + 1; r < length && r < PatternGrid::visible_rows; ++r)
                        if (PatternGrid::IsBeatRow(r)) { expected = r; break; }
                } else {
                    for (unsigned r = origin; r > 0; --r)
                        if (r-1 < PatternGrid::visible_rows && PatternGrid::IsBeatRow(r-1)) { expected = r-1; break; }
                }
                editor.SetLocked(true); // Publication locks data, not navigation.
                CHECK_EQ(editor.Apply(forward ? EditorAction::NextBeat : EditorAction::PreviousBeat), expected != origin || (!forward && !origin));
                CHECK_EQ(editor.Cursor().row, expected);
                CHECK_EQ(editor.Cursor().channel, cursor.channel); CHECK(editor.Cursor().field == cursor.field);
                CHECK(BroTracker::SamePattern(before, *editor.Draft()));
                CHECK(!editor.Dirty()); CHECK(!editor.CanUndo()); CHECK(!editor.CanRedo());
                BroTracker::SparsePreviewCell captured;
                for (unsigned i = 0; i < BroTracker::kSparsePreviewCellCapacity; ++i)
                    CHECK(!editor.TakeCapturedCell(i, captured));
                if(editor.Cursor().header) CHECK(editor.Apply(EditorAction::Down));
                while (editor.Cursor().row < origin) CHECK(editor.Apply(EditorAction::Down));
                while (editor.Cursor().row > origin) CHECK(editor.Apply(EditorAction::Up));
            }
        }
    }
}

TEST_CASE(EditorRestoresValidCursorAndClampsChangedPatternDimensions) {
    LocalPatternEditor editor;editor.Sync(ConnectedDraft());
    EditCursor cursor{12,1,EditField::Instrument,false};editor.RestoreCursor(cursor);
    CHECK_EQ(editor.Cursor().row,12u);CHECK_EQ(editor.Cursor().channel,1u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
    auto live=ConnectedDraft();live->device_pattern->pattern.active_rows=4;live->device_pattern->pattern.active_channels=1;
    live->position.rows=4;live->position.channels=1;editor.ResetContext();editor.Sync(live);editor.RestoreCursor(cursor);
    CHECK_EQ(editor.Cursor().row,3u);CHECK_EQ(editor.Cursor().channel,0u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
    cursor.header=true;editor.RestoreCursor(cursor);CHECK(editor.Cursor().header);CHECK_EQ(editor.Cursor().row,0u);
}

TEST_CASE(SessionRestorationRetainsKnownCursorAndDefaultsOnlyForFreshEditor) {
    for(bool header:{false,true}) {
        LocalPatternEditor editor;editor.Sync(ConnectedDraft());editor.RestoreCursor({12,1,EditField::Instrument,header});
        const auto cursor=editor.Cursor();editor.Sync(std::nullopt);editor.Sync(std::nullopt);
        auto restored=ConnectedDraft();restored->device_pattern->pattern.active_rows=4;restored->device_pattern->pattern.active_channels=1;
        restored->position.rows=4;restored->position.channels=1;editor.Sync(restored);
        CHECK(editor.Ready());CHECK_EQ(editor.Cursor().row,header?0u:3u);CHECK_EQ(editor.Cursor().channel,0u);
        CHECK_EQ(editor.Cursor().field,cursor.field);CHECK_EQ(editor.Cursor().header,header);
        editor.ResetContext(true);editor.Sync(restored);CHECK_EQ(editor.Cursor().row,header?0u:3u);CHECK_EQ(editor.Cursor().field,EditField::Instrument);
        LocalPatternEditor fresh;fresh.Sync(restored);CHECK_EQ(fresh.Cursor().row,0u);CHECK_EQ(fresh.Cursor().channel,0u);
        CHECK_EQ(fresh.Cursor().field,EditField::Note);CHECK(!fresh.Cursor().header);
    }
}
