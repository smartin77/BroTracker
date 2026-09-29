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
