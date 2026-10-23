#include "test_framework.h"
#include "bringup/host_channel_mute.h"
#include "bringup/midi_output.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_fixture.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include <vector>
#include <tuple>
#include <cstring>
using namespace BroTracker;
namespace {
LivePlaybackView MuteView() {
    LivePlaybackView v;v.telemetry_available=true;v.position.rows=32;v.position.channels=8;
    DevicePatternSnapshot s;s.transfer_id=1;s.tempo_hundredths=15500;s.pattern.active_rows=32;s.pattern.active_channels=8;
    v.device_pattern=s;v.mute_mask=0;return v;
}
}
TEST_CASE(HeaderNavigationHasDistinctTargetAndNoPatternHistoryOrPreviewChanges) {
    LocalPatternEditor e;e.Sync(MuteView());auto before=*e.Draft();
    CHECK(e.Apply(EditorAction::Right));CHECK(e.Cursor().field==EditField::Instrument);
    CHECK(e.Apply(EditorAction::PreviousBeat));CHECK(e.Cursor().header);CHECK_EQ(e.Cursor().row,0);
    CHECK(!e.Apply(EditorAction::Up));CHECK(!e.Apply(EditorAction::PreviousBeat));
    CHECK(!e.Apply(EditorAction::Left));CHECK(e.Apply(EditorAction::Right));CHECK_EQ(e.Cursor().channel,1);
    CHECK(e.Cursor().field==EditField::Instrument);CHECK(e.Apply(EditorAction::NextChannel));
    CHECK_EQ(e.Cursor().channel,2);CHECK(e.Cursor().field==EditField::Instrument);
    CHECK(!e.Apply(EditorAction::Increment));CHECK(!e.Apply(EditorAction::NoteOff));CHECK(!e.Apply(EditorAction::Clear));
    CHECK(e.Apply(EditorAction::NextBeat));CHECK(!e.Cursor().header);CHECK_EQ(e.Cursor().row,0);
    CHECK(e.Cursor().field==EditField::Instrument);
    for(unsigned r=1;r<32;++r) CHECK(e.Apply(EditorAction::Down));
    CHECK(!e.Apply(EditorAction::Down));CHECK(!e.Cursor().header);
    CHECK(SamePattern(before,*e.Draft()));CHECK(!e.Dirty());CHECK(!e.CanUndo());
    SparsePreviewCell cell;for(unsigned i=0;i<kSparsePreviewCellCapacity;++i) CHECK(!e.TakeCapturedCell(i,cell));
    CHECK(e.SelectHeader(7));CHECK(!e.Apply(EditorAction::Right));CHECK(e.Apply(EditorAction::Down));CHECK(!e.Cursor().header);
}
TEST_CASE(MuteHostCoalescesIndependentTogglesAndReconnectNeverReplaysIntent) {
    HostChannelMute h;auto rx=[&](const char* s){return h.Response(s,std::strlen(s));};
    CHECK(!h.Toggle(0));CHECK(rx("BTMUTE1 CAP 1"));CHECK(rx("BTMUTE1 STATE 1 0 1 0"));
    CHECK(h.Toggle(0));CHECK(h.Begin());char line[96];std::size_t length=0;
    CHECK(h.FormatLine(line,sizeof(line),length));CHECK_EQ(std::string(line),"BTMUTE1 SET 1 1 1\n");
    h.BeforeWrite(0);h.LineComplete();CHECK(h.Toggle(1));CHECK(h.Toggle(2));
    CHECK_EQ(h.Mask(),0);CHECK(rx("BTMUTE1 SUBMITTED 1 1"));CHECK(rx("BTMUTE1 APPLIED 1 1"));
    CHECK(rx("BTMUTE1 STATE 1 1 2 1"));CHECK_EQ(h.Mask(),1);CHECK(h.Begin());
    CHECK(h.FormatLine(line,sizeof(line),length));CHECK_EQ(std::string(line),"BTMUTE1 SET 1 2 7\n");
    h.LineComplete();CHECK(rx("BTMUTE1 REJECTED 1 2"));CHECK(std::strstr(h.Status(),"rejected"));
    CHECK(rx("BTMUTE1 STATE 1 1 2 1"));CHECK(h.Toggle(7));h.Disconnect();
    CHECK(!h.Toggle(0));CHECK(rx("BTMUTE1 CAP 1"));CHECK(rx("BTMUTE1 STATE 1 1 2 1"));CHECK(!h.Begin());
    CHECK_EQ(h.Mask(),1);CHECK(!rx("BTMUTE1 STATE 1 1 2 256"));
    CHECK(h.Toggle(0));CHECK(h.Begin());h.BeforeWrite(100);h.LineComplete();h.Tick(5100);
    CHECK(std::strstr(h.Status(),"uncertain"));CHECK(!h.Toggle(4));CHECK(h.NeedsRead(5100));
    CHECK(rx("BTMUTE1 STATE 1 2 3 0"));CHECK_EQ(h.Mask(),0);CHECK(h.Available());CHECK(!h.Begin());
}
TEST_CASE(HeaderViewportHitTestingUsesActualOutputAndPadding) {
    for(int scale:{1,2,3}) {
        int x=-1,y=-1;
        CHECK(PatternGrid::WindowToFrame(56*scale,42*scale,640*scale,480*scale,1280*scale,960*scale,0,0,1280*scale,960*scale,x,y));
        CHECK_EQ(PatternGrid::HeaderAt(x,y),0);
        CHECK(PatternGrid::WindowToFrame(156,92,840,580,1680,1160,200,100,1280,960,x,y));
        CHECK_EQ(x,56);CHECK_EQ(y,42);CHECK_EQ(PatternGrid::HeaderAt(x,y),0);
    }
    int x,y;CHECK(!PatternGrid::WindowToFrame(0,0,840,580,1680,1160,200,100,1280,960,x,y));
    CHECK_EQ(PatternGrid::HeaderAt(56,54),-1);CHECK_EQ(PatternGrid::HeaderAt(29,42),-1);
    CHECK_EQ(PatternGrid::HeaderAt(83,42),-1);CHECK_EQ(PatternGrid::HeaderAt(452,42),7);
}
TEST_CASE(MuteSuppressesInstrumentChangesReleasesSharedMidiAndUnmutesWithoutCatchup) {
    RealtimePattern p;p.active_rows=4;p.active_channels=2;
    p.cells[0][0]={69,1};p.cells[0][1]={69,1};
    p.cells[1][0]={NOTE_EMPTY,2}; // Must NOT select Synth when muted.
    p.cells[2][0]={72,kNoInstrumentUpdate};p.cells[3][1]={NOTE_OFF,kNoInstrumentUpdate};
    VariableRatePatternPlayer player;CHECK_EQ(player.Configure(30000,44100,p,SelectDevelopmentFixture({}).bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);std::int16_t pcm[128];
    MidiOutput midi;midi.Lifecycle(true,1);midi.TakeAbortRequest();midi.Service(0,[](const auto*,unsigned){return true;});midi.ConfirmTransfers();
    std::vector<std::uint32_t> words;
    auto consume=[&](unsigned frames){
        const auto base=player.GetNextSamplePosition();CHECK_EQ(player.Render(pcm,frames),VariableRatePatternPlayerStatus::Success);
        auto b=player.TakeMidiCommands();const unsigned now=unsigned(base*1000000/44100);CHECK(midi.Enqueue(b,base,now));
        midi.Service(now+2900,[&](const auto* w,unsigned n){words.insert(words.end(),w,w+n);return true;});midi.ConfirmTransfers();
        return b;
    };
    consume(1);CHECK_EQ(words.size(),1);CHECK_EQ(player.SetMuteMask(1),VariableRatePatternPlayerStatus::Success);
    auto release=consume(1);CHECK_EQ(release.count,1);CHECK_EQ(release.commands[0].action,VariableRateVoiceAction::Release);
    CHECK_EQ(words.size(),1); // Peer still owns shared 69.
    while(player.GetNextSamplePosition()<2300) consume(std::min<std::uint64_t>(128,2300-player.GetNextSamplePosition()));
    CHECK_EQ(player.SetMuteMask(0),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.ReplaceWorkingPattern(p),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(consume(1).count,0);
    bool midi72=false;
    while(player.GetNextSamplePosition()<4500) {auto b=consume(std::min<std::uint64_t>(128,4500-player.GetNextSamplePosition()));for(unsigned i=0;i<b.count;++i) if(b.commands[i].synth_note==72) {midi72=true;CHECK_EQ(b.commands[i].action,VariableRateVoiceAction::TriggerMidi);}}
    CHECK(midi72); // Retained MIDI selection, not suppressed Synth selection.
    CHECK_EQ(player.SetMuteMask(3),VariableRatePatternPlayerStatus::Success);consume(1);
    CHECK(std::find(words.end()-2,words.end(),midi.Pack(false,72))!=words.end());
}
TEST_CASE(MuteSampleFadeSynthReleaseAndBlockPartitionIndependence) {
std::vector<std::int16_t> data(20000,12000);
    auto run=[&](unsigned block,InstrumentType type) {
        VariableRateSampleBindings bindings;bindings.count=1;bindings.bindings[0]={0,60,{data.data(),data.size(),44100},type};
        RealtimePattern p;p.active_rows=4;p.active_channels=1;p.cells[0][0]={60,0};
        VariableRatePatternPlayer player;CHECK_EQ(player.Configure(12500,44100,p,bindings),VariableRatePatternPlayerStatus::Success);CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> audio;std::int16_t out[128];
        for(unsigned pos=0;pos<2000;) {
            if(pos==512) CHECK_EQ(player.SetMuteMask(1),VariableRatePatternPlayerStatus::Success);
            if(pos==1500) CHECK_EQ(player.SetMuteMask(0),VariableRatePatternPlayerStatus::Success);
            unsigned n=std::min(block,2000-pos);if(pos<512) n=std::min(n,512-pos);else if(pos<1500)n=std::min(n,1500-pos);
            CHECK_EQ(player.Render(out,n),VariableRatePatternPlayerStatus::Success);audio.insert(audio.end(),out,out+n);player.TakeMidiCommands();pos+=n;
        }
        CHECK_EQ(player.GetNextSamplePosition(),2000);return audio;
    };
    for(auto type:{InstrumentType::Sample,InstrumentType::Synth}) {
        const auto a=run(128,type);CHECK(a==run(1,type));CHECK(a==run(97,type));
        CHECK(a[512]!=0);for(unsigned i=1300;i<2000;++i) CHECK_EQ(a[i],0);
        if(type==InstrumentType::Sample) {CHECK_EQ(a[511],12000);CHECK(a[513]<a[512]);CHECK_EQ(a[600],0);}
    }
}
TEST_CASE(MuteAudioBoundaryPrecedesTransportAndPersistsThroughPublicationAndCoreErrors) {
    using C=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    auto f=SelectDevelopmentFixture({});C c;CHECK_EQ(c.InitializeAudio(15500,44100,f.pattern,f.bindings),PatternPlayerStatus::Success);
    CHECK_EQ(c.SubmitMute(1,1),PatternRequestStatus::Accepted);CHECK_EQ(c.SubmitMute(5,2),PatternRequestStatus::Accepted);
    CHECK_EQ(c.Snapshot().mute_mask,0);CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t out[128];c.AudioBlock(out,128,true);CHECK_EQ(c.Snapshot().mute_mask,5);CHECK_EQ(c.Snapshot().applied_mute_id,2);
    for(auto action:{PatternRequest::Pause,PatternRequest::Continue,PatternRequest::Stop}) {CHECK_EQ(c.Submit(action),PatternRequestStatus::Accepted);c.AudioBlock(out,128,true);CHECK_EQ(c.Snapshot().mute_mask,5);}
    CHECK_EQ(c.SubmitPattern(f.pattern,1,16000),PatternRequestStatus::Accepted);c.AudioBlock(out,128,true);
    CHECK_EQ(c.Snapshot().mute_mask,5);CHECK_EQ(c.Snapshot().tempo,16000);
    CHECK_EQ(c.SubmitMute(0,2),PatternRequestStatus::InvalidRequest);
    c.AudioBlock(nullptr,128,false);CHECK_EQ(c.Snapshot().fault,PatternFault::Allocation);CHECK_EQ(c.Snapshot().mute_mask,5);
}
TEST_CASE(HeaderMuteCrossUsesGlyphCoverageAndPreservesOrangeAndOuterBorders) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));auto live=MuteView();LocalPatternEditor editor;editor.Sync(live);CHECK(editor.SelectHeader(0));
    Tune tune;Pattern pattern;Framebuffer base(640,480),muted(640,480);
    RenderMainScreen(base,tune,pattern,live,&editor);live.mute_mask=255;RenderMainScreen(muted,tune,pattern,live,&editor);
    unsigned cuts=0;
    for(unsigned ch=0;ch<8;++ch) {
        const int left=30+53*ch,cx=left+25,cy=42;
        unsigned points=0,arms[4]{};
        for(int y=30;y<=54;++y) for(int x=left;x<=left+52;++x) {
            const auto& a=base.PixelData()[y*640+x];const auto& b=muted.PixelData()[y*640+x];
            const int dx=x-cx,dy=y-cy;
            const bool cross=std::abs(dx)==std::abs(dy) && std::abs(dx)<=6;
            if(cross) {
                ++points;
                if(dx) ++arms[(dx>0?0:2)+(dy>0?0:1)];
                const bool glyph=FixedGlyphCovers(char('1'+ch),x-left-23,y-38);
                CHECK_EQ(b.r,glyph?16:76);CHECK_EQ(b.g,glyph?16:195);CHECK_EQ(b.b,glyph?16:201);
                if(glyph && a.r!=16) ++cuts;
                // Four pixels or more from the selected frame and outer borders.
                CHECK(x>=left+6 && x<=left+46 && y>=35 && y<=48);
            } else CHECK(std::memcmp(&a,&b,sizeof(Color))==0);
        }
        CHECK_EQ(points,25);for(auto arm:arms) CHECK_EQ(arm,6);
    }
    CHECK(cuts>0);
    const auto& frame=muted.PixelData()[31*640+32];CHECK_EQ(frame.r,255);CHECK_EQ(frame.g,160);
    for(unsigned x=0;x<454;++x) CHECK(std::memcmp(base.PixelData()+54*640+x,muted.PixelData()+54*640+x,sizeof(Color))==0);
}

TEST_CASE(MuteCancelsDeferredOffAndRetainsExactReleaseTargetsAcrossPartitions) {
    using E=std::tuple<std::uint64_t,VariableRateVoiceAction,Note>;
    auto run=[](unsigned block) {
        RealtimePattern p;p.active_rows=4;p.active_channels=1;p.cells[0][0]={69,1};
        p.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};p.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
        p.cells[2][0]={72,kNoInstrumentUpdate};VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(30000,44100,p,SelectDevelopmentFixture({}).bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);std::int16_t out[128];std::vector<E> events;
        for(unsigned pos=0;pos<5000;) {
            if(pos==3000) CHECK_EQ(player.SetMuteMask(1),VariableRatePatternPlayerStatus::Success);
            if(pos==3500) CHECK_EQ(player.SetMuteMask(0),VariableRatePatternPlayerStatus::Success);
            unsigned n=std::min(block,5000-pos);if(pos<3000)n=std::min(n,3000-pos);else if(pos<3500)n=std::min(n,3500-pos);
            CHECK_EQ(player.Render(out,n),VariableRatePatternPlayerStatus::Success);auto b=player.TakeMidiCommands();
            for(unsigned i=0;i<b.count;++i) events.emplace_back(pos+b.commands[i].sample_offset,b.commands[i].action,b.commands[i].synth_note);
            pos+=n;
        }
        return events;
    };
    const auto a=run(128);CHECK(a==run(1));CHECK(a==run(97));CHECK_EQ(a.size(),3);
    CHECK_EQ(std::get<0>(a[1]),3000);CHECK_EQ(std::get<1>(a[1]),VariableRateVoiceAction::Release);
    CHECK_EQ(std::get<0>(a[2]),4410);CHECK_EQ(std::get<1>(a[2]),VariableRateVoiceAction::TriggerMidi);
}

TEST_CASE(MuteReleaseDoesNotBlanketDisableRendererOrTurnMidiSinkFaultIntoCoreError) {
    VariableRateVoiceBlockRenderer renderer;CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    std::int16_t sample[256];for(auto& x:sample)x=10000;
    std::int16_t storage[8][128];VariableRateVoiceOutputs out;for(unsigned c=0;c<8;++c)out.channels[c]=storage[c];
    VariableRateVoiceCommandBatch b;b.count=1;b.commands[0]={0,0,VariableRateVoiceAction::Trigger,{sample,256,44100}};
    CHECK_EQ(renderer.Render(out,64,b),VariableRateBlockStatus::Success);
    b.commands[0].action=VariableRateVoiceAction::Release;CHECK_EQ(renderer.Render(out,128,b),VariableRateBlockStatus::Success);
    CHECK_EQ(storage[0][127],0);
    b.commands[0].action=VariableRateVoiceAction::Trigger;CHECK_EQ(renderer.Render(out,64,b),VariableRateBlockStatus::Success);
    CHECK_EQ(storage[0][0],10000); // A future manual path can issue commands after release.
    using C=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    C control;auto f=SelectDevelopmentFixture({});f.pattern.cells[0][2]={69,1};
    CHECK_EQ(control.InitializeAudio(15500,44100,f.pattern,f.bindings),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);MidiOutput midi;midi.Lifecycle(true,1);
    std::int16_t pcm[128];control.AudioBlock(pcm,128,true);CollectMidiBlock(control,midi,0);midi.TransportFailure(true);
    CHECK_EQ(control.SubmitMute(4,1),PatternRequestStatus::Accepted);control.AudioBlock(pcm,128,true);CollectMidiBlock(control,midi,2903);
    CHECK(midi.Faulted());CHECK(control.Snapshot().running);CHECK_EQ(control.Snapshot().fault,PatternFault::None);
    CHECK_EQ(control.Snapshot().next_sample,256);CHECK_EQ(control.Snapshot().mute_mask,4);
}

TEST_CASE(MuteRejectedFrozenRequestPreservesIndependentLaterChannelToggles) {
    HostChannelMute h;auto rx=[&](const char* s){return h.Response(s,std::strlen(s));};
    CHECK(rx("BTMUTE1 CAP 1"));CHECK(rx("BTMUTE1 STATE 1 0 1 0"));
    CHECK(h.Toggle(0));CHECK(h.Begin());h.LineComplete();CHECK(h.Toggle(1));
    CHECK(rx("BTMUTE1 REJECTED 1 1"));CHECK(std::strstr(h.Status(),"rejected"));
    CHECK(rx("BTMUTE1 STATE 1 0 1 0"));CHECK(h.Begin());char line[96];std::size_t n=0;
    CHECK(h.FormatLine(line,sizeof(line),n));CHECK_EQ(std::string(line),"BTMUTE1 SET 1 2 2\n");
}
