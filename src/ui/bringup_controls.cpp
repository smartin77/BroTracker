#include "bringup_controls.h"
#include "host_logging.h"
void BringUpControls::Log(const char* message) {
    BroTracker::HostLog(log_,BroTracker::ClassifyHostDiagnostic("Controls",message),"Controls",message);
}
void BringUpControls::Request(BringUpAction action, const char* source, std::uint32_t now) {
    if (action == BringUpAction::None || quit_) return;
    if(action==BringUpAction::Exit) {
        screen_phase_=ScreenPhase::Idle;
        if(!exit_after_stop_) {exit_after_stop_=true;serial_.BeginExit(now);}
        quit_=serial_.ExitReady();return;
    }
    if(exit_after_stop_)return;
    if(serial_.UserActionsBlocked() && !(action==BringUpAction::ResetStop && serial_.StopAvailable()))return;
    if(action==BringUpAction::ScreenRight) {
        if(!ScreenPending()) {load_save_screen_=false;screen_status_="";}
        return;
    }
    if(action==BringUpAction::ScreenLeft) {
        if(PatternInputBlocked())return; // One transition, regardless of repeated input.
        if(serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending()) {
            screen_status_="Pattern Load/Save unavailable: wait for current operation";return;
        }
        screen_started_=now;
        if(serial_.Playing() || serial_.Paused() || serial_.Done()) {
            if(!serial_.Stop()) {screen_status_="Pattern Load/Save: STOP unavailable";return;}
            screen_phase_=ScreenPhase::Stop;screen_status_="Pattern Load/Save: waiting for STOP acknowledgement";
        } else if(serial_.MidiRecoveryStopped()) {
            if(serial_.RequestFile(BringUpSerial::FileAction::Session,now)) {
                screen_phase_=ScreenPhase::Save;screen_status_="Pattern Load/Save: saving session";
            } else screen_status_="Pattern Load/Save: session save unavailable";
        } else screen_status_="Pattern Load/Save: stopped state unconfirmed";
        return;
    }
    if(PatternInputBlocked() && action!=BringUpAction::ResetStop)return;
    if (action == BringUpAction::PatternToggle) {
        if (!serial_.SupportsPatternTransport()) action = BringUpAction::Start;
        else {
            if (stop_pending_ || exit_after_stop_ || serial_.TransportPending()) return;
            if (serial_.Paused()) { if (serial_.Continue()) Log("CONTINUE queued"); }
            else if (serial_.Playing()) { if (serial_.Pause()) Log("PAUSE queued"); }
            else { if (serial_.Start()) Log("START from pattern zero queued"); }
            return;
        }
    }
    const bool active = serial_.Connected() && (!serial_.Finished() || serial_.PublicationBusy());
    const bool exit = action == BringUpAction::Exit || (action == BringUpAction::StopExit && !active);
    const char* verb = exit ? "EXIT" : action == BringUpAction::Start ? (active ? "RESTART" : "START") : "STOP";
    BroTracker::HostLogf(log_,BroTracker::HostLogLevel::Debug,"Controls","%s requested by %s",verb,source);
    if (exit) exit_after_stop_ = true;
    if (stop_pending_ || (exit_after_stop_ && !exit)) { Log("action suppressed: shutdown/STOP pending"); return; }
    if (action == BringUpAction::Start) {
        if (!serial_.Connected()) Log("START ignored: waiting for Teensy handshake");
        else if (serial_.StartPending()) Log("START/RESTART suppressed: START pending");
        else if (serial_.Start()) {
            restart_pending_ = active;
            Log(active ? "RESTART queued via START" : "START queued");
        }
        return;
    }
    if (!active && !(action == BringUpAction::ResetStop && serial_.Connected())) {
        quit_ = exit_after_stop_;
        Log(quit_ ? "EXIT: no STOP necessary" : "STOP: already ready/stopped/finished; staying");
    } else if (serial_.Stop()) {
        stop_pending_ = true;
        stop_started_ = now;
        Log(exit_after_stop_ ? "STOP queued; EXIT after acknowledgement" : "STOP queued; remain in application");
    }
}
void BringUpControls::Tick(std::uint32_t now) {
    serial_.Tick(now);
    if(exit_after_stop_ && serial_.ExitReady()) {quit_=true;return;}
    if(ScreenPending()) {
        if(serial_.UserActionsBlocked()) {
            screen_phase_=ScreenPhase::Idle;screen_status_="Pattern Load/Save cancelled: device unavailable or synchronizing";
        } else if(screen_phase_==ScreenPhase::Stop) {
            if(serial_.Stopped() && serial_.MidiRecoveryStopped()) {
                if(serial_.RequestFile(BringUpSerial::FileAction::Session,now)) {
                    screen_phase_=ScreenPhase::Save;screen_status_="Pattern Load/Save: saving session";
                } else {screen_phase_=ScreenPhase::Idle;screen_status_="Pattern Load/Save: session save unavailable";}
            } else if(now-screen_started_>=12500) {
                screen_phase_=ScreenPhase::Idle;screen_status_="Pattern Load/Save: STOP unconfirmed";
            }
        } else if(!serial_.FilePending()) {
            load_save_screen_=serial_.SessionSaveSucceeded() && serial_.MidiRecoveryStopped();
            screen_phase_=ScreenPhase::Idle;
            screen_status_=load_save_screen_?"Session saved | Pattern Load/Save < Pattern":"Pattern Load/Save: session save failed; current work retained";
        }
    }
    if (!stop_pending_) return;
    if (serial_.Stopped()) {
        Log(exit_after_stop_ ? "STOP acknowledged; EXIT" : "STOP acknowledged; ready");
        stop_pending_ = false;
        quit_ = exit_after_stop_;
    } else if (!serial_.Connected() || (exit_after_stop_ && now - stop_started_ >= 12500)) {
        Log(exit_after_stop_ ? "STOP unconfirmed (disconnect/timeout); EXIT" : "STOP unconfirmed; remaining in application");
        stop_pending_ = false;
        quit_ = exit_after_stop_;
    }
}
