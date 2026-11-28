// Compile/link roots only. Never invoked by production; no transport handler.
#include "tools/btt/fs_save.h"
using namespace btt;
using namespace btt::prototype;
extern "C" {
char size_FsSave[sizeof(FsSave)];
char size_Document[sizeof(Document)];
char size_Workspace[sizeof(Workspace)];
char size_FsBackings[sizeof(FsBackings)];
char size_ReaderOwner[sizeof(ReaderOwner)];
char size_FsFile[sizeof(FsFile)];
// Isolated incremental reservation: existing source Document/registry/pins are
// reused, but one comparison/validation Document and Workspace are provided.
__attribute__((section(".dmabuffers"),aligned(32)))
char btt_fs_save_reservation[(sizeof(FsSave)+sizeof(Document)+sizeof(Workspace)+31)/32*32];
int btt_fs_save_probe(FsSave* save,const Document* source,FsBackings* registry,
    FsBackings::Token token,const char* destination,Document* scratch,Workspace* work,bool ready) {
    const auto result=save->Run(*source,*registry,token,destination,*scratch,*work,ready,13750);
    const bool cleaned=save->Cleanup(ready);
    return int(result)+int(cleaned)+int(save->CanDestroy())+int(save->detail())+
        int(save->candidate_retained())+int(save->previous_retained())+int(save->promoted());
}
}
