#pragma once
#include "core/playback/row_events.h"

// Pattern-context data history, independent of input bindings and visible screens.
// 64 transactions, each with room for every current-capacity active cell.
class PatternEditHistory {
public:
    static constexpr unsigned kCapacity = 64;
    static constexpr unsigned kCells = BroTracker::kRealtimePatternRowCapacity * BroTracker::kRealtimePatternChannelCapacity;
    struct Cell { Note note; std::uint8_t instrument; BroTracker::NoteOffTiming timing; };
    struct Change { std::uint8_t row, channel; Cell before, after; };
    struct Transaction { unsigned count = 0; Change changes[kCells]{}; };
    void Clear() noexcept { first_=count_=applied_=0; }
    bool CanUndo() const noexcept { return applied_!=0; }
    bool CanRedo() const noexcept { return applied_<count_; }
    void Record(const BroTracker::RealtimePattern& before, const BroTracker::RealtimePattern& after) noexcept {
        // Build directly into the next ring slot only after finding a real change.
        bool changed=false;
        for(unsigned r=0;r<before.active_rows;++r) for(unsigned c=0;c<before.active_channels;++c)
            changed |= !Equal(Read(before,r,c),Read(after,r,c));
        if(!changed) return;
        count_=applied_; // discard redo branch
        if(count_==kCapacity) { first_=(first_+1)%kCapacity; --count_; --applied_; }
        auto& tx=transactions_[(first_+count_)%kCapacity]; tx.count=0;
        for(unsigned r=0;r<before.active_rows;++r) for(unsigned c=0;c<before.active_channels;++c) {
            const auto a=Read(before,r,c), b=Read(after,r,c);
            if(!Equal(a,b)) tx.changes[tx.count++]={static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(c),a,b};
        }
        ++count_; ++applied_;
    }
    bool Undo(BroTracker::RealtimePattern& pattern) noexcept {
        if(!CanUndo()) return false;
        Apply(pattern,transactions_[(first_+--applied_)%kCapacity],false); return true;
    }
    bool Redo(BroTracker::RealtimePattern& pattern) noexcept {
        if(!CanRedo()) return false;
        Apply(pattern,transactions_[(first_+applied_++)%kCapacity],true); return true;
    }
private:
    static Cell Read(const BroTracker::RealtimePattern& p,unsigned r,unsigned c) noexcept {
        return {p.cells[r][c].note,p.cells[r][c].instrument,p.note_off_timing[r][c]};
    }
    static bool Equal(Cell a,Cell b) noexcept { return a.note==b.note && a.instrument==b.instrument && a.timing==b.timing; }
    static void Apply(BroTracker::RealtimePattern& p,const Transaction& tx,bool forward) noexcept {
        for(unsigned i=0;i<tx.count;++i) { const auto& change=tx.changes[i]; const auto value=forward?change.after:change.before;
            p.cells[change.row][change.channel].note=value.note;
            p.cells[change.row][change.channel].instrument=value.instrument;
            p.note_off_timing[change.row][change.channel]=value.timing;
        }
    }
    Transaction transactions_[kCapacity]{};
    unsigned first_=0,count_=0,applied_=0;
};
