#include "test_framework.h"
#include "bringup/diagnostic_channel_gains.h"
using namespace BroTracker;
TEST_CASE(DiagnosticGainParsingStrictAndAtomic) {
    Pcm16ChannelGains gains;
    CHECK_EQ(ParseDiagnosticChannelGains(nullptr,gains),DiagnosticGainConfiguration::Absent);
    CHECK_EQ(ParseDiagnosticChannelGains("65536,65536,16384,65536,65536,65536,65536,0",gains),DiagnosticGainConfiguration::Valid);
    CHECK_EQ(gains.channels[2],16384u); CHECK_EQ(gains.channels[7],0u);
    for(const auto* invalid:{"","0,0,0,0,0,0,0","0,0,0,0,0,0,0,0,0","0,0,0,0,0,0,0,65537",
        "0,0,0,0,0,0,0,4294967296","0,0,0,0,0,0,0,-1","0,0,0,0,0,0,0,+1",
        "0,0,0,0,0,0,0, 1","0,0,0,0,0,0,0,0 ","0,0,0,0,0,0,0,00000000000"}) {
        CHECK_EQ(ParseDiagnosticChannelGains(invalid,gains),DiagnosticGainConfiguration::Invalid);
        CHECK_EQ(gains.channels[2],16384u); CHECK_EQ(gains.channels[7],0u);
    }
}
TEST_CASE(DiagnosticGainOneShotInitialSessionAndNoReconnectReplay) {
    Pcm16ChannelGains out;
    DiagnosticChannelGainRequest absent(nullptr),invalid("1,2"),valid("65536,65536,8192,65536,65536,65536,65536,65536");
    CHECK(!absent.TakeRequest(true,true,true,out)); CHECK(!invalid.TakeRequest(true,true,true,out));
    CHECK(!valid.TakeRequest(false,true,true,out)); CHECK(!valid.TakeRequest(true,false,true,out));
    CHECK(!valid.TakeRequest(true,true,false,out)); CHECK(valid.TakeRequest(true,true,true,out));
    CHECK_EQ(out.channels[2],8192u); CHECK(!valid.TakeRequest(true,true,true,out));
    CHECK(!valid.TakeRequest(false,false,false,out)); CHECK(!valid.TakeRequest(true,true,true,out));
    DiagnosticChannelGainRequest lost("0,0,0,0,0,0,0,0");
    CHECK(!lost.TakeRequest(true,false,false,out)); CHECK(!lost.TakeRequest(false,false,false,out));
    CHECK(!lost.TakeRequest(true,true,true,out)); // first session ended before discovery
}
