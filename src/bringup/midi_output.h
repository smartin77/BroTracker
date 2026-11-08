#pragma once
#include "core/audio/variable_rate_voice_block_renderer.h"
#include "device_facts.h"
#include <cstdint>

namespace BroTracker {
// One owner at a time. Teensy audio and timer IRQs must have equal priority;
// main-context access requires interrupt exclusion. No allocation or I/O here.
class MidiOutput {
public:
    static constexpr unsigned Capacity=1024, ServiceBudget=8;
    static constexpr std::uint32_t MaximumNoteDelayMs=160;
    // 120ms: (42+2)*16=704; 160ms: (56+2)*16=928. Round up to
    // a power of two to retain cheap ring indexing; service margin is unchanged.
    static_assert(Capacity>=((std::uint64_t(MaximumNoteDelayMs)*44100u+127999u)/128000u+2u)*kVariableRateVoiceCommandCapacity,
                  "Delay horizon must fit the fixed queue");
    static_assert((Capacity&(Capacity-1u))==0,"Keep ring indexing power-of-two bounded");
    enum class DelaySetting { Applied, Invalid, NotStopped, Pending, Cleanup, AudioNotReady };
    DelaySetting NoteDelayReadiness(bool stopped) const noexcept {
        if(!stopped)return DelaySetting::NotStopped;
        if(count_ || cancel_mask_)return DelaySetting::Pending;
        // A latched playback fault can remain after cleanup is confirmed. It
        // still requires existing START recovery, but must not deadlock setup.
        if(cleanup_ || abort_ || HasNotes())return DelaySetting::Cleanup;
        return DelaySetting::Applied;
    }
    DelaySetting SetNoteDelayMs(std::uint32_t ms,AudioOutputChoice route,
                               AudioInitialization audio,bool stopped) noexcept {
        const bool known_route=route==AudioOutputChoice::Default || route==AudioOutputChoice::Bridge ||
            route==AudioOutputChoice::Pt8211 || route==AudioOutputChoice::AudioShield;
        const bool known_state=audio==AudioInitialization::Uninitialized || audio==AudioInitialization::Initialized ||
            audio==AudioInitialization::Failed;
        if(ms>MaximumNoteDelayMs || !known_route || !known_state ||
            ((route==AudioOutputChoice::Default)!=(audio==AudioInitialization::Uninitialized)))return DelaySetting::Invalid;
        const auto readiness=NoteDelayReadiness(stopped);
        if(readiness!=DelaySetting::Applied)return readiness;
        if(ms && audio!=AudioInitialization::Initialized)return DelaySetting::AudioNotReady;
        // Output queue timebase is micros(): integer milliseconds are exact.
        delay_us_=route==AudioOutputChoice::Bridge && audio==AudioInitialization::Initialized ?
            static_cast<std::uint32_t>(std::uint64_t(ms)*1000u):0;
        return DelaySetting::Applied;
    }
    std::uint32_t NoteDelayUs() const noexcept {return delay_us_;}
    static constexpr std::uint32_t DeadlineUs=(128u*1000000u+44100u-1)/44100u;
    struct Counters {
        std::uint32_t submitted=0, busy=0, late=0, overflow=0, disconnect=0;
        std::uint32_t transfers=0, max_lateness_us=0, cleanup_submitted=0, transport_failure=0;
    } counters;
    enum class Fault { None, Unavailable, Deadline, Overflow, Transport, Invalid, Lifecycle, Timer, CompletionStall };
    Fault Reason() const noexcept { return reason_; }
    bool Available() const noexcept { return connected_; } // USB configuration only, not a consumer.
    bool TakeAbortRequest() noexcept { const bool result=abort_; abort_=false; return result; }
    // Explicit STOP -> START boundary only. Completion cannot auto-resume a song.
    bool Recover(bool stopped) noexcept {
        if(!stopped || !connected_ || cleanup_ || abort_) return false;
        fault_=false; reason_=Fault::None; cleanup_blocked_=false; return true;
    }
    enum class Selection { Selected, Invalid, RestartRequired };
    Selection Select(unsigned channel) noexcept {
        if(channel<1 || channel>16) return Selection::Invalid;
        if(activated_ && channel!=channel_) return Selection::RestartRequired;
        channel_=channel; activated_=true; return Selection::Selected;
    }
    void TransportFailure(bool stalled=false) noexcept {
        if(fault_ && !cleanup_sent_) return;
        Increment(counters.transport_failure);
        const bool already_failed=fault_;
        Fail(stalled ? Fault::CompletionStall : Fault::Transport);
        // One best-effort cleanup pass per failure/reset. A stalled cleanup is
        // retained as uncertain, not retried endlessly or called delivered.
        cleanup_blocked_=already_failed;
    }
    void TimerFailure() noexcept { Fail(Fault::Timer); }
    unsigned Channel() const noexcept { return channel_; }
    bool Activated() const noexcept { return activated_; }
    bool Faulted() const noexcept { return fault_; }
    bool PendingCleanup() const noexcept { return cleanup_; }
    unsigned Queued() const noexcept { return count_; }
    // Quarantined uncertainty is not active I/O. Keep the pitch ledger/fault,
    // but allow stopped/paused storage once cancellation has completed and no
    // DMA or queued event can still run. Explicit Reset re-arms cleanup and
    // blocks storage again until completed or safely quarantined.
    bool StorageReady(bool backend_idle) const noexcept {
        return backend_idle && !abort_ && !count_ && !cancel_mask_ &&
            (!cleanup_ || (fault_ && cleanup_blocked_));
    }

    // Cancel unsent work, release only notes this adapter has submitted.
    // Submission is never an assertion of host/external delivery.
    void Reset() noexcept { head_=count_=cancel_mask_=0; cleanup_=true; cleanup_cursor_=0; cleanup_all_submitted_=false; cleanup_sent_=false; cleanup_blocked_=false; abort_=true; }
    // Successful render handoff only. Compact once, retaining peer order, then
    // release submitted ownership through the immediate (undelayed) service path.
    void CancelChannels(unsigned mask) noexcept {
        mask&=255u;unsigned retained=0;
        for(unsigned i=0;i<count_;++i) {
            const auto event=queue_[(head_+i)%Capacity];
            if(!(mask&(1u<<event.tracker)))queue_[(head_+retained++)%Capacity]=event;
        }
        count_=retained;
        if(!cleanup_ && !fault_)cancel_mask_|=mask; // Global/quarantined cleanup already owns release.
    }
    // Called only after the backend observes all accepted DMA transfers complete
    // without errors. Host USB completion is still not external delivery proof.
    void ConfirmTransfers() noexcept {
        if(cleanup_) {
            if(cleanup_blocked_ || !cleanup_all_submitted_) return;
            for(auto& bits:uncertain_) bits=0;
            cleanup_=false;
        } else if(!fault_) {
            for(auto& bits:uncertain_) bits=0;
            for(auto note:notes_) if(note>=0) Mark(static_cast<unsigned>(note));
        }
    }
    void Lifecycle(bool connected, std::uint32_t generation) noexcept {
        if(connected_!=connected || generation_!=generation) {
            if(count_ || HasNotes()) { Increment(counters.disconnect); if(!fault_) reason_=Fault::Lifecycle; fault_=true; }
            Reset(); connected_=connected; generation_=generation;
            // Retain submitted ownership for best-effort per-note cleanup after
            // reconnection. Actual external state is unknown while disconnected.
        }
    }
    bool Enqueue(const VariableRateVoiceCommandBatch& batch, std::uint64_t base,
                 std::uint32_t render_us) noexcept {
        if(fault_) return false;
        if(batch.count && !connected_) { Increment(counters.disconnect); Fail(Fault::Unavailable); return false; }
        if(batch.count>kVariableRateVoiceCommandCapacity || batch.count>Capacity-count_) {
            Increment(counters.overflow); Fail(Fault::Overflow); return false;
        }
        for(unsigned i=0;i<batch.count;++i) {
            const auto& c=batch.commands[i];
            const bool midi=c.action==VariableRateVoiceAction::TriggerMidi;
            const bool valid_action=midi || c.action==VariableRateVoiceAction::Release || c.action==VariableRateVoiceAction::Stop ||
                c.action==VariableRateVoiceAction::Trigger || c.action==VariableRateVoiceAction::TriggerSynth;
            if(!valid_action || (midi && !IsPitchedPatternNote(c.synth_note)) ||
               c.channel>=8 || c.sample_offset>=128 || base>UINT64_MAX-c.sample_offset ||
               (count_ && base+c.sample_offset<queue_[(head_+count_-1)%Capacity].sample) ||
               (i && c.sample_offset<batch.commands[i-1].sample_offset)) { Fail(Fault::Invalid); return false; }
        }
        for(unsigned i=0;i<batch.count;++i) {
            const auto& c=batch.commands[i];
            auto& e=queue_[(head_+count_++)%Capacity];
            e={base+c.sample_offset,render_us+static_cast<std::uint32_t>(c.sample_offset*1000000u/44100u)+delay_us_,
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
            if(cleanup_blocked_ || cleanup_all_submitted_) return; // Await backend completion, not just acceptance.
            unsigned cursor=cleanup_cursor_;
            while(cursor<128 && n<ServiceBudget) {
                if(uncertain_[cursor/32] & (1u<<(cursor%32))) words[n++]=Pack(false,cursor);
                ++cursor;
            }
            if(n && !try_submit(words,n)) { Increment(counters.busy); return; }
            if(n) { Increment(counters.transfers); Increment(counters.cleanup_submitted); cleanup_sent_=true; }
            cleanup_cursor_=cursor;
            if(cursor==128) {
                for(auto& note:notes_) note=-1;
                cleanup_all_submitted_=true;
            }
            return;
        }
        int tentative[8]; for(unsigned c=0;c<8;++c) tentative[c]=notes_[c];
        if(cancel_mask_) {
            for(unsigned c=0;c<8;++c)if(cancel_mask_&(1u<<c))tentative[c]=-1;
            for(unsigned c=0;c<8;++c)if((cancel_mask_&(1u<<c)) && notes_[c]>=0 && !Contains(tentative,notes_[c])) {
                const auto word=Pack(false,notes_[c]);
                bool duplicate=false;for(unsigned i=0;i<n;++i)duplicate|=words[i]==word;
                if(!duplicate)words[n++]=word;
            }
            if(n && !try_submit(words,n)) {Increment(counters.busy);return;}
            if(n) {Increment(counters.transfers);for(unsigned i=0;i<n;++i)Mark((words[i]>>16)&127u);}
            for(unsigned c=0;c<8;++c)notes_[c]=tentative[c];
            cancel_mask_=0;return; // At most one transfer and eight urgent OFFs.
        }
        unsigned consumed=0;
        while(consumed<count_ && consumed<ServiceBudget) {
            const auto& e=queue_[(head_+consumed)%Capacity];
            const auto lateness=static_cast<std::int32_t>(now-e.release_us);
            if(lateness<0) break;
            if(static_cast<std::uint32_t>(lateness)>counters.max_lateness_us) counters.max_lateness_us=lateness;
            if(static_cast<std::uint32_t>(lateness)>DeadlineUs) { Increment(counters.late); Fail(Fault::Deadline); return; }
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
    void Fail(Fault reason) noexcept { if(!fault_) reason_=reason; fault_=true; Reset(); }
    struct Event { std::uint64_t sample=0; // Musical sample position, never shifted.
        std::uint32_t release_us=0; // Delivery deadline, including fixed delay.
        std::uint8_t tracker=0,note=0; bool on=false; };
    Event queue_[Capacity]{};
    int notes_[8]={-1,-1,-1,-1,-1,-1,-1,-1};
    std::uint32_t uncertain_[4]{};
    unsigned cleanup_cursor_=0;
    bool cleanup_all_submitted_=false,cleanup_sent_=false;
    unsigned head_=0,count_=0,channel_=12;
    bool activated_=false,connected_=false,cleanup_=false,fault_=false,abort_=false,cleanup_blocked_=false;
    Fault reason_=Fault::None;
    std::uint32_t generation_=0;
    std::uint32_t delay_us_=0;
    unsigned cancel_mask_=0;
};
// Shared platform handoff policy: always drain the engine export, even while a
// sink is quarantined. MIDI admission failure never mutates core/audio control.
template<class Control>
void CollectMidiBlock(Control& control, MidiOutput& midi, std::uint32_t render_us,
                      unsigned frames=128) noexcept {
    auto& player=control.OutputPlayer().MidiPlayer();
    if(player.TakeMidiReset()) midi.Reset();
    auto cancelled=player.TakeMidiCancelMask();
    if(!midi.NoteDelayUs())cancelled=0; // Preserve the existing zero-delay mute handoff exactly.
    if(cancelled)midi.CancelChannels(cancelled);
    auto batch=player.TakeMidiCommands();
    unsigned retained=0;
    for(unsigned i=0;i<batch.count;++i) {
        const auto& c=batch.commands[i];
        if(c.channel<8 && c.action==VariableRateVoiceAction::Release && !c.sample_offset && (cancelled&(1u<<c.channel)))continue;
        batch.commands[retained++]=c;
    }
    batch.count=retained;
    if(batch.count) (void)midi.Enqueue(batch,player.GetNextSamplePosition()-frames,render_us);
}
}
