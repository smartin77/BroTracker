# Native regression only: never invokes PlatformIO or builds firmware.
import argparse,subprocess,sys
from pathlib import Path
from patch_core import prepare,patch_bytes,VERSION,ORIGINAL,PATCHED
p=argparse.ArgumentParser();p.add_argument('--framework',required=True,type=Path);p.add_argument('--build',required=True,type=Path);p.add_argument('--cxx',default='g++');a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.build.mkdir(parents=True,exist_ok=True)
original,patched=prepare(a.framework,a.build/'core',trace=True)
source=original.read_bytes();assert patch_bytes(source,VERSION).count(PATCHED)==1
text=patched.read_text();assert 'bt_trace.Capture' in text and text.index('bt_trace.Capture')<text.index('usb_prepare_transfer(&tx_transfer')
assert '++bt_updates' in text and '++bt_discards' in text
# The authoritative C-compatible declaration must precede the first caller.
assert text.count('#include "usb_lifecycle_trace.h"') == 1
assert '#include "usb_tx_trace.h"\n#include "usb_lifecycle_trace.h"' in text
assert text.index('#include "usb_lifecycle_trace.h"') < text.index('void usb_audio_configure(void)')
assert text.index('#include "usb_lifecycle_trace.h"') < text.index('brotracker_usb_lifecycle_event(6,')
assert 'extern "C" void brotracker_usb_lifecycle_event(uint32_t,uint32_t,uint32_t,uint32_t);' not in text
fixture=a.build/'fixture';fixture.mkdir(exist_ok=True)
(fixture/'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cstring>
#include <string>
extern uint32_t test_ms;
inline uint32_t millis(){return test_ms;}
struct TestSerial { int space=8192; bool connected=true; std::string output;
 operator bool() const {return connected;}
 int availableForWrite()const{return space;}
 unsigned write(const uint8_t* p,unsigned n){output.append(reinterpret_cast<const char*>(p),n);return n;}
 void println(const char* s){output+=s;output+='\\n';}
};
extern TestSerial Serial;
''')
test=r'''
#include "Arduino.h"
#include "usb_tx_trace.h"
#include <stdexcept>
#include <iostream>
TestSerial Serial;uint32_t test_ms=0;
static BroTrackerUsbTrace::Buffer buffer;
extern "C" void brotracker_usb_history_snapshot(BroTrackerUsbTrace::History* out){*out=BroTrackerUsbTrace::History{};}
extern "C" bool brotracker_usb_trace_arm(){return buffer.Arm(100);}
extern "C" void brotracker_usb_trace_freeze(){if(buffer.armed)buffer.Freeze();}
extern "C" void brotracker_usb_trace_release(){buffer.Release();}
extern "C" const BroTrackerUsbTrace::Buffer* brotracker_usb_trace_buffer(){return &buffer;}
void Drain(unsigned ticks){for(unsigned i=0;i<ticks;++i){test_ms+=20;if(i%50==0)BroTracker::UsbTraceProtocolActivity();BroTracker::ServiceUsbTrace(false);}}
void Check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){
 uint8_t payload[180];for(unsigned i=0;i<180;++i)payload[i]=i;
 BroTracker::UsbTraceStart();Check(buffer.armed,"explicit START arm");
 for(unsigned i=0;i<128;++i){unsigned bytes=(i%10==9?45:44)*4;
  buffer.Capture(payload,bytes,40,true,i+5,100+i*1000,200+i*600000,i/3,2);
  const auto& p=buffer.packets[i];Check(p.bytes==bytes && p.copied==40 && p.shortage==1,"packet metadata");
  Check(p.sequence==i+5 && p.micros==100+i*1000 && p.updates==i/3 && p.discards==2,"counters/timestamps");
  Check(std::memcmp(p.payload,payload,bytes)==0,"exact post-fill bytes");
 }
 Check(buffer.frozen && !buffer.armed && buffer.count==128,"freeze full");
 auto saved=buffer.packets[0];buffer.Capture(payload,180,45,false,999,0,0,0,0);Check(buffer.count==128 && buffer.packets[0].sequence==saved.sequence,"no overwrite");
 BroTracker::UsbTraceStart();Check(buffer.session==1,"pending trace survives restart");
 Serial.output.clear();Serial.space=0;BroTracker::ServiceUsbTrace(false);Check(Serial.output.empty(),"no wait when CDC full");
 Serial.space=8192;BroTracker::ServiceUsbTrace(true);Check(Serial.output.empty(),"no dump during audio playback");
 BroTracker::UsbTraceProtocolActivity();test_ms+=20;Serial.space=256;
 BroTracker::ServiceUsbTrace(false);Check(Serial.output.empty(),"reserved reply space");
 Serial.space=8192;
 for(unsigned i=0;i<20;++i){test_ms+=20;BroTracker::ServiceUsbTrace(false);}
 auto slice=Serial.output;unsigned slice_lines=0;for(char c:slice)if(c=='\n')++slice_lines;
 Check(slice_lines==8,"fixed nonaccumulating per-command line credit");
 test_ms+=100;BroTracker::ServiceUsbTrace(false);Check(Serial.output==slice,"no unsolicited credit refill");
 BroTracker::UsbTraceProtocolActivity();BroTracker::UsbTraceProtocolActivity(false);test_ms+=19;
 BroTracker::ServiceUsbTrace(false);Check(Serial.output==slice,"partial command priority/quiet interval");
 BroTracker::UsbTraceReplay();Serial.output.clear();Drain(9000);
 Check(Serial.output.find("USBTRACE1 BEGIN s=1")!=std::string::npos && Serial.output.find("USBTRACE1 END s=1 packets=128")!=std::string::npos,"complete dump");
 unsigned lines=0;std::size_t start=0;while(start<Serial.output.size()){auto end=Serial.output.find('\n',start);Check(end!=std::string::npos && end-start<192,"bounded transport line");++lines;start=end+1;}
 Check(lines==900,"metadata and payload fragment count");
 std::cout<<Serial.output; // captured fixture is reassembled by Python
 BroTracker::UsbTraceReplay();Serial.connected=false;BroTracker::ServiceUsbTrace(false);Serial.connected=true;Serial.output.clear();
 Drain(9000);Check(Serial.output.find("END s=1")!=std::string::npos,"reconnect replay retained trace");
 BroTracker::UsbTraceStart();Check(buffer.session==2 && buffer.count==0,"new START after retrieval");
 buffer.Capture(payload,176,0,true,1,0,0,0,0);BroTracker::UsbTraceFreeze();Check(buffer.frozen && buffer.count==1,"early STOP keeps prefix");
 Serial.output.clear();Drain(200);Check(Serial.output.find("END s=2 packets=1")!=std::string::npos,"partial trace dump");
}
'''
(a.build/'trace_test.cpp').write_bytes(test.encode());exe=a.build/'trace_test.exe'
subprocess.run([a.cxx,'-std=c++14','-Wall','-Wextra','-DBROTRACKER_USB_TX_TRACE','-I'+str(fixture),'-I'+str(root/'firmware/teensy/BroTracker'),str(a.build/'trace_test.cpp'),str(root/'firmware/teensy/BroTracker/usb_tx_trace.cpp'),'-o',str(exe)],check=True)
out=subprocess.check_output([str(exe.resolve())],text=True);(a.build/'trace-fixture.log').write_text(out)
chunks={};metadata={}
for line in out.splitlines():
 if not line.startswith("USBTRACE1 "):continue
 fields=line.split();tag=fields[1];pairs=dict(f.split('=',1) for f in fields[2:] if '=' in f)
 if tag=='P':metadata[int(pairs['i'])]=int(pairs['n'])
 if tag=='D':chunks[(int(pairs['i']),int(pairs['o']))]=bytes.fromhex(fields[-1]) if '=' not in fields[-1] else b''
for i,n in metadata.items():
 data=b''.join(chunks[(i,offset)] for offset in range(0,192,32));assert data==bytes(range(180))[:n]
assert len(metadata)==128
# Macro-disabled host compilation has no trace implementation symbols.
subprocess.run([a.cxx,'-std=c++14','-c',str(root/'firmware/teensy/BroTracker/usb_tx_trace.cpp'),'-o',str(a.build/'disabled.o')],check=True)
print('128-packet exact payload/counter/freeze/STOP/completion/replay/CDC-space tests passed; all payload fragments reconstructed. Trace-disabled compile passed.')

# Compile the actual generated packet fill/update/tx-event functions on the
# host with peripheral stubs, so copied/shortage/update/discard hooks are tested.
def function(fragment, start):
    pos=fragment.index(start);opening=fragment.index('{',pos);depth=1;i=opening+1
    while depth:
        depth+=(fragment[i]=='{')-(fragment[i]=='}');i+=1
    return fragment[pos:i]
callback=function(text,'unsigned int usb_audio_transmit_callback(void)')
copy=function(text,'static void copy_from_buffers(')
update=function(text,'void AudioOutputUSB::update(void)')
tx=function(text,'static void tx_event(transfer_t *t)\n{')
harness=r"""
#include "usb_tx_trace.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <iostream>
#define AUDIO_BLOCK_SAMPLES 128
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
struct audio_block_t{int16_t data[128];};
struct AudioStream{static void release(audio_block_t*){}};
static audio_block_t *input_left,*input_right;
struct AudioOutputUSB:AudioStream{
 static audio_block_t *left_1st,*left_2nd,*right_1st,*right_2nd;
 static uint16_t offset_1st;
 audio_block_t* receiveWritable(unsigned n){return n?input_right:input_left;}
 static audio_block_t* allocate(){return nullptr;}
 void update();
};
audio_block_t *AudioOutputUSB::left_1st,*AudioOutputUSB::left_2nd,*AudioOutputUSB::right_1st,*AudioOutputUSB::right_2nd;
uint16_t AudioOutputUSB::offset_1st;
uint8_t usb_audio_transmit_setting=1;
uint16_t usb_audio_transmit_buffer[90];
static BroTrackerUsbTrace::Buffer bt_trace;
static uint32_t bt_sequence,bt_updates,bt_discards;
static unsigned bt_copied;static bool bt_shortage;static unsigned bt_last_state=~0u,bt_last_update_state=~0u;
static unsigned allocation_failures;
extern "C" void brotracker_usb_lifecycle_event(uint32_t type,uint32_t,uint32_t,uint32_t){if(type==9)++allocation_failures;}
static uint32_t time_us=100;
static uint32_t micros(){time_us+=1000;return time_us;}
static uint32_t ARM_DWT_CYCCNT;
static void bt_barrier(){}
struct transfer_t{};static transfer_t tx_transfer;
static uint32_t feedback_accumulator,usb_audio_sync_rshift,usb_audio_sync_feedback;
#define AUDIO_TX_ENDPOINT 1
static uint8_t submitted[180];static unsigned submitted_bytes;
static void usb_prepare_transfer(transfer_t*,void* data,int len,int){submitted_bytes=len;std::memcpy(submitted,data,len);}
static void arm_dcache_flush_delete(void*,int){}
static void usb_transmit(int,transfer_t*){}
unsigned int usb_audio_transmit_callback(void);
@COPY@
@UPDATE@
@CALLBACK@
@TX@
void Check(bool ok){if(!ok)throw std::runtime_error("generated core trace hooks failed");}
int main(){
 AudioOutputUSB out;audio_block_t a{},b{},c{};
 for(int i=0;i<128;++i){a.data[i]=100+i;b.data[i]=200+i;c.data[i]=300+i;}
 input_left=input_right=&a;out.update();input_left=input_right=&b;out.update();input_left=input_right=&c;out.update();
 Check(bt_updates==3 && bt_discards==1);
 // Reset the output queue, then use exactly one128-frame block.
 usb_audio_transmit_setting=0;input_left=input_right=nullptr;out.update();
 usb_audio_transmit_setting=1;input_left=input_right=&a;out.update();
 bt_trace.Arm(100);tx_event(nullptr);tx_event(nullptr);tx_event(nullptr);
 Check(bt_trace.count==3 && bt_trace.packets[0].copied==44 && bt_trace.packets[1].copied==44);
 const auto& p=bt_trace.packets[2];Check(p.copied==40 && p.shortage && p.bytes==176 && p.updates==5 && p.discards==1);
 for(unsigned i=160;i<176;++i)Check(p.payload[i]==0);
 Check(submitted_bytes==p.bytes && std::memcmp(submitted,p.payload,p.bytes)==0);
 input_left=input_right=nullptr;out.update();Check(allocation_failures==1);
 std::cout<<"Actual instrumented USB fill/update/tx-event host regression passed\n";
}
"""
for token,frag in [('@COPY@',copy),('@UPDATE@',update),('@CALLBACK@',callback),('@TX@',tx)]:harness=harness.replace(token,frag)
coretest=a.build/'core_trace_test.cpp';coretest.write_bytes(harness.encode());coreexe=a.build/'core_trace_test.exe'
subprocess.run([a.cxx,'-std=c++14','-DBROTRACKER_USB_TX_TRACE','-I'+str(root/'firmware/teensy/BroTracker'),str(coretest),'-o',str(coreexe)],check=True)
subprocess.run([str(coreexe.resolve())],check=True)
