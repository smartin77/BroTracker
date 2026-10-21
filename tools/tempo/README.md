# Bounded live-tempo hardware check

With development firmware uploaded and BTX/other serial clients closed:

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/python.exe" tools/tempo/check_hardware.py --hardware
```

The script discovers the actual unique Teensy USB MIDI/audio/serial port. It uses
supported controls to check startup, default bridge negotiation, range rejection,
tempo application while stopped/playing/paused, advancing telemetry, snapshot
checksums, version-2 publication, published-baseline restoration, STOP/START and
reconnect retention. On success it restores the initial 125.00 BPM fixture and
published baseline and leaves playback stopped. On failure it attempts STOP and
closes serial; inspect the transcript for the last authoritative state.

Logs/JSON go to existing ignored `build/live-tempo/` storage. This is an automated
serial check, not audio capture, manual UX/listening approval or a bridge
investigation. No handheld deployment/SSH or config/SD persistence is involved.
See [Live tempo control](../../docs/LIVE_TEMPO_CONTROL.md) for the input, timing,
protocol, retention and manual-check contracts.
