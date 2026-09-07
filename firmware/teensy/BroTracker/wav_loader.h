#pragma once

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
}
