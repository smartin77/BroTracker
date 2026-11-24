#include "sample_waveform.h"
#include "core/audio/wav_buffer_loader.h"

#include <cstring>
#include <new>

namespace BroTracker {
namespace {
constexpr unsigned kPlayingAllowanceMs = 6000;
bool SameSource(WaveformSource a, WaveformSource b) {
    return a.token==b.token && a.frames==b.frames && a.rate==b.rate &&
        a.channels==b.channels && a.bits==b.bits;
}

std::uint32_t MixBits(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}

int PcmValue(const std::uint8_t* sample, unsigned bytes) {
    if(bytes==1)return (int(*sample)-128)*256;
    const auto raw=WavBufferDetail::U16(sample);
    return raw>=32768?int(raw)-65536:int(raw);
}
}

bool SampleWaveformCache::Begin(WaveformSource source) {
    *this = {};
    if(!source.token || !source.frames || source.Bytes()>UINT32_MAX ||
       !WavBufferDetail::SupportsIntegerPcm(1,source.channels,source.bits,source.rate,true,true,true,true)) {
        Fail();return false;
    }
    source_ = source;
    columns_ = std::min<std::size_t>(source.frames,kColumns);
    if(source.frames<=4096 && source.Bytes()<=16*1024) {
        windows_[window_count_++] = {0,source.frames};
        return true;
    }

    // From one stratum per 12 columns toward one per 24 at three seconds.
    // Also cap the number of idle credits by source size, since each can
    // return up to 768 bytes even when the target window is smaller.
    const auto duration_weight=std::min<std::uint64_t>(12,std::uint64_t(source.frames)*4/source.rate);
    const auto spatial=std::max<std::uint64_t>(4,columns_/(12+duration_weight));
    const auto affordable=std::max<std::uint64_t>(4,source.Bytes()/(4*kWaveformIdleBytes));
    const auto strata=static_cast<unsigned>(std::min({spatial,affordable,std::uint64_t(kMaxWindows)}));
    const auto frames_per_window=kWaveformPlayingBytes/source.Stride();
    const auto add_window=[&](unsigned index) {
        const auto first=std::uint32_t(std::uint64_t(index)*source.frames/strata);
        const auto end=std::uint32_t(std::uint64_t(index+1)*source.frames/strata);
        const auto width=std::min(frames_per_window,end-first);
        const auto left=index==0?first:index==strata-1?end-width:
            first+MixBits(source.frames+index*0x9e3779b9U)%(end-first-width+1);
        windows_[window_count_++]={left,left+width};
    };
    add_window(0);
    add_window(strata-1);
    // Recursively bisect the stratum indices, visiting the largest gaps first.
    for(unsigned step=1;step<strata;step*=2)
        for(unsigned i=1;i<2*step;i+=2) {
            const auto index=i*(strata-1)/(2*step);
            if(index> (i-1)*(strata-1)/(2*step) && index<(i+1)*(strata-1)/(2*step))
                add_window(index);
        }
    return true;
}

std::uint32_t SampleWaveformCache::FirstMissing(std::uint32_t first,std::uint32_t end) const {
    const auto next=std::lower_bound(coverage_.begin(),coverage_.end(),first,
        [](Range range,std::uint32_t frame){return range.end<=frame;});
    return next!=coverage_.end() && next->first<=first ? std::min(end,next->end) : first;
}

void SampleWaveformCache::AddCoverage(Range range) {
    auto first=std::lower_bound(coverage_.begin(),coverage_.end(),range.first,
        [](Range old,std::uint32_t frame){return old.end<frame;});
    auto end=first;
    while(end!=coverage_.end() && end->first<=range.end) {
        range.first=std::min(range.first,end->first);
        range.end=std::max(range.end,end->end);
        ++end;
    }
    if(first==end)coverage_.insert(first,range);
    else {
        *first=range;
        coverage_.erase(first+1,end);
    }
}

bool SampleWaveformCache::Append(std::uint32_t offset,const std::uint8_t* pcm,unsigned size) {
    const auto stride=source_.Stride();
    if(state_!=State::Loading || storage_limited_ || !stride || !size || size>kWaveformIdleBytes ||
       offset%stride || size%stride || std::uint64_t(offset)+size>source_.Bytes())return false;

    const auto first=offset/stride, end=first+size/stride;
    const auto first_page=offset/kPageBytes, last_page=(offset+size-1)/kPageBytes;
    for(auto index=first_page;index<=last_page;++index) {
        if(pages_.find(index)!=pages_.end())continue;
        if(pages_.size()==kMaxPages) {storage_limited_=true;return false;}
        try {
            pages_.emplace(index,std::make_unique<Page>());
        } catch(const std::bad_alloc&) {
            storage_limited_=true;return false;
        }
    }
    const auto sample_bytes=source_.bits/8;
    for(auto frame=FirstMissing(first,end);frame<end;frame=FirstMissing(frame+1,end)) {
        const auto byte=frame*stride;
        const auto* sample=pcm+(byte-offset);
        auto& page=*pages_.find(byte/kPageBytes)->second;
        std::memcpy(page.data()+byte%kPageBytes,sample,stride);
        const int left=PcmValue(sample,sample_bytes);
        const auto value=static_cast<std::int16_t>(source_.channels==1?left:
            (left+PcmValue(sample+sample_bytes,sample_bytes))/2);
        auto& peak=peaks_[std::uint64_t(frame)*columns_/source_.frames];
        peak.minimum=std::min(peak.minimum,value);
        peak.maximum=std::max(peak.maximum,value);
        ++peak.frames;
        bytes_loaded_+=stride;
    }
    AddCoverage({first,end});
    if(bytes_loaded_==source_.Bytes())state_=State::Ready;
    if(overview_pending_)overview_pending_=NextOverviewFrame()!=source_.frames;
    if(OverviewReady())UpdateLargestGap();
    return true;
}

void SampleWaveformCache::UpdateLargestGap() {
    largest_gap_={};
    std::uint32_t first=0;
    for(const auto range:coverage_) {
        if(range.first-first>largest_gap_.end-largest_gap_.first)
            largest_gap_={first,range.first};
        first=range.end;
    }
    if(source_.frames-first>largest_gap_.end-largest_gap_.first)
        largest_gap_={first,source_.frames};
}

SampleWaveformCache::Range SampleWaveformCache::RefinementWindow(unsigned byte_limit) const {
    const auto length=largest_gap_.end-largest_gap_.first;
    if(!length)return {};
    const auto width=std::min(length,byte_limit/source_.Stride());
    const auto first=largest_gap_.first+(length-width)/2;
    return {first,first+width};
}

bool SampleWaveformCache::ReadFrame(std::uint32_t frame,std::uint8_t* pcm) const {
    if(frame>=source_.frames || FirstMissing(frame,frame+1)==frame)return false;
    const auto offset=frame*source_.Stride();
    const auto& page=*pages_.find(offset/kPageBytes)->second;
    std::memcpy(pcm,page.data()+offset%kPageBytes,source_.Stride());
    return true;
}

std::uint32_t SampleWaveformCache::NextOverviewFrame() const {
    for(unsigned i=0;i<window_count_;++i) {
        const auto& window=windows_[i];
        const auto missing=FirstMissing(window.first,window.end);
        if(missing<window.end)return missing;
    }
    return source_.frames;
}

std::uint64_t SampleWaveformCache::OverviewPlayingEstimateMs() const {
    const auto stride=source_.Stride();
    std::uint64_t ms=0;
    for(unsigned i=0;i<window_count_;++i) {
        const auto& window=windows_[i];
        const auto missing=FirstMissing(window.first,window.end);
        if(missing==window.end)continue;
        ms+=WaveformPlayingEstimateMs(std::uint64_t(window.end-missing)*stride);
        // Nonsequential CAP 3 windows require a separate GET/META exchange.
        if(window_count_>1)ms+=75;
    }
    return ms;
}

void SampleWaveformTransfer::Select(unsigned number,std::string_view path,bool supported) {
    if(number_==number && path_==path && supported_==supported)return;
    if(id_)cancel_=id_;
    id_=0;number_=number;path_=path;supported_=supported;active_=nullptr;used_=nullptr;
    playing_admitted_=false;
    phase_=supported && number && number<=99 && ValidSamplePath(path)?Phase::Get:Phase::Idle;
}

const SampleWaveformCache& SampleWaveformTransfer::View() const {
    static const SampleWaveformCache loading;
    static const auto unavailable=[] { SampleWaveformCache cache;cache.Fail();return cache; }();
    if(active_)return *active_;
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
    if(!playing_ || phase_==Phase::Complete || phase_==Phase::Idle)return false;
    if(!active_)return phase_==Phase::Metadata && metadata_playing_ms_>=kPlayingAllowanceMs;
    if(active_->Metadata().channels==2 || *used_>=kPlayingAllowanceMs)return true;
    if(!active_->OverviewReady())return !playing_admitted_;
    return phase_==Phase::Next && RefinementPlayingEstimateMs()>kPlayingAllowanceMs-*used_;
}

std::uint64_t SampleWaveformTransfer::RefinementPlayingEstimateMs() const {
    const auto stride=active_->Metadata().Stride();
    const auto window=active_->RefinementWindow(kWaveformPlayingBytes);
    if(ranged_)return WaveformPlayingEstimateMs((window.end-window.first)*stride);
    const auto bytes=WaveformReadBytes(active_->Metadata(),window.first*stride,kWaveformPlayingBytes);
    const bool seek=!first_credit_ && window.first*stride!=cursor_;
    // GET carries a hex path. Include wire time for long paths and margin for
    // the additional metadata turnaround even on short paths.
    const auto get_ms=std::max<std::size_t>(75,(2*path_.size()+128+5)/6);
    return WaveformPlayingEstimateMs(bytes)+(seek?get_ms:0);
}

void SampleWaveformTransfer::AdmitPlaying() {
    playing_admitted_=active_ && used_ && active_->Metadata().channels==1 &&
        !active_->OverviewReady() &&
        active_->OverviewPlayingEstimateMs()<=kPlayingAllowanceMs-*used_;
}

void SampleWaveformTransfer::Disconnect() {
    // IDs remain monotonic. Entries are scoped to an acknowledged connection.
    Select(0,{},false);
    for(auto& entry:cache_)entry={};
    next_cache_=0;cancel_=0;budgets_={};retired_through_=0;
    ticked_=playing_=ranged_=false;
}

void SampleWaveformTransfer::Fail() {
    if(id_)cancel_=id_;
    // Keep verified bytes available for a later source revalidation.
    id_=0;phase_=Phase::Idle;active_=nullptr;used_=nullptr;
}

void SampleWaveformTransfer::Tick(std::uint32_t now,bool playing) {
    const auto elapsed=ticked_?now-last_tick_:0;
    const bool awaiting=phase_==Phase::Metadata || phase_==Phase::Data;
    auto* used=active_?used_:phase_==Phase::Metadata?&metadata_playing_ms_:nullptr;
    if(playing_ && used && phase_!=Phase::Complete) {
        const auto waiting=awaiting?std::min(elapsed,now-sent_at_):elapsed;
        const auto charged=!Deferred()?std::min(waiting,kPlayingAllowanceMs-*used):0;
        *used+=charged;
        if(awaiting)sent_at_+=waiting-charged;
    }
    if(playing && !playing_)AdmitPlaying();
    last_tick_=now;ticked_=true;playing_=playing;
    if(!Deferred() && awaiting && now-sent_at_>=12000)Fail();
}

std::size_t SampleWaveformTransfer::Get(char* line,std::size_t capacity,std::uint32_t now) {
    if(sequence_==UINT32_MAX){Fail();return 0;}
    const auto id=sequence_+1;
    const auto n=std::snprintf(line,capacity,"BTWAVE1 GET 1 %lu %u ",(unsigned long)id,number_-1);
    if(n<=0 || std::size_t(n)+1>=capacity ||
       !EncodePathToken(path_.c_str(),line+n,capacity-n-1)){Fail();return 0;}
    const auto end=std::strlen(line);
    line[end]='\n';line[end+1]=0;
    sequence_=id_=id;phase_=Phase::Metadata;sent_at_=now;metadata_playing_ms_=0;
    return end+1;
}

std::size_t SampleWaveformTransfer::Command(char* line,std::size_t capacity,std::uint32_t now) {
    if(cancel_) {
        const auto n=std::snprintf(line,capacity,"BTWAVE1 CANCEL 1 %lu\n",(unsigned long)cancel_);
        if(n<=0 || std::size_t(n)>=capacity)return 0;
        cancel_=0;return n;
    }
    if(phase_==Phase::Get)return Get(line,capacity,now);
    if(phase_!=Phase::Next || Deferred())return 0;

    const auto source=active_->Metadata();
    const bool refining=active_->OverviewReady();
    const auto window=active_->RefinementWindow(playing_?kWaveformPlayingBytes:kWaveformIdleBytes);
    const auto frame=refining?window.first:active_->NextOverviewFrame();
    const auto offset=frame*source.Stride();
    const bool bounded=refining && ranged_;
    if(!bounded && offset!=cursor_ && !first_credit_)return Get(line,capacity,now);
    requested_limit_=bounded?(window.end-window.first)*source.Stride():kWaveformIdleBytes;
    const auto n=bounded
        ? std::snprintf(line,capacity,"BTWAVE1 RANGE 1 %lu %lu %u\n",
            (unsigned long)id_,(unsigned long)offset,requested_limit_)
        : std::snprintf(line,capacity,"BTWAVE1 %s 1 %lu %lu\n",
            offset!=cursor_?"RESUME":"NEXT",(unsigned long)id_,(unsigned long)offset);
    if(n<=0 || std::size_t(n)>=capacity){Fail();return 0;}
    requested_offset_=offset;first_credit_=false;phase_=Phase::Data;sent_at_=now;
    return n;
}

void SampleWaveformTransfer::Response(std::string_view line) {
    if(line==kWaveformRangeCapability) {ranged_=true;return;}
    if(!id_)return;
    std::uint32_t fields[7];
    if(SnapshotFields(line.data(),line.size(),"BTWAVE1 ERROR ",fields,2) &&
       fields[0]==1 && fields[1]==id_) {Fail();return;}
    if(phase_==Phase::Metadata && SnapshotFields(line.data(),line.size(),"BTWAVE1 META ",fields,7) &&
       fields[0]==1 && fields[1]==id_) {
        const WaveformSource source{fields[2],fields[3],fields[4],fields[5],fields[6]};
        auto entry=std::find_if(cache_.begin(),cache_.end(),[&](const auto& cached) {
            return source.token && SameSource(cached.Metadata(),source);
        });
        if(entry==cache_.end()) {
            SampleWaveformCache fresh;
            if(!fresh.Begin(source)){Fail();return;}
            entry=cache_.begin()+next_cache_;
            *entry=std::move(fresh);
            next_cache_=(next_cache_+1)%cache_.size();
        }
        active_=&*entry;
        used_=&Allowance(source.token);
        *used_=std::min(kPlayingAllowanceMs,*used_+metadata_playing_ms_);
        AdmitPlaying();
        cursor_=0;first_credit_=true;
        phase_=active_->Status()==SampleWaveformCache::State::Ready || active_->StorageLimited()
            ? Phase::Complete:Phase::Next;
        return;
    }
    std::uint8_t pcm[kWaveformIdleBytes];
    std::uint32_t id,offset;
    unsigned bytes;
    if(phase_!=Phase::Data || !ParseWaveformData(line,id,offset,pcm,bytes) || id!=id_)return;
    if(offset!=requested_offset_ || bytes>requested_limit_ || !active_->Append(offset,pcm,bytes)) {
        if(active_->StorageLimited())phase_=Phase::Complete;
        else Fail();
        return;
    }
    cursor_=offset+bytes;
    phase_=active_->Status()==SampleWaveformCache::State::Ready?Phase::Complete:Phase::Next;
}

} // namespace BroTracker
