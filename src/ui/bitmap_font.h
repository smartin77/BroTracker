#pragma once
// Shared host-only native BDF reader and UTF-8 drawing, extracted from font preview.
#include "renderer/framebuffer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace HostBitmap {
namespace fs=std::filesystem;
inline void Require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
// Strict UTF-8 scalar decoding. Invalid bytes consume one byte and become U+FFFD.
inline std::vector<unsigned> Decode(const std::string& text) {
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
inline Face ReadBdf(std::istream& in) {
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
inline Face Bdf(const fs::path& path) {
    std::ifstream input(path); Require(bool(input),"Cannot open "+path.string()); return ReadBdf(input);
}
struct Bounds {int width=0,top=0,bottom=0,missing=0;};
inline Bounds Measure(const Face& face,const std::string& text) {
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
inline void Text(Framebuffer& fb,const Face& face,int x,int baseline,const std::string& text,Color color=Color{225,225,225}) {
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
}
