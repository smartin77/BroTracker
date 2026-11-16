#pragma once
#include <Audio.h>
#include <SD.h>
#include <sd_access.h>
#include "sd_wav_preparation.h"
#include "core/audio/streamed_sample.h"
#include <algorithm>
#include "bringup/instrument_definition.h"
#include "core/audio/wav_inspection_job.h"
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
        std::int16_t rings[kStreamVoiceCount][kStreamRingFrames];
        std::int16_t starts[kSourceCapacity][kStreamStartFrames];
        std::int16_t staging[kStreamReadFrames];
    };
    void Prepare(bool sd_ready,Storage& memory,const DevelopmentWavInspection& inspection) {
        memory_=&memory;sd_ready_=sd_ready;
        for(unsigned i=0;i<kStreamVoiceCount;++i) buffers_[i].ring=memory.rings[i];
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
          for(unsigned i=3;i<kSourceCapacity;++i)for(unsigned channel=0;channel<kStreamVoiceCount;++channel)
              if(states_[channel].sample==&sources_[i] || buffers_[channel].sample==&sources_[i])retained[i]=true;
        }
        SdReadScope ownership;if(!ownership)return;
        for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i] && !retained[i] && !(rollback_pins_&(1u<<i)))ReleaseSource(i);
    }
    unsigned SourceCount()const{return count_;}
    template<class Bindings> void PinRollback(const Bindings& bindings) {
        rollback_pins_=0;
        for(unsigned i=0;i<kSourceCapacity;++i)for(unsigned j=0;j<bindings.count;++j)
            if(bindings.bindings[j].sample.streaming==&sources_[i])rollback_pins_|=1u<<i;
    }
    void UnpinRollback(){rollback_pins_=0;}
    bool RefillNeeded()const {
        StreamInterruptGuard guard;
        for(unsigned i=0;i<kStreamVoiceCount;++i)if(states_[i].sample && states_[i].step && buffers_[i].end<states_[i].sample->frames &&
            failed_generation_[i]!=states_[i].generation && StreamRefillUrgency(states_[i],buffers_[i])!=UINT64_MAX)return true;
        return false;
    }
    bool BackgroundHeadroom(unsigned reserve_frames=1024)const {
        StreamInterruptGuard guard;
        for(unsigned i=0;i<kStreamVoiceCount;++i) {
            const auto& s=states_[i];const auto& b=buffers_[i];
            if(!s.sample || !s.step || s.position>=s.sample->frames)continue;
            const auto consumed=std::max(s.position,kStreamStartFrames);
            const auto ahead=b.end>consumed?b.end-consumed:0;
            const auto start=s.position<kStreamStartFrames?std::min(kStreamStartFrames,s.sample->frames)-s.position:0;
            if(((std::uint64_t(ahead+start)<<32)/s.step)<reserve_frames)return false;
        }
        return true;
    }
    // One close at most; retain sources needed by bindings, rollback or voices.
    template<class Bindings> bool ReclaimOne(const Bindings& bindings) {
        for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i] && !(rollback_pins_&(1u<<i)) && i!=job_index_) {
            bool keep=false;
            for(unsigned j=0;j<bindings.count;++j)keep|=bindings.bindings[j].sample.streaming==&sources_[i];
            {StreamInterruptGuard guard;for(unsigned c=0;c<kStreamVoiceCount;++c)keep|=states_[c].sample==&sources_[i] || buffers_[c].sample==&sources_[i];}
            if(!keep){SdReadScope lease;if(!lease)return false;ReleaseSource(i);return true;}
        }
        return false;
    }
    bool BeginPrepareAsync(const char* path) {
        if(job_phase_!=JobIdle && job_phase_!=JobDone)return false;
        job_view_={};job_result_=InstrumentAvailability::PreparationFailed;job_index_=kSourceCapacity;job_read_=0;job_inspection_.Begin();
        job_phase_=JobDone;
        if(!sd_ready_ || !memory_){job_result_=InstrumentAvailability::StorageUnavailable;return true;}
        if(!InstrumentDefinitions::ValidResource(path)){job_result_=InstrumentAvailability::NoResource;return true;}
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i],path) && reports[i].view.data) {
            job_view_=reports[i].view;job_result_=InstrumentAvailability::Ready;return true;
        }
        unsigned i=3;while(i<kSourceCapacity && occupied_[i])++i;
        if(i==kSourceCapacity)return true;
        job_index_=i;std::strcpy(job_path_,path);job_phase_=JobOpen;return true;
    }
    bool PreparationDone()const{return job_phase_==JobDone;}
    InstrumentAvailability PreparationResult()const{return job_result_;}
    Pcm16MonoView PreparedView()const{return job_view_;}
    void CancelPreparation(){if(job_phase_!=JobIdle && job_phase_!=JobDone)job_phase_=JobClose;}
    // No loop over file chunks: one open, bounded inspection/read, <=512-byte
    // PCM seek/read, or close per foreground turn, after active refills.
    void PreparationStep() {
        if(job_phase_==JobIdle || job_phase_==JobDone)return;
        SdReadScope lease;if(!lease)return;
        auto& file=files_[job_index_];
        if(job_phase_==JobClose){file.close();job_index_=kSourceCapacity;job_phase_=JobDone;return;}
        if(job_phase_==JobOpen) {
            if(!file.open(job_path_,O_RDONLY)){job_result_=InstrumentAvailability::ResourceUnavailable;job_phase_=JobClose;return;}
            job_phase_=JobInspect;return;
        }
        if(job_phase_==JobInspect) {
            class Reader final:public WavBufferReader {
                FsFile& file_;
            public:explicit Reader(FsFile& file):file_(file){}
                bool Size(std::uint64_t& n)noexcept override{n=file_.size();return !file_.isDirectory();}
                bool Seek(std::uint64_t at)noexcept override{return at<=UINT32_MAX && file_.seekSet(at);}
                std::size_t Read(std::uint8_t* p,std::size_t n)noexcept override{const auto r=file_.read(p,n);return r<0?0:r;}
            } reader(file);
            job_inspection_.Step(reader);
            if(job_inspection_.Done()) {
                const auto& m=job_inspection_.Metadata();
                if(job_inspection_.Status()!=WavBufferStatus::Success || !m.frames || m.frames>UINT32_MAX || m.data_offset>UINT32_MAX)job_phase_=JobClose;
                else job_phase_=JobPcm;
            }
            return;
        }
        const auto& m=job_inspection_.Metadata();const auto frames=std::min<std::uint64_t>(kStreamStartFrames,m.frames);
        const auto n=std::min<std::uint64_t>(256,frames-job_read_);
        if(!file.seekSet(m.data_offset+job_read_*2) || file.read(memory_->starts[job_index_]+job_read_,n*2)!=int(n*2)){job_phase_=JobClose;return;}
        job_read_+=n;if(job_read_!=frames)return;
        offsets_[job_index_]=m.data_offset;sources_[job_index_]={memory_->starts[job_index_],static_cast<std::uint32_t>(m.frames),buffers_};
        auto& report=reports[job_index_];report.attempted=report.sd_ready=true;report.capacity=kStreamStartFrames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};report.view={sources_[job_index_].start,sources_[job_index_].frames,44100,&sources_[job_index_]};
        std::strcpy(paths_[job_index_],job_path_);occupied_[job_index_]=true;last_prepared_=job_index_;++count_;
        __DMB(); // Publish immutable PCM/source data before the audio binding can reference it.
        job_view_=report.view;job_result_=InstrumentAvailability::Ready;job_index_=kSourceCapacity;job_phase_=JobDone;
    }
    // Audio owner only, after the transactional render commits. No filesystem
    // side effects from validation/probe copies or discarded tentative renders.
    void PublishVoice(unsigned i,const StreamedVoiceSnapshot& state) noexcept {
        auto& ready=buffers_[i];
        if(state.generation!=ready.generation || state.sample!=ready.sample) {
            ready.generation=state.generation;ready.sample=state.sample;
            ready.begin=ready.end=state.sample?std::min(kStreamStartFrames,state.sample->frames):0;
        }
        states_[i]=state;
    }
    template<class Player> void Publish(const Player& player) noexcept {
        for(unsigned i=0;i<8;++i)PublishVoice(i,player.SampleVoice(i)->StreamSnapshot());
    }
    void Service() {
        if(!memory_) return;
        int best=-1; std::uint64_t least=UINT64_MAX;
        StreamedVoiceSnapshot selected; StreamedVoiceBuffer span;
        { StreamInterruptGuard guard;
        for(unsigned i=0;i<kStreamVoiceCount;++i) {
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
    enum JobPhase{JobIdle,JobOpen,JobInspect,JobPcm,JobClose,JobDone};JobPhase job_phase_=JobIdle;
    unsigned job_index_=kSourceCapacity;std::uint64_t job_read_=0;char job_path_[64]{};
    WavInspectionJob job_inspection_;Pcm16MonoView job_view_{};
    InstrumentAvailability job_result_=InstrumentAvailability::PreparationFailed;unsigned rollback_pins_=0;
    bool occupied_[kSourceCapacity]{};unsigned last_prepared_=kSourceCapacity;
    bool sd_ready_=false;unsigned count_=3;char paths_[kSourceCapacity][64]{};
    FsFile files_[kSourceCapacity]; std::uint32_t offsets_[kSourceCapacity]{};
    StreamedSample sources_[kSourceCapacity]; StreamedVoiceBuffer buffers_[kStreamVoiceCount];
    StreamedVoiceSnapshot states_[kStreamVoiceCount]; std::uint64_t failed_generation_[kStreamVoiceCount]{};
};
}
