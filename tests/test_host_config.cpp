#include "test_framework.h"
#include "ui/host_config.h"
#include <fstream>
#include <string>
using namespace BroTracker;
TEST_CASE(HostConfigDefaultsAndSupportedLengths) {
    HostConfig config;
    CHECK_EQ(config.schema_version, 1); CHECK_EQ(config.new_pattern_length, 32);
    for (unsigned rows : {16u,32u,64u}) {
        const auto text = "; comment\r\n[btx]\r\n schema_version = 1\nnew_pattern_length=" + std::to_string(rows);
        CHECK(ParseHostConfig(text, config) == HostConfigStatus::Loaded);
        CHECK_EQ(config.new_pattern_length, rows);
    }
}
TEST_CASE(HostConfigTransactionalValidationAndBounds) {
    HostConfig config; config.new_pattern_length = 64;
    for (const auto text : {"", "[btx]\nschema_version=2\nnew_pattern_length=16",
         "[btx]\nschema_version=1\nnew_pattern_length=17",
         "[btx]\nschema_version=1\nnew_pattern_length=-16",
         "[btx]\nschema_version=1\nnew_pattern_length=4294967296",
         "[btx]\nschema_version=1\nnew_pattern_length=16x",
         "[btx]\nschema_version=1\nschema_version=1\nnew_pattern_length=16",
         "[btx]\nschema_version=1\nnew_pattern_length=16\nnew_pattern_length=32",
         "[btx]\nschema_version=1", "[other]\nschema_version=1\nnew_pattern_length=16",
         "[btx]\nschema_version=1\nnew_pattern_length=16\nunknown=1",
         "[btx]\n[btx]\nschema_version=1\nnew_pattern_length=16"}) {
        CHECK(ParseHostConfig(text, config) == HostConfigStatus::Invalid);
        CHECK_EQ(config.new_pattern_length,64); CHECK_EQ(config.schema_version,1);
    }
    std::string text = "[btx]\nschema_version=1\nnew_pattern_length=16\n#";
    text.resize(kMaxHostConfigBytes,'x');
    CHECK(ParseHostConfig(text, config) == HostConfigStatus::Loaded);
    text += 'x';
    CHECK(ParseHostConfig(text, config) == HostConfigStatus::TooLarge);
    CHECK_EQ(config.new_pattern_length,16);
}
TEST_CASE(HostConfigFileLoadingAndSafeRecovery) {
    const auto directory = std::filesystem::temp_directory_path() / "brotracker-host-config-test";
    CHECK(std::filesystem::create_directory(directory));
    const auto path = directory / "config.ini";
    CHECK(LoadHostConfig(path,false).status == HostConfigStatus::Missing);
    { std::ofstream file(path); file << "[btx]\nschema_version=1\nnew_pattern_length=64\n"; }
    auto result = LoadHostConfig(path,false);
    CHECK(result.status == HostConfigStatus::Loaded); CHECK_EQ(result.config.new_pattern_length,64);
    result = LoadHostConfig(path,true);
    CHECK(result.status == HostConfigStatus::SafeDefaults); CHECK_EQ(result.config.new_pattern_length,32);
    { std::ofstream file(path); file << "[btx]\nschema_version=1\nnew_pattern_length=64\nbad=1"; }
    result = LoadHostConfig(path,false);
    CHECK(result.status == HostConfigStatus::Invalid); CHECK_EQ(result.config.new_pattern_length,32);
    { std::ofstream file(path); file << std::string(kMaxHostConfigBytes+1,'x'); }
    result = LoadHostConfig(path,false);
    CHECK(result.status == HostConfigStatus::TooLarge); CHECK_EQ(result.config.new_pattern_length,32);
    CHECK(LoadHostConfig(directory,false).status == HostConfigStatus::Unreadable);
    CHECK(LoadHostConfig("relative.ini",false).status == HostConfigStatus::Unreadable);
    CHECK(LoadHostConfig(directory,true).status == HostConfigStatus::SafeDefaults);
    CHECK(std::filesystem::file_size(path)==kMaxHostConfigBytes+1); // Never rewritten.
    std::filesystem::remove(path); std::filesystem::remove(directory);
    char name[]="btx", safe[]="--safe-config", other[]="--safe-config-extra";
    char* args[]={name,other,safe};
    CHECK(!UseSafeConfig(2,args)); CHECK(UseSafeConfig(3,args));
    CHECK(!UseSafeConfig(0,nullptr));
}
