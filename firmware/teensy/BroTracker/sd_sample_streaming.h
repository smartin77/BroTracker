#pragma once
#include <Audio.h>
#include <SD.h>
#include <sd_access.h>
#include "sd_wav_preparation.h"
#include "core/audio/streamed_sample.h"
#include <algorithm>
#include <cstring>
#include "bringup/instrument_definition.h"
#include "bringup/sample_waveform_protocol.h"
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
    SdSampleStreaming()=default;
    SdSampleStreaming(const SdSampleStreaming&)=delete;
    SdSampleStreaming& operator=(const SdSampleStreaming&)=delete;
    // Foreground only. Tokens are scoped to this producer's lifetime. Copying a
    // token does not retain it; each successful acquire/retain needs one release.
    struct CandidatePin { const SdSampleStreaming* producer=nullptr; std::uint32_t generation=0; unsigned slot=kSourceCapacity; };
    enum class PinResult { Ready, InvalidGeneration, Capacity };
    enum class SourceCapacity { Available, Full, GenerationExhausted };
    enum class PreparationStatus { Pending, Ready, Missing, Capacity, PathCapacity, Unsupported, Io,
        Cancelled, Quarantined, Stale, GenerationExhausted, StorageUnavailable };
    // Owned tickets never wrap/reset. Legacy completed results remain reserved
    // until their caller acknowledges consumption; existing legacy APIs retain
    // their previous replacement behavior amongst themselves.
    bool AcknowledgePreparation() noexcept {
        if(owned_job_ || job_phase_!=JobDone)return false;
        job_phase_=JobIdle;return true;
    }
    bool BeginOwnedPreparation(const char* path,std::uint32_t& ticket) {
        if(ticket || owned_job_ || job_phase_!=JobIdle || job_sequence_==UINT32_MAX)return false;
        if(!BeginPrepareAsync(path))return false;
        ticket=owned_job_=++job_sequence_;
        PinOwnedCompletion();return true;
    }
    bool PreparationTicketsExhausted() const noexcept {return job_sequence_==UINT32_MAX;}
    PreparationStatus OwnedPreparationStatus(std::uint32_t ticket) const noexcept {
        if(!ticket || ticket!=owned_job_)return PreparationStatus::Stale;
        if(owned_quarantined_)return PreparationStatus::Quarantined;
        return job_phase_==JobDone?job_detail_:PreparationStatus::Pending;
    }
    void StepOwnedPreparation(std::uint32_t ticket) {
        if(!ticket || ticket!=owned_job_)return;
        PreparationStepImpl();PinOwnedCompletion();
    }
    bool CancelOwnedPreparation(std::uint32_t ticket) noexcept {
        if(!ticket || ticket!=owned_job_)return false;
        if(owned_pin_.generation){(void)ReleaseCandidatePin(owned_pin_);owned_pin_={};}
        job_detail_=PreparationStatus::Cancelled;
        if(job_phase_!=JobDone)job_phase_=JobClose;
        return true;
    }
    // Transfers the completion pin on Ready, otherwise acknowledges a checked
    // terminal result. Pending/quarantined cleanup cannot be acknowledged away.
    bool TakeOwnedPreparation(std::uint32_t ticket,CandidatePin& pin) noexcept {
        if(!ticket || ticket!=owned_job_ || job_phase_!=JobDone || owned_quarantined_ || pin.generation)return false;
        pin=owned_pin_;owned_pin_={};owned_job_=0;job_phase_=JobIdle;return true;
    }
    SourceCapacity PreparationCapacity() const noexcept {
        if(source_sequence_==UINT32_MAX)return SourceCapacity::GenerationExhausted;
        for(unsigned i=3;i<kSourceCapacity;++i)if(!occupied_[i] && i!=job_index_)return SourceCapacity::Available;
        return SourceCapacity::Full;
    }
    // Snapshot identity without borrowing a PCM view. Acquire must recheck it.
    std::uint32_t PreparedGeneration(const char* path) const noexcept {
        if(!path)return 0;
        for(unsigned i=0;i<kSourceCapacity;++i)
            if(occupied_[i] && reports[i].view.data && !std::strcmp(paths_[i].Get(),path))return source_tokens_[i];
        return 0;
    }
    PinResult AcquireCandidatePin(std::uint32_t generation,CandidatePin& output) noexcept {
        if(output.generation)return PinResult::InvalidGeneration; // Never overwrite an owned reservation.
        for(unsigned i=0;generation && i<kSourceCapacity;++i)
            if(occupied_[i] && reports[i].view.data && source_tokens_[i]==generation) {
                if(candidate_pins_[i]==UINT16_MAX)return PinResult::Capacity;
                ++candidate_pins_[i];output={this,generation,i};return PinResult::Ready;
            }
        return PinResult::InvalidGeneration;
    }
    bool RetainCandidatePin(CandidatePin pin) noexcept {
        if(!ValidCandidatePin(pin) || candidate_pins_[pin.slot]==UINT16_MAX)return false;
        ++candidate_pins_[pin.slot];return true;
    }
    bool ReleaseCandidatePin(CandidatePin pin) noexcept {
        if(!ValidCandidatePin(pin))return false;
        --candidate_pins_[pin.slot];return true;
    }
    // Returned view/path remain valid only while caller owns a pin (or another
    // existing protection). Access does not transfer ownership to an audio voice.
    bool AccessCandidatePin(CandidatePin pin,Pcm16MixedSampleView& view,const char*& path) const noexcept {
        if(!ValidCandidatePin(pin))return false;
        view=reports[pin.slot].view;path=paths_[pin.slot].Get();return true;
    }
    struct Storage {
        std::int16_t rings[kStreamVoiceCount][kStreamRingFrames];
        std::int16_t starts[kSourceCapacity][kStreamStartFrames];
        std::int16_t staging[kStreamReadFrames];
    };
    // Stopped foreground, bindings/renderers detached. Pins/rollback/published
    // voice spans refuse reset. Sequence is never reset, including reinitialization.
    // An uncertain close quarantines its slot/path and refuses reinitialization.
    bool Reset() {
        if(owned_job_)return false;
        for(unsigned i=0;i<kSourceCapacity;++i)if(candidate_pins_[i])return false;
        if(rollback_pins_)return false;
        {StreamInterruptGuard guard;for(unsigned c=0;c<kStreamVoiceCount;++c)
            if(states_[c].sample || buffers_[c].sample)return false;}
        SdReadScope lease;if(!lease)return false;
        sd_ready_=false;memory_=nullptr;bool closed=true;
        for(unsigned i=0;i<kSourceCapacity;++i) {
            source_tokens_[i]=0;reports[i]={};
            if(files_[i] && !files_[i].close()){occupied_[i]=true;closed=false;continue;}
            occupied_[i]=false;paths_[i].Reset();sources_[i]={};offsets_[i]=0;fingerprints_[i]={};
        }
        count_=3;for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i])++count_;
        if(!closed){job_phase_=JobIdle;job_index_=kSourceCapacity;return false;}
        job_path_.Reset();job_view_={};job_phase_=JobIdle;job_index_=kSourceCapacity;job_read_=0;
        job_result_=InstrumentAvailability::PreparationFailed;last_prepared_=kSourceCapacity;count_=3;
        for(unsigned c=0;c<kStreamVoiceCount;++c){states_[c]={};buffers_[c]={};failed_generation_[c]=0;}
        return true;
    }
    void Prepare(bool sd_ready,Storage& memory,const DevelopmentWavInspection& inspection) {
        if(!Reset())return;
        memory_=&memory;sd_ready_=sd_ready;
        for(unsigned i=0;i<kStreamVoiceCount;++i) buffers_[i].ring=memory.rings[i];
        for(unsigned i=0;i<3;++i) {
            occupied_[i]=true;
            (void)paths_[i].Assign(kDevelopmentWavPaths[i]);
            const auto& found=inspection.files[i];
            auto& source=sources_[i]; auto& report=reports[i];
            report.attempted=true; report.sd_ready=sd_ready; report.capacity=kStreamStartFrames;
            report.address=reinterpret_cast<std::uintptr_t>(memory.starts[i]);
            if(!sd_ready || found.validation!=WavBufferStatus::Success || source_sequence_==UINT32_MAX) continue;
            SdReadScope ownership; if(!ownership) continue;
            if(!files_[i].open(kDevelopmentWavPaths[i],O_RDONLY)) continue;
            StreamedSample candidate;
            if(!DescribeSource(found.metadata,files_[i].size(),memory.starts[i],candidate) ||
                !ReadFrames(files_[i],static_cast<std::uint32_t>(found.metadata.data_offset),candidate,0,
                    candidate.PrefixFrames(),memory.starts[i],kStreamStartBytes)) { files_[i].close(); continue; }
            offsets_[i]=static_cast<std::uint32_t>(found.metadata.data_offset);
            source=candidate;
            StampSource(i,found.metadata.file_bytes);
            report.capacity=source.layout.start_frames;
            report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
            report.view=MakeView(source);
        }
    }
    // Stopped foreground only. New immutable sources never replace published PCM.
    InstrumentAvailability PreparePath(const char* path,Pcm16MonoView& output) {
        Pcm16MixedSampleView prepared;
        const auto result=PreparePath(path,prepared,false,false,false,false);
        if(result==InstrumentAvailability::Ready) output=prepared.Mono();
        return result;
    }
    InstrumentAvailability PreparePath(const char* path,Pcm16MixedSampleView& output,bool stereo=true,
        bool lower_rates=true,bool pcm8_and_8363=true,bool library_rates=true) {
        if(!sd_ready_ || !memory_)return InstrumentAvailability::StorageUnavailable;
        if(!InstrumentDefinitions::ValidResource(path))return InstrumentAvailability::NoResource;
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i].Get(),path) && reports[i].view.data) {
            const auto& format=sources_[i].format;
            if(!WavBufferDetail::SupportsIntegerPcm(1,format.channels,format.bits_per_sample,format.sample_rate_hz,
                stereo,lower_rates,pcm8_and_8363,library_rates))return InstrumentAvailability::PreparationFailed;
            output=reports[i].view;return InstrumentAvailability::Ready;
        }
        if(PreparationCapacity()!=SourceCapacity::Available)return InstrumentAvailability::PreparationFailed;
        unsigned index=3;while(index<kSourceCapacity && (occupied_[index] || index==job_index_))++index;
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
        if(InspectWavMetadata(reader,metadata,kWavInspectionChunkLimit,stereo,lower_rates,pcm8_and_8363,library_rates)!=WavBufferStatus::Success ||
            !DescribeSource(metadata,metadata.file_bytes,memory_->starts[index],source)) {
            AbandonPreparation(index,candidate);return InstrumentAvailability::PreparationFailed;
        }
        const auto offset=static_cast<std::uint32_t>(metadata.data_offset);
        if(!ReadFrames(file,offset,source,0,source.PrefixFrames(),memory_->starts[index],kStreamStartBytes)) {
            AbandonPreparation(index,candidate);return InstrumentAvailability::PreparationFailed;
        }
        offsets_[index]=offset;
        sources_[index]=source;
        StampSource(index,metadata.file_bytes);
        auto& report=reports[index];report.attempted=report.sd_ready=true;report.capacity=source.layout.start_frames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};
        report.view=MakeView(sources_[index]);
        paths_[index]=candidate;occupied_[index]=true;last_prepared_=index;++count_;output=report.view;return InstrumentAvailability::Ready;
    }
    // Only the just-prepared, uninstalled candidate is discardable on rejection.
    void DiscardLast(const char* path) {
        if(last_prepared_>=kSourceCapacity || !occupied_[last_prepared_] || std::strcmp(paths_[last_prepared_].Get(),path))return;
        if(ProtectedSource(last_prepared_))return;
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
        for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i] && !retained[i] && !ProtectedSource(i))ReleaseSource(i);
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
        for(unsigned i=3;i<kSourceCapacity;++i)if(occupied_[i] && !ProtectedSource(i)) {
            bool keep=false;
            for(unsigned j=0;j<bindings.count;++j)keep|=bindings.bindings[j].sample.streaming==&sources_[i];
            {StreamInterruptGuard guard;for(unsigned c=0;c<kStreamVoiceCount;++c)keep|=states_[c].sample==&sources_[i] || buffers_[c].sample==&sources_[i];}
            if(!keep){SdReadScope lease;if(!lease)return false;ReleaseSource(i);return true;}
        }
        return false;
    }
    bool BeginPrepareAsync(const char* path) {
        if(owned_job_)return false;
        if(job_phase_!=JobIdle && job_phase_!=JobDone)return false;
        job_detail_=PreparationStatus::Io;owned_quarantined_=false;
        job_path_.Reset();job_view_={};job_result_=InstrumentAvailability::PreparationFailed;job_index_=kSourceCapacity;job_read_=0;job_inspection_.Begin(true,true,true,true);
        job_phase_=JobDone;
        if(!sd_ready_ || !memory_){job_detail_=PreparationStatus::StorageUnavailable;job_result_=InstrumentAvailability::StorageUnavailable;return true;}
        if(!InstrumentDefinitions::ValidResource(path)){job_detail_=PreparationStatus::Unsupported;job_result_=InstrumentAvailability::NoResource;return true;}
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && !std::strcmp(paths_[i].Get(),path) && reports[i].view.data) {
            job_detail_=PreparationStatus::Ready;job_view_=reports[i].view;job_result_=InstrumentAvailability::Ready;return true;
        }
        if(PreparationCapacity()!=SourceCapacity::Available){job_detail_=PreparationCapacity()==SourceCapacity::Full?PreparationStatus::Capacity:PreparationStatus::GenerationExhausted;return true;}
        unsigned i=3;while(i<kSourceCapacity && occupied_[i])++i;
        if(i==kSourceCapacity)return true;
        if(!job_path_.Assign(path)){job_detail_=PreparationStatus::PathCapacity;job_result_=InstrumentAvailability::PathCapacity;return true;}
        job_index_=i;job_phase_=JobOpen;return true;
    }
    bool PreparationDone()const{return !owned_job_ && job_phase_==JobDone;}
    InstrumentAvailability PreparationResult()const{return owned_job_?InstrumentAvailability::NotPrepared:job_result_;}
    Pcm16MonoView PreparedView()const{return owned_job_?Pcm16MonoView{}:job_view_.Mono();}
    Pcm16MixedSampleView PreparedMixedView()const{return owned_job_?Pcm16MixedSampleView{}:job_view_;}
    void CancelPreparation(){if(!owned_job_ && job_phase_!=JobIdle && job_phase_!=JobDone)job_phase_=JobClose;}
    // No loop over file chunks: one open, bounded inspection/read, <=512-byte
    // PCM seek/read, or close per foreground turn, after active refills.
    void PreparationStep() {
        if(!owned_job_)PreparationStepImpl();
    }
private:
    void PreparationStepImpl() {
        if(job_phase_==JobIdle || job_phase_==JobDone)return;
        SdReadScope lease;if(!lease)return;
        auto& file=files_[job_index_];
        auto& source=sources_[job_index_]; // Reserved foreground slot, unpublished until the prefix is complete.
        if(job_phase_==JobClose){
            if(owned_job_) {
                if(file && !file.close()){owned_quarantined_=true;return;}
                owned_quarantined_=false;source={};
            } else AbandonPreparation(job_index_,job_path_);
            job_path_.Reset();job_index_=kSourceCapacity;job_phase_=JobDone;return;
        }
        if(job_phase_==JobOpen) {
            if(!file.open(job_path_.Get(),O_RDONLY)){job_detail_=PreparationStatus::Missing;job_result_=InstrumentAvailability::ResourceUnavailable;job_phase_=JobClose;return;}
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
                    !DescribeSource(m,m.file_bytes,memory_->starts[job_index_],source)) {
                    job_detail_=job_inspection_.Status()==WavBufferStatus::IoError?PreparationStatus::Io:PreparationStatus::Unsupported;
                    job_phase_=JobClose;
                }
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
        if(source_sequence_==UINT32_MAX){job_detail_=PreparationStatus::GenerationExhausted;job_phase_=JobClose;return;}
        StampSource(job_index_,job_inspection_.Metadata().file_bytes);
        auto& report=reports[job_index_];report.attempted=report.sd_ready=true;report.capacity=source.layout.start_frames;
        report.result={SdWavBufferStatus::Success,WavBufferStatus::Success};report.view=MakeView(source);
        paths_[job_index_]=job_path_;job_path_.Reset();occupied_[job_index_]=true;last_prepared_=job_index_;++count_;
        __DMB(); // Publish immutable PCM/source data before the audio binding can reference it.
        job_detail_=PreparationStatus::Ready;job_view_=report.view;job_result_=InstrumentAvailability::Ready;job_index_=kSourceCapacity;job_phase_=JobDone;
    }
public:
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
    // Reports a selected refill even if the read could not complete. Optional
    // foreground work can yield without scanning the voices a second time.
    bool Service(unsigned audition_reserve=1024) {
        if(!memory_) return false;
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
        if(best<0) return false;
        unsigned source=0;
        while(source<kSourceCapacity && (!occupied_[source] || &sources_[source]!=selected.sample)) ++source;
        if(source==kSourceCapacity) return true;
        const auto& sample=*selected.sample;
        const auto at=std::max({span.end,selected.position,sample.PrefixFrames()});
        if(at>=sample.frames) return true;
        const auto count=std::min<std::uint32_t>(sample.layout.read_frames,sample.frames-at);
        const auto started=micros(); bool ok=false;
        {
            SdReadScope ownership; if(!ownership) return true; // retry later, no lost save intent
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
        if(!current) { ++stale_reads; return true; }
        if(!ok) { failed_generation_[best]=selected.generation; return true; }
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
        return true;
    }
    StreamedVoiceSnapshot Snapshot(unsigned i) const {
        StreamInterruptGuard guard; return states_[i];
    }
    unsigned P99() const { unsigned n=0; for(unsigned i=0;i<128;++i) { n+=histogram[i]; if(reads && n>=(reads*99+99)/100) return i==127?max_read_us:(i+1)*100; } return 0; }
    // Foreground only. Reuse the exact prepared resource without a new file
    // handle or long-lived SD lease.
    WaveformSource DescribeWaveform(const Pcm16MixedSampleView& view) const {
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && view.streaming==&sources_[i]) {
            const auto& source=sources_[i];
            return {source_tokens_[i],source.frames,source.format.sample_rate_hz,
                source.format.channels,source.format.bits_per_sample};
        }
        return {};
    }
    WaveformFingerprint DescribeWaveformFingerprint(std::uint32_t token) const {
        for(unsigned i=0;token && i<kSourceCapacity;++i)
            if(occupied_[i] && source_tokens_[i]==token)return fingerprints_[i];
        return {};
    }
    bool ReadWaveform(std::uint32_t token,std::uint32_t offset,std::uint8_t* pcm,unsigned bytes) {
        if(!token || !bytes || bytes>kWaveformIdleBytes)return false;
        for(unsigned i=0;i<kSourceCapacity;++i)if(occupied_[i] && source_tokens_[i]==token) {
            const auto stride=sources_[i].layout.bytes_per_frame;
            if(offset%stride || bytes%stride)return false;
            SdReadScope lease;
            return lease && ReadFrames(files_[i],offsets_[i],sources_[i],offset/stride,
                bytes/stride,pcm,kWaveformIdleBytes);
        }
        return false;
    }
    SdWavPreparation reports[kSourceCapacity];
    std::uint32_t reads=0,max_read_us=0,io_errors=0,stale_reads=0,histogram[128]{};
    std::uint64_t total_read_us=0;
private:
    void PinOwnedCompletion() noexcept {
        if(!owned_job_ || job_phase_!=JobDone || job_detail_!=PreparationStatus::Ready || owned_pin_.generation)return;
        const auto generation=DescribeWaveform(job_view_).token;
        const auto result=AcquireCandidatePin(generation,owned_pin_);
        if(result!=PinResult::Ready)job_detail_=result==PinResult::Capacity?PreparationStatus::Capacity:PreparationStatus::Io;
    }
    friend struct SdSampleStreamingTestAccess;
    // Failed/cancelled preparations were never pinnable or published. Keep an
    // uncertain handle and its path reserved, exactly as for failed reclamation.
    void AbandonPreparation(unsigned i,const ResourcePathRef& path) {
        if(files_[i] && !files_[i].close()) {occupied_[i]=true;paths_[i]=path;++count_;}
        else sources_[i]={};
    }
    bool ValidCandidatePin(CandidatePin pin) const noexcept {
        return pin.producer==this && pin.slot<kSourceCapacity && pin.generation && occupied_[pin.slot] &&
            source_tokens_[pin.slot]==pin.generation && candidate_pins_[pin.slot] && reports[pin.slot].view.data;
    }
    bool ProtectedSource(unsigned i) const noexcept {
        if(candidate_pins_[i] || (rollback_pins_&(1u<<i)) || i==job_index_)return true;
        StreamInterruptGuard guard;
        for(unsigned c=0;c<kStreamVoiceCount;++c)
            if(states_[c].sample==&sources_[i] || buffers_[c].sample==&sources_[i])return true;
        return false;
    }
    void StampSource(unsigned index,std::uint64_t file_bytes) {
        const auto& source=sources_[index];
        const auto bytes=source.PrefixFrames()*source.layout.bytes_per_frame;
        fingerprints_[index]={file_bytes,WaveformPrefixDigest(
            reinterpret_cast<const std::uint8_t*>(source.start),bytes),offsets_[index],bytes};
        source_tokens_[index]=source_sequence_==UINT32_MAX?0:++source_sequence_;
    }
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
        if(ProtectedSource(index))return;
        if(files_[index] && !files_[index].close()) {reports[index].view={};return;} // uncertain close: do not reuse
        source_tokens_[index]=0;
        occupied_[index]=false;paths_[index].Reset();reports[index]={};sources_[index]={};offsets_[index]=0;--count_;
        if(last_prepared_==index)last_prepared_=kSourceCapacity;
    }
    Storage* memory_=nullptr;
    std::uint32_t owned_job_=0,job_sequence_=0;
    CandidatePin owned_pin_{};
    PreparationStatus job_detail_=PreparationStatus::Io;bool owned_quarantined_=false;
    enum JobPhase{JobIdle,JobOpen,JobInspect,JobPcm,JobClose,JobDone};JobPhase job_phase_=JobIdle;
    unsigned job_index_=kSourceCapacity;std::uint32_t job_read_=0;ResourcePathRef job_path_;
    WavInspectionJob job_inspection_;Pcm16MixedSampleView job_view_{};
    InstrumentAvailability job_result_=InstrumentAvailability::PreparationFailed;unsigned rollback_pins_=0;
    bool occupied_[kSourceCapacity]{};unsigned last_prepared_=kSourceCapacity;
    bool sd_ready_=false;unsigned count_=3;ResourcePathRef paths_[kSourceCapacity];
    FsFile files_[kSourceCapacity]; std::uint32_t offsets_[kSourceCapacity]{};
    WaveformFingerprint fingerprints_[kSourceCapacity]{};
    std::uint32_t source_tokens_[kSourceCapacity]{},source_sequence_=0;
    std::uint16_t candidate_pins_[kSourceCapacity]{};
    StreamedSample sources_[kSourceCapacity]; StreamedVoiceBuffer buffers_[kStreamVoiceCount];
    StreamedVoiceSnapshot states_[kStreamVoiceCount]; std::uint64_t failed_generation_[kStreamVoiceCount]{};
};
}
