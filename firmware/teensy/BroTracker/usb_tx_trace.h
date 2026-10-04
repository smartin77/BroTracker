#pragma once
#include <cstdint>
#include <cstring>
namespace BroTrackerUsbTrace {
constexpr unsigned kPackets=128, kPayload=180;
struct Packet {
    uint32_t sequence, micros, cycles, updates, discards;
    uint16_t bytes, copied;
    uint8_t shortage;
    uint8_t payload[kPayload];
};
struct Buffer {
    Packet packets[kPackets]{};
    volatile unsigned count=0;
    volatile bool armed=false, frozen=false;
    uint32_t session=0, arm_micros=0;
    bool Arm(uint32_t now) {
        if(armed || frozen)return false; // never overwrite an unretrieved trace
        count=0;arm_micros=now;++session;armed=true;return true;
    }
    void Capture(const void* data,unsigned bytes,unsigned copied,bool shortage,
                 uint32_t seq,uint32_t us,uint32_t cycles,uint32_t updates,uint32_t discards) {
        if(!armed || frozen)return;
        if(bytes>kPayload){Freeze();return;}
        auto& p=packets[count];p.sequence=seq;p.micros=us;p.cycles=cycles;
        p.updates=updates;p.discards=discards;p.bytes=bytes;p.copied=copied;p.shortage=shortage;
        std::memcpy(p.payload,data,bytes);
        ++count;if(count==kPackets)Freeze();
    }
    void Freeze(){armed=false;frozen=true;}
    void Release(){armed=false;frozen=false;}
};
static_assert(sizeof(Buffer)<32*1024,"Bounded static trace storage");
}
#ifdef BROTRACKER_USB_TX_TRACE
extern "C" {
bool brotracker_usb_trace_arm();
void brotracker_usb_trace_freeze();
void brotracker_usb_trace_release();
const BroTrackerUsbTrace::Buffer* brotracker_usb_trace_buffer();
}
namespace BroTracker { void UsbTraceStart(); void UsbTraceFreeze(); void UsbTraceReplay(); void ServiceUsbTrace(bool playing); }
#endif
