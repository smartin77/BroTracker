#include "test_framework.h"
#include "ui/local_pattern_editor.h"
#include "ui/editor_repeat.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
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
TEST_CASE(LocalEditorNavigationAndRawFields) {
    LocalPatternEditor editor;
    CHECK(!editor.Apply(EditorAction::Increment));
    auto live = ConnectedDraft(); editor.Sync(live);
    CHECK(editor.Ready()); CHECK(!editor.Dirty());
    CHECK(!editor.Apply(EditorAction::Up)); CHECK(!editor.Apply(EditorAction::Left));
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
    CHECK(editor.Apply(EditorAction::SetValue, 254));
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 254);
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
    CHECK_EQ(editor.Cursor().row, 0); CHECK(!editor.Dirty());
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
    CHECK_EQ(editor.Draft()->cells[0][0].note, 24);
    CHECK(editor.Apply(EditorAction::Decrement)); CHECK_EQ(editor.Draft()->cells[0][0].note, 127);
    CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK(editor.Apply(EditorAction::Decrement));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 24);
    for (unsigned i = 0; i < 12; ++i) CHECK(editor.Apply(EditorAction::Increment));
    CHECK_EQ(editor.Draft()->cells[0][0].note, 36);
    CHECK(editor.Apply(EditorAction::SetValue, 127));
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
    CHECK(!editor.Apply(EditorAction::Decrement));
    CHECK(editor.Apply(EditorAction::SetValue, 254)); CHECK(!editor.Apply(EditorAction::Increment));
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
    CHECK_EQ(editor.Draft()->cells[0][0].note, NOTE_EMPTY); CHECK_EQ(editor.Draft()->cells[0][0].instrument, 8);
    CHECK(editor.Apply(EditorAction::Left)); CHECK(editor.Apply(EditorAction::NoteOff));
    CHECK_EQ(editor.OffTiming(0, 0), LocalNoteOffTiming::Arrival);
    CHECK_EQ(editor.Draft()->cells[0][0].instrument, 8);
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
    for (const auto action : {EditorAction::Increment, EditorAction::Decrement}) {
        LocalPatternEditor editor; editor.Sync(ConnectedDraft()); EditorRepeat repeat;
        const unsigned endpoint = action == EditorAction::Increment ? 127 : 24;
        const unsigned before = action == EditorAction::Increment ? 126 : 25;
        const unsigned wrapped = action == EditorAction::Increment ? 24 : 127;
        CHECK(editor.Apply(EditorAction::SetValue, before));
        CHECK(repeat.Begin(action, 0, editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note, endpoint);
        CHECK(!repeat.Tick(399, true, editor)); CHECK(repeat.Tick(400, true, editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note, wrapped);
        CHECK(!repeat.Tick(499, true, editor)); CHECK(repeat.Tick(500, true, editor));
        // Reach the boundary again during accelerated repetition, then renew delay.
        CHECK(editor.Apply(EditorAction::SetValue, before));
        CHECK(repeat.Tick(2100, true, editor)); CHECK_EQ(editor.Draft()->cells[0][0].note, endpoint);
        CHECK(!repeat.Tick(2499, true, editor)); CHECK(repeat.Tick(2500, true, editor));
        CHECK_EQ(editor.Draft()->cells[0][0].note, wrapped);
        CHECK(!repeat.Tick(2599, true, editor)); CHECK(repeat.Tick(2600, true, editor));
        CHECK(!repeat.Tick(2699, true, editor)); CHECK(repeat.Tick(2700, true, editor));
        repeat.Release(action);
        CHECK(editor.Apply(EditorAction::SetValue, endpoint));
        CHECK(repeat.Begin(action, 3000, editor)); // New physical press wraps immediately.
        CHECK_EQ(editor.Draft()->cells[0][0].note, wrapped);
        CHECK(!repeat.Tick(3399, true, editor)); CHECK(repeat.Tick(3400, true, editor));
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
        // Former information-label rows are now entirely empty pattern grid.
        for (unsigned y = 290; y < 340; ++y) for (unsigned x = 2; x < 453; ++x) {
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
                "L1 / B: RESTART | R1 / X: STOP (stay)"}) {
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
    RenderMainScreen(fb, tune, preview);
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
