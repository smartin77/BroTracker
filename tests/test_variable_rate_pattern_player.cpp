#include "test_framework.h"
#include "core/playback/variable_rate_pattern_player.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include "core/playback/native_rate_pattern_player.h"

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
    struct VariableRatePatternPlayerTestAccess
    {
        static const VariableRateSampleCommandPreparer& Preparer(const VariableRatePatternPlayer& player)
        { return player.preparer_; }
        static const VariableRateVoiceBlockRenderer& Renderer(const VariableRatePatternPlayer& player)
        { return player.renderer_; }
        static void SeedTick(VariableRatePatternPlayer& player, std::uint64_t tick) {
            CHECK_EQ(player.cursor_.ResetAtTick(tick, player.next_sample_), TickCursorStatus::Success);
        }
        static bool Pending(const VariableRatePatternPlayer& player, unsigned channel) {
            return player.pending_.entries[channel].active;
        }
        static std::uint64_t PendingSample(const VariableRatePatternPlayer& player, unsigned channel) {
            return player.pending_.entries[channel].sample;
        }
        static void SetPosition(VariableRatePatternPlayer& player, std::uint64_t position)
        { player.next_sample_ = position; }
        static void SetConsumedTick(VariableRatePatternPlayer& player, std::uint64_t tick)
        { player.position_ = {true, tick, 0, 0}; }
        static void ExhaustCursor(VariableRatePatternPlayer& player)
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
    VariableRateSampleBindings Bindings(std::uint32_t rate=44100)
    {
        VariableRateSampleBindings bindings{}; bindings.count=1;
        bindings.bindings[0]={7,60,{pcm,8,rate}};
        return bindings;
    }
    RealtimePattern Pattern()
    {
        RealtimePattern pattern; pattern.active_channels=2;
        pattern.cells[0][0]={60,7}; pattern.cells[0][1]={60,7};
        return pattern;
    }
    void CheckPlayback(const VariableRatePatternPlayer& actual, const VariableRatePatternPlayer& expected)
    {
        CHECK_EQ(actual.IsRunning(),expected.IsRunning());
        CHECK_EQ(actual.GetTransportState(),expected.GetTransportState());
        CHECK_EQ(actual.GetNextSamplePosition(),expected.GetNextSamplePosition());
        const auto a_position = actual.GetPlaybackPosition();
        const auto e_position = expected.GetPlaybackPosition();
        CHECK_EQ(a_position.valid, e_position.valid);
        CHECK_EQ(a_position.absolute_tick, e_position.absolute_tick);
        CHECK_EQ(a_position.pattern_row, e_position.pattern_row);
        CHECK_EQ(a_position.loop_index, e_position.loop_index);
        for (std::uint32_t c=0;c<8;++c)
        {
            CHECK_EQ(actual.GetChannelGains().channels[c],expected.GetChannelGains().channels[c]);
            CHECK_EQ(VariableRatePatternPlayerTestAccess::Pending(actual,c),VariableRatePatternPlayerTestAccess::Pending(expected,c));
            CHECK_EQ(VariableRatePatternPlayerTestAccess::PendingSample(actual,c),VariableRatePatternPlayerTestAccess::PendingSample(expected,c));
            LogicalChannelState a,b;
            CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(actual).GetChannel(c,a),ChannelStateStatus::Success);
            CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(expected).GetChannel(c,b),ChannelStateStatus::Success);
            CHECK_EQ(a.note,b.note); CHECK_EQ(a.instrument,b.instrument);
            const auto* va=VariableRatePatternPlayerTestAccess::Renderer(actual).GetVoice(c);
            const auto* vb=VariableRatePatternPlayerTestAccess::Renderer(expected).GetVoice(c);
            CHECK_EQ(va->IsActive(),vb->IsActive());
            CHECK_EQ(va->GetFramePosition(),vb->GetFramePosition());
            CHECK_EQ(va->GetFractionalPhase(),vb->GetFractionalPhase());
            CHECK_EQ(va->GetStep(),vb->GetStep());
            CHECK_EQ(va->GetOutputSampleRate(),vb->GetOutputSampleRate());
        }
    }
    void CheckCleared(const VariableRatePatternPlayer& player)
    {
        CHECK_EQ(player.GetNextSamplePosition(),0);
        CHECK(!player.GetPlaybackPosition().valid);
        for (std::uint32_t c=0;c<8;++c)
        {
            LogicalChannelState state;
            CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(player).GetChannel(c,state),ChannelStateStatus::Success);
            CHECK_EQ(state.note,NOTE_EMPTY); CHECK_EQ(state.instrument,kNoInstrumentUpdate);
            CHECK(!VariableRatePatternPlayerTestAccess::Renderer(player).GetVoice(c)->IsActive());
        }
    }
}
TEST_CASE(VariableRate_PatternPlayerPauseContinueSilenceContinuationSameRowAndLoop) {
    auto pattern = Pattern(); pattern.active_rows = 3; pattern.active_channels = 1;
    pattern.cells[1][0] = {60, kNoInstrumentUpdate};
    pattern.cells[2][0] = {60, kNoInstrumentUpdate};
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(12753, 100, pattern, Bindings(100)), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::InvalidTransportState);
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::InvalidTransportState);
    CHECK_EQ(player.Start(), VariableRatePatternPlayerStatus::Success);
    std::int16_t out[128]{};
    CHECK_EQ(player.Render(out, 5), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0], 10); CHECK_EQ(out[4], 50);
    const auto pos = player.GetPlaybackPosition(); const auto sample = player.GetNextSamplePosition();
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetTransportState(), VariableRatePatternTransportState::Paused);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, pos.absolute_tick);
    CHECK_EQ(player.Render(out, 128), VariableRatePatternPlayerStatus::Success);
    for (auto value : out) CHECK_EQ(value, 0);
    CHECK_EQ(player.GetNextSamplePosition(), sample);
    LogicalChannelState state;
    CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(player).GetChannel(0, state), ChannelStateStatus::Success);
    CHECK_EQ(state.instrument, 7); CHECK_EQ(state.note, 60);
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::Success);
    std::uint64_t next = 0;
    CHECK_EQ(TickToSamplePosition(0, 12753, 100, next), TickToSampleStatus::Success);
    CHECK_EQ(player.GetNextSamplePosition(), next);
    CHECK_EQ(player.Render(out, 2), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0], 10); CHECK_EQ(out[1], 20);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 0);
    CHECK_EQ(player.Render(out, 2), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0], 30); CHECK_EQ(out[1], 40); // Row command not replayed.
    CHECK_EQ(TickToSamplePosition(192, 12753, 100, next), TickToSampleStatus::Success);
    while (player.GetNextSamplePosition() < next + 1) {
        CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::Success);
    }
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 2);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(TickToSamplePosition(192, 12753, 100, next), TickToSampleStatus::Success);
    CHECK_EQ(player.GetNextSamplePosition(), next);
    CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0], 10); CHECK_EQ(player.GetPlaybackPosition().pattern_row, 2);
    CHECK_EQ(player.GetPlaybackPosition().loop_index, 0);
    player.Stop(); player.Stop(); CHECK_EQ(player.GetTransportState(), VariableRatePatternTransportState::Stopped);
    CHECK_EQ(player.GetNextSamplePosition(), 0); CHECK(!player.GetPlaybackPosition().valid);
    CHECK_EQ(player.Render(out, 3), VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0], 0);
}
TEST_CASE(VariableRate_PatternPlayerPauseBoundaryAndContinueRollback) {
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(12753, 44100, Pattern(), Bindings()), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success); // Nothing consumed: row zero pending.
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.GetNextSamplePosition(), 0);
    std::int16_t out[128]{}; CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::Success);
    std::uint64_t boundary = 0;
    CHECK_EQ(TickToSamplePosition(96, 12753, 44100, boundary), TickToSampleStatus::Success);
    while (player.GetNextSamplePosition() < boundary) {
        const auto count = std::min<std::uint64_t>(128, boundary - player.GetNextSamplePosition());
        CHECK_EQ(player.Render(out, count), VariableRatePatternPlayerStatus::Success);
    }
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 0);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    const auto reference = player;
    CHECK_EQ(player.Configure(0, 44100, Pattern(), Bindings()), VariableRatePatternPlayerStatus::InvalidTempo);
    CheckPlayback(player, reference);
    out[0] = 999;
    CHECK_EQ(player.Render(nullptr, 1), VariableRatePatternPlayerStatus::InvalidDestination);
    CHECK_EQ(player.Render(out, 129), VariableRatePatternPlayerStatus::InvalidFrameCount);
    CHECK_EQ(out[0], 999); CheckPlayback(player, reference);
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetNextSamplePosition(), 0); // Reported row zero, not unconsumed boundary row.
    CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 0);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    VariableRatePatternPlayerTestAccess::SetConsumedTick(player, UINT64_MAX);
    const auto exhausted = player;
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::RangeOverflow); CheckPlayback(player, exhausted);
    CHECK_EQ(player.GetTransportState(), VariableRatePatternTransportState::Paused);
    VariableRatePatternPlayerTestAccess::SetConsumedTick(player, UINT64_MAX / 2);
    const auto conversion_overflow = player;
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::RangeOverflow); CheckPlayback(player, conversion_overflow);
    // Continue succeeds transactionally; a later unsupported row fails Render
    // without losing its new segment or changing output.
    auto bad = Pattern(); bad.cells[1][0] = {61, 99};
    CHECK_EQ(player.Configure(12753, 44100, bad, Bindings()), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(), VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(), VariableRatePatternPlayerStatus::Success);
    VariableRatePatternPlayerTestAccess::SetConsumedTick(player,96);
    CHECK_EQ(player.Continue(), VariableRatePatternPlayerStatus::Success);
    const auto retry = player; out[0] = 999;
    CHECK_EQ(player.Render(out, 1), VariableRatePatternPlayerStatus::UnknownInstrument);
    CHECK_EQ(out[0], 999); CheckPlayback(player, retry);
}

TEST_CASE(VariableRate_PatternPlayer_TransportSilenceRestartAndRenderValidation)
{
    VariableRatePatternPlayer player;
    player.Stop(); player.Stop(); CheckCleared(player);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::NotConfigured);
    CHECK_EQ(player.Render(nullptr,0),VariableRatePatternPlayerStatus::Success);
    std::int16_t output[128]; std::fill_n(output,128,123);
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::NotConfigured); CHECK_EQ(output[0],123);
    CHECK_EQ(player.Configure(12753,44100,Pattern(),Bindings()),VariableRatePatternPlayerStatus::Success);
    CHECK(!player.IsRunning()); CheckCleared(player);
    CHECK_EQ(player.Render(output,128),VariableRatePatternPlayerStatus::Success);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(player.GetNextSamplePosition(),0);
    CHECK_EQ(player.Render(nullptr,1),VariableRatePatternPlayerStatus::InvalidDestination);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,3),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(output[0],20); CHECK_EQ(output[1],40); CHECK_EQ(output[2],60);
    auto original=player;
    CHECK_EQ(player.Render(nullptr,0),VariableRatePatternPlayerStatus::Success); CheckPlayback(player,original);
    std::fill_n(output,128,123);
    CHECK_EQ(player.Render(output,129),VariableRatePatternPlayerStatus::InvalidFrameCount);
    CHECK_EQ(player.Render(output,UINT64_MAX),VariableRatePatternPlayerStatus::InvalidFrameCount);
    CHECK_EQ(player.Render(nullptr,1),VariableRatePatternPlayerStatus::InvalidDestination);
    CheckPlayback(player,original); for (auto value:output) CHECK_EQ(value,123);
    CHECK_EQ(player.Render(output,2),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(output[0],80); CHECK_EQ(output[1],100);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CheckCleared(player);
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(output[0],20);
    player.Stop(); player.Stop(); CHECK(!player.IsRunning()); CheckCleared(player);
    CHECK_EQ(player.Render(output,128),VariableRatePatternPlayerStatus::Success);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(player.GetNextSamplePosition(),0);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(output[0],20);
    CHECK_EQ(player.Configure(12000,44100,Pattern(),Bindings()),VariableRatePatternPlayerStatus::Success);
    CHECK(!player.IsRunning()); CheckCleared(player);
}

TEST_CASE(VariableRate_PatternPlayer_TransactionalConfigurationAndMetadataCopies)
{
    VariableRatePatternPlayer player;
    auto pattern=Pattern(); auto bindings=Bindings();
    CHECK_EQ(player.Configure(12753,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t output[3]; CHECK_EQ(player.Render(output,3),VariableRatePatternPlayerStatus::Success);
    auto reference=player;
    const auto reject=[&](std::uint32_t tempo,std::uint32_t rate,const RealtimePattern& p,
        const VariableRateSampleBindings& b,VariableRatePatternPlayerStatus status) {
        CHECK_EQ(player.Configure(tempo,rate,p,b),status); CheckPlayback(player,reference);
    };
    reject(0,44100,pattern,bindings,VariableRatePatternPlayerStatus::InvalidTempo);
    reject(12753,0,pattern,bindings,VariableRatePatternPlayerStatus::InvalidSampleRate);
    auto invalid=pattern;
    for (const auto rows:{0,65,255}) { invalid.active_rows=static_cast<std::uint8_t>(rows);
        reject(12753,44100,invalid,bindings,VariableRatePatternPlayerStatus::InvalidDimensions); }
    invalid=pattern;
    for (const auto channels:{0,9,255}) { invalid.active_channels=static_cast<std::uint8_t>(channels);
        reject(12753,44100,invalid,bindings,VariableRatePatternPlayerStatus::InvalidDimensions); }
    invalid=pattern; invalid.cells[15][1].note=128; // Validate more than the first row.
    reject(12753,44100,invalid,bindings,VariableRatePatternPlayerStatus::InvalidNote);
    for (const auto note : {0, 23}) {
        invalid=pattern; invalid.cells[15][1].note=static_cast<Note>(note);
        reject(12753,44100,invalid,bindings,VariableRatePatternPlayerStatus::InvalidNote);
    }
    auto bad=bindings; bad.count=17;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::InvalidBindingCount);
    bad=bindings; bad.count=2; bad.bindings[1]=bad.bindings[0];
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::DuplicateInstrument);
    bad=bindings; bad.bindings[0].instrument=255;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::ReservedInstrument);
    bad=bindings; bad.bindings[0].reference_note=NOTE_OFF;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::InvalidReferenceNote);
    bad=bindings; bad.bindings[0].sample.data=nullptr;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::InvalidSample);
    bad=bindings; bad.bindings[0].sample.sample_rate_hz=48000;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::SampleRateMismatch);
    bad=bindings; bad.bindings[0].sample.frame_count=SIZE_MAX;
    reject(12753,44100,pattern,bad,VariableRatePatternPlayerStatus::RangeOverflow);
    std::int16_t expected[3];
    CHECK_EQ(player.Render(output,3),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(reference.Render(expected,3),VariableRatePatternPlayerStatus::Success);
    CHECK(std::equal(output,output+3,expected));
    pattern.active_rows=1; pattern.active_channels=1; pattern.cells[0][7].note=128;
    pattern.cells[15][0].note=128; // Inactive cells need not be valid.
    CHECK_EQ(player.Configure(12753,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    pattern.cells[0][0]={NOTE_OFF,0}; bindings.bindings[0].sample={};
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(output,3),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(output[0],10); CHECK_EQ(output[1],20); CHECK_EQ(output[2],30);
}

TEST_CASE(VariableRate_PatternPlayer_LateUnsupportedEventRollbackAndRetry)
{
    const auto run=[&](Note note,std::uint8_t instrument,VariableRatePatternPlayerStatus expected) {
        auto pattern=Pattern(); pattern.active_rows=2; pattern.active_channels=1;
        pattern.cells[1][0]={note,instrument};
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12000,100,pattern,Bindings(100)),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        // Row 1 is at sample 12. A block can prepare row 0 successfully, then fail.
        std::int16_t output[16]; std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,2),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(output[0],10); CHECK_EQ(output[1],20);
        const auto reference=player; // Existing active voice and established selection.
        std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,14),expected);
        CheckPlayback(player,reference); for (auto value:output) CHECK_EQ(value,777);
        CHECK_EQ(player.Render(output,10),VariableRatePatternPlayerStatus::Success);
        for (std::size_t i=0;i<10;++i) CHECK_EQ(output[i],i+2<8 ? pcm[i+2] : 0);
        CHECK_EQ(player.GetNextSamplePosition(),12);
        const auto boundary=player;
        std::fill_n(output,16,777);
        CHECK_EQ(player.Render(output,1),expected); CheckPlayback(player,boundary);
        CHECK_EQ(output[0],777);
        player.Stop(); CheckCleared(player);
    };
    run(60,99,VariableRatePatternPlayerStatus::UnknownInstrument);
    auto pattern=Pattern(); pattern.cells[0][0]={60,kNoInstrumentUpdate};
    VariableRatePatternPlayer missing;
    CHECK_EQ(missing.Configure(12000,44100,pattern,Bindings()),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(missing.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t output=777;
    CHECK_EQ(missing.Render(&output,1),VariableRatePatternPlayerStatus::MissingInstrument);
    CHECK_EQ(output,777); CheckCleared(missing); CHECK(missing.IsRunning());
}

TEST_CASE(VariableRate_PatternPlayer_SeparateCommandAndTickBudgetsAndArithmeticErrors)
{
    // 120.00 BPM / 100 Hz: row starts every 12.5 samples. Forty frames
    // contain four rows (32 commands) but only 320 ticks: command overflow alone.
    auto pattern=Pattern(); pattern.active_rows=1; pattern.active_channels=8;
    for (auto& cell:pattern.cells[0]) cell={NOTE_OFF,kNoInstrumentUpdate};
    VariableRateSampleBindings empty_bindings{};
    VariableRatePatternPlayer commands;
    CHECK_EQ(commands.Configure(12000,100,pattern,empty_bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(commands.Start(),VariableRatePatternPlayerStatus::Success);
    const auto command_reference=commands;
    std::int16_t command_output[40]; std::fill_n(command_output,40,999);
    CHECK_EQ(commands.Render(command_output,40),VariableRatePatternPlayerStatus::CommandCapacityExceeded);
    for (auto value:command_output) CHECK_EQ(value,999);
    CheckPlayback(commands,command_reference);
    // Exactly two rows / sixteen commands succeed; end-boundary row 2 is pending.
    CHECK_EQ(commands.Render(command_output,25),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(commands.GetNextSamplePosition(),25);
    for (std::size_t i=0;i<25;++i) CHECK_EQ(command_output[i],0);
    std::int16_t output=999;
    // No commands: only the independent emitted-tick budget can fail.
    RealtimePattern empty_pattern;
    VariableRatePatternPlayer ticks;
    CHECK_EQ(ticks.Configure(12000,100,empty_pattern,empty_bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(ticks.Start(),VariableRatePatternPlayerStatus::Success);
    const auto tick_reference=ticks;
    std::int16_t span[128]; std::fill_n(span,128,999);
    CHECK_EQ(ticks.Render(span,128),VariableRatePatternPlayerStatus::TickBudgetExceeded);
    for (auto value : span) { CHECK_EQ(value, 999); }
    CheckPlayback(ticks, tick_reference);
    // Exactly 512 ticks in [0,64), followed by the allowed completion pull.
    CHECK_EQ(ticks.Render(span,64),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(ticks.GetNextSamplePosition(),64);
    for (std::size_t i=0;i<64;++i) CHECK_EQ(span[i],0);
    CHECK_EQ(ticks.Render(span,64),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(ticks.GetNextSamplePosition(),128);
    VariableRatePatternPlayerTestAccess::SetPosition(ticks,UINT64_MAX);
    const auto range_reference=ticks;
    CHECK_EQ(ticks.Render(&output,1),VariableRatePatternPlayerStatus::RangeOverflow);
    CHECK_EQ(output,999); CheckPlayback(ticks,range_reference);
    ticks.Stop(); CHECK_EQ(ticks.Start(),VariableRatePatternPlayerStatus::Success);
    VariableRatePatternPlayerTestAccess::ExhaustCursor(ticks);
    const auto exhausted_reference=ticks;
    CHECK_EQ(ticks.Render(&output,1),VariableRatePatternPlayerStatus::ArithmeticExhausted);
    CHECK_EQ(output,999); CheckPlayback(ticks,exhausted_reference);
    CHECK_EQ(ticks.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(ticks.Render(&output,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(output,0);
}

TEST_CASE(VariableRate_PatternPlayer_16RowExpectedPcmAcrossPartitionsLoopsAndBoundaries)
{
    // Independently expected absolute rows at 44100 Hz, 127.53 BPM:
    // floor(row*264600000/51012). No production converter builds the oracle.
    constexpr std::size_t total=90000;
    std::vector<std::int16_t> a(7000),b(6000);
    for (std::size_t i=0;i<a.size();++i) a[i]=static_cast<std::int16_t>(100+i%31);
    for (std::size_t i=0;i<b.size();++i) b[i]=static_cast<std::int16_t>(-20-static_cast<int>(i%13));
    VariableRateSampleBindings bindings{}; bindings.count=2;
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
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
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
            CHECK_EQ(player.Render(mixed.data()+start,count),VariableRatePatternPlayerStatus::Success);
            start+=count;
            CHECK_EQ(player.GetNextSamplePosition(),start);
            // Independent inverse of the absolute floor conversion: t*A < end*D.
            // Covers empty rows, both partitions and the 16-row loop boundary.
            const auto position = player.GetPlaybackPosition();
            const auto last_tick = (start * 12753ULL * 384 - 1) / (44100ULL * 6000);
            CHECK(position.valid);
            CHECK_EQ(position.absolute_tick, last_tick);
            CHECK_EQ(position.pattern_row, (last_tick / 96) % 16);
            CHECK_EQ(position.loop_index, last_tick / 96 / 16);
        }
        CHECK(player.IsRunning());
        player.Stop(); std::int16_t stopped[128]; std::fill_n(stopped,128,999);
        for (std::size_t block=0;block<total/128+1;++block)
            CHECK_EQ(player.Render(stopped,128),VariableRatePatternPlayerStatus::Success);
        for (auto value:stopped) CHECK_EQ(value,0);
        CHECK_EQ(player.GetNextSamplePosition(),0); CHECK(!player.IsRunning()); CheckCleared(player);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Render(stopped,128),VariableRatePatternPlayerStatus::Success);
        CHECK(std::equal(stopped,stopped+128,expected.begin()));
        return mixed;
    };
    const auto regular=collect(false),irregular=collect(true);
    CHECK(regular==expected); CHECK(irregular==expected); CHECK(regular==irregular);
}

TEST_CASE(VariableRate_PatternPlayer_ConsumedPositionEmptyRowsTickFreeBlocksAndReset)
{
    VariableRatePatternPlayer player;
    RealtimePattern pattern;
    VariableRateSampleBindings bindings{};
    CHECK_EQ(player.Configure(12000, 44100, pattern, bindings), VariableRatePatternPlayerStatus::Success);
    CHECK(!player.GetPlaybackPosition().valid);
    CHECK_EQ(player.Start(), VariableRatePatternPlayerStatus::Success);
    CHECK(!player.GetPlaybackPosition().valid);
    std::int16_t output[128]{};
    CHECK_EQ(player.Render(output, 1), VariableRatePatternPlayerStatus::Success);
    CHECK(player.GetPlaybackPosition().valid);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 0);
    // [1,57) contains no tick; reporting must not follow the next block position.
    CHECK_EQ(player.Render(output, 56), VariableRatePatternPlayerStatus::Success);
    CHECK(player.GetPlaybackPosition().valid);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 0);
    CHECK_EQ(player.Render(nullptr, 0), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 0);
    const auto advance = [&](std::uint64_t end) {
        while (player.GetNextSamplePosition() < end)
            CHECK_EQ(player.Render(output, std::min<std::uint64_t>(128,
                end - player.GetNextSamplePosition())), VariableRatePatternPlayerStatus::Success);
    };
    advance(5512); // Tick 96 is at sample 5512, excluded from this block.
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 95);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 0);
    CHECK_EQ(player.Render(output, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 96);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 1);
    advance(88200);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 1535);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 15);
    CHECK_EQ(player.GetPlaybackPosition().loop_index, 0);
    CHECK_EQ(player.Render(output, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 1536);
    CHECK_EQ(player.GetPlaybackPosition().pattern_row, 0);
    CHECK_EQ(player.GetPlaybackPosition().loop_index, 1);
    const auto saved = player;
    CHECK_EQ(player.Render(nullptr, 0), VariableRatePatternPlayerStatus::Success);
    CheckPlayback(player, saved);
    CHECK_EQ(player.Render(nullptr, 1), VariableRatePatternPlayerStatus::InvalidDestination);
    CheckPlayback(player, saved);
    CHECK_EQ(player.Start(), VariableRatePatternPlayerStatus::Success);
    CheckCleared(player);
    CHECK_EQ(player.Render(output, 1), VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick, 0);
    player.Stop(); CheckCleared(player);
    CHECK_EQ(player.Render(output, 128), VariableRatePatternPlayerStatus::Success);
    CHECK(!player.GetPlaybackPosition().valid);
    CHECK_EQ(player.Configure(12753, 44100, pattern, bindings), VariableRatePatternPlayerStatus::Success);
    CHECK(!player.GetPlaybackPosition().valid);
}

TEST_CASE(VariableRate_CoreOffTimingExpectedPcmPartitionsAndLoopOrdering) {
    std::array<std::int16_t, 100> a, b; a.fill(10); b.fill(20);
    VariableRateSampleBindings bindings{}; bindings.count = 2;
    bindings.bindings[0] = {7,60,{a.data(),a.size(),100}};
    bindings.bindings[1] = {8,60,{b.data(),b.size(),100}};
    for (auto mode : {NoteOffTiming::Arrival, NoteOffTiming::EndOfPosition}) {
        RealtimePattern pattern; pattern.active_rows = 4; pattern.active_channels = 2;
        pattern.cells[0][0] = {60,7}; pattern.cells[0][1] = {60,8};
        pattern.cells[1][0] = {NOTE_OFF,0xFF}; pattern.note_off_timing[1][0] = mode;
        pattern.cells[2][0] = {NOTE_EMPTY,7}; // Instrument-only never retriggers.
        pattern.cells[3][1] = {NOTE_OFF,0xFF};
        pattern.note_off_timing[3][1] = NoteOffTiming::EndOfPosition;
        std::vector<std::int16_t> reference;
        for (const auto& partitions : {std::vector<unsigned>{11,12,12,12}, std::vector<unsigned>{1,7,19,3,17}}) {
            VariableRatePatternPlayer player;
            CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
            CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
            std::vector<std::int16_t> output(150); unsigned at = 0, part = 0;
            while (at < output.size()) {
                unsigned n = std::min<unsigned>(partitions[part++ % partitions.size()], output.size()-at);
                CHECK_EQ(player.Render(output.data()+at,n),VariableRatePatternPlayerStatus::Success); at += n;
            }
            for (unsigned sample = 0; sample < output.size(); ++sample) {
                unsigned loop = 0;
                // Independent rational row boundaries, not scheduler-generated expectation.
                while (((loop+1)*4ULL*150000/12753) <= sample) ++loop;
                const auto stop = (loop*4ULL + (mode == NoteOffTiming::Arrival ? 1 : 2))*150000/12753;
                CHECK_EQ(output[sample], sample < stop ? 30 : 20);
            }
            if (reference.empty()) reference=output; else CHECK(output==reference);
        }
    }
}
TEST_CASE(VariableRate_CoreOffTimingPendingRollbackPauseResetAndCapacity) {
    std::array<std::int16_t,100> pcm_long; pcm_long.fill(10);
    auto bindings=Bindings(100); bindings.bindings[0].sample={pcm_long.data(),100,100};
    RealtimePattern pattern; pattern.active_rows=4; pattern.active_channels=1;
    pattern.cells[0][0]={60,7}; pattern.cells[1][0]={NOTE_OFF,0xFF};
    pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    pattern.cells[2][0]={60,0xFF};
    VariableRatePatternPlayer player; CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t out[128]{}; CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success);
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    auto saved=player; out[0]=999;
    CHECK_EQ(player.Render(nullptr,12),VariableRatePatternPlayerStatus::InvalidDestination);
    CheckPlayback(player,saved); CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0)); CHECK_EQ(out[0],999);
    auto invalid=pattern; invalid.note_off_timing[1][0]=static_cast<NoteOffTiming>(99);
    CHECK_EQ(player.Configure(12753,100,invalid,bindings),VariableRatePatternPlayerStatus::InvalidNoteOffTiming);
    CheckPlayback(player,saved); CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Render(out,11),VariableRatePatternPlayerStatus::Success); // End 23 excludes boundary.
    for (unsigned i=0;i<11;++i) CHECK_EQ(out[i],10);
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Render(out,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],10); // Stop then new note.
    CHECK(!VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); CHECK(!VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Render(out,3),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],0);
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Render(out,1),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0],0); // OFF row replay: silenced voice stays silent, deferred Stop is renewed.
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(VariableRatePatternPlayerTestAccess::PendingSample(player,0),23);
    player.Stop(); player.Stop(); CHECK(!VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],10);
    pattern.active_rows=1; pattern.active_channels=8;
    for (unsigned c=0;c<8;++c) { pattern.cells[0][c]={NOTE_OFF,0xFF}; pattern.note_off_timing[0][c]=NoteOffTiming::EndOfPosition; }
    CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); saved=player; std::fill_n(out,40,999);
    CHECK_EQ(player.Render(out,40),VariableRatePatternPlayerStatus::CommandCapacityExceeded);
    CheckPlayback(player,saved);
    for(unsigned i=0;i<40;++i) CHECK_EQ(out[i],999);
    CHECK(!VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success); CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
}
TEST_CASE(VariableRate_CoreOffTimingBoundaryOverflowAndLateFailurePreservePending) {
    VariableRatePatternPlayer player; RealtimePattern pattern; pattern.active_rows=1; pattern.active_channels=1;
    pattern.cells[0][0]={NOTE_OFF,0xFF}; pattern.note_off_timing[0][0]=NoteOffTiming::EndOfPosition;
    VariableRateSampleBindings empty{};
    CHECK_EQ(player.Configure(UINT32_MAX,1,pattern,empty),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    VariableRatePatternPlayerTestAccess::SeedTick(player,(UINT64_MAX/kTicksPerRow)*kTicksPerRow);
    auto saved=player; std::int16_t out=999;
    CHECK_EQ(player.Render(&out,1),VariableRatePatternPlayerStatus::RangeOverflow); CHECK_EQ(out,999); CheckPlayback(player,saved);
    pattern.active_rows=4; pattern.cells[0][0]={60,7}; pattern.cells[1][0]={NOTE_OFF,0xFF};
    pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition; pattern.cells[2][0]={61,99};
    auto bindings=Bindings(100);
    CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); std::int16_t span[24];
    CHECK_EQ(player.Render(span,12),VariableRatePatternPlayerStatus::Success); saved=player; std::fill_n(span,24,999);
    CHECK_EQ(player.Render(span,12),VariableRatePatternPlayerStatus::UnknownInstrument); CheckPlayback(player,saved);
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0)); for(auto value:span) CHECK_EQ(value,999);
    CHECK_EQ(player.Render(span,11),VariableRatePatternPlayerStatus::Success);
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
}

TEST_CASE(VariableRate_CoreOffTimingMetadataPropagationValidationAndCopy) {
    RealtimePattern pattern; pattern.active_rows=2; pattern.active_channels=1;
    pattern.cells[0][0]={NOTE_OFF,7}; pattern.note_off_timing[0][0]=NoteOffTiming::EndOfPosition;
    RowEventBatch rows{};
    CHECK_EQ(GenerateRowEvents(MusicalTick{0,0,0},pattern,rows),RowEventStatus::Success);
    CHECK_EQ(rows.events[0].note_off_timing,NoteOffTiming::EndOfPosition);
    CHECK_EQ(rows.events[0].instrument,7);
    VariableRateSampleCommandPreparer preparer; auto bindings=Bindings(100);
    CHECK_EQ(preparer.Configure(100,bindings),VariableSampleCommandStatus::Success);
    VariableRateVoiceCommandBatch commands{}; commands.count=1;
    CHECK_EQ(preparer.Prepare(rows,commands),VariableSampleCommandStatus::Success); CHECK_EQ(commands.count,0);
    LogicalChannelState state; CHECK_EQ(preparer.GetChannel(0,state),ChannelStateStatus::Success);
    CHECK_EQ(state.note,NOTE_OFF); CHECK_EQ(state.instrument,7);
    rows.events[0].note_off_timing=static_cast<NoteOffTiming>(99);
    commands.count=1;
    CHECK_EQ(preparer.Prepare(rows,commands),VariableSampleCommandStatus::InvalidNoteOffTiming); CHECK_EQ(commands.count,1);
    LogicalChannelState unchanged; CHECK_EQ(preparer.GetChannel(0,unchanged),ChannelStateStatus::Success);
    CHECK_EQ(unchanged.instrument,state.instrument); CHECK_EQ(unchanged.note,state.note);
    auto saved_rows=rows; pattern.note_off_timing[0][0]=static_cast<NoteOffTiming>(99);
    CHECK_EQ(GenerateRowEvents(MusicalTick{0,0,0},pattern,rows),RowEventStatus::InvalidNoteOffTiming);
    CHECK_EQ(rows.events[0].note_off_timing,saved_rows.events[0].note_off_timing);
    pattern.note_off_timing[0][0]=NoteOffTiming::EndOfPosition;
    VariableRatePatternPlayer player; CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    pattern.note_off_timing[0][0]=NoteOffTiming::Arrival; // Player owns its metadata copy.
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); std::int16_t out;
    CHECK_EQ(player.Render(&out,1),VariableRatePatternPlayerStatus::Success);
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK(!VariableRatePatternPlayerTestAccess::Pending(player,0));
}

TEST_CASE(VariableRate_CoreOffTimingSampleConversionOverflowIsTransactional) {
    // Find the final representable tick in the test, then consume its row start.
    std::uint64_t low=0, high=UINT64_MAX;
    while (low < high) {
        const auto middle=low+(high-low)/2+1; std::uint64_t sample;
        if (TickToSamplePosition(middle,1,UINT32_MAX,sample)==TickToSampleStatus::Success) low=middle;
        else high=middle-1;
    }
    RealtimePattern pattern; pattern.active_rows=1; pattern.active_channels=1;
    pattern.cells[0][0]={NOTE_OFF,0xFF}; pattern.note_off_timing[0][0]=NoteOffTiming::EndOfPosition;
    VariableRateSampleBindings empty{}; VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(1,UINT32_MAX,pattern,empty),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    VariableRatePatternPlayerTestAccess::SeedTick(player,(low/kTicksPerRow)*kTicksPerRow);
    const auto saved=player; std::int16_t out=999;
    CHECK_EQ(player.Render(&out,1),VariableRatePatternPlayerStatus::RangeOverflow);
    CHECK_EQ(out,999); CheckPlayback(player,saved);
}

TEST_CASE(VariableRate_StoppedPatternReplacementValidatesContinuationAndPlaysRetainedBindings) {
    VariableRatePatternPlayer player;
    auto original=Pattern(); auto bindings=Bindings(100);
    CHECK_EQ(player.ReplacePattern(original),VariableRatePatternPlayerStatus::NotConfigured);
    CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
    RealtimePattern replacement; replacement.active_rows=3; replacement.active_channels=1;
    replacement.cells[0][0]={NOTE_EMPTY,7}; // Selection without trigger.
    replacement.cells[1][0]={60,0xFF};
    replacement.cells[2][0]={NOTE_OFF,0xFF}; replacement.note_off_timing[2][0]=NoteOffTiming::EndOfPosition;
    CHECK_EQ(player.ReplacePattern(replacement),VariableRatePatternPlayerStatus::Success); CheckCleared(player);
    CHECK_EQ(player.GetTransportState(),VariableRatePatternTransportState::Stopped);
    replacement.cells[1][0].note=61; // Copies caller metadata.
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t out[64]; CHECK_EQ(player.Render(out,60),VariableRatePatternPlayerStatus::Success);
    for (unsigned i=0;i<60;++i) {
        // Row 1 triggers at floor(150000/12753)=11; next loop at row 4=47.
        const unsigned trigger=i>=47 ? 47 : 11;
        const auto expected=(i>=trigger && i-trigger<8) ? pcm[i-trigger] : 0;
        CHECK_EQ(out[i],expected);
    }
    player.Stop();
    // A later instrument selection cannot retroactively resolve the first note;
    // validation must begin from cleared state, not assume a prior loop.
    replacement.active_rows=2; replacement.cells[0][0]={60,7}; replacement.cells[1][0]={NOTE_EMPTY,99};
    replacement.cells[0][0].instrument=0xFF;
    CHECK_EQ(player.ReplacePattern(replacement),VariableRatePatternPlayerStatus::MissingInstrument);
    replacement.active_rows=3; replacement.cells[0][0]={NOTE_EMPTY,7};
    replacement.cells[1][0]={60,0xFF}; replacement.cells[2][0]={NOTE_EMPTY,99};
    // Selection at row zero repairs unknown last-loop selection: valid semantics.
    CHECK_EQ(player.ReplacePattern(replacement),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Render(out,60),VariableRatePatternPlayerStatus::Success);
}
TEST_CASE(VariableRate_StoppedPatternReplacementRejectionsPreserveAllPlayback) {
    VariableRatePatternPlayer player; const auto original=Pattern(); const auto bindings=Bindings(100);
    CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
    const auto baseline=player;
    for (unsigned kind=0;kind<7;++kind) {
        auto candidate=original; VariableRatePatternPlayerStatus expected;
        switch(kind) {
        case 0: candidate.active_rows=0; expected=VariableRatePatternPlayerStatus::InvalidDimensions; break;
        case 1: candidate.active_channels=9; expected=VariableRatePatternPlayerStatus::InvalidDimensions; break;
        case 2: candidate.cells[0][0].note=23; expected=VariableRatePatternPlayerStatus::InvalidNote; break;
        case 3: candidate.note_off_timing[0][0]=static_cast<NoteOffTiming>(2); expected=VariableRatePatternPlayerStatus::InvalidNoteOffTiming; break;
        case 4: candidate.cells[0][0].instrument=0xFF; expected=VariableRatePatternPlayerStatus::MissingInstrument; break;
        case 5: candidate.cells[0][0].instrument=99; expected=VariableRatePatternPlayerStatus::UnknownInstrument; break;
        default: candidate.cells[0][0].instrument=98; expected=VariableRatePatternPlayerStatus::UnknownInstrument; break;
        }
        CHECK_EQ(player.ReplacePattern(candidate),expected); CheckPlayback(player,baseline);
        auto probe=player; auto reference=baseline; std::int16_t actual[12], wanted[12];
        CHECK_EQ(probe.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(reference.Start(),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(probe.Render(actual,12),VariableRatePatternPlayerStatus::Success); CHECK_EQ(reference.Render(wanted,12),VariableRatePatternPlayerStatus::Success);
        for(unsigned i=0;i<12;++i) CHECK_EQ(actual[i],wanted[i]);
    }
    auto endoff=original; endoff.cells[1][0]={NOTE_OFF,0xFF}; endoff.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    CHECK_EQ(player.ReplacePattern(endoff),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); std::int16_t out[12];
    CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success); const auto playing=player;
    CHECK(VariableRatePatternPlayerTestAccess::Pending(player,0));
    CHECK_EQ(player.ReplacePattern(original),VariableRatePatternPlayerStatus::InvalidTransportState); CheckPlayback(player,playing);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); const auto paused=player;
    CHECK_EQ(player.ReplacePattern(original),VariableRatePatternPlayerStatus::InvalidTransportState); CheckPlayback(player,paused);
    player.Stop(); CHECK_EQ(player.ReplacePattern(original),VariableRatePatternPlayerStatus::Success); CheckCleared(player);
}

TEST_CASE(VariableRate_CurrentRowReplayAtRowStartsMidRowsAndLaterLoopsIsPartitionIndependent) {
    auto pattern=Pattern(); pattern.active_rows=3; pattern.active_channels=1;
    pattern.cells[1][0]={60,kNoInstrumentUpdate}; pattern.cells[2][0]={60,kNoInstrumentUpdate};
    for (const unsigned loop : {0u,1u,3u}) for (unsigned row=0;row<3;++row)
        for (const unsigned consumed_frames : {1u,4u}) {
            VariableRatePatternPlayer player;
            CHECK_EQ(player.Configure(12753,100,pattern,Bindings(100)),VariableRatePatternPlayerStatus::Success);
            CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
            const auto tick=(loop*3+row)*kTicksPerRow;
            std::uint64_t start=0; CHECK_EQ(TickToSamplePosition(tick,12753,100,start),TickToSampleStatus::Success);
            std::int16_t out[128]{};
            while(player.GetNextSamplePosition()<start+consumed_frames) {
                const auto n=std::min<std::uint64_t>(32,start+consumed_frames-player.GetNextSamplePosition());
                CHECK_EQ(player.Render(out,n),VariableRatePatternPlayerStatus::Success);
            }
            CHECK_EQ(player.GetPlaybackPosition().pattern_row,row); CHECK_EQ(player.GetPlaybackPosition().loop_index,loop);
            for(unsigned repeat=0;repeat<2;++repeat) {
                CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
                CHECK_EQ(player.Render(out,4),VariableRatePatternPlayerStatus::Success);
                for(unsigned i=0;i<4;++i) CHECK_EQ(out[i],0);
                CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.GetNextSamplePosition(),start);
                auto irregular=player; std::int16_t split[8]{};
                CHECK_EQ(player.Render(out,8),VariableRatePatternPlayerStatus::Success);
                CHECK_EQ(irregular.Render(split,1),VariableRatePatternPlayerStatus::Success);
                CHECK_EQ(irregular.Render(split+1,3),VariableRatePatternPlayerStatus::Success);
                CHECK_EQ(irregular.Render(split+4,4),VariableRatePatternPlayerStatus::Success);
                for(unsigned i=0;i<8;++i) { CHECK_EQ(out[i],pcm[i]); CHECK_EQ(split[i],pcm[i]); }
                CHECK_EQ(player.GetPlaybackPosition().pattern_row,row); CHECK_EQ(player.GetPlaybackPosition().loop_index,loop);
                CheckPlayback(player,irregular);
            }
        }
}

TEST_CASE(VariableRate_PausedReplacementRebuildsNewContinuationWithoutConsumingReportedRow) {
    const std::int16_t alternate[]={-11,-22,-33,-44,-55,-66,-77,-88};
    auto bindings=Bindings(100); bindings.count=2; bindings.bindings[1]={8,60,{alternate,8,100}};
    RealtimePattern original; original.active_rows=3; original.active_channels=1;
    original.cells[0][0]={NOTE_EMPTY,7}; original.cells[1][0]={60,255}; original.cells[2][0]={60,7};
    auto replacement=original; replacement.cells[0][0].instrument=8;
    for(unsigned loop : {0u,1u,5u}) for(unsigned row=0;row<3;++row) {
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::uint64_t start=0; CHECK_EQ(TickToSamplePosition((loop*3+row)*kTicksPerRow,12753,100,start),TickToSampleStatus::Success);
        std::int16_t out[128]{};
        while(player.GetNextSamplePosition()<start+1) {
            const auto count=std::min<std::uint64_t>(32,start+1-player.GetNextSamplePosition());
            CHECK_EQ(player.Render(out,count),VariableRatePatternPlayerStatus::Success);
        }
        CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); const auto before=player.GetPlaybackPosition();
        const auto next=player.GetNextSamplePosition();
        CHECK_EQ(player.ReplacePausedPattern(replacement),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.GetTransportState(),VariableRatePatternTransportState::Paused);
        CHECK_EQ(player.GetPlaybackPosition().absolute_tick,before.absolute_tick);
        CHECK_EQ(player.GetPlaybackPosition().loop_index,loop); CHECK_EQ(player.GetNextSamplePosition(),next);
        LogicalChannelState state; CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(player).GetChannel(0,state),ChannelStateStatus::Success);
        CHECK_EQ(state.instrument,row ? 8 : loop ? 7 : 255);
        CHECK_EQ(player.Render(out,8),VariableRatePatternPlayerStatus::Success); for(unsigned i=0;i<8;++i) CHECK_EQ(out[i],0);
        CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.GetNextSamplePosition(),start);
        CHECK_EQ(player.Render(out,8),VariableRatePatternPlayerStatus::Success);
        for(unsigned i=0;i<8;++i) CHECK_EQ(out[i],row==1 ? alternate[i] : row==2 ? pcm[i] : 0);
    }
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.ReplacePausedPattern(replacement),VariableRatePatternPlayerStatus::Success);
    LogicalChannelState state; CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(player).GetChannel(0,state),ChannelStateStatus::Success);
    CHECK_EQ(state.instrument,255); CHECK_EQ(state.note,NOTE_EMPTY); CHECK(!player.GetPlaybackPosition().valid);
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(player.GetNextSamplePosition(),0);
}
TEST_CASE(VariableRate_PausedReplacementValidationAndRollback) {
    auto original=Pattern(); original.active_rows=3; original.active_channels=1;
    original.cells[1][0]={NOTE_OFF,255}; original.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    VariableRatePatternPlayer player;
    CHECK_EQ(player.ReplacePausedPattern(original),VariableRatePatternPlayerStatus::NotConfigured);
    CHECK_EQ(player.Configure(12753,100,original,Bindings(100)),VariableRatePatternPlayerStatus::Success);
    auto saved=player; CHECK_EQ(player.ReplacePausedPattern(original),VariableRatePatternPlayerStatus::InvalidTransportState); CheckPlayback(player,saved);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); saved=player;
    CHECK_EQ(player.ReplacePausedPattern(original),VariableRatePatternPlayerStatus::InvalidTransportState); CheckPlayback(player,saved);
    std::int16_t out[128]{}; CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); saved=player;
    for(unsigned mode=0;mode<7;++mode) {
        auto bad=original; auto status=VariableRatePatternPlayerStatus::InvalidDimensions;
        if(mode==0) bad.active_rows=2;
        if(mode==1) bad.active_channels=2;
        if(mode==2) { bad.cells[0][0].note=23; status=VariableRatePatternPlayerStatus::InvalidNote; }
        if(mode==3) { bad.note_off_timing[1][0]=static_cast<NoteOffTiming>(9); status=VariableRatePatternPlayerStatus::InvalidNoteOffTiming; }
        if(mode==4) { bad.cells[0][0].instrument=255; status=VariableRatePatternPlayerStatus::MissingInstrument; }
        if(mode==5) { bad.cells[0][0].instrument=99; status=VariableRatePatternPlayerStatus::UnknownInstrument; }
        if(mode==6) { bad.cells[0][0].instrument=98; status=VariableRatePatternPlayerStatus::UnknownInstrument; }
        CHECK_EQ(player.ReplacePausedPattern(bad),status); CheckPlayback(player,saved);
        auto retry=player, reference=saved;
        CHECK_EQ(retry.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(reference.Continue(),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(retry.Render(out,1),VariableRatePatternPlayerStatus::Success); std::int16_t expected=99;
        CHECK_EQ(reference.Render(&expected,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],expected);
    }
    for(const auto timing : {NoteOffTiming::Arrival,NoteOffTiming::EndOfPosition}) {
        auto replacement=original; replacement.note_off_timing[1][0]=timing;
        auto trial=saved; CHECK_EQ(trial.ReplacePausedPattern(replacement),VariableRatePatternPlayerStatus::Success);
        CHECK(!VariableRatePatternPlayerTestAccess::Pending(trial,0));
        LogicalChannelState state; CHECK_EQ(VariableRatePatternPlayerTestAccess::Preparer(trial).GetChannel(0,state),ChannelStateStatus::Success);
        CHECK_EQ(state.note,60); CHECK_EQ(state.instrument,7); // Paused OFF row not consumed in reconstruction.
        CHECK_EQ(trial.Continue(),VariableRatePatternPlayerStatus::Success); CHECK_EQ(trial.Render(out,1),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(out[0],0); CHECK_EQ(VariableRatePatternPlayerTestAccess::Pending(trial,0),timing==NoteOffTiming::EndOfPosition);
    }
}


TEST_CASE(VariableRate_WorkingPreviewBoundaryStopsReconstructionAndPartitionIndependence) {
    std::array<std::int16_t,128> a{},b{}; a.fill(1); b.fill(2);
    VariableRateSampleBindings bindings{}; bindings.count=2;
    bindings.bindings[0]={7,60,{a.data(),a.size(),100}};
    bindings.bindings[1]={8,60,{b.data(),b.size(),100}};
    RealtimePattern original; original.active_rows=4; original.active_channels=2;
    original.cells[0][0]={60,7}; original.cells[0][1]={60,7};
    original.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};
    original.cells[1][1]={NOTE_OFF,kNoInstrumentUpdate};
    original.note_off_timing[1][0]=original.note_off_timing[1][1]=NoteOffTiming::EndOfPosition;
    auto candidate=original; candidate.cells[0][0]={NOTE_EMPTY,8};
    candidate.cells[0][1]={NOTE_EMPTY,8}; // Reconstruction, not actual past audio.
    candidate.cells[2][0]={60,kNoInstrumentUpdate}; candidate.cells[2][1]={60,kNoInstrumentUpdate};
    // Independent rational boundaries: row 1=11, row 2=23, loop=47 at 100Hz/127.53.
    const auto render=[&](const std::vector<unsigned>& spans) {
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> pcm(100); CHECK_EQ(player.Render(pcm.data(),12),VariableRatePatternPlayerStatus::Success);
        auto deleted=candidate; deleted.cells[2][0]={}; deleted.cells[2][1]={};
        CHECK_EQ(player.ReplaceWorkingPattern(deleted),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.ReplaceWorkingPattern(candidate),VariableRatePatternPlayerStatus::Success);
        const auto reference=player; auto bad=candidate; bad.cells[3][1]={61,99};
        CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::UnknownInstrument);
        CheckPlayback(player,reference); CHECK(player.HasPendingWorkingPattern());
        std::int16_t sentinel=999;
        CHECK_EQ(player.Render(&sentinel,129),VariableRatePatternPlayerStatus::InvalidFrameCount);
        CHECK_EQ(sentinel,999); CHECK(player.HasPendingWorkingPattern());
        unsigned offset=12,index=0;
        while(offset<pcm.size()) {
            const auto count=std::min<unsigned>(spans[index++%spans.size()],pcm.size()-offset);
            CHECK_EQ(player.Render(pcm.data()+offset,count),VariableRatePatternPlayerStatus::Success); offset+=count;
        }
        CHECK(!player.HasPendingWorkingPattern());
        // Two original voices until 23. Deferred Stops precede BOTH new triggers.
        // New instrument-only row 0 continues voices; row 2 retriggers each loop.
        for(unsigned i=0;i<pcm.size();++i) CHECK_EQ(pcm[i],i<23?2:4);
        return pcm;
    };
    CHECK(render({8})==render({1,11,3,7}));
}

TEST_CASE(VariableRate_WorkingPreviewDeletionArrivalPauseStopAndPublicationRestrictions) {
    std::array<std::int16_t,128> pcm{}; pcm.fill(7);
    VariableRateSampleBindings bindings{}; bindings.count=1; bindings.bindings[0]={7,60,{pcm.data(),pcm.size(),100}};
    RealtimePattern original; original.active_rows=2; original.active_channels=1;
    original.cells[0][0]={60,7}; original.cells[1][0]={60,7};
    VariableRatePatternPlayer player;
    CHECK_EQ(player.ReplaceWorkingPattern(original),VariableRatePatternPlayerStatus::NotConfigured);
    CHECK_EQ(player.Configure(12753,100,original,bindings),VariableRatePatternPlayerStatus::Success);
    auto changed=original; changed.cells[1][0]={};
    CHECK_EQ(player.ReplaceWorkingPattern(changed),VariableRatePatternPlayerStatus::Success); CheckCleared(player);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t out[128]{}; CHECK_EQ(player.Render(out,5),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.ReplaceWorkingPattern(changed),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,7),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<7;++i) CHECK_EQ(out[i],7); // Deletion does not stop sounding audio.
    auto off=changed; off.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};
    CHECK_EQ(player.ReplaceWorkingPattern(off),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
    CHECK(player.HasPendingWorkingPattern());
    CHECK_EQ(player.Render(out,3),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],0);
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],0);
    CHECK(!player.HasPendingWorkingPattern()); // Arrival OFF on replayed row 1.
    CHECK_EQ(player.ReplacePattern(original),VariableRatePatternPlayerStatus::InvalidTransportState);
    CHECK_EQ(player.ReplacePausedPattern(original),VariableRatePatternPlayerStatus::InvalidTransportState);
    CHECK_EQ(player.ReplaceWorkingPattern(original),VariableRatePatternPlayerStatus::Success);
    player.Stop(); CHECK(!player.HasPendingWorkingPattern()); CheckCleared(player);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,12),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<12;++i) CHECK_EQ(out[i],7);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
    const auto before=player.GetPlaybackPosition();
    CHECK_EQ(player.ReplaceWorkingPattern(off),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick,before.absolute_tick);
    CHECK_EQ(player.GetTransportState(),VariableRatePatternTransportState::Paused);
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],0);
    auto bad=original; bad.active_rows=3; const auto reference=player;
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::InvalidDimensions); CheckPlayback(player,reference);
    bad=original; bad.cells[0][0]={60,99};
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::UnknownInstrument); CheckPlayback(player,reference);
    bad=original; bad.cells[0][0]={60,kNoInstrumentUpdate};
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::MissingInstrument); CheckPlayback(player,reference);
    bad=original; bad.cells[0][0]={23,7};
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::InvalidNote); CheckPlayback(player,reference);
    bad=original; bad.note_off_timing[0][0]=static_cast<NoteOffTiming>(9);
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::InvalidNoteOffTiming); CheckPlayback(player,reference);
}


TEST_CASE(VariableRate_WorkingPreviewBoundaryOwnershipAndLateRollbackRetry) {
    auto original=Pattern(); original.active_rows=2; original.active_channels=8;
    for(unsigned c=0;c<8;++c) original.cells[0][c]={60,7};
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(150000,100,original,Bindings(100)),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    auto candidate=original; for(unsigned c=0;c<8;++c) candidate.cells[1][c]={60,7};
    CHECK_EQ(player.ReplaceWorkingPattern(candidate),VariableRatePatternPlayerStatus::Success);
    const auto reference=player;
    std::int16_t output[16]; std::fill(std::begin(output),std::end(output),999);
    CHECK_EQ(player.Render(output,3),VariableRatePatternPlayerStatus::CommandCapacityExceeded);
    CheckPlayback(player,reference); CHECK(player.HasPendingWorkingPattern());
    for(auto value:output) CHECK_EQ(value,999);
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(output[0],80);
    CHECK(!player.HasPendingWorkingPattern());
    // Row 1 at sample 1 belongs to the NEXT block, not the preceding [0,1).
    auto off=candidate; for(unsigned c=0;c<8;++c) off.cells[1][c]={NOTE_OFF,kNoInstrumentUpdate};
    CHECK_EQ(player.ReplaceWorkingPattern(off),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(nullptr,0),VariableRatePatternPlayerStatus::Success); CHECK(player.HasPendingWorkingPattern());
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::Success); CHECK_EQ(output[0],0);
    CHECK(!player.HasPendingWorkingPattern());
}

TEST_CASE(VariableRate_PatternPlayerGainPcmAndTransactionalUpdates) {
    VariableRatePatternPlayer player;
    for(auto gain:player.GetChannelGains().channels) CHECK_EQ(gain,kPcm16UnityGain);
    CHECK_EQ(player.Configure(12753,44100,Pattern(),Bindings()),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t out[2];
    CHECK_EQ(player.Render(out,2),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0],20); CHECK_EQ(out[1],40); // unchanged unity
    auto gains=player.GetChannelGains(); gains.channels[0]=kPcm16UnityGain/2; gains.channels[1]=0;
    const auto reference=player;
    CHECK_EQ(player.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetNextSamplePosition(),reference.GetNextSamplePosition());
    CHECK_EQ(player.Render(out,2),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[0],15); CHECK_EQ(out[1],20); // ongoing voices, no restart
    const auto saved=player;
    for(unsigned c=0;c<8;++c) {
        auto bad=gains; bad.channels[c]=kPcm16UnityGain+1;
        CHECK_EQ(player.SetChannelGains(bad),VariableRatePatternPlayerStatus::InvalidGain); CheckPlayback(player,saved);
    }
    out[0]=999; out[1]=999;
    CHECK_EQ(player.Render(out,129),VariableRatePatternPlayerStatus::InvalidFrameCount);
    CheckPlayback(player,saved); CHECK_EQ(out[0],999); CHECK_EQ(out[1],999);
    CHECK_EQ(player.Render(out,2),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],25); CHECK_EQ(out[1],30);
    for(auto& gain:gains.channels) gain=0;
    CHECK_EQ(player.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,2),VariableRatePatternPlayerStatus::Success); CHECK_EQ(out[0],0); CHECK_EQ(out[1],0);
}
TEST_CASE(VariableRate_PatternPlayerGainsSurviveTransportReplacementAndConfigureFailure) {
    VariableRatePatternPlayer player; auto pattern=Pattern();
    CHECK_EQ(player.Configure(12753,44100,pattern,Bindings()),VariableRatePatternPlayerStatus::Success);
    Pcm16ChannelGains gains; gains.channels[0]=12345; gains.channels[7]=0;
    const auto check=[&]() { for(unsigned c=0;c<8;++c) CHECK_EQ(player.GetChannelGains().channels[c],gains.channels[c]); };
    CHECK_EQ(player.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.ReplacePattern(pattern),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.ReplaceWorkingPattern(pattern),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); check();
    std::int16_t out;
    CHECK_EQ(player.Render(&out,1),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.ReplacePausedPattern(pattern),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.ReplaceWorkingPattern(pattern),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success); check();
    CHECK_EQ(player.ReplaceWorkingPattern(pattern),VariableRatePatternPlayerStatus::Success);
    CHECK(player.HasPendingWorkingPattern());
    CHECK_EQ(player.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success); CHECK(player.HasPendingWorkingPattern());
    const auto saved=player;
    CHECK_EQ(player.Configure(0,44100,pattern,Bindings()),VariableRatePatternPlayerStatus::InvalidTempo); CheckPlayback(player,saved);
    auto bad=pattern; bad.active_rows=0;
    CHECK_EQ(player.Configure(12753,44100,bad,Bindings()),VariableRatePatternPlayerStatus::InvalidDimensions); CheckPlayback(player,saved);
    auto bindings=Bindings(); bindings.bindings[0].sample.sample_rate_hz=48000;
    CHECK_EQ(player.Configure(12753,44100,pattern,bindings),VariableRatePatternPlayerStatus::SampleRateMismatch); CheckPlayback(player,saved);
    player.Stop(); check(); CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success); check(); player.Stop(); check();
    CHECK_EQ(player.Configure(12753,44100,pattern,Bindings()),VariableRatePatternPlayerStatus::Success);
    for(auto gain:player.GetChannelGains().channels) CHECK_EQ(gain,kPcm16UnityGain);
}
TEST_CASE(VariableRate_PatternPlayerGainBoundariesPartitionIndependenceAndDeferredOff) {
    const std::int16_t sample[32]={100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,
        100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
    VariableRateSampleBindings bindings; bindings.count=1; bindings.bindings[0]={7,60,{sample,32,100}};
    RealtimePattern pattern; pattern.active_rows=2; pattern.active_channels=1;
    pattern.cells[0][0]={60,7}; pattern.cells[1][0]={NOTE_OFF,255};
    pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    const auto collect=[&](unsigned limit) {
        VariableRatePatternPlayer player; CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> output(36);
        for(unsigned begin=0;begin<36;) {
            if(begin==5 || begin==14 || begin==25) {
                auto gains=player.GetChannelGains(); gains.channels[0]=begin==5 ? kPcm16UnityGain/2 : begin==14 ? 0 : kPcm16UnityGain;
                const auto position=player.GetNextSamplePosition();
                const auto pending=VariableRatePatternPlayerTestAccess::Pending(player,0);
                CHECK_EQ(player.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success);
                CHECK_EQ(player.GetNextSamplePosition(),position);
                CHECK_EQ(VariableRatePatternPlayerTestAccess::Pending(player,0),pending);
            }
            unsigned end=std::min(36u,begin+limit);
            for(auto boundary:{5u,14u,25u}) if(begin<boundary) end=std::min(end,boundary);
            CHECK_EQ(player.Render(output.data()+begin,end-begin),VariableRatePatternPlayerStatus::Success); begin=end;
        }
        return output;
    };
    const auto regular=collect(4),irregular=collect(7);
    CHECK(regular==irregular);
    // Row boundaries at samples 11,23,35. End OFF at 23 precedes loop trigger.
    for(unsigned i=0;i<36;++i) CHECK_EQ(regular[i],i<5 ? 100 : i<14 ? 50 : i<25 ? 0 : 100);
}

TEST_CASE(VariableRate_IndependentTransposedPcmAcrossLoopsAndOffBoundaries) {
    std::array<std::int16_t,64> ramp{},negative{};
    for(unsigned i=0;i<64;++i) { ramp[i]=static_cast<std::int16_t>(100*i); negative[i]=-10; }
    const std::int16_t short_pcm[]={10,20,30,40};
    VariableRateSampleBindings bindings{}; bindings.count=3;
    bindings.bindings[0]={7,60,{ramp.data(),ramp.size(),100}};
    bindings.bindings[1]={8,60,{negative.data(),negative.size(),100}};
    bindings.bindings[2]={9,60,{short_pcm,4,100}};
    RealtimePattern pattern; pattern.active_rows=3; pattern.active_channels=3;
    pattern.cells[0][0]={61,7}; pattern.cells[0][1]={60,8}; pattern.cells[0][2]={48,9};
    pattern.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    pattern.cells[1][1]={NOTE_OFF,kNoInstrumentUpdate}; // Arrival.
    pattern.cells[1][2]={NOTE_EMPTY,9}; // Must not retrigger the completed half-rate voice.
    pattern.cells[2][0]={72,kNoInstrumentUpdate}; // Due deferred Stop precedes this Trigger.
    const auto collect=[&](const std::vector<unsigned>& partitions) {
        VariableRatePatternPlayer player;
        CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
        std::vector<std::int16_t> result(110);
        unsigned start=0,part=0;
        while(start<result.size()) {
            auto count=std::min<unsigned>(partitions[part++%partitions.size()],result.size()-start);
            // Explicit half-open boundary partition in every loop.
            const auto next_row=(start*12753ULL/150000)+1;
            const auto boundary=next_row*150000/12753;
            if(boundary>start && start+count>boundary) count=static_cast<unsigned>(boundary-start);
            CHECK_EQ(player.Render(result.data()+start,count),VariableRatePatternPlayerStatus::Success);
            start+=count;
        }
        return result;
    };
    const auto regular=collect({8}),irregular=collect({1,7,3,11});
    const std::int16_t first[]={0,111,222,333,444,555,666,772};
    for(unsigned i=0;i<8;++i) CHECK_EQ(regular[i],first[i]);
    // Independent duration/PCM oracle, no production mapping, cursor or voice.
    const long double semitone=std::exp2(1.0L/12.0L);
    const std::int16_t half[]={10,15,20,25,30,35,40,40};
    unsigned absolute_row=0;
    for(unsigned frame=0;frame<regular.size();++frame) {
        while((absolute_row+1)*150000ULL/12753<=frame) ++absolute_row;
        const unsigned loop=absolute_row/3;
        const auto loop_start=loop*3ULL*150000/12753;
        const auto stop=(loop*3ULL+1)*150000/12753;
        const auto retrigger=(loop*3ULL+2)*150000/12753;
        const auto age=frame-(frame>=retrigger ? retrigger : loop_start);
        const auto source=age*(frame>=retrigger ? 2.0L : semitone);
        const auto a=source<64 ? std::llround(100*std::min(source,63.0L)) : 0;
        const auto b=frame<stop ? -10 : 0;
        const auto half_age=frame-loop_start;
        const auto c=half_age<8 ? half[half_age] : 0;
        CHECK_EQ(regular[frame],a+b+c); CHECK_EQ(irregular[frame],a+b+c);
    }
}
TEST_CASE(VariableRate_UnityEquivalentToNativeRateThroughLifecycleAndReplacements) {
    std::array<std::int16_t,80> pcm{};
    for(unsigned i=0;i<pcm.size();++i) pcm[i]=static_cast<std::int16_t>(i%2 ? -200-i : 100+i);
    NativeRateSampleBindings native_bindings{}; native_bindings.count=1;
    native_bindings.bindings[0]={7,60,{pcm.data(),pcm.size(),100}};
    VariableRateSampleBindings variable_bindings{}; variable_bindings.count=1;
    variable_bindings.bindings[0]={7,60,{pcm.data(),pcm.size(),100}};
    RealtimePattern pattern; pattern.active_rows=3; pattern.active_channels=2;
    pattern.cells[0][0]={60,7}; pattern.cells[0][1]={NOTE_EMPTY,7};
    pattern.cells[1][0]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.note_off_timing[1][0]=NoteOffTiming::EndOfPosition;
    pattern.cells[1][1]={60,kNoInstrumentUpdate}; pattern.cells[2][0]={60,kNoInstrumentUpdate};
    NativeRatePatternPlayer native; VariableRatePatternPlayer variable;
    CHECK_EQ(native.Configure(12753,100,pattern,native_bindings),PatternPlayerStatus::Success);
    CHECK_EQ(variable.Configure(12753,100,pattern,variable_bindings),VariableRatePatternPlayerStatus::Success);
    const auto render=[&](unsigned count) {
        std::int16_t a[128]{},b[128]{};
        CHECK_EQ(native.Render(a,count),PatternPlayerStatus::Success);
        CHECK_EQ(variable.Render(b,count),VariableRatePatternPlayerStatus::Success);
        for(unsigned i=0;i<count;++i) CHECK_EQ(a[i],b[i]);
        CHECK_EQ(native.GetNextSamplePosition(),variable.GetNextSamplePosition());
        const auto np=native.GetPlaybackPosition(); const auto vp=variable.GetPlaybackPosition();
        CHECK_EQ(np.valid,vp.valid); CHECK_EQ(np.absolute_tick,vp.absolute_tick);
        CHECK_EQ(np.pattern_row,vp.pattern_row); CHECK_EQ(np.loop_index,vp.loop_index);
    };
    render(3);
    CHECK_EQ(native.Start(),PatternPlayerStatus::Success); CHECK_EQ(variable.Start(),VariableRatePatternPlayerStatus::Success);
    render(17);
    Pcm16ChannelGains gains; gains.channels[0]=12345; gains.channels[1]=kPcm16UnityGain/2;
    CHECK_EQ(native.SetChannelGains(gains),PatternPlayerStatus::Success);
    CHECK_EQ(variable.SetChannelGains(gains),VariableRatePatternPlayerStatus::Success);
    auto candidate=pattern; candidate.cells[2][1]={NOTE_OFF,kNoInstrumentUpdate};
    CHECK_EQ(native.ReplaceWorkingPattern(candidate),PatternPlayerStatus::Success);
    CHECK_EQ(variable.ReplaceWorkingPattern(candidate),VariableRatePatternPlayerStatus::Success);
    render(30);
    CHECK_EQ(native.Pause(),PatternPlayerStatus::Success); CHECK_EQ(variable.Pause(),VariableRatePatternPlayerStatus::Success);
    render(5);
    CHECK_EQ(native.ReplacePausedPattern(pattern),PatternPlayerStatus::Success);
    CHECK_EQ(variable.ReplacePausedPattern(pattern),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(native.Continue(),PatternPlayerStatus::Success); CHECK_EQ(variable.Continue(),VariableRatePatternPlayerStatus::Success);
    render(29);
    native.Stop(); variable.Stop(); render(8);
    CHECK_EQ(native.ReplacePattern(candidate),PatternPlayerStatus::Success);
    CHECK_EQ(variable.ReplacePattern(candidate),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(native.Start(),PatternPlayerStatus::Success); CHECK_EQ(variable.Start(),VariableRatePatternPlayerStatus::Success);
    render(30); render(30); render(15); // Stay within the 512-tick budget at 100 Hz.
}
TEST_CASE(VariableRate_TransposedPauseReplacementAndPendingPreviewReplay) {
    const std::int16_t a[]={0,100,200,300,400,500,600,700};
    const std::int16_t b[]={10,20,30,40,50,60,70,80};
    VariableRateSampleBindings bindings{}; bindings.count=2;
    bindings.bindings[0]={7,60,{a,8,100}}; bindings.bindings[1]={8,72,{b,8,100}};
    RealtimePattern pattern; pattern.active_rows=2; pattern.active_channels=1;
    pattern.cells[0][0]={NOTE_EMPTY,7}; pattern.cells[1][0]={61,kNoInstrumentUpdate};
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(12753,100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t out[128]{};
    CHECK_EQ(player.Render(out,14),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(out[11],0); CHECK_EQ(out[12],106); CHECK_EQ(out[13],212);
    CHECK_EQ(player.Pause(),VariableRatePatternPlayerStatus::Success);
    const auto paused=player.GetPlaybackPosition(); const auto next=player.GetNextSamplePosition();
    auto replacement=pattern; replacement.cells[0][0]={NOTE_EMPTY,8}; replacement.cells[1][0]={60,kNoInstrumentUpdate};
    CHECK_EQ(player.ReplacePausedPattern(replacement),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetPlaybackPosition().absolute_tick,paused.absolute_tick);
    CHECK_EQ(player.GetNextSamplePosition(),next);
    CHECK_EQ(player.Render(out,3),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<3;++i) CHECK_EQ(out[i],0);
    CHECK_EQ(player.Continue(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.GetNextSamplePosition(),11);
    CHECK_EQ(player.Render(out,4),VariableRatePatternPlayerStatus::Success);
    const std::int16_t half[]={10,15,20,25};
    for(unsigned i=0;i<4;++i) CHECK_EQ(out[i],half[i]);
    auto obsolete=replacement; obsolete.cells[0][0]={72,8};
    CHECK_EQ(player.ReplaceWorkingPattern(obsolete),VariableRatePatternPlayerStatus::Success);
    auto newest=replacement; newest.cells[0][0]={84,8};
    CHECK_EQ(player.ReplaceWorkingPattern(newest),VariableRatePatternPlayerStatus::Success);
    auto bad=newest; bad.cells[0][0].instrument=99; const auto saved=player;
    CHECK_EQ(player.ReplaceWorkingPattern(bad),VariableRatePatternPlayerStatus::UnknownInstrument);
    CheckPlayback(player,saved); CHECK(player.HasPendingWorkingPattern());
    CHECK_EQ(player.Render(out,8),VariableRatePatternPlayerStatus::Success); // End exactly at loop start 23.
    CHECK(player.HasPendingWorkingPattern());
    CHECK_EQ(player.Render(out,4),VariableRatePatternPlayerStatus::Success);
    const std::int16_t doubled[]={10,30,50,70};
    for(unsigned i=0;i<4;++i) CHECK_EQ(out[i],doubled[i]);
    CHECK(!player.HasPendingWorkingPattern()); CHECK_EQ(player.GetPlaybackPosition().loop_index,1);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Render(out,4),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<4;++i) CHECK_EQ(out[i],doubled[i]);
    player.Stop(); CHECK_EQ(player.Render(out,4),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<4;++i) CHECK_EQ(out[i],0);
}

TEST_CASE(VariableRate_FractionalVoiceLateFailureRollbackAndSmallerRetry) {
    VariableRatePatternPlayer player;
    auto pattern=Pattern(); pattern.active_rows=2; pattern.active_channels=1;
    pattern.cells[0][0]={61,7}; pattern.cells[1][0]={60,99};
    CHECK_EQ(player.Configure(12753,100,pattern,Bindings(100)),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    std::int16_t output[14]{};
    CHECK_EQ(player.Render(output,2),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(output[0],10); CHECK_EQ(output[1],21);
    const auto saved=player;
    CHECK(VariableRatePatternPlayerTestAccess::Renderer(player).GetVoice(0)->GetFractionalPhase()!=0);
    std::fill_n(output,14,999);
    CHECK_EQ(player.Render(output,14),VariableRatePatternPlayerStatus::UnknownInstrument);
    CheckPlayback(player,saved);
    for(auto value:output) CHECK_EQ(value,999);
    CHECK_EQ(player.Render(output,9),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(output[0],31); CHECK_EQ(player.GetNextSamplePosition(),11);
    const auto boundary=player; output[0]=999;
    CHECK_EQ(player.Render(output,1),VariableRatePatternPlayerStatus::UnknownInstrument);
    CHECK_EQ(output[0],999); CheckPlayback(player,boundary);
}
