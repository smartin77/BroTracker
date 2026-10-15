#include "firmware/teensy/BroTracker/sd_wav_inspection.h"
#include "test_framework.h"
#include "firmware/teensy/BroTracker/sd_wav_buffer_loader.h"
#include "firmware/teensy/BroTracker/sd_wav_preparation.h"
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
TEST_CASE(SdBootPreparationSuccessFailureAndBoundedReport) {
    for(unsigned mode=0;mode<5;++mode) {
        Fixture(); std::int16_t pcm[2]={9,9}; SdWavPreparation preparation;
        if(mode==1) FakeSd::fail_open=true;
        if(mode==2) FakeSd::bytes[20]=3;
        if(mode==3) FakeSd::short_at=44;
        preparation.Prepare(mode!=4,"Samples/test2.wav",pcm,2);
        CHECK(preparation.attempted);
        CHECK_EQ(preparation.view.frame_count,mode==0 ? 2 : 0);
        CHECK_EQ(preparation.view.data,mode==0 ? pcm : nullptr);
        const auto opens=FakeSd::opens;
        preparation.Prepare(true,"Samples/test2.wav",pcm,2); CHECK_EQ(FakeSd::opens,opens);
        SdPreparationReport report; CHECK(report.Offer(preparation)); CHECK(!report.Offer(preparation));
        for(unsigned budget=0;report.HasData() && budget<160;++budget) {
            std::size_t n; const char* data=report.Data(n); CHECK(n<160);
            report.Consume(0); CHECK(data==report.Data(n)); report.Consume(1);
        }
        CHECK(!report.HasData()); report.Reset(); CHECK(report.Offer(preparation)); Released();
    }
    Fixture(); std::int16_t pcm[2]; SdWavPreparation preparation;
    preparation.Prepare(true,"Samples/test2.wav",pcm,1);
    CHECK_EQ(preparation.result.load_status,WavBufferStatus::InsufficientCapacity);
    CHECK_EQ(preparation.view.data,nullptr); Released();
    preparation.capacity=UINT64_MAX; preparation.address=UINTPTR_MAX;
    preparation.view.frame_count=SIZE_MAX; preparation.view.sample_rate_hz=UINT32_MAX;
    SdPreparationReport report; CHECK(report.Offer(preparation));
    std::size_t n; const char* data=report.Data(n); CHECK(n<160); CHECK_EQ(data[n-1],'\n');
    report.Consume(3); CHECK(report.Partial()); report.Reset(); CHECK(!report.HasData());
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

TEST_CASE(DevelopmentWavInspectionExactPathsNoPcmAndFailures) {
    for(unsigned mode=0;mode<10;++mode) {
        Fixture();
        if(mode==1) FakeSd::fail_open=true;
        if(mode==2) FakeSd::directory=true;
        if(mode==3) FakeSd::bytes[20]=3;
        if(mode==4) FakeSd::bytes.pop_back();
        if(mode==5) FakeSd::short_at=0;
        if(mode==6) FakeSd::fail_seek=true;
        if(mode==7) FakeSd::bytes[0]='X';
        if(mode==9) FakeSd::failed_path="Samples/test2.wav";
        FakeSd::on_read=ExcludedWriter;
        DevelopmentWavInspection inspection; inspection.InspectOnce(mode!=8);
        CHECK_EQ(FakeSd::opens,mode==8?0:3);
        for(unsigned i=0;i<FakeSd::paths.size();++i) CHECK_EQ(FakeSd::paths[i],std::string(kDevelopmentWavPaths[i]));
        CHECK(std::string(kDevelopmentWavPaths[0])=="Samples/test.wav");
        CHECK(std::string(kDevelopmentWavPaths[1])=="Samples/test2.wav");
        CHECK(std::string(kDevelopmentWavPaths[2])=="Samples/test3.wav");
        CHECK(FakeSd::highest_read_end<=44); // Headers only; no fake PCM read.
        const auto opens=FakeSd::opens; inspection.InspectOnce(true); CHECK_EQ(FakeSd::opens,opens);
        std::uint64_t total=999; CHECK_EQ(inspection.Total(total),mode==0);
        CHECK_EQ(total,mode==0?12:999); Released();
        if(mode==0) for(const auto& f:inspection.files) {
            CHECK_EQ(f.metadata.file_bytes,48); CHECK_EQ(f.metadata.frames,2);
            CHECK_EQ(f.metadata.required_pcm_bytes,4); CHECK_EQ(f.metadata.duration_us,45);
        }
        CHECK_EQ(FakeSd::closes,mode==1 || mode==8 ? 0 : mode==9 ? 2 : 3);
        if(mode==9) { CHECK_EQ(inspection.files[0].validation,WavBufferStatus::Success);
            CHECK_EQ(inspection.files[1].open_status,SdWavBufferStatus::OpenFailed);
            CHECK_EQ(inspection.files[2].validation,WavBufferStatus::Success); }
        if(mode==1) CHECK_EQ(inspection.files[0].open_status,SdWavBufferStatus::OpenFailed);
        if(mode==2) CHECK_EQ(inspection.files[0].open_status,SdWavBufferStatus::Directory);
        if(mode==3) CHECK_EQ(inspection.files[0].validation,WavBufferStatus::UnsupportedFormat);
        if(mode==4) CHECK_EQ(inspection.files[0].validation,WavBufferStatus::Truncated);
        if(mode==5 || mode==6) CHECK_EQ(inspection.files[0].validation,WavBufferStatus::IoError);
        if(mode==7) CHECK_EQ(inspection.files[0].validation,WavBufferStatus::InvalidContainer);
        WavInspectionReport report; report.Offer(inspection);
        std::string text;
        for(unsigned budget=0;report.HasData() && budget<1024;++budget) {
            std::size_t count=0; const char* data=report.Data(count); CHECK(count<256 && count>0);
            const char c=*data; report.Consume(0); CHECK_EQ(*report.Data(count),c);
            if(report.Partial()) report.Offer(inspection); // Never replaces owned partial line.
            text+=c; report.Consume(1);
        }
        CHECK(!report.HasData()); CHECK_EQ(std::count(text.begin(),text.end(),'\n'),4);
        CHECK(text.find("Samples/test.wav")!=std::string::npos);
        CHECK(text.find(mode==0?"WAVTOTAL 1 1 12":"WAVTOTAL 1 0 0")!=std::string::npos);
        report.Offer(inspection); std::size_t count=0; report.Data(count); report.Consume(1);
        CHECK(report.Partial()); report.Reset(); CHECK(!report.HasData());
    }
}
TEST_CASE(DevelopmentWavReportWorstCaseFieldsAndCheckedTotal) {
    DevelopmentWavInspection inspection; inspection.attempted=inspection.sd_ready=true;
    for(auto& f:inspection.files) {
        f.open_status=SdWavBufferStatus::Success; f.validation=WavBufferStatus::Success;
        auto& m=f.metadata; m.file_known=m.format_known=m.data_known=m.duration_known=true;
        m.file_bytes=m.data_bytes=m.frames=m.duration_us=m.required_pcm_bytes=UINT64_MAX;
        m.format=m.channels=m.bits=UINT16_MAX; m.sample_rate=UINT32_MAX;
    }
    std::uint64_t total=123; CHECK(!inspection.Total(total)); CHECK_EQ(total,123);
    WavInspectionReport report; report.Offer(inspection);
    for(unsigned i=0;i<4;++i) { std::size_t count=0; const char* data=report.Data(count);
        CHECK(count>0 && count<256); CHECK_EQ(data[count-1],'\n'); report.Consume(count); }
    CHECK(!report.HasData());
}

namespace {
const char* unsupported_path=nullptr;
void FormatPerPath(const char* path) { FakeSd::bytes[20]=unsupported_path && std::string(path)==unsupported_path ? 3 : 1; }
}
TEST_CASE(TwoDevelopmentWavsPrepareIndependentlyAndKeepExactPaths) {
    CHECK_EQ(kDevelopmentManualWavFrameCapacity*2,136568);
    for(unsigned mode=0;mode<8;++mode) {
        Fixture(); std::int16_t pcm2[2]={9,9},pcm3[2]={8,8}; SdWavPreparation test2,test3;
        unsupported_path=mode==3?kDevelopmentWavPaths[1]:mode==4?kDevelopmentWavPaths[2]:nullptr;
        FakeSd::on_open=FormatPerPath; FakeSd::on_read=ExcludedWriter;
        FakeSd::failed_path=mode==1?kDevelopmentWavPaths[1]:mode==2?kDevelopmentWavPaths[2]:nullptr;
        PrepareDevelopmentWavs(mode!=7,test2,pcm2,mode==5?1:2,test3,pcm3,mode==6?1:2);
        const bool valid2=mode!=1 && mode!=3 && mode!=5 && mode!=7;
        const bool valid3=mode!=2 && mode!=4 && mode!=6 && mode!=7;
        CHECK_EQ(test2.view.data,valid2?pcm2:nullptr); CHECK_EQ(test3.view.data,valid3?pcm3:nullptr);
        if(valid2) { CHECK_EQ(pcm2[0],1); CHECK_EQ(pcm2[1],-1); }
        if(valid3) { CHECK_EQ(pcm3[0],1); CHECK_EQ(pcm3[1],-1); }
        if(mode!=7) { CHECK_EQ(FakeSd::paths.size(),2); CHECK_EQ(FakeSd::paths[0],"Samples/test2.wav"); CHECK_EQ(FakeSd::paths[1],"Samples/test3.wav"); }
        const auto opens=FakeSd::opens;
        PrepareDevelopmentWavs(true,test2,pcm2,2,test3,pcm3,2); CHECK_EQ(FakeSd::opens,opens); Released();
        SdPreparationReport report; CHECK(report.Offer(test3,kDevelopmentWavPaths[2]));
        std::size_t length=0; const char* line=report.Data(length);
        CHECK(std::string(line,length).find("BTPATTERN1 WAVPREP 2 Samples/test3.wav ")==0);
        report.Consume(1); CHECK(report.Partial()); CHECK(!report.Offer(test2));
        report.Consume(length-1); CHECK(!report.HasData());
    }
    SdWavPreparation maximum; maximum.capacity=UINT64_MAX; maximum.address=UINTPTR_MAX;
    maximum.view.frame_count=SIZE_MAX; maximum.view.sample_rate_hz=UINT32_MAX;
    SdPreparationReport report; CHECK(report.Offer(maximum,kDevelopmentWavPaths[2]));
    std::size_t length=0; report.Data(length); CHECK(length<160);
}
