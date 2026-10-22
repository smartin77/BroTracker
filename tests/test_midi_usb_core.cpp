// Compile the actual core extension against a bounded mock of PJRC's DMA API.
#include "test_framework.h"
#include <cstdint>
#include <cstring>
namespace {
constexpr unsigned TX_NUM=4,TX_SIZE=512,MIDI_TX_ENDPOINT=3;
constexpr unsigned USB_PORTSC1_CCS=1,USB_PORTSC1_SUSP=2;
std::uint32_t USB1_PORTSC1=1;
std::uint8_t usb_configuration=1,brotracker_midi_ready=1,tx_head=0;
std::uint32_t brotracker_midi_generation=1;
std::uint16_t tx_packet_size=64;
struct transfer_t { std::uint32_t status=0; } tx_transfer[4];
std::uint8_t txbuffer[4*512]{};
unsigned calls=0,last_size=0;
std::uint32_t usb_transfer_status(const transfer_t* t) { return t->status; }
void usb_prepare_transfer(transfer_t* t,const void*,unsigned size,unsigned) { t->status=0x80; last_size=size; }
void arm_dcache_flush_delete(void*,unsigned) {}
void usb_transmit(unsigned,transfer_t*) { ++calls; }
#include "../tools/teensy_usb_audio/midi_tx.inc"
}
TEST_CASE(MidiActualCoreSubmissionNeverWaitsAndMonitorsLastOffCompletion) {
    const std::uint32_t words[16]={0x00458b08};
    CHECK_EQ(brotracker_midi_tx_fault(0),0);
    for(unsigned i=0;i<4;++i) CHECK_EQ(brotracker_midi_try_submit(words,16,100),1);
    CHECK_EQ(brotracker_midi_tx_idle(),0);
    CHECK_EQ(calls,4); CHECK_EQ(last_size,64); CHECK_EQ(tx_head,0);
    CHECK_EQ(brotracker_midi_try_submit(words,1,200),0); CHECK_EQ(calls,4);
    CHECK_EQ(brotracker_midi_tx_fault(40100),0); CHECK_EQ(brotracker_midi_tx_fault(40101),1);
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
