#pragma once
#include "ui/btt_candidate.h"
#include <array>
#include <condition_variable>
#include <mutex>
#include <set>
#include <vector>
// Per-client deterministic doubles; no production settings, environment or sleeps.
class CandidateIoFaults final:public BttCandidateIo {
public:
    enum Fault { None,ShortWrite,FlushFailure,SyncFailure,CloseFailure,ReopenFailure,ReaderCloseFailure,RemoveFileFailure,RemoveDirectoryFailure,Count };
private:
    mutable std::mutex mutex_;std::condition_variable wake_;
    std::array<bool,Count> faults_{};std::array<unsigned,Count> hits_{};
    bool gate_=false,entered_=false,released_=true;
    unsigned bytes_=0,removes_=0;std::set<FILE*> writers_,readers_;std::vector<std::string> reports_;
    bool Consume(Fault fault) noexcept {std::lock_guard<std::mutex> lock(mutex_);if(!faults_[fault])return false;faults_[fault]=false;++hits_[fault];return true;}
public:
    void Arm(std::initializer_list<Fault> faults,bool gate=false) {
        std::lock_guard<std::mutex> lock(mutex_);faults_.fill(false);hits_.fill(0);
        for(auto fault:faults)if(fault!=None)faults_[fault]=true;
        bytes_=removes_=0;reports_.clear();gate_=gate;entered_=false;released_=!gate;
    }
    void Arm(Fault fault,bool gate=false){Arm({fault},gate);}
    void Release(){std::lock_guard<std::mutex> lock(mutex_);released_=true;wake_.notify_all();}
    bool AwaitFlush(){std::unique_lock<std::mutex> lock(mutex_);return wake_.wait_for(lock,std::chrono::seconds(5),[&]{return entered_;});}
    unsigned Written()const{std::lock_guard<std::mutex> lock(mutex_);return bytes_;}
    unsigned Hits(Fault fault)const{std::lock_guard<std::mutex> lock(mutex_);return hits_[fault];}
    unsigned Removes()const{std::lock_guard<std::mutex> lock(mutex_);return removes_;}
    unsigned Handles()const{std::lock_guard<std::mutex> lock(mutex_);return unsigned(writers_.size()+readers_.size());}
    bool Reported(const std::string& value)const{std::lock_guard<std::mutex> lock(mutex_);for(const auto& report:reports_)if(report.find(value)!=std::string::npos)return true;return false;}
    unsigned Write(FILE* file,const void* data,unsigned n) noexcept override {
        {std::lock_guard<std::mutex> lock(mutex_);writers_.insert(file);}
        const unsigned written=BttCandidateIo::Write(file,data,Consume(ShortWrite)?n-1:n);
        {std::lock_guard<std::mutex> lock(mutex_);bytes_+=written;}return written;
    }
    bool Flush(FILE* file) noexcept override {
        {std::unique_lock<std::mutex> lock(mutex_);
            if(gate_){entered_=true;wake_.notify_all();wake_.wait(lock,[&]{return released_;});gate_=false;}}
        const bool actual=BttCandidateIo::Flush(file);return !Consume(FlushFailure)&&actual;
    }
    bool Sync(FILE* file) noexcept override {const bool actual=BttCandidateIo::Sync(file);return !Consume(SyncFailure)&&actual;}
    bool Close(FILE* file) noexcept override {
        bool reader;
        {std::lock_guard<std::mutex> lock(mutex_);reader=readers_.erase(file)!=0;writers_.erase(file);}
        // A failed fclose still consumes its FILE. Close the real stream, then
        // inject the uncertain result; never leave a live fake descriptor behind.
        const bool actual=BttCandidateIo::Close(file);return !Consume(reader?ReaderCloseFailure:CloseFailure)&&actual;
    }
    FILE* OpenRead(const std::filesystem::path& path) noexcept override {
        if(Consume(ReopenFailure))return nullptr;
        FILE* file=BttCandidateIo::OpenRead(path);
        if(file){std::lock_guard<std::mutex> lock(mutex_);readers_.insert(file);}return file;
    }
    void Remove(const std::filesystem::path& path,std::error_code& error) noexcept override {
        {std::lock_guard<std::mutex> lock(mutex_);++removes_;}
        if(Consume(path.filename()=="candidate.btt"?RemoveFileFailure:RemoveDirectoryFailure)){error=std::make_error_code(std::errc::permission_denied);return;}
        BttCandidateIo::Remove(path,error);
    }
    void Report(const std::string& message) noexcept override {std::lock_guard<std::mutex> lock(mutex_);reports_.push_back(message);}
};
struct CandidateGateRelease {
    std::shared_ptr<CandidateIoFaults> io;
    ~CandidateGateRelease(){io->Release();}
};
