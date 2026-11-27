#pragma once
#include "Audio.h"
#include <cstdarg>
#include <cstdio>
#include <string>
#define DMAMEM
inline unsigned millis(){return FakeStreamPlatform::ticks/1000;}
inline void yield(){}
struct DeviceSerial {
    std::string input,output;
    void begin(unsigned){}
    unsigned available()const{return input.size();}
    int read(){const auto c=input.front();input.erase(0,1);return c;}
    void printf(const char* format,...){char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);output+=text;}
    void println(const char* text){output+=text;output+='\n';}
};
inline DeviceSerial Serial;
