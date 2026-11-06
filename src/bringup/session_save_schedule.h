#pragma once
#include "pattern_file.h"
namespace BroTracker {
// Main-owner pending intent survives blocked storage and failed writes.
// Failed I/O is latched until another mutation/request, preventing a tight retry loop.
class SessionSaveSchedule {
public:
    void Changed() noexcept {pending_=true;failed_=false;}
    bool Pending()const noexcept {return pending_;}
    bool Ready(bool storage_ready)const noexcept {return pending_ && !failed_ && storage_ready;}
    void Completed(PatternFileResult result) noexcept {
        if(result==PatternFileResult::Ok)pending_=false;
        else if(result!=PatternFileResult::Busy)failed_=true;
    }
private:
    bool pending_=false,failed_=false;
};
}
