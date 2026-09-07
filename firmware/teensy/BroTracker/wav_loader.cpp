#include "wav_loader.h"

#include <SD.h>

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
        File file = SD.open(path, FILE_READ);
        if (!file)
            return false;

        char riff_tag[4] = {0};
        std::uint32_t riff_size = 0;
        char wave_tag[4] = {0};

        if (!ReadExact(file, riff_tag, 4) ||
            !ReadExact(file, &riff_size, 4) ||
            !ReadExact(file, wave_tag, 4) ||
            !MatchesTag(riff_tag, "RIFF") ||
            !MatchesTag(wave_tag, "WAVE"))
        {
            file.close();
            return false;
        }

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

            if (!ReadExact(file, chunk_id, 4) || !ReadExact(file, &chunk_size, 4))
                break;

            if (MatchesTag(chunk_id, "fmt "))
            {
                std::uint8_t fmt_buffer[16] = {0};
                if (chunk_size < sizeof(fmt_buffer) ||
                    !ReadExact(file, fmt_buffer, sizeof(fmt_buffer)))
                {
                    break;
                }

                std::memcpy(&audio_format, fmt_buffer + 0, 2);
                std::memcpy(&channel_count, fmt_buffer + 2, 2);
                std::memcpy(&sample_rate, fmt_buffer + 4, 4);
                std::memcpy(&bits_per_sample, fmt_buffer + 14, 2);
                have_fmt = true;

                SkipBytes(file, chunk_size - sizeof(fmt_buffer));
            }
            else if (MatchesTag(chunk_id, "data"))
            {
                if (!have_fmt ||
                    audio_format != kPcmAudioFormat ||
                    channel_count != kSupportedChannelCount ||
                    sample_rate != kSupportedSampleRateHz ||
                    bits_per_sample != kSupportedBitsPerSample)
                {
                    break;
                }

                frame_count = chunk_size / sizeof(std::int16_t);
                pcm_data = new (std::nothrow) std::int16_t[frame_count];
                if (pcm_data == nullptr)
                {
                    frame_count = 0;
                    break;
                }

                const std::uint32_t bytes_to_read = frame_count * sizeof(std::int16_t);
                if (!ReadExact(file, pcm_data, bytes_to_read))
                {
                    delete[] pcm_data;
                    pcm_data = nullptr;
                    frame_count = 0;
                    break;
                }

                found_data = true;
                break;
            }
            else
            {
                SkipBytes(file, chunk_size + (chunk_size & 1));
            }
        }

        file.close();

        if (!found_data || pcm_data == nullptr || frame_count == 0)
        {
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

        return true;
    }
}
