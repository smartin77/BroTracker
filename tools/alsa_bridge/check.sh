#!/usr/bin/env bash
# Device-free ALSA checks. No real capture/playback hardware is opened.
set -euo pipefail
binary=$1
work=$(mktemp -d)
child=
cleanup() {
    if [[ -n "$child" ]]; then kill -TERM "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
    rm -rf -- "$work"
}
trap cleanup EXIT
"$binary" --help >"$work/help" 2>&1
if "$binary" >"$work/missing" 2>&1; then echo 'Missing device arguments accepted' >&2; exit 1; fi
if "$binary" null brotracker_nonexistent_device >"$work/error" 2>&1; then echo 'Invalid output accepted' >&2; exit 1; fi
grep -q 'Opening playback device: brotracker_nonexistent_device' "$work/error"
grep -q 'Bridge failed:' "$work/error"
# File PCMs exercise actual interleaved frame forwarding, including a partial
# final block and ring wrap; null slaves require no sound card or clock.
for ((i=0; i<9001; ++i)); do printf '\001\002\003\004'; done >"$work/input.raw"
cat >"$work/alsa.conf" <<EOF
pcm.null { type null }
pcm.test_capture {
    type file
    slave.pcm "null"
    file "/dev/null"
    infile "$work/input.raw"
    format "raw"
}
pcm.test_playback {
    type file
    slave.pcm "null"
    file "$work/output.raw"
    format "raw"
}
EOF
ALSA_CONFIG_PATH="$work/alsa.conf" "$binary" test_capture test_playback >"$work/run.log" 2>&1 &
child=$!
# Give the process time to start under QEMU, then exercise Ctrl+C shutdown.
for ((i=0; i<100; ++i)); do
    if grep -q 'Queue capacity:' "$work/run.log"; then break; fi
    if ! kill -0 "$child" 2>/dev/null; then cat "$work/run.log"; wait "$child"; exit 1; fi
    sleep 0.05
done
sleep 0.2
kill -INT "$child"
if ! wait "$child"; then cat "$work/run.log"; exit 1; fi
child=
cat "$work/run.log"
grep -q 'capture: S16_LE, 2 channels, 44100 Hz; period=' "$work/run.log"
grep -q 'playback: S16_LE, 2 channels, 44100 Hz; period=' "$work/run.log"
grep -q 'Signal received; closing audio devices.' "$work/run.log"
grep -q 'captured=9001 frames, submitted=9001 frames' "$work/run.log"
grep -q 'capture_overruns=0, playback_underruns=0' "$work/run.log"
cmp "$work/input.raw" "$work/output.raw"
echo 'ALSA file/null PCM checks passed (not a hardware audio/latency test).'

# Opt-in recording must preserve exactly the successful capture bytes, and
# every fresh process (including recovery) must get a distinct WAV.
mkdir "$work/diagnostics"
for signal in INT TERM; do
    BROTRACKER_DIAGNOSTICS_DIR="$work/diagnostics" ALSA_CONFIG_PATH="$work/alsa.conf" "$binary" test_capture test_playback > "$work/record-$signal.log" 2>&1 &
    child=$!
    for ((i=0; i<150; ++i)); do
        if grep -q 'Queue capacity:' "$work/record-$signal.log"; then break; fi
        sleep 0.02
    done
    sleep 0.3
    kill -"$signal" "$child"
    wait "$child"
    child=
    grep -q 'recording_end' "$work/record-$signal.log"
done
wavs=("$work/diagnostics/"*.wav)
[[ ${#wavs[@]} == 2 ]]
for wav in "${wavs[@]}"; do
    [[ $(stat -c %s "$wav") == 36048 ]]
    dd if="$wav" of="$work/recorded.raw" bs=44 skip=1 status=none
    cmp "$work/input.raw" "$work/recorded.raw"
done
awk -F '\t' 'NF != 6 { exit 1 }' "$work/diagnostics/events.tsv"
grep -q 'recording_start' "$work/diagnostics/events.tsv"
awk -F '\t' '
    $5 == "recording_setup" { setup[$4] = NR }
    $5 == "capture_start" { if (!setup[$4] || setup[$4] >= NR) exit 1; capture[$4] = NR }
    $5 == "recording_start" { if (!capture[$4] || capture[$4] >= NR) exit 1; starts++ }
    END { if (starts != 2) exit 1 }
' "$work/diagnostics/events.tsv"
grep -q 'recording_end' "$work/diagnostics/events.tsv"
# Recording open failure must not prevent ordinary audio forwarding.
BROTRACKER_DIAGNOSTICS_DIR="$work/missing/subdir" ALSA_CONFIG_PATH="$work/alsa.conf" "$binary" test_capture test_playback > "$work/open-fail.log" 2>&1 &
child=$!
sleep 0.6
kill -INT "$child"
wait "$child"
child=
grep -q 'Recording disabled:' "$work/open-fail.log"
cmp "$work/input.raw" "$work/output.raw"
echo 'PCM recording integration checks passed (INT/TERM finalization and open failure audio continuation).'

# Paired diagnostic integration: real ALSA file/null reads and partial final
# block; compare byte-exact capture and accepted streams with the offline tool.
mkdir "$work/clicks"
BROTRACKER_BRIDGE_CLICKS_DIR="$work/clicks" BROTRACKER_BRIDGE_CLICKS_LIMIT_SECONDS=240 ALSA_CONFIG_PATH="$work/alsa.conf" "$binary" test_capture test_playback > "$work/clicks.log" 2>&1 &
child=$!
for ((i=0; i<150; ++i)); do
    if grep -q 'Click evidence:' "$work/clicks.log"; then break; fi
    sleep 0.02
done
sleep 0.2
kill -INT "$child"
wait "$child"
child=
runs=("$work/clicks/"bridge-*)
[[ ${#runs[@]} == 1 ]]
grep -q '"limit_seconds":240' "${runs[0]}/run.json"
cmp "$work/input.raw" "${runs[0]}/received.s16le"
cmp "$work/input.raw" "${runs[0]}/accepted.s16le"
python3 -B "$(dirname -- "${BASH_SOURCE[0]}")/analyze_clicks.py" "${runs[0]}" --output "$work/analysis.json"
grep -q '"complete": true' "$work/analysis.json"
grep -q '"accepted_equals_mapped_received": true' "$work/analysis.json"
echo 'Paired click diagnostics ALSA file/null integration passed (not physical evidence).'

# Standalone run helper: on/off conditions must be unique, capture metadata,
# keep the bridge independent, and preserve a failed startup instead of deleting it.
mkdir "$work/sessions"
printf '{"workload":"synthetic ALSA file fixture"}\n' > "$work/workload.json"
for mode in on off; do
    ALSA_CONFIG_PATH="$work/alsa.conf" bash "$(dirname -- "${BASH_SOURCE[0]}")/run-clicks.sh" "$mode" "$binary" "$work/sessions" "$work/workload.json" test_capture test_playback > "$work/helper-$mode.log" 2>&1 &
    child=$!
    for ((i=0; i<150; ++i)); do
        sessions=("$work/sessions/"*"-$mode-"*)
        if [[ -f "${sessions[0]}/bridge.pid" ]]; then break; fi
        sleep 0.02
    done
    sleep 0.2
    kill -TERM "$child"
    code=0; wait "$child" || code=$?
    child=
    [[ $code == 143 ]]
    [[ -f "${sessions[0]}/exit-status" && -f "${sessions[0]}/system.txt" ]]
    [[ -f "${sessions[0]}/workload.json" ]]
    if [[ $mode == on ]]; then
        traces=("${sessions[0]}/"bridge-*)
        [[ ${#traces[@]} == 1 ]]
        python3 -B "$(dirname -- "${BASH_SOURCE[0]}")/analyze_clicks.py" "${traces[0]}" --output "${sessions[0]}/analysis.json"
    else
        [[ $(find "${sessions[0]}" -name received.s16le | wc -l) == 0 ]]
    fi
done
if bash "$(dirname -- "${BASH_SOURCE[0]}")/run-clicks.sh" on "$binary" "$work/sessions" "$work/workload.json" null brotracker_nonexistent_device > "$work/helper-failed.log" 2>&1; then
    echo 'Run helper hid failed bridge startup' >&2; exit 1
fi
[[ $(find "$work/sessions" -name exit-status | wc -l) == 3 ]]
echo 'Standalone on/off run helper checks passed; failed startup preserved.'

# Listening mode uses the same running bridge; simulated BTX acknowledgments only.
mkdir "$work/listening"
python3 - "$work/listen-control" <<'PY'
import sys,struct,time
with open(sys.argv[1],'wb') as f:
    data=bytearray(4096)
    struct.pack_into('<IIII',data,0,1,0,0,1)
    struct.pack_into('<QQII',data,16,time.monotonic_ns(),0,1,15500)
    f.write(data)
PY
BROTRACKER_LISTEN_CONTROL="$work/listen-control" BROTRACKER_LISTEN_BASE="$work/listening" ALSA_CONFIG_PATH="$work/alsa.conf" "$binary" test_capture test_playback > "$work/listening.log" 2>&1 &
child=$!
for ((i=0;i<150;++i)); do
    if grep -q 'Queue capacity:' "$work/listening.log"; then break; fi
    sleep 0.02
done
sleep 0.3
python3 - "$work/listen-control" <<'PY'
import sys,struct,time,mmap
with open(sys.argv[1],'r+b') as f:
    m=mmap.mmap(f.fileno(),4096)
    struct.pack_into('<QQII',m,40,time.monotonic_ns(),0,3,15500)
    struct.pack_into('<I',m,12,3)
    struct.pack_into('<I',m,0,2)
    m.close()
PY
sleep 0.2
kill -TERM "$child"; wait "$child"; child=
python3 - "$work/listening" "$work/input.raw" <<'PY'
from pathlib import Path
import sys,json,wave
base=Path(sys.argv[1]);runs=list(base.glob('bridge-*'));assert len(runs)==1,runs
run=runs[0];expected=Path(sys.argv[2]).read_bytes()
for name in ('received.wav','accepted.wav'):
    with wave.open(str(run/name),'rb') as w:
        assert w.getparams()[:3]==(2,2,44100)
        assert w.readframes(w.getnframes())==expected,name
assert not json.loads((run/'manifest.json').read_text())['incomplete']
assert not json.loads((run/'completion.json').read_text())['incomplete']
assert (run/'alsa-parameters.txt').is_file()
PY
cmp "$work/input.raw" "$work/output.raw"
echo 'Listening file/null bridge integration preserves exact received/accepted PCM'
