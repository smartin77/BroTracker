#pragma once
#include "bitmap_font.h"

// Presentation only: usable by any host file browser, independent of LIST/storage.
struct FileListView {
    static constexpr unsigned visible_rows=6;
    std::vector<std::string> names;
    unsigned selected=0,first=0;
    void Move(int direction) {
        if(names.empty())return;
        if(direction>0 && selected+1<names.size())++selected;
        else if(direction<0 && selected)--selected;
        first=(selected/visible_rows)*visible_rows;
    }
    struct Scroll {unsigned offset,length;};
    Scroll Indicator(unsigned height)const {
        if(names.empty())return {0,0};
        if(names.size()<=visible_rows)return {0,height};
        const auto visible=std::min<std::size_t>(visible_rows,names.size()-first);
        const unsigned offset=unsigned(height*first/names.size());
        const unsigned end=unsigned(height*(first+visible)/names.size());
        return {offset,std::max(1u,end-offset)};
    }
};
inline void DrawFileList(Framebuffer& fb,const FileListView& view,int x,int y,int width,
    const HostBitmap::Face& heading,const HostBitmap::Face& filename,const std::string& title) {
    constexpr Color border{130,130,196},text{225,225,225},highlight{153,153,153},ink{16,16,16};
    constexpr int row_height=18,header_height=26;
    HostBitmap::Text(fb,heading,x,y+heading.ascent,title,text);
    y+=header_height;
    fb.Rectangle(x,y,width,FileListView::visible_rows*row_height+2,border);
    const auto scroll=view.Indicator(FileListView::visible_rows*row_height);
    fb.FilledRectangle(x+width-5,y+1+scroll.offset,2,scroll.length,Color{255,255,255});
    for(unsigned row=0;row<FileListView::visible_rows && view.first+row<view.names.size();++row) {
        const auto item=view.first+row;const int top=y+1+row*row_height;
        const bool selected=item==view.selected;
        if(selected)fb.FilledRectangle(x+1,top,width-8,row_height,highlight);
        HostBitmap::Text(fb,filename,x+8,top+3+filename.ascent,
            view.names[item].substr(0,(width-22)/filename.cell),selected?ink:text);
    }
}
