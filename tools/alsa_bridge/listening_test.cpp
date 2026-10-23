#include "listening.h"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <algorithm>
namespace fs=std::filesystem;
void Require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
std::vector<char> Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Stall(){sleep(10);}
// Sixty seconds of frame-positioned audio, accelerated independently of wall time.
// 1 kHz short reads; two partial writes, one read retry per tick, one write retry
// every other tick. Includes 50 status pairs/s and 1 host observation/s.
void CapacityRegression(const fs::path& root){
 // Two WAV streams, conservative 256-byte TSV + 256-byte log row per event,
 // plus headers/metadata. Storage is separate from fixed in-memory queue capacity.
 static_assert(listening::kLimitFrames*8+88+listening::kLimitRecords*512+65536 <
     576ULL*1024*1024,"documented listening storage bound");
 auto trace=std::make_unique<clicks::Trace>(root.c_str(),clicks::kSlots,nullptr,
     listening::kLimitSeconds,true,listening::kLimitFrames,listening::kLimitRecords);
 uint64_t records=0,captured=0,played=0,old_limit_frame=0;
 const auto deadline=listening::Now()+25000000000ULL;
 auto emit=[&](clicks::Record r,const void* pcm=nullptr){
  while(trace->PendingRecords()>512){Require(listening::Now()<deadline,"capacity fixture writer stalled");usleep(1000);}
  Require(trace->Accepting(),"60-second fixture unexpectedly truncated");
  r.ns=1000000000ULL+captured*1000000000ULL/44100;
  r.captured=captured;r.played=played;
  trace->Emit(r,pcm,true);++records;
  if(records==150000)old_limit_frame=captured;
 };
 clicks::Record r;r.kind=clicks::Transport;r.step=listening::Start;emit(r);
 std::array<unsigned char,45*4> pcm{};
 for(unsigned i=0;i<pcm.size();++i)pcm[i]=static_cast<unsigned char>(i*37+11);
 for(unsigned ms=0;ms<60000;++ms){
  const unsigned frames=ms%10==9?45:44;
  r={};r.kind=clicks::Read;r.requested=256;r.result=frames;r.source=captured;emit(r,pcm.data());captured+=frames;
  const unsigned first=frames/2;
  r.kind=clicks::Write;r.requested=frames;r.result=first;r.source=played;emit(r,pcm.data());played+=first;
  r.requested=frames-first;r.result=frames-first;r.source=played;emit(r,pcm.data()+first*4);played+=frames-first;
  r={};r.kind=clicks::Read;r.requested=256;r.result=-11;emit(r);
  if(ms%2==0){r.kind=clicks::Write;r.requested=22;emit(r);}
  if(ms%20==0){r={};r.kind=clicks::StatusCapture;emit(r);r.kind=clicks::StatusPlayback;emit(r);}
  if(ms%1000==0){r={};r.kind=clicks::Transport;r.step=listening::Observation;emit(r);}
 }
 r={};r.kind=clicks::Transport;r.step=listening::Stop;emit(r);trace->Finish();
 Require(records==276062 && records>150000 && records<listening::kLimitRecords,"independent duration record count");
 Require(captured==2646000 && played==captured,"exact sixty-second frame totals");
 Require(old_limit_frame<44100*60,"old budget truncates this workload");
 auto completion=Bytes(trace->Directory()+"/completion.json");std::string text(completion.begin(),completion.end());
 Require(text.find("\"incomplete\":false")!=text.npos,"complete accelerated duration fixture");
 auto received=Bytes(trace->Directory()+"/received.wav"),accepted=Bytes(trace->Directory()+"/accepted.wav");
 Require(received.size()==44+captured*4 && received==accepted,"short reads/partial writes retain exact PCM");
 // Exercise the record cap itself and no-splice behavior with a tiny budget.
 auto capped=std::make_unique<clicks::Trace>(root.c_str(),clicks::kSlots,nullptr,180,true,0,3);
 r={};r.kind=clicks::Read;r.result=1;
 for(unsigned i=0;i<5;++i)capped->Emit(r,pcm.data());
 Require(!capped->Accepting(),"record cap stops evidence");capped->Finish();
 completion=Bytes(capped->Directory()+"/completion.json");text.assign(completion.begin(),completion.end());
 Require(text.find("\"loss_reason\":2")!=text.npos && text.find("\"first_lost_seq\":4")!=text.npos,"explicit record-cap loss");
 Require(Bytes(capped->Directory()+"/received.wav").size()==44+3*4,"no PCM spliced after cap");
 std::cout<<"60-second capacity: "<<records<<" records; old cap reached at "<<double(old_limit_frame)/44100<<" PCM seconds\n";
}
int main(int argc,char** argv){try{
 Require(argc==2,"output root");fs::path root=argv[1];fs::create_directories(root);
 std::string unique=(root/"listen-test-XXXXXX").string();Require(mkdtemp(&unique[0]),"unique root");root=unique;
 unsetenv("BROTRACKER_LISTEN_CONTROL");{listening::Observer off;Require(!off.Enabled(),"default disabled");}
 const auto control=(root/"control").string();int fd=open(control.c_str(),O_RDWR|O_CREAT|O_EXCL,0600);Require(fd>=0,"control open");Require(ftruncate(fd,4096)==0,"control size");close(fd);
 setenv("BROTRACKER_LISTEN_CONTROL",control.c_str(),1);setenv("BROTRACKER_LISTEN_BASE",unique.c_str(),1);
 const unsigned char pcm[]={1,2,3,4,5,6,7,8};
 {
  listening::Mapping ui;listening::Observer observer;Require(observer.Enabled(),"enabled");observer.Parameters("capture=null playback=null capture_holdback_frames=0\n");
  auto state=[&](unsigned kind){ui.Publish(kind,15500,123);observer.Poll(102,2,5,0);};
  auto frames=[&]{clicks::Record r;r.kind=clicks::Read;r.source=100;r.captured=100;r.result=2;observer.Emit(r,pcm);r.kind=clicks::Write;observer.Emit(r,pcm);r.kind=clicks::Discard;r.result=5;observer.Emit(r);};
  state(listening::Start);frames();state(listening::Pause);frames();
  state(listening::Continue);frames();state(listening::Stop);
  state(listening::Start);frames();state(listening::Exit);
 }
 unsigned attempts=0;
 for(const auto& entry:fs::directory_iterator(root))if(entry.is_directory()){
  ++attempts;auto received=Bytes(entry.path()/"received.wav"),accepted=Bytes(entry.path()/"accepted.wav");
  Require(received.size()==52 && accepted.size()==52,"segments exclude paused PCM");
  Require(std::memcmp(received.data(),"RIFF",4)==0,"WAV header");
  Require(std::equal(pcm,pcm+8,received.begin()+44) && received==accepted,"exact received/accepted bytes");
  const auto events=Bytes(entry.path()/"transfers.tsv");std::string text(events.begin(),events.end());Require(text.find("\t100\t")!=text.npos,"source coordinates");
  auto manifest=Bytes(entry.path()/"manifest.json");text.assign(manifest.begin(),manifest.end());Require(text.find("\"incomplete\":false")!=text.npos,"completed manifest");
  Require(fs::exists(entry.path()/"diagnostic.log") && fs::exists(entry.path()/"alsa-parameters.txt"),"metadata and log");
 }
 Require(attempts==3,"unique START/CONTINUE attempts");
 CapacityRegression(root);
 // Exercise production trace's evidence-only cap; no silence or partial splice.
 {auto limited=std::make_unique<clicks::Trace>(unique.c_str(),clicks::kSlots,nullptr,180,true,1,10);clicks::Record r;r.kind=clicks::Read;r.result=2;limited->Emit(r,pcm);limited->Finish();auto c=Bytes(limited->Directory()+"/completion.json");Require(std::string(c.begin(),c.end()).find("\"incomplete\":true")!=std::string::npos,"frame cap incomplete");Require(Bytes(limited->Directory()+"/received.wav").size()==44,"no altered samples");}
 // Real asynchronous disk failure through an owned descriptor, no audio device.
 {
  auto disk=std::make_unique<clicks::Trace>(unique.c_str(),clicks::kSlots,nullptr,180,true,100,100);
  const auto path=disk->Directory()+"/received.wav";
  int owned=-1;
  for(const auto& entry:fs::directory_iterator("/proc/self/fd")) {
   std::error_code error;const auto target=fs::read_symlink(entry.path(),error);
   if(!error && target==path){owned=std::stoi(entry.path().filename().string());break;}
  }
  Require(owned>=0,"owned WAV descriptor");int full=open("/dev/full",O_WRONLY);Require(full>=0,"disk fault fixture");Require(dup2(full,owned)==owned,"inject disk failure");close(full);
  clicks::Record r;r.kind=clicks::Read;r.result=2;disk->Emit(r,pcm);disk->Finish();
  auto c=Bytes(disk->Directory()+"/completion.json");Require(std::string(c.begin(),c.end()).find("\"writer_failed\":true")!=std::string::npos,"disk failure incomplete");
 }
 // Failed setup and a stalled worker cannot block producer/finalization indefinitely.
 setenv("BROTRACKER_LISTEN_BASE","/nonexistent/brotracker-no-directory",1);
 {listening::Observer failed;listening::Mapping ui;ui.Publish(listening::Start);failed.Poll(0,0,0,0);}
 setenv("BROTRACKER_LISTEN_BASE",unique.c_str(),1);const auto begin=listening::Now();
 {listening::Observer stalled(Stall);listening::Mapping ui;ui.Publish(listening::Start);stalled.Poll(0,0,0,0);clicks::Record r;r.kind=clicks::Read;r.result=2;for(unsigned i=0;i<6000;++i)stalled.Emit(r,pcm);}
 Require(listening::Now()-begin<3000000000ULL,"bounded exit");
 std::cout<<"Listening lifecycle/PCM/failure/bounds checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
