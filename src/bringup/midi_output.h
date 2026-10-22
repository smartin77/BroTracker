#pragma once
#include "core/audio/variable_rate_voice_block_renderer.h"
#include <cstdint>

namespace BroTracker {
// One owner at a time. Teensy audio and timer IRQs must have equal priority;
// main-context access requires interrupt exclusion. No allocation or I/O here.
class MidiOutput {
public:
    static constexpr unsigned Capacity=64, ServiceBudget=8;
    static constexpr std::uint32_t DeadlineUs=(128u*1000000u+44100u-1)/44100u;
    struct Counters {
        std::uint32_t submitted=0, busy=0, late=0, overflow=0, disconnect=0;
        std::uint32_t transfers=0, max_lateness_us=0, cleanup_submitted=0, transport_failure=0;
    } counters;
    enum class Selection { Selected, Invalid, RestartRequired };
    Selection Select(unsigned channel) noexcept {
        if(channel<1 || channel>16) return Selection::Invalid;
        if(activated_ && channel!=channel_) return Selection::RestartRequired;
        channel_=channel; activated_=true; return Selection::Selected;
    }
    void TransportFailure() noexcept { if(!fault_) { Increment(counters.transport_failure); Fail(); } }
    unsigned Channel() const noexcept { return channel_; }
    bool Activated() const noexcept { return activated_; }
    bool Faulted() const noexcept { return fault_; }
    bool PendingCleanup() const noexcept { return cleanup_; }
    unsigned Queued() const noexcept { return count_; }
    // Cancel unsent work, release only notes this adapter has submitted.
    // Submission is never an assertion of host/external delivery.
    void Reset() noexcept { head_=count_=0; cleanup_=true; cleanup_cursor_=0; cleanup_all_submitted_=false; }
    // Called only after the backend observes all accepted DMA transfers complete
    // without errors. Host USB completion is still not external delivery proof.
    void ConfirmTransfers() noexcept {
        if(cleanup_) {
            if(!cleanup_all_submitted_) return;
            for(auto& bits:uncertain_) bits=0;
            cleanup_=false;
        } else if(!fault_) {
            for(auto& bits:uncertain_) bits=0;
            for(auto note:notes_) if(note>=0) Mark(static_cast<unsigned>(note));
        }
    }
    void Lifecycle(bool connected, std::uint32_t generation) noexcept {
        if(connected_!=connected || generation_!=generation) {
            if(count_ || HasNotes()) { Increment(counters.disconnect); fault_=true; }
            Reset(); connected_=connected; generation_=generation;
            // Retain submitted ownership for best-effort per-note cleanup after
            // reconnection. Actual external state is unknown while disconnected.
        }
    }
    bool Enqueue(const VariableRateVoiceCommandBatch& batch, std::uint64_t base,
                 std::uint32_t render_us) noexcept {
        if(fault_) return false;
        if(batch.count && !connected_) { Increment(counters.disconnect); Fail(); return false; }
        if(batch.count>kVariableRateVoiceCommandCapacity || batch.count>Capacity-count_) {
            Increment(counters.overflow); Fail(); return false;
        }
        for(unsigned i=0;i<batch.count;++i) {
            const auto& c=batch.commands[i];
            const bool midi=c.action==VariableRateVoiceAction::TriggerMidi;
            const bool valid_action=midi || c.action==VariableRateVoiceAction::Stop ||
                c.action==VariableRateVoiceAction::Trigger || c.action==VariableRateVoiceAction::TriggerSynth;
            if(!valid_action || (midi && !IsPitchedPatternNote(c.synth_note)) ||
               c.channel>=8 || c.sample_offset>=128 || base>UINT64_MAX-c.sample_offset ||
               (count_ && base+c.sample_offset<queue_[(head_+count_-1)%Capacity].sample) ||
               (i && c.sample_offset<batch.commands[i-1].sample_offset)) { Fail(); return false; }
        }
        for(unsigned i=0;i<batch.count;++i) {
            const auto& c=batch.commands[i];
            auto& e=queue_[(head_+count_++)%Capacity];
            e={base+c.sample_offset,render_us+static_cast<std::uint32_t>(c.sample_offset*1000000u/44100u),
               static_cast<std::uint8_t>(c.channel),static_cast<std::uint8_t>(c.synth_note),
               c.action==VariableRateVoiceAction::TriggerMidi};
            Increment(counters.submitted);
        }
        return true;
    }
    // try_submit(words,count) must be all-or-none, nonblocking, and initiate
    // transfer immediately. <=16 USB-MIDI words, <=1 transfer per timer service.
    template<class Submit> void Service(std::uint32_t now, Submit try_submit) noexcept {
        if(!connected_) return;
        std::uint32_t words[16]{}; unsigned n=0;
        if(cleanup_) {
            if(cleanup_all_submitted_) return; // Await backend completion, not just acceptance.
            unsigned cursor=cleanup_cursor_;
            while(cursor<128 && n<ServiceBudget) {
                if(uncertain_[cursor/32] & (1u<<(cursor%32))) words[n++]=Pack(false,cursor);
                ++cursor;
            }
            if(n && !try_submit(words,n)) { Increment(counters.busy); return; }
            if(n) { Increment(counters.transfers); Increment(counters.cleanup_submitted); }
            cleanup_cursor_=cursor;
            if(cursor==128) {
                for(auto& note:notes_) note=-1;
                cleanup_all_submitted_=true;
            }
            return;
        }
        int tentative[8]; for(unsigned c=0;c<8;++c) tentative[c]=notes_[c];
        unsigned consumed=0;
        while(consumed<count_ && consumed<ServiceBudget) {
            const auto& e=queue_[(head_+consumed)%Capacity];
            const auto lateness=static_cast<std::int32_t>(now-e.release_us);
            if(lateness<0) break;
            if(static_cast<std::uint32_t>(lateness)>counters.max_lateness_us) counters.max_lateness_us=lateness;
            if(static_cast<std::uint32_t>(lateness)>DeadlineUs) { Increment(counters.late); Fail(); return; }
            const int old=tentative[e.tracker]; tentative[e.tracker]=-1;
            if(old>=0 && !Contains(tentative,old)) words[n++]=Pack(false,old);
            if(e.on) {
                if(!Contains(tentative,e.note)) words[n++]=Pack(true,e.note);
                tentative[e.tracker]=e.note;
            }
            ++consumed;
        }
        if(!consumed) return;
        if(n && !try_submit(words,n)) { Increment(counters.busy); return; }
        if(n) Increment(counters.transfers);
        // Include OFFs until the backend confirms completion: a failed final
        // OFF must remain recoverable after its logical owner has gone away.
        for(unsigned i=0;i<n;++i) Mark((words[i]>>16)&127u);
        for(unsigned c=0;c<8;++c) notes_[c]=tentative[c];
        head_=(head_+consumed)%Capacity; count_-=consumed;
    }
    // Channel numbering is converted only here, at the USB MIDI adapter.
    std::uint32_t Pack(bool on,unsigned note) const noexcept {
        return (on?9u:8u) | (((on?0x90u:0x80u)|(channel_-1u))<<8) |
            (note<<16) | ((on?100u:0u)<<24);
    }
private:
    static void Increment(std::uint32_t& n) noexcept { if(n!=UINT32_MAX) ++n; }
    static bool Contains(const int* notes,int note) noexcept {
        for(unsigned c=0;c<8;++c) if(notes[c]==note) return true;
        return false;
    }
    void Mark(unsigned note) noexcept { uncertain_[note/32] |= 1u<<(note%32); }
    bool HasNotes() const noexcept { for(auto bits:uncertain_) if(bits) return true; return false; }
    void Fail() noexcept { fault_=true; Reset(); }
    struct Event { std::uint64_t sample=0; std::uint32_t release_us=0; std::uint8_t tracker=0,note=0; bool on=false; };
    Event queue_[Capacity]{};
    int notes_[8]={-1,-1,-1,-1,-1,-1,-1,-1};
    std::uint32_t uncertain_[4]{};
    unsigned cleanup_cursor_=0;
    bool cleanup_all_submitted_=false;
    unsigned head_=0,count_=0,channel_=12;
    bool activated_=false,connected_=false,cleanup_=false,fault_=false;
    std::uint32_t generation_=0;
};
}
