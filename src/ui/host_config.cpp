#include "host_config.h"
#include <array>
#include <fstream>
#include <system_error>

namespace BroTracker {
namespace {
std::string_view Trim(std::string_view s) noexcept {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}
bool Decimal(std::string_view s, std::uint32_t& value) noexcept {
    if (s.empty()) return false;
    std::uint32_t n = 0;
    for (const auto ch : s) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<unsigned>(ch - '0');
        if (n > (UINT32_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    value = n; return true;
}
}
HostConfigStatus ParseHostConfig(std::string_view text, HostConfig& output) noexcept {
    if (text.size() > kMaxHostConfigBytes) return HostConfigStatus::TooLarge;
    HostConfig candidate;
    unsigned section=0;
    bool display=false,numbering=false,diagnostics=false,audible=false;
    bool btx=false,logging=false,version=false,length=false,mode=false,preserve=false,editing=false,direction=false,audio=false,audio_output=false,midi=false,midi_channel=false;
    while (!text.empty()) {
        const auto end=text.find('\n');
        auto line=Trim(text.substr(0,end));
        text=end==text.npos ? std::string_view{} : text.substr(end+1);
        if(line.empty() || line.front()=='#' || line.front()==';') continue;
        if(line=="[btx]") { if(btx) return HostConfigStatus::Invalid; btx=true; section=1; continue; }
        if(line=="[logging]") { if(logging) return HostConfigStatus::Invalid; logging=true; section=2; continue; }
        if(line=="[editing]") {if(editing) return HostConfigStatus::Invalid; editing=true; section=3; continue;}
        if(line=="[audio]") {if(audio) return HostConfigStatus::Invalid; audio=true; section=4; continue;}
        if(line=="[midi]") {if(midi) return HostConfigStatus::Invalid; midi=true; section=5; continue;}
        if(line=="[display]") {if(display) return HostConfigStatus::Invalid; display=true; section=6; continue;}
        if(line=="[diagnostics]") {if(diagnostics) return HostConfigStatus::Invalid; diagnostics=true; section=7; continue;}
        if(!section) return HostConfigStatus::Invalid;
        const auto eq=line.find('='); if(eq==line.npos) return HostConfigStatus::Invalid;
        const auto key=Trim(line.substr(0,eq)), raw=Trim(line.substr(eq+1));
        std::uint32_t value=0;
        if(section==7 && key=="bridge_audible_test" && !audible) {
            audible=true;
            if(raw=="yes" || raw=="on" || raw=="true" || raw=="1") candidate.bridge_audible_test=true;
            else if(!raw.empty() && raw!="no" && raw!="off" && raw!="false" && raw!="0") candidate.bridge_audible_test_invalid=true;
        } else if(section==6 && key=="row_numbering" && !numbering) {
            numbering=true;
            if(raw=="d0") candidate.row_numbering=RowNumbering::D0;
            else if(raw=="h1") candidate.row_numbering=RowNumbering::H1;
            else if(raw=="h0") candidate.row_numbering=RowNumbering::H0;
            else if(!raw.empty() && raw!="d1") candidate.row_numbering_invalid=true;
        } else if(section==5 && key=="output_channel" && !midi_channel) {
            midi_channel=true;
            if(!raw.empty()) {
                if(Decimal(raw,value) && value>=1 && value<=16) candidate.midi_output_channel=value;
                else candidate.midi_output_invalid=true;
            }
        } else if(section==4 && key=="output" && !audio_output) {
            audio_output=true;
            if(raw=="bridge") candidate.audio_output=AudioOutputChoice::Bridge;
            else if(raw=="pt8211") candidate.audio_output=AudioOutputChoice::Pt8211;
            else if(!raw.empty()) candidate.audio_output_invalid=true;
        } else if(section==3 && (key=="value_step_direction" || key=="note_step_direction") && !direction) {
            if(raw!="normal" && raw!="reversed") return HostConfigStatus::Invalid;
            candidate.value_step_direction=raw=="reversed" ? ValueStepDirection::Reversed : ValueStepDirection::Normal;
            direction=true;
        } else if(section==2 && key=="mode" && !mode) {
            if(raw!="basic" && raw!="debug") return HostConfigStatus::Invalid;
            candidate.logging_mode=raw=="debug" ? HostLogMode::Debug : HostLogMode::Basic; mode=true;
        } else if(section==2 && key=="preserve_last" && !preserve) {
            if(!Decimal(raw,value) || value>20) return HostConfigStatus::Invalid;
            candidate.preserve_last=value; preserve=true;
        } else if(section==1 && key=="schema_version" && !version) {
            if(!Decimal(raw,value) || value!=1) return HostConfigStatus::Invalid;
            version=true; candidate.schema_version=value;
        } else if(section==1 && key=="new_pattern_length" && !length) {
            if(!Decimal(raw,value) || (value!=16 && value!=32 && value!=64)) return HostConfigStatus::Invalid;
            length=true; candidate.new_pattern_length=value;
        } else return HostConfigStatus::Invalid;
    }
    if(!btx || !version || !length) return HostConfigStatus::Invalid;
    output = candidate; return HostConfigStatus::Loaded;
}
bool UseSafeConfig(int argc, char* const argv[]) noexcept {
    for (int i = 1; argv && i < argc; ++i)
        if (argv[i] && std::string_view(argv[i]) == "--safe-config") return true;
    return false;
}
HostConfigResolution LoadHostConfig(const std::filesystem::path& path, bool safe) noexcept {
    HostConfigResolution result;
    if (safe) { result.status = HostConfigStatus::SafeDefaults; return result; }
    try {
        std::error_code error;
        if (!path.is_absolute()) { result.status = HostConfigStatus::Unreadable; return result; }
        const bool exists = std::filesystem::exists(path, error);
        if (error) { result.status = HostConfigStatus::Unreadable; return result; }
        if (!exists) return result;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            result.status = HostConfigStatus::Unreadable; return result;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) { result.status = HostConfigStatus::Unreadable; return result; }
        std::array<char, kMaxHostConfigBytes + 1> buffer{};
        file.read(buffer.data(), buffer.size());
        if (file.bad() || (!file.eof() && file.fail())) {
            result.status = HostConfigStatus::Unreadable; return result;
        }
        result.status = ParseHostConfig({buffer.data(), static_cast<std::size_t>(file.gcount())}, result.config);
    } catch (...) { result = {}; result.status = HostConfigStatus::Unreadable; }
    return result;
}
const char* HostConfigDiagnostic(HostConfigStatus status) noexcept {
    switch (status) {
    case HostConfigStatus::Loaded: return "config.ini loaded";
    case HostConfigStatus::Missing: return "config.ini absent; using built-in defaults";
    case HostConfigStatus::SafeDefaults: return "--safe-config: external config bypassed; using built-in defaults";
    case HostConfigStatus::TooLarge: return "config.ini exceeds 4096 bytes; using defaults. Reduce the file or use --safe-config";
    case HostConfigStatus::Invalid: return "Invalid config.ini; using defaults. Check required [btx] fields and optional [logging] mode=basic/debug, preserve_last=0..20 [audio] output=bridge/pt8211 and [editing] value_step_direction=normal/reversed (deprecated alias note_step_direction; use one name only), or use --safe-config";
    default: return "Cannot read config.ini; using defaults. Check path/permissions or use --safe-config";
    }
}
} // namespace BroTracker
