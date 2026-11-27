// Exercise the actual opt-in entry point against a dedicated positional SD fake.
#include "firmware/teensy/btt_device_diagnostic/main.cpp"
#include <iostream>
namespace BroTracker {
StreamInterruptGuard::StreamInterruptGuard()noexcept{mask_=FakeStreamPlatform::primask;FakeStreamPlatform::primask=1;}
StreamInterruptGuard::~StreamInterruptGuard()noexcept{FakeStreamPlatform::primask=mask_;}
}
int main(int argc,char** argv){
    const std::string scenario=argc>1?argv[1]:"success";
    const std::string wav=std::string("/")+kWav;
    const std::string rewrite=BTT_BACKING_DIRECTORY "/00000001-1-00000002.btt";
    DeviceSd::files["/Samples/user.wav"].bytes={1,2,3,4};
    // Historical physical-run evidence remains in the original parent namespace.
    DeviceSd::files["/BroTracker/.btt-prototype"].directory=true;
    DeviceSd::files["/BroTracker/.btt-prototype/device-diagnostic.wav"].bytes={10,11,12};
    DeviceSd::files["/BroTracker/.btt-prototype/00000001-1-00000002.btt"].bytes={20,21,22};
    if(scenario=="collision")DeviceSd::files[wav].bytes={9,8,7};
    if(scenario=="write_failure")DeviceSd::fail_write=true;
    if(scenario=="rewrite_failure")DeviceSd::fail_write_path=rewrite;
    if(scenario=="backing_quarantine"){DeviceSd::fail_close_path=rewrite;DeviceSd::close_failure_closes_inner=true;}
    setup();Serial.input="BTTDIAG STATUS\n";for(unsigned i=0;i<5;++i)loop();
    Serial.input=std::string("BTTDIAG RUN")+'\0'+"extra\n";
    for(unsigned i=0;i<5;++i)loop(); // An embedded NUL must not authorize RUN.
    if(DeviceSd::begin_calls || DeviceSd::open_calls || DeviceSd::write_calls || DeviceSd::created)return 1;
    const auto run_offset=Serial.output.size();
    Serial.input="BTTDIAG RUN\n";
    bool injected=false;
    for(unsigned i=0;i<3000 && diagnostic.phase!=Phase::Done && diagnostic.phase!=Phase::Failed;++i){
        if(scenario=="quarantine" && diagnostic.phase==Phase::CancelDrain && diagnostic.cycle==1){DeviceSd::fail_close_path=wav;injected=true;}
        loop();
    }
    bool ok=DeviceSd::begin_calls==1 && DeviceSd::removed==0 && DeviceSd::files["/Samples/user.wav"].bytes==std::vector<uint8_t>{1,2,3,4};
    ok=ok && DeviceSd::files["/BroTracker/.btt-prototype/device-diagnostic.wav"].bytes==std::vector<uint8_t>{10,11,12} &&
        DeviceSd::files["/BroTracker/.btt-prototype/00000001-1-00000002.btt"].bytes==std::vector<uint8_t>{20,21,22};
    if(scenario=="success"){
        ok=ok && diagnostic.phase==Phase::Done && DeviceSd::created==7 && diagnostic.case_index==1 && diagnostic.cycle==8 &&
            Serial.output.find("PASS step=suite")!=std::string::npos && Serial.output.substr(run_offset).find("FAIL")==std::string::npos;
        for(const auto& item:DeviceSd::files)ok=ok && item.second.opens==0;
    } else {
        ok=ok && diagnostic.phase==Phase::Failed;
        if(scenario=="collision")ok=ok && DeviceSd::created==0 && DeviceSd::write_calls==0 && DeviceSd::files[wav].bytes==std::vector<uint8_t>{9,8,7};
        if(scenario=="quarantine"){
            ok=ok && injected && Serial.output.find("quarantined")!=std::string::npos;
            Serial.input="BTTDIAG CLEANUP\n";for(unsigned i=0;i<10;++i)loop();ok=ok && diagnostic.phase==Phase::Failed;
            DeviceSd::fail_close_path.clear();
        }
        DeviceSd::fail_write=false;DeviceSd::fail_write_path.clear();Serial.input="BTTDIAG CLEANUP\n";
        for(unsigned i=0;i<100 && diagnostic.phase!=Phase::Done;++i)loop();
        if(scenario=="backing_quarantine"){
            ok=ok && diagnostic.phase==Phase::Failed && diagnostic.detail==310 && !diagnostic.pool->CanDestroy() &&
                diagnostic.cleanup_backings[1].resources.quarantined && DeviceSd::files[rewrite].opens==0;
        } else ok=ok && diagnostic.phase==Phase::Done;
        ok=ok && Serial.output.find("PASS step=suite")==std::string::npos;
        if(scenario=="rewrite_failure"){
            ok=ok && diagnostic.first_code==4 && std::string(diagnostic.first_step)=="noedit_rewrite_reopen_validate" &&
                diagnostic.cleanup_backings[1].resources.recorded==Code::Io && !diagnostic.cleanup_backings[1].resources.open &&
                !diagnostic.cleanup_backings[1].resources.quarantined && diagnostic.first_backings[1].resources.writer;
        }
    }
    ok=ok && BroTracker::ResourcePaths().UniqueCount()==0 && DeviceSd::removed==0;
    std::cout<<"SDK EOF model rejected_seeks="<<DeviceSd::rejected_seeks<<" last_offset="<<DeviceSd::rejected_offset<<" size="<<DeviceSd::rejected_size<<'\n';
    std::cout<<Serial.output;
    std::cout<<(ok?"SIMULATOR PASS ":"SIMULATOR FAIL ")<<scenario<<" (not physical SD evidence)\n";
    return ok?0:1;
}
