#include "wav_loader.h"

#include <Arduino.h>
#include <SD.h>

#include "diagnostics.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

namespace BroTracker
{
namespace
{
    constexpr std::uint32_t kSupportedSampleRateHz = 44100;
    constexpr std::uint16_t kSupportedBitsPerSample = 16;
    constexpr std::uint16_t kSupportedChannelCount = 1;
    constexpr std::uint16_t kPcmAudioFormat = 1;

    void DiagnosticLogToSerial(const char* message)
    {
        Serial.println(message);
        DiagnosticLog(message);
    }

    void DiagnosticLogFormatted(const char* format, ...)
    {
        char buffer[128];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        DiagnosticLogToSerial(buffer);
    }

    bool ReadExact(File& file, void* buffer, std::size_t size)
    {
        const int result = file.read(reinterpret_cast<std::uint8_t*>(buffer), size);
        return result >= 0 && static_cast<std::size_t>(result) == size;
    }

    bool MatchesTag(const char* tag, const char* expected)
    {
        return std::memcmp(tag, expected, 4) == 0;
    }

    void SkipBytes(File& file, std::uint32_t count)
    {
        if (count > 0)
            file.seek(file.position() + count);
    }
}

    bool LoadWavSampleFromSd(const char* path, Sample& out_sample)
    {
        DiagnosticLogToSerial("=== WAV Loader Diagnostics ===");
        
        File file = SD.open(path, FILE_READ);
        if (!file)
        {
            DiagnosticLogToSerial("WAV: Failed to open file");
            return false;
        }
        
        DiagnosticLogToSerial("WAV: File opened successfully");

        char riff_tag[4] = {0};
        std::uint32_t riff_size = 0;
        char wave_tag[4] = {0};

        if (!ReadExact(file, riff_tag, 4))
        {
            DiagnosticLogToSerial("WAV: Failed to read RIFF tag");
            file.close();
            return false;
        }
        
        if (!ReadExact(file, &riff_size, 4))
        {
            DiagnosticLogToSerial("WAV: Failed to read RIFF size");
            file.close();
            return false;
        }
        
        if (!ReadExact(file, wave_tag, 4))
        {
            DiagnosticLogToSerial("WAV: Failed to read WAVE tag");
            file.close();
            return false;
        }

        if (!MatchesTag(riff_tag, "RIFF"))
        {
            DiagnosticLogFormatted("WAV: Invalid RIFF tag: %c%c%c%c",
                riff_tag[0], riff_tag[1], riff_tag[2], riff_tag[3]);
            file.close();
            return false;
        }
        
        if (!MatchesTag(wave_tag, "WAVE"))
        {
            DiagnosticLogFormatted("WAV: Invalid WAVE tag: %c%c%c%c",
                wave_tag[0], wave_tag[1], wave_tag[2], wave_tag[3]);
            file.close();
            return false;
        }
        
        DiagnosticLogToSerial("WAV: RIFF/WAVE header valid");

        bool have_fmt = false;
        std::uint16_t audio_format = 0;
        std::uint16_t channel_count = 0;
        std::uint32_t sample_rate = 0;
        std::uint16_t bits_per_sample = 0;

        std::int16_t* pcm_data = nullptr;
        std::uint32_t frame_count = 0;
        bool found_data = false;

        while (file.available())
        {
            char chunk_id[4] = {0};
            std::uint32_t chunk_size = 0;

            if (!ReadExact(file, chunk_id, 4))
            {
                DiagnosticLogToSerial("WAV: Failed to read chunk ID");
                break;
            }
            
            if (!ReadExact(file, &chunk_size, 4))
            {
                DiagnosticLogToSerial("WAV: Failed to read chunk size");
                break;
            }

            if (MatchesTag(chunk_id, "fmt "))
            {
                DiagnosticLogFormatted("WAV: Found fmt chunk, size=%lu", (unsigned long)chunk_size);
                
                std::uint8_t fmt_buffer[16] = {0};
                if (chunk_size < sizeof(fmt_buffer))
                {
                    DiagnosticLogFormatted("WAV: fmt chunk too small (size=%lu, need 16)",
                        (unsigned long)chunk_size);
                    break;
                }
                
                if (!ReadExact(file, fmt_buffer, sizeof(fmt_buffer)))
                {
                    DiagnosticLogToSerial("WAV: Failed to read fmt data");
                    break;
                }

                std::memcpy(&audio_format, fmt_buffer + 0, 2);
                std::memcpy(&channel_count, fmt_buffer + 2, 2);
                std::memcpy(&sample_rate, fmt_buffer + 4, 4);
                std::memcpy(&bits_per_sample, fmt_buffer + 14, 2);
                
                DiagnosticLogFormatted("WAV: Audio Format=%u (need 1)",
                    (unsigned int)audio_format);
                DiagnosticLogFormatted("WAV: Channels=%u (need 1)",
                    (unsigned int)channel_count);
                DiagnosticLogFormatted("WAV: Sample Rate=%lu Hz (need 44100)",
                    (unsigned long)sample_rate);
                DiagnosticLogFormatted("WAV: Bits Per Sample=%u (need 16)",
                    (unsigned int)bits_per_sample);
                    
                have_fmt = true;

                SkipBytes(file, chunk_size - sizeof(fmt_buffer));
            }
            else if (MatchesTag(chunk_id, "data"))
            {
                DiagnosticLogFormatted("WAV: Found data chunk, size=%lu bytes",
                    (unsigned long)chunk_size);
                
                if (!have_fmt)
                {
                    DiagnosticLogToSerial("WAV: data chunk found before fmt chunk");
                    break;
                }
                
                if (audio_format != kPcmAudioFormat)
                {
                    DiagnosticLogFormatted("WAV: Unsupported audio format %u",
                        (unsigned int)audio_format);
                    break;
                }
                
                if (channel_count != kSupportedChannelCount)
                {
                    DiagnosticLogFormatted("WAV: Unsupported channel count %u",
                        (unsigned int)channel_count);
                    break;
                }
                
                if (sample_rate != kSupportedSampleRateHz)
                {
                    DiagnosticLogFormatted("WAV: Unsupported sample rate %lu Hz",
                        (unsigned long)sample_rate);
                    break;
                }
                
                if (bits_per_sample != kSupportedBitsPerSample)
                {
                    DiagnosticLogFormatted("WAV: Unsupported bits per sample %u",
                        (unsigned int)bits_per_sample);
                    break;
                }

                frame_count = chunk_size / sizeof(std::int16_t);
                DiagnosticLogFormatted("WAV: Allocating %lu frames (~%lu bytes)",
                    (unsigned long)frame_count, (unsigned long)chunk_size);
                    
                pcm_data = new (std::nothrow) std::int16_t[frame_count];
                if (pcm_data == nullptr)
                {
                    DiagnosticLogFormatted("WAV: Memory allocation failed for %lu frames",
                        (unsigned long)frame_count);
                    frame_count = 0;
                    break;
                }

                const std::uint32_t bytes_to_read = frame_count * sizeof(std::int16_t);
                if (!ReadExact(file, pcm_data, bytes_to_read))
                {
                    DiagnosticLogFormatted("WAV: Failed to read %lu bytes of PCM data",
                        (unsigned long)bytes_to_read);
                    delete[] pcm_data;
                    pcm_data = nullptr;
                    frame_count = 0;
                    break;
                }

                DiagnosticLogFormatted("WAV: Successfully read %lu frames",
                    (unsigned long)frame_count);
                found_data = true;
                break;
            }
            else
            {
                DiagnosticLogFormatted("WAV: Skipping unknown chunk: %c%c%c%c (size=%lu)",
                    chunk_id[0], chunk_id[1], chunk_id[2], chunk_id[3],
                    (unsigned long)chunk_size);
                SkipBytes(file, chunk_size + (chunk_size & 1));
            }
        }

        file.close();

        if (!found_data)
        {
            DiagnosticLogToSerial("WAV: data chunk not found or validation failed");
            delete[] pcm_data;
            return false;
        }
        
        if (pcm_data == nullptr || frame_count == 0)
        {
            DiagnosticLogToSerial("WAV: PCM data is null or frame count is zero");
            delete[] pcm_data;
            return false;
        }

        out_sample.format = SampleFormat::Pcm16Mono44100;
        out_sample.sample_rate_hz = sample_rate;
        out_sample.channel_count = static_cast<std::uint8_t>(channel_count);
        out_sample.bits_per_sample = static_cast<std::uint8_t>(bits_per_sample);
        out_sample.data = pcm_data;
        out_sample.frame_count = frame_count;
        out_sample.loop_start = 0;
        out_sample.loop_end = 0;
        out_sample.loop_type = LoopType::None;

        DiagnosticLogToSerial("WAV: Sample loaded successfully");
        return true;
    }
}
