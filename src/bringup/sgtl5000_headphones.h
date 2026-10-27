#pragma once
#include <cstdint>

namespace BroTracker {
// Boot/main-context only. No board-presence claim, heap, retry or audio-ISR I/O.
// Codec/Bus are PJRC AudioControlSGTL5000/Wire, injectable for host tests.
template<class Codec, class Bus>
const char* InitializeShieldHeadphones(Codec& codec, Bus& bus) {
    codec.setAddress(0); // LOW, fixed 7-bit 0x0A on Rev D2.
    if (!codec.enable()) return "shield-enable-failed";
    const auto fail = [&](const char* reason) {
        (void)codec.muteHeadphone(); (void)codec.muteLineout(); // best effort
        return reason;
    };
    if (!codec.muteLineout()) return fail("shield-line-mute-failed");
    // enable() requests SELECT_HP=0/DAC. Do not call 1.62's reversed selector.
    if (!codec.volume(0.80f)) return fail("shield-volume-failed");
    if (!codec.unmuteHeadphone()) return fail("shield-unmute-failed");
    struct Expected {std::uint16_t reg, mask, value;};
    constexpr Expected expected[] = {
        {0x0024,0x0150,0x0100}, // DAC route, HP unmuted, line-out muted
        {0x0022,0x7F7F,0x1919}, // volume 0.80 -> -0.5dB on both channels
        {0x0010,0xFFFF,0x3C3C}, // DAC 0dB
        {0x000E,0x000C,0x0000}, // DAC channels unmuted
        {0x000A,0x0030,0x0010}, // I2S -> DAC, not DAP/ADC
        {0x0004,0x003F,0x0004}, // 44.1kHz, 256Fs MCLK
        {0x0006,0x01FF,0x0030}  // slave, 16-bit I2S in 64Fs frames
    };
    for (const auto& item : expected) {
        bus.beginTransmission(std::uint8_t(0x0A));
        auto queued = bus.write(std::uint8_t(item.reg >> 8));
        queued += bus.write(std::uint8_t(item.reg));
        const auto status = bus.endTransmission(queued == 2 ? false : true);
        if (queued != 2 || status != 0) return fail("shield-readback-failed");
        if (bus.requestFrom(std::uint8_t(0x0A),std::uint8_t(2)) != 2 || bus.available() != 2)
            return fail("shield-readback-failed");
        const int high = bus.read(), low = bus.read();
        if (high < 0 || low < 0) return fail("shield-readback-failed");
        const auto value = (unsigned(high) << 8) | unsigned(low);
        if ((value & item.mask) != item.value) return fail("shield-config-mismatch");
    }
    return nullptr; // Checked control/readback only, never verified analog output.
}
}
