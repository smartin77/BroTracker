#pragma once

#include <SD.h>

#include "sample.h"

namespace BroTracker
{
    // Loads a PCM WAV file from the SD card into RAM.
    //
    // Currently supported: PCM, 16-bit, mono, 44.1 kHz. Other combinations
    // are rejected so unsupported files fail loudly instead of playing back
    // incorrectly. Support for additional formats can be added inside this
    // loader without changing the Sample or SamplePlayer interfaces.
    //
    // Returns true and fills `out_sample` on success. `out_sample` is left
    // unchanged on failure and any partially allocated data is released.
    bool LoadWavSampleFromSd(const char* path, Sample& out_sample);

    // Metadata for an open WAV PCM stream, returned by OpenWavPcmStream().
    struct WavStreamInfo
    {
        File file;
        std::uint32_t sample_rate_hz = 0;
        std::uint8_t channel_count = 0;
        std::uint8_t bits_per_sample = 0;
        std::uint32_t data_chunk_offset = 0;
        std::uint32_t data_chunk_size = 0;
        std::uint32_t frame_count = 0;
    };

    // Parses a WAV file's RIFF/fmt/data chunks without loading PCM data,
    // for streaming playback instead of full in-RAM loading.
    //
    // Currently supported: PCM, 16-bit, mono, 44.1 kHz, matching
    // LoadWavSampleFromSd(). On success, `out_info.file` is left open and
    // positioned at the start of PCM data; the caller owns the handle and
    // is responsible for closing it. On failure, the file is closed and
    // `out_info` is left unspecified.
    bool OpenWavPcmStream(const char* path, WavStreamInfo& out_info);
}
