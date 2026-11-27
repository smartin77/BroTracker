#pragma once
// Provisional developer-only BTT schema. No production runtime integration.
#include "code_placement.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace btt {
enum class Result { Ok, Invalid, Unsupported, Capacity, Io, Backing };
// Implementations guarantee immutable bytes for their entire owned lifetime.
// identity() names the underlying storage, including across wrapper objects.
struct Reader {
    virtual ~Reader() = default;
    virtual uint32_t size() const = 0;
    virtual const void* identity() const = 0;
    virtual unsigned read(uint32_t, void*, unsigned) const = 0;
};
// Foreground-only fixed owners implement this small control boundary. The
// control must outlive its pins. Copies only increment an existing control;
// they never allocate. Acquisition/exhaustion belongs to the bounded owner.
struct ReaderControl {
    virtual void retain() noexcept = 0;
    virtual void release() noexcept = 0;
    virtual const Reader* reader() const noexcept = 0;
    virtual uint32_t count() const noexcept = 0;
protected:
    ~ReaderControl() = default;
};
class ReaderOwner {
    std::shared_ptr<const Reader> shared_; // Compatibility with existing host fixtures.
    ReaderControl* fixed_=nullptr;
public:
    ReaderOwner() noexcept = default;
    template<class T> ReaderOwner(std::shared_ptr<T> p) noexcept:shared_(std::move(p)){}
    static ReaderOwner Pin(ReaderControl& c) noexcept {
        ReaderOwner p;
        if(c.reader()) {c.retain();p.fixed_=&c;}
        return p;
    }
    ReaderOwner(const ReaderOwner& p) noexcept:shared_(p.shared_),fixed_(p.fixed_) {if(fixed_)fixed_->retain();}
    ReaderOwner(ReaderOwner&& p) noexcept:shared_(std::move(p.shared_)),fixed_(p.fixed_) {p.fixed_=nullptr;}
    ~ReaderOwner() {if(fixed_)fixed_->release();}
    ReaderOwner& operator=(ReaderOwner p) noexcept {swap(p);return *this;}
    void swap(ReaderOwner& p) noexcept {shared_.swap(p.shared_);std::swap(fixed_,p.fixed_);}
    void reset() noexcept {ReaderOwner{}.swap(*this);}
    const Reader* get() const noexcept {return fixed_?fixed_->reader():shared_.get();}
    const Reader* operator->() const noexcept {return get();}
    const Reader& operator*() const noexcept {return *get();}
    explicit operator bool() const noexcept {return get()!=nullptr;}
    long use_count() const noexcept {return fixed_?static_cast<long>(fixed_->count()):shared_.use_count();}
};
struct Writer {
    virtual ~Writer() = default;
    virtual const void* identity() const = 0;
    virtual unsigned write(uint32_t, const void*, unsigned) = 0;
    virtual bool resize(uint32_t) = 0;
};
struct Entry { char type[4]{}; uint32_t id=0, offset=0, length=0, crc=0, flags=0; };
struct Span { uint32_t offset=0, length=0; uint8_t encoding=0; bool present=false; };
struct Workspace {
    std::array<uint8_t,512> block{};
    std::array<uint16_t,256> tags{};
    std::array<Span,8> fields{};
    unsigned field_count=0;
    uint64_t logical_fields=0;
};
struct Limits { unsigned entries=256, patterns=32, instruments=99, orders=1024, cells=512; };
struct Status {
    bool unsupported_values=false, unresolved_patterns=false, unresolved_instruments=false;
    // Resource resolution is deliberately not attempted on the host filesystem.
    bool resources_unchecked=false;
};
struct Document {
    ReaderOwner backing;
    std::array<Entry,256> entries{};
    unsigned count=0, channels=0;
    uint32_t bpm=0, next_instrument=0, next_pattern=0;
    uint32_t saved_bytes=0, whole_crc=0;
    uint16_t minor=0;
    Status status{};
    bool accepted=false;
};
struct Cell {
    uint8_t note=255, timing=0;
    uint32_t instrument=0;
    std::array<uint16_t,3> fx{};
    std::array<uint8_t,3> values{};
};
// Naturally aligned arrays retain the 15 logical bytes per cell without
// retaining Cell's ABI padding. Cell remains a temporary accessor value.
struct Pattern {
    std::array<uint8_t,512> notes{}, timing{};
    std::array<uint32_t,512> instruments{};
    std::array<std::array<uint16_t,3>,512> fx{};
    std::array<std::array<uint8_t,3>,512> values{};
    uint16_t rows=0;
    uint8_t channels=0;
    bool ready=false;

    Pattern() noexcept { notes.fill(255); }
    void Clear(unsigned r,unsigned c) {
        ready=false;rows=static_cast<uint16_t>(r);channels=static_cast<uint8_t>(c);
        notes.fill(255);timing.fill(0);instruments.fill(0);fx.fill({});values.fill({});
    }
    Cell At(unsigned i) const { return {notes[i],timing[i],instruments[i],fx[i],values[i]}; }
    void Put(unsigned i,const Cell& c) {
        notes[i]=c.note;timing[i]=c.timing;instruments[i]=c.instrument;fx[i]=c.fx;values[i]=c.values;
    }
};
const Entry* Find(const Document&, const char*, uint32_t);
// All outputs are caller-owned candidates, disposable on failure. No operation
// publishes state. Never pass active/current state as a candidate.
// Infrequent validation/inspection and stopped rewrite/save use XIP flash on
// Teensy. Materialize/Find and shared parsing/read helpers retain ITCM placement
// for future foreground pattern access; no playback/cache timing is implied.
BTT_FLASHMEM("validate") Result Validate(ReaderOwner, Document& candidate, Workspace&, Limits = {});
Result Materialize(const Document&, uint32_t id, Pattern& candidate, Workspace&, unsigned capacity=512);
// Compact saved-data view. Opaque/unsupported field bytes remain recoverable
// through section_offset/length and an owned backing; never a runtime projection.
struct SavedInstrument {
    uint32_t id=0,section_offset=0,section_length=0,resource_offset=0,resource_length=0;
    uint16_t type=0,root=0,fine=0,midi=0;
    uint8_t resource_encoding=0,present=0,understood=0;
    bool resource_present=false;
    // Bit 0 Root, bit 1 Fine, bit 2 MIDI. Presence is independent of value.
};
BTT_FLASHMEM("inspect_instrument") Result InspectInstrument(const Document&,uint32_t id,SavedInstrument& candidate,Workspace&);
// bpm=0 retains saved BPM. Inserts each absent Sample tuning default independently.
// Writes only a distinct temporary backing. Caller must finish/sync/close, reopen,
// Validate and compare before promotion; original ownership survives every failure.
BTT_FLASHMEM("rewrite") Result Rewrite(const Document&, Writer& temporary, Workspace&, uint32_t bpm=0);
// Abstract host transaction. finish() checks sync/close; reopen() returns owned
// immutable candidate storage. promote() must retain the previous valid bytes on
// every failure. This contract is tested with a fake, not a FAT implementation.
struct Store {
    virtual ~Store() = default;
    virtual bool finish() = 0;
    virtual ReaderOwner reopen() = 0;
    virtual bool promote() = 0;
};
BTT_FLASHMEM("save") Result Save(const Document&, Writer& temporary, Store&, Document& reopened_candidate,
            Workspace&, uint32_t bpm=0);
}
