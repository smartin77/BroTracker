#pragma once

namespace BroTracker
{
    // Initialize diagnostics and SD card infrastructure
    bool DiagnosticsInitialize();

    // Log a message to the initialization log (for startup diagnostics)
    bool DiagnosticLog(const char* message);

    // Blink the LED a specified number of times
    void DiagnosticBlink(unsigned int count);

    // Tool-specific logging support
    // Finds or creates the next sequential log file for a tool
    // File format: BroTracker/<tool_name>-NNNN.log (e.g., audio_timing_benchmark-0001.log)
    // Returns an opaque handle; caller must pass to ToolLogMessage and CloseToolLogFile
    // Returns nullptr if the file cannot be opened
    void* OpenToolLogFile(const char* tool_name);

    // Helper to close a tool log file
    void CloseToolLogFile(void* log_file_handle);

    // Log a message to a tool-specific log file with timestamp
    bool ToolLogMessage(void* log_file_handle, const char* message);
}