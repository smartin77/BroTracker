#pragma once
#include "pattern_publication.h"
namespace BroTracker {
// Main-owner metadata only. Baseline stays separate in publication storage.
// One frozen transfer, one accepted pending copy and one applied working copy.
// Synchronize via ordered audio-owner notifications, never read player metadata.
struct PatternPreviewReadback {
    RealtimePattern working, pending;
    std::uint32_t working_id=0, pending_id=0, published_id=0;
    bool Accepted(PatternPublication& transfer, std::uint32_t id, bool success) noexcept {
        if (!transfer.Submitted() || transfer.Identity()!=id) return false;
        if (success) { pending=*transfer.Frozen(); pending_id=id; }
        transfer.Finish(); return true; // Audio no longer retains the staging pointer.
    }
    bool Applied(std::uint32_t id) noexcept {
        if (!id || id!=pending_id) return false;
        working=pending; working_id=id; pending_id=0; return true;
    }
    void Superseded(std::uint32_t id) noexcept { if (id==pending_id) pending_id=0; }
    void Published(const RealtimePattern& baseline, std::uint32_t id) noexcept {
        working=baseline; working_id=pending_id=0; published_id=id;
    }
};
inline bool FormatPreviewDiscovery(const PatternPublication& transfer,char* line,std::size_t capacity) noexcept {
    if (!line || !capacity) return false;
    const int n=std::snprintf(line,capacity,"BTPREVIEW1 NEXT 1 %" PRIu32 "\n",transfer.NextIdentity());
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
inline bool FormatPreviewState(std::uint32_t published,std::uint32_t working,std::uint32_t pending,
    char* line,std::size_t capacity) noexcept {
    if (!line || !capacity) return false;
    const int n=std::snprintf(line,capacity,"BTPREVIEW1 STATE 1 %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",published,working,pending);
    return n>=0 && static_cast<std::size_t>(n)<capacity;
}
}
