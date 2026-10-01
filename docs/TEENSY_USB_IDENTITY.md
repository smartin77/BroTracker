# Teensy USB identity

The BroTracker firmware advertises the USB product name **BroTracker USB audio**.
The host application remains **BroTracker Terminal**; BTX is communication
shorthand, not a device or application name.

## Project-local override

`firmware/teensy/BroTracker/usb_names.c` supplies the strong
`usb_string_product_name` symbol supported by the installed Teensy core's
`usb_names.h` and weak descriptor aliases (Teensyduino 1.62). It contains a
static 42-byte USB string descriptor: 20 UTF-16LE characters plus the header.
It uses no heap or runtime initialization. Installed framework files are not
modified.

`platformio.ini` still selects `USB_MIDI_AUDIO_SERIAL`. VID/PID remains
`16c0:048a`; manufacturer, hardware-derived serial number, six interfaces,
endpoint layout and audio formats remain unchanged. Idle boot, host clock
synchronization, startup serial handoff, BTTEST1, audio and MIDI code are
unchanged.

The current full/high-speed audio descriptors have zero `iFunction`,
`iInterface`, `iTerminal`, `iChannelNames` and `iFeature` string indexes.
CDC and MIDI interface/function string indexes are also zero; MIDI jack
names use no separate strings. The product name therefore identifies the
whole composite device, including its audio function, rather than adding
an audio-only interface name. Hosts may derive MIDI/audio labels from it;
CDC may retain a generic COM-port label. Exact displayed labels are host
policy, not additional names supplied by this override.

## Host compatibility

Windows CDC discovery matches the hardware instance identity
`USB\VID_16C0&PID_048A`, independently of friendly names. No Windows code or
package change is needed. Windows may retain the old friendly name in its
device cache even when the actual USB product descriptor has changed.
Verify the descriptor separately from Device Manager/audio endpoint labels;
do not rename devices through the registry or local settings.

ArkOS CDC discovery also uses VID/PID and remains compatible. The current
ALSA bridge's automatic capture discovery accepts only ALSA ID `MIDIAudio`
or card name `Teensy MIDI/Audio`. If both change after enumeration, `--auto`
will reject the renamed capture device as no usable Teensy capture endpoint.
The exact new ALSA ID must be observed on hardware; it is not assumed here.
Explicit capture/playback PCM arguments bypass that automatic identity match
and remain available, using freshly enumerated card numbers. Rockchip output
matching is unaffected. Migrating ArkOS automatic audio discovery is a
separate task; its source and deployment package are unchanged here.

## Validation and post-flash check

Build without flashing: `pio run -e teensy41`.

For this change, linked ELF inspection verified the exact 42-byte product
string and descriptor-table reference. The 18-byte device descriptor,
10-byte qualifier and both 341-byte full/high-speed configuration descriptors
matched the pre-change firmware byte-for-byte. Manufacturer and serial
storage also matched; the serial descriptor still points to the core's
hardware-serial implementation. Existing host CTest checks passed (4/4).

After a separate user flash, physically unplug/replug and verify:

1. The live USB product descriptor is exactly `BroTracker USB audio`, with
   VID/PID `16c0:048a` and the same hardware serial number. Record actual
   audio, MIDI and CDC labels separately, allowing for Windows cached names.
2. BroTracker Terminal discovers CDC, handshakes in IDLE without playback,
   and supports START, restart, STOP and reconnect as before. Confirm host
   clock synchronization and preservation of an early BTTEST1 HELLO.
3. Audio capture/playback and MIDI still enumerate and function. On ArkOS,
   record the actual ALSA card name/ID before migrating automatic discovery;
   do not treat existing `--auto` discovery as compatible without checking.

These post-flash hardware checks have not been performed for this rename.
