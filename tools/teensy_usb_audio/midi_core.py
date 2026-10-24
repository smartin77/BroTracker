"""Version-locked, nonblocking USB MIDI transfer extension; installed core is immutable."""
from pathlib import Path
import hashlib
SHA256='ee885657eddc165205df08d27b85bc397988f37551e660f75d0a2414145ac048'
def prepare_midi(framework,build):
    source=Path(framework)/'cores/teensy4/usb_midi.c'
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA256:
        raise RuntimeError('USB MIDI core changed; review nonblocking extension before building')
    text=data.decode()
    text=text.replace('void usb_midi_configure(void)', 'volatile uint32_t brotracker_midi_generation = 0;\nvolatile uint8_t brotracker_midi_ready = 0;\nvoid usb_midi_configure(void)',1)
    text=text.replace('printf("usb_midi_configure\\n");','printf("usb_midi_configure\\n");\n\t++brotracker_midi_generation;\n\tbrotracker_midi_ready = 1;',1)
    extension=Path(__file__).with_name('midi_tx.inc').read_text()
    # Insert while MIDI_INTERFACE definitions and core-private ring are in scope.
    anchor='void usb_midi_send_sysex_buffer_has_term('
    assert text.count(anchor)==1
    text=text.replace(anchor,extension+'\n'+anchor,1)
    target=Path(build)/'core-patches/usb_midi.c'
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_text()!=text: target.write_text(text)
    return source.resolve(),target


def prepare_lifecycle(framework,build,trace=False):
    from lifecycle_core import USB_SHA256
    source=Path(framework)/'cores/teensy4/usb.c'
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=USB_SHA256:
        raise RuntimeError('USB lifecycle source changed; review MIDI reset/suspend hooks')
    target=Path(build)/'core-patches/usb.c'
    text=target.read_text() if trace else data.decode()
    text='#include <stdint.h>\n#if defined(USB_MIDI_AUDIO_SERIAL)\nextern volatile uint32_t brotracker_midi_generation;\nextern volatile uint8_t brotracker_midi_ready;\n#endif\n'+text
    for anchor,statement in [
        ('if (status & USB_USBSTS_URI) { // page 3164','brotracker_midi_ready=0; ++brotracker_midi_generation;'),
        ('if (status & USB_USBSTS_SLI) { // page 3165','++brotracker_midi_generation;'),
        ('usb_configuration = setup.wValue;','brotracker_midi_ready=0; ++brotracker_midi_generation;')]:
        assert text.count(anchor)==1
        # SET_CONFIGURATION must invalidate BEFORE the core configures MIDI below.
        if anchor.startswith('usb_configuration'):
            text=text.replace(anchor,'#if defined(USB_MIDI_AUDIO_SERIAL)\n'+statement+'\n#endif\n'+anchor,1)
        else:
            text=text.replace(anchor,anchor+'\n#if defined(USB_MIDI_AUDIO_SERIAL)\n'+statement+'\n#endif',1)
    if not target.exists() or target.read_text()!=text: target.write_text(text)
    return source.resolve(),target
