#include "test_framework.h"
#include "bringup/pattern_control.h"
#include <algorithm>

using namespace BroTracker;
namespace
{
    const std::int16_t pcm[] = {10,20,30,40,50,60,70,80};
    RealtimePattern Pattern()
    {
        RealtimePattern pattern; pattern.active_channels=2;
        pattern.cells[0][0]={60,7}; pattern.cells[0][1]={60,7};
        return pattern;
    }
    NativeRateSampleBindings Bindings(std::uint32_t rate=44100)
    {
        NativeRateSampleBindings bindings{}; bindings.count=1;
        bindings.bindings[0]={7,60,{pcm,8,rate}};
        return bindings;
    }
    void Applied(PatternBringUpControl& control,PatternRequest request,
        PatternPlayerStatus result=PatternPlayerStatus::Success)
    {
        PatternAppliedRequest applied;
        CHECK(control.TakeApplied(applied));
        CHECK_EQ(applied.request,request); CHECK_EQ(applied.result,result);
    }
}
TEST_CASE(PatternBringUpPauseContinueAppliedSnapshotsAndFaultStop) {
    PatternBringUpControl control; auto pattern = Pattern(); pattern.cells[1][0] = {60, kNoInstrumentUpdate};
    CHECK_EQ(control.InitializeAudio(12753, 44100, pattern, Bindings()), PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start), PatternRequestStatus::Accepted);
    std::int16_t out[128]{}; control.AudioBlock(out, 1, true); Applied(control, PatternRequest::Start);
    const auto before = control.Snapshot();
    CHECK_EQ(control.Submit(PatternRequest::Pause), PatternRequestStatus::Accepted);
    CHECK_EQ(control.Snapshot().transport, PatternTransportState::Playing);
    control.AudioBlock(out, 128, true); Applied(control, PatternRequest::Pause);
    CHECK_EQ(control.Snapshot().transport, PatternTransportState::Paused);
    CHECK_EQ(control.Snapshot().position.absolute_tick, before.position.absolute_tick);
    for (auto value : out) CHECK_EQ(value, 0);
    CHECK_EQ(control.Submit(PatternRequest::Continue), PatternRequestStatus::Accepted);
    control.AudioBlock(out, 1, true); Applied(control, PatternRequest::Continue);
    CHECK_EQ(control.Snapshot().transport, PatternTransportState::Playing);
    CHECK_EQ(control.Snapshot().position.pattern_row, 0); CHECK_EQ(out[0], 20);
    CHECK_EQ(control.Submit(PatternRequest::Pause), PatternRequestStatus::Accepted);
    control.AudioBlock(nullptr, 128, false); Applied(control, PatternRequest::Pause);
    CHECK_EQ(control.Snapshot().transport, PatternTransportState::Stopped);
    CHECK(!control.Snapshot().position.valid);
    CHECK_EQ(control.Submit(PatternRequest::Continue), PatternRequestStatus::Accepted);
    control.AudioBlock(out, 1, true);
    Applied(control, PatternRequest::Continue, PatternPlayerStatus::InvalidTransportState);
}

TEST_CASE(PatternBringUp_FifoAppliedStatusAndBackpressure)
{
    PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,44100,Pattern(),Bindings()),PatternPlayerStatus::Success);
    CHECK(!control.Snapshot().running); CHECK_EQ(control.Snapshot().next_sample,0);
    CHECK_EQ(control.Submit(static_cast<PatternRequest>(99)),PatternRequestStatus::InvalidRequest);
    for (std::size_t i=0;i<kPatternRequestCapacity;++i)
        CHECK_EQ(control.Submit(i%2 ? PatternRequest::Stop : PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Full);
    PatternAppliedRequest untouched{PatternRequest::Start,PatternPlayerStatus::InvalidTempo};
    CHECK(!control.TakeApplied(untouched));
    CHECK_EQ(untouched.result,PatternPlayerStatus::InvalidTempo);
    CHECK(!control.Snapshot().running); // Queued intent is not applied status/ack.
    std::int16_t output[4]={999,999,999,999};
    control.AudioBlock(output,4,true);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK(!control.Snapshot().running);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    control.AudioBlock(output,4,true); // Full ack FIFO: START stays pending.
    CHECK(!control.Snapshot().running);
    for (std::size_t i=0;i<kPatternRequestCapacity;++i)
        Applied(control,i%2 ? PatternRequest::Stop : PatternRequest::Start);
    control.AudioBlock(output,4,true);
    Applied(control,PatternRequest::Start);
    CHECK(control.Snapshot().running); CHECK_EQ(control.Snapshot().next_sample,4);
    const auto playing = control.Snapshot();
    CHECK(playing.position.valid); CHECK_EQ(playing.position.absolute_tick, 0);
    CHECK_EQ(playing.position.pattern_row, 0); CHECK_EQ(playing.position.loop_index, 0);
    CHECK_EQ(playing.active_rows, 16); CHECK_EQ(playing.active_channels, 2);
    for (std::size_t i=0;i<4;++i) CHECK_EQ(output[i],2*pcm[i]);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    CHECK_EQ(control.Snapshot().next_sample,4);
    control.AudioBlock(output,4,true); Applied(control,PatternRequest::Start);
    CHECK_EQ(output[0],20); CHECK_EQ(control.Snapshot().next_sample,4);
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    CHECK(control.Snapshot().running);
    control.AudioBlock(output,4,true); Applied(control,PatternRequest::Stop);
    CHECK(!control.Snapshot().running); CHECK_EQ(control.Snapshot().next_sample,0);
    CHECK(!control.Snapshot().position.valid);
    // STATUS/HELLO use snapshots alone: repeated polling and audio never start.
    for (std::size_t i=0;i<4;++i) { (void)control.Snapshot(); control.AudioBlock(output,4,true); }
    for (auto value:output) CHECK_EQ(value,0);
    CHECK(!control.Snapshot().running);
}

TEST_CASE(PatternBringUp_RenderFailureZeroesStopsAndLatchesUntilExplicitStart)
{
    PatternBringUpControl control;
    auto pattern=Pattern(); pattern.active_rows=2; pattern.active_channels=1;
    pattern.cells[1][0]={61,kNoInstrumentUpdate};
    CHECK_EQ(control.InitializeAudio(12000,100,pattern,Bindings(100)),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t output[16]; std::fill_n(output,16,999);
    control.AudioBlock(output,16,true); Applied(control,PatternRequest::Start);
    for (auto value:output) CHECK_EQ(value,0); // No prefix/stale PCM survives late rejection.
    const auto failed=control.Snapshot();
    CHECK(!failed.running); CHECK_EQ(failed.next_sample,0);
    CHECK(!failed.position.valid);
    CHECK_EQ(failed.fault,PatternFault::Render);
    CHECK_EQ(failed.player_error,PatternPlayerStatus::UnsupportedPitch);
    CHECK_EQ(failed.failures,1);
    control.AudioBlock(output,4,true);
    CHECK_EQ(control.Snapshot().failures,1); // No automatic retry of frozen position.
    CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    control.AudioBlock(output,4,true); Applied(control,PatternRequest::Stop);
    CHECK_EQ(control.Snapshot().fault,PatternFault::Render);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    control.AudioBlock(output,4,true); Applied(control,PatternRequest::Start);
    CHECK_EQ(control.Snapshot().fault,PatternFault::None); CHECK_EQ(output[0],10);
    CHECK_EQ(control.Snapshot().last_fault,PatternFault::Render); // Diagnostic evidence retained.
}

TEST_CASE(PatternBringUp_AllocationFailureAndConfigurationFailure)
{
    PatternBringUpControl control;
    CHECK_EQ(control.InitializeAudio(12753,44100,Pattern(),Bindings()),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    std::int16_t output[4]; control.AudioBlock(output,4,true); Applied(control,PatternRequest::Start);
    CHECK_EQ(control.Snapshot().next_sample,4);
    CHECK(control.Snapshot().position.valid);
    control.AudioBlock(nullptr,128,false);
    CHECK(!control.Snapshot().running); CHECK_EQ(control.Snapshot().next_sample,0);
    CHECK(!control.Snapshot().position.valid);
    CHECK_EQ(control.Snapshot().allocation_failures,1);
    CHECK_EQ(control.Snapshot().fault,PatternFault::Allocation);
    std::fill_n(output,4,999); control.AudioBlock(output,4,true);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    control.AudioBlock(output,4,true); Applied(control,PatternRequest::Start);
    CHECK_EQ(output[0],20); CHECK_EQ(control.Snapshot().allocation_failures,1);
    PatternBringUpControl bad;
    CHECK_EQ(bad.InitializeAudio(0,44100,Pattern(),Bindings()),PatternPlayerStatus::InvalidTempo);
    CHECK_EQ(bad.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
    bad.AudioBlock(output,4,true); Applied(bad,PatternRequest::Start,PatternPlayerStatus::NotConfigured);
    for (auto value:output) CHECK_EQ(value,0);
    CHECK_EQ(bad.Snapshot().fault,PatternFault::Configuration);
}

TEST_CASE(PatternBringUp_CoherentConsumedPositionBeforeFirstTickAndAcrossLoops)
{
    PatternBringUpControl control;
    RealtimePattern empty; empty.active_rows = 2; empty.active_channels = 1;
    NativeRateSampleBindings bindings{};
    CHECK_EQ(control.InitializeAudio(12000, 100, empty, bindings), PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start), PatternRequestStatus::Accepted);
    control.AudioBlock(nullptr, 0, true); Applied(control, PatternRequest::Start);
    auto status = control.Snapshot();
    CHECK(status.running); CHECK(!status.position.valid); CHECK_EQ(status.next_sample, 0);
    std::int16_t output[4]{};
    for (std::size_t i = 0; i < 7; ++i) control.AudioBlock(output, 4, true);
    status = control.Snapshot();
    CHECK(status.running); CHECK(status.position.valid); CHECK_EQ(status.next_sample, 28);
    CHECK_EQ(status.active_rows, 2); CHECK_EQ(status.active_channels, 1);
    CHECK_EQ(status.position.absolute_tick, (28ULL * 12000 * 384 - 1) / (100 * 6000));
    CHECK_EQ(status.position.pattern_row, 0); CHECK_EQ(status.position.loop_index, 1);
    CHECK_EQ(control.Submit(PatternRequest::Start), PatternRequestStatus::Accepted);
    control.AudioBlock(nullptr, 0, true); Applied(control, PatternRequest::Start);
    status = control.Snapshot();
    CHECK(status.running); CHECK(!status.position.valid); CHECK_EQ(status.next_sample, 0);
    CHECK_EQ(control.Submit(PatternRequest::Stop), PatternRequestStatus::Accepted);
    control.AudioBlock(nullptr, 0, true); Applied(control, PatternRequest::Stop);
    status = control.Snapshot();
    CHECK(!status.running); CHECK(!status.position.valid); CHECK_EQ(status.next_sample, 0);
}

TEST_CASE(PauseNearBoundaryAppliesBeforeRenderingAnotherBlock) {
    for (const unsigned count : {11u,12u}) {
        PatternBringUpControl control; auto pattern=Pattern(); pattern.active_channels=1;
        pattern.cells[1][0]={60,kNoInstrumentUpdate};
        CHECK_EQ(control.InitializeAudio(12753,100,pattern,Bindings(100)),PatternPlayerStatus::Success);
        CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
        std::int16_t out[128]{}; control.AudioBlock(out,count,true); Applied(control,PatternRequest::Start);
        const auto before=control.Snapshot();
        CHECK_EQ(control.Submit(PatternRequest::Pause),PatternRequestStatus::Accepted);
        control.AudioBlock(out,32,true); Applied(control,PatternRequest::Pause);
        const auto paused=control.Snapshot(); CHECK_EQ(paused.next_sample,before.next_sample);
        CHECK_EQ(paused.position.absolute_tick,before.position.absolute_tick);
        CHECK_EQ(paused.position.pattern_row,count==11 ? 0 : 1);
        for(unsigned i=0;i<32;++i) CHECK_EQ(out[i],0);
        CHECK_EQ(control.Submit(PatternRequest::Continue),PatternRequestStatus::Accepted);
        control.AudioBlock(out,1,true); Applied(control,PatternRequest::Continue);
        CHECK_EQ(control.Snapshot().position.pattern_row,paused.position.pattern_row); CHECK_EQ(out[0],10);
    }
}

TEST_CASE(PatternPerformanceWrapBudgetSaturationAndFormatting) {
    std::uint32_t budget=99;
    CHECK(PatternCycleBudget(600000000,128,44100,budget)); CHECK_EQ(budget,1741496);
    CHECK(PatternCycleBudget(150000000,128,48000,budget)); CHECK_EQ(budget,400000);
    CHECK(!PatternCycleBudget(0,128,44100,budget)); CHECK_EQ(budget,400000);
    CHECK(!PatternCycleBudget(UINT32_MAX,UINT32_MAX,1,budget));
    PatternPerformance s; RecordPatternCycles(s,UINT32_MAX-9,10,true,20);
    CHECK_EQ(s.initialization_cycles,20); CHECK_EQ(s.maximum_cycles,0);
    RecordPatternCycles(s,UINT32_MAX-9,10,false,20); CHECK_EQ(s.over_budget,0);
    RecordPatternCycles(s,0,21,false,20); CHECK_EQ(s.over_budget,1); CHECK_EQ(s.maximum_cycles,21);
    CHECK_EQ(s.callbacks,3); CHECK_EQ(s.steady_callbacks,2);
    s={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    RecordPatternCycles(s,0,1,false,0); CHECK_EQ(s.callbacks,UINT32_MAX); CHECK_EQ(s.over_budget,UINT32_MAX);
    char line[kPatternPerformanceLineCapacity]; std::size_t length=0;
    CHECK(FormatPatternPerformance(s,line,sizeof(line),length)); CHECK(length<sizeof(line)); CHECK_EQ(line[length-1],'\n');
    CHECK(!FormatPatternPerformance(s,line,4,length));
    PatternBringUpControl control; control.RecordPerformance(0,5,false,4);
    CHECK_EQ(control.Snapshot().performance.over_budget,1);
}

TEST_CASE(PatternPerformanceStrictParsingAndInitializationSeparation) {
    PatternPerformance value{9,8,7,6,5,4};
    for(const char* bad : {"BTPATTERN1 PERF 2 100 99 2249 55901 0 1741496",
        "BTPATTERN1 PERF 1 100 101 2249 55901 0 1741496", "BTPATTERN1 PERF 1 100 99 2249 55901 100 1741496",
        "BTPATTERN1 PERF 1 100 99 2249 55901 0 0", "BTPATTERN1 PERF 1 4294967296 99 2249 55901 0 1741496",
        "BTPATTERN1 PERF 1 100 99 2249 55901 0 1741496 extra"}) {
        CHECK(!ParsePatternPerformance(bad,std::strlen(bad),value)); CHECK_EQ(value.maximum_cycles,6);
    }
    const char* good="BTPATTERN1 PERF 1 1 0 2249 0 0 1741496";
    CHECK(ParsePatternPerformance(good,std::strlen(good),value));
    CHECK_EQ(value.steady_callbacks,0); CHECK_EQ(value.initialization_cycles,2249); CHECK_EQ(value.maximum_cycles,0);
}
