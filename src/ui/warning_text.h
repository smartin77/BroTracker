#pragma once
#include "warning_locale.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
// Minimal host-only warning catalog. Selection/reload is independent of drawing.
class WarningText {
public:
    enum Key { TeensyHeading, TeensyBody, SdHeading, SdBody, Instrument, Type, Name, Load, Save, FineTune, Unavailable, Sample, Midi, SineSynth, Original, Count };
    WarningText() { Reset(); }
    const std::string& Get(Key key) const {return values_[key];}
    void Parse(std::string_view source) {
        Reset(); bool warnings=false;
        std::istringstream input{std::string(source)}; std::string line;
        while(std::getline(input,line)) {
            if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
            auto trim=[](std::string s) {auto a=s.find_first_not_of(" \t\r");return a==s.npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);};
            line=trim(line);if(line.empty() || line[0]=='#' || line[0]==';')continue;
            if(line[0]=='[') {warnings=line=="[warnings]";continue;}
            const auto eq=line.find('=');if(!warnings || eq==line.npos)continue;
            const auto key=trim(line.substr(0,eq)); auto value=trim(line.substr(eq+1));
            if(value.empty())continue;
            std::string decoded;
            for(size_t i=0;i<value.size();++i) {
                if(value[i]=='\\' && i+1<value.size() && value[i+1]=='n') {decoded+='\n';++i;}
                else decoded+=value[i];
            }
            for(unsigned i=0;i<Count;++i)if(key==keys_[i])values_[i]=decoded;
        }
    }
    void Select(const std::filesystem::path& assets,std::string language="en_US") {
        Reset();char locale[32];
        if(!NormalizeWarningLocale(language,locale,sizeof(locale)))return;
        std::ifstream input(assets/"translations"/(std::string(locale)+".ini"),std::ios::binary);
        if(input) {std::ostringstream data;data<<input.rdbuf();Parse(data.str());}
    }
private:
    void Reset() {values_={"TEENSY BOARD NOT CONNECTED","Connect the Teensy board to continue.","SD CARD UNAVAILABLE","The application cannot continue without an SD card.","Instrument","Type","Name","Load","Save","Fine Tune","unavailable","Sample","MIDI","Sine Synth","Original"};}
    inline static constexpr std::array<const char*,Count> keys_{"teensy_heading","teensy_body","sd_heading","sd_body","instrument","instrument_type","instrument_name","instrument_load","instrument_save","instrument_fine_tune","instrument_unavailable","instrument_sample","instrument_midi","instrument_sine_synth","instrument_original"};
    std::array<std::string,Count> values_;
};
