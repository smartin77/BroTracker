#include "test_framework.h"
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
    void Same(const PatternTelemetry& a, const PatternTelemetry& b)
    {
        CHECK_EQ(a.running, b.running); CHECK_EQ(a.valid, b.valid);
        CHECK_EQ(a.tick, b.tick); CHECK_EQ(a.row, b.row); CHECK_EQ(a.loop, b.loop);
        CHECK_EQ(a.rows, b.rows); CHECK_EQ(a.channels, b.channels);
    }
    struct PositionTransport : SerialTransport
    {
        bool online = true;
        std::string input, output;
        bool Open(FILE*, const char*) override { return online; }
        void Close() override {}
        bool Healthy() override { return online; }
        int Read(char* data, unsigned size) override
        {
            const auto count = std::min<std::size_t>(size, input.size());
            std::memcpy(data, input.data(), count); input.erase(0, count);
            return static_cast<int>(count);
        }
        int Write(const char* data, unsigned size) override
        { output.append(data, size); return static_cast<int>(size); }
        const char* Error() const override { return "test disconnect"; }
    };
    struct Session
    {
        PositionTransport* port = new PositionTransport;
        BringUpSerial serial{nullptr, std::unique_ptr<SerialTransport>(port)};
        std::uint32_t now = 0;
        void Tick() { serial.Tick(now++); }
        void Receive(const std::string& line) { port->input += line; Tick(); }
        void Connect(const char* state = "PLAYING")
        {
            Tick(); CHECK_EQ(port->output, "BTTEST1 HELLO\n"); port->output.clear();
            Receive(std::string("BTTEST1 STATE ") + state + "\n");
            CHECK(serial.Connected());
        }
        LivePlaybackView View() const { return *serial.PlaybackView(now); }
    };
    constexpr char position[] = "BTPATTERN1 POS 1 1 1 1536 0 1 16 2\n";
}

// The injectable transport is used above. Satisfy the unused default constructor
// without depending on Windows discovery or Linux device availability.
std::unique_ptr<SerialTransport> MakeSerialTransport()
{ return std::make_unique<PositionTransport>(); }

TEST_CASE(PatternTelemetry_StrictFieldsOverflowAndUnchangedRejection)
{
    const PatternTelemetry sentinel{true, true, 1536, 0, 1, 16, 2};
    PatternTelemetry value;
    CHECK(ParsePatternTelemetry(position, std::strlen(position) - 1, value));
    Same(value, sentinel);
    constexpr char stopped[] = "BTPATTERN1 POS 1 0 0 0 0 0 1 8";
    CHECK(ParsePatternTelemetry(stopped, sizeof(stopped) - 1, value));
    const std::vector<std::string> invalid = {
        "BTPATTERN1 POS 2 1 1 1536 0 1 16 2", // Version.
        "BTPATTERN1 POS 1 2 1 1536 0 1 16 2",
        "BTPATTERN1 POS 1 1 2 1536 0 1 16 2",
        "BTPATTERN1 POS 1 0 1 1536 0 1 16 2", // Valid requires running.
        "BTPATTERN1 POS 1 1 0 1536 0 1 16 2", // Invalid is canonical zero.
        "BTPATTERN1 POS 1 1 1 1536 1 1 16 2", // Tick/row mismatch.
        "BTPATTERN1 POS 1 1 1 1536 0 2 16 2", // Tick/loop mismatch.
        "BTPATTERN1 POS 1 1 1 0 0 0 0 2",
        "BTPATTERN1 POS 1 1 1 0 0 0 17 2",
        "BTPATTERN1 POS 1 1 1 0 0 0 16 0",
        "BTPATTERN1 POS 1 1 1 0 0 0 16 9",
        "BTPATTERN1 POS 1 1 1 18446744073709551616 0 0 16 2",
        "BTPATTERN1 POS 1 1 1 0 0 18446744073709551616 16 2",
        "BTPATTERN1 POS 1 1 1 0 0 0 4294967297 2",
        "BTPATTERN1 POS 1 1 1 -1 0 0 16 2",
        "BTPATTERN1 POS 1 1 1 +0 0 0 16 2",
        "BTPATTERN1 POS 1 1 1 0 0 0 16", // Missing.
        "BTPATTERN1 POS 1 1 1 0 0 0 16 2 extra",
        "BTPATTERN1 POS 1 1 1 0 0 0 16 2 ",
        "BTPATTERN1 POS 1 1 1 0\t0 0 16 2",
        "BTPATTERN1 POS 1 1 1 0  0 0 16 2",
        "BTPATTERN1 POS 1 1 1 0\r 0 0 16 2",
        std::string(300, '0')
    };
    for (const auto& line : invalid)
    {
        value = sentinel;
        CHECK(!ParsePatternTelemetry(line.data(), line.size(), value));
        Same(value, sentinel);
    }
    value = sentinel;
    CHECK(!ParsePatternTelemetry(nullptr, 0, value)); Same(value, sentinel);
    // Maximum tick round-trips without narrowing, including derived row/loop.
    const auto absolute_row = UINT64_MAX / kTicksPerRow;
    const PatternTelemetry maximum{true, true, UINT64_MAX, absolute_row % 16, absolute_row / 16, 16, 8};
    char formatted[kPatternTelemetryLineCapacity]{}; std::size_t length = 0;
    CHECK(FormatPatternTelemetry(maximum, formatted, length));
    CHECK(length < sizeof(formatted)); CHECK_EQ(formatted[length - 1], '\n');
    CHECK(ParsePatternTelemetry(formatted, length - 1, value)); Same(value, maximum);
}

TEST_CASE(PatternTelemetry_CoalescesBackpressureWithoutInterleavingPartialLines)
{
    PatternTelemetrySender sender;
    sender.Offer({true, true, 0, 0, 0, 16, 2});
    std::size_t count;
    (void)sender.Data(count); // Not written yet: replaceable.
    sender.Offer({true, true, 96, 1, 0, 16, 2});
    auto data = sender.Data(count);
    std::string wire(data, 5); sender.Consume(5);
    CHECK(sender.Partial());
    for (std::uint64_t tick = 192; tick <= 1536; tick += 96)
        sender.Offer({true, true, tick, (tick / 96) % 16, tick / 1536, 16, 2});
    data = sender.Data(count); wire.append(data, count); sender.Consume(count);
    CHECK_EQ(wire, "BTPATTERN1 POS 1 1 1 96 1 0 16 2\n");
    data = sender.Data(count); wire.assign(data, count); sender.Consume(count);
    CHECK_EQ(wire, position); // No intermediate updates/backlog.
    (void)sender.Data(count); CHECK_EQ(count, 0);
    sender.Offer({true, true, 0, 0, 0, 16, 2}); sender.Clear();
    (void)sender.Data(count); CHECK_EQ(count, 0);
}

TEST_CASE(PatternTelemetry_FragmentationMalformedLinesLegacyAndFreshness)
{
    Session s; s.Connect();
    CHECK(!s.View().telemetry_available); // Legacy PLAYING works without position.
    for (const char c : std::string(position)) s.Receive(std::string(1, c));
    CHECK(s.View().fresh); CHECK(s.View().position.valid);
    CHECK_EQ(s.View().position.loop, 1);
    const auto saved = s.View().position;
    s.Receive("BTPATTERN1 POS 1 1 1 18446744073709551616 0 0 16 2\n");
    Same(s.View().position, saved);
    s.Receive(std::string(400, 'X') + "\nBTTEST1 STARTED\n");
    CHECK(s.serial.Connected()); CHECK(std::strstr(s.serial.Status(), "playing"));
    Same(s.View().position, saved);
    std::string nul = "BTPATTERN1 POS 1 1 1 0 0 0 16 2";
    nul += '\0'; nul += "garbage\n"; s.Receive(nul); Same(s.View().position, saved);
    s.Receive("BTPATTERN1 POS 1 1 1 96\r 1 0 16 2\n"); Same(s.View().position, saved);
    s.Receive("BTPATTERN1 POS 1 1 1 96 1 0 16 2\r\n");
    CHECK_EQ(s.View().position.row, 1);
    const auto received_at = s.now - 1;
    CHECK(s.serial.PlaybackView(received_at + 499)->fresh);
    const auto stale = s.serial.PlaybackView(received_at + 500);
    CHECK(stale->telemetry_available); CHECK(!stale->fresh); CHECK(!stale->position.valid);
    CHECK(!ResolvePatternDisplay(stale).playback_highlight);
    s.Receive(position); CHECK(s.View().fresh);
    s.Receive("BTTEST1 ERROR pattern fault=2\n");
    CHECK(!s.View().position.valid); CHECK(!s.View().fresh);
    s.Receive(position); CHECK(!s.View().fresh); // Telemetry cannot revive error.
}

TEST_CASE(PatternTelemetry_PendingRestartStopDisconnectAndAlreadyRunningReconnect)
{
    Session s; s.Connect(); s.Receive(position);
    CHECK(s.serial.Start()); CHECK(!s.View().position.valid);
    s.Receive(position); CHECK(!s.View().fresh); // Queued then transmitted barrier.
    s.Tick(); CHECK_EQ(s.port->output, "BTTEST1 START\n"); s.port->output.clear();
    s.Receive(position); CHECK(!s.View().fresh);
    s.Receive(std::string(400, 'X') + "\nBTTEST1 STARTED\nBTPATTERN1 POS 1 1 1 0 0 0 16 2\n");
    CHECK(!s.serial.StartPending()); // Overlong telemetry does not swallow ACK.
    CHECK(s.View().fresh); CHECK_EQ(s.View().position.tick, 0);
    CHECK(s.serial.Stop()); CHECK(!s.View().position.valid);
    s.Receive(position); CHECK(!s.View().fresh);
    s.Tick(); CHECK_EQ(s.port->output, "BTTEST1 STOP\n"); s.port->output.clear();
    s.Receive("BTTEST1 STOPPED\nBTPATTERN1 POS 1 0 0 0 0 0 16 2\n");
    CHECK(s.serial.Stopped()); CHECK(s.View().fresh); CHECK(!s.View().position.valid);
    s.port->online = false; s.Tick(); CHECK(!s.serial.PlaybackView(s.now));
    s.port->online = true; s.now = 1000; s.Tick();
    CHECK_EQ(s.port->output, "BTTEST1 HELLO\n"); s.port->output.clear();
    s.Receive(std::string("BTTEST1 STATE PLAYING\n") + position); s.Tick();
    CHECK(s.View().fresh); CHECK_EQ(s.View().position.loop, 1);
    CHECK_EQ(s.port->output, ""); // Reconnect restores position, NEVER sends START.
    // Unsigned timestamp wrap preserves the timeout contract.
    s.now = UINT32_MAX - 100; s.Receive(position);
    CHECK(s.serial.PlaybackView(100)->fresh);
    CHECK(!s.serial.PlaybackView(500)->fresh);
}

TEST_CASE(PatternDisplay_ReportedRowUnknownContentsAndPreviewRemainDistinct)
{
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Tune tune; tune.title = "HOST PREVIEW";
    Pattern preview; preview.length = 32;
    Framebuffer framebuffer(640, 480);
    RenderMainScreen(framebuffer, tune, preview);
    const std::vector<Color> original(framebuffer.PixelData(), framebuffer.PixelData() + 640 * 480);
    CHECK(!ResolvePatternDisplay(std::nullopt).device_mode);
    const std::optional<LivePlaybackView> live = LivePlaybackView{true, true,
        {true, true, 96, 1, 0, 16, 2}};
    const auto display = ResolvePatternDisplay(live);
    CHECK(display.device_mode); CHECK(display.playback_highlight);
    CHECK_EQ(display.row, 1); CHECK_EQ(display.rows, 16); CHECK_EQ(display.channels, 2);
    RenderMainScreen(framebuffer, tune, preview, live);
    const std::vector<Color> device(framebuffer.PixelData(), framebuffer.PixelData() + 640 * 480);
    Tune other_tune; other_tune.title = "DIFFERENT HOST TUNE"; other_tune.tempo = 99;
    Pattern other_preview; other_preview.number = 9; other_preview.length = 32;
    other_preview.channels.resize(8);
    for (auto& channel : other_preview.channels)
    {
        channel.rows.resize(32);
        for (auto& cell : channel.rows) cell = {60, 7};
    }
    RenderMainScreen(framebuffer, other_tune, other_preview, live);
    CHECK(std::memcmp(device.data(), framebuffer.PixelData(), device.size() * sizeof(Color)) == 0);
    // Reported row 1 receives teal playback background; fixed edit row 12 does
    // not move to row 1 or masquerade as firmware content. No data on row 16+.
    const auto pixel = [&](int x, int y) { return framebuffer.PixelData()[y * 640 + x]; };
    CHECK_EQ(pixel(32, 62 + 13).g, 64);
    CHECK_EQ(pixel(32, 62 + 12 * 13).g, 16);
    CHECK_EQ(pixel(32, 62 + 16 * 13).g, 16);
    auto stale = live; stale->fresh = false;
    CHECK(!ResolvePatternDisplay(stale).playback_highlight);
    RenderMainScreen(framebuffer, tune, preview, stale);
    CHECK_EQ(pixel(32, 62 + 13).g, 16);
    std::optional<LivePlaybackView> legacy = LivePlaybackView{};
    CHECK(ResolvePatternDisplay(legacy).device_mode);
    CHECK(!ResolvePatternDisplay(legacy).playback_highlight);
    RenderMainScreen(framebuffer, tune, preview); // Standalone/default remains exact.
    CHECK(std::memcmp(original.data(), framebuffer.PixelData(), original.size() * sizeof(Color)) == 0);
}
