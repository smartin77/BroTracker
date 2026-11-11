#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "serial_transport.h"
#include "bringup/pattern_directory.h"
#include "bringup/audio_output_selection.h"
#include "bringup/device_facts.h"
#include "bringup/host_midi_note_delay.h"
#include "bringup/development_pattern_selection.h"
#include "live_playback_view.h"
#include "local_pattern_editor.h"
#include "bringup/host_pattern_publication.h"
#include "bringup/host_pattern_preview.h"
#include "bringup/host_channel_gains.h"
#include "bringup/host_channel_mute.h"
#include "bringup/host_master_volume.h"
#include "bringup/host_tempo.h"
#include "midi_health.h"

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
    void InspectInstrument(unsigned number) {
        if(number<1 || number>99 || number==identity_number_)return;
        identity_number_=number;identity_ready_=identity_facts_ready_=false;identity_request_=true;identity_resource_[0]=0;
    }
    std::string InstrumentIdentity() const {
        if(!connected_ || !identity_supported_)return "Unknown (identity unavailable)";
        if(!identity_ready_)return "Unknown (awaiting device)";
        return identity_type_==0?(identity_facts_ready_?"Undefined":"Unbound (legacy binding only)"):identity_type_==1?"Sample":identity_type_==2?"MIDI":identity_type_==3?"Sine Synth":"Unknown";
    }
    std::string InstrumentAvailabilityText() const {
        if(!connected_ || !identity_supported_)return "Unknown (device unavailable)";
        if(!identity_ready_)return "Unknown (awaiting device)";
        if(!identity_facts_ready_)return "Unknown (legacy firmware)";
        switch(identity_availability_) {
        case 0:return "Undefined";case 1:return identity_type_==1?"Prepared":"Ready";
        case 2:return "No sample selected";case 3:return "Resource unavailable";
        case 4:return "Preparation failed";case 5:return "Not prepared";
        case 6:return "SD unavailable";case 7:return "External MIDI device unknown";
        }return "Unknown";
    }
    std::string InstrumentResource() const {
        if(!connected_ || !identity_facts_ready_)return "Unknown";
        return !std::strcmp(identity_resource_,"-")?"None":identity_resource_;
    }
    bool Publish(LocalPatternEditor& editor, std::uint32_t now);
    void SyncEditor(LocalPatternEditor& editor, std::uint32_t now);
    bool PublicationBusy() const { return publish_queued_ || publication_.Blocking(); }
    bool ReconnectRecoveryPending() const { return continuity_pending_; }
    // Internal compatibility helper restores the retained local published baseline, without device writes.
    bool RestoreReconnectDraft(LocalPatternEditor& editor, std::uint32_t now);
    const char* PublicationStatus() const {
        if(continuity_pending_) {
            if(publication_.Blocking() || publication_.Phase()==BroTracker::HostPublicationPhase::Rejected ||
                publication_.Phase()==BroTracker::HostPublicationPhase::Mismatch) return publication_.Status();
            if(snapshot_failed_) return "Reconnect readback failed: local work retained; reconnect to retry";
            if(continuity_auto_) return preview_.Phase()==BroTracker::HostPreviewPhase::Rejected ?
                "Reconnect recovery rejected: host draft retained; STOP available; see preview/log" :
                "Reconnect: recovering retained host edits; waiting for verified working readback; STOP available";
            return continuity_ready_ ? "Reconnect differs: host work retained. Open Load/Save for explicit recovery" :
                "Reconnect unconfirmed: host work retained; waiting for safe stopped readback; STOP available";
        }

        if(file_status_[0]) return file_status_;
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
    bool VolumeAvailable() const {return !UserActionsBlocked() && volume_.Available();}
    bool StepVolume(int direction) {return !UserActionsBlocked() && volume_.Step(direction);}
    const char* VolumeStatus() const {
        if(!connected_)return "Vol --";
        if(!volume_.Supported())return "Vol -- (unsupported)";
        if(!volume_.Available())return "Vol ? (synchronizing)";
        std::snprintf(volume_status_,sizeof(volume_status_),"Vol %u%%",volume_.Percent());return volume_status_;
    }
    bool TempoAvailable() const { return !UserActionsBlocked() && tempo_.Available() && !PublicationBusy() && !TransportPending(); }
    bool StepTempo(int amount) { return TempoAvailable() && tempo_.Step(amount); }
    bool RestoreTempo() {
        const auto* published=pattern_assembler_.Snapshot();
        return TempoAvailable() && published && tempo_.Request(published->tempo_hundredths);
    }
    const BroTracker::HostTempo& Tempo() const { return tempo_; }
    bool ToggleChannelMute(unsigned channel) { return !UserActionsBlocked() && mute_.Toggle(channel); }
    bool MuteAvailable() const {return !UserActionsBlocked() && mute_.Available();}
    bool RequestChannelGains(const BroTracker::Pcm16ChannelGains& gains) {
        return !UserActionsBlocked() && gains_.Request(gains);
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
    void SetStartupPattern(std::string_view value) { startup_configured_=true;startup_pattern_valid_=startup_pattern_.Assign(value); }
    void SetStartupPattern(const BroTracker::DevelopmentPattern& value) { SetStartupPattern(std::string_view(value.stem)); }
    void ConfigureStartupFallback(unsigned rows) {startup_fallback_enabled_=true;startup_blank_rows_=rows;}
    bool RecoverRetainedWork(std::uint32_t now) {LocalPatternEditor unused;return Publish(unused,now);}
    bool RecoveryMenuAvailable()const{return ReconnectRecoveryActionsAvailable() || PublicationReadbackRetryAvailable();}
    bool StartupPatternReady() const {return startup_selection_enabled_ ? (startup_editor_ready_ || load_rejected_) : (!startup_configured_ || initial_sync_ready_);}
    enum class FileAction { None, Save, Load, Test, Session, Pattern, PatternSave };
    enum class PatternLoadState { Idle, Pending, Succeeded, Failed };
    bool RequestPatternLoad(std::string_view name,std::uint32_t now);
    PatternLoadState PatternLoadResult()const {return pattern_load_state_==PatternLoadState::Pending && startup_failed_?PatternLoadState::Failed:pattern_load_state_;}
    bool RequestPatternList(std::uint32_t offset,std::uint32_t now);
    bool PatternListBusy()const{return list_queued_ || list_pending_;}
    bool PatternListReady()const{return list_ready_;}
    const BroTracker::PatternDirectoryPage& PatternFiles()const{return list_page_;}
    const char* PatternListStatus()const{return list_status_;}
    bool RequestFile(FileAction action, std::uint32_t now, bool wait_for_storage=false,const EditCursor* view=nullptr);
    bool Done() const {return connected_ && !std::strcmp(state_,"DONE");}
    bool FilePending() const {return file_action_!=FileAction::None;}
    const char* PatternSource()const{return pattern_source_.c_str();}
    bool PatternSaveAvailable()const{return pattern_save_supported_ && !pattern_source_.empty();}
    bool PatternSaveSucceeded()const{return pattern_save_succeeded_;}
    bool SessionSnapshotAvailable() const {return connected_ && snapshot_metadata_supported_;}
    bool SessionSaveSucceeded() const {return session_save_succeeded_;}
    void BeginExit(std::uint32_t now);
    bool ExitReady() const {return exit_done_;}
    BroTracker::HostMidiHealth MidiHealth(std::uint32_t now) const {
        BroTracker::HostMidiHealth h;h.connected=connected_;h.supported=midi_supported_;
        if(midi_output_state_!=UINT32_MAX) {
            h.age_ms=now-midi_health_at_;h.known=connected_ && h.age_ms<=3000;
            h.configured=midi_output_state_&1;h.fault=midi_output_state_&2;
            h.cleanup=midi_output_state_&4;h.reason=midi_output_state_>>3;
        }
        return h;
    }
    const char* MidiStatus() const {
        if(midi_failed_)return "MIDI rejected/restart required";
        if(connected_ && midi_supported_ && midi_output_state_==UINT32_MAX)
            return midi_confirmed_?"MIDI channel acknowledged; endpoint health unknown":"MIDI awaiting acknowledgment";
        BroTracker::FormatMidiHealth(MidiHealth(diagnostic_now_),MidiRecoveryStopped(),midi_output_status_,sizeof(midi_output_status_));
        return midi_output_status_;
    }
    bool MidiRecoveryStopped() const {return connected_ && !std::strcmp(state_,"IDLE") && !TransportPending();}
    void SetAudioOutputPreference(BroTracker::AudioOutputChoice value) { audio_preference_=value; }
    bool AudioOutputConfirmed() const { return audio_confirmed_; }
    void SetMidiNoteDelay(unsigned ms,bool invalid=false){midi_delay_.Configure(ms,invalid);}
    bool MidiDelayReady()const{return midi_delay_.Ready();}
    const char* MidiDelayStatus()const{return midi_delay_.Status();}
    bool DeviceFactsSupported() const {return device_facts_supported_;}
    const BroTracker::DeviceFacts* DeviceFacts() const {return device_facts_valid_?&device_facts_:nullptr;}
    bool AudioTransportReady() const {
        return !(audio_supported_ || audio_preference_!=BroTracker::AudioOutputChoice::Default) || audio_confirmed_;
    }
    const char* AudioReadinessStatus() const;
    // Diagnostic-only read: close only at a complete-line, command-idle boundary.
    bool DiagnosticReleaseReady() const {
        return tx_offset_==tx_size_ && pending_==Command::None && !TransportPending()
            && !PublicationBusy() && !pattern_assembler_.Active();
    }
    // Last device-reported route, including ERROR active=. Confirmation is separate.
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
    enum class SdAvailability { Unknown, Available, Unavailable };
    SdAvailability SdStatus() const {return connected_ ? sd_status_ : SdAvailability::Unknown;}
    // Unknown/legacy status is never converted into a card failure.
    bool UserActionsBlocked() const {
        return !connected_ || SdStatus()==SdAvailability::Unavailable || !StartupPatternReady() ||
            (!startup_selection_enabled_ && !initial_sync_ready_) || !pattern_assembler_.Snapshot() || continuity_pending_;
    }
    bool StopAvailable() const {return connected_ && SdStatus()!=SdAvailability::Unavailable;}
    // Only re-arm GET for an uncertain publication; never authorize host writes.
    bool PublicationReadbackRetryAvailable() const {
        return StopAvailable() && publication_.Phase()==BroTracker::HostPublicationPhase::Uncertain &&
            snapshot_version_==2 && !pattern_assembler_.Active();
    }
    // Explicit existing recovery operations only, never ordinary editing/transport.
    bool ReconnectRecoveryActionsAvailable() const {
        return StopAvailable() && continuity_pending_ && continuity_frozen_ &&
            pattern_assembler_.Snapshot() && (!preview_.Supported() || preview_.VerifiedWorking());
    }
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
    BroTracker::HostMidiNoteDelay midi_delay_;
    char logged_midi_delay_[256]{};
    bool device_facts_supported_=false,device_facts_valid_=false;
    BroTracker::DeviceFacts device_facts_;
    bool sd_status_supported_=false;
    SdAvailability sd_status_=SdAvailability::Unknown;
    void (*observer_)(void*,const char*,std::uint32_t)=nullptr;
    void* observer_context_=nullptr;
    void Observe(const char* event,std::uint32_t now){if(observer_)observer_(observer_context_,event,now);}
    enum class Command { None, List, Hello, Status, Start, Stop, Pause, Continue, Audio, Midi, StorageReady, Storage, File };
    BroTracker::DevelopmentPattern startup_pattern_;
    bool startup_pattern_valid_=true,load_rejected_=false,startup_configured_=false,initial_sync_ready_=false,draft_deferred_=false;
    BroTracker::DevelopmentPattern file_pattern_;
    PatternLoadState pattern_load_state_=PatternLoadState::Idle;
    BroTracker::PatternDirectoryPage list_page_{},list_staging_{};
    bool list_supported_=false,list_queued_=false,list_pending_=false,list_ready_=false;
    std::uint32_t list_id_=0,list_offset_=0,list_started_=0;
    char list_status_[96]{};
    FileAction file_action_=FileAction::None;
    std::string pattern_source_;
    bool pattern_save_supported_=false,pattern_save_succeeded_=false;
    bool session_save_succeeded_=false,session_storage_ready_=false,session_ready_required_=false;
    char file_status_[160]{};
    bool exit_requested_=false,exit_done_=false;
    std::uint32_t exit_started_=0,file_requested_at_=0;
    bool FileIdle() const;
    const char* LoadLabel() const;
    char load_label_[40]{};
    bool working_storage_supported_=false;
    bool storage_supported_=false,startup_attempted_=false,startup_applied_=false,startup_done_=false,startup_failed_=false;
    bool storage_ready_supported_=false,startup_stop_sent_=false,startup_ready_sent_=false,startup_storage_ready_=false;
    bool startup_context_cleared_=false,startup_editor_ready_=false;
    bool startup_selection_enabled_=false;
    bool startup_fallback_enabled_=false,startup_decided_=false,startup_fallback_=false,boot_status_known_=false,boot_session_valid_=false,boot_fallback_allowed_=false,new_pattern_supported_=false;
    unsigned startup_blank_rows_=32;
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
    void SnapshotFailed(std::uint32_t now);
    bool write_backpressure_=false;
    void ClearPosition();
    BroTracker::PatternSnapshotAssembler pattern_assembler_;
    unsigned snapshot_version_ = 1;
    bool snapshot_supported_ = false, snapshot_requested_ = false, snapshot_failed_ = false;
    unsigned snapshot_retries_=0;
    bool snapshot_retry_pending_=false;
    std::uint32_t snapshot_retry_at_=0;
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
    mutable char midi_output_status_[192]{};
    std::uint32_t midi_health_at_=0;
    unsigned midi_output_state_=UINT32_MAX;
    bool midi_supported_=false,midi_sent_=false,midi_confirmed_=false,midi_failed_=false;
    bool audio_supported_=false, audio_sent_=false, audio_confirmed_=false, audio_failed_=false;
    char audio_active_[16]="unknown";
    const char* audio_failure_reason_="rejected";
    mutable char audio_status_[256]{};
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
    // Host RAM only. Frozen across disconnect, including reset/different dimensions.
    BroTracker::DevicePatternSnapshot continuity_baseline_{};
    BroTracker::RealtimePattern continuity_draft_{};
    std::uint32_t continuity_tempo_=0;
    bool continuity_pending_=false,continuity_ready_=false,continuity_frozen_=false,continuity_auto_=false;

    BroTracker::HostChannelGains gains_;
    mutable char tempo_status_[96]{};
    BroTracker::HostTempo tempo_; bool tx_tempo_=false, tempo_publication_=false;
    BroTracker::HostMasterVolume volume_;bool tx_volume_=false;mutable char volume_status_[48]{};
    bool identity_supported_=false,identity_request_=false,identity_ready_=false,identity_extended_=false,identity_facts_ready_=false;
    unsigned identity_availability_=0;char identity_resource_[64]{};
    unsigned identity_number_=0,identity_type_=0;
    bool snapshot_metadata_supported_=false,restore_view_pending_=false,host_view_seen_=false;
    EditCursor host_view_{},restored_view_{},save_view_{};
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
