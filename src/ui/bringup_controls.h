#pragma once
#include "bringup_serial.h"
// Temporary host test controls; no engine or final-protocol responsibilities.
enum class BringUpAction { None, Start, Stop, StopExit, Exit, PatternToggle, ResetStop, ScreenLeft, ScreenRight };
class BringUpControls {
public:
    BringUpControls(BringUpSerial& serial, FILE* log) : serial_(serial), log_(log) {}
    void Request(BringUpAction action, const char* source, std::uint32_t now);
    void Tick(std::uint32_t now);
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
