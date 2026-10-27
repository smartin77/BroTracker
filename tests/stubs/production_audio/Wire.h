#pragma once
#include "Audio.h"
struct FakeWire {
 unsigned reg=0,bytes=0,index=0;
 void beginTransmission(std::uint8_t address){AudioFake::Foreground();if(address!=0xa)AudioFake::state.badGuard=true;reg=bytes=index=0;}
 std::size_t write(std::uint8_t b){reg=(reg<<8)|b;++bytes;return AudioFake::state.readError==1?0:1;}
 unsigned endTransmission(bool){if(bytes!=2)AudioFake::state.badGuard=true;return AudioFake::state.readError==2?2:0;}
 unsigned requestFrom(std::uint8_t,std::uint8_t n){if(n!=2)AudioFake::state.badGuard=true;++AudioFake::state.reads;return AudioFake::state.readError==3?1:2;}
 int available(){return AudioFake::state.readError==4?1:2;}
 int read(){auto& g=AudioFake::state;if(g.readError==5)return -1;unsigned v=reg==0x24?g.ana:reg==0x22?g.hp:reg==0x10?0x3c3c:reg==0xe?0:reg==0xa?0x10:reg==4?4:0x30;
 if(int(g.reads)-1==g.mismatch)v^=reg==0xe?4:0x10;return index++==0?int(v>>8):int(v&255);}
};inline FakeWire Wire;
