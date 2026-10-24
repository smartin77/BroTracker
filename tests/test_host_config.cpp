#include "test_framework.h"
#include "temporary_directory.h"
#include "ui/host_config.h"
#include "ui/host_logging.h"
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
    TemporaryTestDirectory fixture;
    const auto directory = fixture.Path() / "config";
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

TEST_CASE(HostConfigNoteDirectionDefaultsValidationAndSafeBypass) {
    using Direction=ValueStepDirection; const std::string btx="[btx]\nschema_version=1\nnew_pattern_length=32\n";
    HostConfig c; CHECK(c.value_step_direction==Direction::Normal);
    CHECK(ParseHostConfig(btx,c)==HostConfigStatus::Loaded); CHECK(c.value_step_direction==Direction::Normal);
    CHECK(ParseHostConfig(btx+"[editing]\nnote_step_direction=reversed",c)==HostConfigStatus::Loaded);
    CHECK(c.value_step_direction==Direction::Reversed);
    for(auto invalid : {"note_step_direction=other","note_step_direction=Reversed","note_step_direction=",
        "note_step_direction=reversed\nnote_step_direction=normal","unknown=normal","[editing]"}) {
        CHECK(ParseHostConfig(btx+"[editing]\n"+invalid,c)==HostConfigStatus::Invalid);
        CHECK(c.value_step_direction==Direction::Reversed); CHECK_EQ(c.new_pattern_length,32u);
    }
    CHECK(ParseHostConfig("[editing]\nnote_step_direction=normal\n"+btx,c)==HostConfigStatus::Loaded);
    CHECK(c.value_step_direction==Direction::Normal);
    const auto safe=LoadHostConfig(std::filesystem::path{},true);
    CHECK(safe.status==HostConfigStatus::SafeDefaults); CHECK(safe.config.value_step_direction==Direction::Normal);
}

TEST_CASE(ValueStepDirectionCanonicalAliasAndConflictRollback) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=32\n[editing]\n";
    HostConfig c;
    CHECK(ParseHostConfig(base+"value_step_direction=reversed",c)==HostConfigStatus::Loaded);
    CHECK(c.value_step_direction==ValueStepDirection::Reversed);
    CHECK(ParseHostConfig(base+"note_step_direction=normal",c)==HostConfigStatus::Loaded);
    CHECK(c.value_step_direction==ValueStepDirection::Normal);
    for(const auto invalid:{"value_step_direction=reversed\nnote_step_direction=normal",
        "note_step_direction=normal\nvalue_step_direction=normal", "value_step_direction=other",
        "value_step_direction=normal\nvalue_step_direction=normal"}) {
        CHECK(ParseHostConfig(base+invalid,c)==HostConfigStatus::Invalid);
        CHECK(c.value_step_direction==ValueStepDirection::Normal); CHECK_EQ(c.new_pattern_length,32u);
    }
}

TEST_CASE(HostConfigAudioPrecedenceAndInvalidFieldFallback) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=64\n";
    HostConfig c;
    CHECK(ParseHostConfig(base,c)==HostConfigStatus::Loaded);
    CHECK(c.audio_output==AudioOutputChoice::Default);
    for(const auto text : {"[audio]\noutput=", "[audio]\n output = \t ", "[audio]"}) {
        CHECK(ParseHostConfig(base+text,c)==HostConfigStatus::Loaded);
        CHECK(c.audio_output==AudioOutputChoice::Default); CHECK(!c.audio_output_invalid);
    }
    CHECK(ParseHostConfig(base+" [audio] \n output = pt8211 \r\n",c)==HostConfigStatus::Loaded);
    CHECK(c.audio_output==AudioOutputChoice::Pt8211);
    CHECK(ParseHostConfig(base+"[audio]\noutput=bridge",c)==HostConfigStatus::Loaded);
    CHECK(c.audio_output==AudioOutputChoice::Bridge);
    for(const auto value : {"PT8211","Bridge","shield","bad"}) {
        CHECK(ParseHostConfig(base+"[audio]\noutput="+value,c)==HostConfigStatus::Loaded);
        CHECK(c.audio_output_invalid); CHECK(c.audio_output==AudioOutputChoice::Default);
        CHECK_EQ(c.new_pattern_length,64u);
    }
    CHECK(ParseHostConfig(base+"[audio]\noutput=bridge\noutput=pt8211",c)==HostConfigStatus::Invalid);
    CHECK(ParseHostConfig(base+"[audio]\n[Audio]\noutput=bridge",c)==HostConfigStatus::Invalid);
    CHECK(LoadHostConfig({},true).config.audio_output==AudioOutputChoice::Default);
}
TEST_CASE(ProductionAudioSelectionLocksOnceAndReconnectIsIdempotent) {
    AudioOutputSelection selection;
    CHECK(selection.Active()==AudioOutputChoice::Default);
    CHECK(selection.CanSelect(AudioOutputChoice::Pt8211,AudioOutputChoice::Bridge));
    selection.Commit(AudioOutputChoice::Pt8211,AudioOutputChoice::Bridge);
    CHECK(selection.Active()==AudioOutputChoice::Pt8211);
    CHECK(selection.CanSelect(AudioOutputChoice::Pt8211,AudioOutputChoice::Bridge));
    CHECK(!selection.CanSelect(AudioOutputChoice::Bridge,AudioOutputChoice::Bridge));
    CHECK(selection.CanSelect(AudioOutputChoice::Default,AudioOutputChoice::Bridge));
    selection.Commit(AudioOutputChoice::Default,AudioOutputChoice::Bridge);
    CHECK(selection.Active()==AudioOutputChoice::Pt8211);
    selection.Commit(AudioOutputChoice::Bridge,AudioOutputChoice::Bridge);
    CHECK(selection.Active()==AudioOutputChoice::Pt8211);
    AudioOutputSelection fallback;
    fallback.Commit(AudioOutputChoice::Default,AudioOutputChoice::Bridge);
    CHECK(fallback.Active()==AudioOutputChoice::Bridge);
    CHECK(fallback.CanSelect(AudioOutputChoice::Bridge,AudioOutputChoice::Bridge));
}

TEST_CASE(ProductionAudioRequestStrictParsing) {
    AudioOutputChoice c=AudioOutputChoice::Default;
    CHECK(ParseAudioOutputRequest("BTAUDIO1 SET 1 pt8211",c));CHECK(c==AudioOutputChoice::Pt8211);
    CHECK(ParseAudioOutputRequest("BTAUDIO1 SET 1 bridge",c));CHECK(c==AudioOutputChoice::Bridge);
    CHECK(ParseAudioOutputRequest("BTAUDIO1 SET 1 default",c));CHECK(c==AudioOutputChoice::Default);
    for(const auto bad : {"BTAUDIO1 SET 2 bridge","BTAUDIO1 SET 1 PT8211","BTAUDIO1 SET 1 shield",
                         "BTAUDIO1 SET 1 bridge extra","BTAUDIO1 SET 1 ","BTAUDIO1"})
        CHECK(!ParseAudioOutputRequest(bad,c));
}

TEST_CASE(RowNumberingConfigPrecedenceDefaultsInvalidAndTransitions) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=64\n";
    HostConfig c;CHECK(c.row_numbering==RowNumbering::D1);
    struct Case {const char* name;RowNumbering mode;const char* labels[5];};
    const Case cases[]={
        {"d1",RowNumbering::D1,{"01","10","16","17","32"}},
        {"d0",RowNumbering::D0,{"00","09","15","16","31"}},
        {"h1",RowNumbering::H1,{"01","0A","10","11","20"}},
        {"h0",RowNumbering::H0,{"00","09","0F","10","1F"}}};
    for(const auto& item:cases) {
        CHECK(ParseHostConfig(base+"[display]\nrow_numbering="+item.name,c)==HostConfigStatus::Loaded);
        CHECK(c.row_numbering==item.mode);CHECK(!c.row_numbering_invalid);CHECK_EQ(std::string(RowNumberingName(c.row_numbering)),item.name);
        unsigned index=0;for(unsigned row:{0u,9u,15u,16u,31u}) CHECK_EQ(FormatRowNumber(row,c.row_numbering),item.labels[index++]);
        CHECK_EQ(c.new_pattern_length,64u);
    }
    for(const auto suffix:{"","[display]","[display]\nrow_numbering=","[display]\nrow_numbering= \t"}) {
        CHECK(ParseHostConfig(base+suffix,c)==HostConfigStatus::Loaded);CHECK(c.row_numbering==RowNumbering::D1);CHECK(!c.row_numbering_invalid);
    }
    for(const auto value:{"D1","H0","hex","d2","1","d1 ; comment"}) {
        CHECK(ParseHostConfig(base+"[display]\nrow_numbering="+value,c)==HostConfigStatus::Loaded);
        CHECK(c.row_numbering==RowNumbering::D1);CHECK(c.row_numbering_invalid);CHECK_EQ(c.new_pattern_length,64u);
    }
    for(const auto suffix:{"[display]\nrow_numbering=d0\nrow_numbering=h1","[display]\n[display]","[display]\nunknown=h0"})
        CHECK(ParseHostConfig(base+suffix,c)==HostConfigStatus::Invalid);
}
TEST_CASE(DocumentedExampleParsesWithinBoundAndNeverProvidesRuntimeFallback) {
    std::ifstream source("config/config.ini.example",std::ios::binary);CHECK(bool(source));
    const std::string text{std::istreambuf_iterator<char>(source),{}};CHECK(text.size()<=kMaxHostConfigBytes);
    HostConfig c;CHECK(ParseHostConfig(text,c)==HostConfigStatus::Loaded);CHECK(c.audio_output==AudioOutputChoice::Bridge);
    CHECK(c.row_numbering==RowNumbering::D1);CHECK(c.logging_mode==HostLogMode::Debug);CHECK_EQ(c.preserve_last,5u);
    TemporaryTestDirectory dir;const auto config=dir.Path()/"config.ini";
    {std::ofstream example(dir.Path()/"config.ini.example");example<<text;}
    auto result=LoadHostConfig(config,false);CHECK(result.status==HostConfigStatus::Missing);
    CHECK(result.config.logging_mode==HostLogMode::Basic);CHECK_EQ(result.config.preserve_last,0u);
    CHECK(result.config.audio_output==AudioOutputChoice::Default);CHECK(!std::filesystem::exists(config));
    {std::ofstream file(config);file<<"[btx]\nschema_version=1\nnew_pattern_length=32\n[display]\nrow_numbering=h0";}
    result=LoadHostConfig(config,false);CHECK(result.config.row_numbering==RowNumbering::H0);
    result=LoadHostConfig(config,true);CHECK(result.status==HostConfigStatus::SafeDefaults);CHECK(result.config.row_numbering==RowNumbering::D1);
    std::ifstream unchanged(config);const std::string content{std::istreambuf_iterator<char>(unchanged),{}};CHECK(content.find("row_numbering=h0")!=std::string::npos);
}
TEST_CASE(InvalidRowNumberingHasUsefulBasicStartupWarning) {
    FILE* file=std::tmpfile();CHECK(file!=nullptr);if(!file)return;
    HostConfigResolution result;result.status=ParseHostConfig("[btx]\nschema_version=1\nnew_pattern_length=32\n[display]\nrow_numbering=bad",result.config);
    ConfigureHostLogging(HostLogMode::Basic);LogHostStartup(file,"fixture",result,std::filesystem::absolute("config.ini"),{});
    std::rewind(file);std::string log;char buffer[512];while(std::fgets(buffer,sizeof(buffer),file)) log+=buffer;std::fclose(file);
    CHECK(log.find("expected lowercase d1/d0/h1/h0; using d1")!=std::string::npos);
}

TEST_CASE(BridgeAudibleConfigIsOptInSharedAndFieldLocalOnInvalidValues) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=64\n";
    HostConfig c;CHECK(!c.bridge_audible_test);
    CHECK(ParseHostConfig(base,c)==HostConfigStatus::Loaded);CHECK(!c.bridge_audible_test);
    for(auto value:{"yes","on","true","1"}) {
        CHECK(ParseHostConfig(base+"[diagnostics]\nbridge_audible_test="+value,c)==HostConfigStatus::Loaded);
        CHECK(c.bridge_audible_test);CHECK(!c.bridge_audible_test_invalid);
    }
    for(auto value:{"no","off","false","0",""," \t"}) {
        CHECK(ParseHostConfig(base+"[diagnostics]\nbridge_audible_test="+value,c)==HostConfigStatus::Loaded);
        CHECK(!c.bridge_audible_test);CHECK(!c.bridge_audible_test_invalid);
    }
    for(auto value:{"YES","True","2","-1","yes ; inline comment"}) {
        CHECK(ParseHostConfig(base+"[diagnostics]\nbridge_audible_test="+value,c)==HostConfigStatus::Loaded);
        CHECK(!c.bridge_audible_test);CHECK(c.bridge_audible_test_invalid);CHECK_EQ(c.new_pattern_length,64u);
    }
    CHECK(ParseHostConfig(base+"[diagnostics]\nbridge_audible_test=yes\nbridge_audible_test=no",c)==HostConfigStatus::Invalid);
    CHECK(ParseHostConfig(base+"[diagnostics]\n[diagnostics]",c)==HostConfigStatus::Invalid);
    CHECK(!LoadHostConfig({},true).config.bridge_audible_test);
}

TEST_CASE(ExecutableRelativeConfigIgnoresLaunchDirectoryAndExample) {
    TemporaryTestDirectory temp;
    const auto installed=temp.Path()/"installed";
    const auto elsewhere=temp.Path()/"other-working-directory";
    std::filesystem::create_directories(installed/"config");
    std::filesystem::create_directories(elsewhere/"config");
    struct RestoreCwd { std::filesystem::path original=std::filesystem::current_path();
        ~RestoreCwd(){std::filesystem::current_path(original);} } restore;
    std::filesystem::current_path(elsewhere);
    {std::ofstream example(installed/"config/config.ini.example");example<<"invalid example";}
    {std::ofstream unrelated(elsewhere/"config/config.ini");unrelated<<"invalid CWD config";}
    const auto path=ExecutableConfigPath(installed);
    CHECK(path==installed/"config/config.ini");CHECK(path.is_absolute());
    auto result=LoadHostConfig(path,false);
    CHECK(result.status==HostConfigStatus::Missing);
    CHECK(result.config.row_numbering==RowNumbering::D1);
    CHECK(result.config.audio_output==AudioOutputChoice::Default);
    CHECK(!std::filesystem::exists(path));
    const std::string bytes="[btx]\r\nschema_version=1\r\nnew_pattern_length=32\r\n[audio]\r\noutput=pt8211\r\n[display]\r\nrow_numbering=h0\r\n";
    {std::ofstream actual(path,std::ios::binary);actual<<bytes;}
    result=LoadHostConfig(path,false);CHECK(result.status==HostConfigStatus::Loaded);
    CHECK(result.config.audio_output==AudioOutputChoice::Pt8211);
    CHECK(result.config.row_numbering==RowNumbering::H0);
    std::ifstream unchanged(path,std::ios::binary);
    CHECK_EQ(std::string(std::istreambuf_iterator<char>(unchanged),{}),bytes);
    CHECK(ExecutableConfigPath({}).empty());
    CHECK(ExecutableConfigPath("relative-executable-directory").empty());
    CHECK(LoadHostConfig(ExecutableConfigPath({}),false).status==HostConfigStatus::Unreadable);
}
TEST_CASE(HostDevelopmentStartupPatternConfiguration) {
    const std::string base="[btx]\nschema_version=1\nnew_pattern_length=32\n";
    HostConfig c;CHECK(ParseHostConfig(base,c)==HostConfigStatus::Loaded);CHECK_EQ(std::string(c.startup_pattern.stem),"none");
    for(const std::string value:std::vector<std::string>{"","none","NONE","patern4","patern4.btdev","none.btdev","ambient_Take-7","4",std::string(24,'a'),"../x","x/y","x\\y","x.btdev","\"patern4\"","has space",std::string(25,'a')}) {
        CHECK(ParseHostConfig(base+"[development]\nstartup_pattern="+value,c)==HostConfigStatus::Loaded);
        const bool valid=value.empty() || ValidDevelopmentSelection(value);
        CHECK_EQ(c.startup_pattern_invalid,!valid);
        CHECK_EQ(std::string(c.startup_pattern.stem),!valid || value.empty()?"none":value);
    }
    CHECK(ParseHostConfig(base+"[development]\nstartup_pattern=patern4\nstartup_pattern=patern1",c)==HostConfigStatus::Invalid);
}
