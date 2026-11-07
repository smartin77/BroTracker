#include "test_framework.h"
#include "bringup/pattern_player_selection.h"
#include "firmware/teensy/BroTracker/production_audio_output.h"
#include "firmware/teensy/BroTracker/production_audio_guard.h"
#include <array>
#include <algorithm>
#include <iostream>
using namespace BroTracker;
namespace BroTracker {
AudioActivationMask BeginAudioActivation() { AudioFake::state.guarded=true;return {}; }
void EndAudioActivation(AudioActivationMask) { AudioFake::state.guarded=false; }
}
namespace {
using Block=std::array<std::int16_t,128>;
void Hash(std::uint32_t& hash,const Block& block) {
    for(auto pcm:block) { const auto word=static_cast<std::uint16_t>(pcm);
        hash=(hash^(word&255u))*16777619u;hash=(hash^(word>>8))*16777619u; }
}
void Audio(SelectedPatternBringUpControl& control,Block& left,Block& right) {
    left.fill(1234);right.fill(2345);
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
    control.AudioBlock(left.data(),left.size(),true,right.data());
#else
    control.AudioBlock(left.data(),left.size(),true);
    right=left; // Normal production graph fans source output zero to both sides.
#endif
    PatternAppliedRequest ack;
    while(control.TakeApplied(ack))CHECK_EQ(ack.result,PatternPlayerStatus::Success);
}
void CheckSynthRight(const Block& left,const Block& right) {
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
    for(auto pcm:right)CHECK_EQ(pcm,0);
#else
    CHECK(left==right);
#endif
    (void)left;
}
void Configure(SelectedPatternBringUpControl& control,bool sample,bool synth,bool midi,
    Pcm16MonoView pcm={}) {
    SelectedPatternBindings bindings;bindings.count=3;
    bindings.bindings[0]={0,60,pcm,InstrumentType::Sample,80};
    bindings.bindings[1]={2,0,{},InstrumentType::Synth};
    bindings.bindings[2]={1,0,{},InstrumentType::MIDI};
    if(!sample) { bindings.bindings[0]=bindings.bindings[2];bindings.count=2; }
    RealtimePattern pattern;pattern.active_rows=16;pattern.active_channels=8;
    if(sample)pattern.cells[0][0]={60,0};
    if(synth) {pattern.cells[0][1]={69,2};pattern.cells[1][1]={NOTE_OFF,kNoInstrumentUpdate};}
    if(midi) {pattern.cells[0][2]={69,1};pattern.cells[1][2]={NOTE_OFF,kNoInstrumentUpdate};}
    CHECK_EQ(control.InitializeAudio(12500,44100,pattern,bindings),PatternPlayerStatus::Success);
    CHECK_EQ(control.Submit(PatternRequest::Start),PatternRequestStatus::Accepted);
}
void Ramp(SelectedPatternBringUpControl& control,unsigned block) {
    if(block==2)CHECK_EQ(control.SubmitVolume(50,1),PatternRequestStatus::Accepted);
    if(block==13)CHECK_EQ(control.SubmitVolume(0,2),PatternRequestStatus::Accepted);
    if(block==19)CHECK_EQ(control.SubmitVolume(100,3),PatternRequestStatus::Accepted);
}
}
TEST_CASE(DiagnosticStereoGraphUsesSharedSourceChannelsForEveryBackend) {
    for(auto route:{AudioOutputChoice::Bridge,AudioOutputChoice::Pt8211,AudioOutputChoice::AudioShield}) {
        AudioFake::state={};AudioStream source;AudioOutputUSB usb;ProductionAudioOutput output;
        CHECK(output.Select(route,source,usb)==nullptr);CHECK(!AudioFake::state.badGuard);
        const auto& g=AudioFake::state;
        CHECK_EQ(g.patch[0].from,0);CHECK_EQ(g.patch[0].to,0);
        const unsigned right_patch=route==AudioOutputChoice::Bridge?2:1;
        CHECK_EQ(g.patch[right_patch].from,int(kProductionRightSourceChannel));CHECK_EQ(g.patch[right_patch].to,1);
        if(route==AudioOutputChoice::Bridge) {CHECK_EQ(g.patch[1].from,0);CHECK_EQ(g.patch[1].to,0);}
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        CHECK_EQ(kProductionRightSourceChannel,1u);
#else
        CHECK_EQ(kProductionRightSourceChannel,0u);
#endif
    }
}
TEST_CASE(DiagnosticSynthOutputPreservesLeftGainMasterRampReleaseAndStoppedSilence) {
    SelectedPatternBringUpControl control;Configure(control,false,true,false);
    Block left,right;std::uint32_t hash=2166136261u;bool nonzero=false;
    for(unsigned block=0;block<64;++block) {
        Ramp(control,block);Audio(control,left,right);CheckSynthRight(left,right);Hash(hash,left);
        nonzero=nonzero || std::any_of(left.begin(),left.end(),[](auto x){return x!=0;});
        CHECK_EQ(control.Snapshot().fault,PatternFault::None);
    }
    CHECK(nonzero);CHECK_EQ(control.Submit(PatternRequest::Stop),PatternRequestStatus::Accepted);
    Audio(control,left,right);CHECK(left==Block{});CHECK(right==Block{});
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
    right.fill(2345);control.AudioBlock(nullptr,128,false,right.data());CHECK(right==Block{});
#endif
    std::cout<<"LEFT_SIGNATURE synth "<<hash<<'\n';
}
TEST_CASE(DiagnosticMixedStreamAndSynthKeepsSamplePcmOnBothChannelsAndMidiOffsets) {
    std::array<std::int16_t,2048> start;
    std::array<std::int16_t,4096> ring;
    const auto pcm=[](unsigned frame){return std::int16_t(int((frame*37)%2001)-1000);};
    for(unsigned i=0;i<start.size();++i)start[i]=pcm(i);
    for(unsigned i=2048;i<6144;++i)ring[i%4096]=pcm(i);
    StreamedVoiceBuffer ready[8]{};for(auto& b:ready)b.ring=ring.data();
    StreamedSample sample{start.data(),6144,ready};
    const Pcm16MonoView view{start.data(),6144,44100,&sample};
    SelectedPatternBringUpControl mixed,samples;Configure(mixed,true,true,true,view);
    // Independent ready metadata for the Sample-only reference player.
    StreamedVoiceBuffer reference_ready[8]{};for(auto& b:reference_ready)b.ring=ring.data();
    StreamedSample reference_sample{start.data(),6144,reference_ready};
    Configure(samples,true,false,false,{start.data(),6144,44100,&reference_sample});
    Block left,right,sample_left,sample_right;std::uint32_t hash=2166136261u,midi_hash=2166136261u;
    for(unsigned block=0;block<64;++block) {
        Ramp(mixed,block);Ramp(samples,block);
        Audio(mixed,left,right);Audio(samples,sample_left,sample_right);Hash(hash,left);
        CHECK(sample_left==sample_right);
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        CHECK(right==sample_left);
#else
        CHECK(right==left);
#endif
        if(block==0) {
            const auto a=mixed.OutputPlayer().MidiPlayer().SampleVoice(0)->StreamSnapshot();
            ready[0].generation=a.generation;ready[0].sample=&sample;ready[0].begin=2048;ready[0].end=6144;
            const auto b=samples.OutputPlayer().MidiPlayer().SampleVoice(0)->StreamSnapshot();
            reference_ready[0].generation=b.generation;reference_ready[0].sample=&reference_sample;
            reference_ready[0].begin=2048;reference_ready[0].end=6144;
        }
        if(block<32)CHECK_EQ(mixed.OutputPlayer().MidiPlayer().SampleVoice(0)->GetFramePosition(),(block+1)*128u);
        const auto events=mixed.OutputPlayer().MidiPlayer().TakeMidiCommands();
        for(unsigned i=0;i<events.count;++i) {
            const auto& e=events.commands[i];
            for(auto value:{std::uint64_t(e.action),std::uint64_t(e.channel),e.sample_offset,std::uint64_t(e.synth_note)})
                for(unsigned byte=0;byte<8;++byte)midi_hash=(midi_hash^unsigned((value>>(8*byte))&255u))*16777619u;
        }
    }
    CHECK_EQ(mixed.OutputPlayer().MidiPlayer().SampleVoice(0)->StreamSnapshot().underruns,0u);
    std::cout<<"LEFT_SIGNATURE mixed "<<hash<<" MIDI_SIGNATURE "<<midi_hash<<'\n';
}
TEST_CASE(DiagnosticSynthCrossfadeTailNeverLeaksThroughSampleReplacement) {
    VariableRateVoiceBlockRenderer renderer;CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    std::int16_t outputs[8][128]{},right[8][128]{};
    VariableRateVoiceOutputs out;for(unsigned i=0;i<8;++i) {
        out.channels[i]=outputs[i];
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        out.diagnostic_without_synth[i]=right[i];
#endif
    }
    std::array<std::int16_t,4096> silence{};
    VariableRateVoiceCommandBatch commands;commands.count=1;
    commands.commands[0]={0,17,VariableRateVoiceAction::TriggerSynth,{},kSourceFrameStepUnity,69};
    std::uint32_t hash=2166136261u;
    for(unsigned block=0;block<8;++block) {
        if(block==3) {commands.count=1;commands.commands[0]={0,11,VariableRateVoiceAction::Trigger,{silence.data(),silence.size(),44100}};}
        if(block==4) {commands.count=1;commands.commands[0]={0,31,VariableRateVoiceAction::TriggerSynth,{},kSourceFrameStepUnity,72};}
        if(block==6) {commands.count=1;commands.commands[0]={0,9,VariableRateVoiceAction::Stop,{}};}
        CHECK_EQ(renderer.Render(out,128,commands),VariableRateBlockStatus::Success);commands.count=0;
        Block left,r;std::copy_n(outputs[0],128,left.begin());
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        std::copy_n(right[0],128,r.begin());
#else
        r=left;
#endif
        CheckSynthRight(left,r);Hash(hash,left);
    }
    std::cout<<"LEFT_SIGNATURE transitions "<<hash<<'\n';
}
TEST_CASE(DiagnosticSampleTailIsRetainedWhenSwitchingToSynth) {
    VariableRateVoiceBlockRenderer renderer;CHECK_EQ(renderer.Configure(44100),VariableRateBlockStatus::Success);
    std::int16_t output[8][128]{},right[8][128]{};VariableRateVoiceOutputs out;
    for(unsigned i=0;i<8;++i) {
        out.channels[i]=output[i];
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        out.diagnostic_without_synth[i]=right[i];
#endif
    }
    std::array<std::int16_t,4096> sample;sample.fill(3000);
    VariableRateVoiceCommandBatch commands;commands.count=1;
    commands.commands[0]={0,0,VariableRateVoiceAction::Trigger,{sample.data(),sample.size(),44100}};
    CHECK_EQ(renderer.Render(out,128,commands),VariableRateBlockStatus::Success);
    for(auto pcm:output[0])CHECK_EQ(pcm,3000);
    commands.commands[0]={0,0,VariableRateVoiceAction::TriggerSynth,{},kSourceFrameStepUnity,69};
    CHECK_EQ(renderer.Render(out,128,commands),VariableRateBlockStatus::Success);
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
    for(unsigned i=0;i<128;++i) {
        const auto expected=i<88 ? std::int16_t(std::lround(3000*(1-SmoothEndpoint(float(i)/87)))) : 0;
        CHECK_EQ(right[0][i],expected);
    }
#endif
    Block left;std::copy_n(output[0],128,left.begin());std::uint32_t hash=2166136261u;Hash(hash,left);
    std::cout<<"LEFT_SIGNATURE sample-tail "<<hash<<'\n';
}
