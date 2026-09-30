#include "serial_transport.h"
class Unavailable final : public SerialTransport {
public:
 bool Open(FILE*, const char*) override { return false; }
 void Close() override {}
 bool Healthy() override { return false; }
 int Read(char*, unsigned) override { return -1; }
 int Write(const char*, unsigned) override { return -1; }
 const char* Error() const override { return "serial unavailable"; }
};
std::unique_ptr<SerialTransport> MakeSerialTransport() { return std::make_unique<Unavailable>(); }
