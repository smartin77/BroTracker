#include "test_framework.h"
#include "bringup/pattern_fixture.h"
#include "bringup/host_pattern_publication.h"
#include "core/playback/native_rate_pattern_player.h"
#include "core/playback/variable_rate_pattern_player.h"
#include "ui/local_pattern_editor.h"
#include <cstring>
#include <vector>

using namespace BroTracker;
namespace {
const std::int16_t length_pcm[] = {10,20,30};
std::uint64_t RowSample(unsigned row) {
    std::uint64_t sample=0;
    CHECK_EQ(TickToSamplePosition(std::uint64_t(row)*kTicksPerRow,12753,100,sample),TickToSampleStatus::Success);
    return sample;
}
template<class Player, class Bindings> void CheckLength(unsigned rows) {
    Bindings bindings; bindings.count=1; bindings.bindings[0]={0,60,{length_pcm,3,100}};
    RealtimePattern pattern; pattern.active_rows=static_cast<std::uint8_t>(rows); pattern.active_channels=1;
    pattern.cells[0][0]={60,0}; pattern.cells[15][0]={60,0};
    pattern.cells[rows-1][0]={60,0};
    if(rows>16) pattern.cells[16][0]={60,0};
    Player player;
    using Status=decltype(player.Configure(12753,100,pattern,bindings));
    CHECK_EQ(player.Configure(12753,100,pattern,bindings),Status::Success);
    CHECK_EQ(player.Start(),Status::Success);
    auto invalid=pattern; invalid.cells[rows-1][0].note=23;
    CHECK(player.Configure(12753,100,invalid,bindings)!=Status::Success);
    CHECK(player.IsRunning()); CHECK_EQ(player.GetNextSamplePosition(),0);
    for(std::uint64_t sample=0;sample<RowSample(rows)+3;++sample) {
        std::int16_t value=-1;
        CHECK_EQ(player.Render(&value,1),Status::Success);
        int expected=0;
        for(unsigned row=0;row<=rows;++row) {
            const auto wrapped=row%rows;
            if(wrapped==0 || wrapped==15 || wrapped==rows-1 || (rows>16 && wrapped==16)) {
                const auto start=RowSample(row);
                if(sample>=start && sample-start<3) expected=length_pcm[sample-start];
            }
        }
        CHECK_EQ(value,expected);
        if(rows>16 && sample==RowSample(16)) {
            CHECK_EQ(player.GetPlaybackPosition().pattern_row,16);
            CHECK_EQ(player.GetPlaybackPosition().loop_index,0);
        }
        if(sample==RowSample(rows)) {
            CHECK_EQ(player.GetPlaybackPosition().pattern_row,0);
            CHECK_EQ(player.GetPlaybackPosition().loop_index,1);
        }
    }
    std::vector<std::int16_t> reference(RowSample(rows)+3), partitioned(reference.size());
    for(const auto chunk:{32u,7u}) {
        CHECK_EQ(player.Start(),Status::Success);
        auto& output=chunk==32 ? reference : partitioned;
        for(std::size_t offset=0;offset<output.size();) {
            const auto count=std::min<std::size_t>(chunk,output.size()-offset);
            CHECK_EQ(player.Render(output.data()+offset,count),Status::Success); offset+=count;
        }
    }
    CHECK(reference==partitioned);
    player.Stop();
    auto wrong_timing=pattern; wrong_timing.note_off_timing[rows-1][0]=static_cast<NoteOffTiming>(2);
    CHECK(player.ReplacePattern(wrong_timing)!=Status::Success);
    auto unknown=pattern; unknown.cells[rows-1][0].instrument=254;
    CHECK(player.ReplacePattern(unknown)!=Status::Success);
    CHECK(player.ReplacePattern(invalid)!=Status::Success);
    CHECK_EQ(player.ReplacePattern(pattern),Status::Success);
    CHECK_EQ(player.Start(),Status::Success);
    std::int16_t value=0; CHECK_EQ(player.Render(&value,1),Status::Success); CHECK_EQ(value,10);
}
}
TEST_CASE(PatternLengthsFixturePreservesEventsAndEmptyTail) {
    CHECK_EQ(kPatternBringUpFixtureRows,32);
    const auto original=MakeBeeperPattern(16);
    for(const auto rows:{16,32,64}) {
        const auto pattern=MakeBeeperPattern(static_cast<std::uint8_t>(rows));
        CHECK_EQ(pattern.active_rows,rows);
        for(unsigned r=0;r<kRealtimePatternRowCapacity;++r) for(unsigned c=0;c<8;++c) {
            CHECK_EQ(pattern.cells[r][c].note, r<16 ? original.cells[r][c].note : NOTE_EMPTY);
            CHECK_EQ(pattern.cells[r][c].instrument, r<16 ? original.cells[r][c].instrument : kNoInstrumentUpdate);
            CHECK_EQ(pattern.note_off_timing[r][c],NoteOffTiming::Arrival);
        }
    }
}
TEST_CASE(PatternLengthsBothPlayersProgressUpperRowsAndLoopWithoutTruncation) {
    for(const auto rows:{16,32,64}) {
        CheckLength<NativeRatePatternPlayer,NativeRateSampleBindings>(rows);
        CheckLength<VariableRatePatternPlayer,VariableRateSampleBindings>(rows);
    }
}
TEST_CASE(PatternLengthsSnapshotPublicationPreviewAllCells) {
    for(const auto rows:{32,64}) {
        RealtimePattern pattern; pattern.active_rows=static_cast<std::uint8_t>(rows); pattern.active_channels=8;
        pattern.cells[rows-1][7]={NOTE_OFF,254};
        pattern.note_off_timing[rows-1][7]=NoteOffTiming::EndOfPosition;
        pattern.cells[16][0]={74,0};
        PatternSnapshotSender sender(pattern,12753); CHECK(sender.Begin(1,0,2));
        PatternSnapshotAssembler assembler; CHECK(assembler.Request(1,0,2));
        unsigned lines=0;
        while(sender.HasData() && lines<514) {
            std::size_t size=0; const char* line=sender.Data(size);
            assembler.Accept(line,size-1,0); sender.Consume(size); ++lines;
        }
        CHECK_EQ(lines,unsigned(rows*8+2)); CHECK(!sender.HasData()); CHECK(assembler.Snapshot());
        CHECK(SamePattern(assembler.Snapshot()->pattern,pattern));
        CHECK(assembler.Request(2,0,2));
        const char bad_begin[]="BTPATTERN1 BEGIN 2 2 65 8 12753 520";
        CHECK_EQ(assembler.Accept(bad_begin,sizeof(bad_begin)-1,0),SnapshotAssemblyStatus::Rejected);
        CHECK(SamePattern(assembler.Snapshot()->pattern,pattern));
        for(const bool preview:{false,true}) {
            HostPatternPublication host; CHECK(host.Begin(1,*assembler.Snapshot(),pattern,0));
            PatternPublication device;
            for(unsigned i=0;i<unsigned(rows*8+2);++i) {
                char line[kPatternSnapshotLineCapacity]; std::size_t size=0;
                CHECK(host.FormatLine(line,sizeof(line),size,preview));
                PublicationMessage message; CHECK(ParsePublication(line,size-1,message,preview));
                CHECK_EQ(device.Accept(message,0),i==unsigned(rows*8+1) ? PublicationStatus::Ready : PublicationStatus::Receiving);
                host.LineComplete();
            }
            CHECK(device.Freeze()); CHECK(device.Frozen()); CHECK(SamePattern(*device.Frozen(),pattern));
            PublicationMessage bad{PublicationKind::Begin,{2,65,8,520,0}};
            CHECK_EQ(device.Accept(bad,0),PublicationStatus::Busy);
            CHECK(SamePattern(*device.Frozen(),pattern));
        }
    }
}
TEST_CASE(PatternLengthsUpperRowEditorHistoryRestoreAndRollback) {
    LocalPatternEditor editor;
    LivePlaybackView live; live.telemetry_available=true; live.position.rows=64; live.position.channels=8;
    DevicePatternSnapshot snapshot; snapshot.version=2; snapshot.transfer_id=1; snapshot.tempo_hundredths=12753;
    snapshot.pattern.active_rows=64; snapshot.pattern.active_channels=8; live.device_pattern=snapshot;
    editor.Sync(live); CHECK(editor.Ready());
    for(unsigned r=1;r<64;++r) CHECK(editor.Apply(EditorAction::Down));
    for(unsigned c=1;c<8;++c) CHECK(editor.Apply(EditorAction::NextChannel));
    CHECK(editor.Apply(EditorAction::SetValue,74)); CHECK(editor.Undo()); CHECK(editor.Redo());
    CHECK_EQ(editor.Draft()->cells[63][7].note,74);
    CHECK(!editor.Apply(EditorAction::Down)); CHECK(!editor.Apply(EditorAction::SetValue,23));
    CHECK_EQ(editor.Draft()->cells[63][7].note,74);
    CHECK(editor.Apply(EditorAction::Restore)); CHECK(!editor.Dirty()); CHECK(editor.Undo());
    CHECK_EQ(editor.Draft()->cells[63][7].note,74); CHECK_EQ(editor.Baseline()->pattern.cells[63][7].note,NOTE_EMPTY);
    PatternEditHistory history; auto before=snapshot.pattern; auto after=before;
    for(unsigned r=0;r<64;++r) for(unsigned c=0;c<8;++c) after.cells[r][c]={60,0};
    history.Record(before,after); CHECK(history.Undo(after)); CHECK(SamePattern(after,before));
    CHECK(history.Redo(after)); CHECK_EQ(after.cells[63][7].note,60);
}

TEST_CASE(DevelopmentPatternEightChannelsEveryRowAndEmptyUnusedCells) {
    for(const auto rows:{16,32,64}) {
        const auto p=MakeDevelopmentPattern(static_cast<std::uint8_t>(rows));
        CHECK_EQ(p.active_rows,rows); CHECK_EQ(p.active_channels,8);
        for(unsigned row=0;row<64;++row) for(unsigned channel=0;channel<8;++channel) {
            const auto cell=p.cells[row][channel];
            CHECK_EQ(p.note_off_timing[row][channel],NoteOffTiming::Arrival);
            if(row<unsigned(rows) && channel<2) {
                CHECK(cell.note>=60 && cell.note<=74); CHECK(cell.note!=NOTE_OFF);
                CHECK_EQ(cell.instrument,channel);
            } else { CHECK_EQ(cell.note,NOTE_EMPTY); CHECK_EQ(cell.instrument,kNoInstrumentUpdate); }
        }
        CHECK_EQ(p.cells[0][0].note,60); CHECK_EQ(p.cells[0][1].note,62);
        CHECK_EQ(p.cells[1][0].note,64); CHECK_EQ(p.cells[1][1].note,65);
    }
}

TEST_CASE(DevelopmentBindingsUseCellInstrumentIdsAndNeverSubstituteMissingSamples) {
    const std::int16_t a[]={10,20},b[]={30,40},wav[]={100,200};
    VariableRateSampleBindings bindings; bindings.count=2;
    bindings.bindings[0]={0,60,{a,2,44100}}; bindings.bindings[1]={1,62,{b,2,44100}};
    for(const auto view:{Pcm16MonoView{},Pcm16MonoView{wav,2,48000},Pcm16MonoView{wav,2,44100}}) {
        auto fixture=SelectDevelopmentFixture(bindings,view);
        const bool valid=view.sample_rate_hz==44100;
        CHECK_EQ(fixture.pattern.active_channels,8); CHECK_EQ(fixture.bindings.count,valid?3:2);
        if(valid) { CHECK_EQ(fixture.bindings.bindings[2].instrument,3); CHECK_EQ(fixture.bindings.bindings[2].reference_note,60); }
        VariableRatePatternPlayer player; CHECK_EQ(player.Configure(12753,44100,fixture.pattern,fixture.bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); std::int16_t pcm[2];
        CHECK_EQ(player.Render(pcm,2),VariableRatePatternPlayerStatus::Success); CHECK_EQ(pcm[0],40); CHECK_EQ(pcm[1],60);
        // Explicit ID 3 can play on any channel; CH3 is not instrument-routed.
        if(valid) {
            player.Stop(); fixture.pattern.cells[0][0]={60,3}; fixture.pattern.cells[0][1]={NOTE_EMPTY,kNoInstrumentUpdate};
            CHECK_EQ(player.Configure(12753,44100,fixture.pattern,fixture.bindings),VariableRatePatternPlayerStatus::Success);
            CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Render(pcm,2),VariableRatePatternPlayerStatus::Success); CHECK_EQ(pcm[0],100); CHECK_EQ(pcm[1],200);
        }
    }
}

TEST_CASE(DevelopmentTest2AndTest3BindingsIndependentAndChannelsEmpty) {
    const std::int16_t a[]={10,20},b[]={30,40},sample2[]={100,200},sample3[]={300,400};
    VariableRateSampleBindings beepers; beepers.count=2;
    beepers.bindings[0]={0,60,{a,2,44100}}; beepers.bindings[1]={1,62,{b,2,44100}};
    for(unsigned mask=0;mask<4;++mask) {
        auto f=SelectDevelopmentFixture(beepers,mask&1?Pcm16MonoView{sample2,2,44100}:Pcm16MonoView{},
            mask&2?Pcm16MonoView{sample3,2,44100}:Pcm16MonoView{});
        CHECK_EQ(f.bindings.count,2+unsigned(bool(mask&1))+unsigned(bool(mask&2)));
        CHECK_EQ(f.bindings.bindings[0].reference_note,60); CHECK_EQ(f.bindings.bindings[1].reference_note,62);
        for(unsigned row=0;row<f.pattern.active_rows;++row) for(unsigned ch=2;ch<8;++ch) {
            CHECK_EQ(f.pattern.cells[row][ch].note,NOTE_EMPTY); CHECK_EQ(f.pattern.cells[row][ch].instrument,kNoInstrumentUpdate);
            CHECK_EQ(f.pattern.note_off_timing[row][ch],NoteOffTiming::Arrival);
        }
        for(unsigned i=2;i<f.bindings.count;++i) {
            const auto& binding=f.bindings.bindings[i]; CHECK(binding.instrument==3 || binding.instrument==4);
            CHECK_EQ(binding.reference_note,60); CHECK_EQ(binding.sample.data,binding.instrument==3?sample2:sample3);
            auto pattern=f.pattern; pattern.cells[0][0]={60,binding.instrument}; pattern.cells[0][1]={NOTE_EMPTY,kNoInstrumentUpdate};
            VariableRatePatternPlayer player; CHECK_EQ(player.Configure(12753,44100,pattern,f.bindings),VariableRatePatternPlayerStatus::Success);
            CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); std::int16_t pcm[4];
            CHECK_EQ(player.Render(pcm,4),VariableRatePatternPlayerStatus::Success);
            CHECK_EQ(pcm[0],binding.instrument==3?100:300); CHECK_EQ(pcm[1],binding.instrument==3?200:400);
            CHECK_EQ(pcm[2],0); CHECK_EQ(pcm[3],0);
        }
    }
}
