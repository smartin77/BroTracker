#pragma once
#include "sd_sample_streaming.h"
#include "core/audio/variable_rate_sample_voice.h"
namespace BroTracker {
// Foreground owns preparation and file lifetime. Voice and span publication are
// bounded under the audio exclusion guard; the ISR only renders prepared PCM.
// Uses the shared streaming engine's ninth ring, never a Pattern channel/binding.
class SdWavAudition {
public:
    explicit SdWavAudition(SdSampleStreaming& stream):stream_(stream) {
        voice_.SetStreamChannel(kAuditionStreamChannel);(void)voice_.Configure(44100);
    }
    void Cancel() {
        pending_=false;path_[0]=0;completed_=0;active_id_=0;
        if(preparing_)stream_.CancelPreparation();
        StreamInterruptGuard guard;voice_.Stop();stream_.PublishVoice(kAuditionStreamChannel,voice_.StreamSnapshot());
    }
    bool Request(std::uint32_t id,const char* path) {
        if(!id || !InstrumentDefinitions::ValidResource(path))return false;
        Cancel();id_=id;std::strcpy(path_,path);pending_=true;return true;
    }
    void CancelThrough(std::uint32_t id) {if(id>=id_)Cancel();}
    // Foreground: terminate only the audition if its low-priority refill misses.
    void CheckPlayback() {
        if(!active_id_)return;
        StreamInterruptGuard guard;
        const auto state=voice_.StreamSnapshot();
        if(state.underruns!=start_underruns_) {
            voice_.Stop();stream_.PublishVoice(kAuditionStreamChannel,voice_.StreamSnapshot());
            completed_=active_id_;result_=8;active_id_=0;
        } else if(!state.sample)active_id_=0;
    }
    bool Busy()const{return pending_ || preparing_;}
    // Called before any other owner of BeginPrepareAsync. Drains cancelled work
    // before beginning the latest request; a superseded result is never triggered.
    template<class Bindings> void Service(const Bindings& bindings) {
        if(preparing_) {
            stream_.PreparationStep();
            if(!stream_.PreparationDone())return;
            preparing_=false;
            if(job_id_==id_ && pending_) {
                pending_=false;result_=stream_.PreparationResult()==InstrumentAvailability::Ready?0:stream_.PreparationResult()==InstrumentAvailability::PathCapacity?9:4;
                if(!result_) {
                    const auto view=stream_.PreparedMixedView();
                    std::uint64_t step=0;
                    if(!ComposeStreamingStep(kSourceFrameStepUnity,view.sample_rate_hz,44100,step))result_=4;
                    else {
                        StreamInterruptGuard guard;
                        result_=view.Trigger(voice_,step)==VariableVoiceStatus::Success?0:4;
                        const auto state=voice_.StreamSnapshot();
                        if(!result_){active_id_=id_;start_underruns_=state.underruns;}
                        stream_.PublishVoice(kAuditionStreamChannel,state);
                    }
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
    // Audio owner, independent of Pattern transport. Both channels share phase.
    bool Render(std::int16_t* pcm,std::size_t count,std::int16_t* right=nullptr) {
        if(!voice_.IsActive() || (!right && voice_.IsStereo()))return false;
        if(right)(void)voice_.RenderStereo(pcm,right,count);
        else (void)voice_.Render(pcm,count);
        stream_.PublishVoice(kAuditionStreamChannel,voice_.StreamSnapshot());
        return true;
    }
    // Add to the Pattern bus before its shared master ramp. No audio-pool blocks
    // or Pattern channel slots are used by the audition.
    template<std::size_t Count>
    void Mix(std::int16_t (&left)[Count],std::int16_t (&right)[Count]) {
        if(!voice_.IsActive())return;
        std::int16_t audition_left[Count],audition_right[Count];
        if(!Render(audition_left,Count,audition_right))return;
        for(std::size_t i=0;i<Count;++i) {
            left[i]=std::clamp(int(left[i])+audition_left[i],-32768,32767);
            right[i]=std::clamp(int(right[i])+audition_right[i],-32768,32767);
        }
    }
    StreamedVoiceSnapshot Snapshot()const{return voice_.StreamSnapshot();}
private:
    SdSampleStreaming& stream_;VariableRateSampleVoice voice_;
    char path_[kSamplePathMaximum+1]{};bool pending_=false,preparing_=false;
    std::uint32_t id_=0,job_id_=0,completed_=0,active_id_=0,start_underruns_=0;unsigned result_=0;
};
}
