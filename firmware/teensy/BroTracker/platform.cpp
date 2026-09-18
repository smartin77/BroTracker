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
    SamplePlayer g_sample_player_a;
    SamplePlayer g_sample_player_b;
    AudioMixer4 g_audio_mixer;
    AudioOutputMQS g_audio_output_mqs;

    AudioConnection g_patch_source_to_mixer(g_audio_test_source, 0, g_audio_mixer, 0);
    AudioConnection g_patch_player_a_to_mixer(g_sample_player_a, 0, g_audio_mixer, 1);
    AudioConnection g_patch_player_b_to_mixer(g_sample_player_b, 0, g_audio_mixer, 2);
    AudioConnection g_patch_mixer_to_mqs(g_audio_mixer, 0, g_audio_output_mqs, 0);

    constexpr bool kEnableAudioTestTone = false;

    const char kTest1SamplePath[] = "Samples/test.wav";
    const char kTest2SamplePath[] = "Samples/test2.wav";
    const char kTest3SamplePath[] = "Samples/test3.wav";

    constexpr unsigned int kSimultaneousTestLoops = 4;

    enum class TestPlaybackState
    {
        Test1,
        Test2,
        Test3,
        SimultaneousTest,
        Done
    };

    TestPlaybackState g_test_playback_state = TestPlaybackState::Test1;
    unsigned int g_simultaneous_test_loop = 0;

    // Tracks whether the finished/underrun report has already been printed,
    // and whether streaming was ever observed playing (finished detection
    // relies on the public IsStreamPlaying() transitioning true -> false).
    bool g_stream_was_playing = false;
    bool g_stream_finished_reported = false;

    bool OpenAndPlayStream(SamplePlayer& player, const char* path)
    {
        WavStreamInfo info;

        if (!OpenWavPcmStream(path, info))
        {
            Serial.print("Test stream: failed to open ");
            Serial.println(path);
            return false;
        }

        Serial.print("Test stream: opened ");
        Serial.println(path);

        player.SetStream(std::move(info));
        player.PlayStream();

        Serial.print("Test stream: playback armed ");
        Serial.println(path);

        return true;
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

        #if defined(AUDIO_INTERFACE)
            Serial.println("USB AUDIO: ENABLED");
        #else
            Serial.println("USB AUDIO: DISABLED");
        #endif

        AudioMemory(8);

        g_audio_mixer.gain(0, kEnableAudioTestTone ? 1.0f : 0.0f);
        g_audio_mixer.gain(1, 1.0f);
        g_audio_mixer.gain(2, 1.0f);
    }

    void KernelInit()
    {
        DiagnosticLog("BroTracker starting");
        DiagnosticLog("Core initialized");

        g_scheduler.Initialize();
        DiagnosticLog("Scheduler initialized");

        DiagnosticLog("Playback engine initialized");
        DiagnosticLog("Storage initialized");
        OpenAndPlayStream(g_sample_player_a, kTest1SamplePath);
        DiagnosticLog("MIDI initialized");
        DiagnosticLog("BroTracker ready");
    }

    void KernelRun()
    {
        // Kernel main loop.
        // Audio block processing and Scheduler advancement happen in
        // AudioTestSource::update(), driven by the Teensy Audio Library.

        // SD refill for the streaming sample path; never called from
        // AudioStream::update() or any other realtime/audio callback.
        g_sample_player_a.ServiceStreaming();
        g_sample_player_b.ServiceStreaming();

        if (g_test_playback_state == TestPlaybackState::SimultaneousTest)
        {
            // Both streams must be fully primed before either one starts.
            if (g_sample_player_a.IsStreamPrimed() &&
                g_sample_player_b.IsStreamPrimed())
            {
                g_sample_player_a.StartStream();
                g_sample_player_b.StartStream();
            }
        }
        else if (g_sample_player_a.IsStreamPrimed())
        {
            g_sample_player_a.StartStream();
        }

        if (g_sample_player_a.IsStreamPlaying() || g_sample_player_b.IsStreamPlaying())
        {
            g_stream_was_playing = true;
        }
        else if (g_stream_was_playing && !g_stream_finished_reported)
        {
            g_stream_finished_reported = true;

            Serial.println("Test stream: finished");

            Serial.print("Test stream A: underrun count = ");
            Serial.println(g_sample_player_a.StreamUnderrunCount());

            Serial.print("Test stream B: underrun count = ");
            Serial.println(g_sample_player_b.StreamUnderrunCount());

            // ServiceStreaming() above already closed the SD files for the
            // finished stream, so the SD interface is clean at this point.
            DiagnosticBlink(3);

            if (g_test_playback_state == TestPlaybackState::Test1)
            {
                g_test_playback_state = TestPlaybackState::Test2;

                g_stream_was_playing = false;
                g_stream_finished_reported = false;

                OpenAndPlayStream(g_sample_player_a, kTest2SamplePath);
            }
            else if (g_test_playback_state == TestPlaybackState::Test2)
            {
                g_test_playback_state = TestPlaybackState::Test3;

                g_stream_was_playing = false;
                g_stream_finished_reported = false;

                OpenAndPlayStream(g_sample_player_a, kTest3SamplePath);
            }
            else if (g_test_playback_state == TestPlaybackState::Test3)
            {
                g_test_playback_state = TestPlaybackState::SimultaneousTest;

                g_stream_was_playing = false;
                g_stream_finished_reported = false;

                OpenAndPlayStream(g_sample_player_a, kTest2SamplePath);
                OpenAndPlayStream(g_sample_player_b, kTest3SamplePath);
            }
            else if (g_test_playback_state == TestPlaybackState::SimultaneousTest)
            {
                ++g_simultaneous_test_loop;

                if (g_simultaneous_test_loop < kSimultaneousTestLoops)
                {
                    g_stream_was_playing = false;
                    g_stream_finished_reported = false;

                    OpenAndPlayStream(g_sample_player_a, kTest2SamplePath);
                    OpenAndPlayStream(g_sample_player_b, kTest3SamplePath);
                }
                else
                {
                    g_test_playback_state = TestPlaybackState::Done;

                    Serial.println("Test stream: simultaneous playback finished");
                    DiagnosticBlink(3);
                }
            }
        }
    }
}
