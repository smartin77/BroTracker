#include <Arduino.h>
#include <Audio.h>

namespace {
constexpr float kAmplitude = 0.08f;
constexpr float kRampMs = 10.0f;
constexpr uint32_t kReleaseAllowanceMs = 12; // Includes envelope's 8-sample rounding.
constexpr uint32_t kBurstMs = 250;
constexpr uint32_t kShortSilenceMs = 500;
constexpr uint32_t kLongSilenceMs = 1500;

AudioSynthWaveformSine leftTone, rightTone;
AudioEffectEnvelope leftEnvelope, rightEnvelope;
AudioOutputPT8211 output;
AudioConnection leftSource(leftTone, leftEnvelope);
AudioConnection rightSource(rightTone, rightEnvelope);
AudioConnection leftOutput(leftEnvelope, 0, output, 0);
AudioConnection rightOutput(rightEnvelope, 0, output, 1);

enum class Phase { Left, ShortSilence, Right, LongSilence };
Phase phase = Phase::LongSilence;
uint32_t phaseStarted = 0;

void ConfigureEnvelope(AudioEffectEnvelope& envelope) {
    envelope.delay(0);
    envelope.attack(kRampMs);
    envelope.hold(0);
    envelope.decay(0);
    envelope.sustain(1.0f);
    envelope.release(kRampMs);
}

void ServiceStartupInfo() {
    static constexpr char text[] =
        "PT8211 isolated Teensy 4.1 smoke test\n"
        "BCK=21 WS=20 DIN=7; 3.3V and common GND\n"
        "L:440Hz/250ms, silence/500ms, R:880Hz/250ms, silence/1500ms; 10ms ramps; amplitude=0.08\n"
        "Module presence UNKNOWN: no digital status response. No BTX/SD/USB audio.\n";
    static size_t offset = 0;
    if (!Serial || offset == sizeof(text) - 1) return;
    const int available = Serial.availableForWrite();
    if (available <= 0) return;
    size_t count = sizeof(text) - 1 - offset;
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
    // Start silent; sequence runs independently of serial connection/module presence.
    phaseStarted = millis();
}

void loop() {
    ServiceStartupInfo();
    const uint32_t now = millis();
    const uint32_t duration = (phase == Phase::Left || phase == Phase::Right) ? kBurstMs :
        kReleaseAllowanceMs + (phase == Phase::ShortSilence ? kShortSilenceMs : kLongSilenceMs);
    if (static_cast<uint32_t>(now - phaseStarted) < duration) return;
    phaseStarted = now; // At most one transition per loop; never catch up with a burst.
    switch (phase) {
    case Phase::LongSilence:
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
