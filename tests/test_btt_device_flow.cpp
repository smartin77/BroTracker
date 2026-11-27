// Exercise the actual opt-in entry point against a dedicated positional SD fake.
#include "firmware/teensy/btt_device_diagnostic/main.cpp"
#include <iostream>
namespace BroTracker {
StreamInterruptGuard::StreamInterruptGuard()noexcept{mask_=FakeStreamPlatform::primask;FakeStreamPlatform::primask=1;}
StreamInterruptGuard::~StreamInterruptGuard()noexcept{FakeStreamPlatform::primask=mask_;}
}
int main(int argc,char** argv){
    const std::string scenario=argc>1?argv[1]:"success";
    const std::string wav="/BroTracker/.btt-prototype/device-diagnostic.wav";
    DeviceSd::files["/Samples/user.wav"].bytes={1,2,3,4};
    if(scenario=="collision")DeviceSd::files[wav].bytes={9,8,7};
    if(scenario=="write_failure")DeviceSd::fail_write=true;
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
        DeviceSd::fail_write=false;Serial.input="BTTDIAG CLEANUP\n";
        for(unsigned i=0;i<100 && diagnostic.phase!=Phase::Done;++i)loop();
        ok=ok && diagnostic.phase==Phase::Done && Serial.output.find("PASS step=suite")==std::string::npos;
    }
    ok=ok && BroTracker::ResourcePaths().UniqueCount()==0 && DeviceSd::removed==0;
    std::cout<<Serial.output;
    std::cout<<(ok?"SIMULATOR PASS ":"SIMULATOR FAIL ")<<scenario<<" (not physical SD evidence)\n";
    return ok?0:1;
}
