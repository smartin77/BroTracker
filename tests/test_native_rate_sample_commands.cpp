#include "test_framework.h"
#include "core/playback/native_rate_sample_commands.h"
#include "core/audio/pcm16_mixer.h"
#include <algorithm>
#include <vector>

using namespace BroTracker;
namespace
{
    const std::int16_t pcm_a[] = {100,200,300,400};
    const std::int16_t pcm_b[] = {-10,-20,-30};
    NativeRateSampleBindings Bindings()
    {
        NativeRateSampleBindings table{};
        table.count = 2;
        table.bindings[0] = {0,60,{pcm_a,4,44100}};
        table.bindings[1] = {254,62,{pcm_b,3,44100}};
        return table;
    }
    RowEvent EventAt(std::uint8_t channel, Note note, std::uint8_t instrument,
        std::uint64_t offset = 0)
    {
        return {channel,0,9,123,456,offset,note,instrument};
    }
    void CheckState(const NativeRateSampleCommandPreparer& preparer, std::uint32_t channel,
        Note note, std::uint8_t instrument)
    {
        LogicalChannelState state;
        CHECK_EQ(preparer.GetChannel(channel,state),ChannelStateStatus::Success);
        CHECK_EQ(state.note,note); CHECK_EQ(state.instrument,instrument);
    }
    void CheckBatch(const RamVoiceCommandBatch& actual, const RamVoiceCommandBatch& expected)
    {
        CHECK_EQ(actual.count,expected.count);
        // Check inactive storage too: the entire output must be preserved on failure.
        for (std::size_t i = 0; i < kRamVoiceCommandCapacity; ++i)
        {
            const auto& a=actual.commands[i]; const auto& b=expected.commands[i];
            CHECK_EQ(a.channel,b.channel); CHECK_EQ(a.sample_offset,b.sample_offset);
            CHECK_EQ(a.action,b.action); CHECK_EQ(a.sample.data,b.sample.data);
            CHECK_EQ(a.sample.frame_count,b.sample.frame_count);
            CHECK_EQ(a.sample.sample_rate_hz,b.sample.sample_rate_hz);
        }
    }
    RamVoiceCommandBatch PrepareOne(NativeRateSampleCommandPreparer& preparer, const RowEvent& event)
    {
        RowEventBatch batch{}; batch.count=1; batch.events[0]=event;
        RamVoiceCommandBatch output{};
        CHECK_EQ(preparer.Prepare(batch,output),SampleCommandStatus::Success);
        return output;
    }
}

TEST_CASE(SampleCommands_ContinuationRetriggerOrderingAndStop)
{
    NativeRateSampleCommandPreparer preparer;
    CHECK_EQ(preparer.Configure(44100,Bindings()),SampleCommandStatus::Success);
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    auto output=PrepareOne(preparer,EventAt(0,60,0,UINT64_MAX));
    CHECK_EQ(output.count,1); CHECK_EQ(output.commands[0].sample.data,pcm_a);
    CHECK_EQ(output.commands[0].sample_offset,UINT64_MAX); // No block-bound clamping here.
    output=PrepareOne(preparer,EventAt(1,NOTE_EMPTY,254));
    CHECK_EQ(output.count,0); CheckState(preparer,1,NOTE_EMPTY,254);
    output=PrepareOne(preparer,EventAt(1,62,kNoInstrumentUpdate,17));
    CHECK_EQ(output.commands[0].channel,1); CHECK_EQ(output.commands[0].sample.data,pcm_b);
    output=PrepareOne(preparer,EventAt(0,60,kNoInstrumentUpdate));
    CHECK_EQ(output.count,1); CHECK_EQ(output.commands[0].action,RamVoiceAction::Trigger);
    output=PrepareOne(preparer,EventAt(0,NOTE_EMPTY,254));
    CHECK_EQ(output.count,0); CheckState(preparer,0,60,254);
    output=PrepareOne(preparer,EventAt(0,62,kNoInstrumentUpdate));
    CHECK_EQ(output.commands[0].sample.data,pcm_b);
    output=PrepareOne(preparer,EventAt(0,NOTE_EMPTY,kNoInstrumentUpdate));
    CHECK_EQ(output.count,0); CheckState(preparer,0,62,254);
    output=PrepareOne(preparer,EventAt(0,NOTE_OFF,0));
    CHECK_EQ(output.commands[0].action,RamVoiceAction::Stop); CheckState(preparer,0,NOTE_OFF,0);
    output=PrepareOne(preparer,EventAt(0,NOTE_EMPTY,254));
    CHECK_EQ(output.count,0); CheckState(preparer,0,NOTE_OFF,254);
    output=PrepareOne(preparer,EventAt(2,NOTE_OFF,kNoInstrumentUpdate));
    CHECK_EQ(output.count,1); CheckState(preparer,2,NOTE_OFF,kNoInstrumentUpdate);
    output=PrepareOne(preparer,EventAt(3,NOTE_OFF,99)); // No lookup needed even for unknown ID.
    CHECK_EQ(output.count,1); CheckState(preparer,3,NOTE_OFF,99);
    RowEventBatch ordered{}; ordered.count=3;
    ordered.events[0]=EventAt(0,60,0,5);
    ordered.events[1]=EventAt(0,NOTE_OFF,kNoInstrumentUpdate,5);
    ordered.events[2]=EventAt(1,62,kNoInstrumentUpdate,5);
    CHECK_EQ(preparer.Prepare(ordered,output),SampleCommandStatus::Success);
    CHECK_EQ(output.count,3);
    CHECK_EQ(output.commands[0].action,RamVoiceAction::Trigger);
    CHECK_EQ(output.commands[1].action,RamVoiceAction::Stop);
    CHECK_EQ(output.commands[2].channel,1);
    const RowEventBatch empty_batch{};
    CHECK_EQ(preparer.Prepare(empty_batch,output),SampleCommandStatus::Success);
    CHECK_EQ(output.count,0); CheckState(preparer,0,NOTE_OFF,0); CheckState(preparer,1,62,254);
    RowEventBatch loop_start{}; loop_start.count=1;
    loop_start.events[0]=EventAt(1,62,kNoInstrumentUpdate);
    loop_start.events[0].loop_index=10;
    CHECK_EQ(preparer.Prepare(loop_start,output),SampleCommandStatus::Success);
    CHECK_EQ(output.commands[0].sample.data,pcm_b); // Loop change never clears selection.
    LogicalChannelState snapshot{5,6};
    CHECK_EQ(preparer.GetChannel(UINT32_MAX,snapshot),ChannelStateStatus::InvalidChannel);
    CHECK_EQ(snapshot.note,5); CHECK_EQ(snapshot.instrument,6);
    preparer.Reset();
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    output=PrepareOne(preparer,EventAt(0,60,0)); CHECK_EQ(output.count,1);
}

TEST_CASE(SampleCommands_TransactionalPreparationFailures)
{
    NativeRateSampleCommandPreparer preparer;
    RowEventBatch input{};
    RamVoiceCommandBatch output{}; output.count=16;
    for (auto& command:output.commands) command={7,123,RamVoiceAction::Trigger,{pcm_b,3,44100}};
    const auto sentinel=output;
    CHECK_EQ(preparer.Prepare(input,output),SampleCommandStatus::NotConfigured);
    CheckBatch(output,sentinel);
    CHECK_EQ(preparer.Configure(44100,Bindings()),SampleCommandStatus::Success);
    for (std::uint8_t c=0;c<8;++c) (void)PrepareOne(preparer,EventAt(c,60,0));
    const auto unchanged=[&]() {
        CheckBatch(output,sentinel);
        for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,60,0);
    };
    const auto reject=[&](RowEvent bad, SampleCommandStatus expected) {
        input.count=2; input.events[0]=EventAt(0,62,254,3); input.events[1]=bad;
        CHECK_EQ(preparer.Prepare(input,output),expected); unchanged();
    };
    reject(EventAt(1,60,99,4),SampleCommandStatus::UnknownInstrument);
    reject(EventAt(1,61,0,4),SampleCommandStatus::UnsupportedPitch);
    reject(EventAt(8,60,0,4),SampleCommandStatus::InvalidChannel);
    reject(EventAt(255,60,0,4),SampleCommandStatus::InvalidChannel);
    for (unsigned int note=128;note<NOTE_OFF;++note)
        reject(EventAt(1,static_cast<Note>(note),0,4),SampleCommandStatus::InvalidNote);
    reject(EventAt(1,60,0,2),SampleCommandStatus::UnorderedOffsets);
    input.count=9;
    CHECK_EQ(preparer.Prepare(input,output),SampleCommandStatus::InvalidEventCount); unchanged();
    input.count=255;
    CHECK_EQ(preparer.Prepare(input,output),SampleCommandStatus::InvalidEventCount); unchanged();
    preparer.Reset(); input.count=1; input.events[0]=EventAt(0,60,kNoInstrumentUpdate);
    CHECK_EQ(preparer.Prepare(input,output),SampleCommandStatus::MissingInstrument);
    CheckBatch(output,sentinel); CheckState(preparer,0,NOTE_EMPTY,kNoInstrumentUpdate);
    input.count=8;
    for (std::uint8_t c=0;c<8;++c) input.events[c]=EventAt(c,60,0);
    CHECK_EQ(preparer.Prepare(input,output),SampleCommandStatus::Success);
    CHECK_EQ(output.count,8); // Every event retained, never truncated.
}

TEST_CASE(SampleCommands_ConfigurationValidationAndReconfiguration)
{
    NativeRateSampleCommandPreparer preparer;
    const auto original=Bindings();
    CHECK_EQ(preparer.Configure(0,original),SampleCommandStatus::InvalidSampleRate);
    const RowEventBatch empty_batch{};
    RamVoiceCommandBatch unconfigured_output{};
    CHECK_EQ(preparer.Prepare(empty_batch,unconfigured_output),SampleCommandStatus::NotConfigured);
    CHECK_EQ(preparer.Configure(44100,original),SampleCommandStatus::Success);
    (void)PrepareOne(preparer,EventAt(0,60,0));
    const auto reject=[&](const NativeRateSampleBindings& bad, SampleCommandStatus expected,
        std::uint32_t rate=44100) {
        CHECK_EQ(preparer.Configure(rate,bad),expected);
        CheckState(preparer,0,60,0);
        const auto output=PrepareOne(preparer,EventAt(0,60,kNoInstrumentUpdate));
        CHECK_EQ(output.commands[0].sample.data,pcm_a);
        CHECK_EQ(output.commands[0].sample.sample_rate_hz,44100);
    };
    reject(original,SampleCommandStatus::InvalidSampleRate,0);
    auto bad=original; bad.count=17; reject(bad,SampleCommandStatus::InvalidBindingCount);
    bad.count=SIZE_MAX; reject(bad,SampleCommandStatus::InvalidBindingCount);
    bad=original; bad.bindings[1].instrument=0; reject(bad,SampleCommandStatus::DuplicateInstrument);
    bad=original; bad.bindings[1].instrument=255; reject(bad,SampleCommandStatus::ReservedInstrument);
    for (const auto note:{128,254,255}) {
        bad=original; bad.bindings[1].native_rate_note=static_cast<Note>(note);
        reject(bad,SampleCommandStatus::InvalidNativeRateNote);
    }
    bad=original; bad.bindings[1].sample.data=nullptr; reject(bad,SampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.frame_count=0; reject(bad,SampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.sample_rate_hz=0; reject(bad,SampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.sample_rate_hz=48000; reject(bad,SampleCommandStatus::SampleRateMismatch);
    bad=original; bad.bindings[1].sample.frame_count=RamSampleVoice::kMaximumFrameCount+1;
    reject(bad,SampleCommandStatus::RangeOverflow);
    NativeRateSampleBindings full{}; full.count=kNativeRateSampleBindingCapacity;
    for (std::size_t i=0;i<full.count;++i)
        full.bindings[i]={static_cast<std::uint8_t>(i),static_cast<Note>(i),{pcm_a,4,48000}};
    CHECK_EQ(preparer.Configure(48000,full),SampleCommandStatus::Success);
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    auto output=PrepareOne(preparer,EventAt(7,15,15));
    CHECK_EQ(output.commands[0].sample.sample_rate_hz,48000);
    output=PrepareOne(preparer,EventAt(0,0,0)); CHECK_EQ(output.count,1);
    full.bindings[15].native_rate_note=127;
    CHECK_EQ(preparer.Configure(48000,full),SampleCommandStatus::Success);
    output=PrepareOne(preparer,EventAt(7,127,15)); CHECK_EQ(output.count,1);
    NativeRateSampleBindings empty_bindings{};
    CHECK_EQ(preparer.Configure(44100,empty_bindings),SampleCommandStatus::Success);
    output=PrepareOne(preparer,EventAt(0,NOTE_OFF,kNoInstrumentUpdate)); CHECK_EQ(output.count,1);
    // Metadata is stored by value, not retained through the caller's table.
    auto copied=Bindings();
    CHECK_EQ(preparer.Configure(44100,copied),SampleCommandStatus::Success);
    copied.bindings[0]={254,62,{pcm_b,3,44100}};
    output=PrepareOne(preparer,EventAt(0,60,0)); CHECK_EQ(output.commands[0].sample.data,pcm_a);
}

TEST_CASE(SampleCommands_EndToEndLoopingMixedPcmAcrossPartitions)
{
    // Deliberately small native-rate fixture: row starts at floor(n*600000/51012).
    // PCM lasts longer than a row, so retriggers and stops affect actual output.
    const std::int16_t a[]={100,200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1500,1600};
    const std::int16_t b[]={-10,-20,-30,-40,-50,-60,-70,-80,-90,-100,-110,-120,-130,-140,-150,-160};
    NativeRateSampleBindings bindings{}; bindings.count=2;
    bindings.bindings[0]={0,60,{a,16,100}}; bindings.bindings[1]={254,62,{b,16,100}};
    RealtimePattern pattern;
    pattern.active_rows=4; pattern.active_channels=2;
    pattern.cells[0][0]={60,0}; pattern.cells[0][1]={62,254};
    pattern.cells[1][0]={60,kNoInstrumentUpdate}; pattern.cells[1][1]={NOTE_OFF,kNoInstrumentUpdate};
    pattern.cells[2][0]={NOTE_EMPTY,254}; pattern.cells[2][1]={62,kNoInstrumentUpdate};
    pattern.cells[3][0]={NOTE_OFF,kNoInstrumentUpdate}; pattern.cells[3][1]={NOTE_OFF,kNoInstrumentUpdate};
    const auto collect=[&](bool irregular) {
        MusicalTickCursor cursor;
        NativeRateSampleCommandPreparer preparer;
        RamVoiceBlockRenderer renderer;
        CHECK_EQ(cursor.Configure(12753,100),TickCursorStatus::Success);
        CHECK_EQ(preparer.Configure(100,bindings),SampleCommandStatus::Success);
        CHECK_EQ(renderer.Configure(100),RamBlockStatus::Success);
        std::vector<std::int16_t> mixed(96);
        std::size_t start=0,part=0,row_events=0;
        while (start<mixed.size())
        {
            const std::size_t requested=irregular ? (part++*7)%13+1 : 8;
            const auto count=std::min(requested,mixed.size()-start);
            CHECK_EQ(cursor.BeginBlock(start,count),TickCursorStatus::Success);
            RamVoiceCommandBatch commands{};
            MusicalTick tick; TickCursorStatus status;
            while ((status=cursor.Pull(tick))==TickCursorStatus::Tick)
            {
                RowEventBatch rows{};
                CHECK_EQ(GenerateRowEvents(tick,pattern,rows),RowEventStatus::Success);
                RamVoiceCommandBatch prepared{};
                CHECK_EQ(preparer.Prepare(rows,prepared),SampleCommandStatus::Success);
                row_events+=rows.count;
                // Fail the fixture explicitly rather than truncating or overrunning.
                if (commands.count+prepared.count>kRamVoiceCommandCapacity) {
                    CHECK(false); return std::vector<std::int16_t>{};
                }
                for (std::size_t i=0;i<prepared.count;++i) {
                    CHECK_EQ(prepared.commands[i].sample_offset,tick.sample_position-start);
                    commands.commands[commands.count++]=prepared.commands[i];
                }
                if (rows.count && tick.tick_index==4*kTicksPerRow) {
                    CHECK_EQ(tick.sample_position,47);
                    CheckState(preparer,0,60,0); CheckState(preparer,1,62,254);
                }
            }
            CHECK_EQ(status,TickCursorStatus::BlockComplete);
            std::int16_t channel_pcm[8][13]{};
            RamVoiceOutputs outputs; Pcm16MixInputs inputs;
            for (std::size_t c=0;c<8;++c) { outputs.channels[c]=channel_pcm[c]; inputs.channels[c]=channel_pcm[c]; }
            CHECK_EQ(renderer.Render(outputs,count,commands),RamBlockStatus::Success);
            CHECK_EQ(MixPcm16Mono(inputs,mixed.data()+start,count),Pcm16MixStatus::Success);
            start+=count;
        }
        CHECK_EQ(row_events,18); // Nine rows, two raw commands each, including instrument-only.
        return mixed;
    };
    // Independent expected triggers/stops: no scheduler, preparer or voice used.
    const std::size_t row_starts[]={0,11,23,35,47,58,70,82,94};
    std::vector<std::int16_t> expected(96);
    std::size_t next_row=0,position_a=16,position_b=16;
    for (std::size_t frame=0;frame<expected.size();++frame)
    {
        if (next_row<9 && frame==row_starts[next_row]) {
            switch (next_row++%4) {
            case 0: position_a=0; position_b=0; break;
            case 1: position_a=0; position_b=16; break;
            case 2: position_b=0; break;
            case 3: position_a=16; position_b=16; break;
            }
        }
        const auto va=position_a<16 ? a[position_a++] : 0;
        const auto vb=position_b<16 ? b[position_b++] : 0;
        expected[frame]=static_cast<std::int16_t>(va+vb);
    }
    CHECK(collect(false)==expected); CHECK(collect(true)==expected);
}
