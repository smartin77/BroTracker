#include "recording.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <chrono>
#include <sys/stat.h>
static void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static std::vector<unsigned char> Read(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static uint32_t U32(const std::vector<unsigned char>& b,size_t p){return b[p]|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24;}
static void Wav(const std::string& file,const std::vector<unsigned char>& pcm){
 auto b=Read(file);Require(b.size()==44+pcm.size(),"WAV size");Require(std::memcmp(b.data(),"RIFF",4)==0 && std::memcmp(b.data()+8,"WAVEfmt ",8)==0,"WAV magic");
 Require(U32(b,4)==36+pcm.size() && U32(b,40)==pcm.size() && U32(b,24)==44100 && U32(b,28)==176400,"WAV fields");
 Require(b[20]==1 && b[22]==2 && b[32]==4 && b[34]==16,"PCM stereo16");Require(std::equal(pcm.begin(),pcm.end(),b.begin()+44),"PCM preservation");
}
static ssize_t Partial(int fd,const void* p,size_t n){return ::write(fd,p,std::min<size_t>(n,7));}
static std::atomic<bool> held{false},release_writer{false};
static ssize_t Held(int fd,const void* p,size_t n){if(n!=44){held=true;while(!release_writer)std::this_thread::sleep_for(std::chrono::milliseconds(1));}return ::write(fd,p,n);}
static std::atomic<int> calls{0};
static ssize_t Failure(int fd,const void* p,size_t n){if(++calls==2){errno=EIO;return -1;}return ::write(fd,p,n);}
static std::thread WorkerFailure(void (*)(recording::Recorder*), recording::Recorder*){throw std::runtime_error("worker creation failure");}
static std::atomic<bool> empty_observed{false},release_empty{false};
static void EmptyObservation(){
 if(!empty_observed.exchange(true))while(!release_empty.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
int main(){
 char temp[]="/tmp/brotracker-recording-XXXXXX";const char* dir=mkdtemp(temp);Require(dir,"temp directory");
 try {
 std::vector<unsigned char> pcm(9001*4);for(size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<unsigned char>(i*37);
 std::fill(pcm.begin(),pcm.begin()+1024,0); // initial silence is retained
 // Hold the writer after its empty observation; publish final PCM and
 // completion while held. Old termination logic silently omitted these bytes.
 for(int kind=0;kind<3;++kind) {
  empty_observed=false;release_empty=false;
  auto r=std::make_unique<recording::Recorder>(dir,kind==1?2:recording::kFrames,8,::write,nullptr,EmptyObservation);
  while(!empty_observed)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  r->Push(pcm.data()+1024,2);
  if(kind==2)r->Push(pcm.data()+1032,2); // overflow after accepted prefix
  std::thread finisher([&]{r->Finish();});
  while(!r->CompletionPublished())std::this_thread::sleep_for(std::chrono::milliseconds(1));
  release_empty=true;finisher.join();Wav(r->File(),{pcm.begin()+1024,pcm.begin()+1032});
 }
 {auto r=std::make_unique<recording::Recorder>(dir);std::string file=r->File();r->CaptureStartFailed(-19);r->Finish();Require(access(file.c_str(),F_OK)!=0,"unstarted WAV removed");}
 std::string first;
 {auto r=std::make_unique<recording::Recorder>(dir,recording::kFrames,recording::kCapacity,Partial);first=r->File();r->Push(pcm.data(),9001);r->Finish();Wav(first,pcm);}
 {auto r=std::make_unique<recording::Recorder>(dir,3);Require(first!=r->File(),"recovery unique filename");r->Push(pcm.data(),9001);r->Finish();Wav(r->File(),{pcm.begin(),pcm.begin()+12});}
 {auto r=std::make_unique<recording::Recorder>(dir,recording::kFrames,8,Held);r->Push(pcm.data(),2);
  while(!held)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  r->Push(pcm.data()+8,2);release_writer=true;r->Finish();Wav(r->File(),{pcm.begin(),pcm.begin()+8});}
 {auto r=std::make_unique<recording::Recorder>(dir,recording::kFrames,recording::kCapacity,Failure);r->Push(pcm.data(),10);r->Finish();Wav(r->File(),{});}
 // Exception unwinding is the same recorder destruction path used by USB
 // ENODEV/other ALSA failures, and must finalize its accepted prefix.
 std::string disconnect_file;
 try {auto r=std::make_unique<recording::Recorder>(dir);disconnect_file=r->File();r->Push(pcm.data(),10);throw std::runtime_error("simulated ENODEV");}catch(...){}
 Wav(disconnect_file,{pcm.begin(),pcm.begin()+40});
 // Exercise ring wrap with deterministic draining, and the real production
 // 180-second cap without waiting 180 seconds on a sound device.
 {auto r=std::make_unique<recording::Recorder>(dir,recording::kFrames,65536);
  uint64_t remaining=recording::kFrames;
  while(remaining){size_t n=std::min<uint64_t>(9001,remaining);r->Push(pcm.data(),n);remaining-=n;while(r->Pending())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  r->Push(pcm.data(),100);r->Finish();struct stat st{};Require(stat(r->File().c_str(),&st)==0 && st.st_size==31752044,"real 180-second cap");
  int fd=open(r->File().c_str(),O_RDONLY);std::vector<unsigned char> h(44);Require(read(fd,h.data(),44)==44,"limit header read");close(fd);Require(U32(h,40)==31752000,"real limit header");}
 bool failed=false;try{auto r=std::make_unique<recording::Recorder>("/no/such/brotracker/path");}catch(...){failed=true;}Require(failed,"open failure");
 failed=false;try{auto r=std::make_unique<recording::Recorder>(dir,recording::kFrames,recording::kCapacity,::write,WorkerFailure);}catch(...){failed=true;}Require(failed,"worker failure");
 std::thread a([&]{for(int i=0;i<30;++i)recording::Event(dir,"parallel","a\tb\nc");});
 std::thread b([&]{for(int i=0;i<30;++i)recording::Event(dir,"parallel","other");});a.join();b.join();
 std::ifstream events(std::string(dir)+"/events.tsv");std::string line,all;int parallel=0;
 while(std::getline(events,line)){Require(std::count(line.begin(),line.end(),'\t')==5,"timeline record integrity");all+=line+'\n';if(line.find("\tparallel\t")!=std::string::npos)++parallel;}
 Require(parallel==60 && all.find("a\\tb\\nc")!=std::string::npos,"timeline escaping");
 for(auto token:{"duration_limit","recording_overflow","recording_failure","recording_end","incomplete=yes","recording_setup","capture_start_failure","recording_cancelled"})Require(all.find(token)!=std::string::npos,token);
 std::cout<<"Recording exact PCM/header, limit, overflow, partial writes, writer/open failure, unique recovery names, timeline/worker checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 // Leave no reusable test data; all paths were created in our private directory.
 for(auto& name:{std::string("events.tsv")})unlink((std::string(dir)+"/"+name).c_str());
 // WAVs are useful only on failure; remove the private fixture on success.
 std::string command="rm -rf -- "+std::string(dir);return std::system(command.c_str())==0?0:1;
}
