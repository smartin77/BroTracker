#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace BroTracker {
// Foreground-only FIFO: bytes are reclaimed as USB accepts them. No heap,
// compaction or copying on drain. A wrapped line is sent in two spans.
class SerialTxQueue {
public:
    static constexpr unsigned kBytes=6144,kMessages=51,kLineBytes=640;
    bool Room(unsigned messages,unsigned bytes)const noexcept {
        return messages<=kMessages-count_ && bytes<=kBytes-used_;
    }
    bool Push(const char* text)noexcept {
        const auto length=std::strlen(text);
        if(!length || length>=kLineBytes || !Room(1,length)){++rejected_;return false;}
        const auto tail=(head_+used_)%kBytes;
        const auto first=std::min<std::size_t>(length,kBytes-tail);
        std::memcpy(bytes_+tail,text,first);std::memcpy(bytes_,text+first,length-first);
        lines_[(line_head_+count_)%kMessages]={std::uint16_t(length),0};
        used_+=length;++count_;peak_bytes_=std::max(peak_bytes_,used_);peak_messages_=std::max(peak_messages_,count_);return true;
    }
    const char* Data(std::size_t& length)const noexcept {
        if(!count_){length=0;return nullptr;}
        length=std::min<unsigned>(lines_[line_head_].remaining,kBytes-head_);return bytes_+head_;
    }
    bool Partial()const noexcept{return count_ && lines_[line_head_].sent;}
    void Consume(unsigned bytes)noexcept {
        std::size_t span;Data(span);bytes=std::min<std::size_t>(bytes,span);if(!bytes)return;
        auto& line=lines_[line_head_];line.remaining-=bytes;line.sent+=bytes;
        head_=(head_+bytes)%kBytes;used_-=bytes;
        if(!line.remaining){line_head_=(line_head_+1)%kMessages;--count_;}
    }
    void Clear()noexcept{head_=used_=line_head_=count_=0;}
    unsigned Count()const noexcept{return count_;}
    unsigned Used()const noexcept{return used_;}
    unsigned PeakBytes()const noexcept{return peak_bytes_;}
    unsigned PeakMessages()const noexcept{return peak_messages_;}
    unsigned Rejected()const noexcept{return rejected_;}
private:
    struct Line{std::uint16_t remaining=0,sent=0;};
    char bytes_[kBytes]{};Line lines_[kMessages]{};
    unsigned head_=0,used_=0,line_head_=0,count_=0,peak_bytes_=0,peak_messages_=0,rejected_=0;
};
static_assert(sizeof(SerialTxQueue)==6376,"TX queue fixed storage budget");
}
