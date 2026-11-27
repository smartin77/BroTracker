// Compile/link-only candidate lifecycle root. Never called by production.
#include "tools/btt/resource_admission.h"
#include "tools/btt/sd_source_service.h"
// One cooperative foreground turn, not a blocking preparation harness.
extern "C" int btt_sd_source_probe(btt::prototype::SdSourceService* sources,
    btt::prototype::Candidate* candidate,btt::prototype::AdmissionScratch* scratch,bool cancel) noexcept {
    if(cancel)return static_cast<int>(candidate->Cancel());
    (void)sources->Service(); // Caller has already serviced producer refills.
    return static_cast<int>(candidate->Step(*sources,*scratch));
}
extern "C" btt::prototype::SourceService* btt_construct_source_service(void* storage,
    BroTracker::SdSampleStreaming* producer) noexcept {
    return new(storage)btt::prototype::SdSourceService(*producer);
}
extern "C" int btt_resource_probe(btt::prototype::Arena* arena,const btt::Document* document,
                                  btt::prototype::SourceService* sources) noexcept {
    using namespace btt::prototype;
    Object<Candidate> candidate;
    Object<AdmissionScratch> scratch;
    Object<btt::Pattern> pattern;
    Object<btt::Workspace> workspace;
    auto cleanup=[&]() noexcept {
        if(candidate.get())(void)arena->Destroy(candidate);
        if(scratch.get())(void)arena->Destroy(scratch);
        if(pattern.get())(void)arena->Destroy(pattern);
        if(workspace.get())(void)arena->Destroy(workspace);
    };
    if(arena->New(candidate)!=Code::Ok || arena->New(scratch)!=Code::Ok ||
       arena->New(pattern)!=Code::Ok || arena->New(workspace)!=Code::Ok){cleanup();return 1;}
    if(candidate->Begin(*document,{101,{}},*workspace,*pattern,*scratch)==btt::Result::Ok) {
        (void)candidate->Step(*sources,*scratch);
        StoppedExchange exchange;
        if(candidate->initial_resources_ready()) {
            (void)exchange.Commit(*candidate,true);(void)exchange.Detach(true);
        }
    }
    (void)candidate->Discard();
    if(arena->Destroy(candidate)!=Code::Ok || arena->Destroy(scratch)!=Code::Ok ||
       arena->Destroy(pattern)!=Code::Ok || arena->Destroy(workspace)!=Code::Ok)return 2;
    return 0;
}
