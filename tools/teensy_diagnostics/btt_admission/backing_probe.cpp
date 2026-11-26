// Explicit compile/link roots for the real adapter, never called by production.
#include "tools/btt/fs_backend.h"
template class btt::prototype::Backings<btt::prototype::FsBackend>;
extern "C" int btt_backing_probe(void* memory,size_t bytes,btt::prototype::FsBackend* backend) noexcept {
    using namespace btt::prototype;
    constexpr size_t metadata=(sizeof(Arena)+31)/32*32;
    if(!memory || reinterpret_cast<uintptr_t>(memory)%32 || bytes<=metadata)return 1;
    auto* arena=new(memory) Arena;
    if(arena->Init(static_cast<unsigned char*>(memory)+metadata,bytes-metadata)!=Code::Ok)return 1;
    Object<FsBackings> pool;
    Object<btt::Document> document;
    Object<btt::Pattern> pattern;
    Object<btt::Workspace> workspace;
    if(arena->New(pool,*backend)!=Code::Ok || arena->New(document)!=Code::Ok ||
       arena->New(pattern)!=Code::Ok || arena->New(workspace)!=Code::Ok) {
        (void)arena->Reset();arena->~Arena();return 2;
    }
    if(arena->Destroy(document)!=Code::Ok || arena->Destroy(pattern)!=Code::Ok ||
       arena->Destroy(workspace)!=Code::Ok || arena->Destroy(pool)!=Code::Ok)return 3;
    const auto result=arena->Reset();arena->~Arena();return result==Code::Ok?0:4;
}
