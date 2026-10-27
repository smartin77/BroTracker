#include "production_audio_output.h"
#include <Wire.h>
#include "production_audio_guard.h"
#include "../../../src/bringup/sgtl5000_headphones.h"

namespace BroTracker {
const char* ProductionAudioOutput::Select(AudioOutputChoice request, AudioStream& source, AudioOutputUSB& usb) {
        constexpr auto fallback=BROTRACKER_AUDIO_OUTPUT==BROTRACKER_OUTPUT_PT8211 ?
            AudioOutputChoice::Pt8211 : AudioOutputChoice::Bridge;
        if(!selection_.CanSelect(request,fallback)) return "restart-required";
        if(Active()!=AudioOutputChoice::Default) return error_;
        const auto next=request==AudioOutputChoice::Default ? fallback : request;
        // PJRC connect() and initialize_memory() execute __enable_irq().
        // BASEPRI independently blocks installed audio IRQ priorities: DMA uses
        // startup's 128, software audio uses update_setup()'s 208. Threshold 16
        // also covers the constructor enabling either IRQ with work pending.
        // BASEPRI_MAX never weakens a stricter caller mask. Priority-zero IRQs
        // and faults are not masked; they do not run either audio handler.
        const auto saved=BeginAudioActivation();
        if(next==AudioOutputChoice::Pt8211) {
            auto* output=new (&hardware_) AudioOutputPT8211;
            new (&patches_[0]) AudioConnection(source,0,*output,0);
            new (&patches_[1]) AudioConnection(source,0,*output,1);
        } else if(next==AudioOutputChoice::AudioShield) {
            auto* output=new (&hardware_) AudioOutputI2S;
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
        EndAudioActivation(saved);
        // I2C and enable()'s delays require interrupts: never inside that guard.
        // Route is already reserved even on failure; no other hardware may start.
        if(next==AudioOutputChoice::AudioShield)
            error_=InitializeShieldHeadphones(codec_,Wire);
        return error_;
    }
}
