#include "platform.h"

#include "audio_test_source.h"
#include "diagnostics.h"
#include "sample_player.h"
#include "wav_loader.h"

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <scheduler.h>
#include <utility>

namespace BroTracker
{
namespace
{
    // Minimal native audio path used to verify the Teensy audio processing
    // boundary on real hardware:
    //   AudioTestSource, SamplePlayer -> AudioMixer4 -> AudioOutputMQS
    // A USB Audio path is attached alongside it when the selected Teensy
    // USB type includes an audio interface.
    Scheduler g_scheduler;
    AudioTestSource g_audio_test_source(g_scheduler);
    SamplePlayer g_sample_player;
    AudioMixer4 g_audio_mixer;
    AudioOutputMQS g_audio_output_mqs;

    AudioConnection g_patch_source_to_mixer(g_audio_test_source, 0, g_audio_mixer, 0);
    AudioConnection g_patch_player_to_mixer(g_sample_player, 0, g_audio_mixer, 1);
    AudioConnection g_patch_mixer_to_mqs(g_audio_mixer, 0, g_audio_output_mqs, 0);

    const char kTestSamplePath[] = "Samples/test.wav";

    // Tracks whether the finished/underrun report has already been printed,
    // and whether streaming was ever observed playing (finished detection
    // relies on the public IsStreamPlaying() transitioning true -> false).
    bool g_stream_was_playing = false;
    bool g_stream_finished_reported = false;

    void OpenTestStream()
    {
        WavStreamInfo info;
        if (!OpenWavPcmStream(kTestSamplePath, info))
        {
            Serial.println("Test stream: failed to open Samples/test.wav");
            return;
        }

        Serial.println("Test stream: opened successfully");
        g_sample_player.SetStream(std::move(info));
        g_sample_player.PlayStream();
        Serial.println("Test stream: playback armed");
    }

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
        g_audio_mixer.gain(1, 1.0f);
    }

    void KernelInit()
    {
        DiagnosticLog("BroTracker starting");
        DiagnosticLog("Core initialized");

        g_scheduler.Initialize();
        DiagnosticLog("Scheduler initialized");

        DiagnosticLog("Playback engine initialized");
        DiagnosticLog("Storage initialized");
        OpenTestStream();
        DiagnosticLog("MIDI initialized");
        DiagnosticLog("BroTracker ready");

        DiagnosticBlink(3);
    }

    void KernelRun()
    {
        // Kernel main loop.
        // Audio block processing and Scheduler advancement happen in
        // AudioTestSource::update(), driven by the Teensy Audio Library.

        // SD refill for the streaming sample path; never called from
        // AudioStream::update() or any other realtime/audio callback.
        g_sample_player.ServiceStreaming();

        if (g_sample_player.IsStreamPlaying())
        {
            g_stream_was_playing = true;
        }
        else if (g_stream_was_playing && !g_stream_finished_reported)
        {
            g_stream_finished_reported = true;
            Serial.println("Test stream: finished");
            Serial.print("Test stream: underrun count = ");
            Serial.println(g_sample_player.StreamUnderrunCount());
        }
    }
}
