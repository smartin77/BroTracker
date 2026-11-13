#include "test_framework.h"
#include "bringup/working_session.h"
#include "bringup/wav_directory.h"
#include "bringup/instrument_commit.h"
#include "bringup/session_save_schedule.h"
#include "bringup/pattern_directory.h"
#include "firmware/teensy/BroTracker/sd_pattern_storage.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_preview.h"
#include "bringup/pattern_storage_protocol.h"
#include <array>
using namespace BroTracker;
TEST_CASE(ProductionStorageDispatchAcceptsSnapshotCoordinatesIncludingZeroRow) {
    const char* valid[]={"BTSTORE1 SAVE 2 0 0 0 0","BTSTORE1 SAVE 2 0 7 1 1","BTSTORE1 SAVE 2 19 6 1 0","BTSTORE1 SAVE 2 63 7 1 1"};
    const std::uint32_t expected[][4]={{0,0,0,0},{0,7,1,1},{19,6,1,0},{63,7,1,1}};
    for(unsigned n=0;n<4;++n) {
        std::uint32_t view[4]={99,99,99,99};
        CHECK_EQ(ParseStorageOperation(valid[n]),StorageOperation::Save);
        CHECK(ParseSessionSnapshotSave(valid[n],std::strlen(valid[n]),view));
        for(unsigned i=0;i<4;++i)CHECK_EQ(view[i],expected[n][i]);
    }
    CHECK_EQ(ParseStorageOperation("BTSTORE1 SAVE 1"),StorageOperation::Save);
    std::uint32_t legacy[4]={99,99,99,99};
    CHECK(!ParseSessionSnapshotSave("BTSTORE1 SAVE 1",std::strlen("BTSTORE1 SAVE 1"),legacy));CHECK_EQ(legacy[0],99u);
    for(const char* bad:{"BTSTORE1 SAVE 2","BTSTORE1 SAVE 2 0 0 0","BTSTORE1 SAVE 2 0 0 0 0 0",
        "BTSTORE1 SAVE 2 64 0 0 0","BTSTORE1 SAVE 2 0 8 0 0","BTSTORE1 SAVE 2 0 0 2 0","BTSTORE1 SAVE 2 0 0 0 2",
        "BTSTORE1 SAVE 2 -1 0 0 0","BTSTORE1 SAVE 2 +1 0 0 0","BTSTORE1 SAVE 2 4294967296 0 0 0",
        "BTSTORE1 SAVE 2 0  0 0 0","BTSTORE1 SAVE 2 0 0 0 0 ","BTSTORE1 SAVE 2 0 0 0 0\r",
        "BTSTORE1 SAVE 3 0 0 0 0","BTSTORE1 SAVE 0 0 0 0 0","BTSTORE1 SAVE 02 0 0 0 0",
        "BTSTORE1 SAVE 0002 0 0 0 0","BTSTORE1 SAVE 1 0 0 0 0","BTSTORE2 SAVE 2 0 0 0 0"}) {
        std::uint32_t view[4]={99,99,99,99};
        CHECK_EQ(ParseStorageOperation(bad),StorageOperation::Invalid);
        CHECK(!ParseSessionSnapshotSave(bad,std::strlen(bad),view));
        for(const auto value:view)CHECK_EQ(value,99u);
    }
    // Request IDs still require nonzero; the fix must not weaken the shared parser.
    std::uint32_t id=99;CHECK(!SnapshotFields("BTPATTERN1 GET 2 0",std::strlen("BTPATTERN1 GET 2 0"),"BTPATTERN1 GET 2 ",&id,1));CHECK_EQ(id,99u);
}
namespace {
void Same(const RealtimePattern& a,const RealtimePattern& b) {
    CHECK_EQ(a.active_rows,b.active_rows);CHECK_EQ(a.active_channels,b.active_channels);
    for(unsigned r=0;r<64;++r)for(unsigned c=0;c<8;++c){CHECK_EQ(a.cells[r][c].note,b.cells[r][c].note);CHECK_EQ(a.cells[r][c].instrument,b.cells[r][c].instrument);CHECK_EQ(a.note_off_timing[r][c],b.note_off_timing[r][c]);}
}
RealtimePattern Varied() {
    RealtimePattern p; p.active_rows=64;p.active_channels=8;
    for(unsigned r=0;r<64;++r)for(unsigned c=0;c<8;++c){p.cells[r][c]={static_cast<Note>(24+(r*8+c)%104),static_cast<std::uint8_t>(r*8+c)};p.note_off_timing[r][c]=((r+c)%2)?NoteOffTiming::Arrival:NoteOffTiming::EndOfPosition;}
    p.cells[1][2]={NOTE_OFF,255};p.cells[2][2]={NOTE_OFF,1};p.cells[3][0]={NOTE_EMPTY,255};return p;
}
void Recrc(std::array<std::uint8_t,kPatternFileCapacity>& b,std::size_t n){PatternFilePut32(b.data()+20,PatternFileCrc(b.data(),n));}
void ExclusiveIo(){CHECK(!SdAccess::AcquireRead());CHECK(!SdAccess::AcquireWrite());}
void Released(){CHECK(SdAccess::AcquireWrite());SdAccess::ReleaseWrite();}
}
TEST_CASE(PatternFileExactRoundTripKindsAndAllActiveMetadata) {
    const auto source=Varied();std::array<std::uint8_t,kPatternFileCapacity> b{};std::size_t n=0;
    for(auto kind:{PatternFileKind::Session,PatternFileKind::Development}){
        CHECK_EQ(EncodePatternFile(kind,source,15500,b.data(),b.size(),n),PatternFileResult::Ok);CHECK_EQ(n,1560u);
        CHECK_EQ(b[4],1);CHECK_EQ(b[6],24);CHECK_EQ(PatternFileU32(b.data()+12),15500u);
        RealtimePattern target;std::uint32_t tempo=12500;
        CHECK_EQ(DecodePatternFile(kind,b.data(),n,target,tempo),PatternFileResult::Ok);Same(source,target);CHECK_EQ(tempo,15500u);
    }
    // Known CRC-32/ISO-HDLC check vector (no excluded field in a nine-byte input).
    CHECK_EQ(PatternFileCrc(reinterpret_cast<const std::uint8_t*>("123456789"),9),0xcbf43926u);
}
TEST_CASE(PatternFileRejectsEveryTruncationCorruptionAndSemanticBoundsTransactionally) {
    auto source=Varied();std::array<std::uint8_t,kPatternFileCapacity> good{};std::size_t n=0;
    CHECK_EQ(EncodePatternFile(PatternFileKind::Session,source,15501,good.data(),good.size(),n),PatternFileResult::Ok);
    auto unchanged=MakeDevelopmentPattern();std::uint32_t tempo=12753;auto target=unchanged;
    for(std::size_t size=0;size<n;++size){CHECK(DecodePatternFile(PatternFileKind::Session,good.data(),size,target,tempo)!=PatternFileResult::Ok);}
    for(std::size_t i=0;i<n;++i){auto b=good;b[i]^=0x40;CHECK(DecodePatternFile(PatternFileKind::Session,b.data(),n,target,tempo)!=PatternFileResult::Ok);}
    for(auto index:{4u,5u,6u,7u,8u,9u,10u,11u,24u,26u}){
        auto b=good;b[index]=index==8?65:index==9?9:index==24?23:static_cast<std::uint8_t>(3);Recrc(b,n);
        CHECK(DecodePatternFile(PatternFileKind::Session,b.data(),n,target,tempo)!=PatternFileResult::Ok);
    }
    for(auto value:{0u,3199u,30001u,UINT32_MAX}){auto b=good;PatternFilePut32(b.data()+12,value);Recrc(b,n);CHECK(DecodePatternFile(PatternFileKind::Session,b.data(),n,target,tempo)!=PatternFileResult::Ok);}
    CHECK_EQ(DecodePatternFile(PatternFileKind::Development,good.data(),n,target,tempo),PatternFileResult::Unsupported);
    CHECK_EQ(DecodePatternFile(PatternFileKind::Session,good.data(),n+1,target,tempo),PatternFileResult::Invalid);
    CHECK_EQ(DecodePatternFile(PatternFileKind::Session,nullptr,n,target,tempo),PatternFileResult::Invalid);
    Same(target,unchanged);CHECK_EQ(tempo,12753u);
}
TEST_CASE(DevelopmentFilesExplicitNoOverwriteMissingAndExactSeedContents) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    RealtimePattern scratch,target=Varied();const auto initial=target;std::uint32_t tempo=12500;
    CHECK_EQ(store.Load(backend,kDevelopmentPattern1Path,PatternFileKind::Development,target,tempo),PatternFileResult::Missing);Same(target,initial);CHECK_EQ(tempo,12500u);
    for(bool midi:{false,true}){
        CHECK_EQ(store.CreateDevelopment(backend,midi?3:1,scratch),PatternFileResult::Ok);
        const auto path=midi?kDevelopmentPattern3Path:kDevelopmentPattern1Path;const auto original=FakePatternSd::files[path].bytes;
        CHECK_EQ(store.CreateDevelopment(backend,midi?3:1,scratch),PatternFileResult::Exists);CHECK(FakePatternSd::files[path].bytes==original);
        CHECK_EQ(store.Load(backend,path,PatternFileKind::Development,target,tempo),PatternFileResult::Ok);Same(target,scratch);CHECK_EQ(tempo,15500u);
        auto theme=MakeDevelopmentPattern(32);for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c){
            if(c!=4 || !midi){CHECK_EQ(target.cells[r][c].note,theme.cells[r][c].note);CHECK_EQ(target.cells[r][c].instrument,theme.cells[r][c].instrument);}
        }
        if(midi){
            constexpr unsigned rows[]={0,6,8,12,16,22,24,28},notes[]={36,43,48,43,41,48,41,43},offs[]={3,7,11,15,19,23,27,31};
            for(unsigned r=0;r<32;++r){unsigned note=NOTE_EMPTY,instrument=255;
                for(unsigned i=0;i<8;++i){if(r==rows[i]){note=notes[i];instrument=1;}if(r==offs[i])note=NOTE_OFF;}
                CHECK_EQ(target.cells[r][4].note,note);CHECK_EQ(target.cells[r][4].instrument,instrument);CHECK_EQ(target.note_off_timing[r][4],NoteOffTiming::Arrival);
            }
        }
    }
    CHECK(!FakePatternSd::files.count("/BroTracker/development/pattern2.btdev"));Released();
}
TEST_CASE(SessionFailuresPreserveLiveDestinationAndPriorFileAndCoordinateSd) {
    for(unsigned fault=0;fault<10;++fault){
        FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
        const auto old=MakeDevelopmentPattern();const auto next=Varied();
        CHECK_EQ(store.SaveSession(backend,old,15500),PatternFileResult::Ok);const auto previous=FakePatternSd::files[kSessionPath].bytes;
        FakePatternSd::rename_calls=0;
        if(fault==0)FakePatternSd::short_write=true;
        if(fault==1)FakePatternSd::short_read=true;
        if(fault==2)FakePatternSd::fail_sync=true;
        if(fault==3)FakePatternSd::corrupt_read=true;
        if(fault==4)FakePatternSd::fail_rename_mask=1u<<1;
        if(fault==5)FakePatternSd::fail_rename_mask=1u<<2;
        if(fault==6)FakePatternSd::fail_rename_mask=(1u<<2)|(1u<<3);
        if(fault==7)FakePatternSd::fail_remove=true;
        if(fault==8)FakePatternSd::files[kSessionCandidatePath]={false,{1,2,3}};
        if(fault==9)FakePatternSd::fail_open=true;
        const auto result=store.SaveSession(backend,next,16000);
        if(fault==7){CHECK_EQ(result,PatternFileResult::BackupRetained);CHECK(FakePatternSd::files[kSessionPreviousPath].bytes==previous);}
        else if(fault==6){CHECK_EQ(result,PatternFileResult::RecoveryRequired);CHECK(FakePatternSd::files[kSessionPreviousPath].bytes==previous);}
        else {CHECK(result!=PatternFileResult::Ok);CHECK(FakePatternSd::files[kSessionPath].bytes==previous);}
        Released();
    }
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;auto p=Varied();
    {SdReadScope reader;CHECK(reader);CHECK_EQ(store.SaveSession(backend,p,15500),PatternFileResult::Busy);CHECK(FakePatternSd::files.empty());}
    FakePatternSd::on_io=ExclusiveIo;CHECK_EQ(store.SaveSession(backend,p,15500),PatternFileResult::Ok);FakePatternSd::on_io=nullptr;
    CHECK(SdAccess::AcquireWrite());std::uint32_t tempo=12500;CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,p,tempo),PatternFileResult::Busy);SdAccess::ReleaseWrite();
    const auto before=p;FakePatternSd::files[kSessionPath].bytes[30]^=1;
    CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,p,tempo),PatternFileResult::Invalid);Same(p,before);CHECK_EQ(tempo,12500u);
    FakePatternSd::files[kSessionPath].bytes.resize(kPatternFileCapacity+1);CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,p,tempo),PatternFileResult::Invalid);Same(p,before);Released();
}
TEST_CASE(SessionPublishedBaselineRestoreControlReadbackMuteAndBusyBoundaries) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    auto published=MakeDevelopmentPattern();PatternPreviewReadback working;working.Published(published,0);
    const auto bindings=SelectDevelopmentFixture({}).bindings;Control control;CHECK(!control.StorageReady());
    CHECK_EQ(control.InitializeAudio(15500,44100,published,bindings),PatternPlayerStatus::Success);CHECK(control.StorageReady());
    std::int16_t audio[128]{};PatternAppliedRequest ack;
    CHECK_EQ(control.SubmitMute(4,1),PatternRequestStatus::Accepted);CHECK(!control.StorageReady());control.AudioBlock(audio,128,true);CHECK(control.StorageReady());
    // An unpublished working preview differs in notes and live tempo from baseline.
    auto draft=published;draft.cells[0][0].note=72;PatternPublication preview;
    CHECK(StageStoredPattern(preview,draft,15500,0));CHECK_EQ(control.SubmitPreview(*preview.Frozen(),preview.Identity()),PatternRequestStatus::Accepted);
    control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){if(ack.request==PatternRequest::Preview)CHECK(working.Accepted(preview,ack.publication_id,true));else if(ack.request==PatternRequest::PreviewApplied)CHECK(working.Applied(ack.publication_id));}
    CHECK(control.StorageReady());CHECK_EQ(working.working.cells[0][0].note,72);CHECK_EQ(published.cells[0][0].note,60);
    CHECK_EQ(control.SubmitTempo(17000,1),PatternRequestStatus::Accepted);CHECK(!control.StorageReady());control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){}CHECK_EQ(control.Snapshot().tempo,17000u);
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    CHECK_EQ(store.SaveSession(backend,published,15500),PatternFileResult::Ok);
    RealtimePattern saved;std::uint32_t tempo=0;CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,saved,tempo),PatternFileResult::Ok);Same(saved,published);CHECK_EQ(tempo,15500u);
    saved.cells[1][0]={NOTE_OFF,255};saved.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;saved.cells[2][0]={NOTE_OFF,255};
    CHECK_EQ(store.SaveSession(backend,saved,15501),PatternFileResult::Ok);CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,saved,tempo),PatternFileResult::Ok);
    PatternPublication restore;CHECK(StageStoredPattern(restore,saved,tempo,0));CHECK_EQ(control.SubmitPattern(*restore.Frozen(),restore.Identity(),tempo),PatternRequestStatus::Accepted);CHECK(!control.StorageReady());
    control.AudioBlock(audio,128,true);CHECK(!control.StorageReady());CHECK(control.TakeApplied(ack));CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK(restore.Complete(ack.publication_id,true,published));working.Published(published,ack.publication_id);
    CHECK(control.StorageReady());CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped);CHECK_EQ(control.Snapshot().tempo,15501u);CHECK_EQ(control.Snapshot().published_tempo,15501u);CHECK_EQ(control.Snapshot().mute_mask,4u);Same(published,saved);Same(working.working,saved);
    PatternSnapshotSender sender(published,15501);PatternSnapshotAssembler host;CHECK(sender.Begin(1,0,2));CHECK(host.Request(1,0,2));
    while(sender.HasData()){std::size_t n;const auto* data=sender.Data(n);(void)host.Accept(data,n-1,1);sender.Consume(n);}CHECK(host.Snapshot());Same(host.Snapshot()->pattern,saved);CHECK_EQ(host.Snapshot()->tempo_hundredths,15501u);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);CHECK(!control.StorageReady());control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){}CHECK(!control.StorageReady());
    CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){}CHECK(!control.StorageReady());
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);control.AudioBlock(audio,128,true);CHECK(!control.StorageReady());while(control.TakeApplied(ack)){}CHECK(control.StorageReady());CHECK_EQ(control.Snapshot().mute_mask,4u);
}
TEST_CASE(StorageProtocolExplicitVersionsAndNoInMemoryRestoreOverload) {
    CHECK_EQ(ParseStorageOperation("BTSTORE1 SAVE 1"),StorageOperation::Save);CHECK_EQ(ParseStorageOperation("BTSTORE1 RESTORE 1"),StorageOperation::Restore);
    CHECK_EQ(ParseStorageOperation("BTSTORE1 SEED 1 1"),StorageOperation::Seed1);CHECK_EQ(ParseStorageOperation("BTSTORE1 LOAD 1 3"),StorageOperation::Load);
    for(const auto* line:{"BTSTORE1 SAVE 2","BTSTORE1 RESTORE 1 ","BTTEST1 RESTORE","BTSTORE1 SEED 1 pattern2","BTSTORE1 LOAD 1 ../x"})CHECK_EQ(ParseStorageOperation(line),StorageOperation::Invalid);
}
TEST_CASE(SessionIncompatibleBindingsRejectWithoutPartialBaselineOrTempo) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    auto baseline=MakeDevelopmentPattern();const auto original=baseline;Control control;
    CHECK_EQ(control.InitializeAudio(15500,44100,baseline,SelectDevelopmentFixture({}).bindings),PatternPlayerStatus::Success);
    auto incompatible=baseline;incompatible.cells[0][0]={60,99};
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    CHECK_EQ(store.SaveSession(backend,incompatible,16001),PatternFileResult::Ok);
    RealtimePattern loaded;std::uint32_t tempo=0;CHECK_EQ(store.Load(backend,kSessionPath,PatternFileKind::Session,loaded,tempo),PatternFileResult::Ok);
    PatternPublication transfer;CHECK(StageStoredPattern(transfer,loaded,tempo,0));
    CHECK_EQ(control.SubmitPattern(*transfer.Frozen(),transfer.Identity(),tempo),PatternRequestStatus::Accepted);
    std::int16_t audio[128];control.AudioBlock(audio,128,true);PatternAppliedRequest ack;CHECK(control.TakeApplied(ack));CHECK(ack.result!=PatternPlayerStatus::Success);
    CHECK(transfer.Complete(ack.publication_id,false,baseline));Same(baseline,original);
    CHECK_EQ(control.Snapshot().tempo,15500u);CHECK_EQ(control.Snapshot().published_tempo,15500u);CHECK_EQ(control.Snapshot().published_id,0u);CHECK(control.StorageReady());
}
TEST_CASE(DevelopmentPartialWriteCannotBecomeCanonicalAndStaleCandidateBlocks) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;RealtimePattern scratch;
    FakePatternSd::short_write=true;CHECK_EQ(store.CreateDevelopment(backend,1,scratch),PatternFileResult::Io);CHECK(!FakePatternSd::files.count(kDevelopmentPattern1Path));
    FakePatternSd::short_write=false;FakePatternSd::files["/BroTracker/development/patern1.btdev.candidate"]={false,{1,2}};
    CHECK_EQ(store.CreateDevelopment(backend,1,scratch),PatternFileResult::RecoveryRequired);CHECK(!FakePatternSd::files.count(kDevelopmentPattern1Path));Released();
}
TEST_CASE(SessionCanonicalDirectoryIsNotRenamedOrReplaced) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    FakePatternSd::files[kSessionPath].directory=true;const auto p=MakeDevelopmentPattern();
    CHECK_EQ(store.SaveSession(backend,p,15500),PatternFileResult::Io);
    CHECK(FakePatternSd::files[kSessionPath].directory);CHECK_EQ(FakePatternSd::rename_calls,0u);CHECK(!FakePatternSd::files.count(kSessionCandidatePath));Released();
}
TEST_CASE(ExplicitMidiReplacementPreservesOtherColumnsAndSeedNoOverwrite) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    auto old=MakeDevelopmentPattern(32);
    for(unsigned r=0;r<32;++r){old.cells[r][4]={90,8};old.note_off_timing[r][4]=NoteOffTiming::EndOfPosition;}
    old.cells[5][7]={77,2};old.note_off_timing[5][7]=NoteOffTiming::EndOfPosition;
    std::array<std::uint8_t,kPatternFileCapacity> bytes{};std::size_t n=0;
    CHECK_EQ(EncodePatternFile(PatternFileKind::Development,old,15500,bytes.data(),bytes.size(),n),PatternFileResult::Ok);
    FakePatternSd::files[kDevelopmentPattern3Path]={false,{bytes.begin(),bytes.begin()+n}};
    FakePatternSd::files[kDevelopmentPattern1Path]={false,{1,2,3}};FakePatternSd::files[kSessionPath]={false,{4,5,6}};
    const auto theme=FakePatternSd::files[kDevelopmentPattern1Path].bytes,session=FakePatternSd::files[kSessionPath].bytes;
    RealtimePattern scratch,loaded;std::uint32_t tempo=0;
    CHECK_EQ(store.ReplaceDevelopmentPattern3(backend,scratch),PatternFileResult::Ok);
    CHECK_EQ(store.Load(backend,kDevelopmentPattern3Path,PatternFileKind::Development,loaded,tempo),PatternFileResult::Ok);CHECK_EQ(tempo,15500u);
    auto expected=old;SetDevelopmentMidiBassPart(expected);Same(loaded,expected);
    for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c)if(c!=4){CHECK_EQ(loaded.cells[r][c].note,old.cells[r][c].note);CHECK_EQ(loaded.cells[r][c].instrument,old.cells[r][c].instrument);CHECK_EQ(loaded.note_off_timing[r][c],old.note_off_timing[r][c]);}
    CHECK(FakePatternSd::files[kDevelopmentPattern1Path].bytes==theme);CHECK(FakePatternSd::files[kSessionPath].bytes==session);
    const auto replaced=FakePatternSd::files[kDevelopmentPattern3Path].bytes;
    CHECK_EQ(store.CreateDevelopment(backend,3,scratch),PatternFileResult::Exists);CHECK(FakePatternSd::files[kDevelopmentPattern3Path].bytes==replaced);
    CHECK_EQ(ParseStorageOperation("BTSTORE1 REPLACE 1 3"),StorageOperation::Replace3);
    for(const auto* bad:{"BTSTORE1 REPLACE 1 theme","BTSTORE1 REPLACE 1 3 extra","BTSTORE1 REPLACE 2 midi"})CHECK_EQ(ParseStorageOperation(bad),StorageOperation::Invalid);Released();
}
TEST_CASE(ExplicitMidiReplacementMissingInvalidBusyAndFailedPromotion) {
    for(unsigned mode=0;mode<8;++mode){
        FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;RealtimePattern scratch;
        if(mode==0){CHECK_EQ(store.ReplaceDevelopmentPattern3(backend,scratch),PatternFileResult::Missing);CHECK(FakePatternSd::files.empty());continue;}
        auto old=MakeDevelopmentPattern(mode==1?16:32);old.cells[2][4]={64,1};
        std::array<std::uint8_t,kPatternFileCapacity> b{};std::size_t n=0;CHECK_EQ(EncodePatternFile(PatternFileKind::Development,old,mode==2?15501:15500,b.data(),b.size(),n),PatternFileResult::Ok);
        FakePatternSd::files[kDevelopmentPattern3Path]={false,{b.begin(),b.begin()+n}};const auto original=FakePatternSd::files[kDevelopmentPattern3Path].bytes;
        if(mode==3)FakePatternSd::short_write=true;if(mode==4)FakePatternSd::fail_sync=true;
        if(mode==5)FakePatternSd::fail_rename_mask=1u<<2;if(mode==6)FakePatternSd::fail_rename_mask=(1u<<2)|(1u<<3);
        if(mode==7)CHECK(SdAccess::AcquireRead());
        const auto result=store.ReplaceDevelopmentPattern3(backend,scratch);CHECK(result!=PatternFileResult::Ok);
        if(mode==7){CHECK_EQ(result,PatternFileResult::Busy);SdAccess::ReleaseRead();}
        if(mode==6){CHECK_EQ(result,PatternFileResult::RecoveryRequired);CHECK(FakePatternSd::files["/BroTracker/development/patern3.btdev.previous"].bytes==original);}
        else CHECK(FakePatternSd::files[kDevelopmentPattern3Path].bytes==original);
        Released();
    }
}
TEST_CASE(NumberedDevelopmentVariantsExactCellsMetadataAndTempo) {
    for(unsigned number=0;number<=4;++number) {
        RealtimePattern p;MakeStoredDevelopmentPattern(p,number);CHECK_EQ(p.active_rows,32);CHECK_EQ(p.active_channels,8);CHECK_EQ(DevelopmentPatternTempo(number),number?15500u:12500u);
        const auto theme=MakeDevelopmentPattern(32);
        constexpr unsigned rows[]={0,6,8,12,16,22,24,28},notes[]={36,43,48,43,41,48,41,43},offs[]={3,7,11,15,19,23,27,31};
        for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c) {
            unsigned note=NOTE_EMPTY,instrument=255;
            if(number && c<2){note=theme.cells[r][c].note;instrument=theme.cells[r][c].instrument;}
            if((number==2 || number==4) && c==2 && (r==0 || r==16 || r==28)){note=60;instrument=r?255:0;}
            if((number==2 || number==4) && c==3 && (r==0 || r==12 || r==28)){note=60;instrument=r?255:3;}
            if((number==3 || number==4) && c==4)for(unsigned i=0;i<8;++i){if(r==rows[i]){note=notes[i];instrument=1;}if(number==3 && r==offs[i])note=NOTE_OFF;}
            CHECK_EQ(p.cells[r][c].note,note);CHECK_EQ(p.cells[r][c].instrument,instrument);CHECK_EQ(p.note_off_timing[r][c],NoteOffTiming::Arrival);
        }
    }
}
TEST_CASE(NumberedMigrationVerifiesExactBytesBeforeRemovingSource) {
    for(unsigned scenario=0;scenario<7;++scenario){
        FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
        const char* source="/BroTracker/development/midi.btdev";const char* dest=DevelopmentPatternPath(3);
        RealtimePattern p;MakeStoredDevelopmentPattern(p,3);p.note_off_timing[5][5]=NoteOffTiming::EndOfPosition;
        std::array<std::uint8_t,kPatternFileCapacity> bytes{};std::size_t n=0;CHECK_EQ(EncodePatternFile(PatternFileKind::Development,p,15500,bytes.data(),bytes.size(),n),PatternFileResult::Ok);
        const std::vector<std::uint8_t> original(bytes.begin(),bytes.begin()+n);
        if(scenario!=1)FakePatternSd::files[source]={false,original};
        if(scenario==2)FakePatternSd::files[dest]={false,{1,2,3}};
        if(scenario==3)FakePatternSd::short_write=true;
        if(scenario==4)FakePatternSd::fail_rename_mask=1u<<1;
        if(scenario==5)FakePatternSd::fail_remove=true;
        if(scenario==6)FakePatternSd::files[dest]={false,original};
        const auto result=store.MigrateDevelopment(backend,3);
        if(scenario==0 || scenario==6){CHECK_EQ(result,PatternFileResult::Ok);CHECK(!FakePatternSd::files.count(source));CHECK(FakePatternSd::files[dest].bytes==original);}
        else if(scenario==1)CHECK_EQ(result,PatternFileResult::Missing);
        else {CHECK(result!=PatternFileResult::Ok);CHECK(FakePatternSd::files[source].bytes==original);if(scenario==2)CHECK(FakePatternSd::files[dest].bytes==std::vector<std::uint8_t>({1,2,3}));}
        if(scenario==5){FakePatternSd::fail_remove=false;CHECK_EQ(store.MigrateDevelopment(backend,3),PatternFileResult::Ok);CHECK(!FakePatternSd::files.count(source));}
        Released();
    }
}
TEST_CASE(CleanBaselineUsesPublicationAndPreservesMuteWithoutStorage) {
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    auto old=MakeDevelopmentPattern();Control control;CHECK_EQ(control.InitializeAudio(15500,44100,old,SelectDevelopmentFixture({}).bindings),PatternPlayerStatus::Success);
    CHECK_EQ(control.SubmitMute(5,1),PatternRequestStatus::Accepted);std::int16_t out[128];control.AudioBlock(out,128,true);
    RealtimePattern clean;MakeStoredDevelopmentPattern(clean,0);PatternPublication transfer;
    CHECK(StageStoredPattern(transfer,clean,12500,0));CHECK_EQ(control.SubmitPattern(*transfer.Frozen(),transfer.Identity(),12500),PatternRequestStatus::Accepted);
    control.AudioBlock(out,128,true);PatternAppliedRequest ack;CHECK(control.TakeApplied(ack));CHECK_EQ(ack.result,PatternPlayerStatus::Success);
    CHECK(transfer.Complete(ack.publication_id,true,old));Same(old,clean);CHECK_EQ(control.Snapshot().tempo,12500u);CHECK_EQ(control.Snapshot().published_tempo,12500u);CHECK_EQ(control.Snapshot().mute_mask,5u);CHECK_EQ(control.Snapshot().transport,PatternTransportState::Stopped);
}
TEST_CASE(DevelopmentStemLoadingUsesFileContentsAndRejectsPathsBeforeIo) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    RealtimePattern original;original.active_rows=16;original.active_channels=3;original.cells[1][2]={NOTE_OFF,255};original.note_off_timing[1][2]=NoteOffTiming::EndOfPosition;
    std::array<std::uint8_t,kPatternFileCapacity> bytes{};std::size_t size=0;CHECK_EQ(EncodePatternFile(PatternFileKind::Development,original,12753,bytes.data(),bytes.size(),size),PatternFileResult::Ok);
    FakePatternSd::files["/BroTracker/development/ambient_Take.btdev"]={false,{bytes.begin(),bytes.begin()+size}};
    FakePatternSd::files["/BroTracker/development/none.btdev"]={false,{bytes.begin(),bytes.begin()+size}};
    RealtimePattern dest;std::uint32_t tempo=0;CHECK_EQ(store.LoadDevelopment(backend,"ambient_Take",dest,tempo),PatternFileResult::Ok);Same(dest,original);CHECK_EQ(tempo,12753u);
    CHECK_EQ(store.LoadDevelopment(backend,"ambient_Take.btdev",dest,tempo),PatternFileResult::Ok);Same(dest,original);
    CHECK_EQ(store.LoadDevelopment(backend,"none.btdev",dest,tempo),PatternFileResult::Ok);Same(dest,original);CHECK_EQ(tempo,12753u);
    const auto unchanged=dest;
    for(const auto& name:std::vector<std::string>{"","../x","x/y","x\\y","x.bad","x y",std::string(25,'a'),"none"}) {
        CHECK_EQ(store.LoadDevelopment(backend,name,dest,tempo),PatternFileResult::Invalid);Same(dest,unchanged);CHECK_EQ(tempo,12753u);
        CHECK_EQ(ParseStorageOperation(("BTSTORE1 LOAD 1 "+name).c_str()),(name=="none")?StorageOperation::Load:StorageOperation::Invalid);
    }
    char path[kDevelopmentPathCapacity];CHECK(BuildDevelopmentPath(std::string(24,'x'),path,sizeof(path)));CHECK(!BuildDevelopmentPath(std::string(25,'x'),path,sizeof(path)));
    CHECK_EQ(std::string(path),"/BroTracker/development/"+std::string(24,'x')+".btdev");
    CHECK_EQ(store.LoadDevelopment(backend,"4",dest,tempo),PatternFileResult::Missing); // Literal4, no numbered alias.
}

TEST_CASE(TemporaryBtmPreservesWorkingIntentAndLeavesRetiredDraftUntouched) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;
    WorkingSessionStore<SdPatternFiles> store;PatternFileStore<SdPatternFiles> tunes;
    WorkingSession s;s.published=MakeDevelopmentPattern();s.working=s.published;s.working.cells[0][0].note=61;
    s.published_tempo=15500;s.working_tempo=12753;
    CHECK_EQ(tunes.SaveDraft(backend,s.published,15500),PatternFileResult::Ok);
    const auto tune=FakePatternSd::files["/BroTracker/tune.btdraft"].bytes;
    CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
    Same(s.working,restored.published);Same(s.working,restored.working);
    CHECK_EQ(restored.published_tempo,12753u);CHECK_EQ(restored.working_tempo,12753u);
    CHECK(FakePatternSd::files["/BroTracker/tune.btdraft"].bytes==tune);
    const auto good=FakePatternSd::files[kSessionPath].bytes;
    for(std::size_t n=0;n<good.size();++n) {
        auto bad=good;bad[n]^=64;
        auto target=restored;CHECK(DecodeWorkingSession(bad.data(),bad.size(),target)!=PatternFileResult::Ok);
        Same(target.working,restored.working);Same(target.published,restored.published);
    }
    for(std::size_t n=0;n<good.size();++n) {
        auto target=restored;CHECK(DecodeWorkingSession(good.data(),n,target)!=PatternFileResult::Ok);
        Same(target.working,restored.working);
    }
}
TEST_CASE(WorkingSessionInterruptedReplacementRetainsCommittedCopyAndReportsUncertainty) {
    for(unsigned fault=0;fault<9;++fault) {
        FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
        WorkingSession old;old.published=MakeDevelopmentPattern();old.working=old.published;
        CHECK_EQ(store.Save(backend,old),PatternFileResult::Ok);const auto committed=FakePatternSd::files[kSessionPath].bytes;
        auto next=old;next.working.cells[0][0].note=61;
        FakePatternSd::rename_calls=0;
        if(fault==0)FakePatternSd::short_write=true;
        if(fault==1)FakePatternSd::fail_sync=true;
        if(fault==2)FakePatternSd::corrupt_read=true;
        if(fault==3)FakePatternSd::fail_rename_mask=1u<<1;
        if(fault==4)FakePatternSd::fail_rename_mask=1u<<2;
        if(fault==5)FakePatternSd::fail_rename_mask=(1u<<2)|(1u<<3);
        if(fault==6)FakePatternSd::fail_remove=true;
        if(fault==7)FakePatternSd::files[kSessionCandidatePath]={false,{1,2,3}};
        if(fault==8)FakePatternSd::fail_open=true;
        const auto result=store.Save(backend,next);CHECK(result!=PatternFileResult::Ok);
        if(fault==5 || fault==6)CHECK(FakePatternSd::files[kSessionPreviousPath].bytes==committed);
        else CHECK(FakePatternSd::files[kSessionPath].bytes==committed);
    }
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession s;s.published=MakeDevelopmentPattern();s.working=s.published;s.working.cells[0][0].note=61;
    CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    FakePatternSd::files[kSessionPreviousPath]=FakePatternSd::files[kSessionPath];FakePatternSd::files.erase(kSessionPath);
    FakePatternSd::files[kSessionCandidatePath]={false,{1,2,3}};
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::BackupRetained);Same(s.working,restored.working);
    CHECK_EQ(store.Save(backend,s),PatternFileResult::RecoveryRequired); // Never erase interrupted evidence.
    FakePatternSd::files[kSessionPreviousPath].bytes[25]^=1;
    const auto before=restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::RecoveryRequired);Same(before.working,restored.working);
}
TEST_CASE(PauseStorageBarrierAllowsCoherentPendingPreviewButPlayingNeverWrites) {
    BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings> control;
    auto p=MakeDevelopmentPattern();const auto bindings=SelectDevelopmentFixture({}).bindings;
    CHECK_EQ(control.InitializeAudio(15500,44100,p,bindings),PatternPlayerStatus::Success);
    std::int16_t audio[128]{};PatternAppliedRequest ack;
    CHECK(control.StorageReady(true));CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK(!control.StorageReady(true));control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){}
    CHECK(!control.StorageReady(true));CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
    control.AudioBlock(audio,128,true);CHECK(!control.StorageReady(true));while(control.TakeApplied(ack)){}
    CHECK(control.StorageReady(true));CHECK(!control.StorageReady());
    p.cells[0][0].note=61;CHECK_EQ(control.SubmitPreview(p,1),PatternRequestStatus::Accepted);
    control.AudioBlock(audio,128,true);while(control.TakeApplied(ack)){}
    CHECK(control.StorageReady(true));CHECK(!control.StorageReady());
}

TEST_CASE(PowerRestartReconstructsOneWorkingPatternAndBpmButNeverStarts) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> files;
    WorkingSession saved;saved.published=MakeDevelopmentPattern();saved.working=saved.published;
    saved.working.cells[0][0].note=61;saved.published_tempo=15500;saved.working_tempo=12753;
    CHECK_EQ(files.Save(backend,saved),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(files.Load(backend,restored),PatternFileResult::Ok);
    BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings> rebooted;
    CHECK_EQ(rebooted.InitializeAudio(restored.working_tempo,44100,restored.working,SelectDevelopmentFixture({}).bindings,restored.published_tempo),PatternPlayerStatus::Success);
    CHECK_EQ(rebooted.Snapshot().transport,PatternTransportState::Stopped);CHECK(!rebooted.Snapshot().running);
    CHECK_EQ(rebooted.Snapshot().tempo,12753u);CHECK_EQ(rebooted.Snapshot().published_tempo,12753u);
    std::int16_t audio[128]{};rebooted.AudioBlock(audio,128,true);
    CHECK(!rebooted.Snapshot().running);for(auto sample:audio)CHECK_EQ(sample,0);
    Same(restored.working,saved.working);Same(restored.published,saved.working);
}

TEST_CASE(PatternDirectoryListsValidatedFixturesAndOtherFilesReadOnlyAcrossPages) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;RealtimePattern scratch;
    for(unsigned i=1;i<=4;++i)CHECK_EQ(store.CreateDevelopment(backend,i,scratch),PatternFileResult::Ok);
    const auto good=FakePatternSd::files[DevelopmentPatternPath(1)].bytes;
    for(unsigned i=0;i<12;++i)FakePatternSd::files["/BroTracker/development/extra"+std::to_string(i)+".btdev"].bytes=good;
    auto corrupt=good;corrupt.back()^=1;FakePatternSd::files["/BroTracker/development/bad.btdev"].bytes=corrupt;
    FakePatternSd::files["/BroTracker/development/skip.txt"].bytes=good;
    FakePatternSd::files["/BroTracker/development/folder.btdev"].directory=true;
    FakePatternSd::files["/BroTracker/development/abcdefghijklmnopqrstuvwxyz.btdev"].bytes=good;
    const auto size=FakePatternSd::files.size();std::vector<std::string> names;PatternDirectoryPage page;std::uint32_t offset=0;
    for(unsigned step=0;step<8;++step) {
        CHECK_EQ(ReadPatternDirectoryPage(backend,offset,page),PatternFileResult::Ok);
        for(unsigned i=0;i<page.count;++i)names.emplace_back(page.names[i]);
        if(page.done)break;CHECK(page.next>offset);offset=page.next;
    }
    CHECK(page.done);CHECK_EQ(names.size(),16u);CHECK_EQ(FakePatternSd::files.size(),size);
    for(unsigned i=1;i<=4;++i)CHECK(std::find(names.begin(),names.end(),"patern"+std::to_string(i)+".btdev")!=names.end());
    for(const auto& name:names) {
        RealtimePattern loaded;std::uint32_t tempo=0;CHECK_EQ(store.LoadDevelopment(backend,name,loaded,tempo),PatternFileResult::Ok);
    }
    FakePatternSd::fail_open=true;const auto previous=page;
    CHECK_EQ(ReadPatternDirectoryPage(backend,0,page),PatternFileResult::Io);CHECK_EQ(page.count,previous.count);
}

TEST_CASE(PatternSaveDevelopmentRoundTripAndFailedReplacementPreservesOriginal) {
    for(unsigned fault=0;fault<5;++fault) {
        FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
        auto original=MakeDevelopmentPattern();CHECK_EQ(store.SaveDevelopment(backend,"original.btdev",original,12753),PatternFileResult::Ok);
        const std::string path="/BroTracker/development/original.btdev";const auto bytes=FakePatternSd::files[path].bytes;FakePatternSd::rename_calls=0;
        auto edited=original;edited.cells[5][2]={75,7};edited.note_off_timing[6][2]=NoteOffTiming::EndOfPosition;
        if(fault==1)FakePatternSd::short_write=true;if(fault==2)FakePatternSd::fail_sync=true;
        if(fault==3)FakePatternSd::fail_rename_mask=1u<<2;if(fault==4)FakePatternSd::files[path+".candidate"]={false,{1,2,3}};
        const auto result=store.SaveDevelopment(backend,"original.btdev",edited,13337);
        if(fault) {CHECK(result!=PatternFileResult::Ok);CHECK(FakePatternSd::files[path].bytes==bytes);}
        else {CHECK_EQ(result,PatternFileResult::Ok);RealtimePattern loaded;std::uint32_t tempo=0;CHECK_EQ(store.LoadDevelopment(backend,"original.btdev",loaded,tempo),PatternFileResult::Ok);Same(edited,loaded);CHECK_EQ(tempo,13337u);}
        Released();
    }
    DevelopmentPattern name;CHECK(ParseStoragePatternSave("BTSTORE1 PATTERNSAVE 1 original.btdev",name));
    CHECK(!ParseStoragePatternSave("BTSTORE1 PATTERNSAVE 1 ../original.btdev",name));CHECK(!ParseStoragePatternSave("BTSTORE1 PATTERNSAVE 1 none",name));
}

TEST_CASE(AutomaticSessionIntentSurvivesDeferralAndFailedWriteAndInvalidFileIsPreserved) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession work;work.published=MakeDevelopmentPattern();work.working=work.published;work.working.cells[0][0]={75,7};
    SessionSaveSchedule schedule;schedule.Changed();CHECK(!schedule.Ready(false));CHECK(schedule.Pending());
    CHECK(schedule.Ready(true));FakePatternSd::short_write=true;const auto error=store.Save(backend,work);CHECK_EQ(error,PatternFileResult::Io);
    schedule.Completed(error);CHECK(schedule.Pending());CHECK(!schedule.Ready(true));
    FakePatternSd::short_write=false;schedule.Changed();CHECK(schedule.Ready(true));schedule.Completed(store.Save(backend,work));CHECK(!schedule.Pending());
    WorkingSession loaded;CHECK_EQ(store.Load(backend,loaded),PatternFileResult::Ok);CHECK_EQ(loaded.working.cells[0][0].note,75u);
    FakePatternSd::files[kSessionPath].bytes={1,2,3};const auto invalid=FakePatternSd::files[kSessionPath].bytes;
    CHECK(store.PreserveInvalid(backend));CHECK(FakePatternSd::files["/BroTracker/current.btsession.invalid"].bytes==invalid);
    CHECK_EQ(store.Save(backend,work),PatternFileResult::Ok);CHECK(!store.PreserveInvalid(backend));
    CHECK_EQ(ParseStorageOperation("BTSTORE1 DRAFTLOAD 1"),StorageOperation::Invalid);
    CHECK_EQ(ParseStorageOperation("BTSTORE1 DRAFTSAVE 1"),StorageOperation::Invalid);
}

TEST_CASE(SessionMetadataRoundtripLegacyAndInvalidCoordinatesPreserveMusic) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession s;s.published=MakeDevelopmentPattern();s.working=s.published;s.view={19,6,1,0};s.mute_mask=165;
    CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    WorkingSession loaded;CHECK_EQ(store.Load(backend,loaded),PatternFileResult::Ok);
    CHECK_EQ(loaded.view.row,19u);CHECK_EQ(loaded.view.channel,6u);CHECK_EQ(loaded.view.field,1u);CHECK_EQ(loaded.mute_mask,165u);
    const auto bytes=FakePatternSd::files[kSessionPath].bytes;
    for(bool header:{false,true}) {
        auto invalid=bytes;auto* m=invalid.data()+invalid.size()-8;m[0]=255;m[1]=255;m[2]=255;m[3]=header?1:255;
        PatternFilePut32(invalid.data()+20,PatternFileCrc(invalid.data(),invalid.size()));
        CHECK_EQ(DecodeWorkingSession(invalid.data(),invalid.size(),loaded),PatternFileResult::Ok);
        CHECK_EQ(loaded.view.row,header?0u:31u);CHECK_EQ(loaded.view.channel,7u);CHECK_EQ(loaded.view.field,0u);
        Same(s.working,loaded.working);
    }
    s.view={22,3,1,1};CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    CHECK_EQ(store.Load(backend,loaded),PatternFileResult::Ok);CHECK_EQ(loaded.view.row,0u);CHECK_EQ(loaded.view.header,1u);
}

TEST_CASE(TemporaryBtmDefinitionsRoundtripAndMalformedDefinitionIsTransactional) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession s;s.working=MakeDevelopmentPattern();s.working.cells[0][0]={64,5};s.working_tempo=13925;
    CHECK(s.definitions.Append(5,1,"Samples/missing.wav"));CHECK(s.definitions.Append(6,2,nullptr));
    CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    WorkingSession target;CHECK_EQ(store.Load(backend,target),PatternFileResult::Ok);
    CHECK_EQ(target.definitions.Count(),7u);CHECK_EQ(target.working_tempo,13925u);
    CHECK_EQ(target.working.cells[0][0].instrument,5u);
    CHECK(std::strcmp(target.definitions.At(5)->resource,"Samples/missing.wav")==0);
    auto bytes=FakePatternSd::files[kSessionPath].bytes;auto offset=24+PatternFileU32(bytes.data()+8);
    bytes[offset]=9;PatternFilePut32(bytes.data()+20,PatternFileCrc(bytes.data(),bytes.size()));
    CHECK_EQ(DecodeWorkingSession(bytes.data(),bytes.size(),target),PatternFileResult::Invalid);
    CHECK_EQ(target.definitions.Count(),7u);CHECK_EQ(target.working_tempo,13925u);CHECK_EQ(target.working.cells[0][0].instrument,5u);
}

TEST_CASE(TemporaryBtmMissingSampleKeepsSlotAndStopsVoiceWithoutFallback) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession s;s.definitions.Reset(false);
    CHECK(s.definitions.Append(0,3,nullptr));CHECK(s.definitions.Append(1,1,"Samples/missing.wav"));
    s.working={};s.working.active_rows=16;s.working.active_channels=1;s.working.cells[0][0]={64,1};s.working.cells[1][0]={65,2};
    s.working_tempo=14725;CHECK_EQ(store.Save(backend,s),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
    const auto bindings=RestoreInstrumentBindings(restored.definitions,SelectDevelopmentFixture({}).bindings);
    CHECK_EQ(bindings.count,1u);CHECK_EQ(bindings.bindings[0].type,InstrumentType::Synth);
    CHECK_EQ(restored.definitions.Count(),2u);CHECK_EQ(restored.working.cells[0][0].instrument,1u);
    CHECK_EQ(restored.working.cells[1][0].instrument,2u); // Sole trailing undefined slot survives.
    VariableRateBringUpPlayer player;
    CHECK_EQ(player.Configure(restored.working_tempo,44100,restored.working,bindings),PatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
    std::int16_t audio[128];
    for(unsigned block=0;block<64;++block) {
        CHECK_EQ(player.Render(audio,128),PatternPlayerStatus::Success);
        for(auto sample:audio)CHECK_EQ(sample,0); // Never falls back to defined Synth slot 01.
    }
}
TEST_CASE(TemporaryBtmPreparedAssetsFollowResourceNotSavedSlot) {
    InstrumentDefinitions definitions(false);CHECK(definitions.Append(0,1,"Samples/test3.wav"));
    CHECK(definitions.Append(1,2,nullptr));CHECK(definitions.Append(2,1,"Samples/test3.wav"));
    VariableRateSampleBindings assets;assets.count=1;assets.bindings[0].instrument=3;
    assets.bindings[0].reference_note=60;assets.bindings[0].sample_level_percent=80;
    const auto restored=RestoreInstrumentBindings(definitions,assets);
    CHECK_EQ(restored.count,3u);CHECK_EQ(restored.bindings[0].instrument,0u);CHECK_EQ(restored.bindings[2].instrument,2u);
    CHECK_EQ(restored.bindings[0].reference_note,60u);CHECK_EQ(restored.bindings[2].sample_level_percent,80u);
}
TEST_CASE(LegacyBtwsVersionsAndBtssUseDevelopmentDefinitionsAndMigrateWorkingIntent) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    auto pattern=MakeDevelopmentPattern();std::array<std::uint8_t,kPatternFileCapacity> record{};std::size_t size=0;
    CHECK_EQ(EncodePatternFile(PatternFileKind::Session,pattern,15500,record.data(),record.size(),size),PatternFileResult::Ok);
    for(unsigned version:{0u,1u,2u}) {
        std::vector<std::uint8_t> bytes;
        if(!version)bytes.assign(record.data(),record.data()+size);
        else {
            bytes.resize(24+2*size+(version==2?8:0));std::memcpy(bytes.data(),"BTWS",4);bytes[4]=version;bytes[6]=24;
            PatternFilePut32(bytes.data()+8,size);PatternFilePut32(bytes.data()+12,size);
            std::memcpy(bytes.data()+24,record.data(),size);std::memcpy(bytes.data()+24+size,record.data(),size);
            auto* working=bytes.data()+24+size;working[24]=61;PatternFilePut32(working+12,12753);
            PatternFilePut32(working+20,PatternFileCrc(working,size));
            if(version==2){bytes[24+2*size]=19;bytes[24+2*size+4]=165;}
            PatternFilePut32(bytes.data()+20,PatternFileCrc(bytes.data(),bytes.size()));
        }
        FakePatternSd::files[kSessionPath].bytes=bytes;WorkingSession target;target.definitions.Reset(false);
        CHECK_EQ(store.Load(backend,target),PatternFileResult::Ok);CHECK_EQ(target.definitions.Count(),5u);
        Same(target.published,pattern);CHECK_EQ(target.published_tempo,15500u);
        CHECK_EQ(target.working.cells[0][0].note,version?61u:pattern.cells[0][0].note);
        CHECK_EQ(target.working_tempo,version?12753u:15500u);
        CHECK_EQ(target.view.row,version==2?19u:0u);CHECK_EQ(target.mute_mask,version==2?165u:0u);
        CHECK_EQ(store.Save(backend,target),PatternFileResult::Ok);
        CHECK(std::memcmp(FakePatternSd::files[kSessionPath].bytes.data(),"BTM1",4)==0);
        WorkingSession migrated;CHECK_EQ(store.Load(backend,migrated),PatternFileResult::Ok);
        Same(migrated.working,target.working);Same(migrated.published,target.working);
        CHECK_EQ(migrated.working_tempo,target.working_tempo);CHECK_EQ(migrated.published_tempo,target.working_tempo);
    }
}

TEST_CASE(LegacyBtwsV2ReservedMetadataRejectsWithoutMutatingDestination) {
    const auto published=MakeDevelopmentPattern(16);
    auto working=published;working.cells[0][0]={61,3};
    std::vector<std::uint8_t> bytes(32+2*kPatternFileCapacity);
    std::size_t a=0,c=0;
    CHECK_EQ(EncodePatternFile(PatternFileKind::Session,published,15500,bytes.data()+24,kPatternFileCapacity,a),PatternFileResult::Ok);
    CHECK_EQ(EncodePatternFile(PatternFileKind::Session,working,13725,bytes.data()+24+a,kPatternFileCapacity,c),PatternFileResult::Ok);
    bytes.resize(32+a+c);std::memcpy(bytes.data(),"BTWS",4);bytes[4]=2;bytes[6]=24;
    PatternFilePut32(bytes.data()+8,a);PatternFilePut32(bytes.data()+12,c);
    auto* metadata=bytes.data()+24+a+c;
    metadata[0]=7;metadata[1]=2;metadata[2]=0;metadata[3]=1;metadata[4]=90;
    PatternFilePut32(bytes.data()+20,PatternFileCrc(bytes.data(),bytes.size()));
    WorkingSession valid;
    CHECK_EQ(DecodeWorkingSession(bytes.data(),bytes.size(),valid),PatternFileResult::Ok);

    WorkingSession destination;destination.published=Varied();destination.working=MakeDevelopmentPattern();
    destination.working.cells[0][0]={72,2};destination.published_tempo=11111;destination.working_tempo=22222;
    destination.definitions.Reset(false);
    CHECK(destination.definitions.Append(0,1,"Samples/destination.wav"));
    CHECK(destination.definitions.Append(1,2,nullptr));CHECK(destination.definitions.Append(2,3,nullptr));
    destination.view={11,3,1,0};destination.mute_mask=165;
    const auto before=destination;
    for(unsigned reserved:{5u,6u,7u}) {
        auto malformed=bytes;malformed[24+a+c+reserved]=1;
        PatternFilePut32(malformed.data()+20,PatternFileCrc(malformed.data(),malformed.size()));
        CHECK_EQ(PatternFileU32(malformed.data()+20),PatternFileCrc(malformed.data(),malformed.size()));
        CHECK_EQ(DecodeWorkingSession(malformed.data(),malformed.size(),destination),PatternFileResult::Invalid);
        Same(destination.published,before.published);Same(destination.working,before.working);
        CHECK_EQ(destination.published_tempo,before.published_tempo);CHECK_EQ(destination.working_tempo,before.working_tempo);
        CHECK_EQ(destination.definitions.Count(),before.definitions.Count());
        for(unsigned i=0;i<before.definitions.Count();++i) {
            const auto* actual=destination.definitions.At(i);const auto* expected=before.definitions.At(i);
            CHECK(actual!=nullptr);if(!actual)continue;
            CHECK_EQ(actual->type,expected->type);
            if(expected->resource) {CHECK(actual->resource!=nullptr);if(actual->resource)CHECK(std::strcmp(actual->resource,expected->resource)==0);}
            else CHECK(actual->resource==nullptr);
        }
        CHECK_EQ(destination.view.row,before.view.row);CHECK_EQ(destination.view.channel,before.view.channel);
        CHECK_EQ(destination.view.field,before.view.field);CHECK_EQ(destination.view.header,before.view.header);
        CHECK_EQ(destination.mute_mask,before.mute_mask);
    }
}

TEST_CASE(TemporaryBtmMalformedDefinitionsAndIoRestorePreserveActiveSession) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession source;source.working=MakeDevelopmentPattern();source.working_tempo=13725;
    CHECK_EQ(store.Save(backend,source),PatternFileResult::Ok);const auto good=FakePatternSd::files[kSessionPath].bytes;
    WorkingSession active;active.working=MakeDevelopmentPattern();active.working.cells[0][0]={72,5};
    CHECK(active.definitions.Append(5,3,nullptr));active.working_tempo=14550;
    for(unsigned fault=0;fault<8;++fault) {
        auto bad=good;const auto offset=24+PatternFileU32(bad.data()+8);
        if(fault==0)bad[offset]=0;
        if(fault==1)std::memset(bad.data()+offset+1,0,64); // Sample requires a resource.
        if(fault==2)bad[offset+1]='/';
        if(fault==3){bad[offset+1]='.';bad[offset+2]='.';}
        if(fault==4)std::memset(bad.data()+offset+1,'a',64); // No terminator.
        if(fault==5)bad[offset+64]=1; // Nonzero padding.
        if(fault==6)bad[offset+65+1]='x'; // MIDI must have no resource.
        if(fault==7)PatternFilePut32(bad.data()+12,100);
        PatternFilePut32(bad.data()+20,PatternFileCrc(bad.data(),bad.size()));
        FakePatternSd::files[kSessionPath].bytes=bad;
        CHECK_EQ(store.Load(backend,active),PatternFileResult::Invalid);
        CHECK_EQ(active.working.cells[0][0].note,72u);CHECK_EQ(active.working_tempo,14550u);CHECK_EQ(active.definitions.Count(),6u);
    }
    FakePatternSd::files[kSessionPath].bytes=good;FakePatternSd::fail_open=true;
    CHECK(store.Load(backend,active)!=PatternFileResult::Ok);
    CHECK_EQ(active.working.cells[0][0].note,72u);CHECK_EQ(active.working_tempo,14550u);CHECK_EQ(active.definitions.Count(),6u);
}
TEST_CASE(TemporaryBtmMaximumTableAndOwnedReferencesSurviveCopy) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession source;source.working=MakeDevelopmentPattern(64);source.definitions.Reset(false);
    char path[]="Samples/test.wav";
    for(unsigned i=0;i<99;++i)CHECK(source.definitions.Append(i,1,path));
    path[0]='X';WorkingSession copy=source;source.definitions.Reset(false);
    CHECK_EQ(store.Save(backend,copy),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
    CHECK_EQ(restored.definitions.Count(),99u);CHECK(std::strcmp(restored.definitions.At(98)->resource,"Samples/test.wav")==0);
    CHECK_EQ(InstrumentSelectionMaximum(restored.definitions.Count()),99u);
}

TEST_CASE(WavPickerListsOnlyBoundedSdFilenamesWithoutChangingFiles) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;
    FakePatternSd::files[kWavDirectory].directory=true;
    for(const char* name:{"other.wav","OTHER.WAV","unsupported.wav","notwav.txt","../bad.wav"})FakePatternSd::files[std::string(kWavDirectory)+"/"+name].bytes={1,2,3};
    FakePatternSd::files[std::string(kWavDirectory)+"/folder.wav"].directory=true;
    WavDirectoryPage page;CHECK_EQ(ReadWavDirectoryPage(backend,0,page),PatternFileResult::Ok);
    CHECK_EQ(page.count,3u);CHECK(page.done);CHECK_EQ(FakePatternSd::rename_calls,0u);
    char resource[64];CHECK(BuildWavResource("other.wav",resource,sizeof(resource)));CHECK_EQ(std::string(resource),std::string("BroTracker/Samples/other.wav"));
    for(const char* invalid:{"../bad.wav","a/b.wav","with space.wav",".wav","other.wav.candidate"})CHECK(!BuildWavResource(invalid,resource,sizeof(resource)));
}
