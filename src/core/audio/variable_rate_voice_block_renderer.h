#pragma once

#include "variable_rate_sample_voice.h"
#include "pcm16_gain_mixer.h"
#include <algorithm>
#include <type_traits>
#include "sine_synth_voice.h"
#include "core/playback/row_events.h"

namespace BroTracker
{
    // Initial command implementation capacity, not a permanent pattern-format limit.
    constexpr std::size_t kVariableRateVoiceCommandCapacity = 16;
    enum class VariableRateVoiceAction { Trigger, Stop, TriggerSynth, TriggerMidi, Release };
    // Explicitly construct from the matching PCM view. Existing mono commands
    // retain their mono view and cannot carry stereo PCM.
    class Pcm16MixedSampleView {
    public:
        Pcm16MixedSampleView()=default;
        Pcm16MixedSampleView(Pcm16MonoView view) noexcept
            : Pcm16MixedSampleView(view.data,view.frame_count,view.sample_rate_hz,view.streaming) {}
        explicit Pcm16MixedSampleView(Pcm16StereoView view) noexcept
            : Pcm16MixedSampleView(view.data,view.frame_count,view.sample_rate_hz,view.streaming) { stereo_=true; }
        Pcm16MixedSampleView(const std::int16_t* pcm,std::size_t frames,std::uint32_t rate,
            const StreamedSample* source=nullptr) noexcept
            : data(pcm),frame_count(frames),sample_rate_hz(rate),streaming(source) {}
        bool IsStereo() const noexcept { return stereo_; }
        Pcm16MonoView Mono() const noexcept {
            return stereo_ ? Pcm16MonoView{} : Pcm16MonoView{data,frame_count,sample_rate_hz,streaming};
        }
        VariableVoiceStatus Trigger(VariableRateSampleVoice& voice,std::uint64_t step) const noexcept {
            if(stereo_) return voice.TriggerStereo({data,frame_count,sample_rate_hz,streaming},step);
            return voice.Trigger({data,frame_count,sample_rate_hz,streaming},step);
        }
        const std::int16_t* data=nullptr;
        std::size_t frame_count=0;
        std::uint32_t sample_rate_hz=0;
        const StreamedSample* streaming=nullptr;
    private:
        bool stereo_=false;
    };
    template<class Sample> struct BasicVariableRateVoiceCommand
    {
        std::uint32_t channel = 0;
        std::uint64_t sample_offset = 0;
        VariableRateVoiceAction action = VariableRateVoiceAction::Stop;
        Sample sample;
        std::uint64_t step_q32_32 = kSourceFrameStepUnity;
        Note synth_note = 69; // Raw Synth/MIDI pitch; never the display octave convention.
        std::uint8_t sample_level_percent = 100;
        std::uint8_t midi_channel = 0;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        std::uint8_t instrument = kNoInstrumentUpdate; // Raw binding identity, not channel or PCM address.
#endif
    };
    template<class Sample> struct BasicVariableRateVoiceCommandBatch
    {
        std::size_t count = 0;
        BasicVariableRateVoiceCommand<Sample> commands[kVariableRateVoiceCommandCapacity]{};
    };
    using VariableRateVoiceCommand=BasicVariableRateVoiceCommand<Pcm16MonoView>;
    using VariableRateVoiceCommandBatch=BasicVariableRateVoiceCommandBatch<Pcm16MonoView>;
    using VariableRateMixedVoiceCommandBatch=BasicVariableRateVoiceCommandBatch<Pcm16MixedSampleView>;
    struct VariableRateVoiceOutputs
    {
        std::int16_t* channels[kRealtimePatternChannelCapacity]{};
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
        // Optional diagnostic right contribution (excludes Synth and Sample #04),
        // before the shared channel-gain mix.
        std::int16_t* diagnostic_without_synth[kRealtimePatternChannelCapacity]{};
#endif
    };
    // Supplied by stereo callers as fixed reusable storage. Mono callers do not
    // allocate this scratch. All spans used in a render are overwritten.
    struct VariableRateStereoMixScratch {
        static constexpr std::size_t kFrameCapacity=128;
        std::int16_t left[kFrameCapacity],right[kFrameCapacity];
        std::int64_t left_sum[kFrameCapacity],right_minus_left[kFrameCapacity];
    };
    enum class VariableRateBlockStatus
    {
        Success, NotConfigured, InvalidSampleRate, InvalidCommandCount,
        InvalidChannel, InvalidAction, InvalidOffset, UnorderedOffsets,
        InvalidDestination, RangeOverflow, InvalidSample, SampleRateMismatch, InvalidStep, InvalidNote, InvalidGain
    };

    // Single-owner audio consumer of prepared sample/Synth commands; no scheduling,
    // clock selection, synchronization, tempo correction or independent timeline.
    // Sample: explicit positive Q32.32 steps; matching source/output rates required.
    // Synth: MIDI-compatible note tuning, independent one-shot envelope per channel.
    // Linear interpolation may alias and changes pitch AND duration; no note mapping.
    // MIDI scheduling is separate and need not wait for audio block boundaries.
    class VariableRateVoiceBlockRenderer
    {
    public:
        // Valid configuration clears all playback; zero fails without mutation.
        [[nodiscard]] VariableRateBlockStatus Configure(std::uint32_t sample_rate_hz) noexcept
        {
            if (sample_rate_hz == 0) return VariableRateBlockStatus::InvalidSampleRate;
            for (unsigned i=0;i<kRealtimePatternChannelCapacity;++i) { voices_[i].SetStreamChannel(i); (void)voices_[i].Configure(sample_rate_hz); }
            for (auto& synth : synths_) (void)synth.Configure(sample_rate_hz);
            Reset();
            sample_rate_hz_ = sample_rate_hz;
            return VariableRateBlockStatus::Success;
        }
        void Reset() noexcept {
            for (auto& voice : voices_) voice.Reset();
            for (auto& synth : synths_) synth.Reset();
            for (auto& channel : channels_) channel = {};
        }
        // Read-only snapshot; nullptr for out-of-range channels. No mutable access.
        const VariableRateSampleVoice* GetVoice(std::uint32_t channel) const noexcept
        {
            return channel < kRealtimePatternChannelCapacity ? &voices_[channel] : nullptr;
        }

        // Preflight the whole request before touching voices/output. Offsets are
        // uint64_t, checked before size_t conversion, ordered nondecreasing and
        // strictly below frame_count. Equal-offset commands retain input order.
        // An end-boundary command belongs to the next block at offset zero.
        // Configured zero-frame blocks accept only empty batches and permit null
        // outputs. Stop ignores its sample/step fields: Sample stops immediately; Synth
        // releases over 15 ms. Reset clears all voices and transitions immediately.
        // TriggerMidi explicitly clears this channel and emits no MIDI events.
        // Caller guarantees eight writable, mutually non-overlapping output spans,
        // each of frame_count frames, disjoint from ALL retained/triggered immutable
        // sample data, this object and request metadata. Sample extents/lifetimes
        // are caller-owned through replacement, stop, reset, configure or completion.
        // Metadata must remain unchanged during the call. Extents/aliasing cannot
        // be inferred from pointers. No sample data copies for validation/ownership.
        // All rejection paths preserve every voice and every destination byte.
        // Fixed storage, no allocation/I/O/callbacks/exceptions. Work O(8*frames +
        // 8*16): at most 16 commands and 17 render intervals. No measured Teensy
        // processing budget is claimed; caller bounds frames and command preparation.
        [[nodiscard]] VariableRateBlockStatus Render(const VariableRateVoiceOutputs& output,
            std::uint64_t frame_count, const VariableRateVoiceCommandBatch& batch) noexcept
        {
            return RenderBlock(output,frame_count,batch);
        }
        // Frame-bounded normal stereo mix of Sample and Synth commands. PCM,
        // output spans and scratch must be mutually disjoint (immutable PCM
        // sources may share storage). Zero frames allow null destinations.
        // Preflight rejects the entire block before writing either output or
        // mutating voices. All steps after preflight are infallible.
        [[nodiscard]] VariableRateBlockStatus RenderStereo(std::int16_t* left,std::int16_t* right,
            std::uint64_t frame_count,const VariableRateMixedVoiceCommandBatch& batch,
            const Pcm16ChannelGains& gains,VariableRateStereoMixScratch& scratch,bool diagnostic=false) noexcept
        {
            if(frame_count>VariableRateStereoMixScratch::kFrameCapacity) return VariableRateBlockStatus::RangeOverflow;
            if(frame_count && (!left || !right || left==right)) return VariableRateBlockStatus::InvalidDestination;
            for(const auto gain:gains.channels)
                if(gain>kPcm16UnityGain) return VariableRateBlockStatus::InvalidGain;
            return RenderBlock(StereoOutput{left,right,gains,scratch,diagnostic},frame_count,batch);
        }
    private:
        struct StereoOutput {
            std::int16_t* left;
            std::int16_t* right;
            const Pcm16ChannelGains& gains;
            VariableRateStereoMixScratch& scratch;
            bool diagnostic;
        };
        static VariableVoiceStatus TriggerSample(VariableRateSampleVoice& voice,Pcm16MonoView sample,
            std::uint64_t step) noexcept { return voice.Trigger(sample,step); }
        static VariableVoiceStatus TriggerSample(VariableRateSampleVoice& voice,const Pcm16MixedSampleView& sample,
            std::uint64_t step) noexcept { return sample.Trigger(voice,step); }
        template<class Output,class Sample>
        VariableRateBlockStatus RenderBlock(const Output& output,std::uint64_t frame_count,
            const BasicVariableRateVoiceCommandBatch<Sample>& batch) noexcept
        {
            if (sample_rate_hz_ == 0) return VariableRateBlockStatus::NotConfigured;
            if (batch.count > kVariableRateVoiceCommandCapacity) return VariableRateBlockStatus::InvalidCommandCount;
            if (frame_count > VariableRateSampleVoice::kMaximumFrameCount) return VariableRateBlockStatus::RangeOverflow;
            if constexpr (std::is_same_v<Output,VariableRateVoiceOutputs>) {
                if(frame_count) for(std::size_t channel=0;channel<kRealtimePatternChannelCapacity;++channel) {
                    if(!output.channels[channel]) return VariableRateBlockStatus::InvalidDestination;
                    if(HasDistinctRight(channel)) return VariableRateBlockStatus::InvalidSample;
                }
            }

            VariableRateSampleVoice probe = voices_[0];
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                if (command.channel >= kRealtimePatternChannelCapacity) return VariableRateBlockStatus::InvalidChannel;
                if (command.action != VariableRateVoiceAction::Trigger && command.action != VariableRateVoiceAction::Stop &&
                    command.action != VariableRateVoiceAction::TriggerSynth && command.action != VariableRateVoiceAction::TriggerMidi &&
                    command.action != VariableRateVoiceAction::Release)
                    return VariableRateBlockStatus::InvalidAction;
                if (command.sample_offset >= frame_count) return VariableRateBlockStatus::InvalidOffset;
                if (i != 0 && command.sample_offset < batch.commands[i - 1].sample_offset)
                    return VariableRateBlockStatus::UnorderedOffsets;
                if (command.action == VariableRateVoiceAction::TriggerSynth) {
                    if(sample_rate_hz_<32000 || sample_rate_hz_>192000) return VariableRateBlockStatus::InvalidSampleRate;
                    if(!IsPitchedPatternNote(command.synth_note)) return VariableRateBlockStatus::InvalidNote;
                }
                if (command.action == VariableRateVoiceAction::Trigger)
                {
                    if(command.sample_level_percent>100) return VariableRateBlockStatus::InvalidGain;
                    const auto status = TriggerSample(probe,command.sample,command.step_q32_32);
                    if (status == VariableVoiceStatus::InvalidStep) return VariableRateBlockStatus::InvalidStep;
                    if (status == VariableVoiceStatus::InvalidSample) return VariableRateBlockStatus::InvalidSample;
                    if (status == VariableVoiceStatus::RangeOverflow) return VariableRateBlockStatus::RangeOverflow;
                    if (status == VariableVoiceStatus::SampleRateMismatch) return VariableRateBlockStatus::SampleRateMismatch;
                }
            }
            if (frame_count == 0) return VariableRateBlockStatus::Success;
            std::size_t position = 0;
            for (std::size_t i = 0; i < batch.count; ++i)
            {
                const auto& command = batch.commands[i];
                const auto offset = static_cast<std::size_t>(command.sample_offset);
                RenderSpan(output, position, offset - position);
                auto& voice = voices_[command.channel];
                auto& channel = channels_[command.channel];
                auto& synth = synths_[command.channel];
                if(command.action == VariableRateVoiceAction::Release) {
                    if(channel.synth) synth.Release();
                    else { channel.release_length=std::max<std::uint32_t>(2,(sample_rate_hz_+499)/500); channel.release=channel.release_length; }
                } else if(command.action == VariableRateVoiceAction::TriggerMidi) {
                    voice.Reset(); synth.Reset(); channel={}; // MIDI contributes no local PCM.
                } else if(command.action == VariableRateVoiceAction::Stop) {
                    if(channel.synth) synth.Release(); else voice.Stop();
                } else {
                    channel.release=channel.release_length=0;
                    // Fixed 2 ms smoothstep crossfade from the last emitted value.
                    // A repeated trigger captures the current mixed value; no tail stack.
                    // Sample-to-sample replacement keeps its established semantics.
                    if(channel.synth || channel.transition<channel.length || command.action == VariableRateVoiceAction::TriggerSynth) {
                        channel.from=channel.last; channel.right_from=channel.right_last; channel.transition=0;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                        channel.diagnostic_from=channel.diagnostic_last;
#endif
                        channel.length=static_cast<std::uint32_t>((std::uint64_t(sample_rate_hz_)*2+500)/1000);
                        if(channel.length<2) channel.length=2;
                    }
                    channel.synth=command.action == VariableRateVoiceAction::TriggerSynth;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                    channel.diagnostic_excluded=channel.synth || command.instrument==3;
#endif
                    channel.sample_level_percent=command.sample_level_percent;
                    if(channel.synth) { voice.Reset(); (void)synth.Trigger(command.synth_note); }
                    else { synth.Reset(); (void)TriggerSample(voice,command.sample,command.step_q32_32); }
                }
                position = offset;
            }
            RenderSpan(output, position, static_cast<std::size_t>(frame_count) - position);
            return VariableRateBlockStatus::Success;
        }
        void RenderSpan(const VariableRateVoiceOutputs& output, std::size_t start, std::size_t count) noexcept
        {
            if (count == 0) return;
            for(std::size_t channel=0;channel<kRealtimePatternChannelCapacity;++channel) {
                auto* left=output.channels[channel]+start;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                auto* diagnostic=output.diagnostic_without_synth[channel];
                RenderChannel<false,true>(channel,left,diagnostic ? diagnostic+start : nullptr,count);
#else
                RenderChannel<false,false>(channel,left,nullptr,count);
#endif
            }
        }
    public:
        bool HasDistinctRight(std::size_t channel) const noexcept {
            const auto& state=channels_[channel];
            return voices_[channel].IsStereo() ||
                (state.transition<state.length && state.from!=state.right_from);
        }
    private:
        void RenderSpan(const StereoOutput& output,std::size_t start,std::size_t count) noexcept {
            if(!count) return;
            auto& scratch=output.scratch;
            std::fill_n(scratch.left_sum,count,0);
            std::fill_n(scratch.right_minus_left,count,0);
            for(std::size_t channel=0;channel<kRealtimePatternChannelCapacity;++channel) {
                const auto gain=output.gains.channels[channel];
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                const bool diagnostic=output.diagnostic;
#else
                constexpr bool diagnostic=false;
#endif
                if(diagnostic || HasDistinctRight(channel)) {
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                    if(diagnostic) RenderChannel<true,true>(channel,scratch.left,scratch.right,count);
                    else
#endif
                    RenderChannel<true,false>(channel,scratch.left,scratch.right,count);
                    for(std::size_t i=0;i<count;++i) {
                        scratch.left_sum[i]+=std::int64_t(scratch.left[i])*gain;
                        scratch.right_minus_left[i]+=std::int64_t(scratch.right[i]-scratch.left[i])*gain;
                    }
                } else {
                    RenderChannel<false,false>(channel,scratch.left,nullptr,count);
                    for(std::size_t i=0;i<count;++i) scratch.left_sum[i]+=std::int64_t(scratch.left[i])*gain;
                }
            }
            // An eight-voice difference fits in 37 signed bits including headroom.
            // Add it to the exact left sum, then round and saturate each bus.
            for(std::size_t i=0;i<count;++i) {
                output.left[start+i]=Pcm16GainMixDetail::RoundAndSaturate(scratch.left_sum[i]);
                output.right[start+i]=Pcm16GainMixDetail::RoundAndSaturate(scratch.left_sum[i]+scratch.right_minus_left[i]);
            }
        }
        static void ScaleSample(std::int16_t* pcm,std::size_t count,std::uint8_t percent) noexcept {
            if(percent==100) return;
            for(std::size_t i=0;i<count;++i) {
                const auto value=std::int32_t(pcm[i])*percent;
                pcm[i]=static_cast<std::int16_t>((value+(value>=0?50:-50))/100);
            }
        }
        template<bool Stereo,bool Diagnostic>
        void RenderChannel(std::size_t channel,std::int16_t* left,std::int16_t* right,std::size_t count) noexcept {
            auto& state=channels_[channel];
            auto& voice=voices_[channel];
            if(!state.synth) {
                if constexpr(Stereo) (void)voice.RenderStereo(left,right,count);
                else (void)voice.Render(left,count);
                ScaleSample(left,count,state.sample_level_percent);
                if constexpr(Stereo) ScaleSample(right,count,state.sample_level_percent);
                if(state.transition>=state.length && !state.release) {
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                    if constexpr(Diagnostic) {
                        state.diagnostic_last=state.diagnostic_excluded ? 0 : (Stereo ? right[count-1] : left[count-1]);
                        if(right) {
                            if(state.diagnostic_excluded)std::fill_n(right,count,0);
                            else if constexpr(!Stereo) std::copy_n(left,count,right);
                        }
                    }
#endif
                    state.last=left[count-1];
                    if constexpr(Stereo) state.right_last=right[count-1];
                    else state.right_last=left[count-1];
                    return;
                }
            }
            for(std::size_t i=0;i<count;++i) {
                float value=state.synth ? synths_[channel].Next() : float(left[i]);
                float other=0;
                if constexpr(Stereo) other=state.synth ? value : float(right[i]);
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                if constexpr(Diagnostic) other=state.diagnostic_excluded ? 0.0f : float(Stereo ? right[i] : left[i]);
#endif
                if(state.transition<state.length) {
                    const float weight=SmoothEndpoint(float(state.transition++)/float(state.length-1));
                    value=state.from*(1.0f-weight)+value*weight;
                    if constexpr(Stereo && !Diagnostic) other=state.right_from*(1.0f-weight)+other*weight;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                    if constexpr(Diagnostic) other=state.diagnostic_from*(1.0f-weight)+other*weight;
#endif
                }
                if(state.release) {
                    const auto weight=float(state.release-1)/float(state.release_length-1);
                    value*=weight;
                    if constexpr(Stereo || Diagnostic) other*=weight;
                    if(!--state.release) voice.Stop();
                } else if(!state.synth && state.release_length) {
                    value=other=0;
                }
                left[i]=static_cast<std::int16_t>(std::lround(value));
                if constexpr(Stereo && !Diagnostic) right[i]=static_cast<std::int16_t>(std::lround(other));
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
                if constexpr(Diagnostic) {
                    const auto sample=static_cast<std::int16_t>(std::lround(other));
                    if(right) right[i]=sample;
                    state.diagnostic_last=sample;
                }
#endif
            }
            state.last=left[count-1];
            if constexpr(Stereo) state.right_last=right[count-1];
            else state.right_last=left[count-1];
        }
        struct ChannelTransition {
            bool synth=false;
            std::uint8_t sample_level_percent=100;
            std::uint32_t transition=0,length=0,release=0,release_length=0;
            std::int16_t from=0,last=0;
            std::int16_t right_from=0,right_last=0;
#ifdef BROTRACKER_DIAGNOSTIC_SYNTH_LEFT
            float diagnostic_from=0,diagnostic_last=0;
            bool diagnostic_excluded=false;
#endif
        } channels_[kRealtimePatternChannelCapacity]{};
        SineSynthVoice synths_[kRealtimePatternChannelCapacity]{};
        VariableRateSampleVoice voices_[kRealtimePatternChannelCapacity]{};
        std::uint32_t sample_rate_hz_ = 0;
    };
}
