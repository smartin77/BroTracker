#include "test_framework.h"
#include "ui/host_logging.h"
#include <fstream>
#include <string>
using namespace BroTracker;
namespace {
std::string Read(const std::filesystem::path& p) {std::ifstream in(p); return {std::istreambuf_iterator<char>(in),{}};}
void Write(const std::filesystem::path& p,const char* text) {std::ofstream out(p); out<<text;}
}
TEST_CASE(HostLoggingConfigCompatibilityAndRollback) {
    HostConfig c;
    CHECK(c.logging_mode==HostLogMode::Basic); CHECK_EQ(c.preserve_last,0u);
    const std::string btx="[btx]\nschema_version=1\nnew_pattern_length=64\n";
    CHECK(ParseHostConfig(btx,c)==HostConfigStatus::Loaded);
    CHECK(c.logging_mode==HostLogMode::Basic);
    for(unsigned n:{0u,5u,20u}) {
        CHECK(ParseHostConfig(btx+"[logging]\nmode=debug\npreserve_last="+std::to_string(n),c)==HostConfigStatus::Loaded);
        CHECK(c.logging_mode==HostLogMode::Debug); CHECK_EQ(c.preserve_last,n);
    }
    for(const char* bad:{"mode=verbose","mode=DEBUG","preserve_last=21","preserve_last=-1","preserve_last=4294967296",
        "mode=basic\nmode=debug","preserve_last=1\npreserve_last=2","unknown=1","[logging]"}) {
        CHECK(ParseHostConfig(btx+"[logging]\n"+bad,c)==HostConfigStatus::Invalid);
        CHECK(c.logging_mode==HostLogMode::Debug); CHECK_EQ(c.preserve_last,20u); CHECK_EQ(c.new_pattern_length,64u);
    }
    CHECK(ParseHostConfig("[logging]\nmode=basic\n"+btx,c)==HostConfigStatus::Loaded);
    CHECK(c.logging_mode==HostLogMode::Basic); CHECK_EQ(c.preserve_last,0u);
    const auto dir=std::filesystem::temp_directory_path()/"brotracker-logging-config-test";
    CHECK(std::filesystem::create_directory(dir)); const auto path=dir/"config.ini";
    Write(path,(btx+"[logging]\nmode=debug\npreserve_last=5").c_str());
    CHECK(LoadHostConfig(path,false).config.logging_mode==HostLogMode::Debug);
    auto safe=LoadHostConfig(path,true); CHECK(safe.config.logging_mode==HostLogMode::Basic); CHECK_EQ(safe.config.preserve_last,0u);
    Write(path,(btx+"[logging]\nmode=debug\npreserve_last=21").c_str());
    auto invalid=LoadHostConfig(path,false); CHECK(invalid.status==HostConfigStatus::Invalid);
    CHECK(invalid.config.logging_mode==HostLogMode::Basic); CHECK_EQ(invalid.config.new_pattern_length,32u);
    std::filesystem::remove_all(dir);
}
TEST_CASE(HostLoggingFilteringTimestampsAndChangedRecovery) {
    for(const char* payload:{"BTPATTERN1 CELL 2 1 0 60 0 0","BTPATTERN1 POS 2 1 1 0 0 0 16 3",
        "BTPATTERN1 PERF 1 1 1 1 1 0 100","flow running=1","local draft changed: checksum=1"})
        CHECK(ClassifyHostDiagnostic("USB",payload)==HostLogLevel::Debug);
    for(const char* important:{"BTTEST1 STOPPED","BTTEST1 PAUSED","BTTEST1 STATE PLAYING","BTPREVIEW1 REJECTED 1 2 missing-instrument",
        "Preview outcome uncertain; recovering","Working pattern synchronized","write failed"})
        CHECK(ClassifyHostDiagnostic("USB",important)==HostLogLevel::Basic);
    CHECK(ClassifyHostDiagnostic("Terminal error","Texture unavailable") == HostLogLevel::Basic);
    CHECK(ClassifyHostDiagnostic("Windows audio disabled","HRESULT=0x1") == HostLogLevel::Basic);
    char stamp[192]; CHECK(FormatHostTimestamp(0,330,42,stamp,sizeof(stamp)));
    CHECK(std::string(stamp).find("1970-01-01T05:30:00.000+05:30")!=std::string::npos);
    CHECK(std::string(stamp).find("mono_ms=42")!=std::string::npos);
    CHECK(FormatHostTimestamp(0,-210,43,stamp,sizeof(stamp)));
    CHECK(std::string(stamp).find("1969-12-31T20:30:00.000-03:30")!=std::string::npos);
    CHECK(!FormatHostTimestamp(-1,0,0,stamp,sizeof(stamp))); CHECK(!FormatHostTimestamp(0,0,0,stamp,1));
    ConfigureHostLogging(HostLogMode::Basic); FILE* f=std::tmpfile(); CHECK(f);
    HostLog(f,HostLogLevel::Debug,"USB","hidden CELL");
    HostLog(f,HostLogLevel::Basic,"USB","[host_ms=1] waiting for device");
    HostLog(f,HostLogLevel::Basic,"USB","[host_ms=2] waiting for device");
    HostLog(f,HostLogLevel::Basic,"USB","connected: recovered");
    HostLog(f,HostLogLevel::Basic,"USB","[host_ms=3] waiting for device");
    std::rewind(f); std::string text; char line[1024]; while(std::fgets(line,sizeof(line),f)) text+=line;
    CHECK(text.find("hidden CELL")==text.npos); CHECK(text.find("host_ms=2")==text.npos);
    CHECK(text.find("host_ms=3")!=text.npos); CHECK(text.find("utc=")!=text.npos);
    ConfigureHostLogging(HostLogMode::Debug); HostLog(f,HostLogLevel::Debug,"USB","visible CELL");
    std::rewind(f); text.clear(); while(std::fgets(line,sizeof(line),f)) text+=line;
    CHECK(text.find("visible CELL")!=text.npos); std::fclose(f); ConfigureHostLogging(HostLogMode::Basic);
}
TEST_CASE(HostLoggingRotationLimitsAppendAndSafeFailure) {
    const auto dir=std::filesystem::temp_directory_path()/"brotracker-logging-test";
    CHECK(std::filesystem::create_directory(dir));
    const auto p=dir/"terminal.log",one=dir/"terminal(1).log",two=dir/"terminal(2).log";
    Write(p,"current"); Write(one,"one"); Write(two,"two"); Write(dir/"terminal(20).log","twenty"); Write(dir/"unrelated.log","evidence");
    HostConfig config; config.logging_mode=HostLogMode::Debug; config.preserve_last=2;
    auto plan=PrepareHostLog(p,config,true); CHECK(plan.append); CHECK_EQ(Read(one),"one"); CHECK_EQ(Read(p),"current");
    plan=PrepareHostLog(p,config,false); CHECK(!plan.append); CHECK_EQ(Read(one),"current"); CHECK_EQ(Read(two),"one");
    CHECK(!std::filesystem::exists(dir/"terminal(20).log")); CHECK_EQ(Read(dir/"unrelated.log"),"evidence");
    CHECK_EQ(Read(p),"current"); // Rotation never truncates current before opening.
    std::filesystem::remove(two); CHECK(std::filesystem::create_directory(two));
    plan=PrepareHostLog(p,config,false); CHECK(plan.append); CHECK_EQ(Read(p),"current"); CHECK_EQ(Read(one),"current");
    std::filesystem::remove(two); config.logging_mode=HostLogMode::Basic;
    plan=PrepareHostLog(p,config,false); CHECK(!plan.append); CHECK_EQ(Read(one),"current");
    config.logging_mode=HostLogMode::Debug; config.preserve_last=0;
    plan=PrepareHostLog(p,config,false); CHECK(!plan.append); CHECK_EQ(Read(one),"current");
    config.preserve_last=21; CHECK(PrepareHostLog(p,config,false).append);
    config.preserve_last=20; CHECK(!PrepareHostLog(p,config,false).append);
    std::filesystem::remove(p); config.preserve_last=1;
    CHECK(!PrepareHostLog(p,config,false).append); CHECK(std::filesystem::exists(one));
    CHECK(!std::filesystem::exists(two));
    CHECK(std::filesystem::create_directory(p));
    const auto unsafe=PrepareHostLog(p,config,false); CHECK(unsafe.append); CHECK(unsafe.use_stderr);
    CHECK(std::filesystem::is_directory(p)); CHECK_EQ(Read(dir/"unrelated.log"),"evidence");
    std::filesystem::remove_all(dir);
}
