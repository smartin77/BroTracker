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

void BringUpSerial::Disconnect(const char* reason)
{
    Log("disconnected/error:", reason);
    gains_.Disconnect(); tx_gains_=false;
    publication_.Disconnect();
    preview_.Disconnect(); tx_preview_=false;
    if(publish_queued_) { publish_queued_=false; preview_.Suspend(false); }
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
    if(editor.Ready()) for(unsigned i=0;i<BroTracker::kSparsePreviewCellCapacity;++i) {
        BroTracker::SparsePreviewCell cell;
        if(editor.TakeCapturedCell(i,cell) && preview_session_ && !publish_queued_ && !publication_.Blocking())
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
    if(preview_session_ && editor_seen_ && editor.Ready() && !publish_queued_ &&
        !publication_.Blocking() && !BroTracker::SamePattern(observed_editor_,*editor.Draft()))
        preview_.Observe(*editor.Draft(),now);
    editor.Sync(PlaybackView(now));
    if(preview_.Supported()) preview_session_=true;
    if(preview_.Supported() && editor.Ready()) {
        preview_.SetBaseline(*editor.Baseline());
        if(const auto* recovered=preview_.RecoveredDraft()) {
            if(publish_queued_ || editor.RecoverDraft(*recovered)) preview_.RecoveredInstalled();
        }
        if(!preview_.Recovering() && !publish_queued_ && !publication_.Blocking()) preview_.Observe(*editor.Draft(),now);
    }
    if(publication_.TakeConfirmed()) {
        if(const auto* snapshot=pattern_assembler_.Snapshot()) {
            if(editor.Rebase(*snapshot) && preview_.Supported()) preview_.Published(snapshot->pattern);
        }
    }
    if(publish_queued_ && connected_ && !Playing() && preview_.CanPublish() && pending_==Command::None &&
        !TransportPending() && !pattern_assembler_.Active() && next_fresh_ && next_publication_id_ && tx_offset_==tx_size_) {
        if(publication_.Begin(next_publication_id_,queued_baseline_,queued_publication_,now)) {
            publish_queued_=false; next_publication_id_=0; next_fresh_=false;
            next_refresh_=true; publication_readback_requested_=false;
        }
    }
    if(!publish_queued_ && !publication_.Blocking()) preview_.Suspend(false);
    editor.SetLocked(PublicationBusy());
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
    if(editor.Ready()) preview_.RetainLocal(editor.Dirty());
    editor_seen_=editor.Ready();
    if(editor_seen_) observed_editor_=*editor.Draft();
}

const char* BringUpSerial::PublicationUnavailable(const LocalPatternEditor& editor) const {
    if(!connected_) return "Connect Teensy to publish";
    if(!publication_supported_) return "Firmware does not support publication";
    if(PublicationBusy()) return "Publication already in progress";
    if(Playing()) return "Pause or stop playback to publish";
    if(Paused() && !paused_publication_supported_) return "Stop playback: paused publication unsupported";
    if(!Paused() && std::strcmp(state_,"IDLE")) return "Playback state unavailable for publication";
    if(TransportPending() || pending_!=Command::None) return "Wait for transport reply";
    if(pattern_assembler_.Active() || !snapshot_requested_) return "Wait for device pattern";
    if(!editor.Baseline() || editor.Baseline()->version!=2 || snapshot_version_!=2) return "Timing-aware device pattern required";
    if(!editor.Dirty()) return "No changes to publish";
    if(!next_fresh_) return "Wait for publication identity refresh";
    if(!next_publication_id_) return "Publication identities exhausted";
    if(tx_offset_!=tx_size_) return "Wait for serial transmission";
    return nullptr;
}

bool BringUpSerial::Publish(LocalPatternEditor& editor, std::uint32_t now)
{
    // F5 may resolve an uncertain outcome with GET, never resend publication.
    if(publication_.Phase()==BroTracker::HostPublicationPhase::Uncertain) {
        if(!connected_ || snapshot_version_!=2 || pattern_assembler_.Active()) return false;
        publication_readback_requested_=false; snapshot_requested_=false; return true;
    }
    if(preview_.Supported() && editor.Ready() && editor.Dirty() && connected_ && publication_supported_ &&
        !Playing() && (Stopped() || !std::strcmp(state_,"IDLE") || (Paused() && paused_publication_supported_)) &&
        !PublicationBusy() && !TransportPending() && editor.Baseline()->version==2 && next_fresh_ && next_publication_id_) {
        queued_baseline_=*editor.Baseline(); queued_publication_=*editor.Draft(); publish_queued_=true;
        preview_.Observe(queued_publication_,now); preview_.Suspend(true); editor.SetLocked(true); next_refresh_=true;
        return true;
    }
    if(const auto* reason=PublicationUnavailable(editor)) { publication_.Unavailable(reason); return false; }
    const auto* baseline=editor.Baseline();
    if(!publication_.Begin(next_publication_id_,*baseline,*editor.Draft(),now)) return false;
    next_publication_id_=0; next_fresh_=false; next_discovery_expected_=false;
    next_refresh_=true; publication_readback_requested_=false;
    editor.SetLocked(true); Log("publication frozen; explicit request"); return true;
}

bool BringUpSerial::Start()
{
    if (!connected_ || TransportPending() || PublicationBusy()) return false;
    start_queued_ = true;
    ClearPosition();
    start_cancelled_ = start_unconfirmed_ = false;
    stopped_ = false;
    Log("START requested");
    return true;
}

bool BringUpSerial::Stop()
{
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
    if (stop_queued_ || pending_ == Command::Stop) return "Connected - stopping";
    if (start_queued_ || pending_ == Command::Start) return "Connected - starting";
    if (transport_queued_ == Command::Pause || pending_ == Command::Pause) return "Connected - pausing";
    if (transport_queued_ == Command::Continue || pending_ == Command::Continue) return "Connected - continuing";
    if (Paused()) return "Connected - PAUSED";
    if (std::strcmp(state_, "PLAYING") == 0) return "Connected - playing";
    if (std::strcmp(state_, "DONE") == 0) return "Connected - completed";
    if (std::strcmp(state_, "ERROR") == 0) return "Connected - playback ERROR";
    return "Connected - idle";
}

void BringUpSerial::Send(Command command, std::uint32_t now)
{
    const char* verb = command == Command::Hello ? "HELLO" :
                       command == Command::Status ? "STATUS" :
                       command == Command::Start ? "START" :
                       command == Command::Pause ? "PAUSE" : command == Command::Continue ? "CONTINUE" : "STOP";
    tx_size_ = std::snprintf(tx_, sizeof(tx_), "BTTEST1 %s\n", verb);
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
    if (!connected_) return std::nullopt;
    auto view = live_;
    if(now-performance_at_>=kCallbackPerformanceStaleMs) view.callback_performance.reset();
    if (const auto* snapshot = pattern_assembler_.Snapshot()) view.device_pattern = *snapshot;
    view.preview_supported=preview_.Supported();
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
    if (!std::strncmp(line_,"BTPATTERN1 PERF ",sizeof("BTPATTERN1 PERF ")-1)) {
        BroTracker::PatternPerformance report;
        if(connected_ && BroTracker::ParsePatternPerformance(line_,used_,report)) {
            live_.callback_performance=report; performance_at_=now;
        }
    }
    else if (!std::strncmp(line_,"BTGAIN1",7)) {
        if(connected_) (void)gains_.Response(line_,used_,now);
    }
    else if (!std::strncmp(line_,"BTPREVIEW1",10)) {
        if(connected_) (void)preview_.Response(line_,used_,now);
    }
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
            result == BroTracker::SnapshotAssemblyStatus::Timeout) { snapshot_failed_ = true; publication_.ReadbackFailed(); }
        if(result==BroTracker::SnapshotAssemblyStatus::Complete && publication_.NeedsReadback())
            (void)publication_.Verify(*pattern_assembler_.Snapshot());
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
        if (pending_ == Command::Start) pending_ = Command::None;
    }
    else if (std::strcmp(line_, "BTTEST1 PAUSED") == 0 || std::strcmp(line_, "BTTEST1 CONTINUED") == 0)
    {
        const auto expected = !std::strcmp(line_, "BTTEST1 PAUSED") ? Command::Pause : Command::Continue;
        if (pending_ != expected) return;
        std::strcpy(state_, expected == Command::Pause ? "PAUSED" : "PLAYING");
        pending_ = Command::None;
        // Do not relabel pre-command telemetry as the applied position. Firmware
        // follows this ACK with a coherent fresh snapshot; wait for that barrier.
        position_received_ = false;
    }
    else if (std::strcmp(line_, "BTTEST1 STOPPED") == 0)
    {
        ClearPosition();
        std::strcpy(state_, "IDLE");
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
    }
    else if (std::strncmp(line_, "BTTEST1 ERROR ", 14) == 0)
    {
        std::strcpy(state_, "ERROR");
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
    gains_.Tick(now);
    publication_.Tick(now);
    preview_.Tick(now);
    if (pattern_assembler_.Tick(now) == BroTracker::SnapshotAssemblyStatus::Timeout) {
        snapshot_failed_ = true; publication_.ReadbackFailed();
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
    if(tx_gains_ && tx_offset_==0 && !tx_write_attempted_ && TransportPending()) { tx_size_=0; tx_gains_=false; }
    if (tx_offset_ < tx_size_)
    {
        // An asynchronous transport may attempt transmission before reporting
        // completion. A later disconnect must conservatively report UNKNOWN.
        if (pending_ == Command::Start) start_write_attempted_ = true;
        if(tx_publication_) publication_.BeforeWrite();
        if(tx_preview_) preview_.BeforeWrite();
        if(tx_gains_) gains_.BeforeWrite(now);
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
                if(tx_gains_) { gains_.LineComplete(); tx_gains_=false; }
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
    if (pending_ != Command::None && now - sent_at_ >= 6000)
    { Disconnect("command/handshake timeout; execution unconfirmed"); return; }
    if (pending_ == Command::None && tx_offset_ == tx_size_)
    {
        if (start_queued_)
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
            else if(preview_.Sending()) {
                if(preview_.FormatLine(tx_,sizeof(tx_),length)) {
                    tx_size_=static_cast<unsigned>(length); tx_offset_=0; tx_preview_=true;
                }
            }
            else if(!publish_queued_ && pattern_assembler_.Snapshot() &&
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
