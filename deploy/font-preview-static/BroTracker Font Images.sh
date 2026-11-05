#!/bin/bash
# Static images only: no generator, BTX startup, MIDI, audio or config changes.
set -e
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
images="$here/font-preview-static"
[[ ! -f "$here/static-01-terminus-6x12.bmp" ]] || images="$here"
ports="$here"
[[ ${here##*/} != font-preview-static ]] || ports=$(dirname -- "$here")
buttons="$ports/brotracker/midi-connect.py"
if command -v mpv >/dev/null 2>&1; then viewer=mpv
elif command -v ffplay >/dev/null 2>&1; then viewer=ffplay
else echo 'Font images: existing mpv or ffplay is required; nothing was installed.' >&2; exit 1
fi
command -v python3 >/dev/null 2>&1 || { echo 'Font images: existing python3 is required.' >&2; exit 1; }
[[ -r "$buttons" ]] || { echo "Font images: existing button helper missing: $buttons" >&2; exit 1; }
viewer_pid=
button_pid=
cleanup() {
    trap - EXIT INT TERM HUP
    for pid in "$button_pid" "$viewer_pid"; do
        [[ -z "$pid" ]] || { kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; }
    done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
for image in static-01-terminus-6x12.bmp static-02-terminus-8x14.bmp static-03-pattern-warning.bmp; do
    [[ -r "$images/$image" ]] || { echo "Missing image: $images/$image" >&2; exit 1; }
    if [[ $viewer == mpv ]]; then
        mpv --no-config --fullscreen --video-unscaled=yes --image-display-duration=inf \
            --no-audio --no-osc --osd-level=0 --input-default-bindings=no --input-terminal=no "$images/$image" &
    else
        ffplay -fs -x 640 -y 480 -an -sn -loglevel error -nostats "$images/$image" &
    fi
    viewer_pid=$!
    # Reuse the existing, physically documented R36H button reader. Importing
    # the helper does not call its main(), start MIDI routing or create config.
    # New presses only: held buttons, releases and repeats do not skip images.
    python3 -B - "$buttons" >/dev/null <<'PY' &
import os
from pathlib import Path
import runpy
import sys
helper = runpy.run_path(sys.argv[1], run_name='font_images_buttons')
names = ('retrogame_joypad', 'GO-Super Gamepad')
readable = False
for node in Path('/sys/class/input').glob('event*'):
    try:
        if (node / 'device/name').read_text().strip() in names and os.access('/dev/input/' + node.name, os.R_OK):
            readable = True
    except OSError:
        pass
if not readable:
    sys.stderr.write('Font images: handheld gamepad input is unavailable to this user.\n')
    sys.exit(1)
while not helper['wait_for_handheld'](seconds=86400):
    pass
PY
    button_pid=$!
    while kill -0 "$viewer_pid" 2>/dev/null && kill -0 "$button_pid" 2>/dev/null; do sleep 0.1; done
    if ! kill -0 "$viewer_pid" 2>/dev/null; then
        echo 'Font images: image viewer closed; returning to Ports.' >&2
        exit 1
    fi
    wait "$button_pid" || exit 1
    button_pid=
    kill "$viewer_pid" 2>/dev/null || true
    wait "$viewer_pid" 2>/dev/null || true
    viewer_pid=
done
# The third new button press closes the third image and returns to Ports.
