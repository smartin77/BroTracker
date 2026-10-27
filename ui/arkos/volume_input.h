#pragma once
#include "ui/volume_repeat.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
// Verified R36H GPIO profile. Never grab a whole gamepad/keyboard or stop ogage.
namespace ArkOSVolume {
constexpr unsigned WordBits=sizeof(unsigned long)*8,Words=(KEY_MAX+WordBits)/WordBits;
inline bool Bit(const unsigned long* bits,unsigned key) noexcept {return bits[key/WordBits] & (1ul<<(key%WordBits));}
inline bool UpdateKey(const input_event& e,bool& up,bool& down) noexcept {
    if(e.type!=EV_KEY || (e.code!=KEY_VOLUMEUP && e.code!=KEY_VOLUMEDOWN) || (e.value!=0 && e.value!=1))return false;
    (e.code==KEY_VOLUMEUP?up:down)=e.value!=0;return true;
}
template<class Read> bool DiscardBeforeArm(Read read) noexcept {
    //0 means drained, negative means lost input, positive consumed/interrupt.
    for(unsigned i=0;i<8;++i){const int result=read();if(result<=0)return result==0;}
    return false; // Never interpret a backlog we could not drain within budget.
}
inline bool VolumeOnlyEvents(unsigned long types) noexcept {
    constexpr auto allowed=(1ul<<EV_SYN)|(1ul<<EV_KEY)|(1ul<<EV_REP);
    return (types&(1ul<<EV_KEY)) && !(types&~allowed);
}
inline bool Eligible(const char* name,const unsigned long* bits) noexcept {
    if(std::strcmp(name,"odroidgo3-keys") || !Bit(bits,KEY_VOLUMEUP) || !Bit(bits,KEY_VOLUMEDOWN))return false;
    for(unsigned k=0;k<=KEY_MAX;++k)if(k!=KEY_VOLUMEUP && k!=KEY_VOLUMEDOWN && Bit(bits,k))return false;
    return true;
}
}
class LinuxVolumeInput {
public:
    ~LinuxVolumeInput(){Close();}
    void Focus(bool value) noexcept {focused_=value;if(!value)repeat_.Cancel();}
    bool Captured() const noexcept {return fd_>=0;}
    int Poll(std::uint32_t now,bool available) noexcept {
        const bool enabled=available && focused_;
        if(!enabled)repeat_.Cancel();
        if(fd_<0) {if(!enabled)return 0;if(!probed_ || now-probed_at_>=1000){probed_=true;probed_at_=now;Open(now);}if(fd_<0)return 0;}
        int steps=0;input_event events[16];
        const auto bytes=read(fd_,events,sizeof(events));
        if(bytes<0 && errno!=EAGAIN && errno!=EINTR){Close();return 0;}
        if(bytes==0 || (bytes>0 && bytes%sizeof(input_event))){Close();return 0;}
        if(bytes>0)for(unsigned i=0;i<unsigned(bytes)/sizeof(input_event);++i) {
            const auto& e=events[i];
            if(e.type==EV_SYN && e.code==SYN_DROPPED){dropping_=true;steps=0;repeat_.Cancel();continue;}
            if(dropping_) {if(e.type==EV_SYN && e.code==SYN_REPORT){dropping_=false;if(!Resync(now)){Close();return 0;}}continue;}
            if(!ArkOSVolume::UpdateKey(e,up_,down_))continue;
            const int step=repeat_.Edge(up_,down_,now);
            if(enabled)steps+=step;else repeat_.Cancel();
        }
        if(!enabled){if(!repeat_.Held())Close();return 0;}
        return steps+repeat_.Tick(now);
    }
private:
    void Close() noexcept {if(fd_>=0){(void)ioctl(fd_,EVIOCGRAB,0);close(fd_);}fd_=-1;repeat_={};up_=down_=dropping_=false;}
    bool Resync(std::uint32_t now) noexcept {
        unsigned long keys[ArkOSVolume::Words]{};
        if(ioctl(fd_,EVIOCGKEY(sizeof(keys)),keys)<0)return false;
        up_=ArkOSVolume::Bit(keys,KEY_VOLUMEUP);down_=ArkOSVolume::Bit(keys,KEY_VOLUMEDOWN);
        (void)repeat_.Edge(up_,down_,now);repeat_.Cancel();return true;
    }
    bool DiscardQueued() noexcept {
        input_event scratch[16];
        return ArkOSVolume::DiscardBeforeArm([&]() {
            const auto n=read(fd_,scratch,sizeof(scratch));
            if(n<0)return errno==EAGAIN?0:errno==EINTR?1:-1;
            return n>0 && n%sizeof(input_event)==0?1:-1;
        });
    }
    void Open(std::uint32_t now) noexcept {
        auto* dir=opendir("/dev/input");if(!dir)return;
        unsigned inspected=0;
        while(inspected++<64) {
            const auto* entry=readdir(dir);if(!entry)break;
            if(std::strncmp(entry->d_name,"event",5))continue;
            char path[320];std::snprintf(path,sizeof(path),"/dev/input/%s",entry->d_name);
            const auto fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
            char name[64]{};unsigned long bits[ArkOSVolume::Words]{},types=0;
            const bool eligible=ioctl(fd,EVIOCGBIT(0,sizeof(types)),&types)>=0 && ArkOSVolume::VolumeOnlyEvents(types) && ioctl(fd,EVIOCGNAME(sizeof(name)-1),name)>=0 && ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)>=0 && ArkOSVolume::Eligible(name,bits);
            if(!eligible || ioctl(fd,EVIOCGRAB,1)<0){close(fd);continue;}
            fd_=fd;if(!DiscardQueued() || !Resync(now))Close();break;
        }
        closedir(dir);
    }
    VolumeRepeat repeat_;int fd_=-1;bool focused_=true,probed_=false,up_=false,down_=false,dropping_=false;
    std::uint32_t probed_at_=0;
};
