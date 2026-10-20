#include "click_diagnostics.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstdlib>

void Require(bool value,const char* message) {
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
std::atomic<bool> release_writer{false};
void Gate() { while(!release_writer.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
std::string Read(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv) {
    Require(argc==2,"test output directory required");
    auto q=std::make_unique<clicks::Queue>(2);
    clicks::Record r,out;
    r.seq=1; Require(q->Push(r),"first slot");
    r.seq=2; Require(q->Push(r),"second slot");
    r.seq=3; Require(!q->Push(r),"bounded queue full");
    Require(q->Pop(out) && out.seq==1,"FIFO first");
    Require(q->Push(r),"wrap slot");
    Require(q->Pop(out) && out.seq==2,"FIFO second");
    Require(q->Pop(out) && out.seq==3,"FIFO wrap");
    Require(!q->Pop(out),"empty");
    // A stalled writer cannot make the producer block or splice later records.
    auto t=std::make_unique<clicks::Trace>(argv[1],2,Gate);
    t->Emit(r); t->Emit(r); t->Emit(r); t->Emit(r);
    release_writer.store(true); t->Finish();
    const auto c=Read(t->Directory()+"/completion.json");
    Require(c.find("\"lost_records\":2")!=std::string::npos,"all losses explicit");
    Require(c.find("\"first_lost_seq\":3")!=std::string::npos,"first lost sequence");
    Require(c.find("\"loss_reason\":1")!=std::string::npos,"overflow reason");
    Require(c.find("\"incomplete\":true")!=std::string::npos,"overflow incomplete");
    // A short accepted transfer copies only accepted bytes, not requested bytes.
    auto ok=std::make_unique<clicks::Trace>(argv[1]);
    const unsigned char pcm[]={1,2,3,4,5,6,7,8,9,10,11,12};
    r.kind=clicks::Write; r.requested=3; r.result=2;
    ok->Emit(r,pcm); r.result=0; ok->Emit(r,pcm); r.result=-11; ok->Emit(r,pcm);
    ok->Finish();
    Require(Read(ok->Directory()+"/accepted.s16le")==std::string((const char*)pcm,8),"short/zero/negative accepted byte accounting");
    Require(Read(ok->Directory()+"/received.s16le").empty(),"no invented capture bytes");
    bool rejected=false;
    try { auto bad=std::make_unique<clicks::Trace>(argv[1],clicks::kSlots,nullptr,301); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"recording duration has a hard maximum");
    auto limited=std::make_unique<clicks::Trace>(argv[1],clicks::kSlots,nullptr,1);
    r.kind=clicks::Service;r.result=0;limited->Emit(r);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));limited->Emit(r);limited->Finish();
    const auto limit=Read(limited->Directory()+"/completion.json");
    Require(limit.find("\"loss_reason\":2")!=std::string::npos,"duration loss explicit");
    Require(limit.find("\"incomplete\":true")!=std::string::npos,"duration loss cannot pass");
    std::puts("click diagnostic queue/overflow/partial accounting checks passed");
}
