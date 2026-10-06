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
