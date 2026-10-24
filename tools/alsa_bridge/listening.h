#pragma once
#include "listening_control.h"
#include "click_diagnostics.h"
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <new>
#include <fstream>
#include <filesystem>
namespace listening {
constexpr unsigned kLimitSeconds=180;
constexpr uint64_t kLimitFrames=7938000, kLimitRecords=1000000;
struct Shared {
    clicks::Queue queue;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> lost{0};
};
inline const char* Name(unsigned kind){
    switch(kind){case Start:return "START";case Pause:return "PAUSE";case Stop:return "STOP";
        case Continue:return "CONTINUE";case Disconnect:return "DISCONNECT";case Exit:return "EXIT";
        case Reconnect:return "BRIDGE_OR_HOST_RECONNECT";case Error:return "ERROR";case EvidenceLoss:return "RECORDING_LOSS";default:return "OBSERVATION";}
}
inline uint64_t BinaryIdentity(const char* path){
    std::ifstream f(path,std::ios::binary);uint64_t h=14695981039346656037ULL;char b[4096];
    if(!f)return 0;
    while(f){f.read(b,sizeof(b));for(std::streamsize i=0;i<f.gcount();++i){h^=static_cast<unsigned char>(b[i]);h*=1099511628211ULL;}}
    return h;
}
// Child-only writer. Any missing/partial manifest or completion means incomplete.
// No final analysis. Disk stalls cannot block the bridge/launcher indefinitely.
inline void Worker(Shared* shared,const char* base) {
    std::unique_ptr<clicks::Trace> trace;FILE* log=nullptr;
    std::string metadata,previous;uint64_t initial_loss=0;unsigned segment=0;
    const auto bridge_identity=BinaryIdentity("/proc/self/exe");
    auto ui_path=std::filesystem::read_symlink("/proc/self/exe").parent_path()/"BroTrackerTerminal";
    const auto ui_identity=BinaryIdentity(ui_path.c_str());
    auto manifest=[&](bool complete,const char* ended){
        if(!trace)return;
        const auto path=trace->Directory()+"/manifest.json";FILE* f=std::fopen(path.c_str(),"w");if(!f)return;
        std::fprintf(f,"{\"schema\":1,\"mode\":\"bridge_audible_test\",\"pid\":%ld,\"segment\":%u,"
            "\"bridge_fnv1a64\":\"%016llx\",\"btx_fnv1a64\":\"%016llx\",\"build\":\"%s %s\","
            "\"rate\":44100,\"channels\":2,\"format\":\"S16_LE\",\"clock\":\"CLOCK_MONOTONIC\","
            "\"limit_seconds\":%u,\"limit_frames_per_stream\":%llu,\"limit_records\":%llu,"
            "\"handoff_lost\":%llu,\"ended_by\":\"%s\",\"incomplete\":%s,"
            "\"host_events_are_audible_onset\":false,\"silence_inserted\":false}\n",
            long(getpid()),segment,(unsigned long long)bridge_identity,(unsigned long long)ui_identity,__DATE__,__TIME__,kLimitSeconds,(unsigned long long)kLimitFrames,(unsigned long long)kLimitRecords,
            (unsigned long long)(shared->lost.load()-initial_loss),ended,complete?"false":"true");
        std::fclose(f);
    };
    auto finish=[&](const char* why){
        if(!trace)return;
        if(shared->lost.load()!=initial_loss)trace->Incomplete();
        trace->Finish();
        // completion.json is authoritative for internal queue/disk/limit losses.
        std::ifstream f(trace->Directory()+"/completion.json");std::string c{std::istreambuf_iterator<char>(f),{}};
        const bool good=c.find("\"incomplete\":false")!=std::string::npos && !trace->Failed() && shared->lost.load()==initial_loss;
        manifest(good && std::strcmp(why,"bridge_exit_or_recovery"),why);previous=trace->Directory();
        if(log){std::fprintf(log,"finalized=%s incomplete=%s\n",why,good?"no":"yes");std::fclose(log);log=nullptr;}
        trace.reset();
    };
    clicks::Record r;
    for(;;){
        if(!shared->queue.Pop(r)) {
            if(shared->done.load()){if(!shared->queue.Pop(r))break;}
            else {usleep(2000);continue;}
        }
        if(r.kind==clicks::Metadata){metadata=std::string(reinterpret_cast<char*>(r.pcm.data()));continue;}
        if(r.kind==clicks::Transport) {
            if(r.step==Start || r.step==Continue || r.step==Reconnect){
                finish("new_activation");
                initial_loss=static_cast<uint64_t>(r.delay);++segment;
                try {
                    trace=std::make_unique<clicks::Trace>(base,clicks::kSlots,nullptr,kLimitSeconds,true,kLimitFrames,kLimitRecords);
                    // Incomplete until both completion markers commit; killed workers
                    // retain this initial marker and partial WAVs, never a false pass.
                    manifest(false,"active_or_interrupted");
                    FILE* params=std::fopen((trace->Directory()+"/alsa-parameters.txt").c_str(),"w");
                    if(params){std::fputs(metadata.c_str(),params);std::fclose(params);}else trace->Incomplete();
                    log=std::fopen((trace->Directory()+"/diagnostic.log").c_str(),"w");
                    if(log)std::fprintf(log,"activation=%s host_ns=%lld bridge_ns=%llu previous_segment=%s\n",Name(r.step),(long long)r.avail,(unsigned long long)r.ns,previous.c_str());
                    else trace->Incomplete();
                    std::fprintf(stderr,"Listening recording: %s\n",trace->Directory().c_str());
                } catch(const std::exception& e){std::fprintf(stderr,"Listening recording unavailable: %s; audio continues\n",e.what());trace.reset();}
            }
            if(log)std::fprintf(log,"%s host_ns=%lld bridge_ns=%llu captured=%llu accepted=%llu discarded=%llu\n",Name(r.step),(long long)r.avail,(unsigned long long)r.ns,(unsigned long long)r.captured,(unsigned long long)r.played,(unsigned long long)r.discarded);
            if(trace)trace->Emit(r,nullptr,true);
            if(r.step!=Start && r.step!=Continue && r.step!=Reconnect && r.step!=Observation)finish(Name(r.step));
        } else if(trace){
            if(log && (r.kind==clicks::Recovery || r.kind==clicks::Discard))
                std::fprintf(log,"bridge_event=%u ns=%llu source=%llu accepted=%llu discarded=%llu queued=%u result=%lld step=%u\n",r.kind,(unsigned long long)r.ns,(unsigned long long)r.source,(unsigned long long)r.played,(unsigned long long)r.discarded,r.queued,(long long)r.result,r.step);
            trace->Emit(r,(r.kind==clicks::Read || r.kind==clicks::Write)?r.pcm.data():nullptr,true);
        }
        if(trace && !trace->Accepting())finish("recording_limit_or_failure");
    }
    finish("bridge_exit_or_recovery");
}
class Observer {
    Mapping control;
    Shared* shared=nullptr;pid_t child=-1;
    uint32_t lost=0;bool active=false,broken=false,evidence_lost=false;
public:
    explicit Observer(void (*worker_gate)()=nullptr){
        const auto* base=std::getenv("BROTRACKER_LISTEN_BASE");
        if(!control.Enabled() || !base || base[0]!='/')return;
        auto* memory=mmap(nullptr,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
        if(memory==MAP_FAILED){std::fprintf(stderr,"Listening queue unavailable; audio continues\n");return;}
        shared=new(memory) Shared;const auto parent=getpid();child=fork();
        if(child==0){
            if(prctl(PR_SET_PDEATHSIG,SIGKILL)!=0 || getppid()!=parent)_exit(1);
            signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);
            try{if(worker_gate)worker_gate();Worker(shared,base);}catch(...){std::fprintf(stderr,"Listening worker failed; partial evidence retained\n");}
            _exit(0);
        }
        if(child<0){std::fprintf(stderr,"Listening worker fork failed; audio continues\n");shared->~Shared();munmap(shared,sizeof(Shared));shared=nullptr;}
    }
    bool Enabled() const {return shared;}
    void Push(clicks::Record r,const void* pcm=nullptr) noexcept {
        if(!shared)return;
        r.ns=Now();
        if(r.kind!=clicks::Transport && r.kind!=clicks::Metadata) {
            if(evidence_lost)return;
            // Reserve control slots. First recording loss closes evidence instead
            // of splicing later PCM into a deceptively continuous WAV.
            if(shared->queue.Size()>=clicks::kSlots-128) {
                ++shared->lost;evidence_lost=broken=true;r.kind=clicks::Transport;r.step=EvidenceLoss;
                shared->queue.Push(r);return;
            }
        }
        if(pcm && r.result>0 && r.result<=int64_t(clicks::kChunk))std::memcpy(r.pcm.data(),pcm,size_t(r.result)*4);
        if(!shared->queue.Push(r)){++shared->lost;evidence_lost=broken=true;}
    }
    void Parameters(const std::string& text){
        clicks::Record r;r.kind=clicks::Metadata;std::snprintf(reinterpret_cast<char*>(r.pcm.data()),r.pcm.size(),"%s",text.c_str());Push(r);
    }
    void Poll(uint64_t captured,uint64_t played,uint64_t discarded,uint32_t queued) noexcept {
        if(!shared)return;
        auto report=[&](const ControlEvent& e){
            clicks::Record r;r.kind=clicks::Transport;r.step=e.kind;r.avail=e.ns;r.result=e.tick;r.source=captured;r.state=e.tempo;
            r.delay=shared->lost.load();r.captured=captured;r.played=played;r.discarded=discarded;r.queued=queued;Push(r);
            if(e.kind!=Observation){active=e.kind==Start || e.kind==Continue || e.kind==Reconnect;if(active){broken=false;evidence_lost=false;}}
        };
        ControlEvent e;for(unsigned n=0;n<64 && control.Pop(e);++n)report(e);
        if(control.Lost()!=lost){lost=control.Lost();++shared->lost;report({Now(),0,Disconnect,0});broken=true;}
        // A restarted bridge records an explicitly labeled separate segment, never
        // claims continuity through its predecessor's failure.
        if(!broken && !active && (control.State()==Start || control.State()==Continue || control.State()==Reconnect))report({Now(),0,Reconnect,0});
    }
    void Emit(clicks::Record r,const void* pcm=nullptr) noexcept {if(active)Push(r,pcm);}
    ~Observer(){
        if(!shared)return;
        shared->done.store(true);
        const auto deadline=Now()+2000000000ULL;bool reaped=false;
        while(Now()<deadline){if(waitpid(child,nullptr,WNOHANG)==child){reaped=true;break;}usleep(2000);}
        if(!reaped){kill(child,SIGKILL);std::fprintf(stderr,"Listening finalization timeout; absent/incomplete markers remain authoritative\n");
            // SIGKILL can remain pending in uninterruptible kernel I/O. Never wait
            // indefinitely; kernel reparents/reaps if it cannot exit immediately.
            for(unsigned n=0;n<25;++n){if(waitpid(child,nullptr,WNOHANG)==child)break;usleep(2000);}}
        munmap(shared,sizeof(Shared));
    }
};
}
