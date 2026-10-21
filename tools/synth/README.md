# Synth reference and bounded transport checks

From the repository root, with the native C++ toolchain on PATH:

```powershell
New-Item -ItemType Directory -Force build/synth-foundation
g++ -std=c++17 -O2 -Isrc -Ilibraries/scheduler/src tools/synth/render_reference.cpp -o build/synth-foundation/render_reference.exe
./build/synth-foundation/render_reference.exe build/synth-foundation/reference.wav
python tools/synth/check_reference.py build/synth-foundation/reference.wav
```

The C++ program renders through the production block renderer; Python verifies
frequency, duration/endpoints, peak and the retrigger/OFF sequence. JSON is written
beside the WAV. All generated files belong in ignored `build/`, not Git.

After building/uploading `teensy41_dev`, with BTX/other serial clients closed:

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/python.exe" tools/synth/check_hardware.py --hardware
```

This discovers the actual Teensy audio/MIDI/serial port, requires one matching
device, checks initial IDLE, negotiates the default USB_BRIDGE route, runs START
for three seconds, verifies advancing telemetry, sends STOP, verifies IDLE/zeroed
position and closes its serial handle. Transcript and JSON go to ignored build
storage. It does not record audio, exercise bridge diagnostics, enable holdback,
claim listening/UX approval or deploy to a handheld. The physical check requires
authorization; `--hardware` makes that intent explicit.
