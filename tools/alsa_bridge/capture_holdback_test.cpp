#include "capture_holdback.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

void Check(bool value){if(!value) std::abort();}
int main() {
    using capture_holdback::Request;
    Check(Request(44,256)==0 && Request(45,256)==0 && Request(46,256)==1);
    Check(Request(301,256)==256 && Request(2047,256)==256);
    Check(Request(-EAGAIN,256)==0 && Request(-EPIPE,256)==0);
    Check(Request(100,2)==2 && Request(100,0)==0);
    for(unsigned frames:{45u,64u,128u}) {
        Check(Request(frames-1,256,frames)==0 && Request(frames,256,frames)==0);
        Check(Request(frames+1,256,frames)==1 && Request(frames+256,256,frames)==256);
        Check(Request(2048,256,frames)==256 && Request(frames+100,2,frames)==2);
        Check(Request(frames+100,0,frames)==0 && Request(-EAGAIN,256,frames)==0);
        capture_holdback::Metrics m;m.Observe(frames,frames);m.Observe(frames+1,frames);
        Check(m.samples==2 && m.gated==1);
    }
    Check(Request(0,256,0)==256); // Disabled path retains normal chunk request.
    for(const char* legacy:{static_cast<const char*>(nullptr),"0","1","true",""}) {
        const auto absent=capture_holdback::Select(nullptr,legacy);
        Check(absent.frames==(legacy && legacy[0]=='1'?45u:0u) && !absent.invalid);
        for(const char* numeric:{"0","45","64","128"}) {
            const auto selected=capture_holdback::Select(numeric,legacy);
            Check(selected.frames==unsigned(std::strtoul(numeric,nullptr,10)) && !selected.invalid);
        }
        for(const char* invalid:{"","-1","129","4294967296","999999999999999999999999","45x","true"," 45","+45"}) {
            const auto selected=capture_holdback::Select(invalid,legacy);
            Check(selected.frames==0 && selected.invalid);
        }
    }
    for(unsigned reserve:{45u,64u,128u}) {
    // Simulated publication-before-copy: newest packet still contains stale
    // ring data. Exercise both ring wraps and short/EAGAIN transfers, using
    // the actual production request limiter; never consume that newest span.
    std::array<uint64_t,2048> ring{};
    uint64_t published=0,consumed=0,pending_start=0;
    for(unsigned tick=0;tick<500;++tick) {
        // Complete the previously published packet before publishing the next.
        for(uint64_t x=pending_start;x<published;++x) ring[x%ring.size()]=x+1;
        pending_start=published;published+=(tick%10==0?45:44);
        const auto available=published-consumed;
        const auto limit=std::min(uint64_t(256),8192-consumed%8192);
        const auto request=Request(available,limit,reserve);
        const auto returned=tick%13==0?0:(tick%7==0?request/2:request); // EAGAIN or short.
        Check(returned<=request && consumed+request<= (published>reserve?published-reserve:0));
        for(uint64_t x=consumed;x<consumed+returned;++x) Check(ring[x%ring.size()]==x+1);
        consumed+=returned;
    }
    Check(published-consumed>=reserve);
    } // Shutdown does not silently drain the guard.
    // No cached quota survives recovery; fresh availability is authoritative.
    Check(Request(-EPIPE,256)==0);Check(Request(44,256)==0);Check(Request(89,256)==44);
    capture_holdback::Metrics metrics;metrics.Observe(44);metrics.Observe(-EAGAIN);metrics.Observe(100);
    Check(metrics.samples==3 && metrics.gated==1 && metrics.max_available==100);
    std::puts("holdback bounds, packet-publication model, short/EAGAIN, wraps, recovery and shutdown passed");
}
