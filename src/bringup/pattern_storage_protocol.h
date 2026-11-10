#pragma once
#include <cstring>
#include "development_pattern_selection.h"
#include "pattern_snapshot.h"
namespace BroTracker {
enum class StorageOperation { Invalid, Save, Restore, Seed1, Seed2, Seed3, Seed4,
    Load, Replace3, Migrate1, Migrate2, Migrate3, Migrate4, SaveDraft, LoadDraft, SavePattern };
inline unsigned StoragePatternNumber(StorageOperation op) noexcept {
    if(op>=StorageOperation::Seed1 && op<=StorageOperation::Seed4)return unsigned(op)-unsigned(StorageOperation::Seed1)+1;
    if(op>=StorageOperation::Migrate1 && op<=StorageOperation::Migrate4)return unsigned(op)-unsigned(StorageOperation::Migrate1)+1;
    return op==StorageOperation::Replace3?3:0;
}
inline bool ParseStorageLoad(const char* line,DevelopmentPattern& output) noexcept {
    constexpr const char prefix[]="BTSTORE1 LOAD 1 ";
    if(std::strncmp(line,prefix,sizeof(prefix)-1))return false;
    return output.Assign(line+sizeof(prefix)-1);
}
inline bool ParseStoragePatternSave(const char* line,DevelopmentPattern& output) noexcept {
    constexpr const char prefix[]="BTSTORE1 PATTERNSAVE 1 ";
    if(std::strncmp(line,prefix,sizeof(prefix)-1))return false;
    const std::string_view name(line+sizeof(prefix)-1);
    return DevelopmentHasExtension(name) && output.Assign(name) && !output.IsNone();
}
inline bool ParseSessionSnapshotSave(const char* line,std::size_t length,std::uint32_t (&view)[4]) noexcept {
    // SnapshotFields requires a nonzero first field (normally a request ID).
    // Parse the nonzero version first: zero is valid for every view coordinate.
    std::uint32_t fields[5]{};
    if(!SnapshotFields(line,length,"BTSTORE1 SAVE ",fields,5) || fields[0]!=2 ||
        std::strncmp(line,"BTSTORE1 SAVE 2 ",sizeof("BTSTORE1 SAVE 2 ")-1) ||
        fields[1]>=64 || fields[2]>=8 || fields[3]>=2 || fields[4]>=2)return false;
    for(unsigned i=0;i<4;++i)view[i]=fields[i+1];
    return true;
}
inline StorageOperation ParseStorageOperation(const char* line) noexcept {
    std::uint32_t view[4]{};
    if(ParseSessionSnapshotSave(line,std::strlen(line),view))return StorageOperation::Save;
    DevelopmentPattern name;if(ParseStorageLoad(line,name))return StorageOperation::Load;
    if(ParseStoragePatternSave(line,name))return StorageOperation::SavePattern;
    struct Entry {const char* line;StorageOperation operation;};
    constexpr Entry entries[]={{"BTSTORE1 SAVE 1",StorageOperation::Save},{"BTSTORE1 RESTORE 1",StorageOperation::Restore},
        {"BTSTORE1 SEED 1 1",StorageOperation::Seed1},{"BTSTORE1 SEED 1 2",StorageOperation::Seed2},
        {"BTSTORE1 SEED 1 3",StorageOperation::Seed3},{"BTSTORE1 SEED 1 4",StorageOperation::Seed4},
        {"BTSTORE1 REPLACE 1 3",StorageOperation::Replace3},
        {"BTSTORE1 MIGRATE 1 1",StorageOperation::Migrate1},{"BTSTORE1 MIGRATE 1 2",StorageOperation::Migrate2},
        {"BTSTORE1 MIGRATE 1 3",StorageOperation::Migrate3},{"BTSTORE1 MIGRATE 1 4",StorageOperation::Migrate4}};
    for(const auto& e:entries)if(!std::strcmp(line,e.line))return e.operation;
    return StorageOperation::Invalid;
}
inline const char* StorageOperationName(StorageOperation op) noexcept {
    constexpr const char* names[]={"invalid","save","restore","seed-1","seed-2","seed-3","seed-4",
        "load","replace-3","migrate-1","migrate-2","migrate-3","migrate-4","draft-save","draft-load","pattern-save"};
    return names[static_cast<unsigned>(op)];
}
}
