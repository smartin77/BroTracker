#!/usr/bin/env bash
# Disposable launcher/tools, no real hardware or installed Ports writes.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
log=/tmp/brotracker-arkos.log
pid=
[[ ! -f "$log" ]] || cp "$log" "$work/old-log"
cleanup() {
    [[ -z "$pid" ]] || { kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
    if [[ -f "$work/old-log" ]]; then cp "$work/old-log" "$log"; else rm -f "$log"; fi
    rm -rf "$work"
}
trap cleanup EXIT
mkdir -p "$work/ports/brotracker" "$work/bin" "$work/home" "$work/sys" "$work/proc" "$work/dev"
mkdir -p "$work/sys/bus/usb/devices/fixture"
printf '16c0\n' > "$work/sys/bus/usb/devices/fixture/idVendor"
printf '048a\n' > "$work/sys/bus/usb/devices/fixture/idProduct"
printf '480\n' > "$work/sys/bus/usb/devices/fixture/speed"
mkdir -p "$work/proc/asound/card1/pcm0c/sub0"
printf 'Capture: Running, Momentary freq = 44100\n' > "$work/proc/asound/card1/stream0"
printf 'state: RUNNING\n' > "$work/proc/asound/card1/pcm0c/sub0/status"
printf 'rate: 44100\n' > "$work/proc/asound/card1/pcm0c/sub0/hw_params"
cp "$repo/tools/arkos/port/BroTracker Diagnostics.sh" "$work/ports/"
# Fixture roots avoid collecting the development host's devices.
sed -i "s|/sys/|$work/sys/|g; s|/proc/asound|$work/proc/asound|g; s|/dev/ttyACM|$work/dev/ttyACM|g; s|/dev/snd|$work/dev/snd|g; s/sleep 1 /sleep 0.1 /; s/command_report arecord -l/command_report unavailable-diagnostic-test-tool -l/" "$work/ports/BroTracker Diagnostics.sh"
cat > "$work/ports/BroTracker Terminal.sh" <<'STUB'
#!/bin/bash
echo $$ > "$TEST_DIR/normal.pid"
: > /tmp/brotracker-arkos.log
printf 'Queue capacity: fixture\nDiscontinuity: fixture\n' >> /tmp/brotracker-arkos.log
sleep_pid=
trap '[[ -z "$sleep_pid" ]] || { kill "$sleep_pid" 2>/dev/null; wait "$sleep_pid" 2>/dev/null; }; echo "Summary: stopped" >> /tmp/brotracker-arkos.log; exit 143' TERM INT
sleep "${STUB_SECONDS:-0.8}" & sleep_pid=$!
wait "$sleep_pid"
echo 'Summary: complete' >> /tmp/brotracker-arkos.log
exit 7
STUB
for tool in arecord aplay amixer; do
    printf '#!/bin/bash\necho "fixture %s read permission denied" >&2\nexit 1\n' "$tool" > "$work/bin/$tool"
done
cat > "$work/bin/dmesg" <<'STUB'
#!/bin/bash
echo 'early boot USB enumeration fixture'
echo 'subsequent USB/audio fixture'
STUB
chmod +x "$work/bin/"*
export HOME="$work/home" TEST_DIR="$work" PATH="$work/bin:$PATH"
run_case() {
    bash "$work/ports/BroTracker Diagnostics.sh" & pid=$!
    if [[ "$1" == signal ]]; then
        sleep 0.3
        printf 'BroTracker USB audio reconnected\n' > "$work/sys/bus/usb/devices/fixture/product"
        sleep 0.8
        live_runs=("$HOME/BroTracker/diagnostics/"*)
        live_run=${live_runs[${#live_runs[@]}-1]}
        [[ -s "$live_run/btx-stream.log" && ! -e "$live_run/exit-status" ]]
        # Network/session hangup must not end collection.
        kill -HUP "$pid"
        kill -0 "$pid"
        kill -TERM "$pid"
        expected=143
    else expected=7; fi
    result=0; wait "$pid" || result=$?
    pid=
    [[ "$result" == "$expected" ]]
    ! kill -0 "$(cat "$work/normal.pid")" 2>/dev/null
}
run_case normal
run_case normal
export STUB_SECONDS=30
run_case signal
runs=("$HOME/BroTracker/diagnostics/"*)
[[ ${#runs[@]} == 3 ]]
for run in "${runs[@]}"; do
    [[ -f "$run/exit-status" && -s "$run/btx-final.log" && -s "$run/btx-stream.log" ]]
    grep -q 'Queue capacity: fixture' "$run/btx-stream.log"
    grep -q 'Summary:' "$run/btx-final.log"
    grep -q 'read permission denied' "$run/snapshots.log"
    grep -q 'missing tool: unavailable-diagnostic-test-tool' "$run/snapshots.log"
    grep -q 'early boot USB enumeration' "$run/kernel-1.log"
    grep -q 'USB/CDC/ALSA inventory changed' "$run/snapshots.log"
    grep -q 'Momentary freq = 44100' "$run/snapshots.log"
    grep -q 'state: RUNNING' "$run/snapshots.log"
    grep -q 'speed: 480' "$run/snapshots.log"
done
# Collector worker and its sleep must be reaped, not just the normal launcher.
if ps -eo args | grep -F "$work/ports/BroTracker Diagnostics.sh" | grep -v grep; then
    echo 'Diagnostic worker survived' >&2; exit 1
fi
grep -q 'BroTracker USB audio reconnected' "${runs[2]}/snapshots.log"
echo 'Diagnostic unique-run, live/final log, denied-tool, HUP, exit-status and cleanup checks passed'
