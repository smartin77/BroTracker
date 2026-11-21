#include "sample_waveform.h"
#include "core/audio/wav_buffer_loader.h"

namespace BroTracker {
namespace {
constexpr unsigned kPlayingAllowanceMs=6000;
bool SameSource(WaveformSource a,WaveformSource b) {
    return a.token==b.token && a.frames==b.frames && a.rate==b.rate &&
        a.channels==b.channels && a.bits==b.bits;
}
}

bool SampleWaveformCache::Begin(WaveformSource source) {
    *this={};
    if(!source.token || !source.frames || source.Bytes()>UINT32_MAX ||
       !WavBufferDetail::SupportsIntegerPcm(1,source.channels,source.bits,source.rate,true,true,true,true)) {
        Fail();return false;
    }
    source_=source;
    columns_=std::min<std::size_t>(source.frames,kColumns);
    return true;
}

bool SampleWaveformCache::Append(std::uint32_t offset,const std::uint8_t* pcm,unsigned size) {
    const auto stride=source_.Stride();
    if(state_!=State::Loading || !stride || !size || size>kWaveformIdleBytes ||
       offset!=bytes_loaded_ || size%stride || size>source_.Bytes()-offset) return false;
    const auto sample_bytes=source_.bits/8;
    auto frame=offset/stride;
    for(unsigned at=0;at<size;at+=stride,++frame) {
        auto& peak=peaks_[std::uint64_t(frame)*columns_/source_.frames];
        for(unsigned channel=0;channel<source_.channels;++channel) {
            const auto* sample=pcm+at+channel*sample_bytes;
            const auto raw=sample_bytes==2?WavBufferDetail::U16(sample):0;
            const auto value=static_cast<std::int16_t>(sample_bytes==1?(int(*sample)-128)*256:
                raw>=32768?int(raw)-65536:int(raw));
            peak.minimum=std::min(peak.minimum,value);
            peak.maximum=std::max(peak.maximum,value);
        }
    }
    bytes_loaded_+=size;
    if(bytes_loaded_==source_.Bytes())state_=State::Ready;
    return true;
}

void SampleWaveformTransfer::Select(unsigned number,std::string_view path,bool supported) {
    if(number_==number && path_==path && supported_==supported)return;
    if(id_)cancel_=id_;
    id_=0;number_=number;path_=path;supported_=supported;ready_=nullptr;used_=nullptr;resume_=false;
    if(supported && number && number<=99 && ValidSamplePath(path))
        phase_=Phase::Get;
    else phase_=Phase::Idle;
}

const SampleWaveformCache& SampleWaveformTransfer::View() const {
    static const SampleWaveformCache loading;
    static const auto unavailable=[] { SampleWaveformCache cache;cache.Fail();return cache; }();
    if(ready_)return *ready_;
    if(Pulling())return working_;
    return phase_==Phase::Idle?unavailable:loading;
}

unsigned& SampleWaveformTransfer::Allowance(std::uint32_t token) {
    for(auto& budget:budgets_)if(budget.token==token)return budget.used;
    auto& oldest=*std::min_element(budgets_.begin(),budgets_.end(),
        [](const auto& a,const auto& b){return a.token<b.token;});
    const unsigned used=token<=retired_through_?kPlayingAllowanceMs:0;
    retired_through_=std::max(retired_through_,oldest.token);
    oldest={token,used};return oldest.used;
}

bool SampleWaveformTransfer::Deferred() const {
    return playing_ && (phase_==Phase::Metadata
        ? metadata_playing_ms_>=kPlayingAllowanceMs
        : Pulling() && used_ && (!playing_admitted_ || *used_>=kPlayingAllowanceMs));
}

void SampleWaveformTransfer::AdmitPlaying() {
    playing_admitted_=used_ && WaveformPlayingEstimateMs(working_.Metadata().Bytes()-working_.BytesLoaded())
        <=kPlayingAllowanceMs-*used_;
}

void SampleWaveformTransfer::Disconnect() {
    // IDs stay monotonic even across transport sessions. Completed entries are
    // session-scoped because the device can have rebooted or changed media.
    for(auto& entry:cache_)entry={};
    next_cache_=0;ready_=nullptr;
    Select(0,{},false);
    cancel_=0;working_={};budgets_={};retired_through_=0;
    used_=nullptr;ticked_=playing_=false;
}

void SampleWaveformTransfer::Fail() {
    if(id_)cancel_=id_;
    if(Pulling())working_={};
    id_=0;phase_=Phase::Idle;ready_=nullptr;used_=nullptr;
}

void SampleWaveformTransfer::Tick(std::uint32_t now,bool playing) {
    const auto elapsed=ticked_?now-last_tick_:0;
    const bool awaiting_reply=phase_==Phase::Metadata || phase_==Phase::Data;
    auto* used=phase_==Phase::Metadata?&metadata_playing_ms_:used_;
    if(playing_ && used) {
        const auto waiting=awaiting_reply?std::min(elapsed,now-sent_at_):elapsed;
        const auto active=phase_==Phase::Metadata || playing_admitted_
            ? std::min(waiting,kPlayingAllowanceMs-*used) : 0;
        *used+=active;
        // Keep the original request outstanding, with its deadline frozen
        // during intentional deferral. Stop/Pause resumes that deadline.
        if(awaiting_reply)sent_at_+=waiting-active;
    }
    if(playing && !playing_ && Pulling())AdmitPlaying();
    last_tick_=now;ticked_=true;playing_=playing;
    constexpr std::uint32_t timeout_ms=12000;
    if(!Deferred() && awaiting_reply && now-sent_at_>=timeout_ms)Fail();
}

std::size_t SampleWaveformTransfer::Command(char* line,std::size_t capacity,std::uint32_t now) {
    if(cancel_) {
        const auto n=std::snprintf(line,capacity,"BTWAVE1 CANCEL 1 %lu\n",(unsigned long)cancel_);
        if(n<=0 || std::size_t(n)>=capacity)return 0;
        cancel_=0;return n;
    }
    if(phase_==Phase::Get) {
        if(sequence_==UINT32_MAX){Fail();return 0;}
        const auto id=sequence_+1;
        const auto n=std::snprintf(line,capacity,"BTWAVE1 GET 1 %lu %u ",(unsigned long)id,number_-1);
        if(n<=0 || std::size_t(n)+1>=capacity ||
            !EncodePathToken(path_.c_str(),line+n,capacity-n-1)){Fail();return 0;}
        const auto end=std::strlen(line);
        line[end]='\n';line[end+1]=0;
        sequence_=id_=id;phase_=Phase::Metadata;sent_at_=now;metadata_playing_ms_=0;return end+1;
    }
    if(phase_==Phase::Next && !Deferred()) {
        const auto n=std::snprintf(line,capacity,"BTWAVE1 %s 1 %lu %lu\n",
            resume_?"RESUME":"NEXT",(unsigned long)id_,(unsigned long)working_.BytesLoaded());
        if(n<=0 || std::size_t(n)>=capacity){Fail();return 0;}
        resume_=false;phase_=Phase::Data;sent_at_=now;return n;
    }
    return 0;
}

void SampleWaveformTransfer::Response(std::string_view line) {
    if(!id_)return;
    std::uint32_t fields[7];
    if(SnapshotFields(line.data(),line.size(),"BTWAVE1 ERROR ",fields,2) &&
       fields[0]==1 && fields[1]==id_) {Fail();return;}
    if(phase_==Phase::Metadata && SnapshotFields(line.data(),line.size(),"BTWAVE1 META ",fields,7) &&
       fields[0]==1 && fields[1]==id_) {
        const WaveformSource source{fields[2],fields[3],fields[4],fields[5],fields[6]};
        for(const auto& entry:cache_)if(entry.Status()==SampleWaveformCache::State::Ready &&
           SameSource(entry.Metadata(),source)) {
            ready_=&entry;phase_=Phase::Complete;return;
        }
        if((!source.token || !SameSource(working_.Metadata(),source)) && !working_.Begin(source)){Fail();return;}
        used_=&Allowance(source.token);
        *used_=std::min(kPlayingAllowanceMs,*used_+metadata_playing_ms_);
        AdmitPlaying();
        resume_=working_.BytesLoaded()!=0;
        phase_=Phase::Next;return;
    }
    std::uint8_t pcm[kWaveformIdleBytes];
    std::uint32_t id,offset;
    unsigned bytes;
    if(phase_!=Phase::Data || !ParseWaveformData(line,id,offset,pcm,bytes) || id!=id_)return;
    if(!working_.Append(offset,pcm,bytes)){Fail();return;}
    if(working_.Status()!=SampleWaveformCache::State::Ready){phase_=Phase::Next;return;}
    cache_[next_cache_]=working_;ready_=&cache_[next_cache_];
    *used_=0;used_=nullptr;working_={};
    next_cache_=(next_cache_+1)%cache_.size();phase_=Phase::Complete;
}

} // namespace BroTracker
