#!/usr/bin/env bash
# Stub processes only; never access real audio, USB, or a console.
set -euo pipefail
# Adopt/reap descendants of the deliberately SIGKILLed launcher, even on
# test hosts whose PID 1 does not reap orphans. No production helper needed.
if [[ ${LAUNCHER_TEST_SUBREAPER:-0} != 1 ]]; then
    exec python3 - "$0" <<'PYTHON'
import ctypes, os, subprocess, sys, time
if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
    raise OSError(ctypes.get_errno(), "PR_SET_CHILD_SUBREAPER")
child = subprocess.Popen(["bash", sys.argv[1]],
                         env=dict(os.environ, LAUNCHER_TEST_SUBREAPER="1"))
result = None
while True:
    try:
        pid, status = os.waitpid(-1, os.WNOHANG)
    except ChildProcessError:
        break
    if pid == child.pid:
        result = os.WEXITSTATUS(status) if os.WIFEXITED(status) else 128 + os.WTERMSIG(status)
    if not pid:
        time.sleep(0.02)
sys.exit(result if result is not None else 1)
PYTHON
fi
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
log=/tmp/brotracker-arkos.log
launcher_pid=
[[ ! -e "$log" ]] || cp -p "$log" "$work/old-log"
cleanup() {
    if [[ -n "$launcher_pid" ]]; then kill -TERM "$launcher_pid" 2>/dev/null || true; wait "$launcher_pid" 2>/dev/null || true; fi
    for f in "$work/"*.pid; do
        [[ -f "$f" ]] || continue
        kill -TERM "$(cat "$f")" 2>/dev/null || true
    done
    if [[ -f "$work/old-log" ]]; then cp -p "$work/old-log" "$log"; else rm -f "$log"; fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'cat "$log" >&2' ERR
mkdir -p "$work/ports/brotracker" "$work/data/PortMaster"
cp "$repo/tools/arkos/port/BroTracker Terminal.sh" "$work/ports/BroTracker Terminal.sh"
export XDG_DATA_HOME="$work/data" TEST_DIR="$work"
cat > "$work/data/PortMaster/control.txt" <<'STUB'
get_controls() { sdl_controllerconfig=test-mapping; }
interfere() {
    if [[ ${HELPER_INTERFERENCE:-0} == 1 ]]; then
        trap 'echo helper-exit-trap' EXIT
        trap 'echo helper-term-trap' TERM
        set -euo pipefail
    fi
}
interfere
pm_platform_helper() { echo 'stub platform helper'; interfere; }
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
cat > "$work/ports/brotracker/BroTrackerTerminal" <<'STUB'
#!/bin/bash
[[ "$BROTRACKER_APPEND_LOG" == 1 && "$SDL_GAMECONTROLLERCONFIG" == test-mapping ]] || exit 80
echo $$ > "$TEST_DIR/ui.pid"
echo 'stub UI append' >> /tmp/brotracker-arkos.log
child=
trap '[[ -z "$child" ]] || { kill -TERM "$child" 2>/dev/null; wait "$child" 2>/dev/null; }; exit 143' TERM HUP INT
if [[ "$UI_SECONDS" == recovered || "$UI_SECONDS" == configured ]]; then
    marker="stub recovered audio configured"
    [[ "$UI_SECONDS" != configured ]] || marker="Queue capacity:"
    # Wait for the actual diagnostic, not a QEMU wall-time guess.
    for ((i=0; i<400; ++i)); do
        if grep -q "$marker" /tmp/brotracker-arkos.log; then exit 7; fi
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
# An overlapping bridge fails the test, including its startup period.
if ! mkdir "$TEST_DIR/bridge-active" 2>/dev/null; then
    echo duplicate > "$TEST_DIR/duplicate"; exit 83
fi
trap 'rmdir "$TEST_DIR/bridge-active"' EXIT
echo $$ > "$TEST_DIR/bridge.pid"
echo "stub bridge stdout attempt=$count"
echo "stub bridge stderr attempt=$count" >&2
if [[ "$BRIDGE_MODE" == fail || ( "$BRIDGE_MODE" == retry && "$count" -lt 3 ) ||
      ( "$BRIDGE_MODE" == recover && "$count" -gt 1 && "$count" -lt 3 ) ||
      ( "$BRIDGE_MODE" == prolonged && "$count" -gt 1 && "$count" -lt 12 ) ||
      ( "$BRIDGE_MODE" == recovery_wait && "$count" -gt 1 ) ]]; then
    echo 'Device busy or Teensy absent'; exit 1
fi
echo 'Queue capacity: stub configured'
if [[ ( "$BRIDGE_MODE" == recover || "$BRIDGE_MODE" == prolonged || "$BRIDGE_MODE" == recovery_wait ) && "$count" == 1 ]]; then
    echo 'stub runtime USB unplug'; exit 9
fi
if [[ "$BRIDGE_MODE" == recover || "$BRIDGE_MODE" == prolonged ]]; then
    echo 'stub recovered audio configured'
fi
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
    rm -f "$work/count" "$work/finished" "$work/orphan" "$work/duplicate" "$work/"*.pid
    cp "$repo/tools/arkos/port/BroTracker Terminal.sh" "$work/ports/BroTracker Terminal.sh"
    # Accelerate only long recovery scenarios in the disposable launcher.
    if [[ "$1" == recover || "$1" == prolonged ]]; then
        sed -i 's/sleep "$backoff" \&/sleep 0.1 \&/' "$work/ports/BroTracker Terminal.sh"
    fi
    bash "$work/ports/BroTracker Terminal.sh" & launcher_pid=$!
    sleep 0.15
    record_children "$launcher_pid"
    if [[ "$3" == killed ]]; then
        sleep 0.5
        record_children "$launcher_pid"
        kill -KILL "$launcher_pid"
        wait "$launcher_pid" 2>/dev/null || true
        launcher_pid=
        # The UI is not owned by the audio supervisor; end the orphan stub.
        kill -TERM "$(cat "$work/ui.pid")" 2>/dev/null || true
        for ((i=0; i<100; ++i)); do
            alive=0
            for f in "$work/"*.pid; do
                [[ -f "$f" ]] || continue
                if kill -0 "$(cat "$f")" 2>/dev/null; then alive=1; fi
            done
            ((alive == 0)) && break
            sleep 0.1
        done
        [[ "$alive" == 0 && ! -f "$work/finished" ]]
        grep -q 'main launcher disappeared' "$log"
        grep -q 'audio supervisor stopped and children reaped' "$log"
        echo "Launcher check passed: $1 / killed (all descendants reaped)"
        return
    fi
    if [[ "$3" == terminate ]]; then
        sleep 0.35
        record_children "$launcher_pid"
        kill -TERM "$launcher_pid"
        expected=143
    else expected=7; fi
    result=0; wait "$launcher_pid" || result=$?
    launcher_pid=
    [[ "$result" == "$expected" ]] || { cat "$log"; echo "wrong exit $result"; exit 1; }
    [[ -f "$work/finished" && ! -f "$work/orphan" && ! -f "$work/duplicate" && ! -d "$work/bridge-active" ]] || { cat "$log"; echo 'cleanup failure'; exit 1; }
    grep -q 'Launcher: starting' "$log"
    grep -q 'stub UI append' "$log"
    grep -q 'stub platform helper' "$log"
    grep -q 'Launcher: children reaped; UI/launcher status=' "$log"
    ! grep -q 'helper-.*-trap' "$log"
    grep -q 'stub pm_finish' "$log"
    echo "Launcher check passed: $1 / $3"
}
run_case retry configured normal
[[ $(cat "$work/count") == 3 ]]
grep -q 'stub bridge stderr attempt=1' "$log"
grep -q 'bridge-stopped' "$log"
run_case recover recovered normal
[[ $(cat "$work/count") == 3 ]]
grep -q 'initial startup configured; bridge exited status=9' "$log"
grep -q 'audio runtime recovery failed status=1' "$log"
grep -q 'stub recovered audio configured' "$log"
run_case prolonged recovered normal
[[ $(cat "$work/count") == 12 ]]
grep -q 'audio runtime recovery attempt 11' "$log"
grep -q 'audio runtime recovery waiting 16 seconds' "$log"
# Runtime recovery remains interruptible while waiting after failed discovery.
run_case recovery_wait 30 terminate
[[ $(cat "$work/count") == 1 ]]
grep -q 'audio runtime recovery waiting 2 seconds' "$log"
grep -q 'audio supervisor stopped and children reaped' "$log"
run_case fail 3 normal
[[ $(cat "$work/count") == 2 ]]
run_case fail 30 terminate
run_case ready 30 terminate
export HELPER_INTERFERENCE=1
run_case retry configured normal
[[ $(cat "$work/count") == 3 ]]
run_case ready 30 terminate
run_case ready 30 killed
run_case fail 30 killed
unset HELPER_INTERFERENCE
mv "$work/ports/brotracker/BroTrackerAlsaBridge" "$work/bridge-disabled"
run_case ready 0.2 normal
grep -q 'missing/not executable' "$log"
echo 'All launcher retry/log/cleanup/exit checks passed'
