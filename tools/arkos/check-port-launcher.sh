#!/usr/bin/env bash
# Stub processes only; never access real audio, USB, or a console.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
log=/tmp/brotracker-arkos.log
launcher_pid=
[[ ! -e "$log" ]] || cp -p "$log" "$work/old-log"
cleanup() {
    if [[ -n "$launcher_pid" ]]; then kill -TERM "$launcher_pid" 2>/dev/null || true; wait "$launcher_pid" 2>/dev/null || true; fi
    if [[ -f "$work/old-log" ]]; then cp -p "$work/old-log" "$log"; else rm -f "$log"; fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'cat "$log" >&2' ERR
mkdir -p "$work/ports/brotracker" "$work/data/PortMaster"
cp "$repo/tools/arkos/port/BroTracker.sh" "$work/ports/BroTracker.sh"
export XDG_DATA_HOME="$work/data" TEST_DIR="$work"
cat > "$work/data/PortMaster/control.txt" <<'STUB'
get_controls() { sdl_controllerconfig=test-mapping; }
pm_platform_helper() { echo 'stub platform helper'; }
pm_finish() {
    for f in "$TEST_DIR"/*.pid; do
        [[ -f "$f" ]] || continue
        if kill -0 "$(cat "$f")" 2>/dev/null; then echo "orphan: $f" > "$TEST_DIR/orphan"; fi
    done
    echo finished > "$TEST_DIR/finished"
    echo 'stub pm_finish'
    return 99
}
STUB
cat > "$work/ports/brotracker/BroTrackerArkOSUI" <<'STUB'
#!/bin/bash
[[ "$BROTRACKER_APPEND_LOG" == 1 && "$SDL_GAMECONTROLLERCONFIG" == test-mapping ]] || exit 80
echo $$ > "$TEST_DIR/ui.pid"
echo 'stub UI append' >> /tmp/brotracker-arkos.log
child=
trap '[[ -z "$child" ]] || { kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; }; exit 143' TERM HUP INT
if [[ "$UI_SECONDS" == exhausted ]]; then
    # Wait for the actual budget-exhausted diagnostic, not a QEMU wall-time guess.
    for ((i=0; i<400; ++i)); do
        if grep -q 'audio startup retries exhausted' /tmp/brotracker-arkos.log; then exit 7; fi
        sleep 0.1 & child=$!
        echo "$child" > "$TEST_DIR/ui-sleep.pid"
        wait "$child"
    done
    exit 82
fi
sleep "$UI_SECONDS" &
child=$!
echo "$child" > "$TEST_DIR/ui-sleep.pid"
wait "$child"
exit 7
STUB
cat > "$work/ports/brotracker/BroTrackerAlsaBridge" <<'STUB'
#!/bin/bash
[[ "$1" == --auto ]] || exit 81
count=0
[[ ! -f "$TEST_DIR/count" ]] || read -r count < "$TEST_DIR/count"
count=$((count+1)); echo "$count" > "$TEST_DIR/count"
echo $$ > "$TEST_DIR/bridge.pid"
echo "stub bridge stdout attempt=$count"
echo "stub bridge stderr attempt=$count" >&2
if [[ "$BRIDGE_MODE" == fail || ( "$BRIDGE_MODE" == retry && "$count" -lt 3 ) ]]; then
    echo 'Device busy or Teensy absent'; exit 1
fi
echo 'Queue capacity: stub configured'
if [[ "$BRIDGE_MODE" == runtime ]]; then echo 'stub runtime USB unplug'; exit 9; fi
sleep 30 &
child=$!
echo "$child" > "$TEST_DIR/bridge-sleep.pid"
trap 'kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; echo bridge-stopped; exit 0' TERM HUP INT
wait "$child"
STUB
chmod +x "$work/ports/brotracker/"*
record_children() {
    local parent=$1 descendant
    for descendant in $(cat "/proc/$parent/task/$parent/children" 2>/dev/null); do
        echo "$descendant" > "$work/descendant-$descendant.pid"
        record_children "$descendant"
    done
}
run_case() {
    export BRIDGE_MODE=$1 UI_SECONDS=$2
    rm -f "$work/count" "$work/finished" "$work/orphan" "$work/"*.pid
    bash "$work/ports/BroTracker.sh" & launcher_pid=$!
    sleep 0.15
    record_children "$launcher_pid"
    if [[ "$3" == terminate ]]; then
        sleep 0.35
        record_children "$launcher_pid"
        kill -TERM "$launcher_pid"
        expected=143
    else expected=7; fi
    result=0; wait "$launcher_pid" || result=$?
    launcher_pid=
    [[ "$result" == "$expected" ]] || { cat "$log"; echo "wrong exit $result"; exit 1; }
    [[ -f "$work/finished" && ! -f "$work/orphan" ]] || { cat "$log"; echo 'cleanup failure'; exit 1; }
    grep -q 'Launcher: starting' "$log"
    grep -q 'stub UI append' "$log"
    grep -q 'stub platform helper' "$log"
    grep -q 'stub pm_finish' "$log"
    echo "Launcher check passed: $1 / $3"
}
run_case retry 5 normal
[[ $(cat "$work/count") == 3 ]]
grep -q 'stub bridge stderr attempt=1' "$log"
grep -q 'bridge-stopped' "$log"
run_case runtime 1 normal
[[ $(cat "$work/count") == 1 ]]
grep -q 'no runtime restart' "$log"
run_case fail 3 normal
[[ $(cat "$work/count") == 2 ]]
run_case fail exhausted normal
[[ $(cat "$work/count") == 8 ]]
grep -q 'audio startup retries exhausted; UI remains usable' "$log"
run_case fail 30 terminate
run_case ready 30 terminate
mv "$work/ports/brotracker/BroTrackerAlsaBridge" "$work/bridge-disabled"
run_case ready 0.2 normal
grep -q 'missing/not executable' "$log"
echo 'All launcher retry/log/cleanup/exit checks passed'
