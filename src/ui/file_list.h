#pragma once
#include "bitmap_font.h"

// Presentation only: usable by any host file browser, independent of LIST/storage.
struct FileListView {
    static constexpr unsigned visible_rows=6;
    std::vector<std::string> names;
    unsigned selected=0,first=0;
};
inline void DrawFileList(Framebuffer& fb,const FileListView& view,int x,int y,int width,
    const HostBitmap::Face& heading,const HostBitmap::Face& filename,const std::string& title) {
    constexpr Color border{130,130,196},text{225,225,225},highlight{153,153,153},ink{16,16,16};
    constexpr int row_height=18,header_height=26;
    fb.Rectangle(x,y,width,header_height+FileListView::visible_rows*row_height+2,border);
    HostBitmap::Text(fb,heading,x+8,y+5+heading.ascent,title,text);
    fb.HorizontalLine(x,x+width-1,y+header_height,border);
    for(unsigned row=0;row<FileListView::visible_rows && view.first+row<view.names.size();++row) {
        const auto item=view.first+row;const int top=y+header_height+1+row*row_height;
        const bool selected=item==view.selected;
        if(selected)fb.FilledRectangle(x+1,top,width-2,row_height,highlight);
        HostBitmap::Text(fb,filename,x+8,top+3+filename.ascent,
            view.names[item].substr(0,(width-16)/filename.cell),selected?ink:text);
    }
}
