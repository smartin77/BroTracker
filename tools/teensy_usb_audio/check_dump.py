"""Native dump + production host protocol simulation; no firmware build."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--before',type=Path);p.add_argument('--cxx',default='g++');a=p.parse_args();a.build.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];fixture=a.build/'fixture';fixture.mkdir(exist_ok=True)
(fixture/'Arduino.h').write_bytes(r"""#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
extern uint32_t test_ms;
inline uint32_t millis(){return test_ms;}
struct TestSerial {
 bool connected=true;std::string pending,all;unsigned max_queue=0;
 operator bool()const{return connected;}
 int availableForWrite()const{return 8192;} // driver drains USB into OS RX queue
 unsigned write(const uint8_t* p,unsigned n){pending.append(reinterpret_cast<const char*>(p),n);all.append(reinterpret_cast<const char*>(p),n);max_queue=std::max(max_queue,unsigned(pending.size()));return n;}
 void println(const char* p){std::string s=std::string(p)+"\n";write(reinterpret_cast<const uint8_t*>(s.data()),s.size());}
};extern TestSerial Serial;
""".encode())
def extract(s,start):
 pos=s.index(start);i=s.index('{',pos);depth=1;j=i+1
 while depth:depth+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[pos:j]
platform=root/'firmware/teensy/BroTracker/platform.cpp'
if a.before:platform=a.before.with_name('platform.cpp')
service=extract(platform.read_text(),'void ServiceBringUpSerial()')
cpp=r"""
#include "Arduino.h"
#include "usb_tx_trace.h"
#include "ui/bringup_serial.h"
#include <iostream>
#include <stdexcept>
uint32_t test_ms=0;TestSerial Serial;
static BroTrackerUsbTrace::Buffer buffer;
extern "C" void brotracker_usb_history_snapshot(BroTrackerUsbTrace::History* out){*out={};for(unsigned i=0;i<200;++i)out->Add(i*1000,i%9+1,i,0,0);}
extern "C" bool brotracker_usb_trace_arm(){return buffer.Arm(100);}
extern "C" void brotracker_usb_trace_freeze(){if(buffer.armed)buffer.Freeze();}
extern "C" void brotracker_usb_trace_release(){buffer.Release();}
extern "C" const BroTrackerUsbTrace::Buffer* brotracker_usb_trace_buffer(){return &buffer;}
static std::string device_input;
int ReadStartupSerialByte(){if(device_input.empty())return -1;char c=device_input[0];device_input.erase(0,1);return static_cast<unsigned char>(c);}
namespace BroTracker {
void ReportSequence(){Serial.println("BTTEST1 STATE IDLE");}
bool StartSequence(){return true;}
void StopSequence(){}
@SERVICE@
}
void Check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
struct Transport:SerialTransport {
 unsigned opens=0,closes=0,commands=0;std::string tx;
 bool Open(FILE*,const char*)override{++opens;Serial.connected=true;return true;}
 void Close()override{++closes;Serial.pending.clear();}
 bool Healthy()override{return Serial.connected;}
 const char* Error()const override{return "simulated disconnect";}
 int Read(char* data,unsigned size)override{if(Serial.pending.empty())return 0;Check(size==1,"host byte parser unchanged");*data=Serial.pending[0];Serial.pending.erase(0,1);return 1;}
 int Write(const char* data,unsigned size)override{
  tx.append(data,size);if(tx.back()=='\n'){
   ++commands;Check(tx=="BTTEST1 HELLO\n"||tx=="BTTEST1 STATUS\n","no START/STOP replay or new protocol command");
   device_input+=tx;
   tx.clear();
  }return size;
 }
};
std::unique_ptr<SerialTransport> MakeSerialTransport(){return std::unique_ptr<SerialTransport>(new Transport);}
int main(){
 BroTracker::UsbTraceStart();uint8_t payload[180]{};
 for(unsigned i=0;i<128;++i)buffer.Capture(payload,176,44,false,i,i*1000,0,i/3,0);
 auto* peer=new Transport;BringUpSerial host(nullptr,std::unique_ptr<SerialTransport>(peer));host.Tick(0);BroTracker::ServiceBringUpSerial();host.Tick(0);Check(host.Connected(),"initial HELLO");
 // Firmware runs 1ms slices; slow but responsive SDL frame reads 1024 bytes
 // every 100ms using the unmodified 6000ms production command timeout.
 for(test_ms=1;test_ms<=160000;++test_ms){
  BroTracker::ServiceBringUpSerial();BroTracker::ServiceUsbTrace(false);
  if(test_ms%100==0){host.Tick(test_ms);if(peer->closes)break;}
 }
#if BOUNDED
 Check(peer->closes==0&&host.Connected(),"HELLO/STATUS stays responsive during dump");
 Check(Serial.all.find("USBTRACE1 END")!=std::string::npos,"bounded dump completes under regular STATUS");
 Check(Serial.max_queue<1800,"bounded diagnostic head-of-line bytes");
 // Physical CDC loss leaves trace retained; recovery HELLO replays it.
 Serial.connected=false;host.Tick(test_ms);Check(!host.Connected(),"disconnect handled");unsigned closes=peer->closes;
 for(unsigned stop=test_ms+165000;test_ms<stop;++test_ms){BroTracker::ServiceBringUpSerial();BroTracker::ServiceUsbTrace(false);if(test_ms%100==0)host.Tick(test_ms);}
 Check(peer->closes==closes&&host.Connected(),"replay does not cause reopen loop");
 auto first=Serial.all.find("USBTRACE1 END");Check(Serial.all.find("USBTRACE1 END",first+1)!=std::string::npos,"retained replay completes");
 std::cout<<"Bounded dump: HELLO/STATUS, timeout unchanged, reconnect replay and cleanup passed; peak queue="<<Serial.max_queue<<" bytes\n";
#else
 Check(peer->closes==1&&!host.Connected(),"unbounded dump must reproduce production command timeout");
 std::cout<<"Original dump timeout reproduced at "<<test_ms<<" ms; peak queue="<<Serial.max_queue<<" bytes\n";
#endif
}
"""
(a.build/'dump_test.cpp').write_bytes(cpp.replace('@SERVICE@',service).encode())
cases=[(False,a.before)] if a.before else [(True,root/'firmware/teensy/BroTracker/usb_tx_trace.cpp')]
for bounded,source in cases:
 exe=a.build/('bounded.exe' if bounded else 'original.exe')
 subprocess.run([a.cxx,'-std=c++14','-DBROTRACKER_USB_TX_TRACE','-DBOUNDED='+str(int(bounded)),'-I'+str(fixture),'-I'+str(root/'firmware/teensy/BroTracker'),'-I'+str(root/'src'),str(a.build/'dump_test.cpp'),str(source),str(root/'src/ui/bringup_serial.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
