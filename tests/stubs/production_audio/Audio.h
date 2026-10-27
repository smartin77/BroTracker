#pragma once
#include <cstddef>
#include <cstdint>
namespace AudioFake {
enum Kind {Source,Usb,Pt,I2s,Mqs};
struct Patch {int source,output,from,to;};
struct State {
 bool guarded=false,badGuard=false,enable=true,line=true,volume=true,unmute=true;
 unsigned pt=0,i2s=0,mqs=0,patches=0,memory=0,enables=0,controls=0,reads=0;
 int readError=0,mismatch=-1;
 unsigned ana=0x36,hp=0x7f7f;float headphone=0;
 Patch patch[3]{};
};inline State state;
inline void Guarded(){if(!state.guarded)state.badGuard=true;}
inline void Foreground(){if(state.guarded)state.badGuard=true;}
}
class AudioStream {public: explicit AudioStream(int k=AudioFake::Source):kind(k){} int kind;};
class AudioOutputUSB:public AudioStream {public:AudioOutputUSB():AudioStream(AudioFake::Usb){}};
class AudioOutputPT8211:public AudioStream {public:AudioOutputPT8211():AudioStream(AudioFake::Pt){AudioFake::Guarded();++AudioFake::state.pt;}};
class AudioOutputI2S:public AudioStream {public:AudioOutputI2S():AudioStream(AudioFake::I2s){AudioFake::Guarded();++AudioFake::state.i2s;}};
class AudioOutputMQS:public AudioStream {public:AudioOutputMQS():AudioStream(AudioFake::Mqs){AudioFake::Guarded();++AudioFake::state.mqs;}};
class AudioConnection {public:AudioConnection(AudioStream& s,int from,AudioStream& d,int to){AudioFake::Guarded();auto& g=AudioFake::state;g.patch[g.patches++]={s.kind,d.kind,from,to};}};
inline void AudioMemory(unsigned n){AudioFake::Guarded();AudioFake::state.memory=n;}
class AudioControlSGTL5000 {
public:
 void setAddress(std::uint8_t address){AudioFake::Foreground();if(address!=0)AudioFake::state.badGuard=true;}
 bool enable(){AudioFake::Foreground();++AudioFake::state.enables;AudioFake::state.ana=0x36;return AudioFake::state.enable;}
 bool muteLineout(){AudioFake::Foreground();++AudioFake::state.controls;AudioFake::state.ana|=0x100;return AudioFake::state.line;}
 bool muteHeadphone(){AudioFake::Foreground();++AudioFake::state.controls;AudioFake::state.ana|=0x10;return true;}
 bool volume(float v){AudioFake::Foreground();++AudioFake::state.controls;auto& g=AudioFake::state;g.headphone=v;unsigned n=128-unsigned(v*129+.499f);g.hp=n|(n<<8);g.ana&=~0x10;return g.volume;}
 bool unmuteHeadphone(){AudioFake::Foreground();++AudioFake::state.controls;AudioFake::state.ana&=~0x10;return AudioFake::state.unmute;}
 // Deliberately no headphoneSelect method: production must not use it.
};
