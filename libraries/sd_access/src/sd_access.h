#pragma once

// Shared SD read/write exclusivity mechanism, per
// docs/ARCHITECTURE_DECISIONS.md D0046 (SD Read/Write Exclusivity).
//
// SD-card reads and writes must never be active at the same time, for the
// whole SD device, regardless of which module, file or File handle is
// involved. SdReader and SdWriter are the sanctioned way to touch the SD
// card: both consult the same shared guard (BroTracker::SdAccess), so the
// rule is enforced by construction instead of relying on convention.
//
// Multiple concurrent SdReader instances are allowed (e.g. several sample
// streams reading at once). A writer requires that no reader, and no other
// writer, is currently active; a reader requires that no writer is active.

#include <SD.h>

#include <cstddef>
#include <cstdint>

namespace BroTracker
{
    // Low-level ownership guard. SdReader/SdWriter/SdReadScope are the
    // intended callers; direct use is only needed to guard a whole
    // non-realtime "phase" of SD reads that use raw File handles under the
    // hood (e.g. diagnostic tools), instead of one SdReader per file.
    namespace SdAccess
    {
        // Multiple concurrent readers are allowed.
        bool AcquireRead();
        void ReleaseRead();

        // Exclusive: fails while any reader or another writer is active.
        bool AcquireWrite();
        void ReleaseWrite();
    }

    // RAII scope that reserves "SD reading in progress" for its lifetime,
    // without owning a File itself. Intended for diagnostic/benchmark code
    // that performs several raw SD.open()/File reads within one logical
    // read phase and wants that whole phase treated as a single reader.
    class SdReadScope
    {
    public:
        SdReadScope() : acquired_(SdAccess::AcquireRead()) {}
        ~SdReadScope() { if (acquired_) SdAccess::ReleaseRead(); }

        SdReadScope(const SdReadScope&) = delete;
        SdReadScope& operator=(const SdReadScope&) = delete;

        explicit operator bool() const { return acquired_; }

    private:
        bool acquired_;
    };

    // RAII wrapper around a read-only SD File. Holds the shared read lock
    // for as long as the file is open; safe to keep open for the full
    // duration of a long-lived stream (playback holds the lock for its
    // entire lifetime, which is what keeps SD writes out of playback).
    class SdReader
    {
    public:
        SdReader() = default;
        ~SdReader() { close(); }

        SdReader(const SdReader&) = delete;
        SdReader& operator=(const SdReader&) = delete;

        SdReader(SdReader&& other) noexcept
            : file_(other.file_), holds_lock_(other.holds_lock_)
        {
            other.holds_lock_ = false;
        }

        SdReader& operator=(SdReader&& other) noexcept
        {
            if (this != &other)
            {
                close();
                file_ = other.file_;
                holds_lock_ = other.holds_lock_;
                other.holds_lock_ = false;
            }
            return *this;
        }

        // Opens `path` for reading. Fails and leaves the reader closed if
        // an SD writer is currently active.
        bool open(const char* path)
        {
            close();

            if (!SdAccess::AcquireRead())
                return false;

            file_ = SD.open(path, FILE_READ);
            if (!file_)
            {
                SdAccess::ReleaseRead();
                return false;
            }

            holds_lock_ = true;
            return true;
        }

        void close()
        {
            if (file_)
                file_.close();

            if (holds_lock_)
            {
                SdAccess::ReleaseRead();
                holds_lock_ = false;
            }
        }

        explicit operator bool() const { return static_cast<bool>(file_); }

        int read(void* buffer, std::size_t size) { return file_.read(buffer, size); }
        bool seek(std::uint32_t position) { return file_.seek(position); }
        std::uint32_t position() { return file_.position(); }
        int available() { return file_.available(); }
        std::uint64_t size() { return file_.size(); }
        bool isDirectory() { return file_.isDirectory(); }

    private:
        File file_;
        bool holds_lock_ = false;
    };

    // RAII wrapper around a write-only SD File. Fails to open while any
    // reader (or another writer) is active, so callers must be structured
    // to write in short bursts between read phases (see D0046).
    class SdWriter
    {
    public:
        SdWriter() = default;
        ~SdWriter() { close(); }

        SdWriter(const SdWriter&) = delete;
        SdWriter& operator=(const SdWriter&) = delete;

        bool open(const char* path, int mode = FILE_WRITE)
        {
            close();

            if (!SdAccess::AcquireWrite())
                return false;

            file_ = SD.open(path, mode);
            if (!file_)
            {
                SdAccess::ReleaseWrite();
                return false;
            }

            holds_lock_ = true;
            return true;
        }

        void close()
        {
            if (file_)
            {
                file_.flush();
                file_.close();
            }

            if (holds_lock_)
            {
                SdAccess::ReleaseWrite();
                holds_lock_ = false;
            }
        }

        explicit operator bool() const { return static_cast<bool>(file_); }

        std::size_t write(const void* buffer, std::size_t size)
        {
            return file_.write(static_cast<const std::uint8_t*>(buffer), size);
        }

        void print(const char* text) { file_.print(text); }
        void println(const char* text) { file_.println(text); }
        void println() { file_.println(); }
        void flush() { file_.flush(); }

    private:
        File file_;
        bool holds_lock_ = false;
    };
}
