#pragma once
#include <cstdio>
#include <memory>
// Host-only nonblocking byte transport. Zero means no progress; -1 is fatal.
// Discovery must never send data to devices that do not match our identity.
class SerialTransport {
public:
    virtual ~SerialTransport() = default;
    virtual bool Open(FILE* log, const char* test_device) = 0;
    // Optional platform-specific discovery result; default preserves other hosts.
    virtual bool AccessDenied() const { return false; }
    virtual void Close() = 0;
    virtual bool Healthy() = 0;
    virtual int Read(char* data, unsigned size) = 0;
    virtual int Write(const char* data, unsigned size) = 0;
    virtual const char* Error() const = 0;
};
std::unique_ptr<SerialTransport> MakeSerialTransport();
