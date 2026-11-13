/* AERIA OS: MIDI. Our own code. The DIN ports on UART0 (0xFC060000, 31,250 baud: 132 MHz / (32 x 132), the pins
 * as the bootstrap sets them, PAR_UART 0x33); USB-MIDI is left out of the build (sadnoise: DIN only). Polled: every pass of the main
 * loop moves what the UART can take and parses what came in.
 *   aos_midi_out(status, d1, d2)   one message to both outs (2- or 3-byte by status; real-time bytes alone)
 *   module_midi(status, d1, d2)    every complete message that comes in, DIN or USB (the UI's: out.c, marbles.c)
 * MIDI THRU is the hardware's own. */
#define UART0 0xFC060000u
void module_midi(unsigned st, unsigned a, unsigned b);
static uint8_t mo_ring[1024];
static uint16_t mo_w, mo_r;
static uint32_t midi_in_count, midi_out_count, midi_drops;
static void usb_midi_queue(unsigned st, unsigned a, unsigned b);

static void midi_init(void)
{
    REG8(UART0 + 8) = 0x20; REG8(UART0 + 8) = 0x30; REG8(UART0 + 8) = 0x40; REG8(UART0 + 8) = 0x10;
    REG8(UART0 + 4) = 0xDD;                /* UCSR: the bus clock both ways */
    REG8(UART0 + 0) = 0x13;                /* UMR1: 8 bits, no parity */
    REG8(UART0 + 0) = 0x07;                /* UMR2: 1 stop bit */
    REG8(UART0 + 0x14) = 0;                /* UIMR: polled */
    REG8(UART0 + 0x18) = 0;                /* UBG1 */
    REG8(UART0 + 0x1C) = 132;              /* UBG2: 132 MHz / (32 x 132) = 31,250 baud exactly */
    REG8(UART0 + 8) = 0x05;                /* receiver and transmitter on */
}
static int midi_len(unsigned st)
{
    if (st >= 0xF8) return 1;
    switch (st & 0xF0) { case 0xC0: case 0xD0: return 2; case 0xF0: return st == 0xF2 ? 3 : st == 0xF1 || st == 0xF3 ? 2 : 1; }
    return 3;
}
int aos_midi_out(unsigned st, unsigned a, unsigned b)
{
    int n = midi_len(st);
    if ((uint16_t)(mo_w - mo_r) > sizeof mo_ring - 4) { midi_drops++; return 0; }
    mo_ring[mo_w++ % sizeof mo_ring] = (uint8_t)st;
    if (n > 1) mo_ring[mo_w++ % sizeof mo_ring] = (uint8_t)(a & 127);
    if (n > 2) mo_ring[mo_w++ % sizeof mo_ring] = (uint8_t)(b & 127);
    usb_midi_queue(st, a & 127, b & 127);
    midi_out_count++;
    return 1;
}
/* the DIN parser: running status, real-time bytes pass straight through, SysEx skipped */
static uint8_t mi_st, mi_d[2], mi_n;
uint32_t mlog_t[512], mlog_n; uint8_t mlog_b[512];   /* bring-up: every byte that came in, and when */
static void midi_byte_in(uint8_t c)
{
    mlog_t[mlog_n & 511] = now(); mlog_b[mlog_n & 511] = c; mlog_n++;
    if (c >= 0xF8) { seq_midi_rt(c); module_midi(c, 0, 0); return; }
    if (c & 0x80) { mi_st = c >= 0xF0 ? (c == 0xF0 || c == 0xF7 ? 0 : c) : c; mi_n = 0; if (mi_st && midi_len(mi_st) == 1) { module_midi(mi_st, 0, 0); mi_st = 0; } return; }
    if (!mi_st) return;
    mi_d[mi_n++] = c;
    if (mi_n + 1 >= midi_len(mi_st)) {
        if ((mi_st & 0xF0) == 0xC0) seq_midi_pc(mi_st & 15u, mi_d[0]);
        if ((mi_st & 0xF0) == 0x90) seq_midi_note(mi_st & 15u, mi_d[0], mi_d[1]);
        module_midi(mi_st, mi_d[0], mi_n > 1 ? mi_d[1] : 0); midi_in_count++;
        mi_n = 0;
        if (mi_st >= 0xF0) mi_st = 0;   /* system common messages have no running status */
    }
}
/* what is due goes out: called from the main loop and, so the sequencer's clock is even, from inside its long loops */
static void midi_tx(void)
{
    seq_midi_flush();                      /* the sequencer's notes and clock, each at its moment */
    for (int k = 0; k < 8 && mo_r != mo_w && (REG8(UART0 + 4) & 0x04); k++) REG8(UART0 + 0xC) = mo_ring[mo_r++ % sizeof mo_ring];
}
static void midi_poll(void)
{
    midi_tx();
    if (REG8(UART0 + 4) & 0x70) REG8(UART0 + 8) = 0x40;   /* overrun / framing: reset the error status */
    for (int k = 0; k < 16 && (REG8(UART0 + 4) & 0x01); k++) midi_byte_in(REG8(UART0 + 0xC));
}
