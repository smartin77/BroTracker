#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
constexpr float kAmplitude = 0.20f;
constexpr float kRampMs = 10.0f;
constexpr uint32_t kReleaseAllowanceMs = 12; // Includes envelope's 8-sample rounding.
constexpr uint32_t kBurstMs = 250;
constexpr uint32_t kShortSilenceMs = 500;
constexpr uint32_t kLongSilenceMs = 1500;

AudioSynthWaveformSine leftTone, rightTone;
AudioEffectEnvelope leftEnvelope, rightEnvelope;
// Construct hardware before AudioMemory so I2S owns updates (no timer fallback).
AudioOutputI2S output;
AudioAnalyzePeak leftPeak, rightPeak;
AudioControlSGTL5000 codec;
bool codecReady = false;
char startupInfo[1024]{};
AudioConnection leftSource(leftTone, leftEnvelope);
AudioConnection rightSource(rightTone, rightEnvelope);
AudioConnection leftOutput(leftEnvelope, 0, output, 0);
AudioConnection rightOutput(rightEnvelope, 0, output, 1);
AudioConnection leftProbe(leftEnvelope, 0, leftPeak, 0);
AudioConnection rightProbe(rightEnvelope, 0, rightPeak, 0);

// Retain the complete bounded diagnostic even when Serial connects late.
char diagnosticInfo[8192]{};
size_t diagnosticSize = 0, diagnosticOffset = 0, startupOffset = 0;
bool diagnosticOverflow = false;
void AppendDiagnostic(const char* format, ...) {
    if (diagnosticOverflow) return;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(diagnosticInfo + diagnosticSize,
        sizeof(diagnosticInfo) - diagnosticSize, format, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(diagnosticInfo) - diagnosticSize) {
        diagnosticInfo[diagnosticSize] = 0;
        diagnosticOverflow = true;
        return;
    }
    diagnosticSize += static_cast<size_t>(n);
}

struct RegisterValue { bool valid = false; uint16_t value = 0; };
RegisterValue ReadRegister(uint16_t reg) {
    // Address-pointer selection only: no register value/configuration write.
    Wire.beginTransmission(uint8_t(0x0A));
    size_t queued = Wire.write(uint8_t(reg >> 8));
    queued += Wire.write(uint8_t(reg));
    const uint8_t status = Wire.endTransmission(queued == 2 ? false : true);
    if (queued != 2 || status != 0) {
        AppendDiagnostic("REG 0x%04X ERROR pointer-transfer queued=%u status=%u (no value)\n",
            unsigned(reg), unsigned(queued), unsigned(status));
        return {};
    }
    const unsigned received = Wire.requestFrom(uint8_t(0x0A), uint8_t(2));
    if (received != 2 || Wire.available() != 2) {
        AppendDiagnostic("REG 0x%04X ERROR data-transfer received=%u available=%d (no value)\n",
            unsigned(reg), received, Wire.available());
        while (Wire.available()) (void)Wire.read();
        return {};
    }
    const int high = Wire.read(), low = Wire.read();
    if (high < 0 || low < 0) {
        AppendDiagnostic("REG 0x%04X ERROR missing-byte (no value)\n", unsigned(reg));
        return {};
    }
    const uint16_t value = (unsigned(high) << 8) | unsigned(low);
    AppendDiagnostic("REG 0x%04X = 0x%04X\n", unsigned(reg), unsigned(value));
    return {true, value};
}

void PrintGain(const char* label, unsigned raw, bool headphone) {
    if (!headphone && raw < 0x3C) {
        AppendDiagnostic("%s raw=0x%02X RESERVED\n", label, raw); return;
    }
    if (!headphone && raw >= 0xFC) {
        AppendDiagnostic("%s raw=0x%02X MUTED\n", label, raw); return;
    }
    // Headphone steps are 0.5dB; DAC nominal 0.5017dB, rounded to tenths here.
    const int tenths = headphone ? 120 - int(raw) * 5 :
        -int(((raw - 0x3C) * 5017u + 500u) / 1000u);
    const unsigned magnitude = tenths < 0 ? unsigned(-tenths) : unsigned(tenths);
    AppendDiagnostic("%s raw=0x%02X gain=%s%u.%udB\n", label, raw,
        tenths < 0 ? "-" : "+", magnitude / 10, magnitude % 10);
}

void RegisterDiagnostics() {
    AppendDiagnostic("DIAG BEGIN: read-only registers once; PCM probes only, analog UNVERIFIED\n");
    for (uint16_t reg : {0x0000, 0x0002, 0x0004, 0x0006, 0x000A, 0x000E,
                         0x0010, 0x0022, 0x0024, 0x0026, 0x0028, 0x002C,
                         0x002E, 0x0030, 0x0034, 0x0036, 0x003C, 0x0100}) {
        const auto read = ReadRegister(reg);
        if (!read.valid) {
            if (reg == 0x0024) AppendDiagnostic("HP route=UNKNOWN SELECT_HP=UNKNOWN MUTE_HP=UNKNOWN MUTE_LO=UNKNOWN (read failed)\n");
            continue; // Never decode an error as register zero.
        }
        const auto value = read.value;
        if (reg == 0x0000)
            AppendDiagnostic("ID part=0x%02X revision=0x%02X; not PCB identity\n", value >> 8, value & 255);
        if (reg == 0x0024)
            AppendDiagnostic("HP SELECT_HP(bit6)=%u route=%s MUTE_HP(bit4)=%u MUTE_LO(bit8)=%u\n",
                unsigned((value >> 6) & 1), value & 0x40 ? "LINEIN" : "DAC",
                unsigned((value >> 4) & 1), unsigned((value >> 8) & 1));
        if (reg == 0x000E)
            AppendDiagnostic("DAC mute L=%u R=%u\n", unsigned((value >> 2) & 1), unsigned((value >> 3) & 1));
        if (reg == 0x0022 || reg == 0x0010) {
            const bool hp = reg == 0x0022;
            PrintGain(hp ? "HP L" : "DAC L", value & (hp ? 127 : 255), hp);
            PrintGain(hp ? "HP R" : "DAC R", (value >> 8) & (hp ? 127 : 255), hp);
        }
        if (reg == 0x0036)
            AppendDiagnostic("STATUS LRSHORT=%u CSHORT=%u PLL_LOCK=%u (PLL lock not required in this mode)\n",
                unsigned((value >> 9) & 1), unsigned((value >> 8) & 1), unsigned((value >> 4) & 1));
    }
    AppendDiagnostic("REG END; peaks: 3 cycles, 50..200ms per phase, normalized peak in millionths\n");
}

enum class Phase { Left, ShortSilence, Right, LongSilence };
Phase phase = Phase::LongSilence;
uint32_t phaseStarted = 0;
unsigned diagnosticCycle = 0;
bool peakWindow = false, phaseReported = false;
unsigned peakSamples[2]{}, peakMillionths[2]{};

void SamplePeaks() {
    if (!diagnosticCycle || diagnosticCycle > 3) return;
    const uint32_t elapsed = millis() - phaseStarted;
    if (elapsed >= 50 && !peakWindow && !phaseReported) {
        // Discard accumulated attack/release/previous-phase samples first.
        AudioNoInterrupts();
        (void)leftPeak.available(); (void)leftPeak.read();
        (void)rightPeak.available(); (void)rightPeak.read();
        AudioInterrupts();
        peakSamples[0] = peakSamples[1] = peakMillionths[0] = peakMillionths[1] = 0;
        peakWindow = true;
    }
    if (!peakWindow) return;
    AudioAnalyzePeak* probes[] = {&leftPeak, &rightPeak};
    for (unsigned i = 0; i < 2; ++i) {
        AudioNoInterrupts();
        const bool available = probes[i]->available();
        const float peak = available ? probes[i]->read() : 0;
        AudioInterrupts();
        if (available) {
            ++peakSamples[i];
            const unsigned scaled = unsigned(peak * 1000000.0f + 0.5f);
            if (scaled > peakMillionths[i]) peakMillionths[i] = scaled;
        }
    }
    if (elapsed < 200) return;
    const char* label = phase == Phase::Left ? "LEFT_ACTIVE" : phase == Phase::Right ? "RIGHT_ACTIVE" :
        phase == Phase::ShortSilence ? "SILENT_AFTER_LEFT" : "SILENT_AFTER_RIGHT";
    AppendDiagnostic("PEAK cycle=%u phase=%s window=50..%lums L=%u reads=%u R=%u reads=%u\n",
        diagnosticCycle, label, static_cast<unsigned long>(elapsed),
        peakMillionths[0], peakSamples[0], peakMillionths[1], peakSamples[1]);
    AppendDiagnostic("  reads=0 means NO_BLOCKS, not measured zero; I2S zero-fills missing channels\n");
    peakWindow = false; phaseReported = true;
    if (phase == Phase::LongSilence && diagnosticCycle == 3) {
        ++diagnosticCycle;
        AppendDiagnostic("DIAG COMPLETE: 3 cycles; pool high-water=%u/16; analog UNVERIFIED\n", unsigned(AudioMemoryUsageMax()));
    }
}

void ConfigureEnvelope(AudioEffectEnvelope& envelope) {
    envelope.delay(0);
    envelope.attack(kRampMs);
    envelope.hold(0);
    envelope.decay(0);
    envelope.sustain(1.0f);
    envelope.release(kRampMs);
}

void ServiceStartupInfo() {
    if (!Serial) return;
    const int available = Serial.availableForWrite();
    if (available <= 0) return;
    const bool startup = startupOffset < std::strlen(startupInfo);
    size_t& offset = startup ? startupOffset : diagnosticOffset;
    const char* text = startup ? startupInfo : diagnosticInfo;
    size_t count = (startup ? std::strlen(startupInfo) : diagnosticSize) - offset;
    if (count > 64) count = 64;
    if (count > static_cast<size_t>(available)) count = static_cast<size_t>(available);
    offset += Serial.write(reinterpret_cast<const uint8_t*>(text + offset), count);
}
} // namespace

void setup() {
    Serial.begin(115200);
    AudioMemory(16);
    AudioNoInterrupts();
    leftTone.frequency(440.0f);
    rightTone.frequency(880.0f);
    leftTone.amplitude(kAmplitude);
    rightTone.amplitude(kAmplitude);
    ConfigureEnvelope(leftEnvelope);
    ConfigureEnvelope(rightEnvelope);
    AudioInterrupts();
    // All codec/I2C work is boot-only foreground work, with audio IRQs enabled.
    codec.setAddress(LOW); // Rev D2 fixed 7-bit address 0x0A; not a presence scan.
    const bool enabled = codec.enable();
    bool lineMuted = false, volumeSet = false, headphoneUnmuted = false;
    if (enabled) {
        lineMuted = codec.muteLineout();
        // enable() requests SELECT_HP=0 (DAC). The installed 1.62 selector
        // reverses this bit; retain enable's route and report actual readback.
        volumeSet = codec.volume(0.50f); // Headphones only; does not set line level.
        headphoneUnmuted = codec.unmuteHeadphone();
    }
    codecReady = enabled && lineMuted && volumeSet && headphoneUnmuted;
    if (enabled && !codecReady) {
        // Best effort only; the graph remains silent regardless of I2C mute delivery.
        (void)codec.muteHeadphone();
        (void)codec.muteLineout();
    }
    std::snprintf(startupInfo, sizeof(startupInfo),
        "SGTL5000 Audio Shield isolated Teensy 4.1 test (Rev D2 priority)\n"
        "MCLK=23 BCLK=21 LRCLK=20 TX=7 SDA=18 SCL=19; 3.3V/GND; RX=8 unused\n"
        "enable()=%s (limited API check, not full initialization verification)\n"
        "muteLineout=%s volume(0.50)=%s unmuteHeadphone=%s; route determined by register readback\n"
        "%s\n"
        "Headphone test; line-out requested muted. L:440Hz/250ms, silence/500ms, R:880Hz/250ms, silence/1500ms\n"
        "10ms ramps, amplitude=0.20; silence includes 12ms release allowance\n"
        "I2C 0x0A does not identify a board or confirm clocks/analog audio. Listening required.\n"
        "No BTX/USB audio/SD/input/optional memory. No automatic initialization retry.\n",
        enabled ? "true" : "false",
        !enabled ? "not attempted" : lineMuted ? "true" : "false",
        !enabled ? "not attempted" : volumeSet ? "true" : "false",
        !enabled ? "not attempted" : headphoneUnmuted ? "true" : "false",
        codecReady ? "Control calls returned true; tone sequence enabled; audio UNVERIFIED" :
                     "Control initialization/configuration failed; tone sequence disabled");
    // Start silent; no wait for Serial or host application.
    phaseStarted = millis();
    RegisterDiagnostics();
    if (!codecReady) AppendDiagnostic("DIAG COMPLETE: codec checks failed; no peak cycles\n");
}

void loop() {
    ServiceStartupInfo();
    if (!codecReady) return;
    const uint32_t now = millis();
    SamplePeaks();
    const uint32_t duration = (phase == Phase::Left || phase == Phase::Right) ? kBurstMs :
        kReleaseAllowanceMs + (phase == Phase::ShortSilence ? kShortSilenceMs : kLongSilenceMs);
    if (static_cast<uint32_t>(now - phaseStarted) < duration) return;
    phaseStarted = now; // At most one transition per loop; never catch up with a burst.
    peakWindow = false; phaseReported = false;
    switch (phase) {
    case Phase::LongSilence:
        if (diagnosticCycle < 3) ++diagnosticCycle;
        leftEnvelope.noteOn();
        phase = Phase::Left;
        break;
    case Phase::Left:
        leftEnvelope.noteOff();
        phase = Phase::ShortSilence;
        break;
    case Phase::ShortSilence:
        rightEnvelope.noteOn();
        phase = Phase::Right;
        break;
    case Phase::Right:
        rightEnvelope.noteOff();
        phase = Phase::LongSilence;
        break;
    }
}
