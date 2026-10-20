#pragma once
// Explicit host main-loop experiment. No audio-thread I/O and no serial proxy.
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <ctime>
#include "ui/live_playback_view.h"
inline uint64_t OfflineMonotonicNs() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t);
    return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
class OfflineClickMode {
    const char* dir_;
    const char* phase_="preparing";
    const char* error_="";
    uint64_t began_=OfflineMonotonicNs(), last_poll_=0, last_write_=0, first_tick_=0;
    bool started_=false, first_=false, written_=false, exit_=false, release_=false;
    uint32_t hash_=0, tempo_=0;
    std::array<uint32_t,8> gains_{};
    BroTracker::PatternTelemetry position_{};
    bool Save(const char* name,const char* data,size_t bytes) {
        const std::string path=std::string(dir_)+"/"+name, pending=path+".pending";
        FILE* f=std::fopen(pending.c_str(),"wb"); if(!f) return false;
        bool ok=std::fwrite(data,1,bytes,f)==bytes;
        if(std::fclose(f)!=0) ok=false;
        return ok && std::rename(pending.c_str(),path.c_str())==0;
    }
    void Publish(uint64_t now) {
        char text[2048];
        const int n=std::snprintf(text,sizeof(text),
            "{\"schema\":1,\"ns\":%llu,\"phase\":\"%s\",\"error\":\"%s\",\"start_requested\":%s,"
            "\"pattern_hash\":%u,\"tempo_hundredths\":%u,\"tick\":%llu,\"row\":%llu,\"loop\":%llu,"
            "\"valid\":%s,\"running\":%s,\"gains\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",
            (unsigned long long)now,phase_,error_,started_?"true":"false",hash_,tempo_,
            (unsigned long long)position_.tick,(unsigned long long)position_.row,(unsigned long long)position_.loop,
            position_.valid?"true":"false",position_.running?"true":"false",
            gains_[0],gains_[1],gains_[2],gains_[3],gains_[4],gains_[5],gains_[6],gains_[7]);
        if(n<0 || size_t(n)>=sizeof(text) || !Save("status.json",text,size_t(n))) {
            error_="status-write-failed"; phase_="failed"; exit_=true;
        }
        last_write_=now;
    }
    bool Workload(const LivePlaybackView& view) {
        if(!view.device_pattern || view.device_pattern->version!=2 || !view.channel_gains) return false;
        const auto& snapshot=*view.device_pattern; const auto& p=snapshot.pattern;
        auto hash=BroTracker::SnapshotHeaderHash(p.active_rows,p.active_channels,snapshot.tempo_hundredths);
        char text[16384]; size_t used=0;
        auto append=[&](const char* format,auto... args) {
            if(used>=sizeof(text)) return;
            int n=std::snprintf(text+used,sizeof(text)-used,format,args...);
            if(n<0 || size_t(n)>=sizeof(text)-used) used=sizeof(text); else used+=size_t(n);
        };
        append("{\"version\":2,\"rows\":%u,\"channels\":%u,\"tempo_hundredths\":%u,\"cells\":[",
            unsigned(p.active_rows),unsigned(p.active_channels),snapshot.tempo_hundredths);
        unsigned i=0;
        for(unsigned r=0;r<p.active_rows;++r) for(unsigned c=0;c<p.active_channels;++c,++i) {
            const auto cell=p.cells[r][c]; const auto timing=uint8_t(p.note_off_timing[r][c]);
            hash=BroTracker::SnapshotHashByte(BroTracker::SnapshotHashByte(BroTracker::SnapshotHashByte(hash,cell.note),cell.instrument),timing);
            append("%s[%u,%u,%u]",i?",":"",unsigned(cell.note),unsigned(cell.instrument),unsigned(timing));
        }
        for(unsigned c=0;c<8;++c) gains_[c]=view.channel_gains->gains.channels[c];
        append("],\"pattern_hash\":%u,\"gains\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",hash,
            gains_[0],gains_[1],gains_[2],gains_[3],gains_[4],gains_[5],gains_[6],gains_[7]);
        if(used>=sizeof(text) || !Save("workload.json",text,used)) { Fail("workload-write-failed"); return false; }
        hash_=hash; tempo_=snapshot.tempo_hundredths; return true;
    }
public:
    explicit OfflineClickMode(const char* directory):dir_(directory) {}
    bool Enabled() const { return dir_!=nullptr; }
    bool Exit() const { return exit_; }
    bool ReleaseRequested() const { return release_; }
    bool Failed() const { return *error_!=0; }
    void Fail(const char* reason) { error_=reason; phase_="failed"; exit_=true; }
    // At most ten bounded command reads and two <=2 KiB status writes per second,
    // plus immediate transition reports. Workload is written once before START.
    template<class Serial> void Tick(Serial& serial,uint32_t host_ms,uint64_t now=OfflineMonotonicNs()) {
        if(!Enabled() || exit_ || now-last_poll_<100000000) return;
        last_poll_=now;
        if(now-began_>210000000000ULL) Fail("runner-watchdog-timeout");
        const auto previous=phase_;
        const auto view=serial.PlaybackView(host_ms);
        if(view) position_=view->position;
        if(!started_ && serial.Connected() && (serial.Playing() || serial.Paused())) Fail("preexisting-playback");
        if(!written_ && !exit_ && serial.AudioOutputConfirmed() && !std::strcmp(serial.ActiveAudioOutput(),"bridge") && view) {
            written_=Workload(*view);
            if(written_) phase_="prepared";
        }
        char command[32]{};
        { std::ifstream f(std::string(dir_)+"/command.txt"); f.read(command,sizeof(command)-1); }
        if(!std::strcmp(command,"START\n") && !started_ && written_ && !exit_) {
            if(serial.Start()) { started_=true; phase_="starting"; }
        }
        if(started_ && !exit_ && !serial.StartPending() && !serial.TransportPending() && serial.Playing()
            && view && view->fresh && view->position.valid && view->position.running) {
            if(!first_) { first_=true; first_tick_=view->position.tick; }
            else if(view->position.tick>first_tick_) phase_="running";
        }
        if(!std::strcmp(command,"RELEASE\n") && !exit_) {
            if(started_ && !std::strcmp(phase_,"running") && serial.DiagnosticReleaseReady()) { release_=true; exit_=true; phase_="releasing"; }
            else if(!started_ || std::strcmp(phase_,"running")) Fail("premature-release-command");
        }
        if(*command && std::strcmp(command,"START\n") && std::strcmp(command,"RELEASE\n")) Fail("invalid-runner-command");
        if(std::strcmp(phase_,previous) || now-last_write_>=500000000ULL) Publish(now);
    }
    // Caller MUST have destroyed BringUpSerial/transport before this report.
    void SerialReleased(uint64_t now=OfflineMonotonicNs()) {
        if(!Enabled()) return;
        if(!release_ && !Failed()) Fail("premature-btx-exit");
        phase_=Failed()?"failed":"released"; Publish(now);
    }
};
