# PT8211 isolated smoke test

Uses the installed Teensy Audio `AudioOutputPT8211`, with no BroTracker application,
scheduler, SD loading, USB audio or BTX. The independent source filter selects only
this directory's main.cpp. The normal default environment remains teensy41_dev.

Wiring for the PJRC Teensy-4-compatible module: BCK=21, WS=20, DIN=7, 3.3V and
common GND. The driver has no module-presence/status response; a successful build
or configured output peripheral does not establish that a DAC is attached.

Build from the repository root:

```powershell
pio run -e teensy41_pt8211_test
```

For later manual upload and monitoring (close BTX before opening the monitor):

```powershell
pio run -e teensy41_pt8211_test -t upload
pio device monitor -e teensy41_pt8211_test
```

Upload is not performed during preparation of this test.

The sequence starts with silence, then repeats a left-only 440 Hz burst, 500 ms
silence, right-only 880 Hz burst and 1500 ms silence. Gates last 250 ms with 10 ms
attack/release ramps; silence intervals include a further 12 ms release allowance.
Peak oscillator amplitude is 0.08 (about -22 dBFS). This is a routing smoke test,
not a calibrated output-level, frequency or timing measurement. Startup serial
text is retained for the first serial connection and sent in bounded chunks.

## Manual A/B check

1. Disconnect **all power**, including USB and any external supply, before every
   module attachment/removal. Verify orientation, pin alignment and common GND.
2. With the module attached, connect its analog output to a suitable stereo input
   or powered amplifier at low volume. Verify low tone on left only, silence,
   higher tone on right only, then the longer silence. Record any reversal/noise.
3. Disconnect all power, detach the module, then power Teensy again. Expect the
   same serial banner/sequence and no DAC analog output because the DAC is absent.
   Do not treat serial output as evidence of module presence or audio correctness.
4. Power down fully before reattaching and repeat A to distinguish wiring/output
   issues. Neither condition should be reported as automatic presence detection.

Compilation is checked separately; upload, listening and attached/detached hardware
validation remain manual. This test does not change production output routing.
