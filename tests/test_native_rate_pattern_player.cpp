#include "test_framework.h"
#include "core/playback/native_rate_pattern_player.h"
#include <algorithm>
#include <array>
#include <vector>

using namespace BroTracker;
namespace BroTracker
{
    // Same inline test hook definition as test_scheduler.cpp (ODR-identical).
    struct MusicalTickCursorTestAccess
    {
        static void Seed(MusicalTickCursor& cursor, std::uint64_t next_tick,
                         std::uint64_t completed_end)
        {
            cursor.next_tick_ = next_tick;
            cursor.block_end_ = completed_end;
        }
    };
    struct NativeRatePatternPlayerTestAccess
    {
        static const NativeRateSampleCommandPreparer& Preparer(const NativeRatePatternPlayer& player)
        { return player.preparer_; }
        static const RamVoiceBlockRenderer& Renderer(const NativeRatePatternPlayer& player)
        { return player.renderer_; }
        static void SetPosition(NativeRatePatternPlayer& player, std::uint64_t position)
        { player.next_sample_ = position; }
        static void ExhaustCursor(NativeRatePatternPlayer& player)
        {
            // Seed an otherwise impractical arithmetic boundary using the cursor's
            // existing test hook. One absolute conversion exceeds uint64_t.
            (void)player.cursor_.Configure(1,UINT32_MAX);
            MusicalTickCursorTestAccess::Seed(player.cursor_,UINT64_MAX,0);
            (void)player.cursor_.BeginBlock(0,1);
            MusicalTick tick;
            CHECK_EQ(player.cursor_.Pull(tick),TickCursorStatus::ArithmeticExhausted);
        }
    };
}
namespace
{
    const std::int16_t pcm[]={10,20,30,40,50,60,70,80};
    NativeRateSampleBindings Bindings(std::uint32_t rate=44100)
    {
        NativeRateSampleBindings bindings{}; bindings.count=1;
        bindings.bindings[0]={7,60,{pcm,8,rate}};
        return bindings;
    }
    RealtimePattern Pattern()
    {
        RealtimePattern pattern; pattern.active_channels=2;
        pattern.cells[0][0]={60,7}; pattern.cells[0][1]={60,7};
        return pattern;
    }
    void CheckPlayback(const NativeRatePatternPlayer& actual, const NativeRatePatternPlayer& expected)
    {
        CHECK_EQ(actual.IsRunning(),expected.IsRunning());
        CHECK_EQ(actual.GetNextSamplePosition(),expected.GetNextSamplePosition());
        for (std::uint32_t c=0;c<8;++c)
        {
            LogicalChannelState a,b;
            CHECK_EQ(NativeRatePatternPlayerTestAccess::Preparer(actual).GetChannel(c,a),ChannelStateStatus::Success);
            CHECK_EQ(NativeRatePatternPlayerTestAccess::Preparer(expected).GetChannel(c,b),ChannelStateStatus::Success);
            CHECK_EQ(a.note,b.note); CHECK_EQ(a.instrument,b.instrument);
            const auto* va=NativeRatePatternPlayerTestAccess::Renderer(actual).GetVoice(c);
            const auto* vb=NativeRatePatternPlayerTestAccess::Renderer(expected).GetVoice(c);
            CHECK_EQ(va->IsActive(),vb->IsActive());
            CHECK_EQ(va->GetFramePosition(),vb->GetFramePosition());
            CHECK_EQ(va->GetOutputSampleRate(),vb->GetOutputSampleRate());
        }
    }
    void CheckCleared(const NativeRatePatternPlayer& player)
    {
        CHECK_EQ(player.GetNextSamplePosition(),0);
        for (std::uint32_t c=0;c<8;++c)
        {
            LogicalChannelState state;
            CHECK_EQ(NativeRatePatternPlayerTestAccess::Preparer(player).GetChannel(c,state),ChannelStateStatus::Success);
            CHECK_EQ(state.note,NOTE_EMPTY); CHECK_EQ(state.instrument,kNoInstrumentUpdate);
            CHECK(!NativeRatePatternPlayerTestAccess::Renderer(player).GetVoice(c)->IsActive());
        }
    }
}

TEST_CASE(PatternPlayer_TransportSilenceRestartAndRenderValidation)
{
    NativeRatePatternPlayer player;
    player.Stop(); player.Stop(); CheckCleared(player);
    CHECK_EQ(player.Start(),PatternPlayerStatus::NotConfigured);
    CHECK_EQ(player.Render(nullptr,0),PatternPlayerStatus::Success);
    std::int16_t output[128]; std::fill_n(output,128,123);
    CHECK_EQ(player.Render(output,1),PatternPlayerStatus::NotConfigured); CHECK_EQ(output[0],123);
    CHECK_EQ(player.Configure(12753,44100,Pattern(),Bindings()),PatternPlayerStatus::Success);
    CHECK(!player.IsRunning()); CheckCleared(player);
    CHECK_EQ(player.Render(output,128),PatternPlayerStatus::Success);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(player.GetNextSamplePosition(),0);
    CHECK_EQ(player.Render(nullptr,1),PatternPlayerStatus::InvalidDestination);
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,3),PatternPlayerStatus::Success);
    CHECK_EQ(output[0],20); CHECK_EQ(output[1],40); CHECK_EQ(output[2],60);
    auto original=player;
    CHECK_EQ(player.Render(nullptr,0),PatternPlayerStatus::Success); CheckPlayback(player,original);
    std::fill_n(output,128,123);
    CHECK_EQ(player.Render(output,129),PatternPlayerStatus::InvalidFrameCount);
    CHECK_EQ(player.Render(output,UINT64_MAX),PatternPlayerStatus::InvalidFrameCount);
    CHECK_EQ(player.Render(nullptr,1),PatternPlayerStatus::InvalidDestination);
    CheckPlayback(player,original); for (auto value:output) CHECK_EQ(value,123);
    CHECK_EQ(player.Render(output,2),PatternPlayerStatus::Success);
    CHECK_EQ(output[0],80); CHECK_EQ(output[1],100);
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success); CheckCleared(player);
    CHECK_EQ(player.Render(output,1),PatternPlayerStatus::Success); CHECK_EQ(output[0],20);
    player.Stop(); player.Stop(); CHECK(!player.IsRunning()); CheckCleared(player);
    CHECK_EQ(player.Render(output,128),PatternPlayerStatus::Success);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(player.GetNextSamplePosition(),0);
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,1),PatternPlayerStatus::Success); CHECK_EQ(output[0],20);
    CHECK_EQ(player.Configure(12000,44100,Pattern(),Bindings()),PatternPlayerStatus::Success);
    CHECK(!player.IsRunning()); CheckCleared(player);
}

TEST_CASE(PatternPlayer_TransactionalConfigurationAndMetadataCopies)
{
    NativeRatePatternPlayer player;
    auto pattern=Pattern(); auto bindings=Bindings();
    CHECK_EQ(player.Configure(12753,44100,pattern,bindings),PatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
    std::int16_t output[3]; CHECK_EQ(player.Render(output,3),PatternPlayerStatus::Success);
    auto reference=player;
    const auto reject=[&](std::uint32_t tempo,std::uint32_t rate,const RealtimePattern& p,
        const NativeRateSampleBindings& b,PatternPlayerStatus status) {
        CHECK_EQ(player.Configure(tempo,rate,p,b),status); CheckPlayback(player,reference);
    };
    reject(0,44100,pattern,bindings,PatternPlayerStatus::InvalidTempo);
    reject(12753,0,pattern,bindings,PatternPlayerStatus::InvalidSampleRate);
    auto invalid=pattern;
    for (const auto rows:{0,17,255}) { invalid.active_rows=static_cast<std::uint8_t>(rows);
        reject(12753,44100,invalid,bindings,PatternPlayerStatus::InvalidDimensions); }
    invalid=pattern;
    for (const auto channels:{0,9,255}) { invalid.active_channels=static_cast<std::uint8_t>(channels);
        reject(12753,44100,invalid,bindings,PatternPlayerStatus::InvalidDimensions); }
    invalid=pattern; invalid.cells[15][1].note=128; // Validate more than the first row.
    reject(12753,44100,invalid,bindings,PatternPlayerStatus::InvalidNote);
    auto bad=bindings; bad.count=17;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::InvalidBindingCount);
    bad=bindings; bad.count=2; bad.bindings[1]=bad.bindings[0];
    reject(12753,44100,pattern,bad,PatternPlayerStatus::DuplicateInstrument);
    bad=bindings; bad.bindings[0].instrument=255;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::ReservedInstrument);
    bad=bindings; bad.bindings[0].native_rate_note=NOTE_OFF;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::InvalidNativeRateNote);
    bad=bindings; bad.bindings[0].sample.data=nullptr;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::InvalidSample);
    bad=bindings; bad.bindings[0].sample.sample_rate_hz=48000;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::SampleRateMismatch);
    bad=bindings; bad.bindings[0].sample.frame_count=SIZE_MAX;
    reject(12753,44100,pattern,bad,PatternPlayerStatus::RangeOverflow);
    std::int16_t expected[3];
    CHECK_EQ(player.Render(output,3),PatternPlayerStatus::Success);
    CHECK_EQ(reference.Render(expected,3),PatternPlayerStatus::Success);
    CHECK(std::equal(output,output+3,expected));
    pattern.active_rows=1; pattern.active_channels=1; pattern.cells[0][7].note=128;
    pattern.cells[15][0].note=128; // Inactive cells need not be valid.
    CHECK_EQ(player.Configure(12753,44100,pattern,bindings),PatternPlayerStatus::Success);
    pattern.cells[0][0]={NOTE_OFF,0}; bindings.bindings[0].sample={};
    CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,3),PatternPlayerStatus::Success);
    CHECK_EQ(output[0],10); CHECK_EQ(output[1],20); CHECK_EQ(output[2],30);
}

TEST_CASE(PatternPlayer_LateUnsupportedEventRollbackAndRetry)
{
    const auto run=[&](Note note,std::uint8_t instrument,PatternPlayerStatus expected) {
        auto pattern=Pattern(); pattern.active_rows=2; pattern.active_channels=1;
        pattern.cells[1][0]={note,instrument};
        NativeRatePatternPlayer player;
        CHECK_EQ(player.Configure(12000,100,pattern,Bindings(100)),PatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
        // Row 1 is at sample 12. A block can prepare row 0 successfully, then fail.
        std::int16_t output[16]; std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,2),PatternPlayerStatus::Success);
        CHECK_EQ(output[0],10); CHECK_EQ(output[1],20);
        const auto reference=player; // Existing active voice and established selection.
        std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,14),expected);
        CheckPlayback(player,reference); for (auto value:output) CHECK_EQ(value,777);
        CHECK_EQ(player.Render(output,10),PatternPlayerStatus::Success);
        for (std::size_t i=0;i<10;++i) CHECK_EQ(output[i],i+2<8 ? pcm[i+2] : 0);
        CHECK_EQ(player.GetNextSamplePosition(),12);
        const auto boundary=player;
        std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,1),expected); CheckPlayback(player,boundary);
        CHECK_EQ(output[0],777);
        player.Stop(); CheckCleared(player);
    };
    run(61,kNoInstrumentUpdate,PatternPlayerStatus::UnsupportedPitch);
    run(60,99,PatternPlayerStatus::UnknownInstrument);
    auto pattern=Pattern(); pattern.cells[0][0]={60,kNoInstrumentUpdate};
    NativeRatePatternPlayer missing;
    CHECK_EQ(missing.Configure(12000,44100,pattern,Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(missing.Start(),PatternPlayerStatus::Success);
    std::int16_t output=777;
    CHECK_EQ(missing.Render(&output,1),PatternPlayerStatus::MissingInstrument);
    CHECK_EQ(output,777); CheckCleared(missing); CHECK(missing.IsRunning());
}

TEST_CASE(PatternPlayer_SeparateCommandAndTickBudgetsAndArithmeticErrors)
{
    // 120.00 BPM / 100 Hz: row starts every 12.5 samples. Forty frames
    // contain four rows (32 commands) but only 320 ticks: command overflow alone.
    auto pattern=Pattern(); pattern.active_rows=1; pattern.active_channels=8;
    for (auto& cell:pattern.cells[0]) cell={NOTE_OFF,kNoInstrumentUpdate};
    NativeRateSampleBindings empty_bindings{};
    NativeRatePatternPlayer commands;
    CHECK_EQ(commands.Configure(12000,100,pattern,empty_bindings),PatternPlayerStatus::Success);
    CHECK_EQ(commands.Start(),PatternPlayerStatus::Success);
    const auto command_reference=commands;
    std::int16_t command_output[40]; std::fill_n(command_output,40,999);
    CHECK_EQ(commands.Render(command_output,40),PatternPlayerStatus::CommandCapacityExceeded);
    for (auto value:command_output) CHECK_EQ(value,999);
    CheckPlayback(commands,command_reference);
    // Exactly two rows / sixteen commands succeed; end-boundary row 2 is pending.
    CHECK_EQ(commands.Render(command_output,25),PatternPlayerStatus::Success);
    CHECK_EQ(commands.GetNextSamplePosition(),25);
    for (std::size_t i=0;i<25;++i) CHECK_EQ(command_output[i],0);
    std::int16_t output=999;
    // No commands: only the independent emitted-tick budget can fail.
    RealtimePattern empty_pattern;
    NativeRatePatternPlayer ticks;
    CHECK_EQ(ticks.Configure(12000,100,empty_pattern,empty_bindings),PatternPlayerStatus::Success);
    CHECK_EQ(ticks.Start(),PatternPlayerStatus::Success);
    const auto tick_reference=ticks;
    std::int16_t span[128]; std::fill_n(span,128,999);
    CHECK_EQ(ticks.Render(span,128),PatternPlayerStatus::TickBudgetExceeded);
    for (auto value : span) { CHECK_EQ(value, 999); }
    CheckPlayback(ticks, tick_reference);
    // Exactly 512 ticks in [0,64), followed by the allowed completion pull.
    CHECK_EQ(ticks.Render(span,64),PatternPlayerStatus::Success);
    CHECK_EQ(ticks.GetNextSamplePosition(),64);
    for (std::size_t i=0;i<64;++i) CHECK_EQ(span[i],0);
    CHECK_EQ(ticks.Render(span,64),PatternPlayerStatus::Success);
    CHECK_EQ(ticks.GetNextSamplePosition(),128);
    NativeRatePatternPlayerTestAccess::SetPosition(ticks,UINT64_MAX);
    const auto range_reference=ticks;
    CHECK_EQ(ticks.Render(&output,1),PatternPlayerStatus::RangeOverflow);
    CHECK_EQ(output,999); CheckPlayback(ticks,range_reference);
    ticks.Stop(); CHECK_EQ(ticks.Start(),PatternPlayerStatus::Success);
    NativeRatePatternPlayerTestAccess::ExhaustCursor(ticks);
    const auto exhausted_reference=ticks;
    CHECK_EQ(ticks.Render(&output,1),PatternPlayerStatus::ArithmeticExhausted);
    CHECK_EQ(output,999); CheckPlayback(ticks,exhausted_reference);
    CHECK_EQ(ticks.Start(),PatternPlayerStatus::Success);
    CHECK_EQ(ticks.Render(&output,1),PatternPlayerStatus::Success); CHECK_EQ(output,0);
}

TEST_CASE(PatternPlayer_16RowExpectedPcmAcrossPartitionsLoopsAndBoundaries)
{
    // Independently expected absolute rows at 44100 Hz, 127.53 BPM:
    // floor(row*264600000/51012). No production converter builds the oracle.
    constexpr std::size_t total=90000;
    std::vector<std::int16_t> a(7000),b(6000);
    for (std::size_t i=0;i<a.size();++i) a[i]=static_cast<std::int16_t>(100+i%31);
    for (std::size_t i=0;i<b.size();++i) b[i]=static_cast<std::int16_t>(-20-static_cast<int>(i%13));
    NativeRateSampleBindings bindings{}; bindings.count=2;
    bindings.bindings[0]={7,60,{a.data(),a.size(),44100}};
    bindings.bindings[1]={9,62,{b.data(),b.size(),44100}};
    RealtimePattern pattern; pattern.active_channels=2;
    pattern.cells[0][0]={60,7}; pattern.cells[0][1]={NOTE_EMPTY,9};
    pattern.cells[1][0]={60,kNoInstrumentUpdate}; pattern.cells[1][1]={62,kNoInstrumentUpdate};
    pattern.cells[2][0]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[3][1]={62,kNoInstrumentUpdate};
    pattern.cells[4][1]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[15][0]={60,kNoInstrumentUpdate};
    // Includes sample end padding, row-zero retrigger and selection across loops.
    std::vector<std::int16_t> expected(total);
    std::size_t next_row=0,position_a=a.size(),position_b=b.size();
    for (std::size_t frame=0;frame<total;++frame)
    {
        const auto row_sample=next_row*264600000ULL/51012ULL;
        if (frame==row_sample)
        {
            switch (next_row++%16)
            {
            case 0: position_a=0; break;
            case 1: position_a=0; position_b=0; break;
            case 2: position_a=a.size(); break;
            case 3: position_b=0; break;
            case 4: position_b=b.size(); break;
            case 15: position_a=0; break;
            }
        }
        const auto va=position_a<a.size() ? a[position_a++] : 0;
        const auto vb=position_b<b.size() ? b[position_b++] : 0;
        expected[frame]=static_cast<std::int16_t>(va+vb);
    }
    const auto collect=[&](bool irregular) {
        NativeRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,44100,pattern,bindings),PatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
        std::vector<std::int16_t> mixed(total);
        std::size_t start=0,part=0,next_boundary=1;
        while (start<total)
        {
            auto count=std::min<std::size_t>(irregular ? (part++*37)%128+1 : 128,total-start);
            if (irregular) // Deliberately end a block exactly at every row start.
            {
                const auto boundary=next_boundary*264600000ULL/51012ULL;
                if (start+count>=boundary) { count=static_cast<std::size_t>(boundary-start); ++next_boundary; }
            }
            CHECK(count>0 && count<=128);
            CHECK_EQ(player.Render(mixed.data()+start,count),PatternPlayerStatus::Success);
            start+=count;
            CHECK_EQ(player.GetNextSamplePosition(),start);
        }
        CHECK(player.IsRunning());
        player.Stop(); std::int16_t stopped[128]; std::fill_n(stopped,128,999);
        for (std::size_t block=0;block<total/128+1;++block)
            CHECK_EQ(player.Render(stopped,128),PatternPlayerStatus::Success);
        for (auto value:stopped) CHECK_EQ(value,0);
        CHECK_EQ(player.GetNextSamplePosition(),0); CHECK(!player.IsRunning()); CheckCleared(player);
        CHECK_EQ(player.Start(),PatternPlayerStatus::Success);
        CHECK_EQ(player.Render(stopped,128),PatternPlayerStatus::Success);
        CHECK(std::equal(stopped,stopped+128,expected.begin()));
        return mixed;
    };
    const auto regular=collect(false),irregular=collect(true);
    CHECK(regular==expected); CHECK(irregular==expected); CHECK(regular==irregular);
}
