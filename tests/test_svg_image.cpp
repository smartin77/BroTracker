#include "test_framework.h"
#include "temporary_directory.h"
#include "ui/svg_image.h"
#include "ui/warning_renderer.h"
#include "ui/sdl/host_paths.h"
#include <fstream>

TEST_CASE(SvgLoadFailuresLeaveNoDrawableAsset) {
    TemporaryTestDirectory temp;SvgImage image;Framebuffer target(640,480);
    CHECK(!image.Load(temp.Path()/"missing.svg"));CHECK(!image.Loaded());
    CHECK(!image.Draw(target,0,0,64,64));
    for(const auto* source:{"not an SVG","<svg width=\"64\" height=\"64\"></svg>",
        "<svg width=\"64\" height=\"64\"><path d=\"M\"/></svg>"}) {
        const auto path=temp.Path()/"bad.svg";{std::ofstream out(path);out<<source;}
        CHECK(!image.Load(path));CHECK(!image.Loaded());
    }
}
TEST_CASE(SvgRetainsParsedGeometryAfterSourceIsClosedAndHandlesClipping) {
    TemporaryTestDirectory temp;const auto path=temp.Path()/"geometry.svg";
    {std::ofstream out(path);out<<R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">
        <path d="M4 28 L16 4 L28 28 Z" fill="#ff4b4b" fill-opacity="0.5" stroke="#ff4b4b" stroke-width="3" stroke-linejoin="round"/>
        <path d="M16 12 V20" fill="none" stroke="#101010" stroke-width="2" stroke-linecap="round"/>
        </svg>)";}
    SvgImage image;CHECK(image.Load(path));CHECK(image.Loaded());
    std::filesystem::remove(path);Framebuffer target(640,480);
    CHECK(image.Draw(target,10,10,64,64));CHECK(image.Draw(target,-10,-10,32,64));
    CHECK(image.Draw(target,1000,1000,64,64));CHECK(!image.Draw(target,0,0,0,64));
    CHECK(!image.Load(path));CHECK(!image.Loaded());
}
TEST_CASE(ExecutableAssetPathIsAbsoluteAndMissingIconDoesNotPreventWarningDrawing) {
    CHECK(RuntimeAssetsPath().is_absolute());
    TemporaryTestDirectory temp;const auto fonts=temp.Path()/"fonts/terminus";
    std::filesystem::create_directories(fonts);
    for(const auto* file:{"ter-u14n.bdf","ter-u14b.bdf"})
        std::filesystem::copy_file(std::filesystem::path("assets/fonts/terminus")/file,fonts/file);
    WarningRenderer warning(temp.Path());Framebuffer target(640,480);
    warning.Draw(target,"SD CARD UNAVAILABLE","The application cannot continue without an SD card.");
    {std::filesystem::create_directories(temp.Path()/"icons");std::ofstream out(temp.Path()/"icons/font-preview-warning.svg");out<<"malformed";}
    WarningRenderer malformed(temp.Path());
    malformed.Draw(target,"TEENSY BOARD NOT CONNECTED","Connect the Teensy board to continue.");
}
