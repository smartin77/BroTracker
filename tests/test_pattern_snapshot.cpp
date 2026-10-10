#include "test_framework.h"
#include "bringup/pattern_snapshot.h"
#include "bringup/pattern_control.h"
#include "ui/bringup_serial.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace BroTracker;
namespace
{
    RealtimePattern Fixture()
    {
        RealtimePattern p; p.active_rows = 3; p.active_channels = 2;
        p.cells[0][0] = {60, 0}; p.cells[0][1] = {NOTE_EMPTY, 254};
        p.cells[1][0] = {NOTE_OFF, 255}; p.cells[1][1] = {62, 255};
        p.cells[2][0] = {NOTE_OFF, 254}; // Explicit selection with OFF is preserved.
        return p;
    }
    std::vector<std::string> Lines(const RealtimePattern& pattern, std::uint32_t id = 1,
        std::uint32_t tempo_hundredths = 12753)
    {
        PatternSnapshotSender sender(pattern, tempo_hundredths);
        CHECK(sender.Begin(id, 0));
        std::vector<std::string> result;
        for (unsigned budget = 0; sender.HasData() && budget < 130; ++budget)
        {
            std::size_t count; const auto* data = sender.Data(count);
            CHECK(count > 0); result.emplace_back(data, count - 1); sender.Consume(count);
        }
        CHECK(!sender.HasData()); return result;
    }
    void Same(const DevicePatternSnapshot& snapshot, const RealtimePattern& pattern)
    {
        CHECK_EQ(snapshot.tempo_hundredths, 12753);
        CHECK_EQ(snapshot.pattern.active_rows, pattern.active_rows);
        CHECK_EQ(snapshot.pattern.active_channels, pattern.active_channels);
        for (unsigned r = 0; r < pattern.active_rows; ++r)
            for (unsigned c = 0; c < pattern.active_channels; ++c)
            {
                CHECK_EQ(snapshot.pattern.cells[r][c].note, pattern.cells[r][c].note);
                CHECK_EQ(snapshot.pattern.cells[r][c].instrument, pattern.cells[r][c].instrument);
            }
    }
    SnapshotAssemblyStatus Feed(PatternSnapshotAssembler& assembly, const std::string& line,
        std::uint32_t now = 1)
    { return assembly.Accept(line.data(), line.size(), now); }
    struct SnapshotTransport : SerialTransport
    {
        bool online = true;
        unsigned write_limit = 32;
        std::string input, output;
        bool Open(FILE*, const char*) override { return online; }
        void Close() override { input.clear(); }
        bool Healthy() override { return online; }
        int Read(char* data, unsigned count) override
        {
            const auto n = std::min<std::size_t>(count, input.size());
            std::memcpy(data, input.data(), n); input.erase(0, n); return static_cast<int>(n);
        }
        int Write(const char* data, unsigned count) override
        { count = std::min(count, write_limit); output.append(data, count); return static_cast<int>(count); }
        const char* Error() const override { return "test disconnect"; }
    };
    struct Session
    {
        SnapshotTransport* transport = new SnapshotTransport;
        BringUpSerial serial{nullptr, std::unique_ptr<SerialTransport>(transport)};
        std::uint32_t now = 0;
        void Tick() { serial.Tick(now++); }
        void Receive(const std::string& line) { transport->input += line; Tick(); }
        void Ready(bool capable)
        {
            Tick(); CHECK_EQ(transport->output, "BTTEST1 HELLO\n"); transport->output.clear();
            Receive(std::string("BTTEST1 STATE PLAYING\n") +
                "BTPATTERN1 POS 1 1 1 0 0 0 3 2\n" + (capable ? "BTPATTERN1 SNAPCAP 1\n" : ""));
            CHECK(serial.Connected());
        }
    };
}

TEST_CASE(PatternSnapshot_RoundTripAllRawCellsAndAtomicPublication)
{
    const auto pattern = Fixture(); const auto lines = Lines(pattern);
    PatternSnapshotAssembler assembly; CHECK(assembly.Request(1, 0));
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        CHECK(!assembly.Snapshot());
        CHECK_EQ(Feed(assembly, lines[i]), i + 1 == lines.size() ?
            SnapshotAssemblyStatus::Complete : SnapshotAssemblyStatus::Receiving);
    }
    CHECK(assembly.Snapshot()); Same(*assembly.Snapshot(), pattern);
    CHECK_EQ(assembly.Snapshot()->transfer_id, 1);
    // A later failed transfer cannot replace a committed snapshot.
    CHECK(assembly.Request(2, 10));
    CHECK_EQ(Feed(assembly, "BTPATTERN1 BEGIN 1 2 3 2 12753 6", 11), SnapshotAssemblyStatus::Receiving);
    CHECK_EQ(Feed(assembly, "BTPATTERN1 CELL 1 2 0 128 0", 12), SnapshotAssemblyStatus::Rejected);
    Same(*assembly.Snapshot(), pattern);
    // Full capacity and maximum ID/tempo fit bounded formatting/parsing.
    RealtimePattern maximum;
    const auto full = Lines(maximum, UINT32_MAX, UINT32_MAX);
    CHECK_EQ(full.size(), 130); CHECK(assembly.Request(UINT32_MAX, 20));
    for (const auto& line : full) (void)Feed(assembly, line, 21);
    CHECK_EQ(assembly.Snapshot()->tempo_hundredths, UINT32_MAX);
    CHECK_EQ(assembly.Snapshot()->pattern.active_channels, 8);
}

TEST_CASE(PatternSnapshot_RejectionOrderingIdentityCompletionTimeoutAndCancellation)
{
    const auto pattern = Fixture(); const auto lines = Lines(pattern);
    const std::vector<std::string> bad_headers = {
        "BTPATTERN1 BEGIN 2 1 3 2 12753 6", "BTPATTERN1 BEGIN 1 2 3 2 12753 6",
        "BTPATTERN1 BEGIN 1 1 0 2 12753 0", "BTPATTERN1 BEGIN 1 1 17 2 12753 34",
        "BTPATTERN1 BEGIN 1 1 3 0 12753 0", "BTPATTERN1 BEGIN 1 1 3 9 12753 27",
        "BTPATTERN1 BEGIN 1 1 3 2 0 6", "BTPATTERN1 BEGIN 1 1 3 2 12753 5",
        "BTPATTERN1 BEGIN 1 1 3 2 4294967296 6", "BTPATTERN1 BEGIN 1 1 3 2 -1 6",
        "BTPATTERN1 BEGIN 1 1 3 2 +1 6", "BTPATTERN1 BEGIN 1 1 3 2 12753 6 extra",
        "BTPATTERN1 BEGIN 1 1 3  2 12753 6", "BTPATTERN1 BEGIN 1 1 3\t2 12753 6",
        std::string(100, '0')};
    for (const auto& line : bad_headers)
    {
        PatternSnapshotAssembler a; CHECK(a.Request(1, 0));
        CHECK_EQ(Feed(a, line), SnapshotAssemblyStatus::Rejected); CHECK(!a.Snapshot());
    }
    const std::vector<std::string> bad_cells = {
        "BTPATTERN1 CELL 1 1 1 60 0", "BTPATTERN1 CELL 1 2 0 60 0",
        "BTPATTERN1 CELL 1 1 0 128 0", "BTPATTERN1 CELL 1 1 0 253 0",
        "BTPATTERN1 CELL 1 1 0 60 256", "BTPATTERN1 CELL 1 1 0 60 4294967296",
        "BTPATTERN1 CELL 1 1 0 60", "BTPATTERN1 CELL 1 1 0 60 0 "};
    for (const auto& line : bad_cells)
    {
        PatternSnapshotAssembler a; CHECK(a.Request(1, 0)); (void)Feed(a, lines[0]);
        CHECK_EQ(Feed(a, line), SnapshotAssemblyStatus::Rejected); CHECK(!a.Snapshot());
    }
    for (const auto mode : {0, 1, 2, 3, 4})
    {
        PatternSnapshotAssembler a; CHECK(a.Request(1, 0)); (void)Feed(a, lines[0]);
        if (mode == 0) CHECK_EQ(Feed(a, lines[0]), SnapshotAssemblyStatus::Rejected); // Duplicate BEGIN.
        if (mode == 1)
        { (void)Feed(a, lines[1]); CHECK_EQ(Feed(a, lines[1]), SnapshotAssemblyStatus::Rejected); }
        if (mode == 2) CHECK_EQ(Feed(a, lines.back()), SnapshotAssemblyStatus::Rejected); // Missing cells.
        if (mode >= 3)
        {
            for (std::size_t i = 1; i + 1 < lines.size(); ++i) (void)Feed(a, lines[i]);
            CHECK_EQ(Feed(a, mode == 3 ? "BTPATTERN1 END 1 1 6 0" : "BTPATTERN1 END 1 1 5 0"),
                SnapshotAssemblyStatus::Rejected);
        }
        CHECK(!a.Snapshot());
    }
    PatternSnapshotAssembler a; CHECK(!a.Request(0, 0)); CHECK(a.Request(1, UINT32_MAX - 100));
    CHECK_EQ(a.Tick(100), SnapshotAssemblyStatus::Ignored);
    CHECK_EQ(a.Tick(5000), SnapshotAssemblyStatus::Timeout);
    CHECK_EQ(Feed(a, lines[0], 5001), SnapshotAssemblyStatus::Ignored);
    CHECK(a.Request(1, 0)); a.Cancel();
    for (const auto& line : lines) CHECK_EQ(Feed(a, line), SnapshotAssemblyStatus::Ignored);
    CHECK(!a.Snapshot());
    CHECK(a.Request(1, 0));
    for (const auto& line : lines) (void)Feed(a, line);
    a.Clear(); CHECK(!a.Snapshot());
}

TEST_CASE(PatternSnapshot_FairInterleavingAckPriorityPartialWritesAndPlaybackIndependence)
{
    const auto pattern = Fixture();
    PatternSnapshotSender sender(pattern, 12753); PatternTelemetrySender positions;
    PatternOutputMux mux(positions, sender); CHECK(sender.Begin(7, 0));
    CHECK(!sender.Begin(8, 0)); // Busy leaves original transfer intact.
    PatternSnapshotAssembler assembly; CHECK(assembly.Request(7, 0));
    std::string received, ack = "BTTEST1 STARTED\n", wire;
    std::size_t ack_offset = 0; unsigned snapshots = 0, telemetry = 0;
    bool stop_ack = false;
    for (unsigned step = 0; step < 1000; ++step)
    {
        positions.Offer({true, true, 0, 0, 0, 3, 2}); // Continuously due: must not starve cells.
        const bool ack_waiting = step >= (stop_ack ? 80u : 3u) && ack_offset < ack.size();
        const auto lane = mux.Select(ack_waiting, ack_offset != 0 && ack_offset < ack.size());
        if (ack_waiting && !positions.Partial() && !sender.Partial()) CHECK_EQ(lane, PatternOutputLane::Command);
        std::size_t count; const char* data;
        if (lane == PatternOutputLane::Command) { data = ack.data() + ack_offset; count = ack.size() - ack_offset; }
        else data = lane == PatternOutputLane::Position ? positions.Data(count) : sender.Data(count);
        const auto written = step % 5 == 0 ? 0 : std::min<std::size_t>(5, count); // Backpressure/partial writes.
        wire.append(data, written); received.append(data, written);
        if (lane == PatternOutputLane::Command) ack_offset += written;
        else mux.Consume(lane, written);
        const auto newline = received.find('\n');
        if (newline != std::string::npos)
        {
            const auto line = received.substr(0, newline); received.erase(0, newline + 1);
            if (line.find("BTPATTERN1 POS") == 0)
            { PatternTelemetry p; CHECK(ParsePatternTelemetry(line.data(), line.size(), p)); ++telemetry; }
            else if (line.find("BTTEST1") == 0)
                CHECK(line == "BTTEST1 STARTED" || line == "BTTEST1 STOPPED");
            else { (void)Feed(assembly, line); ++snapshots; }
        }
        if (assembly.Snapshot() && stop_ack && ack_offset == ack.size()) break;
        if (!stop_ack && ack_offset == ack.size())
        { ack = "BTTEST1 STOPPED\n"; ack_offset = 0; stop_ack = true; }
    }
    CHECK(assembly.Snapshot()); Same(*assembly.Snapshot(), pattern);
    CHECK_EQ(snapshots, 8); CHECK(telemetry >= 7);
    CHECK(wire.find("BTTEST1 STARTED\n") != std::string::npos);
    CHECK(wire.find("BTTEST1 STOPPED\n") > wire.find("BTTEST1 STARTED\n"));
    // Serving immutable metadata does not invoke Start, Stop, Render or advance time.
    RealtimePattern empty; NativeRateSampleBindings bindings{};
    PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753, 44100, empty, bindings), PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start), PatternRequestStatus::Accepted);
    std::int16_t pcm[128]{}; control.AudioBlock(pcm, 128, true);
    const auto before = control.Snapshot();
    PatternSnapshotSender read_only(empty, 12753); CHECK(read_only.Begin(1, 0));
    for (unsigned i = 0; i < 130; ++i)
    { std::size_t count; (void)read_only.Data(count); read_only.Consume(count); }
    const auto after = control.Snapshot();
    CHECK_EQ(before.running, after.running); CHECK_EQ(before.next_sample, after.next_sample);
    CHECK_EQ(before.position.absolute_tick, after.position.absolute_tick);
    // Timeout completes an already partial line only; never starts another.
    CHECK(sender.Begin(9, 10)); std::size_t count; (void)sender.Data(count); sender.Consume(2);
    sender.Tick(5010); CHECK(sender.Partial()); (void)sender.Data(count); sender.Consume(count);
    CHECK(!sender.HasData());
}

TEST_CASE(PatternSnapshot_HostLegacyDiscoveryFragmentationTimeoutAndReconnect)
{
    Session legacy; legacy.Ready(false); legacy.Tick(); CHECK_EQ(legacy.transport->output, "");
    legacy.Receive("BTPATTERN1 SNAPCAP 2\n"); legacy.Tick(); CHECK_EQ(legacy.transport->output, "");
    Session s; s.Ready(true); s.Tick(); CHECK_EQ(s.transport->output, "BTPATTERN1 GET 1 1\n");
    CHECK(s.serial.PlaybackView(s.now)->pattern_loading); CHECK(!s.serial.PlaybackView(s.now)->device_pattern);
    const auto pattern = Fixture(); const auto lines = Lines(pattern);
    for (const auto& line : lines)
        for (char c : line + "\n") s.Receive(std::string(1, c));
    CHECK(s.serial.PlaybackView(s.now)->device_pattern);
    Same(*s.serial.PlaybackView(s.now)->device_pattern, pattern);
    s.transport->online = false; s.Tick(); CHECK(!s.serial.PlaybackView(s.now));
    s.transport->online = true; s.now = 1000; s.transport->output.clear(); s.Tick();
    CHECK_EQ(s.transport->output, "BTTEST1 HELLO\n"); s.transport->output.clear();
    s.Receive("BTTEST1 STATE PLAYING\nBTPATTERN1 SNAPCAP 1\nBTPATTERN1 POS 1 1 1 96 1 0 3 2\n");
    s.Tick(); CHECK_EQ(s.transport->output, "BTPATTERN1 GET 1 2\n");
    CHECK(!s.serial.PlaybackView(s.now)->device_pattern);
    s.Receive(lines[0] + "\n"); // Cancelled transfer identity must not be accepted.
    CHECK(s.serial.PlaybackView(s.now)->pattern_failed);
    CHECK(s.serial.Connected()); CHECK(s.serial.PlaybackView(s.now)->position.valid);
    CHECK_EQ(s.transport->output.find("START"), std::string::npos);
    Session timeout; timeout.Ready(true); timeout.Tick(); timeout.now += 5000; timeout.Tick();
    CHECK(timeout.serial.PlaybackView(timeout.now)->pattern_failed); CHECK(timeout.serial.Connected());
    for (const auto& line : lines) timeout.Receive(line + "\n");
    CHECK(!timeout.serial.PlaybackView(timeout.now)->device_pattern);
}

TEST_CASE(PatternSnapshot_SharedHostPartialRequestWithStartStopAndMalformedStaging)
{
    Session s; s.Ready(true); s.transport->write_limit = 2;
    s.Tick(); CHECK(s.serial.Start());
    const auto pattern = Fixture(); const auto lines = Lines(pattern);
    s.Receive(lines[0] + "\n");
    for (unsigned i = 0; i < 40; ++i) s.Tick();
    CHECK_EQ(s.transport->output, "BTPATTERN1 GET 1 1\nBTTEST1 START\n");
    s.Receive("BTTEST1 STARTED\nBTPATTERN1 POS 1 1 1 96 1 0 3 2\n");
    CHECK(!s.serial.StartPending());
    s.Receive(lines[1] + "\n"); s.Receive(lines[2] + "\n");
    CHECK(!s.serial.PlaybackView(s.now)->device_pattern);
    CHECK(s.serial.Stop());
    for (unsigned i = 0; i < 12; ++i) s.Tick();
    s.Receive("BTTEST1 STOPPED\nBTPATTERN1 POS 1 0 0 0 0 0 3 2\n");
    for (std::size_t i = 3; i < lines.size(); ++i) s.Receive(lines[i] + "\n");
    CHECK(s.serial.Stopped()); CHECK(!s.serial.PlaybackView(s.now)->position.valid);
    CHECK(s.serial.PlaybackView(s.now)->device_pattern);
    Same(*s.serial.PlaybackView(s.now)->device_pattern, pattern);
    CHECK_EQ(s.transport->output, "BTPATTERN1 GET 1 1\nBTTEST1 START\nBTTEST1 STOP\n");

    Session bad; bad.Ready(true); bad.Tick(); bad.Receive(lines[0] + "\n");
    const auto saved = bad.serial.PlaybackView(bad.now)->position;
    bad.Receive(std::string(300, 'X') + "\nBTTEST1 STARTED\n");
    CHECK(bad.serial.Connected()); CHECK(bad.serial.PlaybackView(bad.now)->pattern_failed);
    CHECK_EQ(bad.serial.PlaybackView(bad.now)->position.tick, saved.tick);
    for (const auto& line : lines) bad.Receive(line + "\n");
    CHECK(!bad.serial.PlaybackView(bad.now)->device_pattern);
}

TEST_CASE(PatternSnapshot_DisplayActualDeviceCellsTempoAndDimensionGuard)
{
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    const auto pattern = Fixture(); PatternSnapshotAssembler a; CHECK(a.Request(1, 0));
    for (const auto& line : Lines(pattern)) (void)Feed(a, line);
    std::optional<LivePlaybackView> live = LivePlaybackView{};
    live->telemetry_available = live->fresh = true; live->position = {true, true, 96, 1, 0, 3, 2};
    Framebuffer fb(640, 480); Tune tune; Pattern preview;
    RenderMainScreen(fb, tune, preview, live);
    const std::vector<Color> unavailable(fb.PixelData(), fb.PixelData() + 640 * 480);
    live->device_pattern = *a.Snapshot(); CHECK(ResolvePatternDisplay(live).device_pattern);
    RenderMainScreen(fb, tune, preview, live);
    const std::vector<Color> device(fb.PixelData(), fb.PixelData() + 640 * 480);
    CHECK(std::memcmp(device.data(), unavailable.data(), device.size() * sizeof(Color)) != 0);
    tune.title = "UNRELATED"; tune.tempo = 12; preview.channels.resize(8);
    for (auto& channel : preview.channels) { channel.rows.resize(32); for (auto& cell : channel.rows) cell = {72, 9}; }
    RenderMainScreen(fb, tune, preview, live);
    CHECK(std::memcmp(device.data(), fb.PixelData(), device.size() * sizeof(Color)) == 0);
    const auto differs = [&](int x, int y, int width, int height) {
        for (int row = y; row < y + height; ++row)
            for (int column = x; column < x + width; ++column)
                if (std::memcmp(&device[row * 640 + column], &fb.PixelData()[row * 640 + column], sizeof(Color)))
                    return true;
        return false;
    };
    live->device_pattern->pattern.cells[0][0] = {72, 12};
    RenderMainScreen(fb, tune, preview, live);
    CHECK(differs(30, 62, 53, 13)); // Actual device cell, not just title/status.
    live->device_pattern->pattern = pattern;
    live->device_pattern->tempo_hundredths = 12754;
    RenderMainScreen(fb, tune, preview, live);
    CHECK(!differs(242, 0, 53, 26)); // Hundredths retained but one-decimal display.
    live->device_pattern->tempo_hundredths = 12853;
    RenderMainScreen(fb, tune, preview, live);
    CHECK(differs(242, 0, 53, 26));
    live->position.rows = 4; CHECK(!ResolvePatternDisplay(live).device_pattern);
    live->device_pattern->tempo_hundredths = 12753;
    CHECK_EQ(live->device_pattern->tempo_hundredths, 12753); // Display precision never alters metadata.
}
