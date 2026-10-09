#include "test_framework.h"
#include "core/audio/ram_voice_block_renderer.h"
#include <array>
#include <vector>

using namespace BroTracker;
namespace
{
    constexpr std::int16_t pcm[] = {10, 20, 30, 40, 50, 60, 70, 80};
    constexpr std::int16_t other[] = {-1, -2, -3};
    const Pcm16MonoView sample{pcm, 8, 44100}, replacement{other, 3, 44100};
    struct Outputs
    {
        std::int16_t data[8][8]{};
        RamVoiceOutputs view;
        Outputs() { for (std::size_t i = 0; i < 8; ++i) view.channels[i] = data[i]; }
    };
    RamVoiceCommand Trigger(std::uint32_t channel, std::uint64_t offset, Pcm16MonoView view = sample)
    { return {channel, offset, RamVoiceAction::Trigger, view}; }
    RamVoiceCommand Stop(std::uint32_t channel, std::uint64_t offset)
    { return {channel, offset, RamVoiceAction::Stop, {}}; }
    void Check(const std::int16_t* data, std::initializer_list<std::int16_t> expected)
    { std::size_t i = 0; for (auto value : expected) CHECK_EQ(data[i++], value); }
}

TEST_CASE(RamBlock_ExactOffsetsSimultaneousAndInputOrder)
{
    RamVoiceBlockRenderer renderer;
    Outputs out;
    CHECK_EQ(renderer.Configure(44100), RamBlockStatus::Success);
    RamVoiceCommandBatch batch;
    batch.count = 6;
    batch.commands[0] = Trigger(0, 0);
    batch.commands[1] = Trigger(1, 2);
    batch.commands[2] = Stop(0, 4);
    batch.commands[3] = Trigger(1, 4, replacement);
    batch.commands[4] = Stop(2, 4);
    batch.commands[5] = Trigger(2, 4);
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::Success);
    Check(out.data[0], {10,20,30,40,0,0,0,0});
    Check(out.data[1], {0,0,10,20,-1,-2,-3,0});
    Check(out.data[2], {0,0,0,0,10,20,30,40});
    for (std::size_t c = 3; c < 8; ++c) Check(out.data[c], {0,0,0,0,0,0,0,0});
    batch.count = 3;
    batch.commands[0] = Trigger(0, 0);
    batch.commands[1] = Stop(0, 0);
    batch.commands[2] = Trigger(2, 0); // Same-sample retrigger.
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,0});
    Check(out.data[2], {10,20,30,40,50,60,70,80});
    CHECK(!renderer.GetVoice(2)->IsActive());
}

TEST_CASE(RamBlock_ContinuationBoundaryResetAndCapacity)
{
    const RamVoiceCommandBatch empty_batch{};
    RamVoiceBlockRenderer renderer;
    CHECK_EQ(renderer.Configure(44100), RamBlockStatus::Success);
    Outputs out;
    RamVoiceCommandBatch batch;
    batch.count = 1; batch.commands[0] = Trigger(0, 6);
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,10,20});
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), RamBlockStatus::Success);
    Check(out.data[0], {30,40,50,60,70,80,0,0});
    batch.commands[0] = Trigger(0, 8);
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::InvalidOffset);
    batch.commands[0] = Trigger(0, 0);
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::Success);
    Check(out.data[0], {10,20,30,40,50,60,70,80});
    batch.count = kRamVoiceCommandCapacity;
    for (std::size_t i = 0; i < batch.count; ++i) batch.commands[i] = Trigger(0, 7);
    CHECK_EQ(renderer.Render(out.view, 8, batch), RamBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,10});
    renderer.Reset();
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), RamBlockStatus::Success);
    Check(out.data[0], {0,0,0,0,0,0,0,0});
    CHECK_EQ(renderer.Configure(48000), RamBlockStatus::Success);
    CHECK_EQ(renderer.GetVoice(0)->GetOutputSampleRate(), 48000);
    CHECK(renderer.GetVoice(8) == nullptr);
}

TEST_CASE(RamBlock_RejectionsPreserveAllVoicesAndOutput)
{
    const RamVoiceCommandBatch empty_batch{};
    RamVoiceBlockRenderer renderer;
    Outputs out;
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), RamBlockStatus::NotConfigured);
    CHECK_EQ(renderer.Configure(44100), RamBlockStatus::Success);
    RamVoiceCommandBatch seed;
    seed.count = 8;
    for (std::size_t c = 0; c < 8; ++c) seed.commands[c] = Trigger(c, 7);
    CHECK_EQ(renderer.Render(out.view, 8, seed), RamBlockStatus::Success);
    CHECK_EQ(renderer.Configure(0), RamBlockStatus::InvalidSampleRate);
    const auto reject = [&](RamVoiceCommandBatch batch, RamBlockStatus expected,
                            std::uint64_t frames = 8, int null_channel = -1) {
        for (auto& channel : out.data) for (auto& value : channel) value = 123;
        auto view = out.view;
        if (null_channel >= 0) view.channels[null_channel] = nullptr;
        CHECK_EQ(renderer.Render(view, frames, batch), expected);
        for (std::size_t c = 0; c < 8; ++c)
        {
            CHECK(renderer.GetVoice(c)->IsActive());
            CHECK_EQ(renderer.GetVoice(c)->GetFramePosition(), 1);
            CHECK_EQ(renderer.GetVoice(c)->GetOutputSampleRate(), 44100);
            for (auto value : out.data[c]) CHECK_EQ(value, 123);
        }
    };
    RamVoiceCommandBatch batch;
    batch.count = 2; batch.commands[0] = Stop(0, 0); batch.commands[1] = Trigger(1, 2);
    batch.commands[1].channel = 8; reject(batch, RamBlockStatus::InvalidChannel);
    batch.commands[1] = Trigger(1, 2); batch.commands[1].action = static_cast<RamVoiceAction>(99);
    reject(batch, RamBlockStatus::InvalidAction);
    batch.commands[1] = Trigger(1, 8); reject(batch, RamBlockStatus::InvalidOffset);
    batch.commands[1].sample_offset = UINT64_MAX; reject(batch, RamBlockStatus::InvalidOffset);
    batch.commands[0] = Stop(0, 3); batch.commands[1] = Trigger(1, 2);
    reject(batch, RamBlockStatus::UnorderedOffsets);
    batch.commands[0] = Stop(0, 0);
    for (const auto invalid : {Pcm16MonoView{nullptr,8,44100}, Pcm16MonoView{pcm,0,44100},
                              Pcm16MonoView{pcm,8,0}})
    { batch.commands[1] = Trigger(1, 2, invalid); reject(batch, RamBlockStatus::InvalidSample); }
    batch.commands[1] = Trigger(1, 2, {pcm,8,48000}); reject(batch, RamBlockStatus::SampleRateMismatch);
    batch.commands[1] = Trigger(1, 2, {pcm, SIZE_MAX,44100}); reject(batch, RamBlockStatus::RangeOverflow);
    batch.commands[1] = Trigger(1, 2);
    reject(batch, RamBlockStatus::InvalidDestination, 8, 7);
    reject(batch, RamBlockStatus::RangeOverflow, UINT64_MAX);
    reject(batch, RamBlockStatus::InvalidOffset, 0);
    batch.count = 17; reject(batch, RamBlockStatus::InvalidCommandCount);
    CHECK_EQ(renderer.Render({}, 0, empty_batch), RamBlockStatus::Success);
    CHECK_EQ(renderer.GetVoice(0)->GetFramePosition(), 1);
    CHECK_EQ(renderer.Render(out.view, 8, empty_batch), RamBlockStatus::Success);
    for (auto& channel : out.data) Check(channel, {20,30,40,50,60,70,80,0});
}

TEST_CASE(RamBlock_PatternCursorCompositionAcrossPartitions)
{
    RealtimePattern prepared;
    prepared.active_rows = 2; prepared.active_channels = 2;
    prepared.cells[0][0] = {60, 1}; prepared.cells[0][1] = {62, 1};
    prepared.cells[1][0] = {NOTE_OFF, 0xFF};
    const RealtimePattern pattern = prepared;
    // Modest fixture sample overlaps the next row/loop; production has no lookup.
    std::vector<std::int16_t> long_pcm(6000, 123);
    const Pcm16MonoView long_sample{long_pcm.data(), long_pcm.size(),44100};
    constexpr std::size_t total = 22000;
    const auto collect = [&](bool irregular) {
        RamVoiceBlockRenderer renderer;
        MusicalTickCursor cursor;
        CHECK_EQ(renderer.Configure(44100), RamBlockStatus::Success);
        CHECK_EQ(cursor.Configure(12753,44100), TickCursorStatus::Success);
        std::array<std::vector<std::int16_t>,8> output;
        for (auto& channel : output) channel.resize(total);
        std::size_t start = 0, part = 0;
        while (start < total)
        {
            const auto requested = irregular ? (part++ * 73) % 251 + 1 : 128;
            const auto count = requested < total - start ? requested : total - start;
            CHECK_EQ(cursor.BeginBlock(start,count), TickCursorStatus::Success);
            RamVoiceCommandBatch commands;
            MusicalTick tick; TickCursorStatus status;
            while ((status = cursor.Pull(tick)) == TickCursorStatus::Tick)
            {
                RowEventBatch rows;
                CHECK_EQ(GenerateRowEvents(tick,pattern,rows), RowEventStatus::Success);
                for (std::size_t i = 0; i < rows.count; ++i)
                {
                    CHECK(commands.count < kRamVoiceCommandCapacity);
                    if (commands.count >= kRamVoiceCommandCapacity) continue;
                    const auto& event = rows.events[i];
                    // Test-only translation of chosen commands into explicit audio actions.
                    commands.commands[commands.count++] = event.note == NOTE_OFF ?
                        Stop(event.channel,event.sample_offset) : Trigger(event.channel,event.sample_offset,long_sample);
                }
            }
            CHECK_EQ(status, TickCursorStatus::BlockComplete);
            RamVoiceOutputs view;
            for (std::size_t c = 0; c < 8; ++c) view.channels[c] = output[c].data() + start;
            CHECK_EQ(renderer.Render(view,count,commands), RamBlockStatus::Success);
            start += count;
        }
        return output;
    };
    const auto regular = collect(false), irregular = collect(true);
    CHECK(regular == irregular);
    std::uint64_t row1 = 0, loop1 = 0;
    CHECK_EQ(TickToSamplePosition(96,12753,44100,row1), TickToSampleStatus::Success);
    CHECK_EQ(TickToSamplePosition(192,12753,44100,loop1), TickToSampleStatus::Success);
    CHECK_EQ(regular[0][0],123);
    CHECK_EQ(regular[0][row1 - 1],123);
    CHECK_EQ(regular[0][row1],0);
    CHECK_EQ(regular[0][loop1],123);
    CHECK_EQ(regular[1][5999],123);
    CHECK_EQ(regular[1][6000],0);
    for (std::size_t c = 2; c < 8; ++c) for (auto value : regular[c]) CHECK_EQ(value,0);
}
