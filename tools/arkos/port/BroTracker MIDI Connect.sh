#!/bin/bash
# Manual development utility. No BTX, playback, background service or network.
launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
umask 077
log_dir="$HOME/BroTracker"
mkdir -p -- "$log_dir" || exit 1
log="$log_dir/midi-connect.log"
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
    printf '\nLog: %s\nReturning to Ports in 8 seconds.\n' "$log"
} > "$log" 2>&1
printf '\033c' >&3
cat -- "$log" >&3
sleep 8
# Do not call pm_finish: this console-only tool needs no platform setup or network.
exit "$result"
