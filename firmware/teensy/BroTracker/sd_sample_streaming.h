#pragma once
#include <Audio.h>
#include <SD.h>
#include <sd_access.h>
#include "sd_wav_preparation.h"
#include "core/audio/streamed_sample.h"
#include <algorithm>
#include <cstring>
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
            (void)paths_[i].Assign(kDevelopmentWavPaths[i]);
            const auto& found=inspection.files[i];
            auto& source=sources_[i]; auto& report=reports[i];
            report.attempted=true; report.sd_ready=sd_ready; report.capacity=kStreamStartFrames;
            report.address=reinterpret_cast<std::uintptr_t>(memory.starts[i]);
            if(!sd_ready || found.validation!=WavBufferStatus::Success) continue;
            SdReadScope ownership; if(!ownership) continue;
            if(!files_[i].open(kDevelopmentWavPaths[i],O_RDONLY)) continue;
            StreamedSample candidate;
            if(!DescribeSource(found.metadata,files_[i].size(),memory.starts[i],candidate) ||
                !ReadFrames(files_[i],static_cast<std::uint32_t>(found.metadata.data_offset),candidate,0,
                    candidate.PrefixFrames(),memory.starts[i],kStreamStartBytes)) { files_[i].close(); continue; }
            offsets_[i]=static_cast<std::uint32_t>(found.metadata.data_offset);
            source=candidate;
            report.capacity=source.layout.start_frames;
            report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
            report.view=MakeView(source);
        }
    }
    // Stopped foreground only. New immutable sources never replace published PCM.
    InstrumentAvailability PreparePath(const char* path,Pcm16MonoView& output) {
        Pcm16MixedSampleView prepared;
        const auto result=PreparePath(path,prepared,false,false,false);
        if(result==InstrumentAvailability::Ready) output=prepared.Mono();
        return result;
    }
    InstrumentAvailability PreparePath(const char* path,Pcm16MixedSampleView& output,bool stereo=true,bool lower_rates=true,bool pcm8_and_8363=true) {
        if(!sd_ready_ || !memory_)return InstrumentAvailability::StorageUnavailable;
        if(!InstrumentDefinitions::ValidResource(path))return InstrumentAvailability::NoResource;
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i].Get(),path) && reports[i].view.data) {
            const auto& format=sources_[i].format;
            if(!WavBufferDetail::SupportsIntegerPcm(1,format.channels,format.bits_per_sample,format.sample_rate_hz,
                stereo,lower_rates,pcm8_and_8363))return InstrumentAvailability::PreparationFailed;
            output=reports[i].view;return InstrumentAvailability::Ready;
        }
        unsigned index=3;while(index<kSourceCapacity && occupied_[index])++index;
        if(index==kSourceCapacity)return InstrumentAvailability::PreparationFailed;
        ResourcePathRef candidate;if(!candidate.Assign(path))return InstrumentAvailability::PathCapacity;
        SdReadScope ownership;if(!ownership)return InstrumentAvailability::NotPrepared;
        auto& file=files_[index];
        if(!file.open(candidate.Get(),O_RDONLY))return InstrumentAvailability::ResourceUnavailable;
        class Reader final:public WavBufferReader {
            FsFile& file_;
        public:
            explicit Reader(FsFile& f):file_(f){}
            bool Size(std::uint64_t& bytes)noexcept override {bytes=file_.size();return !file_.isDirectory();}
            bool Seek(std::uint64_t offset)noexcept override{return offset<=UINT32_MAX && file_.seekSet(offset);}
            std::size_t Read(std::uint8_t* data,std::size_t size)noexcept override {const auto n=file_.read(data,size);return n<0?0:n;}
        } reader(file);
        WavMetadata metadata;
        StreamedSample source;
        if(InspectWavMetadata(reader,metadata,kWavInspectionChunkLimit,stereo,lower_rates,pcm8_and_8363)!=WavBufferStatus::Success ||
            !DescribeSource(metadata,metadata.file_bytes,memory_->starts[index],source)) {
            file.close();return InstrumentAvailability::PreparationFailed;
        }
        const auto offset=static_cast<std::uint32_t>(metadata.data_offset);
        if(!ReadFrames(file,offset,source,0,source.PrefixFrames(),memory_->starts[index],kStreamStartBytes)) {
            file.close();return InstrumentAvailability::PreparationFailed;
        }
        offsets_[index]=offset;
        sources_[index]=source;
        auto& report=reports[index];report.attempted=report.sd_ready=true;report.capacity=source.layout.start_frames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
        report.view=MakeView(sources_[index]);
        paths_[index]=candidate;occupied_[index]=true;last_prepared_=index;++count_;output=report.view;return InstrumentAvailability::Ready;
    }
    // Only the just-prepared, uninstalled candidate is discardable on rejection.
    void DiscardLast(const char* path) {
        if(last_prepared_>=kSourceCapacity || !occupied_[last_prepared_] || std::strcmp(paths_[last_prepared_].Get(),path))return;
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
    bool RefillNeeded(unsigned channels=kStreamVoiceCount)const {
        StreamInterruptGuard guard;
        for(unsigned i=0;i<channels;++i)if(states_[i].sample && states_[i].step && buffers_[i].end<states_[i].sample->frames &&
            failed_generation_[i]!=states_[i].generation && StreamRefillUrgency(states_[i],buffers_[i])!=UINT64_MAX)return true;
        return false;
    }
    bool BackgroundHeadroom(unsigned reserve_frames=1024,unsigned channels=kStreamVoiceCount)const {
        StreamInterruptGuard guard;
        for(unsigned i=0;i<channels;++i) {
            const auto& s=states_[i];const auto& b=buffers_[i];
            if(!s.sample || !s.step || s.position>=s.sample->frames)continue;
            const auto end=StreamReadyEnd(*s.sample,b,s.generation,s.position);
            if(end<s.sample->frames && StreamReadyOutputFrames(s,end)<reserve_frames)return false;
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
        job_path_.Reset();job_view_={};job_result_=InstrumentAvailability::PreparationFailed;job_index_=kSourceCapacity;job_read_=0;job_inspection_.Begin(true,true,true);
        job_phase_=JobDone;
        if(!sd_ready_ || !memory_){job_result_=InstrumentAvailability::StorageUnavailable;return true;}
        if(!InstrumentDefinitions::ValidResource(path)){job_result_=InstrumentAvailability::NoResource;return true;}
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i].Get(),path) && reports[i].view.data) {
            job_view_=reports[i].view;job_result_=InstrumentAvailability::Ready;return true;
        }
        unsigned i=3;while(i<kSourceCapacity && occupied_[i])++i;
        if(i==kSourceCapacity)return true;
        if(!job_path_.Assign(path)){job_result_=InstrumentAvailability::PathCapacity;return true;}
        job_index_=i;job_phase_=JobOpen;return true;
    }
    bool PreparationDone()const{return job_phase_==JobDone;}
    InstrumentAvailability PreparationResult()const{return job_result_;}
    Pcm16MonoView PreparedView()const{return job_view_.Mono();}
    Pcm16MixedSampleView PreparedMixedView()const{return job_view_;}
    void CancelPreparation(){if(job_phase_!=JobIdle && job_phase_!=JobDone)job_phase_=JobClose;}
    // No loop over file chunks: one open, bounded inspection/read, <=512-byte
    // PCM seek/read, or close per foreground turn, after active refills.
    void PreparationStep() {
        if(job_phase_==JobIdle || job_phase_==JobDone)return;
        SdReadScope lease;if(!lease)return;
        auto& file=files_[job_index_];
        auto& source=sources_[job_index_]; // Reserved foreground slot, unpublished until the prefix is complete.
        if(job_phase_==JobClose){file.close();source={};job_path_.Reset();job_index_=kSourceCapacity;job_phase_=JobDone;return;}
        if(job_phase_==JobOpen) {
            if(!file.open(job_path_.Get(),O_RDONLY)){job_result_=InstrumentAvailability::ResourceUnavailable;job_phase_=JobClose;return;}
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
                if(job_inspection_.Status()!=WavBufferStatus::Success ||
                    !DescribeSource(m,m.file_bytes,memory_->starts[job_index_],source))job_phase_=JobClose;
                else job_phase_=JobPcm;
            }
            return;
        }
        const auto offset=static_cast<std::uint32_t>(job_inspection_.Metadata().data_offset);
        const auto frames=source.PrefixFrames();
        constexpr std::uint32_t kPreparationReadBytes=512;
        const auto n=std::min(kPreparationReadBytes/source.layout.bytes_per_frame,frames-job_read_);
        auto* pcm=reinterpret_cast<unsigned char*>(memory_->starts[job_index_]);
        if(!ReadFrames(file,offset,source,job_read_,n,pcm+job_read_*source.layout.bytes_per_frame,kPreparationReadBytes)){
            job_phase_=JobClose;return;
        }
        job_read_+=n;if(job_read_!=frames)return;
        offsets_[job_index_]=offset;
        auto& report=reports[job_index_];report.attempted=report.sd_ready=true;report.capacity=source.layout.start_frames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};report.view=MakeView(source);
        paths_[job_index_]=job_path_;job_path_.Reset();occupied_[job_index_]=true;last_prepared_=job_index_;++count_;
        __DMB(); // Publish immutable PCM/source data before the audio binding can reference it.
        job_view_=report.view;job_result_=InstrumentAvailability::Ready;job_index_=kSourceCapacity;job_phase_=JobDone;
    }
    // Audio owner only, after the transactional render commits. No filesystem
    // side effects from validation/probe copies or discarded tentative renders.
    void PublishVoice(unsigned i,const StreamedVoiceSnapshot& state) noexcept {
        auto& ready=buffers_[i];
        if(state.generation!=ready.generation || state.sample!=ready.sample) {
            ready.generation=state.generation;ready.sample=state.sample;
            ready.begin=ready.end=state.sample?state.sample->PrefixFrames():0;
        }
        states_[i]=state;
    }
    template<class Player> void Publish(const Player& player) noexcept {
        for(unsigned i=0;i<8;++i)PublishVoice(i,player.SampleVoice(i)->StreamSnapshot());
    }
    void Service(unsigned audition_reserve=1024) {
        if(!memory_) return;
        int best=-1; std::uint64_t least=UINT64_MAX;
        StreamedVoiceSnapshot selected; StreamedVoiceBuffer span;
        { StreamInterruptGuard guard;
        for(unsigned i=0;i<kStreamVoiceCount;++i) {
            if(i==kAuditionStreamChannel && best>=0)break;
            const auto& state=states_[i]; const auto& ready=buffers_[i];
            if(!state.sample || !state.step || ready.end>=state.sample->frames || failed_generation_[i]==state.generation) continue;
            const auto until=StreamRefillUrgency(state,ready);
            if(until<least && (i!=kAuditionStreamChannel ||
                BackgroundHeadroom(audition_reserve,kAuditionStreamChannel))) { best=i; least=until; selected=state; span=ready; }
        }
        }
        if(best<0) return;
        unsigned source=0;
        while(source<kSourceCapacity && (!occupied_[source] || &sources_[source]!=selected.sample)) ++source;
        if(source==kSourceCapacity) return;
        const auto& sample=*selected.sample;
        const auto at=std::max({span.end,selected.position,sample.PrefixFrames()});
        if(at>=sample.frames) return;
        const auto count=std::min<std::uint32_t>(sample.layout.read_frames,sample.frames-at);
        const auto started=micros(); bool ok=false;
        {
            SdReadScope ownership; if(!ownership) return; // retry later, no lost save intent
            ok=ReadFrames(files_[source],offsets_[source],sample,at,count,memory_->staging,kStreamReadBytes);
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
        const auto ring_at=at%sample.layout.ring_frames;
        const auto first=std::min<std::uint32_t>(count,sample.layout.ring_frames-ring_at);
        const auto stride=sample.layout.bytes_per_frame;
        auto* ring=reinterpret_cast<unsigned char*>(memory_->rings[best]);
        const auto* staging=reinterpret_cast<const unsigned char*>(memory_->staging);
        std::memcpy(ring+ring_at*stride,staging,first*stride);
        if(first<count) std::memcpy(ring,staging+first*stride,(count-first)*stride);
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
    bool DescribeSource(const WavMetadata& metadata,std::uint64_t file_bytes,const std::int16_t* start,
        StreamedSample& output) noexcept {
        const StreamSourceFormat format{metadata.sample_rate,metadata.channels,metadata.bits};
        const auto stride=format.BytesPerFrame();
        if(metadata.format!=1 || !stride || !IsStreamingPcmRate(format.sample_rate_hz) ||
            metadata.block_align!=stride || metadata.byte_rate!=std::uint64_t(format.sample_rate_hz)*stride ||
            !metadata.frames || metadata.frames>UINT32_MAX || metadata.data_offset>UINT32_MAX) return false;
        // Frame count now fits in 32 bits, so the extent multiplication fits in uint64_t.
        if(metadata.data_bytes!=metadata.frames*stride || metadata.data_offset>file_bytes ||
            metadata.data_bytes>file_bytes-metadata.data_offset) return false;
        output={start,static_cast<std::uint32_t>(metadata.frames),buffers_,format};
        return true;
    }
    static Pcm16MixedSampleView MakeView(const StreamedSample& source) noexcept {
        if(source.format.channels==2)
            return Pcm16MixedSampleView(Pcm16StereoView{source.start,source.frames,source.format.sample_rate_hz,&source});
        return {source.start,source.frames,source.format.sample_rate_hz,&source};
    }
    static bool ReadFrames(FsFile& file,std::uint32_t data_offset,const StreamedSample& source,
        std::uint32_t first,std::uint32_t count,void* destination,std::uint32_t capacity) {
        const auto stride=source.layout.bytes_per_frame;
        if(!stride || !count || first>source.frames || count>source.frames-first) return false;
        const auto bytes=std::uint64_t(count)*stride;
        // These products and their sum fit: frame indices and data_offset are 32-bit,
        // and DescribeSource has already checked the full data extent.
        const auto offset=std::uint64_t(data_offset)+std::uint64_t(first)*stride;
        return bytes<=capacity && bytes<=INT32_MAX && file.seekSet(offset) &&
            file.read(destination,static_cast<std::size_t>(bytes))==static_cast<int>(bytes);
    }

    // Caller holds foreground SD ownership and has proved this slot unretained.
    void ReleaseSource(unsigned index) {
        if(!files_[index].close()) {reports[index].view={};return;} // uncertain close: do not reuse
        occupied_[index]=false;paths_[index].Reset();reports[index]={};sources_[index]={};offsets_[index]=0;--count_;
        if(last_prepared_==index)last_prepared_=kSourceCapacity;
    }
    Storage* memory_=nullptr;
    enum JobPhase{JobIdle,JobOpen,JobInspect,JobPcm,JobClose,JobDone};JobPhase job_phase_=JobIdle;
    unsigned job_index_=kSourceCapacity;std::uint32_t job_read_=0;ResourcePathRef job_path_;
    WavInspectionJob job_inspection_;Pcm16MixedSampleView job_view_{};
    InstrumentAvailability job_result_=InstrumentAvailability::PreparationFailed;unsigned rollback_pins_=0;
    bool occupied_[kSourceCapacity]{};unsigned last_prepared_=kSourceCapacity;
    bool sd_ready_=false;unsigned count_=3;ResourcePathRef paths_[kSourceCapacity];
    FsFile files_[kSourceCapacity]; std::uint32_t offsets_[kSourceCapacity]{};
    StreamedSample sources_[kSourceCapacity]; StreamedVoiceBuffer buffers_[kStreamVoiceCount];
    StreamedVoiceSnapshot states_[kStreamVoiceCount]; std::uint64_t failed_generation_[kStreamVoiceCount]{};
};
}
