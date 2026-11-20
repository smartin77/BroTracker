#pragma once
#include "bitmap_font.h"
#include "core/constants.h"
#include "pattern_grid.h"
#include <optional>

// Presentation only: usable by any host file browser, independent of LIST/storage.
struct FileListView {
    static constexpr unsigned visible_rows=6;
    std::vector<std::string> names;
    unsigned selected=0,first=0,rows=visible_rows;
    void KeepVisible() {
        if(selected<first)first=selected;
        else if(selected>=first+rows)first=selected-rows+1;
    }
    void Move(std::int64_t steps) {
        if(names.empty())return;
        const auto current=std::int64_t(selected);
        const auto last=std::int64_t(names.size()-1);
        selected=unsigned(current+std::clamp(steps,-current,last-current));
        if(rows==visible_rows)first=(selected/visible_rows)*visible_rows;else KeepVisible();
    }
    bool Highlight(unsigned index) {
        if(index>=names.size())return false;
        selected=index;KeepVisible();return true;
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
struct FileListArea {
    int left,top,width,row_height;
    unsigned rows;
    std::optional<unsigned> EntryAt(const FileListView& view,int x,int y) const {
        if(x<left || x>=left+width || y<top)return {};
        const auto row=unsigned((y-top)/row_height);
        const auto entry=view.first+row;
        if(row>=rows || entry>=view.names.size())return {};
        return entry;
    }
};
constexpr int kPatternListHeaderHeight=26,kPatternListWidth=454-16,kPatternListLeft=8;
constexpr FileListArea PatternLoadListArea() {
    return {kPatternListLeft+1,PatternGrid::header_y+8+kPatternListHeaderHeight+1,
        kPatternListWidth-8,18,FileListView::visible_rows};
}
inline void DrawFileList(Framebuffer& fb,const FileListView& view,
    const HostBitmap::Face& heading,const HostBitmap::Face& filename,const std::string& title) {
    constexpr Color border{130,130,196},text{225,225,225},highlight{153,153,153},ink{16,16,16};
    const auto area=PatternLoadListArea();
    const int x=area.left-1,y=area.top-1,width=kPatternListWidth,row_height=area.row_height;
    HostBitmap::Text(fb,heading,x,y-kPatternListHeaderHeight+heading.ascent,title,text);
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
constexpr unsigned SampleBrowserRows(unsigned height,unsigned header_lines=1) {
    return height>(header_lines+1)*kSampleBrowserLineHeight?(height-(header_lines+1)*kSampleBrowserLineHeight)/kSampleBrowserLineHeight:1;
}
// Terminus 14 is fixed-width with an eight-pixel cell.
constexpr unsigned SampleBrowserColumns(int width) {
    constexpr int cell_width=8;
    return std::max(1,(width-8)/cell_width);
}
inline FileListArea SampleBrowserListArea(const std::string& directory,int width=SCREEN_WIDTH,int height=SCREEN_HEIGHT) {
    const auto columns=SampleBrowserColumns(width);
    const auto lines=std::max<std::size_t>(1,(directory.size()+columns-1)/columns);
    return {0,int(lines*kSampleBrowserLineHeight),width-6,kSampleBrowserLineHeight,SampleBrowserRows(height,lines)};
}
inline void DrawSampleBrowser(Framebuffer& fb,FileListView view,const std::string& directory,const std::string& status) {
    static const auto face=HostBitmap::Bdf("assets/fonts/terminus/ter-u14n.bdf");
    constexpr Color text{225,225,225},highlight{153,153,153},ink{16,16,16},border{130,130,196};
    const int width=fb.Width(),height=fb.Height();
    const auto area=SampleBrowserListArea(directory,width,height);
    const auto columns=SampleBrowserColumns(width);
    const int top=area.top,bottom=height-kSampleBrowserLineHeight;
    for(unsigned i=0;i<unsigned(top)/kSampleBrowserLineHeight;++i)HostBitmap::Text(fb,face,4,i*kSampleBrowserLineHeight+2+face.ascent,directory.substr(i*columns,columns),text);
    fb.HorizontalLine(0,width-1,top-1,border);fb.HorizontalLine(0,width-1,bottom,border);
    view.rows=area.rows;view.KeepVisible();
    const auto scroll=view.Indicator(bottom-top);
    if(!view.names.empty())fb.FilledRectangle(width-4,top+scroll.offset,2,scroll.length,text);
    for(unsigned row=0;row<view.rows && view.first+row<view.names.size();++row) {
        const auto item=view.first+row;const int y=top+row*kSampleBrowserLineHeight;
        const bool selected=item==view.selected;
        if(selected)fb.FilledRectangle(area.left,y,area.width,area.row_height,highlight);
        HostBitmap::Text(fb,face,4,y+2+face.ascent,view.names[item].substr(0,(width-12)/face.cell),selected?ink:text);
    }
    HostBitmap::Text(fb,face,4,bottom+2+face.ascent,status,text);
}
