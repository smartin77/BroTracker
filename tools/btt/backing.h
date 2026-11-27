#pragma once
#include "foundation.h"
#include "arena.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace btt::prototype {
// All file mutations in the private namespace MUST go through this registry.
// Files are exclusive-created, write-once, then sealed. No pathname imports,
// renames, overwrite opens, recovery scanning or automatic source deletion.
template<class Backend> class Backings {
public:
    static constexpr unsigned kSlots=3;
    static constexpr uint32_t kMaxBytes=1048576;
    struct Token {uint32_t domain=0,generation=0;unsigned slot=kSlots;};
private:
    enum class State {Free, Writing, Sealed, Retired, Quarantine};
    struct Slot final:Reader,ReaderControl {
        Backings* pool=nullptr;
        typename Backend::Handle file;
        Token token{};
        State state=State::Free;
        uint32_t refs=0,length=0;
        bool writer=false,created=false,poisoned=false,sealed=false;
        Code last=Code::Ok;
        char path[96]{};
        Slot() noexcept = default;
        void retain() noexcept override {++refs;}
        void release() noexcept override {
            if(!--refs)pool->Retire(*this);
        }
        const Reader* reader() const noexcept override {return state==State::Sealed?this:nullptr;}
        uint32_t count() const noexcept override {return refs;}
        uint32_t size() const override {return length;}
        const void* identity() const override {return state==State::Sealed?this:nullptr;}
        unsigned read(uint32_t at,void* p,unsigned n) const override {return pool->Read(token,at,p,n);}
    };
    Backend& backend_;
    std::array<Slot,kSlots> slots_{};
    uint32_t domain_=0,next_=1;
    inline static uint32_t next_domain_=1;
    Slot* Lookup(Token t) noexcept {
        if(t.domain!=domain_ || t.slot>=kSlots)return nullptr;
        auto& s=slots_[t.slot];
        return s.state!=State::Free && s.token.generation==t.generation?&s:nullptr;
    }
    bool Close(Slot& s) noexcept {
        if(!backend_.Close(s.file)) {s.last=Code::Io;s.state=State::Quarantine;return false;}
        return true;
    }
    void Retire(Slot& s) noexcept {
        s.state=State::Retired;
        (void)Close(s); // Last-release cannot report: Inspect/Discard exposes failure.
    }
    Code Abort(Token token) noexcept {
        auto* s=Lookup(token);if(!s)return Code::Stale;
        s->writer=false;s->state=State::Retired;
        return Close(*s)?Code::Ok:Code::Io;
    }
public:
    class Output final:public Writer {
        friend class Backings;
        Backings* pool_=nullptr;
        Token token_{};
        void Release() noexcept {if(pool_)(void)pool_->Abort(token_);pool_=nullptr;}
    public:
        Output() noexcept = default;
        Output(const Output&)=delete;
        Output& operator=(const Output&)=delete;
        Output(Output&& p) noexcept:pool_(p.pool_),token_(p.token_) {p.pool_=nullptr;}
        Output& operator=(Output&& p) noexcept {
            if(this!=&p) {Release();pool_=p.pool_;token_=p.token_;p.pool_=nullptr;}return *this;
        }
        ~Output() {Release();}
        Token token() const noexcept {return token_;}
        const void* identity() const override {return pool_?pool_->Identity(token_):nullptr;}
        unsigned write(uint32_t at,const void* p,unsigned n) override {
            return pool_?pool_->Write(token_,at,p,n):0;
        }
        bool resize(uint32_t n) override {return pool_ && pool_->Resize(token_,n)==Code::Ok;}
        Code Finish(ReaderOwner& candidate) noexcept {
            if(!pool_)return Code::Stale;
            auto* pool=pool_;pool_=nullptr;return pool->Seal(token_,candidate);
        }
        Code Cancel() noexcept {
            if(!pool_)return Code::Stale;
            auto* pool=pool_;pool_=nullptr;return pool->Abort(token_);
        }
    };
    // Backend context must outlive this pool and every pin (e.g. a static SD
    // service). It is not owned or destroyed by a slot or Document.
    explicit Backings(Backend& backend) noexcept:backend_(backend) {
        // A domain is never reused within this process, even when the pool is
        // destroyed/reconstructed at the same address. Exhaustion is fallible.
        domain_=next_domain_;
        if(next_domain_)++next_domain_;
        for(auto& s:slots_)s.pool=this;
    }
    Backings(const Backings&)=delete;
    Backings& operator=(const Backings&)=delete;
    bool CanDestroy() const noexcept {
        for(const auto& s:slots_)if(s.refs || s.writer || s.state==State::Quarantine || backend_.Open(s.file))return false;
        return true;
    }
    // Infrequent registry mutations are cold. Reads, acquisition, reference
    // release/close and asynchronous Sample preparation retain their placement.
    BTT_FLASHMEM("backing_create") Code Create(Output& candidate,Token& token) noexcept {
        if(!domain_ || !next_)return Code::Capacity;
        if(candidate.pool_)return Code::Busy;
        unsigned index=0;while(index<kSlots && slots_[index].state!=State::Free)++index;
        if(index==kSlots)return Code::Capacity;
        auto& s=slots_[index];s.token={domain_,next_++,index};s.last=Code::Ok;
        s.length=0;s.poisoned=false;s.created=false;s.sealed=false;
        std::snprintf(s.path,sizeof(s.path),"/BroTracker/.btt-prototype/%08lx-%u-%08lx.btt",
            static_cast<unsigned long>(domain_),index,static_cast<unsigned long>(s.token.generation));
        if(!backend_.Create(s.file,s.path)) {
            // O_EXCL failure never authorizes deletion of an existing file.
            s.state=State::Retired;
            if(Close(s))s.state=State::Free;
            return Code::Io;
        }
        s.created=true;s.state=State::Writing;s.writer=true;
        candidate.pool_=this;candidate.token_=s.token;token=s.token;return Code::Ok;
    }
    Code Acquire(Token token,ReaderOwner& candidate) noexcept {
        auto* s=Lookup(token);if(!s)return Code::Stale;
        if(s->state==State::Retired && s->created && s->sealed) {
            uint32_t length=0;
            if(!backend_.ReadOpen(s->file,s->path,length) || length!=s->length) {
                (void)Close(*s);s->last=Code::Io;return Code::Io;
            }
            s->state=State::Sealed;
        }
        if(s->state!=State::Sealed)return s->state==State::Quarantine?Code::Io:Code::Busy;
        // Copies cannot exhaust uint32 refs within the bounded arena/target
        // address space. Explicit acquisitions still guard the counter limit.
        if(s->refs==std::numeric_limits<uint32_t>::max())return Code::Capacity;
        candidate=ReaderOwner::Pin(*s);return Code::Ok;
    }
    Code OpenWriter(Token token,Output&) noexcept {
        return Lookup(token)?Code::Busy:Code::Stale; // Write-once: only Create grants a writer.
    }
    const void* Identity(Token token) noexcept {return Lookup(token);}
    Code Inspect(Token token) noexcept {auto* s=Lookup(token);return s?s->last:Code::Stale;}
    BTT_FLASHMEM("backing_discard") Code Discard(Token token) noexcept {
        auto* s=Lookup(token);if(!s)return Code::Stale;
        if(s->refs || s->writer)return Code::Busy;
        if(!Close(*s))return Code::Io;
        if(s->created && !backend_.Remove(s->path)) {s->last=Code::Io;return Code::Io;}
        s->state=State::Free;s->created=false;return Code::Ok;
    }
    unsigned Read(Token token,uint32_t at,void* p,unsigned n) noexcept {
        auto* s=Lookup(token);
        if(!s || s->state!=State::Sealed || !p || n>512 || at>s->length || n>s->length-at)return 0;
        if(!backend_.Read(s->file,at,p,n)) {s->last=Code::Io;return 0;}
        return n;
    }
private:
    // Only the unique Output lease can mutate or seal its file. In particular,
    // a caller cannot seal via a token and leave an old writer able to abort a pin.
    BTT_FLASHMEM("backing_write") unsigned Write(Token token,uint32_t at,const void* p,unsigned n) noexcept {
        auto* s=Lookup(token);
        if(!s || s->state!=State::Writing || !s->writer || s->poisoned)return 0;
        if(!p || n>512 || at>kMaxBytes || n>kMaxBytes-at) {s->last=Code::Invalid;s->poisoned=true;return 0;}
        if(!backend_.Write(s->file,at,p,n)) {s->last=Code::Io;s->poisoned=true;return 0;}
        s->length=std::max<uint32_t>(s->length,at+n);return n;
    }
    BTT_FLASHMEM("backing_resize") Code Resize(Token token,uint32_t n) noexcept {
        auto* s=Lookup(token);if(!s)return Code::Stale;
        if(s->state!=State::Writing || !s->writer || s->poisoned)return Code::Busy;
        if(n>kMaxBytes)return Code::Capacity;
        if(!backend_.Resize(s->file,n)) {s->poisoned=true;s->last=Code::Io;return Code::Io;}
        s->length=n;return Code::Ok;
    }
    BTT_FLASHMEM("backing_seal") Code Seal(Token token,ReaderOwner& candidate) noexcept {
        auto* s=Lookup(token);if(!s)return Code::Stale;
        if(s->state!=State::Writing || !s->writer)return Code::Busy;
        s->writer=false;
        const bool synced=!s->poisoned && backend_.Sync(s->file);
        s->state=State::Retired;
        const bool closed=Close(*s);
        if(!synced || !closed) {s->last=Code::Io;return Code::Io;}
        s->sealed=true;
        const auto result=Acquire(token,candidate);
        if(result!=Code::Ok)s->last=result;
        return result;
    }
};
}
