#include "platform.h"

#include "audio_test_source.h"
#include "diagnostics.h"

#include <Arduino.h>
#include <Audio.h>
#include <scheduler.h>

namespace BroTracker
{
namespace
{
    // Minimal native audio path used to verify the Teensy audio processing
    // boundary on real hardware:
    //   AudioTestSource -> AudioMixer4 -> AudioOutputMQS
    // A USB Audio path is attached alongside it when the selected Teensy
    // USB type includes an audio interface.
    Scheduler g_scheduler;
    AudioTestSource g_audio_test_source(g_scheduler);
    AudioMixer4 g_audio_mixer;
    AudioOutputMQS g_audio_output_mqs;

    AudioConnection g_patch_source_to_mixer(g_audio_test_source, 0, g_audio_mixer, 0);
    AudioConnection g_patch_mixer_to_mqs(g_audio_mixer, 0, g_audio_output_mqs, 0);

#if defined(AUDIO_INTERFACE)
    AudioOutputUSB g_audio_output_usb;
    AudioConnection g_patch_mixer_to_usb_left(g_audio_mixer, 0, g_audio_output_usb, 0);
    AudioConnection g_patch_mixer_to_usb_right(g_audio_mixer, 0, g_audio_output_usb, 1);
#endif
}

    void PlatformInit()
    {
        Serial.begin(115200);

        while (!Serial && millis() < 3000)
        {
        }

        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);
        DiagnosticsInitialize();

        AudioMemory(8);
        g_audio_mixer.gain(0, 1.0f);
    }

    void KernelInit()
    {
        DiagnosticLog("BroTracker starting");
        DiagnosticLog("Core initialized");

        g_scheduler.Initialize();
        DiagnosticLog("Scheduler initialized");

        DiagnosticLog("Playback engine initialized");
        DiagnosticLog("Storage initialized");
        DiagnosticLog("MIDI initialized");
        DiagnosticLog("BroTracker ready");

        DiagnosticBlink(3);
    }

    void KernelRun()
    {
        // Kernel main loop.
        // Audio block processing and Scheduler advancement happen in
        // AudioTestSource::update(), driven by the Teensy Audio Library.
    }
}