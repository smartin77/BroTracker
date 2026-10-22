// Compiled inside the version-locked PJRC USB MIDI translation unit and by the
// native mocked-DMA contract test. Caller serializes ring access/configuration.
static uint32_t bt_midi_started_us[TX_NUM];
static uint8_t bt_midi_watched[TX_NUM];
static uint32_t bt_midi_watch_generation;

// Same 40 ms transport-stall policy as the installed core's send timeout, but
// observed without waiting. This is a failure watchdog, not latency compensation.
int brotracker_midi_tx_fault(uint32_t now)
{
    if (bt_midi_watch_generation != brotracker_midi_generation) {
        memset(bt_midi_watched, 0, sizeof(bt_midi_watched));
        bt_midi_watch_generation = brotracker_midi_generation;
    }
    if (!brotracker_midi_ready) return 0;
    for (unsigned i=0; i<TX_NUM; ++i) if (bt_midi_watched[i]) {
        uint32_t status=usb_transfer_status(tx_transfer+i);
        if (status & 0x68) return 1;
        if (!(status & 0x80)) bt_midi_watched[i]=0;
        else if ((uint32_t)(now-bt_midi_started_us[i]) > 40000) return 1;
    }
    return 0;
}

// A completion observation, not proof of receipt beyond the USB host.
int brotracker_midi_tx_idle(void)
{
    if (!brotracker_midi_ready) return 0;
    for (unsigned i=0; i<TX_NUM; ++i)
        if (usb_transfer_status(tx_transfer+i) & (0x80 | 0x68)) return 0;
    return 1;
}

// Sole BroTracker writer: stock send/send_now/SysEx must not also use this ring.
// Return 1 means DMA submission, never proof of host or external delivery.
int brotracker_midi_try_submit(const uint32_t *words, uint32_t count, uint32_t now)
{
    if (!brotracker_midi_ready || !usb_configuration || !(USB1_PORTSC1 & USB_PORTSC1_CCS) ||
        (USB1_PORTSC1 & USB_PORTSC1_SUSP) || !count || count>16 || !tx_packet_size) return 0;
    transfer_t *xfer=tx_transfer+tx_head;
    uint32_t status=usb_transfer_status(xfer);
    if (status & (0x80 | 0x68)) return 0;
    uint8_t *buffer=txbuffer+tx_head*TX_SIZE;
    memcpy(buffer,words,count*4);
    usb_prepare_transfer(xfer,buffer,count*4,0);
    arm_dcache_flush_delete(buffer,TX_SIZE);
    bt_midi_watched[tx_head]=1;
    bt_midi_started_us[tx_head]=now;
    usb_transmit(MIDI_TX_ENDPOINT,xfer);
    tx_head=(tx_head+1)%TX_NUM;
    return 1;
}
