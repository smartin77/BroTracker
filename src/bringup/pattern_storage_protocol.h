#pragma once
#include <cstring>
namespace BroTracker {
enum class StorageOperation { Invalid, Save, Restore, SeedTheme, SeedMidi, LoadTheme, LoadMidi };
inline StorageOperation ParseStorageOperation(const char* line) noexcept {
    struct Entry {const char* line;StorageOperation operation;};
    constexpr Entry entries[]={{"BTSTORE1 SAVE 1",StorageOperation::Save},{"BTSTORE1 RESTORE 1",StorageOperation::Restore},
        {"BTSTORE1 SEED 1 theme",StorageOperation::SeedTheme},{"BTSTORE1 SEED 1 midi",StorageOperation::SeedMidi},
        {"BTSTORE1 LOAD 1 theme",StorageOperation::LoadTheme},{"BTSTORE1 LOAD 1 midi",StorageOperation::LoadMidi}};
    for(const auto& e:entries) if(!std::strcmp(line,e.line)) return e.operation;
    return StorageOperation::Invalid;
}
inline const char* StorageOperationName(StorageOperation op) noexcept {
    constexpr const char* names[]={"invalid","save","restore","seed-theme","seed-midi","load-theme","load-midi"};
    return names[static_cast<unsigned>(op)];
}
}
