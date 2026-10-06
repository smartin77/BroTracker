"""Native regression only; does not build firmware or access hardware."""
import argparse,subprocess
from pathlib import Path
from patch_core import prepare
from lifecycle_core import patch_usb
p=argparse.ArgumentParser();p.add_argument('--framework',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--cxx',default='g++');a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.build.mkdir(parents=True,exist_ok=True)
source=a.framework/'cores/teensy4/usb.c'
original=source.read_bytes();generated=patch_usb(original)
assert generated.count(b'brotracker_usb_lifecycle_event(')==5
try:patch_usb(original+b'\n')
except RuntimeError:pass
else:raise AssertionError('unknown usb.c accepted')
prepare(a.framework,a.build,trace=True)
assert (a.build/'core-patches/usb.c').read_bytes()==generated
ordinary=a.build/'ordinary';prepare(a.framework,ordinary,trace=False)
assert not (ordinary/'core-patches/usb.c').exists()
assert b'brotracker_usb_lifecycle_event' not in (ordinary/'core-patches/usb_audio.cpp').read_bytes()
cpp=r"""
#include "usb_tx_trace.h"
#include <iostream>
#include <stdexcept>
void Check(bool b){if(!b)throw std::runtime_error("history regression");}
int main(){
 constexpr BroTrackerUsbTrace::History initialized;static_assert(initialized.total==0,"constant boot initialization");
 BroTrackerUsbTrace::History h;Check(h.Count()==0);
 for(unsigned i=0;i<300;++i)h.Add(1000+i,i%8+1,i,i+1,i+2);
 Check(h.total==300 && h.Count()==128);
 for(unsigned i=0;i<32;++i)Check(h.At(i).sequence==i); // pre-START evidence retained
 for(unsigned i=32;i<128;++i)Check(h.At(i).sequence==204+i-32);
 auto snapshot=h;h.Add(9999,2,0,0,0);
 Check(snapshot.total==300 && snapshot.At(127).sequence==299 && h.At(127).sequence==300);
 Check(snapshot.At(0).micros==1000 && snapshot.At(127).a==299);
 std::cout<<"History retention/order/overflow/stable snapshot passed; storage "<<sizeof(h)<<" bytes\n";
}
"""
f=a.build/'history_test.cpp';f.write_bytes(cpp.encode());exe=a.build/'history_test.exe'
subprocess.run([a.cxx,'-std=c++14','-Wall','-Wextra','-I'+str(root/'firmware/teensy/BroTracker'),str(f),'-o',str(exe)],check=True)
subprocess.run([str(exe.resolve())],check=True)
# The usb.c boundary must remain valid C, not depend on C++ types.
c=a.build/'boundary.c';c.write_bytes(b'#include "usb_lifecycle_trace.h"\nvoid test(void){brotracker_usb_lifecycle_event(1,2,3,4);}\n')
subprocess.run([a.cxx,'-x','c','-std=c11','-I'+str(root/'firmware/teensy/BroTracker'),'-c',str(c),'-o',str(a.build/'boundary.o')],check=True)
assert source.read_bytes()==original
print('USB lifecycle source guard, isolated generation, ordinary-build exclusion and C boundary passed.')
