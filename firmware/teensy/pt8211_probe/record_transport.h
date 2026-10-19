#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace ProbeWire {
inline uint32_t Crc(uint32_t state,const char* bytes,size_t size) {
    for(size_t i=0;i<size;++i) {
        state^=uint8_t(bytes[i]);
        for(unsigned bit=0;bit<8;++bit) state=(state>>1)^((state&1)?0xedb88320u:0);
    }
    return state;
}

// The same implementation is exercised by native short/zero-write tests.
class Dump {
    char payload[512]{}, frame[768]{};
    uint32_t started=0;
    bool begun=false;
public:
    uint32_t sequence=0, stream=0xffffffffu, shortWrites=0, zeroWrites=0;
    bool failed=false;
    const char* reason="none";
    static constexpr uint32_t kDumpMs=60000, kAckMs=2000;

    template<class IO,class Clock>
    bool Write(IO& io,Clock& clock,const char* bytes,size_t size) {
        size_t offset=0;
        while(offset<size && uint32_t(clock.now()-started)<kDumpMs) {
            int available=io.availableForWrite();
            if(available>0) {
                size_t count=size-offset;
                if(count>64) count=64;
                if(count>size_t(available)) count=available;
                const size_t accepted=io.write(reinterpret_cast<const uint8_t*>(bytes+offset),count);
                if(accepted>count) { reason="invalid-write-count"; return false; }
                if(!accepted) ++zeroWrites;
                else if(accepted<count) ++shortWrites;
                offset+=accepted;
            }
            clock.idle();
        }
        if(offset!=size) { reason="dump-deadline"; return false; }
        return true;
    }

    template<class IO,class Clock>
    bool Record(IO& io,Clock& clock,bool end,const char* format,...) {
        if(failed) return false;
        if(!begun) { started=clock.now(); begun=true; }
        va_list args; va_start(args,format);
        const int length=vsnprintf(payload,sizeof(payload),format,args);
        va_end(args);
        if(length<0 || size_t(length)>=sizeof(payload)) { reason="payload-format"; return Abort(io,clock); }
        const uint32_t crc=~Crc(0xffffffffu,payload,length);
        const int size=snprintf(frame,sizeof(frame),"{\"protocol\":2,\"seq\":%lu,\"crc32\":\"%08lx\",\"payload\":%s}\n",
            static_cast<unsigned long>(sequence),static_cast<unsigned long>(crc),payload);
        if(size<0 || size_t(size)>=sizeof(frame)) { reason="frame-format"; return Abort(io,clock); }
        // frame/payload remain immutable until the exact ACK; no record retransmission.
        if(!Write(io,clock,frame,size)) return Abort(io,clock);
        char expected[48], received[48]{};
        const int expectedSize=snprintf(expected,sizeof(expected),"ACK %lu %08lx",
            static_cast<unsigned long>(sequence),static_cast<unsigned long>(crc));
        if(expectedSize<0 || size_t(expectedSize)>=sizeof(expected)) { reason="ack-format"; return Abort(io,clock); }
        const uint32_t ackStart=clock.now(); size_t used=0; bool acknowledged=false;
        while(uint32_t(clock.now()-ackStart)<kAckMs && uint32_t(clock.now()-started)<kDumpMs) {
            if(io.available()) {
                const int value=io.read();
                if(value=='\n') {
                    received[used]=0;
                    if(strcmp(received,expected)!=0) { reason="ack-mismatch"; return Abort(io,clock); }
                    acknowledged=true; break;
                }
                if(value<0) continue;
                if(used>=sizeof(received)-1) { reason="ack-overflow"; return Abort(io,clock); }
                received[used++]=char(value);
            } else clock.idle();
        }
        if(!acknowledged) { reason="ack-deadline"; return Abort(io,clock); }
        if(!end) stream=Crc(stream,payload,length);
        ++sequence;
        return true;
    }

    template<class IO,class Clock>
    bool Abort(IO& io,Clock& clock) {
        failed=true;
        // Separate 100-ms best-effort failure notification; never continue records.
        char message[96];
        const int n=snprintf(message,sizeof(message),"\n!ABORT seq=%lu reason=%s\n",static_cast<unsigned long>(sequence),reason);
        const uint32_t start=clock.now(); size_t offset=0;
        if(n>0 && size_t(n)<sizeof(message)) while(offset<size_t(n) && uint32_t(clock.now()-start)<100) {
            if(io.availableForWrite()>0) {
                const size_t accepted=io.write(reinterpret_cast<const uint8_t*>(message+offset),1);
                if(accepted>1) break;
                offset+=accepted;
            }
            clock.idle();
        }
        return false;
    }
};
} // namespace ProbeWire
