#pragma once
#include "../../tools/btt/foundation.h"
#include "bringup/btt_export_protocol.h"
#include <filesystem>
#include <string>
#include <future>
#include <cstdio>
// Host filesystem boundary. Production uses these ordinary system operations;
// tests inject a per-client implementation, never environment/INI fault controls.
struct BttCandidateIo {
    virtual ~BttCandidateIo()=default;
    virtual unsigned Write(FILE*,const void*,unsigned) noexcept;
    virtual bool Flush(FILE*) noexcept;
    virtual bool Sync(FILE*) noexcept;
    virtual bool Close(FILE*) noexcept;
    virtual FILE* OpenRead(const std::filesystem::path&) noexcept;
    virtual void Remove(const std::filesystem::path&,std::error_code&) noexcept;
    virtual void Report(const std::string&) noexcept;
};
// A temporary, independently owned full document. Never a durable session.
class BttCandidateReceiver {
    struct File;
    std::shared_ptr<BttCandidateIo> io_;
    enum class Phase { Idle, Begin, Data, Validate, End, Cancel } phase_=Phase::Idle;
    std::filesystem::path directory_,path_;
    FILE* file_=nullptr;
    std::shared_ptr<File> staged_;
    btt::ReaderOwner candidate_;
    uint32_t id_=0,generation_=0,length_=0,crc_=0,offset_=0,at_=0;
    bool queued_=false,waiting_=false;
    std::string validation_artifact_,nonce_,boot_,status_="No exported candidate";
    std::string notice_,display_status_="No exported candidate";
    void SetStatus(std::string);
    void Notice(std::string);
    std::string Finish(FILE*);
    void Cleanup() noexcept;
    void Fail(const char* reason);
    struct Validation {std::shared_ptr<File> file;std::string error;};
    std::future<Validation> validation_;
    void ValidateFile();
    bool ConsumeValidation();
public:
    explicit BttCandidateReceiver(std::shared_ptr<BttCandidateIo> io={});
    ~BttCandidateReceiver();
    static std::filesystem::path UserPath();
    bool Begin(const std::filesystem::path&,uint32_t id,const char* boot,uint32_t now);
    bool Active()const noexcept{return phase_!=Phase::Idle;}
    void Cancel();
    void Disconnect();
    bool Tick(uint32_t now);
    bool Response(std::string_view line,uint32_t now);
    unsigned Command(char*,unsigned,uint32_t now);
    const btt::ReaderOwner& Candidate()const{return candidate_;}
    const std::string& Status()const{return display_status_;}
    const std::filesystem::path& PendingPath()const{return path_;}
};
