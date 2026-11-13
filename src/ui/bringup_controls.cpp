#include "bringup_controls.h"
#include "host_logging.h"
#include <algorithm>
void BringUpControls::Log(const char* message) {
    BroTracker::HostLog(log_,BroTracker::ClassifyHostDiagnostic("Controls",message),"Controls",message);
}
FileListView BringUpControls::BrowserList()const {
    if(replace_prompt_)return FileListView{};
    if(wav_picker_)return browser_ready_?browser_list_:FileListView{};
    if(recovery_screen_) {FileListView view;view.names={"Recover retained host work"};return view;}
    if(browser_menu_) {FileListView menu;menu.names={"Load",serial_.PatternSaveAvailable()?"Save":"Save (unavailable)"};menu.selected=browser_action_;return menu;}
    return browser_ready_?browser_list_:FileListView{};
}
std::string BringUpControls::BrowserText()const {
    if(replace_prompt_)return "Replace saved Sample WAV?\nOld: "+SavedInstrumentPath()+"\nNew: "+replacement_candidate_+"\n\nEnter / A: stage replacement\nEscape / B: keep saved path";
    if(wav_picker_)return browser_collecting_?"Reading /BroTracker/Samples/...":browser_ready_?(browser_list_.names.empty()?"No WAV files":"Select a WAV; preparation occurs on confirmed return"):serial_.PatternListStatus();
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
bool BringUpControls::InstrumentTypeEditable()const {
    return instrument_screen_ && serial_.InstrumentCollectionKnown() && instrument_number_==serial_.InstrumentCollectionSize()+1 && instrument_number_<=99;
}
std::string BringUpControls::InstrumentDraftType()const {
    if(!InstrumentTypeEditable())return {};
    const auto type=instrument_number_==draft_slot_?definition_draft_.type:1;
    return type==1?"Sample":type==2?"MIDI":"Sine Synth";
}
std::string BringUpControls::SavedInstrumentPath()const {
    return serial_.InspectedSample(instrument_number_)?serial_.InstrumentResource():std::string{};
}
std::string BringUpControls::InstrumentLoadWarning()const {
    if(!serial_.InspectedSample(instrument_number_))return {};
    const auto status=serial_.InstrumentAvailabilityText();
    return status=="Prepared"?std::string{}:"Load warning: "+status+"; locate WAV with Load";
}
bool BringUpControls::InstrumentLoadAvailable()const {
    const bool sample=InstrumentTypeEditable()?InstrumentDraftType()=="Sample":serial_.InspectedSample(instrument_number_) && serial_.RelinkSupported();
    return instrument_screen_ && !wav_picker_ && !definition_return_ && sample && serial_.WavPickerAvailable() &&
        !serial_.UserActionsBlocked() && !serial_.FilePending() && !serial_.TransportPending() && !serial_.PublicationBusy();
}
void BringUpControls::Request(BringUpAction action, const char* source, std::uint32_t now) {
    if (action == BringUpAction::None || quit_) return;
    if(instrument_screen_ && !wav_picker_ && (action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack))action=BringUpAction::InstrumentCancel;
    if(action==BringUpAction::InstrumentCancel && !exit_after_stop_ && InstrumentCancellationAvailable()) {
        definition_return_=replacement_return_=replace_prompt_=false;definition_draft_={};sample_path_.clear();draft_slot_=0;
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
    if(wav_picker_ && replace_prompt_) {
        if(action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack || action==BringUpAction::ScreenLeft || action==BringUpAction::ScreenRight) {
            replace_prompt_=wav_picker_=false;replacement_candidate_.clear();sample_path_.clear();definition_draft_.sample=nullptr;
            screen_status_="Replacement declined; saved WAV path retained";return;
        }
        if(action==BringUpAction::BrowserConfirm && !serial_.UserActionsBlocked()) {
            sample_path_=replacement_candidate_;definition_draft_.sample=sample_path_.c_str();definition_draft_.explicit_type=true;draft_slot_=instrument_number_;
            replace_prompt_=wav_picker_=false;screen_status_="Replacement staged; confirm return to prepare before replacing";return;
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(wav_picker_) {
        if(action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack || action==BringUpAction::ScreenRight || action==BringUpAction::ScreenLeft) {
            wav_picker_=false;screen_status_="WAV selection cancelled; previous draft retained";return;
        }
        if(!serial_.UserActionsBlocked() && browser_ready_) {
            if(action==BringUpAction::BrowserUp)browser_list_.Move(-1);
            if(action==BringUpAction::BrowserDown)browser_list_.Move(1);
            if(action==BringUpAction::BrowserConfirm && !browser_list_.names.empty()) {
                char path[64];
                if(BroTracker::BuildWavResource(browser_list_.names[browser_list_.selected],path,sizeof(path))) {
                    const auto saved=SavedInstrumentPath();
                    if(!saved.empty() && saved!=path) {replacement_candidate_=path;replace_prompt_=true;screen_status_="Confirm Sample WAV replacement";return;}
                    sample_path_=path;definition_draft_.sample=sample_path_.c_str();definition_draft_.explicit_type=true;draft_slot_=instrument_number_;
                    wav_picker_=false;screen_status_="WAV selected; confirm return to prepare and define, or cancel";
                }
            }
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(instrument_screen_) {
        if(action==BringUpAction::InstrumentPreviousField || action==BringUpAction::InstrumentNextField) {
            if(!definition_return_ && InstrumentTypeEditable())instrument_field_=InstrumentLoadAvailable()?(action==BringUpAction::InstrumentPreviousField?0:1):0;
            return;
        }
        if(action==BringUpAction::InstrumentActivate) {
            if(InstrumentFocus()==0)action=BringUpAction::InstrumentTypeNext;
            else if(InstrumentFocus()==1)action=BringUpAction::BrowserConfirm;
            else return;
        }
        if(action==BringUpAction::InstrumentIncrease || action==BringUpAction::InstrumentDecrease) {
            if(InstrumentFocus()!=0 || !InstrumentTypeEditable() || definition_return_)return;
            draft_slot_=instrument_number_;definition_draft_.type=action==BringUpAction::InstrumentIncrease?definition_draft_.type%3+1:(definition_draft_.type+1)%3+1;
            definition_draft_.explicit_type=true;definition_draft_.sample=definition_draft_.type==1 && !sample_path_.empty()?sample_path_.c_str():nullptr;
            screen_status_="Type draft changed; confirm return or cancel";return;
        }

        if(action==BringUpAction::BrowserConfirm) {
            if(InstrumentLoadAvailable() && !serial_.PatternListBusy()) {
                wav_picker_=true;browser_list_={};browser_ready_=false;
                browser_collecting_=serial_.RequestPatternList(0,now,true);screen_status_="Sample Load";
            } else screen_status_="Sample Load unavailable: select Sample in the trailing slot and synchronize";
            return;
        }
        if(action==BringUpAction::InstrumentSample || action==BringUpAction::InstrumentMidi || action==BringUpAction::InstrumentSynth || action==BringUpAction::InstrumentTypeNext) {
            if(!InstrumentTypeEditable() || InstrumentFocus()!=0 || definition_return_)return;
            draft_slot_=instrument_number_;
            definition_draft_.type=action==BringUpAction::InstrumentSample?1:action==BringUpAction::InstrumentMidi?2:action==BringUpAction::InstrumentSynth?3:definition_draft_.type%3+1;
            definition_draft_.sample=definition_draft_.type==1 && !sample_path_.empty()?sample_path_.c_str():nullptr;
            definition_draft_.explicit_type=true;screen_status_="Definition draft changed; confirm return to commit, or cancel";return;
        }
        if(definition_return_ && action!=BringUpAction::ResetStop)return;

        if(action==BringUpAction::ScreenDown) {
            const auto limit=serial_.InstrumentCollectionLimit();
            if(!limit || instrument_number_>limit) {
                screen_status_=!limit?"Return unavailable: waiting for instrument collection; cancel is available":"Return unavailable: select a numbered slot or cancel";
                return;
            }

            if(serial_.UserActionsBlocked() || serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending() ||
                !editor_ || editor_->Locked()) {
                screen_status_="Return unavailable: synchronize/recover before instrument assignment";return;
            }
            if(!InstrumentTypeEditable() && serial_.InspectedSample(instrument_number_) && draft_slot_==instrument_number_ && !sample_path_.empty()) {
                if(serial_.RelinkInstrument(instrument_number_,sample_path_.c_str(),now)) {definition_return_=replacement_return_=true;screen_status_="Preparing candidate WAV before relinking Sample...";}
                else screen_status_=serial_.InstrumentCommitError();return;
            }
            if(!sample_path_.empty() && definition_draft_.type!=1)definition_draft_.sample=nullptr;
            if(serial_.InstrumentCollectionSupported() && instrument_number_==serial_.InstrumentCollectionSize()+1 && instrument_number_==draft_slot_ && definition_draft_.Qualifies()) {
                if(serial_.CommitInstrument(definition_draft_.type,definition_draft_.sample,now)) {definition_return_=true;screen_status_="Committing definition on Teensy...";}
                else screen_status_=serial_.InstrumentCommitError();
                return;
            }
            if(!editor_->AssignInstrumentAt(instrument_entry_.source,instrument_number_-1)) {screen_status_="Assignment blocked; cancel or synchronize";return;}
            serial_.SyncEditor(*editor_,now);
            instrument_screen_=false;screen_status_.clear();
        } else if(action==BringUpAction::ScreenLeft || action==BringUpAction::ScreenRight) {
            const auto limit=serial_.InstrumentCollectionLimit();
            if(!limit) {screen_status_="Waiting for instrument collection; cancel remains available";return;}
            if(action==BringUpAction::ScreenLeft)instrument_number_=instrument_number_>limit?limit:instrument_number_==1?(serial_.InstrumentCollectionSupported()?limit:1):instrument_number_-1;
            else if(instrument_number_>limit)instrument_number_=1;
            else if(instrument_number_<limit)++instrument_number_;
            instrument_field_=0;
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
    if(definition_return_ && !serial_.InstrumentCommitPending()) {
        definition_return_=false;
        if(!serial_.InstrumentCommitSucceeded()) {screen_status_=serial_.InstrumentCommitError();replacement_return_=false;}
        else if(replacement_return_) {replacement_return_=false;instrument_screen_=false;if(editor_)editor_->RestoreCursor(instrument_entry_.source);sample_path_.clear();definition_draft_={};draft_slot_=0;screen_status_="Sample WAV relinked; Pattern references unchanged";}
        else if(editor_ && editor_->AssignInstrumentAt(instrument_entry_.source,instrument_number_-1)) {serial_.SyncEditor(*editor_,now);instrument_screen_=false;definition_draft_={};draft_slot_=0;screen_status_.clear();}
        else screen_status_="Definition committed; assignment blocked. Retry return or cancel";
    }
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
            if(instrument_screen_) {instrument_field_=0;replacement_return_=false;instrument_number_=instrument_entry_.number;definition_draft_={};sample_path_.clear();draft_slot_=0;definition_return_=false;}
            screen_phase_=ScreenPhase::Idle;
            screen_status_=instrument_screen_?"Session saved | Instrument":load_save_screen_?"Session saved | Pattern Load/Save < Pattern":"Screen entry: session save failed; current work retained";
        }
    }
    if((load_save_screen_ || wav_picker_) && !recovery_screen_) {
        if(browser_collecting_ && !serial_.PatternListBusy()) {
            if(!serial_.PatternListReady()) {browser_collecting_=false;browser_list_={};}
            else {
                const auto& page=serial_.PatternFiles();
                for(unsigned i=0;i<page.count;++i)browser_list_.names.emplace_back(page.names[i]);
                if(!page.done) {
                    if(!serial_.RequestPatternList(page.next,now,wav_picker_)) {browser_collecting_=false;browser_list_={};}
                } else {
                    std::sort(browser_list_.names.begin(),browser_list_.names.end());
                    browser_collecting_=false;browser_ready_=true;
                }
            }
        }
        if(wav_picker_) {}
        else if(browser_saving_) {
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
