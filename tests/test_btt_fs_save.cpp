#include "test_framework.h"
#include "fs_save.h"
#include "btt_pattern_layout.h"
#include <iostream>
#include <cstdlib>
#include <new>

static bool count_allocations=false;
static size_t adapter_allocations=0,fake_allocations=0;
void* operator new(size_t n) {
    if(auto* p=std::malloc(n?n:1)) {
        if(count_allocations) {if(DeviceSd::allocation_scope)++fake_allocations;else ++adapter_allocations;}
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

namespace {
using namespace btt;
using namespace btt::prototype;
namespace f=btt_layout;
constexpr auto canonical="/Tunes/save.btt";
constexpr auto candidate="/Tunes/save.btt.candidate";
constexpr auto previous="/Tunes/save.btt.previous";
void ResetFaults() {
    DeviceSd::fail_write=false;DeviceSd::fail_write_path.clear();DeviceSd::fail_read_path.clear();
    DeviceSd::fail_sync_path.clear();DeviceSd::fail_close_path.clear();DeviceSd::fail_remove_path.clear();
    DeviceSd::fail_open_path.clear();DeviceSd::short_read=DeviceSd::short_write=0;
    DeviceSd::fail_rename=DeviceSd::fail_rename_second=DeviceSd::rename_calls=0;
    DeviceSd::close_failure_closes_inner=false;DeviceSd::read_open_hook={};
}
std::vector<uint8_t> Tune(uint32_t bpm=12500) {
    std::vector<uint8_t> tune,order,inst,payload;
    f::Tlv(tune,1,4,std::vector<uint8_t>(16,73));f::Tlv(tune,2,5,{'S','a','v','e'});
    f::Scalar(tune,3,4,bpm);f::Scalar(tune,4,1,8);f::Scalar(tune,5,4,100);f::Scalar(tune,6,4,1000);
    f::Tlv(tune,777,99,{0,255,7},false);
    for(unsigned id:{101,303,101})f::Put(payload,id,4);f::Tlv(order,1,6,payload);
    std::vector<f::Section> sections{{"TUNE",0,tune},{"ORDR",0,order}};
    for(unsigned id:{11,22,33}) {
        inst.clear();f::Scalar(inst,1,2,1);f::Tlv(inst,2,6,{0,'x','.','w','a','v'},false);
        if(id!=11)f::Scalar(inst,3,2,id==33?0:72,false);
        if(id!=22)f::Scalar(inst,4,2,id==33?199:12,false);
        f::Tlv(inst,777,99,{0,254,42},false);sections.push_back({"INST",id,inst});
    }
    for(unsigned id:{101,303}) {
        f::Pattern p;p.Clear(3);f::Cell cell;cell.note=60;cell.instrument=11;
        cell.fx={601,602,603};cell.values={0,42,255};p.Put(id==101?0:17,cell);
        payload.clear();CHECK(f::Encode(p,3,payload));sections.push_back({"PATT",id,payload});
    }
    sections.push_back({"XTRA",99,std::vector<uint8_t>(60000,173),false});return f::Envelope(sections);
}
struct Bytes final:Writer {
    std::vector<uint8_t> bytes;
    const void* identity()const override{return this;}
    bool resize(uint32_t n)override{bytes.resize(n);return true;}
    unsigned write(uint32_t at,const void* p,unsigned n)override{
        if(at+n>bytes.size())bytes.resize(at+n);std::memcpy(bytes.data()+at,p,n);return n;
    }
};
struct Fixture {
    FsBackend backend{true};FsBackings registry{backend};FsBackings::Token token;
    ReaderOwner owner;Document source,scratch;Workspace work;FsSave save;
    std::vector<uint8_t> original,expected,old;
    Fixture(bool existing=false) {
        ResetFaults();DeviceSd::files.clear();original=Tune();old=Tune(14500);
        FsBackings::Output out;CHECK_EQ(registry.Create(out,token),Code::Ok);
        for(unsigned at=0;at<original.size();at+=512) {
            const auto n=std::min<size_t>(512,original.size()-at);CHECK_EQ(out.write(at,original.data()+at,n),n);
        }
        CHECK_EQ(out.Finish(owner),Code::Ok);CHECK_EQ(Validate(owner,source,work),Result::Ok);
        Bytes bytes;CHECK_EQ(Rewrite(source,bytes,work,13750),Result::Ok);expected=std::move(bytes.bytes);
        if(existing)DeviceSd::files[canonical].bytes=old;
    }
    SaveOutcome Run(bool ready=true,const char* path="Tunes/save.btt") {
        count_allocations=true;
        const auto result=save.Run(source,registry,token,path,scratch,work,ready,13750);
        count_allocations=false;CHECK_EQ(adapter_allocations,0u);return result;
    }
    void SourceIntact() {
        CHECK(source.accepted);CHECK_EQ(source.backing->identity(),registry.Identity(token));
        CHECK_EQ(source.backing.use_count(),2);uint8_t block[512];
        for(unsigned at=0;at<original.size();at+=512) {
            const auto n=std::min<size_t>(512,original.size()-at);CHECK_EQ(source.backing->read(at,block,n),n);
            CHECK(!std::memcmp(block,original.data()+at,n));
        }
        CHECK(!scratch.backing);CHECK(!scratch.accepted);
    }
    ~Fixture() {
        ResetFaults();CHECK(save.Cleanup(true));CHECK(save.CanDestroy());
        source.backing.reset();owner.reset();CHECK_EQ(registry.Discard(token),Code::Ok);CHECK(registry.CanDestroy());
    }
};
TEST_CASE(FsSaveNewExistingRepeatedAndCompletePreservation) {
    for(bool existing:{false,true}) {
        Fixture f(existing);
        for(unsigned i=0;i<5;++i) {
            CHECK_EQ(f.Run(),SaveOutcome::Saved);CHECK(f.save.promoted());CHECK(f.save.CanDestroy());
            CHECK(DeviceSd::files[canonical].bytes==f.expected);
            CHECK(!SD.sdfs.exists(candidate));CHECK(!SD.sdfs.exists(previous));f.SourceIntact();
        }
        // Exact Rewrite comparison above includes every opaque section/TLV,
        // order, both patterns/all FX and tune identity. Inspect default rules.
        SavedInstrument inst;CHECK_EQ(InspectInstrument(f.source,33,inst,f.work),Result::Ok);
        CHECK_EQ(inst.root,0u);CHECK_EQ(inst.fine,199u);
        CHECK(f.expected.size()==f.original.size()+20); // Two independent missing u16 TLVs.
    }
}
TEST_CASE(FsSaveCollisionAndPathOwnershipRejections) {
    Fixture f(true);
    for(const char* path:{candidate,previous}) {
        DeviceSd::files[path].bytes={7,8};const auto before=DeviceSd::removed;
        CHECK_EQ(f.Run(),SaveOutcome::Collision);CHECK_EQ(DeviceSd::removed,before);
        CHECK(DeviceSd::files[path].bytes==std::vector<uint8_t>({7,8}));DeviceSd::files.erase(path);
    }
    for(const char* path:{"BROTRACKER/.BTT-PROTOTYPE/a.btt","BroTracker/.btt-load/a.btt",
        "BroTracker/.BTT-PROTOTYPE","BROTRA~1/file.btt","Tunes/../save.btt","Tunes/save.btt.",
        "Tunes/save.btt ","/Tunes/save.btt","Tunes//save.btt"})CHECK_EQ(f.Run(true,path),SaveOutcome::Invalid);
    auto token=f.token;++token.generation;
    CHECK_EQ(f.save.Run(f.source,f.registry,token,"Tunes/save.btt",f.scratch,f.work,true),SaveOutcome::Invalid);
    CHECK_EQ(f.save.Run(f.source,f.registry,f.token,"Tunes/save.btt",f.source,f.work,true),SaveOutcome::Invalid);
    CHECK_EQ(f.Run(false),SaveOutcome::Busy);
    CHECK(BroTracker::SdAccess::AcquireRead());CHECK_EQ(f.Run(),SaveOutcome::Busy);BroTracker::SdAccess::ReleaseRead();
    CHECK(DeviceSd::files[canonical].bytes==f.old);f.SourceIntact();
    // FAT case folding also applies to recovery collisions.
    DeviceSd::files["/TUNES/SAVE.BTT.CANDIDATE"].bytes={9};CHECK_EQ(f.Run(),SaveOutcome::Collision);
    DeviceSd::files.erase(candidate);
}
TEST_CASE(FsSaveIoFaultsPreservePriorBytesAndSource) {
    for(unsigned fault=0;fault<12;++fault) {
        Fixture f(true);
        switch(fault) {
        case 0:DeviceSd::fail_write_path=candidate;break;
        case 1:DeviceSd::short_write=24;break;
        case 2:DeviceSd::fail_sync_path=candidate;break;
        case 3:DeviceSd::fail_close_path=candidate;break;
        case 4:DeviceSd::fail_open_path=candidate;break; // exclusive create
        case 5:DeviceSd::read_open_hook=[](const std::string& p){if(p==candidate)DeviceSd::fail_open_path=candidate;};break;
        case 6:DeviceSd::fail_read_path=candidate;break;
        case 7:DeviceSd::short_read=32;break;
        case 8:DeviceSd::fail_close_path=canonical;break;
        case 9:DeviceSd::fail_close_path=candidate;DeviceSd::close_failure_closes_inner=true;break;
        case 10:DeviceSd::read_open_hook=[](const std::string& p){if(p==candidate)DeviceSd::short_read=32;};break;
        case 11:DeviceSd::read_open_hook=[](const std::string& p){if(p==candidate)DeviceSd::files[p].directory=true;};break;
        }
        auto result=f.Run();CHECK(result==SaveOutcome::Failed || result==SaveOutcome::CleanupRequired);
        CHECK(!f.save.promoted());CHECK(DeviceSd::files[canonical].bytes==f.old);CHECK(!SD.sdfs.exists(previous));
        if(fault==3 || fault==8) {
            CHECK(!f.save.CanDestroy());CHECK(!f.save.Cleanup(false));CHECK_EQ(f.Run(),SaveOutcome::Busy);
        }
        ResetFaults();CHECK(f.save.Cleanup(true));CHECK(!SD.sdfs.exists(candidate));f.SourceIntact();
        CHECK_EQ(f.Run(),SaveOutcome::Saved);CHECK(DeviceSd::files[canonical].bytes==f.expected);
        std::cout<<"fault="<<fault<<" prior=exact source=owned cleanup=ok retry=saved\n";
    }
}
TEST_CASE(FsSaveReopenedValidationAndExactComparisonFailures) {
    for(bool valid:{false,true}) {
        Fixture f(true);const auto wrong=Tune(10000);
        DeviceSd::read_open_hook=[&](const std::string& p){if(p==candidate) {
            if(valid)DeviceSd::files[p].bytes=wrong;
            else DeviceSd::files[p].bytes[0]^=1;
        }};
        CHECK_EQ(f.Run(),SaveOutcome::Failed);CHECK(f.save.detail()!=Result::Ok);
        CHECK(DeviceSd::files[canonical].bytes==f.old);CHECK(!SD.sdfs.exists(candidate));f.SourceIntact();
    }
}
TEST_CASE(FsSavePromotionRollbackBackupAndCleanupOutcomes) {
    for(unsigned fault=0;fault<7;++fault) {
        Fixture f(fault!=6);
        if(fault==0)DeviceSd::fail_rename=1;
        if(fault==1 || fault==2)DeviceSd::fail_rename=2;
        if(fault==2)DeviceSd::fail_rename_second=3;
        if(fault==3)DeviceSd::fail_remove_path=previous;
        if(fault==4){DeviceSd::fail_write_path=candidate;DeviceSd::fail_remove_path=candidate;}
        if(fault==5){DeviceSd::fail_rename=2;DeviceSd::fail_remove_path=candidate;}
        if(fault==6)DeviceSd::fail_rename=1;
        const auto result=f.Run();
        if(fault==2) {
            CHECK_EQ(result,SaveOutcome::RollbackFailed);CHECK(!SD.sdfs.exists(canonical));
            CHECK(DeviceSd::files[previous].bytes==f.old);CHECK(DeviceSd::files[candidate].bytes==f.expected);
            CHECK(f.save.previous_retained());CHECK(f.save.candidate_retained());
        } else if(fault==3) {
            CHECK_EQ(result,SaveOutcome::BackupRetained);CHECK(f.save.promoted());
            CHECK(DeviceSd::files[canonical].bytes==f.expected);CHECK(DeviceSd::files[previous].bytes==f.old);
        } else {
            CHECK_EQ(result,fault==4 || fault==5?SaveOutcome::CleanupRequired:SaveOutcome::PromotionFailed);
            if(fault!=6)CHECK(DeviceSd::files[canonical].bytes==f.old);
            else CHECK(!SD.sdfs.exists(canonical));
        }
        const bool pending=fault>=2 && fault<=5;
        if(fault==4)CHECK(DeviceSd::files[candidate].bytes.empty());
        if(fault==5)CHECK(DeviceSd::files[candidate].bytes==f.expected);
        CHECK_EQ(!f.save.CanDestroy(),pending);
        if(pending)CHECK_EQ(f.Run(),SaveOutcome::Busy);
        ResetFaults();CHECK(f.save.Cleanup(true));CHECK(f.save.CanDestroy());
        CHECK(!SD.sdfs.exists(previous));CHECK(!SD.sdfs.exists(candidate));
        if(fault!=6)CHECK(DeviceSd::files[canonical].bytes==(fault==3?f.expected:f.old));
        f.SourceIntact();
        std::cout<<"promotion-fault="<<fault<<" outcome="<<int(result)<<" retained="<<pending<<" cleanup=ok\n";
    }
}
TEST_CASE(FsSaveInvalidCanonicalAndChangedBackingBlockPromotion) {
    Fixture f(true);DeviceSd::files[canonical].bytes[0]^=1;const auto bad=DeviceSd::files[canonical].bytes;
    CHECK_EQ(f.Run(),SaveOutcome::Failed);CHECK(DeviceSd::files[canonical].bytes==bad);CHECK(!SD.sdfs.exists(candidate));
    DeviceSd::files[canonical].bytes=f.old;
    // Deliberate violation of registry immutability: Rewrite must detect it.
    for(auto& item:DeviceSd::files)if(item.first.find(".btt-prototype")!=std::string::npos)item.second.bytes.back()^=1;
    CHECK_EQ(f.Run(),SaveOutcome::Failed);CHECK_EQ(f.save.detail(),Result::Backing);
    CHECK(DeviceSd::files[canonical].bytes==f.old);CHECK(f.source.accepted);CHECK_EQ(f.owner.use_count(),2);
}
TEST_CASE(FsSaveVerifiedCandidateCloseAndRecoveryRetryRemainChecked) {
    Fixture f(true);
    DeviceSd::read_open_hook=[](const std::string& p){if(p==candidate)DeviceSd::fail_close_path=candidate;};
    CHECK_EQ(f.Run(),SaveOutcome::CleanupRequired);CHECK(!f.save.CanDestroy());
    CHECK(DeviceSd::files[canonical].bytes==f.old);CHECK(DeviceSd::files[candidate].bytes==f.expected);
    CHECK_EQ(DeviceSd::files[candidate].opens,1u);CHECK(!f.save.Cleanup(true));
    ResetFaults();CHECK(f.save.Cleanup(true));f.SourceIntact();
    DeviceSd::fail_rename=2;DeviceSd::fail_rename_second=3;
    CHECK_EQ(f.Run(),SaveOutcome::RollbackFailed);
    DeviceSd::fail_rename=4;CHECK(!f.save.Cleanup(true));
    CHECK(DeviceSd::files[previous].bytes==f.old);CHECK(DeviceSd::files[candidate].bytes==f.expected);
    CHECK(!SD.sdfs.exists(canonical));ResetFaults();CHECK(f.save.Cleanup(true));
    CHECK(DeviceSd::files[canonical].bytes==f.old);f.SourceIntact();
}
TEST_CASE(FsSaveAllocationAndOwnershipLifetimeEvidence) {
    Fixture f(true);
    const auto before=f.source.backing.use_count();
    for(unsigned i=0;i<16;++i) {
        CHECK_EQ(f.Run(),SaveOutcome::Saved);CHECK_EQ(f.source.backing.use_count(),before);
        CHECK(f.save.CanDestroy());CHECK(!f.scratch.backing);
    }
    CHECK_EQ(adapter_allocations,0u);CHECK(fake_allocations>0);
    std::cout<<"allocation-count: adapter="<<adapter_allocations<<" fake-STL="<<fake_allocations
        <<" source-refs="<<before<<" after="<<f.source.backing.use_count()<<" repeated=16\n";
}
}
