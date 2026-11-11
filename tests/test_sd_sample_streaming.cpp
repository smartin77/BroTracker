#include "test_framework.h"
#include "firmware/teensy/BroTracker/sd_sample_streaming.h"
#include "core/audio/variable_rate_sample_voice.h"
#include "bringup/pattern_fixture.h"
#include "bringup/instrument_identity.h"
#include <array>
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
            file.metadata.frames=frames; file.metadata.data_offset=DataOffset;
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
        CHECK_EQ(player.voices[channel].Trigger(producer.reports[source].view,kSourceFrameStepUnity),VariableVoiceStatus::Success);
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
        inspected.metadata.frames=32;inspected.metadata.data_offset=44;
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
