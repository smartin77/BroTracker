#pragma once

#include <cstdint>
#include <cstdio>
#include "serial_transport.h"

// Temporary BTTEST1 hardware-test exchange, NOT BroTracker's final protocol.
// Shared command ordering, parser and timeouts; platform transport owns I/O.
class BringUpSerial
{
public:
    explicit BringUpSerial(FILE* log, const char* test_device = nullptr);
    BringUpSerial(FILE* log, std::unique_ptr<SerialTransport> transport);
    ~BringUpSerial();
    BringUpSerial(const BringUpSerial&) = delete;
    BringUpSerial& operator=(const BringUpSerial&) = delete;
    void Tick(std::uint32_t now);
    bool Start();
    bool Stop();
    bool Connected() const { return connected_; }
    bool Stopped() const { return stopped_; }
    bool Finished() const;
    bool StartCancelled() const { return start_cancelled_; }
    // Read-only UI state: queued or transmitted START still awaiting a response.
    bool StartPending() const { return start_queued_ || pending_ == Command::Start; }
    const char* Status() const;

private:
    enum class Command { None, Hello, Status, Start, Stop };
    void Log(const char* message, const char* detail = "");
    void Disconnect(const char* reason);
    void Send(Command command, std::uint32_t now);
    void OnLine();
    std::unique_ptr<SerialTransport> transport_;
    bool opened_ = false;
    FILE* log_;
    const char* test_device_;
    bool connected_ = false;
    bool stopped_ = false;
    Command pending_ = Command::None;
    // At most one START followed by one STOP; STOP must never replace START.
    bool start_queued_ = false;
    bool stop_queued_ = false;
    bool start_cancelled_ = false;
    bool start_unconfirmed_ = false;
    bool start_write_attempted_ = false;
    std::uint32_t last_attempt_ = 0;
    std::uint32_t sent_at_ = 0;
    bool attempted_ = false;
    char tx_[32]{};
    unsigned int tx_size_ = 0, tx_offset_ = 0;
    char line_[256]{};
    unsigned int used_ = 0;
    bool overflow_ = false;
    char state_[16] = "UNKNOWN";
};
