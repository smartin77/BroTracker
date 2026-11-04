#include "test_framework.h"
#include "ui/warning_presentation.h"
using Overlay=HostWarningPresentation::Overlay;
TEST_CASE(InitialDiscoveryGraceExpiresAtFiveSecondsWithoutBlocking) {
    HostWarningPresentation view(100);
    CHECK(view.Select(false,false,100)==Overlay::None);
    CHECK(view.Select(false,false,5099)==Overlay::None);
    CHECK(view.Select(false,false,5100)==Overlay::Disconnected);
    CHECK(view.Select(false,false,7000)==Overlay::Disconnected);
}
TEST_CASE(ConnectionWithinGraceNeverFlashesDisconnectedAndLaterLossIsImmediate) {
    HostWarningPresentation view(0);
    CHECK(view.Select(false,false,4998)==Overlay::None);
    CHECK(view.Select(true,false,4999)==Overlay::None);
    CHECK(view.Select(true,false,5000)==Overlay::None);
    CHECK(view.Select(false,false,5001)==Overlay::Disconnected);
    HostWarningPresentation early(0);
    early.ObserveConnection(true); // Connection observed during an idle-service pass.
    CHECK(early.Select(false,false,100)==Overlay::Disconnected);
    CHECK(early.Select(true,false,101)==Overlay::None);
    CHECK(early.Select(false,false,102)==Overlay::Disconnected);
}
TEST_CASE(ConnectedSynchronizationHasNoOverlayAndSdWarningIsNeverDelayed) {
    HostWarningPresentation view(0);
    // Selection deliberately has no user-readiness input: connected sync is status-only.
    CHECK(view.Select(true,false,1)==Overlay::None);
    CHECK(view.Select(true,false,9000)==Overlay::None);
    CHECK(view.Select(true,true,9001)==Overlay::SdUnavailable);
    CHECK(view.Select(false,true,9002)==Overlay::Disconnected); // Stale SD cannot win.
    HostWarningPresentation early(0);
    CHECK(early.Select(true,true,1)==Overlay::SdUnavailable);
}
TEST_CASE(StartupGraceHandlesMillisecondCounterWrap) {
    const std::uint32_t start=UINT32_MAX-1000;
    HostWarningPresentation view(start);
    CHECK(view.Select(false,false,start+4999u)==Overlay::None);
    CHECK(view.Select(false,false,start+5000u)==Overlay::Disconnected);
}
