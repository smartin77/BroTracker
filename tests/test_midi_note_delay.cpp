#include "test_framework.h"
#include "bringup/midi_output.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_fixture.h"
#include <vector>
using namespace BroTracker;
namespace {
VariableRateVoiceCommandBatch MidiEvent(unsigned channel,Note note,unsigned offset=0,
    VariableRateVoiceAction action=VariableRateVoiceAction::TriggerMidi) {
    VariableRateVoiceCommandBatch b;b.count=1;b.commands[0]={channel,offset,action,{},kSourceFrameStepUnity,note};return b;
}
struct Harness {
    MidiOutput midi;std::vector<std::uint32_t> words;
    Harness(unsigned ms=0,AudioOutputChoice route=AudioOutputChoice::Bridge) {
        midi.Lifecycle(true,1);midi.TakeAbortRequest();Service(0);
        CHECK_EQ(midi.SetNoteDelayMs(ms,route,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Applied);
    }
    void Service(std::uint32_t now,bool complete=true) {
        midi.Service(now,[&](const std::uint32_t* w,unsigned n){CHECK(n<=16);words.insert(words.end(),w,w+n);return true;});
        if(complete)midi.ConfirmTransfers();
    }
};
using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
void Initialize(Control& c) {
    auto fixture=SelectDevelopmentFixture({});fixture.pattern={};fixture.pattern.active_rows=4;fixture.pattern.active_channels=8;
    fixture.pattern.cells[0][2]={69,1};
    CHECK_EQ(c.InitializeAudio(12500,44100,fixture.pattern,fixture.bindings),PatternPlayerStatus::Success);
    CHECK_EQ(c.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
}
}
TEST_CASE(MidiFixedDelayIsExactMillisecondsWithExistingFractionalSampleFloor) {
    for(unsigned ms:{0u,1u,7u,40u,82u,120u,160u}) {
        Harness h(ms);const auto delay=ms*1000u;
        CHECK_EQ(h.midi.NoteDelayUs(),delay);
        CHECK(h.midi.Enqueue(MidiEvent(0,69,1),1000,100)); // floor(1e6/44100)=22us
        h.Service(121+delay);CHECK(h.words.empty());h.Service(122+delay);
        CHECK_EQ(h.words.size(),1u);CHECK_EQ(h.words[0],h.midi.Pack(true,69));
        CHECK(h.midi.Enqueue(MidiEvent(0,0,44,VariableRateVoiceAction::Stop),1128,3000));
        h.Service(3996+delay);CHECK_EQ(h.words.size(),1u);h.Service(3997+delay);
        CHECK_EQ(h.words.size(),2u);CHECK_EQ(h.words[1],h.midi.Pack(false,69));
        CHECK_EQ(h.midi.counters.max_lateness_us,0u);
    }
    Harness wrap(160);CHECK(wrap.midi.Enqueue(MidiEvent(0,69,44),0,UINT32_MAX-500));
    wrap.Service(160495);CHECK(wrap.words.empty());wrap.Service(160496);CHECK_EQ(wrap.words.size(),1u);
    CHECK(wrap.midi.Enqueue(MidiEvent(0,0,44,VariableRateVoiceAction::Stop),128,UINT32_MAX-400));
    wrap.Service(160595);CHECK_EQ(wrap.words.size(),1u);wrap.Service(160596);
    CHECK_EQ(wrap.words.size(),2u);CHECK_EQ(wrap.words.back(),wrap.midi.Pack(false,69));
}
TEST_CASE(MidiFixedDelayPreservesSameNoteRetriggerDurationAndOrder) {
    Harness zero,delayed(11);
    const auto run=[](Harness& h,unsigned shift) {
        const auto on=MidiEvent(0,69),off=MidiEvent(0,0,0,VariableRateVoiceAction::Stop);
        CHECK(h.midi.Enqueue(on,0,100));CHECK(h.midi.Enqueue(off,128,2100));
        CHECK(h.midi.Enqueue(on,256,2100));CHECK(h.midi.Enqueue(off,384,4100));
        for(unsigned due:{100u,2100u,4100u})h.Service(due+shift);
    };
    run(zero,0);run(delayed,11000);CHECK(zero.words==delayed.words);CHECK_EQ(zero.words.size(),4u);
    CHECK_EQ(delayed.words[1],delayed.midi.Pack(false,69));CHECK_EQ(delayed.words[2],delayed.midi.Pack(true,69));
}
TEST_CASE(MidiDelaySettingRejectsUnsafeChangesAndOtherRoutesRemainUndelayed) {
    Harness h(4);
    CHECK_EQ(h.midi.SetNoteDelayMs(161,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Invalid);
    CHECK_EQ(h.midi.SetNoteDelayMs(UINT32_MAX,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Invalid);
    CHECK_EQ(h.midi.SetNoteDelayMs(1,AudioOutputChoice::Default,AudioInitialization::Uninitialized,true),MidiOutput::DelaySetting::AudioNotReady);
    CHECK_EQ(h.midi.SetNoteDelayMs(1,AudioOutputChoice::Bridge,AudioInitialization::Initialized,false),MidiOutput::DelaySetting::NotStopped);
    CHECK_EQ(h.midi.SetNoteDelayMs(1,AudioOutputChoice::Bridge,AudioInitialization::Failed,true),MidiOutput::DelaySetting::AudioNotReady);
    CHECK_EQ(h.midi.NoteDelayUs(),4000u);
    CHECK(h.midi.Enqueue(MidiEvent(0,69),0,0));
    CHECK_EQ(h.midi.SetNoteDelayMs(0,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Pending);
    h.Service(4000);CHECK_EQ(h.midi.SetNoteDelayMs(0,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Cleanup);
    h.midi.Reset();CHECK_EQ(h.midi.SetNoteDelayMs(0,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Cleanup);
    h.midi.TakeAbortRequest();h.Service(4001);
    CHECK_EQ(h.midi.SetNoteDelayMs(2,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Applied);
    CHECK_EQ(h.midi.NoteDelayUs(),2000u);
    for(auto route:{AudioOutputChoice::Pt8211,AudioOutputChoice::AudioShield}) {
        Harness direct(160,route);CHECK_EQ(direct.midi.NoteDelayUs(),0u);
        CHECK(direct.midi.Enqueue(MidiEvent(0,69),0,0));direct.Service(0);CHECK_EQ(direct.words.size(),1u);
    }
}
TEST_CASE(MidiDelayedQueueIsBoundedAndOverflowFaultDropsBacklog) {
    Harness h(160);for(unsigned i=0;i<MidiOutput::Capacity;++i)CHECK(h.midi.Enqueue(MidiEvent(i%8,69),i,0));
    h.Service(159999);CHECK(h.words.empty());
    h.Service(160000);CHECK_EQ(h.midi.Queued(),MidiOutput::Capacity-MidiOutput::ServiceBudget);
    Harness full(160);for(unsigned i=0;i<MidiOutput::Capacity;++i)CHECK(full.midi.Enqueue(MidiEvent(0,69),i,0));
    CHECK(!full.midi.Enqueue(MidiEvent(0,70),MidiOutput::Capacity,0));CHECK(full.midi.Faulted());
    CHECK_EQ(full.midi.Queued(),0u);full.Service(160000);CHECK(full.words.empty());
}
TEST_CASE(MidiMaximumDelaySustainsConservativeProductionBurstBound) {
    Harness h(160);unsigned now=0,peak=0;
    for(unsigned block=0;block<200;++block) {
        const auto render=static_cast<unsigned>(std::uint64_t(block)*128*1000000/44100);
        while(now<render){h.Service(now);now+=100;}
        VariableRateVoiceCommandBatch batch;batch.count=kVariableRateVoiceCommandCapacity;
        for(unsigned i=0;i<batch.count;++i)batch.commands[i]={i%8,0,VariableRateVoiceAction::TriggerMidi,{},kSourceFrameStepUnity,Note(60+i%8)};
        CHECK(h.midi.Enqueue(batch,std::uint64_t(block)*128,render));
        peak=std::max(peak,h.midi.Queued());CHECK(!h.midi.Faulted());
    }
    CHECK(peak>=56*16);CHECK(peak<=MidiOutput::Capacity);
    while(now<800000){h.Service(now);now+=100;}
    CHECK(!h.midi.Faulted());CHECK_EQ(h.midi.Queued(),0u);CHECK(h.words.size()>0);
}
TEST_CASE(MidiLargeDelayedCancellationRetainsOtherChannelsAndExactEventOrder) {
    Harness h(160);std::vector<unsigned> due;std::vector<std::uint32_t> expected;
    int last[3]={-1,-1,-1};unsigned survivors=0;
    // 56 production-sized bursts: 896 entries, substantially more than the old capacity.
    for(unsigned i=0;i<56*16;++i) {
        const auto channel=i%3,offset=i%16;
        const auto note=Note(channel==0?100+i%12:channel==1?60+i%12:80+i%12);
        const auto render=unsigned(std::uint64_t(i/16)*128*1000000/44100);
        CHECK(h.midi.Enqueue(MidiEvent(channel,note,offset),std::uint64_t(i/16)*128,render));
        if(channel) {
            ++survivors;due.push_back(render+offset*1000000/44100+160000);
            if(last[channel]>=0)expected.push_back(h.midi.Pack(false,last[channel]));
            expected.push_back(h.midi.Pack(true,note));last[channel]=note;
        }
    }
    CHECK_EQ(h.midi.Queued(),896u);h.midi.CancelChannels(1);CHECK_EQ(h.midi.Queued(),survivors);
    h.Service(159999);CHECK(h.words.empty());
    for(auto time:due)h.Service(time);
    CHECK(h.words==expected);CHECK_EQ(h.midi.Queued(),0u);CHECK(!h.midi.Faulted());
    h.Service(500000);CHECK(h.words==expected); // No cancelled ON reappears.
}
TEST_CASE(MidiDelayedFaultAndLifecycleCancelPendingOnsAndReleaseSubmittedNotesPromptly) {
    for(bool disconnect:{false,true})for(bool delivered:{false,true}) {
        Harness h(20);CHECK(h.midi.Enqueue(MidiEvent(0,69),0,100));if(delivered)h.Service(20100);
        CHECK(h.midi.Enqueue(MidiEvent(1,72),128,21000));const auto before=h.words.size();
        if(disconnect){h.midi.Lifecycle(false,1);h.midi.Lifecycle(true,2);}else h.midi.TransportFailure();
        h.midi.TakeAbortRequest();h.Service(21100);
        CHECK_EQ(h.words.size(),before+(delivered?1u:0u));
        if(delivered)CHECK_EQ(h.words.back(),h.midi.Pack(false,69));
        const auto n=h.words.size();h.Service(50000);CHECK_EQ(h.words.size(),n);
        h.midi.CancelChannels(255);h.Service(50100);CHECK(h.midi.StorageReady(true));
    }
}
TEST_CASE(MidiMuteDropsOnlyAffectedPendingNotesAndBypassesDelayForCleanup) {
    Harness h(20);CHECK(h.midi.Enqueue(MidiEvent(0,69),0,0));CHECK(h.midi.Enqueue(MidiEvent(1,72),0,0));
    h.midi.CancelChannels(1);h.Service(0);h.Service(20000);
    CHECK_EQ(h.words.size(),1u);CHECK_EQ(h.words.back(),h.midi.Pack(true,72));
    CHECK(h.midi.Enqueue(MidiEvent(1,74),128,20100));h.midi.CancelChannels(2);
    CHECK_EQ(h.midi.SetNoteDelayMs(0,AudioOutputChoice::Bridge,AudioInitialization::Initialized,true),MidiOutput::DelaySetting::Pending);
    h.midi.Service(20101,[&](const std::uint32_t* w,unsigned n){CHECK_EQ(n,1u);CHECK_EQ(w[0],h.midi.Pack(false,72));return false;});
    CHECK_EQ(h.midi.counters.busy,1u);CHECK_EQ(h.words.size(),1u);h.Service(20102);
    CHECK_EQ(h.words.back(),h.midi.Pack(false,72));CHECK_EQ(h.midi.Queued(),0u);
    const auto size=h.words.size();h.Service(50000);CHECK_EQ(h.words.size(),size);
    Harness shared;CHECK(shared.midi.Enqueue(MidiEvent(0,69),0,0));CHECK(shared.midi.Enqueue(MidiEvent(1,69),0,0));shared.Service(0);
    shared.midi.CancelChannels(1);shared.Service(1);CHECK_EQ(shared.words.size(),1u);
    shared.midi.CancelChannels(2);shared.Service(2);CHECK_EQ(shared.words.size(),2u);
}
TEST_CASE(MidiRealControlStopPauseMuteAndProbeRollbackNeverLeakDelayedOns) {
    for(auto action:{PatternRequest::Stop,PatternRequest::Pause})for(bool delivered:{false,true}) {
        Control control;Initialize(control);Harness h(20);std::int16_t pcm[128];
        control.AudioBlock(pcm,128,true);CollectMidiBlock(control,h.midi,0);h.midi.TakeAbortRequest();h.Service(0);
        if(delivered)h.Service(20000);const auto before=h.words.size();
        CHECK_EQ(control.Submit(action),PatternRequestStatus::Accepted);
        const auto cancelled_at=delivered?20100u:2903u;
        control.AudioBlock(pcm,128,true);CollectMidiBlock(control,h.midi,cancelled_at);h.midi.TakeAbortRequest();h.Service(cancelled_at);
        CHECK_EQ(h.words.size(),before+(delivered?1u:0u));
        for(std::size_t i=before;i<h.words.size();++i)CHECK_EQ((h.words[i]>>8)&0xf0u,0x80u);
        const auto size=h.words.size();h.Service(50000);CHECK_EQ(h.words.size(),size);
        if(action==PatternRequest::Stop) {
            CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
            control.AudioBlock(pcm,128,true);CollectMidiBlock(control,h.midi,60000);h.midi.TakeAbortRequest();h.Service(60000);
            h.Service(79999);CHECK_EQ(h.words.size(),size);h.Service(80000);CHECK(h.words.size()>size);
        }
    }
    for(bool delivered:{false,true}) {
        Control control;Initialize(control);Harness h(20);std::int16_t pcm[128];
        control.AudioBlock(pcm,128,true);CollectMidiBlock(control,h.midi,0);h.midi.TakeAbortRequest();h.Service(0);
        if(delivered)h.Service(20000);const auto before=h.words.size();
        CHECK_EQ(control.SubmitMute(4,1),PatternRequestStatus::Accepted);
        const auto cancelled_at=delivered?20100u:2903u;
        control.AudioBlock(pcm,128,true);CollectMidiBlock(control,h.midi,cancelled_at);h.Service(cancelled_at);
        CHECK_EQ(h.words.size(),before+(delivered?1u:0u));
        const auto size=h.words.size();h.Service(50000);CHECK_EQ(h.words.size(),size);
    }
    Control actual;Initialize(actual);Harness h(20);std::int16_t pcm[128];
    auto discarded=actual;discarded.AudioBlock(pcm,128,true); // Probe never collected.
    actual.AudioBlock(pcm,128,true);CollectMidiBlock(actual,h.midi,0);
    CHECK_EQ(h.midi.Queued(),1u);CollectMidiBlock(actual,h.midi,0);CHECK_EQ(h.midi.Queued(),1u);
    h.midi.TakeAbortRequest();h.Service(0);h.Service(20000);CHECK_EQ(h.words.size(),1u);
    auto fixture=SelectDevelopmentFixture({});fixture.pattern={};fixture.pattern.active_rows=4;fixture.pattern.active_channels=8;
    fixture.pattern.cells[0][0]={69,1};fixture.pattern.cells[0][1]={60,127}; // Valid syntax, missing binding at render.
    VariableRatePatternPlayer failed;
    CHECK_EQ(failed.Configure(12500,44100,fixture.pattern,fixture.bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(failed.Start(),VariableRatePatternPlayerStatus::Success);failed.TakeMidiReset();
    CHECK_EQ(failed.SetMuteMask(4),VariableRatePatternPlayerStatus::Success);
    CHECK(failed.Render(pcm,128)!=VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(failed.GetNextSamplePosition(),0u);CHECK_EQ(failed.TakeMidiCommands().count,0u);
    CHECK_EQ(failed.TakeMidiCancelMask(),0u); // No committed export from a partial/probe render.
}
