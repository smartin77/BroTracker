#pragma once
#include <SDL.h>
#include "ui/bringup_serial.h"
#include <fstream>
#include <memory>
#include <optional>
// Explicit automation mode of the delivered BTX. It uses the production client
// and platform transport; never LOADs, saves a session or issues transport.
namespace BttExportMode {
struct RecordingTransport final:SerialTransport {
    std::unique_ptr<SerialTransport> inner=MakeSerialTransport();
    std::ofstream rx,events;FILE* log;
    RecordingTransport(const std::filesystem::path& root,FILE* output):rx(root/"serial.raw",std::ios::binary),events(root/"serial-events.log"),log(output){}
    bool Open(FILE*,const char* device)override{return rx && events && inner->Open(log,device);}
    void Close()override{inner->Close();}
    bool Healthy()override{return inner->Healthy() && bool(rx) && bool(events);}
    const char* Error()const override{return inner->Error();}
    void Record(const char* direction,const char* p,unsigned n){events<<SDL_GetTicks()<<' '<<direction<<' ';
        constexpr char hex[]="0123456789abcdef";for(unsigned i=0;i<n;++i){const auto b=uint8_t(p[i]);events<<hex[b>>4]<<hex[b&15];}events<<'\n';events.flush();}
    int Read(char* p,unsigned n)override{const int count=inner->Read(p,n);if(count>0){rx.write(p,count);rx.flush();Record("RX",p,count);}return count;}
    int Write(const char* p,unsigned n)override{const int count=inner->Write(p,n);if(count>0)Record("TX",p,count);return count;}
};
inline std::optional<int> Run(int argc,char** argv) {
    if(argc<2 || std::strcmp(argv[1],"--export-btt"))return {};
    if(argc!=5 || !BroTracker::ValidDeviceBoard(argv[3]) || (!BroTracker::ValidDeviceBoot(argv[4]) && std::strcmp(argv[4],"unavailable")))return 2;
    const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    if(!std::filesystem::create_directory(root))return 3;
    FILE* log=nullptr;
#ifdef _WIN32
    log=_wfopen((root/"client.log").c_str(),L"w");
#else
    log=std::fopen((root/"client.log").c_str(),"w");
#endif
    if(!log)return 4;
    std::unique_ptr<FILE,decltype(&std::fclose)> log_owner(log,std::fclose);std::setvbuf(log,nullptr,_IONBF,0);
    BringUpSerial client(log,std::make_unique<RecordingTransport>(root,log));LocalPatternEditor editor;
    bool requested=false;const auto start=SDL_GetTicks();
    while(SDL_GetTicks()-start<180000) {
        const auto now=SDL_GetTicks();client.Tick(now);client.SyncEditor(editor,now);
        if(const auto* facts=client.DeviceFacts()) {
            if(std::strcmp(facts->board,argv[3]) || std::strcmp(*facts->boot?facts->boot:"unavailable",argv[4]))return 5;
        }
        if(!requested) {
            if(client.RequestBttExport(now,root/"candidates"))requested=true;
            else if(client.BttExportStatus()!="No exported candidate"){std::fprintf(log,"FAILED %s\n",client.BttExportStatus().c_str());return 10;}
        }
        if(requested && !client.BttExportActive()) {
            std::fprintf(log,"RESULT %s\n",client.BttExportStatus().c_str());
            if(!client.BttCandidate() || !client.MidiRecoveryStopped())return 6;
#ifdef _WIN32
            FILE* output=_wfopen((root/"exported.btt").c_str(),L"wbx");
#else
            FILE* output=std::fopen((root/"exported.btt").c_str(),"wbx");
#endif
            if(!output)return 7;
            uint8_t block[512];bool ok=true;const auto& candidate=*client.BttCandidate();
            for(uint32_t at=0;ok && at<candidate.size();at+=sizeof(block)) {
                const unsigned n=std::min<uint32_t>(sizeof(block),candidate.size()-at);
                ok=candidate.read(at,block,n)==n && std::fwrite(block,1,n,output)==n;
            }
            const bool flushed=std::fflush(output)==0,closed=std::fclose(output)==0;
            return ok&&flushed&&closed?0:8;
        }
        SDL_Delay(2);
    }
    std::fprintf(log,"TIMEOUT %s\n",client.BttExportStatus().c_str());return 9;
}
}
