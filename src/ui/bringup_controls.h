#pragma once
#include "bringup_serial.h"
#include "file_list.h"
#include "wav_browser.h"
#include "instrument_navigation.h"
#include <array>
// Temporary host test controls; no engine or final-protocol responsibilities.
enum class BringUpAction { None, Start, Stop, StopExit, Exit, PatternToggle, ResetStop, ScreenLeft, ScreenRight, ScreenUp, ScreenDown, BrowserUp, BrowserDown, BrowserAudition, BrowserConfirm, BrowserCancel, BrowserBack, InstrumentCancel, InstrumentSample, InstrumentMidi, InstrumentSynth, InstrumentTypeNext, InstrumentPreviousField, InstrumentNextField, InstrumentIncrease, InstrumentDecrease, InstrumentActivate };
class BringUpControls {
public:
    BringUpControls(BringUpSerial& serial, FILE* log) : serial_(serial), log_(log) {}
    void Request(BringUpAction action, const char* source, std::uint32_t now);
    bool FileListNavigationAvailable()const;
    void NavigateFileList(std::int64_t steps);
    void ClickFileList(int x,int y,bool confirm,std::uint32_t now);
    void ObserveEditor(LocalPatternEditor& editor) { editor_=&editor; }
    bool InstrumentCancellationAvailable()const{return (instrument_screen_ && !wav_picker_) || (ScreenPending() && instrument_destination_);}
    bool InstrumentScreen()const{return instrument_screen_;}
    const BroTracker::RealtimePattern* InstrumentEntryPattern()const{return instrument_entry_pattern_?&*instrument_entry_pattern_:nullptr;}
    bool InstrumentVisitModified()const;
    unsigned OriginalInstrumentNumber()const{return instrument_entry_.number;}
    unsigned InstrumentDraftTypeCode()const;
    std::string InstrumentDraftType()const;
    bool InstrumentTypeEditable()const;
    unsigned InstrumentField()const{return instrument_field_;}
    int InstrumentFocus()const{return instrument_field_==2?2:InstrumentTypeEditable()?int(instrument_field_):serial_.InspectedSample(instrument_number_)?1:-1;}
    bool ReplacementConfirming()const{return replace_prompt_;}
    std::string InstrumentLoadWarning()const;
    std::string SavedInstrumentPath()const;
    std::string InstrumentAssignedResource()const;
    unsigned InstrumentNumber()const{return instrument_number_;}
    void Tick(std::uint32_t now);
    void SetConfirmLoadReplace(bool value){confirm_load_replace_=value;}
    std::string WavDirectory()const{return wav_browser_.Directory();}
    bool WavPicker()const{return wav_picker_;}
    bool InstrumentLoadAvailable()const;
    std::string InstrumentDraftResource()const{
        if(instrument_number_ && instrument_number_<=99 && original_types_[instrument_number_-1] && desired_types_[instrument_number_-1]==1 && desired_resources_[instrument_number_-1]!=applied_resources_[instrument_number_-1])return desired_resources_[instrument_number_-1];
        return instrument_number_==draft_slot_ && definition_draft_.type==1?sample_path_:std::string{};
    }
    bool BrowserInputAvailable()const{return (wav_picker_ || InstrumentLoadAvailable() || load_save_screen_) && !browser_loading_ && !browser_saving_;}
    bool RecoveryScreen()const{return recovery_screen_;}
    bool BrowserConfirming()const{return browser_confirm_ || recovery_confirm_;}
    bool BrowserLoading()const{return browser_loading_ || browser_saving_;}
    std::string BrowserText()const;
    FileListView BrowserList()const;
    bool LoadSaveScreen() const {return load_save_screen_ || wav_picker_;}
    bool ScreenPending() const {return screen_phase_!=ScreenPhase::Idle;}
    bool PatternInputBlocked() const {return exit_after_stop_ || ScreenPending() || load_save_screen_ || instrument_screen_;}
    const char* ScreenStatus() const {return screen_status_.c_str();}
    const std::string& ExitStatus()const{return serial_.ExitStatus();}
    bool Quit() const { return quit_; }
    bool StopPending() const { return stop_pending_; }
    bool Exiting() const { return exit_after_stop_; }
    bool RestartPending() const { return restart_pending_; }
private:
    void Log(const char* message);
    void FinishInstrumentVisit();
    void CloseInstrumentView();
    bool InstrumentDraftChanged()const;
    bool HasLiveTypes()const;
    bool LiveTypesSettled()const;
    void TickLiveTypes(std::uint32_t now);
    bool CaptureLiveDefinition();
    bool StageLiveSample(const std::string& path,std::uint32_t now);
    void OpenSampleBrowser(std::uint32_t now);
    void OpenWavDirectory(const std::string& path,std::uint32_t now);
    WavBrowser wav_browser_;bool wav_collecting_=false;
    std::string wav_visit_directory_;
    std::size_t wav_next_startup_root_=0;
    void LoadSelected(std::uint32_t now);
    bool confirm_load_replace_=true,browser_confirm_=false,browser_loading_=false,browser_initialized_=false;
    FileListView browser_list_;
    bool recovery_screen_=false,recovery_confirm_=false;
    bool browser_menu_=true,browser_saving_=false;
    unsigned browser_action_=0;
    bool browser_collecting_=false,browser_ready_=false;
    enum class ScreenPhase { Idle, Stop, Save };
    ScreenPhase screen_phase_=ScreenPhase::Idle;
    LocalPatternEditor* editor_=nullptr;
    InstrumentEntry instrument_entry_{};
    // Host-only entry copy. Editor history stays in LocalPatternEditor.
    std::optional<BroTracker::RealtimePattern> instrument_entry_pattern_;
    bool live_assignment_=false,assignment_confirm_=false,assignment_rollback_=false;
    // MIDI/Synth definitions have no Sample resource. First-touch originals,
    // latest desired values, and acknowledged values stay separate for rollback.
    std::array<unsigned,99> original_types_{},desired_types_{},applied_types_{};
    std::array<std::string,99> original_resources_{},desired_resources_{},applied_resources_{};
    std::string live_resource_sent_;
    bool visit_closing_=false;
    bool live_type_inflight_=false,live_type_failed_=false;
    unsigned live_type_slot_=0,live_type_sent_=0;
    bool instrument_destination_=false,instrument_screen_=false;
    unsigned instrument_number_=1,draft_slot_=0;BroTracker::InstrumentDefinitionDraft definition_draft_{};bool definition_return_=false;
    unsigned instrument_field_=0;bool replacement_return_=false,replace_prompt_=false;std::string replacement_candidate_;
    bool wav_picker_=false;std::string sample_path_;
    bool load_save_screen_=false;
    std::string screen_status_;
    std::string audition_feedback_;
    std::uint32_t screen_started_=0;
    BringUpSerial& serial_;
    FILE* log_;
    bool quit_ = false, stop_pending_ = false, exit_after_stop_ = false, restart_pending_ = false;
    std::uint32_t stop_started_ = 0;
};
