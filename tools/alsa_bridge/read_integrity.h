#pragma once
// Independent read-return observations: no PCM pointers cross this queue.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace read_integrity {
constexpr size_t kSlots=4096, kMaxBytes=1024;
constexpr uint64_t kMaxRecords=1000000;
static_assert(ATOMIC_LLONG_LOCK_FREE==2 && ATOMIC_BOOL_LOCK_FREE==2,"lock-free read integrity counters required");
inline uint64_t Digest(const void* data,size_t bytes) noexcept {
    // FNV-1a 64, byte order is the actual returned PCM byte order.
    uint64_t h=14695981039346656037ULL;
    const auto* p=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<bytes;++i) { h^=p[i]; h*=1099511628211ULL; }
    return h;
}
struct Record { uint64_t id=0,source=0,frames=0,digest=0; };
class Trace {
    std::array<Record,kSlots> records{};
    std::atomic<uint64_t> in{0},out{0};
    std::atomic<bool> done{false},failed{false};
    uint64_t attempted=0,lost=0,written=0;
    size_t capacity;
    std::string directory;
    FILE* file=nullptr;
    std::thread worker;
    void Writer() noexcept {
        for(;;) {
            const auto o=out.load(std::memory_order_relaxed);
            if(o!=in.load(std::memory_order_acquire)) {
                const Record r=records[o%capacity];
                out.store(o+1,std::memory_order_release);
                if(std::fprintf(file,"%llu\t%llu\t%llu\t%016llx\n",
                    (unsigned long long)r.id,(unsigned long long)r.source,
                    (unsigned long long)r.frames,(unsigned long long)r.digest)<0) failed.store(true);
                ++written;
            } else if(done.load(std::memory_order_acquire)) {
                if(o==in.load(std::memory_order_acquire)) break;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(std::fflush(file)!=0) failed.store(true);
        if(fsync(fileno(file))!=0) failed.store(true);
        if(std::fclose(file)!=0) failed.store(true);
        file=nullptr;
        const auto pending=directory+"/read-integrity-completion.pending.json";
        FILE* f=std::fopen(pending.c_str(),"wx");
        if(!f) { failed.store(true); return; }
        const int n=std::fprintf(f,"{\"schema\":1,\"algorithm\":\"fnv1a64\",\"attempted\":%llu,\"written\":%llu,\"lost\":%llu,\"incomplete\":%s}\n",
            (unsigned long long)attempted,(unsigned long long)written,(unsigned long long)lost,
            (failed.load() || lost)?"true":"false");
        bool valid=n>=0;
        if(std::fflush(f)!=0 || fsync(fileno(f))!=0) valid=false;
        if(std::fclose(f)!=0) valid=false;
        if(valid && std::rename(pending.c_str(),(directory+"/read-integrity-completion.json").c_str())!=0) valid=false;
        if(!valid) failed.store(true);
    }
public:
    explicit Trace(const std::string& dir,size_t slots=kSlots,void (*gate)()=nullptr):capacity(slots),directory(dir) {
        if(!slots || slots>kSlots) throw std::runtime_error("invalid read integrity capacity");
        file=std::fopen((dir+"/read-integrity.tsv").c_str(),"wx");
        if(!file) throw std::runtime_error("read integrity file creation failed");
        if(std::fprintf(file,"id\tsource\tframes\tdigest\n")<0) { std::fclose(file);throw std::runtime_error("read integrity header failed"); }
        try { worker=std::thread([this,gate]{if(gate) gate();Writer();}); }
        catch(...) { std::fclose(file);throw; }
    }
    void Observe(const void* pcm,uint64_t source,size_t frames) noexcept {
        const auto id=++attempted;
        // Bound hashing even if a broken caller exceeds its requested read size.
        if(!frames || frames>kMaxBytes/4) { ++lost;return; }
        const Record r{id,source,frames,Digest(pcm,frames*4)};
        const auto i=in.load(std::memory_order_relaxed);
        if(lost || failed.load(std::memory_order_relaxed) || id>kMaxRecords ||
           i-out.load(std::memory_order_acquire)>=capacity) { ++lost;return; }
        records[i%capacity]=r;in.store(i+1,std::memory_order_release);
    }
    void Finish() {
        done.store(true,std::memory_order_release);
        if(worker.joinable()) worker.join();
    }
    bool Failed() const { return failed.load(); }
    ~Trace(){Finish();}
};
} // namespace read_integrity
