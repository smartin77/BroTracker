// Offline native-bitmap preview generator. BroTracker contributors; GPL-3.0-or-later.
#include "renderer/framebuffer.h"
#include "ui/font.h"
#include "ui/text_renderer.h"
#include "core/tune_loader.h"
#include "tools/ui_preview/pattern_fixture.h"
#include "warning_icon.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr Color background{16,16,16}, foreground{225,225,225}, accent{140,190,220};
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
// Strict scalar decoding, local to the preview. Invalid bytes consume one byte
// and become U+FFFD; production decoding/geometry is deliberately untouched.
std::vector<unsigned> Decode(const std::string& text) {
    std::vector<unsigned> result;
    for (size_t i=0; i<text.size();) {
        unsigned first=static_cast<unsigned char>(text[i]);
        unsigned cp=first; int count=1;
        if (first>=0xc2 && first<=0xdf) {cp=first&31; count=2;}
        else if (first>=0xe0 && first<=0xef) {cp=first&15; count=3;}
        else if (first>=0xf0 && first<=0xf4) {cp=first&7; count=4;}
        else if (first>=0x80) {result.push_back(0xfffd); ++i; continue;}
        bool valid=i+count<=text.size();
        for (int j=1; valid && j<count; ++j) {
            unsigned b=static_cast<unsigned char>(text[i+j]);
            if ((b&0xc0)!=0x80) valid=false;
            else cp=(cp<<6)|(b&63);
        }
        if ((count==2 && cp<0x80)||(count==3 && cp<0x800)||
            (count==4 && cp<0x10000)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff)) valid=false;
        result.push_back(valid?cp:0xfffd); i+=valid?count:1;
    }
    return result;
}
const std::vector<std::string> samples={
    "Pattern Load/Save", "Load pattern", "Save pattern", "patern1.btdev", "patern4.btdev",
    "SD CARD UNAVAILABLE", "TEENSY BOARD NOT CONNECTED", "Replace the current working session?",
    "Confirm / Cancel", "0123456789  Il1 O0  [] () + - / :",
    u8"Príliš žltý kôň úpenlivo erdží.", u8"ľščťžýáíéôä ĽŠČŤŽÝÁÍÉÔÄ ŕĺďň ŔĹĎŇ"};
struct RasterGlyph {
    int width=0,height=0,left=0,top=0,advance=0;
    std::vector<unsigned char> pixels;
};
struct Face {
    std::map<unsigned,RasterGlyph> glyphs;
    int ascent=0,descent=0,line=0,cell=0;
    const RasterGlyph* Find(unsigned cp) const {
        auto it=glyphs.find(cp); return it==glyphs.end()?nullptr:&it->second;
    }
};
// Restricted BDF 2.1 reader: fixed-width, <=16px cells, Unicode encoding,
// horizontal metrics. Reject unsupported input rather than resize/crop it.
Face ReadBdf(std::istream& in) {
    Face face; std::string line; int declared=-1, seen=0;
    bool header=false, ended=false;
    while(std::getline(in,line)) {
        std::istringstream fields(line); std::string key; fields>>key;
        if(key=="STARTFONT") {std::string version; fields>>version; header=version=="2.1";}
        else if(key=="FONTBOUNDINGBOX") {
            int height=0,x=0,y=0; fields>>face.cell>>height>>x>>y;
            Require(face.cell>0 && face.cell<=16 && height>0 && height<=32 && x==0,"Unsupported BDF cell");
        } else if(key=="FONT_ASCENT") fields>>face.ascent;
        else if(key=="FONT_DESCENT") fields>>face.descent;
        else if(key=="CHARS") fields>>declared;
        else if(key=="ENDFONT") ended=true;
        else if(key=="STARTCHAR") {
            RasterGlyph glyph; int encoding=-1; bool bitmap=false, end=false;
            while(std::getline(in,line)) {
                std::istringstream item(line); item>>key;
                if(key=="ENCODING") item>>encoding;
                else if(key=="DWIDTH") {int dy=0; item>>glyph.advance>>dy; Require(dy==0,"Vertical BDF advance");}
                else if(key=="BBX") {int bottom=0; item>>glyph.width>>glyph.height>>glyph.left>>bottom;
                    glyph.top=-bottom-glyph.height;}
                else if(key=="BITMAP") {
                    Require(glyph.width>0 && glyph.width<=16 && glyph.height>0 && glyph.height<=32,"Unsupported BDF glyph");
                    bitmap=true;
                    for(int row=0; row<glyph.height; ++row) {
                        Require(bool(std::getline(in,line)),"Truncated BDF bitmap");
                        if(!line.empty() && line.back()=='\r') line.pop_back();
                        int digits=((glyph.width+7)/8)*2;
                        Require(int(line.size())==digits && line.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos,"Invalid BDF row");
                        unsigned bits=std::stoul(line,nullptr,16);
                        for(int col=0; col<glyph.width; ++col)
                            glyph.pixels.push_back(bits & (1u<<(digits*4-col-1)) ? 255:0);
                    }
                } else if(key=="ENDCHAR") {end=true; break;}
            }
            Require(end && bitmap && encoding>=0 && encoding<=0x10ffff && glyph.advance==face.cell &&
                !face.Find(encoding),"Invalid/duplicate BDF glyph");
            face.glyphs.emplace(encoding,std::move(glyph)); ++seen;
        }
    }
    Require(header && ended && seen==declared && seen>0 && face.ascent>0 && face.descent>=0,"Incomplete BDF");
    face.line=face.ascent+face.descent;
    return face;
}
Face Bdf(const fs::path& path) {
    std::ifstream input(path); Require(bool(input),"Cannot open "+path.string()); return ReadBdf(input);
}
struct Bounds {int width=0,top=0,bottom=0,missing=0;};
Bounds Measure(const Face& face,const std::string& text) {
    Bounds b;
    for(auto cp:Decode(text)) {
        const auto* glyph=face.Find(cp);
        if(!glyph) {++b.missing; glyph=face.Find(0xfffd);}
        if(glyph) {
            for(int row=0; row<glyph->height; ++row) for(int col=0; col<glyph->width; ++col)
                if(glyph->pixels[row*glyph->width+col]>=128) {
                    b.top=std::min(b.top,glyph->top+row);
                    b.bottom=std::max(b.bottom,glyph->top+row+1);
                }
            b.width+=glyph->advance;
        } else b.width+=face.cell;
    }
    return b;
}
// Pixel clipping is provided by the production framebuffer. A visible box is
// used if a face has neither the requested character nor U+FFFD.
void Text(Framebuffer& fb,const Face& face,int x,int baseline,const std::string& text,Color color=foreground) {
    for(auto cp:Decode(text)) {
        const auto* glyph=face.Find(cp); if(!glyph) glyph=face.Find(0xfffd);
        if(!glyph) {fb.Rectangle(x,baseline-face.ascent,face.cell-1,face.line-1,color); x+=face.cell; continue;}
        for(int row=0; row<glyph->height; ++row) for(int col=0; col<glyph->width; ++col)
            if(glyph->pixels[row*glyph->width+col]>=128) {
                fb.SetPixel(x+glyph->left+col,baseline+glyph->top+row,color);
            }
        x+=glyph->advance;
    }
}
void Label(Framebuffer& fb,int y,const std::string& text) {DrawFixedText(fb,12,y,text,accent);}
enum class Flow { BR, Paragraph };
struct Content { Flow flow; std::string text; };
struct ContentPage { std::string id; std::vector<Content> blocks; };
std::vector<std::string> Wrap(const Face& face,const std::string& text,int width) {
    Require(text.find('\n')==std::string::npos,"Paragraph data must not contain BR");
    std::istringstream words(text); std::string word,line; std::vector<std::string> lines;
    while(words>>word) {
        Require(Measure(face,word).width<=width,"Word cannot fit without clipping");
        const auto next=line.empty()?word:line+" "+word;
        if(Measure(face,next).width>width) {lines.push_back(line); line=word;}
        else line=next;
    }
    if(!line.empty()) lines.push_back(line);
    return lines;
}
struct Line {std::string text; int baseline,advance,block; Flow flow;};
std::vector<Line> Layout(const Face& face,const ContentPage& page,int x=36,int top=66,int width=568,int bottom=446) {
    std::vector<Line> result; int baseline=top+face.ascent;
    for(size_t i=0;i<page.blocks.size();++i) {
        const auto& block=page.blocks[i];
        Require(block.text.find('\n')==std::string::npos,"BR must be explicit data");
        auto lines=block.flow==Flow::BR?std::vector<std::string>{block.text}:Wrap(face,block.text,width);
        int spacing=block.flow==Flow::BR?face.line:face.line*3/2;
        std::string reconstructed;
        for(const auto& text:lines) {
            auto bounds=Measure(face,text);
            Require(bounds.missing==0,"Missing content glyph: "+text);
            Require(bounds.width<=width && x>=0 && x+width<=640 && baseline+bounds.top>=top && baseline+bounds.bottom<=bottom,"Content bounds");
            for(auto cp:Decode(text)) {const auto* glyph=face.Find(cp);
                Require(glyph && glyph->left>=0 && glyph->left+glyph->width<=glyph->advance,"Unexpected bitmap overhang");}
            result.push_back({text,baseline,spacing,int(i),block.flow}); baseline+=spacing;
            if(!reconstructed.empty()) reconstructed+=" "; reconstructed+=text;
        }
        Require(reconstructed==block.text,"Wrapped content lost/reordered text");
    }
    return result;
}
constexpr Color warningRed{255,75,75};
void RoundedPanel(Framebuffer& fb,int x,int y,int width,int height,int radius) {
    auto inside=[](int px,int py,int w,int h,int r) {
        int dx=std::max({r-px,0,px-(w-r-1)}),dy=std::max({r-py,0,py-(h-r-1)});
        return px>=0 && py>=0 && px<w && py<h && dx*dx+dy*dy<=r*r;
    };
    for(int row=0;row<height;++row) for(int col=0;col<width;++col) if(inside(col,row,width,height,radius))
        fb.SetPixel(x+col,y+row,inside(col-2,row-2,width-4,height-4,radius-2)?background:warningRed);
}
Framebuffer Warning(const fs::path& root,const Face& regular,const Face& bold,bool sd) {
    Tune tune=LoadTuneFromJson((root/"assets/dummy_my_tune.json").string().c_str());
    Require(!tune.patterns.empty(),"Pattern fixture tune is empty");
    Framebuffer fb(640,480);
    Require(RenderProductionPatternFixture(fb,tune,tune.patterns.front()),"Production Pattern fixture");
    for(int y=0;y<480;++y) for(int x=0;x<640;++x) {
        auto p=fb.PixelData()[y*640+x]; fb.SetPixel(x,y,Color{std::uint8_t(p.r/2),std::uint8_t(p.g/2),std::uint8_t(p.b/2)});
    }
    RoundedPanel(fb,48,164,544,152,20);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        auto pixel=warning_icon[y*64+x]; if(pixel) fb.SetPixel(76+x,208+y,pixel==1?warningRed:background);
    }
    const std::string heading=sd?"SD CARD UNAVAILABLE":"TEENSY BOARD NOT CONNECTED";
    ContentPage explanation{"warning",sd?std::vector<Content>{
        {Flow::Paragraph,"BroTracker requires an SD card."}, {Flow::Paragraph,"Please exit the application."}}:
        std::vector<Content>{{Flow::Paragraph,"Connect the Teensy board to continue."}}};
    Require(Measure(bold,heading).missing==0 && Measure(bold,heading).width<=400,"Warning heading bounds");
    Text(fb,bold,164,220,heading,warningRed);
    for(const auto& line:Layout(regular,explanation,164,236,400,296))
        Text(fb,regular,164,line.baseline,line.text,warningRed);
    return fb;
}
// Preserve both native sizes; this fixture does not choose a production font.
void ExportStaticThree(const fs::path& root,const fs::path& output) {
    const auto dir=root/"assets/fonts/terminus";
    ContentPage shared{"static-comparison",{}};
    for(const auto& sample:samples) shared.blocks.push_back({Flow::BR,sample});
    for(const auto& line:{"", "Pattern menu: Load pattern / Save pattern", "> patern1.btdev    32 rows    8 channels", "  patern4.btdev    64 rows    8 channels", ""})
        shared.blocks.push_back({Flow::BR,line});
    shared.blocks.push_back({Flow::Paragraph,
        u8"Pr\u00edli\u0161 \u017elt\u00fd k\u00f4\u0148 \u00fapenlivo erd\u017e\u00ed. Pri na\u010d\u00edtan\u00ed vzoru si prezrite n\u00e1zov s\u00faboru a po\u010det riadkov. Zvolen\u00fd s\u00fabor zost\u00e1va ozna\u010den\u00fd v zozname. Pred ulo\u017een\u00edm potvr\u010fte zmenu pracovnej rel\u00e1cie alebo sa vr\u00e1\u0165te sp\u00e4\u0165 bez zmien."});
    for(int px:{12,14}) {
        auto face=Bdf(dir/(px==12?"ter-u12n.bdf":"ter-u14n.bdf"));
        Framebuffer fb(640,480); fb.Clear(background);
        Label(fb,8,std::string("Terminus ")+(px==12?"6x12 Regular":"8x14 Regular")+" | native 640x480 | identical comparison text");
        Label(fb,22,"BR="+std::to_string(face.line)+"px | paragraph="+std::to_string(face.line*3/2)+"px | width=568px");
        for(const auto& line:Layout(face,shared,36,42,568,426))
            Text(fb,face,36,line.baseline,line.text);
        Label(fb,442,px==12?"Native 6x12 Bold unavailable.":"Separate genuine native 8x14 Bold sample:");
        if(px==14) Text(fb,Bdf(dir/"ter-u14b.bdf"),36,473,"Pattern Load/Save  Confirm / Cancel");
        Require(fb.SaveBMP(output/(px==12?"static-01-terminus-6x12.bmp":"static-02-terminus-8x14.bmp")),"Static image export");
    }
    Require(Warning(root,Bdf(dir/"ter-u14n.bdf"),Bdf(dir/"ter-u14b.bdf"),true)
        .SaveBMP(output/"static-03-pattern-warning.bmp"),"Static warning export");
}
} // namespace
int main(int argc,char** argv) try {
    if((argc!=3 && argc!=4) || (argc==4 && std::string(argv[3])!="--static-three")) {
        std::cerr<<"Usage: BroTrackerFontPreview <repo-root> <output-dir> [--static-three]\n";
        return 2;
    }
    const fs::path root=fs::absolute(argv[1]), output=fs::absolute(argv[2]);
    Require(LoadUiFont((root/"assets/fonts/brotracker.btf").string().c_str()),"BT font load");
    fs::create_directories(output);
    ExportStaticThree(root,output);
    return 0;
} catch(const std::exception& error) {std::cerr<<"Font preview: "<<error.what()<<"\n"; return 1;}
