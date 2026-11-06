#pragma once
#include "bringup_serial.h"
#include "file_list.h"
// Temporary host test controls; no engine or final-protocol responsibilities.
enum class BringUpAction { None, Start, Stop, StopExit, Exit, PatternToggle, ResetStop, ScreenLeft, ScreenRight, BrowserUp, BrowserDown, BrowserFirst, BrowserNext, BrowserConfirm, BrowserCancel };
class BringUpControls {
public:
    BringUpControls(BringUpSerial& serial, FILE* log) : serial_(serial), log_(log) {}
    void Request(BringUpAction action, const char* source, std::uint32_t now);
    void Tick(std::uint32_t now);
    void SetConfirmLoadReplace(bool value){confirm_load_replace_=value;}
    bool BrowserInputAvailable()const{return load_save_screen_ && !browser_loading_;}
    bool BrowserLoading()const{return browser_loading_;}
    std::string BrowserText()const;
    FileListView BrowserList()const;
    bool LoadSaveScreen() const {return load_save_screen_;}
    bool ScreenPending() const {return screen_phase_!=ScreenPhase::Idle;}
    bool PatternInputBlocked() const {return ScreenPending() || load_save_screen_;}
    const char* ScreenStatus() const {return screen_status_;}
    bool Quit() const { return quit_; }
    bool StopPending() const { return stop_pending_; }
    bool Exiting() const { return exit_after_stop_; }
    bool RestartPending() const { return restart_pending_; }
private:
    void Log(const char* message);
    void LoadSelected(std::uint32_t now);
    bool confirm_load_replace_=true,browser_confirm_=false,browser_loading_=false,browser_initialized_=false;
    unsigned browser_selected_=0,browser_first_=0;
    std::vector<std::uint32_t> browser_offsets_{0};
    int browser_page_direction_=0;
    bool BrowsePage(int direction,std::uint32_t now);
    enum class ScreenPhase { Idle, Stop, Save };
    ScreenPhase screen_phase_=ScreenPhase::Idle;
    bool load_save_screen_=false;
    const char* screen_status_="";
    std::uint32_t screen_started_=0;
    BringUpSerial& serial_;
    FILE* log_;
    bool quit_ = false, stop_pending_ = false, exit_after_stop_ = false, restart_pending_ = false;
    std::uint32_t stop_started_ = 0;
};
