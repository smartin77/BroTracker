#include "diagnostics.h"

#include <Arduino.h>
#include <SD.h>
#include <TimeLib.h>
#include <sd_access.h>

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

// Opaque handle for a tool-specific sequential log file. Only the path is
// retained (D0046): each log write opens the SD writer, appends and
// closes again immediately, rather than holding a writer open for the
// tool's entire run, which would otherwise block SD reads (e.g. sample
// streaming) for as long as the tool kept running.
struct ToolLogHandle
{
    char path[80] = {};
};

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

void FormatTimestamp(char* buffer, std::size_t size)
{
    const time_t current = now();

    std::snprintf(
        buffer,
        size,
        "%04d-%02d-%02d %02d:%02d:%02d",
        year(current),
        month(current),
        day(current),
        hour(current),
        minute(current),
        second(current));
}

ToolLogHandle* OpenToolLogFileInternal(const char* tool_name)
{
    if (!diagnostics_ready || tool_name == nullptr)
        return nullptr;

    ToolLogHandle* handle = new (std::nothrow) ToolLogHandle();
    if (handle == nullptr)
        return nullptr;

    // Find the next available sequence number
    unsigned int sequence = 1;

    for (sequence = 1; sequence <= 9999; ++sequence)
    {
        std::snprintf(
            handle->path,
            sizeof(handle->path),
            "%s/%s-%04u.log",
            kLogDirectory,
            tool_name,
            sequence);

        if (!SD.exists(handle->path))
            break;
    }

    BroTracker::SdWriter writer;
    if (!writer.open(handle->path))
    {
        delete handle;
        return nullptr;
    }

    char timestamp[32];
    FormatTimestamp(timestamp, sizeof(timestamp));

    // Write initial timestamp header for this tool run
    writer.println("=== Tool Log Start ===");
    writer.println(timestamp);
    writer.println("");

    return handle;
}

void CloseToolLogFileInternal(ToolLogHandle* handle)
{
    if (handle == nullptr)
        return;

    BroTracker::SdWriter writer;
    if (writer.open(handle->path))
    {
        writer.println("");
        writer.println("=== Tool Log End ===");
    }

    delete handle;
}

bool ToolLogMessageInternal(ToolLogHandle* handle, const char* message)
{
    if (handle == nullptr || message == nullptr)
        return false;

    // Opening can fail if an SD read is active elsewhere (D0046); the
    // message is then dropped from the SD log but the caller's own
    // Serial output (done separately by callers) is unaffected.
    BroTracker::SdWriter writer;
    if (!writer.open(handle->path))
        return false;

    char timestamp[32];
    FormatTimestamp(timestamp, sizeof(timestamp));

    writer.println(timestamp);
    writer.println(message);
    return true;
}
}  // namespace (close anonymous namespace)

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

        // Opening can fail if an SD read is active elsewhere (D0046),
        // e.g. a sample stream or a WAV file being loaded; the message is
        // then simply not persisted rather than racing the active read.
        SdWriter writer;
        if (!writer.open(kLogPath))
            return false;

        char timestamp[32];
        FormatTimestamp(timestamp, sizeof(timestamp));

        writer.println(timestamp);
        writer.println(message);
        return true;
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

    // Opaque handle versions that wrap the ToolLogHandle implementations
    // from the anonymous namespace above.
    void* OpenToolLogFile(const char* tool_name)
    {
        return OpenToolLogFileInternal(tool_name);
    }

    void CloseToolLogFile(void* log_file_handle)
    {
        CloseToolLogFileInternal(static_cast<ToolLogHandle*>(log_file_handle));
    }

    bool ToolLogMessage(void* log_file_handle, const char* message)
    {
        return ToolLogMessageInternal(static_cast<ToolLogHandle*>(log_file_handle), message);
    }
}