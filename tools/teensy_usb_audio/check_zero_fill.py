# Compile the actual patched zero-fill expression, with original negative controls.
import argparse
import hashlib
import subprocess
from pathlib import Path
from patch_core import prepare, patch_bytes, ORIGINAL, PATCHED, VERSION
parser=argparse.ArgumentParser()
parser.add_argument('--framework',type=Path,required=True)
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--cxx',default='g++')
args=parser.parse_args()
original,patched=prepare(args.framework,args.build)
source=original.read_bytes()
assert patched.read_bytes()==patch_bytes(source,VERSION)
for data,version in [(source,'unknown'),(source+b'\n',VERSION),(source+ORIGINAL,VERSION)]:
    try:patch_bytes(data,version)
    except RuntimeError:pass
    else:raise AssertionError('incompatible source accepted')
line=next(x.strip() for x in patched.read_text().splitlines() if PATCHED.decode() in x)
cpp=r'''
#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using Operation=void (*)(uint16_t*,unsigned,unsigned);
void Patched(uint16_t* usb_audio_transmit_buffer,unsigned len,unsigned num){ @PATCHED@ }
void Original(uint16_t* usb_audio_transmit_buffer,unsigned len,unsigned num){ @ORIGINAL@ }
bool Check(Operation op,unsigned target,unsigned len){
 std::array<uint16_t,106> storage;
 for(unsigned i=0;i<storage.size();++i)storage[i]=uint16_t(0x5100+i);
 auto* buffer=storage.data()+8;
 auto before=storage;
 op(buffer,len,target-len);
 const auto* actual=reinterpret_cast<const unsigned char*>(storage.data());
 const auto* expected=reinterpret_cast<const unsigned char*>(before.data());
 const unsigned prefix=16, end=prefix+target*4;
 for(unsigned i=0;i<sizeof(storage);++i){
  const unsigned char value=(i>=prefix+len*4 && i<end)?0:expected[i];
  if(actual[i]!=value)return false;
 }
 return true;
}
int main(){
 unsigned cases=0,negative=0;
 for(unsigned target:{44u,45u})for(unsigned len=0;len<=target;++len){
  if(!Check(Patched,target,len))throw std::runtime_error("patched prefix/tail/guard failure");
  if(!Check(Original,target,len))++negative;
  ++cases;
 }
 if(Check(Original,44,40)||Check(Original,45,39))throw std::runtime_error("original must fail even/odd shortages");
 if(!Check(Original,44,0)||!Check(Original,45,0))throw std::runtime_error("empty control");
 std::cout<<cases<<" exhaustive packet cases passed; original fails "<<negative<<" cases; prefix, stereo tail and outside guards verified\n";
}
'''
cpp=cpp.replace('@PATCHED@',line).replace('@ORIGINAL@',ORIGINAL.decode())
args.build.mkdir(parents=True,exist_ok=True)
cppfile=args.build/'zero_fill_test.cpp';cppfile.write_bytes(cpp.encode())
exe=args.build/'zero_fill_test.exe'
subprocess.run([args.cxx,'-std=c++14','-Wall','-Wextra','-Werror',str(cppfile),'-o',str(exe)],check=True)
subprocess.run([str(exe.resolve())],check=True)
assert hashlib.sha256(original.read_bytes()).hexdigest()==hashlib.sha256(source).hexdigest()
print('Version/source rejection and generated guarded core copy verified; installed source unchanged.')
