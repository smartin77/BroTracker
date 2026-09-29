#!/bin/bash

launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
app_dir="$launcher_dir/brotracker"

log=/tmp/brotracker-arkos.log
: > "$log" || exit 1
exec >> "$log" 2>&1
printf 'Launcher: starting BroTracker UI and audio\n'
ui_pid=
audio_pid=
cleanup() {
    result=$?
    trap - EXIT INT TERM HUP
    # The supervisor owns/reaps the bridge and its retry sleep.
    if [[ -n "$audio_pid" ]]; then
        kill -TERM "$audio_pid" 2>/dev/null || true
        wait "$audio_pid" 2>/dev/null || true
    fi
    if [[ -n "$ui_pid" ]]; then
        kill -TERM "$ui_pid" 2>/dev/null || true
        wait "$ui_pid" 2>/dev/null || true
    fi
    printf 'Launcher: children reaped; UI/launcher status=%s\n' "$result"
    if type pm_finish >/dev/null 2>&1; then pm_finish; fi
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

audio_supervisor() {
    trap - EXIT INT TERM HUP
    bridge_pid=
    delay_pid=
    stop_audio() {
        trap - INT TERM HUP
        if [[ -n "$bridge_pid" ]]; then
            kill -TERM "$bridge_pid" 2>/dev/null || true
            wait "$bridge_pid" 2>/dev/null || true
        fi
        if [[ -n "$delay_pid" ]]; then
            kill -TERM "$delay_pid" 2>/dev/null || true
            wait "$delay_pid" 2>/dev/null || true
        fi
        printf 'Launcher: audio supervisor stopped and children reaped\n'
        exit 0
    }
    trap stop_audio INT TERM HUP
    if [[ ! -x ./BroTrackerAlsaBridge ]]; then
        printf 'Launcher: audio unavailable: BroTrackerAlsaBridge missing/not executable; UI remains usable\n'
        exit 0
    fi
    for ((attempt=1; attempt<=8; ++attempt)); do
        kill -0 "$ui_pid" 2>/dev/null || exit 0
        printf 'Launcher: audio startup attempt %s/8\n' "$attempt"
        line_count=$(wc -l < "$log")
        ./BroTrackerAlsaBridge --auto >> "$log" 2>&1 &
        bridge_pid=$!
        wait "$bridge_pid"
        bridge_status=$?
        bridge_pid=
        # Existing verified bridge marker, printed after PCM configuration.
        # Be conservative: never restart after this point, even if capture
        # start fails immediately afterwards or the process is killed.
        if tail -n "+$((line_count + 1))" "$log" | grep -q '^Queue capacity:'; then
            printf 'Launcher: configured bridge exited status=%s; no runtime restart (check bridge diagnostics)\n' "$bridge_status"
            exit 0
        fi
        printf 'Launcher: audio initial startup failed status=%s\n' "$bridge_status"
        if ((attempt == 8)); then break; fi
        kill -0 "$ui_pid" 2>/dev/null || exit 0
        sleep 2 &
        delay_pid=$!
        wait "$delay_pid"
        delay_pid=
    done
    printf 'Launcher: audio startup retries exhausted; UI remains usable\n'
}

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
    controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
    controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder="/roms/ports/PortMaster"
fi

if [ -f "$controlfolder/control.txt" ]; then
    source "$controlfolder/control.txt"
    get_controls
    export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
fi

cd -- "$app_dir" || exit 1

if type pm_platform_helper >/dev/null 2>&1; then
    pm_platform_helper "$app_dir/BroTrackerArkOSUI"
fi

# Direct UI runs keep their normal fresh log; only this launch appends.
BROTRACKER_APPEND_LOG=1 ./BroTrackerArkOSUI &
ui_pid=$!
audio_supervisor &
audio_pid=$!
wait "$ui_pid"
EXIT_CODE=$?
ui_pid=
exit "$EXIT_CODE"
