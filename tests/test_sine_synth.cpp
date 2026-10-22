#include "test_framework.h"
#include "bringup/pattern_fixture.h"
#include "core/playback/variable_rate_pattern_player.h"
#include "core/note_formatter.h"
#include "ui/instrument_formatter.h"
#include <algorithm>
#include <array>
#include <vector>
using namespace BroTracker;
namespace {
VariableRateVoiceCommand Synth(unsigned channel,unsigned offset,unsigned note) {
    VariableRateVoiceCommand c; c.channel=channel; c.sample_offset=offset;
    c.action=VariableRateVoiceAction::TriggerSynth; c.synth_note=static_cast<Note>(note); return c;
}
struct Harness {
    VariableRateVoiceBlockRenderer renderer;
    std::int16_t pcm[8][2048]{};
    VariableRateVoiceOutputs outputs;
    Harness() { CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
        for(unsigned c=0;c<8;++c) outputs.channels[c]=pcm[c]; }
    void Render(unsigned frames) {
        Render(frames,VariableRateVoiceCommandBatch{});
    }
    void Render(unsigned frames,VariableRateVoiceCommandBatch batch) {
        CHECK_EQ(renderer.Render(outputs,frames,batch),VariableRateBlockStatus::Success);
    }
};
}
TEST_CASE(SynthTuningNoteConventionAndSafeInvalidInputs) {
    CHECK_EQ(SynthNoteFrequency(69),440.0);
    CHECK(std::abs(SynthNoteFrequency(24)-32.70319566)<0.00001);
    CHECK(std::abs(SynthNoteFrequency(127)-12543.853951)<0.00001);
    CHECK_EQ(FormatNote(69,AccidentalMode::Sharp),"A-3");
    for(unsigned n:{0u,23u,128u,254u,255u,256u,100000u}) CHECK_EQ(SynthNoteFrequency(n),0.0);
    SineSynthVoice voice; CHECK(!voice.Trigger(69)); CHECK(!voice.Configure(0));
    CHECK(!voice.Configure(24000)); CHECK(voice.Configure(44100));
    CHECK(!voice.Trigger(128)); CHECK(!voice.Trigger(256));
    for(unsigned n:{24u,69u,127u}) { CHECK(voice.Trigger(n));
        for(unsigned i=0;i<44100;++i) { const auto v=voice.Next(); CHECK(std::isfinite(v)); CHECK(std::abs(v)<=2048); }
        CHECK(!voice.Active()); CHECK_EQ(voice.Next(),0); }
}
TEST_CASE(SynthEnvelopeExactEndpointsDurationAndRelease) {
    for(unsigned rate:{32000u,44100u,48000u,96000u}) {
        SynthEnvelope e; e.Configure(rate); e.Trigger();
        const unsigned attack=(rate*15+500)/1000;
        float previous=0;
        for(unsigned i=0;i<rate;++i) { const auto value=e.Next();
            if(i==0 || i==rate-1) CHECK_EQ(value,0);
            if(i<attack) CHECK(value>=previous); else CHECK(value<=previous);
            if(i==attack-1) CHECK_EQ(value,1);
            CHECK(value>=0 && value<=1); previous=value;
        }
        CHECK(!e.Active()); CHECK_EQ(e.Next(),0);
        e.Trigger(); for(unsigned i=0;i<rate/5;++i) previous=e.Next();
        e.Release(); CHECK_EQ(e.Next(),previous);
        for(unsigned i=1;i<attack;++i) { const auto value=e.Next(); CHECK(value<=previous); previous=value; }
        CHECK_EQ(previous,0); CHECK(!e.Active()); CHECK_EQ(e.Next(),0);
        e.Trigger(); e.Release(); CHECK_EQ(e.Next(),0); e.Reset(); CHECK(!e.Active());
    }
}
TEST_CASE(SynthExactOffsetsAllChannelsAndIndependentOff) {
    Harness h; VariableRateVoiceCommandBatch batch;
    for(unsigned c=0;c<8;++c) batch.commands[batch.count++]=Synth(c,13,69);
    h.Render(2048,batch);
    for(unsigned c=0;c<8;++c) {
        for(unsigned i=0;i<=13;++i) CHECK_EQ(h.pcm[c][i],0);
        CHECK(h.pcm[c][700]!=0);
        for(unsigned i=0;i<2048;++i) CHECK_EQ(h.pcm[c][i],h.pcm[0][i]);
    }
    VariableRateVoiceCommand stop; stop.channel=3; stop.sample_offset=17; stop.action=VariableRateVoiceAction::Stop;
    batch=VariableRateVoiceCommandBatch(); batch.commands[batch.count++]=stop; h.Render(2048,batch);
    for(unsigned i=0;i<17;++i) CHECK_EQ(h.pcm[3][i],h.pcm[2][i]);
    for(unsigned i=17+662;i<2048;++i) CHECK_EQ(h.pcm[3][i],0);
    CHECK(h.pcm[2][900]!=0); CHECK_EQ(h.pcm[2][900],h.pcm[7][900]);
}
TEST_CASE(SynthRetriggerReplacementAndRapidTransitionsAreContinuousAndBounded) {
    Harness h; VariableRateVoiceCommandBatch batch; batch.commands[batch.count++]=Synth(0,0,69); h.Render(1500,batch);
    const auto before=h.pcm[0][1499];
    batch=VariableRateVoiceCommandBatch(); batch.commands[batch.count++]=Synth(0,0,72); batch.commands[batch.count++]=Synth(0,1,24);
    batch.commands[batch.count++]=Synth(0,2,127); h.Render(1000,batch);
    CHECK_EQ(h.pcm[0][0],before); CHECK_EQ(h.pcm[0][1],before); CHECK_EQ(h.pcm[0][2],before);
    for(unsigned i=0;i<1000;++i) CHECK(std::abs(int(h.pcm[0][i]))<=2048);
    const std::int16_t sample[]={10000,10000,10000,10000};
    const auto last=h.pcm[0][999]; batch=VariableRateVoiceCommandBatch(); batch.count=1;
    batch.commands[0]={0,0,VariableRateVoiceAction::Trigger,{sample,4,44100}};
    h.Render(128,batch); CHECK_EQ(h.pcm[0][0],last); CHECK_EQ(h.pcm[0][127],0);
    h.renderer.Reset(); batch.commands[0]={0,0,VariableRateVoiceAction::Trigger,{sample,4,44100}};
    h.Render(1,batch); CHECK_EQ(h.pcm[0][0],10000);
    batch.commands[0]=Synth(0,0,69); h.Render(128,batch); CHECK_EQ(h.pcm[0][0],10000);
    h.renderer.Reset(); h.Render(128);
    for(unsigned i=0;i<128;++i) CHECK_EQ(h.pcm[0][i],0);
}
TEST_CASE(SynthBlockPartitionIndependenceWithRetriggerOffAndEqualOffsets) {
    const unsigned times[]={7,850,851,1400,1400,2500};
    VariableRateVoiceCommand events[]={Synth(0,0,69),Synth(0,0,72),Synth(0,0,24),Synth(1,0,127),Synth(0,0,60),{0,0,VariableRateVoiceAction::Stop}};
    auto run=[&](unsigned block) {
        Harness h; std::vector<std::int16_t> result; unsigned next=0;
        for(unsigned position=0;position<5000;) {
            const auto frames=std::min(block,5000-position); VariableRateVoiceCommandBatch batch;
            while(next<6 && times[next]<position+frames) {
                auto c=events[next]; c.sample_offset=times[next++]-position; batch.commands[batch.count++]=c;
            }
            h.Render(frames,batch);
            for(unsigned i=0;i<frames;++i) for(unsigned c=0;c<8;++c) result.push_back(h.pcm[c][i]);
            position+=frames;
        } return result;
    };
    const auto reference=run(2048);
    for(unsigned block:{1u,7u,88u,128u,661u}) CHECK(reference==run(block));
}
TEST_CASE(SynthRendererRejectsInvalidRequestsTransactionally) {
    Harness h; VariableRateVoiceCommandBatch b; b.count=1; b.commands[0]=Synth(0,0,69); h.Render(1000,b);
    auto saved=h.renderer; const auto sentinel=h.pcm[0][0];
    for(unsigned note:{23u,128u,254u,255u}) {
        b.commands[0]=Synth(0,0,note); CHECK_EQ(h.renderer.Render(h.outputs,128,b),VariableRateBlockStatus::InvalidNote);
        CHECK_EQ(h.pcm[0][0],sentinel);
    }
    b.commands[0]=Synth(8,0,69); CHECK_EQ(h.renderer.Render(h.outputs,128,b),VariableRateBlockStatus::InvalidChannel);
    b.commands[0]=Synth(0,128,69); CHECK_EQ(h.renderer.Render(h.outputs,128,b),VariableRateBlockStatus::InvalidOffset);
    b.count=2; b.commands[0]=Synth(0,20,69); b.commands[1]=Synth(0,19,60);
    CHECK_EQ(h.renderer.Render(h.outputs,128,b),VariableRateBlockStatus::UnorderedOffsets);
    Harness expected; expected.renderer=saved; expected.Render(128); h.Render(128);
    for(unsigned i=0;i<128;++i) CHECK_EQ(h.pcm[0][i],expected.pcm[0][i]);
}
TEST_CASE(SynthFixtureIdentitySampleSelectionMidiSilenceAndTypeIndependentOfId) {
    const std::int16_t sample[]={1000,2000,3000,4000},other[]={-100,-200};
    const auto fixture=SelectDevelopmentFixture({sample,4,44100},{other,2,44100});
    CHECK_EQ(fixture.bindings.count,4);
    CHECK_EQ(FormatInstrument(0),"01"); CHECK_EQ(FormatInstrument(1),"02"); CHECK_EQ(FormatInstrument(2),"03");
    CHECK_EQ(fixture.pattern.cells[0][0].instrument,2); CHECK_EQ(fixture.pattern.cells[0][1].instrument,2);
    VariableRateSampleCommandPreparer prepare; CHECK_EQ(prepare.Configure(44100,fixture.bindings),VariableSampleCommandStatus::Success);
    RowEventBatch rows; rows.count=2; rows.events[0]={0,0,0,0,0,0,60,0}; rows.events[1]={7,0,0,0,0,0,69,2};
    VariableRateVoiceCommandBatch commands; CHECK_EQ(prepare.Prepare(rows,commands),VariableSampleCommandStatus::Success);
    CHECK_EQ(commands.commands[0].sample.data,sample); CHECK_EQ(commands.commands[1].action,VariableRateVoiceAction::TriggerSynth);
    Harness h; h.Render(1500,commands); CHECK_EQ(h.pcm[0][0],1000); CHECK(h.pcm[7][700]!=0);
    rows.count=1; rows.events[0]={7,0,0,0,0,0,69,1};
    CHECK_EQ(prepare.Prepare(rows,commands),VariableSampleCommandStatus::Success);
    CHECK_EQ(commands.commands[0].action,VariableRateVoiceAction::TriggerMidi); h.Render(128,commands);
    for(unsigned i=0;i<128;++i) CHECK_EQ(h.pcm[7][i],0);
    // Another project can bind Synth at an arbitrary identity.
    auto bindings=fixture.bindings; bindings.bindings[1].instrument=42;
    CHECK_EQ(prepare.Configure(44100,bindings),VariableSampleCommandStatus::Success);
    rows.events[0].instrument=42; CHECK_EQ(prepare.Prepare(rows,commands),VariableSampleCommandStatus::Success);
    CHECK_EQ(commands.commands[0].action,VariableRateVoiceAction::TriggerSynth);
}
TEST_CASE(SynthTransportStopRestartAndPauseClearAudio) {
    const auto fixture=SelectDevelopmentFixture({}); VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(12753,44100,fixture.pattern,fixture.bindings),VariableRatePatternPlayerStatus::Success);
    std::int16_t pcm[128]; CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::vector<std::int16_t> first;
    for(unsigned b=0;b<10;++b) { CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success); first.insert(first.end(),pcm,pcm+128); }
    CHECK(std::any_of(first.begin(),first.end(),[](auto v){return v!=0;}));
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success); for(auto v:pcm) CHECK_EQ(v,0);
    player.Stop(); CHECK_EQ(player.GetNextSamplePosition(),0); CHECK(!player.GetPlaybackPosition().valid);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    for(unsigned b=0;b<10;++b) { CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);
        for(unsigned i=0;i<128;++i) CHECK_EQ(pcm[i],first[b*128+i]); }
    player.Stop(); CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success); for(auto v:pcm) CHECK_EQ(v,0);
}

TEST_CASE(SynthPatternSchedulingOffsetsAndPartitionsRemainExact) {
    auto fixture=SelectDevelopmentFixture({});
    RealtimePattern pattern; pattern.active_rows=4; pattern.active_channels=8;
    pattern.cells[1][0]={69,2}; pattern.cells[2][0]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[3][0]={60,2};
    auto run=[&](unsigned block) {
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,44100,pattern,fixture.bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> result; std::int16_t pcm[128];
        for(unsigned position=0;position<20000;) {
            const auto count=std::min(block,20000-position);
            CHECK_EQ(player.Render(pcm,count),VariableRatePatternPlayerStatus::Success);
            result.insert(result.end(),pcm,pcm+count); position+=count;
        } return result;
    };
    const auto reference=run(128); CHECK(reference==run(1)); CHECK(reference==run(97));
    std::uint64_t onset=0,off=0;
    CHECK_EQ(TickToSamplePosition(kTicksPerRow,12753,44100,onset),TickToSampleStatus::Success);
    CHECK_EQ(TickToSamplePosition(2*kTicksPerRow,12753,44100,off),TickToSampleStatus::Success);
    CHECK(std::all_of(reference.begin(),reference.begin()+onset+1,[](auto v){return v==0;}));
    CHECK(reference[onset+700]!=0);
    CHECK(std::all_of(reference.begin()+off+662,reference.begin()+off+1000,[](auto v){return v==0;}));
}

TEST_CASE(SynthBindingsRejectInvalidTypesRatesDuplicatesAndMissingSampleWithoutFallback) {
    const std::int16_t pcm[]={1234};
    for(const auto view:{Pcm16MonoView{},Pcm16MonoView{pcm,1,48000}}) {
        const auto fixture=SelectDevelopmentFixture(view); CHECK_EQ(fixture.bindings.count,2);
        VariableRateSampleCommandPreparer p;
        CHECK_EQ(p.Configure(44100,fixture.bindings),VariableSampleCommandStatus::Success);
        RowEventBatch rows; rows.count=1; rows.events[0]={0,0,0,0,0,0,60,0};
        VariableRateVoiceCommandBatch commands; commands.count=7;
        CHECK_EQ(p.Prepare(rows,commands),VariableSampleCommandStatus::UnknownInstrument); CHECK_EQ(commands.count,7);
    }
    auto bindings=SelectDevelopmentFixture({pcm,1,44100}).bindings;
    VariableRateSampleCommandPreparer p;
    CHECK_EQ(p.Configure(44100,bindings),VariableSampleCommandStatus::Success);
    CHECK_EQ(p.Configure(24000,bindings),VariableSampleCommandStatus::InvalidSampleRate);
    bindings.bindings[1].instrument=bindings.bindings[0].instrument;
    CHECK_EQ(p.Configure(44100,bindings),VariableSampleCommandStatus::DuplicateInstrument);
    bindings=SelectDevelopmentFixture({}).bindings;
    bindings.bindings[0].type=static_cast<InstrumentType>(255);
    CHECK_EQ(p.Configure(44100,bindings),VariableSampleCommandStatus::InvalidSample);
    RowEventBatch rows; rows.count=1; rows.events[0]={0,0,0,0,0,0,60,0};
    VariableRateVoiceCommandBatch commands;
    CHECK_EQ(p.Prepare(rows,commands),VariableSampleCommandStatus::Success); // Previous valid table retained.
    CHECK_EQ(commands.commands[0].sample.data,pcm);
}
