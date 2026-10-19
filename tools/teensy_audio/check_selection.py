"""Activation IRQ model over the actual production method and installed core guards.

Models internal CPSIE in DMA allocation, patching and AudioMemory; pends DMA and
software IRQs during every stage. Does not emulate clocks/DMA or prove audio quality.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--framework',type=Path,default=Path.home()/'.platformio/packages/framework-arduinoteensy')
    parser.add_argument('--cxx',default='g++')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    firmware=root/'firmware/teensy/BroTracker'
    args.build.mkdir(parents=True,exist_ok=True)
    files={p:(args.framework/p).read_bytes() for p in (
        'cores/teensy4/AudioStream.cpp','cores/teensy4/AudioStream.h',
        'cores/teensy4/DMAChannel.cpp','cores/teensy4/DMAChannel.h',
        'cores/teensy4/startup.c','cores/teensy4/imxrt.h','libraries/Audio/output_pt8211.cpp','libraries/Audio/output_mqs.cpp')}
    stream=files['cores/teensy4/AudioStream.cpp'].decode()
    startup=files['cores/teensy4/startup.c'].decode()
    dma=files['cores/teensy4/DMAChannel.h'].decode()
    assert '#define __enable_irq()  __asm__ volatile("CPSIE i"' in files['cores/teensy4/imxrt.h'].decode()
    assert 'NVIC_SET_PRIORITY(i, 128)' in startup
    assert 'NVIC_SET_PRIORITY(IRQ_SOFTWARE, 208)' in stream
    attach=dma.split('void attachInterrupt(void (*isr)(void))',1)[1].split('}',1)[0]
    assert 'NVIC_ENABLE_IRQ' in attach and 'NVIC_SET_PRIORITY' not in attach
    for part in (stream.split('int AudioConnection::connect(void)',1)[1].split('int AudioConnection::connect(AudioStream',1)[0],
                 stream.split('void AudioStream::initialize_memory',1)[1].split('// Allocate',1)[0],
                 files['cores/teensy4/DMAChannel.cpp'].decode().split('void DMAChannel::begin',1)[1]):
        assert '__enable_irq()' in part
    for p in ('libraries/Audio/output_pt8211.cpp','libraries/Audio/output_mqs.cpp'):
        text=files[p].decode()
        assert 'dma.attachInterrupt(isr);' in text and 'update_responsibility = update_setup();' in text
        assert 'NVIC_SET_PRIORITY' not in text
    source=(firmware/'production_audio_output.cpp').read_text()
    begin='''uint32_t mask, priority_mask;
        asm volatile("mrs %0, primask\\n\\tmrs %1, basepri\\n\\tcpsid i\\n\\tmsr basepri_max, %2\\n\\tdsb\\n\\tisb"
            : "=&r"(mask), "=&r"(priority_mask) : "r"(16u) : "memory");'''
    end='''asm volatile("msr primask, %0\\n\\tmsr basepri, %1\\n\\tdsb\\n\\tisb"
            :: "r"(mask), "r"(priority_mask) : "memory");'''
    assert source.count(begin)==source.count(end)==1
    source=source.replace(begin,'uint32_t mask=primask, priority_mask=basepri; primask=1; if(!basepri || basepri>16) basepri=16; Pump();')
    source=source.replace(end,'primask=mask; Pump(); basepri=priority_mask; Pump();')
    stub=r'''
#pragma once
#include <cstdint>
#include <cassert>
#include <stdexcept>
#define AUDIO_INTERFACE 2
inline uint32_t primask=0,basepri=0;
inline unsigned owners=0,pools=0,patches=0,upstream=0,enables=0,attempts=0,dma_runs=0,updates=0;
inline int kinds[3]{},channels[3]{};
inline unsigned expected_patches=0;
inline bool hardware_done=false,dma_enabled=false,software_enabled=false,dma_pending=false,software_pending=false;
bool SelectionCommitted();
inline void Ready(){if(!hardware_done || patches!=expected_patches || pools!=1 || !SelectionCommitted()) throw std::runtime_error("premature audio interrupt");}
inline void Pump(){
    ++attempts;
    if(!primask && (!basepri || 128<basepri) && dma_enabled && dma_pending){Ready();dma_pending=false;++dma_runs;software_pending=true;}
    if(!primask && (!basepri || 208<basepri) && software_enabled && software_pending){Ready();software_pending=false;++updates;}
}
inline void Pend(){dma_pending=software_pending=true;Pump();}
inline void EnableIrq(){primask=0;++enables;Pend();}
class AudioStream {public:int kind=0;};
class AudioOutputUSB:public AudioStream {public:AudioOutputUSB(){kind=3;}};
inline void Begin(){
    assert(!owners++); // DMAChannel::begin enables PRIMASK before completion.
    EnableIrq();
    dma_enabled=software_enabled=true;Pend(); // IRQs enabled while constructor incomplete.
}
class AudioOutputMQS:public AudioStream {public:AudioOutputMQS(){Begin();kind=1;hardware_done=true;Pend();}};
class AudioOutputPT8211:public AudioStream {public:AudioOutputPT8211(){Begin();kind=2;hardware_done=true;Pend();}};
class AudioConnection {public:AudioConnection(AudioStream&,int,AudioStream& destination,int channel){
    if(!owners){++upstream;EnableIrq();return;} // Existing legacy mixer patches.
    assert(patches<3);kinds[patches]=destination.kind;channels[patches]=channel;
    primask=1;EnableIrq(); // connect() enables before returning to its constructor.
    ++patches;Pend();
}};
inline void InitializeMemory(unsigned n){assert(owners==1 && !pools && n==8);primask=1;Pend();++pools;EnableIrq();}
#define AudioMemory(n) InitializeMemory(n)
'''
    harness=r'''
#include "production_audio_output.h"
#include <cstdio>
BroTracker::ProductionAudioOutput output;
AudioStream input,mixer;
AudioOutputUSB usb;
bool SelectionCommitted(){return output.Active()!=BroTracker::AudioOutputChoice::Default;}
int main(int argc,char** argv){try{
    using namespace BroTracker;
    assert(argc==5 && !owners && !pools && !patches);
    const auto request=argv[1][0]=='p' ? AudioOutputChoice::Pt8211 : argv[1][0]=='b' ? AudioOutputChoice::Bridge : AudioOutputChoice::Default;
    const auto expected=request==AudioOutputChoice::Default ? AudioOutputChoice::Bridge : request;
    const bool legacy=argv[4][0]=='l';
    if(legacy){AudioConnection a(input,0,mixer,0),b(input,0,mixer,1),c(input,0,mixer,2);assert(upstream==3);}
    expected_patches=expected==AudioOutputChoice::Pt8211 ? 2 : 3;
    primask=argv[2][0]=='1';basepri=static_cast<unsigned>(std::strtoul(argv[3],nullptr,10));
    const auto original_mask=primask,original_priority=basepri;
    auto& source=legacy ? mixer : input;
    assert(!output.Select(request,source,usb));
    assert(output.Active()==expected && primask==original_mask && basepri==original_priority && owners==1 && pools==1);
    assert(patches==expected_patches && enables>=patches+2 && attempts>patches+2);
    if(expected==AudioOutputChoice::Pt8211) assert(kinds[0]==2 && kinds[1]==2 && channels[0]==0 && channels[1]==1);
    else assert(kinds[0]==1 && kinds[1]==3 && kinds[2]==3 && channels[1]==0 && channels[2]==1);
    if(original_mask || original_priority) assert(!dma_runs && !updates);
    else assert(dma_runs && updates);
    const auto count=patches;
    assert(!output.Select(expected,source,usb));assert(!output.Select(AudioOutputChoice::Default,source,usb));
    const auto other=expected==AudioOutputChoice::Bridge ? AudioOutputChoice::Pt8211 : AudioOutputChoice::Bridge;
    assert(output.Select(other,source,usb));
    assert(output.Active()==expected && owners==1 && pools==1 && patches==count && primask==original_mask && basepri==original_priority);
    primask=basepri=0;Pump();assert(dma_runs && updates);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 3;}}
'''
    (args.build/'Audio.h').write_text(stub)
    (args.build/'test.cpp').write_text(harness)
    def compile_one(code,name):
        cpp=args.build/(name+'.cpp');cpp.write_text(code)
        binary=args.build/(name+'.exe')
        subprocess.run([args.cxx,'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(args.build),'-I'+str(firmware),
                        str(cpp),str(args.build/'test.cpp'),'-o',str(binary)],check=True)
        return binary.resolve()
    binary=compile_one(source,'activation')
    count=0
    for graph in ('pattern','legacy'):
        for route in ('bridge','pt8211','default'):
            for mask in ('0','1'):
                for priority in ('0','16','64'):
                    subprocess.run([str(binary),route,mask,priority,graph],check=True);count+=1
    negative=compile_one(source.replace('if(!basepri || basepri>16) basepri=16;',''),'primask-only')
    for graph in ('pattern','legacy'):
        failed=subprocess.run([str(negative),'bridge','0','0',graph],capture_output=True,text=True)
        assert failed.returncode==3 and 'premature audio interrupt' in failed.stderr
    receipt={'passed_cases':count,'negative_controls_reproduced':2,'installed_source_sha256':{p:hashlib.sha256(b).hexdigest() for p,b in files.items()},
             'limit':'Modeled IRQ delivery and installed source guards, not real DMA/audio/listening validation.'}
    (args.build/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'{count} cases passed: pending DMA/software IRQs, internal CPSIE, both graphs/routes, PRIMASK/BASEPRI restoration; 2 PRIMASK-only negative controls failed as expected')


if __name__=='__main__':main()
