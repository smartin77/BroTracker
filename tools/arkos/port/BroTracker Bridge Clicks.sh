#!/bin/bash
# Copy this entry to Ports; set repo.path next to it during online preparation.
set -uo pipefail
launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
pathfile="$launcher_dir/BroTracker Bridge Clicks.repo.path"
if [[ ! -r "$pathfile" ]]; then
    printf 'FAILED: missing %s\nSee tools/alsa_bridge/README.md\n' "$pathfile"
    sleep 3
    exit 1
fi
IFS= read -r repo < "$pathfile"
if [[ "$repo" != /* || ! -f "$repo/tools/alsa_bridge/offline_runner.py" ]]; then
    echo 'FAILED: repo.path must name the absolute transferred checkout directory'
    sleep 3
    exit 1
fi
if ! command -v python3 >/dev/null; then
    echo 'FAILED: Python 3.7+ is required; install during online preparation'
    sleep 3
    exit 1
fi
export XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
# Use the same PortMaster platform/controller setup as the normal launcher.
# PortMaster initialization and helpers may read unset variables; keep nounset off.
set +u
for controlfolder in /opt/system/Tools/PortMaster /opt/tools/PortMaster "$XDG_DATA_HOME/PortMaster" /roms/ports/PortMaster; do
    if [[ -f "$controlfolder/control.txt" ]]; then
        source "$controlfolder/control.txt"
        get_controls
        export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-}"
        break
    fi
done
if type pm_platform_helper >/dev/null 2>&1; then
    pm_platform_helper "$repo/deploy/arkos/BroTrackerTerminal"
fi
set +e
printf 'PREPARING offline bridge-click experiment; no Wi-Fi or hub required.\n'
usb_args=()
if [[ -r "$launcher_dir/BroTracker Bridge Clicks.usbmon" ]]; then
    IFS= read -r usb_option < "$launcher_dir/BroTracker Bridge Clicks.usbmon"
    if [[ "$usb_option" == enabled ]]; then usb_args=(--usbmon); fi
    if [[ "$usb_option" == read-integrity ]]; then usb_args=(--usbmon --read-integrity); fi
    if [[ "$usb_option" == capture-holdback ]]; then usb_args=(--usbmon --read-integrity --capture-holdback); fi
fi
python3 -B "$repo/tools/alsa_bridge/offline_runner.py" --repo "$repo" "${usb_args[@]}"
code=$?
printf 'Diagnostics finished, status=%s. Results remain in %s/assets/hardware_diagnostics/bridge_clicks/\n' "$code" "$repo"
# Do not invoke the external pm_finish return hook: it may wait for network.
# Exiting this owned Ports entry returns directly; no network or input required.
exit "$code"
