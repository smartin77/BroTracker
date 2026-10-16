#include "test_framework.h"
#include "core/playback/variable_rate_sample_commands.h"
#include "core/audio/pcm16_mixer.h"
#include <algorithm>
#include <vector>

using namespace BroTracker;
namespace
{
    const std::int16_t pcm_a[] = {100,200,300,400};
    const std::int16_t pcm_b[] = {-10,-20,-30};
    VariableRateSampleBindings Bindings()
    {
        VariableRateSampleBindings table{};
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
    void CheckState(const VariableRateSampleCommandPreparer& preparer, std::uint32_t channel,
        Note note, std::uint8_t instrument)
    {
        LogicalChannelState state;
        CHECK_EQ(preparer.GetChannel(channel,state),ChannelStateStatus::Success);
        CHECK_EQ(state.note,note); CHECK_EQ(state.instrument,instrument);
    }
    void CheckBatch(const VariableRateVoiceCommandBatch& actual, const VariableRateVoiceCommandBatch& expected)
    {
        CHECK_EQ(actual.count,expected.count);
        // Check inactive storage too: the entire output must be preserved on failure.
        for (std::size_t i = 0; i < kVariableRateVoiceCommandCapacity; ++i)
        {
            const auto& a=actual.commands[i]; const auto& b=expected.commands[i];
            CHECK_EQ(a.channel,b.channel); CHECK_EQ(a.sample_offset,b.sample_offset);
            CHECK_EQ(a.action,b.action); CHECK_EQ(a.sample.data,b.sample.data);
            CHECK_EQ(a.sample.frame_count,b.sample.frame_count);
            CHECK_EQ(a.sample.sample_rate_hz,b.sample.sample_rate_hz);
            CHECK_EQ(a.step_q32_32,b.step_q32_32);
        }
    }
    VariableRateVoiceCommandBatch PrepareOne(VariableRateSampleCommandPreparer& preparer, const RowEvent& event)
    {
        RowEventBatch batch{}; batch.count=1; batch.events[0]=event;
        VariableRateVoiceCommandBatch output{};
        CHECK_EQ(preparer.Prepare(batch,output),VariableSampleCommandStatus::Success);
        return output;
    }
}

TEST_CASE(VariableSampleCommands_ContinuationRetriggerOrderingAndStop)
{
    VariableRateSampleCommandPreparer preparer;
    CHECK_EQ(preparer.Configure(44100,Bindings()),VariableSampleCommandStatus::Success);
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    auto output=PrepareOne(preparer,EventAt(0,60,0,UINT64_MAX));
    CHECK_EQ(output.count,1); CHECK_EQ(output.commands[0].sample.data,pcm_a);
    CHECK_EQ(output.commands[0].sample_offset,UINT64_MAX); // No block-bound clamping here.
    output=PrepareOne(preparer,EventAt(1,NOTE_EMPTY,254));
    CHECK_EQ(output.count,0); CheckState(preparer,1,NOTE_EMPTY,254);
    output=PrepareOne(preparer,EventAt(1,62,kNoInstrumentUpdate,17));
    CHECK_EQ(output.commands[0].channel,1); CHECK_EQ(output.commands[0].sample.data,pcm_b);
    output=PrepareOne(preparer,EventAt(0,60,kNoInstrumentUpdate));
    CHECK_EQ(output.count,1); CHECK_EQ(output.commands[0].action,VariableRateVoiceAction::Trigger);
    output=PrepareOne(preparer,EventAt(0,NOTE_EMPTY,254));
    CHECK_EQ(output.count,0); CheckState(preparer,0,60,254);
    output=PrepareOne(preparer,EventAt(0,62,kNoInstrumentUpdate));
    CHECK_EQ(output.commands[0].sample.data,pcm_b);
    output=PrepareOne(preparer,EventAt(0,NOTE_EMPTY,kNoInstrumentUpdate));
    CHECK_EQ(output.count,0); CheckState(preparer,0,62,254);
    output=PrepareOne(preparer,EventAt(0,NOTE_OFF,0));
    CHECK_EQ(output.commands[0].action,VariableRateVoiceAction::Stop); CheckState(preparer,0,NOTE_OFF,0);
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
    CHECK_EQ(preparer.Prepare(ordered,output),VariableSampleCommandStatus::Success);
    CHECK_EQ(output.count,3);
    CHECK_EQ(output.commands[0].action,VariableRateVoiceAction::Trigger);
    CHECK_EQ(output.commands[1].action,VariableRateVoiceAction::Stop);
    CHECK_EQ(output.commands[2].channel,1);
    const RowEventBatch empty_batch{};
    CHECK_EQ(preparer.Prepare(empty_batch,output),VariableSampleCommandStatus::Success);
    CHECK_EQ(output.count,0); CheckState(preparer,0,NOTE_OFF,0); CheckState(preparer,1,62,254);
    RowEventBatch loop_start{}; loop_start.count=1;
    loop_start.events[0]=EventAt(1,62,kNoInstrumentUpdate);
    loop_start.events[0].loop_index=10;
    CHECK_EQ(preparer.Prepare(loop_start,output),VariableSampleCommandStatus::Success);
    CHECK_EQ(output.commands[0].sample.data,pcm_b); // Loop change never clears selection.
    LogicalChannelState snapshot{5,6};
    CHECK_EQ(preparer.GetChannel(UINT32_MAX,snapshot),ChannelStateStatus::InvalidChannel);
    CHECK_EQ(snapshot.note,5); CHECK_EQ(snapshot.instrument,6);
    preparer.Reset();
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    output=PrepareOne(preparer,EventAt(0,60,0)); CHECK_EQ(output.count,1);
}

TEST_CASE(VariableSampleCommands_TransactionalPreparationFailures)
{
    VariableRateSampleCommandPreparer preparer;
    RowEventBatch input{};
    VariableRateVoiceCommandBatch output{}; output.count=16;
    for (auto& command:output.commands) command={7,123,VariableRateVoiceAction::Trigger,{pcm_b,3,44100}};
    const auto sentinel=output;
    CHECK_EQ(preparer.Prepare(input,output),VariableSampleCommandStatus::NotConfigured);
    CheckBatch(output,sentinel);
    CHECK_EQ(preparer.Configure(44100,Bindings()),VariableSampleCommandStatus::Success);
    for (std::uint8_t c=0;c<8;++c) (void)PrepareOne(preparer,EventAt(c,60,0));
    const auto unchanged=[&]() {
        CheckBatch(output,sentinel);
        for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,60,0);
    };
    const auto reject=[&](RowEvent bad, VariableSampleCommandStatus expected) {
        input.count=2; input.events[0]=EventAt(0,62,254,3); input.events[1]=bad;
        CHECK_EQ(preparer.Prepare(input,output),expected); unchanged();
    };
    reject(EventAt(1,60,99,4),VariableSampleCommandStatus::UnknownInstrument);
    reject(EventAt(8,60,0,4),VariableSampleCommandStatus::InvalidChannel);
    reject(EventAt(255,60,0,4),VariableSampleCommandStatus::InvalidChannel);
    for (unsigned int note=0;note<NOTE_OFF;++note)
    {
        if (IsPatternNote(note)) continue;
        reject(EventAt(1,static_cast<Note>(note),0,4),VariableSampleCommandStatus::InvalidNote);
    }
    auto bad_timing=EventAt(1,NOTE_OFF,0,4);
    bad_timing.note_off_timing=static_cast<NoteOffTiming>(99);
    reject(bad_timing,VariableSampleCommandStatus::InvalidNoteOffTiming);
    reject(EventAt(1,60,0,2),VariableSampleCommandStatus::UnorderedOffsets);
    input.count=9;
    CHECK_EQ(preparer.Prepare(input,output),VariableSampleCommandStatus::InvalidEventCount); unchanged();
    input.count=255;
    CHECK_EQ(preparer.Prepare(input,output),VariableSampleCommandStatus::InvalidEventCount); unchanged();
    preparer.Reset(); input.count=1; input.events[0]=EventAt(0,60,kNoInstrumentUpdate);
    CHECK_EQ(preparer.Prepare(input,output),VariableSampleCommandStatus::MissingInstrument);
    CheckBatch(output,sentinel); CheckState(preparer,0,NOTE_EMPTY,kNoInstrumentUpdate);
    input.count=8;
    for (std::uint8_t c=0;c<8;++c) input.events[c]=EventAt(c,60,0);
    CHECK_EQ(preparer.Prepare(input,output),VariableSampleCommandStatus::Success);
    CHECK_EQ(output.count,8); // Every event retained, never truncated.
}

TEST_CASE(VariableSampleCommands_ConfigurationValidationAndReconfiguration)
{
    VariableRateSampleCommandPreparer preparer;
    const auto original=Bindings();
    CHECK_EQ(preparer.Configure(0,original),VariableSampleCommandStatus::InvalidSampleRate);
    const RowEventBatch empty_batch{};
    VariableRateVoiceCommandBatch unconfigured_output{};
    CHECK_EQ(preparer.Prepare(empty_batch,unconfigured_output),VariableSampleCommandStatus::NotConfigured);
    CHECK_EQ(preparer.Configure(44100,original),VariableSampleCommandStatus::Success);
    (void)PrepareOne(preparer,EventAt(0,60,0));
    const auto reject=[&](const VariableRateSampleBindings& bad, VariableSampleCommandStatus expected,
        std::uint32_t rate=44100) {
        CHECK_EQ(preparer.Configure(rate,bad),expected);
        CheckState(preparer,0,60,0);
        const auto output=PrepareOne(preparer,EventAt(0,60,kNoInstrumentUpdate));
        CHECK_EQ(output.commands[0].sample.data,pcm_a);
        CHECK_EQ(output.commands[0].sample.sample_rate_hz,44100);
    };
    reject(original,VariableSampleCommandStatus::InvalidSampleRate,0);
    auto bad=original; bad.count=17; reject(bad,VariableSampleCommandStatus::InvalidBindingCount);
    bad.count=SIZE_MAX; reject(bad,VariableSampleCommandStatus::InvalidBindingCount);
    bad=original; bad.bindings[1].instrument=0; reject(bad,VariableSampleCommandStatus::DuplicateInstrument);
    bad=original; bad.bindings[1].instrument=255; reject(bad,VariableSampleCommandStatus::ReservedInstrument);
    for (const auto note:{0,23,128,254,255}) {
        bad=original; bad.bindings[1].reference_note=static_cast<Note>(note);
        reject(bad,VariableSampleCommandStatus::InvalidReferenceNote);
    }
    bad=original; bad.bindings[1].sample.data=nullptr; reject(bad,VariableSampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.frame_count=0; reject(bad,VariableSampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.sample_rate_hz=0; reject(bad,VariableSampleCommandStatus::InvalidSample);
    bad=original; bad.bindings[1].sample.sample_rate_hz=48000; reject(bad,VariableSampleCommandStatus::SampleRateMismatch);
    bad=original; bad.bindings[1].sample.frame_count=VariableRateSampleVoice::kMaximumFrameCount+1;
    reject(bad,VariableSampleCommandStatus::RangeOverflow);
    VariableRateSampleBindings full{}; full.count=kVariableRateSampleBindingCapacity;
    for (std::size_t i=0;i<full.count;++i)
        full.bindings[i]={static_cast<std::uint8_t>(i),static_cast<Note>(24+i),{pcm_a,4,48000}};
    CHECK_EQ(preparer.Configure(48000,full),VariableSampleCommandStatus::Success);
    for (std::uint32_t c=0;c<8;++c) CheckState(preparer,c,NOTE_EMPTY,kNoInstrumentUpdate);
    auto output=PrepareOne(preparer,EventAt(7,39,15));
    CHECK_EQ(output.commands[0].sample.sample_rate_hz,48000);
    output=PrepareOne(preparer,EventAt(0,24,0)); CHECK_EQ(output.count,1);
    full.bindings[15].reference_note=127;
    CHECK_EQ(preparer.Configure(48000,full),VariableSampleCommandStatus::Success);
    output=PrepareOne(preparer,EventAt(7,127,15)); CHECK_EQ(output.count,1);
    VariableRateSampleBindings empty_bindings{};
    CHECK_EQ(preparer.Configure(44100,empty_bindings),VariableSampleCommandStatus::Success);
    output=PrepareOne(preparer,EventAt(0,NOTE_OFF,kNoInstrumentUpdate)); CHECK_EQ(output.count,1);
    // Metadata is stored by value, not retained through the caller's table.
    auto copied=Bindings();
    CHECK_EQ(preparer.Configure(44100,copied),VariableSampleCommandStatus::Success);
    copied.bindings[0]={254,62,{pcm_b,3,44100}};
    output=PrepareOne(preparer,EventAt(0,60,0)); CHECK_EQ(output.commands[0].sample.data,pcm_a);
}


TEST_CASE(VariableSampleCommandsReferenceBoundariesAndExactOctaves) {
    VariableRateSampleCommandPreparer preparer;
    auto bindings=Bindings();
    CHECK_EQ(preparer.Configure(44100,bindings),VariableSampleCommandStatus::Success);
    for(const auto pair : {std::pair<unsigned,std::uint64_t>{60,kSourceFrameStepUnity},
                          {48,kSourceFrameStepUnity/2}, {72,kSourceFrameStepUnity*2}}) {
        const auto result=PrepareOne(preparer,EventAt(0,static_cast<Note>(pair.first),0));
        CHECK_EQ(result.commands[0].step_q32_32,pair.second);
    }
    for(const auto reference : {24,127}) {
        bindings.bindings[0].reference_note=static_cast<Note>(reference);
        CHECK_EQ(preparer.Configure(44100,bindings),VariableSampleCommandStatus::Success);
        for(const auto target : {24,127}) {
            const auto result=PrepareOne(preparer,EventAt(0,static_cast<Note>(target),0));
            std::uint64_t expected=0;
            CHECK_EQ(NoteToPlaybackStep(target,reference,expected),NotePlaybackStepStatus::Success);
            CHECK_EQ(result.commands[0].step_q32_32,expected);
            CHECK(expected>0);
        }
    }
}
TEST_CASE(VariableSampleCommandsDeferredOffRemainsPlayerOwned) {
    VariableRateSampleCommandPreparer preparer;
    CHECK_EQ(preparer.Configure(44100,Bindings()),VariableSampleCommandStatus::Success);
    (void)PrepareOne(preparer,EventAt(0,60,0));
    auto off=EventAt(0,NOTE_OFF,254);
    off.note_off_timing=NoteOffTiming::EndOfPosition;
    auto result=PrepareOne(preparer,off);
    CHECK_EQ(result.count,0); CheckState(preparer,0,NOTE_OFF,254);
    off.note_off_timing=NoteOffTiming::Arrival;
    result=PrepareOne(preparer,off);
    CHECK_EQ(result.count,1); CHECK_EQ(result.commands[0].action,VariableRateVoiceAction::Stop);
    result=PrepareOne(preparer,EventAt(0,50,kNoInstrumentUpdate)); // Explicit reference 62: octave down.
    CHECK_EQ(result.commands[0].step_q32_32,kSourceFrameStepUnity/2);
}
TEST_CASE(VariableSampleCommandsRendererMixerExpectedFractionalPcmAndPartitions) {
    const std::int16_t ramp[]={0,100,200,300,400,500};
    const std::int16_t small[]={10,20,30,40};
    const std::int16_t negative[]={-10,-20,-30};
    VariableRateSampleBindings bindings{}; bindings.count=3;
    bindings.bindings[0]={0,60,{ramp,6,44100}};
    bindings.bindings[1]={1,60,{negative,3,44100}};
    bindings.bindings[2]={2,60,{small,4,44100}};
    const RowEvent events[]={EventAt(0,61,0,0),EventAt(1,48,2,0),
        EventAt(0,72,kNoInstrumentUpdate,4),EventAt(1,NOTE_OFF,kNoInstrumentUpdate,6),
        EventAt(0,NOTE_EMPTY,1,8),EventAt(0,60,kNoInstrumentUpdate,9)};
    const auto collect=[&](bool irregular) {
        VariableRateSampleCommandPreparer preparer;
        VariableRateVoiceBlockRenderer renderer;
        CHECK_EQ(preparer.Configure(44100,bindings),VariableSampleCommandStatus::Success);
        CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
        std::vector<std::int16_t> mixed(20);
        std::size_t start=0,part=0;
        while(start<mixed.size()) {
            const std::size_t requested=irregular ? (part++%3==0 ? 1 : 5) : 4;
            const auto count=std::min(requested,mixed.size()-start);
            RowEventBatch rows{};
            for(auto event:events) if(event.sample_offset>=start && event.sample_offset<start+count) {
                CHECK(rows.count<kRealtimePatternChannelCapacity);
                event.sample_offset-=start; rows.events[rows.count++]=event;
            }
            VariableRateVoiceCommandBatch commands{};
            CHECK_EQ(preparer.Prepare(rows,commands),VariableSampleCommandStatus::Success);
            std::int16_t channels[8][5]{};
            VariableRateVoiceOutputs output;
            Pcm16MixInputs inputs;
            for(unsigned c=0;c<8;++c) { output.channels[c]=channels[c]; inputs.channels[c]=channels[c]; }
            CHECK_EQ(renderer.Render(output,count,commands),VariableRateBlockStatus::Success);
            CHECK_EQ(MixPcm16Mono(inputs,mixed.data()+start,count),Pcm16MixStatus::Success);
            start+=count;
        }
        return mixed;
    };
    // Semitone ratio ~1.059463: ramp rounds to 0,106,212,318 before retrigger.
    // Second channel octave-down contributes 10,15,20,25,30,35 before Stop.
    const std::int16_t expected[]={10,121,232,343,30,235,400,0,0,-10,-20,-30,0,0,0,0,0,0,0,0};
    const auto regular=collect(false),irregular=collect(true);
    for(unsigned i=0;i<20;++i) { CHECK_EQ(regular[i],expected[i]); CHECK_EQ(irregular[i],expected[i]); }
}
