#pragma once
// Service serial during the existing 16 ms UI idle span. No extra editor-repeat
// ticks or rendering; fixed passes, no elapsed-time catch-up and no I/O waits.
constexpr unsigned kWindowsSerialServiceSliceMs=2;
constexpr unsigned kWindowsSerialIdleServicePasses=8;
static_assert(kWindowsSerialServiceSliceMs*kWindowsSerialIdleServicePasses==16,
    "Keep the existing UI idle span");
