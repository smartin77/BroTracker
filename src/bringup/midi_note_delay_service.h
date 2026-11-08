#pragma once
#include "midi_note_delay_protocol.h"
#include "midi_output.h"
namespace BroTracker {
static_assert(kMaximumMidiNoteDelayMs==MidiOutput::MaximumNoteDelayMs,"Protocol and engine range must agree");
struct MidiDelayReply {
    enum class Kind { Error, State, Selected } kind=Kind::Error;
    std::uint32_t id=0,intent=0,effective=0,condition=1;
};
// Caller owns/excludes MIDI IRQ access. Output facts are read, never selected.
template<class Output> MidiDelayReply ApplyMidiDelayRequest(const MidiDelayRequest& request,
    bool connected,bool stopped,const Output& output,MidiOutput& midi) {
    MidiDelayReply reply;reply.id=request.id;reply.intent=request.ms;
    if(!connected)return reply;
    const auto result=output.Initialization()!=AudioInitialization::Initialized ? MidiOutput::DelaySetting::AudioNotReady :
        request.set?midi.SetNoteDelayMs(request.ms,output.Active(),output.Initialization(),stopped):midi.NoteDelayReadiness(stopped);
    reply.effective=midi.NoteDelayUs()/1000;reply.condition=static_cast<unsigned>(result);
    if(!request.set)reply.kind=MidiDelayReply::Kind::State;
    else if(result==MidiOutput::DelaySetting::Applied)reply.kind=MidiDelayReply::Kind::Selected;
    return reply;
}
inline std::size_t FormatMidiDelayReply(const MidiDelayReply& r,char* out,std::size_t cap) {
    int n;
    if(r.kind==MidiDelayReply::Kind::State)n=std::snprintf(out,cap,"BTMIDIDELAY1 STATE 1 %lu %lu %lu\n",(unsigned long)r.id,(unsigned long)r.effective,(unsigned long)r.condition);
    else if(r.kind==MidiDelayReply::Kind::Selected)n=std::snprintf(out,cap,"BTMIDIDELAY1 SELECTED 1 %lu %lu %lu\n",(unsigned long)r.id,(unsigned long)r.intent,(unsigned long)r.effective);
    else n=std::snprintf(out,cap,"BTMIDIDELAY1 ERROR 1 %lu %lu\n",(unsigned long)r.id,(unsigned long)r.condition);
    return n<0 || static_cast<std::size_t>(n)>=cap?0:static_cast<std::size_t>(n);
}
}
