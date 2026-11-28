#pragma once
#include "pattern_snapshot.h"
#include <string_view>
namespace BroTracker {
enum class BttSaveAction { Save, Status, Recover };
struct BttSaveRequest {BttSaveAction action=BttSaveAction::Status;uint32_t id=0,target=0;};
inline bool ParseBttSaveRequest(std::string_view line,BttSaveRequest& request) noexcept {
    uint32_t f[2]{};
    for(const auto action:{BttSaveAction::Save,BttSaveAction::Status}) {
        const char* prefix=action==BttSaveAction::Save?"BTBTT1 SAVE 1 ":"BTBTT1 SAVEGET 1 ";
        if(SnapshotFields(line.data(),line.size(),prefix,f,1) && f[0]) {
            request={action,f[0],0};return true;
        }
    }
    if(SnapshotFields(line.data(),line.size(),"BTBTT1 RECOVER 1 ",f,2) && f[0] && f[1]) {
        request={BttSaveAction::Recover,f[0],f[1]};return true;
    }
    return false;
}
}
