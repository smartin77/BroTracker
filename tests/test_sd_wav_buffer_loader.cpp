#include "test_framework.h"
#include "firmware/teensy/BroTracker/sd_wav_buffer_loader.h"
using namespace BroTracker;
namespace {
void Fixture() {
    FakeSd::Reset();
    FakeSd::bytes={'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,1,0,0xff,0xff};
}
unsigned exclusions=0;
void ExcludedWriter() { ++exclusions; CHECK(!SdAccess::AcquireWrite()); }
void Released() { CHECK(SdAccess::AcquireWrite()); SdAccess::ReleaseWrite(); }
}
TEST_CASE(SdWavAdapterSuccessAndSharedLockLifetime) {
    Fixture(); exclusions=0; FakeSd::on_read=ExcludedWriter;
    std::int16_t pcm[2]={9,9}; Pcm16MonoView view;
    const auto result=LoadWavBufferFromSd("/sample.wav",pcm,2,view);
    CHECK_EQ(result.status,SdWavBufferStatus::Success); CHECK_EQ(pcm[0],1); CHECK_EQ(pcm[1],-1);
    CHECK_EQ(view.data,pcm); CHECK_EQ(view.frame_count,2); CHECK_EQ(view.sample_rate_hz,44100);
    CHECK(exclusions>0); CHECK_EQ(FakeSd::opens,1); CHECK_EQ(FakeSd::closes,1); Released();
}
TEST_CASE(SdWavAdapterFailuresReleaseFileAndLockAndPreserveMetadata) {
    for(unsigned mode=0;mode<8;++mode) {
        Fixture(); std::int16_t pcm[2]={9,9}; Pcm16MonoView view{pcm,99,123};
        const char* path="/sample.wav"; auto expected=SdWavBufferStatus::LoadFailed;
        if(mode==0) { path=nullptr; expected=SdWavBufferStatus::InvalidPath; }
        if(mode==1) { path=""; expected=SdWavBufferStatus::InvalidPath; }
        if(mode==2) { FakeSd::fail_open=true; expected=SdWavBufferStatus::OpenFailed; }
        if(mode==3) { FakeSd::directory=true; expected=SdWavBufferStatus::Directory; }
        if(mode==4) FakeSd::short_at=44;
        if(mode==5) FakeSd::fail_seek=true;
        if(mode==6) FakeSd::negative_read=true;
        if(mode==7) FakeSd::bytes[20]=3;
        FakeSd::on_read=ExcludedWriter;
        const auto result=LoadWavBufferFromSd(path,pcm,2,view);
        CHECK_EQ(result.status,expected); CHECK_EQ(view.data,pcm); CHECK_EQ(view.frame_count,99); CHECK_EQ(view.sample_rate_hz,123);
        CHECK_EQ(pcm[0],9); CHECK_EQ(pcm[1],9);
        CHECK_EQ(FakeSd::closes,mode<3 ? 0 : 1); Released();
    }
}
TEST_CASE(SdWavAdapterSeekNarrowingAndWriterExclusion) {
    Fixture(); {
        SdWavBufferReader reader; CHECK_EQ(reader.Open("/sample.wav"),SdWavBufferStatus::Success);
        CHECK(!reader.Seek(std::uint64_t(UINT32_MAX)+1)); CHECK_EQ(FakeSd::seeks,0);
        std::uint8_t byte; FakeSd::negative_read=true; CHECK_EQ(reader.Read(&byte,1),0);
        CHECK(!SdAccess::AcquireWrite()); reader.Close(); Released();
    }
    CHECK_EQ(FakeSd::closes,1);
    CHECK(SdAccess::AcquireWrite());
    std::int16_t pcm[2]; Pcm16MonoView view;
    CHECK_EQ(LoadWavBufferFromSd("/sample.wav",pcm,2,view).status,SdWavBufferStatus::OpenFailed);
    SdAccess::ReleaseWrite(); Released();
}
