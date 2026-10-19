#include "production_audio_output.h"

namespace BroTracker {
const char* ProductionAudioOutput::Select(AudioOutputChoice request, AudioStream& source, AudioOutputUSB& usb) {
        constexpr auto fallback=BROTRACKER_AUDIO_OUTPUT==BROTRACKER_OUTPUT_PT8211 ?
            AudioOutputChoice::Pt8211 : AudioOutputChoice::Bridge;
        if(!selection_.CanSelect(request,fallback)) return "restart-required";
        if(Active()!=AudioOutputChoice::Default) return nullptr;
        const auto next=request==AudioOutputChoice::Default ? fallback : request;
        // PJRC connect() and initialize_memory() execute __enable_irq().
        // BASEPRI independently blocks installed audio IRQ priorities: DMA uses
        // startup's 128, software audio uses update_setup()'s 208. Threshold 16
        // also covers the constructor enabling either IRQ with work pending.
        // BASEPRI_MAX never weakens a stricter caller mask. Priority-zero IRQs
        // and faults are not masked; they do not run either audio handler.
        uint32_t mask, priority_mask;
        asm volatile("mrs %0, primask\n\tmrs %1, basepri\n\tcpsid i\n\tmsr basepri_max, %2\n\tdsb\n\tisb"
            : "=&r"(mask), "=&r"(priority_mask) : "r"(16u) : "memory");
        if(next==AudioOutputChoice::Pt8211) {
            auto* output=new (&hardware_) AudioOutputPT8211;
            new (&patches_[0]) AudioConnection(source,0,*output,0);
            new (&patches_[1]) AudioConnection(source,0,*output,1);
        } else {
            auto* output=new (&hardware_) AudioOutputMQS;
            new (&patches_[0]) AudioConnection(source,0,*output,0);
            new (&patches_[1]) AudioConnection(source,0,usb,0);
            new (&patches_[2]) AudioConnection(source,0,usb,1);
        }
        // The hardware constructor has already claimed update_setup(); this
        // avoids AudioMemory's dynamically allocated fallback IntervalTimer.
        AudioMemory(8);
        selection_.Commit(request,fallback);
        // Restore PRIMASK before removing BASEPRI protection, including callers
        // that entered with interrupts disabled. Pending audio may run only once
        // the driver, patches, memory pool and committed route are complete.
        asm volatile("msr primask, %0\n\tmsr basepri, %1\n\tdsb\n\tisb"
            :: "r"(mask), "r"(priority_mask) : "memory");
        return nullptr;
    }
}
