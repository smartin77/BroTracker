#pragma once
#include "local_pattern_editor.h"

// Snapshot at the request boundary; never consult playback channel history.
struct InstrumentEntry {
    EditCursor source{};
    unsigned number=1;
    std::uint8_t original=0xff;
    std::string reason="Instrument unavailable: editor not synchronized";
    explicit operator bool() const noexcept {return reason.empty();}
};
inline InstrumentEntry CaptureInstrumentEntry(const LocalPatternEditor& editor,bool allow_undefined=false) noexcept {
    InstrumentEntry result;result.source=editor.Cursor();
    const auto* pattern=editor.Draft();
    if(!pattern)return result;
    const auto cursor=result.source;
    if(cursor.header) {result.reason="Instrument unavailable: channel header";return result;}
    if(cursor.row>=pattern->active_rows || cursor.channel>=pattern->active_channels)return result;
    const auto cell=pattern->cells[cursor.row][cursor.channel];
    if(cell.note==NOTE_OFF && cursor.field==EditField::Instrument) {
        result.reason="Instrument unavailable: NOTE OFF timing field";return result;
    }
    result.original=cell.instrument;
    if(cell.instrument!=0xff) {
        if(cell.instrument>98 && !allow_undefined) {result.reason="Instrument unavailable: ID outside 01-99";return result;}
        result.number=unsigned(cell.instrument)+1;result.reason.clear();return result;
    }
    for(unsigned row=cursor.row;row>0;) {
        const auto id=pattern->cells[--row][cursor.channel].instrument;
        if(id==0xff)continue;
        if(id>98 && !allow_undefined) {result.reason="Inherited instrument "+std::to_string(unsigned(id)+1)+" unavailable: outside 01-99";return result;}
        result.number=unsigned(id)+1;result.reason.clear();return result;
    }
    result.reason.clear();return result;
}
