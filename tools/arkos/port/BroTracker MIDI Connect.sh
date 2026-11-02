#!/bin/bash
# Development ALSA keeper. No BTX, CDC, playback control or network required.
launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
umask 077
log_dir="$HOME/BroTracker"
mkdir -p -- "$log_dir" || exit 1
log="$log_dir/midi-connect-launch.log"
if [[ -t 1 ]]; then
    exec 3>&1
elif [[ -w /dev/tty1 ]]; then
    exec 3>/dev/tty1 || exit 1
else
    printf 'FAILED: no writable handheld console (/dev/tty1); no route changed.\n' | tee "$log"
    exit 1
fi
{
    printf 'BroTracker MIDI Connect (development)\n%s\n' "$(date -Iseconds)"
    if command -v python3 >/dev/null 2>&1; then
        python3 -B "$launcher_dir/brotracker/midi-connect.py"
        result=$?
    else
        echo 'FAILED: Python 3 is missing. Install it during development setup.'
        result=1
    fi
    printf '\nKeeper log: %s/midi-connect.log\nReturning to Ports within 10 seconds; keeper continues independently.\n' "$log_dir"
} > "$log" 2>&1
printf '\033c' >&3
trap 'printf "\033[0m" >&3' EXIT
if [[ "$result" -ne 0 ]]; then
    if [[ -z ${NO_COLOR+x} ]]; then printf '\033[1;31m' >&3; fi
    printf '  Launch: FAILED (exit %s)\n' "$result" >&3
    printf '\033[0m  Details: ~/BroTracker/\n           midi-connect-launch.log\n\n' >&3
fi
if command -v python3 >/dev/null 2>&1; then
    python3 -B "$launcher_dir/brotracker/midi-connect.py" --screen >&3 2>> "$log"
    python3 -B "$launcher_dir/brotracker/midi-connect.py" --wait-buttons >&3 2>> "$log"
else
    printf '\033[1;31m  Status: UNAVAILABLE\033[0m\n\n  Python 3 is missing.\n  Returning in 10 seconds.\n' >&3
    sleep 10
fi
# Do not call pm_finish: this console-only tool needs no platform setup or network.
exit "$result"
