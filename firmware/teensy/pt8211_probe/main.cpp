#include <Arduino.h>
#include <Audio.h>
#include <new>
#include "record_transport.h"

namespace {
constexpr unsigned kTrials=64, kObservations=8, kControls=100, kCadenceCapacity=4096;
constexpr uint32_t kSettleUs=100, kTimeoutUs=100, kObservationUs=10;
const uint8_t pins[]={7,20,21};
struct Trial { uint8_t pin, target, initial, final; uint16_t observations; uint32_t cycles; bool timeout; };
Trial trials[3*2*kTrials];
volatile uint32_t intervals[kCadenceCapacity], intervalCount=0, callbacks=0, lastCycle=0;
volatile bool collecting=false;
class Cadence : public AudioStream {
    audio_block_t* queue[1];
public:
    Cadence():AudioStream(1,queue) {}
    void update() override {
        auto* block=receiveReadOnly(); if(block) release(block);
        if(!collecting) return;
        const uint32_t now=ARM_DWT_CYCCNT;
        if(callbacks && intervalCount<kCadenceCapacity) intervals[intervalCount++]=now-lastCycle;
        lastCycle=now; ++callbacks;
    }
};
struct Graph {
    AudioSynthWaveformSine left,right;
    AudioEffectEnvelope le,re;
    Cadence cadence;
    AudioOutputPT8211 output;
    AudioConnection a,b,c,d,e;
    Graph():a(left,le),b(right,re),c(le,0,output,0),d(re,0,output,1),e(left,0,cadence,0) {}
};
// No AudioStream/output constructors run before the input-only measurements.
alignas(Graph) unsigned char graphStorage[sizeof(Graph)];
struct Control { uint32_t elapsed, count; float cpu, peak; unsigned memory, memoryPeak; };
Control controls[kControls];

ProbeWire::Dump dump; // Fixed static payload/frame storage.
struct ForegroundClock {
    uint32_t now() const { return millis(); }
    void idle() const { yield(); }
};
uint32_t CpuMicro(float value) { return uint32_t(double(value)*1000000.0+0.5); }

void MeasurePins() {
    unsigned index=0;
    for(auto pin:pins) pinMode(pin,INPUT_PULLDOWN);
    for(auto pin:pins) {
        for(auto other:pins) if(other!=pin) pinMode(other,INPUT_PULLDOWN);
        for(unsigned target=0;target<2;++target) for(unsigned n=0;n<kTrials;++n) {
        auto& r=trials[index++]; r.pin=pin; r.target=target;
        pinMode(pin,target?INPUT_PULLDOWN:INPUT_PULLUP);
        delayMicroseconds(kSettleUs);
        r.initial=digitalRead(pin);
        // Only internal pulls change; GPIO direction remains INPUT throughout.
        uint32_t mask; asm volatile("mrs %0, primask" : "=r"(mask)); __disable_irq();
        const uint32_t start=ARM_DWT_CYCCNT;
        pinMode(pin,target?INPUT_PULLUP:INPUT_PULLDOWN);
        const uint32_t limit=(F_CPU_ACTUAL/1000000)*kTimeoutUs;
        while(digitalRead(pin)!=target && uint32_t(ARM_DWT_CYCCNT-start)<limit) {}
        r.cycles=ARM_DWT_CYCCNT-start;
        r.final=digitalRead(pin); r.timeout=r.cycles>=limit || r.final!=target;
        if(!mask) __enable_irq();
        // Settled repeated observations, bit i is observation i (HIGH=1).
        delayMicroseconds(kSettleUs);
        r.observations=0;
        for(unsigned i=0;i<kObservations;++i) {
            r.observations|=digitalRead(pin)<<i; delayMicroseconds(kObservationUs);
        }
    }
    }
    for(auto pin:pins) pinMode(pin,INPUT); // Remove pulls before normal peripheral mux setup.
}
void Envelope(AudioEffectEnvelope& e) {
    e.delay(0); e.attack(10); e.hold(0); e.decay(0); e.sustain(1); e.release(10);
}
void Run() {
    MeasurePins();
    auto* g=new(graphStorage) Graph; // Placement construction, fixed storage, once per boot.
    AudioMemory(16); // Output already owns updates; avoids AudioMemory's timer fallback.
    AudioNoInterrupts();
    g->left.frequency(440); g->right.frequency(880);
    g->left.amplitude(0.08f); g->right.amplitude(0.08f);
    Envelope(g->le); Envelope(g->re);
    AudioProcessorUsageMaxReset(); AudioMemoryUsageMaxReset();
    collecting=true;
    AudioInterrupts();
    const uint32_t start=millis(); uint32_t phaseStart=start; unsigned phase=0;
    for(unsigned i=0;i<kControls;) {
        const uint32_t now=millis();
        // Initial long silence, L250, silence512, R250, silence1512; ramps 10ms.
        const uint32_t duration=phase==0?1512:phase==2?512:250;
        if(uint32_t(now-phaseStart)>=duration) {
            phaseStart=now;
            if(phase==0) { g->le.noteOn(); phase=1; }
            else if(phase==1) { g->le.noteOff(); phase=2; }
            else if(phase==2) { g->re.noteOn(); phase=3; }
            else { g->re.noteOff(); phase=0; }
        }
        if(uint32_t(now-start)>=(i+1)*100) {
            controls[i++]={uint32_t(now-start),callbacks,AudioProcessorUsage(),
                AudioProcessorUsageMax(),AudioMemoryUsage(),AudioMemoryUsageMax()};
        }
    }
    AudioNoInterrupts(); collecting=false; AudioInterrupts();
    g->le.noteOff(); g->re.noteOff();
    ForegroundClock clock;
    if(!dump.Record(Serial,clock,false,"{\"type\":\"meta\",\"schema\":2,\"source_sha256\":\"%s\",\"build\":\"%s %s\",\"teensyduino\":%d,\"cpu_hz\":%lu,\"trials\":%u,\"observations\":%u,\"settle_us\":%lu,\"timeout_us\":%lu,\"observation_us\":%lu,\"control_ms\":10000,\"block_samples\":%d,\"sample_rate\":44100,\"ack_ms\":2000,\"dump_ms\":60000,\"record_retries\":0}",
        PROBE_SOURCE_SHA,__DATE__,__TIME__,TEENSYDUINO,F_CPU_ACTUAL,kTrials,kObservations,kSettleUs,kTimeoutUs,kObservationUs,AUDIO_BLOCK_SAMPLES)) return;
    for(const auto& r:trials) if(!dump.Record(Serial,clock,false,"{\"type\":\"pin\",\"pin\":%u,\"target\":%u,\"initial\":%u,\"final\":%u,\"bits\":%u,\"cycles\":%lu,\"timeout\":%s}",r.pin,r.target,r.initial,r.final,r.observations,r.cycles,r.timeout?"true":"false")) return;
    for(const auto& c:controls) {
        const uint32_t cpu=CpuMicro(c.cpu),peak=CpuMicro(c.peak);
        if(!dump.Record(Serial,clock,false,"{\"type\":\"control\",\"elapsed_ms\":%lu,\"callbacks\":%lu,\"cpu\":%lu.%06lu,\"cpu_peak\":%lu.%06lu,\"memory\":%u,\"memory_peak\":%u}",c.elapsed,c.count,cpu/1000000,cpu%1000000,peak/1000000,peak%1000000,c.memory,c.memoryPeak)) return;
    }
    for(uint32_t i=0;i<intervalCount;++i) if(!dump.Record(Serial,clock,false,"{\"type\":\"cadence\",\"cycles\":%lu}",intervals[i])) return;
    (void)dump.Record(Serial,clock,true,"{\"type\":\"end\",\"callbacks\":%lu,\"intervals\":%lu,\"cadence_overflow\":%s,\"records\":%lu,\"stream_crc32\":\"%08lx\",\"short_writes\":%lu,\"zero_writes\":%lu,\"record_retries\":0}",callbacks,intervalCount,callbacks>kCadenceCapacity+1?"true":"false",dump.sequence+1,~dump.stream,dump.shortWrites,dump.zeroWrites);
}
bool done=false;
}
void setup() {
    Serial.begin(115200);
    ARM_DEMCR|=ARM_DEMCR_TRCENA; ARM_DWT_CTRL|=ARM_DWT_CTRL_CYCCNTENA;
}
void loop() {
    static char command[8]; static unsigned length=0;
    if(!done && Serial.available()) {
        const char c=Serial.read();
        if(c=='\n') { command[length]=0; if(!strcmp(command,"GO")) { done=true; Run(); } length=0; }
        else if(c!='\r') { if(length<sizeof(command)-1) command[length++]=c; else length=0; }
    }
}
