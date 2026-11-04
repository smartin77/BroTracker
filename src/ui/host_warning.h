#pragma once
#include "bringup_serial.h"
#include "warning_renderer.h"
#include "warning_text.h"
#include "warning_presentation.h"
inline void DrawHostWarning(Framebuffer& fb,const BringUpSerial& serial,const WarningRenderer& renderer,const WarningText& text,
    HostWarningPresentation& presentation,std::uint32_t now) {
    const auto overlay=presentation.Select(serial.Connected(),serial.SdStatus()==BringUpSerial::SdAvailability::Unavailable,now);
    if(overlay==HostWarningPresentation::Overlay::Disconnected)
        renderer.Draw(fb,text.Get(WarningText::TeensyHeading),text.Get(WarningText::TeensyBody));
    else if(overlay==HostWarningPresentation::Overlay::SdUnavailable)
        renderer.Draw(fb,text.Get(WarningText::SdHeading),text.Get(WarningText::SdBody));
    // Connected synchronization/recovery remains in the existing status panel.
    // Overlay visibility never changes the authoritative input/readiness barrier.
}
