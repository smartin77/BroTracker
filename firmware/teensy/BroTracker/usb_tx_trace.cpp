#include "usb_tx_trace.h"
#ifdef BROTRACKER_USB_TX_TRACE
#include <Arduino.h>
#include <cstdio>
namespace BroTracker {
namespace {
unsigned line_index=0;
bool dump_complete=false;
BroTrackerUsbTrace::History history;
bool history_ready=false,history_done=false;
unsigned history_line=0;
unsigned dump_credit=0;
uint32_t last_protocol_ms=0,last_emit_ms=0;
// At most eight <192-byte lines between serviced protocol commands. Never
// accumulate credit. Leave CDC space for replies and yield between lines.
bool WriteDiagnostic(const char* line,unsigned n) {
    const uint32_t now=millis();
    if(!dump_credit || uint32_t(now-last_protocol_ms)<20 ||
       uint32_t(now-last_emit_ms)<20)return false;
    if(Serial.availableForWrite()<int(n+256))return false;
    if(Serial.write(reinterpret_cast<const uint8_t*>(line),n)!=n)return false;
    --dump_credit;last_emit_ms=now;return true;
}
bool HistoryLine(char* out,unsigned size) {
    if(!history_ready){brotracker_usb_history_snapshot(&history);history_ready=true;}
    unsigned count=history.Count();
    if(history_line==0)std::snprintf(out,size,"USBLIFE1 BEGIN total=%lu retained=%u omitted=%lu\n",(unsigned long)history.total,count,(unsigned long)(history.total-count));
    else if(history_line<=count){
        auto e=history.At(history_line-1);
        std::snprintf(out,size,"USBLIFE1 E seq=%lu us=%lu type=%lu a=%lu b=%lu c=%lu\n",(unsigned long)e.sequence,(unsigned long)e.micros,(unsigned long)e.type,(unsigned long)e.a,(unsigned long)e.b,(unsigned long)e.c);
    }else{std::snprintf(out,size,"USBLIFE1 END\n");return false;}
    return true;
}
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
    dump_credit=0;
    if(dump_complete){brotracker_usb_trace_release();dump_complete=false;history_ready=false;}
    line_index=0;history_line=0;history_done=false;
    if(!brotracker_usb_trace_arm())Serial.println("USBTRACE1 BUSY retained_trace=1");
}
void UsbTraceFreeze(){brotracker_usb_trace_freeze();}
void UsbTraceReplay(){line_index=0;dump_complete=false;history_line=0;history_done=false;dump_credit=0;}
void UsbTraceProtocolActivity(bool grant_dump){
    last_protocol_ms=millis();
    if(grant_dump)dump_credit=8;
}
void ServiceUsbTrace(bool playing){
    if(playing)return;
    const auto* b=brotracker_usb_trace_buffer();if(!b->frozen)return;
    if(!Serial){UsbTraceReplay();return;}
    if(dump_complete)return;
    if(!history_done){
        char line[192];bool more=HistoryLine(line,sizeof(line));unsigned n=std::strlen(line);
        if(!WriteDiagnostic(line,n))return;
        ++history_line;if(!more)history_done=true;
        return;
    }
    char line[192];bool more=Format(line,sizeof(line),line_index,*b);unsigned n=std::strlen(line);
    if(!WriteDiagnostic(line,n))return;
    ++line_index;if(!more)dump_complete=true; // retained until next explicit START
}
}
#endif
