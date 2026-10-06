#pragma once
#include <atomic>
#include <array>
#include <thread>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <algorithm>

namespace recording {
static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "Recording requires lock-free counters");
struct Stamp {
    timespec wall{}, mono{};
    static Stamp Now() { Stamp s; clock_gettime(CLOCK_REALTIME, &s.wall); clock_gettime(CLOCK_BOOTTIME, &s.mono); return s; }
};
inline std::string Escape(const std::string& s) {
    std::string out;
    for(char c:s) { if(c=='\\') out+="\\\\"; else if(c=='\t') out+="\\t"; else if(c=='\n') out+="\\n"; else if(c=='\r') out+="\\r"; else out+=c; }
    return out;
}
inline bool WriteAll(int fd, const void* data, size_t bytes, ssize_t (*writer)(int,const void*,size_t)=::write) {
    const auto* p=static_cast<const unsigned char*>(data);
    while(bytes) { auto n=writer(fd,p,bytes); if(n<0 && errno==EINTR) continue; if(n<=0) return false; p+=n; bytes-=n; } return true;
}
// Called only outside the audio loop; flock serializes whole records with shells.
inline void Event(const char* dir, const char* event, const std::string& details, Stamp s=Stamp::Now()) {
    if(!dir || !*dir) return;
    int fd=open((std::string(dir)+"/events.tsv").c_str(),O_WRONLY|O_APPEND|O_CREAT,0600);
    if(fd<0) { std::perror("Timeline open"); return; }
    tm local{}; localtime_r(&s.wall.tv_sec,&local); char wall[80], mono[80];
    strftime(wall,sizeof(wall),"%Y-%m-%dT%H:%M:%S%z",&local);
    std::snprintf(mono,sizeof(mono),"%lld.%09ld",static_cast<long long>(s.mono.tv_sec),s.mono.tv_nsec);
    const std::string line=std::string(wall)+"\t"+mono+"\tbridge-source\t"+std::to_string(getpid())+"\t"+event+"\t"+Escape(details)+"\n";
    if(flock(fd,LOCK_EX)==0) { if(!WriteAll(fd,line.data(),line.size()) || fsync(fd)!=0) std::perror("Timeline write"); flock(fd,LOCK_UN); }
    else std::perror("Timeline lock");
    close(fd);
}
constexpr size_t kCapacity=2*1024*1024;
constexpr uint64_t kFrames=7938000;
static_assert(kFrames == 180ULL * 44100, "180-second recording cap");
class Recorder {
    std::array<unsigned char,kCapacity> queue{};
    std::atomic<uint64_t> produced{0}, consumed{0};
    // 0=accepting, 1=normal end, 2=limit, 3=overflow, 4=writer failure, 5=capture never started
    std::atomic<int> reason{0};
    std::atomic<bool> finished{false};
    struct Note { Stamp stamp; char event[32]; char text[160]; };
    std::array<Note,64> notes{};
    std::atomic<unsigned> note_in{0},note_out{0},note_lost{0};
    std::thread worker;
    const char* directory;
    std::string filename;
    int fd=-1;
    uint64_t accepted=0,written=0;
    const uint64_t limit;
    const size_t capacity;
    ssize_t (*write_fn)(int,const void*,size_t);
    void (*empty_queue_hook)();
    std::array<unsigned char,44> Header() {
        std::array<unsigned char,44> h{};
        auto put=[&](int i,uint32_t v,int n){for(int k=0;k<n;++k)h[i+k]=static_cast<unsigned char>(v>>(k*8));};
        std::memcpy(h.data(),"RIFF",4);put(4,36+written,4);std::memcpy(h.data()+8,"WAVEfmt ",8);
        put(16,16,4);put(20,1,2);put(22,2,2);put(24,44100,4);put(28,176400,4);put(32,4,2);put(34,16,2);
        std::memcpy(h.data()+36,"data",4);put(40,written,4);return h;
    }
    void WriteNotes() {
        unsigned out=note_out.load(std::memory_order_relaxed), in=note_in.load(std::memory_order_acquire);
        while(out!=in) { const auto& n=notes[out%notes.size()]; Event(directory,n.event,n.text,n.stamp); ++out; note_out.store(out,std::memory_order_release); }
    }
    void Writer() noexcept {
        try {
            for(;;) {
                WriteNotes();
                auto out=consumed.load(std::memory_order_relaxed), in=produced.load(std::memory_order_acquire);
                if(out!=in) {
                    size_t n=std::min<uint64_t>(in-out,capacity-out%capacity);
                    if(!WriteAll(fd,queue.data()+out%capacity,n,write_fn)) { reason.store(4); std::perror("Recording write"); break; }
                    written+=n; consumed.store(out+n,std::memory_order_release);
                } else {
                    if(empty_queue_hook) empty_queue_hook(); // deterministic race test seam
                    const int completion=reason.load(std::memory_order_acquire);
                    if(completion!=0) {
                        // Completion is published after the last PCM. Our first
                        // empty observation may precede that publication.
                        if(completion==4 || produced.load(std::memory_order_acquire)==out) break;
                        continue;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            WriteNotes();
            auto h=Header();
            bool valid=ftruncate(fd,44+written)==0 && lseek(fd,0,SEEK_SET)>=0 && WriteAll(fd,h.data(),h.size(),write_fn) && fsync(fd)==0;
            if(close(fd)!=0) valid=false;
            fd=-1;
            if(!valid) reason.store(4);
            const int why=reason.load();
            if(why==5) {
                if(unlink(filename.c_str())!=0) std::perror("Remove unstarted recording");
                Event(directory,"recording_cancelled","capture never started; wav="+filename+" frames=0");
                return;
            }
            const char* end=why==2?"duration_limit":why==3?"recording_overflow":why==4?"recording_failure":"recording_end";
            if(why==4) Event(directory,"recording_failure","write/header/fsync/close failed wav="+filename);
            Event(directory,"recording_end","wav="+filename+" reason="+end+" frames="+std::to_string(written/4)+" incomplete="+(why>=3?"yes":"no")+" dropped_timeline_notes="+std::to_string(note_lost.load()));
            std::fprintf(stderr,"Recording: %s wav=%s frames=%llu incomplete=%s\n",end,filename.c_str(),static_cast<unsigned long long>(written/4),why>=3?"yes":"no");
            // Keep servicing bounded source-timestamped xrun notes after the
            // WAV limit/overflow, until audio shuts down; no more PCM accepted.
            while(!finished.load(std::memory_order_acquire)) { WriteNotes(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            WriteNotes();
            Event(directory,"recording_worker_end","wav="+filename+" dropped_timeline_notes="+std::to_string(note_lost.load()));
        } catch(...) { reason.store(4); if(fd>=0){close(fd);fd=-1;} std::fprintf(stderr,"Recording worker failed; audio continues\n"); }
    }
public:
    Recorder(const char* dir,uint64_t frames=kFrames,size_t bytes=kCapacity,ssize_t (*writer)(int,const void*,size_t)=::write, std::thread (*spawn)(void (*)(Recorder*),Recorder*)=nullptr, void (*empty_hook)()=nullptr)
        :directory(dir),limit(frames),capacity(bytes),write_fn(writer),empty_queue_hook(empty_hook) {
        if(bytes<4 || bytes>kCapacity || bytes%4)throw std::runtime_error("Invalid recording queue capacity");
        filename=std::string(dir)+"/capture-"+std::to_string(getpid())+"-XXXXXX.wav";
        fd=mkstemps(&filename[0],4);if(fd<0)throw std::runtime_error("Recording open failed: "+filename+": "+std::strerror(errno));
        auto h=Header();if(!WriteAll(fd,h.data(),h.size(),write_fn)){close(fd);fd=-1;throw std::runtime_error("Recording header failed");}
        try { Event(directory,"recording_setup","wav="+filename+" limit_frames="+std::to_string(limit)); worker=spawn ? spawn([](Recorder* r){r->Writer();},this) : std::thread(&Recorder::Writer,this); }
        catch(...){close(fd);fd=-1;Event(directory,"recording_failure","worker/open setup failed wav="+filename);throw;}
    }
    // Single audio producer: bounded copies/atomics only. No disk, locks or heap.
    void Push(const void* data,size_t frames) noexcept {
        if(reason.load(std::memory_order_acquire)!=0)return;
        auto n=std::min<uint64_t>(frames,limit-accepted)*4;
        auto in=produced.load(std::memory_order_relaxed),out=consumed.load(std::memory_order_acquire);
        if(n>capacity-(in-out)){NoteEvent("recording_overflow","queue_full; accepting stopped without a gap");int active=0;reason.compare_exchange_strong(active,3);return;}
        size_t first=std::min<uint64_t>(n,capacity-in%capacity);
        std::memcpy(queue.data()+in%capacity,data,first);
        std::memcpy(queue.data(),static_cast<const unsigned char*>(data)+first,n-first);
        accepted+=n/4;produced.store(in+n,std::memory_order_release);
        if(accepted==limit){NoteEvent("duration_limit","7938000-frame production cap (or unit-test limit)");int active=0;reason.compare_exchange_strong(active,2);}
    }
    void NoteEvent(const char* event,const char* text) noexcept {
        unsigned in=note_in.load(),out=note_out.load(std::memory_order_acquire);
        if(in-out==notes.size()){++note_lost;return;}
        auto& n=notes[in%notes.size()];n.stamp=Stamp::Now();
        std::snprintf(n.event,sizeof(n.event),"%s",event);std::snprintf(n.text,sizeof(n.text),"%s",text);
        note_in.store(in+1,std::memory_order_release);
    }
    void Xrun(const char* side,int error) noexcept {
        char text[100];std::snprintf(text,sizeof(text),"side=%s errno=%d recorded_frames=%llu",side,error,static_cast<unsigned long long>(accepted));NoteEvent("alsa_xrun",text);
    }
    void CaptureStarted() noexcept {
        NoteEvent("capture_start","ALSA capture successfully started");
        NoteEvent("recording_start","accepting capture PCM; initial silence included");
    }
    void CaptureStartFailed(int error) noexcept {
        char text[80];std::snprintf(text,sizeof(text),"ALSA capture start errno=%d",error);
        NoteEvent("capture_start_failure",text);
        int active=0;reason.compare_exchange_strong(active,5);
    }
    bool CompletionPublished() const {return reason.load(std::memory_order_acquire)!=0;}
    void Finish(){finished.store(true,std::memory_order_release);int active=0;reason.compare_exchange_strong(active,1);if(worker.joinable())worker.join();}
    ~Recorder(){Finish();}
    uint64_t Pending() const {return produced.load()-consumed.load();}
    const std::string& File()const{return filename;}
};
}
