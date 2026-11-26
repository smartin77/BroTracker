#include "resource_admission.h"
#include <cstring>

namespace btt::prototype {
void Candidate::ReleaseRuntime() noexcept {
    for(auto& d:definitions_) {
        d.source.Reset();d.path.Reset();
        if(d.source_status==SourceStatus::Ready)d.source_status=SourceStatus::Released;
        if(d.path_status==PathStatus::Owned)d.path_status=PathStatus::Valid;
    }
}
Admission Candidate::Block(Admission result) noexcept {ReleaseRuntime();state_=result;return result;}
const Definition* Candidate::Find(uint32_t id) const noexcept {
    for(unsigned i=0;i<count_;++i)if(definitions_[i].saved.id==id)return &definitions_[i];
    return nullptr;
}
Result Candidate::Path(Definition& d,AdmissionScratch& scratch) noexcept {
    const auto& s=d.saved;
    if(!s.resource_present) {d.path_status=PathStatus::Absent;return Result::Ok;}
    if(s.resource_encoding!=6) {d.path_status=PathStatus::UnsupportedEncoding;return Result::Ok;}
    if(!s.resource_length) return Result::Invalid;
    uint8_t base=0;
    if(backing_->read(s.resource_offset,&base,1)!=1)return Result::Io;
    if(base) {d.path_status=PathStatus::UnavailableBase;return Result::Ok;}
    const auto bytes=s.resource_length-1;
    if(bytes>BroTracker::kSamplePathMaximum) {d.path_status=PathStatus::TooLong;return Result::Ok;}
    if(backing_->read(s.resource_offset+1,scratch.path,bytes)!=bytes)return Result::Io;
    scratch.path[bytes]=0;
    d.path_status=BroTracker::ValidSamplePath(std::string_view(scratch.path,bytes))?PathStatus::Valid:PathStatus::Invalid;
    return Result::Ok;
}
Result Candidate::Begin(const Document& document,InitialRequirements initial,Workspace& work,
                        Pattern& pattern,AdmissionScratch& scratch) noexcept {
    if(linked_)return Result::Backing;
    (void)Discard();
    if(!document.accepted || !document.backing)return Result::Backing;
    backing_=document.backing;initial_pattern_=initial.pattern_id;
    for(unsigned i=0;i<document.count;++i) {
        const auto& e=document.entries[i];if(std::memcmp(e.type,"INST",4))continue;
        if(count_==definitions_.size()) {Block(Admission::Blocked);return Result::Capacity;}
        auto& d=definitions_[count_++];
        const auto result=InspectInstrument(document,e.id,d.saved,work);
        if(result!=Result::Ok) {Block(Admission::Io);return result;}
        const auto& s=d.saved;
        if(s.type==1) {d.effective_root=s.present&1?s.root:60;d.effective_fine=s.present&2?s.fine:0;}
        if(s.type==2)d.effective_midi=s.present&4?s.midi:1;
        if(s.type<1 || s.type>3)d.settings=SettingStatus::UnsupportedType;
        else if(s.present!=s.understood)d.settings=SettingStatus::UnsupportedEncoding;
        else if((s.type!=1 && (s.present&3)) || (s.type!=2 && (s.present&4)) || (s.type!=1 && s.resource_present))
            d.settings=SettingStatus::Inappropriate;
        else if((s.type==1 && (d.effective_root<24 || d.effective_root>127 || d.effective_fine>99)) ||
                (s.type==2 && (d.effective_midi<1 || d.effective_midi>16)))d.settings=SettingStatus::UnsupportedValue;
        const auto path=Path(d,scratch);if(path!=Result::Ok) {Block(Admission::Io);return path;}
    }
    const auto materialized=Materialize(document,initial.pattern_id,pattern,work);
    if(materialized!=Result::Ok) {Block(Admission::Io);return materialized;}
    auto require=[&](uint32_t id) {
        if(!id)return;
        for(unsigned i=0;i<count_;++i)if(definitions_[i].saved.id==id) {
            definitions_[i].required=true;
            if(definitions_[i].saved.type==1)definitions_[i].source_status=SourceStatus::NotPrepared;
            return;
        }
        unresolved_=true;
    };
    for(unsigned c=0;c<pattern.channels;++c)require(initial.incoming_instruments[c]);
    auto current=initial.incoming_instruments;
    for(unsigned i=0;i<pattern.rows*pattern.channels;++i) {
        const auto cell=pattern.At(i);const unsigned channel=i%pattern.channels;
        if(cell.instrument) {current[channel]=cell.instrument;require(cell.instrument);}
        if(cell.note>=24 && cell.note<=127 && !current[channel])unresolved_=true;
    }
    state_=Admission::Scanned;return Result::Ok;
}
Admission Candidate::Step(SourceService& service,AdmissionScratch& scratch) noexcept {
    if(linked_ || (state_!=Admission::Scanned && state_!=Admission::Progress))return state_;
    if(unresolved_)return Block(Admission::Blocked);
    while(cursor_<count_) {
        auto& d=definitions_[cursor_];
        if(!d.required) {++cursor_;continue;}
        if(d.settings!=SettingStatus::Supported)return Block(Admission::Blocked);
        if(d.saved.type!=1) {++cursor_;continue;}
        if(!d.path.Get()) {
            const auto result=Path(d,scratch);
            if(result!=Result::Ok) {d.source_status=SourceStatus::Io;return Block(Admission::Io);}
            if(d.path_status!=PathStatus::Valid)return Block(Admission::Blocked);
            if(!d.path.Assign(scratch.path)) {d.path_status=PathStatus::Capacity;return Block(Admission::Blocked);}
            d.path_status=PathStatus::Owned;d.source_status=SourceStatus::Pending;state_=Admission::Progress;return state_;
        }
        const auto result=service.Acquire(d.path.Get(),d.source);d.source_status=result;
        if(result==SourceStatus::Ready && !d.source) {d.source_status=SourceStatus::Io;return Block(Admission::Io);}
        if(result!=SourceStatus::Ready) return Block(result==SourceStatus::Io?Admission::Io:Admission::Blocked);
        ++cursor_;state_=Admission::Progress;return state_;
    }
    state_=Admission::Ready;return state_;
}
Code Candidate::Cancel() noexcept {
    if(linked_)return Code::Busy;
    ReleaseRuntime();state_=Admission::Cancelled;return Code::Ok;
}
Code Candidate::Discard() noexcept {
    if(linked_)return Code::Busy;
    ReleaseRuntime();backing_.reset();
    for(auto& d:definitions_) {
        d.saved={};d.effective_root=d.effective_fine=d.effective_midi=0;
        d.path_status=PathStatus::Absent;d.settings=SettingStatus::Supported;d.source_status=SourceStatus::NotRequired;d.required=false;
    }
    count_=cursor_=0;initial_pattern_=0;unresolved_=false;state_=Admission::Empty;return Code::Ok;
}
Code StoppedExchange::Commit(Candidate& next,bool stopped) noexcept {
    if(!stopped || previous_ || next.linked_ || !next.initial_resources_ready())return Code::Busy;
    previous_=current_;current_=&next;next.linked_=true;return Code::Ok;
}
Code StoppedExchange::Rollback(bool stopped) noexcept {
    if(!stopped || !previous_)return Code::Busy;
    std::swap(current_,previous_);return Code::Ok;
}
Code StoppedExchange::ReleasePrevious(bool stopped) noexcept {
    if(!stopped)return Code::Busy;
    if(previous_) {previous_->linked_=false;(void)previous_->Discard();previous_=nullptr;}
    return Code::Ok;
}
Code StoppedExchange::Detach(bool stopped) noexcept {
    if(!stopped || previous_)return Code::Busy;
    if(current_)current_->linked_=false;
    current_=nullptr;return Code::Ok;
}
}
