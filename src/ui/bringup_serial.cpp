#include "bringup_serial.h"
#include "host_logging.h"
#include <cstring>

BringUpSerial::BringUpSerial(FILE* log, const char* test_device)
    : transport_(MakeSerialTransport()), log_(log), test_device_(test_device)
{
    Log("waiting for Teensy USB CDC 16c0:048a");
}

BringUpSerial::BringUpSerial(FILE* log, std::unique_ptr<SerialTransport> transport)
    : transport_(std::move(transport)), log_(log), test_device_(nullptr) {}
BringUpSerial::~BringUpSerial() = default;

void BringUpSerial::Log(const char* message, const char* detail)
{
    if (!log_) return;
    char text[2048];
    const int n=std::snprintf(text,sizeof(text),"[host_ms=%" PRIu32 "] %s %s",diagnostic_now_,message,detail);
    if(n<0 || static_cast<std::size_t>(n)>=sizeof(text)) return;
    const auto level=BroTracker::ClassifyHostDiagnostic("USB bring-up",std::string_view(message));
    const auto payload_level=BroTracker::ClassifyHostDiagnostic("USB bring-up",std::string_view(detail));
    BroTracker::HostLog(log_, level==BroTracker::HostLogLevel::Basic || payload_level==BroTracker::HostLogLevel::Basic ? BroTracker::HostLogLevel::Basic : BroTracker::HostLogLevel::Debug,"USB bring-up",text);
}

const char* BringUpSerial::LoadLabel() const {
    return load_label_;
}
bool BringUpSerial::FileIdle() const {
    return connected_ && !PublicationBusy() && !TransportPending() &&
        !pattern_assembler_.Active() && (!preview_.Supported() || !preview_.TransferBusy());
}
bool BringUpSerial::RequestFile(FileAction action,std::uint32_t now) {
    diagnostic_now_=now;
    const bool early_test=action==FileAction::Test && !attempted_;
    if((!early_test && (!connected_ || !storage_supported_)) || file_action_!=FileAction::None || (!early_test && !StartupPatternReady()) || continuity_pending_) {
        std::strcpy(file_status_,"File action unavailable: connect/synchronize first");return false;
    }
    if(action==FileAction::Save && draft_deferred_) {
        std::strcpy(file_status_,"Tune Save already pending in device RAM; Pause/Stop to finish");return false;
    }
    if(action!=FileAction::Test && !working_storage_supported_) {
        std::strcpy(file_status_,"Working-session/tune storage unsupported: update Teensy firmware");return false;
    }
    if(action==FileAction::Load || action==FileAction::Test) {
        if((!early_test && !FileIdle()) || Playing() || Paused() || (action==FileAction::Test && !startup_pattern_valid_)) {
            std::strcpy(file_status_,"Load unavailable: STOP and wait for synchronization; check filename");return false;
        }
        load_rejected_=false;startup_selection_enabled_=true;startup_attempted_=startup_applied_=startup_done_=startup_failed_=false;
        startup_stop_sent_=startup_ready_sent_=startup_storage_ready_=false;
        startup_context_cleared_=startup_editor_ready_=false;startup_revision_=0;
        if(action==FileAction::Load) std::strcpy(load_label_,"draft-load");
        else std::snprintf(load_label_,sizeof(load_label_),"load-%s",BroTracker::DevelopmentPatternName(startup_pattern_));
        preview_.Suspend(true);
    }
    file_action_=action;file_requested_at_=now;
    std::strcpy(file_status_,action==FileAction::Save?"Save queued; synchronizing working edits":action==FileAction::Session?"Session save queued":"Load requested; current work retained until validation");
    Log("File action:",file_status_);return true;
}
void BringUpSerial::BeginExit(std::uint32_t now) {
    exit_requested_=true;exit_started_=now;
    if(!RequestFile(FileAction::Session,now)) {
        Log("EXIT: session save unavailable; latest host-only edits may not be persisted");exit_done_=true;
    }
}

void BringUpSerial::Disconnect(const char* reason)
{
    if((startup_attempted_ || startup_ready_sent_ || startup_stop_sent_) && !startup_editor_ready_) startup_failed_=true; // Never replay an uncertain load on reconnect.
    if(editor_seen_ && StartupPatternReady()) {
        if(!continuity_pending_) continuity_frozen_=false;
        continuity_pending_=true; continuity_ready_=false;
    }
    initial_sync_ready_=false;
    if(file_action_!=FileAction::None) {
        std::strcpy(file_status_,"File outcome uncertain after disconnect; no automatic retry");
        file_action_=FileAction::None;
    }
    if(exit_requested_)exit_done_=true;
    storage_supported_=false;working_storage_supported_=false;storage_ready_supported_=false;
    Observe("DISCONNECT",diagnostic_now_);
    Log("disconnected/error:", reason);
    midi_supported_=midi_sent_=midi_confirmed_=midi_failed_=false;
    midi_output_status_[0]=0; midi_output_state_=UINT32_MAX;
    audio_supported_=audio_sent_=audio_confirmed_=audio_failed_=false;
    std::strcpy(audio_active_,"unknown");
    volume_.Disconnect();tx_volume_=false;
    mute_.Disconnect(); tx_mute_=false;
    gains_.Disconnect(); tx_gains_=false; tempo_.Disconnect(); tx_tempo_=false; tempo_publication_=false;
    publication_.Disconnect();
    preview_.Disconnect(); if(continuity_pending_) preview_.Suspend(true); tx_preview_=false;
    if(publish_queued_) { publish_queued_=false; preview_.Suspend(continuity_pending_); }
    paused_publication_supported_=false;
    publication_supported_=next_discovery_expected_=next_fresh_=false;
    next_publication_id_=0; publication_readback_requested_=false; next_refresh_=false; tx_publication_=false;
    if (start_queued_ || (pending_ == Command::Start && !start_write_attempted_))
    {
        start_cancelled_ = true;
        Log("START cancelled before transmission; press again after reconnect");
    }
    else if (pending_ == Command::Start)
    {
        start_unconfirmed_ = true;
        Log("START transmission attempted; playback unconfirmed, not replaying");
    }
    transport_->Close();
    opened_ = false;
    connected_ = false;
    stopped_ = false;
    pending_ = Command::None;
    start_queued_ = stop_queued_ = false;
    tx_size_ = tx_offset_ = used_ = 0;
    tx_write_attempted_=false;
    overflow_ = false;
    live_ = {};
    position_received_ = false;
    pattern_assembler_.Clear();
    snapshot_version_ = 1;
    snapshot_supported_ = snapshot_requested_ = snapshot_failed_ = false;
    transport_supported_ = false; transport_queued_ = Command::None;
    std::strcpy(state_, "UNKNOWN");
}

void BringUpSerial::SyncEditor(LocalPatternEditor& editor, std::uint32_t now)
{
    diagnostic_now_=now;
    if(continuity_pending_) {
        // Freeze the actual editor before its normal disconnected Sync can clear it.
        // This also captures a last edit made immediately before the failed I/O.
        if(!continuity_frozen_ && editor.Ready()) {
            continuity_baseline_=*editor.Baseline(); continuity_draft_=*editor.Draft();
            continuity_frozen_=true;
        }
        editor.SetLocked(true); preview_.Suspend(true);
        const auto* baseline=pattern_assembler_.Snapshot();
        if(baseline) preview_.SetBaseline(*baseline);
        if(tempo_.Available()) preview_.SetWorkingTempo(tempo_.State().tempo);
        const auto* working=preview_.VerifiedWorking();
        if(!preview_.Supported() && baseline) working=&baseline->pattern;
        if(connected_ && baseline && working && continuity_frozen_) {
            if(publication_.TakeConfirmed() && editor.Rebase(*baseline,publication_.FrozenTempo())) {
                if(tempo_.Supported()) tempo_.Refresh();
                preview_.SetBaseline(*baseline); preview_.Published(baseline->pattern);
                continuity_pending_=continuity_ready_=false;
                Log("Explicit reconnect recovery published and verified; no SD save or START");
            } else if(!continuity_ready_) {
                const bool same=BroTracker::SamePattern(continuity_baseline_.pattern,baseline->pattern) &&
                    continuity_baseline_.tempo_hundredths==baseline->tempo_hundredths &&
                    BroTracker::SamePattern(continuity_draft_,*working) &&
                    continuity_tempo_==(tempo_.Available()?tempo_.State().tempo:baseline->tempo_hundredths);
                if(same) {
                    continuity_pending_=false; editor.ResetContext(); editor.Sync(PlaybackView(now));
                    (void)editor.RecoverDraft(*working); preview_.ConfirmMatchingWorking();
                    Log("Reconnect readback matches retained pattern contents; boot/transport continuity not proven");
                } else {
                    continuity_ready_=true;
                    Log("Reconnect continuity not established; local draft and published baseline retained; Shift+F5 explicitly publishes to RAM, Shift+F4 restores local baseline; no SD load/save or START");
                }
            }
        }
        if(continuity_pending_) return;
        preview_.Suspend(false);
    }
    const bool startup_barrier=startup_selection_enabled_ && !StartupPatternReady();
    if(startup_barrier) {
        // An explicit SD selection establishes a new context. Discard pre-load
        // draft/captured edits once, before they can seed preview recovery.
        // Never repeat this on ordinary reconnect after startup synchronized.
        if(startup_applied_ && !startup_context_cleared_) {editor.ResetContext();editor_seen_=false;startup_context_cleared_=true;}
        editor.SetLocked(true);
        preview_.Suspend(true);
        preview_.RetainLocal(false);
    }
    if(editor.Ready()) for(unsigned i=0;i<BroTracker::kSparsePreviewCellCapacity;++i) {
        BroTracker::SparsePreviewCell cell;
        if(editor.TakeCapturedCell(i,cell) && !startup_barrier && preview_session_ && !publish_queued_ && !publication_.Blocking())
            preview_.ObserveCell(cell,now);
    }
    if(editor_seen_ && editor.Ready() && !BroTracker::SamePattern(observed_editor_,*editor.Draft())) {
        const auto& draft=*editor.Draft();
        auto hash=BroTracker::PublicationHeaderHash(draft.active_rows,draft.active_channels);
        for(unsigned r=0;r<draft.active_rows;++r) for(unsigned c=0;c<draft.active_channels;++c) {
            hash=BroTracker::SnapshotHashByte(hash,draft.cells[r][c].note);
            hash=BroTracker::SnapshotHashByte(hash,draft.cells[r][c].instrument);
            hash=BroTracker::SnapshotHashByte(hash,static_cast<std::uint8_t>(draft.note_off_timing[r][c]));
        }
        char detail[96];
        std::snprintf(detail,sizeof(detail),"checksum=%" PRIu32 " row=%u channel=%u",hash,editor.Cursor().row,editor.Cursor().channel);
        Log("local draft changed:",detail);
    }
    // Capture edits before applying an asynchronously completed recovery readback.
    // A transport disconnect may already have cleared capability this frame.
    if(!startup_barrier && preview_session_ && editor_seen_ && editor.Ready() && !publish_queued_ &&
        !publication_.Blocking() && !BroTracker::SamePattern(observed_editor_,*editor.Draft()))
        preview_.Observe(*editor.Draft(),now);
    if(!startup_barrier || (startup_done_ && !startup_failed_)) editor.Sync(PlaybackView(now));
    if(preview_.Supported()) preview_session_=true;
    if(preview_.Supported() && editor.Ready()) {
        preview_.SetBaseline(*editor.Baseline());
        if(tempo_.Available()) preview_.SetWorkingTempo(tempo_.State().tempo);
        if(const auto* recovered=preview_.RecoveredDraft()) {
            if(publish_queued_ || editor.RecoverDraft(*recovered)) preview_.RecoveredInstalled();
        }
        if(!startup_barrier && !preview_.Recovering() && !publish_queued_ && !publication_.Blocking()) preview_.Observe(*editor.Draft(),now);
    }
    if(startup_barrier && startup_done_ && !startup_failed_ && editor.Ready() &&
        (!preview_.Supported() || (preview_.Phase()==BroTracker::HostPreviewPhase::Synchronized && !preview_.RecoveredDraft()))) {
        startup_editor_ready_=true;file_action_=FileAction::None;std::strcpy(file_status_,"Load applied and verified; playback stopped");
        Log("SD startup editor/working-state recovery synchronized");
    }
    if(publication_.TakeConfirmed()) {
        if(tempo_.Supported()) tempo_.Refresh();
        if(const auto* snapshot=pattern_assembler_.Snapshot()) {
            if(editor.Rebase(*snapshot,publication_.FrozenTempo()) && preview_.Supported()) preview_.Published(snapshot->pattern);
        }
    }
    if(publish_queued_ && connected_ && !Playing() && preview_.CanPublish() && pending_==Command::None &&
        !TransportPending() && !pattern_assembler_.Active() && next_fresh_ && next_publication_id_ && tx_offset_==tx_size_) {
        if(publication_.Begin(next_publication_id_,queued_baseline_,queued_publication_,now,tempo_publication_?queued_baseline_.tempo_hundredths:0)) {
            publish_queued_=false; next_publication_id_=0; next_fresh_=false;
            next_refresh_=true; publication_readback_requested_=false;
        }
    }
    if(!startup_selection_enabled_ && connected_ && editor.Ready() && pattern_assembler_.Snapshot() &&
        (!preview_.Supported() || preview_.VerifiedWorking())) initial_sync_ready_=true;
    if(StartupPatternReady() && !publish_queued_ && !publication_.Blocking()) preview_.Suspend(false);
    editor.SetLocked(exit_requested_ || file_action_==FileAction::Save || file_action_==FileAction::Session || !StartupPatternReady() || PublicationBusy());
    publication_.RefreshAvailability(PublicationUnavailable(editor));
    if(std::strcmp(logged_preview_status_,preview_.Status())) {
        std::snprintf(logged_preview_status_,sizeof(logged_preview_status_),"%s",preview_.Status());
        Log("preview status:",logged_preview_status_);
    }
    if(std::strcmp(logged_publication_status_,publication_.Status())) {
        std::snprintf(logged_publication_status_,sizeof(logged_publication_status_),"%s",publication_.Status());
        Log("publication status:",logged_publication_status_);
    }
    if(std::strcmp(logged_gain_status_,gains_.Status())) {
        std::snprintf(logged_gain_status_,sizeof(logged_gain_status_),"%s",gains_.Status());
        Log("gain status:",logged_gain_status_);
    }
    if(editor.Ready() && StartupPatternReady()) preview_.RetainLocal(editor.Dirty());
    editor_seen_=editor.Ready();
    if(editor_seen_) {
        observed_editor_=*editor.Draft();
        if(connected_ && tempo_.Available()) continuity_tempo_=tempo_.State().tempo;
        else if(!continuity_tempo_) continuity_tempo_=editor.Baseline()->tempo_hundredths;
    }
}

const char* BringUpSerial::PublicationUnavailable(const LocalPatternEditor& editor) const {
    if(!StartupPatternReady()) return "Wait for SD startup pattern/readback";
    if(!connected_) return "Connect Teensy to publish";
    if(!publication_supported_) return "Firmware does not support publication";
    if(tempo_.Supported() && !tempo_publication_) return "Tempo-aware publication unsupported";
    if(PublicationBusy()) return "Publication already in progress";
    if(Playing()) return "Pause or stop playback to publish";
    if(Paused() && !paused_publication_supported_) return "Stop playback: paused publication unsupported";
    if(!Paused() && std::strcmp(state_,"IDLE")) return "Playback state unavailable for publication";
    if(TransportPending() || pending_!=Command::None) return "Wait for transport reply";
    if(pattern_assembler_.Active() || !snapshot_requested_) return "Wait for device pattern";
    if(!editor.Baseline() || editor.Baseline()->version!=2 || snapshot_version_!=2) return "Timing-aware device pattern required";
    if(tempo_publication_ && !tempo_.Synchronized()) return "Wait for authoritative tempo";
    if(!editor.Dirty() && (!tempo_publication_ || tempo_.State().tempo==tempo_.State().published)) return "No changes to publish";
    if(!next_fresh_) return "Wait for publication identity refresh";
    if(!next_publication_id_) return "Publication identities exhausted";
    if(tx_offset_!=tx_size_) return "Wait for serial transmission";
    return nullptr;
}

bool BringUpSerial::RestoreReconnectDraft(LocalPatternEditor& editor, std::uint32_t now)
{
    // Preserve Shift+F4's local Restore meaning. Never adopt the boot fixture or write
    // to the device here. Publication remains a separate explicit Shift+F5 action.
    if(!continuity_pending_ || !continuity_frozen_ || !editor.Ready() || PublicationBusy()) return false;
    editor.SetLocked(false);
    const bool restored=editor.Apply(EditorAction::Restore);
    editor.SetLocked(true);
    if(!restored) return false;
    continuity_draft_=*editor.Draft();
    continuity_tempo_=continuity_baseline_.tempo_hundredths;
    publication_=BroTracker::HostPatternPublication{};
    Log("Reconnect Shift+F4 Restore: retained local published baseline restored; device unchanged; Shift+F5 Publish remains explicit");
    diagnostic_now_=now; return true;
}

bool BringUpSerial::Publish(LocalPatternEditor& editor, std::uint32_t now)
{
    // Shift+F5 may resolve an uncertain outcome with GET, never resend publication.
    if(publication_.Phase()==BroTracker::HostPublicationPhase::Uncertain) {
        if(!connected_ || snapshot_version_!=2 || pattern_assembler_.Active()) return false;
        publication_readback_requested_=false; snapshot_requested_=false; return true;
    }
    if(continuity_pending_) {
        // Shift+F5 is explicit RAM publication, not an automatic recovery or SD save.
        // Freeze the retained candidate directly: preview dimensions may differ
        // from the reboot fixture, so preview-before-publication is inappropriate.
        const auto* baseline=pattern_assembler_.Snapshot();
        if(!continuity_ready_ || !connected_ || !baseline || (preview_.Supported() && !preview_.VerifiedWorking()) ||
            !publication_supported_ || PublicationBusy() || Playing() || TransportPending() ||
            pending_!=Command::None || !next_fresh_ || !next_publication_id_ ||
            tx_offset_!=tx_size_ || pattern_assembler_.Active() ||
            (Paused() && !paused_publication_supported_) ||
            (tempo_.Supported() && (!tempo_publication_ || !tempo_.Synchronized()))) return false;
        // A fresh NEXT discovery supplies a non-reused ID. This explicit
        // choice replaces the old transaction coordinator, never replays it.
        publication_=BroTracker::HostPatternPublication{};
        if(!publication_.Begin(next_publication_id_,*baseline,continuity_draft_,now,
                tempo_publication_?continuity_tempo_:0)) return false;
        next_publication_id_=0;next_fresh_=false;next_refresh_=true;publication_readback_requested_=false;
        Log("Explicit reconnect recovery requested: retained draft publication to RAM; no SD save or START");
        return true;
    }

    if(tempo_.Supported() && !tempo_publication_) {publication_.Unavailable("Tempo-aware publication unsupported");return false;}
    if(tempo_publication_ && !tempo_.Synchronized()) return false;
    if(preview_.Supported() && editor.Ready() && editor.Dirty() && connected_ && publication_supported_ &&
        !Playing() && (Stopped() || !std::strcmp(state_,"IDLE") || (Paused() && paused_publication_supported_)) &&
        !PublicationBusy() && !TransportPending() && editor.Baseline()->version==2 && next_fresh_ && next_publication_id_) {
        queued_baseline_=*editor.Baseline(); if(tempo_publication_) queued_baseline_.tempo_hundredths=tempo_.State().tempo; queued_publication_=*editor.Draft(); publish_queued_=true;
        preview_.Observe(queued_publication_,now); preview_.Suspend(true); editor.SetLocked(true); next_refresh_=true;
        return true;
    }
    if(const auto* reason=PublicationUnavailable(editor)) { publication_.Unavailable(reason); return false; }
    const auto* baseline=editor.Baseline();
    if(!publication_.Begin(next_publication_id_,*baseline,*editor.Draft(),now,tempo_publication_?tempo_.State().tempo:0)) return false;
    next_publication_id_=0; next_fresh_=false; next_discovery_expected_=false;
    next_refresh_=true; publication_readback_requested_=false;
    editor.SetLocked(true); Log("publication frozen; explicit request"); return true;
}

bool BringUpSerial::Start()
{
    if(file_action_!=FileAction::None || !StartupPatternReady() || continuity_pending_) return false;
    if (!connected_ || TransportPending() || PublicationBusy()) return false;
    if((audio_supported_ || audio_preference_!=BroTracker::AudioOutputChoice::Default) && !audio_confirmed_) return false;
    if(midi_supported_ && !midi_confirmed_) return false;
    start_queued_ = true;
    ClearPosition();
    start_cancelled_ = start_unconfirmed_ = false;
    stopped_ = false;
    Log("START requested");
    return true;
}

bool BringUpSerial::Stop()
{
    tempo_.StopRequested();
    if (!connected_) { Log("STOP unavailable: device disconnected"); return false; }
    publication_.StopRequested();
    if(tx_publication_ && tx_offset_==0 && !tx_write_attempted_ && publication_.Phase()==BroTracker::HostPublicationPhase::Rejected) {
        tx_size_=0; tx_publication_=false; // Whole unattempted line may be dropped, never a partial line.
    }
    stop_queued_ = true;
    ClearPosition();
    stopped_ = false;
    Log("STOP requested");
    return true;
}

bool BringUpSerial::TransportPending() const
{ return start_queued_ || stop_queued_ || transport_queued_ != Command::None ||
    pending_ == Command::Start || pending_ == Command::Stop ||
    pending_ == Command::Pause || pending_ == Command::Continue; }
bool BringUpSerial::Paused() const { return connected_ && !std::strcmp(state_, "PAUSED"); }
bool BringUpSerial::Playing() const { return connected_ && !std::strcmp(state_, "PLAYING"); }
bool BringUpSerial::QueueTransport(Command command) {
    if (!connected_ || !transport_supported_ || TransportPending() || PublicationBusy()) return false;
    if ((command == Command::Pause && !Playing()) || (command == Command::Continue && !Paused())) return false;
    if(command==Command::Continue && (continuity_pending_ || file_action_!=FileAction::None || !StartupPatternReady())) return false;
    if(command==Command::Continue && midi_supported_ && !midi_confirmed_) return false;
    transport_queued_ = command; stopped_ = false; return true;
}
bool BringUpSerial::Pause() { return QueueTransport(Command::Pause); }
bool BringUpSerial::Continue() { return QueueTransport(Command::Continue); }

bool BringUpSerial::Finished() const
{
    return connected_ && !TransportPending() &&
        (std::strcmp(state_, "DONE") == 0 || std::strcmp(state_, "IDLE") == 0 ||
         std::strcmp(state_, "ERROR") == 0);
}

const char* BringUpSerial::Status() const
{
    if (!connected_)
    {
        if (transport_->AccessDenied()) return "Teensy serial access denied - see log";
        if (start_cancelled_) return "Waiting - START cancelled (not sent)";
        if (start_unconfirmed_) return "Waiting - START result unknown";
        return "Waiting for Teensy USB";
    }
    if(midi_failed_) return "MIDI setup failed - see log; restart Teensy if required";
    if(startup_failed_) return "SD startup pattern failed/uncertain - see log; no automatic retry";
    if(!StartupPatternReady()) return "Loading SD development pattern (playback stopped)";
    if(midi_supported_ && !midi_confirmed_) return "Waiting for MIDI channel acknowledgment";
    if(audio_failed_) return "Audio selection failed - see log; restart Teensy to change route";
    if((audio_supported_ || audio_preference_!=BroTracker::AudioOutputChoice::Default) && !audio_confirmed_) return "Waiting for Teensy audio selection";
    if (stop_queued_ || pending_ == Command::Stop) return "Connected - stopping";
    if (start_queued_ || pending_ == Command::Start) return "Connected - starting";
    if (transport_queued_ == Command::Pause || pending_ == Command::Pause) return "Connected - pausing";
    if (transport_queued_ == Command::Continue || pending_ == Command::Continue) return "Connected - continuing";
    if (Paused()) return "Connected - PAUSED";
    if (std::strcmp(state_, "PLAYING") == 0) return "Connected - playing";
    if (std::strcmp(state_, "DONE") == 0) return "Connected - completed";
    if (std::strcmp(state_, "ERROR") == 0) return "Connected - playback ERROR";
    return midi_supported_ ? "Connected - idle" : "Connected - idle (MIDI unsupported)";
}

void BringUpSerial::Send(Command command, std::uint32_t now)
{
    const char* verb = command == Command::Hello ? "HELLO" :
                       command == Command::Status ? "STATUS" :
                       command == Command::Start ? "START" :
                       command == Command::Pause ? "PAUSE" : command == Command::Continue ? "CONTINUE" : command == Command::Audio ? "AUDIO" : command == Command::Midi ? "MIDI" : command == Command::Storage ? "SD LOAD" : command==Command::File ? "SD SAVE" : command==Command::StorageReady ? "SD READY" : "STOP";
    tx_size_ = std::snprintf(tx_, sizeof(tx_), "BTTEST1 %s\n", verb);
    if(command==Command::Midi) {
        tx_size_=std::snprintf(tx_,sizeof(tx_),"BTMIDI1 SET 1 %u\n",midi_channel_); midi_sent_=true;
    }
    if(command==Command::Audio) {
        tx_size_=std::snprintf(tx_,sizeof(tx_),"BTAUDIO1 SET 1 %s\n",BroTracker::AudioOutputName(audio_preference_));
        audio_sent_=true;
    }
    if(command==Command::StorageReady) {
        tx_size_=std::snprintf(tx_,sizeof(tx_),"BTSTORE1 READY 1\n");startup_ready_sent_=true;
    }
    if(command==Command::Storage) {
        tx_size_=std::snprintf(tx_,sizeof(tx_),file_action_==FileAction::Load?"BTSTORE1 DRAFTLOAD 1\n":"BTSTORE1 LOAD 1 %s\n",BroTracker::DevelopmentPatternName(startup_pattern_));
        startup_attempted_=true;
    }
    if(command==Command::File) {
        tx_size_=std::snprintf(tx_,sizeof(tx_),file_action_==FileAction::Save?"BTSTORE1 DRAFTSAVE 1\n":"BTSTORE1 SAVE 1\n");
    }
    tx_offset_ = 0; tx_publication_=false;
    if(command==Command::Hello || command==Command::Status) {
        next_discovery_expected_=true; next_refresh_=false; preview_.DiscoveryRequested();
    }
    pending_ = command;
    if (command == Command::Start) start_write_attempted_ = false;
    sent_at_ = now;
    Log("queued for TX:", verb);
}

void BringUpSerial::ClearPosition()
{
    position_received_ = false;
    live_.fresh = false;
    live_.position.running = live_.position.valid = live_.position.paused = false;
    live_.position.tick = live_.position.row = live_.position.loop = 0;
}

std::optional<LivePlaybackView> BringUpSerial::PlaybackView(std::uint32_t now) const
{
    if(continuity_pending_ && continuity_frozen_) {
        LivePlaybackView retained;
        retained.device_pattern=continuity_baseline_;
        retained.telemetry_available=true;
        retained.position.rows=continuity_draft_.active_rows;
        retained.position.channels=continuity_draft_.active_channels;
        retained.tempo_hundredths=continuity_tempo_;
        return retained; // No live highlight or implied device playback continuity.
    }
    if (!connected_) return std::nullopt;
    auto view = live_;
    if(now-performance_at_>=kCallbackPerformanceStaleMs) view.callback_performance.reset();
    if (const auto* snapshot = pattern_assembler_.Snapshot()) view.device_pattern = *snapshot;
    view.preview_supported=preview_.Supported();
    if(tempo_.Supported() && tempo_.Phase()!=BroTracker::HostTempoPhase::Unavailable) {
        view.tempo_hundredths=tempo_.State().tempo; view.desired_tempo=tempo_.Desired();
        view.tempo_pending=!tempo_.Synchronized();
    }
    if(mute_.Known()) view.mute_mask=mute_.Mask();
    view.mute_status=mute_.Status();
    view.gains_supported=gains_.Supported();
    view.gain_status=gains_.Status();
    if(const auto* applied=gains_.Authoritative()) view.channel_gains=*applied;
    if(const auto* desired=gains_.Desired()) view.desired_gains=*desired;
    view.pattern_loading = pattern_assembler_.Active();
    view.pattern_failed = snapshot_failed_;
    view.fresh = position_received_ && now - position_at_ < kLivePositionStaleMs;
    if (!view.fresh)
    {
        view.position.valid = false;
        view.position.tick = view.position.row = view.position.loop = 0;
    }
    return view;
}

void BringUpSerial::OnLine(std::uint32_t now)
{
    Log("RX:", line_);
    const bool draft_result=!std::strncmp(line_,"BTSTORE1 RESULT 1 draft-save ",sizeof("BTSTORE1 RESULT 1 draft-save ")-1);
    const bool session_result=!std::strncmp(line_,"BTSTORE1 RESULT 1 save ",sizeof("BTSTORE1 RESULT 1 save ")-1);
    const bool command_reply=pending_==Command::File &&
        ((draft_result && file_action_==FileAction::Save) || (session_result && file_action_==FileAction::Session));
    if(command_reply || (draft_result && draft_deferred_)) {
        const auto* result=line_+(draft_result?sizeof("BTSTORE1 RESULT 1 draft-save ")-1:sizeof("BTSTORE1 RESULT 1 save ")-1);
        std::snprintf(file_status_,sizeof(file_status_),"%s: %.120s",draft_result?"Tune Save":"Session save",result);
        if(draft_result)draft_deferred_=!std::strncmp(result,"deferred-playing ",17);
        if(draft_result && draft_deferred_)std::strcpy(file_status_,"Tune Save pending in device RAM; will write after Pause/Stop (not yet saved)");
        Log("File result:",file_status_);
        if(command_reply) {
            pending_=Command::None;file_action_=FileAction::None;
            if(exit_requested_)exit_done_=true;
        }
        return;
    }
    if(!std::strncmp(line_,"BTSTORE1 RESULT 1 session-",25)) {
        Log("Session persistence:",line_);
        if(!std::strstr(line_," ok ") && !std::strstr(line_," missing "))
            std::snprintf(file_status_,sizeof(file_status_),"Session recovery/storage: %.115s",line_+18);
        return;
    }
    if(!std::strcmp(line_,"BTSTORE1 SESSIONCAP 2")) {if(connected_)working_storage_supported_=true;return;}
    if(!std::strcmp(line_,"BTSTORE1 READYCAP 1")) {if(connected_)storage_ready_supported_=true;return;}
    if(pending_==Command::StorageReady && !std::strcmp(line_,"BTSTORE1 READY 1")) {
        startup_storage_ready_=true;pending_=Command::None;return;
    }
    if(pending_==Command::StorageReady && !std::strncmp(line_,"BTSTORE1 NOTREADY 1 ",19)) {
        startup_failed_=true;file_action_=FileAction::None;pending_=Command::None;Log("SD startup readiness ERROR:",line_);return;
    }
    if(!std::strcmp(line_,"BTSTORE1 CAP 1")) {if(connected_)storage_supported_=true;return;}
    if(pending_==Command::Storage && !std::strncmp(line_,"BTSTORE1",8)) {
        char prefix[80];std::uint32_t id=0;
        std::snprintf(prefix,sizeof(prefix),"BTSTORE1 SUBMITTED 1 %s ",LoadLabel());
        if(BroTracker::SnapshotFields(line_,used_,prefix,&id,1)) {startup_revision_=id;return;}
        std::snprintf(prefix,sizeof(prefix),"BTSTORE1 RESULT 1 %s ok ",LoadLabel());
        const bool ok=BroTracker::SnapshotFields(line_,used_,prefix,&id,1) && startup_revision_ && id==startup_revision_;
        pending_=Command::None;
        if(!ok) {startup_failed_=true;file_action_=FileAction::None;
            load_rejected_=!startup_revision_ && (std::strstr(line_," missing ") || std::strstr(line_," invalid ") ||
                std::strstr(line_," unsupported ") || std::strstr(line_," busy-or-not-ready ") || std::strstr(line_," sd-unavailable "));

            std::snprintf(file_status_,sizeof(file_status_),"Load failed/uncertain: %.110s",line_);Log("File load ERROR:",line_);return;}
        preview_.FileLoaded();
        preview_session_=false;pattern_assembler_.Clear();snapshot_failed_=false;editor_seen_=false;
        tempo_.Refresh();
        startup_applied_=true;snapshot_requested_=false;next_refresh_=true;
        Log("SD startup pattern applied; awaiting v2 baseline readback:",line_);return;
    }
    if(!std::strncmp(line_,"BTMIDI1 OUTPUT 1 ",17)) {
        std::uint32_t f[4]{};
        if(connected_ && BroTracker::SnapshotFields(line_,used_,"BTMIDI1 OUTPUT 1 ",f,4) &&
           f[0]<=1 && f[1]<=1 && f[2]<=1 && f[3]<=8) {
            const unsigned state=f[0] | (f[1]<<1) | (f[2]<<2) | (f[3]<<3);
            if(state!=midi_output_state_) {
                midi_output_state_=state;
                static constexpr const char* reasons[]={"none","unavailable","deadline","overflow",
                    "DMA error","invalid event","USB lifecycle","timer","completion stall"};
                if(f[1]) std::snprintf(midi_output_status_,sizeof(midi_output_status_),
                    "MIDI fault: %s; %s; %s",reasons[f[3]],f[0]?"STOP/START retry":"USB unavailable",
                    f[2]?"cleanup uncertain":"external delivery unknown");
                else std::snprintf(midi_output_status_,sizeof(midi_output_status_),"%s; %s",
                    f[0]?"MIDI USB configured":"MIDI USB unavailable",
                    f[2]?"cleanup uncertain":"external delivery unknown");
                Log("MIDI output:",line_);
            }
        }
        return;
    }
    if(!std::strcmp(line_,"BTMIDI1 CAP 1")) { if(connected_) midi_supported_=true; return; }
    if(pending_==Command::Midi && !std::strncmp(line_,"BTMIDI1 SELECTED 1 ",18)) {
        std::uint32_t channel=0;
        midi_confirmed_=BroTracker::SnapshotFields(line_,used_,"BTMIDI1 SELECTED 1 ",&channel,1) && channel==midi_channel_;
        midi_failed_=!midi_confirmed_; pending_=Command::None;
        if(midi_failed_) start_queued_=false;
        Log(MidiStatus()); return;
    }
    if(pending_==Command::Midi && (!std::strncmp(line_,"BTMIDI1 ERROR ",14) || !std::strncmp(line_,"BTTEST1 ERROR ",14))) {
        midi_failed_=true; start_queued_=false; pending_=Command::None; Log("MIDI setup ERROR:",line_); return;
    }
    if(!std::strcmp(line_,"BTAUDIO1 CAP 1")) { if(connected_) audio_supported_=true; return; }
    if(pending_==Command::Audio && !std::strncmp(line_,"BTAUDIO1 SELECTED 1 ",20)) {
        const char* route=line_+20;
        const bool known=!std::strcmp(route,"bridge") || !std::strcmp(route,"pt8211") || !std::strcmp(route,"audio_shield");
        const bool matches=known && (audio_preference_==BroTracker::AudioOutputChoice::Default ||
            !std::strcmp(route,BroTracker::AudioOutputName(audio_preference_)));
        audio_confirmed_=matches; audio_failed_=!matches;
        if(!matches) start_queued_=false;
        if(known) std::snprintf(audio_active_,sizeof(audio_active_),"%s",route);
        pending_=Command::None;
        Log(matches ? "audio selection confirmed:" : "audio selection ERROR mismatched acknowledgment:",line_);
        return;
    }
    if(pending_==Command::Audio && (!std::strncmp(line_,"BTAUDIO1 ERROR ",15) || !std::strncmp(line_,"BTTEST1 ERROR ",14))) {
        audio_failed_=true;start_queued_=false;pending_=Command::None;Log("audio selection ERROR:",line_);return;
    }
    if (!std::strncmp(line_,"BTPATTERN1 PERF ",sizeof("BTPATTERN1 PERF ")-1)) {
        BroTracker::PatternPerformance report;
        if(connected_ && BroTracker::ParsePatternPerformance(line_,used_,report)) {
            live_.callback_performance=report; performance_at_=now;
        }
    }
    else if(!std::strncmp(line_,"BTVOL1",6)) {if(connected_)(void)volume_.Response(line_,used_);}
    else if (!std::strncmp(line_,"BTMUTE1",7)) { if(connected_) (void)mute_.Response(line_,used_); }
    else if (!std::strncmp(line_,"BTGAIN1",7)) {
        if(connected_) (void)gains_.Response(line_,used_,now);
    }
    else if (!std::strncmp(line_,"BTPREVIEW1",10)) {
        if(connected_) (void)preview_.Response(line_,used_,now);
    }
    else if(!std::strncmp(line_,"BTTEMPO1",8)) { if(connected_) (void)tempo_.Response(line_,used_); }
    else if(!std::strcmp(line_,"BTPUBLISH1 TEMPOCAP 2")) {if(connected_) tempo_publication_=true;}
    else if (!std::strcmp(line_,BroTracker::kPausedPatternPublicationCapability))
    { if(connected_) paused_publication_supported_=true; }
    else if (!std::strcmp(line_,BroTracker::kPatternPublicationCapability))
    { if(connected_) publication_supported_=true; }
    else if (!std::strncmp(line_,"BTPUBLISH1 NEXT",14)) {
        std::uint32_t values[2]{};
        if(connected_ && next_discovery_expected_ &&
            BroTracker::SnapshotFields(line_,used_,"BTPUBLISH1 NEXT ",values,2) && values[0]==1) {
            next_publication_id_=values[1];
            if(next_publication_id_ && next_publication_id_<=publication_.LastIdentity())
                next_publication_id_=publication_.LastIdentity()==UINT32_MAX ? 0 : publication_.LastIdentity()+1;
            next_fresh_=true; next_discovery_expected_=false;
        }
    }
    else if (!std::strncmp(line_,"BTPUBLISH1",10)) {
        const auto before=publication_.Phase();
        if(connected_ && publication_.Response(line_,used_,now)) {
            if(publication_.NeedsReadback() && before!=publication_.Phase()) {
                publication_readback_requested_=false; snapshot_requested_=false;
            }
        }
    }
    else if (!std::strcmp(line_, "BTPATTERN1 TRANSPORTCAP 1"))
    { if (connected_) transport_supported_ = true; }
    else if (!std::strcmp(line_, BroTracker::kPatternSnapshotCapability) || !std::strcmp(line_, "BTPATTERN1 SNAPCAP 2"))
    {
        if (connected_) { snapshot_supported_ = true;
            if (!std::strcmp(line_, "BTPATTERN1 SNAPCAP 2")) snapshot_version_ = 2; }
    }
    else if (!std::strncmp(line_, "BTPATTERN1 BEGIN", 15) ||
        !std::strncmp(line_, "BTPATTERN1 CELL", 14) ||
        !std::strncmp(line_, "BTPATTERN1 END", 13) ||
        !std::strncmp(line_, "BTPATTERN1 SNAPERR", 17))
    {
        const auto result = pattern_assembler_.Accept(line_, used_, now);
        if (result == BroTracker::SnapshotAssemblyStatus::Rejected ||
            result == BroTracker::SnapshotAssemblyStatus::Timeout) { snapshot_failed_ = true; publication_.ReadbackFailed(); if(startup_applied_ && !startup_done_)startup_failed_=true; }
        if(result==BroTracker::SnapshotAssemblyStatus::Complete && publication_.NeedsReadback())
            (void)publication_.Verify(*pattern_assembler_.Snapshot());
        // Only the original startup transaction's awaited baseline completes startup.
        // Later snapshots (including reconnect recovery) do not establish a file load.
        if(result==BroTracker::SnapshotAssemblyStatus::Complete && startup_applied_ && !startup_done_ && !startup_failed_) {
            startup_done_=true;Log("SD startup pattern loaded and baseline readback verified:",BroTracker::DevelopmentPatternName(startup_pattern_));
        }
    }
    else if (std::strncmp(line_, "BTPATTERN1 POS", 14) == 0)
    {
        BroTracker::PatternTelemetry value;
        // Telemetry cannot acknowledge a command or revive an error. Ignore
        // positions in flight during START/restart/STOP, until their applied ACK.
        if (connected_ && !TransportPending() &&
            std::strcmp(state_, "ERROR") && std::strcmp(state_, "DONE") &&
            BroTracker::ParsePatternTelemetry(line_, used_, value) &&
            value.running == Playing() && value.paused == Paused())
        {
            live_.telemetry_available = live_.fresh = true;
            live_.position = value;
            position_received_ = true;
            position_at_ = now;
        }
    }
    else if (std::strncmp(line_, "BTTEST1 STATE ", 14) == 0)
    {
        const char* value = line_ + 14;
        if (std::strcmp(value, "PLAYING") && std::strcmp(value, "DONE") &&
            std::strcmp(value, "IDLE") && std::strcmp(value, "ERROR") && std::strcmp(value, "PAUSED")) return;
        // A superseded STATUS reply cannot acknowledge or override transport.
        if ((transport_queued_ != Command::None || pending_ == Command::Pause ||
             pending_ == Command::Continue) && std::strcmp(value, "ERROR")) return;
        if(std::strcmp(state_,value)) Observe(!std::strcmp(value,"PLAYING")?"RECONNECT":!std::strcmp(value,"PAUSED")?"PAUSE":!std::strcmp(value,"ERROR")?"ERROR":"STOP",now);
        std::snprintf(state_, sizeof(state_), "%s", value);
        if (std::strcmp(value, "PLAYING") && std::strcmp(value, "PAUSED")) ClearPosition();
        if (!std::strcmp(value, "ERROR")) transport_queued_ = Command::None;
        if (pending_ == Command::Hello || pending_ == Command::Status)
        {
            if (!connected_) Log("connected: BTTEST1 handshake accepted");
            connected_ = true;
            pending_ = Command::None;
        }
    }
    else if (std::strcmp(line_, "BTTEST1 STARTED") == 0)
    {
        std::strcpy(state_, "PLAYING");
        Observe("START",now);
        if (pending_ == Command::Start) pending_ = Command::None;
    }
    else if (std::strcmp(line_, "BTTEST1 PAUSED") == 0 || std::strcmp(line_, "BTTEST1 CONTINUED") == 0)
    {
        const auto expected = !std::strcmp(line_, "BTTEST1 PAUSED") ? Command::Pause : Command::Continue;
        if (pending_ != expected) return;
        std::strcpy(state_, expected == Command::Pause ? "PAUSED" : "PLAYING");
        Observe(expected==Command::Pause?"PAUSE":"CONTINUE",now);
        pending_ = Command::None;
        // Do not relabel pre-command telemetry as the applied position. Firmware
        // follows this ACK with a coherent fresh snapshot; wait for that barrier.
        position_received_ = false;
    }
    else if (std::strcmp(line_, "BTTEST1 STOPPED") == 0)
    {
        ClearPosition();
        std::strcpy(state_, "IDLE");
        Observe("STOP",now);
        if (pending_ == Command::Stop)
        {
            stopped_ = true;
            pending_ = Command::None;
        }
    }
    else if (std::strcmp(line_, "BTTEST1 DONE") == 0)
    {
        ClearPosition();
        std::strcpy(state_, "DONE");
        Observe("STOP",now);
    }
    else if (std::strncmp(line_, "BTTEST1 ERROR ", 14) == 0)
    {
        std::strcpy(state_, "ERROR");
        Observe("ERROR",now);
        ClearPosition();
        transport_queued_ = Command::None;
        if (pending_ == Command::Start || pending_ == Command::Pause || pending_ == Command::Continue) pending_ = Command::None;
    }
}

void BringUpSerial::Tick(std::uint32_t now)
{
    diagnostic_now_=now;
    // Fixed burst: ready transfers do not pay one UI/render interval per cell.
    // Each pass rechecks transport priority and retains partial-line ownership.
    // No waiting for I/O completion; asynchronous/backpressured writes stay pending.
    for (unsigned pass = 0; pass < 64; ++pass) {
        write_backpressure_=false;
        ServiceSerial(now);
        if (!opened_ || !connected_ || write_backpressure_) break;
    }
}

void BringUpSerial::ServiceSerial(std::uint32_t now)
{
    tempo_.Tick(now);
    mute_.Tick(now);
    volume_.Tick(now);
    gains_.Tick(now);
    publication_.Tick(now);
    preview_.Tick(now);
    if (pattern_assembler_.Tick(now) == BroTracker::SnapshotAssemblyStatus::Timeout) {
        snapshot_failed_ = true; publication_.ReadbackFailed();
        if(startup_applied_ && !startup_done_)startup_failed_=true;
    }
    if(publication_.NeedsReadback() && !publication_readback_requested_ && connected_ && snapshot_version_==2)
        snapshot_requested_=false;
    if (!opened_)
    {
        if (attempted_ && now - last_attempt_ < 1000) return;
        attempted_ = true;
        last_attempt_ = now;
        if (!transport_->Open(log_, test_device_)) return;
        opened_ = true;
        Send(Command::Hello, now);
    }
    if (!transport_->Healthy()) { Disconnect(transport_->Error()); return; }
    if(tx_publication_ && tx_offset_==0 && !tx_write_attempted_ && publication_.Phase()!=BroTracker::HostPublicationPhase::Sending) {
        tx_size_=0; tx_publication_=false;
    }
    if(tx_preview_ && tx_offset_==0 && !tx_write_attempted_ && (!preview_.Sending() || TransportPending())) { tx_size_=0; tx_preview_=false; }
    if(tx_tempo_ && tx_offset_==0 && !tx_write_attempted_ && TransportPending()) { tx_size_=0; tx_tempo_=false; }
    if(tx_mute_ && tx_offset_==0 && !tx_write_attempted_ && TransportPending()) { tx_size_=0; tx_mute_=false; }
    if(tx_gains_ && tx_offset_==0 && !tx_write_attempted_ && TransportPending()) { tx_size_=0; tx_gains_=false; }
    if (tx_offset_ < tx_size_)
    {
        // An asynchronous transport may attempt transmission before reporting
        // completion. A later disconnect must conservatively report UNKNOWN.
        if (pending_ == Command::Start) start_write_attempted_ = true;
        if(tx_publication_) publication_.BeforeWrite();
        if(tx_preview_) preview_.BeforeWrite();
        if(tx_mute_) mute_.BeforeWrite(now);
        if(tx_volume_)volume_.BeforeWrite(now);
        if(tx_gains_) gains_.BeforeWrite(now);
        if(tx_tempo_) tempo_.BeforeWrite(now);
        tx_write_attempted_=true;
        const auto count = transport_->Write(tx_ + tx_offset_, tx_size_ - tx_offset_);
        write_backpressure_=count >= 0 && static_cast<unsigned>(count) < tx_size_ - tx_offset_;
        if (count > 0)
        {
            tx_offset_ += count;
            if (tx_offset_ == tx_size_) {
                tx_write_attempted_=false;
                Log("TX complete:", tx_);
                if(tx_publication_) { publication_.LineComplete(); tx_publication_=false; }
                if(tx_preview_) { preview_.LineComplete(); tx_preview_=false; }
                if(tx_mute_) {mute_.LineComplete();tx_mute_=false;}
                if(tx_volume_){volume_.LineComplete();tx_volume_=false;}
                if(tx_gains_) { gains_.LineComplete(); tx_gains_=false; }
                if(tx_tempo_) { tempo_.LineComplete(); tx_tempo_=false; }
            }
        }
        else if (count < 0)
        { Disconnect(transport_->Error()); return; }
    }
    // Bound receive work per SDL frame, including verbose firmware diagnostics.
    for (unsigned int budget = 0; budget < 1024; ++budget)
    {
        char c;
        const auto count = transport_->Read(&c, 1);
        if (count < 0)
        { Disconnect(transport_->Error()); return; }
        if (count <= 0) break;
        if (c == '\n')
        {
            // Permit CRLF, but preserve embedded CR for strict field rejection.
            if (used_ && line_[used_ - 1] == '\r') --used_;
            line_[used_] = '\0';
            if (overflow_)
            {
                Log("RX error: overlong line discarded");
                // Conservatively cancel staging, but never touch applied transport,
                // position or a previously committed snapshot.
                if(std::strncmp(line_,"BTGAIN1",7)) {
                    if(preview_.Supported()) preview_.ReadFailure();
                    if (pattern_assembler_.Active())
                    { pattern_assembler_.Cancel(); snapshot_failed_ = true; publication_.ReadbackFailed(); }
                }
            }
            else OnLine(now);
            used_ = 0;
            overflow_ = false;
        }
        else if (c == '\0') overflow_ = true;
        else if (used_ + 1 < sizeof(line_)) line_[used_++] = c;
        else overflow_ = true;
    }
    // Transport outranks informational STATUS. Finish its line, but do not wait
    // for its reply. STATE received during transport is ignored above; ACKs differ.
    if (pending_ == Command::Status && (transport_queued_ != Command::None || stop_queued_ || start_queued_) && tx_offset_ == tx_size_)
        pending_ = Command::None;
    if(pending_==Command::Midi && now-sent_at_>=6000 && tx_offset_==tx_size_) {
        midi_failed_=true; start_queued_=false; pending_=Command::None; Log("MIDI setup ERROR: acknowledgment timeout; not retrying this session");
    }
    if(pending_==Command::Audio && now-sent_at_>=6000 && tx_offset_==tx_size_) {
        audio_failed_=true;start_queued_=false;pending_=Command::None;Log("audio selection ERROR: acknowledgment timeout; not retrying this session");
    }
    if((pending_==Command::Storage || pending_==Command::StorageReady) && now-sent_at_>=15000) {
        startup_failed_=true;file_action_=FileAction::None;pending_=Command::None;Log("SD startup pattern ERROR: timeout; not retrying");
        if(tx_offset_!=tx_size_) {Disconnect("partial storage command; outcome unknown");return;}
    }
    if((file_action_==FileAction::Save || file_action_==FileAction::Session) && pending_!=Command::File && now-file_requested_at_>=15000) {
        std::strcpy(file_status_,"Save not sent: working edits/tempo could not be verified; current work retained");
        Log("File ERROR:",file_status_);file_action_=FileAction::None;
        if(exit_requested_)exit_done_=true;
    }
    if(pending_==Command::File && now-sent_at_>=15000) {
        std::strcpy(file_status_,"Save outcome uncertain: timed out; inspect device files before retry");
        Log("File ERROR:",file_status_);file_action_=FileAction::None;pending_=Command::None;
        if(exit_requested_)exit_done_=true;
    }
    if(exit_requested_ && !exit_done_ && now-exit_started_>=16000) {
        Log("EXIT: session save unconfirmed; host-only edits may be lost; playback left running");exit_done_=true;
    }
    if (pending_ != Command::None && pending_!=Command::File && pending_!=Command::Storage && pending_!=Command::StorageReady && now - sent_at_ >= 6000)
    { Disconnect("command/handshake timeout; execution unconfirmed"); return; }
    if (pending_ == Command::None && tx_offset_ == tx_size_)
    {
        if(connected_ && !audio_sent_ && (audio_supported_ || audio_preference_!=BroTracker::AudioOutputChoice::Default)) {
            Send(Command::Audio,now);
        }
        else if(connected_ && midi_supported_ && !midi_sent_ && !stop_queued_ && transport_queued_==Command::None) { Send(Command::Midi,now); }
        else if(connected_ && startup_selection_enabled_ && !startup_attempted_ && !startup_failed_) {
            if(!storage_supported_ || !storage_ready_supported_ || !audio_confirmed_ || (midi_supported_ && !midi_confirmed_) || snapshot_version_!=2) {
                startup_failed_=true;Log("SD startup pattern ERROR: readiness-capable storage, setup and stopped v2 firmware required");
            } else if(!startup_stop_sent_) {
                startup_stop_sent_=true;stopped_=false;Send(Command::Stop,now);
            } else if(!startup_ready_sent_) Send(Command::StorageReady,now);
            else if(startup_storage_ready_) Send(Command::Storage,now);
        }
        else if((file_action_==FileAction::Save || file_action_==FileAction::Session) && FileIdle() &&
            editor_seen_ && (!preview_.Supported() || (preview_.VerifiedWorking() &&
                BroTracker::SamePattern(observed_editor_,*preview_.VerifiedWorking()))) &&
            (!tempo_.Supported() || tempo_.Synchronized())) {
            Send(Command::File,now);
        }
        else if (start_queued_)
        {
            Send(Command::Start, now);
            start_queued_ = false;
        }
        else if (transport_queued_ != Command::None)
        {
            const auto command = transport_queued_; transport_queued_ = Command::None; Send(command, now);
            // Queue the write in this same service, before a host render/present.
            tx_write_attempted_=true;
            const auto count = transport_->Write(tx_,tx_size_);
            if (count > 0) { tx_offset_ = count; if(tx_offset_ == tx_size_) {tx_write_attempted_=false; Log("TX complete:",tx_);} }
            else if (count < 0) { Disconnect(transport_->Error()); return; }
        }
        else if (stop_queued_)
        {
            Send(Command::Stop, now);
            stop_queued_ = false;
        }
        else if (publication_.Phase()==BroTracker::HostPublicationPhase::Sending) {
            std::size_t length=0;
            if(publication_.FormatLine(tx_,sizeof(tx_),length)) {
                tx_size_=static_cast<unsigned>(length); tx_offset_=0; tx_publication_=true;
            }
        }
        else if (connected_ && snapshot_supported_ && !snapshot_requested_)
        {
            snapshot_requested_ = true;
            if(publication_.NeedsReadback()) publication_readback_requested_=true;
            if (next_snapshot_id_ == 0) snapshot_failed_ = true;
            else
            {
                const auto id = next_snapshot_id_;
                next_snapshot_id_ = id == UINT32_MAX ? 0 : id + 1;
                (void)pattern_assembler_.Request(id, now, snapshot_version_);
                static_assert(sizeof(tx_) >= sizeof("BTPATTERN1 GET 1 ") + 10 + 1,
                    "GET must fit maximum uint32 identity plus LF/NUL");
                const int written = std::snprintf(tx_, sizeof(tx_), "BTPATTERN1 GET %u %" PRIu32 "\n", snapshot_version_, id);
                if (written < 0 || static_cast<std::size_t>(written) >= sizeof(tx_))
                { pattern_assembler_.Cancel(); snapshot_failed_ = true; tx_size_ = 0; }
                else tx_size_ = static_cast<unsigned>(written);
                tx_offset_ = 0; // Independent request: never replaces a BTTEST1 ACK wait.
            }
        }
        else if(connected_ && volume_.Supported() && !pattern_assembler_.Active() &&
            (volume_.NeedsRead(now) || volume_.Sending() || volume_.Begin())) {
            if(volume_.NeedsRead(now) && !volume_.Sending()) {
                std::strcpy(tx_,"BTVOL1 GET 1\n");tx_size_=std::strlen(tx_);tx_offset_=0;volume_.ReadSent(now);
            } else {std::size_t n=0;if(volume_.FormatLine(tx_,sizeof(tx_),n)){tx_size_=n;tx_offset_=0;tx_volume_=true;}}
        }
        else if(connected_ && tempo_.Supported() && !PublicationBusy() && !pattern_assembler_.Active() &&
            (tempo_.NeedsRead(now) || tempo_.Sending() || tempo_.Begin())) {
            if(tempo_.NeedsRead(now) && !tempo_.Sending()) {
                std::strcpy(tx_,"BTTEMPO1 GET 1\n"); tx_size_=static_cast<unsigned>(std::strlen(tx_)); tx_offset_=0; tempo_.ReadSent(now);
            } else {
                std::size_t size=0; if(tempo_.FormatLine(tx_,sizeof(tx_),size)) {tx_size_=size;tx_offset_=0;tx_tempo_=true;}
            }
        }
        else if(connected_ && !PublicationBusy() && !pattern_assembler_.Active() &&
            (mute_.NeedsRead(now) || mute_.Sending() || mute_.Begin())) {
            if(mute_.NeedsRead(now) && !mute_.Sending()) {
                std::strcpy(tx_,"BTMUTE1 GET 1\n");tx_size_=std::strlen(tx_);tx_offset_=0;mute_.ReadSent(now);
            } else {
                std::size_t size=0;if(mute_.FormatLine(tx_,sizeof(tx_),size)) {tx_size_=size;tx_offset_=0;tx_mute_=true;}
            }
        }
        else if (connected_ && gains_.Supported() && !PublicationBusy() &&
            !pattern_assembler_.Active() && (!preview_.Supported() || (!preview_.TransferBusy() && !preview_.Recovering())) &&
            (gains_.NeedsRead(now) || gains_.Sending() || gains_.Begin(now))) {
            if(gains_.NeedsRead(now) && !gains_.Sending()) {
                std::strcpy(tx_,"BTGAIN1 GET 1\n"); tx_size_=static_cast<unsigned>(std::strlen(tx_));
                tx_offset_=0; gains_.ReadSent(now);
            } else {
                std::size_t length=0;
                if(gains_.FormatLine(tx_,sizeof(tx_),length)) {
                    tx_size_=static_cast<unsigned>(length); tx_offset_=0; tx_gains_=true;
                }
            }
        }
        else if (connected_ && preview_.Supported() && !publication_.Blocking() && !pattern_assembler_.Active()) {
            std::size_t length=0;
            if(preview_.NeedsStatus() || next_refresh_) Send(Command::Status,now);
            else if(preview_.Recovering() && next_snapshot_id_ &&
                preview_.BeginRead(next_snapshot_id_,now,tx_,sizeof(tx_),length)) {
                next_snapshot_id_=next_snapshot_id_==UINT32_MAX ? 0 : next_snapshot_id_+1;
                tx_size_=static_cast<unsigned>(length); tx_offset_=0;
            }
            else if(StartupPatternReady() && preview_.Sending()) {
                if(preview_.FormatLine(tx_,sizeof(tx_),length)) {
                    tx_size_=static_cast<unsigned>(length); tx_offset_=0; tx_preview_=true;
                }
            }
            else if(StartupPatternReady() && !continuity_pending_ && !publish_queued_ && pattern_assembler_.Snapshot() &&
                preview_.TryBegin(*pattern_assembler_.Snapshot(),now)) {
                if(preview_.FormatLine(tx_,sizeof(tx_),length)) {
                    tx_size_=static_cast<unsigned>(length); tx_offset_=0; tx_preview_=true;
                }
            }
            else if(now-sent_at_>=1000) Send(Command::Status,now);
        }
        else if (connected_ && (next_refresh_ || now - sent_at_ >= 1000)) Send(Command::Status, now);
    }
}
