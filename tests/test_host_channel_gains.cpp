#include "test_framework.h"
#include "bringup/host_channel_gains.h"
#include <cstring>
#include <string>
using namespace BroTracker;
namespace {
bool Reply(HostChannelGains& h,const char* s,unsigned now=0) { return h.Response(s,std::strlen(s),now); }
void State(HostChannelGains& h,Pcm16ChannelGains g,unsigned applied,unsigned next,unsigned now=0) {
    char line[96]; CHECK(FormatPatternGainState(g,applied,next,line,sizeof(line)));
    CHECK(h.Response(line,std::strlen(line)-1,now));
}
}
TEST_CASE(HostGainsStrictTenFieldStateParser) {
    HostGainState output; const char* valid="BTGAIN1 STATE 1 4294967295 0 0 65536 1 65535 32768 0 65536 42";
    CHECK(ParseHostGainState(valid,std::strlen(valid),output)); CHECK_EQ(output.applied_id,UINT32_MAX);
    CHECK_EQ(output.gains.channels[7],42u); const auto saved=output;
    const char* bad[]={"BTGAIN1 STATE 2 0 1 0 0 0 0 0 0 0 0","BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0",
        "BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0 0 0","BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0 0 ",
        "BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0 65537","BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0 -1",
        "BTGAIN1 STATE 1 0 4294967296 0 0 0 0 0 0 0 0","BTGAIN1 STATE 1 1 1 0 0 0 0 0 0 0 0",
        "BTGAIN1 STATE 1 0 1 0 0 0 0 0 0 0 +1","BTGAIN1 STATE 1 0 1  0 0 0 0 0 0 0 0"};
    for(auto line:bad) { CHECK(!ParseHostGainState(line,std::strlen(line),output)); CHECK_EQ(output.applied_id,saved.applied_id); CHECK_EQ(output.gains.channels[7],42u); }
    std::uint32_t fields[10]{};
    CHECK(!SnapshotFields(valid,std::strlen(valid),"BTGAIN1 STATE 1 ",fields,10));
}
TEST_CASE(HostGainsCapabilityInitialStateCoalescingAndVerification) {
    HostChannelGains host; Pcm16ChannelGains device; device.channels[0]=12345;
    CHECK(!host.Request(device)); CHECK(!Reply(host,"BTGAIN1 CAP 2")); CHECK(Reply(host,"BTGAIN1 CAP 1"));
    State(host,device,3,4); CHECK(host.Available()); CHECK_EQ(host.Authoritative()->gains.channels[0],12345u);
    CHECK(!host.Begin(0)); // discovery must never send unity
    auto desired=device; desired.channels[0]=32768; CHECK(host.Request(desired)); CHECK(host.Begin(10));
    char line[96]; std::size_t size=0; CHECK(host.FormatLine(line,sizeof(line),size)); const std::string frozen(line,size);
    desired.channels[0]=65536; CHECK(host.Request(desired)); desired.channels[0]=0; CHECK(host.Request(desired));
    CHECK(host.FormatLine(line,sizeof(line),size)); CHECK_EQ(std::string(line,size),frozen);
    CHECK(frozen.find("SET 1 4 32768 ")!=std::string::npos); host.LineComplete();
    CHECK(!Reply(host,"BTGAIN1 APPLIED 1 5")); CHECK(Reply(host,"BTGAIN1 SUBMITTED 1 4"));
    CHECK(!Reply(host,"BTGAIN1 SUBMITTED 1 4")); CHECK_EQ(host.Authoritative()->gains.channels[0],12345u);
    CHECK(Reply(host,"BTGAIN1 APPLIED 1 4")); CHECK(host.NeedsRead(20)); host.ReadSent(20);
    State(host,device,3,5,21); CHECK(!host.Begin(21)); CHECK(!host.NeedsRead(21)); CHECK(host.NeedsRead(1020));
    device.channels[0]=32768; State(host,device,4,5,1020); CHECK(host.Begin(1030));
    CHECK(host.FormatLine(line,sizeof(line),size)); CHECK(std::strstr(line,"SET 1 5 0 "));
    auto invalid=desired; invalid.channels[7]=65537; CHECK(!host.Request(invalid));
}
TEST_CASE(HostGainsRejectionTimeoutDisconnectExhaustion) {
    HostChannelGains host; Pcm16ChannelGains device; CHECK(Reply(host,"BTGAIN1 CAP 1")); State(host,device,0,1);
    auto desired=device; desired.channels[0]=0; CHECK(host.Request(desired)); CHECK(host.Begin(0)); host.LineComplete();
    CHECK(!Reply(host,"BTGAIN1 ERROR 1 1 rubbish")); CHECK(Reply(host,"BTGAIN1 ERROR 1 1 queue-full"));
    State(host,device,0,1); CHECK(!host.Begin(0)); CHECK_EQ(host.Phase(),HostGainPhase::Rejected);
    CHECK(host.Request(desired)); CHECK(host.Begin(1)); char line[96]; std::size_t size;
    CHECK(host.FormatLine(line,sizeof(line),size)); CHECK(std::strstr(line,"SET 1 2 ")); host.BeforeWrite(1); host.LineComplete();
    host.Tick(5001); CHECK_EQ(host.Phase(),HostGainPhase::Uncertain); CHECK(!host.Request(device));
    State(host,device,0,3); CHECK(!host.Begin(6000));
    host.Disconnect(); CHECK(!host.Available()); CHECK(!host.Desired());
    CHECK(Reply(host,"BTGAIN1 CAP 1")); State(host,device,0,3); CHECK_EQ(host.Phase(),HostGainPhase::Uncertain);
    device.channels[0]=0; State(host,device,2,3); CHECK(!host.Begin(7000)); CHECK_EQ(host.Phase(),HostGainPhase::Ready);
    CHECK(!host.Desired()); State(host,device,UINT32_MAX,0); CHECK_EQ(host.Phase(),HostGainPhase::Exhausted);
    CHECK(!host.Request(desired));
}
