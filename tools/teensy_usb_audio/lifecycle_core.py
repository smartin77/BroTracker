import hashlib
USB_SHA256="8cb03e83e90527e5574c7977bc8957edbc16f27c2b210c7949a507158f8bf78c"
def patch_usb(source):
    if hashlib.sha256(source).hexdigest()!=USB_SHA256:
        raise RuntimeError("Incompatible Teensy4 usb.c; lifecycle trace requires exact validated source")
    text=source.decode()
    text='#include "usb_lifecycle_trace.h"\n'+text
    for old,new in [
      ('FLASHMEM void usb_init(void)\n{','FLASHMEM void usb_init(void)\n{\n    brotracker_usb_lifecycle_event(1,0,0,0);'),
      ('if (status & USB_USBSTS_URI) { // page 3164','if (status & USB_USBSTS_URI) { // page 3164\n        brotracker_usb_lifecycle_event(2,usb_configuration,usb_audio_transmit_setting,USB1_PORTSC1);'),
      ('usb_configuration = setup.wValue;','brotracker_usb_lifecycle_event(3,usb_configuration,setup.wValue,0);\n        usb_configuration = setup.wValue;'),
      ('usb_audio_transmit_setting = setup.wValue;','brotracker_usb_lifecycle_event(4,usb_audio_transmit_setting,setup.wValue,0);\n            usb_audio_transmit_setting = setup.wValue;'),
      ('usb_audio_receive_setting = setup.wValue;','brotracker_usb_lifecycle_event(5,usb_audio_receive_setting,setup.wValue,0);\n            usb_audio_receive_setting = setup.wValue;')]:
        if text.count(old)!=1:raise RuntimeError('Incompatible lifecycle anchor: '+old)
        text=text.replace(old,new,1)
    return text.encode()
