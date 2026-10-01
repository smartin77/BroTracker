#!/bin/bash

launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
app_dir="$launcher_dir/brotracker"

log=/tmp/brotracker-arkos.log
: > "$log" || exit 1
exec >> "$log" 2>&1
printf 'Launcher: starting BroTracker Terminal (BTX) and audio\n'
ui_pid=
audio_pid=
cleanup() {
    result=$1
    set +e
    trap - EXIT
    trap "" INT TERM HUP
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
    if type pm_finish >/dev/null 2>&1; then pm_finish || true; fi
    exit "$result"
}

audio_supervisor() {
    trap - EXIT INT TERM HUP
    bridge_pid=
    delay_pid=
    stop_audio() {
        trap "" INT TERM HUP
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
    # Never block in wait while the launcher may have disappeared. /proc
    # also detects a dead (zombie) launcher before its own parent reaps it.
    wait_with_parent() {
        local child=$1 parent_stat
        while kill -0 "$child" 2>/dev/null; do
            parent_stat=
            if ! read -r parent_stat < "/proc/$launcher_pid/stat" 2>/dev/null ||
               [[ "${parent_stat##*) }" == Z* ]]; then
                printf 'Launcher: main launcher disappeared; stopping audio supervisor\n'
                stop_audio
            fi
            sleep 0.2
        done
        wait "$child"
    }
    if [[ ! -x ./BroTrackerAlsaBridge ]]; then
        printf 'Launcher: audio unavailable: BroTrackerAlsaBridge missing/not executable; UI remains usable\n'
        exit 0
    fi
    phase="initial startup"
    attempt=0
    backoff=2
    while kill -0 "$ui_pid" 2>/dev/null; do
        attempt=$((attempt + 1))
        printf 'Launcher: audio %s attempt %s; fresh --auto discovery\n' "$phase" "$attempt"
        line_count=$(wc -l < "$log")
        ./BroTrackerAlsaBridge --auto >> "$log" 2>&1 &
        bridge_pid=$!
        bridge_status=0
        wait_with_parent "$bridge_pid" || bridge_status=$?
        bridge_pid=
        # The bridge closes both PCMs before exit; wait reaps it before the
        # next process re-enumerates numeric endpoints. Never replay START.
        if tail -n "+$((line_count + 1))" "$log" | grep -q '^Queue capacity:'; then
            printf 'Launcher: audio %s configured; bridge exited status=%s; entering runtime recovery\n' "$phase" "$bridge_status"
            phase="runtime recovery"
            attempt=0
            backoff=2
        else
            printf 'Launcher: audio %s failed status=%s; UI remains usable\n' "$phase" "$bridge_status"
        fi
        kill -0 "$ui_pid" 2>/dev/null || break
        printf 'Launcher: audio %s waiting %s seconds before rediscovery\n' "$phase" "$backoff"
        sleep "$backoff" &
        delay_pid=$!
        wait_with_parent "$delay_pid" || true
        delay_pid=
        # Cap the interval, not the number of attempts: a prolonged absence
        # must still recover while BTX is open. Cleanup interrupts this wait.
        ((backoff >= 16)) || backoff=$((backoff * 2))
    done

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
    pm_platform_helper "$app_dir/BroTrackerTerminal"
fi

# PortMaster helpers may replace traps or enable errexit. Establish our
# lifecycle only after all helper setup, before creating any owned children.
set +e
trap - ERR
trap 'cleanup "$?"' EXIT
trap 'cleanup 130' INT
trap 'cleanup 143' TERM
trap 'cleanup 129' HUP
launcher_pid=$BASHPID

# Direct UI runs keep their normal fresh log; only this launch appends.
BROTRACKER_APPEND_LOG=1 ./BroTrackerTerminal &
ui_pid=$!
audio_supervisor &
audio_pid=$!
EXIT_CODE=0
wait "$ui_pid" || EXIT_CODE=$?
ui_pid=
cleanup "$EXIT_CODE"
