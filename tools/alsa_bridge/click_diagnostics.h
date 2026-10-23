#pragma once
// Opt-in evidence collection. One audio producer, one disk writer; no producer I/O.
#include <array>
#include "recording.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace clicks {
constexpr size_t kChunk = 256, kFrameBytes = 4, kSlots = 4096;
static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "lock-free evidence counters required");
inline uint64_t Now() noexcept {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec)*1000000000ULL + t.tv_nsec;
}
enum Kind { Service=1, Read, Write, StatusCapture, StatusPlayback, Recovery,
            Discard, Start, End, Transport, Metadata };
// Fixed schema, raw PCM only for positive Read/Write results. Source is the
// absolute capture frame index, so partial writes and discarded spans correlate.
struct Record {
    uint64_t seq=0, ns=0, captured=0, played=0, discarded=0, source=0;
    int64_t result=0, avail=0, delay=0;
    uint32_t kind=0, requested=0, queued=0, state=0, step=0;
    std::array<unsigned char,kChunk*kFrameBytes> pcm{};
};
class Queue {
    std::array<Record,kSlots> slots{};
    std::atomic<uint64_t> in{0},out{0};
    size_t capacity;
public:
    explicit Queue(size_t n=kSlots):capacity(n) {
        if(!n || n>kSlots) throw std::runtime_error("invalid click queue capacity");
    }
    uint64_t Size() const noexcept {return in.load(std::memory_order_relaxed)-out.load(std::memory_order_acquire);}
    bool Push(const Record& r) noexcept {
        const auto i=in.load(std::memory_order_relaxed);
        if(i-out.load(std::memory_order_acquire)==capacity) return false;
        slots[i%capacity]=r; in.store(i+1,std::memory_order_release); return true;
    }
    bool Pop(Record& r) noexcept {
        const auto o=out.load(std::memory_order_relaxed);
        if(o==in.load(std::memory_order_acquire)) return false;
        r=slots[o%capacity]; out.store(o+1,std::memory_order_release); return true;
    }
};
class Trace {
    Queue queue;
    bool wav=false;
    uint64_t received_bytes=0,accepted_bytes=0,received_frames=0,accepted_frames=0;
    uint64_t frame_limit=0,record_limit=0;
    std::atomic<bool> done{false}, failed{false};
    std::atomic<uint64_t> lost{0}, first_lost{0};
    std::atomic<int> loss_reason{0}; // 1=queue full, 2=duration limit, 3=writer failure
    uint64_t sequence=0, written=0, began=Now();
    const uint64_t limit_ns;
    FILE *events=nullptr,*received=nullptr,*accepted=nullptr;
    std::thread worker;
    std::string directory;
    bool Save(const Record& r) noexcept {
        if((r.kind==Read || r.kind==Write) && r.result>0) {
            const size_t bytes=static_cast<size_t>(r.result)*kFrameBytes;
            if(std::fwrite(r.pcm.data(),1,bytes,r.kind==Read?received:accepted)!=bytes) return false;
            (r.kind==Read?received_bytes:accepted_bytes)+=bytes;
        }
        return std::fprintf(events,
            "%llu\t%llu\t%u\t%u\t%lld\t%llu\t%llu\t%llu\t%llu\t%u\t%lld\t%lld\t%u\t%u\n",
            (unsigned long long)r.seq,(unsigned long long)r.ns,r.kind,r.requested,
            (long long)r.result,(unsigned long long)r.source,(unsigned long long)r.captured,
            (unsigned long long)r.played,(unsigned long long)r.discarded,r.queued,
            (long long)r.avail,(long long)r.delay,r.state,r.step)>0;
    }
    void Writer() noexcept {
        Record r;
        for(;;) {
            if(queue.Pop(r)) {
                if(!failed.load() && !Save(r)) failed.store(true);
                ++written;
            } else if(done.load(std::memory_order_acquire)) {
                // Recheck after observing completion (producer publishes last record first).
                if(queue.Pop(r)) { if(!failed.load() && !Save(r)) failed.store(true); ++written; }
                else break;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(wav) for(FILE* f:{received,accepted}) {
            const auto header=recording::PcmHeader(static_cast<uint32_t>(f==received?received_bytes:accepted_bytes));
            if(std::fseek(f,0,SEEK_SET) || std::fwrite(header.data(),1,header.size(),f)!=header.size()) failed.store(true);
        }
        for(FILE* f : {events,received,accepted}) {
            if(std::fflush(f)!=0 || fsync(fileno(f))!=0) failed.store(true);
            if(std::fclose(f)!=0) failed.store(true);
        }
        events=received=accepted=nullptr;
        // Publish the completion marker only after its bytes have flushed and
        // closed successfully. A failed final write must not look complete.
        const auto pending=directory+"/completion.pending.json";
        FILE* f=std::fopen(pending.c_str(),"wx");
        if(f) {
            const bool incomplete=failed.load() || lost.load()!=0;
            const int n=std::fprintf(f,"{\"schema\":1,\"attempted_records\":%llu,\"queued_records\":%llu,"
                "\"lost_records\":%llu,\"first_lost_seq\":%llu,\"loss_reason\":%d,\"writer_failed\":%s,\"incomplete\":%s}\n",
                (unsigned long long)sequence,(unsigned long long)written,(unsigned long long)lost.load(),
                (unsigned long long)first_lost.load(),loss_reason.load(),failed.load()?"true":"false",incomplete?"true":"false");
            bool valid=n>=0;
            if(std::fflush(f)!=0 || fsync(fileno(f))!=0) valid=false;
            if(std::fclose(f)!=0) valid=false;
            if(valid && std::rename(pending.c_str(),(directory+"/completion.json").c_str())!=0) valid=false;
            if(!valid) failed.store(true);
        } else failed.store(true);
    }
public:
    explicit Trace(const char* base,size_t slots=kSlots,void (*writer_gate)()=nullptr,unsigned seconds=180,bool wave=false,uint64_t max_frames=0,uint64_t max_records=0)
        :queue(slots),wav(wave),frame_limit(max_frames),record_limit(max_records),limit_ns(uint64_t(seconds)*1000000000ULL) {
        if(seconds<1 || seconds>300) throw std::runtime_error("click limit must be 1..300 seconds");
        directory=std::string(base)+"/bridge-XXXXXX";
        if(!mkdtemp(&directory[0])) throw std::runtime_error("create unique click run directory failed");
        events=std::fopen((directory+"/transfers.tsv").c_str(),"wx");
        received=std::fopen((directory+(wav?"/received.wav":"/received.s16le")).c_str(),"wx");
        accepted=std::fopen((directory+(wav?"/accepted.wav":"/accepted.s16le")).c_str(),"wx");
        if(!events || !received || !accepted) {
            for(FILE* f : {events,received,accepted}) if(f) std::fclose(f);
            throw std::runtime_error("open click evidence files failed (preserved)");
        }
        if(wav) {
            const auto header=recording::PcmHeader(0);
            if(std::fwrite(header.data(),1,header.size(),received)!=header.size() ||
               std::fwrite(header.data(),1,header.size(),accepted)!=header.size()) {
                for(FILE* f:{events,received,accepted}) std::fclose(f);
                throw std::runtime_error("WAV header creation failed");
            }
        }
        std::fprintf(events,"seq\tns\tkind\trequested\tresult\tsource\tcaptured\tplayed\tdiscarded\tqueued\tavail\tdelay\tstate\tstep\n");
        try { worker=std::thread([this,writer_gate]{ if(writer_gate) writer_gate(); Writer(); }); }
        catch(...) { for(FILE* f : {events,received,accepted}) std::fclose(f); throw; }
    }
    const std::string& Directory() const { return directory; }
    // Each call is a fixed copy plus lock-free counters, even after diagnostic
    // overflow. Stop accepting at the first loss; never splice evidence gaps.
    void Emit(Record r,const void* pcm=nullptr,bool source_time=false) noexcept {
        r.seq=++sequence; if(!source_time) r.ns=Now();
        if(pcm && r.result>0 && r.result<=int64_t(kChunk))
            std::memcpy(r.pcm.data(),pcm,static_cast<size_t>(r.result)*kFrameBytes);
        if(lost.load()) { ++lost; return; }
        auto& frames=r.kind==Read?received_frames:accepted_frames;
        const bool pcm_record=(r.kind==Read || r.kind==Write) && r.result>0;
        const bool capped=(pcm_record && r.result>int64_t(kChunk)) || (record_limit && sequence>record_limit) ||
            (pcm_record && frame_limit && uint64_t(r.result)>frame_limit-frames);
        const int why=failed.load()?3:(capped || Now()-began>limit_ns)?2:!queue.Push(r)?1:0;
        if(!why && pcm_record) frames+=r.result;
        if(why) { loss_reason.store(why); first_lost.store(r.seq); ++lost; }
    }
    void Incomplete() noexcept { if(!lost.load()){first_lost.store(sequence+1);loss_reason.store(1);} ++lost; }
    void Finish() {
        done.store(true,std::memory_order_release);
        if(worker.joinable()) worker.join();
    }
    // Read-only queue occupancy; used to pace accelerated device-free fixtures.
    uint64_t PendingRecords() const noexcept {return queue.Size();}
    bool Accepting() const {return !failed.load() && !lost.load();}
    bool Failed() const { return failed.load(); }
    ~Trace(){Finish();}
};
} // namespace clicks
