#include "bringup_serial.h"
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
    std::fprintf(log_, "USB bring-up: %s %s\n", message, detail);
    std::fflush(log_);
}

void BringUpSerial::Disconnect(const char* reason)
{
    Log("disconnected/error:", reason);
    publication_.Disconnect();
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
    editor.Sync(PlaybackView(now));
    if(publication_.TakeConfirmed()) {
        if(const auto* snapshot=pattern_assembler_.Snapshot()) (void)editor.Rebase(*snapshot);
    }
    editor.SetLocked(PublicationBusy());
}

bool BringUpSerial::Publish(LocalPatternEditor& editor, std::uint32_t now)
{
    // F5 may resolve an uncertain outcome with GET, never resend publication.
    if(publication_.Phase()==BroTracker::HostPublicationPhase::Uncertain) {
        if(!connected_ || snapshot_version_!=2 || pattern_assembler_.Active()) return false;
        publication_readback_requested_=false; snapshot_requested_=false; return true;
    }
    const auto* baseline=editor.Baseline();
    if(!connected_ || !publication_supported_ || !next_fresh_ || !next_publication_id_ ||
        !baseline || baseline->version!=2 || snapshot_version_!=2 || !editor.Dirty() ||
        std::strcmp(state_,"IDLE") || TransportPending() || pending_!=Command::None ||
        pattern_assembler_.Active() || !snapshot_requested_ || tx_offset_!=tx_size_ || PublicationBusy()) {
        publication_.Unavailable(); return false;
    }
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
    if(tx_publication_ && tx_offset_==0 && publication_.Phase()==BroTracker::HostPublicationPhase::Rejected) {
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
    if(command==Command::Hello || command==Command::Status) { next_discovery_expected_=true; next_refresh_=false; }
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
    if (const auto* snapshot = pattern_assembler_.Snapshot()) view.device_pattern = *snapshot;
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
    if (!std::strcmp(line_,BroTracker::kPatternPublicationCapability))
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
    publication_.Tick(now);
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
    if(tx_publication_ && tx_offset_==0 && publication_.Phase()!=BroTracker::HostPublicationPhase::Sending) {
        tx_size_=0; tx_publication_=false;
    }
    if (tx_offset_ < tx_size_)
    {
        // An asynchronous transport may attempt transmission before reporting
        // completion. A later disconnect must conservatively report UNKNOWN.
        if (pending_ == Command::Start) start_write_attempted_ = true;
        if(tx_publication_) publication_.BeforeWrite();
        const auto count = transport_->Write(tx_ + tx_offset_, tx_size_ - tx_offset_);
        if (count > 0)
        {
            tx_offset_ += count;
            if (tx_offset_ == tx_size_) {
                Log("TX complete:", tx_);
                if(tx_publication_) { publication_.LineComplete(); tx_publication_=false; }
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
                if (pattern_assembler_.Active())
                { pattern_assembler_.Cancel(); snapshot_failed_ = true; publication_.ReadbackFailed(); }
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
    if (pending_ == Command::Status && transport_queued_ != Command::None && tx_offset_ == tx_size_)
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
            const auto count = transport_->Write(tx_,tx_size_);
            if (count > 0) { tx_offset_ = count; if(tx_offset_ == tx_size_) Log("TX complete:",tx_); }
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
        else if (connected_ && (next_refresh_ || now - sent_at_ >= 1000)) Send(Command::Status, now);
    }
}
