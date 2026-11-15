#pragma once
#include <Audio.h>
#include <SD.h>
#include <sd_access.h>
#include "sd_wav_preparation.h"
#include "core/audio/streamed_sample.h"
#include <algorithm>
#include "bringup/instrument_definition.h"
namespace BroTracker {
// Preserve nesting and provide compiler barriers for all ISR/main publications.
// These scopes cover only bounded metadata work, never file I/O or PCM copies.
class StreamInterruptGuard {
public:
#if defined(__arm__)
    StreamInterruptGuard() noexcept { asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask_) :: "memory"); }
    ~StreamInterruptGuard() noexcept { asm volatile("msr primask, %0" :: "r"(mask_) : "memory"); }
#else
    // Host tests substitute only the interrupt-register boundary.
    StreamInterruptGuard() noexcept;
    ~StreamInterruptGuard() noexcept;
#endif
private:
    std::uint32_t mask_;
};
// Foreground serializes positional reads through one cursor per immutable source.
// An open FsFile is idle between operations: each complete seek/read holds the
// device read guard. No retained read lock prevents stopped session saves.
// The only writers are existing pattern/session writers, never these WAV paths.
class SdSampleStreaming {
public:
    static constexpr unsigned kSourceCapacity=16;
    struct Storage {
        std::int16_t rings[8][kStreamRingFrames];
        std::int16_t starts[kSourceCapacity][kStreamStartFrames];
        std::int16_t staging[kStreamReadFrames];
    };
    void Prepare(bool sd_ready,Storage& memory,const DevelopmentWavInspection& inspection) {
        memory_=&memory;sd_ready_=sd_ready;
        for(unsigned i=0;i<8;++i) buffers_[i].ring=memory.rings[i];
        for(unsigned i=0;i<3;++i) {
            occupied_[i]=true;
            std::strcpy(paths_[i],kDevelopmentWavPaths[i]);
            const auto& found=inspection.files[i];
            auto& source=sources_[i]; auto& report=reports[i];
            report.attempted=true; report.sd_ready=sd_ready; report.capacity=kStreamStartFrames;
            report.address=reinterpret_cast<std::uintptr_t>(memory.starts[i]);
            if(!sd_ready || found.validation!=WavBufferStatus::Success || !found.metadata.frames ||
                found.metadata.frames>UINT32_MAX || found.metadata.data_offset>UINT32_MAX) continue;
            SdReadScope ownership; if(!ownership) continue;
            if(!files_[i].open(kDevelopmentWavPaths[i],O_RDONLY)) continue;
            const auto count=std::min<std::uint64_t>(kStreamStartFrames,found.metadata.frames);
            if(!files_[i].seekSet(found.metadata.data_offset) || files_[i].read(memory.starts[i],count*2)!=int(count*2)) { files_[i].close(); continue; }
            offsets_[i]=found.metadata.data_offset;
            source={memory.starts[i],static_cast<std::uint32_t>(found.metadata.frames),buffers_};
            report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
            report.view={source.start,source.frames,44100,&source};
        }
    }
    // Stopped foreground only. New immutable sources never replace published PCM.
    InstrumentAvailability PreparePath(const char* path,Pcm16MonoView& output) {
        if(!sd_ready_ || !memory_)return InstrumentAvailability::StorageUnavailable;
        if(!InstrumentDefinitions::ValidResource(path))return InstrumentAvailability::NoResource;
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i],path) && reports[i].view.data) {
            output=reports[i].view;return InstrumentAvailability::Ready;
        }
        unsigned index=3;while(index<kSourceCapacity && occupied_[index])++index;
        if(index==kSourceCapacity)return InstrumentAvailability::PreparationFailed;
        SdReadScope ownership;if(!ownership)return InstrumentAvailability::NotPrepared;
        auto& file=files_[index];
        if(!file.open(path,O_RDONLY))return InstrumentAvailability::ResourceUnavailable;
        class Reader final:public WavBufferReader {
            FsFile& file_;
        public:
            explicit Reader(FsFile& f):file_(f){}
            bool Size(std::uint64_t& bytes)noexcept override {bytes=file_.size();return !file_.isDirectory();}
            bool Seek(std::uint64_t offset)noexcept override{return offset<=UINT32_MAX && file_.seekSet(offset);}
            std::size_t Read(std::uint8_t* data,std::size_t size)noexcept override {const auto n=file_.read(data,size);return n<0?0:n;}
        } reader(file);
        WavMetadata metadata;
        if(InspectWavMetadata(reader,metadata)!=WavBufferStatus::Success || !metadata.frames || metadata.frames>UINT32_MAX || metadata.data_offset>UINT32_MAX) {
            file.close();return InstrumentAvailability::PreparationFailed;
        }
        const auto frames=std::min<std::uint64_t>(kStreamStartFrames,metadata.frames);
        if(!file.seekSet(metadata.data_offset) || file.read(memory_->starts[index],frames*2)!=int(frames*2)) {
            file.close();return InstrumentAvailability::PreparationFailed;
        }
        offsets_[index]=metadata.data_offset;
        sources_[index]={memory_->starts[index],static_cast<std::uint32_t>(metadata.frames),buffers_};
        auto& report=reports[index];report.attempted=report.sd_ready=true;report.capacity=kStreamStartFrames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
        report.view={sources_[index].start,sources_[index].frames,44100,&sources_[index]};
        std::strcpy(paths_[index],path);occupied_[index]=true;last_prepared_=index;++count_;output=report.view;return InstrumentAvailability::Ready;
    }
    // Only the just-prepared, uninstalled candidate is discardable on rejection.
    void DiscardLast(const char* path) {
        if(last_prepared_>=kSourceCapacity || !occupied_[last_prepared_] || std::strcmp(paths_[last_prepared_],path))return;
        SdReadScope ownership;if(ownership)ReleaseSource(last_prepared_);
    }
    // Stopped foreground only, after successful binding publication. Never compact
    // the table: all descriptors and start buffers used by other slots stay fixed.
    // Voice snapshots/spans conservatively pin a source until Publish clears them.
    template<class Bindings> void ReleaseUnused(const Bindings& bindings,bool stopped) {
        if(!stopped || bindings.count>sizeof(bindings.bindings)/sizeof(bindings.bindings[0]))return;
        bool retained[kSourceCapacity]{};
        for(unsigned i=3;i<kSourceCapacity;++i)for(unsigned j=0;j<bindings.count;++j)
            if(bindings.bindings[j].sample.streaming==&sources_[i])retained[i]=true;
        { StreamInterruptGuard guard;
          for(unsigned i=3;i<kSourceCapacity;++i)for(unsigned channel=0;channel<8;++channel)
              if(states_[channel].sample==&sources_[i] || buffers_[channel].sample==&sources_[i])retained[i]=true;
        }
        SdReadScope ownership;if(!ownership)return;
        for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i] && !retained[i])ReleaseSource(i);
    }
    unsigned SourceCount()const{return count_;}
    // Audio owner only, after the transactional render commits. No filesystem
    // side effects from validation/probe copies or discarded tentative renders.
    template<class Player> void Publish(const Player& player) noexcept {
        for(unsigned i=0;i<8;++i) {
            const auto state=player.SampleVoice(i)->StreamSnapshot();
            auto& ready=buffers_[i];
            if(state.generation!=ready.generation || state.sample!=ready.sample) {
                ready.generation=state.generation; ready.sample=state.sample;
                ready.begin=ready.end=state.sample ? std::min(kStreamStartFrames,state.sample->frames) : 0;
            }
            states_[i]=state;
        }
    }
    void Service() {
        if(!memory_) return;
        int best=-1; std::uint64_t least=UINT64_MAX;
        StreamedVoiceSnapshot selected; StreamedVoiceBuffer span;
        { StreamInterruptGuard guard;
        for(unsigned i=0;i<8;++i) {
            const auto& state=states_[i]; const auto& ready=buffers_[i];
            if(!state.sample || !state.step || ready.end>=state.sample->frames || failed_generation_[i]==state.generation) continue;
            const auto until=StreamRefillUrgency(state,ready);
            if(until<least) { best=i; least=until; selected=state; span=ready; }
        }
        }
        if(best<0) return;
        unsigned source=0;
        while(source<kSourceCapacity && (!occupied_[source] || &sources_[source]!=selected.sample)) ++source;
        if(source==kSourceCapacity) return;
        const auto at=std::max(span.end,std::max(selected.position,kStreamStartFrames));
        if(at>=selected.sample->frames) return;
        const auto count=std::min(kStreamReadFrames,selected.sample->frames-at);
        const auto started=micros(); bool ok=false;
        {
            SdReadScope ownership; if(!ownership) return; // retry later, no lost save intent
            ok=files_[source].seekSet(std::uint64_t(offsets_[source])+std::uint64_t(at)*2) &&
                files_[source].read(memory_->staging,count*2)==int(count*2);
        }
        const auto elapsed=micros()-started; ++reads; total_read_us+=elapsed;
        max_read_us=std::max(max_read_us,elapsed);
        ++histogram[std::min<unsigned>(elapsed/100,127)];
        if(!ok) ++io_errors;
        bool current;
        { StreamInterruptGuard guard;
            current=buffers_[best].generation==selected.generation && buffers_[best].sample==selected.sample;
        }
        if(!current) { ++stale_reads; return; }
        if(!ok) { failed_generation_[best]=selected.generation; return; }
        // Writes occupy unretained future ring slots. New generations use only
        // immutable starts until completion is checked and published below.
        for(unsigned j=0;j<count;++j) memory_->rings[best][(at+j)%kStreamRingFrames]=memory_->staging[j];
        __DMB();
        { StreamInterruptGuard guard;
            if(!PublishStreamRead(buffers_[best],selected,at,count)) ++stale_reads;
        }
    }
    StreamedVoiceSnapshot Snapshot(unsigned i) const {
        StreamInterruptGuard guard; return states_[i];
    }
    unsigned P99() const { unsigned n=0; for(unsigned i=0;i<128;++i) { n+=histogram[i]; if(reads && n>=(reads*99+99)/100) return i==127?max_read_us:(i+1)*100; } return 0; }
    SdWavPreparation reports[kSourceCapacity];
    std::uint32_t reads=0,max_read_us=0,io_errors=0,stale_reads=0,histogram[128]{};
    std::uint64_t total_read_us=0;
private:
    // Caller holds foreground SD ownership and has proved this slot unretained.
    void ReleaseSource(unsigned index) {
        if(!files_[index].close()) {reports[index].view={};return;} // uncertain close: do not reuse
        occupied_[index]=false;paths_[index][0]=0;reports[index]={};sources_[index]={};offsets_[index]=0;--count_;
        if(last_prepared_==index)last_prepared_=kSourceCapacity;
    }
    Storage* memory_=nullptr;
    bool occupied_[kSourceCapacity]{};unsigned last_prepared_=kSourceCapacity;
    bool sd_ready_=false;unsigned count_=3;char paths_[kSourceCapacity][64]{};
    FsFile files_[kSourceCapacity]; std::uint32_t offsets_[kSourceCapacity]{};
    StreamedSample sources_[kSourceCapacity]; StreamedVoiceBuffer buffers_[8];
    StreamedVoiceSnapshot states_[8]; std::uint64_t failed_generation_[8]{};
};
}
