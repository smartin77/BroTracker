#pragma once
#include <Arduino.h>
#include <Entropy.h>
#include <usb_names.h>
#include "../../../src/bringup/boot_identity.h"
#include "../../../src/bringup/device_facts.h"

namespace BroTracker {
class DeviceIdentity {
public:
    // Called before audio activation/MIDI timer at application startup only.
    void Initialize() {
        if(boot_.Status()!=BootIdentity::State::NotStarted)return;
        const auto& serial=usb_string_serial_number;
        if(serial.bDescriptorType==3 && serial.bLength>=4 && serial.bLength<=22 && !(serial.bLength&1)) {
            bool valid=true;const unsigned count=(serial.bLength-2)/2;
            for(unsigned i=0;i<count;++i) {
                const auto c=serial.wString[i];if(c<'0' || c>'9')valid=false;
                board_[i]=static_cast<char>(c);
            }
            if(!valid)board_[0]=0;
        }
        EntropySource source;
        boot_.Begin(source,millis());
        // Clock-independent iteration ceiling also protects against a stalled tick.
        for(unsigned i=0;i<1000000 && boot_.Status()==BootIdentity::State::Pending;++i)
            boot_.Poll(source,millis());
        boot_.Fail();
    }
    DeviceFacts Facts()const {
        DeviceFacts facts;std::strcpy(facts.board,board_);std::strcpy(facts.boot,boot_.Text());
        return facts;
    }
private:
    struct EntropySource {
        void Begin() {Entropy.Initialize();} // Resets TRNG and discards stale hardware data.
        BootIdentity::Read ReadWords(std::uint32_t (&words)[4]) {
            // Entropy.available() copies data even on ERR; do not accept that path.
            if(TRNG_MCTL & TRNG_MCTL_ERR)return BootIdentity::Read::Failed;
            if(Entropy.available()<4)return BootIdentity::Read::Waiting;
            if(TRNG_MCTL & TRNG_MCTL_ERR)return BootIdentity::Read::Failed;
            for(auto& word:words)word=Entropy.random(); // Available cached words: no waiting.
            return BootIdentity::Read::Ready;
        }
    };
    char board_[11]{};
    BootIdentity boot_;
};
}
