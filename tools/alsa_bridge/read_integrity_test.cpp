#include "read_integrity.h"
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <memory>
#include <atomic>
#include <sys/stat.h>

void Check(bool ok) { if(!ok) std::abort(); }
std::atomic<bool> release_writer{false};
void Gate(){while(!release_writer.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));}
std::string Read(const std::string& p) {std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv) {
    Check(argc==2);
    char dir[4096];std::snprintf(dir,sizeof(dir),"%s/read-integrity-XXXXXX",argv[1]);Check(mkdtemp(dir)!=nullptr);
    Check(read_integrity::Digest("hello",5)==0xa430d84680aabd0bULL);
    {
        auto trace=std::make_unique<read_integrity::Trace>(dir);
        unsigned char pcm[]={1,2,3,4,5,6,7,8,99,99,99,99};
        trace->Observe(pcm,0,2); // Exactly eight returned bytes, not the requested three frames.
        pcm[0]=77;
        trace->Observe(pcm,2,1);trace->Finish();
        const auto bytes=Read(std::string(dir)+"/read-integrity.tsv");
        char expected[40];const unsigned char original[]={1,2,3,4,5,6,7,8};
        std::snprintf(expected,sizeof(expected),"1\t0\t2\t%016llx",(unsigned long long)read_integrity::Digest(original,8));
        Check(bytes.find(expected)!=std::string::npos);
        Check(Read(std::string(dir)+"/read-integrity-completion.json").find("\"incomplete\":false")!=std::string::npos);
    }
    std::snprintf(dir,sizeof(dir),"%s/read-integrity-overflow-XXXXXX",argv[1]);Check(mkdtemp(dir)!=nullptr);
    auto trace=std::make_unique<read_integrity::Trace>(dir,1,Gate);
    unsigned char pcm[4]={};trace->Observe(pcm,0,1);trace->Observe(pcm,1,1);trace->Observe(pcm,2,1);
    release_writer.store(true);trace->Finish();
    const auto done=Read(std::string(dir)+"/read-integrity-completion.json");
    Check(done.find("\"lost\":2")!=std::string::npos && done.find("\"incomplete\":true")!=std::string::npos);
    std::puts("read-return hash, copied record, partial read and bounded overflow passed");
}
