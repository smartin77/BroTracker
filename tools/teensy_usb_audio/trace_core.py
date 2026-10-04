# Inject trace hooks only into the version/hash-validated, zero-fill-patched core.
def trace_bytes(patched):
    text=patched.decode()
    def insert(old,new):
        nonlocal text
        if text.count(old)!=1:raise RuntimeError('Incompatible USB trace anchor: '+old)
        text=text.replace(old,new,1)
    insert('#include "debug/printf.h"','#include "debug/printf.h"\n#include "usb_tx_trace.h"')
    insert('static void tx_event(transfer_t *t)\n{',r'''#ifdef BROTRACKER_USB_TX_TRACE
static BroTrackerUsbTrace::Buffer bt_trace;
static uint32_t bt_sequence, bt_updates, bt_discards;
static unsigned bt_copied;
static bool bt_shortage;
static uint32_t bt_irq_mask(){uint32_t value;__asm__ volatile("mrs %0, primask":"=r"(value)::"memory");return value;}
static void bt_restore_irq(uint32_t value){__asm__ volatile("msr primask, %0"::"r"(value):"memory");}
static void bt_barrier(){__asm__ volatile("dmb":::"memory");}
extern "C" bool brotracker_usb_trace_arm() {
    uint32_t mask=bt_irq_mask();__disable_irq();
    bool ok=bt_trace.Arm(micros());bt_barrier();bt_restore_irq(mask);return ok;
}
extern "C" void brotracker_usb_trace_freeze() {
    uint32_t mask=bt_irq_mask();__disable_irq();
    if(bt_trace.armed){bt_trace.Freeze();}bt_barrier();bt_restore_irq(mask);
}
extern "C" void brotracker_usb_trace_release() {
    uint32_t mask=bt_irq_mask();__disable_irq();
    bt_trace.Release();bt_barrier();bt_restore_irq(mask);
}
extern "C" const BroTrackerUsbTrace::Buffer* brotracker_usb_trace_buffer(){bt_barrier();return &bt_trace;}
#endif
static void tx_event(transfer_t *t)
{''')
    insert('\tint len = usb_audio_transmit_callback();','''#ifdef BROTRACKER_USB_TX_TRACE
    bt_copied=0;bt_shortage=false;
#endif
\tint len = usb_audio_transmit_callback();
#ifdef BROTRACKER_USB_TX_TRACE
    bt_trace.Capture(usb_audio_transmit_buffer,len,bt_copied,bt_shortage,
                     ++bt_sequence,micros(),ARM_DWT_CYCCNT,bt_updates,bt_discards);
    bt_barrier();
#endif''')
    insert('void AudioOutputUSB::update(void)\n{','''void AudioOutputUSB::update(void)
{
#ifdef BROTRACKER_USB_TX_TRACE
    ++bt_updates;
#endif''')
    insert('\t\t// buffer overrun - PC is consuming too slowly','''#ifdef BROTRACKER_USB_TX_TRACE
        ++bt_discards;
#endif
\t\t// buffer overrun - PC is consuming too slowly''')
    insert('\t\t\t// buffer underrun - PC is consuming too quickly','''#ifdef BROTRACKER_USB_TX_TRACE
            bt_shortage=true;
#endif
\t\t\t// buffer underrun - PC is consuming too quickly''')
    insert('\t\tlen += num;','''#ifdef BROTRACKER_USB_TX_TRACE
        bt_copied+=num;
#endif
\t\tlen += num;''')
    return text.encode()
