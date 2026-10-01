#pragma once
#include <cstdio>
#include <memory>

// Owns WASAPI discovery/I/O and joins before the terminal closes its shared log.
// Does not communicate with firmware or alter Windows device settings.
class WindowsAudioBridge {
public:
    explicit WindowsAudioBridge(FILE* log);
    ~WindowsAudioBridge();
    void PollDiagnostics(); // SDL thread: counters only, never audio I/O.
    void Stop();
    WindowsAudioBridge(const WindowsAudioBridge&) = delete;
    WindowsAudioBridge& operator=(const WindowsAudioBridge&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
