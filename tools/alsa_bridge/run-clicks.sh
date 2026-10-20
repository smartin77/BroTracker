#!/usr/bin/env bash
# Standalone bridge evidence session; no UI/serial commands, no deployment.
set -euo pipefail
if (( $# < 4 )) || [[ $1 != on && $1 != off ]]; then
    echo "Usage: $0 on|off BINARY EVIDENCE_BASE WORKLOAD.json [--auto | CAPTURE PLAYBACK]" >&2
    exit 2
fi
mode=$1; binary=$(realpath -e -- "$2"); base=$3; workload=$(realpath -e -- "$4"); shift 4
(( $# )) || set -- --auto
umask 077
mkdir -p -- "$base"
base=$(realpath -e -- "$base")
run=$(mktemp -d "$base/$(date -u +%Y%m%dT%H%M%SZ)-$mode-XXXXXX")
printf 'Evidence run: %s\n' "$run"
cp -- "$workload" "$run/workload.json"
printf '%s\n' "$mode" > "$run/diagnostics-mode"
printf '%s\n' "$binary" "$@" > "$run/command.txt"
{
    date -u -Iseconds
    uname -a
    cat /etc/os-release
    printf '\nALSA cards:\n'; cat /proc/asound/cards || true
    printf '\nUSB topology:\n'; if command -v lsusb >/dev/null; then lsusb -t; lsusb; fi
    printf '\nVersions / identity:\n'; sha256sum -- "$binary" "${BASH_SOURCE[0]}"
    for source in main.cpp click_diagnostics.h analyze_clicks.py; do
        sha256sum -- "$(dirname -- "${BASH_SOURCE[0]}")/$source" || true
    done
    git -C "$(dirname -- "${BASH_SOURCE[0]}")/../.." rev-parse HEAD || true
    ldd "$binary" || true
    printf '\nProcess inventory before standalone launch:\n'; ps -eo pid,ppid,comm,args
} > "$run/system.txt" 2>&1
# Baseline must disable both new paired diagnostics and the old WAV recorder.
unset BROTRACKER_DIAGNOSTICS_DIR BROTRACKER_BRIDGE_CLICKS_DIR
if [[ $mode == on ]]; then export BROTRACKER_BRIDGE_CLICKS_DIR="$run"; fi
child=
finish() {
    trap '' INT TERM HUP
    if [[ -n $child ]]; then
        kill -TERM "$child" 2>/dev/null || true
        status=0; wait "$child" || status=$?
        printf '%s\n' "$status" > "$run/exit-status"
    fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
"$binary" "$@" > "$run/bridge.log" 2>&1 &
child=$!
printf '%s\n' "$child" > "$run/bridge.pid"
status=0; wait "$child" || status=$?
child=
printf '%s\n' "$status" > "$run/exit-status"
exit "$status"
