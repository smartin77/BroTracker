#pragma once
#include "bringup_serial.h"
#include "warning_renderer.h"
#include "warning_text.h"
inline void DrawHostWarning(Framebuffer& fb,const BringUpSerial& serial,const WarningRenderer& renderer,const WarningText& text) {
    if(!serial.Connected())renderer.Draw(fb,text.Get(WarningText::TeensyHeading),text.Get(WarningText::TeensyBody));
    else if(serial.SdStatus()==BringUpSerial::SdAvailability::Unavailable)
        renderer.Draw(fb,text.Get(WarningText::SdHeading),text.Get(WarningText::SdBody));
    else if(serial.UserActionsBlocked())
        renderer.Draw(fb,text.Get(WarningText::SyncHeading),text.Get(WarningText::SyncBody));
}
