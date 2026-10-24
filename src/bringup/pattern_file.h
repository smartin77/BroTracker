#pragma once
#include "core/playback/row_events.h"
#include "core/tempo.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BroTracker {
enum class PatternFileKind : std::uint8_t { Development, Session };
enum class PatternFileResult { Ok, Invalid, Unsupported, Missing, Exists, Busy, Io, RecoveryRequired, BackupRetained };
inline const char* PatternFileResultName(PatternFileResult r) noexcept {
    constexpr const char* names[]={"ok","invalid","unsupported","missing","exists","busy","io","recovery-required","backup-retained"};
    return names[static_cast<unsigned>(r)];
}
constexpr std::size_t kPatternFileHeaderSize=24;
constexpr std::size_t kPatternFileCapacity=kPatternFileHeaderSize+3*kRealtimePatternRowCapacity*kRealtimePatternChannelCapacity;
inline std::uint32_t PatternFileU32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
inline void PatternFilePut32(std::uint8_t* p,std::uint32_t n) noexcept {
    for(unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(n>>(8*i));
}
inline std::uint32_t PatternFileCrc(const std::uint8_t* p,std::size_t size) noexcept {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<size;++i) {
        if(i>=20 && i<24) continue; // CRC field is excluded; all other header bytes are covered.
        crc^=p[i];
        for(unsigned b=0;b<8;++b) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return crc^0xffffffffu;
}
inline bool ValidStoredPattern(const RealtimePattern& p,std::uint32_t tempo) noexcept {
    if(!p.active_rows || p.active_rows>kRealtimePatternRowCapacity || !p.active_channels ||
        p.active_channels>kRealtimePatternChannelCapacity || !ValidInteractiveTempo(tempo)) return false;
    for(unsigned r=0;r<p.active_rows;++r) for(unsigned c=0;c<p.active_channels;++c)
        if(!IsPatternNote(p.cells[r][c].note) || !ValidNoteOffTiming(p.note_off_timing[r][c])) return false;
    return true;
}
inline PatternFileResult EncodePatternFile(PatternFileKind kind,const RealtimePattern& p,std::uint32_t tempo,
    std::uint8_t* bytes,std::size_t capacity,std::size_t& size) noexcept {
    const auto needed=kPatternFileHeaderSize+3u*p.active_rows*p.active_channels;
    if(!bytes || !ValidStoredPattern(p,tempo) || capacity<needed) return PatternFileResult::Invalid;
    std::memset(bytes,0,kPatternFileHeaderSize);
    std::memcpy(bytes,kind==PatternFileKind::Session?"BTSS":"BTDP",4);
    bytes[4]=1; bytes[6]=kPatternFileHeaderSize; bytes[8]=p.active_rows; bytes[9]=p.active_channels;
    PatternFilePut32(bytes+12,tempo); PatternFilePut32(bytes+16,static_cast<std::uint32_t>(needed-kPatternFileHeaderSize));
    auto at=kPatternFileHeaderSize;
    for(unsigned r=0;r<p.active_rows;++r) for(unsigned c=0;c<p.active_channels;++c) {
        bytes[at++]=p.cells[r][c].note; bytes[at++]=p.cells[r][c].instrument;
        bytes[at++]=static_cast<std::uint8_t>(p.note_off_timing[r][c]);
    }
    PatternFilePut32(bytes+20,PatternFileCrc(bytes,needed)); size=needed; return PatternFileResult::Ok;
}
inline PatternFileResult ValidatePatternFile(PatternFileKind kind,const std::uint8_t* b,std::size_t size) noexcept {
    if(!b || size<kPatternFileHeaderSize || size>kPatternFileCapacity) return PatternFileResult::Invalid;
    if(std::memcmp(b,kind==PatternFileKind::Session?"BTSS":"BTDP",4) || b[4]!=1 || b[5]!=0)
        return PatternFileResult::Unsupported;
    if(b[6]!=24 || b[7] || b[10] || b[11] || !b[8] || b[8]>kRealtimePatternRowCapacity ||
        !b[9] || b[9]>kRealtimePatternChannelCapacity || !ValidInteractiveTempo(PatternFileU32(b+12)) ||
        PatternFileU32(b+16)!=3u*b[8]*b[9] || size!=kPatternFileHeaderSize+PatternFileU32(b+16) ||
        PatternFileU32(b+20)!=PatternFileCrc(b,size)) return PatternFileResult::Invalid;
    for(std::size_t at=kPatternFileHeaderSize;at<size;at+=3)
        if(!IsPatternNote(b[at]) || b[at+2]>1) return PatternFileResult::Invalid;
    return PatternFileResult::Ok;
}
// Two passes: validate completely before any destination byte/tempo is changed. No full-pattern stack temporary.
inline PatternFileResult DecodePatternFile(PatternFileKind kind,const std::uint8_t* b,std::size_t size,
    RealtimePattern& p,std::uint32_t& tempo) noexcept {
    const auto result=ValidatePatternFile(kind,b,size); if(result!=PatternFileResult::Ok) return result;
    p={}; p.active_rows=b[8]; p.active_channels=b[9]; auto at=kPatternFileHeaderSize;
    for(unsigned r=0;r<p.active_rows;++r) for(unsigned c=0;c<p.active_channels;++c) {
        p.cells[r][c]={b[at],b[at+1]}; p.note_off_timing[r][c]=static_cast<NoteOffTiming>(b[at+2]); at+=3;
    }
    tempo=PatternFileU32(b+12); return PatternFileResult::Ok;
}
}
