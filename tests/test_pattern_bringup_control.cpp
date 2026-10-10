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
