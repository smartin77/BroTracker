#!/bin/bash
# Development ALSA keeper. No BTX, CDC, playback control or network required.
launcher_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
umask 077
temp_base=${TMPDIR:-/tmp}
[[ "$temp_base" == /* && -d "$temp_base" ]] || temp_base=/tmp
log_dir="$temp_base/brotracker-$(id -u)"
[[ ! -L "$log_dir" && ( ! -e "$log_dir" || -O "$log_dir" ) ]] || exit 1
mkdir -p -- "$log_dir" || exit 1
chmod 700 -- "$log_dir" || exit 1
log="$log_dir/brotracker-midi-connect-launch.log"
if [[ -t 1 ]]; then
    exec 3>&1
elif [[ -w /dev/tty1 ]]; then
    exec 3>/dev/tty1 || exit 1
else
    printf 'FAILED: no writable handheld console (/dev/tty1); no route changed.\n' | tee "$log"
    exit 1
fi
# Same parenthesized archive naming as BTX; preserve three previous launches.
for n in 3 2 1; do
    from="$log_dir/brotracker-midi-connect-launch($((n-1))).log"
    [[ "$n" != 1 ]] || from="$log"
    [[ ! -f "$from" ]] || mv -- "$from" "$log_dir/brotracker-midi-connect-launch($n).log" || exit 1
done
{
    printf 'BroTracker MIDI Connect (development)\n%s\n' "$(date -Iseconds)"
    if command -v python3 >/dev/null 2>&1; then
        python3 -B "$launcher_dir/brotracker/midi-connect.py"
        result=$?
    else
        echo 'FAILED: Python 3 is missing. Install it during development setup.'
        result=1
    fi
    printf '\nKeeper log: %s/brotracker-midi-connect.log\nPorts cleanup may stop the keeper; subscription and delivery must be checked in BTX.\n' "$log_dir"
} > "$log" 2>&1
printf '\033c' >&3
trap 'printf "\033[0m" >&3' EXIT
if [[ "$result" -ne 0 ]]; then
    if [[ -z ${NO_COLOR+x} ]]; then printf '\033[1;31m' >&3; fi
    printf '  Launch: FAILED (exit %s)\n' "$result" >&3
    printf '\033[0m  Log: %s/\n  brotracker-midi-connect-launch.log\n\n' "$log_dir" >&3
fi
if command -v python3 >/dev/null 2>&1; then
    python3 -B "$launcher_dir/brotracker/midi-connect.py" --screen --wait-buttons >&3 2>> "$log"
else
    printf '\033[1;31m  Status: UNAVAILABLE\033[0m\n\n  Python 3 is missing.\n  Returning in 10 seconds.\n' >&3
    sleep 10
fi
# Do not call pm_finish: this console-only tool needs no platform setup or network.
exit "$result"
