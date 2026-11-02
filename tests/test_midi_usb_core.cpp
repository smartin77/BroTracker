// Compile the actual core extension against a bounded mock of PJRC's DMA API.
#include "test_framework.h"
#include <cstdint>
#include <cstring>
#include "bringup/midi_output.h"
#include "bringup/pattern_control.h"
#include "bringup/variable_rate_player_adapter.h"
#include "bringup/pattern_fixture.h"
namespace {
constexpr unsigned TX_NUM=4,TX_SIZE=512,MIDI_TX_ENDPOINT=3;
constexpr unsigned USB_PORTSC1_CCS=1,USB_PORTSC1_SUSP=2;
std::uint32_t USB1_PORTSC1=1, USB1_ENDPTFLUSH=0, USB1_ENDPTSTATUS=0, USB1_ENDPTPRIME=0;
std::uint8_t usb_configuration=1,brotracker_midi_ready=1,tx_head=0;
std::uint32_t brotracker_midi_generation=1;
std::uint16_t tx_packet_size=64,tx_available=0;
struct transfer_t { std::uint32_t status=0; } tx_transfer[4];
std::uint8_t txbuffer[4*512]{};
unsigned calls=0,last_size=0;
std::uint32_t usb_transfer_status(const transfer_t* t) { return t->status; }
void usb_prepare_transfer(transfer_t* t,const void*,unsigned size,unsigned) { t->status=0x80; last_size=size; }
void arm_dcache_flush_delete(void*,unsigned) {}
void usb_transmit(unsigned,transfer_t*) { ++calls; }
void usb_config_tx(unsigned ep,unsigned size,int,void*) { CHECK_EQ(ep,MIDI_TX_ENDPOINT); CHECK_EQ(size,tx_packet_size); }
#include "../tools/teensy_usb_audio/midi_tx.inc"
}
TEST_CASE(MidiActualCoreSubmissionNeverWaitsAndMonitorsLastOffCompletion) {
    const std::uint32_t words[16]={0x00458b08};
    CHECK_EQ(brotracker_midi_tx_fault(0),0);
    for(unsigned i=0;i<4;++i) CHECK_EQ(brotracker_midi_try_submit(words,16,100),1);
    CHECK_EQ(brotracker_midi_tx_idle(),0);
    CHECK_EQ(calls,4); CHECK_EQ(last_size,64); CHECK_EQ(tx_head,0);
    CHECK_EQ(brotracker_midi_try_submit(words,1,200),0); CHECK_EQ(calls,4);
    CHECK_EQ(brotracker_midi_tx_fault(40100),0); CHECK_EQ(brotracker_midi_tx_fault(40101),2);
    for(auto& t:tx_transfer) t.status=0;
    CHECK_EQ(brotracker_midi_tx_fault(50000),0); CHECK_EQ(brotracker_midi_tx_idle(),1);
    CHECK_EQ(brotracker_midi_try_submit(words,1,50000),1); CHECK_EQ(last_size,4);
    CHECK_EQ(std::memcmp(txbuffer,words,4),0);
    tx_transfer[0].status=0x40; CHECK_EQ(brotracker_midi_tx_fault(50001),1);
    ++brotracker_midi_generation; CHECK_EQ(brotracker_midi_tx_fault(50002),0);
    USB1_PORTSC1=USB_PORTSC1_SUSP|USB_PORTSC1_CCS;
    CHECK_EQ(brotracker_midi_try_submit(words,1,50003),0);
    USB1_PORTSC1=0; CHECK_EQ(brotracker_midi_try_submit(words,1,50003),0);
    USB1_PORTSC1=1; brotracker_midi_ready=0; CHECK_EQ(brotracker_midi_try_submit(words,1,50003),0);
    brotracker_midi_ready=1; usb_configuration=0; CHECK_EQ(brotracker_midi_try_submit(words,1,50003),0);
    usb_configuration=1; CHECK_EQ(brotracker_midi_try_submit(words,17,50003),0);
    CHECK_EQ(brotracker_midi_try_submit(words,0,50003),0);
}

namespace {
unsigned test_now=0;
void ResetBackend() {
    ++brotracker_midi_generation; brotracker_midi_ready=usb_configuration=1;
    USB1_PORTSC1=1; USB1_ENDPTFLUSH=USB1_ENDPTSTATUS=USB1_ENDPTPRIME=0;
    bt_midi_cancelling=0; tx_head=0; calls=0;
    for(auto& t:tx_transfer) t.status=0;
    (void)brotracker_midi_tx_fault(0);
}
void Pump(BroTracker::MidiOutput& out,unsigned now,bool consumer) {
    test_now=now;
    if(out.TakeAbortRequest()) brotracker_midi_cancel_begin();
    // Model asynchronous hardware flush: no completion until this service.
    if(USB1_ENDPTFLUSH) USB1_ENDPTFLUSH=0;
    if(!brotracker_midi_cancel_service()) return;
    if(consumer) for(auto& t:tx_transfer) t.status=0;
    const auto fault=brotracker_midi_tx_fault(now);
    if(fault) {
        out.TransportFailure(fault==2);
        if(out.TakeAbortRequest()) brotracker_midi_cancel_begin();
        return;
    }
    if(brotracker_midi_tx_idle()) out.ConfirmTransfers();
    out.Service(now,[](const std::uint32_t* words,unsigned n) {
        return brotracker_midi_try_submit(words,n,test_now)!=0;
    });
}
}

TEST_CASE(MidiActualBackendQuarantinesStaleDmaWithoutBlockingOrFalseCompletion) {
    ResetBackend();
    const std::uint32_t note=0x64459b09;
    CHECK(brotracker_midi_try_submit(&note,1,0));
    CHECK(brotracker_midi_tx_fault(40001)); // Host configured, never consumed.
    brotracker_midi_cancel_begin();
    CHECK(!brotracker_midi_try_submit(&note,1,40002));
    CHECK(!brotracker_midi_tx_idle());
    CHECK(!brotracker_midi_cancel_service()); // Stuck flush cannot block caller.
    USB1_ENDPTFLUSH=0; USB1_ENDPTSTATUS=1u<<(MIDI_TX_ENDPOINT+16);
    CHECK(!brotracker_midi_cancel_service()); CHECK(USB1_ENDPTFLUSH);
    USB1_ENDPTFLUSH=USB1_ENDPTSTATUS=0;
    CHECK(brotracker_midi_cancel_service());
    for(const auto& t:tx_transfer) CHECK_EQ(t.status,0); // No delayed ON remains.
    CHECK_EQ(tx_head,0); CHECK(brotracker_midi_tx_idle());
}
TEST_CASE(MidiNoConsumerMixedPlaybackKeepsExactPcmAndTimelineAndNeedsExplicitRecovery) {
    using namespace BroTracker;
    ResetBackend();
    using Control=BasicPatternBringUpControl<VariableRateBringUpPlayer,VariableRateSampleBindings>;
    Control actual,reference;
    const std::int16_t sample[]={1000,-1000,700,-700};
    auto fixture=SelectDevelopmentFixture({sample,4,44100});
    fixture.pattern.active_rows=4;
    for(unsigned r=0;r<4;++r) {
        fixture.pattern.cells[r][0]={60,0}; // Sample
        fixture.pattern.cells[r][1]={69,2}; // Synth
        fixture.pattern.cells[r][2]={69,1}; // MIDI, displayed tracker channel 3
    }
    CHECK_EQ(actual.InitializeAudio(12500,44100,fixture.pattern,fixture.bindings),PatternPlayerStatus::Success);
    CHECK_EQ(reference.InitializeAudio(12500,44100,fixture.pattern,fixture.bindings),PatternPlayerStatus::Success);
    CHECK_EQ(actual.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK_EQ(reference.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    MidiOutput midi; midi.Lifecycle(true,brotracker_midi_generation);
    std::int16_t a[128],b[128]; bool audible=false;
    for(unsigned block=0;block<1000;++block) {
        const unsigned now=block*2903;
        actual.AudioBlock(a,128,true); reference.AudioBlock(b,128,true);
        CollectMidiBlock(actual,midi,now);
        for(unsigned i=0;i<128;++i) { CHECK_EQ(a[i],b[i]); audible|=a[i]!=0; }
        Pump(midi,now,false); // MIDI endpoint configured but no host reads.
        const auto state=actual.Snapshot();
        CHECK(state.running); CHECK_EQ(state.fault,PatternFault::None);
        CHECK_EQ(state.next_sample,(block+1)*128u);
        CHECK_EQ(state.position.absolute_tick,reference.Snapshot().position.absolute_tick);
    }
    CHECK(audible); CHECK(midi.Faulted()); CHECK(midi.PendingCleanup());
    CHECK_EQ(midi.Queued(),0); CHECK(midi.counters.transport_failure>=1);
    CHECK_EQ(midi.Reason(),MidiOutput::Fault::CompletionStall);
    CHECK(midi.StorageReady(brotracker_midi_tx_idle()));
    CHECK(!actual.StorageReady()); // Playback still vetoes SD despite idle MIDI quarantine.
    const auto before=calls;
    for(unsigned i=0;i<10;++i) Pump(midi,3000000+i*100,true);
    CHECK_EQ(calls,before); // No late backlog, including on consumer appearance.
    CHECK(!midi.Recover(false)); CHECK(!midi.Recover(true)); // cleanup remains uncertain
    CHECK_EQ(actual.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    actual.AudioBlock(a,128,true); CollectMidiBlock(actual,midi,3100000);
    Pump(midi,3100000,true); Pump(midi,3100100,true);
    CHECK(!midi.PendingCleanup()); CHECK(midi.Faulted());
    CHECK(midi.Recover(actual.Snapshot().transport==PatternTransportState::Stopped));
    CHECK_EQ(actual.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    actual.AudioBlock(a,128,true); CollectMidiBlock(actual,midi,3200000);
    Pump(midi,3200000,true); Pump(midi,3200100,true);
    CHECK(!midi.Faulted()); CHECK(actual.Snapshot().running);
    // Genuine audio allocation failure remains fatal independently of MIDI.
    actual.AudioBlock(nullptr,128,false);
    CHECK_EQ(actual.Snapshot().fault,PatternFault::Allocation); CHECK(!actual.Snapshot().running);
}

TEST_CASE(MidiQuarantinedCleanupAllowsStorageOnlyAfterActualDmaCancellation) {
    using namespace BroTracker;
    ResetBackend();MidiOutput midi;midi.Lifecycle(true,brotracker_midi_generation);
    CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    Pump(midi,0,true);Pump(midi,1,true);
    CHECK(midi.StorageReady(brotracker_midi_tx_idle()));
    VariableRateVoiceCommandBatch batch{};batch.count=1;
    batch.commands[0].action=VariableRateVoiceAction::TriggerMidi;
    batch.commands[0].channel=0;batch.commands[0].synth_note=69;
    CHECK(midi.Enqueue(batch,0,100));CHECK(!midi.StorageReady(true));
    Pump(midi,100,false);CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    Pump(midi,40101,false); // Original ON stalled; cancellation requested.
    CHECK(midi.Faulted());CHECK(midi.PendingCleanup());
    CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    Pump(midi,40102,false); // Cancellation finishes; targeted OFF submitted.
    CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    Pump(midi,80103,false); // OFF also stalled; quarantine, still cancelling.
    CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    Pump(midi,80104,false); // Hardware flush now complete; no active MIDI work.
    CHECK(midi.StorageReady(brotracker_midi_tx_idle()));
    CHECK(midi.PendingCleanup());CHECK(midi.Faulted());CHECK(!midi.Recover(true));
    CHECK_EQ(midi.Reason(),MidiOutput::Fault::CompletionStall);
    const auto submissions=calls;
    for(unsigned n=0;n<20;++n) Pump(midi,81000+n,true);
    CHECK_EQ(calls,submissions);CHECK(midi.PendingCleanup()); // Consumer arrival is not cleanup proof.
    CHECK(!midi.StorageReady(false)); // DMA/flush uncertainty still vetoes storage.
    midi.Reset();CHECK(!midi.StorageReady(true)); // Explicit STOP/reset re-arms reliable cleanup.
    Pump(midi,82000,true);CHECK(!midi.StorageReady(brotracker_midi_tx_idle()));
    CHECK_EQ(calls,submissions+1); // Uncertain pitch survived storage eligibility.
    std::uint32_t cleanup_word=0;std::memcpy(&cleanup_word,txbuffer,sizeof(cleanup_word));
    CHECK_EQ(cleanup_word,midi.Pack(false,69));
    Pump(midi,82001,true);CHECK(!midi.PendingCleanup());CHECK(midi.Faulted());
    CHECK(midi.StorageReady(brotracker_midi_tx_idle()));CHECK(!midi.Recover(false));
    CHECK(midi.Recover(true));CHECK(!midi.Faulted());
}
