#include "diagnostics.h"

#include <Arduino.h>
#include <SD.h>
#include <TimeLib.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
constexpr char kLogDirectory[] = "BroTracker";
constexpr char kLogPath[] = "BroTracker/initialization.log";
constexpr std::uint32_t kHostTimeSyncTimeoutMs = 1500;
constexpr std::uint32_t kLedFlashMs = 750;
constexpr std::uint32_t kLedPauseMs = 750;
bool diagnostics_ready = false;

void SetCompileTimeClock()
{
    int month = 1;

    if (__DATE__[0] == 'J')
    {
        month = (__DATE__[1] == 'a')
            ? 1
            : (__DATE__[2] == 'n' ? 6 : 7);
    }
    else if (__DATE__[0] == 'F')
        month = 2;
    else if (__DATE__[0] == 'M')
        month = (__DATE__[2] == 'r') ? 3 : 5;
    else if (__DATE__[0] == 'A')
        month = (__DATE__[1] == 'p') ? 4 : 8;
    else if (__DATE__[0] == 'S')
        month = 9;
    else if (__DATE__[0] == 'O')
        month = 10;
    else if (__DATE__[0] == 'N')
        month = 11;
    else if (__DATE__[0] == 'D')
        month = 12;

    const int day = (__DATE__[4] == ' ')
        ? __DATE__[5] - '0'
        : (__DATE__[4] - '0') * 10 + (__DATE__[5] - '0');

    const int year =
        (__DATE__[7] - '0') * 1000 +
        (__DATE__[8] - '0') * 100 +
        (__DATE__[9] - '0') * 10 +
        (__DATE__[10] - '0');

    const int hour =
        (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
    const int minute =
        (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');
    const int second =
        (__TIME__[6] - '0') * 10 + (__TIME__[7] - '0');

    setTime(hour, minute, second, day, month, year);
    Teensy3Clock.set(now());
}

void SdDateTimeCallback(
    std::uint16_t* date,
    std::uint16_t* time,
    std::uint8_t* ms10)
{
    const time_t current = now();

    *date = FS_DATE(year(current), month(current), day(current));
    *time = FS_TIME(hour(current), minute(current), second(current));
    *ms10 = (second(current) & 1) ? 100 : 0;
}

bool TryParseUnixEpoch(
    const char* text,
    std::uint32_t& epoch_out)
{
    if (text == nullptr || text[0] == '\0')
        return false;

    const char* payload = text;

    if (payload[0] == 'E' && payload[1] == 'P' &&
        payload[2] == 'O' && payload[3] == 'C' &&
        payload[4] == 'H' && payload[5] == ':')
    {
        payload += 6;
    }
    else if (payload[0] == 'T')
    {
        ++payload;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(payload, &end, 10);

    if (end == payload || *end != '\0' || parsed < 946684800ul)
        return false;

    epoch_out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool TrySyncClockFromHost()
{
    if (!Serial)
        return false;

    char line[48] = {};
    std::size_t index = 0;
    const std::uint32_t start = millis();

    while (millis() - start < kHostTimeSyncTimeoutMs)
    {
        while (Serial.available() > 0)
        {
            const int raw = Serial.read();

            if (raw < 0)
                continue;

            const char character = static_cast<char>(raw);

            if (character == '\r' || character == '\n')
            {
                if (index == 0)
                    continue;

                line[index] = '\0';
                std::uint32_t epoch = 0;

                if (TryParseUnixEpoch(line, epoch))
                {
                    setTime(static_cast<time_t>(epoch));
                    Teensy3Clock.set(now());
                    return true;
                }

                index = 0;
                continue;
            }

            if (index + 1 < sizeof(line))
                line[index++] = character;
            else
                index = 0;
        }
    }

    return false;
}

void WriteTimestamp(File& log_file)
{
    char timestamp[32] = {};
    const time_t current = now();

    std::snprintf(
        timestamp,
        sizeof(timestamp),
        "%04d-%02d-%02d %02d:%02d:%02d",
        year(current),
        month(current),
        day(current),
        hour(current),
        minute(current),
        second(current));

    log_file.print("Timestamp: ");
    log_file.println(timestamp);
}
}

namespace BroTracker
{
    bool DiagnosticsInitialize()
    {
        diagnostics_ready = false;
        SetCompileTimeClock();
        TrySyncClockFromHost();

        if (!SD.begin(BUILTIN_SDCARD))
        {
            Serial.println("SD initialization: FAIL");
            return false;
        }

        FsDateTime::setCallback(SdDateTimeCallback);

        if (!SD.exists(kLogDirectory))
        {
            if (!SD.mkdir(kLogDirectory) && !SD.exists(kLogDirectory))
                return false;
        }

        diagnostics_ready = true;
        return true;
    }

    bool DiagnosticLog(const char* message)
    {
        if (!diagnostics_ready)
            return false;

        File log_file = SD.open(kLogPath, FILE_WRITE);

        if (!log_file)
            return false;

        WriteTimestamp(log_file);
        log_file.println(message);
        log_file.flush();
        const bool write_succeeded = log_file;
        log_file.close();
        return write_succeeded;
    }

    void DiagnosticBlink(unsigned int count)
    {
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, LOW);

        for (unsigned int flash = 0; flash < count; ++flash)
        {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(kLedFlashMs);
            digitalWrite(LED_BUILTIN, LOW);
            delay(kLedPauseMs);
        }
    }
}