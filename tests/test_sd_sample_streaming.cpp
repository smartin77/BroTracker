#include "test_framework.h"
#include "firmware/teensy/BroTracker/sd_sample_streaming.h"
#include "firmware/teensy/BroTracker/sd_wav_audition.h"
#include "ui/wav_browser.h"
#include "core/audio/variable_rate_sample_voice.h"
#include "core/playback/variable_rate_pattern_player.h"
#include "bringup/pattern_fixture.h"
#include "bringup/instrument_identity.h"
#include "bringup/instrument_commit.h"
#include "bringup/working_session.h"
#include "bringup/live_instrument_visit.h"
#include "firmware/teensy/BroTracker/sd_wav_browse_job.h"
#include "firmware/teensy/BroTracker/sd_pattern_storage.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>
using namespace BroTracker;

namespace BroTracker {
StreamInterruptGuard::StreamInterruptGuard() noexcept {
    mask_=FakeStreamPlatform::primask; FakeStreamPlatform::primask=1;
}
StreamInterruptGuard::~StreamInterruptGuard() noexcept { FakeStreamPlatform::primask=mask_; }
}
namespace {
constexpr std::uint32_t DataOffset=44;
void DescribeMonoWav(WavMetadata& metadata,std::uint32_t frames) {
    metadata.format=1;metadata.channels=1;metadata.bits=16;metadata.block_align=2;
    metadata.sample_rate=44100;metadata.byte_rate=88200;
    metadata.frames=frames;metadata.data_offset=DataOffset;metadata.data_bytes=std::uint64_t(frames)*2;
}
void ReadOwned() {
    CHECK_EQ(FakeStreamPlatform::primask,0u); // Seek/read must remain outside the ISR guard.
    const bool acquired=SdAccess::AcquireWrite(); CHECK(!acquired);
    if(acquired)SdAccess::ReleaseWrite();
}
void ReadReleased() {
    CHECK_EQ(FakeStreamPlatform::primask,0u);
    const bool acquired=SdAccess::AcquireWrite(); CHECK(acquired);
    if(acquired)SdAccess::ReleaseWrite();
}
struct Player {
    VariableRateSampleVoice voices[8];
    const VariableRateSampleVoice* SampleVoice(unsigned channel) const { return &voices[channel]; }
};
struct Fixture {
    SdSampleStreaming::Storage memory{};
    SdSampleStreaming producer;
    Player player;
    explicit Fixture(std::uint32_t frames=12288) {
        FakePatternSd::Reset(); FakeStreamPlatform::primask=FakeStreamPlatform::ticks=0;
        FakeStreamPlatform::on_barrier=nullptr;
        FakePatternSd::on_seek=FakePatternSd::on_io=ReadOwned;
        DevelopmentWavInspection inspection;
        inspection.attempted=inspection.sd_ready=true;
        for(unsigned source=0;source<3;++source) {
            auto& bytes=FakePatternSd::files[kDevelopmentWavPaths[source]].bytes;
            bytes.resize(DataOffset+frames*2);
            for(std::uint32_t frame=0;frame<frames;++frame) {
                const auto word=static_cast<std::uint16_t>(Pcm(source,frame));
                bytes[DataOffset+frame*2]=word&255u; bytes[DataOffset+frame*2+1]=word>>8;
            }
            auto& file=inspection.files[source];
            file.open_status=SdWavBufferStatus::Success; file.validation=WavBufferStatus::Success;
            DescribeMonoWav(file.metadata,frames);
        }
        producer.Prepare(true,memory,inspection); // Real preparation, handles and immutable starts.
        for(unsigned i=0;i<8;++i) {
            player.voices[i].SetStreamChannel(i);
            CHECK_EQ(player.voices[i].Configure(44100),VariableVoiceStatus::Success);
        }
        for(unsigned source=0;source<3;++source) {
            CHECK_EQ(producer.reports[source].result.status,SdWavBufferStatus::Success);
            for(unsigned frame=0;frame<kStreamStartFrames;++frame) CHECK_EQ(memory.starts[source][frame],Pcm(source,frame));
        }
        FakePatternSd::seeks.clear(); FakePatternSd::reads.clear(); ReadReleased();
    }
    ~Fixture() { FakePatternSd::on_seek=FakePatternSd::on_io=nullptr; FakeStreamPlatform::on_barrier=nullptr; }
    static std::int16_t Pcm(unsigned source,std::uint32_t frame) {
        return static_cast<std::int16_t>(int((frame*137+source*997)%60001)-30000);
    }
    void Trigger(unsigned channel=0,unsigned source=0) {
        CHECK_EQ(producer.reports[source].view.Trigger(player.voices[channel],kSourceFrameStepUnity),VariableVoiceStatus::Success);
        producer.Publish(player);
    }
    StreamedVoiceBuffer Span(unsigned channel=0) const {
        return producer.reports[0].view.streaming->voices[channel];
    }
    void CheckSpan(unsigned channel,unsigned source,std::uint32_t begin,std::uint32_t end) const {
        const auto span=Span(channel); const auto state=player.voices[channel].StreamSnapshot();
        CHECK_EQ(span.sample,producer.reports[source].view.streaming); CHECK_EQ(span.generation,state.generation);
        CHECK_EQ(span.ring,memory.rings[channel]); CHECK_EQ(span.begin,begin); CHECK_EQ(span.end,end);
        CHECK(end-begin<=kStreamRingFrames);
        for(auto frame=begin;frame<end;++frame) CHECK_EQ(span.ring[frame%kStreamRingFrames],Pcm(source,frame));
    }
    void Consume(unsigned channel,unsigned source,std::uint32_t count) {
        const auto at=player.voices[channel].GetFramePosition();
        std::vector<std::int16_t> pcm(count);
        CHECK_EQ(player.voices[channel].Render(pcm.data(),count),VariableVoiceStatus::Success);
        for(unsigned i=0;i<count;++i) CHECK_EQ(pcm[i],Pcm(source,static_cast<std::uint32_t>(at+i)));
        producer.Publish(player);
    }
    void WrappedRefillReady() {
        Trigger(); producer.Service(); Consume(0,0,kStreamStartFrames);
        producer.Service(); Consume(0,0,kStreamReadFrames);
        CheckSpan(0,0,2048,6144); // Next read wraps over still-advertised, already-consumed slots.
    }
};
void SameMetadata(const StreamedVoiceBuffer& a,const StreamedVoiceBuffer& b) {
    CHECK_EQ(a.ring,b.ring); CHECK_EQ(a.sample,b.sample); CHECK_EQ(a.generation,b.generation);
    CHECK_EQ(a.begin,b.begin); CHECK_EQ(a.end,b.end);
}
enum class Fault { Seek, NegativeRead, ShortRead, TruncatedFile };
void SetFault(Fault fault) {
    if(fault==Fault::Seek)FakePatternSd::fail_seek_path=kDevelopmentWavPaths[0];
    if(fault==Fault::NegativeRead)FakePatternSd::negative_read_path=kDevelopmentWavPaths[0];
    if(fault==Fault::ShortRead)FakePatternSd::short_read_path=kDevelopmentWavPaths[0];
    if(fault==Fault::TruncatedFile)FakePatternSd::files[kDevelopmentWavPaths[0]].bytes.resize(DataOffset+6144*2+513);
}
void FailurePreservesBufferedPcm(Fault fault) {
    Fixture f; f.WrappedRefillReady();
    const auto before=f.Span();
    std::array<std::int16_t,kStreamRingFrames> ring;
    std::copy_n(f.memory.rings[0],ring.size(),ring.begin());
    SetFault(fault); f.producer.Service();
    CHECK_EQ(f.producer.reads,3u); CHECK_EQ(f.producer.io_errors,1u);
    CHECK_EQ(FakePatternSd::seeks.size(),3u);
    CHECK_EQ(FakePatternSd::reads.size(),fault==Fault::Seek?2u:3u);
    CHECK_EQ(FakePatternSd::seeks.back().offset,DataOffset+6144u*2);
    if(fault!=Fault::Seek) CHECK_EQ(FakePatternSd::reads.back().count,4096u);
    SameMetadata(f.Span(),before); CHECK(std::equal(ring.begin(),ring.end(),f.memory.rings[0]));
    f.CheckSpan(0,0,2048,6144); ReadReleased();
    for(unsigned i=0;i<3;++i) f.producer.Service();
    CHECK_EQ(FakePatternSd::seeks.size(),3u); // Failure latches only this generation.
    f.Consume(0,0,2048); // Already-published audio stays playable after the failure.
    std::int16_t silent=1;
    CHECK_EQ(f.player.voices[0].Render(&silent,1),VariableVoiceStatus::Success); CHECK_EQ(silent,0);
    CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,1u);
    CHECK_EQ(f.player.voices[0].StreamSnapshot().eofs,0u);
    f.producer.Publish(f.player); f.producer.Service(); CHECK_EQ(FakePatternSd::seeks.size(),3u);
    FakePatternSd::fail_seek_path.clear(); FakePatternSd::negative_read_path.clear(); FakePatternSd::short_read_path.clear();
    f.Trigger(); CHECK(f.Span().generation!=before.generation);
    f.producer.Service(); f.CheckSpan(0,0,2048,4096);
    CHECK_EQ(FakePatternSd::seeks.size(),4u);
    CHECK_EQ(FakePatternSd::reads.size(),fault==Fault::Seek?3u:4u);
    CHECK_EQ(f.producer.io_errors,1u); f.Consume(0,0,4096); ReadReleased();
}
}
TEST_CASE(SdStreamProducerSeekFailureLatchesGenerationAndRetainsPcm) { FailurePreservesBufferedPcm(Fault::Seek); }
TEST_CASE(SdStreamProducerNegativeAndShortReadsNeverPublishPartialPcm) {
    FailurePreservesBufferedPcm(Fault::NegativeRead);
    FailurePreservesBufferedPcm(Fault::ShortRead);
    FailurePreservesBufferedPcm(Fault::TruncatedFile);
}
TEST_CASE(SdStreamProducerFinalPartialChunkIsSuccessOnlyWhenAllExpectedBytesArrive) {
    for(bool short_read:{false,true}) {
        Fixture f(kStreamStartFrames+kStreamReadFrames+123);
        f.Trigger(); f.producer.Service(); f.Consume(0,0,kStreamStartFrames);
        const auto before=f.Span();
        std::array<std::int16_t,kStreamRingFrames> ring;
        std::copy_n(f.memory.rings[0],ring.size(),ring.begin());
        if(short_read)FakePatternSd::short_read_path=kDevelopmentWavPaths[0];
        f.producer.Service();
        CHECK_EQ(FakePatternSd::reads.size(),2u); CHECK_EQ(FakePatternSd::reads.back().count,246u);
        CHECK_EQ(FakePatternSd::reads.back().offset,DataOffset+4096u*2);
        CHECK_EQ(f.producer.io_errors,short_read?1u:0u);
        if(short_read) { SameMetadata(f.Span(),before); CHECK(std::equal(ring.begin(),ring.end(),f.memory.rings[0])); }
        else f.CheckSpan(0,0,2048,4219);
        f.producer.Service(); CHECK_EQ(FakePatternSd::reads.size(),2u);
        CHECK_EQ(f.player.voices[0].StreamSnapshot().eofs,0u);
        if(!short_read) {
            f.Consume(0,0,2171);
            CHECK_EQ(f.player.voices[0].StreamSnapshot().eofs,1u);
            CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,0u);
            CHECK(!f.player.voices[0].IsActive()); f.producer.Service();
            CHECK_EQ(FakePatternSd::reads.size(),2u);
        }
        ReadReleased();
    }
}
TEST_CASE(SdStreamProducerFailedVoiceDoesNotBlockAnotherSource) {
    for(auto fault:{Fault::Seek,Fault::NegativeRead,Fault::ShortRead}) {
        Fixture f; f.Trigger(0,0); f.Trigger(1,1); SetFault(fault);
        f.producer.Service(); f.CheckSpan(0,0,2048,2048);
        f.producer.Service(); f.CheckSpan(1,1,2048,4096);
        f.Consume(1,1,2048); f.producer.Service(); f.CheckSpan(1,1,2048,6144);
        f.Consume(1,1,4096);
        CHECK_EQ(f.player.voices[1].StreamSnapshot().underruns,0u);
        CHECK_EQ(f.producer.io_errors,1u); CHECK_EQ(FakePatternSd::seeks.size(),3u);
        CHECK_EQ(FakePatternSd::seeks[0].path,std::string(kDevelopmentWavPaths[0]));
        CHECK_EQ(FakePatternSd::seeks[1].path,std::string(kDevelopmentWavPaths[1]));
        CHECK_EQ(FakePatternSd::seeks[2].path,std::string(kDevelopmentWavPaths[1]));
        CHECK_EQ(FakePatternSd::reads.size(),fault==Fault::Seek?2u:3u); ReadReleased();
    }
}

namespace {
enum class Change { Stop, Retrigger, ReplaceSource };
Fixture* changing=nullptr;
Change change=Change::Stop;
StreamedVoiceBuffer after_change;
void ChangeAudioState() {
    // Deterministic ISR handoff; no recursive Service, threads, clocks or sleeps.
    CHECK_EQ(FakeStreamPlatform::primask,0u);
    FakeStreamPlatform::primask=1;
    if(change==Change::Stop)changing->player.voices[0].Stop();
    else changing->Trigger(0,change==Change::ReplaceSource?1:0);
    changing->producer.Publish(changing->player);
    after_change=changing->Span();
    FakeStreamPlatform::primask=0;
    FakePatternSd::on_io=ReadOwned; FakeStreamPlatform::on_barrier=nullptr;
}
void DuringRead() { ReadOwned(); ChangeAudioState(); }
void BeforeCommit() { ReadReleased(); ChangeAudioState(); }
void CancelledCompletion(bool after_copy,Change action,bool read_failure=false) {
    Fixture f; f.WrappedRefillReady();
    const auto old=f.Span();
    std::array<std::int16_t,kStreamRingFrames> ring;
    std::copy_n(f.memory.rings[0],ring.size(),ring.begin());
    changing=&f; change=action;
    if(read_failure)FakePatternSd::negative_read_path=kDevelopmentWavPaths[0];
    if(after_copy)FakeStreamPlatform::on_barrier=BeforeCommit;
    else FakePatternSd::on_io=DuringRead;
    f.producer.Service();
    CHECK_EQ(f.producer.stale_reads,1u); CHECK_EQ(f.producer.io_errors,read_failure?1u:0u);
    CHECK_EQ(FakePatternSd::seeks.size(),3u); CHECK_EQ(FakePatternSd::reads.size(),3u);
    CHECK_EQ(FakePatternSd::reads.back().offset,DataOffset+6144u*2); CHECK_EQ(FakePatternSd::reads.back().count,4096u);
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(kDevelopmentWavPaths[0]));
    SameMetadata(f.Span(),after_change); CHECK(f.Span().generation!=old.generation);
    if(!after_copy) CHECK(std::equal(ring.begin(),ring.end(),f.memory.rings[0]));
    for(unsigned source=0;source<3;++source)
        for(unsigned frame=0;frame<2048;++frame) CHECK_EQ(f.memory.starts[source][frame],Fixture::Pcm(source,frame));
    if(action==Change::Stop) {
        CHECK_EQ(f.Span().sample,nullptr); CHECK_EQ(f.Span().begin,0u); CHECK_EQ(f.Span().end,0u);
        std::int16_t silent=1; CHECK_EQ(f.player.voices[0].Render(&silent,1),VariableVoiceStatus::Success); CHECK_EQ(silent,0);
        f.producer.Service(); CHECK_EQ(FakePatternSd::reads.size(),3u);
        f.Trigger();
    }
    const unsigned source=action==Change::ReplaceSource?1:0;
    FakePatternSd::negative_read_path.clear();
    f.CheckSpan(0,source,2048,2048);
    f.Consume(0,source,128); // Current generation's immutable start remains playable.
    f.producer.Service(); f.CheckSpan(0,source,2048,4096);
    CHECK_EQ(FakePatternSd::reads.size(),4u); CHECK_EQ(FakePatternSd::seeks.size(),4u);
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(kDevelopmentWavPaths[source]));
    CHECK_EQ(FakePatternSd::reads.back().offset,DataOffset+2048u*2);
    f.Consume(0,source,3968); CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,0u);
    CHECK_EQ(f.producer.stale_reads,1u); ReadReleased(); changing=nullptr;
}
}
TEST_CASE(SdStreamProducerRejectsStopAndRetriggerDuringRead) {
    for(auto action:{Change::Stop,Change::Retrigger,Change::ReplaceSource})CancelledCompletion(false,action);
}
TEST_CASE(SdStreamProducerRejectsStopAndRetriggerAfterCopyBeforeCommit) {
    for(auto action:{Change::Stop,Change::Retrigger,Change::ReplaceSource})CancelledCompletion(true,action);
}
TEST_CASE(SdStreamProducerStaleReadFailureDoesNotLatchTheNewGeneration) {
    CancelledCompletion(false,Change::Retrigger,true);
}

TEST_CASE(StreamingPreparationKeepsDefinitionIdentityAcrossMissingAndReadFailures) {
    for(unsigned mode=0;mode<3;++mode) {
        FakePatternSd::Reset();FakeStreamPlatform::primask=0;
        SdSampleStreaming::Storage memory{};SdSampleStreaming producer;DevelopmentWavInspection inspection;
        inspection.attempted=inspection.sd_ready=true;auto& inspected=inspection.files[1];
        inspected.open_status=mode==1?SdWavBufferStatus::OpenFailed:SdWavBufferStatus::Success;
        inspected.validation=mode==1?WavBufferStatus::IoError:WavBufferStatus::Success;
        DescribeMonoWav(inspected.metadata,32);
        const auto& definition=*DevelopmentInstrumentDefinition(0);
        if(mode!=1)FakePatternSd::files[definition.resource].bytes.resize(44+64);
        if(mode==2)FakePatternSd::short_read_path=definition.resource;
        producer.Prepare(true,memory,inspection);
        const auto availability=SamplePreparationAvailability(definition,producer.reports[1],inspected);
        CHECK(availability==(mode==0?InstrumentAvailability::Ready:mode==1?InstrumentAvailability::ResourceUnavailable:InstrumentAvailability::PreparationFailed));
        const auto facts=DevelopmentInstrumentFacts(0,availability);CHECK_EQ(facts.type,1u);CHECK_EQ(std::string(facts.resource),std::string("Samples/test2.wav"));
        const auto playable=SelectDevelopmentFixture(producer.reports[1].view).bindings;
        CHECK_EQ(playable.count,mode==0?3u:2u);CHECK_EQ(InstrumentBindingIdentity(playable,0),mode==0?1u:0u);
    }
}

TEST_CASE(SdStreamPreparationRejectsInvalidSourceLayoutBeforeReadingPcm) {
    for(unsigned mode=0;mode<6;++mode) {
        FakePatternSd::Reset();
        SdSampleStreaming::Storage memory{};
        SdSampleStreaming producer;
        DevelopmentWavInspection inspection;
        auto& found=inspection.files[0];
        found.validation=WavBufferStatus::Success;
        DescribeMonoWav(found.metadata,32);
        auto& bytes=FakePatternSd::files[kDevelopmentWavPaths[0]].bytes;
        bytes.resize(DataOffset+64);
        switch(mode) {
            case 0: ++found.metadata.data_bytes; break;
            case 1: --found.metadata.byte_rate; break;
            case 2: ++found.metadata.block_align; break;
            case 3: bytes.pop_back(); break;
            case 4: found.metadata.data_offset=UINT64_MAX; break;
            case 5: found.metadata.frames=std::uint64_t{UINT32_MAX}+1; break;
        }
        producer.Prepare(true,memory,inspection);
        CHECK(producer.reports[0].view.streaming==nullptr);
        CHECK(FakePatternSd::reads.empty());
    }
}

TEST_CASE(SdStreamRefillRecoversBeyondGapWithAReadThatWrapsTheRing) {
    Fixture f;
    f.Trigger();
    f.producer.Service();
    f.CheckSpan(0,0,2048,4096);
    f.Consume(0,0,4096);
    std::vector<std::int16_t> missing(2904);
    CHECK_EQ(f.player.voices[0].Render(missing.data(),missing.size()),VariableVoiceStatus::Success);
    for(auto value:missing) CHECK_EQ(value,0);
    f.producer.Publish(f.player);
    CHECK(!f.producer.BackgroundHeadroom());
    f.producer.Service();
    CHECK_EQ(FakePatternSd::reads.back().offset,DataOffset+7000u*2);
    CHECK_EQ(FakePatternSd::reads.back().count,kStreamReadBytes);
    f.CheckSpan(0,0,7000,9048);
    CHECK(f.producer.BackgroundHeadroom());
    f.Consume(0,0,2048);
    CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,2904u);
}

namespace {
std::vector<std::uint8_t> PickerWav(unsigned frames=10000,unsigned rate=44100,unsigned source=0) {
    std::vector<std::uint8_t> bytes(44+frames*2);
    std::memcpy(bytes.data(),"RIFF",4);PatternFilePut32(bytes.data()+4,bytes.size()-8);
    std::memcpy(bytes.data()+8,"WAVEfmt ",8);PatternFilePut32(bytes.data()+16,16);
    bytes[20]=1;bytes[22]=1;PatternFilePut32(bytes.data()+24,rate);PatternFilePut32(bytes.data()+28,rate*2);
    bytes[32]=2;bytes[34]=16;std::memcpy(bytes.data()+36,"data",4);PatternFilePut32(bytes.data()+40,frames*2);
    for(unsigned i=0;i<frames;++i) {const auto word=static_cast<std::uint16_t>(Fixture::Pcm(source,i));bytes[44+i*2]=word;bytes[45+i*2]=word>>8;}
    return bytes;
}
TEST_CASE(StreamingPreparationUsesPaddedChunkOffsetAndReadsOnlyWholeFrames) {
    Fixture f;
    const char* path="BroTracker/Samples/padded.wav";
    auto bytes=PickerWav(257);
    bytes.insert(bytes.begin()+36,{'J','U','N','K',1,0,0,0,42,0}); // One data byte and its pad.
    PatternFilePut32(bytes.data()+4,static_cast<std::uint32_t>(bytes.size()-8));
    FakePatternSd::files[path].bytes=bytes;
    CHECK(f.producer.BeginPrepareAsync(path));
    for(unsigned turn=0;turn<32 && !f.producer.PreparationDone();++turn) f.producer.PreparationStep();
    CHECK(f.producer.PreparationDone());
    CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::Ready);
    const auto view=f.producer.PreparedView();
    CHECK_EQ(view.frame_count,257u);
    CHECK(view.streaming!=nullptr);
    CHECK(FakePatternSd::reads.size()>=2);
    if(!view.streaming || FakePatternSd::reads.size()<2) return;
    CHECK_EQ(view.streaming->PrefixFrames(),257u);
    CHECK_EQ(FakePatternSd::reads[FakePatternSd::reads.size()-2].offset,54u);
    CHECK_EQ(FakePatternSd::reads[FakePatternSd::reads.size()-2].count,512u);
    CHECK_EQ(FakePatternSd::reads.back().offset,566u);
    CHECK_EQ(FakePatternSd::reads.back().count,2u);
    for(unsigned i=0;i<257;++i) CHECK_EQ(view.data[i],Fixture::Pcm(0,i));

    const char* malformed="BroTracker/Samples/incomplete-frame.wav";
    PatternFilePut32(bytes.data()+50,513); // The final byte is now RIFF padding.
    FakePatternSd::files[malformed].bytes=bytes;
    Pcm16MonoView rejected;
    CHECK_EQ(f.producer.PreparePath(malformed,rejected),InstrumentAvailability::PreparationFailed);
    CHECK(rejected.data==nullptr);
    CHECK(f.producer.BeginPrepareAsync(malformed));
    for(unsigned turn=0;turn<32 && !f.producer.PreparationDone();++turn) f.producer.PreparationStep();
    CHECK(f.producer.PreparationDone());
    CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::PreparationFailed);
    CHECK(f.producer.PreparedView().data==nullptr);
}
}
TEST_CASE(SelectedNonDevelopmentWavUsesRealValidationAndStreamingRefills) {
    Fixture fixture;
    const char* path="BroTracker/Samples/selected.wav";
    FakePatternSd::files[path].bytes=PickerWav();Pcm16MonoView view;
    CHECK_EQ(fixture.producer.PreparePath(path,view),InstrumentAvailability::Ready);
    CHECK_EQ(view.frame_count,10000u);CHECK_EQ(fixture.producer.SourceCount(),4u);
    CHECK_EQ(fixture.player.voices[0].Trigger(view,kSourceFrameStepUnity),VariableVoiceStatus::Success);
    fixture.producer.Publish(fixture.player);fixture.producer.Service();
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(path));
    CHECK_EQ(FakePatternSd::reads.back().offset,44u+kStreamStartFrames*2);
    std::int16_t pcm[128];
    for(unsigned block=0;block<48;++block) {
        const auto position=fixture.player.voices[0].GetFramePosition();
        CHECK_EQ(fixture.player.voices[0].Render(pcm,128),VariableVoiceStatus::Success);
        for(unsigned i=0;i<128;++i)CHECK_EQ(pcm[i],Fixture::Pcm(0,position+i));
        fixture.producer.Publish(fixture.player);fixture.producer.Service();
    }
    CHECK_EQ(fixture.player.voices[0].StreamSnapshot().underruns,0u);
    Pcm16MonoView cached;CHECK_EQ(fixture.producer.PreparePath(path,cached),InstrumentAvailability::Ready);
    CHECK_EQ(cached.streaming,view.streaming);CHECK_EQ(fixture.producer.SourceCount(),4u);
}
TEST_CASE(FailedWavPreparationAndBindingAppendDoNotDefineTheSlot) {
    Fixture fixture;InstrumentDefinitions definitions;auto bindings=SelectDevelopmentFixture({}).bindings;
    const char* path="BroTracker/Samples/invalid.wav";FakePatternSd::files[path].bytes=PickerWav(4096,22050);
    Pcm16MonoView view;
    CHECK_EQ(fixture.producer.PreparePath(path,view),InstrumentAvailability::PreparationFailed);
    CHECK_EQ(definitions.Count(),5u);CHECK_EQ(fixture.producer.SourceCount(),3u);CHECK(!view.data);
    FakePatternSd::files[path].bytes=PickerWav();FakePatternSd::short_read_path=path;
    CHECK_EQ(fixture.producer.PreparePath(path,view),InstrumentAvailability::PreparationFailed);
    CHECK_EQ(fixture.producer.SourceCount(),3u);FakePatternSd::short_read_path.clear();
    CHECK_EQ(fixture.producer.PreparePath(path,view),InstrumentAvailability::Ready);
    InstrumentDefineRequest request;request.expected=5;request.type=1;std::strcpy(request.resource,path);
    VariableRateSampleBinding prepared;prepared.sample=view;prepared.reference_note=60;
    CHECK_EQ(CommitInstrumentDefinition(definitions,bindings,request,true,[](const auto&){return false;},&prepared),5u);
    CHECK_EQ(definitions.Count(),5u);CHECK_EQ(bindings.count,2u);
}
TEST_CASE(RestoredMissingSelectedWavKeepsDefinitionSlotAndPatternReferences) {
    Fixture fixture;SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession source;source.working=MakeDevelopmentPattern();source.working.cells[0][0]={64,5};source.working_tempo=13725;
    const char* path="BroTracker/Samples/removed.wav";CHECK(source.definitions.Append(5,1,path));
    CHECK_EQ(store.Save(backend,source),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
    InstrumentAvailability availability=InstrumentAvailability::Undefined;
    const auto bindings=PrepareSessionBindings(restored.definitions,[&](unsigned id,const char* resource,Pcm16MonoView& sample) {
        const auto status=fixture.producer.PreparePath(resource,sample);if(id==5)availability=status;return status;
    });
    CHECK_EQ(availability,InstrumentAvailability::ResourceUnavailable);CHECK_EQ(restored.definitions.Count(),6u);
    CHECK_EQ(std::string(restored.definitions.At(5)->resource),std::string(path));CHECK_EQ(restored.working.cells[0][0].instrument,5u);
    CHECK_EQ(restored.working_tempo,13725u);CHECK_EQ(InstrumentBindingIdentity(bindings,5),0u);
}

TEST_CASE(RestoredSelectedWavIsPreparedBySavedPathOutsideDevelopmentAssets) {
    Fixture fixture;const char* path="BroTracker/Samples/restored.wav";FakePatternSd::files[path].bytes=PickerWav();
    SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
    WorkingSession source;source.working={};source.working.active_rows=16;source.working.active_channels=1;source.working.cells[0][0]={60,5};
    source.working_tempo=13725;CHECK(source.definitions.Append(5,1,path));CHECK_EQ(store.Save(backend,source),PatternFileResult::Ok);
    WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
    SdSampleStreaming::Storage reboot_memory{};SdSampleStreaming rebooted;DevelopmentWavInspection no_assets;
    rebooted.Prepare(true,reboot_memory,no_assets);InstrumentAvailability selected=InstrumentAvailability::Undefined;
    const auto bindings=PrepareSessionBindings(restored.definitions,[&](unsigned id,const char* resource,Pcm16MonoView& sample) {
        const auto result=rebooted.PreparePath(resource,sample);if(id==5)selected=result;return result;
    });
    CHECK_EQ(selected,InstrumentAvailability::Ready);CHECK_EQ(InstrumentBindingIdentity(bindings,5),1u);
    CHECK_EQ(restored.working_tempo,13725u);CHECK_EQ(restored.working.cells[0][0].instrument,5u);
    const auto& sample=bindings.bindings[bindings.count-1].sample;
    CHECK(sample.streaming!=nullptr);CHECK_EQ(sample.frame_count,10000u);
    for(unsigned i=0;i<kStreamStartFrames;++i)CHECK_EQ(sample.data[i],Fixture::Pcm(0,i));
}

TEST_CASE(BtmRoundtripPreservesProductionDevelopmentSampleSettingsAndRoutingByWavIdentity) {
    for(bool reordered:{false,true}) {
        Fixture fixture;
        const auto production=SelectDevelopmentFixture(fixture.producer.reports[1].view,
            fixture.producer.reports[2].view,fixture.producer.reports[0].view).bindings;
        CHECK_EQ(production.count,5u);
        WorkingSession session;session.working={};session.working.active_rows=16;session.working.active_channels=8;
        session.working_tempo=13725;
        // Compare ordinary slots 01/04/05, and repeat with the same WAVs in other
        // slots. Binding-table order and channel number must not determine routing.
        constexpr unsigned reordered_ids[]={4,2,0,1,3};
        if(reordered) {
            session.definitions.Reset(false);
            for(unsigned original:reordered_ids) {
                const auto* definition=DevelopmentInstrumentDefinition(original);
                CHECK(session.definitions.Append(session.definitions.Count(),InstrumentTypeCode(definition->type),definition->resource));
            }
        }
        constexpr unsigned channels[]={5,1,4,2,7};
        constexpr Note notes[]={60,62,64,72,48};
        for(unsigned slot=0;slot<session.definitions.Count();++slot)
            session.working.cells[0][channels[slot]]={notes[slot],static_cast<std::uint8_t>(slot)};
        SdPatternFiles backend;backend.ready=true;WorkingSessionStore<SdPatternFiles> store;
        CHECK_EQ(store.Save(backend,session),PatternFileResult::Ok);
        WorkingSession restored;CHECK_EQ(store.Load(backend,restored),PatternFileResult::Ok);
        CHECK_EQ(restored.working_tempo,session.working_tempo);
        unsigned prepared=0;
        const auto bindings=PrepareSessionBindings(restored.definitions,[&](unsigned,const char* path,Pcm16MonoView& sample) {
            ++prepared;return fixture.producer.PreparePath(path,sample);
        });
        CHECK_EQ(prepared,3u);CHECK_EQ(bindings.count,production.count);
        VariableRateSampleCommandPreparer original_player,restored_player;
        CHECK_EQ(original_player.Configure(44100,production),VariableSampleCommandStatus::Success);
        CHECK_EQ(restored_player.Configure(44100,bindings),VariableSampleCommandStatus::Success);
        for(unsigned slot=0;slot<restored.definitions.Count();++slot) {
            const auto original_id=reordered?reordered_ids[slot]:slot;
            const auto* definition=restored.definitions.At(slot);
            const auto* original_definition=DevelopmentInstrumentDefinition(original_id);
            CHECK_EQ(definition->type,original_definition->type);
            if(definition->resource)CHECK(std::strcmp(definition->resource,original_definition->resource)==0);
            const VariableRateSampleBinding* expected=nullptr;const VariableRateSampleBinding* actual=nullptr;
            for(unsigned i=0;i<production.count;++i)if(production.bindings[i].instrument==original_id)expected=&production.bindings[i];
            for(unsigned i=0;i<bindings.count;++i)if(bindings.bindings[i].instrument==slot)actual=&bindings.bindings[i];
            CHECK(expected!=nullptr);CHECK(actual!=nullptr);if(!expected || !actual)continue;
            CHECK_EQ(actual->instrument,slot);CHECK_EQ(actual->reference_note,expected->reference_note);
            CHECK_EQ(actual->sample_level_percent,expected->sample_level_percent);CHECK_EQ(actual->type,expected->type);
            CHECK_EQ(actual->sample.data,expected->sample.data);CHECK_EQ(actual->sample.frame_count,expected->sample.frame_count);
            CHECK_EQ(actual->sample.sample_rate_hz,expected->sample.sample_rate_hz);CHECK_EQ(actual->sample.streaming,expected->sample.streaming);

            const auto channel=channels[slot];const auto cell=restored.working.cells[0][channel];
            CHECK_EQ(cell.instrument,slot);CHECK_EQ(cell.note,notes[slot]);
            RowEventBatch events{};events.count=1;
            events.events[0]={static_cast<std::uint8_t>(channel),0,0,0,0,7+slot*5,cell.note,static_cast<std::uint8_t>(original_id)};
            VariableRateVoiceCommandBatch before{},after{};
            CHECK_EQ(original_player.Prepare(events,before),VariableSampleCommandStatus::Success);
            events.events[0].instrument=cell.instrument;
            CHECK_EQ(restored_player.Prepare(events,after),VariableSampleCommandStatus::Success);
            CHECK_EQ(before.count,1u);CHECK_EQ(after.count,1u);
            const auto& a=after.commands[0];const auto& b=before.commands[0];
            CHECK_EQ(a.channel,channel);CHECK_EQ(a.channel,b.channel);CHECK_EQ(a.sample_offset,b.sample_offset);
            CHECK_EQ(a.action,b.action);CHECK_EQ(a.synth_note,b.synth_note);CHECK_EQ(a.step_q32_32,b.step_q32_32);
            CHECK_EQ(a.sample_level_percent,b.sample_level_percent);CHECK_EQ(a.sample.data,b.sample.data);
            CHECK_EQ(a.sample.frame_count,b.sample.frame_count);CHECK_EQ(a.sample.sample_rate_hz,b.sample.sample_rate_hz);
            CHECK_EQ(a.sample.streaming,b.sample.streaming);
            CHECK_EQ(a.action,definition->type==InstrumentType::Sample?VariableRateVoiceAction::Trigger:
                definition->type==InstrumentType::MIDI?VariableRateVoiceAction::TriggerMidi:VariableRateVoiceAction::TriggerSynth);
        }
    }
}

namespace {
const VariableRateSampleBinding* CacheBinding(const VariableRateSampleBindings& bindings,unsigned id) {
    for(unsigned i=0;i<bindings.count;++i)if(bindings.bindings[i].instrument==id)return &bindings.bindings[i];return nullptr;
}
VariableRateSampleBinding CacheCandidate(Pcm16MonoView sample) {
    VariableRateSampleBinding binding;binding.reference_note=60;binding.sample_level_percent=80;binding.sample=sample;return binding;
}
InstrumentDefineRequest CacheRequest(unsigned slot,const std::string& path) {
    InstrumentDefineRequest request;request.expected=slot;request.type=1;std::strcpy(request.resource,path.c_str());return request;
}
}
TEST_CASE(RepeatedSampleReplacementReusesRetiredSlotsBeyondCacheCapacity) {
    Fixture fixture;InstrumentDefinitions definitions;
    auto bindings=SelectDevelopmentFixture(fixture.producer.reports[1].view,fixture.producer.reports[2].view,fixture.producer.reports[0].view).bindings;
    RealtimePattern pattern;pattern.active_rows=16;pattern.active_channels=1;pattern.cells[0][0]={60,0};
    VariableRatePatternPlayer player;CHECK_EQ(player.Configure(13725,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    unsigned successful=0;
    for(unsigned attempt=0;attempt<3*SdSampleStreaming::kSourceCapacity;++attempt) {
        const auto old=CacheBinding(bindings,0)->sample;const auto old_path=std::string(definitions.At(0)->resource);
        const auto first=old.data[0];const auto last=old.data[kStreamStartFrames-1];
        const auto path="BroTracker/Samples/replace_"+std::to_string(attempt)+".wav";
        FakePatternSd::files[path].bytes=PickerWav(10000,44100,attempt+10);Pcm16MonoView view;
        const auto result=fixture.producer.PreparePath(path.c_str(),view);CHECK_EQ(result,InstrumentAvailability::Ready);if(result!=InstrumentAvailability::Ready)break;
        // Candidate preparation must not evict or overwrite the current binding.
        CHECK_EQ(std::string(definitions.At(0)->resource),old_path);CHECK_EQ(CacheBinding(bindings,0)->sample.streaming,old.streaming);
        CHECK_EQ(old.data[0],first);CHECK_EQ(old.data[kStreamStartFrames-1],last);
        CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(0,path),CacheCandidate(view),true,[&](const auto& binding){return player.ReplaceSampleBinding(binding);}),0u);
        fixture.producer.Publish(player);fixture.producer.ReleaseUnused(bindings,true);
        CHECK_EQ(fixture.producer.SourceCount(),4u);CHECK_EQ(definitions.Count(),5u);CHECK_EQ(std::string(definitions.At(0)->resource),path);
        CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);std::int16_t pcm[128];
        CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);
        fixture.producer.Publish(player);fixture.producer.Service();
        CHECK_EQ(FakePatternSd::reads.back().path,path);CHECK_EQ(player.SampleVoice(0)->StreamSnapshot().sample,view.streaming);
        CHECK_EQ(player.SampleVoice(0)->StreamSnapshot().underruns,0u);
        player.Stop();fixture.producer.Publish(player);++successful;
    }
    CHECK_EQ(successful,3u*SdSampleStreaming::kSourceCapacity);ReadReleased();
}
TEST_CASE(SharedDynamicSourceRemainsPlayableAndDoesNotMoveAcrossReclamation) {
    Fixture fixture;InstrumentDefinitions definitions;auto bindings=SelectDevelopmentFixture(fixture.producer.reports[1].view).bindings;
    RealtimePattern pattern;pattern.active_rows=16;pattern.active_channels=2;pattern.cells[0][0]={60,0};pattern.cells[0][1]={60,5};
    VariableRatePatternPlayer player;CHECK_EQ(player.Configure(13725,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    const std::string shared="BroTracker/Samples/shared.wav",other="BroTracker/Samples/other.wav";
    FakePatternSd::files[shared].bytes=PickerWav(10000,44100,8);FakePatternSd::files[other].bytes=PickerWav(10000,44100,9);
    Pcm16MonoView shared_view,other_view;CHECK_EQ(fixture.producer.PreparePath(shared.c_str(),shared_view),InstrumentAvailability::Ready);
    CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(0,shared),CacheCandidate(shared_view),true,[&](const auto& b){return player.ReplaceSampleBinding(b);}),0u);
    const auto alias=CacheRequest(5,shared);const auto prepared=CacheCandidate(shared_view);
    CHECK_EQ(CommitInstrumentDefinition(definitions,bindings,alias,true,[&](const auto& b){return player.AppendInstrumentBinding(b);},&prepared),0u);
    CHECK_EQ(fixture.producer.PreparePath(other.c_str(),other_view),InstrumentAvailability::Ready);
    CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(0,other),CacheCandidate(other_view),true,[&](const auto& b){return player.ReplaceSampleBinding(b);}),0u);
    fixture.producer.Publish(player);fixture.producer.ReleaseUnused(bindings,true);
    CHECK_EQ(fixture.producer.SourceCount(),5u);CHECK_EQ(CacheBinding(bindings,5)->sample.streaming,shared_view.streaming);
    CHECK_EQ(shared_view.streaming->start,shared_view.data);
    for(unsigned i=0;i<kStreamStartFrames;++i)CHECK_EQ(shared_view.data[i],Fixture::Pcm(8,i));
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);std::int16_t pcm[128];
    for(unsigned block=0;block<48;++block) {
        CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);fixture.producer.Publish(player);fixture.producer.Service();
    }
    CHECK_EQ(player.SampleVoice(1)->StreamSnapshot().sample,shared_view.streaming);CHECK_EQ(player.SampleVoice(1)->StreamSnapshot().underruns,0u);
    CHECK(std::any_of(FakePatternSd::reads.begin(),FakePatternSd::reads.end(),[&](const auto& read){return read.path==shared && read.offset>=44+kStreamStartFrames*2;}));
    player.Stop();fixture.producer.Publish(player);
    // Both slots now share the higher descriptor. Free the lower slot without
    // moving it: Service must find live descriptors beyond the occupied count.
    CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(5,other),CacheCandidate(other_view),true,[&](const auto& b){return player.ReplaceSampleBinding(b);}),0u);
    fixture.producer.Publish(player);fixture.producer.ReleaseUnused(bindings,true);
    CHECK_EQ(fixture.producer.SourceCount(),4u);CHECK_EQ(CacheBinding(bindings,0)->sample.streaming,other_view.streaming);CHECK_EQ(CacheBinding(bindings,5)->sample.streaming,other_view.streaming);
    const auto* address=other_view.streaming;const auto* prefix=other_view.data;
    Pcm16MonoView reused;const char* third="BroTracker/Samples/third.wav";FakePatternSd::files[third].bytes=PickerWav(10000,44100,10);
    CHECK_EQ(fixture.producer.PreparePath(third,reused),InstrumentAvailability::Ready);CHECK_EQ(reused.streaming,shared_view.streaming);
    CHECK_EQ(other_view.streaming,address);CHECK_EQ(other_view.data,prefix);fixture.producer.DiscardLast(third);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);
    fixture.producer.Publish(player);fixture.producer.Service();CHECK_EQ(FakePatternSd::reads.back().path,other);
    CHECK_EQ(definitions.Count(),6u);ReadReleased();
}
TEST_CASE(FailedReplacementRetainsPathBindingAndReadableOldSource) {
    Fixture fixture;InstrumentDefinitions definitions;auto bindings=SelectDevelopmentFixture(fixture.producer.reports[1].view).bindings;
    RealtimePattern pattern;pattern.active_rows=16;pattern.active_channels=1;pattern.cells[0][0]={60,0};
    VariableRatePatternPlayer player;CHECK_EQ(player.Configure(13725,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    const std::string old_path="BroTracker/Samples/retained.wav",bad_path="BroTracker/Samples/rejected.wav";
    FakePatternSd::files[old_path].bytes=PickerWav(10000,44100,11);Pcm16MonoView retained;
    CHECK_EQ(fixture.producer.PreparePath(old_path.c_str(),retained),InstrumentAvailability::Ready);
    CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(0,old_path),CacheCandidate(retained),true,[&](const auto& b){return player.ReplaceSampleBinding(b);}),0u);
    fixture.producer.Publish(player);fixture.producer.ReleaseUnused(bindings,true);
    FakePatternSd::files[bad_path].bytes=PickerWav(10000,22050);Pcm16MonoView candidate;
    CHECK_EQ(fixture.producer.PreparePath(bad_path.c_str(),candidate),InstrumentAvailability::PreparationFailed);
    FakePatternSd::files[bad_path].bytes=PickerWav(10000,44100,12);FakePatternSd::short_read_path=bad_path;
    CHECK_EQ(fixture.producer.PreparePath(bad_path.c_str(),candidate),InstrumentAvailability::PreparationFailed);FakePatternSd::short_read_path.clear();
    CHECK_EQ(fixture.producer.PreparePath(bad_path.c_str(),candidate),InstrumentAvailability::Ready);
    CHECK_EQ(RelinkSampleDefinition(definitions,bindings,CacheRequest(0,bad_path),CacheCandidate(candidate),true,[](const auto&){return false;}),5u);
    fixture.producer.DiscardLast(bad_path.c_str());fixture.producer.ReleaseUnused(bindings,true);
    CHECK_EQ(fixture.producer.SourceCount(),4u);CHECK_EQ(std::string(definitions.At(0)->resource),old_path);
    CHECK_EQ(CacheBinding(bindings,0)->sample.data,retained.data);CHECK_EQ(CacheBinding(bindings,0)->sample.streaming,retained.streaming);
    for(unsigned i=0;i<kStreamStartFrames;++i)CHECK_EQ(retained.data[i],Fixture::Pcm(11,i));
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);std::int16_t pcm[128];
    for(unsigned block=0;block<48;++block){CHECK_EQ(player.Render(pcm,128),VariableRatePatternPlayerStatus::Success);fixture.producer.Publish(player);fixture.producer.Service();}
    CHECK_EQ(player.SampleVoice(0)->StreamSnapshot().sample,retained.streaming);CHECK_EQ(player.SampleVoice(0)->StreamSnapshot().underruns,0u);
    CHECK_EQ(FakePatternSd::reads.back().path,old_path);CHECK_EQ(definitions.Count(),5u);ReadReleased();
}
TEST_CASE(DynamicSourceReclamationIsStoppedOnlyAndPinsRetainedVoiceSnapshots) {
    Fixture fixture;const char* path="BroTracker/Samples/pinned.wav";FakePatternSd::files[path].bytes=PickerWav();Pcm16MonoView view;
    CHECK_EQ(fixture.producer.PreparePath(path,view),InstrumentAvailability::Ready);
    VariableRateSampleBindings no_bindings;
    fixture.producer.ReleaseUnused(no_bindings,false);CHECK_EQ(fixture.producer.SourceCount(),4u);
    CHECK_EQ(fixture.player.voices[0].Trigger(view,kSourceFrameStepUnity),VariableVoiceStatus::Success);fixture.producer.Publish(fixture.player);
    fixture.producer.ReleaseUnused(no_bindings,true);CHECK_EQ(fixture.producer.SourceCount(),4u);
    fixture.player.voices[0].Stop(); // Stale published spans still pin the descriptor.
    fixture.producer.ReleaseUnused(no_bindings,true);CHECK_EQ(fixture.producer.SourceCount(),4u);
    fixture.producer.Publish(fixture.player);fixture.producer.ReleaseUnused(no_bindings,true);CHECK_EQ(fixture.producer.SourceCount(),3u);
    ReadReleased();
}

TEST_CASE(FullyRetainedCacheRejectsCandidateWithoutEvictingTheCurrentSource) {
    Fixture fixture;InstrumentDefinitions definitions(false);VariableRateSampleBindings bindings;
    for(unsigned i=0;i<SdSampleStreaming::kSourceCapacity-3;++i) {
        const auto path="BroTracker/Samples/live_"+std::to_string(i)+".wav";FakePatternSd::files[path].bytes=PickerWav(10000,44100,i);
        Pcm16MonoView view;CHECK_EQ(fixture.producer.PreparePath(path.c_str(),view),InstrumentAvailability::Ready);
        CHECK(definitions.Append(i,1,path.c_str()));auto binding=CacheCandidate(view);binding.instrument=i;bindings.bindings[bindings.count++]=binding;
    }
    fixture.producer.ReleaseUnused(bindings,true);CHECK_EQ(fixture.producer.SourceCount(),SdSampleStreaming::kSourceCapacity);
    const auto retained=bindings.bindings[0];const auto path=std::string(definitions.At(0)->resource);
    const char* candidate_path="BroTracker/Samples/no_spare.wav";FakePatternSd::files[candidate_path].bytes=PickerWav();
    auto candidate=retained.sample;
    CHECK_EQ(fixture.producer.PreparePath(candidate_path,candidate),InstrumentAvailability::PreparationFailed);
    CHECK_EQ(candidate.streaming,retained.sample.streaming);CHECK_EQ(candidate.data,retained.sample.data);
    CHECK_EQ(std::string(definitions.At(0)->resource),path);CHECK_EQ(bindings.bindings[0].sample.streaming,retained.sample.streaming);
    CHECK_EQ(retained.sample.data[0],Fixture::Pcm(0,0));CHECK_EQ(retained.sample.data[kStreamStartFrames-1],Fixture::Pcm(0,kStreamStartFrames-1));
    CHECK_EQ(fixture.producer.SourceCount(),SdSampleStreaming::kSourceCapacity);ReadReleased();
}

TEST_CASE(CooperativePreparationYieldsBetweenReadsAndKeepsRollbackAndVoiceSourcesPinned) {
    Fixture f;const char* old_path="BroTracker/Samples/original.wav";const char* path="BroTracker/Samples/cooperative.wav";
    FakePatternSd::files[old_path].bytes=PickerWav(10000,44100,7);FakePatternSd::files[path].bytes=PickerWav(10000,44100,8);
    Pcm16MonoView original;CHECK_EQ(f.producer.PreparePath(old_path,original),InstrumentAvailability::Ready);
    VariableRateSampleBindings originals;originals.count=1;originals.bindings[0]=CacheCandidate(original);f.producer.PinRollback(originals);
    VariableRateSampleBindings none;CHECK(!f.producer.ReclaimOne(none));
    CHECK(f.producer.BeginPrepareAsync(path));f.Trigger(0,0);f.Trigger(1,1);
    unsigned steps=0;
    while(!f.producer.PreparationDone() && steps++<200) {
        f.producer.Service();f.producer.Service();
        const auto count=FakePatternSd::reads.size();
        if(f.producer.BackgroundHeadroom())f.producer.PreparationStep();
        CHECK(FakePatternSd::reads.size()-count<=1u);
        if(FakePatternSd::reads.size()>count)CHECK(FakePatternSd::reads.back().count<=512u);
        f.Consume(0,0,128);f.Consume(1,1,128);ReadReleased();
    }
    CHECK(steps>8);CHECK(f.producer.PreparationDone());CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::Ready);
    CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,0u);CHECK_EQ(f.player.voices[1].StreamSnapshot().underruns,0u);
    const auto loaded=f.producer.PreparedView();CHECK(loaded.streaming!=original.streaming);
    CHECK_EQ(original.data[0],Fixture::Pcm(7,0));
    // Original is no longer bound, but rollback and then a live voice retain it.
    f.producer.UnpinRollback();CHECK_EQ(f.player.voices[2].Trigger(original,kSourceFrameStepUnity),VariableVoiceStatus::Success);f.producer.Publish(f.player);
    VariableRateSampleBindings candidate;candidate.count=1;candidate.bindings[0]=CacheCandidate(loaded);
    CHECK(!f.producer.ReclaimOne(candidate));f.player.voices[2].Stop();f.producer.Publish(f.player);
    CHECK(f.producer.ReclaimOne(candidate));CHECK_EQ(f.producer.SourceCount(),4u);
}

TEST_CASE(CooperativePreparationCancellationFailureAndFullPinnedCacheNeverEvictTheCurrentBinding) {
    Fixture f;const char* path="BroTracker/Samples/cancel.wav";FakePatternSd::files[path].bytes=PickerWav();
    const auto original=f.producer.reports[0].view.Mono();
    CHECK(f.producer.BeginPrepareAsync(path));
    for(unsigned i=0;i<5;++i)f.producer.PreparationStep();
    f.producer.CancelPreparation();f.producer.PreparationStep();CHECK(f.producer.PreparationDone());CHECK_EQ(f.producer.SourceCount(),3u);
    FakePatternSd::files[path].bytes=PickerWav(10000,48000);CHECK(f.producer.BeginPrepareAsync(path));
    for(unsigned i=0;i<30 && !f.producer.PreparationDone();++i)f.producer.PreparationStep();
    CHECK(f.producer.PreparationDone());CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::PreparationFailed);CHECK_EQ(f.producer.SourceCount(),3u);
    VariableRateSampleBindings pins;
    for(unsigned i=3;i<SdSampleStreaming::kSourceCapacity;++i) {
        const auto p="BroTracker/Samples/pin_"+std::to_string(i)+".wav";FakePatternSd::files[p].bytes=PickerWav();Pcm16MonoView v;
        CHECK_EQ(f.producer.PreparePath(p.c_str(),v),InstrumentAvailability::Ready);auto b=CacheCandidate(v);b.instrument=i;pins.bindings[pins.count++]=b;
    }
    f.producer.PinRollback(pins);VariableRateSampleBindings none;CHECK(!f.producer.ReclaimOne(none));
    CHECK(f.producer.BeginPrepareAsync(path));CHECK(f.producer.PreparationDone());CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::PreparationFailed);
    CHECK_EQ(f.producer.reports[0].view.data,original.data);CHECK_EQ(f.producer.SourceCount(),16u);ReadReleased();
}

TEST_CASE(CooperativeBrowserInspectsChunksOneReadPerTurnAndRetainsCompletePageSemantics) {
    Fixture f;FakePatternSd::files["/BroTracker/Samples"].directory=true;
    FakePatternSd::files["/BroTracker/Samples/tone.wav"].bytes=PickerWav();
    SdWavBrowseJob job;WavOpenRequest request;request.version=3;request.request=1;std::strcpy(request.directory,"/BroTracker/Samples");
    CHECK(job.Begin(request));CHECK(!job.Begin(request));unsigned steps=0;
    while(!job.Done() && steps++<100) {
        const auto reads=FakePatternSd::reads.size();job.Step();CHECK(FakePatternSd::reads.size()-reads<=1u);
        if(FakePatternSd::reads.size()>reads)CHECK(FakePatternSd::reads.back().count<=18u);
        ReadReleased();
    }
    CHECK(job.Done());CHECK_EQ(job.Result(),WavDirectoryResult::Ok);CHECK_EQ(job.Page().count,1u);
    CHECK_EQ(job.Page().entries[0].facts.status,WavBufferStatus::Success);CHECK_EQ(job.Page().entries[0].facts.sample_rate,44100u);
}

TEST_CASE(ManualAuditionStreamsLongWavToEofAndRetriggersFromZeroWithoutPatternBindings) {
    Fixture f;SdWavAudition audition(f.producer);VariableRateSampleBindings bindings;
    constexpr unsigned frames=100000;const char* path="BroTracker/Samples/long.wav";
    FakePatternSd::files[path].bytes=PickerWav(frames,44100,4);
    const auto prepare=[&](unsigned id) {
        CHECK(audition.Request(id,path));
        for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
        CHECK(!audition.Busy());CHECK(audition.Snapshot().sample);CHECK_EQ(audition.Snapshot().position,0u);
        CHECK_EQ(audition.Snapshot().step,kSourceFrameStepUnity);
    };
    const auto consume=[&](unsigned at,unsigned count) {
        f.producer.Service();std::int16_t pcm[128]{};
        CHECK(audition.Render(pcm,count));
        for(unsigned i=0;i<count;++i)CHECK_EQ(pcm[i],Fixture::Pcm(4,at+i));
    };
    prepare(1);for(unsigned at=0;at<8192;at+=128)consume(at,128);
    CHECK(audition.Snapshot().sample);prepare(2); // replace active stream, restart frame zero
    for(unsigned at=0;at<frames;at+=128)consume(at,std::min(128u,frames-at));
    CHECK(!audition.Snapshot().sample);CHECK_EQ(audition.Snapshot().underruns,0u);
    CHECK_EQ(audition.Snapshot().eofs,1u);CHECK_EQ(bindings.count,0u);
    // Neither retrigger nor EOF leaks dynamic sources.
    while(f.producer.ReclaimOne(bindings)){}CHECK_EQ(f.producer.SourceCount(),3u);
}
TEST_CASE(ManualAuditionCancellationAndNewerRequestsFenceLatePreparation) {
    Fixture f;SdWavAudition audition(f.producer);VariableRateSampleBindings bindings;
    const char* old="BroTracker/Samples/old.wav";const char* fresh="BroTracker/Samples/new.wav";
    FakePatternSd::files[old].bytes=PickerWav(10000,44100,5);
    FakePatternSd::files[fresh].bytes=PickerWav(10000,44100,6);
    CHECK(audition.Request(1,old));
    for(unsigned i=0;i<5;++i)audition.Service(bindings);
    CHECK(audition.Request(2,fresh));
    for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
    std::int16_t pcm[128]{};CHECK(audition.Render(pcm,128));CHECK_EQ(pcm[0],Fixture::Pcm(6,0));
    audition.Cancel();CHECK(!audition.Snapshot().sample);
    CHECK(audition.Request(3,old));for(unsigned i=0;i<5;++i)audition.Service(bindings);
    audition.Cancel();for(unsigned i=0;i<100;++i)audition.Service(bindings);
    CHECK(!audition.Snapshot().sample);CHECK(!audition.Busy());
    CHECK(audition.Request(4,fresh));
    audition.CancelThrough(4);
    for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
    CHECK(!audition.Snapshot().sample);CHECK(!audition.Busy());
    CHECK(audition.Request(5,fresh));for(unsigned i=0;i<100;++i)audition.Service(bindings);
    CHECK(audition.Snapshot().sample);audition.CancelThrough(4);CHECK(audition.Render(pcm,128));audition.CancelThrough(5);CHECK(!audition.Snapshot().sample);
    // Cached preparation can complete immediately. Cancellation before its
    // publication turn still prevents that successful result from starting PCM.
    CHECK(audition.Request(7,fresh));audition.Service(bindings);audition.Cancel();
    for(unsigned i=0;i<100;++i)audition.Service(bindings);
    CHECK(!audition.Snapshot().sample);
    FakePatternSd::files[old].bytes=PickerWav(10000,48000);
    CHECK(audition.Request(8,old));for(unsigned i=0;i<100;++i)audition.Service(bindings);
    CHECK(!audition.Snapshot().sample);
    while(f.producer.ReclaimOne(bindings)){}CHECK_EQ(f.producer.SourceCount(),3u);
}

TEST_CASE(AuditionRefillYieldsToPatternAndWaitsForItsReserve) {
    Fixture f;SdWavAudition audition(f.producer);VariableRateSampleBindings bindings;
    const char* path="BroTracker/Samples/preview.wav";
    FakePatternSd::files[path].bytes=PickerWav();
    CHECK(audition.Request(1,path));
    for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
    std::int16_t preview[128]{};
    CHECK(audition.Render(preview,128));
    f.Trigger();
    FakePatternSd::reads.clear();
    f.producer.Service();
    CHECK_EQ(FakePatternSd::reads.size(),1u);
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(kDevelopmentWavPaths[0]));
    f.producer.Service(8192);
    CHECK_EQ(FakePatternSd::reads.size(),1u);
    f.producer.Service();
    CHECK_EQ(FakePatternSd::reads.size(),2u);
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(path));
}

TEST_CASE(ProgressiveBrowserPublishesUsableBatchesBefore306FileScanCompletesAndNeverMovesSelection) {
    Fixture f;const char* dir="/Samples/Wav-LQ/Acid";FakePatternSd::files[dir].directory=true;
    FakePatternSd::files[std::string(dir)+"/nested"].directory=true;
    FakePatternSd::files[std::string(dir)+"/ignore.txt"].bytes={0};
    for(unsigned i=0;i<306;++i){char name[96];std::snprintf(name,sizeof(name),"%s/tone_%03u.wav",dir,i);FakePatternSd::files[name].bytes=PickerWav(16);}
    WavBrowser browser;browser.Begin(dir);SdWavBrowseJob job;WavOpenRequest request;request.version=3;std::strcpy(request.directory,dir);
    unsigned batches=0,first_selected=0,first_scroll=0;
    do {
        ++request.request;CHECK(job.Begin(request));unsigned steps=0;
        while(!job.Done() && steps++<400) {
            const auto reads=FakePatternSd::reads.size();job.Step();CHECK(FakePatternSd::reads.size()-reads<=1u);
            ReadReleased();
        }
        CHECK(job.Done());CHECK(job.Result()==WavDirectoryResult::Ok);
        const auto page=job.Page();CHECK(page.count<=kPatternListPageSize);CHECK(page.scanned<=64);CHECK(browser.Accept(page));job.Consumed();++batches;
        if(batches==1) {
            CHECK(!browser.Loading());CHECK(browser.Reading());CHECK(browser.SamplesRead()>1);CHECK(browser.SamplesRead()<306);
            for(unsigned i=0;i<3;++i)browser.Move(1);
            std::string path;CHECK_EQ(browser.Select(path),2u);CHECK(path.find("Samples/Wav-LQ/Acid/tone_")==0);
            CHECK(browser.Message().find("samples read")!=std::string::npos);CHECK(browser.Message().find("PCM")!=std::string::npos);
        }
        if(batches==3) {for(unsigned i=0;i<21;++i)browser.Move(1);first_selected=browser.View().selected;first_scroll=browser.View().first;CHECK(browser.Reading());}
        if(batches>3){CHECK_EQ(browser.View().selected,first_selected);CHECK_EQ(browser.View().first,first_scroll);}
        request.offset=page.next;if(page.done)break;
    }while(batches<100);
    CHECK(batches>3);CHECK(!browser.Reading());CHECK_EQ(browser.SamplesRead(),306u);CHECK_EQ(browser.View().names.size(),308u);
    CHECK(browser.Message().find("306 samples read")!=std::string::npos);
}
TEST_CASE(CooperativeBrowserSupersedesMidMetadataAndReleasesOldHandlesBeforeOpeningNewDirectory) {
    Fixture f;FakePatternSd::files["/old"].directory=true;FakePatternSd::files["/new"].directory=true;
    FakePatternSd::files["/old/old.wav"].bytes=PickerWav(16);FakePatternSd::files["/new/new.wav"].bytes=PickerWav(16);
    SdWavBrowseJob job;WavOpenRequest request;request.version=3;request.request=1;std::strcpy(request.directory,"/old");
    CHECK(job.Begin(request));for(unsigned i=0;i<7;++i)job.Step();
    request.request=2;std::strcpy(request.directory,"/new");CHECK(job.Begin(request));
    for(unsigned i=0;i<200 && !job.Done();++i)job.Step();
    CHECK(job.Done());CHECK_EQ(job.Request().request,2u);CHECK_EQ(std::string(job.Page().directory),std::string("/new"));
    CHECK_EQ(job.Page().count,1u);CHECK_EQ(std::string(job.Page().entries[0].name),std::string("new.wav"));ReadReleased();
}

TEST_CASE(CooperativeEightWavBatchKeepsActiveStreamingVoiceRefilled) {
    Fixture f(100000);FakePatternSd::files["/batch"].directory=true;
    for(unsigned i=0;i<8;++i)FakePatternSd::files["/batch/file_"+std::to_string(i)+".wav"].bytes=PickerWav(16);
    f.Trigger();SdWavBrowseJob job;WavOpenRequest request;request.version=3;request.request=1;std::strcpy(request.directory,"/batch");CHECK(job.Begin(request));
    unsigned turns=0;
    while(!job.Done() && turns++<500) {
        f.producer.Service();f.producer.Service();
        if(!f.producer.RefillNeeded() && f.producer.BackgroundHeadroom()) {
            const auto reads=FakePatternSd::reads.size();job.Step();CHECK(FakePatternSd::reads.size()-reads<=1u);
        }
        f.Consume(0,0,128);
    }
    CHECK(job.Done());CHECK_EQ(job.Page().count,8u);CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,0u);
}

TEST_CASE(LongSpacedResourceIsSharedByDefinitionsCacheAndRollbackAndStreamsThroughEof) {
    Fixture f;const auto base=ResourcePaths().UsedBytes();
    const auto path="Samples/Jungle riddims/"+std::string(80,'a')+"/long sample.wav";
    FakePatternSd::files[path].bytes=PickerWav(10000,44100,7);
    InstrumentDefinitions defs(false);CHECK(defs.Append(0,1,path.c_str()));CHECK(defs.Append(1,1,path.c_str()));
    CHECK_EQ(defs.At(0)->resource,defs.At(1)->resource);CHECK_EQ(ResourcePaths().UsedBytes(),base+path.size()+1);
    Pcm16MonoView view;CHECK_EQ(f.producer.PreparePath(path.c_str(),view),InstrumentAvailability::Ready);
    CHECK_EQ(ResourcePaths().UsedBytes(),base+path.size()+1); // cache holds the same interned path
    VariableRateSampleBindings bindings;bindings.count=1;bindings.bindings[0]=CacheCandidate(view);
    LiveInstrumentVisit visit;CHECK(visit.Begin(1,defs,bindings));f.producer.PinRollback(bindings);
    CHECK(defs.Replace(0,2,nullptr));CHECK(defs.Replace(1,3,nullptr));
    CHECK_EQ(visit.Restore(0,defs,bindings,[](unsigned,const auto*){return true;}),0u);
    CHECK_EQ(std::string(defs.At(0)->resource),path);CHECK(visit.End(1));f.producer.UnpinRollback();
    CHECK_EQ(f.player.voices[0].Trigger(view,kSourceFrameStepUnity),VariableVoiceStatus::Success);f.producer.Publish(f.player);
    defs.Reset(false);bindings={};CHECK(!f.producer.ReclaimOne(bindings)); // voice retains file identity
    for(unsigned at=0;at<10000;at+=128){f.producer.Service();std::int16_t pcm[128]{};const auto n=std::min(128u,10000-at);CHECK_EQ(f.player.voices[0].Render(pcm,n),VariableVoiceStatus::Success);for(unsigned i=0;i<n;++i)CHECK_EQ(pcm[i],Fixture::Pcm(7,at+i));f.producer.Publish(f.player);}
    CHECK_EQ(f.player.voices[0].StreamSnapshot().underruns,0u);
    while(f.producer.ReclaimOne(bindings)){}CHECK_EQ(ResourcePaths().UsedBytes(),base);
}
TEST_CASE(PathPoolHasBoundedActualLengthStorageAndRejectsCapacityWithoutChangingDefinition) {
    ResourcePathPool pool;const auto used=pool.UsedBytes();
    const auto id=pool.Acquire("Samples/shared.wav");CHECK(id);CHECK_EQ(pool.Acquire("Samples/shared.wav"),id);CHECK_EQ(pool.UniqueCount(),1u);
    pool.Release(id);CHECK_EQ(pool.UniqueCount(),1u);pool.Release(id);CHECK_EQ(pool.UsedBytes(),used);
    ResourcePathPool::Id ids[256]{};unsigned n=0;
    for(;n<256;++n){const auto path="Samples/"+std::to_string(n)+"/"+std::string(220,'x')+".wav";ids[n]=pool.Acquire(path.c_str());if(!ids[n])break;}
    CHECK(n>20 && n<99);CHECK(pool.UsedBytes()<=kResourcePathBytes);for(unsigned i=0;i<n;++i)pool.Release(ids[i]);CHECK_EQ(pool.UsedBytes(),0u);
    CHECK(pool.Acquire("Samples/reused.wav"));
    Fixture f;InstrumentDefinitions defs(false);CHECK(defs.Append(0,1,"Samples/old.wav"));const auto original=defs.At(0)->resource;
    std::vector<ResourcePathRef> pressure;
    for(unsigned i=0;i<256;++i){ResourcePathRef ref;const auto path="Samples/pressure_"+std::to_string(i)+"/"+std::string(220,'x')+".wav";if(!ref.Assign(path.c_str()))break;pressure.push_back(ref);}
    const auto candidate="Samples/"+std::string(240,'z')+".wav";
    CHECK(!defs.Replace(0,1,candidate.c_str()));CHECK_EQ(defs.At(0)->resource,original);CHECK_EQ(std::string(original),std::string("Samples/old.wav"));
    Pcm16MonoView rejected;CHECK_EQ(f.producer.PreparePath(candidate.c_str(),rejected),InstrumentAvailability::PathCapacity);
    InstrumentDefineRequest request;request.request=1;request.expected=0;request.type=1;std::strcpy(request.resource,candidate.c_str());
    VariableRateSampleBindings bindings;bindings.count=1;bindings.bindings[0]=CacheCandidate(f.producer.reports[0].view.Mono());bool installed=false;
    CHECK_EQ(RelinkSampleDefinition(defs,bindings,request,bindings.bindings[0],true,[&](const auto&){installed=true;return true;}),9u);
    CHECK(!installed);CHECK_EQ(defs.At(0)->resource,original);
    pressure.clear();CHECK(defs.Replace(0,1,candidate.c_str()));
}
TEST_CASE(LongPathCodecIsVersionedBoundedAndDoesNotTruncateOrAcceptTraversal) {
    const auto relative="Samples/Jungle riddims/"+std::string(210,'b')+".wav";
    char token[kPathProtocolCapacity],path[kSamplePathMaximum+1];CHECK(relative.size()>63);CHECK(EncodePathToken(relative.c_str(),token,sizeof(token)));
    CHECK(DecodePathToken(token,path,sizeof(path),true));CHECK_EQ(std::string(path),relative);
    WavOpenRequest request;const auto directory="/Samples/Jungle riddims/"+std::string(100,'d');CHECK(EncodePathToken(directory.c_str(),token,sizeof(token)));
    const auto line=std::string("BTSTORE1 WAVOPEN 4 1 0 0 ")+token;CHECK(ParseWavOpen(line.data(),line.size(),request));CHECK_EQ(std::string(request.directory),directory);
    CHECK(!DecodePathToken("@610062",path,sizeof(path),true));CHECK(!ValidSamplePath("Samples/../escape.wav"));CHECK(!ValidSamplePath("Samples//empty.wav"));
    const auto maximum="Samples/"+std::string(243,'a')+".wav";CHECK_EQ(maximum.size(),255u);CHECK(InstrumentDefinitions::ValidResource(maximum.c_str()));
    CHECK(!InstrumentDefinitions::ValidResource((maximum+"x").c_str()));CHECK(!EncodePathToken((maximum+"x").c_str(),token,sizeof(token)));
    InstrumentDefineRequest update;CHECK(EncodePathToken(relative.c_str(),token,sizeof(token)));const auto edit=std::string("BTINS1 EDIT 2 2 0 1 ")+token;CHECK(ParseInstrumentEdit(edit.data(),edit.size(),update));CHECK_EQ(std::string(update.resource),relative);
    InstrumentDefinitions defs(false);CHECK(defs.Append(0,1,relative.c_str()));const auto facts=InstrumentDefinitionFacts(0,defs.At(0),InstrumentAvailability::Ready);char reply[kPathProtocolCapacity];CHECK(FormatInstrumentFacts(facts,reply,sizeof(reply),3));InstrumentFacts parsed;CHECK(ParseInstrumentFacts(reply,std::strlen(reply)-1,parsed));CHECK_EQ(std::string(parsed.resource),relative);
    CHECK(!FormatInstrumentFacts(facts,reply,sizeof(reply),2)); // legacy must never truncate
}

TEST_CASE(ResourceEntryLimitAndFreedGapReuseRemainBoundedBelowByteBudget) {
    ResourcePathPool pool;ResourcePathPool::Id ids[kResourcePathEntries]{};
    for(unsigned i=0;i<kResourcePathEntries;++i){const auto path="Samples/s"+std::to_string(i)+".wav";ids[i]=pool.Acquire(path.c_str());CHECK(ids[i]);}
    CHECK_EQ(pool.UniqueCount(),kResourcePathEntries);CHECK(pool.UsedBytes()<kResourcePathBytes);
    CHECK_EQ(pool.Acquire("Samples/overflow.wav"),0u);
    pool.Release(ids[50]);const auto next=pool.Acquire("Samples/new.wav");CHECK(next);CHECK_EQ(next,ids[50]);
    CHECK_EQ(std::string(pool.Resolve(ids[51])),std::string("Samples/s51.wav"));
}
namespace {
std::vector<std::uint8_t> StereoPickerWav(unsigned frames,unsigned rate=44100);
TEST_CASE(AuditionAddsOnceToEachPatternChannelWithSaturationAndStopsAtEof) {
    Fixture f;SdWavAudition audition(f.producer);VariableRateSampleBindings bindings;
    const char* path="BroTracker/Samples/stereo-preview.wav";
    FakePatternSd::files[path].bytes=StereoPickerWav(2);
    CHECK(audition.Request(1,path));
    for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
    std::int16_t left[4]{1200,32000,1234,-5678},right[4]{1000,-31000,-2345,6789};
    std::int16_t preview_l[4]{},preview_r[4]{};
    CHECK(audition.Render(preview_l,4,preview_r));
    CHECK(!audition.Snapshot().sample);
    CHECK(audition.Request(2,path));
    for(unsigned i=0;i<200 && audition.Busy();++i)audition.Service(bindings);
    audition.Mix(left,right);
    CHECK_EQ(left[0],std::clamp(1200+preview_l[0],-32768,32767));
    CHECK_EQ(left[1],32767);
    CHECK_EQ(right[0],std::clamp(1000+preview_r[0],-32768,32767));
    CHECK_EQ(right[1],-32768);
    CHECK_EQ(left[2],1234);CHECK_EQ(left[3],-5678);
    CHECK_EQ(right[2],-2345);CHECK_EQ(right[3],6789);
    CHECK(!audition.Snapshot().sample);
}

std::vector<std::uint8_t> StereoPickerWav(unsigned frames,unsigned rate) {
    auto wav=PickerWav(frames,rate);
    wav.resize(44+4*frames);
    PatternFilePut32(wav.data()+4,36+4*frames);
    wav[22]=2;PatternFilePut32(wav.data()+28,4*rate);wav[32]=4;
    PatternFilePut32(wav.data()+40,4*frames);
    for(unsigned i=0;i<frames;++i) {
        const std::uint16_t left=1200,right=static_cast<std::uint16_t>(-2700);
        wav[44+4*i]=left&255;wav[45+4*i]=left>>8;
        wav[46+4*i]=right&255;wav[47+4*i]=right>>8;
    }
    return wav;
}
}
TEST_CASE(ProductionStreamingPreparesStereoAndRestoresMonoBindingAfterLiveVisit) {
    Fixture f;
    const char* path="BroTracker/Samples/stereo.wav";
    FakePatternSd::files[path].bytes=StereoPickerWav(7000,22050);
    Pcm16MixedSampleView stereo;
    CHECK_EQ(f.producer.PreparePath(path,stereo),InstrumentAvailability::Ready);
    CHECK(stereo.IsStereo());CHECK_EQ(stereo.frame_count,7000u);CHECK_EQ(stereo.sample_rate_hz,22050u);
    CHECK_EQ(stereo.streaming->layout.ring_frames,2048u);
    CHECK_EQ(stereo.streaming->PrefixFrames(),1024u);
    CHECK(f.producer.BeginPrepareAsync(path));
    CHECK_EQ(f.producer.PreparedMixedView().streaming,stereo.streaming);
    const char* incremental="BroTracker/Samples/incremental-stereo.wav";
    FakePatternSd::files[incremental].bytes=StereoPickerWav(7000,22050);
    CHECK(f.producer.BeginPrepareAsync(incremental));
    for(unsigned i=0;i<32 && !f.producer.PreparationDone();++i) f.producer.PreparationStep();
    CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::Ready);
    CHECK(f.producer.PreparedMixedView().IsStereo());

    auto bindings=SelectDevelopmentFixture(f.producer.reports[1].view).bindings;
    const auto mono=*CacheBinding(bindings,0);
    InstrumentDefinitions definitions;
    RealtimePattern pattern;pattern.active_rows=1;pattern.active_channels=3;
    pattern.cells[0][0]={60,0};pattern.cells[0][1]={60,0};pattern.cells[0][2]={60,2};
    auto centered=mono;centered.instrument=6;bindings.bindings[bindings.count++]=centered;
    pattern.cells[0][1].instrument=6;
    VariableRatePatternPlayer player;
    CHECK_EQ(player.Configure(10000,44100,pattern,bindings),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    f.producer.PinRollback(bindings);
    auto candidate=mono;candidate.sample=stereo;
    CHECK(player.ReplaceLiveInstrumentBinding(candidate));
    std::int16_t left[128],right[128];
    left[0]=77;right[0]=88;
    CHECK_EQ(player.RenderStereo(left,nullptr,128),VariableRatePatternPlayerStatus::InvalidDestination);
    CHECK_EQ(player.GetNextSamplePosition(),0u);CHECK_EQ(left[0],77);CHECK_EQ(right[0],88);
    CHECK_EQ(player.RenderStereo(left,right,128),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(static_cast<int>(left[0])-right[0],3120);
    CHECK_EQ(player.SampleVoice(0)->GetStep(),kSourceFrameStepUnity/2);
    CHECK_EQ(player.SampleVoice(1)->GetStep(),kSourceFrameStepUnity);
    f.producer.Publish(player);f.producer.Service();
    CHECK_EQ(FakePatternSd::reads.back().path,std::string(path));
    CHECK_EQ(FakePatternSd::reads.back().offset,4140u);
    CHECK_EQ(FakePatternSd::reads.back().count,4096u);
    const char* malformed="BroTracker/Samples/stereo-malformed.wav";
    FakePatternSd::files[malformed].bytes=StereoPickerWav(4000);
    FakePatternSd::files[malformed].bytes[32]=2;
    auto rejected=stereo;
    CHECK_EQ(f.producer.PreparePath(malformed,rejected),InstrumentAvailability::PreparationFailed);
    CHECK_EQ(rejected.streaming,stereo.streaming);
    CHECK_EQ(player.SampleVoice(0)->StreamSnapshot().sample,stereo.streaming);
    CHECK(player.ReplaceLiveInstrumentBinding(mono));
    f.producer.UnpinRollback();
    CHECK_EQ(player.Start(),VariableRatePatternPlayerStatus::Success);
    CHECK_EQ(player.RenderStereo(left,right,128),VariableRatePatternPlayerStatus::Success);
    for(unsigned i=0;i<128;++i) CHECK_EQ(left[i],right[i]);
}
namespace {
unsigned RateFixtureByte(unsigned frame,unsigned channel) {
    constexpr unsigned edge_values[]={0,128,255};
    if(frame%7<3)return edge_values[(frame+channel)%3];
    return (frame*73+channel*59)%256;
}
std::int16_t RateFixtureValue(unsigned frame,unsigned channel,unsigned bits) {
    if(bits==8)return static_cast<std::int16_t>(RateFixtureByte(frame,channel)*256L-32768);
    return Fixture::Pcm(channel+1,frame);
}
std::vector<std::uint8_t> RateFixtureWav(unsigned frames,unsigned rate,unsigned channels,unsigned bits=16) {
    auto bytes=channels==2?StereoPickerWav(frames,rate):PickerWav(frames,rate);
    const unsigned stride=channels*(bits/8),size=frames*stride;
    bytes.resize(44+size+(size&1));
    PatternFilePut32(bytes.data()+4,bytes.size()-8);
    PatternFilePut32(bytes.data()+28,rate*stride);
    PatternFilePut32(bytes.data()+40,size);
    bytes[32]=stride;bytes[34]=bits;
    for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<channels;++c) {
        const auto at=44+i*stride+c*(bits/8);
        if(bits==8)bytes[at]=RateFixtureByte(i,c);
        else {
            const auto value=static_cast<std::uint16_t>(RateFixtureValue(i,c,bits));
            bytes[at]=value&255;bytes[at+1]=value>>8;
        }
    }
    if(size&1)bytes.back()=0;
    return bytes;
}
std::int16_t RateReference(std::uint64_t position,unsigned frames,unsigned channel,unsigned bits=16) {
    const auto frame=static_cast<unsigned>(position>>32);
    if(frame>=frames)return 0;
    const auto fraction=static_cast<std::uint32_t>(position)/4294967296.0L;
    return static_cast<std::int16_t>(std::llround(
        (1-fraction)*RateFixtureValue(frame,channel,bits)+fraction*RateFixtureValue(std::min(frame+1,frames-1),channel,bits)));
}
}
TEST_CASE(NativePcmRatesMatchIndependentReferenceAcrossRefillsAndRetrigger) {
    constexpr unsigned frames=14501;
    for(unsigned bits:{8u,16u})for(unsigned rate:{8000u,8363u,11025u,22050u,44100u})for(unsigned channels:{1u,2u}) {
        Fixture f;
        const char* path="BroTracker/Samples/rates.wav";
        FakePatternSd::files[path].bytes=RateFixtureWav(frames,rate,channels,bits);
        Pcm16MixedSampleView view;
        CHECK_EQ(f.producer.PreparePath(path,view),InstrumentAvailability::Ready);
        CHECK_EQ(view.sample_rate_hz,rate); CHECK_EQ(view.IsStereo(),channels==2);
        CHECK_EQ(view.streaming->format.bits_per_sample,bits);
        CHECK_EQ(view.streaming->layout.ring_frames,8192/(channels*(bits/8)));
        if(bits==8) {
            const auto* packed=reinterpret_cast<const unsigned char*>(view.streaming->start);
            CHECK_EQ(packed[0],0u); CHECK_EQ(packed[channels],128u); CHECK_EQ(packed[2*channels],255u);
            if(channels==2)CHECK_EQ(packed[1],128u);
            Pcm16MonoView legacy;
            CHECK_EQ(f.producer.PreparePath(path,legacy),InstrumentAvailability::PreparationFailed);
        }
        VariableRateSampleBindings bindings{};
        bindings.count=1; bindings.bindings[0]={0,60,view};
        VariableRateSampleCommandPreparer commands;
        CHECK_EQ(commands.Configure(44100,bindings),VariableSampleCommandStatus::Success);
        for(unsigned note:{48u,60u,67u,84u}) {
            RowEventBatch events{}; events.count=1;
            events.events[0]={0,0,0,0,0,0,static_cast<std::uint8_t>(note),0};
            VariableRateMixedVoiceCommandBatch batch{};
            CHECK_EQ(commands.Prepare(events,batch),VariableSampleCommandStatus::Success);
            const auto pitch=std::llround(std::exp2((int(note)-60)/12.0L)*4294967296.0L);
            const auto step=static_cast<std::uint64_t>((pitch*rate+22050)/44100);
            CHECK_EQ(batch.commands[0].step_q32_32,step);
            CHECK_EQ(batch.commands[0].sample.streaming,view.streaming);
            auto& voice=f.player.voices[0];
            const auto previous_generation=voice.StreamSnapshot().generation;
            CHECK_EQ(view.Trigger(voice,batch.commands[0].step_q32_32),VariableVoiceStatus::Success);
            CHECK(voice.StreamSnapshot().generation!=previous_generation);
            auto eof=voice.StreamSnapshot().eofs;
            std::uint64_t position=0;
            const auto output_frames=((std::uint64_t(frames)<<32)+step-1)/step;
            for(std::uint64_t i=0;i<output_frames;i+=128) {
                f.producer.Publish(f.player);
                f.producer.Service();
                std::int16_t left[128]{},right[128]{};
                CHECK_EQ(voice.RenderStereo(left,right,128),VariableVoiceStatus::Success);
                for(unsigned j=0;j<128;++j,position+=step) {
                    CHECK_EQ(left[j],RateReference(position,frames,0,bits));
                    CHECK_EQ(right[j],RateReference(position,frames,channels-1,bits));
                }
                if(voice.IsActive()) {
                    CHECK_EQ(voice.StreamSnapshot().position,position>>32);
                    CHECK_EQ(voice.StreamSnapshot().phase,static_cast<std::uint32_t>(position));
                }
            }
            CHECK(!voice.IsActive()); CHECK_EQ(voice.StreamSnapshot().eofs,eof+1);
            CHECK_EQ(voice.StreamSnapshot().underruns,0u);
        }
        CHECK_EQ(f.producer.io_errors,0u);
    }
}
TEST_CASE(LowerRateAuditionEndsAtNativeDurationAndInvalidGeometryPreservesBinding) {
    Fixture f;
    SdWavAudition audition(f.producer);
    auto bindings=SelectDevelopmentFixture(f.producer.reports[1].view).bindings;
    unsigned id=0;
    for(unsigned bits:{8u,16u})for(unsigned rate:{8000u,8363u,11025u,22050u,44100u}) {
        const std::string path="BroTracker/Samples/root"+std::to_string(rate)+"-"+std::to_string(bits)+".wav";
        FakePatternSd::files[path].bytes=RateFixtureWav(3,rate,2,bits);
        CHECK(audition.Request(++id,path.c_str()));
        for(unsigned i=0;i<32 && audition.Busy();++i)audition.Service(bindings);
        unsigned result=99;CHECK_EQ(audition.TakeResult(result),id);CHECK_EQ(result,0u);
        const auto step=(kSourceFrameStepUnity*rate+22050)/44100;
        CHECK_EQ(audition.Snapshot().step,step);
        const auto eof=audition.Snapshot().eofs;
        const auto count=(3*kSourceFrameStepUnity+step-1)/step;
        for(unsigned i=0;i<count;++i) {
            std::int16_t l=0,r=0;
            CHECK(audition.Render(&l,1,&r));
            CHECK_EQ(l,RateReference(i*step,3,0,bits)); CHECK_EQ(r,RateReference(i*step,3,1,bits));
        }
        CHECK_EQ(audition.Snapshot().eofs,eof+1);
        Pcm16MixedSampleView view=f.producer.reports[0].view;
        const auto* original=view.streaming;
        const auto invalid=path+"-bad";
        FakePatternSd::files[invalid].bytes=RateFixtureWav(3,rate,2,bits);
        FakePatternSd::files[invalid].bytes[28]^=1;
        CHECK_EQ(f.producer.PreparePath(invalid.c_str(),view),InstrumentAvailability::PreparationFailed);
        CHECK_EQ(view.streaming,original);
        CHECK(f.producer.BeginPrepareAsync(invalid.c_str()));
        for(unsigned i=0;i<32 && !f.producer.PreparationDone();++i)f.producer.PreparationStep();
        CHECK_EQ(f.producer.PreparationResult(),InstrumentAvailability::PreparationFailed);
        FakePatternSd::files[invalid].bytes=RateFixtureWav(3,48000,2,bits);
        CHECK_EQ(f.producer.PreparePath(invalid.c_str(),view),InstrumentAvailability::PreparationFailed);
        CHECK_EQ(view.streaming,original);
    }
}
TEST_CASE(WavBrowserV6AdmitsLowerRatesWithV5EligibilityIntact) {
    Fixture f;
    FakePatternSd::files["/BroTracker/Samples"].directory=true;
    FakePatternSd::files["/BroTracker/Samples/tone.wav"].bytes=RateFixtureWav(32,11025,2);
    for(unsigned version:{5u,6u}) {
        const auto command="BTSTORE1 WAVOPEN "+std::to_string(version)+" 1 0 0 @2f42726f547261636b65722f53616d706c6573";
        WavOpenRequest request;
        CHECK(ParseWavOpen(command.c_str(),command.size(),request));
        SdWavBrowseJob job;
        CHECK(job.Begin(request));
        for(unsigned i=0;i<32 && !job.Done();++i)job.Step();
        CHECK(job.Done()); CHECK_EQ(job.Result(),WavDirectoryResult::Ok);
        CHECK_EQ(job.Page().count,1u);
        const auto& facts=job.Page().entries[0].facts;
        CHECK_EQ(facts.channels,2u); CHECK_EQ(facts.sample_rate,11025u);
        CHECK_EQ(facts.status,version==6?WavBufferStatus::Success:WavBufferStatus::UnsupportedFormat);
    }
}
TEST_CASE(LowerRateStereoGapAdvancesPhaseAndRefillsAtTheNewSourcePosition) {
    Fixture f;
    constexpr unsigned rate=8000,frames=4000;
    const char* path="BroTracker/Samples/rate-gap.wav";
    FakePatternSd::files[path].bytes=RateFixtureWav(frames,rate,2);
    Pcm16MixedSampleView view;
    CHECK_EQ(f.producer.PreparePath(path,view),InstrumentAvailability::Ready);
    const auto step=(kSourceFrameStepUnity*rate+22050)/44100;
    auto& voice=f.player.voices[0];
    CHECK_EQ(view.Trigger(voice,step),VariableVoiceStatus::Success);
    constexpr unsigned frames_without_refill=7000;
    std::vector<std::int16_t> left(frames_without_refill),right(frames_without_refill);
    CHECK_EQ(voice.RenderStereo(left.data(),right.data(),left.size()),VariableVoiceStatus::Success);
    CHECK_EQ(left.back(),0); CHECK_EQ(right.back(),0);
    auto snapshot=voice.StreamSnapshot();
    const auto position=step*frames_without_refill;
    CHECK_EQ(snapshot.position,position>>32);
    CHECK_EQ(snapshot.phase,static_cast<std::uint32_t>(position));
    CHECK(snapshot.underruns>0);
    f.producer.Publish(f.player);
    CHECK(!f.producer.BackgroundHeadroom());
    f.producer.Service();
    CHECK_EQ(FakePatternSd::reads.back().offset,44u+4u*snapshot.position);
    CHECK(f.producer.BackgroundHeadroom());
    CHECK_EQ(voice.RenderStereo(left.data(),right.data(),128),VariableVoiceStatus::Success);
    for(unsigned i=0;i<128;++i) {
        CHECK_EQ(left[i],RateReference(position+i*step,frames,0));
        CHECK_EQ(right[i],RateReference(position+i*step,frames,1));
    }
    CHECK_EQ(voice.StreamSnapshot().underruns,snapshot.underruns);
}

TEST_CASE(BrowserV7EnablesPackedPcmAnd8363WithoutChangingOldEligibility) {
    for(unsigned bits:{8u,16u})for(unsigned rate:{8363u,22050u}) {
        Fixture f;
        FakePatternSd::files["/BroTracker/Samples"].directory=true;
        FakePatternSd::files["/BroTracker/Samples/tone.wav"].bytes=RateFixtureWav(33,rate,2,bits);
        for(unsigned version:{5u,6u,7u}) {
            SdWavBrowseJob job;WavOpenRequest request;
            const auto message="BTSTORE1 WAVOPEN "+std::to_string(version)+" 1 0 0 @2f42726f547261636b65722f53616d706c6573";
            CHECK(ParseWavOpen(message.c_str(),message.size(),request));
            CHECK(job.Begin(request));
            for(unsigned i=0;i<32 && !job.Done();++i)job.Step();
            CHECK(job.Done());CHECK_EQ(job.Result(),WavDirectoryResult::Ok);
            const auto& facts=job.Page().entries[0].facts;
            CHECK_EQ(facts.sample_rate,rate);CHECK_EQ(facts.bits,bits);
            const bool accepted=version==7 || (version==6 && bits==16 && rate==22050);
            CHECK_EQ(facts.status,accepted?WavBufferStatus::Success:WavBufferStatus::UnsupportedFormat);
        }
    }
}
