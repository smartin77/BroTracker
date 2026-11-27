#if !defined(BROTRACKER_BTT_DEVICE_DIAGNOSTIC)
#error This source requires the explicit opt-in diagnostic environment.
#endif
#include <Arduino.h>
#include <Audio.h> // Producer types only; no graph, AudioMemory or output is constructed.
#include <SD.h>
#include <sd_access.h>
#include <musical_tick_cursor.h>
#include "sd_source_service.h"
#include "fs_backend.h"
#include "device_fixture.h"
#include <malloc.h>
#include <unistd.h>
#include <new>
#include <cstdio>
#include <cstring>

extern "C" char _heap_start, _heap_end;
namespace {
using namespace btt;
using namespace btt::prototype;
using namespace BroTracker;
constexpr const char* kDirectory="/BroTracker/.btt-prototype";
constexpr const char* kWav="BroTracker/.btt-prototype/device-diagnostic.wav";
constexpr unsigned kCycles=8;
DMAMEM alignas(32) unsigned char arena_bytes[65536]; // NOLOAD: initialized explicitly.
DMAMEM SdSampleStreaming::Storage pcm; // Same 143,360-byte storage as production.
alignas(Arena) unsigned char arena_metadata[sizeof(Arena)];
// SDK default constructors only initialize handle metadata; no SD work/allocation.
struct Producer: SdSampleStreaming {Producer() noexcept:SdSampleStreaming(){}};
struct File:FsFile {File() noexcept:FsFile(){}};
struct EmptyBindings {unsigned count=0;struct Binding{Pcm16MixedSampleView sample;} bindings[1];};

enum class Phase { Waiting, Init, WavWrite, WavVerify, Source, Patterns, NoEdit, NoEditVerify,
    Edit, EditVerify, BeginCandidate, CandidateTurn, CancelDrain, CycleEnd, Cleanup, Done, Failed };
struct Diagnostic {
    Arena* arena=nullptr;
    Object<Workspace> work;Object<device::Fixture> fixture,expected;
    Object<FsBackend> backend;Object<FsBackings> pool;
    Object<Document> document,verified;Object<Pattern> pattern;
    Object<Candidate> candidate;Object<AdmissionScratch> scratch;
    Object<Producer> producer;Object<SdSourceService> sources;Object<SourceLease> clone;
    Object<DevelopmentWavInspection> inspection;Object<FsBackings::Output> output;Object<File> file;
    FsBackings::Token tokens[3]{};
    Phase phase=Phase::Waiting;
    unsigned case_index=0,cycle=0,turns=0,wav_at=0,peak=0,base_paths=0,base_bytes=0;
    uint32_t cycle_started=0,service_us=0,max_service_us=0,boundary_msp_min=UINT32_MAX;
    int detail=0;bool started=false,writer_open=false,cleanup_only=false;
    char collision[96]{};

    void Identity() const {
        Serial.printf("BTTDIAG1 BUILD head=%s source=%s schema=1 usb=serial playback=absent physical=PENDING\n",
            BTT_DIAG_GIT_HEAD,BTT_DIAG_SOURCE_SHA);
    }
    void Memory(const char* label) {
#if defined(BROTRACKER_BTT_DIAGNOSTIC_HOST_TEST)
        Serial.printf("BTTDIAG1 MEM %s host_simulator=1 arena=%u peak=%u\n",label,arena?unsigned(arena->used()):0,peak);
#else
        uint32_t msp;asm volatile("mrs %0,msp":"=r"(msp));
        if(msp<boundary_msp_min)boundary_msp_min=msp;
        const auto info=mallinfo();const auto brk=reinterpret_cast<uintptr_t>(sbrk(0));
        const auto end=reinterpret_cast<uintptr_t>(&_heap_end);
        const auto begin=reinterpret_cast<uintptr_t>(&_heap_start);
        const auto uncommitted=brk>=begin && brk<=end?end-brk:0;
        Serial.printf("BTTDIAG1 MEM %s arena=%u peak=%u heap_used_snapshot=%d heap_free_blocks_snapshot=%d heap_uncommitted_snapshot=%lu heap_break=%08lx heap_snapshot_valid=%u boundary_msp=%08lx boundary_msp_min=%08lx\n",
            label,arena?unsigned(arena->used()):0,peak,info.uordblks,info.fordblks,
            static_cast<unsigned long>(uncommitted),static_cast<unsigned long>(brk),unsigned(brk>=begin && brk<=end),
            static_cast<unsigned long>(msp),static_cast<unsigned long>(boundary_msp_min));
#endif
    }
    void Ownership(const char* label) {
        unsigned leases=0;
        if(candidate.get())for(unsigned i=0;i<candidate->count();++i)leases+=bool(candidate->At(i)->source);
        Serial.printf("BTTDIAG1 OWN %s cycle=%u source_slots=%u candidate_leases=%u clone=%u path_records=%u path_bytes=%u backing_refs=%ld service_idle=%u\n",
            label,cycle,producer.get()?producer->SourceCount():0,leases,clone.get()?bool(*clone):0,
            ResourcePaths().UniqueCount(),ResourcePaths().UsedBytes(),document.get()?document->backing.use_count():0,
            sources.get()?sources->CanDestroy():1);
    }
    void Fail(const char* step,int code) {
        phase=Phase::Failed;detail=code;
        Serial.printf("BTTDIAG1 FAIL step=%s code=%d case=%u cycle=%u cleanup=NOT_CONFIRMED files=RETAINED\n",step,code,case_index,cycle);
        if(collision[0])Serial.printf("BTTDIAG1 COLLISION %s\n",collision);
        if(candidate.get())for(unsigned i=0;i<candidate->count();++i){const auto* d=candidate->At(i);
            Serial.printf("BTTDIAG1 DEF id=%lu source_status=%u setting_status=%u path_status=%u root=%u fine=%u\n",
                static_cast<unsigned long>(d->saved.id),unsigned(d->source_status),unsigned(d->settings),unsigned(d->path_status),d->effective_root,d->effective_fine);}
        Ownership("failure");Memory("failure");
    }
    template<class F> bool Measure(const char* step,F operation) {
        detail=0;const auto began=micros();const bool ok=operation();const auto elapsed=micros()-began;
        if(ok)detail=0;
        if(arena && arena->used()>peak)peak=arena->used();
        // Formatting, serial traffic and allocator/stack snapshots are outside the operation timer.
        Serial.printf("BTTDIAG1 %s step=%s us=%lu code=%d case=%u cycle=%u\n",ok?"PASS":"FAIL",step,
            static_cast<unsigned long>(elapsed),detail,case_index,cycle);
        if(!ok)Fail(step,detail);
        return ok;
    }
    bool Objects() {
        if(!arena){
            std::memset(arena_bytes,0,sizeof(arena_bytes));std::memset(&pcm,0,sizeof(pcm));
            arena=new(arena_metadata)Arena;
            if(arena->Init(arena_bytes,sizeof(arena_bytes))!=Code::Ok)return false;
        }
        return arena->New(work)==Code::Ok && arena->New(fixture)==Code::Ok && arena->New(expected)==Code::Ok &&
            arena->New(backend,true)==Code::Ok && arena->New(pool,*backend)==Code::Ok &&
            arena->New(document)==Code::Ok && arena->New(verified)==Code::Ok && arena->New(pattern)==Code::Ok &&
            arena->New(candidate)==Code::Ok && arena->New(scratch)==Code::Ok && arena->New(producer)==Code::Ok &&
            arena->New(sources,*producer)==Code::Ok && arena->New(clone)==Code::Ok &&
            arena->New(inspection)==Code::Ok && arena->New(output)==Code::Ok && arena->New(file)==Code::Ok;
    }
    bool Preflight() {
        if(!SdAccess::AcquireWrite()){detail=100;return false;}
        bool ok=true;
        // Inspect every deterministic output name before creating any file.
        if(SD.sdfs.exists(kWav)){std::snprintf(collision,sizeof(collision),"%s",kWav);ok=false;detail=101;}
        for(unsigned domain=1;domain<=2;++domain)for(unsigned slot=0;slot<3;++slot){char path[96];
            std::snprintf(path,sizeof(path),"%s/%08lx-%u-%08lx.btt",kDirectory,static_cast<unsigned long>(domain),slot,static_cast<unsigned long>(slot+1));
            if(SD.sdfs.exists(path)){std::snprintf(collision,sizeof(collision),"%s",path);ok=false;detail=101;}}
        if(ok && !SD.sdfs.exists(kDirectory) && !SD.sdfs.mkdir(kDirectory,true)){detail=102;ok=false;}
        if(ok){ok=file->open(kDirectory,O_RDONLY) && file->isDirectory();if(!ok)detail=103;
            if(file->isOpen() && !file->close()){detail=104;ok=false;}}
        SdAccess::ReleaseWrite();return ok;
    }
    bool WavOpen() {
        if(!SdAccess::AcquireWrite()){detail=100;return false;}
        const bool ok=file->open(kWav,O_RDWR|O_CREAT|O_EXCL);SdAccess::ReleaseWrite();detail=ok?0:105;return ok;
    }
    bool WavChunk() {
        const auto n=std::min<unsigned>(512,device::kWavBytes-wav_at);
        for(unsigned i=0;i<n;++i)work->block[i]=device::WavByte(wav_at+i);
        if(!SdAccess::AcquireWrite()){detail=100;return false;}
        bool ok=file->write(work->block.data(),n)==n && !file->getError();wav_at+=n;
        if(ok && wav_at==device::kWavBytes){ok=file->sync();const bool closed=file->close();ok=ok && closed;}
        SdAccess::ReleaseWrite();detail=ok?0:106;return ok;
    }
    bool VerifyWav() {
        SdReadScope lease;if(!lease){detail=100;return false;}
        if(!file->open(kWav,O_RDONLY)){detail=107;return false;}
        bool ok=file->size()==device::kWavBytes;
        for(unsigned at=0;ok && at<device::kWavBytes;){const auto n=std::min<unsigned>(512,device::kWavBytes-at);
            ok=file->read(work->block.data(),n)==int(n) && !file->getError();
            for(unsigned i=0;ok && i<n;++i)ok=work->block[i]==device::WavByte(at+i);
            at+=n;
        }
        const bool closed=file->close();detail=ok && closed?0:108;return ok && closed;
    }
    bool Create(unsigned slot) {
        const auto result=pool->Create(*output,tokens[slot]);detail=int(result);writer_open=result==Code::Ok;return writer_open;
    }
    bool Finish(Document& destination) {
        ReaderOwner owner;const auto finished=output->Finish(owner);writer_open=false;
        detail=int(finished);if(finished!=Code::Ok)return false;
        // Production Seal syncs/closes and then reopens through real FsFile backing.
        const auto result=Validate(std::move(owner),destination,*work);detail=int(result);return result==Result::Ok;
    }
    bool Compare(const Reader& reader,const device::Fixture& target) {
        if(reader.size()!=target.size){detail=200;return false;}
        for(unsigned at=0;at<target.size;){const auto n=std::min<unsigned>(512,target.size-at);
            if(reader.read(at,work->block.data(),n)!=n){detail=201;return false;}
            if(std::memcmp(work->block.data(),target.bytes+at,n)){detail=202;return false;}at+=n;}
        return true;
    }
    bool Source() {
        if(!fixture->Build(kWav,case_index?device::Tuning::Absent:device::Tuning::Explicit)){detail=203;return false;}
        if(!Create(0))return false;
        for(unsigned at=0;at<fixture->size;){const auto n=std::min<unsigned>(512,fixture->size-at);
            if(output->write(at,fixture->bytes+at,n)!=n){detail=204;return false;}at+=n;}
        return Finish(*document) && Compare(*document->backing,*fixture);
    }
    bool Patterns() {
        for(unsigned id:{101u,303u}){const auto result=Materialize(*document,id,*pattern,*work);detail=int(result);if(result!=Result::Ok)return false;
            const auto first=pattern->At(0),fx_only=pattern->At(2);
            if(first.note!=(id==101?60:72) || first.instrument!=11 || first.fx!=std::array<uint16_t,3>{17,1024,65535} ||
                first.values!=std::array<uint8_t,3>{0,1,255} || fx_only.note!=255){detail=205;return false;}}
        const auto* order=Find(*document,"ORDR",0);uint8_t bytes[20];
        if(!order || order->length!=20 || document->backing->read(order->offset,bytes,sizeof(bytes))!=sizeof(bytes)){detail=206;return false;}
        for(unsigned i=0;i<3;++i){uint32_t id=0;for(unsigned b=0;b<4;++b)id|=uint32_t(bytes[8+i*4+b])<<(8*b);
            if(id!=(i==1?303u:101u)){detail=207;return false;}}
        return true;
    }
    bool RewriteTo(unsigned slot,uint32_t bpm) {
        if(!Create(slot))return false;
        const auto result=Rewrite(*document,*output,*work,bpm);detail=int(result);return result==Result::Ok && Finish(*verified);
    }
    bool VerifyOutput(uint32_t bpm) {
        if(!expected->Build(kWav,case_index?device::Tuning::Resolved:device::Tuning::Explicit,bpm))return false;
        if(verified->bpm!=bpm || !Compare(*verified->backing,*expected))return false;
        for(unsigned id:{11u,22u,33u}){SavedInstrument value;auto result=InspectInstrument(*verified,id,value,*work);
            detail=int(result);if(result!=Result::Ok)return false;
            const unsigned root=id==33?0:id==22?72:60;
            const unsigned fine=id==33?199:case_index?(id==11?25:0):(id==11?0:25);
            if(value.root!=root || value.fine!=fine || (value.present&3)!=3){detail=208;return false;}}
        verified->backing.reset();return true;
    }
    void BeginCycle() {
        base_paths=ResourcePaths().UniqueCount();base_bytes=ResourcePaths().UsedBytes();turns=0;service_us=max_service_us=0;cycle_started=millis();
        Ownership("before_cycle");
        if(Measure("candidate_begin",[&]{const auto r=candidate->Begin(*document,{101,{}},*work,*pattern,*scratch);detail=int(r);return r==Result::Ok;}))phase=Phase::CandidateTurn;
    }
    SourceStatus Service() {
        const auto began=micros();(void)producer->Service();const auto status=sources->Service();const auto us=micros()-began;
        service_us+=us;max_service_us=std::max(max_service_us,us);return status;
    }
    void CandidateTurn() {
        ++turns;if(turns>500 || millis()-cycle_started>10000){Fail("candidate_timeout",300);return;}
        const auto status=Service();
        if(status==SourceStatus::Quarantined){Fail("owned_close_quarantined",int(status));return;}
        // Odd cycles cancel at progressively later service boundaries, last at
        // Ready BEFORE Candidate::Step can consume the producer completion pin.
        const bool cancel=cycle%2 && (cycle==7?status==SourceStatus::Ready:turns>=cycle*2+1);
        if(cancel){Serial.printf("BTTDIAG1 PASS step=cancel_boundary cycle=%u service_status=%u turns=%u\n",cycle,unsigned(status),turns);phase=Phase::CancelDrain;return;}
        const auto began=micros();const auto result=candidate->Step(*sources,*scratch);const auto us=micros()-began;
        service_us+=us;max_service_us=std::max(max_service_us,us);
        if(result==Admission::Blocked || result==Admission::Io){
            int source=int(SourceStatus::NotRequired);for(unsigned i=0;i<candidate->count();++i){const auto* d=candidate->At(i);
                if(d->required && d->source_status!=SourceStatus::NotPrepared && d->source_status!=SourceStatus::Ready && d->source_status!=SourceStatus::Released){source=int(d->source_status);break;}}
            Fail("candidate_source",source);return;
        }
        if(result==Admission::Ready){
            if(cycle%2){phase=Phase::CancelDrain;return;}
            if(!Measure("candidate_ready_share_clone",[&]{const auto* a=candidate->Find(11);const auto* b=candidate->Find(22);
                detail=301;if(!a || !b || !a->source || !b->source || !(a->source.token()==b->source.token()) || !a->source.Clone(*clone))return false;
                Pcm16MixedSampleView view;const char* path=nullptr;
                if(!sources->Access(clone->token(),view,path) || !path || std::strcmp(path,kWav) || !view.streaming || view.frame_count!=device::kWavFrames || view.sample_rate_hz!=44100)return false;
                const auto* bytes=reinterpret_cast<const uint8_t*>(view.streaming->start);
                for(unsigned i=0;i<view.streaming->PrefixFrames()*2;++i)if(bytes[i]!=device::WavByte(44+i))return false;
                return true;} ))return;
            Ownership("ready");phase=Phase::CancelDrain;
        }
    }
    void CancelDrain() {
        const auto status=Service();if(status==SourceStatus::Quarantined){Fail("cancel_quarantined",int(status));return;}
        const auto began=micros();const auto result=candidate->Cancel();const auto us=micros()-began;
        service_us+=us;max_service_us=std::max(max_service_us,us);
        if(result==Code::Io){Fail("cancel_cleanup_io",int(candidate->cleanup_status()));return;}
        if(result==Code::Busy){if(millis()-cycle_started>10000)Fail("cancel_timeout",302);return;}
        if(result!=Code::Ok){Fail("cancel_failed",int(result));return;}
        if(Measure("candidate_cancel_checked",[&]{detail=303;return candidate->CanDestroy();}))phase=Phase::CycleEnd;
    }
    bool EndCycle() {
        if(*clone){if(producer->ReclaimOne(EmptyBindings{})){detail=304;return false;}clone->Reset();}
        const auto discarded=candidate->Discard();detail=int(discarded);if(discarded!=Code::Ok)return false;
        if(producer->SourceCount()>3)(void)producer->ReclaimOne(EmptyBindings{});
        detail=305;return sources->CanDestroy() && producer->SourceCount()==3 && ResourcePaths().UniqueCount()==base_paths &&
            ResourcePaths().UsedBytes()==base_bytes && document->backing.use_count()==1;
    }
    bool Cleanup() {
        if(clone.get())clone->Reset();
        if(candidate.get()){const auto result=candidate->Discard();if(result!=Code::Ok){detail=int(result);return false;}}
        if(writer_open && output.get()){const auto result=output->Cancel();writer_open=false;if(result!=Code::Ok){detail=int(result);return false;}}
        if(verified.get())verified->backing.reset();
        if(document.get())document->backing.reset();
        if(file.get() && file->isOpen()){
            if(!SdAccess::AcquireWrite()){detail=100;return false;}const bool closed=file->close();SdAccess::ReleaseWrite();
            if(!closed){detail=306;return false;}}
        if(sources.get() && !sources->CanDestroy()){detail=307;return false;}
        if(producer.get() && !producer->Reset()){detail=308;return false;}
        if(pool.get())for(auto token:tokens)if(token.generation && pool->Inspect(token)!=Code::Ok){detail=309;return false;}
        if(pool.get() && !pool->CanDestroy()){detail=310;return false;}
        if(arena){const auto result=arena->Reset();detail=int(result);if(result!=Code::Ok)return false;}
        detail=311;return ResourcePaths().UniqueCount()==0 && ResourcePaths().UsedBytes()==0;
    }
    void Tick() {
        switch(phase) {
        case Phase::Waiting:case Phase::Done:case Phase::Failed:return;
        case Phase::Init:
            Memory("before_mount");
            if(!Measure("sd_begin",[&]{detail=400;return SD.begin(BUILTIN_SDCARD);}))return;
            if(!Measure("arena_init",[&]{detail=int(Code::Capacity);return Objects();}))return;
            if(!Measure("exclusive_preflight",[&]{return Preflight();}))return;
            if(!Measure("wav_exclusive_open",[&]{return WavOpen();}))return;
            phase=Phase::WavWrite;return;
        case Phase::WavWrite:
            if(!Measure("wav_chunk",[&]{return WavChunk();}))return;
            if(wav_at==device::kWavBytes)phase=Phase::WavVerify;
            return;
        case Phase::WavVerify:
            if(Measure("wav_reopen_compare",[&]{return VerifyWav();}))phase=Phase::Source;
            return;
        case Phase::Source:
            if(!Measure("producer_initialize",[&]{producer->Prepare(true,pcm,*inspection);detail=312;return producer->SourceCount()==3;}))return;
            Memory("objects_ready");
            if(Measure("fixture_write_reopen_validate",[&]{return Source();}))phase=Phase::Patterns;
            return;
        case Phase::Patterns:if(Measure("stable_patterns_order_fx",[&]{return Patterns();}))phase=Phase::NoEdit;return;
        case Phase::NoEdit:if(Measure("noedit_rewrite_reopen_validate",[&]{return RewriteTo(1,0);}))phase=Phase::NoEditVerify;return;
        case Phase::NoEditVerify:if(Measure(case_index?"independent_default_insertion":"byte_identical_explicit_defaults",[&]{return VerifyOutput(12500);}))phase=Phase::Edit;return;
        case Phase::Edit:if(Measure("bpm_edit_reopen_validate",[&]{return RewriteTo(2,15000);}))phase=Phase::EditVerify;return;
        case Phase::EditVerify:if(Measure("bpm_unknown_unsupported_preserved",[&]{return VerifyOutput(15000);}))phase=Phase::BeginCandidate;return;
        case Phase::BeginCandidate:BeginCycle();return;
        case Phase::CandidateTurn:CandidateTurn();return;
        case Phase::CancelDrain:CancelDrain();return;
        case Phase::CycleEnd:
            if(!Measure("cycle_ownership_restored",[&]{return EndCycle();}))return;
            Ownership("after_cycle");Memory("after_cycle");
            Serial.printf("BTTDIAG1 PASS step=async_cycle case=%u cycle=%u turns=%u accumulated_us=%lu max_suboperation_us=%lu\n",case_index,cycle,turns,
                static_cast<unsigned long>(service_us),static_cast<unsigned long>(max_service_us));
            phase=++cycle<kCycles?Phase::BeginCandidate:Phase::Cleanup;return;
        case Phase::Cleanup:
            if(candidate.get() && !candidate->CanDestroy()){(void)Service();const auto result=candidate->Cancel();
                if(result==Code::Busy){if(millis()-cycle_started>10000)Fail("cleanup_timeout",302);return;}
                if(result!=Code::Ok){Fail("cleanup_pending",int(result));return;}}
            Ownership("before_cleanup");
            if(!Measure("arena_handles_checked_cleanup",[&]{return Cleanup();}))return;
            Ownership("after_cleanup");Memory("after_cleanup");
            if(!cleanup_only && case_index==0){case_index=1;cycle=0;tokens[0]=tokens[1]=tokens[2]={};
                if(Measure("second_case_objects",[&]{detail=int(Code::Capacity);return Objects();}))phase=Phase::Source;
                return;
            }
            phase=Phase::Done;Serial.printf("BTTDIAG1 PASS step=%s run=COMPLETE stopped_only=1 files=RETAINED arena_peak=%u\n",cleanup_only?"cleanup_only":"suite",peak);return;
        }
    }
    void Command(const char* text) {
        if(!std::strcmp(text,"BTTDIAG STATUS")){Identity();Serial.printf("BTTDIAG1 STATUS phase=%u started=%u case=%u cycle=%u code=%d\n",unsigned(phase),started,case_index,cycle,detail);return;}
        if(!std::strcmp(text,"BTTDIAG RUN")){
            if(started){Serial.println("BTTDIAG1 FAIL step=command reason=one_run_per_boot");return;}
            started=true;Identity();phase=Phase::Init;return;
        }
        if(!std::strcmp(text,"BTTDIAG CLEANUP") && started && phase==Phase::Failed){cleanup_only=true;cycle_started=millis();phase=Phase::Cleanup;return;}
        Serial.println("BTTDIAG1 FAIL step=command reason=expected_RUN_STATUS_or_failed_CLEANUP");
    }
} diagnostic;
char command[48];unsigned command_used=0;bool overflow=false;
}
void setup() {
    Serial.begin(115200);
    diagnostic.Identity();
    Serial.println("BTTDIAG1 WAIT command=BTTDIAG RUN no_tests_or_sd_writes_before_command");
}
void loop() {
    for(unsigned i=0;i<64 && Serial.available();++i){const char c=char(Serial.read());
        if(c=='\r')continue;
        if(c=='\n'){if(overflow)Serial.println("BTTDIAG1 FAIL step=command reason=invalid_ascii_or_too_long");else {command[command_used]=0;diagnostic.Command(command);}
            command_used=0;overflow=false;
        } else if(c>=32 && c<=126 && command_used+1<sizeof(command) && !overflow)command[command_used++]=c;else overflow=true;
    }
    diagnostic.Tick();yield();
}
