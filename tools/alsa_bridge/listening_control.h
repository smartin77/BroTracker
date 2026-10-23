#pragma once
// Private launcher-owned tmpfs mapping. BTX is sole publisher; bridge sole consumer.
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
namespace listening {
enum Event { Start=1,Pause,Stop,Continue,Disconnect,Exit,Observation,Reconnect,Error,EvidenceLoss };
inline uint64_t Now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct ControlEvent {uint64_t ns=0,tick=0;uint32_t kind=0,tempo=0;};
struct Control {uint32_t in,out,lost,state;ControlEvent slots[64];};
static_assert(sizeof(Control)<=4096,"private mapping capacity");
class Mapping {
protected:
    Control* control=nullptr;
public:
    Mapping(){
        const auto* path=std::getenv("BROTRACKER_LISTEN_CONTROL");if(!path || !*path)return;
        int fd=open(path,O_RDWR|O_NOFOLLOW);if(fd<0)return;
        struct stat s{};
        if(fstat(fd,&s) || !S_ISREG(s.st_mode) || s.st_uid!=getuid() || s.st_size!=4096){close(fd);return;}
        auto* p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);
        if(p!=MAP_FAILED)control=static_cast<Control*>(p);
    }
    Mapping(const Mapping&)=delete;Mapping& operator=(const Mapping&)=delete;
    ~Mapping(){if(control)munmap(control,4096);}
    bool Enabled() const {return control!=nullptr;}
    uint32_t Lost() const {return control?__atomic_load_n(&control->lost,__ATOMIC_ACQUIRE):0;}
    unsigned State() const {return control?__atomic_load_n(&control->state,__ATOMIC_ACQUIRE):0;}
    void Publish(unsigned kind,unsigned tempo=0,uint64_t tick=0){
        if(!control)return;
        if(kind!=Observation)__atomic_store_n(&control->state,kind,__ATOMIC_RELEASE);
        const auto in=__atomic_load_n(&control->in,__ATOMIC_RELAXED),out=__atomic_load_n(&control->out,__ATOMIC_ACQUIRE);
        if(in-out>=64){__atomic_fetch_add(&control->lost,1,__ATOMIC_RELEASE);return;}
        control->slots[in%64]={Now(),tick,kind,tempo};__atomic_store_n(&control->in,in+1,__ATOMIC_RELEASE);
    }
    bool Pop(ControlEvent& e){
        if(!control)return false;
        const auto out=__atomic_load_n(&control->out,__ATOMIC_RELAXED),in=__atomic_load_n(&control->in,__ATOMIC_ACQUIRE);
        if(out==in)return false;
        e=control->slots[out%64];__atomic_store_n(&control->out,out+1,__ATOMIC_RELEASE);return true;
    }
};
}
