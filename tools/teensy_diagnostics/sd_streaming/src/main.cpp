#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <sd_access.h>
#include "core/audio/wav_buffer_loader.h"
#include <algorithm>
#include <cstring>
#include <new>
extern "C" {
 extern uint8_t external_psram_size;
 extern char _ebss, _estack, _heap_start, _heap_end;
}
using namespace BroTracker;
#ifndef STREAM_RING_BYTES
#define STREAM_RING_BYTES 16384
#endif
#ifndef STREAM_STAGING_BYTES
#define STREAM_STAGING_BYTES 16384
#endif
namespace {
constexpr uint32_t N=8, Capacity=STREAM_RING_BYTES/2, Head=2048,StageFrames=STREAM_STAGING_BYTES/2;
DMAMEM alignas(32) int16_t rings[N][Capacity];
DMAMEM alignas(32) int16_t heads[N][Head];
DMAMEM alignas(32) int16_t staging[StageFrames];
int16_t resident[70000];
constexpr size_t ResidentFrames=sizeof resident/sizeof resident[0];
SdFs& disk=SD.sdfs;
struct Reader : WavBufferReader {
 FsFile file;
 bool Size(uint64_t& n) noexcept override { n=file.fileSize(); return bool(file); }
 bool Seek(uint64_t n) noexcept override { return file.seekSet(n); }
 size_t Read(uint8_t* p,size_t n) noexcept override { int r=file.read(p,n); return r<0?0:r; }
};
struct Asset { char path[240]{}; uint32_t offset=0,frames=0,hash=2166136261u; } assets[N],shortAsset,largeAsset;
struct Voice {
 volatile uint32_t contentErrors=0,verifiedGenerations=0;
 uint32_t expectedFrames=0,expectedHash=0;
 volatile uint32_t generation=0,pos=0,begin=Head,end=Head,underruns=0,minReady=UINT32_MAX,eofs=0,hash=2166136261u;
 volatile bool active=false;
 uint32_t errors=0,stale=0,reads=0,maxRead=0,totalRead=0,hist[2048]{};
 Reader reader;
} voices[N];
volatile bool running=false,ramMode=false,audioReady=false;
volatile uint32_t ticks=0,maxCycles=0,blocksLost=0,retriggerEvery=0,retriggerCount=0;
uint32_t chunk=4096,threshold=Capacity/2,voiceCount=1;
bool storageReady=false,outputReady=false;
bool readHeld=false;
constexpr const char* FixtureDir="/BroTracker/SDStreamPOC-20261106";
static_assert(STREAM_RING_BYTES==8192 || STREAM_RING_BYTES==16384 || STREAM_RING_BYTES==32768,"supported ring sizes");
uint32_t hashSample(uint32_t hash,int16_t x) { return (hash^uint16_t(x))*16777619u; }
void trigger(unsigned i) { // ISR owner, no I/O or copying; starts from immutable pre-cache.
 auto& v=voices[i];
 if(v.active && v.expectedFrames && v.pos==v.expectedFrames) {
  ++v.verifiedGenerations; if(v.hash!=v.expectedHash) ++v.contentErrors;
 }
 ++v.generation; v.pos=0; v.begin=Head; v.end=Head; v.hash=2166136261u; v.active=true;
}
class Source : public AudioStream {
public: Source():AudioStream(0,nullptr) {}
 void update() override {
  if(!audioReady) return;
  uint32_t start=ARM_DWT_CYCCNT;
  audio_block_t* b=allocate(); // Teensy fixed AudioMemory pool, no heap allocation.
  if(!b) ++blocksLost;
  if(running) {
   if(retriggerEvery && ticks && ticks%retriggerEvery==0) { for(unsigned v=0;v<voiceCount;++v) trigger(v); ++retriggerCount; }
   for(unsigned s=0;s<AUDIO_BLOCK_SAMPLES;++s) {
    int32_t sum=0;
    for(unsigned i=0;i<voiceCount;++i) {
     auto& v=voices[i]; if(!v.active) continue;
     uint32_t limit=ramMode?shortAsset.frames:assets[i].frames;
     if(v.pos>=limit) { v.active=false; ++v.eofs; continue; }
     int16_t x=0;
     if(ramMode) x=resident[v.pos];
     else if(v.pos<Head) x=heads[i][v.pos];
     else if(v.pos>=v.begin && v.pos<v.end) x=rings[i][v.pos%Capacity];
     else { ++v.underruns; }
     uint32_t ready=ramMode?limit-v.pos:(v.pos<Head?Head-v.pos:0)+(v.end>std::max<uint32_t>(uint32_t(v.pos),Head)?v.end-std::max<uint32_t>(uint32_t(v.pos),Head):0);
     // Exclude the intentional final EOF drain from refill headroom minimum.
     if(!ramMode && v.end<limit && ready<v.minReady) v.minReady=ready;
     v.hash=hashSample(v.hash,x); sum+=x;
     ++v.pos; // Timeline continues through starvation; never replay stale backlog.
    }
    if(b) b->data[s]=sum/8;
   }
   ++ticks;
  } else if(b) memset(b->data,0,sizeof b->data);
  if(b) { transmit(b); release(b); }
  uint32_t elapsed=ARM_DWT_CYCCNT-start; if(elapsed>maxCycles) maxCycles=elapsed;
 }
} source;
alignas(AudioOutputPT8211) uint8_t outputStorage[sizeof(AudioOutputPT8211)];
alignas(AudioConnection) uint8_t patchStorage[2][sizeof(AudioConnection)];

bool inspect(const char* path,Asset& a) {
 Reader r; if(!r.file.open(path,O_RDONLY)) return false;
 WavMetadata m; if(InspectWavMetadata(r,m)!=WavBufferStatus::Success || m.frames>UINT32_MAX) return false;
 if(strlen(path)>=sizeof(a.path)) return false;
 strcpy(a.path,path); a.offset=m.data_offset; a.frames=m.frames; return true;
}
unsigned found=0;
void scan(const char* path,unsigned depth=0) {
 if(depth>5 || (found==N && shortAsset.frames)) return;
 Serial.printf("SCAN %s\n",path);
 FsFile dir; if(!dir.open(path,O_RDONLY) || !dir.isDir()) return;
 FsFile f;
 while(f.openNext(&dir,O_RDONLY)) {
  char name[160],full[240]; f.getName(name,sizeof name);
  if(snprintf(full,sizeof full,"%s/%s",path,name)>=int(sizeof full)) { f.close(); continue; }
  bool sub=f.isDir(); f.close();
  if(sub) scan(full,depth+1);
  else {
   Asset a; if(!inspect(full,a)) continue;
   if(!shortAsset.frames && a.frames<=ResidentFrames && a.frames>256) shortAsset=a;
   if(found<N && a.frames*2>1048576) assets[found++]=a;
  }
  if(found==N && shortAsset.frames) break;
 }
}
void service() {
 if(!running || ramMode) return;
 int best=-1; uint32_t least=UINT32_MAX;
 for(unsigned i=0;i<voiceCount;++i) {
  auto& v=voices[i]; if(!v.active || v.end>=assets[i].frames) continue;
  uint32_t p=v.pos,e=v.end,used=e>std::max<uint32_t>(p,Head)?e-std::max<uint32_t>(p,Head):0;
  uint32_t ready=used+(p<Head?Head-p:0);
  if(ready<=threshold && Capacity-used>=chunk/2 && ready<least) { best=i; least=ready; }
 }
 if(best<0) return;
 auto& v=voices[best]; const auto& a=assets[best];
 uint32_t gen=v.generation,at=std::max<uint32_t>(uint32_t(v.end),std::max<uint32_t>(uint32_t(v.pos),Head));
 uint32_t count=std::min<uint32_t>(chunk/2,a.frames-at);
 uint32_t start=micros();
 bool ok=v.reader.Seek(a.offset+uint64_t(at)*2) && v.reader.Read(reinterpret_cast<uint8_t*>(staging),count*2)==count*2;
 uint32_t elapsed=micros()-start; ++v.reads; v.totalRead+=elapsed; ++v.hist[std::min<uint32_t>(elapsed/10,2047u)]; v.maxRead=std::max<uint32_t>(v.maxRead,elapsed);
 if(!ok) { ++v.errors; AudioNoInterrupts(); if(v.generation==gen) v.active=false; AudioInterrupts(); return; }
 // A retrigger can interrupt the read or copy. Staging prevents old SD DMA from touching the new ring.
 if(v.generation!=gen) { ++v.stale; return; }
 for(uint32_t j=0;j<count;++j) rings[best][(at+j)%Capacity]=staging[j];
 __DMB();
 AudioNoInterrupts();
 if(v.generation==gen) { v.begin=at>v.end?at:v.begin; v.end=at+count; }
 else ++v.stale;
 AudioInterrupts();
}
void reference(Asset& a) {
 Reader r; a.hash=2166136261u;
 if(!r.file.open(a.path,O_RDONLY) || !r.Seek(a.offset)) { Serial.println("REFERENCE_ERROR"); return; }
 for(uint32_t at=0;at<a.frames;) {
  uint32_t n=std::min<uint32_t>(StageFrames,a.frames-at);
  if(r.Read(reinterpret_cast<uint8_t*>(staging),n*2)!=n*2) { Serial.println("REFERENCE_ERROR"); return; }
  for(unsigned j=0;j<n;++j) a.hash=hashSample(a.hash,staging[j]);
  at+=n;
 }
 Serial.printf("ASSET %s frames=%lu bytes=%lu hash=%08lx\n",a.path,a.frames,a.frames*2,a.hash);
}
void run(unsigned n,unsigned bytes,unsigned percent,unsigned ms,unsigned stagger,unsigned retrig,unsigned mode) {
 if(!storageReady || !outputReady) { Serial.println("HARDWARE_NOT_READY"); return; }
 if(!n || n>N || mode>3 || retrig>3600000 || !ms || ms>3600000 || bytes>sizeof staging || bytes<2048 || bytes%2048 || bytes>STREAM_RING_BYTES || percent>100 || !percent) { Serial.println("INVALID"); return; }
 running=false; voiceCount=n; chunk=bytes; threshold=Capacity*percent/100; ramMode=mode==1;
 if(mode && (!(mode==3?largeAsset.frames:shortAsset.frames) || n!=1)) { Serial.println("NO_SHORT"); return; }
 // Dedicated/fallback fixtures can bypass scan's size filter; validate before any PCM read.
 if(ramMode && shortAsset.frames>ResidentFrames) { Serial.println("RAM_CAPACITY_EXCEEDED"); return; }
 Asset saved=assets[0]; if(mode) assets[0]=mode==3?largeAsset:shortAsset;
 retriggerEvery=retrig?std::max<uint32_t>(1u,uint64_t(retrig)*44100u/128000u):0;
 for(unsigned i=0;i<n;++i) {
  auto& v=voices[i]; v.reader.file.close();
  v.active=false; v.contentErrors=0; v.verifiedGenerations=0; v.expectedFrames=0; v.underruns=0; v.minReady=UINT32_MAX; v.eofs=0; v.errors=v.stale=v.reads=v.maxRead=v.totalRead=0; memset(v.hist,0,sizeof v.hist);
  if(!v.reader.file.open(assets[i].path,O_RDONLY) || !v.reader.Seek(assets[i].offset) ||
    v.reader.Read(reinterpret_cast<uint8_t*>(heads[i]),std::min<uint32_t>(Head,assets[i].frames)*2)!=std::min<uint32_t>(Head,assets[i].frames)*2) { Serial.println("PREFILL_ERROR"); assets[0]=saved; return; }
  if(ramMode) { v.reader.Seek(shortAsset.offset); if(v.reader.Read(reinterpret_cast<uint8_t*>(resident),shortAsset.frames*2)!=shortAsset.frames*2) { Serial.println("RAM_ERROR"); assets[0]=saved; return; } }
  if(retriggerEvery) {
   v.expectedFrames=std::min<uint32_t>(retriggerEvery*AUDIO_BLOCK_SAMPLES,assets[i].frames);
   v.expectedHash=2166136261u; v.reader.Seek(assets[i].offset);
   for(uint32_t at=0;at<v.expectedFrames;) {
    uint32_t count=std::min<uint32_t>(StageFrames,v.expectedFrames-at);
    if(v.reader.Read(reinterpret_cast<uint8_t*>(staging),count*2)!=count*2) { Serial.println("PREFILL_ERROR"); assets[0]=saved; return; }
    for(unsigned j=0;j<count;++j) v.expectedHash=hashSample(v.expectedHash,staging[j]);
    at+=count;
   }
  }

 }
 ticks=maxCycles=blocksLost=retriggerCount=0; AudioProcessorUsageMaxReset();
 Serial.printf("RUN n=%u read=%u ring=%u threshold=%u ms=%u stagger=%u retrig=%u mode=%u\n",n,bytes,STREAM_RING_BYTES,percent,ms,stagger,retrig,mode);
 uint32_t began=millis(),lastLive=began; unsigned started=0;
 running=true;
 while(millis()-began<ms) {
  if(started<n && millis()-began>=started*stagger) {
   AudioNoInterrupts();
   do { trigger(started++); } while(!stagger && started<n);
   AudioInterrupts();
  }
  service();
  if(millis()-lastLive>=60000) {
   lastLive=millis(); uint32_t under=0,least=UINT32_MAX,stale=0,content=0;
   for(unsigned i=0;i<n;++i) { under+=voices[i].underruns; least=std::min<uint32_t>(least,uint32_t(voices[i].minReady)); stale+=voices[i].stale; content+=voices[i].contentErrors; }
   char live[96]; int length=snprintf(live,sizeof live,"LIVE sec=%lu under=%lu min=%lu stale=%lu content=%lu\n",(lastLive-began)/1000,under,least,stale,content);
   // Foreground-only, bounded and dropped if USB has no room; host cannot pace playback.
   if(length>0 && length<int(sizeof live) && Serial.availableForWrite()>=length) Serial.write(reinterpret_cast<const uint8_t*>(live),length);
  }
  bool active=started<n; for(unsigned i=0;i<n;++i) active|=voices[i].active;
  if(!active) break;
 }
 AudioNoInterrupts(); running=false; for(auto& v:voices) v.active=false; AudioInterrupts();
 Serial.printf("END elapsed=%lu blocks=%lu callback_us=%.3f pool_fail=%lu retriggers=%lu cpu_peak=%.3f\n",millis()-began,ticks,maxCycles/(F_CPU_ACTUAL/1000000.0),blocksLost,retriggerCount,AudioProcessorUsageMax());
 for(unsigned i=0;i<n;++i) {
  auto& v=voices[i]; uint32_t accum=0,p99=0;
  for(unsigned j=0;j<2048;++j) { accum+=v.hist[j]; if(v.reads && accum>=((v.reads*99+99)/100)) { p99=j==2047?v.maxRead:(j+1)*10; break; } }
  Serial.printf("VOICE %u underrun_frames=%lu min_ready_frames=%lu read_count=%lu p99_us_upper=%lu max_us=%lu read_total_us=%lu errors=%lu stale=%lu content_errors=%lu verified_generations=%lu eof=%lu pos=%lu hash=%08lx reference=%08lx\n",i,v.underruns,v.minReady,v.reads,p99,v.maxRead,v.totalRead,v.errors,v.stale,v.contentErrors,v.verifiedGenerations,v.eofs,v.pos,v.hash,assets[i].hash);
 }
 assets[0]=saved; Serial.println("DONE");
}
char command[160]; unsigned commandLength=0;
}
void hardwareInfo() {
 for(auto& v:voices) v.reader.file.close();
 found=0; shortAsset=Asset{}; largeAsset=Asset{}; for(auto& a:assets) a=Asset{};
 outputReady=true; // PT8211 is user-confirmed attached; it has no status/ACK interface.
 Serial.println("OUTPUT PT8211 BCK=21 WS=20 DIN=7 mono_to_both gain=1/8");
 if(!storageReady) storageReady=SD.begin(BUILTIN_SDCARD);
 Serial.printf("SD ready=%u code=%u data=%lu\n",storageReady,disk.sdErrorCode(),disk.sdErrorData());
 if(!storageReady) { Serial.println("NOT_READY"); return; }
 if(!readHeld) readHeld=SdAccess::AcquireRead();
 if(!readHeld) { storageReady=false; Serial.println("OWNERSHIP_ERROR"); return; }
 cid_t cid;
 if(disk.card()->readCID(&cid)) {
  Serial.print("CARD cid="); const auto* p=reinterpret_cast<const uint8_t*>(&cid);
  for(unsigned i=0;i<sizeof cid;++i) Serial.printf("%02x",p[i]);
  Serial.printf(" sectors=%lu fat=%u cluster_sectors=%lu\n",disk.card()->sectorCount(),disk.fatType(),disk.sectorsPerCluster());
 }
 char shortPath[240]; snprintf(shortPath,sizeof shortPath,"%s/short.wav",FixtureDir);
 inspect(shortPath,shortAsset);
 snprintf(shortPath,sizeof shortPath,"%s/large.wav",FixtureDir); inspect(shortPath,largeAsset);
 scan(FixtureDir);
 if(found<N || !shortAsset.frames) {
  if(!shortAsset.frames) inspect("/Samples/test2.wav",shortAsset);
  scan("/Samples/Wav-HQ/DrumLoop");
 }
 Serial.printf("FOUND long=%u short=%s\n",found,shortAsset.path);
 for(unsigned i=0;i<found;++i) reference(assets[i]);
 if(shortAsset.frames) reference(shortAsset);
 if(largeAsset.frames) reference(largeAsset);
 Serial.println(outputReady && found==N && shortAsset.frames?"READY":"NOT_READY");
}
bool makeFixture(const char* path,uint32_t frames,unsigned voice) {
 if(disk.exists(path)) {
  Asset a;
  if(inspect(path,a) && a.frames==frames) { Serial.printf("FIXTURE_REUSE %s\n",path); return true; }
  Serial.printf("FIXTURE_CONFLICT %s\n",path); return false;
 }
 FsFile file;
 if(!file.open(path,O_WRONLY|O_CREAT|O_EXCL)) return false;
 uint8_t header[44]{};
 memcpy(header,"RIFF",4); memcpy(header+8,"WAVEfmt ",8); memcpy(header+36,"data",4);
 auto u32=[&](unsigned at,uint32_t x) { for(unsigned j=0;j<4;++j) header[at+j]=x>>(j*8); };
 u32(4,frames*2+36); u32(16,16); header[20]=1; header[22]=1;
 u32(24,44100); u32(28,88200); header[32]=2; header[34]=16; u32(40,frames*2);
 bool ok=file.write(header,44)==44;
 uint32_t phase=0,increment=uint64_t(110+voice*37)*4294967296ull/44100;
 for(uint32_t at=0;ok && at<frames;) {
  uint32_t count=std::min<uint32_t>(StageFrames,frames-at);
  for(unsigned j=0;j<count;++j) { staging[j]=(int32_t(phase>>16)-32768)/10; phase+=increment; }
  ok=file.write(reinterpret_cast<const uint8_t*>(staging),count*2)==count*2;
  at+=count;
 }
 ok=file.sync() && ok; file.close();
 if(!ok) disk.remove(path); // Only this call's exclusively created partial file.
 Serial.printf("FIXTURE_%s %s frames=%lu\n",ok?"CREATED":"ERROR",path,frames);
 return ok;
}
void prepareFixtures() {
 if(running || !storageReady) { Serial.println("FIXTURE_NOT_READY"); return; }
 for(auto& v:voices) v.reader.file.close();
 if(readHeld) { SdAccess::ReleaseRead(); readHeld=false; }
 if(!SdAccess::AcquireWrite()) { Serial.println("FIXTURE_OWNERSHIP_ERROR"); return; }
 bool ok=disk.exists(FixtureDir) || disk.mkdir(FixtureDir,true);
 char path[240];
 for(unsigned i=0;ok && i<10;++i) {
  if(i==9) snprintf(path,sizeof path,"%s/large.wav",FixtureDir);
  else if(i==8) snprintf(path,sizeof path,"%s/short.wav",FixtureDir);
  else snprintf(path,sizeof path,"%s/voice%02u.wav",FixtureDir,i+1);
  ok=makeFixture(path,i==8?22050u:(i==9?44100u*20u:44100u*360u),i);
 }
 SdAccess::ReleaseWrite(); readHeld=SdAccess::AcquireRead();
 Serial.println(ok?"FIXTURES_DONE":"FIXTURES_ERROR");
}
void setup() {
 Serial.begin(115200);
 // Match production: initialize storage before activating the hardware audio clock.
 storageReady=SD.begin(BUILTIN_SDCARD);
 auto* output=new (outputStorage) AudioOutputPT8211;
 new (patchStorage[0]) AudioConnection(source,0,*output,0);
 new (patchStorage[1]) AudioConnection(source,0,*output,1);
 AudioMemory(12); audioReady=true;
 while(!Serial && millis()<10000) {}
 Serial.printf("SD_STREAM native=%.1f ring=%u heads=%u staging=%u resident=%u psram=%u\n",AUDIO_SAMPLE_RATE_EXACT,unsigned(sizeof rings),unsigned(sizeof heads),unsigned(sizeof staging),unsigned(sizeof resident),external_psram_size);
 Serial.printf("MEMORY dtcm_static_end=%08lx stack_top=%08lx stack_local_headroom=%lu ram2_static=%lu ram2_heap_headroom=%lu voice_state=%u\n",uint32_t(&_ebss),uint32_t(&_estack),uint32_t(&_estack)-uint32_t(&_ebss),uint32_t(&_heap_start)-0x20200000u,uint32_t(&_heap_end)-uint32_t(&_heap_start),unsigned(sizeof voices));
 Serial.printf("BOOT_READY sd=%u use_INFO\n",storageReady);
}
void loop() {
 while(Serial.available()) {
  const char c=Serial.read();
  if(c=='\n') {
   command[commandLength]=0; unsigned n,b,p,ms,s,r,m;
   if(sscanf(command,"RUN %u %u %u %u %u %u %u",&n,&b,&p,&ms,&s,&r,&m)==7) {
    if(n<=found || m) run(n,b,p,ms,s,r,m); else Serial.println("MISSING_ASSETS");
   } else if(!strcmp(command,"INFO")) hardwareInfo();
   else if(!strcmp(command,"PREPARE")) prepareFixtures();
   else if(!strcmp(command,"PING")) Serial.printf("PONG ring=%u found=%u short=%lu sd=%u output=%u\n",STREAM_RING_BYTES,found,shortAsset.frames,storageReady,outputReady);
   commandLength=0;
  } else if(c!='\r' && commandLength<sizeof(command)-1) command[commandLength++]=c;
 }
}
