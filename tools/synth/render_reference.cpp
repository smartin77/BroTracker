// Host-only reference export; audio is produced by the production block renderer.
#include "core/audio/variable_rate_voice_block_renderer.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <iostream>
using namespace BroTracker;
static void U16(std::ostream& s,unsigned v) { s.put(char(v)); s.put(char(v>>8)); }
static void U32(std::ostream& s,unsigned v) { U16(s,v); U16(s,v>>16); }
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    constexpr unsigned rate=44100,segment=48510;
    std::vector<std::int16_t> result;
    for(unsigned scenario=0;scenario<5;++scenario) {
        VariableRateVoiceBlockRenderer renderer;
        if(renderer.Configure(rate)!=VariableRateBlockStatus::Success) return 3;
        std::int16_t pcm[8][128]; VariableRateVoiceOutputs outputs;
        for(unsigned c=0;c<8;++c) outputs.channels[c]=pcm[c];
        const unsigned notes[]={24,60,69,127,69};
        unsigned next=0; const unsigned times[]={0,11025,11100,22050};
        for(unsigned position=0;position<segment;) {
            const auto frames=std::min(128u,segment-position); VariableRateVoiceCommandBatch batch;
            while(next<(scenario==4 ? 4u : 1u) && times[next]<position+frames) {
                VariableRateVoiceCommand command; command.channel=0; command.sample_offset=times[next]-position;
                command.action=(scenario==4 && next==3) ? VariableRateVoiceAction::Stop : VariableRateVoiceAction::TriggerSynth;
                command.synth_note=static_cast<Note>(scenario==4 && next>0 ? (next==1 ? 72 : 60) : notes[scenario]);
                batch.commands[batch.count++]=command; ++next;
            }
            if(renderer.Render(outputs,frames,batch)!=VariableRateBlockStatus::Success) return 4;
            result.insert(result.end(),pcm[0],pcm[0]+frames); position+=frames;
        }
    }
    std::ofstream wav(argv[1],std::ios::binary); const unsigned bytes=unsigned(result.size()*2);
    wav.write("RIFF",4); U32(wav,36+bytes); wav.write("WAVEfmt ",8); U32(wav,16); U16(wav,1); U16(wav,1);
    U32(wav,rate); U32(wav,rate*2); U16(wav,2); U16(wav,16); wav.write("data",4); U32(wav,bytes);
    for(auto value:result) U16(wav,static_cast<std::uint16_t>(value));
    if(!wav) return 5;
    std::cout<<"Exported "<<result.size()<<" frames through VariableRateVoiceBlockRenderer\n";
}
