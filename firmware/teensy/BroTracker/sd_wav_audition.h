#pragma once
#include "sd_sample_streaming.h"
#include "core/audio/variable_rate_sample_voice.h"
#include "core/audio/master_volume.h"
namespace BroTracker {
// Foreground owns preparation and file lifetime. Voice and span publication are
// bounded under the audio exclusion guard; the ISR only renders prepared PCM.
// Uses the shared streaming engine's ninth ring, never a Pattern channel/binding.
class SdWavAudition {
public:
    explicit SdWavAudition(SdSampleStreaming& stream):stream_(stream) {
        voice_.SetStreamChannel(8);(void)voice_.Configure(44100);volume_.Configure(44100);
    }
    void Cancel() {
        pending_=false;path_[0]=0;
        if(preparing_)stream_.CancelPreparation();
        StreamInterruptGuard guard;voice_.Stop();stream_.PublishVoice(8,voice_.StreamSnapshot());
    }
    bool Request(std::uint32_t id,const char* path) {
        if(!id || !InstrumentDefinitions::ValidResource(path))return false;
        Cancel();id_=id;std::strcpy(path_,path);pending_=true;return true;
    }
    bool Busy()const{return pending_ || preparing_;}
    // Called before any other owner of BeginPrepareAsync. Drains cancelled work
    // before beginning the latest request; a superseded result is never triggered.
    template<class Bindings> void Service(const Bindings& bindings,bool stopped) {
        if(!stopped)Cancel();
        if(preparing_) {
            stream_.PreparationStep();
            if(!stream_.PreparationDone())return;
            preparing_=false;
            if(job_id_==id_ && pending_) {
                pending_=false;result_=stream_.PreparationResult()==InstrumentAvailability::Ready?0:4;
                if(!result_) {
                    const auto view=stream_.PreparedView();
                    // All currently supported WAVs are 44100 Hz. Unity playback
                    // uses their native rate, with no instrument pitch/tempo input.
                    StreamInterruptGuard guard;
                    result_=voice_.Trigger(view,kSourceFrameStepUnity)==VariableVoiceStatus::Success?0:4;
                    stream_.PublishVoice(8,voice_.StreamSnapshot());
                }
                completed_=id_;
            }
        }
        if(pending_) {
            if(stream_.ReclaimOne(bindings))return;
            if(stream_.BeginPrepareAsync(path_)){preparing_=true;job_id_=id_;}
        }
    }
    std::uint32_t TakeResult(unsigned& result){const auto id=completed_;completed_=0;result=result_;return id;}
    // Audio owner. Never start audio here; a transport change also silences an
    // accepted audition even if foreground cancellation has not yet run.
    bool Render(std::int16_t* pcm,std::size_t count,bool stopped,unsigned volume) {
        if(!stopped)voice_.Stop();
        const bool rendered=stopped && voice_.IsActive();
        if(rendered) {
            (void)voice_.Render(pcm,count);(void)volume_.Set(volume);volume_.Process(pcm,count);
        }
        stream_.PublishVoice(8,voice_.StreamSnapshot());return rendered;
    }
    StreamedVoiceSnapshot Snapshot()const{return voice_.StreamSnapshot();}
private:
    SdSampleStreaming& stream_;VariableRateSampleVoice voice_;MasterVolume volume_;
    char path_[64]{};bool pending_=false,preparing_=false;
    std::uint32_t id_=0,job_id_=0,completed_=0;unsigned result_=0;
};
}
