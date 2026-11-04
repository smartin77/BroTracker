#include "test_framework.h"
#include "temporary_directory.h"
#include "ui/warning_text.h"
#include "ui/host_config.h"
TEST_CASE(WarningCatalogUtf8HardBreaksAndFallback) {
    WarningText text;
    text.Parse(u8"[warnings]\nsd_body=Pr\u00edli\u0161 \u017elt\u00fd k\u00f4\u0148\\n\u013d\u0160\u010c\u0164\u017d\nteensy_heading=TEST\n");
    CHECK_EQ(text.Get(WarningText::SdBody),u8"Pr\u00edli\u0161 \u017elt\u00fd k\u00f4\u0148\n\u013d\u0160\u010c\u0164\u017d");
    CHECK_EQ(text.Get(WarningText::TeensyBody),"Connect the Teensy board to continue.");
    text.Parse("[warnings]\nsd_heading=\nmalformed\n");
    CHECK_EQ(text.Get(WarningText::SdHeading),"SD CARD UNAVAILABLE");
    text.Select("not-an-asset-directory","sk");
    CHECK_EQ(text.Get(WarningText::TeensyHeading),"TEENSY BOARD NOT CONNECTED");
    text.Select("assets","en");
    CHECK_EQ(text.Get(WarningText::SdBody),"The application cannot continue without an SD card.");
    text.Select("assets","../../escape");
    CHECK_EQ(text.Get(WarningText::SdHeading),"SD CARD UNAVAILABLE");
}
TEST_CASE(OptionalWarningLanguageKeepsExistingConfigCompatible) {
    BroTracker::HostConfig c;
    const std::string old="[btx]\nschema_version=1\nnew_pattern_length=32\n";
    CHECK(BroTracker::ParseHostConfig(old,c)==BroTracker::HostConfigStatus::Loaded);
    CHECK_EQ(std::string(c.language),"en_US");
    CHECK(BroTracker::ParseHostConfig(old+"language=sk_sk\n",c)==BroTracker::HostConfigStatus::Loaded);
    CHECK_EQ(std::string(c.language),"sk_SK");
    CHECK(BroTracker::ParseHostConfig(old+"language=../bad\n",c)==BroTracker::HostConfigStatus::Loaded);
    CHECK_EQ(std::string(c.language),"en_US");
}

TEST_CASE(CanonicalLocaleLoadsEditedIniInsteadOfEnglishFallback) {
    TemporaryTestDirectory temp;std::filesystem::create_directories(temp.Path()/"translations");
    const auto english=temp.Path()/"translations/en_US.ini";
    {std::ofstream file(english);file<<"[warnings]\nsd_body=Edited English from the INI\n";}
    WarningText text;
    for(const auto* language:{"en_US","en","en_us",""}) {
        BroTracker::HostConfig config;
        const std::string ini="[btx]\nschema_version=1\nnew_pattern_length=32\nlanguage="+std::string(language)+"\n";
        CHECK(BroTracker::ParseHostConfig(ini,config)==BroTracker::HostConfigStatus::Loaded);
        CHECK_EQ(std::string(config.language),"en_US");
        text.Select(temp.Path(),config.language);
        CHECK_EQ(text.Get(WarningText::SdBody),"Edited English from the INI");
        CHECK_EQ(text.Get(WarningText::TeensyHeading),"TEENSY BOARD NOT CONNECTED");
    }
    {std::ofstream file(english);file<<"[warnings]\nsd_body=Reloaded English\n";}
    text.Select(temp.Path());CHECK_EQ(text.Get(WarningText::SdBody),"Reloaded English");
    {std::ofstream file(temp.Path()/"translations/sk_SK.ini");file<<"[warnings]\nsd_body=Slovak catalog sentinel\n";}
    BroTracker::HostConfig config;
    CHECK(BroTracker::ParseHostConfig("[btx]\nschema_version=1\nnew_pattern_length=32\nlanguage=sk_SK\n",config)==BroTracker::HostConfigStatus::Loaded);
    text.Select(temp.Path(),config.language);CHECK_EQ(text.Get(WarningText::SdBody),"Slovak catalog sentinel");
    text.Select(temp.Path(),"../en_US");CHECK_EQ(text.Get(WarningText::SdBody),"The application cannot continue without an SD card.");
}
