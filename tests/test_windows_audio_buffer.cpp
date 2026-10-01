#include "ui/windows/audio_buffer.h"
#include "ui/windows/teensy_identity.h"
#include "ui/windows/audio_discovery.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int main() try {
    // Missing/unreadable identities before and after the target must be local
    // failures. A readable unrelated endpoint still must not become a match.
    unsigned skipped = 0, matches = 0, visited = 0;
    for (int candidate : {0, 1, 2, 3, 4}) {
        const auto container = TryCaptureContainer([&] {
            ++visited;
            if (candidate == 0 || candidate == 4)
                throw WindowsAudioFailure("missing ContainerId", E_INVALIDARG);
            if (candidate == 1)
                throw WindowsAudioFailure("unreadable ContainerId", E_ACCESSDENIED);
            return candidate; // 2 is unrelated; 3 is the verified USB container.
        }, [&](const WindowsAudioFailure& error) {
            REQUIRE(error.code == E_INVALIDARG || error.code == E_ACCESSDENIED);
            ++skipped;
        });
        if (container && *container == 3) ++matches;
    }
    REQUIRE(visited == 5); REQUIRE(skipped == 3); REQUIRE(matches == 1);
    // Skipping an invalid candidate must not collapse two valid matches to one.
    matches = 0;
    for (int candidate : {0, 3, 3}) {
        const auto container = TryCaptureContainer([&] {
            if (!candidate) throw WindowsAudioFailure("missing ContainerId", E_INVALIDARG);
            return candidate;
        }, [](const WindowsAudioFailure&) {});
        if (container && *container == 3) ++matches;
    }
    REQUIRE(matches == 2);
    bool unrelated_failure_propagated = false;
    try {
        TryCaptureContainer([]() -> int { throw std::runtime_error("unexpected error"); },
                            [](const WindowsAudioFailure&) {});
    } catch (const std::runtime_error&) { unrelated_failure_propagated = true; }
    REQUIRE(unrelated_failure_propagated);
    WindowsAudioBuffer buffer;
    std::vector<float> in(WindowsAudioBuffer::Capacity * 4), out(882);
    for (unsigned i = 0; i < in.size()/2; ++i) { in[i*2] = .25f; in[i*2+1] = -.5f; }
    buffer.Render(out.data(), 441);
    REQUIRE(buffer.missing == 441);
    for (float v : out) REQUIRE(v == 0);
    buffer.Push(in.data(), 2000, false); buffer.Render(out.data(), 441);
    REQUIRE(buffer.rendered == 441);
    for (unsigned i = 0; i < 441; ++i) { REQUIRE(out[i*2] == .25f); REQUIRE(out[i*2+1] == -.5f); }
    buffer.Reset(); buffer.Push(nullptr, 2000, true); buffer.Render(out.data(), 441);
    for (float v : out) REQUIRE(v == 0);
    buffer.Reset(); buffer.Push(in.data(), WindowsAudioBuffer::Capacity*2, false);
    REQUIRE(buffer.Size() == WindowsAudioBuffer::Capacity);
    REQUIRE(buffer.dropped == WindowsAudioBuffer::Capacity);
    // Simulate 10 minutes with capture clocks +/-1000 ppm from render clock.
    for (double ratio : {.999, 1.001}) {
        WindowsAudioBuffer drift; double fraction = 0;
        drift.Push(in.data(), WindowsAudioBuffer::Target, false);
        for (unsigned tick = 0; tick < 60000; ++tick) {
            fraction += 441 * ratio; unsigned frames = unsigned(fraction); fraction -= frames;
            drift.Push(in.data(), frames, false); drift.Render(out.data(), 441);
            REQUIRE(drift.Size() < WindowsAudioBuffer::Capacity);
            REQUIRE(drift.Step() >= .997 && drift.Step() <= 1.003);
        }
        REQUIRE(drift.dropped == 0); REQUIRE(drift.missing == 0);
        REQUIRE(drift.Size() > 100 && drift.Size() < 3000);
    }
    REQUIRE(IsTeensyIdentity(L"USB\\VID_16C0&PID_048A\\17681760"));
    REQUIRE(IsTeensyIdentity(L"USB\\VID_16C0&PID_048A&MI_03\\anything"));
    REQUIRE(!IsTeensyIdentity(L"USB\\VID_16C0&PID_048A1\\other"));
    REQUIRE(!IsTeensyIdentity(L"BroTracker USB audio"));
    REQUIRE(!IsTeensyIdentity(L"Teensy MIDI/Audio"));
    std::cout << "PASS capture ContainerId failure isolation, stereo, silence, bounded overflow, starvation, +/-1000ppm clock drift, identity\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
