#include "test_framework.h"
#include "firmware/teensy/BroTracker/sd_pattern_storage.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_preview.h"
#include "bringup/pattern_storage_protocol.h"
#include <array>
using namespace BroTracker;
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
    CHECK_EQ(store.Load(backend,kDevelopmentThemePath,PatternFileKind::Development,target,tempo),PatternFileResult::Missing);Same(target,initial);CHECK_EQ(tempo,12500u);
    for(bool midi:{false,true}){
        CHECK_EQ(store.CreateDevelopment(backend,midi,scratch),PatternFileResult::Ok);
        const auto path=midi?kDevelopmentMidiPath:kDevelopmentThemePath;const auto original=FakePatternSd::files[path].bytes;
        CHECK_EQ(store.CreateDevelopment(backend,midi,scratch),PatternFileResult::Exists);CHECK(FakePatternSd::files[path].bytes==original);
        CHECK_EQ(store.Load(backend,path,PatternFileKind::Development,target,tempo),PatternFileResult::Ok);Same(target,scratch);CHECK_EQ(tempo,15500u);
        auto theme=MakeDevelopmentPattern(32);for(unsigned r=0;r<32;++r)for(unsigned c=0;c<8;++c){
            if(c!=4 || !midi){CHECK_EQ(target.cells[r][c].note,theme.cells[r][c].note);CHECK_EQ(target.cells[r][c].instrument,theme.cells[r][c].instrument);}
        }
        if(midi){constexpr unsigned notes[]={64,67,71,67,69,72,69,67};for(unsigned i=0;i<8;++i){CHECK_EQ(target.cells[2+4*i][4].note,notes[i]);CHECK_EQ(target.cells[2+4*i][4].instrument,1);CHECK_EQ(target.cells[3+4*i][4].note,NOTE_OFF);CHECK_EQ(target.note_off_timing[3+4*i][4],NoteOffTiming::Arrival);}}
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
    CHECK_EQ(ParseStorageOperation("BTSTORE1 SEED 1 theme"),StorageOperation::SeedTheme);CHECK_EQ(ParseStorageOperation("BTSTORE1 LOAD 1 midi"),StorageOperation::LoadMidi);
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
    FakePatternSd::short_write=true;CHECK_EQ(store.CreateDevelopment(backend,false,scratch),PatternFileResult::Io);CHECK(!FakePatternSd::files.count(kDevelopmentThemePath));
    FakePatternSd::short_write=false;FakePatternSd::files["/BroTracker/development/theme.btdev.candidate"]={false,{1,2}};
    CHECK_EQ(store.CreateDevelopment(backend,false,scratch),PatternFileResult::RecoveryRequired);CHECK(!FakePatternSd::files.count(kDevelopmentThemePath));Released();
}
TEST_CASE(SessionCanonicalDirectoryIsNotRenamedOrReplaced) {
    FakePatternSd::Reset();SdPatternFiles backend;backend.ready=true;PatternFileStore<SdPatternFiles> store;
    FakePatternSd::files[kSessionPath].directory=true;const auto p=MakeDevelopmentPattern();
    CHECK_EQ(store.SaveSession(backend,p,15500),PatternFileResult::Io);
    CHECK(FakePatternSd::files[kSessionPath].directory);CHECK_EQ(FakePatternSd::rename_calls,0u);CHECK(!FakePatternSd::files.count(kSessionCandidatePath));Released();
}
