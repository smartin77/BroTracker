#include "../../firmware/teensy/pt8211_probe/record_transport.h"
#include <cassert>
#include <string>
#include <algorithm>

struct Clock {
    uint32_t time=0;
    uint32_t now() const { return time; }
    void idle() { ++time; }
};
struct IO {
    std::string bytes, ack;
    unsigned calls=0;
    bool zero=false, capacityZero=false, noAck=false, wrongAck=false, invalid=false;
    int availableForWrite() { return capacityZero?0:13; }
    size_t write(const uint8_t* p,size_t n) {
        ++calls;
        if(invalid) return n+1;
        if(zero || calls%3==0) return 0;
        n=std::min(n,size_t(7));
        bytes.append(reinterpret_cast<const char*>(p),n);
        if(!noAck && bytes.back()=='\n' && bytes.front()=='{') {
            const size_t end=bytes.find(",\"crc32\"");
            const std::string seq=bytes.substr(20,end-20);
            const size_t crc=bytes.find("\"crc32\":\"")+9;
            ack="ACK "+seq+" "+bytes.substr(crc,8)+"\n";
            if(wrongAck) ack="BAD\n";
        }
        return n;
    }
    int available() const { return !ack.empty(); }
    int read() { const char c=ack.front(); ack.erase(0,1); return c; }
};
int main() {
    assert(~ProbeWire::Crc(0xffffffffu,"123456789",9)==0xcbf43926u);
    {
        ProbeWire::Dump dump; IO io; Clock clock;
        assert(dump.Record(io,clock,false,"{\"type\":\"meta\",\"schema\":2}"));
        assert(dump.sequence==1 && dump.shortWrites>0 && dump.zeroWrites>0);
        assert(io.bytes.find("{\"protocol\":2,\"seq\":0,")==0);
        assert(io.bytes.find("\"payload\":{\"type\":\"meta\",\"schema\":2}}\n")!=std::string::npos);
    }
    for(int failure=0;failure<6;++failure) {
        ProbeWire::Dump dump; IO io; Clock clock;
        io.zero=failure==0; io.capacityZero=failure==1;
        io.noAck=failure==2; io.wrongAck=failure==3; io.invalid=failure==4;
        const std::string large(600,'x');
        assert(!dump.Record(io,clock,false,"%s",failure==5?large.c_str():"{}"));
        assert(dump.failed && dump.sequence==0 && clock.now()<=60100);
        const auto bytes=io.bytes;
        assert(!dump.Record(io,clock,false,"{}"));
        assert(io.bytes==bytes); // failure is sticky; no subsequent measurements emitted.
    }
}
