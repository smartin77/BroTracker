#pragma once
#include "bitmap_font.h"
#include "svg_image.h"
#include <cmath>
// Host-only warning face choice. Native 6x12 deliberately uses Regular for both roles.
enum class WarningFont { Terminus8x14, Terminus6x12 };
constexpr WarningFont kWarningFont=WarningFont::Terminus8x14;
class WarningRenderer {
public:
    explicit WarningRenderer(const std::filesystem::path& assets="assets",WarningFont choice=kWarningFont) {
        if(!assets.empty())(void)icon_.Load(assets/"icons/font-preview-warning.svg");
        const auto dir=assets/"fonts/terminus";
        regular_=HostBitmap::Bdf(dir/(choice==WarningFont::Terminus8x14?"ter-u14n.bdf":"ter-u12n.bdf"));
        heading_=choice==WarningFont::Terminus8x14?HostBitmap::Bdf(dir/"ter-u14b.bdf"):regular_;
    }
    void Draw(Framebuffer& fb,const std::string& heading,const std::string& body) const {
        // Content padding, reduced from the accepted mockup's roughly 32px to 18px.
        constexpr int verticalPadding=18,horizontalPadding=28;
        constexpr int cornerRadius=20,outlineThickness=2;
        constexpr int iconSize=64,iconTextGap=24,headingBodyGap=12;
        constexpr int maximumPanelWidth=592,maximumTextWidth=448;
        constexpr double paragraphLineSpacing=1.5;
        const int available=std::min(maximumTextWidth,maximumPanelWidth-2*horizontalPadding-iconSize-iconTextGap);
        struct Line {std::string text;int y;bool bold;};std::vector<Line> lines;
        int textHeight=0,textWidth=0;
        auto layout=[&](const std::string& text,const HostBitmap::Face& face,bool bold) {
            // Explicit breaks advance by native height; only soft wraps add paragraph spacing.
            size_t start=0;
            do {
                const auto end=text.find('\n',start);auto paragraph=text.substr(start,end==text.npos?end:end-start);
                std::string current;
                auto emit=[&](bool soft) {lines.push_back({current,textHeight,bold});textWidth=std::max(textWidth,HostBitmap::Measure(face,current).width);textHeight+=soft?int(std::lround(face.line*paragraphLineSpacing)):face.line;current.clear();};
                std::istringstream words(paragraph);std::string word;
                while(words>>word) {
                    if(!current.empty() && HostBitmap::Measure(face,current+" "+word).width>available)emit(true);
                    // Break overlong words on UTF-8 scalar boundaries, preserving their bytes.
                    for(size_t i=0;i<word.size();) {
                        size_t count=1;const auto c=static_cast<unsigned char>(word[i]);
                        if(c>=0xc2 && c<=0xdf)count=2;else if(c>=0xe0 && c<=0xef)count=3;else if(c>=0xf0 && c<=0xf4)count=4;
                        if(i==0 && !current.empty())current+=' ';
                        auto next=word.substr(i,count);
                        if(HostBitmap::Measure(face,current+next).width>available)emit(true);
                        current+=next;i+=count;
                    }
                }
                emit(false);if(end==text.npos)break;start=end+1;
            }while(true);
        };
        layout(heading,heading_,true);textHeight+=headingBodyGap;layout(body,regular_,false);
        const int contentHeight=std::max(iconSize,textHeight);
        const int width=2*horizontalPadding+iconSize+iconTextGap+textWidth;
        const int height=std::min(int(fb.Height())-16,2*verticalPadding+contentHeight);
        const int x=(int(fb.Width())-width)/2,y=(int(fb.Height())-height)/2;
        const Color red{255,75,75},dark{16,16,16};
        for(unsigned row=0;row<fb.Height();++row)for(unsigned col=0;col<fb.Width();++col) {
            const auto p=fb.PixelData()[row*fb.Width()+col];fb.SetPixel(col,row,Color{std::uint8_t(p.r/2),std::uint8_t(p.g/2),std::uint8_t(p.b/2)});
        }
        auto inside=[](int px,int py,int w,int h,int radius) {int dx=std::max({radius-px,0,px-(w-radius-1)}),dy=std::max({radius-py,0,py-(h-radius-1)});return px>=0 && py>=0 && px<w && py<h && dx*dx+dy*dy<=radius*radius;};
        for(int row=0;row<height;++row)for(int col=0;col<width;++col)if(inside(col,row,width,height,cornerRadius))
            fb.SetPixel(x+col,y+row,inside(col-outlineThickness,row-outlineThickness,width-2*outlineThickness,height-2*outlineThickness,cornerRadius-outlineThickness)?dark:red);
        const int contentTop=y+(height-contentHeight)/2;
        (void)icon_.Draw(fb,x+horizontalPadding,contentTop+(contentHeight-iconSize)/2,iconSize,iconSize);
        for(const auto& line:lines) {
            const auto& face=line.bold?heading_:regular_;
            const int top=contentTop+(contentHeight-textHeight)/2+line.y;
            if(top+face.line<=y+height-verticalPadding)
                HostBitmap::Text(fb,face,x+horizontalPadding+iconSize+iconTextGap,top+face.ascent,line.text,red);
        }
    }
private:
    SvgImage icon_;
    HostBitmap::Face regular_,heading_;
};
