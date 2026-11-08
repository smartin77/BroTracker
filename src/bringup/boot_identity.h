#pragma once
#include <cstdint>
#include <cstdio>

namespace BroTracker {
// Boot-owned, once only. Polling never occurs from a protocol request or ISR.
class BootIdentity {
public:
    enum class Read { Waiting, Ready, Failed };
    enum class State { NotStarted, Pending, Valid, Unavailable };
    static constexpr std::uint32_t TimeoutMs=50;
    template<class Source> void Begin(Source& source,std::uint32_t now) {
        if(state_!=State::NotStarted)return;
        began_=now;state_=State::Pending;source.Begin();
    }
    template<class Source> void Poll(Source& source,std::uint32_t now) {
        if(state_!=State::Pending)return;
        if(now-began_>=TimeoutMs) {Fail();return;}
        std::uint32_t words[4]{};
        const auto result=source.ReadWords(words);
        if(result==Read::Failed) {Fail();return;}
        if(result!=Read::Ready)return;
        if(!(words[0]|words[1]|words[2]|words[3])) {Fail();return;}
        for(unsigned i=0;i<4;++i)std::snprintf(text_+8*i,9,"%08lx",static_cast<unsigned long>(words[i]));
        state_=State::Valid;
    }
    void Fail() {if(state_==State::Pending)state_=State::Unavailable;}
    State Status()const {return state_;}
    const char* Text()const {return text_;}
private:
    State state_=State::NotStarted;
    std::uint32_t began_=0;
    char text_[33]{};
};
}
