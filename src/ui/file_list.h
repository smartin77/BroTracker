#pragma once
#include "bitmap_font.h"

// Presentation only: usable by any host file browser, independent of LIST/storage.
struct FileListView {
    static constexpr unsigned visible_rows=6;
    std::vector<std::string> names;
    unsigned selected=0,first=0,rows=visible_rows;
    void KeepVisible() {
        if(selected<first)first=selected;
        else if(selected>=first+rows)first=selected-rows+1;
    }
    void Move(int direction) {
        if(names.empty())return;
        if(direction>0 && selected+1<names.size())++selected;
        else if(direction<0 && selected)--selected;
        if(rows==visible_rows)first=(selected/visible_rows)*visible_rows;else KeepVisible();
    }
    struct Scroll {unsigned offset,length;};
    Scroll Indicator(unsigned height)const {
        if(names.empty())return {0,0};
        if(names.size()<=rows)return {0,height};
        const auto visible=std::min<std::size_t>(rows,names.size()-first);
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

// Sample Browser owns the complete viewport: one directory line, list, one status line.
constexpr unsigned kSampleBrowserLineHeight=18;
constexpr unsigned SampleBrowserRows(unsigned height) {
    return height>2*kSampleBrowserLineHeight?(height-2*kSampleBrowserLineHeight)/kSampleBrowserLineHeight:1;
}
inline void DrawSampleBrowser(Framebuffer& fb,FileListView view,const std::string& directory,const std::string& status) {
    static const auto face=HostBitmap::Bdf("assets/fonts/terminus/ter-u14n.bdf");
    constexpr Color text{225,225,225},highlight{153,153,153},ink{16,16,16},border{130,130,196};
    const int width=fb.Width(),height=fb.Height(),top=kSampleBrowserLineHeight,bottom=height-kSampleBrowserLineHeight;
    HostBitmap::Text(fb,face,4,2+face.ascent,directory,text);
    fb.HorizontalLine(0,width-1,top-1,border);fb.HorizontalLine(0,width-1,bottom,border);
    view.rows=SampleBrowserRows(height);view.KeepVisible();
    const auto scroll=view.Indicator(bottom-top);
    if(!view.names.empty())fb.FilledRectangle(width-4,top+scroll.offset,2,scroll.length,text);
    for(unsigned row=0;row<view.rows && view.first+row<view.names.size();++row) {
        const auto item=view.first+row;const int y=top+row*kSampleBrowserLineHeight;
        const bool selected=item==view.selected;
        if(selected)fb.FilledRectangle(0,y,width-6,kSampleBrowserLineHeight,highlight);
        HostBitmap::Text(fb,face,4,y+2+face.ascent,view.names[item].substr(0,(width-12)/face.cell),selected?ink:text);
    }
    HostBitmap::Text(fb,face,4,bottom+2+face.ascent,status,text);
}
