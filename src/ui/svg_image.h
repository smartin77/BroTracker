#pragma once
#include <filesystem>
#include <memory>
class Framebuffer;
// UI-thread-only CPU SVG. Load once; retain vector geometry, rasterize during Draw.
// Supports the NanoSVG subset, including paths, fills, strokes and alpha.
class SvgImage {
public:
    SvgImage();
    ~SvgImage();
    SvgImage(const SvgImage&)=delete;
    SvgImage& operator=(const SvgImage&)=delete;
    bool Load(const std::filesystem::path& path) noexcept;
    bool Loaded() const noexcept;
    // Aspect-preserving fit centered in the requested box, clipped to framebuffer.
    // Source-over blends straight RGBA; failure/missing artwork leaves pixels alone.
    bool Draw(Framebuffer& target,int x,int y,int width,int height) const noexcept;
private:
    struct Data;
    std::unique_ptr<Data> data_;
};
