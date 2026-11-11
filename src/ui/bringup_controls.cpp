#include "bringup_controls.h"
#include "host_logging.h"
#include <algorithm>
void BringUpControls::Log(const char* message) {
    BroTracker::HostLog(log_,BroTracker::ClassifyHostDiagnostic("Controls",message),"Controls",message);
}
FileListView BringUpControls::BrowserList()const {
    if(recovery_screen_) {FileListView view;view.names={"Recover retained host work"};return view;}
    if(browser_menu_) {FileListView menu;menu.names={"Load",serial_.PatternSaveAvailable()?"Save":"Save (unavailable)"};menu.selected=browser_action_;return menu;}
    return browser_ready_?browser_list_:FileListView{};
}
std::string BringUpControls::BrowserText()const {
    if(recovery_screen_)return recovery_confirm_?"Confirm recovery: replace device RAM with retained host work? Confirm again, or Cancel to keep both unchanged.":"Host work retained. Select recovery to review before replacing device RAM. No pattern file is overwritten.";
    if(browser_saving_)return "Saving pattern to its original file; waiting for confirmed result...";
    if(browser_menu_) {
        if(!*serial_.PatternSource())return "Save unavailable: load a pattern file first (no known source).";
        if(!serial_.PatternSaveAvailable())return "Save requires PATTERNSAVECAP firmware.";
        return std::string("Save original: ")+serial_.PatternSource();
    }
    if(browser_loading_)return "Loading pattern; waiting for device and working-pattern synchronization...";
    if(browser_confirm_)return std::string("Replace the current working session with ")+browser_list_.names[browser_list_.selected].c_str()+"?\n\nConfirm: Load\nCancel: keep current work";
    if(browser_collecting_)return "Reading all pattern filenames...";
    if(browser_ready_)return browser_list_.names.empty()?"No compatible pattern files":"Select a pattern file";
    return serial_.PatternListStatus();
}
void BringUpControls::LoadSelected(std::uint32_t now) {
    browser_confirm_=false;
    if(serial_.RequestPatternLoad(browser_list_.names[browser_list_.selected].c_str(),now)) {
        browser_loading_=true;screen_status_="Loading pattern; please wait";
    } else screen_status_="Pattern Load unavailable: STOP and synchronize first";
}
void BringUpControls::Request(BringUpAction action, const char* source, std::uint32_t now) {
    if (action == BringUpAction::None || quit_) return;
    if(action==BringUpAction::InstrumentCancel && !exit_after_stop_ && InstrumentCancellationAvailable()) {
        if(ScreenPending())serial_.CancelQueuedScreenSave();screen_phase_=ScreenPhase::Idle;instrument_screen_=false;
        if(editor_)editor_->RestoreCursor(instrument_entry_.source);
        screen_status_="Instrument cancelled; cell unchanged";return;
    }
    if(action==BringUpAction::Exit) {
        screen_phase_=ScreenPhase::Idle;
        if(!exit_after_stop_) {exit_after_stop_=true;serial_.BeginExit(now);}
        quit_=serial_.ExitReady();return;
    }
    if(exit_after_stop_)return;
    // Report setup failures even before editor synchronization has completed.
    // A playing engine must still be pausable after a conflicting reconnect.
    if(serial_.Connected() && !serial_.AudioTransportReady() &&
        (action==BringUpAction::Start || (action==BringUpAction::PatternToggle &&
        (!serial_.SupportsPatternTransport() || !serial_.Playing())))) {
        Log(serial_.AudioReadinessStatus());return;
    }
    if(serial_.Connected() && !serial_.MidiDelayReady() &&
        (action==BringUpAction::Start || (action==BringUpAction::PatternToggle &&
        (!serial_.SupportsPatternTransport() || !serial_.Playing())))) {
        Log(serial_.MidiDelayStatus());return;
    }
    if(load_save_screen_ && action==BringUpAction::ScreenLeft)action=BringUpAction::BrowserBack;
    const bool recovery_available=serial_.RecoveryMenuAvailable();
    // Instrument owns Left for browsing; explicit recovery is entered from Pattern.
    if(action==BringUpAction::ScreenLeft && recovery_available && !load_save_screen_ && !instrument_screen_) {
        recovery_screen_=load_save_screen_=true;recovery_confirm_=false;return;
    }
    if(recovery_screen_) {
        if(action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack || action==BringUpAction::ScreenRight) {
            if(recovery_confirm_ && (action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack))recovery_confirm_=false;
            else recovery_screen_=load_save_screen_=false;
        } else if(action==BringUpAction::BrowserConfirm && recovery_available) {
            if(!recovery_confirm_)recovery_confirm_=true;
            else if(serial_.RecoverRetainedWork(now))recovery_screen_=load_save_screen_=false;
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(instrument_screen_) {
        if(action==BringUpAction::ScreenDown) {
            const auto limit=serial_.InstrumentCollectionLimit();
            if(!limit || instrument_number_>limit) {
                screen_status_=serial_.InstrumentCollectionKnown() && !limit?"Return unavailable: no defined instruments; cancel to Pattern (01 New)":
                    !limit?"Return unavailable: waiting for instrument collection; cancel is available":"Return unavailable: select a defined instrument or cancel";
                return;
            }

            if(serial_.UserActionsBlocked() || serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending() ||
                !editor_ || !editor_->AssignInstrumentAt(instrument_entry_.source,instrument_number_-1)) {
                screen_status_="Return unavailable: synchronize/recover before instrument assignment";return;
            }
            serial_.SyncEditor(*editor_,now);
            instrument_screen_=false;screen_status_.clear();
        } else if(action==BringUpAction::ScreenLeft || action==BringUpAction::ScreenRight) {
            const auto limit=serial_.InstrumentCollectionLimit();
            if(!limit) {screen_status_=serial_.InstrumentCollectionKnown()?"No defined instruments; cancel remains available":"Waiting for instrument collection; cancel remains available";return;}
            if(action==BringUpAction::ScreenLeft)instrument_number_=instrument_number_>limit?limit:instrument_number_==1?(serial_.InstrumentCollectionSupported()?limit:1):instrument_number_-1;
            else if(instrument_number_>limit)instrument_number_=1;
            else if(instrument_number_<limit)++instrument_number_;
            screen_status_.clear();
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(serial_.UserActionsBlocked() && !(action==BringUpAction::ResetStop && serial_.StopAvailable()))return;
    if(load_save_screen_ && (browser_loading_ || browser_saving_) && action!=BringUpAction::ResetStop)return;
    if(load_save_screen_ && action>=BringUpAction::BrowserUp) {
        if(action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack) {
            if(browser_confirm_){browser_confirm_=false;screen_status_="Load cancelled; current work retained";}
            else if(action==BringUpAction::BrowserBack && !browser_menu_) {browser_menu_=true;browser_action_=0;}
            else {load_save_screen_=false;screen_status_="";}
            return;
        }
        if(browser_menu_) {
            if(action==BringUpAction::BrowserUp)browser_action_=0;
            if(action==BringUpAction::BrowserDown)browser_action_=1;
            if(action==BringUpAction::BrowserConfirm) {
                if(browser_action_==0)browser_menu_=false;
                else if(serial_.RequestFile(BringUpSerial::FileAction::PatternSave,now))browser_saving_=true;
                else screen_status_="Pattern Save unavailable; see status";
            }
            return;
        }
        if(browser_confirm_) {if(action==BringUpAction::BrowserConfirm)LoadSelected(now);return;}
        if(!browser_ready_ || browser_collecting_)return;
        if(action==BringUpAction::BrowserUp)browser_list_.Move(-1);
        if(action==BringUpAction::BrowserDown)browser_list_.Move(1);
        if(action==BringUpAction::BrowserConfirm && !browser_list_.names.empty()) {
            if(confirm_load_replace_)browser_confirm_=true;else LoadSelected(now);
        }
        return;
    }
    if(action>=BringUpAction::BrowserUp || action==BringUpAction::ScreenDown)return;
    if(action==BringUpAction::ScreenRight) {
        if(!ScreenPending()) {browser_confirm_=false;load_save_screen_=false;screen_status_="";}
        return;
    }
    if(action==BringUpAction::ScreenLeft || action==BringUpAction::ScreenUp) {
        if(PatternInputBlocked())return;
        instrument_destination_=action==BringUpAction::ScreenUp;
        if(instrument_destination_) {
            if(editor_ && editor_->NewInstrumentSelected()) {screen_status_.clear();return;}
            instrument_entry_=editor_?CaptureInstrumentEntry(*editor_,serial_.InstrumentCollectionSupported()):InstrumentEntry{};
            if(!instrument_entry_) {screen_status_=instrument_entry_.reason;return;}
            if(!serial_.SessionSnapshotAvailable()) {screen_status_="Instrument unavailable: update session-snapshot firmware";return;}
        } // One transition, regardless of repeated input.
        if(serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending()) {
            screen_status_="Pattern Load/Save unavailable: wait for current operation";return;
        }
        browser_initialized_=browser_confirm_=browser_ready_=browser_collecting_=false;browser_list_={};browser_menu_=true;browser_saving_=false;browser_action_=0;
        screen_started_=now;
        if(serial_.Playing() || serial_.Paused() || serial_.Done()) {
            if(!serial_.Stop()) {screen_status_="Screen entry: STOP unavailable";return;}
            screen_phase_=ScreenPhase::Stop;screen_status_="Screen entry: waiting for STOP acknowledgement";
        } else if(serial_.MidiRecoveryStopped()) {
            if(serial_.RequestFile(BringUpSerial::FileAction::Session,now,true,instrument_destination_?&instrument_entry_.source:nullptr)) {
                screen_phase_=ScreenPhase::Save;screen_status_="Screen entry: saving session";
            } else screen_status_="Screen entry: session save unavailable";
        } else screen_status_="Screen entry: stopped state unconfirmed";
        return;
    }
    if(PatternInputBlocked() && action!=BringUpAction::ResetStop)return;
    if (action == BringUpAction::PatternToggle) {
        if (!serial_.SupportsPatternTransport()) action = BringUpAction::Start;
        else {
            if (stop_pending_ || exit_after_stop_ || serial_.TransportPending()) return;
            if (serial_.Paused()) { if (serial_.Continue()) Log("CONTINUE queued"); else Log(serial_.Status()); }
            else if (serial_.Playing()) { if (serial_.Pause()) Log("PAUSE queued"); }
            else { if (serial_.Start()) Log("START from pattern zero queued"); else Log(serial_.Status()); }
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
        else Log(serial_.Status());
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
    if(instrument_screen_ && serial_.UserActionsBlocked())
        screen_status_="Return unavailable: synchronize/recover before instrument assignment";
    if(exit_after_stop_ && serial_.ExitReady()) {quit_=true;return;}
    if(ScreenPending()) {
        if(serial_.UserActionsBlocked()) {
            screen_phase_=ScreenPhase::Idle;screen_status_="Screen entry cancelled: device unavailable or synchronizing";
        } else if(screen_phase_==ScreenPhase::Stop) {
            if(serial_.Stopped() && serial_.MidiRecoveryStopped()) {
                if(serial_.RequestFile(BringUpSerial::FileAction::Session,now,true,instrument_destination_?&instrument_entry_.source:nullptr)) {
                    screen_phase_=ScreenPhase::Save;screen_status_="Screen entry: saving session";
                } else {screen_phase_=ScreenPhase::Idle;screen_status_="Screen entry: session save unavailable";}
            } else if(now-screen_started_>=12500) {
                screen_phase_=ScreenPhase::Idle;screen_status_="Screen entry: STOP unconfirmed";
            }
        } else if(!serial_.FilePending()) {
            const bool entered=serial_.SessionSaveSucceeded() && serial_.MidiRecoveryStopped();
            load_save_screen_=entered && !instrument_destination_;
            instrument_screen_=entered && instrument_destination_;
            if(instrument_screen_)instrument_number_=instrument_entry_.number;
            screen_phase_=ScreenPhase::Idle;
            screen_status_=instrument_screen_?"Session saved | Instrument":load_save_screen_?"Session saved | Pattern Load/Save < Pattern":"Screen entry: session save failed; current work retained";
        }
    }
    if(load_save_screen_ && !recovery_screen_) {
        if(browser_collecting_ && !serial_.PatternListBusy()) {
            if(!serial_.PatternListReady()) {browser_collecting_=false;browser_list_={};}
            else {
                const auto& page=serial_.PatternFiles();
                for(unsigned i=0;i<page.count;++i)browser_list_.names.emplace_back(page.names[i]);
                if(!page.done) {
                    if(!serial_.RequestPatternList(page.next,now)) {browser_collecting_=false;browser_list_={};}
                } else {
                    std::sort(browser_list_.names.begin(),browser_list_.names.end());
                    browser_collecting_=false;browser_ready_=true;
                }
            }
        }
        if(browser_saving_) {
            if(!serial_.FilePending()) {
                browser_saving_=false;
                if(serial_.PatternSaveSucceeded()) {load_save_screen_=false;screen_status_=std::string("Pattern saved: ")+serial_.PatternSource();}
                else screen_status_="Pattern Save failed/uncertain; working pattern retained; see device status";
            }
        } else if(browser_loading_) {
            const auto result=serial_.PatternLoadResult();
            if(result==BringUpSerial::PatternLoadState::Succeeded) {
                browser_loading_=false;load_save_screen_=false;screen_status_="Pattern loaded; playback stopped";
            } else if(result==BringUpSerial::PatternLoadState::Failed) {
                browser_loading_=false;screen_status_="Pattern Load failed/uncertain; current editor retained; see device status";
            }
        } else if(!browser_menu_ && !browser_initialized_) {
            browser_initialized_=true;browser_collecting_=serial_.RequestPatternList(0,now);
        }
    }
    if (!stop_pending_) return;
    if (serial_.Stopped()) {
        Log(exit_after_stop_ ? "STOP acknowledged; EXIT" : "STOP acknowledged; transport stopped");
        stop_pending_ = false;
        quit_ = exit_after_stop_;
    } else if (!serial_.Connected() || (exit_after_stop_ && now - stop_started_ >= 12500)) {
        Log(exit_after_stop_ ? "STOP unconfirmed (disconnect/timeout); EXIT" : "STOP unconfirmed; remaining in application");
        stop_pending_ = false;
        quit_ = exit_after_stop_;
    }
}
