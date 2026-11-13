/* AERIA OS: USB. Our own code. The MCF54455's USB OTG module as a device (a Chipidea/ARC controller, big-endian
 * queue heads, an external ULPI high-speed PHY the bootstrap selects), polled.
 *
 * One composite device (IAD), at high speed:
 *   interfaces 0-1  USB Audio 2.0, six channels IN at 44.1 kHz, 24 bits in 4-byte slots: in A, in B, in C, in D,
 *                   main L, main R (after the master level). EP 0x82 isochronous, asynchronous, every 250 us.
 *                   The DSP engine's recorder (bit 3, the tap) sends the samples; class compliant, no driver.
 *   interface  2    mass storage (bulk-only SCSI) on EP 0x81 / 0x01: the CF card, while USB DISK MODE is on
 *                   (AERIA keeps off the card meanwhile); "no medium" otherwise.
 *   interface  3    AERIA's own link (vendor class, no endpoints): requests on EP0 -- memory, keys, the screen,
 *                   booting a new build from RAM. aeria-os/tools/aos.py speaks it.
 * At full speed: mass storage and the link only (six channels do not fit a full-speed frame).
 *
 * Controller facts (the emulator's usb.h and what 1.40C's bring-up writes, read for understanding):
 *   USBMODE 0x0e (device, big-endian, setup lockout off), EPLISTADDR the queue heads (2 KB aligned), USBCMD.RS on;
 *   dQH +0 caps (max packet << 16, IOS 1 << 15, ZLT off 1 << 29, mult << 30), +8 next dTD, +0xc token,
 *   +0x28 the SETUP packet; dTD +0 next (1: none), +4 token (bytes << 16, IOC 1 << 15, active 0x80), +8.. pages. */
#ifndef USB_MIDI
#define NO_USB_MIDI   /* USB-MIDI's descriptors stopped macOS configuring AERIA (10 Nov 2026): off until that is solved */
#endif
#if defined(NO_UAC2) && !defined(NO_USB_MIDI)
#define MIDI_IF 0     /* test: MIDI without the audio function: 0-1 MIDI, 2 disk, 3 link */
#define MSC_IF 2
#elif !defined(NO_USB_MIDI)
#define MIDI_IF 2
#define MSC_IF 4      /* interfaces: 0-1 audio, 2-3 MIDI, 4 disk, 5 link */
#else
#define MSC_IF 2      /* 0-1 audio, 2 disk, 3 link */
#endif
#define LINK_IF (MSC_IF + 1)
#define USB_BASE 0xFC0B0000u
#ifdef AERIA_FLASH
#define CRUMB(n) (*(volatile uint32_t *)0x100FBFF0u = (n))   /* bring-up: where it last was */
#else
#define CRUMB(n) (*(volatile uint32_t *)0x100FBFF0u = *(volatile uint32_t *)0x100FBEF0u = (n))   /* + a copy only test builds write */
#endif
#define UR(o) (*(volatile uint32_t *)(USB_BASE + (o)))
#define USBCMD 0x140
#define USBSTS 0x144
#define USBINTR 0x148
#define FRINDEX 0x14C
#define DEVADDR 0x154
#define EPLIST 0x158
#define PORTSC 0x184
#define OTGSC 0x1A4
#define USBMODE 0x1A8
#define SETUPSTAT 0x1AC
#define PRIME 0x1B0
#define FLUSH 0x1B4
#define EPSTAT 0x1B8
#define COMPLETE 0x1BC
#define EPCTRL(n) (0x1C0 + 4 * (n))

/* SDRAM for USB: queue heads, transfer descriptors, buffers -- through the uncached alias (SDRAM + 0x08000000:
 * the data cache covers only 0x40000000-0x47FFFFFF), so the controller's DMA and the CPU always agree */
#define USB_MEM 0x4F400000u
#define DQH(i) ((volatile uint32_t *)(USB_MEM + 64u * (i)))          /* i = 2 ep + (in) */
#define DTD(i) ((volatile uint32_t *)(USB_MEM + 0x800u + 64u * (i)))  /* 0 ep0 out, 1 ep0 in, 2 msc out, 3 msc in, 8.. audio */
#define EP0BUF ((uint8_t *)(USB_MEM + 0x4000u))                        /* 8 KB (dTDs 0..95 below it) */
#define MSCBUF ((uint8_t *)(USB_MEM + 0x10000u))                       /* 64 KB */
#define AUDBUF ((uint8_t *)(USB_MEM + 0x20000u))                       /* N_ISO packets */
#define N_ISO 64
typedef char dtds_fit[(0x800 + 64 * (8 + N_ISO) <= 0x4000) ? 1 : -1];
#define ISO_MAX 864                                                    /* 12 frames x 18 ch x 4 bytes (the stems' setting; the six channels use 288 of it) */

static int usb_up, usb_hs, usb_config, usb_alt_audio, msc_armed, um_armed, um_busy;
static uint32_t usb_resets, usb_setups;
static uint32_t iso_head, iso_queued, iso_sent, iso_short;

/* ---- the descriptors ---- */
#define VID 0x1209u   /* pid.codes */
#define PID 0x0001u   /* its test PID: for a private device */
static const uint8_t dev_desc[18] = {18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, VID & 0xFF, VID >> 8, PID & 0xFF, PID >> 8, 0x00, 0x01, 1, 2, 3, 1};
static const uint8_t qual_desc[10] = {10, 6, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 1, 0};
#define AC_LEN (9 + 8 + 17 + 12)
static const uint8_t cfg_hs[] = {
#if !defined(NO_USB_MIDI) && !defined(NO_UAC2)
    9, 2, 0, 0, 6, 1, 0, 0x80, 250,                     /* wTotalLength patched in; 6 interfaces; 500 mA */
#elif !defined(NO_USB_MIDI)
    9, 2, 0, 0, 4, 1, 0, 0x80, 250,
#else
    9, 2, 0, 0, 4, 1, 0, 0x80, 250,
#endif
#ifndef NO_UAC2
#ifndef NO_USB_MIDI
    8, 11, 0, 4, 1, 0, 0x20, 0,                          /* IAD: interfaces 0-3: audio (UAC2) and its MIDI */
#else
    8, 11, 0, 2, 1, 0, 0x20, 0,                          /* IAD: interfaces 0-1, audio, UAC2 */
#endif
    9, 4, 0, 0, 0, 1, 1, 0x20, 4,                        /* AudioControl */
    9, 0x24, 1, 0x00, 0x02, 0x08, AC_LEN & 0xFF, AC_LEN >> 8, 0x00,       /* header, category I/O box */
    8, 0x24, 0x0A, 1, 0x01, 0x01, 0, 0,                  /* clock source 1: internal fixed, frequency readable */
    17, 0x24, 0x02, 2, 0x03, 0x06, 0, 1, 18, 0, 0, 0, 0, 5, 0, 0, 0,  /* input terminal 2: line connector, up to 18 ch, names from string 5 */
    12, 0x24, 0x03, 3, 0x01, 0x01, 0, 2, 1, 0, 0, 0,     /* output terminal 3: USB streaming, from 2, clock 1 */
    9, 4, 1, 0, 0, 1, 2, 0x20, 0,                        /* AudioStreaming alt 0: nothing */
    9, 4, 1, 1, 1, 1, 2, 0x20, 0,                        /* alt 1: the six channels */
    16, 0x24, 0x01, 3, 0x00, 0x01, 0x01, 0, 0, 0, 6, 0, 0, 0, 0, 0,  /* AS general: terminal 3, PCM, 6 ch */
    6, 0x24, 0x02, 0x01, 4, 24,                          /* format I: 4-byte slots, 24 bits */
    7, 5, 0x82, 0x05, 288 & 0xFF, 288 >> 8, 2,           /* EP 2 IN, isochronous async, 250 us */
    8, 0x25, 0x01, 0, 0, 0, 0, 0,                        /* class endpoint */
    9, 4, 1, 2, 1, 1, 2, 0x20, 0,                        /* alt 2: the sequencer's stems -- main L R, then eight stereo tracks */
    16, 0x24, 0x01, 3, 0x00, 0x01, 0x01, 0, 0, 0, 18, 0, 0, 0, 0, 0, /* AS general: terminal 3, PCM, 18 ch */
    6, 0x24, 0x02, 0x01, 4, 24,                          /* format I: 4-byte slots, 24 bits */
    7, 5, 0x82, 0x05, ISO_MAX & 0xFF, ISO_MAX >> 8, 2,   /* EP 2 IN, isochronous async, 250 us */
    8, 0x25, 0x01, 0, 0, 0, 0, 0,                        /* class endpoint */
#endif
#ifndef NO_USB_MIDI
#define BULK_LO 0x00
#define BULK_HI 0x02
    9, 4, MIDI_IF, 0, 0, 1, 1, 0, 0,                     /* AudioControl (MIDI's own) */
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, MIDI_IF + 1,        /* header 1.0, its streaming interface */
    9, 4, MIDI_IF + 1, 0, 2, 1, 3, 0, 13,                /* MIDIStreaming */
    7, 0x24, 1, 0x00, 0x01, 37, 0,                       /* header; wTotalLength 37: the header and the jacks only (USB-MIDI 1.0 6.1.2.1) */
    6, 0x24, 2, 1, 1, 0,                                 /* IN jack 1: embedded (from the computer) */
    6, 0x24, 2, 2, 2, 0,                                 /* IN jack 2: external (the DIN in) */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,                        /* OUT jack 3: embedded (to the computer), from 2 */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,                        /* OUT jack 4: external (the DIN out), from 1 */
    9, 5, 0x03, 0x02, BULK_LO, BULK_HI, 0, 0, 0,         /* EP 3 OUT */
    5, 0x25, 1, 1, 1,
    9, 5, 0x83, 0x02, BULK_LO, BULK_HI, 0, 0, 0,         /* EP 3 IN */
    5, 0x25, 1, 1, 3,
#undef BULK_LO
#undef BULK_HI
#endif
    9, 4, MSC_IF, 0, 2, 0x08, 0x06, 0x50, 11,            /* mass storage, SCSI, bulk-only */
    7, 5, 0x81, 0x02, 0x00, 0x02, 0,
    7, 5, 0x01, 0x02, 0x00, 0x02, 0,
    9, 4, LINK_IF, 0, 0, 0xFF, 0x41, 0x01, 12,           /* AERIA's link */
#ifdef PAD_TEST
    46, 0x24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 46, 0x24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* size test: 92 bytes of vendor class descriptors */
#endif
};
static const uint8_t cfg_fs[] = {
#ifndef NO_USB_MIDI
    9, 2, 0, 0, 4, 1, 0, 0x80, 250,
#else
    9, 2, 0, 0, 2, 1, 0, 0x80, 250,
#endif
#ifndef NO_USB_MIDI
#define BULK_LO 64
#define BULK_HI 0
    9, 4, MIDI_IF, 0, 0, 1, 1, 0, 0,                     /* AudioControl (MIDI's own) */
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, MIDI_IF + 1,        /* header 1.0, its streaming interface */
    9, 4, MIDI_IF + 1, 0, 2, 1, 3, 0, 13,                /* MIDIStreaming */
    7, 0x24, 1, 0x00, 0x01, 37, 0,                       /* header; wTotalLength 37: the header and the jacks only (USB-MIDI 1.0 6.1.2.1) */
    6, 0x24, 2, 1, 1, 0,                                 /* IN jack 1: embedded (from the computer) */
    6, 0x24, 2, 2, 2, 0,                                 /* IN jack 2: external (the DIN in) */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,                        /* OUT jack 3: embedded (to the computer), from 2 */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,                        /* OUT jack 4: external (the DIN out), from 1 */
    9, 5, 0x03, 0x02, BULK_LO, BULK_HI, 0, 0, 0,         /* EP 3 OUT */
    5, 0x25, 1, 1, 1,
    9, 5, 0x83, 0x02, BULK_LO, BULK_HI, 0, 0, 0,         /* EP 3 IN */
    5, 0x25, 1, 1, 3,
#undef BULK_LO
#undef BULK_HI
#endif
    9, 4, MSC_IF, 0, 2, 0x08, 0x06, 0x50, 11,
    7, 5, 0x81, 0x02, 64, 0, 0,
    7, 5, 0x01, 0x02, 64, 0, 0,
    9, 4, LINK_IF, 0, 0, 0xFF, 0x41, 0x01, 12,
};
/* strings: 1-4 maker, product, serial, audio; 5-10 the six channel names (the input terminal's iChannelNames);
 * the disk and link interfaces name 11 and 12 */
static const char *str_of(int i)
{
    switch (i) {
    case 1: return "sadnoise";  case 2: return "AERIA";  case 3: return "AERIA-0001";  case 4: return "AERIA audio";
    case 11: return "AERIA card";  case 12: return "AERIA link";  case 13: return "AERIA MIDI";
    }
    return 0;
}

/* ---- transfers ---- */
static void dtd_fill(volatile uint32_t *d, const void *buf, uint32_t len, int ioc)
{
    uint32_t a = (uint32_t)buf;
    d[0] = 1;
    d[1] = len << 16 | (ioc ? 0x8000u : 0) | 0x80u;
    d[2] = a;
    for (int p = 1; p < 5; p++) d[2 + p] = (a & ~0xFFFu) + 0x1000u * (uint32_t)p;
}
static int ep_bit(int ep, int in) { return in ? 1 << (16 + ep) : 1 << ep; }
static void ep_prime(int ep, int in, volatile uint32_t *d)
{
    volatile uint32_t *q = DQH(2 * ep + in);
    q[2] = (uint32_t)d; q[3] = 0;
    UR(PRIME) = (uint32_t)ep_bit(ep, in);
    uint32_t t = now();
    while ((UR(PRIME) & (uint32_t)ep_bit(ep, in)) && now() - t < MS(5)) {}
}
/* wait for a dTD to retire (or the bus to reset): bytes moved, -1 on reset/timeout */
static void usb_side_work(void);
static int dtd_wait(volatile uint32_t *d, uint32_t len, uint32_t ms)
{
    CRUMB(0x20);
    uint32_t t = now();
    while (d[1] & 0x80u) {
        if (UR(USBSTS) & 0x40u) return -1;
        if (ms && now() - t > MS(ms)) return -1;
        usb_side_work();
    }
    return (int)(len - ((d[1] >> 16) & 0x7FFFu));
}
static void ep0_stall(void) { UR(EPCTRL(0)) = UR(EPCTRL(0)) | 0x00010001u; }
static uint32_t ep0_wlen;   /* the request's wLength */
static void ep0_in(const void *data, uint32_t len)
{
    if (data != EP0BUF) { const uint8_t *s = data; for (uint32_t i = 0; i < len; i++) EP0BUF[i] = s[i]; }
    /* a short answer that ends on a packet boundary needs a zero-length packet after it; a full one must not */
    int zlp = len && len < ep0_wlen && len % 64 == 0;
    dtd_fill(DTD(1), EP0BUF, len, 1);
    if (zlp) { dtd_fill(DTD(4), EP0BUF, 0, 1); DTD(1)[0] = (uint32_t)DTD(4); }
    ep_prime(0, 1, DTD(1));
    dtd_fill(DTD(0), EP0BUF + 6144, 0, 1); ep_prime(0, 0, DTD(0));           /* the status stage */
    dtd_wait(zlp ? DTD(4) : DTD(1), zlp ? 0 : len, 500);
}
static void ep0_ack(void) { dtd_fill(DTD(1), EP0BUF, 0, 1); ep_prime(0, 1, DTD(1)); dtd_wait(DTD(1), 0, 500); }
static int ep0_out(uint32_t len)
{
    dtd_fill(DTD(0), EP0BUF, len, 1); ep_prime(0, 0, DTD(0));
    int n = dtd_wait(DTD(0), len, 1000);
    if (n >= 0) ep0_ack();
    return n;
}

/* ---- the controller ---- */
static void usb_ep_config(void)
{
    for (int i = 0; i < 8; i++) { volatile uint32_t *q = DQH(i); for (int k = 0; k < 16; k++) q[k] = 0; q[2] = 1; }
    DQH(0)[0] = 64u << 16 | 0x8000u | 0x20000000u;         /* EP0 OUT: interrupt on setup; no ZLP wait on full packets */
    DQH(1)[0] = 64u << 16 | 0x20000000u;                   /* EP0 IN: no automatic zero-length packets (ep0_in adds one) */
    uint32_t bulk = usb_hs ? 512u : 64u;
    DQH(2)[0] = bulk << 16 | 0x20000000u; DQH(3)[0] = bulk << 16 | 0x20000000u;
    DQH(5)[0] = 1u << 30 | (uint32_t)ISO_MAX << 16 | 0x20000000u;   /* EP2 IN iso, one transaction a microframe */
    DQH(6)[0] = bulk << 16 | 0x20000000u; DQH(7)[0] = bulk << 16 | 0x20000000u;   /* EP3: USB-MIDI */
}
static void usb_start(void)
{
    CRUMB(0x50);
    UR(USBCMD) = UR(USBCMD) & ~1u;
    { uint32_t t = now(); while (now() - t < MS(2)) {} }
    UR(USBMODE) = 0x0E;
    usb_ep_config();
    UR(EPLIST) = USB_MEM;
    UR(USBINTR) = 0;
    UR(USBSTS) = 0xFFFFFFFFu;
    UR(USBCMD) = UR(USBCMD) | 1u;
    usb_up = 1;
}
static void usb_bus_reset(void)
{
    CRUMB(0x60);
    UR(SETUPSTAT) = UR(SETUPSTAT);
    UR(COMPLETE) = UR(COMPLETE);
    UR(FLUSH) = 0xFFFFFFFFu;
    { uint32_t t = now(); while (UR(FLUSH) && now() - t < MS(10)) {} }
    UR(DEVADDR) = 0;
    for (int n = 1; n < 4; n++) UR(EPCTRL(n)) = 0;
    usb_config = 0; usb_alt_audio = 0; stems_on = 0; msc_armed = 0; iso_queued = 0; um_armed = 0; um_busy = 0;
    usb_hs = ((UR(PORTSC) >> 26) & 3) == 2;
    usb_ep_config();
    usb_resets++;
    UR(USBSTS) = 0x40u;
}

/* ---- the card as a disk ---- */
static int disk_on, disk_attention;
static uint32_t disk_sectors;
static uint8_t sense_key, sense_asc;
static uint32_t msc_tag;
static void msc_arm(void) { dtd_fill(DTD(2), MSCBUF, 31, 1); ep_prime(1, 0, DTD(2)); msc_armed = 1; }
static void msc_csw(uint32_t residue, int status)
{
    uint8_t *c = MSCBUF + 0xF000;
    c[0] = 'U'; c[1] = 'S'; c[2] = 'B'; c[3] = 'S';
    c[4] = (uint8_t)msc_tag; c[5] = (uint8_t)(msc_tag >> 8); c[6] = (uint8_t)(msc_tag >> 16); c[7] = (uint8_t)(msc_tag >> 24);
    c[8] = (uint8_t)residue; c[9] = (uint8_t)(residue >> 8); c[10] = (uint8_t)(residue >> 16); c[11] = (uint8_t)(residue >> 24);
    c[12] = (uint8_t)status;
    dtd_fill(DTD(3), c, 13, 1); ep_prime(1, 1, DTD(3)); dtd_wait(DTD(3), 13, 2000);
}
static int msc_send(const uint8_t *p, uint32_t n)
{
    dtd_fill(DTD(3), p, n, 1); ep_prime(1, 1, DTD(3));
    return dtd_wait(DTD(3), n, 3000);
}
static void sense(uint8_t key, uint8_t asc) { sense_key = key; sense_asc = asc; }
static int disk_ready(void)
{
    if (!disk_on) { sense(0x02, 0x3A); return 0; }
    if (disk_attention) { disk_attention = 0; sense(0x06, 0x28); return 0; }
    return 1;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void msc_command(void)
{
    CRUMB(0x30);
    const uint8_t *w = MSCBUF;
    if (w[0] != 'U' || w[1] != 'S' || w[2] != 'B' || w[3] != 'C') { msc_arm(); return; }
    msc_tag = (uint32_t)w[4] | (uint32_t)w[5] << 8 | (uint32_t)w[6] << 16 | (uint32_t)w[7] << 24;
    uint32_t want = (uint32_t)w[8] | (uint32_t)w[9] << 8 | (uint32_t)w[10] << 16 | (uint32_t)w[11] << 24;
    int in = (w[12] & 0x80) != 0;
    uint8_t cb[16]; for (int i = 0; i < 16; i++) cb[i] = w[15 + i];
    uint8_t *r = MSCBUF + 0x8000;
    uint32_t n = 0; int ok = 1;
    switch (cb[0]) {
    case 0x00: ok = disk_ready(); break;                                     /* TEST UNIT READY */
    case 0x03: for (int i = 0; i < 18; i++) r[i] = 0;                        /* REQUEST SENSE */
        r[0] = 0x70; r[2] = sense_key; r[7] = 10; r[12] = sense_asc; n = 18; sense(0, 0); break;
    case 0x12: {                                                             /* INQUIRY */
        static const char id[] = "sadnoiseAERIA CF CARD    0001";
        for (int i = 0; i < 36; i++) r[i] = 0;
        r[1] = 0x80; r[2] = 2; r[3] = 2; r[4] = 31;
        for (int i = 0; i < 28; i++) r[8 + i] = (uint8_t)id[i];
        n = 36; break; }
    case 0x1A: r[0] = 3; r[1] = 0; r[2] = 0; r[3] = 0; n = 4; break;        /* MODE SENSE (6) */
    case 0x5A: for (int i = 0; i < 8; i++) r[i] = 0; r[1] = 6; n = 8; break;  /* MODE SENSE (10) */
    case 0x1E: case 0x35: case 0x2F: ok = disk_on; if (!ok) sense(0x02, 0x3A); break;   /* PREVENT, SYNC, VERIFY */
    case 0x1B: if ((cb[4] & 3) == 2) { disk_on = 0; } break;                /* START STOP: eject leaves disk mode */
    case 0x23:                                                               /* READ FORMAT CAPACITIES */
        if (!disk_on) { ok = 0; sense(0x02, 0x3A); break; }
        for (int i = 0; i < 12; i++) r[i] = 0;
        r[3] = 8; put_be32(r + 4, disk_sectors); r[8] = 2; r[10] = 2; n = 12; break;
    case 0x25:                                                               /* READ CAPACITY (10) */
        if (!disk_ready()) { ok = 0; break; }
        put_be32(r, disk_sectors - 1); put_be32(r + 4, 512); n = 8; break;
    case 0x28: case 0x2A: {                                                  /* READ (10), WRITE (10) */
        if (!disk_ready()) { ok = 0; break; }
        uint32_t lba = be32(cb + 2), cnt = (uint32_t)cb[7] << 8 | cb[8];
        if (lba + cnt > disk_sectors) { ok = 0; sense(0x05, 0x21); break; }
        uint32_t done = 0;
        while (done < cnt && ok) {
            uint32_t k = cnt - done > 32 ? 32 : cnt - done;                  /* 16 KB a step (a dTD's reach) */
            if (cb[0] == 0x28) {
                if (!card_read(lba + done, (int)k, MSCBUF + 0x8000) || msc_send(MSCBUF + 0x8000, k * 512) < 0) ok = 0;
            } else {
                dtd_fill(DTD(2), MSCBUF + 0x8000, k * 512, 1); ep_prime(1, 0, DTD(2));
                if (dtd_wait(DTD(2), k * 512, 3000) < 0 || !card_write(lba + done, (int)k, MSCBUF + 0x8000)) ok = 0;
            }
            done += k;
        }
        if (!ok) sense(0x03, 0x11);
        msc_csw(ok ? 0 : (cnt - done) * 512, ok ? 0 : 1);
        msc_arm();
        return; }
    default: ok = 0; sense(0x05, 0x20); break;
    }
    if (n > want) n = want;
    if (in && want) {
        if (ok && n) msc_send(r, n);
        else if (!ok) {                                   /* the data the host asked for, as zeros, then a failed CSW */
            uint32_t k = want > 0x7000 ? 0x7000 : want;
            for (uint32_t i = 0; i < k; i++) r[i] = 0;
            msc_send(r, k); n = k;
        }
    }
    msc_csw(want - n, ok ? 0 : 1);
    msc_arm();
}

/* ---- audio: the DSP's tap (rec words through sampler.c's rec_word -> usb_tap) into a ring of frames ---- */
/* the stems' setting (alt 2): 18 channels a frame -- main L R, then tracks 1..8 as pairs, each after its effect */
#define STEM_FRAMES 2048u
static int32_t stem_ring[STEM_FRAMES][18];
static uint32_t stem_w, stem_r, stems_pushed; static int stem_pattern;
static uint32_t stem_dbg[6];   /* bring-up: the ring full; the packet queue found empty; packets sent empty; the least the ring held; the most; the least the queue held */
static void usb_stems(const int32_t *blk, const int32_t *main_lr)
{
    stems_pushed++;
    if (usb_alt_audio != 2) { stem_r = stem_w; return; }
    for (int i = 0; i < 16; i++) {
        if (stem_w - stem_r >= STEM_FRAMES - 1) { stem_dbg[0]++; return; }
        int32_t *f = stem_ring[stem_w % STEM_FRAMES];
        f[0] = main_lr[2 * i]; f[1] = main_lr[2 * i + 1];
        for (int t = 0; t < 8; t++) { f[2 + 2 * t] = blk[32 * t + 2 * i]; f[3 + 2 * t] = blk[32 * t + 2 * i + 1]; }
        if (stem_pattern == 1) for (int c = 0; c < 18; c++) f[c] = (c + 1) << 18;       /* bring-up: every channel its own constant */
        if (stem_pattern == 2) { static uint32_t ctr; ctr++; for (int c = 0; c < 18; c++) f[c] = (int32_t)(ctr & 0x3FFFFF); }   /* ... or a count of frames */
        stem_w++;
    }
}
#define TAP_FRAMES 4096u
static int32_t tap[TAP_FRAMES][6];
static uint32_t tap_w, tap_r, tap_ch;
void aos_pitch_sample(int32_t a, int32_t b, int32_t c, int32_t d);
static void usb_tap(int ch, int32_t v)
{
    if (ch == 0) tap_ch = 0;
    if (tap_ch != (uint32_t)ch) return;
    tap[tap_w % TAP_FRAMES][ch] = v;
    if (++tap_ch == 6) {
        int32_t *f = tap[tap_w % TAP_FRAMES];
        aos_pitch_sample(f[0], f[1], f[2], f[3]);
        resample_frame(f[4], f[5]);
        tap_ch = 0;
        if (usb_alt_audio == 2) {               /* the stems' setting outside the sequencer: the main pair, the stems silent */
            if (stem_w - stem_r < STEM_FRAMES - 1) { int32_t *g = stem_ring[stem_w % STEM_FRAMES]; g[0] = f[4]; g[1] = f[5]; for (int k = 2; k < 18; k++) g[k] = 0; stem_w++; }
        }
        if (usb_alt_audio != 1) tap_r = tap_w;  /* nobody streams the six: keep the ring empty */
        if (tap_w - tap_r < TAP_FRAMES - 1) tap_w++;
    }
}
#define ISO_TARGET 660u
static uint32_t iso_acc, iso_dry; static int iso_prime = 1;
/* keep N_ISO packets queued: each takes 11 frames (12 when the ring is fuller than 8 ms, 10 when under 2 ms) */
static void iso_fill(void)
{
    CRUMB(0x40);
    if (!usb_alt_audio) return;
    if (!iso_queued) stem_dbg[1]++;
    if (iso_queued < stem_dbg[5]) stem_dbg[5] = iso_queued;
    while (iso_queued < N_ISO - 1) {
        const int wide = usb_alt_audio == 2;
        const uint32_t nch = wide ? 18u : 6u;
        /* the packets' sizes follow the ring smoothly: 11.025 frames each, bent by at most 2 % towards a ring that holds
         * ISO_TARGET frames (15 ms: more than the longest the main loop is away). Sizes that jumped between 10 and 12, and a
         * ring that ran dry at every redraw, reached the computer late: it then recorded a quarter second old chunk */
        uint32_t have = wide ? stem_w - stem_r : tap_w - tap_r, n = 0;
        if (iso_prime && have >= ISO_TARGET) iso_prime = 0;
        if (!iso_prime) { if (have < stem_dbg[3]) stem_dbg[3] = have; if (have > stem_dbg[4]) stem_dbg[4] = have; }
        if (!iso_prime) {
            int32_t bend = ((int32_t)have - (int32_t)ISO_TARGET) * 24;
            if (bend > 14450) bend = 14450;
            if (bend < -14450) bend = -14450;
            iso_acc += (uint32_t)(722534 + bend); n = iso_acc >> 16; iso_acc &= 0xFFFFu;
            if (n >= have) { n = have; iso_prime = 1; iso_dry++; }      /* dry: one clean gap while it fills again */
        }
        int slot = (int)(iso_head % N_ISO);
        uint8_t *b = AUDBUF + ISO_MAX * (uint32_t)slot;
        if (wide) {
            uint8_t *p = b;
            for (uint32_t f = 0; f < n; f++, stem_r++) { const int32_t *s = stem_ring[stem_r % STEM_FRAMES]; for (int c = 0; c < 18; c++, p += 4) { uint32_t v = (uint32_t)s[c] << 8; __asm__("byterev %0" : "+d"(v)); *(volatile uint32_t *)(void *)p = v; } }   /* one store a sample: this memory is uncached */
        } else
        for (uint32_t f = 0; f < n; f++, tap_r++) for (int c = 0; c < 6; c++) {
            uint32_t v = (uint32_t)tap[tap_r % TAP_FRAMES][c] << 8;
            uint8_t *p = b + 24 * f + 4 * (uint32_t)c;
            p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
        }
        if (!n) { iso_short++; if (wide) stem_dbg[2]++; }
        volatile uint32_t *d = DTD(8 + slot);
        dtd_fill(d, b, n * 4 * nch, 0);
        d[1] |= 1u << 10;                                   /* MultO 1 */
        volatile uint32_t *prev = DTD(8 + (int)((iso_head + N_ISO - 1) % N_ISO));
        if (iso_queued) {
            prev[0] = (uint32_t)d;                          /* append: the add-dTD tripwire */
            if (!(UR(PRIME) & (1u << 18))) {
                uint32_t st;
                for (int tries = 0; tries < 1000; tries++) { UR(USBCMD) = UR(USBCMD) | (1u << 14); st = UR(EPSTAT) & (1u << 18); if (UR(USBCMD) & (1u << 14)) break; }
                UR(USBCMD) = UR(USBCMD) & ~(1u << 14);
                if (!st) ep_prime(2, 1, d);
            }
        } else ep_prime(2, 1, d);
        iso_head++; iso_queued++;
    }
}
static void iso_reap(void)
{
    while (iso_queued) {
        volatile uint32_t *d = DTD(8 + (int)((iso_head - iso_queued) % N_ISO));
        if (d[1] & 0x80u) break;
        iso_queued--; iso_sent++;
    }
    UR(COMPLETE) = 1u << 18;
}

/* ---- AERIA's link: vendor requests on EP0 ---- */
static uint32_t boot_src, boot_len, flash_pending_src, flash_pending_len;
static void usb_boot_new(void);
static uint8_t fz_by_hand;   /* the link set the freeze delay: the UI's settings stand back (tests) */
static uint8_t spi_last[24];
static void link_request(const uint8_t *s)
{
    uint32_t addr = (uint32_t)(s[3] << 8 | s[2]) << 16 | (uint32_t)(s[5] << 8 | s[4]);   /* wValue the high half, wIndex the low */
    uint32_t len = (uint32_t)s[6] | (uint32_t)s[7] << 8;
    if ((addr >> 27) == (0x40000000u >> 27) && s[1] <= 0x02 && !(s[0] & 0)) addr |= 0x08000000u;   /* SDRAM: through the uncached alias */
    switch (s[1]) {
    case 0x01: {                                    /* MEM READ: wValue:wIndex = address, wLength bytes */
        if (len > 4096) len = 4096;
        for (uint32_t i = 0; i < len; i++) EP0BUF[i] = *(volatile uint8_t *)(addr + i);
        ep0_in(EP0BUF, len); return; }
    case 0x02: {                                    /* MEM WRITE */
        if (len > 4096) { ep0_stall(); return; }
        if (ep0_out(len) < 0) return;
        for (uint32_t i = 0; i < len; i++) *(volatile uint8_t *)(addr + i) = EP0BUF[i];
        return; }
    case 0x03: ev_push((uint16_t)((s[4] & 1) << 8 | s[2])); ep0_ack(); return;            /* KEY: wValue code, wIndex pressed */
    case 0x04: ev_push((uint16_t)(0x8000u | (s[2] & 0x7F) << 8 | s[4])); ep0_ack(); return; /* ENC: wValue enc, wIndex delta (int8) */
    case 0x05: ep0_in(ui_frame, len < 1024 ? len : 1024); return;                       /* SCREEN */
    case 0x06: {                                    /* INFO */
        static char info[96]; int k = 0;
        const char *v = "AERIA OS " __DATE__ " " __TIME__;
        while (v[k] && k < 48) { info[k] = v[k]; k++; } info[k++] = 0;
        uint32_t f[8] = {usb_resets, usb_setups, iso_sent, iso_dry, dsp_words, tap_w, rec_mask | rec_seen_mask << 8, rec_headers};
        for (int i = 0; i < 8; i++) { info[k++] = (char)(f[i] >> 24); info[k++] = (char)(f[i] >> 16); info[k++] = (char)(f[i] >> 8); info[k++] = (char)f[i]; }
        ep0_in(info, len < (uint32_t)k ? len : (uint32_t)k); return; }
    case 0x08: cache_push_all(); ep0_ack(); return;                                          /* PUSH the data cache (a test) */
    case 0x09: {                                    /* READ CACHED: as MEM READ, through the cache */
        addr &= ~0x08000000u; if (len > 4096) len = 4096;
        for (uint32_t i = 0; i < len; i++) EP0BUF[i] = *(volatile uint8_t *)(addr + i);
        ep0_in(EP0BUF, len); return; }
    case 0x13: {                                    /* SPI by hand (bring-up: the real-time clock): up to 4 PUSHR words out, what came back */
        if (len > 16) { ep0_stall(); return; }
        if (ep0_out(len) < 0) return;
        int n = (int)len / 4; uint8_t r[24] = {0};
        for (int i = 0; i < n; i++) *(volatile uint32_t *)0xFC05C034u = be32(EP0BUF + 4 * i);
        { uint32_t t = now(); while (((*(volatile uint32_t *)0xFC05C02Cu >> 4) & 15) != (uint32_t)n && now() - t < MS(5)) {} }
        uint32_t sr = *(volatile uint32_t *)0xFC05C02Cu;
        for (int i = 0; i < n; i++) { uint32_t v = *(volatile uint32_t *)0xFC05C038u; r[4 * i] = (uint8_t)(v >> 24); r[4 * i + 1] = (uint8_t)(v >> 16); r[4 * i + 2] = (uint8_t)(v >> 8); r[4 * i + 3] = (uint8_t)v; }
        r[16] = (uint8_t)(sr >> 24); r[17] = (uint8_t)(sr >> 16); r[18] = (uint8_t)(sr >> 8); r[19] = (uint8_t)sr;
        *(volatile uint32_t *)0xFC05C02Cu = 0x00020000u;
        for (int i = 0; i < 24; i++) spi_last[i] = r[i];
        return; }
    case 0x14: ep0_in(spi_last, 24); return;        /* ... and its result */
    case 0x16: {                                    /* bring-up: the sequencer by hand: wValue what, wIndex its number */
        int a = s[2] | s[3] << 8, b = s[4] | s[5] << 8;
        if (a == 8) { int id = b & 255, lv = (b >> 8) & 15, r = id >> 3; if (r < 17) { led_set(id, lv > 0); tx((uint8_t)(r < 16 ? 0x20 | r : 0xA0)); tx(led_rows[r]); led_sent[r] = led_rows[r]; } tx((uint8_t)(0x30 | lv)); tx((uint8_t)id); }   /* bring-up: one lamp's level by hand (finding the unmapped lamps) */
        else if (a == 7) midi_byte_in((uint8_t)b); else if (a == 6) stem_pattern = b; else if (a == 5) { stems_on = b; aos_tap_want(4, b); } else if (a == 0) aos_seq_mode(b); else if (a == 1) aos_seq_play(b); else if (a == 2) aos_seq_hit(b & 7, b >> 8 ? b >> 8 : 100, 0); else if (a == 3) aos_seq_tempo(b); else if (a == 4) aos_seq_defaults();
        ep0_ack(); return; }
    case 0x15: {                                    /* bring-up: the master's freeze delay by hand: on, sixteenths, bpm, fb, hp, lp, send, wet, frozen */
        if (ep0_out(9) < 0) return;
        aos_freeze_delay(EP0BUF[0], EP0BUF[1], EP0BUF[2], EP0BUF[3], EP0BUF[4], EP0BUF[5], EP0BUF[6], EP0BUF[7], EP0BUF[8]);
        fz_by_hand = 1;
        return; }
    case 0x12: {                                    /* WRITE CACHED: as MEM WRITE, through the cache -- what the running OS sees.
                                                     * MEM READ / WRITE go round the cache (for staging images): a variable the
                                                     * OS keeps in a cached line reads stale and a poke never reaches it
                                                     * (it cost a day of confusing tests, 12 Nov 2026) */
        addr &= ~0x08000000u;
        if (len > 4096) { ep0_stall(); return; }
        if (ep0_out(len) < 0) return;
        for (uint32_t i = 0; i < len; i++) *(volatile uint8_t *)(addr + i) = EP0BUF[i];
        return; }
    case 0x0A: case 0x0B: {                         /* bring-up: restart this image without copying (0x0B: caches off first) */
        ep0_ack(); { uint32_t t = now(); while (now() - t < MS(20)) {} }
        if (dsp_running) dsp_park();
        UR(USBCMD) = UR(USBCMD) & ~1u; { uint32_t t = now(); while (now() - t < MS(50)) {} }
        if (s[1] == 0x0B) { cache_push_all(); cache_off(); }
        __asm__ volatile("jmp 0x40000400");
        return; }
    case 0x0C: {                                    /* FLASH: 12 bytes out (container in SDRAM, length, byte sum) */
        if (ep0_out(12) < 0) return;
        uint32_t src = be32(EP0BUF), n = be32(EP0BUF + 4), sum = be32(EP0BUF + 8), got = 0;
        const uint8_t *c = (const uint8_t *)(src | 0x08000000u);
        if (src < 0x40200000u || src + n > 0x47000000u || n < 64 || n > NOR_END - NOR_OS) { nor_result = 0xEA; return; }
        for (uint32_t i = 0; i < n; i++) got += c[i];
        if (got != sum || c[0] != 'E' || c[1] != 'L' || c[2] != 'E' || c[3] != 'K') { nor_result = 0xEB; return; }
        flash_pending_src = src; flash_pending_len = n;      /* written from the main loop, after this answer */
        return; }
    case 0x0D: {                                    /* NOR TEST: the last sector (unused): erase, program, verify, erase */
        ep0_ack();
        uint8_t *t = MSCBUF + 0x8000; for (int i = 0; i < 4096; i++) t[i] = (uint8_t)(i * 13 + 7);
        nor_result = 1;
        if (!nor_write(0x1F0000u, t, 4096)) { if (nor_result == 1) nor_result = 0xE9; return; }
        nor_result = nor_erase_sector(0x1F0000u) ? 2 : 0xE8;
        return; }
    case 0x0E: {                                    /* FLASH STATUS: result, progress */
        uint8_t r[8]; uint32_t a = nor_result, b = nor_progress;
        for (int i = 0; i < 4; i++) { r[i] = (uint8_t)(a >> (24 - 8 * i)); r[4 + i] = (uint8_t)(b >> (24 - 8 * i)); }
        ep0_in(r, len < 8 ? len : 8); return; }
    case 0x11: {                                    /* WRITE32: a peripheral register (4 bytes out) */
        if (ep0_out(4) < 0) return;
        *(volatile uint32_t *)(uintptr_t)addr = be32(EP0BUF);
        return; }
    case 0x10: {                                    /* HOST PORT by hand: wValue core, wIndex op */
        int core = s[2] & 1, op = s[4];
        REG8(DSP_SELECT) = (uint8_t)core;
        uint8_t r[16] = {0};
        if (op == 0) {                              /* status: ISR ICR CVR, then a word if RXDF */
            r[0] = (uint8_t)REG16(HI_ISR); r[1] = (uint8_t)REG16(HI_ICR); r[2] = (uint8_t)REG16(HI_CVR);
            if (r[0] & 1) { r[3] = 1; r[4] = (uint8_t)REG16(HI_TXH); r[5] = (uint8_t)REG16(HI_TXM); r[6] = (uint8_t)REG16(HI_TXL); }
            REG8(DSP_SELECT) = 0; ep0_in(r, len < 16 ? len : 16); return;
        }
        if (op == 4) {                              /* one engine write, whole (address with bit 31 = Y space, value): never between the OS's own pairs */
            REG8(DSP_SELECT) = 0;
            if (ep0_out(8) < 0) return;
            uint32_t a = be32(EP0BUF);
            dsp_write(a & 0x3FFFFFu, be32(EP0BUF + 4), (int)(a >> 31));
            return;
        }
        if (ep0_out(4) < 0) { REG8(DSP_SELECT) = 0; return; }
        uint32_t v = be32(EP0BUF);
        if (op == 1) { REG16(HI_TXH) = (uint16_t)((v >> 16) & 0xFF); REG16(HI_TXM) = (uint16_t)((v >> 8) & 0xFF); REG16(HI_TXL) = (uint16_t)(v & 0xFF); }
        else if (op == 2) REG16(HI_ICR) = (uint16_t)(v & 0xFF);
        else if (op == 3) REG16(HI_CVR) = (uint16_t)(v & 0xFF);
        REG8(DSP_SELECT) = 0;
        return; }
    case 0x07: {                                    /* BOOT: 8 bytes out: the image in SDRAM (address, length) */
        if (ep0_out(8) < 0) return;
        boot_src = be32(EP0BUF); boot_len = be32(EP0BUF + 4);
        usb_boot_new(); return; }
    }
    ep0_stall();
}

/* ---- EP0 ---- */
static void usb_setup(void)
{
    CRUMB(0x10);
    uint8_t s[8];
    for (int tries = 0; tries < 1000; tries++) { UR(USBCMD) = UR(USBCMD) | (1u << 13); for (int i = 0; i < 8; i++) s[i] = ((volatile uint8_t *)DQH(0))[0x28 + (i & ~3) + 3 - (i & 3)];   /* each word stored byte-reversed (1.40C byterevs them too) */ if (UR(USBCMD) & (1u << 13)) break; }
    UR(USBCMD) = UR(USBCMD) & ~(1u << 13);
    UR(SETUPSTAT) = 1;
    UR(FLUSH) = 0x00010001u;                              /* whatever EP0 still had primed belongs to the last request */
    { uint32_t t = now(); while ((UR(FLUSH) & 0x00010001u) && now() - t < MS(2)) {} }
    { uint32_t t = now(); while ((UR(SETUPSTAT) & 1) && now() - t < MS(2)) {} }
    usb_setups++;
    uint8_t type = s[0], req = s[1];
    uint32_t value = (uint32_t)s[2] | (uint32_t)s[3] << 8, index = (uint32_t)s[4] | (uint32_t)s[5] << 8, len = (uint32_t)s[6] | (uint32_t)s[7] << 8;
    ep0_wlen = len;
    if ((type & 0x60) == 0x40) { link_request(s); return; }
    if ((type & 0x60) == 0x20) {                       /* class */
        if ((type & 0x1F) == 1 && (index & 0xFF) == MSC_IF) {   /* mass storage */
            if (req == 0xFE) { EP0BUF[0] = 0; ep0_in(EP0BUF, len ? 1 : 0); return; }   /* GET MAX LUN */
            if (req == 0xFF) { UR(EPCTRL(1)) = UR(EPCTRL(1)) & ~0x00010001u; msc_arm(); ep0_ack(); return; }   /* reset */
        }
        if ((index & 0xFF) == 0 && (index >> 8) == 1) {    /* audio: the clock source */
            uint32_t cs = value >> 8;
            if (req == 0x01 && cs == 1) { uint8_t f[4] = {0x44, 0xAC, 0, 0}; ep0_in(f, len < 4 ? len : 4); return; }   /* CUR 44100 */
            if (req == 0x01 && cs == 2) { uint8_t v[1] = {1}; ep0_in(v, 1); return; }                                 /* valid */
            if (req == 0x02 && cs == 1) { uint8_t r[14] = {1, 0, 0x44, 0xAC, 0, 0, 0x44, 0xAC, 0, 0, 0, 0, 0, 0}; ep0_in(r, len < 14 ? len : 14); return; }
        }
        if (!(type & 0x80)) { if (len) ep0_out(len); else ep0_ack(); return; }   /* any class OUT: take it */
        ep0_stall(); return;
    }
    switch (req) {
    case 0x06: {                                          /* GET_DESCRIPTOR */
        uint32_t kind = value >> 8, i = value & 0xFF, n = 0;
        uint8_t *b = EP0BUF;
        if (kind == 1) { for (n = 0; n < 18; n++) b[n] = dev_desc[n]; }
        else if (kind == 2 || kind == 7) {
            const uint8_t *c = (kind == 2) == (usb_hs != 0) ? cfg_hs : cfg_fs;
            uint32_t total = c == cfg_hs ? sizeof cfg_hs : sizeof cfg_fs;
            for (n = 0; n < total; n++) b[n] = c[n];
            b[1] = (uint8_t)kind; b[2] = (uint8_t)total; b[3] = (uint8_t)(total >> 8);
        } else if (kind == 6) { for (n = 0; n < 10; n++) b[n] = qual_desc[n]; }
        else if (kind == 3) {
            if (i == 0) { b[0] = 4; b[1] = 3; b[2] = 0x09; b[3] = 0x04; n = 4; }
            else {
                const char *t = str_of((int)i);
                if (i >= 5 && i <= 10) { static const char *const ch[6] = {"In A", "In B", "In C", "In D", "Main L", "Main R"}; t = ch[i - 5]; }
                if (!t) { ep0_stall(); return; }
                n = 2; for (int k = 0; t[k]; k++) { b[n++] = (uint8_t)t[k]; b[n++] = 0; }
                b[0] = (uint8_t)n; b[1] = 3;
            }
        } else { ep0_stall(); return; }
        ep0_in(b, n < len ? n : len); return; }
    case 0x05:                                            /* SET_ADDRESS */
        UR(DEVADDR) = value << 25 | 1u << 24; ep0_ack(); return;
    case 0x09:                                            /* SET_CONFIGURATION */
        usb_config = (int)value;
        if (value) {
            UR(EPCTRL(1)) = 0x00C800C8u;                  /* EP1 bulk in/out, enabled, toggles reset */
            UR(EPCTRL(3)) = 0x00C800C8u;                  /* EP3 bulk in/out: USB-MIDI */
            um_armed = 0; um_busy = 0;
            if (usb_hs) UR(EPCTRL(2)) = 0x00C40008u;      /* EP2 IN iso (its unused OUT side typed bulk, not control) */
            msc_arm();
        }
        ep0_ack(); return;
    case 0x08: EP0BUF[0] = (uint8_t)usb_config; ep0_in(EP0BUF, 1); return;
    case 0x0B:                                            /* SET_INTERFACE */
        if (index == 1) {
            usb_alt_audio = (int)value;
            UR(FLUSH) = 1u << 18; { uint32_t t = now(); while (UR(FLUSH) && now() - t < MS(5)) {} }
            iso_queued = 0;
            if (usb_alt_audio) { tap_r = tap_w; stem_r = stem_w; iso_prime = 1; iso_acc = 0; }
            stems_on = usb_alt_audio == 2;
            aos_usb_audio(usb_alt_audio);
        }
        ep0_ack(); return;
    case 0x0A: EP0BUF[0] = index == 1 ? (uint8_t)usb_alt_audio : 0; ep0_in(EP0BUF, 1); return;
    case 0x00: EP0BUF[0] = 0; EP0BUF[1] = 0; ep0_in(EP0BUF, 2); return;      /* GET_STATUS */
    case 0x01: case 0x03:                                 /* CLEAR / SET FEATURE (endpoint halt) */
        if ((type & 0x1F) == 2 && req == 0x01) {
            int ep = (int)(index & 0x0F), in = (index & 0x80) != 0;
            if (ep) UR(EPCTRL(ep)) = (UR(EPCTRL(ep)) & ~(in ? 0x00010000u : 1u)) | (in ? 0x00400000u : 0x40u);
        }
        ep0_ack(); return;
    }
    ep0_stall();
}

/* ---- USB-MIDI: event packets (cable 0) both ways on EP3 ---- */
#define UMBUF_RX (USB_MEM + 0x8000u)
#define UMBUF_TX (USB_MEM + 0x8400u)
static uint8_t um_q[256][4];
static uint16_t um_w, um_r;
static void usb_midi_queue(unsigned st, unsigned a, unsigned b)
{
    if (!usb_config || (uint16_t)(um_w - um_r) >= 255) return;
    unsigned cin = st >= 0xF8 ? 0xF : (st & 0xF0) == 0xF0 ? (st == 0xF2 ? 3 : st == 0xF1 || st == 0xF3 ? 2 : 5) : st >> 4;
    uint8_t *p = um_q[um_w++ % 256];
    p[0] = (uint8_t)cin; p[1] = (uint8_t)st; p[2] = (uint8_t)a; p[3] = (uint8_t)b;
}
static void usb_midi_service(void)
{
    CRUMB(0x71);
    volatile uint32_t *rx = DTD(5), *txd = DTD(6);
    if (!um_armed) { dtd_fill(rx, (void *)UMBUF_RX, 512, 1); ep_prime(3, 0, rx); um_armed = 1; }
    else if (!(rx[1] & 0x80u)) {
        uint32_t n = 512u - ((rx[1] >> 16) & 0x7FFFu);
        const volatile uint8_t *b = (const volatile uint8_t *)UMBUF_RX;
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            unsigned cin = b[i] & 15;
            if (cin >= 8 || cin == 0xF) module_midi(b[i + 1], b[i + 2], b[i + 3]);
        }
        um_armed = 0;
    }
    if (um_busy && !(txd[1] & 0x80u)) um_busy = 0;
    if (!um_busy && um_w != um_r) {
        volatile uint8_t *o = (volatile uint8_t *)UMBUF_TX;
        uint32_t n = 0;
        while (um_r != um_w && n < 64) { const uint8_t *p = um_q[um_r++ % 256]; o[n] = p[0]; o[n + 1] = p[1]; o[n + 2] = p[2]; o[n + 3] = p[3]; n += 4; }
        dtd_fill(txd, (void *)UMBUF_TX, n, 1); ep_prime(3, 1, txd); um_busy = 1;
    }
}

/* ---- each pass of the main loop ---- */
static uint32_t usb_last_bsv;
static void usb_poll(void)
{
    if (flash_pending_len) {                              /* a FLASH request answered: write it now */
        uint32_t src = flash_pending_src, n = flash_pending_len;
        flash_pending_len = 0; nor_result = 1; nor_progress = 0;
        if (dsp_running) { dsp_park(); dsp_running = 0; }  /* nothing else may run meanwhile */
        nor_result = nor_write(NOR_OS, (const uint8_t *)(src | 0x08000000u), n) ? 2 : (nor_result == 1 ? 0xE4 : nor_result);
    }
    CRUMB(0x01);
    uint32_t bsv = UR(OTGSC) & 0x800u;
    if (bsv && !usb_up) usb_start();
    if (!bsv && usb_up && usb_last_bsv) { UR(USBCMD) = UR(USBCMD) & ~1u; usb_up = 0; usb_config = 0; usb_alt_audio = 0; aos_usb_audio(0); }
    usb_last_bsv = bsv;
    if (!usb_up) return;
    uint32_t st = UR(USBSTS);
    if (st & 0x40u) usb_bus_reset();
    if (st & 0x04u) { usb_hs = ((UR(PORTSC) >> 26) & 3) == 2; UR(USBSTS) = 0x04u; }
    UR(COMPLETE) = UR(COMPLETE);                          /* every transfer is followed by its dTD; the bits only latch */
    if (UR(SETUPSTAT) & 1) usb_setup();
    if (usb_config) {
        if (msc_armed && !(DTD(2)[1] & 0x80u)) { msc_armed = 0; msc_command(); }
        iso_reap(); iso_fill();
        usb_midi_service();
    }
}
/* the card's own waits keep audio moving: called from dtd_wait */
static void usb_iso_side(void) { if (usb_config && usb_alt_audio) { iso_reap(); iso_fill(); } }
static void usb_side_work(void) { if (dsp_running) { dsp_poll(); stream_pump(); } if (usb_config) { iso_reap(); iso_fill(); } }

/* a new AERIA OS, sent into SDRAM over the link: the DSP handed back (its boot-ROM stand-in), USB detached, then a
 * copy loop placed high in SDRAM moves the image to 0x40000400 and starts it, as the RAM-boot gate would */
static void usb_boot_new(void)
{
    if (boot_len < 1024 || boot_len > 0x140000 || boot_src < 0x40200000u || boot_src + boot_len > 0x47000000u) return;
    boot_src |= 0x08000000u;   /* read it uncached too: what the link wrote went through the cache, so push it first */
    __asm__ volatile("nop");
    cache_push_all();
    if (*(volatile uint32_t *)boot_src != 0x4FEFFFE4u) return;
    { uint32_t t = now(); while (now() - t < MS(20)) {} }      /* the status stage on its way */
    if (dsp_running) dsp_park();
    UR(USBCMD) = UR(USBCMD) & ~1u;                              /* off the bus: the host sees it go */
    { uint32_t t = now(); while (now() - t < MS(50)) {} }
    /* the copy loop, placed at 0x47F80000 (written through the uncached alias, so the instruction cache cannot hold
     * an old copy of it): move.l (a0)+,(a1)+ / subq.l #4,d0 / bgt.s -6 / move.l #0xA50CE100,d1 / movec d1,cacr /
     * nop / jmp (a2). The image goes to 0x40000400 through the alias too; the CACR write invalidates all three caches
     * (they stay on: turning them off before the jump faulted, 10 Nov 2026) */
    cache_push_all();
    static const uint16_t loop[] = {0x22D8, 0x5980, 0x6EFA, 0x223C, 0xA50C, 0xE100, 0x4E7B, 0x1002, 0x4E71, 0x4ED2};
    volatile uint16_t *at = (volatile uint16_t *)0x4FF80000u;
    for (int i = 0; i < 10; i++) at[i] = loop[i];
    __asm__ volatile("move.l %0,%%a0\n\tmove.l %1,%%d0\n\tmove.l #0x48000400,%%a1\n\tmove.l #0x40000400,%%a2\n\tjmp 0x47F80000"
                     :: "g"(boot_src), "g"(boot_len) : "a0", "a1", "a2", "d0", "d1", "memory");
}

/* USB DISK MODE (AERIA's SYSTEM page): the card goes to the computer, AERIA stays off it */
void aos_usb_disk(int on)
{
    if (on && !disk_on) {
        disk_sectors = card_ok ? card_capacity() : 0;
        if (!disk_sectors) return;
        disk_on = 1; disk_attention = 1;
    } else if (!on && disk_on) {
        disk_on = 0;
        /* off the bus for a moment and back: the computer lets go of the disk at once instead of retrying a card that
         * went away (a Mac still holding it got stuck, 10 Nov 2026); the audio and MIDI ports come straight back */
        if (usb_up) {
            UR(USBCMD) = UR(USBCMD) & ~1u;
            { uint32_t t = now(); while (now() - t < MS(300)) {} }
            usb_bus_reset();
            UR(USBCMD) = UR(USBCMD) | 1u;
        }
        fat_mount();                                      /* the computer may have changed everything */
    }
}
int aos_disk_busy(void) { return disk_on; }
