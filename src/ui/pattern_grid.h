#pragma once
#include <algorithm>
#include <cstdint>

namespace PatternGrid {
constexpr int channel_x=30,channel_width=53,header_y=29,header_height=26;
// 5x7 bitmap ink becomes 10x14, centered in the unchanged 53x26 header.
constexpr int header_glyph_x=21,header_glyph_y=33,header_cross_x=26,header_cross_y=42,header_cross_half=6;
inline int HeaderAt(int x,int y,unsigned channels=8) noexcept {
    if(y<header_y+1 || y>=header_y+header_height-1 || x<channel_x) return -1;
    const auto channel=(x-channel_x)/channel_width, dx=(x-channel_x)%channel_width;
    return channel<int(channels) && dx>0 && dx<channel_width-1 ? channel : -1;
}
inline bool WindowToFrame(int x,int y,int ww,int wh,int ow,int oh,int vx,int vy,int vw,int vh,int& fx,int& fy) noexcept {
    if(ww<=0 || wh<=0 || ow<=0 || oh<=0 || vw<=0 || vh<=0 || x<0 || y<0 || x>=ww || y>=wh) return false;
    const auto px=std::int64_t(x)*ow/ww-vx,py=std::int64_t(y)*oh/wh-vy;
    if(px<0 || py<0 || px>=vw || py>=vh) return false;
    fx=int(px*640/vw);fy=int(py*480/vh);return true;
}
constexpr unsigned first_beat_row = 0;
constexpr unsigned beat_rows = 4;
constexpr unsigned visible_rows = 32;
constexpr bool IsBeatRow(unsigned row) noexcept {
    return row >= first_beat_row && (row - first_beat_row) % beat_rows == 0;
}
// Strictly directional, visible destinations only; no destination means no move.
inline unsigned BeatDestination(unsigned row, unsigned active_rows, bool forward) noexcept {
    const auto limit = std::min(active_rows, visible_rows);
    auto destination = row;
    for (unsigned beat = first_beat_row; beat < limit; beat += beat_rows) {
        if (forward && beat > row) return beat;
        if (!forward && beat < row) destination = beat;
    }
    return destination;
}
}
