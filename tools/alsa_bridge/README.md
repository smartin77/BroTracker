# Temporary standalone ALSA live bridge

This command-line experiment is separate from BroTracker's UI and Teensy
firmware. It does not send BTTEST1 commands or start the Teensy sequence.
Both device names are required; no card numbers are embedded in the tool.

## Build in the existing Eoan ARM64 root

From the checkout in WSL (no installation or package export):

```sh
bash tools/arkos/run-in-eoan.sh cmake -S /workspace -B /build/alsa-bridge \
  -DCMAKE_BUILD_TYPE=Release -DBROTRACKER_BUILD_ALSA_BRIDGE=ON
bash tools/arkos/run-in-eoan.sh cmake --build /build/alsa-bridge --parallel 2
bash tools/arkos/run-in-eoan.sh cmake -E chdir /build/alsa-bridge ctest --output-on-failure
```

The executable is `$BROTRACKER_EOAN_ROOT/build/alsa-bridge/BroTrackerAlsaBridge`.
ALSA headers/library (`libasound2-dev`) must be present in that root. The new
CMake option defaults OFF; neither existing Windows nor ArkOS UI builds gain
an ALSA dependency. Do not enable it on Windows.

## R36H test (after a separately reviewed transfer)

Use `arecord -L` and `aplay -L` to list capture and playback PCM names. Prefer
`CARD=<name>` over numeric indices. Use the same named devices that worked in
the capture-to-file / playback test; their exact CARD IDs must be obtained on
R36H. From the directory containing the executable:

```sh
./BroTrackerAlsaBridge 'hw:CARD=<Teensy-ID>,DEV=0' 'plughw:CARD=<Rockchip-ID>,DEV=0'
```

Alternatively, this Bash command prompts for both exact names without assuming
any CARD ID:

```sh
read -r -p 'Capture ALSA PCM: ' capture_device
read -r -p 'Playback ALSA PCM: ' playback_device
./BroTrackerAlsaBridge "$capture_device" "$playback_device"
```

Replace the two angle-bracketed IDs with the listed IDs, or pass the exact PCM
names previously tested. `hw` requests the hardware format directly; `plughw`
may convert at its slave. The tool logs both the negotiated application
parameters and ALSA's PCM/slave configuration dump. It requires interleaved
stereo S16_LE at exactly 44,100 Hz at its application interface; unsupported
settings fail instead of silently selecting a different rate/channel count.

Output is opened first. EBUSY is reported explicitly; release the output
manually if needed. The tool never stops EmulationStation or other processes.
Ctrl+C drops queued sound and closes both devices without waiting to drain.
Diagnostics go to stderr and are unbuffered (use `2>alsa-bridge.log` if desired).

## Buffering and interpretation

Each PCM requests a 256-frame period and 2,048-frame buffer; actual negotiated
values and the playback start threshold (half its negotiated buffer) are
printed. A fixed 8,192-frame / 32-KiB queue preserves stereo frame boundaries,
handles short reads/writes, and never grows. Nonblocking I/O retries EAGAIN and
uses a one-millisecond idle wait. These numbers are settings, **not measured
end-to-end latency**.

Overruns/underruns and suspend events are counted and logged. Recovery drops
both PCM queues, discards the application queue, prepares both devices and
restarts capture; discontinuities are explicit. Other errors, including
unplugging, terminate with a nonzero exit rather than hiding failed capture.
Final totals include captured/submitted frames, queue high-water mark and
application frames discarded/remaining. Submitted frames are not proof of
physical playback. A stream reset also discards kernel/device buffering.

There is no adaptive clock-drift correction: independent capture/playback
clocks can eventually exhaust buffering and cause reported xruns. This test
does not establish the cause of the distorted `arecord | aplay` pipeline.
The automated check uses ALSA file/null PCMs to verify stereo bytes, a partial
final block, ring wrap, invalid-device reporting and SIGINT shutdown. Physical
sound quality, both samples staying in sync, long-run drift, device-busy and
unplug behavior, and actual latency still require R36H hardware testing.
