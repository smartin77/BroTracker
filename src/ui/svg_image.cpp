// Host-only NanoSVG integration. BroTracker contributors; GPL-3.0-or-later.
#include "svg_image.h"
#include "renderer/framebuffer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

struct SvgImage::Data {
    NSVGimage* image=nullptr;
    NSVGrasterizer* rasterizer=nullptr;
    // Drawing workspace only, never an artwork cache or substitute for geometry.
    std::vector<unsigned char> rgba;
    ~Data() {nsvgDelete(image);nsvgDeleteRasterizer(rasterizer);}
};
SvgImage::SvgImage()=default;
SvgImage::~SvgImage()=default;
bool SvgImage::Loaded() const noexcept {return data_ && data_->image;}
bool SvgImage::Load(const std::filesystem::path& path) noexcept {
    data_.reset();
    try {
        // Bundle-owned small icons; bound accidental oversized/broken asset reads.
        constexpr std::streamoff maximumAssetBytes=1024*1024;
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input)return false;
        const auto length=input.tellg();
        if(length<=0 || length>maximumAssetBytes)return false;
        std::vector<char> source(static_cast<size_t>(length)+1,0);
        input.seekg(0);
        if(!input.read(source.data(),length))return false;
        auto next=std::make_unique<Data>();
        next->image=nsvgParse(source.data(),"px",96.0f);
        if(!next->image || !next->image->shapes || !std::isfinite(next->image->width) ||
            !std::isfinite(next->image->height) || next->image->width<=0 || next->image->height<=0)return false;
        bool hasGeometry=false;
        for(auto* shape=next->image->shapes;shape;shape=shape->next) {
            for(auto* path=shape->paths;path;path=path->next) {
                if(path->npts<4 || (path->npts-1)%3)return false;
                for(int i=0;i<path->npts*2;++i)if(!std::isfinite(path->pts[i]))return false;
                hasGeometry=true;
            }
        }
        if(!hasGeometry)return false;
        next->rasterizer=nsvgCreateRasterizer();
        if(!next->rasterizer)return false;
        data_=std::move(next);return true;
    }catch(...) {return false;}
}
bool SvgImage::Draw(Framebuffer& target,int x,int y,int width,int height) const noexcept {
    if(!Loaded() || width<=0 || height<=0)return false;
    const int left=std::max(0,x),top=std::max(0,y);
    const int right=static_cast<int>(std::min<std::int64_t>(target.Width(),std::int64_t(x)+width));
    const int bottom=static_cast<int>(std::min<std::int64_t>(target.Height(),std::int64_t(y)+height));
    if(right<=left || bottom<=top)return true;
    try {
        const int w=right-left,h=bottom-top;
        data_->rgba.resize(size_t(w)*h*4);
        const float scale=std::min(width/data_->image->width,height/data_->image->height);
        if(!std::isfinite(scale) || scale<=0)return false;
        const float tx=float(x)-left+(width-data_->image->width*scale)/2;
        const float ty=float(y)-top+(height-data_->image->height*scale)/2;
        nsvgRasterize(data_->rasterizer,data_->image,tx,ty,scale,data_->rgba.data(),w,h,w*4);
        for(int row=0;row<h;++row)for(int col=0;col<w;++col) {
            const auto* src=&data_->rgba[(size_t(row)*w+col)*4];
            const unsigned alpha=src[3];if(!alpha)continue;
            const auto dst=target.PixelData()[size_t(top+row)*target.Width()+left+col];
            auto blend=[&](unsigned source,unsigned background) {return std::uint8_t((source*alpha+background*(255-alpha)+127)/255);};
            target.SetPixel(left+col,top+row,{blend(src[0],dst.r),blend(src[1],dst.g),blend(src[2],dst.b)});
        }
        return true;
    }catch(...) {return false;}
}
