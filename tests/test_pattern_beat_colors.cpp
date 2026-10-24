#include "test_framework.h"
#include "ui/pattern_screen.h"
#include "ui/text_renderer.h"
#include "core/note_formatter.h"
#include <cstring>

namespace {
LivePlaybackView BeatView() {
    LivePlaybackView v; v.telemetry_available=true;
    v.position.rows=32; v.position.channels=8;
    BroTracker::DevicePatternSnapshot snapshot; snapshot.transfer_id=1; snapshot.tempo_hundredths=12500;
    snapshot.pattern.active_rows=32; snapshot.pattern.active_channels=8;
    v.device_pattern=snapshot; return v;
}
void Glyph(const Framebuffer& actual,unsigned row,unsigned channel,bool instrument,
           const std::string& text,Color ink,Color background=Color{16,16,16}) {
    const int x=38+53*channel+(instrument?24:0), y=64+13*row;
    Framebuffer expected(18,9); expected.Clear(background);
    DrawFixedText(expected,0,0,text,ink);
    bool painted=false;
    for(int dy=0;dy<9;++dy) for(int dx=0;dx<(instrument?12:18);++dx) {
        const auto& a=actual.PixelData()[(y+dy)*640+x+dx];
        const auto& e=expected.PixelData()[dy*18+dx];
        CHECK(std::memcmp(&a,&e,sizeof(Color))==0);
        painted|=std::memcmp(&e,&background,sizeof(Color))!=0;
    }
    CHECK(painted);
}
}
TEST_CASE(PatternBeatInkSpansEightChannelsAndDimsEachEmptyFieldIndependently) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Tune tune; Pattern unused; Framebuffer fb(640,480);
    for(unsigned kind=0;kind<4;++kind) {
        auto live=BeatView();
        for(unsigned row=0;row<32;++row) for(unsigned ch=0;ch<8;++ch)
            live.device_pattern->pattern.cells[row][ch]={kind&1?NOTE_EMPTY:Note(69),kind&2?std::uint8_t(255):std::uint8_t(1)};
        RenderMainScreen(fb,tune,unused,live);
        for(unsigned row=0;row<32;++row) for(unsigned ch=0;ch<8;++ch) {
            // Explicit oracle covers 01/05/.../29, not a duplicate call to the predicate.
            const bool beat=row%4==0;
            Glyph(fb,row,ch,false,kind&1?"---":"A-3",kind&1?
                (beat?Color{152,152,152}:Color{104,104,104}):
                (beat?Color{255,255,255}:Color{208,208,208}));
            Glyph(fb,row,ch,true,kind&2?"--":"02",kind&2?
                (beat?Color{104,104,156}:Color{66,66,100}):
                (beat?Color{174,174,232}:Color{130,130,196}));
            // No new beat background bands.
            CHECK_EQ(fb.PixelData()[(62+13*row)*640+32].r,16);
        }
    }
}
TEST_CASE(PatternBeatInkPreservesPlaybackBackgroundAndIndependentEditFrame) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Tune tune; Pattern unused; Framebuffer fb(640,480);
    for(unsigned row:{4u,5u}) {
        auto live=BeatView();
        live.device_pattern->pattern.cells[row][7]={69,1};
        live.fresh=true; live.position.running=true; live.position.valid=true;
        live.position.row=row; live.position.tick=row*96;
        LocalPatternEditor editor; editor.Sync(live); CHECK(editor.Ready());
        RenderMainScreen(fb,tune,unused,live,&editor);
        Glyph(fb,row,7,false,"A-3",row==4?Color{255,255,255}:Color{208,208,208},Color{30,30,30});
        CHECK_EQ(fb.PixelData()[(62+13*row)*640+32].r,30);
        const auto& frame=fb.PixelData()[62*640+36];
        CHECK_EQ(frame.r,255); CHECK_EQ(frame.g,160); CHECK_EQ(frame.b,64);
        // Stale position removes its grey background; the edit frame survives.
        live.fresh=false; RenderMainScreen(fb,tune,unused,live,&editor);
        CHECK_EQ(fb.PixelData()[(62+13*row)*640+32].r,16);
        CHECK_EQ(fb.PixelData()[62*640+36].g,160);
    }
}
TEST_CASE(PatternPreviewInverseSelectionWinsOverBeatBaseEvenForEmptyFields) {
    CHECK(LoadUiFont("assets/fonts/brotracker.btf"));
    Tune tune; Pattern pattern; pattern.channels.resize(8);
    for(auto& channel:pattern.channels) channel.rows.resize(32);
    Framebuffer fb(640,480);
    for(bool populated:{false,true}) {
        pattern.channels[0].rows[12]=populated?Event{69,1}:Event{};
        RenderPreviewScreen(fb,tune,pattern);
        Glyph(fb,12,0,false,populated?"A-3":"---",Color{16,16,16},Color{153,153,153});
        Glyph(fb,12,0,true,populated?"02":"--",Color{78,78,118},Color{153,153,153});
    }
}
