/* AERIA OS 0.1 -- bring-up probe. Boots with no Elektron code at all: takes the CPU, talks to the
 * front panel itself (UART1, polled), draws the AERIA logo, runs the lights, echoes keys and encoders,
 * logs every byte the panel says, then hands back to the flashed OS with a soft reset (SDRAM survives,
 * so the log in battery RAM is read over USB afterwards). Hardware facts: ../HARDWARE.md. */
#include <stdint.h>

#define REG8(a)  (*(volatile uint8_t  *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

/* ---- timer: DTIM3 free-running at the 132 MHz bus (HARDWARE.md 2.1) ---- */
#define DTIM3 0xFC07C000u
static void timer_init(void)
{
    REG16(DTIM3 + 0) = 0;                 /* stop */
    REG32(DTIM3 + 4) = 0xFFFFFFFFu;       /* reference: never */
    REG32(DTIM3 + 0xC) = 0;               /* clear the count */
    REG8(DTIM3 + 3) = 3;                  /* clear events */
    REG16(DTIM3 + 0) = 0x000B;            /* bus/1, free run, enabled (stock's value) */
}
static uint32_t now(void) { return REG32(DTIM3 + 0xC); }
/* ---- the caches, as stock runs them (HARDWARE.md 1.4): SDRAM 0x40000000-0x47FFFFFF copyback through ACR0,
 * everything else cache-inhibited precise; instruction and branch caches on. USB's memory lives in the uncached
 * alias (usb.c), and nothing else in AERIA OS is touched by a DMA master. ---- */
__attribute__((unused)) static void cache_on(void)
{
    __asm__ volatile("move.l #0x4007E020,%%d0\n\tmovec %%d0,%%acr0\n\tmoveq #0,%%d0\n\tmovec %%d0,%%acr1\n\t"
                     "move.l #0xA50CE100,%%d0\n\tmovec %%d0,%%cacr\n\tnop" ::: "d0", "memory");
}
/* write every dirty data line back: CPUSHL over all 512 sets x 4 ways (16 KB data cache) */
static void cache_push_all(void)
{
    __asm__ volatile("moveq #0,%%d0\n"
                     "1:\tmove.l %%d0,%%a0\n\tcpushl %%dc,(%%a0)\n\tlea 1(%%a0),%%a0\n\tcpushl %%dc,(%%a0)\n\tlea 1(%%a0),%%a0\n\t"
                     "cpushl %%dc,(%%a0)\n\tlea 1(%%a0),%%a0\n\tcpushl %%dc,(%%a0)\n\t"
                     "add.l #0x10,%%d0\n\tcmp.l #0x2000,%%d0\n\tbcs.s 1b" ::: "d0", "a0", "memory");
}
static void cache_off(void)
{
    __asm__ volatile("move.l #0x01040100,%%d0\n\tmovec %%d0,%%cacr\n\tnop\n\tmoveq #0,%%d0\n\tmovec %%d0,%%acr0\n\tnop" ::: "d0", "memory");
}
#define US(n) ((uint32_t)(n) * 132u)
#define MS(n) ((uint32_t)(n) * 132000u)

/* ---- the log, in battery RAM nothing else uses (0x100f9000-0x100fc000: past stock's last use 0x100f8f06,
 * below Modwerk's diag log 0x100fc000), so it survives the reset and the base's MEM can read it ---- */
typedef struct {
    uint32_t magic, stage, rx_count, reports, fault_vec, fault_pc, loops, uart_regs[8];
    uint32_t keys_seen, enc_seen, xfade_seen, ident_bytes, last_key, exit_why, polls, reports_listen, reports_poll;
    uint32_t dsp_drained, dsp_sent, dsp_timeouts, dsp_isr_after, dsp_handback_isr, dsp_beats, dsp_last_beat, dsp_beat_or, ms_run;
    uint32_t beat_hist[32], beat_phase[32], handback_drained;
    uint32_t ident_at[8]; uint8_t ident_more[8][12];
    uint8_t  ident[16];
    uint8_t  rx[256];
    uint32_t tx_count;
    uint8_t  txlog[256];
    uint8_t  gpio_boot[256], gpio_run[256];
} Log;
#define LOG ((volatile Log *)0x100F9000u)
/* the DSP's register dumps: bring-up only, now in plain RAM (battery RAM 0x100F9800-0x100FBA00 holds the presets) */
static uint32_t regs_ram[512];
#define REGS ((volatile uint32_t *)regs_ram)
typedef char log_fits[sizeof(Log) <= 0x800 ? 1 : -1];   /* below 0x100F9800: AERIA's saved state and presets */
static void log_byte(uint8_t b) { uint32_t n = LOG->rx_count; if (n < sizeof LOG->rx) LOG->rx[n] = b; LOG->rx_count = n + 1; }
static void log_mark(uint8_t step) { log_byte(0xEE); log_byte(step); LOG->stage = step; }

/* ---- the panel link: UART1, as the bootstrap left it (HARDWARE.md 3.1) ---- */
#define UART1 0xFC064000u
#define USR_RXRDY 0x01
#define USR_TXRDY 0x04
static void tx(uint8_t b)
{
    uint32_t t = now();
    while (!(REG8(UART1 + 4) & USR_TXRDY)) if (now() - t > MS(5)) { LOG->fault_pc = LOG->fault_pc + 1; return; }   /* fault_pc counts TX timeouts here */
    REG8(UART1 + 0xC) = b;
    uint32_t n = LOG->tx_count; if (n < sizeof LOG->txlog) LOG->txlog[n] = b; LOG->tx_count = n + 1;
}
/* The panel link as stock's start-up brings it up (register values read from 1.40C's panel init for
 * understanding; HARDWARE.md 3.7): the panel GPIO lines (one pulsed), then UART1 reset and set to 312,500 baud
 * (bus clock / (32 x 13)), 8N1, enabled. 0.1-0.3 reused the bootstrap's UART as it was: the panel answered,
 * but the LCD never left the bootstrap's text screen. */
static void panel_lines_and_uart(void)
{
    static const uint8_t zero_at[] = {0x7B, 0x83, 0x78, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82};
    REG8(0xFC0A4075u) = 1;
    REG8(0xFC0A4079u) = 0x10;
    for (unsigned i = 0; i < sizeof zero_at; i++) REG8(0xFC0A4000u + zero_at[i]) = 0;
    REG8(0xFC0A4022u) = REG8(0xFC0A4022u) | 0x20;
    REG8(0xFC0A4022u) = REG8(0xFC0A4022u) | 0x40;
    REG8(0xFC0A4052u) = 0xBF;
    { uint32_t t = now(); while (now() - t < US(10)) {} }
    REG8(0xFC0A4022u) = REG8(0xFC0A4022u) & (uint8_t)~0x40;
    /* stock's MKI/MKII probe, as it runs it: bit 5 high, read bit 6, bit 5 low, read bit 6 -- it leaves the
     * line low, which the panel may read (0.6: the panel never left its loader without it) */
    for (int i = 0; i < 10; i++) {
        REG8(0xFC0A403Au) = 0x20;
        { uint32_t t = now(); while (now() - t < US(2)) {} }
        if (!(REG8(0xFC0A403Au) & 0x40)) continue;
        REG8(0xFC0A4052u) = 0xDF;
        { uint32_t t = now(); while (now() - t < US(2)) {} }
        if (!(REG8(0xFC0A403Au) & 0x40)) { LOG->uart_regs[1] = 0x2; break; }   /* MKII: bit 6 follows bit 5 */
        LOG->uart_regs[1] = 0x1;
    }
    LOG->uart_regs[2] = REG8(0xFC0A403Au);
    /* UART1 */
    REG8(0xFC0A4069u) = 0x33;              /* the UART pins */
    REG8(0xFC0A4066u) = 0x43;
    REG8(UART1 + 8) = 0x20;                /* UCR: reset receiver */
    REG8(UART1 + 8) = 0x30;                /* reset transmitter */
    REG8(UART1 + 8) = 0x40;                /* reset error status */
    REG8(UART1 + 8) = 0x10;                /* reset the mode pointer */
    REG8(UART1 + 4) = 0xDD;                /* UCSR: the bus clock both ways */
    REG8(UART1 + 0) = 0x13;                /* UMR1: 8 bits, no parity */
    REG8(UART1 + 0) = 0x07;                /* UMR2: 1 stop bit */
    REG8(UART1 + 0x14) = 0;                /* UIMR: polled */
    REG8(UART1 + 0x18) = 0;                /* UBG1 */
    REG8(UART1 + 0x1C) = 13;               /* UBG2: 132 MHz / (32 x 13) = 317 kbaud, as stock's integer divide gives */
    REG8(UART1 + 8) = 0x05;                /* UCR: receiver and transmitter on */
}
static int rx_ready(void) { return REG8(UART1 + 4) & USR_RXRDY; }
static int rx_wait(uint32_t timeout)
{
    uint32_t t = now();
    while (!rx_ready()) if (now() - t > timeout) return -1;
    uint8_t b = REG8(UART1 + 0xC);
    log_byte(b);
    return b;
}

/* ---- the screen: 128x64, 8 pages, page 7 at the top, bit 0 = top row of the band (HARDWARE.md 3.3) ---- */
#define W 128
#define H 64
static uint8_t fb[8][W], sent[8][W];
static uint8_t blk_force[8][16];    /* 1 = send this block whatever it holds (start, full redraws) */
static int sent_valid;              /* 0 = every block forced on the next passes (the first frame, a full redraw) */
static void cls(void) { for (int p = 0; p < 8; p++) for (int x = 0; x < W; x++) fb[p][x] = 0; }
static void px(int x, int y) { if ((unsigned)x < W && (unsigned)y < H) fb[7 - y / 8][x] |= (uint8_t)(1u << (y & 7)); }
/* send the blocks that changed (or are forced), at most `budget` a call so keys stay responsive. Each block
 * keeps its own state, so a pass cut short by the budget carries on where it stopped next time (0.1-0.9 only
 * marked the frame sent after a whole pass, so the budget restarted it at block 0 forever: the panel got the
 * same four blank blocks a million times and the screen never changed) */
static void stream_side(void);
static void lcd_flush(int budget)
{
    if (!sent_valid) { for (int p = 0; p < 8; p++) for (int b = 0; b < 16; b++) blk_force[p][b] = 1; sent_valid = 1; }
    /* round the screen from where the last call stopped: always starting at the bottom left the top rows behind
     * whenever much changed (the storm's rain: the top of the screen came in pieces, 12 Nov 2026) */
    static int at;
    for (int n = 0; n < 128; n++) {
            int i = (at + n) & 127, p = i >> 4, c = (i & 15) << 3;
            int same = !blk_force[p][c >> 3];
            for (int k = 0; k < 8 && same; k++) same = sent[p][c + k] == fb[p][c + k];
            if (same) continue;
            if (budget-- <= 0) { at = i; return; }
            tx((uint8_t)(0x10 | p)); tx((uint8_t)c);
            for (int k = 0; k < 8; k++) { tx(fb[p][c + k]); sent[p][c + k] = fb[p][c + k]; }
            blk_force[p][c >> 3] = 0;
            stream_side();                 /* ten bytes to the panel are a long time for the stream: it is fed between blocks (a whole new
                                            * screen starved the sequencer for 12-27 ms: measured 12 Nov 2026) */
        }
}

/* AERIA's 3x5 font */
static const uint16_t glyphs[] = {
    0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79a7, 0x79a4, 0x396b, 0x5bed, 0x7497, 0x126a,
    0x5bad, 0x4927, 0x5fed, 0x6b6d, 0x2b6a, 0x6ba4, 0x2b73, 0x6bad, 0x388e, 0x7492,
    0x5b6f, 0x5b6a, 0x5bfd, 0x5aad, 0x5a92, 0x72a7, 0x7b6f, 0x2c97, 0x62a7, 0x628e,
    0x5bc9, 0x798e, 0x39ef, 0x7292, 0x7bef, 0x7bce, 0x05d0, 0x01c0, 0x0410};
static int glyph_of(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    if (c == '+') return 36;
    if (c == '-') return 37;
    if (c == ':') return 38;
    return -1;
}
__attribute__((unused)) static void text(int x, int y, const char *s)
{
    for (; *s; s++, x += 4) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (glyphs[g] & (1u << (14 - (r * 3 + c)))) px(x + c, y + r);
    }
}
/* a digit 4x the font: the slot number, readable from across the room */
__attribute__((unused)) static void big_digit(int x, int y, int d)
{
    const uint16_t g = glyphs[26 + d];
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (g & (1u << (14 - (r * 3 + c))))
        for (int yy = 0; yy < 4; yy++) for (int xx = 0; xx < 4; xx++) px(x + c * 4 + xx, y + r * 4 + yy);
}
__attribute__((unused)) static int text_w(const char *s) { int n = 0; while (s[n]) n++; return n * 4 - 1; }
__attribute__((unused)) static void hex2(char *o, unsigned v) { const char *d = "0123456789ABCDEF"; o[0] = d[(v >> 4) & 15]; o[1] = d[v & 15]; o[2] = 0; }

#include "logo.inc"   /* the AERIA brush logo: 110 columns, two words each (rows 0-31, 32-63) */
__attribute__((unused)) static void draw_logo(int reveal)
{
    int x0 = (W - LOGO_W) / 2;
    for (int c = 0; c < LOGO_W && c < reveal; c++)
        for (int y = 0; y < H; y++)
            if ((y < 32 ? logo_cols[c][0] >> y : logo_cols[c][1] >> (y - 32)) & 1u) px(x0 + c, y);
}

/* ---- LEDs: id = row*8 + bit; trig n's lamp is row (n-1)/4, bit 2*((n-1)%4) (HARDWARE.md 3.4) ---- */
static uint8_t led_rows[17], led_sent[17];
static void led_set(int id, int on) { if (on) led_rows[id >> 3] |= (uint8_t)(1u << (id & 7)); else led_rows[id >> 3] &= (uint8_t)~(1u << (id & 7)); }
__attribute__((unused)) static int trig_led(int n) { return ((n >> 2) << 3) + 2 * (n & 3); }   /* n = 0..15 */
static void led_flush(int force)
{
    /* only rows 0x20-0x2a, the ones stock's init sends: other first bytes may mean something else to the
     * MKII panel, and a misread message would desync its parser for everything after (0.1-0.4 drew nothing) */
    for (int r = 0; r < 11; r++) if (force || led_rows[r] != led_sent[r]) {
        tx((uint8_t)(0x20 | r)); tx(led_rows[r]); led_sent[r] = led_rows[r];
    }
}

/* ---- panel reports (HARDWARE.md 3.6) ---- */
static uint8_t keyrow[8];
static int enc[7], xfade = -1, last_key = -1, held_func, held_no;
static uint16_t evq[64]; static int evq_n;   /* bit 15 encoder (8..14 index, 0..7 delta), else bit 8 pressed + key */
static void ev_push(uint16_t e) { if (evq_n < 64) evq[evq_n++] = e; }
static void report(uint8_t op, const uint8_t *d)
{
    LOG->reports = LOG->reports + 1;
    if ((op & 0xF0) == 0x20 && (op & 15) < 8) {
        int r = op & 15; uint8_t was = keyrow[r], m = d[0];
        for (int b = 0; b < 8; b++) if (((m ^ was) >> b) & 1) {
            int code = r * 8 + b, down = (m >> b) & 1;
            ev_push((uint16_t)((down ? 0x100 : 0) | code));
            if (down) { last_key = code; LOG->last_key = (uint32_t)last_key; }
        }
        keyrow[r] = m; LOG->keys_seen = LOG->keys_seen + 1;
        held_func = (keyrow[0x2D >> 3] >> (0x2D & 7)) & 1;
        held_no = (keyrow[0x32 >> 3] >> (0x32 & 7)) & 1;
    } else if ((op & 0xF0) == 0x30 && (op & 15) < 7) {
        enc[op & 15] += (int8_t)d[0]; LOG->enc_seen = LOG->enc_seen + 1;
        ev_push((uint16_t)(0x8000 | (op & 15) << 8 | d[0]));
    } else if (op == 0x40) { xfade = d[0]; LOG->xfade_seen = LOG->xfade_seen + 1; }
}
/* the parser: the first byte says how many follow */
static uint8_t pk[12]; static int pk_n, pk_want;
static int ident_slot = -1, ident_n;
static void poll_panel(void)
{
    int guard = 64;
    while (rx_ready() && guard--) {
        uint8_t b = REG8(UART1 + 0xC);
        log_byte(b);
        if (ident_slot >= 0 && ident_n < 12) LOG->ident_more[ident_slot][ident_n++] = b;
        if (pk_n == 0) {
            uint8_t hi = b & 0xF0;
            pk_want = hi == 0x20 || hi == 0x30 || hi == 0x40 ? 2 : hi == 0x70 ? 10 : 0;
            if (!pk_want) continue;          /* not a report start: resync on the next byte */
        }
        pk[pk_n++] = b;
        if (pk_n >= pk_want) { report(pk[0], pk + 1); pk_n = 0; }
    }
}

/* ---- leaving: put the panel back in its start-up state and soft-reset into the flashed OS ---- */
#define QUIET_PANEL 0
#ifdef AERIA_FLASH
#define AERIA_FLASHED 1
#else
#define AERIA_FLASHED 0
#endif
#ifdef AERIA_FLASH         /* the flashed build (build.sh with FLASH=1): no bring-up hand-backs, no loopback guard */
#define DEV_MINUTES 0u
#else
#define DEV_MINUTES 0u    /* no timed hand-back: it froze the unit at the loader more than once (12 Nov 2026); FUNC+NO+PROJ still hands back */
#endif
static int dsp_running, warm;
static uint8_t ata_cr_boot;
static void dsp_park(void);
static void hand_back(uint32_t why)
{
    LOG->exit_why = why;
    if (dsp_running) dsp_park();
    REG8(0x90000024u) = ata_cr_boot;   /* the card controller as the bootstrap left it */
    *(volatile uint32_t *)0xFC0B0140u &= ~1u;   /* USB off the bus first (as Modwerk's own resets do) */
    *(volatile uint32_t *)0xFC0B0148u = 0;
    if (!warm) { tx(0x60); tx(0x02); }
    uint32_t t = now(); while (now() - t < MS(30)) {}
    REG8(0xFC0A0000u) = 0x80;     /* RCR: soft reset; SDRAM is kept, the DSPs are left as they are */
    for (;;) {}
}
#include "card.c"
/* the card's listing for the bring-up check: battery RAM 0x100FBD00-0x100FC000 ('CARD', status, names to +0x1F0,
 * the sampler's report at +0x200). 0x100FBA00-0x100FBD00 holds the loaded samples' paths (sampler.c). */
#define CARDLOG ((volatile uint8_t *)0x100FBD00u)
__attribute__((unused)) static void card_log(void)
{
    volatile uint32_t *h = (volatile uint32_t *)CARDLOG;
    h[0] = 0x43415244u; h[1] = 0; h[2] = 0; h[3] = 0;
    uint32_t t = now();
    h[1] = (uint32_t)card_init();
    h[3] = (now() - t) / 132000u;
    if (!h[1]) return;
    h[2] = (uint32_t)fat_mount();
    if (!h[2]) return;
    unsigned o = 16; char nm[64]; uint8_t a; uint32_t c, sz;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t cl = 0;
        if (pass) { if (!fat_find("/AERIA", &c, &sz, &a)) break; cl = c; CARDLOG[o++] = '#'; }
        dir_open(cl);
        while (dir_next(nm, sizeof nm, &a, &c, &sz)) {
            for (int i = 0; nm[i] && o < 0x1F0; i++) CARDLOG[o++] = (uint8_t)nm[i];
            if (o < 0x1F0) CARDLOG[o++] = (a & 0x10) ? '/' : '|';
            if (o >= 0x1F0) break;
        }
    }
    CARDLOG[o] = 0;
}
void aeria_fault(uint32_t fmt, uint32_t pc)
{
    LOG->fault_vec = (fmt >> 18) & 0xFF;
    LOG->fault_pc = pc;
    hand_back(0xFA);
}

/* ---- the DSP: core 0's host port (HI08 host side, 16-bit accesses, value in the low byte; AUDIO.md 2.2) ---- */
#define HI_ICR 0x20000000u
#define HI_CVR 0x20000004u
#define HI_ISR 0x20000008u
#define HI_TXH 0x20000014u
#define HI_TXM 0x20000018u
#define HI_TXL 0x2000001Cu
#define DSP_SELECT 0xFC0A400Cu
#include "dsp_slots.inc"
#include "dsp_stage1.inc"   /* the first stage a cold boot ROM takes (P:$31000 core 0, $32000 core 1) */
#include "dsp_stage1b.inc"  /* our DSP program (dsp/slots.asm): a tone on one TX slot, RX peaks once a second */
#include "dsp_core1.inc"  /* core 1 (dsp/core1.asm): stock's three per-core start-up writes, then idle */
#define DSP_THRU_ORG DSP_SLOTS_ORG
#define dsp_thru dsp_slots
static int hi_put(uint32_t w)
{
    uint32_t t = now();
    while (!(REG16(HI_ISR) & 0x06)) if (now() - t > MS(20)) { LOG->dsp_timeouts = LOG->dsp_timeouts + 1; return 0; }
    REG16(HI_TXH) = (uint16_t)((w >> 16) & 0xFF);
    REG16(HI_TXM) = (uint16_t)((w >> 8) & 0xFF);
    REG16(HI_TXL) = (uint16_t)(w & 0xFF);
    LOG->dsp_sent = LOG->dsp_sent + 1;
    return 1;
}
/* re-park a core before loading it: host command $0F (vector P:$1E, Modwerk's park in both stock payloads' low
 * memory, which our programs never overwrite) pulls a core that wandered off during the soft reset back into a
 * clean boot-ROM stand-in. Returns 1 if the core took the command. */
static int dsp_repark(int core)
{
    REG8(DSP_SELECT) = (uint8_t)core;
    REG16(HI_CVR) = 0x008F;
    uint32_t t = now();
    while (REG16(HI_CVR) & 0x80) if (now() - t > MS(50)) { REG16(HI_CVR) = 0x000F; REG8(DSP_SELECT) = 0; return 0; }   /* nothing took it (a power-on boot ROM): withdrawn */
    t = now(); while (now() - t < MS(5)) {}
    for (int i = 0; i < 16 && (REG16(HI_ISR) & 1); i++) (void)REG16(HI_TXL);
    REG8(DSP_SELECT) = 0;
    return 1;
}
/* core 0 waits in a boot ROM (a power-on) or its stand-in (a parked core): count, address, words, jump */
static void dsp_boot_thru(void)
{
    REG8(DSP_SELECT) = 0;
    for (int i = 0; i < 16 && (REG16(HI_ISR) & 1); i++) { (void)REG16(HI_TXL); LOG->dsp_drained = LOG->dsp_drained + 1; }
    REG16(HI_ICR) = 0x81;                 /* INIT|RREQ: the interface reset stock does before an upload */
    REG16(HI_CVR) = 0;
    /* stage 1 through the boot ROM (or a parked core's stand-in), then the engine through stage 1 */
    { unsigned m = sizeof dsp_stage1 / sizeof dsp_stage1[0];
      if (!hi_put(m) || !hi_put(DSP_STAGE1_ORG)) return;
      for (unsigned i = 0; i < m; i++) if (!hi_put(dsp_stage1[i])) return;
      uint32_t t = now(); while (now() - t < MS(2)) {} }
    unsigned n = sizeof dsp_thru / sizeof dsp_thru[0];
    if (!hi_put(n) || !hi_put(DSP_THRU_ORG)) return;
    for (unsigned i = 0; i < n; i++) if (!hi_put(dsp_thru[i])) return;
    REGS[0] = 0x52454753u; REGS[1] = 0;
    /* the program first sends its 153 peripheral registers as it found them, then again after its own set-up */
    for (unsigned i = 0; i < 252; i++) {
        uint32_t t0 = now();
        while (!(REG16(HI_ISR) & 1)) if (now() - t0 > MS(200)) goto dumped;
        REGS[2 + i] = (uint32_t)(REG16(HI_TXH) & 0xFF) << 16 | (uint32_t)(REG16(HI_TXM) & 0xFF) << 8 | (REG16(HI_TXL) & 0xFF);
        REGS[1] = i + 1;
    }
dumped:;
    uint32_t t = now(); while (now() - t < MS(5)) {}
    LOG->dsp_isr_after = REG16(HI_ISR) & 0xFF;
}
static uint32_t cur_phase = 99;
static void stream_meter(uint32_t w);
static uint32_t dsp_words;
static void stream_pump(void);
static void midi_tx(void);
static int seq_mode;
static int stems_sent;
static int stem_left, stem_at, stem_trk, stem_wide; static int32_t stem_blk[256]; static uint32_t stem_blocks, stem_have;
static uint8_t stem_stereo;   /* a bit a track: it has an effect, so its stem comes as pairs (else its left alone, doubled here) */   /* sampler.c: the sequencer's stems, 256 words a frame */
static void dsp_poll(void)
{
    REG8(DSP_SELECT) = 0;
    /* everything the engine sends comes through its ring: meters, the stream's count, recorded audio -- drain it */
    /* keep reading while the engine keeps sending (it refills the port within a microsecond or two between its
     * frames): up to 2 ms a call, until 20 us pass with nothing */
    uint32_t t0 = now();
    for (int idle = 0;;) {
        if (!(REG16(HI_ISR) & 1)) {
            if (++idle > 64) { if (now() - t0 > MS(2)) break; idle = 0; if (!(REG16(HI_ISR) & 1)) { uint32_t t1 = now(); while (!(REG16(HI_ISR) & 1) && now() - t1 < US(20)) {} if (!(REG16(HI_ISR) & 1)) break; } }
            continue;
        }
        idle = 0;
        dsp_words++;
        if (!(dsp_words & 63)) { stream_pump(); if (seq_mode) midi_tx(); }   /* a long drain (USB audio's tap: 265,000 words a second) must not starve the samples */
        uint32_t w = (uint32_t)(REG16(HI_TXH) & 0xFF) << 16 | (uint32_t)(REG16(HI_TXM) & 0xFF) << 8 | (REG16(HI_TXL) & 0xFF);
        if (stem_left && (w & 0x800000u)) {          /* a stem's word: straight to its place (705,600 of them a second) */
            /* ... and the ones behind it, in a loop of their own: they come one after another, a microsecond apart */
            for (;;) {
                int32_t v = (int32_t)(w << 9) >> 8;
                if (stem_wide) stem_blk[stem_at++] = v; else { stem_blk[stem_at] = v; stem_blk[stem_at + 1] = v; stem_at += 2; }
                if (!(stem_at & 31)) { stem_trk++; stem_wide = (stem_stereo >> (stem_trk & 7)) & 1; }
                if (!--stem_left) { stem_blocks++; stem_have = 1; break; }
                if (!(REG16(HI_ISR) & 1)) { int k = 0; while (!(REG16(HI_ISR) & 1) && ++k < 24) {} if (!(REG16(HI_ISR) & 1)) break; }
                w = (uint32_t)(REG16(HI_TXH) & 0xFF) << 16 | (uint32_t)(REG16(HI_TXM) & 0xFF) << 8 | (REG16(HI_TXL) & 0xFF);
                dsp_words++;
                if (!(w & 0x800000u)) { stream_meter(w); break; }      /* (never inside a block: the block is over) */
            }
            continue;
        }
        stream_meter(w);
        if (w & 0x800000) continue;
        uint32_t n = LOG->dsp_beats;
        if (n < 32) { LOG->beat_hist[n] = w; LOG->beat_phase[n] = cur_phase; }
        LOG->dsp_beats = n + 1; LOG->dsp_last_beat = w; LOG->dsp_beat_or = LOG->dsp_beat_or | w;
    }
}
/* core 1 gets its own start-up program too: stock's payload B writes the same three per-core registers */
static int dsp_boot_core1(void)
{
    REG8(DSP_SELECT) = 1;
    for (int i = 0; i < 16 && (REG16(HI_ISR) & 1); i++) (void)REG16(HI_TXL);
    REG16(HI_ICR) = 0x81;
    REG16(HI_CVR) = 0;
    unsigned m = sizeof dsp_stage1b / sizeof dsp_stage1b[0];
    int ok = hi_put(m) && hi_put(DSP_STAGE1B_ORG);
    for (unsigned i = 0; ok && i < m; i++) ok = hi_put(dsp_stage1b[i]);
    { uint32_t t = now(); while (now() - t < MS(2)) {} }
    unsigned n = sizeof dsp_core1 / sizeof dsp_core1[0];
    ok = ok && hi_put(n) && hi_put(DSP_CORE1_ORG);
    for (unsigned i = 0; ok && i < n; i++) ok = hi_put(dsp_core1[i]);
    /* core 1 now runs its effect engine (dsp/core1.asm): no register dump, just its hello */
    /* core 1 says hello ($c0ffee) once it runs our program */
    { uint32_t t = now(); while (ok && !(REG16(HI_ISR) & 1) && now() - t < MS(50)) {}
      if (REG16(HI_ISR) & 1) LOG->uart_regs[6] = (uint32_t)(REG16(HI_TXH) & 0xFF) << 16 | (uint32_t)(REG16(HI_TXM) & 0xFF) << 8 | (REG16(HI_TXL) & 0xFF); }
    REG8(DSP_SELECT) = 0;
    return ok;
}
static int dsp1_running;
/* our DSP programs poll HF0: each stops what it runs and becomes a boot ROM again for the next OS */
static void dsp_park(void)
{
    if (dsp1_running) {
        REG8(DSP_SELECT) = 1;
        REG16(HI_ICR) = 0x08;
        { uint32_t t = now(); while (now() - t < MS(5)) {} }
        for (int i = 0; i < 8 && (REG16(HI_ISR) & 1); i++) (void)REG16(HI_TXL);
    }
    REG8(DSP_SELECT) = 0;
    REG16(HI_ICR) = 0x08;                 /* HF0 */
    uint32_t t = now(); while (now() - t < MS(10)) {}
    LOG->dsp_handback_isr = REG16(HI_ISR) & 0xFF;
    /* a heartbeat left in the DSP's outbox would read as the next OS's first upload echo */
    for (int i = 0; i < 8 && (REG16(HI_ISR) & 1); i++) { (void)REG16(HI_TXL); LOG->handback_drained = LOG->handback_drained + 1; }
}


/* the ColdFire peripheral snapshot (the same regions, order and layout as the AERIA module's in-stock test,
 * cloudfin/parts/stocktest.c), to battery RAM 0x100FB400: what stock sets up that a soft reset clears */
static void snap(volatile uint8_t **o, uint32_t base, unsigned n) { for (unsigned i = 0; i < n; i++) *(*o)++ = REG8(base + i); }
static void cf_snapshot(void)
{
    return;   /* bring-up only: it wrote over presets 14-16 in battery RAM */
    volatile uint8_t *o = (volatile uint8_t *)0x100FB400u;
    snap(&o, 0xFC0A0000u, 64); snap(&o, 0xFC0C4000u, 16); snap(&o, 0xFC008000u, 96); if (!warm) snap(&o, 0xFC0BC010u, 64); else o += 64;
    snap(&o, 0xFC070000u, 16); snap(&o, 0xFC074000u, 16); snap(&o, 0xFC078000u, 16); snap(&o, 0xFC07C000u, 16);
    snap(&o, 0xFC080000u, 8); snap(&o, 0xFC084000u, 8); snap(&o, 0xFC088000u, 8); snap(&o, 0xFC08C000u, 8);
    snap(&o, 0xFC094000u, 16); snap(&o, 0xFC048000u, 64); snap(&o, 0xFC04C000u, 64); snap(&o, 0xFC044000u, 64);
    snap(&o, 0xFC05C000u, 56); snap(&o, 0xFC000000u, 64);
    o = (volatile uint8_t *)0x100FB800u;
    snap(&o, 0xFC0A4000u, 256);
    if (!warm) { snap(&o, 0xFC040000u, 128); snap(&o, 0xFC004000u, 256); snap(&o, 0xFC0B8000u, 64); snap(&o, 0xFC0B0000u, 512); }
}

/* ---- the audio codec: Cirrus Logic CS42432 (4 in, 6 out, TDM), on the ColdFire's DSPI chip select 0 ----
 * A write is three SPI bytes: chip address 0x9E (1001111, write), the register, the value (the same frames
 * 1.40C's 0x4001c35c sends, read for understanding). Its registers sit in the codec, so a ColdFire restart
 * does not reset them: they hold whatever the last OS left -- which is why AERIA OS's outputs stayed silent
 * after every RAM boot until it wrote register 07 itself (9 Nov 2026). */
#define DSPI_SR    0xFC05C02Cu
#define DSPI_PUSHR 0xFC05C034u
#define DSPI_POPR  0xFC05C038u
static int codec_write(int reg, int val)
{
    REG32(DSPI_PUSHR) = 0x8001009Eu;                          /* CONT, CS0: chip address, write */
    REG32(DSPI_PUSHR) = 0x80010000u | (uint32_t)(reg & 0xFF);  /* the register (MAP) */
    REG32(DSPI_PUSHR) = 0x00010000u | (uint32_t)(val & 0xFF);  /* the value, CS released after it */
    uint32_t t = now();
    while (((REG32(DSPI_SR) >> 4) & 15) != 3) if (now() - t > MS(5)) return 0;
    (void)REG32(DSPI_POPR); (void)REG32(DSPI_POPR); (void)REG32(DSPI_POPR);
    REG32(DSPI_SR) = 0x00020000u;                             /* clear the receive-drain flag, as stock does */
    return 1;
}
/* ---- the real-time clock: a DS1390-style SPI clock on the DSPI chip select 1 (found in the emulator's notes of
 * stock: one register a transaction, `<reg> 00` with the select held between the two frames; 0x01 seconds, 0x02
 * minutes, 0x03 hours, BCD). It keeps the time stock's SET DATE/TIME gave it, on the unit's battery. */
static int rtc_read(int reg)
{
    REG32(DSPI_PUSHR) = 0x80020000u | (uint32_t)(reg & 0xFF);   /* CONT, chip select 1 (measured on the unit: 2 answers nothing) */
    REG32(DSPI_PUSHR) = 0x00020000u;
    uint32_t t = now();
    while (((REG32(DSPI_SR) >> 4) & 15) != 2) if (now() - t > MS(5)) return -1;
    (void)REG32(DSPI_POPR); int v = (int)(REG32(DSPI_POPR) & 0xFF);
    REG32(DSPI_SR) = 0x00020000u;
    return v;
}
/* seconds since midnight, or -1 when the clock does not answer sensibly */
int aos_clock_seconds(void)
{
    int s = rtc_read(1), m = rtc_read(2), h = rtc_read(3);
    if (s < 0 || m < 0 || h < 0) return -1;
    s = (s >> 4 & 7) * 10 + (s & 15); m = (m >> 4 & 7) * 10 + (m & 15);
    h = (h & 0x40) ? ((h >> 4 & 1) * 10 + (h & 15)) % 12 + ((h & 0x20) ? 12 : 0) : (h >> 4 & 3) * 10 + (h & 15);   /* 12- or 24-hour */
    if (s > 59 || m > 59 || h > 23) return -1;
    return h * 3600 + m * 60 + s;
}
#define CODEC_DAC_MUTE 0x07   /* one bit per DAC channel, 1 = muted */

/* gcc may call these */
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; } return d; }
unsigned strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }


static unsigned char ui_frame[1024] __attribute__((aligned(4)));
#include "sampler.c"
#include "seq.c"
#include "nor.c"
#include "midi.c"
static void dsp_write(uint32_t addr, uint32_t val, int yspace);
#include "usb.c"

/* ---- AERIA's UI (aeria_ui.c: cloudfin/aeria12.c in its PREVIEW platform) ---- */
struct modwerk_runtime_api { volatile uint32_t value; };
void module_tick(struct modwerk_runtime_api *api);
void module_draw(unsigned char *frame);
int module_key(int code, int pressed);
int module_enc(int encoder, int delta);
extern uint8_t preview_lane[8][72];
extern int preview_leds[128];
extern uint32_t preview_xfade, preview_step, preview_play;
static struct modwerk_runtime_api ui_api;
static uint32_t ui_ticks;
static void fx_upload(void); static void fx_default_chain(void);
static void grain_win_init(void);
static void aeria_ui_start(void) { cls(); sent_valid = 0; grain_win_init(); fx_upload(); fx_default_chain(); midi_init(); }
void module_fast(uint32_t us);   /* the UI's MIDI modules: precise timing between ticks (marbles.c, pong.c) */
static void aeria_ui_events(void)
{
    for (int i = 0; i < evq_n; i++) {
        uint16_t e = evq[i];
        if (e & 0x8000) module_enc((e >> 8) & 0x7F, (int8_t)(e & 0xFF));
        else module_key(e & 0xFF, (e >> 8) & 1);
    }
    evq_n = 0;
    if (xfade >= 0) preview_xfade = (uint32_t)xfade >> 1;
}
static void aeria_ui_tick(void) { CRUMB(0x06); module_tick(&ui_api); CRUMB(0x07); if ((++ui_ticks & 7) == 0) preview_step++; sampler_tick(); }
/* AERIA draws 128 columns of two big-endian words (line y = bit y&31 of word 2x + (y < 32)); the LCD wants
 * 8 pages per column, page 7 at the top */
static void aeria_ui_draw(void)
{
    module_draw(ui_frame);
    const uint32_t *w = (const uint32_t *)ui_frame;
    for (int x = 0; x < W; x++) {
        uint32_t top = w[2 * x + 1], bot = w[2 * x];
        for (int p = 0; p < 4; p++) { fb[7 - p][x] = (uint8_t)(top >> (8 * p)); fb[3 - p][x] = (uint8_t)(bot >> (8 * p)); }
    }
}
/* ---- the lamps (sadnoise, 10 Nov 2026): every lamp a very dim white by default, what AERIA lights brighter,
 * the key under a finger full -- and every change a fade, one level a tick (a quarter second end to end) ---- */
#define LAMP_DIM 1
#define LAMP_ON 9
#define LAMP_PRESSED 15
/* the A id of each lamp in the measured map (HARDWARE.md 3.4): trigs, tracks, scene A/B, tempo, pages, MIDI,
 * arranger, STOP, PLAY, REC, mixer, project, part */
static const uint8_t lamp_id[] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 40, 42, 44, 46, 48, 50, 52, 54,
                                  32, 34, 38, 66, 68, 86, 80, 82, 70, 64, 88, 90, 84, 102, 120, 122, 72, 74, 76, 78};   /* (72-78: the four lights above PAGE, found 12 Nov 2026 with the owner watching) */
/* the key that lights each of those lamps when pressed (0xFF none) */
static const uint8_t lamp_key[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x19, 0x1a, 0xFF, 0x22, 0x23, 0x24, 0x25, 0x26,
                                   0x35, 0x1f, 0x27, 0x28, 0x29, 0x1e, 0x1c, 0x1d, 0xFF, 0xFF, 0xFF, 0xFF};
#define N_LAMPS ((int)sizeof lamp_id)
static uint8_t lamp_lv[N_LAMPS], lamp_sent[N_LAMPS];
/* the level lamps above REC1 and REC2 (inputs A B C D: ids 62 60 56 58), the card's lamp (36) and, in the sky, the
 * lamp above ARR (64) as the main output's level -- found on 12 Nov 2026 by lighting the unmapped ids one by one.
 * A lamp's brightness is its level: 3.5 dB a step, the last step 1 dB under full scale. */
extern uint32_t vu_now[6]; extern uint32_t card_act_n; extern int card_ok;
static int vu_level(uint32_t v) { uint32_t t = 0xE0000u; int lv = 15; while (lv > 0 && v < t) { t = t * 2u / 3u; lv--; } return lv; }
static const uint8_t vu_id[5] = {62, 60, 56, 58, 36};
static uint8_t vu_sent[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static int vu_out;   /* the main pair's level, 0..15 */
static void vu_lamps(void)
{
    static uint32_t act_seen; static int act_hold;
    int lv[5];
    for (int i = 0; i < 4; i++) { lv[i] = vu_level(vu_now[i]); vu_now[i] -= vu_now[i] / 6u; }
    { uint32_t m = vu_now[4] > vu_now[5] ? vu_now[4] : vu_now[5]; vu_out = vu_level(m); vu_now[4] -= vu_now[4] / 6u; vu_now[5] -= vu_now[5] / 6u; }
    if (card_act_n != act_seen) { act_seen = card_act_n; act_hold = 6; }
    if (act_hold) act_hold--;
    lv[4] = !card_ok ? 0 : act_hold ? ((act_hold & 2) ? 15 : 3) : 6;     /* no card: dark; a card: steady; at work: it flickers bright */
    for (int i = 0; i < 5; i++) if (lv[i] != vu_sent[i]) {
        led_set(vu_id[i], lv[i] > 0); led_set(vu_id[i] + 1, 0);
        tx((uint8_t)(0x30 | lv[i])); tx(vu_id[i]); vu_sent[i] = (uint8_t)lv[i];
    }
}
static int lamps_ready;
static void aeria_ui_leds(void)
{
    static uint32_t at;
    if (now() - at < US(16393)) return;
    at = now();
    if (!lamps_ready) { for (int i = 0; i < N_LAMPS; i++) { lamp_lv[i] = 0; lamp_sent[i] = 0xFF; } lamps_ready = 1; }
    vu_lamps();
    for (int i = 0; i < N_LAMPS; i++) {
        int id = lamp_id[i], k = lamp_key[i];
        int pressed = k != 0xFF && ((keyrow[k >> 3] >> (k & 7)) & 1);
        int v = id < 126 ? (preview_leds[id] > preview_leds[id + 1] ? preview_leds[id] : preview_leds[id + 1]) : 0;
        int on = id < 64 && v;
        int target = pressed ? LAMP_PRESSED : on ? LAMP_ON : LAMP_DIM;
        if (seq_mode) {
            /* the sequencer: no fades -- a lamp is what its step is, at once (dark, a beat's mark, a note, the playhead) */
            target = pressed ? LAMP_PRESSED : v >= 3 ? 15 : v == 2 ? 8 : v == 1 ? 2 : i < 16 ? 0 : LAMP_DIM;
            lamp_lv[i] = (uint8_t)target;
        } else
        if (id == 64) { if (vu_out > target) target = vu_out; lamp_lv[i] = (uint8_t)target; }      /* the sky: the lamp above ARR is the main output's level */
        else
        if (lamp_lv[i] < target) lamp_lv[i]++; else if (lamp_lv[i] > target) lamp_lv[i]--;
        if (lamp_lv[i] != lamp_sent[i]) { tx((uint8_t)(0x30 | lamp_lv[i])); tx((uint8_t)id); lamp_sent[i] = lamp_lv[i]; }
        led_set(id, lamp_lv[i] > 0);
        led_set(id + 1, 0);
    }
}
/* the sound: AERIA's lanes (stock's per-track parameters, as its UI writes them) to the engine. Step 1: the main
 * output level (LAST_T lane 15, 0..127, 108 = unity) as the master gain. The rest follows with the effects. */
static uint8_t lane_sent[8][32];
static int lane_init;
static void dsp_write(uint32_t addr, uint32_t val, int yspace);
/* the bring-up rig loops the headphone out into input C (and MAIN L into B): routing C/D would howl, so this
 * development build keeps C/D out of the mix. 0 in the build that gets flashed. */
#ifdef AERIA_FLASH
#define BRINGUP_NO_CD 0
#else
#define BRINGUP_NO_CD 1
#endif
/* num/den as a 24-bit fraction (bit 23 = 1.0, capped just below), by long division: no 64-bit library calls */
static uint32_t frac(uint32_t num, uint32_t den)
{
    if (!den || num >= den) return 0x7FFFFFu;
    uint32_t q = 0, r = num;
    for (int i = 0; i < 23; i++) { r <<= 1; q <<= 1; if (r >= den) { r -= den; q |= 1; } }
    return q;
}
/* AERIA's HEAD and output lanes (sound12.c) -> the engine: the input matrix ($2e0.., in halves), the main feedback
 * ($2d8 k, $2d9 tone, $2da d/4) and the output level ($2e8) */
static void aeria_ui_sound(void)
{
    if (!lane_init) { for (int t = 0; t < 8; t++) for (int k = 0; k < 32; k++) lane_sent[t][k] = 0xFF; lane_init = 1; }
    const uint8_t *h = preview_lane[4];
    static const uint8_t watch[] = {0, 1, 3, 4, 18, 19, 22, 23};
    int changed = 0;
    for (unsigned i = 0; i < sizeof watch; i++) if (h[watch[i]] != lane_sent[4][watch[i]]) { lane_sent[4][watch[i]] = h[watch[i]]; changed = 1; }
    if (changed) {
        /* gains in halves: g = VOL/64 * IN/64, a half = 0x400000 */
        uint32_t in = h[22] ? h[22] : 64;
        uint32_t gab = (uint32_t)h[1] * in, gcd = (uint32_t)h[4] * in;        /* /4096 = the gain */
        uint32_t ab = frac(gab, 4096u * 2u), cd = frac(gcd, 4096u * 2u);       /* in halves */
        uint32_t m[8] = {0};   /* A->L A->R B->L B->R C->L C->R D->L D->R */
        switch (h[0]) {        /* INAB: 0 off, 1 A B stereo, 2 A, 3 B, 4 A+B */
        case 1: m[0] = ab; m[3] = ab; break;
        case 2: m[0] = ab; m[1] = ab; break;
        case 3: m[2] = ab; m[3] = ab; break;
        case 4: m[0] = m[1] = m[2] = m[3] = ab >> 1; break;
        default: break;
        }
        int incd = BRINGUP_NO_CD ? 0 : h[3];
        switch (incd) {
        case 1: m[4] = cd; m[7] = cd; break;
        case 2: m[4] = cd; m[5] = cd; break;
        case 3: m[6] = cd; m[7] = cd; break;
        case 4: m[4] = m[5] = m[6] = m[7] = cd >> 1; break;
        default: break;
        }
        for (int i = 0; i < 8; i++) dsp_write(0x2E0u + (uint32_t)i, m[i], 0);
        /* feedback: FDBK 0..127 -> 0..0.95, WARM d = 1 + 3w/127 (the engine gets d/4 and k = FDBK'/d) */
        uint32_t fd = 0, w = 0;   /* the main feedback and its soft clip are gone (12 Nov 2026): k = 0 and the engine skips it */ (void)h[18];
        uint32_t k = fd ? frac(fd * 95u * 127u, 100u * 127u * (127u + 3u * w)) : 0;
        uint32_t dq = 0x200000u + w * 0x600000u / 127u;
        /* tone: 200 Hz .. open over FTON, one-pole c ~ 2 pi f / fs */
        uint32_t ft = h[19] > 127 ? 127 : h[19], f = (200u << (ft / 16)) + ((200u << (ft / 16)) * (ft % 16)) / 16;
        uint32_t c = f * 1195u; if (c > 0x7FFFFFu) c = 0x7FFFFFu;
        dsp_write(0x2D9, c, 0); dsp_write(0x2DA, dq, 0); dsp_write(0x2D8, k, 0);
    }
    /* OUT (LAST_T lane 15, 108 = unity) with BAL (lane 16, 64 = centre) as main L ($2e8) and R ($2ec); CUE (lane 17,
     * 127 = unity) as the cue pair's level ($2eb) */
    int v = preview_lane[7][15], bal = preview_lane[7][16], cue = preview_lane[7][17];
    if (v != lane_sent[7][15] || bal != lane_sent[7][16] || cue != lane_sent[7][17]) {
        lane_sent[7][15] = (uint8_t)v; lane_sent[7][16] = (uint8_t)bal; lane_sent[7][17] = (uint8_t)cue;
        uint32_t out = v >= 108 ? 0x7FFFFFu : frac((uint32_t)v, 108u);
        uint32_t gl = bal <= 64 ? 0x7FFFFFu : frac((uint32_t)(127 - bal), 63u), gr = bal >= 64 ? 0x7FFFFFu : frac((uint32_t)bal, 64u);
        dsp_write(0x2E8, ((out >> 8) * (gl >> 8)) >> 7, 0);   /* Q23 x Q23, 15 bits each: plenty for a gain */
        dsp_write(0x2EC, ((out >> 8) * (gr >> 8)) >> 7, 0);
        dsp_write(0x2EB, cue >= 127 ? 0x7FFFFFu : frac((uint32_t)cue, 127u), 0);
    }
}

/* ---- the effects: AERIA OS's own copies (fx_table.inc), loaded into the engine and set per chain slot ---- */
#include "fx_table.inc"
extern uint8_t preview_live_fx[16], preview_part_fx[8][12], preview_slot_p2[7][6];
static void dsp_write(uint32_t addr, uint32_t val, int yspace)
{
    if (!dsp_running) return;
    REG8(DSP_SELECT) = 0;
    int ok = hi_put(0x800000u | (yspace ? 0x400000u : 0) | (addr & 0x3FFFFFu)); ok = hi_put(val & 0xFFFFFFu) && ok;
    if (!ok && !LOG->exit_why) { LOG->exit_why = 0xD0000000u | addr; LOG->last_key = now(); }   /* bring-up: the first write the engine did not take */
    { static int deaf; deaf = ok ? 0 : deaf + 1; if (deaf > 40) dsp_running = 0; }   /* an engine that takes nothing: stop asking (each try is 20 ms) so the OS stays awake */
}
/* every effect's code into the shared window (it aliases P), through the engine's host writes */
static void fx_upload(void)
{
    /* the engine sorts the host's words once a frame, from a 1024-word ring: it must be in its frame loop (its start
     * waits for the converter's frame sync) before 20,000 words arrive */
    { uint32_t t0 = now(); while (now() - t0 < MS(150)) {} }
    for (int e = 0; e < FX_COUNT; e++)
        for (unsigned i = 0; i < fx_words[e]; i++) dsp_write(fx_info[e].org + i, fx_code[e][i], 0);
}
/* AERIA's chain slots, as its shelf numbers them: slot 0 = T5 FX2, then T6 FX1/FX2, T7 FX1/FX2, T8 FX1/FX2 */
#define CHAIN_SLOTS 7
static int slot_t(int s) { return s == 0 ? 4 : 5 + (s - 1) / 2; }
static int slot_f(int s) { return s == 0 ? 1 : (s - 1) & 1; }
#define FX_SLOTS 8      /* the sequencer's tracks use all eight of the engine's slots */
static uint8_t slot_row[FX_SLOTS], knob_sent[FX_SLOTS][6];
static uint32_t p2_sent[FX_SLOTS][3];   /* the FUNC-layer knobs as the effect reads them: words r6+$c..$e */
/* RFRZ, Chloaelis's random freeze, for any effect with a FUNC knob of that name (ECHO, the delays): a slow random
 * on/off whose knob is the chance of a hold; while it holds, bit 0 of the FUNC word +$d is set and the effect freezes
 * (holds 0.4 s to ~4.6 s, gaps 0.3 to ~3 s, as PluginProcessor.cpp 149-157) */
static uint32_t slot_knobs[FX_SLOTS];   /* where each slot's knobs live (core 0's block, or core 1's shared one) */
static uint8_t rfrz_bit[FX_SLOTS][2], rfrz_sent[FX_SLOTS]; static uint32_t rfrz_at[FX_SLOTS][2];
static uint32_t rfrz_rnd = 0x9E3779B9u;
static uint32_t rfrz_r(uint32_t n) { rfrz_rnd ^= rfrz_rnd << 13; rfrz_rnd ^= rfrz_rnd >> 17; rfrz_rnd ^= rfrz_rnd << 5; return n ? rfrz_rnd % n : 0; }
static int p2_raw(int s, int k);
static int fx_named(int row, const char *w)   /* the effect in a row is "AERIA " + w */
{
    if (row <= 0 || row > FX_COUNT) return 0;
    const char *n = fx_info[row - 1].name + 6; int i = 0;
    while (w[i] && n[i] == w[i]) i++;
    return !w[i] && !n[i];
}
/* where each effect takes its random freeze: ECHO bit 0 of FUNC word +$d (p2_word), BBD and PT2399 knob word +6
 * (bit 0 left, bit 1 right), TAPE knob words +6 (left) and +7 (right), non-zero = hold */
static void rfrz_tick(int s, int row)
{
    int k = -1;
    if (row > 0 && row <= FX_COUNT) for (int i = 0; i < 6; i++) { const char *n = fx_info[row - 1].knob2[i]; if (n && n[0] == 'R' && n[1] == 'F' && n[2] == 'R' && n[3] == 'Z') k = i; }
    int r = k >= 0 ? p2_raw(s, k) : 0;                                              /* 0..127 */
    uint32_t t = now();
    for (int side = 0; side < 2; side++) {
        if (!r) { rfrz_bit[s][side] = 0; rfrz_at[s][side] = t; continue; }
        if ((int32_t)(t - rfrz_at[s][side]) < 0) continue;
        if (rfrz_bit[s][side]) { rfrz_bit[s][side] = 0; rfrz_at[s][side] = t + MS(300) + MS(1) * rfrz_r(2500u * (uint32_t)(140 - r) / 127u + 1u); }
        else if ((int)rfrz_r(127) < r) { rfrz_bit[s][side] = 1; rfrz_at[s][side] = t + MS(400) + MS(1) * rfrz_r(3500u * (uint32_t)(40 + r) / 127u + 1u); }
        else rfrz_at[s][side] = t + MS(250) + MS(1) * rfrz_r(1200);
    }
    uint8_t b = (uint8_t)(rfrz_bit[s][0] | rfrz_bit[s][1] << 1);
    if (b == rfrz_sent[s] || k < 0) return;
    rfrz_sent[s] = b;
    if (fx_named(row, "BBD") || fx_named(row, "PT2399")) dsp_write(slot_knobs[s] + 6u, b, 0);
    else if (fx_named(row, "TAPE")) { dsp_write(slot_knobs[s] + 6u, b & 1, 0); dsp_write(slot_knobs[s] + 7u, b >> 1, 0); }
}
static int p2_raw(int s, int k) { return seq_mode ? SQH->fx_knob[s & 7][6 + k] & 0x7F : preview_slot_p2[s][k] & 0x7F; }
static uint32_t p2_word(int s, int w)
{
    if (w == 1 && (rfrz_sent[s] & 1) && fx_named(seq_mode ? SQH->fx_row[s & 7] : slot_row[s], "ECHO")) return (seq_mode ? (uint32_t)(SQH->fx_knob[s & 7][8] & 0x7F) << 16 | (uint32_t)(SQH->fx_knob[s & 7][9] & 0x7F) << 8
                                                : (uint32_t)(preview_slot_p2[s][2] & 0x7F) << 16 | (uint32_t)(preview_slot_p2[s][3] & 0x7F) << 8) | 1u;
    if (seq_mode) return (uint32_t)(SQH->fx_knob[s & 7][6 + 2 * w] & 0x7F) << 16 | (uint32_t)(SQH->fx_knob[s & 7][7 + 2 * w] & 0x7F) << 8;
    return (uint32_t)(preview_slot_p2[s][2 * w] & 0x7F) << 16 | (uint32_t)(preview_slot_p2[s][2 * w + 1] & 0x7F) << 8;
}
static uint32_t route_sent[FX_SLOTS][5];
static uint32_t grain_win[65] = {0};   /* set to a triangle at start (grain_win_init) */
static void grain_win_init(void) { for (int i = 0; i <= 64; i++) { int t = i <= 32 ? i : 64 - i; grain_win[i] = t >= 32 ? 0x7FFFFFu : (uint32_t)t * 0x40000u; } }
static int slots_ready;
/* the engine's slot table (engine.asm): X:$280 + 8s: enabled, proc, init, r6, r7, X:$213 value, init pending */
/* core 1 (dsp/core1.asm): its slot entries, knob blocks and buffers in the shared window, one per chain slot */
#define C1_ENTRY(s) (0x33A10u + 16u * (uint32_t)(s))
#define C1_KNOBS(s) (0x33C00u + 32u * (uint32_t)(s))
#define C1_IN(s) (0x33E00u + 64u * (uint32_t)(s))
#define C1_OUT(s) (0x33E20u + 64u * (uint32_t)(s))
#define PROXY_INIT 0x33002u
#define PROXY_PROC 0x33003u
int aos_place(int s, int row); int aos_slot_core(int s);   /* the UI's shelf.c: which core each slot's effect runs on, by the room each has */
static int8_t slot_on_core[FX_SLOTS];
/* a slot's first knob values and its core: the chain's from the UI's lanes, a sequencer track's from the sequence */
static uint32_t cfg_knob(int s, int k) { return seq_mode ? (uint32_t)(SQH->fx_knob[s & 7][k] & 0x7F) : (uint32_t)(preview_part_fx[slot_t(s)][slot_f(s) * 6 + k] & 0x7F); }
static int cfg_core(int s) { return seq_mode ? (SQH->fx_row[s & 7] && SQH->fx_core[s & 7] == 1) : aos_slot_core(s); }
/* the Lorenz attractor (the UI's lorenz.c, -1000..1000 for x and y) to every FILT in a slot: words +6 (left) and +7
 * (right) of its knob block, Q23; FILT applies its own depth (LRNZ) and glides the steps (12 Nov 2026) */
static int32_t lz_now[2], lz_sent[FX_SLOTS][2];
static int filt_row = -1;
void aos_lorenz(int l, int r) { lz_now[0] = l; lz_now[1] = r; }
static void filt_lorenz_tick(void)
{
    { static uint32_t at; if (now() - at < US(16393)) return; at = now(); }   /* 61 a second, as the UI's tick */
    for (int s = 0; s < FX_SLOTS; s++) rfrz_tick(s, slot_row[s] <= FX_COUNT ? slot_row[s] : 0);
    if (filt_row < 0) { filt_row = 0; for (int i = 0; i < FX_COUNT; i++) { const char *n = fx_info[i].name; if (n[6] == 'F' && n[7] == 'I' && n[8] == 'L' && n[9] == 'T') filt_row = i + 1; } }
    if (!filt_row) return;
    for (int s = 0; s < FX_SLOTS; s++) if (slot_row[s] == filt_row) for (int k = 0; k < 2; k++) if (lz_now[k] != lz_sent[s][k]) {
        lz_sent[s][k] = lz_now[k];
        int32_t v = lz_now[k] > 1000 ? 1000 : lz_now[k] < -1000 ? -1000 : lz_now[k];
        dsp_write(slot_knobs[s] + 6u + (uint32_t)k, (uint32_t)(v * 8388) & 0xFFFFFFu, 0);
    }
}
static void fx_configure(int s, int row)
{
    uint32_t base = 0x280u + 8u * (uint32_t)s, r6 = 0x1800u + 0x20u * (uint32_t)s, r7 = 0x1000u + 0x100u * (uint32_t)s, aw = 0x1900u + (uint32_t)s;
    dsp_write(base, 0, 0);
    dsp_write(C1_ENTRY(s), 0, 0);
    slot_knobs[s] = r6;
    lz_sent[s][0] = lz_sent[s][1] = 0x7FFFFFFF; rfrz_sent[s] = 0;
    dsp_write(r6 + 6u, 0, 0); dsp_write(r6 + 7u, 0, 0); dsp_write(C1_KNOBS(s) + 6u, 0, 0); dsp_write(C1_KNOBS(s) + 7u, 0, 0);   /* FILT's Lorenz words: RAM is not cleared */
    slot_on_core[s] = (int8_t)cfg_core(s);
    if (slot_on_core[s] == 1) {
        /* the effect runs on core 1: core 0's slot is the proxy, both read the shared knob block */
        const FxInfo *fi = &fx_info[row - 1];
        uint32_t e = C1_ENTRY(s), kb = C1_KNOBS(s);
        slot_knobs[s] = kb;
        for (int k = 0; k < 6; k++) { knob_sent[s][k] = 0xFF; dsp_write(kb + (uint32_t)k, cfg_knob(s, k) << 16, 0); }
        for (int w = 0; w < 3; w++) { p2_sent[s][w] = p2_word(s, w); dsp_write(kb + 0xCu + (uint32_t)w, p2_sent[s][w], 0); }
        dsp_write(kb + 0x10u, e, 0); dsp_write(kb + 0x11u, C1_IN(s), 0); dsp_write(kb + 0x12u, C1_OUT(s), 0);
        for (int i = 0; i < 32; i++) dsp_write(C1_OUT(s) + (uint32_t)i, 0, 0);
        dsp_write(e + 1, fi->org + 2, 0); dsp_write(e + 2, fi->org, 0); dsp_write(e + 3, kb, 0);
        dsp_write(e + 4, 0x1000u + 0x100u * (uint32_t)s, 0); dsp_write(e + 5, fi->ybase, 0); dsp_write(e + 6, 1, 0);
        dsp_write(e + 7, 0, 0); dsp_write(e + 8, 0, 0); dsp_write(e + 9, C1_IN(s), 0); dsp_write(e + 10, C1_OUT(s), 0);
        dsp_write(e + 11, 0x1900u + (uint32_t)s, 0);
        dsp_write(e, 1, 0);
        dsp_write(base + 1, PROXY_PROC, 0); dsp_write(base + 2, PROXY_INIT, 0); dsp_write(base + 3, kb, 0);
        dsp_write(base + 4, r7, 0); dsp_write(base + 5, aw, 0); dsp_write(base + 6, 0, 0);
        dsp_write(base + 7, 0x220u + 9u * (uint32_t)s, 0);
        for (int k = 0; k < 5; k++) route_sent[s][k] = 0xFFFFFFFFu;
        dsp_write(base, 1, 0);
        return;
    }
    for (int k = 0; k < 6; k++) knob_sent[s][k] = 0xFF;
    if (row <= 0 || row > FX_COUNT) return;
    const FxInfo *fi = &fx_info[row - 1];
    for (int k = 0; k < 6; k++) dsp_write(r6 + (uint32_t)k, cfg_knob(s, k) << 16, 0);
    for (int w = 0; w < 3; w++) { p2_sent[s][w] = p2_word(s, w); dsp_write(r6 + 0xCu + (uint32_t)w, p2_sent[s][w], 0); }
    dsp_write(aw, fi->ybase, 0);
    dsp_write(base + 1, fi->org + 2, 0);
    dsp_write(base + 2, fi->org, 0);
    dsp_write(base + 3, r6, 0);
    dsp_write(base + 4, r7, 0);
    dsp_write(base + 5, aw, 0);
    dsp_write(base + 6, 1, 0);
    dsp_write(base + 7, 0x220u + 9u * (uint32_t)s, 0);   /* its routing block (engine.asm) */
    for (int k = 0; k < 5; k++) route_sent[s][k] = 0xFFFFFFFFu;
    if (fi->org == 0x35000u) for (int i = 0; i <= 64; i++) dsp_write(r7 + 0x80u + (uint32_t)i, grain_win[i], 0);   /* GRAINS: its window */
    dsp_write(base, 1, 0);
}
/* the routing a slot's panner wants (out.c): out of the chain or not, and four gains L->main R->main L->cue R->cue
 * (Q23); the engine glides to them. Only changes go to the DSP. */
static void usb_iso_side(void);
static void stream_side(void) { if (dsp_running) { dsp_poll(); stream_pump(); } usb_iso_side(); }   /* ... and the computer's packets: a redraw under full load once left their queue empty */
static void route_write(int s, int on, const uint32_t g[4]);
/* the UI's panner. In the sequencer a track's place is the sequencer's own (its PAN) unless the panner is on for it */
void aos_route(int s, int on, const uint32_t g[4])
{
    if (s < 0 || s >= (seq_mode ? FX_SLOTS : CHAIN_SLOTS)) return;
    if (seq_mode) { sq.ui_route[s] = (uint8_t)(on != 0); if (!on) return; }
    route_write(s, on, g);
}
/* bass mono (the engine's, under 50 Hz on the main pair) stands back while anything is placed among the four outputs */
static uint8_t quad_slot[FX_SLOTS];
static void bass_mono_tick(void)
{
    static int sent = -1; int any = 0;
    for (int s = 0; s < FX_SLOTS; s++) any |= quad_slot[s];
    if (any != sent) { sent = any; dsp_write(0x8a41u, (uint32_t)any, 0); }
}
static void route_write(int s, int on, const uint32_t g[4])
{
    if (s < 0 || s >= FX_SLOTS) return;
    quad_slot[s] = (uint8_t)(on && (g[2] | g[3]));
    uint32_t blk = 0x220u + 9u * (uint32_t)s, v[5] = {(uint32_t)(on != 0), g[0], g[1], g[2], g[3]};
    for (int k = 0; k < 5; k++) if (v[k] != route_sent[s][k]) { route_sent[s][k] = v[k]; dsp_write(blk + (uint32_t)k, v[k], 0); }
}
/* whether the cue pair carries a copy of the chain (while nothing is routed to it) */
void aos_cue_chain(int on)
{
    static int sent = -1;
    if (on == sent) return;
    sent = on; dsp_write(0x2EAu, on ? 0x7FFFFFu : 0, 0);
}
/* the weather's filters (cloudfin/parts/wx.c): cutoff indices into wx_table.inc, -1 off; only changes go out */
#include "wx_table.inc"
void aos_weather_filters(int lp, int hp)
{
    static int lp_sent = -2, hp_sent = -2;
    if (lp != lp_sent) {
        if (lp >= 0 && lp < 128) { for (int k = 0; k < 5; k++) dsp_write(0x200u + (uint32_t)k, wx_lp[lp][k], 0); if (lp_sent < 0) for (int k = 0; k < 8; k++) dsp_write(0x9E0u + (uint32_t)k, 0, 0); }
        dsp_write(0x20Au, lp >= 0, 0);
        lp_sent = lp;
    }
    if (hp != hp_sent) {
        if (hp >= 0 && hp < 128) { for (int k = 0; k < 5; k++) dsp_write(0x205u + (uint32_t)k, wx_hp[hp][k], 0); if (hp_sent < 0) for (int k = 0; k < 8; k++) dsp_write(0x9E8u + (uint32_t)k, 0, 0); }
        dsp_write(0x20Bu, hp >= 0, 0);
        hp_sent = hp;
    }
}
/* the grain window (the cloud editor's, 65 points, Q23): into every GRAINS slot */
void aos_grain_window(const uint32_t w[65])
{
    int same = 1;
    for (int i = 0; i <= 64; i++) if (grain_win[i] != w[i]) { same = 0; grain_win[i] = w[i]; }
    if (same) return;
    for (int s = 0; s < CHAIN_SLOTS; s++) if (slot_row[s] > 0 && slot_row[s] <= FX_COUNT && fx_info[slot_row[s] - 1].org == 0x35000u)
        for (int i = 0; i <= 64; i++) dsp_write(0x1000u + 0x100u * (uint32_t)s + 0x80u + (uint32_t)i, grain_win[i], 0);
}
int aos_scale_snap(int dc); int aos_reso_snap(int midi, int *off); int aos_scale_root(void);
/* the master EQ (engine.asm: X:$8800 + 6 x band: a biquad and the band's amount): band 0 a low shelf (a lowpass
 * added to the signal), 1 and 2 bandpasses, 3 a high shelf (a high-pass added); freq an index 0..127 (20 Hz .. 20 kHz,
 * log), db -12..12. Only changes go out; 0 dB switches the band off (the engine skips it). */
#include "eq_table.inc"
void aos_master_eq(int band, int freq, int db)
{
    static int16_t fs[4] = {-1, -1, -1, -1}, ds[4] = {99, 99, 99, 99};
    if (band < 0 || band > 3) return;
    freq = freq < 0 ? 0 : freq > 127 ? 127 : freq; db = db < -12 ? -12 : db > 12 ? 12 : db;
    uint32_t base = 0x8800u + 6u * (uint32_t)band;
    if (freq != fs[band]) {
        const uint32_t *c = band == 0 ? eq_lp1[freq] : band == 3 ? eq_hp1[freq] : eq_bp[freq];
        dsp_write(base + 5, 0, 0);                                   /* flat while its filter changes */
        for (int k = 0; k < 5; k++) dsp_write(base + (uint32_t)k, c[k], 0);
        fs[band] = (int16_t)freq; ds[band] = 99;
    }
    if (db != ds[band]) { dsp_write(base + 5, eq_gain[db + 12], 0); ds[band] = (int16_t)db; }
}
/* the main out's limiter (engine.asm: X:$320): MIX, FUNC layer, LIMT */
void aos_limiter(int on) { static int was = -1; if (on != was) { was = on; dsp_write(0x320, on ? 1 : 0, 0); } }
extern uint8_t preview_scale_key;   /* changes when the scale or its root does */
/* the sequencer's effects: one a track (the sequence's fx_row / fx_knob / fx_core), in the engine's eight slots */
static void seq_fx(void)
{
    const SqHead *h = SQH;
    for (int s = 0; s < FX_SLOTS; s++) {
        int row = h->fx_row[s]; if (row > FX_COUNT) row = 0;
        if (row && fx_named(row, "DIST")) row = 0;   /* no distortion */
        if (row != slot_row[s] || (row && (h->fx_core[s] == 1) != (slot_on_core[s] == 1))) { slot_row[s] = (uint8_t)row; fx_configure(s, row); }
        if (!row) continue;
        for (int k = 0; k < 6; k++) {
            int v = (h->fx_knob[s][k] & 0x7F) + aos_mod_fxk[s][k]; v = v < 0 ? 0 : v > 127 ? 127 : v;
            if (v == knob_sent[s][k]) continue;
            knob_sent[s][k] = (uint8_t)v;
            dsp_write(slot_knobs[s] + (uint32_t)k, (uint32_t)v << 16, 0);
        }
        for (int w = 0; w < 3; w++) if (p2_word(s, w) != p2_sent[s][w]) { p2_sent[s][w] = p2_word(s, w); dsp_write(slot_knobs[s] + 0xCu + (uint32_t)w, p2_sent[s][w], 0); }
    }
}
static void stems_mask(void) { uint8_t m = 0; for (int s = 0; s < FX_SLOTS; s++) if (slot_row[s] >= 1 && slot_row[s] <= FX_COUNT) m |= (uint8_t)(1u << s); stem_stereo = m; }
/* the chain or the sequencer's tracks; when the OS changes face every slot is emptied and set up again */
static void fx_slots(void);
static void fx_either(void)
{
    static int was = -1;
    if (was != seq_mode) {
        if (was >= 0 || seq_mode) for (int s = 0; s < FX_SLOTS; s++) { dsp_write(0x280u + 8u * (uint32_t)s, 0, 0); dsp_write(C1_ENTRY(s), 0, 0); slot_row[s] = 0xFF; slot_knobs[s] = 0x1800u + 0x20u * (uint32_t)s; slots_ready = 1; }
        was = seq_mode;
    }
    if (seq_mode) seq_fx(); else fx_slots();
    stems_mask();
}
static void fx_slots(void)
{
    static int scale_seen = -1; int scale_key = preview_scale_key;
    if (!slots_ready) { for (int s = 0; s < CHAIN_SLOTS; s++) { slot_row[s] = 0xFF; slot_knobs[s] = 0x1800u + 0x20u * (uint32_t)s; } slots_ready = 1; }
    for (int s = 0; s < CHAIN_SLOTS; s++) {
        int t = slot_t(s), f = slot_f(s), row = preview_live_fx[f * 8 + t];
        if (row != slot_row[s]) { slot_row[s] = (uint8_t)row; aos_place(s, row); fx_configure(s, row); }
        else if (row > 0 && row <= FX_COUNT && aos_slot_core(s) != slot_on_core[s]) fx_configure(s, row);   /* moved across to make room */
        if (row <= 0 || row > FX_COUNT) continue;
        uint32_t org = fx_info[row - 1].org;
        if (scale_seen != scale_key && (org == 0x34000u || org == 0x35000u || org == 0x36000u)) for (int k = 0; k < 6; k++) knob_sent[s][k] = 0xFF;
        for (int k = 0; k < 6; k++) {
            int v = preview_lane[t][(f ? 24 : 18) + k];
            if (v == knob_sent[s][k] || v > 127) continue;
            knob_sent[s][k] = (uint8_t)v;
            uint32_t w = (uint32_t)v << 16;
            /* the scale (the UI's scales.c): PITCH's SHFT and RESO's PTCH land on a degree, the part of it that is not
             * a whole semitone goes into their FINE (PITCH's: 64 steps a semitone; RESO's: 64 steps half of one);
             * GRAINS' PTCH takes the nearest semitone */
            if (org == 0x34000u && k < 2) {
                int q = aos_scale_snap((preview_lane[t][(f ? 24 : 18)] - 24) * 1000), st = (q >= 0 ? q + 500 : q - 500) / 1000;
                st = st < -24 ? -24 : st > 24 ? 24 : st;
                if (k == 0) { w = (uint32_t)(st + 24) << 16; knob_sent[s][1] = 0xFF; }
                else { int32_t x = (int32_t)w + (q - st * 1000) * 4194; w = (uint32_t)(x < 0 ? 0 : x > 0x7FFFFF ? 0x7FFFFF : x); }
            } else if (org == 0x35000u && k == 4) {
                int q = aos_scale_snap((v - 24) * 1000), st = (q >= 0 ? q + 500 : q - 500) / 1000;
                w = (uint32_t)((st < -24 ? -24 : st > 24 ? 24 : st) + 24) << 16;
            } else if (org == 0x36000u && k < 2) {
                int off, m = aos_reso_snap(30 + preview_lane[t][(f ? 24 : 18)], &off) - 30;
                if (m < 0) { m = 0; off = 0; } else if (m > 72) { m = 72; off = 0; }
                if (k == 0) { w = (uint32_t)m << 16; knob_sent[s][1] = 0xFF; }
                else { int32_t x = (int32_t)w + off * 8389; w = (uint32_t)(x < 0 ? 0 : x > 0x7FFFFF ? 0x7FFFFF : x); }
            }
            dsp_write(slot_knobs[s] + (uint32_t)k, w, 0);
        }
        for (int w = 0; w < 3; w++) if (p2_word(s, w) != p2_sent[s][w]) { p2_sent[s][w] = p2_word(s, w); dsp_write(slot_knobs[s] + 0xCu + (uint32_t)w, p2_sent[s][w], 0); }
    }
    scale_seen = scale_key;
}
/* the rig AERIA has played with until now: PITCH, DIST, GRAINS, RING, SMEAR (rows = fx_table order + 1) */
static void fx_default_chain(void)
{
    static const uint8_t rig[CHAIN_SLOTS] = {1, 2, 3, 4, 6, 0, 0};
    for (int s = 0; s < CHAIN_SLOTS; s++) {
        int t = slot_t(s), f = slot_f(s), row = rig[s];
        preview_live_fx[f * 8 + t] = (uint8_t)row;
        for (int k = 0; k < 6; k++) preview_part_fx[t][f * 6 + k] = row ? fx_info[row - 1].def[k] : 64;
        for (int k = 0; k < 6; k++) preview_slot_p2[s][k] = row ? fx_info[row - 1].def2[k] : 0;
    }
}

void aeria_main(void)
{
    /* (the caches are on since start.S: before .bss was cleared) */
    for (unsigned i = 0; i < sizeof(Log) / 4; i++) ((volatile uint32_t *)LOG)[i] = 0;
    LOG->magic = 0x414F5331u;   /* 'AOS1' */
    for (int i = 0; i < 256; i++) LOG->gpio_boot[i] = REG8(0xFC0A4000u + i);   /* the pins as the bootstrap leaves them */
    LOG->uart_regs[0] = REG8(UART1 + 4);   /* USR as the bootstrap left it (other reads have side effects) */
    timer_init();
    /* a warm launch from inside stock (cloudfin/parts/stocktest.c) leaves 'WARM' at 0x100FB7F0: the panel is in
     * stock's mode then, so skip its start-up and go straight to the audio */
    warm = REG32(0x100FB7F0u) == 0x5741524Du;
    REG32(0x100FB7F0u) = 0;
    LOG->uart_regs[4] = (uint32_t)warm;
    if (!warm) panel_lines_and_uart();

    if (!warm) {
    /* 1: listen to whatever the panel already says */
    log_mark(1);
    { uint32_t t = now(); while (now() - t < MS(100)) if (rx_ready()) log_byte(REG8(UART1 + 0xC)); }
    /* 2: the MKII loader check stock does (version query only: AERIA never erases or writes the panel) */
    log_mark(2);
    tx(0x60); tx(0x02); tx(0x70); tx(0x00);
    for (int i = 0; i < 5; i++) if (rx_wait(MS(100)) < 0) break;
    tx(0x60); tx(0x00);
    /* give the panel time to leave its loader and start its application (0.1-0.5 asked after 50 ms and got
     * the loader's own reply) */
    { uint32_t t = now(); while (now() - t < MS(500)) if (rx_ready()) log_byte(REG8(UART1 + 0xC)); }
    /* 3: ask who the panel is */
    log_mark(3);
    tx(0x74); tx(0x00);
    for (int i = 0; i < 10; i++) { int b = rx_wait(MS(100)); if (b < 0) break; LOG->ident[i] = (uint8_t)b; LOG->ident_bytes = (uint32_t)(i + 1); }
    /* and 60 00 once more after the identity, as stock's MKII start-up does (0x4001fa08): without it the
     * panel kept showing its loader's text and ignored every LCD block (0.1-0.8) */
    tx(0x60); tx(0x00);
    { uint32_t t = now(); while (now() - t < MS(50)) if (rx_ready()) log_byte(REG8(UART1 + 0xC)); }
    /* 4: every LED to full level, all off, the backlight up */
    log_mark(4);
    for (int id = 0; id < 0x58; id++) { tx(0x3F); tx((uint8_t)id); }   /* stock's 88 ids only */
    led_flush(1);
    /* every LED row stock's start-up sends (1.40C 0x400136a8: rows 0-16, row 16 as 0xA0), not just the 11 that
     * hold front-panel lamps: the rows above 10 are the panel board's other outputs */
    for (int r = 11; r < 16; r++) { tx((uint8_t)(0x20 | r)); tx(0); }
    tx(0xA0); tx(0);
    for (int id = 0x58; id < 0x85; id++) { tx(0x30); tx((uint8_t)id); }   /* level 0, as stock's sweep (0x40055bc8) */
    /* bring-up test: a B5 palette entry (index 2*id + register, then r, g, b) for every panel id on registers
     * 0-2, as stock's led_scheme (0x4003f430 -> 0x400133cc -> 0x40013368) sends at boot */
    for (int id = 0; id < 0x88; id++) for (int reg = 0; reg < 3; reg++) {
        unsigned ix = (unsigned)(2 * id + reg);
        tx(0xB5); tx((uint8_t)(ix >> 8)); tx((uint8_t)ix); tx(0x40); tx(0x40); tx(0x40);
    }
    tx(0xB7); tx(0xFF);
    log_mark(5);
    }
    /* the converters' line: stock drives UART port bit 2 (U0RTS as a GPIO) low as an output; the bootstrap
     * leaves it an input that floats high (gpio_stock.txt vs gpio_run.txt: PDDR_UART 04 vs 00, PODR_UART 00 vs ff) */
    cf_snapshot();
    /* stock's card start-up order: the pins first (1.40C 0x400160f8 -> 0x40014960), then the ATA controller */
    /* the pin functions and the DSP bus timing stock sets after the bootstrap (gpio_stock.txt; FlexBus CSCR2:
     * bootstrap 0x1180, stock 0x180). The in-stock test (cloudfin/parts/stocktest.c) heard AERIA's tone with
     * these in place; RAM-booted AERIA OS without them did not. */
    REG8(0xFC0A4060u) = 0x11;                /* PAR_FEC */
    REG16(0xFC0A406Au) = 0x0A00;             /* PAR_FECI2C */
    REG16(0xFC0A406Eu) = 0x07E7;             /* PAR_ATA */
    REG16(0xFC0A4072u) = 0x8080;             /* PAR_PCI */
    ata_cr_boot = REG8(0x90000024u);
    REG32(0xFC008020u) = 0x00001180u;        /* CSCR2: the bootstrap's timing for the uploads -- a cold DSP's boot ROM
                                              * cannot keep up with no wait states (it read a count of 20 as 6) */
    REG8(0xFC0A4009u) = REG8(0xFC0A4009u) & (uint8_t)~0x04;   /* PODR_UART bit 2 = 0 */
    REG8(0xFC0A4021u) = REG8(0xFC0A4021u) | 0x04;             /* PDDR_UART bit 2 = output */
    /* 6: audio -- our own DSP program on core 0: every input to every output */
    /* upload first: a power-on boot ROM (the flashed AERIA OS's cold start) and a parked core (AERIA OS's own
     * hand-back, Modwerk's park) both take it. Only a core that does not listen is re-parked, then loaded again --
     * re-parking first sent a power-on boot ROM into empty memory (10 Nov 2026, the first flashed start). */
    /* bring-up: battery RAM 0x100FBFE8 = 'DSPD' leaves the DSP untouched, for the link to load by hand (usb.c 0x10) */
    if (*(volatile uint32_t *)0x100FBFE8u == 0x44535044u) { *(volatile uint32_t *)0x100FBFE8u = 0; LOG->dsp_timeouts = 99; goto dsp_skipped; }
    dsp_boot_thru();
    if (LOG->dsp_timeouts) {
        LOG->uart_regs[5] = 0x100u | (uint32_t)dsp_repark(0);
        LOG->dsp_timeouts = 0;
        dsp_boot_thru();
    }
    dsp_running = LOG->dsp_timeouts == 0;
    if (dsp_running) {
        dsp1_running = dsp_boot_core1();
        if (!dsp1_running) { LOG->uart_regs[5] |= 0x200u | (uint32_t)dsp_repark(1) << 1; dsp1_running = dsp_boot_core1(); }
    }
dsp_skipped:
    REG32(0xFC008020u) = 0x00000180u;        /* then no wait states, as stock after its uploads (the stream needs it) */
    LOG->uart_regs[3] = (uint32_t)dsp1_running;
    /* the codec's DAC outputs: unmuted once the DSP is clocking them, as stock does after its boot logo */
    LOG->uart_regs[7] = (uint32_t)codec_write(CODEC_DAC_MUTE, 0x00);
    log_mark(6);
    for (int i = 0; i < 0x100; i++) CARDLOG[0x200 + i] = 0;
    { volatile uint32_t *u = (volatile uint32_t *)(CARDLOG + 0x280); u[15] = UR(OTGSC); u[16] = UR(PORTSC); u[17] = UR(USBCMD); u[18] = UR(USBSTS); }   /* at start */
#ifdef AERIA_FLASH
    card_init();
    fat_mount();
#else
    card_log();
#endif
    { uint32_t t = now(); while (now() - t < MS(200)) {} }
    for (int i = 0; i < 256; i++) LOG->gpio_run[i] = REG8(0xFC0A4000u + i);    /* and with our audio running */

    /* ---- AERIA: its UI (aeria_ui.c) on this OS ---- */
    aeria_ui_start();
    if (dsp_running) { dsp_write(0x2f2, 0x400000, 0); stream_on = 1; intro_start(); }
    uint32_t prev = now(), ms = 0, tick_at = prev, draw_at = prev;
    for (;;) {
        poll_panel();
        { uint32_t d = now() - prev; uint32_t whole = d / 132000u; ms += whole; prev += whole * 132000u; LOG->ms_run = ms; }
        if (dsp_running) { dsp_poll(); stream_pump(); }
        usb_poll();
        CRUMB(0x08); midi_poll();
        { static uint32_t fast_at; uint32_t tn = now(); if (tn - fast_at >= US(250)) { uint32_t us = (tn - fast_at) / 132u; fast_at = tn; if (us < 100000u) { CRUMB(0x09); module_fast(us); } } }
        if ((LOG->loops & 1023) == 0) {      /* bring-up: the sampler's report, card log +0x400 */
            volatile uint32_t *r = (volatile uint32_t *)(CARDLOG + 0x200);
            r[0] = 0x534D504Cu; r[1] = intro_smp.status | (uint32_t)card_ok << 8; r[2] = intro_smp.frames; r[3] = intro_smp.step;
            r[4] = stream_written; r[5] = stream_taken; r[6] = meter_max[4]; r[7] = meter_max[5];
            r[8] = meter_max[0]; r[9] = meter_max[1]; r[10] = voice[0].on | (uint32_t)voice[0].pos << 8;
            for (int i = 0; i < 64; i++) CARDLOG[0x240 + i] = (uint8_t)intro_path[i];
            volatile uint32_t *u = (volatile uint32_t *)(CARDLOG + 0x280);   /* bring-up: USB */
            u[0] = 0x55534221u; u[1] = UR(OTGSC); u[2] = UR(PORTSC); u[3] = UR(USBSTS); u[4] = UR(USBCMD); u[5] = UR(USBMODE);
            u[6] = (uint32_t)usb_up | (uint32_t)usb_hs << 8 | (uint32_t)usb_config << 16; u[7] = usb_resets; u[8] = usb_setups;
            u[9] = UR(EPLIST); u[10] = UR(DEVADDR); u[11] = UR(EPCTRL(0));
            if (!u[12]) { u[12] = 0x80000000u | UR(OTGSC); u[13] = UR(PORTSC); u[14] = UR(USBCMD); }   /* as first seen */
        }
        LOG->loops = LOG->loops + 1;
        /* bring-up hand-back to the flashed OS: FUNC+NO+PROJ, or after DEV_MINUTES (RAM-boot tests only) */
        if (!AERIA_FLASHED && held_func && held_no && ((keyrow[0x1c >> 3] >> (0x1c & 7)) & 1)) {
            /* wait until FUNC is let go: the bootstrap opens its Startup Menu when it sees FUNC held at the reset */
            uint32_t t = now();
            while (held_func && now() - t < MS(10000)) poll_panel();
            { uint32_t t2 = now(); while (now() - t2 < MS(200)) poll_panel(); }
            hand_back(1);
        }
        if (DEV_MINUTES && ms > DEV_MINUTES * 60000u) hand_back(2);
#ifndef AERIA_FLASH
        { volatile uint32_t *dg = (volatile uint32_t *)0x100FBEF4u; dg[0] = ms; dg[1] = LOG->loops; dg[2] = LOG->dsp_timeouts; }   /* test builds: last seen */
#endif
        CRUMB(0x02); aeria_ui_events();
        if (now() - tick_at >= US(16393)) { tick_at += US(16393); aeria_ui_tick(); }    /* 61 a second, as stock's UI tick */
        if (now() - draw_at >= MS(stems_sent ? 100 : 33)) {                              /* 30 frames a second (10 while the stems stream: every bit of time goes to them) */
            draw_at = now();
            CRUMB(0x05); aeria_ui_draw();
            if (ms < 30000 && (ms / 1000) % 2) px(W - 1, 0);   /* bring-up: a blink so a frozen OS is obvious */
        }
        CRUMB(0x03); lcd_flush(6);
        aeria_ui_leds();
        led_flush(0);
        CRUMB(0x04); aeria_ui_sound();
        fx_either(); bass_mono_tick(); filt_lorenz_tick();
    }
}
