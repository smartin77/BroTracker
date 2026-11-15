#pragma once
#include <sd_access.h>
#include "bringup/wav_directory.h"
#include "core/audio/wav_inspection_job.h"
namespace BroTracker {
// One bounded foreground operation per turn, never inside the audio callback.
// Directory and WAV handles are idle between turns; the read lease is not held.
class SdWavBrowseJob {
public:
    bool Begin(const WavOpenRequest& request) {
        if(Busy())return false;
        request_=request;page_={};result_=WavDirectoryResult::Ok;fallback_=0;finish_page_=false;
        std::strcpy(directory_,request.directory);phase_=Open;return true;
    }
    bool Busy()const{return phase_!=Idle && phase_!=DonePhase;}
    bool Done()const{return phase_==DonePhase;}
    void Consumed(){phase_=Idle;}
    const WavOpenRequest& Request()const{return request_;}
    const WavBrowsePage& Page()const{return page_;}
    WavDirectoryResult Result()const{return result_;}
    void Step() {
        if(!Busy())return;
        SdReadScope lease;if(!lease)return;
        if(phase_==Open) {
            if(!dir_.open(directory_,O_RDONLY)) {
                const auto error=SD.sdfs.sdErrorCode()?WavDirectoryResult::ReadError:WavDirectoryResult::Missing;
                if(request_.fallback && std::strcmp(directory_,"/")) {
                    if(!fallback_)fallback_=error==WavDirectoryResult::Missing?1:2;
                    ParentWavDirectory(directory_);return;
                }
                result_=error;phase_=DonePhase;return;
            }
            if(!dir_.isDirectory()){result_=WavDirectoryResult::NotDirectory;phase_=CloseDir;return;}
            page_.next=request_.offset;std::strcpy(page_.directory,directory_);page_.fallback=fallback_;phase_=Seek;return;
        }
        if(phase_==Seek) {
            if(!dir_.seekSet(request_.offset)){result_=WavDirectoryResult::ReadError;phase_=CloseDir;return;}
            phase_=Next;return;
        }
        if(phase_==Next) {
            if(!entry_.openNext(&dir_,O_RDONLY)) {
                if(dir_.getError())result_=WavDirectoryResult::ReadError;
                else page_.done=true;
                phase_=CloseDir;return;
            }
            phase_=Name;return;
        }
        if(phase_==Name) {
            char name[257]{};const auto n=entry_.getName(name,sizeof(name));const auto next=dir_.curPosition();
            if(next>UINT32_MAX || next<=page_.next){result_=WavDirectoryResult::ReadError;finish_page_=true;phase_=CloseEntry;return;}
            page_.next=next;++page_.scanned;const bool directory=entry_.isDirectory();
            finish_page_=page_.scanned>=64;
            if(!n || n>=sizeof(name)-1 || (request_.version<3 && std::strlen(name)>63) || !WavEntryName(name) ||
                (!directory && !(request_.version>=3?WavFilename(name):CompatibleWavName(name)))){phase_=CloseEntry;return;}
            auto& e=page_.entries[page_.count++];std::strcpy(e.name,name);e.kind=directory?WavEntryKind::Directory:WavEntryKind::File;
            finish_page_|=page_.count>=kPatternListPageSize;
            if(!directory && request_.version>=3){inspection_.Begin();phase_=Inspect;finish_page_=true;}
            else phase_=CloseEntry;
            return;
        }
        if(phase_==Inspect) {
            class Reader final:public WavBufferReader {
                FsFile& file_;
            public:explicit Reader(FsFile& f):file_(f){}
                bool Size(std::uint64_t& n)noexcept override{n=file_.size();return true;}
                bool Seek(std::uint64_t n)noexcept override{return n<=UINT32_MAX && file_.seekSet(n);}
                std::size_t Read(std::uint8_t* p,std::size_t n)noexcept override{const auto r=file_.read(p,n);return r<0?0:r;}
            } reader(entry_);
            inspection_.Step(reader);
            if(inspection_.Done()) {
                const auto& m=inspection_.Metadata();auto& f=page_.entries[page_.count-1].facts;
                f.known=true;f.status=inspection_.Status();f.format=m.format;f.channels=m.channels;f.bits=m.bits;f.sample_rate=m.sample_rate;
                phase_=CloseEntry;
            }
            return;
        }
        if(phase_==CloseEntry) {
            if(!entry_.close()){result_=WavDirectoryResult::ReadError;finish_page_=true;}
            phase_=finish_page_?CloseDir:Next;return;
        }
        if(!dir_.close())result_=WavDirectoryResult::ReadError;
        else if(result_!=WavDirectoryResult::Ok && request_.fallback && std::strcmp(directory_,"/")) {
            if(!fallback_)fallback_=2;
            ParentWavDirectory(directory_);result_=WavDirectoryResult::Ok;page_={};phase_=Open;return;
        }
        phase_=DonePhase;
    }
private:
    enum Phase{Idle,Open,Seek,Next,Name,Inspect,CloseEntry,CloseDir,DonePhase};Phase phase_=Idle;
    FsFile dir_,entry_;WavInspectionJob inspection_;WavOpenRequest request_{};WavBrowsePage page_{};
    WavDirectoryResult result_=WavDirectoryResult::Ok;char directory_[64]{};unsigned fallback_=0;bool finish_page_=false;
};
}
