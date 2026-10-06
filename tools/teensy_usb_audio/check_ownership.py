"""Extract and compile production core functions; no PlatformIO/firmware build."""
import argparse,subprocess
from pathlib import Path
from patch_core import patch_bytes,VERSION
p=argparse.ArgumentParser();p.add_argument('--framework',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--cxx',default='g++');p.add_argument('--original-only',action='store_true');a=p.parse_args();a.build.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];core=a.framework/'cores/teensy4';original=(core/'usb_audio.cpp').read_text();patched=patch_bytes(original.encode(),VERSION).decode();stream=(core/'AudioStream.cpp').read_text();usb=(core/'usb.c').read_text()
from lifecycle_core import patch_usb
patch_usb(usb.encode()) # exact installed lifecycle source guard
assert 'AudioMemory(8);' in (root/'firmware/teensy/BroTracker/platform.cpp').read_text()
assert 'AudioInputUSB g_' not in (root/'firmware/teensy/BroTracker/platform.cpp').read_text()
def extract(s,start):
 pos=s.index(start);i=s.index('{',pos);depth=1;j=i+1
 while depth:depth+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[pos:j]
bus_reset=extract(usb,'if (status & USB_USBSTS_URI)')
tx_alt=extract(usb,'if (setup.wIndex == AUDIO_INTERFACE+1)')
rx_alt=extract(usb,'if (setup.wIndex == AUDIO_INTERFACE+2)')
config=usb.split('case 0x0900: // SET_CONFIGURATION',1)[1].split('case 0x0880:',1)[0]
common=r"""
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <iostream>
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define AUDIO_BLOCK_SAMPLES 128
#define NUM_MASKS 1
struct alignas(4) audio_block_t{uint8_t ref_count,reserved;uint16_t memory_pool_index;int16_t data[128];};
struct AudioStream {
 static audio_block_t *memory_pool;static uint32_t memory_pool_available_mask[1];static uint16_t memory_pool_first_mask,memory_used,memory_used_max;
 unsigned num_inputs=2;audio_block_t* inputQueue[2]{};
 static audio_block_t* allocate();static void release(audio_block_t*);
 audio_block_t* receiveWritable(unsigned);audio_block_t* receiveReadOnly(unsigned);
 void transmit(audio_block_t*,unsigned){} // enabled RX with no downstream destinations
};
audio_block_t pool[8];audio_block_t* AudioStream::memory_pool=pool;
uint32_t AudioStream::memory_pool_available_mask[1];uint16_t AudioStream::memory_pool_first_mask,AudioStream::memory_used,AudioStream::memory_used_max;
struct AudioInputUSB:AudioStream {
 static audio_block_t *incoming_left,*incoming_right,*ready_left,*ready_right;
 static uint16_t incoming_count;static uint8_t receive_flag;
 static bool update_responsibility;void begin();void update();
};
audio_block_t *AudioInputUSB::incoming_left,*AudioInputUSB::incoming_right,*AudioInputUSB::ready_left,*AudioInputUSB::ready_right;
uint16_t AudioInputUSB::incoming_count;uint8_t AudioInputUSB::receive_flag;bool AudioInputUSB::update_responsibility;
struct AudioOutputUSB:AudioStream {
 static audio_block_t *left_1st,*left_2nd,*right_1st,*right_2nd;static uint16_t offset_1st;void update();
};
audio_block_t *AudioOutputUSB::left_1st,*AudioOutputUSB::left_2nd,*AudioOutputUSB::right_1st,*AudioOutputUSB::right_2nd;uint16_t AudioOutputUSB::offset_1st;
static bool usb_audio_input_active;static unsigned failed_allocations;
uint8_t usb_audio_transmit_setting,usb_audio_receive_setting;
uint32_t rx_buffer[180]{},feedback_accumulator;uint32_t usb_audio_overrun_count,usb_audio_underrun_count;
uint16_t usb_audio_transmit_buffer[90];
struct transfer_t{uint32_t status;};transfer_t rx_transfer,sync_transfer,tx_transfer;
uint8_t usb_high_speed=1,usb_audio_sync_nbytes,usb_audio_sync_rshift;
#define AUDIO_RX_ENDPOINT 1
#define AUDIO_SYNC_ENDPOINT 2
#define AUDIO_TX_ENDPOINT 3
#define AUDIO_RX_SIZE 180
#define AUDIO_TX_SIZE 180
static unsigned configure_count;
static void usb_config_rx_iso(unsigned,unsigned,unsigned,void(*)(transfer_t*)){++configure_count;}
static void usb_config_tx_iso(unsigned,unsigned,unsigned,void(*)(transfer_t*)){++configure_count;}
static void rx_event(transfer_t*){} // NULL primes transfer without calling receive callback
static void sync_event(transfer_t*){}
unsigned int usb_audio_transmit_callback(void);
static void tx_event(transfer_t*){usb_audio_transmit_callback();}
#define USB_USBSTS_URI 64
#define USB_PORTSC1_PR 256
#define AUDIO_INTERFACE 2
#define CDC_STATUS_INTERFACE 1
#define CDC_DATA_INTERFACE 1
static uint32_t USB1_ENDPTSETUPSTAT,USB1_ENDPTCOMPLETE,USB1_ENDPTPRIME,USB1_ENDPTFLUSH,USB1_PORTSC1=USB_PORTSC1_PR,endpointN_notify_mask;
static unsigned usb_configuration,serial_resets;
static void usb_serial_reset(){++serial_resets;}
static void usb_serial_configure(){}
static void endpoint0_receive(void*,unsigned,unsigned){}
void usb_audio_configure(void);
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Reset(){
 AudioStream::memory_pool_available_mask[0]=255;AudioStream::memory_pool_first_mask=0;AudioStream::memory_used=AudioStream::memory_used_max=0;
 for(unsigned i=0;i<8;++i){pool[i]={};pool[i].memory_pool_index=i;}
 AudioInputUSB::incoming_left=AudioInputUSB::incoming_right=AudioInputUSB::ready_left=AudioInputUSB::ready_right=nullptr;AudioInputUSB::incoming_count=0;AudioInputUSB::receive_flag=0;
 AudioOutputUSB::left_1st=AudioOutputUSB::right_1st=AudioOutputUSB::left_2nd=AudioOutputUSB::right_2nd=nullptr;AudioOutputUSB::offset_1st=0;
 failed_allocations=0;usb_audio_input_active=false;usb_audio_receive_setting=usb_audio_transmit_setting=0;
}
"""
lifecycle='void BusReset(){unsigned status=USB_USBSTS_URI;'+bus_reset+'}\n'
lifecycle+='void TxAlt(unsigned value){struct{unsigned wIndex,wValue;}setup{AUDIO_INTERFACE+1,value};'+tx_alt+'}\n'
lifecycle+='void RxAlt(unsigned value){struct{unsigned wIndex,wValue;}setup{AUDIO_INTERFACE+2,value};'+rx_alt+'}\n'
lifecycle+='void Configure(){struct{unsigned wValue;}setup{1};'+config+'}\n'
parts=[extract(stream,'audio_block_t * AudioStream::allocate(void)').replace('return NULL;','++failed_allocations;return NULL;'),extract(stream,'void AudioStream::release(audio_block_t *block)'),extract(stream,'audio_block_t * AudioStream::receiveWritable(unsigned int index)'),extract(stream,'audio_block_t * AudioStream::receiveReadOnly(unsigned int index)')]
cases=[(False,original)] if a.original_only else [(False,original),(True,patched)]
for fixed,text in cases:
 functions=parts+[extract(text,'void AudioInputUSB::begin(void)'),extract(text,'static void copy_to_buffers('),extract(text,'void usb_audio_receive_callback(unsigned int len)'),extract(text,'void AudioInputUSB::update(void)'),extract(text,'static void copy_from_buffers('),extract(text,'void AudioOutputUSB::update(void)'),extract(text,'unsigned int usb_audio_transmit_callback(void)'),extract(text,'void usb_audio_configure(void)')]
 # The original has the known zero-fill addressing defect. Normalize that sole
 # operation in the negative control so this test isolates ownership failures.
 functions=[f.replace('memset(usb_audio_transmit_buffer + len, 0, num * 4);','memset((uint32_t *)usb_audio_transmit_buffer + len, 0, num * 4);') for f in functions]
 main=r"""
int main(){
 Reset();AudioOutputUSB out;
 // RX activated after the audio pool and firmware inputs are active.
 RxAlt(1);usb_audio_receive_callback(128*4);
 unsigned retained=AudioStream::memory_used;
 Check(retained==@RETAINED@,"uninstantiated RX retained-block demand");
 // Core bus reset leaves audio pointers/settings alone. Reconfiguration primes
 // transfers; toggling RX/TX flags follows SET_INTERFACE's production writes.
 auto reset_count=serial_resets;BusReset();
 Check(serial_resets==reset_count+1 && USB1_ENDPTFLUSH==0xffffffffu && AudioStream::memory_used==retained,"production bus reset flushes CDC/endpoints without freeing audio ownership");
 Configure();RxAlt(0);TxAlt(0);out.update();
 Check(AudioStream::memory_used==retained,"retained RX survives configuration/disabled TX");
 // Two MQS queue blocks, plus mono output shared by MQS/L/R; one free block
 // after four unused RX blocks: writable L succeeds, writable R fails.
 auto* mqs1=AudioStream::allocate();auto* mqs2=AudioStream::allocate();auto* mono=AudioStream::allocate();Check(mqs1&&mqs2&&mono,"present graph inputs");
 for(unsigned i=0;i<128;++i)mono->data[i]=100+i;mono->ref_count=3;
 out.inputQueue[0]=out.inputQueue[1]=mono;TxAlt(1);out.update();
 Check(failed_allocations==(@FIXED@?0:2),"writable-right plus replacement-right failure count");
 Check((AudioOutputUSB::left_1st!=nullptr)==@FIXED@,"valid shared inputs vs writable-copy/queue exhaustion");
 unsigned bytes=usb_audio_transmit_callback();Check(bytes==176||bytes==180,"packet length unchanged");
 if(@FIXED@){for(unsigned i=0;i<bytes/4;++i)Check(usb_audio_transmit_buffer[2*i]==100+i && usb_audio_transmit_buffer[2*i+1]==100+i,"exact stereo payload");}
 else {for(unsigned i=0;i<bytes/2;++i)Check(usb_audio_transmit_buffer[i]==0,"pool exhaustion zeros despite present inputs");}
 TxAlt(0);out.update();Check(AudioOutputUSB::offset_1st==0 && !AudioOutputUSB::left_1st && !AudioOutputUSB::right_1st,"disabled TX releases queue/offset");
 AudioStream::release(mqs1);AudioStream::release(mqs2);AudioStream::release(mono);
 Check(AudioStream::memory_used==retained,"no TX leak or double release");
 // Normal TX queue overflow frees exactly the discarded pair; shared source
 // reference stays owned by MQS, and disabled streaming frees both queued pairs.
 if(@FIXED@){
  Reset();TxAlt(1);
  for(unsigned pass=0;pass<3;++pass){auto* b=AudioStream::allocate();Check(b!=nullptr,"queue input");b->ref_count=3;out.inputQueue[0]=out.inputQueue[1]=b;out.update();AudioStream::release(b);}
  Check(AudioStream::memory_used==4,"two queued TX pairs, oldest pair discarded");
  TxAlt(0);out.update();Check(AudioStream::memory_used==0,"full TX queue cleanup");
 }
 // Missing inputs are different: with room the core queues allocated silence.
 Reset();usb_audio_transmit_setting=1;out.update();Check(AudioOutputUSB::left_1st&&AudioOutputUSB::right_1st,"missing inputs synthesize silence, not queue failure");
 bytes=usb_audio_transmit_callback();for(unsigned i=0;i<bytes/2;++i)Check(usb_audio_transmit_buffer[i]==0,"missing input silence");
 TxAlt(0);out.update();Check(AudioStream::memory_used==0,"missing input cleanup");
 // Normal instantiated RX still retains incoming/ready pairs and services them.
 Reset();AudioInputUSB in;in.begin();RxAlt(1);
 for(unsigned i=0;i<128;++i)rx_buffer[i]=((300+i)<<16)|(100+i);
 usb_audio_receive_callback(128*4);Check(AudioStream::memory_used==4,"enabled RX incoming plus ready pairs");
 for(unsigned i=0;i<128;++i)Check(AudioInputUSB::ready_left->data[i]==100+i&&AudioInputUSB::ready_right->data[i]==300+i,"normal USB input PCM unchanged");
 BusReset();Configure();Check(AudioStream::memory_used==4,"configuration does not lose input ownership");
 in.update();Check(AudioStream::memory_used==2,"enabled RX update releases ready pair");
 auto* incoming_l=AudioInputUSB::incoming_left;auto* incoming_r=AudioInputUSB::incoming_right;
 RxAlt(0);Configure();Check(AudioInputUSB::incoming_left==incoming_l&&AudioInputUSB::incoming_right==incoming_r,"input alternate/configuration retains valid partial buffers");
 // Complete another block; ordinary consumer service works after reconfiguration.
 RxAlt(1);usb_audio_receive_callback(128*4);in.update();Check(AudioStream::memory_used==2,"post-configuration consumer remains functional");
 AudioStream::release(AudioInputUSB::incoming_left);AudioStream::release(AudioInputUSB::incoming_right);Check(AudioStream::memory_used==0,"enabled RX test cleanup");
 Reset();usb_audio_receive_callback(0);Check(AudioStream::memory_used==@ZERO@,"zero-length RX without consumer");
 if(@FIXED@){for(unsigned i=0;i<10;++i){BusReset();Configure();RxAlt(1);usb_audio_receive_callback(176);RxAlt(0);out.update();}Check(AudioStream::memory_used==0,"repeated no-consumer lifecycle has no allocation/leak");}
 std::cout<<"@NAME@: production RX/allocator/receiveWritable/TX/configuration passed; retained="<<retained<<"\n";
}
""".replace('@RETAINED@','0' if fixed else '4').replace('@FIXED@','true' if fixed else 'false').replace('@ZERO@','0' if fixed else '2').replace('@NAME@','patched' if fixed else 'original failure reproduced')
 src=a.build/('fixed.cpp' if fixed else 'original.cpp');src.write_bytes((common+lifecycle+'\n'.join(functions)+main).encode());exe=src.with_suffix('.exe')
 subprocess.run([a.cxx,'-std=c++14',str(src),'-o',str(exe)],check=True);subprocess.run([str(exe.resolve())],check=True)
