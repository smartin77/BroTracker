#include "bringup_serial.h"

#include <cerrno>
#include <cstring>
#ifdef __linux__
#include <cstdlib>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace
{
bool ReadId(const char* directory, const char* name, const char* expected)
{
    char path[PATH_MAX + 32], value[32]{};
    const int length = std::snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (length < 0 || static_cast<unsigned int>(length) >= sizeof(path)) return false;
    FILE* file = std::fopen(path, "r");
    if (!file) return false;
    const bool matches = std::fscanf(file, "%31s", value) == 1 &&
                         std::strcmp(value, expected) == 0;
    std::fclose(file);
    return matches;
}

bool FindTeensy(char* device, unsigned int capacity)
{
    glob_t ports{};
    const int result = glob("/sys/class/tty/ttyACM*/device", 0, nullptr, &ports);
    bool found = false;
    if (result == 0)
    {
        for (unsigned int i = 0; i < ports.gl_pathc && !found; ++i)
        {
            char resolved[PATH_MAX];
            if (!realpath(ports.gl_pathv[i], resolved)) continue;
            while (char* slash = std::strrchr(resolved, '/'))
            {
                if (ReadId(resolved, "idVendor", "16c0") &&
                    ReadId(resolved, "idProduct", "048a"))
                {
                    char tty[64]{};
                    if (std::sscanf(ports.gl_pathv[i], "/sys/class/tty/%63[^/]", tty) == 1)
                    {
                        std::snprintf(device, capacity, "/dev/%s", tty);
                        found = true;
                    }
                    break;
                }
                *slash = '\0';
            }
        }
    }
    globfree(&ports);
    return found;
}
}
#endif

BringUpSerial::BringUpSerial(FILE* log, const char* test_device)
    : log_(log), test_device_(test_device)
{
    Log("waiting for Teensy USB CDC 16c0:048a");
#ifndef __linux__
    Log("serial unavailable: this bring-up transport requires Linux");
#endif
}

BringUpSerial::~BringUpSerial()
{
#ifdef __linux__
    if (fd_ >= 0) close(fd_);
#endif
}

void BringUpSerial::Log(const char* message, const char* detail)
{
    if (!log_) return;
    std::fprintf(log_, "USB bring-up: %s %s\n", message, detail);
    std::fflush(log_);
}

void BringUpSerial::Disconnect(const char* reason)
{
    Log("disconnected/error:", reason);
    if (start_queued_ || (pending_ == Command::Start && tx_offset_ == 0))
    {
        start_cancelled_ = true;
        Log("START cancelled before transmission; press again after reconnect");
    }
    else if (pending_ == Command::Start)
    {
        start_unconfirmed_ = true;
        Log("START transmission attempted; playback unconfirmed, not replaying");
    }
#ifdef __linux__
    if (fd_ >= 0) close(fd_);
#endif
    fd_ = -1;
    connected_ = false;
    stopped_ = false;
    pending_ = Command::None;
    start_queued_ = stop_queued_ = false;
    tx_size_ = tx_offset_ = used_ = 0;
    overflow_ = false;
    std::strcpy(state_, "UNKNOWN");
}

bool BringUpSerial::Start()
{
    if (!connected_ || start_queued_ || stop_queued_ ||
        pending_ == Command::Start || pending_ == Command::Stop) return false;
    start_queued_ = true;
    start_cancelled_ = start_unconfirmed_ = false;
    stopped_ = false;
    Log("START requested");
    return true;
}

bool BringUpSerial::Stop()
{
    if (!connected_) { Log("STOP unavailable: device disconnected"); return false; }
    stop_queued_ = true;
    stopped_ = false;
    Log("STOP requested");
    return true;
}

bool BringUpSerial::Finished() const
{
    return connected_ && pending_ != Command::Start && !start_queued_ &&
        (std::strcmp(state_, "DONE") == 0 || std::strcmp(state_, "IDLE") == 0 ||
         std::strcmp(state_, "ERROR") == 0);
}

const char* BringUpSerial::Status() const
{
    if (!connected_)
    {
        if (start_cancelled_) return "Waiting - START cancelled (not sent)";
        if (start_unconfirmed_) return "Waiting - START result unknown";
        return "Waiting for Teensy USB";
    }
    if (stop_queued_ || pending_ == Command::Stop) return "Connected - stopping";
    if (start_queued_ || pending_ == Command::Start) return "Connected - starting";
    if (std::strcmp(state_, "PLAYING") == 0) return "Connected - playing";
    if (std::strcmp(state_, "DONE") == 0) return "Connected - completed";
    if (std::strcmp(state_, "ERROR") == 0) return "Connected - playback ERROR";
    return "Connected - idle";
}

void BringUpSerial::Send(Command command, std::uint32_t now)
{
    const char* verb = command == Command::Hello ? "HELLO" :
                       command == Command::Status ? "STATUS" :
                       command == Command::Start ? "START" : "STOP";
    tx_size_ = std::snprintf(tx_, sizeof(tx_), "BTTEST1 %s\n", verb);
    tx_offset_ = 0;
    pending_ = command;
    sent_at_ = now;
    Log("queued for TX:", verb);
}

void BringUpSerial::OnLine()
{
    Log("RX:", line_);
    if (std::strncmp(line_, "BTTEST1 STATE ", 14) == 0)
    {
        const char* value = line_ + 14;
        if (std::strcmp(value, "PLAYING") && std::strcmp(value, "DONE") &&
            std::strcmp(value, "IDLE") && std::strcmp(value, "ERROR")) return;
        std::snprintf(state_, sizeof(state_), "%s", value);
        if (pending_ == Command::Hello || pending_ == Command::Status)
        {
            if (!connected_) Log("connected: BTTEST1 handshake accepted");
            connected_ = true;
            pending_ = Command::None;
        }
    }
    else if (std::strcmp(line_, "BTTEST1 STARTED") == 0)
    {
        std::strcpy(state_, "PLAYING");
        if (pending_ == Command::Start) pending_ = Command::None;
    }
    else if (std::strcmp(line_, "BTTEST1 STOPPED") == 0)
    {
        std::strcpy(state_, "IDLE");
        if (pending_ == Command::Stop)
        {
            stopped_ = true;
            pending_ = Command::None;
        }
    }
    else if (std::strcmp(line_, "BTTEST1 DONE") == 0)
        std::strcpy(state_, "DONE");
    else if (std::strncmp(line_, "BTTEST1 ERROR ", 14) == 0)
    {
        std::strcpy(state_, "ERROR");
        if (pending_ == Command::Start) pending_ = Command::None;
    }
}

void BringUpSerial::Tick(std::uint32_t now)
{
#ifdef __linux__
    if (fd_ < 0)
    {
        if (attempted_ && now - last_attempt_ < 1000) return;
        attempted_ = true;
        last_attempt_ = now;
        char device[PATH_MAX];
        if (test_device_) std::snprintf(device, sizeof(device), "%s", test_device_);
        else if (!FindTeensy(device, sizeof(device))) return;
        fd_ = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) { Log("open failed:", std::strerror(errno)); return; }
        termios config{};
        if (tcgetattr(fd_, &config) != 0) { Disconnect(std::strerror(errno)); return; }
        cfmakeraw(&config);
        config.c_cflag |= CLOCAL | CREAD;
        config.c_cflag &= ~CRTSCTS;
        cfsetispeed(&config, B115200);
        cfsetospeed(&config, B115200);
        if (tcsetattr(fd_, TCSANOW, &config) != 0 || tcflush(fd_, TCIOFLUSH) != 0)
        { Disconnect(std::strerror(errno)); return; }
        int bits = TIOCM_DTR | TIOCM_RTS;
        // PTYs used by the host test have no modem lines.
        if (ioctl(fd_, TIOCMBIS, &bits) < 0 && !(test_device_ && errno == ENOTTY))
        { Disconnect(std::strerror(errno)); return; }
        Log("opened:", device);
        Send(Command::Hello, now);
    }

    pollfd port{fd_, POLLIN, 0};
    const int ready = poll(&port, 1, 0);
    if (ready < 0 && errno != EINTR) { Disconnect(std::strerror(errno)); return; }
    if (port.revents & (POLLHUP | POLLERR | POLLNVAL))
    { Disconnect("USB hangup"); return; }
    if (tx_offset_ < tx_size_)
    {
        const auto count = write(fd_, tx_ + tx_offset_, tx_size_ - tx_offset_);
        if (count > 0)
        {
            tx_offset_ += count;
            if (tx_offset_ == tx_size_) Log("TX complete:", tx_);
        }
        else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        { Disconnect(std::strerror(errno)); return; }
    }
    // Bound receive work per SDL frame, including verbose firmware diagnostics.
    for (unsigned int budget = 0; budget < 1024; ++budget)
    {
        char c;
        const auto count = read(fd_, &c, 1);
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        { Disconnect(std::strerror(errno)); return; }
        if (count <= 0) break;
        if (c == '\r') continue;
        if (c == '\n')
        {
            line_[used_] = '\0';
            if (overflow_) Log("RX error: overlong line discarded");
            else OnLine();
            used_ = 0;
            overflow_ = false;
        }
        else if (used_ + 1 < sizeof(line_)) line_[used_++] = c;
        else overflow_ = true;
    }
    if (pending_ != Command::None && now - sent_at_ >= 6000)
    { Disconnect("command/handshake timeout; execution unconfirmed"); return; }
    if (pending_ == Command::None && tx_offset_ == tx_size_)
    {
        if (start_queued_)
        {
            Send(Command::Start, now);
            start_queued_ = false;
        }
        else if (stop_queued_)
        {
            Send(Command::Stop, now);
            stop_queued_ = false;
        }
        else if (connected_ && now - sent_at_ >= 1000) Send(Command::Status, now);
    }
#else
    (void)now;
#endif
}
