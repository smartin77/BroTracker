#include "btt_candidate.h"
#include "bringup/device_facts.h"
#include <random>
#include <algorithm>
#include <cstdlib>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif
namespace fs=std::filesystem;
unsigned BttCandidateIo::Write(FILE* file,const void* data,unsigned n) noexcept {return unsigned(std::fwrite(data,1,n,file));}
bool BttCandidateIo::Flush(FILE* file) noexcept {return std::fflush(file)==0;}
bool BttCandidateIo::Sync(FILE* file) noexcept {
#ifdef _WIN32
    return _commit(_fileno(file))==0;
#else
    return fsync(fileno(file))==0;
#endif
}
bool BttCandidateIo::Close(FILE* file) noexcept {return std::fclose(file)==0;}
FILE* BttCandidateIo::OpenRead(const fs::path& path) noexcept {
#ifdef _WIN32
    return _wfsopen(path.c_str(),L"rb",_SH_DENYWR);
#else
    return std::fopen(path.c_str(),"rb");
#endif
}
void BttCandidateIo::Remove(const fs::path& path,std::error_code& error) noexcept {fs::remove(path,error);}
void BttCandidateIo::Report(const std::string& message) noexcept {std::fprintf(stderr,"BTT candidate: %s\n",message.c_str());}
namespace {
std::string FinishFile(BttCandidateIo& io,FILE* file) {
    const bool flushed=io.Flush(file);
    const bool synced=flushed && io.Sync(file);
    const bool closed=io.Close(file); // fclose consumes the stream even on error; never retry it.
    std::string failures;
    if(!flushed)failures="flush";
    else if(!synced)failures="sync";
    if(!closed)failures+=(failures.empty()?"":"/")+std::string("close");
    return failures;
}
}
struct BttCandidateReceiver::File final:btt::Reader {
    std::shared_ptr<BttCandidateIo> io;fs::path directory;FILE* file=nullptr;uint32_t length=0;bool retired=false;
    File(std::shared_ptr<BttCandidateIo> backend,fs::path dir,uint32_t n):io(std::move(backend)),directory(std::move(dir)),length(n) {
        file=io->OpenRead(directory/"candidate.btt");
        if(file)std::setvbuf(file,nullptr,_IONBF,0);
    }
    std::string Retire() {
        if(retired)return {};
        retired=true; // A retained artifact must not be silently retried/deleted by the destructor.
        if(file){FILE* closing=file;file=nullptr;if(!io->Close(closing))return "Backing close uncertain; artifact retained at "+directory.string();}
        std::error_code ec;io->Remove(directory/"candidate.btt",ec);if(!ec)io->Remove(directory,ec);
        return ec?"Cleanup uncertain; artifact retained at "+directory.string():std::string{};
    }
    ~File(){const auto error=Retire();if(!error.empty())io->Report(error);}
    const void* identity()const override{return this;}
    uint32_t size()const override{return length;}
    unsigned read(uint32_t at,void* p,unsigned n)const override {
        if(!file || n>512 || at>length || n>length-at || std::fseek(file,at,SEEK_SET))return 0;
        return unsigned(std::fread(p,1,n,file));
    }
};
BttCandidateReceiver::BttCandidateReceiver(std::shared_ptr<BttCandidateIo> io):io_(io?std::move(io):std::make_shared<BttCandidateIo>()){}
void BttCandidateReceiver::SetStatus(std::string status) {
    status_=std::move(status);display_status_=status_;
    if(!notice_.empty())display_status_+="; "+notice_;
}
void BttCandidateReceiver::Notice(std::string notice) {
    notice_="Export "+std::to_string(id_)+": "+std::move(notice);SetStatus(status_);io_->Report(notice_);
}
std::string BttCandidateReceiver::Finish(FILE* file) {return FinishFile(*io_,file);}
std::filesystem::path BttCandidateReceiver::UserPath() {
#ifdef _WIN32
    const auto* base=_wgetenv(L"LOCALAPPDATA");fs::path root=base?fs::path(base):fs::path{};
#else
    const auto* base=std::getenv("XDG_DATA_HOME");fs::path root=base?fs::path(base):fs::path{};
    if(!root.is_absolute()){const auto* home=std::getenv("HOME");root=home?fs::path(home)/".local/share":fs::path{};}
#endif
    return root.is_absolute()?root/"BroTracker/btt-candidates-v1":fs::path{};
}
void BttCandidateReceiver::Cleanup() noexcept {
    if(staged_){const auto error=staged_->Retire();staged_.reset();if(!error.empty())Notice(error);}
    if(!directory_.empty()) {
        if(file_){FILE* closing=file_;file_=nullptr;const auto error=Finish(closing);
            if(!error.empty()){Notice("Candidate "+error+" uncertain; artifact retained at "+directory_.string());directory_.clear();path_.clear();return;}}
        std::error_code ec;io_->Remove(path_,ec);if(!ec)io_->Remove(directory_,ec);
        if(ec)Notice("Cleanup uncertain; artifact retained at "+directory_.string());
        else SetStatus(status_+"; temporary artifact cleaned");
        directory_.clear();path_.clear();
    }
}
BttCandidateReceiver::~BttCandidateReceiver(){
    Disconnect();
    if(validation_.valid()){validation_.wait();ConsumeValidation();}
    Cleanup();
}
bool BttCandidateReceiver::Begin(const fs::path& root,uint32_t id,const char* boot,uint32_t now) {
    if(Active() || validation_.valid() || !id || (!BroTracker::ValidDeviceBoot(boot) && std::strcmp(boot,"unavailable")) || !root.is_absolute())return false;
    Cleanup();id_=id;generation_=offset_=length_=crc_=0;boot_=boot;at_=now;waiting_=false;
    try {
        fs::create_directories(root);std::random_device random;char name[40];
        std::snprintf(name,sizeof(name),"export-%08lx%08lx",(unsigned long)uint32_t(random()),(unsigned long)uint32_t(random()));
        nonce_.clear();
        for(unsigned i=0;i<4;++i){char part[9];std::snprintf(part,sizeof(part),"%08lx",(unsigned long)uint32_t(random()));nonce_+=part;}
        if(!BroTracker::ValidDeviceBoot(nonce_))throw std::runtime_error("Export entropy unavailable");
        directory_=root/name;
        if(!fs::create_directory(directory_)){directory_.clear();SetStatus("Candidate directory collision");return false;}
        path_=directory_/"candidate.btt";
#ifdef _WIN32
        const int fd=_wopen(path_.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
        if(fd>=0){file_=_fdopen(fd,"wb");if(!file_)_close(fd);}
#else
        const int fd=open(path_.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0){file_=fdopen(fd,"wb");if(!file_)close(fd);}
#endif
        if(!file_){SetStatus("Candidate create failed");Cleanup();return false;}
        std::setvbuf(file_,nullptr,_IONBF,0);
    } catch(const std::exception& e){SetStatus(e.what());Cleanup();return false;}
    phase_=Phase::Begin;queued_=true;SetStatus("Export queued (candidate only)");return true;
}
void BttCandidateReceiver::Fail(const char* reason) {
    SetStatus(reason);Cleanup();
    // BEGIN may have been accepted with its reply lost. generation 0 CANCEL is
    // resolved by the device only for this exact request, never another source.
    phase_=Phase::Cancel;queued_=true;waiting_=false;
}
void BttCandidateReceiver::Cancel(){if(Active() && phase_!=Phase::Cancel)Fail("Export cancelled");}
void BttCandidateReceiver::Disconnect(){if(Active()){SetStatus("Export disconnected; previous candidate retained");Cleanup();phase_=Phase::Idle;queued_=waiting_=false;}}
bool BttCandidateReceiver::ConsumeValidation() {
    Validation result;
    try {result=validation_.get();}
    catch(const std::exception&){result.error="Validation/cleanup outcome unconfirmed; inspect "+validation_artifact_;}
    if(phase_==Phase::Validate) {
        if(result.file){staged_=std::move(result.file);phase_=Phase::End;queued_=true;waiting_=false;return false;}
        Notice(result.error);Fail("Candidate validation failed");
    } else {
        // Cancellation/disconnect only revoke promotion. Worker diagnostics and
        // retained paths still belong to this export and must remain visible.
        if(result.file){const auto error=result.file->Retire();
            if(!error.empty())Notice(error);
            else SetStatus(status_+"; late validation discarded; temporary artifact cleaned");}
        if(!result.error.empty())Notice(result.error);
    }
    return true;
}
bool BttCandidateReceiver::Tick(uint32_t now) {
    bool report=false;
    if(validation_.valid() && validation_.wait_for(std::chrono::seconds(0))==std::future_status::ready)report=ConsumeValidation();
    if(Active() && now-at_>=BroTracker::kBttExportTimeout){
        if(phase_==Phase::Cancel){phase_=Phase::Idle;queued_=waiting_=false;SetStatus(status_+"; cancellation unconfirmed");}
        else {Fail("Export timeout; previous candidate retained");at_=now;}}
    return report;
}
void BttCandidateReceiver::ValidateFile() {
    // The worker owns every file operation after the final write. Cancellation
    // can drop the transaction without waiting for filesystem validation.
    const auto dir=directory_,path=path_;const auto length=length_,crc=crc_;FILE* file=file_;
    validation_artifact_=dir.string();
    const auto io=io_;
    validation_=std::async(std::launch::async,[io,dir,path,length,crc,file]() -> Validation {
        const auto error=FinishFile(*io,file);
        if(!error.empty())return {{},"Candidate "+error+" uncertain; artifact retained at "+dir.string()};
        auto reader=std::make_shared<File>(io,dir,length);
        const auto failed=[&](std::string reason) -> Validation {
            const auto cleanup=reader->Retire();
            reason+="; "+(cleanup.empty()?std::string("temporary artifact cleaned"):cleanup);
            return {{},std::move(reason)};
        };
        if(!reader->file)return failed("Candidate reopen failed");
        std::error_code ec;if(fs::file_size(path,ec)!=length || ec)return failed("Candidate length changed");
        btt::Document document;btt::Workspace work;
        if(btt::Validate(reader,document,work)!=btt::Result::Ok || document.whole_crc!=crc || document.saved_bytes!=length)
            return failed("Complete BTT validation failed");
        return {std::move(reader),{}};
    });
    file_=nullptr;directory_.clear();path_.clear();phase_=Phase::Validate;queued_=waiting_=false;
}
bool BttCandidateReceiver::Response(std::string_view line,uint32_t now) {
    using namespace BroTracker;
    if(line.substr(0,13)!="BTBTT1 EXPORT")return false;
    if(!Active())return true;
    std::array<std::string_view,10> w{};const auto n=BttExportWords(line,w);uint32_t id=0,g=0;
    if(n<5 || w[0]!="BTBTT1" || w[2]!="1" || !BttExportNumber(w[3],id) || id!=id_){Fail("Malformed/stale export reply");return true;}
    if(n<6 || w[n-1]!=nonce_){Fail("Stale export challenge");return true;}
    if(w[1]=="EXPORTERROR" && n==6){SetStatus("Device export failure: "+std::string(w[4]));Cleanup();phase_=Phase::Idle;queued_=waiting_=false;return true;}
    if(!BttExportNumber(w[4],g)){Fail("Malformed generation");return true;}
    if(phase_==Phase::Cancel){if(w[1]=="EXPORTCANCELLED" && n==6 && (!generation_ || g==generation_)){phase_=Phase::Idle;queued_=waiting_=false;}return true;}
    if(!waiting_){Fail("Unsolicited export reply");return true;}
    at_=now;
    if(phase_==Phase::Begin && w[1]=="EXPORTBEGIN" && n==9) {
        if(!g || !BttExportNumber(w[5],length_) || length_<32 || length_>kBttExportMax || !BttExportNumber(w[6],crc_) || w[7]!=boot_){Fail("Invalid export source/length");return true;}
        generation_=g;phase_=Phase::Data;
    } else if(phase_==Phase::Data && w[1]=="EXPORTDATA" && n==10 && g==generation_) {
        uint32_t at=0,count=0,crc=0;uint8_t bytes[kBttExportChunk];
        if(!BttExportNumber(w[5],at)||at!=offset_||!BttExportNumber(w[6],count)||!count||count!=std::min<uint32_t>(kBttExportChunk,length_-offset_)||!BttExportNumber(w[7],crc)||w[8].size()!=2*count){Fail("Invalid export offset/size");return true;}
        const auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        for(unsigned i=0;i<count;++i){const int a=nibble(w[8][2*i]),b=nibble(w[8][2*i+1]);if(a<0||b<0){Fail("Invalid export hex");return true;}bytes[i]=uint8_t(16*a+b);}
        if((BttExportCrc(0xffffffffu,bytes,count)^0xffffffffu)!=crc){Fail("Export chunk checksum");return true;}
        if(!file_ || io_->Write(file_,bytes,count)!=count || std::ferror(file_)){Fail("Candidate write failed");return true;}
        offset_+=count;
        if(offset_==length_){try {ValidateFile();}catch(const std::exception&){Fail("Candidate validation worker unavailable");}return true;}
    } else if(phase_==Phase::End && w[1]=="EXPORTDONE" && n==8 && g==generation_) {
        uint32_t length=0,crc=0;if(!BttExportNumber(w[5],length)||length!=length_||!BttExportNumber(w[6],crc)||crc!=crc_){Fail("Invalid export completion");return true;}
        candidate_=std::move(staged_);phase_=Phase::Idle;queued_=waiting_=false;SetStatus("Validated complete BTT candidate (not current session)");return true;
    } else {Fail("Conflicting export reply");return true;}
    queued_=true;waiting_=false;return true;
}
unsigned BttCandidateReceiver::Command(char* out,unsigned capacity,uint32_t now) {
    if(!queued_)return 0;
    int n=phase_==Phase::Begin?std::snprintf(out,capacity,"BTBTT1 EXPORT 1 %lu %s\n",(unsigned long)id_,nonce_.c_str()):
        std::snprintf(out,capacity,"BTBTT1 %s 1 %lu %lu %lu %s\n",phase_==Phase::Data?"EXPORTNEXT":phase_==Phase::End?"EXPORTEND":"EXPORTCANCEL",
            (unsigned long)id_,(unsigned long)generation_,(unsigned long)offset_,nonce_.c_str());
    if(n<=0 || unsigned(n)>=capacity)return 0;
    queued_=false;waiting_=true;at_=now;return unsigned(n);
}
