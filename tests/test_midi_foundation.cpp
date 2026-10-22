#include "test_framework.h"
#include "bringup/midi_output.h"
#include "bringup/pattern_fixture.h"
#include "core/playback/variable_rate_pattern_player.h"
#include "ui/host_config.h"
#include <vector>
#include <tuple>
using namespace BroTracker;
namespace {
VariableRateVoiceCommandBatch Command(unsigned tracker, Note note, std::uint64_t offset=0,
    VariableRateVoiceAction action=VariableRateVoiceAction::TriggerMidi) {
    VariableRateVoiceCommandBatch b; b.count=1;
    b.commands[0].channel=tracker; b.commands[0].synth_note=note;
    b.commands[0].sample_offset=offset; b.commands[0].action=action; return b;
}
struct MidiHarness {
    MidiOutput output;
    std::vector<std::uint32_t> words;
    bool busy=false,complete=true;
    MidiHarness() { output.Lifecycle(true,1); Service(0); }
    void Service(std::uint32_t now) { output.Service(now,[&](const std::uint32_t* w,unsigned n) {
        CHECK(n<=16); if(busy) return false; words.insert(words.end(),w,w+n); return true;
    }); if(complete && !busy) output.ConfirmTransfers(); }
    void Send(unsigned tracker,Note note,VariableRateVoiceAction action=VariableRateVoiceAction::TriggerMidi) {
        CHECK(output.Enqueue(Command(tracker,note,0,action),0,0)); Service(0);
    }
};
}
TEST_CASE(MidiConfigurationDecimalFallbackAndExplicitPrecedence) {
    const std::string header="[btx]\nschema_version=1\nnew_pattern_length=32\n[midi]\noutput_channel=";
    for(unsigned channel=1;channel<=16;++channel) {
        HostConfig c; CHECK_EQ(ParseHostConfig(header+std::to_string(channel),c),HostConfigStatus::Loaded);
        CHECK_EQ(c.midi_output_channel,channel); CHECK(!c.midi_output_invalid);
    }
    for(const auto value:{"","0","17","-1","12.0","abc","4294967296"}) {
        HostConfig c; CHECK_EQ(ParseHostConfig(header+value,c),HostConfigStatus::Loaded);
        CHECK_EQ(c.midi_output_channel,12); CHECK_EQ(c.midi_output_invalid,std::string(value)!="");
    }
    HostConfig c; CHECK_EQ(c.midi_output_channel,12);
    CHECK_EQ(ParseHostConfig(header+"1\noutput_channel=2",c),HostConfigStatus::Invalid);
    MidiOutput out; CHECK_EQ(out.Channel(),12); CHECK(!out.Activated());
    CHECK_EQ(out.Select(1),MidiOutput::Selection::Selected);
    CHECK_EQ(out.Select(1),MidiOutput::Selection::Selected);
    CHECK_EQ(out.Select(16),MidiOutput::Selection::RestartRequired); CHECK_EQ(out.Channel(),1);
    CHECK_EQ(out.Select(0),MidiOutput::Selection::Invalid);
    for(unsigned ch=1;ch<=16;++ch) { MidiOutput m; CHECK_EQ(m.Select(ch),MidiOutput::Selection::Selected);
        CHECK_EQ(m.Pack(true,69),9u|((0x90u+ch-1)<<8)|(69u<<16)|(100u<<24)); }
}
TEST_CASE(MidiRawPitchRetriggerDuplicateOwnershipAndReplacement) {
    MidiHarness h; h.Send(0,69); CHECK_EQ(h.words.size(),1); CHECK_EQ(h.words[0],0x64459b09u);
    h.Send(0,69); CHECK_EQ(h.words.size(),3); CHECK_EQ(h.words[1],0x00458b08u);
    h.Send(1,69); CHECK_EQ(h.words.size(),3); // Shared note holds, no ambiguous stack.
    h.Send(0,69); CHECK_EQ(h.words.size(),3); // Shared retrigger cannot release peer.
    h.Send(0,72); CHECK_EQ(h.words.size(),4); CHECK_EQ(h.words.back(),h.output.Pack(true,72));
    h.Send(1,0,VariableRateVoiceAction::Stop); CHECK_EQ(h.words.back(),h.output.Pack(false,69));
    h.Send(0,60,VariableRateVoiceAction::TriggerSynth); CHECK_EQ(h.words.back(),h.output.Pack(false,72));
    h.Send(2,70); h.Send(2,60,VariableRateVoiceAction::Trigger); CHECK_EQ(h.words.back(),h.output.Pack(false,70));
    h.output.Reset(); h.Service(0); CHECK(!h.output.PendingCleanup());
}
TEST_CASE(MidiDeadlinesCapacityBusyRetryAndClockWrap) {
    MidiHarness h; CHECK(h.output.Enqueue(Command(0,69,44),123456,100));
    h.Service(1096); CHECK(h.words.empty()); h.Service(1097); CHECK_EQ(h.words.size(),1);
    h.busy=true; CHECK(h.output.Enqueue(Command(0,70),123500,2000));
    h.Service(2000); CHECK_EQ(h.output.Queued(),1); CHECK_EQ(h.output.counters.busy,1);
    h.Service(2000+MidiOutput::DeadlineUs+1); CHECK(h.output.Faulted()); CHECK_EQ(h.output.counters.late,1);
    CHECK(h.output.PendingCleanup()); h.Service(10000); CHECK(h.output.PendingCleanup());
    h.busy=false; h.Service(10100); CHECK_EQ(h.words.back(),h.output.Pack(false,69)); CHECK(!h.output.PendingCleanup());
    CHECK(!h.output.Enqueue(Command(0,70),0,0));
    MidiHarness full; for(unsigned i=0;i<64;++i) CHECK(full.output.Enqueue(Command(0,69),i,0));
    CHECK_EQ(full.output.Queued(),64); CHECK(!full.output.Enqueue(Command(0,69),64,0));
    CHECK(full.output.Faulted()); CHECK_EQ(full.output.counters.overflow,1);
    MidiHarness bounded; for(unsigned i=0;i<16;++i) CHECK(bounded.output.Enqueue(Command(0,69),i,0));
    bounded.Service(0); CHECK_EQ(bounded.output.Queued(),8); CHECK_EQ(bounded.words.size(),15);
    MidiHarness wrap; CHECK(wrap.output.Enqueue(Command(0,69,44),0,UINT32_MAX-500));
    wrap.Service(495); CHECK(wrap.words.empty()); wrap.Service(496); CHECK_EQ(wrap.words.size(),1);
}
TEST_CASE(MidiTransportCleanupDisconnectAndReconfigurationUnknownDelivery) {
    MidiHarness h; h.Send(0,69); h.Send(1,69); h.Send(2,72);
    h.output.Reset(); h.Service(0); CHECK_EQ(h.words.size(),4); // Two distinct OFFs only.
    CHECK_EQ(h.output.counters.cleanup_submitted,1);
    h.Send(0,69); h.output.Lifecycle(false,1); h.Service(10);
    CHECK(h.output.Faulted()); CHECK(h.output.PendingCleanup()); CHECK_EQ(h.output.counters.disconnect,1);
    const auto n=h.words.size(); h.Service(20000); CHECK_EQ(h.words.size(),n);
    h.output.Lifecycle(true,2); h.Service(20000); CHECK_EQ(h.words.size(),n+1);
    CHECK_EQ(h.words.back(),h.output.Pack(false,69)); // Best effort, no delivery claim.
    MidiHarness queued; CHECK(queued.output.Enqueue(Command(0,69),0,100));
    queued.output.Lifecycle(true,2); queued.Service(100); CHECK(queued.words.empty()); CHECK(queued.output.Faulted());
}
TEST_CASE(MidiEngineExactSamplesDeferredOffTempoLoopsAndPartitionIndependence) {
    using Event=std::tuple<std::uint64_t,unsigned,unsigned,VariableRateVoiceAction>;
    auto run=[](unsigned block) {
        RealtimePattern p; p.active_rows=4; p.active_channels=2;
        p.cells[0][0]={69,1}; p.cells[0][1]={69,1};
        p.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate}; p.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
        p.cells[2][0]={72,1}; p.cells[2][1]={NOTE_EMPTY,2};
        p.cells[3][0]={60,2}; p.cells[3][1]={NOTE_OFF,kNoInstrumentUpdate};
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12500,44100,p,SelectDevelopmentFixture({}).bindings),VariableRatePatternPlayerStatus::Success);
        CHECK(player.TakeMidiReset()); CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK(player.TakeMidiReset());
        std::vector<Event> result; std::int16_t pcm[128];
        for(unsigned pos=0;pos<50000;) {
            if(pos==4000) CHECK_EQ(player.RequestTempo(25000),VariableRatePatternPlayerStatus::Success);
            auto frames=std::min(block,50000-pos); if(pos<4000) frames=std::min(frames,4000-pos);
            const auto base=player.GetNextSamplePosition();
            CHECK_EQ(player.Render(pcm,frames),VariableRatePatternPlayerStatus::Success);
            const auto batch=player.TakeMidiCommands();
            for(unsigned i=0;i<batch.count;++i) {
                const auto& c=batch.commands[i]; result.emplace_back(base+c.sample_offset,c.channel,c.synth_note,c.action);
            }
            CHECK_EQ(player.TakeMidiCommands().count,0); pos+=frames;
        }
        CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); CHECK(player.TakeMidiReset());
        CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);
        player.Stop(); CHECK(player.TakeMidiReset()); CHECK_EQ(player.TakeMidiCommands().count,0);
        return result;
    };
    const auto reference=run(128); CHECK(reference==run(1)); CHECK(reference==run(97));
    CHECK(reference.size()>20); CHECK_EQ(std::get<0>(reference[0]),0); CHECK_EQ(std::get<2>(reference[0]),69);
    // Tempo applies row 1 at old 5292; row 2 follows new 2646-sample row.
    // Its deferred OFF precedes its pitched replacement at exactly 7938.
    CHECK_EQ(std::get<0>(reference[2]),7938); CHECK_EQ(std::get<3>(reference[2]),VariableRateVoiceAction::Stop);
    CHECK_EQ(std::get<0>(reference[3]),7938); CHECK_EQ(std::get<2>(reference[3]),72);
}
TEST_CASE(MidiAdapterRejectsMalformedAndBackwardEventsAndLatchesFailures) {
    for(unsigned error=0;error<5;++error) {
        MidiHarness h; auto b=Command(0,69);
        if(error==0) b.commands[0].channel=8;
        if(error==1) b.commands[0].synth_note=128;
        if(error==2) b.commands[0].sample_offset=128;
        if(error==3) b.commands[0].action=static_cast<VariableRateVoiceAction>(255);
        if(error==4) b.commands[0].sample_offset=1;
        CHECK(!h.output.Enqueue(b,error==4?UINT64_MAX:0,0)); CHECK(h.output.Faulted()); CHECK(h.words.empty());
    }
    MidiHarness order; CHECK(order.output.Enqueue(Command(0,69),100,0));
    CHECK(!order.output.Enqueue(Command(0,70),99,0)); CHECK(order.output.Faulted());
    MidiHarness failure; failure.Send(0,69); failure.output.TransportFailure(); failure.output.TransportFailure();
    CHECK_EQ(failure.output.counters.transport_failure,1); CHECK(failure.output.Faulted());
    failure.Service(0); CHECK_EQ(failure.words.back(),failure.output.Pack(false,69));
    MidiOutput offline; CHECK(!offline.Enqueue(Command(0,69),0,0)); CHECK(offline.Faulted());
}
TEST_CASE(MidiFailedFinalOffRetainsUncertainPitchUntilDmaCompletion) {
    MidiHarness h; h.Send(0,69); // Normal ON completed.
    h.complete=false; h.Send(0,0,VariableRateVoiceAction::Stop); // OFF only accepted.
    CHECK_EQ(h.words.back(),h.output.Pack(false,69));
    h.output.TransportFailure(); h.Service(0); // Retry targeted OFF after failure.
    CHECK(h.output.PendingCleanup()); CHECK_EQ(h.words.size(),3);
    h.complete=true; h.Service(0); CHECK(!h.output.PendingCleanup()); CHECK(h.output.Faulted());
    MidiHarness unplug; unplug.Send(0,72); unplug.complete=false;
    unplug.Send(0,0,VariableRateVoiceAction::Stop);
    unplug.output.Lifecycle(false,1); CHECK(unplug.output.Faulted());
    unplug.output.Lifecycle(true,2); unplug.Service(0);
    CHECK_EQ(unplug.words.size(),3); CHECK_EQ(unplug.words.back(),unplug.output.Pack(false,72));
    // Completion retires old pitches so future cleanup does not release history.
    MidiHarness retired; retired.Send(0,69); retired.Send(0,0,VariableRateVoiceAction::Stop);
    retired.output.Reset(); retired.Service(0); CHECK_EQ(retired.words.size(),2);
}

TEST_CASE(MidiFailureRecoveryDropsBacklogAndKeepsUncertainOwnershipAcrossDisconnect) {
    for(bool overflow:{false,true}) {
        MidiHarness h; h.Send(0,69); h.output.TakeAbortRequest();
        if(overflow) {
            for(unsigned i=0;i<MidiOutput::Capacity;++i) CHECK(h.output.Enqueue(Command(1,70),i,0));
            CHECK(!h.output.Enqueue(Command(1,71),64,0));
        } else {
            CHECK(h.output.Enqueue(Command(1,70),1,0)); h.busy=true;
            h.Service(MidiOutput::DeadlineUs+1);
        }
        CHECK(h.output.Faulted()); CHECK_EQ(h.output.Queued(),0);
        CHECK(h.output.TakeAbortRequest()); CHECK(!h.output.Recover(false));
        h.output.Lifecycle(false,3); const auto n=h.words.size(); h.Service(10000);
        CHECK_EQ(h.words.size(),n); CHECK(h.output.PendingCleanup());
        h.output.ConfirmTransfers(); CHECK(h.output.PendingCleanup());
        h.output.Lifecycle(true,4); h.busy=false; h.Service(11000);
        CHECK_EQ(h.words.size(),n+1); CHECK_EQ(h.words.back(),h.output.Pack(false,69));
        h.output.TakeAbortRequest();
        CHECK(!h.output.Recover(false)); CHECK(h.output.Recover(true));
        CHECK_EQ(h.output.Select(16),MidiOutput::Selection::Selected); // Not yet activated in harness.
        CHECK_EQ(h.output.Select(12),MidiOutput::Selection::RestartRequired);
        CHECK(h.output.Enqueue(Command(1,72),1000,12000)); h.Service(12000);
        CHECK_EQ(h.words.size(),n+2); CHECK_EQ(h.words.back(),h.output.Pack(true,72));
    }
}

TEST_CASE(MidiInitiatingReasonSurvivesCleanupStallAndDisconnect) {
    MidiHarness h; h.Send(0,69);
    CHECK(h.output.Enqueue(Command(1,70),1,0));
    h.Service(MidiOutput::DeadlineUs+1);
    CHECK_EQ(h.output.Reason(),MidiOutput::Fault::Deadline);
    h.complete=false; h.Service(10000);
    h.output.TransportFailure(true);
    CHECK_EQ(h.output.Reason(),MidiOutput::Fault::Deadline);
    CHECK(h.output.PendingCleanup());
    h.output.Lifecycle(false,7);
    CHECK_EQ(h.output.Reason(),MidiOutput::Fault::Deadline);
}

TEST_CASE(MidiPartialCleanupFailureCannotRetireUncompletedPitchLedger) {
    MidiHarness h; h.complete=false;
    for(unsigned note=60;note<70;++note) h.Send(0,static_cast<Note>(note));
    h.output.TransportFailure(true); h.Service(0); // First eight OFFs accepted.
    CHECK(h.output.PendingCleanup());
    h.output.TransportFailure(false); // DMA error before final cleanup packet.
    CHECK_EQ(h.output.Reason(),MidiOutput::Fault::CompletionStall);
    const auto size=h.words.size(); h.complete=true; h.Service(1);
    CHECK_EQ(h.words.size(),size); CHECK(h.output.PendingCleanup());
    h.output.Reset(); h.Service(2); h.Service(3);
    CHECK(!h.output.PendingCleanup()); // Explicit reset retries the whole ledger.
}

TEST_CASE(MidiTimerInitializationFailureIsExplicitAndQuarantinesAdmission) {
    MidiOutput output; output.TimerFailure();
    CHECK_EQ(output.Reason(),MidiOutput::Fault::Timer);
    CHECK(output.Faulted()); CHECK(output.PendingCleanup());
    CHECK(!output.Enqueue(Command(0,69),0,0)); CHECK_EQ(output.Queued(),0);
}
