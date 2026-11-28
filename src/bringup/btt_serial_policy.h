#pragma once
#include "audio_output_selection.h"
#include "instrument_identity.h"
#include "midi_note_delay_protocol.h"
#include "sample_waveform_protocol.h"
#include "session_lineage.h"
#include "wall_clock.h"
#include "btt_save_protocol.h"

namespace BroTracker {
// Shared with the production dispatcher. A request is classified by its complete
// grammar, never by a protocol namespace prefix. Existing handlers still own
// readiness, identity, parameter-range and resource checks.
enum class BttSerialCommand { Unknown, Query, PatternRead, WaveformRead, Hello,
    Transport, AudioSetup, MidiDelaySetup, ClockSetup, LineageClaim, Load, LoadStatus, Cancel,
    Save, SaveStatus, Recover, Media };
enum class BttSerialDisposition { Dispatch, Reject, EphemeralLineage };
inline BttSerialCommand ClassifyBttSerialCommand(std::string_view line) noexcept {
    using C=BttSerialCommand;
    if(line=="BTBTT1 STATUS")return C::LoadStatus;
    if(line=="BTBTT1 CANCEL")return C::Cancel;
    if(line=="BTBTT1 MEDIA 1")return C::Media;
    BttSaveRequest save;
    if(ParseBttSaveRequest(line,save))return save.action==BttSaveAction::Save?C::Save:
        save.action==BttSaveAction::Status?C::SaveStatus:C::Recover;
    if(line=="BTTEST1 HELLO")return C::Hello;
    if(line=="BTTEST1 START" || line=="BTTEST1 STOP" || line=="BTTEST1 PAUSE" || line=="BTTEST1 CONTINUE")return C::Transport;
    for(const auto query:{"BTTEST1 STATUS","BTSTREAM1 GET","BTDEVICE1 GET 1","BTTEMPO1 GET 1",
        "BTMUTE1 GET 1","BTVOL1 GET 1","BTGAIN1 GET 1","BTMIDI1 GET 1","BTINS1 COLLECTION GET 1"})
        if(line==query)return C::Query;
    unsigned slot=0,version=0;uint32_t id=0,f[6]{};
    if(ParseInstrumentIdentityRequest(line.data(),line.size(),slot) || ParseInstrumentFactsRequest(line.data(),line.size(),slot))return C::Query;
    if(ParseSnapshotRequest(line.data(),line.size(),id,version) ||
       (SnapshotFields(line.data(),line.size(),"BTPREVIEW1 GET 1 ",f,2) && f[0] && f[1]<=1))return C::PatternRead;
    if(ParseClockFields(line,"BTCLOCK1 GET ",f,2) && f[1])return C::Query;
    if(ParseClockFields(line,"BTCLOCK1 SET ",f,6) && f[1])return C::ClockSetup;
    MidiDelayRequest delay;
    if(ParseMidiDelayRequest(line.data(),line.size(),delay))return delay.set?C::MidiDelaySetup:C::Query;
    AudioOutputChoice route;bool swap=false;
    if(ParseAudioOutputRequest(line,route) || ParseAudioOutputRequestV2(line,route,swap))return C::AudioSetup;
    constexpr std::string_view claim="BTSESSION1 CLAIM 1 ";
    if(line.substr(0,claim.size())==claim && ValidSessionLineage(line.substr(claim.size())))return C::LineageClaim;
    // Readback transfer controls mutate only their own cursor/credits, not tune
    // state. Frozen publication and staging exclude these independent transfers.
    char path[kSamplePathMaximum+1];bool fingerprint=false;uint32_t waveform_slot=0;
    if(ParseWaveformGet(line,id,waveform_slot,path,&fingerprint))return C::WaveformRead;
    if((SnapshotFields(line.data(),line.size(),"BTWAVE1 NEXT ",f,3) ||
        SnapshotFields(line.data(),line.size(),"BTWAVE1 RESUME ",f,3)) && f[0]==1 && f[1])return C::WaveformRead;
    if(SnapshotFields(line.data(),line.size(),"BTWAVE1 RANGE ",f,4) && f[0]==1 && f[1])return C::WaveformRead;
    if(SnapshotFields(line.data(),line.size(),"BTWAVE1 CANCEL ",f,2) && f[0]==1 && f[1])return C::WaveformRead;
    constexpr std::string_view load="BTBTT1 LOAD ";
    if(line.substr(0,load.size())==load) {
        const auto split=line.find(' ',load.size());
        if(split!=line.npos && SnapshotFields(line.data(),split,load.data(),&id,1) && id &&
           ValidSamplePath(line.substr(split+1)))return C::Load;
    }
    return C::Unknown;
}
inline BttSerialDisposition BttSerialPolicy(std::string_view line,bool busy,bool retained,
    bool recovery=false,bool settled=true) noexcept {
    using D=BttSerialDisposition;using C=BttSerialCommand;
    if(!busy && !retained && !recovery)return D::Dispatch; // Ordinary dispatcher retains its full grammar.
    const auto command=ClassifyBttSerialCommand(line);
    if(recovery) {
        switch(command) {
        case C::Query:case C::LoadStatus:case C::SaveStatus:case C::Save:case C::Recover:return D::Dispatch;
        case C::Hello:case C::AudioSetup:case C::MidiDelaySetup:case C::ClockSetup:
            return !busy && settled?D::Dispatch:D::Reject;
        case C::LineageClaim:return !busy && settled?D::EphemeralLineage:D::Reject;
        default:return D::Reject;
        }
    }
    switch(command) {
    case C::Query:case C::LoadStatus:case C::Cancel:case C::Save:case C::SaveStatus:case C::Recover:case C::Media:return D::Dispatch;
    // LOAD still goes through the real stopped/admission guard. It cannot
    // overwrite a submitted owner's storage or silently stop transport.
    case C::Load:return busy?D::Reject:D::Dispatch;
    case C::PatternRead:case C::WaveformRead:case C::Hello:case C::Transport:
    case C::AudioSetup:case C::MidiDelaySetup:case C::ClockSetup:return busy?D::Reject:D::Dispatch;
    // BTX claims empty lineage on reconnect. A retained BTT is ephemeral: ACK
    // that exchange without establishing a legacy session identity or saving.
    case C::LineageClaim:return busy?D::Reject:D::EphemeralLineage;
    default:return D::Reject;
    }
}
}
