#include "ui/serial_transport.h"
#include <cerrno>
#include <cstring>
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


class LinuxSerialTransport final : public SerialTransport {
    int fd_ = -1;
    const char* error_ = "USB hangup";
    static void Log(FILE* log, const char* message, const char* detail) {
        if (log) { std::fprintf(log, "USB bring-up: %s %s\n", message, detail); std::fflush(log); }
    }
public:
    ~LinuxSerialTransport() override { Close(); }
    bool Open(FILE* log, const char* test_device) override {
        char device[PATH_MAX];
        if (test_device) std::snprintf(device, sizeof(device), "%s", test_device);
        else if (!FindTeensy(device, sizeof(device))) return false;
        fd_ = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) { Log(log, "open failed:", std::strerror(errno)); return false; }
        termios config{};
        if (tcgetattr(fd_, &config) != 0) { Log(log, "configuration failed:", std::strerror(errno)); Close(); return false; }
        cfmakeraw(&config);
        config.c_cflag |= CLOCAL | CREAD;
        config.c_cflag &= ~CRTSCTS;
        cfsetispeed(&config, B115200);
        cfsetospeed(&config, B115200);
        if (tcsetattr(fd_, TCSANOW, &config) != 0 || tcflush(fd_, TCIOFLUSH) != 0)
        { Log(log, "configuration failed:", std::strerror(errno)); Close(); return false; }
        int bits = TIOCM_DTR | TIOCM_RTS;
        // PTYs used by the host test have no modem lines.
        if (ioctl(fd_, TIOCMBIS, &bits) < 0 && !(test_device && errno == ENOTTY))
        { Log(log, "configuration failed:", std::strerror(errno)); Close(); return false; }
        Log(log, "opened:", device);
        return true;
    }
    void Close() override { if (fd_ >= 0) close(fd_); fd_ = -1; }
    bool Healthy() override {
        pollfd port{fd_, POLLIN, 0};
        const int ready = poll(&port, 1, 0);
        if (ready < 0 && errno != EINTR) { error_ = std::strerror(errno); return false; }
        error_ = "USB hangup";
        return !(port.revents & (POLLHUP | POLLERR | POLLNVAL));
    }
    int Result(int count) {
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
            error_ = std::strerror(errno);
        }
        return count;
    }
    int Read(char* data, unsigned size) override { return Result(read(fd_, data, size)); }
    int Write(const char* data, unsigned size) override { return Result(write(fd_, data, size)); }
    const char* Error() const override { return error_; }
};
std::unique_ptr<SerialTransport> MakeSerialTransport() { return std::make_unique<LinuxSerialTransport>(); }
