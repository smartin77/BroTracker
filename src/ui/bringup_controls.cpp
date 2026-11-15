#include "bringup_controls.h"
#include "host_logging.h"
#include <algorithm>
void BringUpControls::Log(const char* message) {
    BroTracker::HostLog(log_,BroTracker::ClassifyHostDiagnostic("Controls",message),"Controls",message);
}
FileListView BringUpControls::BrowserList()const {
    if(replace_prompt_)return FileListView{};
    if(wav_picker_)return wav_browser_.View();
    if(recovery_screen_) {FileListView view;view.names={"Recover retained host work"};return view;}
    if(browser_menu_) {FileListView menu;menu.names={"Load",serial_.PatternSaveAvailable()?"Save":"Save (unavailable)"};menu.selected=browser_action_;return menu;}
    return browser_ready_?browser_list_:FileListView{};
}
std::string BringUpControls::BrowserText()const {
    if(replace_prompt_)return std::string(serial_.LiveEditSupported()?"Replace current Sample WAV?\nOld: ":"Replace saved Sample WAV?\nOld: ")+SavedInstrumentPath()+"\nNew: "+replacement_candidate_+(serial_.LiveEditSupported()?"\n\nEnter / A: prepare and apply\nEscape / B: keep current path":"\n\nEnter / A: stage replacement\nEscape / B: keep saved path");
    if(wav_picker_)return wav_browser_.Message();
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
void BringUpControls::OpenWavDirectory(const std::string& path,bool fallback,std::uint32_t now) {
    wav_browser_.Begin(path);wav_collecting_=serial_.RequestWavDirectory(path,0,fallback,now);
    if(!wav_collecting_)wav_browser_.Fail(BroTracker::WavDirectoryResult::Busy);
}
void BringUpControls::LoadSelected(std::uint32_t now) {
    browser_confirm_=false;
    if(serial_.RequestPatternLoad(browser_list_.names[browser_list_.selected].c_str(),now)) {
        browser_loading_=true;screen_status_="Loading pattern; please wait";
    } else screen_status_="Pattern Load unavailable: STOP and synchronize first";
}
bool BringUpControls::InstrumentTypeEditable()const {
    return instrument_screen_ && (!serial_.LiveEditSupported() || serial_.InstrumentVisitReady()) && serial_.InstrumentCollectionKnown() && instrument_number_<=serial_.InstrumentCollectionLimit() &&
        (instrument_number_==serial_.InstrumentCollectionSize()+1 || (serial_.ReplaceSupported() && serial_.InspectedType(instrument_number_)) ||
            (instrument_number_<=99 && original_types_[instrument_number_-1]));
}
bool BringUpControls::HasLiveTypes()const {
    for(auto type:original_types_)if(type)return true;return false;
}
bool BringUpControls::LiveTypesSettled()const {
    if(live_type_inflight_ || live_type_failed_)return false;
    for(unsigned i=0;i<99;++i)if(original_types_[i] && (desired_types_[i]!=applied_types_[i] || desired_resources_[i]!=applied_resources_[i]))return false;
    return true;
}
bool BringUpControls::CaptureLiveDefinition() {
    if(!instrument_number_ || instrument_number_>serial_.InstrumentCollectionSize())return false;
    const auto slot=instrument_number_-1;
    if(original_types_[slot])return true;
    if(!serial_.InspectedType(instrument_number_))return false;
    if(!original_types_[slot]) {
        original_types_[slot]=desired_types_[slot]=applied_types_[slot]=serial_.InspectedType(instrument_number_);
        const auto path=serial_.InstrumentResource();
        original_resources_[slot]=desired_resources_[slot]=applied_resources_[slot]=original_types_[slot]==1 && BroTracker::InstrumentDefinitions::ValidResource(path.c_str())?path:std::string{};
    }
    return true;
}
bool BringUpControls::StageLiveSample(const std::string& path,std::uint32_t now) {
    if(!serial_.LiveEditSupported() || !serial_.InstrumentVisitReady() || !CaptureLiveDefinition())return false;
    const auto slot=instrument_number_-1;
    desired_types_[slot]=1;desired_resources_[slot]=path;live_type_failed_=false;
    definition_draft_={};sample_path_.clear();draft_slot_=0;TickLiveTypes(now);return true;
}
void BringUpControls::TickLiveTypes(std::uint32_t now) {
    if(live_type_inflight_ && !serial_.InstrumentCommitPending()) {
        const bool success=serial_.InstrumentCommitSucceeded();
        const bool known_failure=serial_.InstrumentCommitResult()<=7;
        if(success){applied_types_[live_type_slot_]=live_type_sent_;applied_resources_[live_type_slot_]=live_resource_sent_;}
        else if(!known_failure)applied_types_[live_type_slot_]=0;
        live_type_failed_=!success && !serial_.InstrumentCommitCancelled() && !assignment_rollback_;
        live_type_inflight_=false;
        if(!success && !serial_.InstrumentCommitCancelled()) {
            screen_status_=serial_.InstrumentCommitError()+(known_failure?"; previous binding retained, Load retries or Escape restores":"; outcome unconfirmed, Escape restores the retained snapshot");
            assignment_confirm_=false;
        } else if(success)screen_status_="Instrument applied in Teensy RAM; Ctrl+Down keeps it, Escape restores the original";
    }
    if(live_type_inflight_ || live_type_failed_ || serial_.InstrumentCommitPending())return;
    for(unsigned i=0;i<99;++i)if(original_types_[i] && (desired_types_[i]!=applied_types_[i] || desired_resources_[i]!=applied_resources_[i])) {
        if(desired_types_[i]==1 && desired_resources_[i].empty() && !assignment_rollback_) {
            screen_status_="Select a WAV with Load; the current playable binding is retained";return;
        }
        const bool restore=assignment_rollback_ || (desired_types_[i]==original_types_[i] && desired_resources_[i]==original_resources_[i]);
        const bool accepted=serial_.LiveEditSupported()?serial_.ReplaceLiveDefinition(i+1,desired_types_[i],desired_resources_[i].c_str(),now,restore):serial_.ReplaceLiveType(i+1,desired_types_[i],now);
        if(accepted) {
            live_type_slot_=i;live_type_sent_=desired_types_[i];live_resource_sent_=desired_resources_[i];live_type_inflight_=true;
            screen_status_=assignment_rollback_?"Restoring original definition and binding; waiting for device application":"Preparing/applying instrument in Teensy RAM";
        } else screen_status_=serial_.InstrumentCommitError();
        return;
    }
}
bool BringUpControls::InstrumentDraftChanged()const {
    return draft_slot_==instrument_number_ && ((definition_draft_.explicit_type &&
        InstrumentDraftTypeCode()!=serial_.InspectedType(instrument_number_)) || !sample_path_.empty());
}
void BringUpControls::FinishInstrumentVisit() {
    if(serial_.InstrumentVisitActive()) {
        serial_.EndInstrumentVisit();visit_closing_=true;screen_status_="Finishing Instrument visit; retaining sources until device acknowledgement";return;
    }
    visit_closing_=false;
    if(editor_)editor_->RestoreCursor(instrument_entry_.source);
    instrument_screen_=false;instrument_entry_pattern_.reset();
    definition_draft_={};sample_path_.clear();draft_slot_=0;
    live_assignment_=assignment_confirm_=assignment_rollback_=false;
    original_types_.fill(0);desired_types_.fill(0);applied_types_.fill(0);
    live_type_inflight_=live_type_failed_=false;
    original_resources_.fill({});desired_resources_.fill({});applied_resources_.fill({});
    screen_status_.clear();
}
unsigned BringUpControls::InstrumentDraftTypeCode()const {
    if(instrument_number_ && instrument_number_<=99 && original_types_[instrument_number_-1])return desired_types_[instrument_number_-1];
    return instrument_number_==draft_slot_?definition_draft_.type:serial_.InspectedType(instrument_number_)?serial_.InspectedType(instrument_number_):1;
}
std::string BringUpControls::InstrumentDraftType()const {
    if(!InstrumentTypeEditable())return {};
    const auto type=InstrumentDraftTypeCode();return type==1?"Sample":type==2?"MIDI":"Sine Synth";
}
std::string BringUpControls::SavedInstrumentPath()const {
    if(instrument_number_ && instrument_number_<=99 && original_types_[instrument_number_-1])return applied_types_[instrument_number_-1]==1?applied_resources_[instrument_number_-1]:std::string{};
    return serial_.InspectedSample(instrument_number_)?serial_.InstrumentResource():std::string{};
}
std::string BringUpControls::InstrumentLoadWarning()const {
    if(!serial_.InspectedSample(instrument_number_))return {};
    const auto status=serial_.InstrumentAvailabilityText();
    return status=="Prepared"?std::string{}:"Load warning: "+status+"; locate WAV with Load";
}
bool BringUpControls::InstrumentLoadAvailable()const {
    const bool sample=InstrumentTypeEditable()?InstrumentDraftType()=="Sample":serial_.InspectedSample(instrument_number_) && serial_.RelinkSupported();
    return instrument_screen_ && (serial_.MidiRecoveryStopped() || serial_.InstrumentVisitReady()) && (!serial_.LiveEditSupported() || !serial_.InstrumentCommitPending()) && !wav_picker_ && !definition_return_ && sample && serial_.WavPickerAvailable() &&
        !serial_.UserActionsBlocked() && !serial_.FilePending() && !serial_.TransportPending() && !serial_.PublicationBusy();
}
void BringUpControls::Request(BringUpAction action, const char* source, std::uint32_t now) {
    if (action == BringUpAction::None || quit_) return;
    if(instrument_screen_ && !wav_picker_ && (action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack))action=BringUpAction::InstrumentCancel;
    if(action==BringUpAction::InstrumentCancel && !exit_after_stop_ && InstrumentCancellationAvailable()) {
        if(visit_closing_)return;
        serial_.CancelLiveDefinition();
        if(live_assignment_ || HasLiveTypes()) {
            assignment_confirm_=false;assignment_rollback_=true;
            for(unsigned i=0;i<99;++i)if(original_types_[i]){desired_types_[i]=original_types_[i];desired_resources_[i]=original_resources_[i];}
            live_type_failed_=false;
            definition_draft_={};sample_path_.clear();draft_slot_=0;
            screen_status_="Restoring original assignment; waiting for device application";
            if(live_assignment_ && editor_ && editor_->RestoreInstrumentAt(instrument_entry_.source,instrument_entry_.original))serial_.SyncEditor(*editor_,now);
            return;
        }
        if(serial_.InstrumentVisitActive()){FinishInstrumentVisit();return;}
        definition_return_=replacement_return_=replace_prompt_=false;definition_draft_={};sample_path_.clear();draft_slot_=0;
        if(ScreenPending())serial_.CancelQueuedScreenSave();screen_phase_=ScreenPhase::Idle;instrument_screen_=false;
        if(editor_)editor_->RestoreCursor(instrument_entry_.source);
        instrument_entry_pattern_.reset();
        screen_status_="Instrument cancelled; cell unchanged";return;
    }
    if(action==BringUpAction::Exit) {
        screen_phase_=ScreenPhase::Idle;
        if(!exit_after_stop_) {exit_after_stop_=true;stop_pending_=restart_pending_=false;serial_.BeginExit(now);}
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
            if(StageLiveSample(replacement_candidate_,now)){replace_prompt_=wav_picker_=false;return;}
            sample_path_=replacement_candidate_;definition_draft_.sample=sample_path_.c_str();definition_draft_.explicit_type=true;draft_slot_=instrument_number_;
            replace_prompt_=wav_picker_=false;screen_status_="Replacement staged; confirm return to prepare before replacing";return;
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(wav_picker_) {
        if(action==BringUpAction::BrowserCancel || action==BringUpAction::BrowserBack || action==BringUpAction::ScreenRight || action==BringUpAction::ScreenLeft) {
            wav_picker_=false;screen_status_="WAV selection cancelled; previous draft retained";return;
        }
        if(!serial_.UserActionsBlocked() && !wav_browser_.Loading()) {
            if(action==BringUpAction::BrowserUp)wav_browser_.Move(-1);
            if(action==BringUpAction::BrowserDown)wav_browser_.Move(1);
            if(action==BringUpAction::BrowserConfirm) {
                std::string path;const auto selection=wav_browser_.Select(path);
                if(selection==1)OpenWavDirectory(path,false,now);
                if(selection==2) {
                    const auto saved=SavedInstrumentPath();
                    if(!saved.empty() && saved!=path) {replacement_candidate_=path;replace_prompt_=true;screen_status_="Confirm Sample WAV replacement";return;}
                    if(StageLiveSample(path,now)){wav_picker_=false;return;}
                    sample_path_=path;definition_draft_.sample=sample_path_.c_str();definition_draft_.explicit_type=true;draft_slot_=instrument_number_;
                    wav_picker_=false;screen_status_="WAV selected; confirm return to prepare and define, or cancel";
                }
            }
        }
        if(action!=BringUpAction::ResetStop)return;
    }
    if(instrument_screen_) {
        if((assignment_confirm_ || assignment_rollback_ || visit_closing_) && action!=BringUpAction::ResetStop)return;
        if(action==BringUpAction::InstrumentPreviousField || action==BringUpAction::InstrumentNextField) {
            if(!definition_return_) {
                const bool load=InstrumentLoadAvailable();
                if(action==BringUpAction::InstrumentNextField)instrument_field_=instrument_field_==2?0:instrument_field_==0 && load?1:2;
                else instrument_field_=instrument_field_==0?2:instrument_field_==1?0:load?1:0;
            }
            return;
        }
        if(action==BringUpAction::InstrumentActivate) {
            if(InstrumentFocus()==1)action=BringUpAction::BrowserConfirm;
            else return;
        }
        if(action==BringUpAction::InstrumentIncrease || action==BringUpAction::InstrumentDecrease) {
            if(serial_.UserActionsBlocked() || !editor_ || editor_->Locked()) {
                screen_status_="Type draft unavailable: synchronize/recover first";return;
            }
            if(InstrumentFocus()==2) {
                if(definition_return_ || !serial_.LivePatternEditingAvailable() || !serial_.InstrumentCollectionKnown()) {
                    screen_status_="Slot selection unavailable: connect and synchronize live Pattern editing";return;
                }
                if(InstrumentDraftChanged()) {screen_status_="Slot selection blocked: Type/WAV draft retained; cancel or finish it first";return;}
                if(!LiveTypesSettled()) {screen_status_="Slot selection waits for the live Type acknowledgement";return;}
                const auto count=std::min(99u,serial_.InstrumentCollectionSize());
                if(!count) {screen_status_="No defined instrument slots available";return;}
                const auto next=action==BringUpAction::InstrumentIncrease?std::min(count,instrument_number_+1):std::min(count,instrument_number_>1?instrument_number_-1:1);
                if(next==instrument_number_)return;
                const auto cursor=editor_->Cursor();
                if(!editor_->AssignInstrumentAt(instrument_entry_.source,next-1)) {screen_status_="Slot assignment blocked; synchronize first";return;}
                editor_->RestoreCursor(cursor);
                instrument_number_=next;live_assignment_=true;
                definition_draft_={};sample_path_.clear();draft_slot_=0;
                serial_.SyncEditor(*editor_,now);serial_.InspectInstrument(instrument_number_);
                screen_status_="Live assignment queued; confirm keeps it, Escape restores the original";return;
            }
            if(InstrumentFocus()!=0 || !InstrumentTypeEditable() || definition_return_)return;
            const auto type=InstrumentDraftTypeCode();
            const auto next=action==BringUpAction::InstrumentIncrease?type%3+1:(type+1)%3+1;
            const auto slot=instrument_number_-1;
            if(serial_.LiveEditSupported() && instrument_number_<=serial_.InstrumentCollectionSize() && CaptureLiveDefinition()) {
                desired_types_[slot]=next;
                desired_resources_[slot]=next==1 && original_types_[slot]==1?original_resources_[slot]:std::string{};
                if(live_type_inflight_)serial_.CancelLiveDefinition();
                live_type_failed_=false;definition_draft_={};draft_slot_=0;sample_path_.clear();TickLiveTypes(now);return;
            }
            if(serial_.LiveTypeSupported() && instrument_number_<=serial_.InstrumentCollectionSize() &&
                (type==2 || type==3) && (next==2 || next==3) && !InstrumentDraftChanged()) {
                if(!original_types_[slot])original_types_[slot]=applied_types_[slot]=serial_.InspectedType(instrument_number_);
                desired_types_[slot]=next;
                definition_draft_={};draft_slot_=0;sample_path_.clear();
                TickLiveTypes(now);return;
            }
            if(original_types_[slot]) {screen_status_="Sample conversion is outside live MIDI/Synth editing; Escape restores the visit";return;}
            draft_slot_=instrument_number_;definition_draft_.type=next;
            definition_draft_.explicit_type=true;definition_draft_.sample=definition_draft_.type==1 && !sample_path_.empty()?sample_path_.c_str():nullptr;
            screen_status_=serial_.MidiRecoveryStopped()?"Type draft changed; confirm return or cancel":"Type draft changed locally; STOP before applying, or Escape to discard";return;
        }

        if(action==BringUpAction::BrowserConfirm) {
            if(InstrumentLoadAvailable() && !serial_.PatternListBusy()) {
                wav_picker_=true;wav_browser_.Reset();OpenWavDirectory(BroTracker::kWavDirectory,true,now);screen_status_="Sample Load";
            } else screen_status_=!serial_.MidiRecoveryStopped()?"WAV Load requires STOP; local Type draft retained":"Sample Load unavailable: select Sample in the trailing slot and synchronize";
            return;
        }
        if(definition_return_ && action!=BringUpAction::ResetStop)return;

        if(action==BringUpAction::ScreenDown) {
            const auto limit=serial_.InstrumentCollectionLimit();
            BroTracker::HostLogf(log_,BroTracker::HostLogLevel::Debug,"Instrument confirm",
                "slot=%u count=%u limit=%u focus=%d draft_type=%u saved_type=%u blocked=%u file=%u publication=%u transport=%u editor=%u locked=%u",
                instrument_number_,serial_.InstrumentCollectionSize(),limit,InstrumentFocus(),InstrumentDraftTypeCode(),
                serial_.InspectedType(instrument_number_),unsigned(serial_.UserActionsBlocked()),unsigned(serial_.FilePending()),
                unsigned(serial_.PublicationBusy()),unsigned(serial_.TransportPending()),unsigned(editor_!=nullptr),unsigned(editor_ && editor_->Locked()));
            if(!limit || instrument_number_>limit) {
                screen_status_=!limit?"Return unavailable: waiting for instrument collection; cancel is available":"Return unavailable: select a numbered slot or cancel";
                return;
            }

            if(serial_.UserActionsBlocked() || serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending() ||
                !editor_ || editor_->Locked()) {
                screen_status_="Return unavailable: synchronize/recover before instrument assignment";return;
            }
            const bool existing=!serial_.InstrumentCollectionSupported() || instrument_number_<=serial_.InstrumentCollectionSize();
            const auto type=InstrumentDraftTypeCode();
            const auto path=!sample_path_.empty()?sample_path_:type==1 && serial_.InspectedSample(instrument_number_)?SavedInstrumentPath():std::string{};
            const bool changed=draft_slot_==instrument_number_ && ((definition_draft_.explicit_type && type!=serial_.InspectedType(instrument_number_)) || !sample_path_.empty());
            if(live_assignment_ || HasLiveTypes()) {
                if(HasLiveTypes() && desired_types_[instrument_number_-1]==1 && desired_resources_[instrument_number_-1].empty()) {
                    screen_status_="Choose a WAV with Load before confirming, or Escape to discard";return;
                }
                if(changed) {screen_status_="Type/WAV draft is not applied; discard it or cancel before returning";return;}
                assignment_confirm_=true;serial_.SyncEditor(*editor_,now);
                screen_status_="Keeping live assignment; waiting for device application";return;
            }
            const auto* pattern=editor_->Draft();
            const auto source=instrument_entry_.source;
            const bool assignment_unchanged=pattern && source.row<pattern->active_rows && source.channel<pattern->active_channels &&
                instrument_entry_.original==instrument_number_-1 && pattern->cells[source.row][source.channel].instrument==instrument_entry_.original;
            // The raw reference must match: an inherited/default selection is
            // still an assignment change when the source cell contains 0xff.
            if(existing && !changed && assignment_unchanged) {
                FinishInstrumentVisit();return;
            }
            if(!serial_.MidiRecoveryStopped()) {
                screen_status_="Pending instrument or assignment change requires STOP; draft retained, Escape discards";return;
            }
            BroTracker::HostLogf(log_,BroTracker::HostLogLevel::Debug,"Instrument confirm",
                "branch=%s type=%u explicit=%u path=%u",existing?(changed?"replace":"assign"):"define-or-assign",type,
                unsigned(definition_draft_.explicit_type),unsigned(!path.empty()));
            if(existing && changed && type==1 && path.empty()) {screen_status_="No sample selected; use Load WAV before confirming";return;}
            if(existing && changed) {
                const bool accepted=serial_.ReplaceSupported()?serial_.ReplaceInstrument(instrument_number_,type,type==1?path.c_str():nullptr,now):
                    type==1 && serial_.RelinkInstrument(instrument_number_,path.c_str(),now);
                if(accepted) {definition_return_=replacement_return_=true;screen_status_="Preparing instrument update on Teensy...";}
                else screen_status_=serial_.InstrumentCommitError();return;
            }
            if(!existing && !(type==1 && path.empty())) {
                definition_draft_.sample=type==1?sample_path_.c_str():nullptr;
                if(!definition_draft_.Qualifies()){screen_status_="Choose a Type before confirming";return;}
                if(serial_.CommitInstrument(type,definition_draft_.sample,now)){definition_return_=true;screen_status_="Committing definition on Teensy...";}
                else screen_status_=serial_.InstrumentCommitError();return;
            }
            if(!editor_->AssignInstrumentAt(instrument_entry_.source,instrument_number_-1)) {screen_status_="Assignment blocked; cancel or synchronize";return;}
            serial_.SyncEditor(*editor_,now);
            instrument_screen_=false;screen_status_.clear();
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
            if(!serial_.Playing() && !serial_.LiveEditSupported() && !serial_.SessionSnapshotAvailable()) {screen_status_="Instrument unavailable: update session-snapshot firmware";return;}
        } // One transition, regardless of repeated input.
        if(serial_.FilePending() || serial_.PublicationBusy() || serial_.TransportPending()) {
            screen_status_="Pattern Load/Save unavailable: wait for current operation";return;
        }
        instrument_entry_pattern_.reset();
        live_assignment_=assignment_confirm_=assignment_rollback_=false;
        original_types_.fill(0);desired_types_.fill(0);applied_types_.fill(0);live_type_inflight_=live_type_failed_=false;
        if(instrument_destination_ && (serial_.Playing() || serial_.LiveEditSupported())) {
            // Capture before exposing the screen. No SD request or transport command:
            // the retained editor owns undo, and this copy is only the entry baseline.
            instrument_entry_pattern_=*editor_->Draft();
            instrument_number_=instrument_entry_.number;instrument_field_=0;
            definition_draft_={};sample_path_.clear();draft_slot_=0;
            definition_return_=replacement_return_=replace_prompt_=false;
            instrument_screen_=true;
            if(serial_.LiveEditSupported())serial_.BeginInstrumentVisit(now);
            screen_status_=serial_.LiveEditSupported()?"Entry captured in BTX RAM | Live slot, Type and WAV editing":serial_.LiveTypeSupported()?"Entry captured in BTX RAM | Live slot and MIDI/Synth Type editing":
                "Entry captured in BTX RAM | Live slot editing; live Type requires LIVETYPECAP firmware";
            return;
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
    if(visit_closing_ && !serial_.InstrumentVisitActive())FinishInstrumentVisit();
    if(HasLiveTypes() && !visit_closing_)TickLiveTypes(now);
    if((assignment_confirm_ || assignment_rollback_) && !visit_closing_ && editor_) {
        if(live_assignment_ && assignment_rollback_ && !editor_->RestoreInstrumentAt(instrument_entry_.source,instrument_entry_.original))
            screen_status_="Rollback pending: synchronize/recover before restoring the original assignment";
        else {
            serial_.SyncEditor(*editor_,now);
            if(LiveTypesSettled() && (!live_assignment_ || (editor_->Draft() && serial_.WorkingPatternApplied(*editor_->Draft()))))FinishInstrumentVisit();
            else if(LiveTypesSettled())screen_status_=std::string(assignment_rollback_?"Rollback pending: ":"Assignment confirmation pending: ")+serial_.WorkingPatternStatus();
        }
    }
    if(instrument_screen_)serial_.InspectInstrument(instrument_number_);
    if(instrument_screen_ && instrument_field_==1 && !wav_picker_ && !InstrumentLoadAvailable())instrument_field_=0;
    if(instrument_screen_ && serial_.UserActionsBlocked())
        screen_status_="Return unavailable: synchronize/recover before instrument assignment";
    if(definition_return_ && !serial_.InstrumentCommitPending()) {
        definition_return_=false;
        if(!serial_.InstrumentCommitSucceeded()) {screen_status_=serial_.InstrumentCommitError();replacement_return_=false;}
        else if(replacement_return_) {replacement_return_=false;instrument_screen_=false;if(editor_)editor_->RestoreCursor(instrument_entry_.source);sample_path_.clear();definition_draft_={};draft_slot_=0;screen_status_="Instrument updated; Pattern references unchanged";}
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
    if(wav_picker_ && wav_collecting_ && !serial_.PatternListBusy()) {
        if(!serial_.PatternListReady()) {wav_collecting_=false;wav_browser_.Fail(serial_.WavDirectoryError());}
        else {
            const auto& page=serial_.WavDirectoryPage();
            if(!wav_browser_.Accept(page)) {wav_collecting_=false;wav_browser_.Fail(BroTracker::WavDirectoryResult::ReadError);}
            else if(page.done)wav_collecting_=false;
            else if(!serial_.RequestWavDirectory(page.directory,page.next,false,now)) {wav_collecting_=false;wav_browser_.Fail(BroTracker::WavDirectoryResult::Busy);}
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
