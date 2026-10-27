#pragma once

#include <cstdint>
#include <cstdio>
#include "serial_transport.h"
#include "bringup/audio_output_selection.h"
#include "bringup/development_pattern_selection.h"
#include "live_playback_view.h"
#include "local_pattern_editor.h"
#include "bringup/host_pattern_publication.h"
#include "bringup/host_pattern_preview.h"
#include "bringup/host_channel_gains.h"
#include "bringup/host_channel_mute.h"
#include "bringup/host_master_volume.h"
#include "bringup/host_tempo.h"

// Temporary BTTEST1 hardware-test exchange, NOT BroTracker's final protocol.
// Shared command ordering, parser and timeouts; platform transport owns I/O.
class BringUpSerial
{
public:
    explicit BringUpSerial(FILE* log, const char* test_device = nullptr);
    BringUpSerial(FILE* log, std::unique_ptr<SerialTransport> transport);
    ~BringUpSerial();
    BringUpSerial(const BringUpSerial&) = delete;
    BringUpSerial& operator=(const BringUpSerial&) = delete;
    void Tick(std::uint32_t now);
    bool Publish(LocalPatternEditor& editor, std::uint32_t now);
    void SyncEditor(LocalPatternEditor& editor, std::uint32_t now);
    bool PublicationBusy() const { return publish_queued_ || publication_.Blocking(); }
    const char* PublicationStatus() const {
        if(tempo_.Supported() && !tempo_.Synchronized() && !PublicationBusy()) {
            if(tempo_.Phase()==BroTracker::HostTempoPhase::Uncertain) return "Tempo uncertain; reading device state";
            std::snprintf(tempo_status_,sizeof(tempo_status_),"Tempo applied %u.%02u | desired %u.%02u (pending)",
                unsigned(tempo_.State().tempo/100),unsigned(tempo_.State().tempo%100),unsigned(tempo_.Desired()/100),unsigned(tempo_.Desired()%100));
            return tempo_status_;
        }
        if(publish_queued_) return "Publication queued; waiting for preview";
        if(PublicationBusy() || publication_.Phase()==BroTracker::HostPublicationPhase::Rejected ||
            publication_.Phase()==BroTracker::HostPublicationPhase::Mismatch) return publication_.Status();
        return preview_.Supported() ? preview_.Status() : publication_.Status();
    }
    // Shared explicit request API. No editor, UI, dirty state or history mutation.
    bool VolumeAvailable() const {return connected_ && volume_.Available();}
    bool StepVolume(int direction) {return connected_ && volume_.Step(direction);}
    const char* VolumeStatus() const {
        if(!connected_)return "Vol --";
        if(!volume_.Supported())return "Vol -- (unsupported)";
        if(!volume_.Available())return "Vol ? (synchronizing)";
        std::snprintf(volume_status_,sizeof(volume_status_),"Vol %u%%",volume_.Percent());return volume_status_;
    }
    bool TempoAvailable() const { return connected_ && tempo_.Available() && !PublicationBusy() && !TransportPending(); }
    bool StepTempo(int amount) { return TempoAvailable() && tempo_.Step(amount); }
    bool RestoreTempo() {
        const auto* published=pattern_assembler_.Snapshot();
        return TempoAvailable() && published && tempo_.Request(published->tempo_hundredths);
    }
    const BroTracker::HostTempo& Tempo() const { return tempo_; }
    bool ToggleChannelMute(unsigned channel) { return connected_ && mute_.Toggle(channel); }
    bool MuteAvailable() const {return connected_ && mute_.Available();}
    bool RequestChannelGains(const BroTracker::Pcm16ChannelGains& gains) {
        return connected_ && gains_.Request(gains);
    }
    bool ChannelGainsSupported() const { return connected_ && gains_.Supported(); }
    bool ChannelGainsAvailable() const { return connected_ && gains_.Available(); }
    const BroTracker::HostGainState* ChannelGains() const { return connected_ ? gains_.Authoritative() : nullptr; }
    const BroTracker::Pcm16ChannelGains* DesiredChannelGains() const { return gains_.Desired(); }
    BroTracker::HostGainPhase ChannelGainPhase() const { return gains_.Phase(); }
    bool ChannelGainsSynchronized() const { return connected_ && gains_.Synchronized(); }
    const char* ChannelGainStatus() const { return gains_.Status(); }
    // Set once at host startup, before the first Tick. Default sends no override.
    void SetMidiOutputChannel(unsigned value) { midi_channel_=value; }
    // App config resolution always calls this, including missing/empty => None.
    // Standalone protocol clients may omit startup initialization entirely.
    void SetStartupPattern(std::string_view value) { startup_selection_enabled_=true; if(!startup_pattern_.Assign(value))startup_failed_=true; }
    void SetStartupPattern(const BroTracker::DevelopmentPattern& value) { SetStartupPattern(std::string_view(value.stem)); }
    bool StartupPatternReady() const {return !startup_selection_enabled_ || startup_editor_ready_;}
    const char* MidiStatus() const { return midi_failed_?"MIDI rejected/restart required":!midi_supported_?"MIDI unsupported by firmware":midi_confirmed_?(midi_output_status_[0]?midi_output_status_:"MIDI channel acknowledged"):"MIDI awaiting acknowledgment"; }
    void SetAudioOutputPreference(BroTracker::AudioOutputChoice value) { audio_preference_=value; }
    bool AudioOutputConfirmed() const { return audio_confirmed_; }
    // Diagnostic-only read: close only at a complete-line, command-idle boundary.
    bool DiagnosticReleaseReady() const {
        return tx_offset_==tx_size_ && pending_==Command::None && !TransportPending()
            && !PublicationBusy() && !pattern_assembler_.Active();
    }
    const char* ActiveAudioOutput() const { return audio_active_; }
    // Optional host observer of confirmed transport replies; no serial ownership.
    void SetTransportObserver(void (*fn)(void*,const char*,std::uint32_t),void* context) {observer_=fn;observer_context_=context;}
    bool Start();
    bool Stop();
    bool Pause();
    bool Continue();
    bool SupportsPatternTransport() const { return transport_supported_; }
    bool Paused() const;
    bool Playing() const;
    bool TransportPending() const;
    bool Connected() const { return connected_; }
    bool Stopped() const { return stopped_; }
    bool Finished() const;
    bool StartCancelled() const { return start_cancelled_; }
    // Read-only UI state: queued or transmitted START still awaiting a response.
    bool StartPending() const { return start_queued_ || pending_ == Command::Start; }
    const char* Status() const;
    // No live view when disconnected. A connected legacy peer is represented
    // explicitly with telemetry_available=false; controls remain usable.
    std::optional<LivePlaybackView> PlaybackView(std::uint32_t now) const;

private:
    void (*observer_)(void*,const char*,std::uint32_t)=nullptr;
    void* observer_context_=nullptr;
    void Observe(const char* event,std::uint32_t now){if(observer_)observer_(observer_context_,event,now);}
    enum class Command { None, Hello, Status, Start, Stop, Pause, Continue, Audio, Midi, StorageReady, Storage };
    BroTracker::DevelopmentPattern startup_pattern_;
    bool storage_supported_=false,startup_attempted_=false,startup_applied_=false,startup_done_=false,startup_failed_=false;
    bool storage_ready_supported_=false,startup_stop_sent_=false,startup_ready_sent_=false,startup_storage_ready_=false;
    bool startup_context_cleared_=false,startup_editor_ready_=false;
    bool startup_selection_enabled_=false;
    std::uint32_t startup_revision_=0;
    bool QueueTransport(Command command);
    const char* PublicationUnavailable(const LocalPatternEditor& editor) const;
    Command transport_queued_ = Command::None;
    bool transport_supported_ = false;
    void Log(const char* message, const char* detail = "");
    void Disconnect(const char* reason);
    void Send(Command command, std::uint32_t now);
    void OnLine(std::uint32_t now);
    void ServiceSerial(std::uint32_t now);
    bool write_backpressure_=false;
    void ClearPosition();
    BroTracker::PatternSnapshotAssembler pattern_assembler_;
    unsigned snapshot_version_ = 1;
    bool snapshot_supported_ = false, snapshot_requested_ = false, snapshot_failed_ = false;
    std::uint32_t next_snapshot_id_ = 1; // Never reused/wrapped within this host instance.
    LivePlaybackView live_;
    bool position_received_ = false;
    std::uint32_t position_at_ = 0;
    std::uint32_t performance_at_ = 0;
    std::unique_ptr<SerialTransport> transport_;
    bool opened_ = false;
    FILE* log_;
    std::uint32_t diagnostic_now_=0;
    char logged_preview_status_[96]{};
    char logged_publication_status_[192]{},logged_gain_status_[192]{};
    const char* test_device_;
    BroTracker::AudioOutputChoice audio_preference_=BroTracker::AudioOutputChoice::Default;
    unsigned midi_channel_=12;
    char midi_output_status_[96]{};
    unsigned midi_output_state_=UINT32_MAX;
    bool midi_supported_=false,midi_sent_=false,midi_confirmed_=false,midi_failed_=false;
    bool audio_supported_=false, audio_sent_=false, audio_confirmed_=false, audio_failed_=false;
    char audio_active_[16]="unknown";
    bool connected_ = false;
    bool stopped_ = false;
    Command pending_ = Command::None;
    // At most one START followed by one STOP; STOP must never replace START.
    bool start_queued_ = false;
    bool stop_queued_ = false;
    bool start_cancelled_ = false;
    bool start_unconfirmed_ = false;
    bool start_write_attempted_ = false;
    std::uint32_t last_attempt_ = 0;
    std::uint32_t sent_at_ = 0;
    bool attempted_ = false;
    BroTracker::HostPatternPublication publication_;
    BroTracker::HostPatternPreview preview_;
    BroTracker::HostChannelGains gains_;
    mutable char tempo_status_[96]{};
    BroTracker::HostTempo tempo_; bool tx_tempo_=false, tempo_publication_=false;
    BroTracker::HostMasterVolume volume_;bool tx_volume_=false;mutable char volume_status_[48]{};
    BroTracker::HostChannelMute mute_; bool tx_mute_=false;
    bool tx_gains_=false;
    bool publish_queued_=false, tx_preview_=false, editor_seen_=false, preview_session_=false;
    BroTracker::RealtimePattern observed_editor_;
    BroTracker::DevicePatternSnapshot queued_baseline_;
    BroTracker::RealtimePattern queued_publication_;
    bool paused_publication_supported_ = false;
    bool publication_supported_ = false, next_discovery_expected_ = false, next_fresh_ = false;
    std::uint32_t next_publication_id_ = 0;
    bool publication_readback_requested_ = false, next_refresh_ = false;
    bool tx_publication_ = false;
    // Zero reported bytes may still mean the asynchronous worker owns the line.
    bool tx_write_attempted_=false;
    char tx_[BroTracker::kPatternSnapshotLineCapacity]{};
    unsigned int tx_size_ = 0, tx_offset_ = 0;
    char line_[256]{};
    unsigned int used_ = 0;
    bool overflow_ = false;
    char state_[16] = "UNKNOWN";
};
