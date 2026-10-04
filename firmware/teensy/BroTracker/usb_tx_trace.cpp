#include "usb_tx_trace.h"
#ifdef BROTRACKER_USB_TX_TRACE
#include <Arduino.h>
#include <cstdio>
namespace BroTracker {
namespace {
unsigned line_index=0;
bool dump_complete=false;
// Seven lines per packet: metadata plus six <=32-byte payload fragments.
bool Format(char* out,unsigned size,unsigned line,const BroTrackerUsbTrace::Buffer& b) {
    if(line==0){std::snprintf(out,size,"USBTRACE1 BEGIN s=%lu arm_us=%lu packets=%u pre_dma=1\n",(unsigned long)b.session,(unsigned long)b.arm_micros,b.count);return true;}
    --line;
    if(line>=b.count*7){std::snprintf(out,size,"USBTRACE1 END s=%lu packets=%u\n",(unsigned long)b.session,b.count);return false;}
    unsigned index=line/7,part=line%7;const auto& p=b.packets[index];
    if(part==0)std::snprintf(out,size,"USBTRACE1 P s=%lu i=%u seq=%lu us=%lu cy=%lu n=%u cp=%u sh=%u up=%lu dr=%lu\n",
        (unsigned long)b.session,index,(unsigned long)p.sequence,(unsigned long)p.micros,(unsigned long)p.cycles,p.bytes,p.copied,p.shortage,(unsigned long)p.updates,(unsigned long)p.discards);
    else {
        unsigned offset=(part-1)*32,n=offset<p.bytes?p.bytes-offset:0;if(n>32)n=32;
        int used=std::snprintf(out,size,"USBTRACE1 D s=%lu i=%u o=%u ",(unsigned long)b.session,index,offset);
        static const char hex[]="0123456789abcdef";
        for(unsigned j=0;j<n;++j){out[used++]=hex[p.payload[offset+j]>>4];out[used++]=hex[p.payload[offset+j]&15];}
        out[used++]='\n';out[used]=0;
    }
    return true;
}
}
void UsbTraceStart(){
    if(dump_complete){brotracker_usb_trace_release();dump_complete=false;}
    line_index=0;
    if(!brotracker_usb_trace_arm())Serial.println("USBTRACE1 BUSY retained_trace=1");
}
void UsbTraceFreeze(){brotracker_usb_trace_freeze();}
void UsbTraceReplay(){line_index=0;dump_complete=false;}
void ServiceUsbTrace(bool playing){
    if(playing)return;
    const auto* b=brotracker_usb_trace_buffer();if(!b->frozen)return;
    if(!Serial){UsbTraceReplay();return;}
    if(dump_complete)return;
    char line[192];bool more=Format(line,sizeof(line),line_index,*b);unsigned n=std::strlen(line);
    if(Serial.availableForWrite()<int(n))return; // never wait for CDC space
    if(Serial.write(reinterpret_cast<const uint8_t*>(line),n)!=n)return;
    ++line_index;if(!more)dump_complete=true; // retained until next explicit START
}
}
#endif
