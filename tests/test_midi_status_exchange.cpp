#include "test_framework.h"
#include "ui/midi_status_exchange.h"
#include <fstream>
using namespace BroTracker;
TEST_CASE(MidiRouteWireRejectsInvalidOrIncompleteObservations) {
    const std::string good="BTMIDIROUTE 1 123 456 00000000-0000-0000-0000-000000000000 10000 unavailable 1\ndetail subscription remains\nusb urb status -32\nvisibility kernel observed\n";
    MidiRouteObservation v;CHECK(ParseMidiRoute(good,v));CHECK_EQ(v.subscription,1u);CHECK_EQ(v.usb_error,"urb status -32");
    CHECK(!ParseMidiRoute(good.substr(0,good.size()-30),v));
    auto bad=good;bad.replace(bad.find("unavailable 1"),13,"connected 0");CHECK(!ParseMidiRoute(bad,v));
    CHECK(!ParseMidiRoute(good+"extra\n",v));CHECK(!ParseMidiRoute(std::string(4097,'x'),v));
}
TEST_CASE(MidiHealthRecoveryHintsNeverTreatCleanupOrFaultAsHealthy) {
    HostMidiHealth h;char text[192];FormatMidiHealth(h,true,text,sizeof(text));CHECK(std::strstr(text,"unknown"));
    h.connected=h.supported=h.known=h.configured=true;h.fault=h.cleanup=true;h.reason=8;
    FormatMidiHealth(h,false,text,sizeof(text));CHECK(std::strstr(text,"completion stall"));CHECK(std::strstr(text,"STOP; wait STOPPED + cleanup=0; then START"));
    FormatMidiHealth(h,true,text,sizeof(text));CHECK(std::strstr(text,"cleanup uncertain"));
    h.cleanup=false;FormatMidiHealth(h,true,text,sizeof(text));CHECK(std::strstr(text,"START requests recovery"));
    h.fault=false;FormatMidiHealth(h,true,text,sizeof(text));CHECK(std::strstr(text,"healthy"));CHECK(std::strstr(text,"external delivery unknown"));
    h.known=false;FormatMidiHealth(h,true,text,sizeof(text));CHECK(std::strstr(text,"stale"));
}
#ifdef __linux__
namespace {
void Put(const std::filesystem::path& path,const std::string& text) {std::ofstream file(path);file<<text;}
std::string Get(const std::filesystem::path& path) {std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
}
TEST_CASE(MidiRuntimeDirectoryMatchesHelperAndIsPrivate) {
    char pattern[]="/tmp/btx-midi-path-XXXXXX";const char* made=mkdtemp(pattern);CHECK(made);
    const auto previous=std::getenv("TMPDIR");const std::string saved=previous?previous:"";
    setenv("TMPDIR",made,1);
    const auto directory=MidiRuntimeDirectory();
    CHECK(directory==std::filesystem::path(made)/("brotracker-"+std::to_string(getuid())));
    {MidiStatusExchange exchange(directory);exchange.Tick({},"stopped",10000);}
    struct stat info{};CHECK(!lstat(directory.c_str(),&info));CHECK_EQ(info.st_mode&0077,0u);
    setenv("TMPDIR","relative",1);CHECK(MidiRuntimeDirectory().parent_path()=="/tmp");
    if(previous)setenv("TMPDIR",saved.c_str(),1);else unsetenv("TMPDIR");
    std::filesystem::remove_all(made);
}
TEST_CASE(MidiExchangeHandlesMissingStaleRestartedOwnersAndAtomicHealthPublication) {
    char pattern[]="/tmp/btx-midi-exchange-XXXXXX";const char* made=mkdtemp(pattern);CHECK(made);
    const std::filesystem::path dir(made);
    {
        MidiStatusExchange exchange(dir);HostMidiHealth h;h.connected=h.supported=h.known=h.configured=true;h.age_ms=10;
        exchange.Tick(h,"stopped",10000);CHECK(exchange.route.find("missing")!=std::string::npos);
        const auto health=Get(dir/"btx-midi-health.state");CHECK(health.find("BTMIDIHEALTH 1 ")==0);
        std::istringstream in(health);std::string magic,boot;unsigned version;std::uint64_t pid,start,stamp;
        in>>magic>>version>>pid>>start>>boot>>stamp;
        const auto wire=[&](std::uint64_t time,std::uint64_t owner_start,const std::string& owner_boot) {
            return "BTMIDIROUTE 1 "+std::to_string(pid)+" "+std::to_string(owner_start)+" "+owner_boot+" "+std::to_string(time)+
                " unavailable 1\ndetail route present\nusb urb status -32\nvisibility reduced: fixture\n";
        };
        Put(dir/"midi-connect-status.btx",wire(10000,start,boot));exchange.Tick(h,"playing",11000);
        CHECK(exchange.route.find("subscription present")!=std::string::npos);CHECK(exchange.usb.find("-32")!=std::string::npos);
        CHECK(exchange.route.find("reduced")!=std::string::npos);
        exchange.Tick(h,"playing",32001);CHECK(exchange.route.find("stale")!=std::string::npos);CHECK(exchange.usb.empty());
        Put(dir/"midi-connect-status.btx",wire(33000,start+1,boot));exchange.Tick(h,"stopped",33001);CHECK(exchange.route.find("stale")!=std::string::npos);
        Put(dir/"midi-connect-status.btx",wire(34000,start,"00000000-0000-0000-0000-000000000000"));exchange.Tick(h,"stopped",34001);CHECK(exchange.route.find("stale")!=std::string::npos);
        Put(dir/"midi-connect-status.btx",wire(35000,start,boot));exchange.Tick(h,"stopped",35001);CHECK(exchange.route.find("subscription present")!=std::string::npos);
        MidiStatusExchange competing(dir);competing.Tick({},"idle",36001);
        CHECK(competing.route.find("publisher unavailable")!=std::string::npos);
        CHECK(Get(dir/"btx-midi-health.state").find(" 35001 ")!=std::string::npos);
    }
    CHECK(!std::filesystem::exists(dir/"btx-midi-health.state"));
    {MidiStatusExchange restarted(dir);restarted.Tick({},"idle",40000);CHECK(Get(dir/"btx-midi-health.state").find(" 40000 ")!=std::string::npos);}
    std::filesystem::remove_all(dir);
}
#endif
