#include "platform.h"
#include "usb_tx_trace.h"

#include "audio_test_source.h"
#include "diagnostics.h"
#include "sample_player.h"
#include "wav_loader.h"

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <scheduler.h>
#include <utility>
#include <cstring>

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

    constexpr float kTest1SourceBpm = 137.915757f;
    constexpr float kTargetBpm = 155.0f;
    constexpr float kTest1PlaybackRate =
        kTargetBpm / kTest1SourceBpm;

    constexpr unsigned int kSimultaneousTestLoops = 4;

    enum class TestPlaybackState
    {
        Test1,
        Test2,
        Test3,
        SimultaneousTest,
        Done,
        Idle,
        Error
    };

    TestPlaybackState g_test_playback_state = TestPlaybackState::Idle;
    unsigned int g_simultaneous_test_loop = 0;

    // Tracks whether the finished/underrun report has already been printed,
    // and whether streaming was ever observed playing (finished detection
    // relies on the public IsStreamPlaying() transitioning true -> false).
    bool g_stream_was_playing = false;
    bool g_stream_finished_reported = false;
    bool g_next_stream_open_attempted = false;
    bool g_next_stream_primed_reported = false;
    bool g_simultaneous_next_open_attempted = false;
    bool g_simultaneous_next_primed_reported = false;

    bool OpenAndPlayStream(
        SamplePlayer& player,
        const char* path,
        float playback_rate = 1.0f)
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

        if (!player.SetStreamPlaybackRate(playback_rate))
        {
            Serial.print("Test stream: failed to set playback rate for ");
            Serial.println(path);
            return false;
        }

        player.PlayStream();

        Serial.print("Test stream: playback armed ");
        Serial.println(path);

        return true;
    }

    // TEMPORARY USB CDC bring-up exchange, not the tracker protocol.
    // All parsing, USB writes and SD operations run in KernelRun/KernelInit.
    void StopSequence()
    {
        g_sample_player_a.StopStreams();
        g_sample_player_b.StopStreams();
        g_test_playback_state = TestPlaybackState::Idle;
        g_stream_was_playing = false;
        g_stream_finished_reported = false;
    }

    void FailSequence()
    {
#ifdef BROTRACKER_USB_TX_TRACE
        UsbTraceFreeze();
#endif
        StopSequence();
        g_test_playback_state = TestPlaybackState::Error;
        Serial.println("BTTEST1 ERROR playback");
    }

    bool StartSequence()
    {
        StopSequence();
        g_simultaneous_test_loop = 0;
        g_next_stream_open_attempted = false;
        g_next_stream_primed_reported = false;
        g_simultaneous_next_open_attempted = false;
        g_simultaneous_next_primed_reported = false;
        g_test_playback_state = TestPlaybackState::Test1;
        if (!OpenAndPlayStream(g_sample_player_a, kTest1SamplePath, kTest1PlaybackRate))
        {
            FailSequence();
            return false;
        }
        return true;
    }

    void ReportSequence()
    {
        switch (g_test_playback_state)
        {
        case TestPlaybackState::Done: Serial.println("BTTEST1 STATE DONE"); break;
        case TestPlaybackState::Idle: Serial.println("BTTEST1 STATE IDLE"); break;
        case TestPlaybackState::Error: Serial.println("BTTEST1 STATE ERROR"); break;
        default: Serial.println("BTTEST1 STATE PLAYING"); break;
        }
    }

    void ServiceBringUpSerial()
    {
        static char line[64];
        static unsigned int used = 0;
        static bool overflow = false;
        if (!Serial)
        {
            used = 0;
            overflow = false;
            return;
        }
        // Bounded work leaves time for SD refill, even with noisy input.
        for (unsigned int budget = 0; budget < 128; ++budget)
        {
            const int raw = ReadStartupSerialByte();
            if (raw < 0) break;
            const char c = static_cast<char>(raw);
            if (c == '\r') continue;
            if (c != '\n')
            {
                if (used + 1 < sizeof(line)) line[used++] = c;
                else overflow = true;
                continue;
            }
            line[used] = '\0';
            if (overflow) Serial.println("BTTEST1 ERROR line-too-long");
            else if (std::strcmp(line, "BTTEST1 HELLO") == 0 ||
                     std::strcmp(line, "BTTEST1 STATUS") == 0) {
#ifdef BROTRACKER_USB_TX_TRACE
                if(std::strcmp(line,"BTTEST1 HELLO")==0)UsbTraceReplay();
#endif
                ReportSequence();
            }
            else if (std::strcmp(line, "BTTEST1 START") == 0)
            {
                if (StartSequence()) {
#ifdef BROTRACKER_USB_TX_TRACE
                    UsbTraceStart();
#endif
                    Serial.println("BTTEST1 STARTED");
                }
            }
            else if (std::strcmp(line, "BTTEST1 STOP") == 0)
            {
#ifdef BROTRACKER_USB_TX_TRACE
                UsbTraceFreeze();
#endif
                StopSequence();
                Serial.println("BTTEST1 STOPPED");
            }
            else Serial.println("BTTEST1 ERROR command");
            used = 0;
            overflow = false;
        }
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
        Serial.print("Test stream: Test1 source BPM = ");
        Serial.print(kTest1SourceBpm, 6);
        Serial.print(", target BPM = ");
        Serial.print(kTargetBpm, 1);
        Serial.print(", playback rate = ");
        Serial.println(kTest1PlaybackRate, 6);
        // Boot silently; only an explicit BTTEST1 START arms the sequence.
        StopSequence();
        DiagnosticLog("MIDI initialized");
        DiagnosticLog("BroTracker ready");
    }

    void KernelRun()
    {
        // Kernel main loop.
        // Audio block processing and Scheduler advancement happen in
        // AudioTestSource::update(), driven by the Teensy Audio Library.

        ServiceBringUpSerial();
#ifdef BROTRACKER_USB_TX_TRACE
        ServiceUsbTrace(g_test_playback_state != TestPlaybackState::Idle &&
            g_test_playback_state != TestPlaybackState::Done &&
            g_test_playback_state != TestPlaybackState::Error);
#endif

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

        if (g_test_playback_state == TestPlaybackState::Test2 &&
            g_sample_player_a.IsStreamPlaying())
        {
            if (!g_next_stream_open_attempted)
            {
                g_next_stream_open_attempted = true;

                WavStreamInfo next_info;
                if (OpenWavPcmStream(kTest3SamplePath, next_info))
                    g_sample_player_a.SetNextStream(std::move(next_info));
            }

            if (!g_next_stream_primed_reported &&
                g_sample_player_a.IsNextStreamPrimed())
            {
                g_next_stream_primed_reported = true;
                Serial.println("Test stream: next stream primed while current is playing");
            }
        }

        const bool simultaneous_pair_playing =
            g_sample_player_a.IsStreamPlaying() &&
            g_sample_player_b.IsStreamPlaying();

        const bool simultaneous_iteration_needs_next =
            g_simultaneous_test_loop + 1 < kSimultaneousTestLoops;

        if (g_test_playback_state == TestPlaybackState::SimultaneousTest &&
            simultaneous_iteration_needs_next &&
            simultaneous_pair_playing)
        {
            if (!g_simultaneous_next_open_attempted)
            {
                g_simultaneous_next_open_attempted = true;

                WavStreamInfo next_info_a;
                WavStreamInfo next_info_b;

                if (OpenWavPcmStream(kTest2SamplePath, next_info_a) &&
                    OpenWavPcmStream(kTest3SamplePath, next_info_b))
                {
                    g_sample_player_a.SetNextStream(std::move(next_info_a));
                    g_sample_player_b.SetNextStream(std::move(next_info_b));
                }
                else
                {
                    FailSequence();
                    Serial.println("Test stream: simultaneous next stream preparation failed");
                }
            }

            if (!g_simultaneous_next_primed_reported &&
                g_sample_player_a.IsNextStreamPrimed() &&
                g_sample_player_b.IsNextStreamPrimed())
            {
                g_simultaneous_next_primed_reported = true;
                Serial.print("Test stream: simultaneous next streams primed for iteration ");
                Serial.println(g_simultaneous_test_loop + 2);
            }
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

            if (g_test_playback_state == TestPlaybackState::Test1)
            {
                g_test_playback_state = TestPlaybackState::Test2;

                g_stream_was_playing = false;
                g_stream_finished_reported = false;

                if (!OpenAndPlayStream(g_sample_player_a, kTest2SamplePath))
                    FailSequence();
            }
            else if (g_test_playback_state == TestPlaybackState::Test2)
            {
                if (g_sample_player_a.PromoteNextStream())
                {
                    g_sample_player_a.StartStream();
                    g_test_playback_state = TestPlaybackState::Test3;
                    g_stream_was_playing = true;
                    g_stream_finished_reported = false;

                    Serial.println("Test stream: promoted prebuffered next stream");
                }
                else
                {
                    FailSequence();
                    Serial.println("Test stream: next stream promotion failed");
                }
            }
            else if (g_test_playback_state == TestPlaybackState::Test3)
            {
                g_test_playback_state = TestPlaybackState::SimultaneousTest;

                g_stream_was_playing = false;
                g_stream_finished_reported = false;

                if (!OpenAndPlayStream(g_sample_player_a, kTest2SamplePath) ||
                    !OpenAndPlayStream(g_sample_player_b, kTest3SamplePath))
                    FailSequence();
            }
            else if (g_test_playback_state == TestPlaybackState::SimultaneousTest)
            {
                ++g_simultaneous_test_loop;

                if (g_simultaneous_test_loop < kSimultaneousTestLoops)
                {
                    if (g_simultaneous_next_primed_reported &&
                        g_sample_player_a.IsNextStreamPrimed() &&
                        g_sample_player_b.IsNextStreamPrimed() &&
                        g_sample_player_a.PromoteNextStream() &&
                        g_sample_player_b.PromoteNextStream())
                    {
                        g_sample_player_a.StartStream();
                        g_sample_player_b.StartStream();

                        g_stream_was_playing = true;
                        g_stream_finished_reported = false;
                        g_simultaneous_next_open_attempted = false;
                        g_simultaneous_next_primed_reported = false;

                        Serial.print("Test stream: promoted simultaneous streams for iteration ");
                        Serial.println(g_simultaneous_test_loop + 1);
                    }
                    else
                    {
                        FailSequence();
                        Serial.println("Test stream: simultaneous next stream promotion failed");
                    }
                }
                else
                {
                    g_test_playback_state = TestPlaybackState::Done;

                    Serial.println("Test stream: simultaneous playback finished");
#ifdef BROTRACKER_USB_TX_TRACE
                    UsbTraceFreeze();
#endif
                    Serial.println("BTTEST1 DONE");
                    DiagnosticBlink(3);
                }
            }
        }
    }
}
