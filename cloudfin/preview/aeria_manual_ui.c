/* SPDX-License-Identifier: MIT
 * AERIA by sadnoise -- a multi-effects instrument in a sky, for the Octatrack MKII.
 *
 * AERIA owns the screen, every key, every encoder and the trig LEDs. Under it the stock engine
 * runs the AERIA project: T3 THRU takes inputs A B C D; its FX1 is HEAD (AERIA's input mixer
 * and main feedback); T3 FX2 and T4-T8 (NEIGHBOR machines, each taking the track before) give
 * eleven chain slots, filled from the SHELF with any installed effect (stock, Modwerk, AERIA's
 * Balloon Shop effects). Sources, off by default, reach HEAD through the key bus: T1 plays a
 * LOOP (or the caught BALLOONS), T2 plays STARS; their own FX1 is TAP (silent out). Every
 * change goes through stock's own routines: the page-1 writer, the base-guarded FX setters,
 * DIRSCAN and the slot loader, USB DISK MODE. The weather moves every knob by what it does.
 *
 *   SRC CHAIN  trig/track 1-11 a slot (FUNC: bypass), A-F its knobs, BANK or trig 12 the shelf,
 *              13/14 weather down/up, 15 auto, 16 lightning
 *   AMP SKY  LFO SOURCE (FUNC+1/2: pick from the card)  FX1 MIX (main feedback)  FX2 SYSTEM
 *   PART BALLOON  AED LOOP  ARR STARS   LEVEL weather   SCENE A/B + trig: crossfader weathers
 *   AB CD SRC3 catch a balloon (again: let go; FUNC+: pop, silently)   T1+T2 / T3+T4 stereo/mono
 *   YES day/night  NO new sky  FUNC+YES intro  FUNC+NO the stock screens   PLAY STOP REC CUE pass.
 *
 * Frame: 128 columns of two big-endian words; line y is bit (y & 31) of word
 * x*2 + (y < 32). No C library, no 64-bit arithmetic, no jump tables. */
#include "modwerk_module.h"
/* Jump tables are data in .text; the loader refuses what they disassemble to. */
#pragma GCC optimize("no-jump-tables")
#include "logo.inc"

#define W 128
#define H 64
#define WATER_Y 54

/* ---- the unit ------------------------------------------------------------ */
#define PAT_STRIDE 2330u
#define PAT_LOCKS 0x59u        /* step records of 32 lock bytes, from the masks */
#define NOLOCK 0xff
#ifdef PREVIEW
uint8_t preview_lane[8][72];
uint16_t preview_dsp[8][32];
int preview_leds[128];
uint32_t preview_xfade = 0, preview_step = 0, preview_play = 1;
int preview_track;
uint8_t preview_pattern[8][2330 + 100];
static void set_led(int id, int n) { if (id >= 0 && id < 128) preview_leds[id] = n; }
static void leds_clear_trigs(void) { for (int i = 0; i < 32; i++) preview_leds[i] = 0; }
static int write_mode = 1;
static uint8_t sent[8][32];
static void lane_put(int t, int slot, int v) { if (!write_mode) return; preview_lane[t][slot] = (uint8_t)v; preview_dsp[t][slot] = (uint16_t)(v << 8); }
static int xfade(void) { return (int)preview_xfade; }
static void set_current_track(int t) { preview_track = t; }
static int current_track(void) { return preview_track; }
static unsigned master_step(void) { return preview_step; }
static int playing(void) { return preview_play == 1; }
static volatile uint8_t *pattern_track(int t) { return &preview_pattern[t][50]; }
static int rig_loaded(void) { return 1; }
static uint32_t bank_base(void) { return 1; }
#else
typedef void (*led_fn)(int id, int ticks);
static void set_led(int id, int n) { ((led_fn)0x40013784u)(id, n); }
/* the trig LEDs (ids 0-31): countdowns 0x460ba9cc (longs) and on-bits 0x460ba9ae (PANEL.md section 8);
 * cleared here so stock's step light never shows; AERIA's own set_led calls then flush them */
static void leds_clear_trigs(void)
{
    volatile uint32_t *count = (volatile uint32_t *)0x460ba9ccu;
    volatile uint8_t *bits = (volatile uint8_t *)0x460ba9aeu;
    for (int i = 0; i < 32; i++) count[i] = 0;
    for (int i = 0; i < 4; i++) bits[i] = 0;
}
/* Every sound change goes through stock's page-1 writer, the MIDI CC store
 * (0x40054cd8(track, flat, value), as Apply Scene calls it): it refuses
 * disabled slots, clamps to the descriptor and writes the working Part, its
 * copy and the live lane -- a knob turn. Only values that changed are sent.
 * write_mode: 0 off, 1 the writer, 2 the bare live lane (diagnostic). */
static int write_mode = 1;
static uint8_t sent[8][32];
static int sent_ready;
typedef void (*p1_writer_fn)(int track, int flat, int value);
static void lane_put(int t, int slot, int v)
{
    if (write_mode == 0 || (unsigned)t > 7u || (unsigned)slot > 29u || v < 0 || v > 127) return;
    if (!sent_ready) { for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff; sent_ready = 1; }
    if (sent[t][slot] == (uint8_t)v) return;
    sent[t][slot] = (uint8_t)v;
    if (write_mode == 1) ((p1_writer_fn)0x40054cd8u)(t, slot, v);
    else *(volatile uint8_t *)(0x80000810u + 72u * (unsigned)t + (unsigned)slot) = (uint8_t)v;
}
static int xfade(void)
{
    uint32_t v = *(volatile uint32_t *)0x400c0c44u; /* the fader value stock's morph last sent */
    return v <= 127u ? (int)v : -1;
}
/* The active track the recorder keys (REC AB 0x40030e6c) read. */
static void set_current_track(int t) { *(volatile uint8_t *)0x100b14ccu = (uint8_t)t; }
static int current_track(void) { return *(volatile uint8_t *)0x100b14ccu; }
static unsigned master_step(void) { return *(volatile uint16_t *)0x800065b4u; }
static int playing(void) { return *(volatile uint32_t *)0x800065b8u == 1u; }
/* The loaded bank (the pointer SYNTIZ reads the Part through, 0x46c82456): 16
 * patterns of 36568 bytes, then the Parts at +0x8ed80; audio track t at +2330 t;
 * the current pattern 0x100b14d0 (stock's own walk at 0x4000de3e). */
static uint32_t bank_base(void)
{
    uint32_t base = *(volatile uint32_t *)0x46c82456u;
    return base >= 0x40000000u && base <= 0x47f00000u ? base : 0;
}
static volatile uint8_t *pattern_track(int t)
{
    uint32_t base = bank_base();
    unsigned p = *(volatile uint8_t *)0x100b14d0u & 15u;
    return (volatile uint8_t *)(base + 36568u * p + PAT_STRIDE * (unsigned)t);
}
/* The AERIA rig is loaded: in the working Part (SYNTIZ's address), T2 is STATIC
 * T1 FLEX (1), T2 STATIC (0), T5 THRU (2), T6-T8 NEIGHBOR (3). Nothing is placed in any other project. */
static int rig_loaded(void)
{
    uint32_t base = *(volatile uint32_t *)0x46c82456u;
    if (base < 0x40000000u || base > 0x47f00000u) return 0;
    volatile uint8_t *part = (volatile uint8_t *)(base + 0x8ed80u + (unsigned)(*(volatile uint8_t *)0x100b14cfu & 3u) * 6322u);
    if (part[0x22 + 0] != 1 || part[0x22 + 1] != 0 || part[0x22 + 4] != 2) return 0;
    for (int t = 5; t < 8; t++) if (part[0x22 + t] != 3) return 0;
    return 1;
}
#endif

/* keys */
#define KEY_TRIG0 0x00
#define KEY_TRACK0 0x10
#define KEY_SCENEA 0x19
#define KEY_SCENEB 0x1a
#define KEY_PAGE 0x1b
#define KEY_PROJ 0x1c
#define KEY_PART 0x1d
#define KEY_AED 0x1e
#define KEY_ARR 0x1f
#define KEY_DOWN 0x20
#define KEY_RIGHT 0x21
#define KEY_SRC 0x22
#define KEY_AMP 0x23
#define KEY_LFO 0x24
#define KEY_FX1 0x25
#define KEY_FX2 0x26
#define KEY_STOP 0x27
#define KEY_PLAY 0x28
#define KEY_REC 0x29
#define KEY_CUE 0x2a
#define KEY_REC_AB 0x2b
#define KEY_REC_CD 0x2c
#define KEY_FUNC 0x2d
#define KEY_PATTERN 0x2e
#define KEY_BANK 0x2f
#define KEY_YES 0x31
#define KEY_NO 0x32
#define KEY_UP 0x33
#define KEY_LEFT 0x34
#define KEY_MIDI 0x35
#define KEY_REC3 0x36
#define KEY_PUSH0 0x38
#define KEY_LEVEL_PUSH 0x3e

/* voices */
#define T_GRAIN 0
#define T_STARS 1
#define T_PLAYER 2
#define T_BALLOON 3
#define T_IN 4

/* ---- small tools ----------------------------------------------------------- */
static uint32_t seed = 0x2f6b9d13u, ticks;
static const int8_t sine[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12};
static int sin10(int a) { return sine[(a >> 4) & 63]; } /* a: 0..1023 a turn */
static int cos10(int a) { return sine[((a >> 4) + 16) & 63]; }
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int isqrt(int n) { int r = 0; while ((r + 1) * (r + 1) <= n) r++; return r; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- pixels ------------------------------------------------------------------ */
static uint32_t *F;
static void px(int x, int y) { if ((unsigned)x < W && (unsigned)y < H) F[x * 2 + (y < 32)] |= 1u << (y & 31); }
static void px_off(int x, int y) { if ((unsigned)x < W && (unsigned)y < H) F[x * 2 + (y < 32)] &= ~(1u << (y & 31)); }
static uint32_t bits(int a, int b)
{
    uint32_t hi = b >= 31 ? 0xffffffffu : (1u << (b + 1)) - 1u;
    return hi & ~((1u << a) - 1u);
}
/* lines a..b of column x: op 0 clear, 1 set, 3 invert */
static void span(int x, int a, int b, int op)
{
    if ((unsigned)x >= W) return;
    if (a < 0) a = 0;
    if (b > H - 1) b = H - 1;
    for (int half = 0; half < 2 && a <= b; half++) {
        int lo = half ? 32 : 0, s = a > lo ? a : lo, e = b < lo + 31 ? b : lo + 31;
        if (s > e) continue;
        uint32_t m = bits(s - lo, e - lo), *w = &F[x * 2 + (half ? 0 : 1)];
        if (op == 0) *w &= ~m; else if (op == 1) *w |= m; else *w ^= m;
    }
}
static void line(int x0, int y0, int x1, int y1)
{
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 256; n++) {
        px(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void disk_clear(int cx, int cy, int r)
{
    for (int dy = -r; dy <= r; dy++) { int w = isqrt(r * r - dy * dy); for (int x = cx - w; x <= cx + w; x++) px_off(x, cy + dy); }
}
static void circle(int cx, int cy, int r)
{
    for (int a = 0; a < 1024; a += r > 8 ? 8 : 16) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127);
}
static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static int dither_on(int x, int y, int level) { return bayer[(y & 3) * 4 + (x & 3)] < level; }

/* ---- 3x5 font ----------------------------------------------------------------- */
static const uint16_t glyphs[] = {
    0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79a7, 0x79a4, 0x396b, 0x5bed, 0x7497, 0x126a,
    0x5bad, 0x4927, 0x5fed, 0x6b6d, 0x2b6a, 0x6ba4, 0x2b73, 0x6bad, 0x388e, 0x7492,
    0x5b6f, 0x5b6a, 0x5bfd, 0x5aad, 0x5a92, 0x72a7, 0x7b6f, 0x2c97, 0x62a7, 0x628e,
    0x5bc9, 0x798e, 0x39ef, 0x7292, 0x7bef, 0x7bce, 0x05d0, 0x01c0, 0x0410,
    0x0002, 0x2922, 0x224a, 0x0007, 0x2400, 0x2aab, 0x12a4, 0x5f7d, 0x52a5};
static int glyph_of(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    static const char more[] = "+-:.()_'&/#%";
    for (int i = 0; more[i]; i++) if (c == more[i]) return 36 + i;
    return -1;
}
static int text_w(const char *s) { int n = 0; while (s[n]) n++; return n * 4 - 1; }
static void text(int x, int y, const char *s, int ink)
{
    for (; *s; s++, x += 4) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (glyphs[g] & (1u << (14 - (r * 3 + c)))) { if (ink) px(x + c, y + r); else px_off(x + c, y + r); }
    }
}
static void num(char *out, int v)
{
    int i = 0;
    if (v < 0) { out[i++] = '-'; v = -v; }
    if (v >= 10000) out[i++] = (char)('0' + v / 10000 % 10);
    if (v >= 1000) out[i++] = (char)('0' + v / 1000 % 10);
    if (v >= 100) out[i++] = (char)('0' + v / 100 % 10);
    if (v >= 10) out[i++] = (char)('0' + v / 10 % 10);
    out[i++] = (char)('0' + v % 10);
    out[i] = 0;
}

/* ---- weather ---------------------------------------------------------------------- */
enum { CLEAR, CLOUDY, RAIN, STORM, SNOW, HEAT, FOG, N_WEATHER };
static const char *const weather_name[N_WEATHER] = {"CLEAR", "CLOUDY", "RAIN", "STORM", "SNOW", "HEAT", "FOG"};
static int weather = CLOUDY, auto_weather, xf_right = STORM, amount;
/* Several weathers at once (sadnoise, 12 Nov 2026): each of CLOUDY RAIN STORM SNOW HEAT FOG is on or off by itself --
 * only SNOW and HEAT never together; none on is CLEAR. wx_on is the set (bit = the weather's number); `weather` is
 * the one turned on last (the sky's main mood, the spells' clock). */
static uint8_t wx_on = 1u << CLOUDY;
#define WON(w) ((wx_on >> (w)) & 1u)
static uint32_t spell_left;
static int flash, thunder, bolt_x, bolt_t, gust;
static const uint16_t spell_s[N_WEATHER][2] = {{90, 300}, {60, 240}, {60, 200}, {30, 120}, {90, 300}, {120, 360}, {60, 180}};
static const uint8_t next_w[N_WEATHER][N_WEATHER] = {
    {4, 11, 0, 0, 0, 5, 0}, {7, 0, 7, 3, 3, 0, 0}, {4, 10, 0, 6, 0, 0, 0}, {0, 8, 12, 0, 0, 0, 0},
    {8, 12, 0, 0, 0, 0, 0}, {12, 3, 0, 5, 0, 0, 0}, {8, 8, 4, 0, 0, 0, 0}};

static char toast[24];
static uint32_t toast_until;
static void say(const char *a, const char *b)
{
    int i = 0;
    while (*a && i < 12) toast[i++] = *a++;
    if (b) { toast[i++] = ' '; while (*b && i < 23) toast[i++] = *b++; }
    toast[i] = 0;
    toast_until = ticks + 80;
}

/* ---- pages ------------------------------------------------------------------------------------ */
typedef struct {
    char name[5];
    uint8_t mask, slot;   /* tracks and lane slot; mask 0 = AERIA's own */
    uint8_t lo, hi;
    int16_t value, user;
    uint8_t drift;
} Param;
#define OWN 0, 0
#define EMPTY {"", 0, 0, 0, 0, 0, 0, 0}
#define T1M 0x01u
#define T2M 0x02u

enum { PG_CHAIN, PG_SKY, PG_SOURCE, PG_LOOP, PG_STARS, PG_BALLOON, PG_MIX, PG_SYSTEM, PG_MIDI, PG_GAIN, PG_MASTER, N_PAGES };
static const char *const page_name[N_PAGES] = {"CHAIN", "SKY", "SOURCE", "LOOP", "STARS", "BALLOON", "MIX", "SYSTEM", "GUMBALLS", "GAIN", "MASTER"};

static Param params[N_PAGES][12] = {
    /* CHAIN: the shelf */
    {EMPTY},
    /* SKY */
    {{"WIND", OWN, 0, 18, 12, 12, 0}, {"CLDS", OWN, 0, 7, 5, 5, 0}, {"WAVE", OWN, 0, 6, 2, 2, 0},
     {"WTHR", OWN, 0, N_WEATHER - 1, CLOUDY, CLOUDY, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, EMPTY,   /* (XFAD and XF B, the crossfade between two weathers, are gone: the fader is the scenes' now) */
     EMPTY, {"SPEL", OWN, 1, 8, 4, 4, 0}, {"DRFT", OWN, 0, 127, 20, 20, 0},
     {"RATE", OWN, 1, 127, 30, 30, 0}, {"NITE", OWN, 0, 1, 0, 0, 0}, {"LITE", OWN, 0, 1, 1, 1, 0}},
    /* SOURCE: a menu */
    {EMPTY},
    /* LOOP: Flex slot 1 on T1, between START and END */
    {{"STRT", T1M, 1, 0, 127, 0, 0, 0}, {"END", OWN, 1, 127, 127, 127, 0}, {"PTCH", T1M, 0, 16, 112, 64, 64, 0},
     {"RATE", T1M, 3, 0, 127, 127, 127, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"ATK", T1M, 12, 0, 127, 0, 0, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* STARS: one-shots on T2 */
    {{"SPD", OWN, 0, 127, 24, 24, 0}, {"NUM", OWN, 1, 8, 4, 4, 0}, {"PRND", OWN, 0, 127, 0, 0, 0},
     {"ARND", OWN, 0, 127, 0, 0, 0}, {"DRND", OWN, 0, 127, 20, 20, 0}, {"CHNC", OWN, 0, 127, 127, 127, 0},
     {"PTCH", OWN, 16, 112, 64, 64, 0}, {"ATK", OWN, 0, 127, 0, 0, 0}, {"DEC", OWN, 0, 127, 96, 96, 0},
     {"BANK", OWN, 0, 7, 0, 0, 0}, {"MODE", OWN, 0, 1, 0, 0, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}},
    /* BALLOON: the caught sounds, played as a cloud on T1 */
    /* (AERIA OS: every floating balloon sings its own long grains; the FUNC layer's six start at 1, so a preset
     * saved before they existed -- it holds 0 there -- keeps their defaults: persist.c) */
    {{"SIZE", T1M, 5, 0, 127, 60, 60, 0}, {"SPRY", OWN, 0, 127, 30, 30, 0}, {"PTCH", T1M, 0, 16, 112, 64, 64, 0},
     {"DENS", T1M, 4, 0, 127, 40, 40, 0}, {"POS", OWN, 0, 127, 20, 20, 0}, {"SEND", OWN, 0, 127, 100, 100, 0},
     {"OCT", OWN, 1, 5, 3, 3, 0}, {"RNDO", OWN, 1, 127, 1, 1, 0}, {"FADE", OWN, 1, 127, 1, 1, 0},
     {"TUN1", OWN, 1, 49, 25, 25, 0}, {"TUN2", OWN, 1, 49, 25, 25, 0}, {"TUN3", OWN, 1, 49, 25, 25, 0}},
    /* MIX: HEAD (T3 FX1): main feedback, its tone, the sources' sends, input level, warmth; then routing */
    {{"FDBK", OWN, 0, 127, 0, 0, 0}, {"FTON", OWN, 0, 127, 90, 90, 0}, EMPTY,
     {"WARM", OWN, 0, 127, 20, 20, 0}, {"HELI", OWN, 0, 1, 0, 0, 0}, {"WSND", OWN, 0, 1, 1, 1, 0},
     {"INAB", OWN, 0, 4, 1, 1, 0}, {"INCD", OWN, 0, 4, 1, 1, 0}, {"SCAL", OWN, 0, 41, 0, 0, 0},
     {"ROOT", OWN, 0, 11, 0, 0, 0}, {"LIMT", OWN, 1, 2, 2, 2, 0}, {"TIDE", OWN, 0, 127, 0, 0, 0}},   /* LIMT 2 on, 1 off (0: a preset from before it) */
    /* SYSTEM */
    {{"SND", OWN, 0, 2, 1, 1, 0}, {"INTR", OWN, 0, 1, 1, 1, 0}, {"LITE", OWN, 0, 1, 1, 1, 0},
     {"AUTO", OWN, 0, 1, 1, 1, 0}, {"SAVR", OWN, 0, 30, 30, 30, 0}, {"WAIT", OWN, 0, 7, 3, 3, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* GUMBALLS: MIDI, its own menu */
    {EMPTY},
    /* GAIN: input A/B and C/D (THRU VOL, 64 = unity), the drive into the chain (HEAD IN),
     * the chain's output (T8 AMP VOL, 108 = unity), the cue send and the balance */
    {{"AB", OWN, 0, 127, 64, 64, 0}, {"CD", OWN, 0, 127, 64, 64, 0}, {"DRIV", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"CUE", OWN, 0, 127, 127, 127, 0}, {"BAL", OWN, 0, 127, 64, 64, 0},
     /* FUNC layer: the master EQ (12 Nov 2026), after everything and never moved by the weather. Four gains 1..25
      * (13 = flat, a dB a step) and the two middle bands' centres (an index on 20 Hz .. 20 kHz); the shelves sit at
      * 150 Hz and 6 kHz. (They start at 1 so a preset from before them, which holds 0 there, keeps them flat.) */
     {"LOW", OWN, 1, 25, 13, 13, 0}, {"LMID", OWN, 1, 25, 13, 13, 0}, {"HMID", OWN, 1, 25, 13, 13, 0},
     {"HIGH", OWN, 1, 25, 13, 13, 0}, {"LMF", OWN, 1, 127, 55, 55, 0}, {"HMF", OWN, 1, 127, 89, 89, 0}},
    /* MASTER (12 Nov 2026): the freeze delay on the main mix, after the chain and never moved by the weather. TIME in
     * sixteenth notes at TMPO (the trigs pick it too), its feedback with a high-pass and a lowpass, how much goes in
     * and how much comes out; YES freezes it, NO taps the tempo. FUNC layer: TMPO (BPM). */
    {{"TIME", OWN, 1, 16, 4, 4, 0}, {"FDBK", OWN, 0, 127, 50, 50, 0}, {"HPF", OWN, 0, 127, 10, 10, 0},
     {"LPF", OWN, 0, 127, 100, 100, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"WET", OWN, 0, 127, 0, 0, 0},
     {"TMPO", OWN, 0, 127, 60, 60, 0}, {"XFRZ", OWN, 1, 2, 2, 2, 0}, EMPTY, EMPTY, EMPTY, EMPTY},   /* XFRZ 2: the crossfader freezes the delay at B (1: it does not) */
};
#define P(page, i) (params[page][i])
/* ---- scenes (sadnoise, 12 Nov 2026), as on the Octatrack: the crossfader moves the sound between two of them -------
 * There are sixteen scenes; A holds one and B another (hold SCENE A or SCENE B and press a trig to choose which).
 * A scene is a set of knob positions: hold SCENE A (or B) and turn any knob, and that position is the scene's for
 * that knob -- the knob's own setting stays as it was. With the fader at A every knob in scene A's set stands at
 * the scene's position, at B at scene B's, and in between it glides; a knob only one of them holds glides between
 * that and its own setting. Hold a scene key and press NO: that scene forgets everything.
 * A knob is a page's (page x 12 + its place) or a chain slot's (132 + slot x 6 + knob). */
#define N_SCENES 16
#define SCENE_IDS (N_PAGES * 12 + 7 * 6)
static int8_t scene_val[N_SCENES][SCENE_IDS];   /* -1: the scene does not hold this knob */
static uint8_t scene_a, scene_b = 1, scene_dirty, scene_ready;
static void scenes_init(void) { if (scene_ready) return; for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) scene_val[s][i] = -1; scene_ready = 1; }
static int scene_mix(int id, int base)
{
    if (!scene_ready) return base;
    int a = scene_val[scene_a][id], b = scene_val[scene_b][id];
    if (a < 0 && b < 0) return base;
    int va = a < 0 ? base : a, vb = b < 0 ? base : b, x = xfade();
    return va + ((vb - va) * x + (vb >= va ? 63 : -63)) / 127;
}
static int pv_base(int page, int i) { Param *p = &params[page][i]; return p->user >= 0 ? p->user : p->value; }
static int pv(int page, int i) { return scene_mix(page * 12 + i, pv_base(page, i)); }

/* sources: off by default */
static int src_loop, src_stars, src_balloons;

/* ---- state -------------------------------------------------------------------------- */
static int muted[8];
static int night, hidden, func_held, func_latch, ready, page, page_until, intro;
static uint32_t ui_until; /* the page shows until then; after, only the sky */
#define UI_SHOW (60u * 10u)
static int scene_held;
static uint32_t held_tracks, stars_total;
static int16_t drift_now[N_PAGES][6], drift_target[N_PAGES][6];
static int algo; /* kept for the stars/player code paths: 2 = stars running */
enum { ALGO_NONE, ALGO_GRAINS, ALGO_STARS, ALGO_PLAYER, ALGO_BALLOON };
#define PG_PLAYER PG_LOOP
/* T5 THRU + T6-T8 NEIGHBOR (one DSP core: NEIGHBOR cannot sit on T1 or T5); sources on the other core.
 * T1 is the sample source, so stock's file preview (it borrows T1's voice) never touches the inputs. */
#define HEAD_T 4           /* T5: the inputs, HEAD in FX1 */
#define LAST_T 7           /* T8: the chain's end */
#define SAMPLE_T 0         /* T1: the loop or the balloons */
#define STARS_T 1          /* T2 */

static void set_weather(int w);
static void sync_to_card(void);
static int balloon_placed;
static void place_balloon(int step);
static void step_auto(void)
{
    if (!auto_weather || spell_left-- > 0) return;
    int pick = range(0, 19), w = 0;
    for (; w < N_WEATHER - 1; w++) { pick -= next_w[weather][w]; if (pick < 0) break; }
    set_weather(w == weather ? CLOUDY : w);
}

/* ---- the pattern: recognise it, then place one step ahead ------------------------------ */
/* Bank-file TRAC data (ot_bank.py): masks at +0, 0xaa at +0x40..0x47, LENGTH +0x50.
 * The RAM record's origin is found, not assumed: the eight 0xaa bytes must sit at
 * the same offset on all eight tracks, with sane lengths, or nothing is written. */
static int pat_off = -100; /* -100 unknown, -99 refused */
static void pattern_probe(void)
{
    if (pat_off != -100) return;
#ifndef PREVIEW
    if (!bank_base()) return; /* no project yet: ask again next tick */
#endif
    for (int off = -48; off <= 48; off++) {
        int good = 1;
        for (int t = 0; t < 8 && good; t++) {
            volatile uint8_t *r = pattern_track(t) + off;
            for (int i = 0x40; i < 0x48; i++) if (r[i] != 0xaa) { good = 0; break; }
            if (good && (r[0x50] == 0 || r[0x50] > 64)) good = 0;
        }
        if (good) { pat_off = off; return; }
    }
    pat_off = -99;
}
static volatile uint8_t *pat(int t) { return pattern_track(t) + pat_off; }
static int pat_len(int t) { int n = pat_off > -99 ? pat(t)[0x50] : 16; return n >= 1 && n <= 64 ? n : 16; }
static void pat_trig(int t, int step, int on)
{
    if (pat_off <= -99) return;
    volatile uint8_t *m = pat(t) + 7 - (step >> 3);
    if (on) *m = (uint8_t)(*m | (1u << (step & 7))); else *m = (uint8_t)(*m & ~(1u << (step & 7)));
}
static void pat_lock(int t, int step, int slot, int v)
{
    if (pat_off <= -99) return;
    pat(t)[PAT_LOCKS + 32u * (unsigned)step + (unsigned)slot] = (uint8_t)v;
}
static void pat_clear_step(int t, int step)
{
    pat_trig(t, step, 0);
    static const uint8_t slots[] = {0, 1, 2, 12, 13, 14, 31};
    for (unsigned i = 0; i < sizeof slots; i++) pat_lock(t, step, slots[i], NOLOCK);
}

/* ---- STARS: an astrolabe of samples ------------------------------------------------------- */
#define PLANETS 16
static uint16_t star_a[8];
static int16_t bird_x[8], bird_y[8], bird_vx[8], bird_vy[8];
static uint8_t planet_x[PLANETS], planet_y[PLANETS];
static uint16_t planet_mask = 0xffff;
static int star_rot, planet_rot, star_flash, star_hit = -1, planet_hit = -1;
#define RING_X 64
#define RING_Y 30
#define RING_R 22

static void new_constellation(void)
{
    for (int i = 0; i < 8; i++) star_a[i] = (uint16_t)range(0, 1023);
    for (int i = 0; i < 8; i++) { bird_x[i] = (int16_t)(range(-14, 14) * 16); bird_y[i] = (int16_t)(range(-14, 14) * 16); bird_vx[i] = (int16_t)range(-8, 8); bird_vy[i] = (int16_t)range(-8, 8); }
    for (int p = 0; p < PLANETS; p++) {
        int a = range(0, 1023), r = range(4, RING_R - 3);
        planet_x[p] = (uint8_t)(RING_X + cos10(a) * r / 127);
        planet_y[p] = (uint8_t)(RING_Y + sin10(a) * r / 127);
    }
}
static int planet_ring_angle(int p) { return (p * 64 + planet_rot) & 1023; }

/* which planet (0..15) sings at this step, or -1 */
static int stars_decide(void)
{
    int n = pv(PG_STARS, 1);
    if (pv(PG_STARS, 10) == 0) { /* ring: a star crossing the needle (top, angle 768) */
        int adv = 4 + pv(PG_STARS, 0) * 3;
        star_rot = (star_rot + adv) & 1023;
        planet_rot = (planet_rot - adv * 3 / 7) & 1023;
        for (int i = 0; i < n; i++) {
            int a = (star_a[i] + star_rot - 768) & 1023;
            if (a < adv) {
                star_hit = i;
                int best = -1, bd = 2048;
                for (int p = 0; p < PLANETS; p++) {
                    if (!(planet_mask & (1u << p))) continue;
                    int d = (planet_ring_angle(p) - 768) & 1023;
                    if (d > 512) d = 1024 - d;
                    if (d < bd) { bd = d; best = p; }
                }
                return best;
            }
        }
        return -1;
    }
    for (int i = 0; i < n; i++)
        for (int p = 0; p < PLANETS; p++) {
            if (!(planet_mask & (1u << p))) continue;
            int dx = RING_X + (bird_x[i] >> 4) - planet_x[p], dy = RING_Y + (bird_y[i] >> 4) - planet_y[p];
            if (dx * dx + dy * dy < 12) { star_hit = i; return p; }
        }
    return -1;
}
static void birds_tick(void)
{
    int n = pv(PG_STARS, 1), spd = 2 + pv(PG_STARS, 0) / 16;
    for (int i = 0; i < n; i++) {
        if ((rnd() & 31) == 0) { bird_vx[i] = (int16_t)clamp(bird_vx[i] + range(-4, 4), -spd, spd); bird_vy[i] = (int16_t)clamp(bird_vy[i] + range(-4, 4), -spd, spd); }
        bird_x[i] = (int16_t)(bird_x[i] + bird_vx[i]);
        bird_y[i] = (int16_t)(bird_y[i] + bird_vy[i]);
        int x = bird_x[i] >> 4, y = bird_y[i] >> 4;
        if (x * x + y * y > (RING_R - 2) * (RING_R - 2)) { bird_vx[i] = (int16_t)(-bird_vx[i] - (x > 0) + (x < 0)); bird_vy[i] = (int16_t)(-bird_vy[i] - (y > 0) + (y < 0)); bird_x[i] = (int16_t)(bird_x[i] * 15 / 16); bird_y[i] = (int16_t)(bird_y[i] * 15 / 16); }
    }
}

/* ---- the sequencer side: one step ahead ---------------------------------------------------------- */
static uint32_t stars_total;
static int last_step = -1, star_placed = -1, player_placed = -1, pad_pending = -1;

static void place_star(int step)
{
    int p = stars_decide();
    planet_hit = -1;
    if (p < 0 || muted[T_STARS] || (int)(rnd() % 128u) > pv(PG_STARS, 5)) return;
    int slot = pv(PG_STARS, 9) * PLANETS + p;
    int prnd = pv(PG_STARS, 2), arnd = pv(PG_STARS, 3), drnd = pv(PG_STARS, 4);
    int ptch = clamp(pv(PG_STARS, 6) + (prnd ? range(-prnd / 4, prnd / 4) : 0), 0, 127);
    int atk = clamp(pv(PG_STARS, 7) + (arnd ? range(0, arnd / 2) : 0), 0, 127);
    int dec = clamp(pv(PG_STARS, 8) + (drnd ? range(-drnd / 2, drnd / 2) : 0), 0, 127);
    pat_lock(T_STARS, step, 31, slot);
    pat_lock(T_STARS, step, 0, ptch);
    pat_lock(T_STARS, step, 12, atk);
    pat_lock(T_STARS, step, 13, dec);      /* HOLD: how long it rings */
    pat_lock(T_STARS, step, 14, dec);      /* REL */
    pat_trig(T_STARS, step, 1);
#if defined(PREVIEW) && defined(AERIAOS)
    { void aos_star_at(int step, int len, int slot, int ptch, int atk, int dec); aos_star_at(step, pat_len(T_STARS), slot, ptch, atk, dec); }
#endif
    star_placed = step;
    planet_hit = p;
    stars_total++;
}
__attribute__((unused)) static void place_player(int step)
{
    if (pad_pending < 0) return;
    pat_lock(T_PLAYER, step, 1, pad_pending * 127 / 15);
    pat_lock(T_PLAYER, step, 31, pv(PG_PLAYER, 9));
    pat_trig(T_PLAYER, step, 1);
    player_placed = step;
    pad_pending = -1;
}
static void sequencer_tick(void)
{
    pattern_probe();
    if (!playing() || !rig_loaded()) { last_step = -1; return; }
    int len = pat_len(T_STARS);
    int s = (int)(master_step() % (unsigned)len);
    if (s == last_step) return;
    last_step = s;
    if (star_placed >= 0 && star_placed != (s + 1) % len) { pat_clear_step(T_STARS, star_placed); star_placed = -1; }
    if (player_placed >= 0 && player_placed != (s + 1) % len) { pat_clear_step(T_PLAYER, player_placed); player_placed = -1; }
    if (planet_hit >= 0) star_flash = 10;
    int next = (s + 1) % len;
    if (algo == ALGO_STARS) place_star(next); else planet_hit = -1;
    if (balloon_placed >= 0 && balloon_placed != next) { pat_clear_step(SAMPLE_T, balloon_placed); balloon_placed = -1; }
    place_balloon(next);
}

/* ---- the shelf: which effect sits in each chain slot -----------------------------------------
 * The chain: T3 THRU (inputs; FX1 = HEAD, the mixer and main feedback), T4-T8 NEIGHBOR.
 * Eleven slots the musician fills from the shelf: T3 FX2, then FX1 and FX2 of T4..T8.
 * The effect lists, names and knob names are read from the firmware's own choosers
 * (the base repoints the setters' lists: research-fx.md section 2), so every installed
 * effect appears by its own name with its own knobs. */
#define N_SLOTS 7
static int slot_track(int s) { return s == 0 ? HEAD_T : HEAD_T + 1 + (s - 1) / 2; }
static int slot_fx(int s) { return s == 0 ? 1 : (s - 1) & 1; } /* 0 FX1, 1 FX2 */

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: the effects are AERIA OS's own (aeria-os/src/fx_table.inc, from the aeria-fx manifests); row 0 is the
 * empty slot. main.c watches preview_live_fx and loads each slot's effect into the DSP engine. */
#include "fx_table.inc"
uint8_t preview_live_fx[16];
uint8_t preview_part_fx[8][12];
uint8_t preview_slot_p2[7][6];   /* each chain slot's FUNC-layer knobs (the effect's page 2): AERIA OS sends them to the DSP */
static int list_count(int fx2) { (void)fx2; return FX_COUNT + 1; }
static int list_id(int fx2, int row) { (void)fx2; return row; }
static void list_name(int fx2, int row, char *out) { (void)fx2; const char *n = row > 0 && row <= FX_COUNT ? fx_info[row - 1].name : "NONE"; int i = 0; while (n[i] && i < 12) { out[i] = n[i]; i++; } out[i] = 0; }
static void list_knob(int fx2, int row, int k, char *out) { (void)fx2; const char *n = row > 0 && row <= FX_COUNT ? fx_info[row - 1].knob[k] : ""; int i = 0; while (n[i] && i < 4) { out[i] = n[i]; i++; } out[i] = 0; }
static int list_count_of(int fx2, int row, int k) { (void)fx2; int c = row > 0 && row <= FX_COUNT ? fx_info[row - 1].count[k] : 128; return c >= 2 && c <= 128 ? c : 128; }
static int live_fx(int t, int fx2) { return preview_live_fx[fx2 * 8 + t]; }
static int part_knob(int t, int fx2, int k) { return preview_part_fx[t][fx2 * 6 + k]; }
/* ---- which DSP core runs a slot's effect (12 Nov 2026) ----------------------------------------------------------
 * A core has about 1,760 instructions for each sample (measured: the old rig's five effects, 1,127 of them, took
 * 64 % of core 0). Core 0 also runs the engine and takes the samples' stream (about 610); core 1 only the effects.
 * An effect bound to a core's own memory (GRAINS, RESO: core 0; VERB, ECHO: core 1) goes there; the others go
 * where there is more room. When it fits nowhere the pick is refused: NO ROOM, DSP FULL. */
#ifndef CORE_ROOM
#define CORE_ROOM 1700
#endif
#define CORE0_BASE 710     /* the engine's own share of core 0 -- with the time it spends sorting the host's words counted (the meter
                            * left that out until 12 Nov 2026: 1,081 of effects then read 93 % in the worst frame) */
#define CORE1_BASE 250     /* core 1 is the weaker in practice: VERB + ECHO (1,404) run clean, VERB + ECHO + SMEAR (1,673) drop
                            * blocks (measured on the unit with USB recording on, 12 Nov 2026) -- so its room ends near 1,450 */
static int8_t slot_core[7] = {-1, -1, -1, -1, -1, -1, -1};
static uint16_t slot_cost[7];
static uint8_t slot_any[7];   /* the slot's effect could run on either core */
static int place_slot_of(int t, int fx2) { for (int s = 0; s < 7; s++) if ((s == 0 ? 4 : 5 + (s - 1) / 2) == t && (s == 0 ? 1 : (s - 1) & 1) == fx2) return s; return -1; }
/* the core for `row` in slot s: 0 or 1, or -1 when there is no room. An effect that may run on either core goes to
 * the emptier one; one bound to a core that is full has the movable effects there sent across to make room (main.c
 * sees their core change and moves them). commit: remember the outcome; otherwise only say whether it would fit. */
static int place_effect(int s, int row, int commit)
{
    if (s < 0 || s >= 7) return 0;
    if (row <= 0 || row > FX_COUNT) { if (commit) { slot_core[s] = -1; slot_cost[s] = 0; slot_any[s] = 0; } return 0; }
    int8_t core[7]; int load[2] = {CORE0_BASE, CORE1_BASE}, cost = fx_cost[row - 1], want = fx_core[row - 1];
    for (int i = 0; i < 7; i++) { core[i] = i == s ? -1 : slot_core[i]; if (core[i] >= 0) load[core[i]] += slot_cost[i]; }
    int c = want < 2 ? want : (load[1] <= load[0] ? 1 : 0);
    if (load[c] + cost > CORE_ROOM && want == 2 && load[c ^ 1] + cost <= CORE_ROOM) c ^= 1;
    for (int i = 0; i < 7 && load[c] + cost > CORE_ROOM; i++)              /* make room: movable effects go across */
        if (core[i] == c && slot_any[i] && load[c ^ 1] + slot_cost[i] <= CORE_ROOM) { core[i] = (int8_t)(c ^ 1); load[c] -= slot_cost[i]; load[c ^ 1] += slot_cost[i]; }
    int fits = load[c] + cost <= CORE_ROOM;
    if (commit) {
        if (fits) for (int i = 0; i < 7; i++) if (i != s) slot_core[i] = core[i];
        slot_core[s] = (int8_t)c; slot_cost[s] = (uint16_t)cost; slot_any[s] = (uint8_t)(want == 2);
    }
    return fits ? c : -1;
}
int aos_place(int s, int row) { place_effect(s, row, 1); return s >= 0 && s < 7 ? slot_core[s] : 0; }   /* main.c, when a slot's effect changes */
int aos_slot_core(int s) { return s >= 0 && s < 7 ? slot_core[s] : -1; }                                 /* ... and each pass: has it been moved? */
int aos_core_load(int core) { int l = core ? CORE1_BASE : CORE0_BASE; for (int i = 0; i < 7; i++) if (slot_core[i] == core) l += slot_cost[i]; return l * 100 / 1760; }
static int pick_effect(int t, int fx2, int row)
{
    if (place_effect(place_slot_of(t, fx2), row, 0) < 0) return 0;   /* no core has the room */
    /* an effect with a delay buffer has one buffer: a second copy would share it, so it is refused */
    if (row > 0 && row <= FX_COUNT && fx_info[row - 1].has_y)
        for (int i = 0; i < 16; i++) if (i != fx2 * 8 + t && preview_live_fx[i] == row) return 0;
    preview_live_fx[fx2 * 8 + t] = (uint8_t)row;
    for (int k = 0; k < 6; k++) preview_part_fx[t][fx2 * 6 + k] = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def[k] : 64;
    return 1;
}
#elif defined(PREVIEW)
static uint8_t preview_live_fx[16];
static const char *const preview_names[] = {"NONE", "FILTER", "CHORUS", "COMB FILTER", "PHASER", "LO-FI", "DELAY", "DARK REV", "PLATE REV", "AERIA PITCH", "AERIA SMEAR"};
static const char *const preview_knobs[11][6] = {{"", "", "", "", "", ""}, {"BASE", "WDTH", "Q", "DPTH", "ATK", "DEC"}, {"DEL", "DEP", "SPD", "FB", "WID", "MIX"},
    {"PTCH", "TUNE", "LP", "FB", "", "MIX"}, {"CNTR", "DEP", "SPD", "FB", "WID", "MIX"}, {"DIST", "NOIS", "AMF", "SRR", "BRR", "AMD"},
    {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {"TIME", "SHVG", "SHVF", "HP", "LP", "MIX"}, {"TIME", "DAMP", "GATE", "HP", "LP", "MIX"},
    {"SHFT", "FINE", "GRN", "FDBK", "TONE", "MIX"}, {"SIZE", "HOLD", "DIFF", "TONE", "MOD", "MIX"}};
static uint8_t preview_part_fx[8][12];
static int list_count(int fx2) { return fx2 ? 11 : 6; }
static int list_id(int fx2, int row) { return fx2 ? row : row; }
static void list_name(int fx2, int row, char *out) { (void)fx2; const char *n = preview_names[row]; int i = 0; while (n[i] && i < 12) { out[i] = n[i]; i++; } out[i] = 0; }
static void list_knob(int fx2, int row, int k, char *out) { (void)fx2; const char *n = preview_knobs[row][k]; int i = 0; while (n[i] && i < 4) { out[i] = n[i]; i++; } out[i] = 0; }
static int list_count_of(int fx2, int row, int k) { (void)fx2; (void)row; (void)k; return 128; }
static int live_fx(int t, int fx2) { return preview_live_fx[fx2 * 8 + t]; }
static int part_knob(int t, int fx2, int k) { return preview_part_fx[t][fx2 * 6 + k]; }
static int pick_effect(int t, int fx2, int row) { preview_live_fx[fx2 * 8 + t] = (uint8_t)list_id(fx2, row); for (int k = 0; k < 6; k++) preview_part_fx[t][fx2 * 6 + k] = 64; return 1; }
#else
#define LIST(s) ((const volatile uint32_t *)*(volatile uint32_t *)((s) ? 0x40052496u : 0x40052706u))
static uint32_t list_desc(int fx2, int row) { return LIST(fx2)[row]; }
static int list_count(int fx2)
{
    const volatile uint32_t *l = LIST(fx2);
    int n = 0;
    while (n < 128 && l[n] >= 0x40000000u && l[n] < 0x48000000u) n++;
    return n;
}
static int list_id(int fx2, int row) { return (int)(*(const volatile uint32_t *)list_desc(fx2, row) & 0xffu); }
static void list_name(int fx2, int row, char *out)
{
    const volatile char *n = (const volatile char *)(list_desc(fx2, row) + 9);
    int i = 0;
    while (i < 12 && n[i] >= ' ' && n[i] < 127) { out[i] = n[i] >= 'a' && n[i] <= 'z' ? (char)(n[i] - 32) : n[i]; i++; }
    out[i] = 0;
}
/* a chooser row already points at the page descriptor P (stock's descriptor + 0x38, read on the unit):
 * id +0, names +0x16 + 6k, counts +0x9a + 4k, enable nibbles +0x18e */
static int list_knob_enabled(int fx2, int row, int k)
{
    uint32_t p = list_desc(fx2, row);
    uint32_t en = *(const volatile uint32_t *)(p + 0x18eu);
    return (int)((en >> (4 * k)) & 1u);
}
static void list_knob(int fx2, int row, int k, char *out)
{
    out[0] = 0;
    if (!list_knob_enabled(fx2, row, k)) return;
    const volatile char *n = (const volatile char *)(list_desc(fx2, row) + 0x16u + 6u * (unsigned)k);
    int i = 0;
    while (i < 4 && n[i] > ' ' && n[i] < 127) { out[i] = n[i] >= 'a' && n[i] <= 'z' ? (char)(n[i] - 32) : n[i]; i++; }
    out[i] = 0;
}
static int list_count_of(int fx2, int row, int k)
{
    uint32_t c = *(const volatile uint32_t *)(list_desc(fx2, row) + 0x9au + 4u * (unsigned)k);
    return c >= 2 && c <= 128 ? (int)c : 128;
}
static int live_fx(int t, int fx2) { return *(volatile uint8_t *)(0x80000ec4u + 8u * (unsigned)fx2 + (unsigned)t); }
/* the working Part's page-1 knob bytes: +0x11a + 24t, FX1 at +12, FX2 at +18 (PARAM_PAGES.md 5g) */
static int part_knob(int t, int fx2, int k)
{
    uint32_t base = bank_base();
    if (!base) return 64;
    volatile uint8_t *part = (volatile uint8_t *)(base + 0x8ed80u + (unsigned)(*(volatile uint8_t *)0x100b14cfu & 3u) * 6322u);
    return part[0x11a + 24 * t + (fx2 ? 18 : 12) + k] & 0x7f;
}
/* The base-guarded stock setter (research-fx.md Recipe A): the DSP loader admits,
 * uploads and charges the effect, or refuses it (DSP FULL) and nothing changes. */
static int pick_effect(int t, int fx2, int row)
{
    volatile uint8_t *cur = (volatile uint8_t *)0x80000000u;
    volatile uint32_t *cursor = (volatile uint32_t *)(fx2 ? 0x460d5ca8u : 0x460d5c94u);
    uint8_t was_t = *cur;
    uint32_t was_c = *cursor;
    *cur = (uint8_t)t;
    *cursor = (uint32_t)row;
    ((void (*)(void))(fx2 ? 0x40052474u : 0x400526e4u))();
    *cur = was_t;
    *cursor = was_c;
    return 1;
}
#endif

static int row_of_id(int fx2, int id)
{
    int n = list_count(fx2);
    for (int r = 0; r < n; r++) if (list_id(fx2, r) == id) return r;
    return -1;
}
__attribute__((unused)) static int row_of_name(int fx2, const char *name)
{
    int n = list_count(fx2);
    char buf[14];
    for (int r = 0; r < n; r++) {
        list_name(fx2, r, buf);
        int i = 0;
        while (name[i] && buf[i] == name[i]) i++;
        if (!name[i] && !buf[i]) return r;
    }
    return -1;
}

/* what AERIA knows about each slot */
typedef struct {
    int16_t id, row;          /* the effect now in it */
    char name[14];
    char knob[6][5];
    uint8_t count[6];
    int16_t value[6], user[6]; /* user -1 follows the weather */
    uint8_t role[6];
    int16_t pending;          /* row asked for, -1 none */
    uint16_t wait;
} Slot;
static Slot slots[N_SLOTS];
static uint16_t slot_bypass;
static int sel_slot = 1, shelf_open, shelf_row;
/* the shelf offers only AERIA's own effects (and "-", empty): never a stock one. TAP and HEAD are AERIA's
 * plumbing with fixed places, so they are not on it either. shelf_row indexes this filtered list. */
static int shelf_rows[48], shelf_n;
static void shelf_build(int fx2)
{
    int n = list_count(fx2);
    char nm[14];
    shelf_n = 0;
    shelf_rows[shelf_n++] = 0;
    for (int r = 1; r < n && shelf_n < 48; r++) {
        list_name(fx2, r, nm);
        if (!(nm[0] == 'A' && nm[1] == 'E' && nm[2] == 'R' && nm[3] == 'I' && nm[4] == 'A' && nm[5] == ' ')) continue;
        if ((nm[6] == 'T' && nm[7] == 'A' && nm[8] == 'P') || (nm[6] == 'H' && nm[7] == 'E' && nm[8] == 'A' && nm[9] == 'D')) continue;
        shelf_rows[shelf_n++] = r;
    }
}
static int shelf_index_of(int row) { for (int i = 0; i < shelf_n; i++) if (shelf_rows[i] == row) return i; return 0; }

/* ---- the weather speaks to knobs by what they are ------------------------------------------
 * role: what a knob does; each weather sets how much of each role it wants, and a knob's
 * target is lo + (hi - lo) * amount / 127. Unknown knobs stay where the musician left them. */
enum { R_NONE, R_SPACE, R_TIME, R_ECHO, R_SEND, R_OPEN, R_DARK, R_GRIT, R_SHIM, R_RESO, R_RATE, R_PITCH, R_SMEAR, R_FREEZE, N_ROLES };
typedef struct { char knob[5]; uint8_t role, lo, hi; } Rule;
static const Rule rules[] = {
    {"MIX", R_SPACE, 0, 120}, {"WET", R_SPACE, 0, 120}, {"TIME", R_TIME, 30, 127}, {"DECY", R_TIME, 30, 127}, {"DCAY", R_TIME, 30, 127},
    {"SIZE", R_TIME, 30, 127}, {"FB", R_ECHO, 0, 115}, {"FDBK", R_ECHO, 0, 115}, {"SEND", R_SEND, 0, 110}, {"VOL", R_SEND, 20, 110},
    {"WDTH", R_OPEN, 40, 127}, {"LP", R_OPEN, 40, 127}, {"TONE", R_OPEN, 30, 110}, {"DAMP", R_DARK, 0, 110}, {"HP", R_DARK, 0, 50},
    {"DIST", R_GRIT, 0, 90}, {"DRV", R_GRIT, 0, 100}, {"DRIV", R_GRIT, 0, 100}, {"NOIS", R_GRIT, 0, 50}, {"BITS", R_GRIT, 0, 80},
    {"DEP", R_SHIM, 20, 120}, {"DPTH", R_SHIM, 20, 120}, {"SPD", R_RATE, 4, 90}, {"RATE", R_RATE, 4, 90},
    {"RES", R_RESO, 0, 35}, {"Q", R_RESO, 0, 25},   /* low resonance (sadnoise, 10 Nov 2026) */ {"HOLD", R_SMEAR, 0, 127}, {"SMR", R_SMEAR, 0, 127}, {"DIFF", R_SMEAR, 20, 127},
    {"FRZ", R_FREEZE, 0, 127},   /* the FFT SMEAR's freeze: the snow's (12 Nov 2026) */
};
#define N_RULES ((int)(sizeof rules / sizeof rules[0]))
/* per weather, how much of each role (0..127): SPACE TIME ECHO SEND OPEN DARK GRIT SHIM RESO RATE PITCH SMEAR FREEZE */
static const uint8_t weather_roles[N_WEATHER][N_ROLES] = {
    /* CLEAR  */ {0, 25, 40, 25, 30, 120, 10, 0, 25, 20, 30, 0, 0, 0},
    /* CLOUDY */ {0, 45, 60, 45, 45, 95, 35, 10, 45, 30, 25, 0, 20, 0},
    /* RAIN   */ {0, 60, 75, 60, 60, 70, 60, 25, 40, 50, 40, 0, 35, 0},
    /* STORM  */ {0, 75, 90, 95, 85, 75, 50, 85, 30, 80, 90, 0, 40, 0},
    /* SNOW   */ {0, 100, 127, 60, 50, 50, 85, 15, 40, 20, 5, 0, 110, 118},
    /* HEAT   */ {0, 30, 45, 35, 30, 95, 20, 50, 120, 30, 80, 0, 105, 0},
    /* FOG    */ {0, 90, 100, 60, 50, 40, 90, 10, 60, 30, 10, 0, 90, 0}};
static int name_has(const char *name, const char *part)
{
    for (int i = 0; name[i]; i++) { int k = 0; while (part[k] && name[i + k] == part[k]) k++; if (!part[k]) return 1; }
    return 0;
}
static int rule_of(const char *knob)
{
    for (int r = 0; r < N_RULES; r++) {
        int i = 0;
        while (knob[i] && rules[r].knob[i] == knob[i]) i++;
        if (!knob[i] && !rules[r].knob[i]) return r;
    }
    return -1;
}
static int slot_target(int s, int k, int w)
{
    Slot *sl = &slots[s];
    int r = rule_of(sl->knob[k]);
    if (r < 0) return -1;
    int amt = weather_roles[w][rules[r].role];
    int v = rules[r].lo + (rules[r].hi - rules[r].lo) * amt / 127;
    /* DIST's RATE is its sample rate (127 = untouched), not a modulation speed: it follows the weather's grit,
     * inverted, and stays near clean -- as a speed it decimated the input in every weather (10 Nov 2026) */
    if (rules[r].role == R_RATE && name_has(sl->name, "DIST")) v = 127 - weather_roles[w][R_GRIT] * 30 / 127;
    return clamp(v, 0, sl->count[k] - 1);
}

/* read a slot's effect from the firmware: its name, knobs, and the Part's values */
static void slot_refresh(int s)
{
    Slot *sl = &slots[s];
    int t = slot_track(s), fx2 = slot_fx(s);
    sl->id = (int16_t)live_fx(t, fx2);
    sl->row = (int16_t)row_of_id(fx2, sl->id);
    if (sl->row < 0) { sl->name[0] = 0; for (int k = 0; k < 6; k++) sl->knob[k][0] = 0; return; }
    list_name(fx2, sl->row, sl->name);
    for (int k = 0; k < 6; k++) {
        list_knob(fx2, sl->row, k, sl->knob[k]);
        sl->count[k] = (uint8_t)(list_count_of(fx2, sl->row, k) > 127 ? 128 : list_count_of(fx2, sl->row, k));
        sl->value[k] = (int16_t)part_knob(t, fx2, k);
        sl->user[k] = -1;
    }
    for (int k = 0; k < 6; k++) sent[t][(fx2 ? 24 : 18) + k] = 0xff;
}
static void slots_init(void) { for (int s = 0; s < N_SLOTS; s++) { slots[s].pending = -1; slot_refresh(s); } }
static int knob_is_mix(const char *n) { return n[0] == 'M' && n[1] == 'I' && n[2] == 'X' && !n[3]; }
static int slot_base(int s, int k) { return slots[s].user[k] >= 0 ? slots[s].user[k] : slots[s].value[k]; }
static int slot_value(int s, int k) { return scene_mix(N_PAGES * 12 + s * 6 + k, slot_base(s, k)); }   /* with the scenes */
/* a knob's value as the musician reads it: pitch in semitones either side of 0, fine tune in cents, a root as its note */
static void signed_text(char *n, int v) { n[0] = v > 0 ? '+' : v < 0 ? '-' : ' '; num(n + 1, v < 0 ? -v : v); }
static void slot_text(int s, int k, int v, char *n)   /* k 0..5 the knobs, 6..11 the FUNC layer's */
{
    const char *t = 0;
#if defined(AERIAOS)
    int row = slots[s].row;
    if (row > 0 && row <= FX_COUNT) {
        const FxInfo *fi = &fx_info[row - 1];
        if (k < 6) { if (fi->text[k] && v >= 0 && v < fi->count[k]) t = fi->text[k][v]; }
        else if (fi->text2[k - 6] && v >= 0 && v < fi->count2[k - 6]) t = fi->text2[k - 6][v];
    }
#endif
    (void)s; (void)k;
    if (!t) { num(n, v); return; }
    int i = 0; while (t[i] && i < 10) { n[i] = t[i]; i++; } n[i] = 0;
}

/* one pick at a time; the loader may take a few ticks to upload, or refuse (DSP FULL) */
static int picking = -1;
static void slot_pick(int s, int row)
{
    if (picking >= 0) return;
    Slot *sl = &slots[s];
    sl->pending = (int16_t)row;
    sl->wait = 0;
    picking = s;
    int ok = pick_effect(slot_track(s), slot_fx(s), row);
#if defined(AERIAOS)
    if (ok) for (int k = 0; k < 6; k++) preview_slot_p2[s][k] = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def2[k] : 0;
#endif
    (void)ok;
}
static void picks_tick(void)
{
    if (picking < 0) return;
    Slot *sl = &slots[picking];
    int want = list_id(slot_fx(picking), sl->pending);
    if (live_fx(slot_track(picking), slot_fx(picking)) == want) {
        slot_refresh(picking);
        say(sl->name, "IN");
        sl->pending = -1; picking = -1;
    } else if (++sl->wait > 300) { /* five seconds: the loader said no */
        sl->pending = -1; picking = -1;
        say("NO ROOM", "DSP FULL");
    }
}

/* the weather and the musician, written into each slot */
static void slots_sound(void)
{
    int xf = -1;   /* no crossfade between weathers any more (12 Nov 2026) */
    for (int s = 0; s < N_SLOTS; s++) {
        Slot *sl = &slots[s];
        if (sl->row <= 0 || sl->pending >= 0) continue;
        int t = slot_track(s), fx2 = slot_fx(s);
        for (int k = 0; k < 6; k++) {
            if (!sl->knob[k][0]) continue;
            if (sl->user[k] < 0 && !knob_is_mix(sl->knob[k])) {      /* the weather moves a knob nobody holds -- except MIX */
                /* the knob's place under every weather that is on, averaged (none on: CLEAR's) */
                int want = -1, sum = 0, cnt = 0;
                for (int w = 1; w < N_WEATHER; w++) if (WON(w)) { int t1 = slot_target(s, k, w); if (t1 >= 0) { sum += t1; cnt++; } }
                want = cnt ? (sum + cnt / 2) / cnt : slot_target(s, k, CLEAR);
                if (want >= 0 && xf > 0) { int b = slot_target(s, k, xf_right); if (b >= 0) want += (b - want) * xf / 127; }
                if (want >= 0 && thunder && rule_of(sl->knob[k]) >= 0 && rules[rule_of(sl->knob[k])].role == R_OPEN) want = want * (256 - thunder) / 256;
                if (want >= 0) sl->value[k] = (int16_t)(sl->value[k] < want ? sl->value[k] + 1 : sl->value[k] > want ? sl->value[k] - 1 : want);
            }
            int v = slot_value(s, k);
            if ((slot_bypass >> s) & 1) { int r = rule_of(sl->knob[k]); if (r >= 0 && (rules[r].role == R_SPACE || rules[r].role == R_SEND)) v = 0; }
            lane_put(t, (fx2 ? 24 : 18) + k, clamp(v, 0, sl->count[k] - 1));
        }
    }
}

/* ---- first start: put HEAD, TAP and the default chain in place, by name, one pick at a time ----
 * Names match the chooser rows (desc+9) by prefix, case aside. A name that is not installed is
 * skipped; a slot already holding it is left alone. */
/* the chain's set-up ends by rebuilding every slot, which lets go of every knob: the saved holds (and the dry start)
 * go back on after it -- they used to be lost at every start (found 12 Nov 2026) */
#if defined(AERIAOS)
static void aeria_state_reapply_slots(void);
static void slots_reheld(void) { aeria_state_reapply_slots(); }
#else
static void slots_reheld(void) {}
#endif
typedef struct { uint8_t track, fx2; char name[13]; } Want_fx;
static const Want_fx setup_list[] = {
    {0, 0, "AERIA TAP"}, {1, 0, "AERIA TAP"}, {4, 0, "AERIA HEAD"},
    /* pitch > disto > grains > ring > smear > filter: three long-buffer effects fill this core's FX2 buffers */
    {4, 1, "AERIA PITCH"}, {5, 0, "AERIA DIST"}, {5, 1, "AERIA GRAIN"},
    {6, 0, "AERIA RING"}, {6, 1, "AERIA SMEAR"}, {7, 0, "NONE"}, {7, 1, "NONE"},   /* no stock effect anywhere */
};
#define N_SETUP ((int)(sizeof setup_list / sizeof setup_list[0]))
static int setup_at = -1, setup_wait, setup_want;
static int row_of_prefix(int fx2, const char *want)
{
    int n = list_count(fx2);
    char buf[14];
    if (want[0] == 'N' && want[1] == 'O' && want[2] == 'N' && want[3] == 'E' && !want[4]) return 0;
    for (int r = 1; r < n; r++) {
        list_name(fx2, r, buf);
        int i = 0;
        while (want[i] && buf[i] == want[i]) i++;
        if (!want[i]) return r;
    }
    return -1;
}
static void setup_start(void) { setup_at = 0; setup_wait = 0; setup_want = -1; }
static int setup_busy(void) { return setup_at >= 0 && setup_at < N_SETUP; }
static void setup_tick(void)
{
    if (!setup_busy() || picking >= 0) return;
    const Want_fx *w = &setup_list[setup_at];
    if (setup_want >= 0) { /* waiting for the last pick to land */
        if (live_fx(w->track, w->fx2) == setup_want || ++setup_wait > 300) { setup_at++; setup_want = -1; setup_wait = 0; }
        if (!setup_busy()) { slots_init(); slots_reheld(); sync_to_card(); say("CHAIN READY", 0); }
        return;
    }
    int row = row_of_prefix(w->fx2, w->name);
    if (row < 0 || live_fx(w->track, w->fx2) == list_id(w->fx2, row)) { setup_at++; if (!setup_busy()) { slots_init(); slots_reheld(); sync_to_card(); say("CHAIN READY", 0); } return; }
    setup_want = list_id(w->fx2, row);
    pick_effect(w->track, w->fx2, row);
}
/* generated by ~/octa/tools/gen_scales.py: Balloon Shop's Western scales and Cloudfin's microtonal ones (Scales.h).
 * Degrees in tenths of a cent above the root, ascending, within one period (an octave; Bohlen-Pierce: 3/1). */
typedef struct { const char *name; uint16_t period, n; const uint16_t *dc; const char *const *lab; } Scale;
static const uint16_t sc_dc_0[7] = {0, 2000, 4000, 5000, 7000, 9000, 11000};
static const char *const sc_lab_0[7] = {"1", "2", "3", "4", "5", "6", "7"};
static const uint16_t sc_dc_1[7] = {0, 2000, 3000, 5000, 7000, 8000, 10000};
static const char *const sc_lab_1[7] = {"1", "2", "B3", "4", "5", "B6", "B7"};
static const uint16_t sc_dc_2[7] = {0, 2000, 3000, 5000, 7000, 8000, 11000};
static const char *const sc_lab_2[7] = {"1", "2", "B3", "4", "5", "B6", "7"};
static const uint16_t sc_dc_3[7] = {0, 2000, 3000, 5000, 7000, 9000, 11000};
static const char *const sc_lab_3[7] = {"1", "2", "B3", "4", "5", "6", "7"};
static const uint16_t sc_dc_4[7] = {0, 2000, 3000, 5000, 7000, 9000, 10000};
static const char *const sc_lab_4[7] = {"1", "2", "B3", "4", "5", "6", "B7"};
static const uint16_t sc_dc_5[7] = {0, 1000, 3000, 5000, 7000, 8000, 10000};
static const char *const sc_lab_5[7] = {"1", "B2", "B3", "4", "5", "B6", "B7"};
static const uint16_t sc_dc_6[7] = {0, 2000, 4000, 6000, 7000, 9000, 11000};
static const char *const sc_lab_6[7] = {"1", "2", "3", "#4", "5", "6", "7"};
static const uint16_t sc_dc_7[7] = {0, 2000, 4000, 5000, 7000, 9000, 10000};
static const char *const sc_lab_7[7] = {"1", "2", "3", "4", "5", "6", "B7"};
static const uint16_t sc_dc_8[7] = {0, 1000, 3000, 5000, 6000, 8000, 10000};
static const char *const sc_lab_8[7] = {"1", "B2", "B3", "4", "#4", "B6", "B7"};
static const uint16_t sc_dc_9[5] = {0, 2000, 4000, 7000, 9000};
static const char *const sc_lab_9[5] = {"1", "2", "3", "5", "6"};
static const uint16_t sc_dc_10[5] = {0, 3000, 5000, 7000, 10000};
static const char *const sc_lab_10[5] = {"1", "B3", "4", "5", "B7"};
static const uint16_t sc_dc_11[6] = {0, 3000, 5000, 6000, 7000, 10000};
static const char *const sc_lab_11[6] = {"1", "B3", "4", "#4", "5", "B7"};
static const uint16_t sc_dc_12[7] = {0, 1000, 4000, 5000, 7000, 8000, 10000};
static const char *const sc_lab_12[7] = {"1", "B2", "3", "4", "5", "B6", "B7"};
static const uint16_t sc_dc_13[6] = {0, 2000, 4000, 6000, 8000, 10000};
static const char *const sc_lab_13[6] = {"1", "2", "3", "#4", "B6", "B7"};
static const uint16_t sc_dc_14[8] = {0, 2000, 3000, 5000, 6000, 8000, 9000, 11000};
static const char *const sc_lab_14[8] = {"1", "2", "B3", "4", "#4", "B6", "6", "7"};
static const uint16_t sc_dc_15[12] = {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000};
static const char *const sc_lab_15[12] = {"1", "B2", "2", "B3", "3", "4", "#4", "5", "B6", "6", "B7", "7"};
static const uint16_t sc_dc_16[12] = {0, 1766, 2039, 2396, 4435, 4708, 6747, 7020, 7377, 9416, 9688, 11727};
static const char *const sc_lab_16[12] = {"1/1", "567/512", "9/8", "147/128", "1323/1024", "21/16", "189/128", "3/2", "49/32", "441/256", "7/4", "63/32"};
static const uint16_t sc_dc_17[7] = {0, 1117, 3863, 4980, 7020, 8137, 10883};
static const char *const sc_lab_17[7] = {"1/1", "16/15", "5/4", "4/3", "3/2", "8/5", "15/8"};
static const uint16_t sc_dc_18[7] = {0, 2039, 3863, 5902, 7020, 9059, 10883};
static const char *const sc_lab_18[7] = {"1/1", "9/8", "5/4", "45/32", "3/2", "27/16", "15/8"};
static const uint16_t sc_dc_19[7] = {0, 1117, 3156, 4980, 7020, 8137, 10176};
static const char *const sc_lab_19[7] = {"1/1", "16/15", "6/5", "4/3", "3/2", "8/5", "9/5"};
static const uint16_t sc_dc_20[7] = {0, 1117, 3156, 5902, 7020, 8137, 10883};
static const char *const sc_lab_20[7] = {"1/1", "16/15", "6/5", "45/32", "3/2", "8/5", "15/8"};
static const uint16_t sc_dc_21[6] = {0, 1117, 3863, 5902, 9059, 10883};
static const char *const sc_lab_21[6] = {"1/1", "16/15", "5/4", "45/32", "27/16", "15/8"};
static const uint16_t sc_dc_22[7] = {0, 1117, 3863, 5902, 7020, 8137, 10883};
static const char *const sc_lab_22[7] = {"1/1", "16/15", "5/4", "45/32", "3/2", "8/5", "15/8"};
static const uint16_t sc_dc_23[7] = {0, 2039, 3156, 4980, 7020, 8844, 10176};
static const char *const sc_lab_23[7] = {"1/1", "9/8", "6/5", "4/3", "3/2", "5/3", "9/5"};
static const uint16_t sc_dc_24[5] = {0, 3156, 4980, 8137, 10176};
static const char *const sc_lab_24[5] = {"1/1", "6/5", "4/3", "8/5", "9/5"};
static const uint16_t sc_dc_25[7] = {0, 2039, 2941, 4980, 7020, 7922, 9961};
static const char *const sc_lab_25[7] = {"1/1", "9/8", "32/27", "4/3", "3/2", "128/81", "16/9"};
static const uint16_t sc_dc_26[5] = {0, 2039, 3863, 7020, 8844};
static const char *const sc_lab_26[5] = {"1/1", "9/8", "5/4", "3/2", "5/3"};
static const uint16_t sc_dc_27[8] = {0, 2039, 3863, 4980, 7020, 9059, 9961, 10883};
static const char *const sc_lab_27[8] = {"1/1", "9/8", "5/4", "4/3", "3/2", "27/16", "16/9", "15/8"};
static const uint16_t sc_dc_28[7] = {0, 1117, 3863, 5902, 7020, 8137, 10883};
static const char *const sc_lab_28[7] = {"1/1", "16/15", "5/4", "45/32", "3/2", "8/5", "15/8"};
static const uint16_t sc_dc_29[22] = {0, 902, 1117, 1824, 2039, 2941, 3156, 3863, 4078, 4980, 5196, 5902, 6117, 7020, 7922, 8137, 8844, 9059, 9961, 10176, 10883, 11098};
static const char *const sc_lab_29[22] = {"1/1", "256/243", "16/15", "10/9", "9/8", "32/27", "6/5", "5/4", "81/64", "4/3", "27/20", "45/32", "729/512", "3/2", "128/81", "8/5", "5/3", "27/16", "16/9", "9/5", "15/8", "243/128"};
static const uint16_t sc_dc_30[7] = {0, 2000, 3500, 5000, 7000, 9000, 10500};
static const char *const sc_lab_30[7] = {"0C", "200C", "350C", "500C", "700C", "900C", "1050C"};
static const uint16_t sc_dc_31[7] = {0, 1500, 3000, 5000, 7000, 8000, 10000};
static const char *const sc_lab_31[7] = {"0C", "150C", "300C", "500C", "700C", "800C", "1000C"};
static const uint16_t sc_dc_32[7] = {0, 1500, 3000, 4000, 6000, 8000, 10000};
static const char *const sc_lab_32[7] = {"0C", "150C", "300C", "400C", "600C", "800C", "1000C"};
static const uint16_t sc_dc_33[5] = {0, 2310, 4740, 7170, 9550};
static const char *const sc_lab_33[5] = {"0C", "231C", "474C", "717C", "955C"};
static const uint16_t sc_dc_34[7] = {0, 1200, 2700, 5400, 6700, 7850, 9500};
static const char *const sc_lab_34[7] = {"0C", "120C", "270C", "540C", "670C", "785C", "950C"};
static const uint16_t sc_dc_35[8] = {0, 2039, 3863, 5513, 7020, 8405, 9688, 10883};
static const char *const sc_lab_35[8] = {"8/8", "9/8", "10/8", "11/8", "12/8", "13/8", "14/8", "15/8"};
static const uint16_t sc_dc_36[43] = {0, 215, 533, 845, 1117, 1506, 1650, 1824, 2039, 2312, 2669, 2941, 3156, 3474, 3863, 4175, 4351, 4708, 4980, 5196, 5513, 5825, 6175, 6487, 6804, 7020, 7292, 7649, 7825, 8137, 8526, 8844, 9059, 9331, 9688, 9961, 10176, 10350, 10494, 10883, 11155, 11467, 11785};
static const char *const sc_lab_36[43] = {"1/1", "81/80", "33/32", "21/20", "16/15", "12/11", "11/10", "10/9", "9/8", "8/7", "7/6", "32/27", "6/5", "11/9", "5/4", "14/11", "9/7", "21/16", "4/3", "27/20", "11/8", "7/5", "10/7", "16/11", "40/27", "3/2", "32/21", "14/9", "11/7", "8/5", "18/11", "5/3", "27/16", "12/7", "7/4", "16/9", "9/5", "20/11", "11/6", "15/8", "40/21", "64/33", "160/81"};
static const uint16_t sc_dc_37[13] = {0, 1332, 3018, 4351, 5825, 7369, 8844, 10176, 11650, 13194, 14669, 16001, 17687};
static const char *const sc_lab_37[13] = {"1/1", "27/25", "25/21", "9/7", "7/5", "75/49", "5/3", "9/5", "49/25", "15/7", "7/3", "63/25", "25/9"};
static const uint16_t sc_dc_38[24] = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 10500, 11000, 11500};
static const char *const sc_lab_38[24] = {"0/24", "1/24", "2/24", "3/24", "4/24", "5/24", "6/24", "7/24", "8/24", "9/24", "10/24", "11/24", "12/24", "13/24", "14/24", "15/24", "16/24", "17/24", "18/24", "19/24", "20/24", "21/24", "22/24", "23/24"};
static const uint16_t sc_dc_39[19] = {0, 632, 1263, 1895, 2526, 3158, 3789, 4421, 5053, 5684, 6316, 6947, 7579, 8211, 8842, 9474, 10105, 10737, 11368};
static const char *const sc_lab_39[19] = {"0/19", "1/19", "2/19", "3/19", "4/19", "5/19", "6/19", "7/19", "8/19", "9/19", "10/19", "11/19", "12/19", "13/19", "14/19", "15/19", "16/19", "17/19", "18/19"};
static const uint16_t sc_dc_40[31] = {0, 387, 774, 1161, 1548, 1935, 2323, 2710, 3097, 3484, 3871, 4258, 4645, 5032, 5419, 5806, 6194, 6581, 6968, 7355, 7742, 8129, 8516, 8903, 9290, 9677, 10065, 10452, 10839, 11226, 11613};
static const char *const sc_lab_40[31] = {"0/31", "1/31", "2/31", "3/31", "4/31", "5/31", "6/31", "7/31", "8/31", "9/31", "10/31", "11/31", "12/31", "13/31", "14/31", "15/31", "16/31", "17/31", "18/31", "19/31", "20/31", "21/31", "22/31", "23/31", "24/31", "25/31", "26/31", "27/31", "28/31", "29/31", "30/31"};
#define N_SCALES 41   /* scale 0 is OFF (free); 1.. are these */
static const Scale scale_list[N_SCALES] = {
    {"MAJOR", 12000, 7, sc_dc_0, sc_lab_0},
    {"MINOR", 12000, 7, sc_dc_1, sc_lab_1},
    {"HARM MINOR", 12000, 7, sc_dc_2, sc_lab_2},
    {"MEL MINOR", 12000, 7, sc_dc_3, sc_lab_3},
    {"DORIAN", 12000, 7, sc_dc_4, sc_lab_4},
    {"PHRYGIAN", 12000, 7, sc_dc_5, sc_lab_5},
    {"LYDIAN", 12000, 7, sc_dc_6, sc_lab_6},
    {"MIXOLYDIAN", 12000, 7, sc_dc_7, sc_lab_7},
    {"LOCRIAN", 12000, 7, sc_dc_8, sc_lab_8},
    {"MAJ PENTA", 12000, 5, sc_dc_9, sc_lab_9},
    {"MIN PENTA", 12000, 5, sc_dc_10, sc_lab_10},
    {"BLUES", 12000, 6, sc_dc_11, sc_lab_11},
    {"PHRYG DOM", 12000, 7, sc_dc_12, sc_lab_12},
    {"WHOLE TONE", 12000, 6, sc_dc_13, sc_lab_13},
    {"DIMINISHED", 12000, 8, sc_dc_14, sc_lab_14},
    {"CHROMATIC", 12000, 12, sc_dc_15, sc_lab_15},
    {"WELL-TUNED", 12000, 12, sc_dc_16, sc_lab_16},
    {"BHAIRAV", 12000, 7, sc_dc_17, sc_lab_17},
    {"YAMAN", 12000, 7, sc_dc_18, sc_lab_18},
    {"BHAIRAVI", 12000, 7, sc_dc_19, sc_lab_19},
    {"TODI", 12000, 7, sc_dc_20, sc_lab_20},
    {"MARWA", 12000, 6, sc_dc_21, sc_lab_21},
    {"PURVI", 12000, 7, sc_dc_22, sc_lab_22},
    {"KAFI", 12000, 7, sc_dc_23, sc_lab_23},
    {"MALKAUNS", 12000, 5, sc_dc_24, sc_lab_24},
    {"DARBARI", 12000, 7, sc_dc_25, sc_lab_25},
    {"BHUPALI", 12000, 5, sc_dc_26, sc_lab_26},
    {"DESH", 12000, 8, sc_dc_27, sc_lab_27},
    {"PURIYA DHAN", 12000, 7, sc_dc_28, sc_lab_28},
    {"22 SHRUTI", 12000, 22, sc_dc_29, sc_lab_29},
    {"MAQAM RAST", 12000, 7, sc_dc_30, sc_lab_30},
    {"MAQAM BAYATI", 12000, 7, sc_dc_31, sc_lab_31},
    {"MAQAM SABA", 12000, 7, sc_dc_32, sc_lab_32},
    {"SLENDRO", 12000, 5, sc_dc_33, sc_lab_33},
    {"PELOG", 12000, 7, sc_dc_34, sc_lab_34},
    {"HARMONICS", 12000, 8, sc_dc_35, sc_lab_35},
    {"PARTCH 43", 12000, 43, sc_dc_36, sc_lab_36},
    {"BOHLEN-PIERCE", 19020, 13, sc_dc_37, sc_lab_37},
    {"24-EDO", 12000, 24, sc_dc_38, sc_lab_38},
    {"19-EDO", 12000, 19, sc_dc_39, sc_lab_39},
    {"31-EDO", 12000, 31, sc_dc_40, sc_lab_40},
};
/* ---- the scale (AERIA OS, sadnoise, 12 Nov 2026), after Balloon Shop and Cloudfin (Scales.h) ----------------------
 * One scale for the whole instrument: MIX, FUNC layer, SCAL (OFF = free) and ROOT. Every pitch knob snaps to it:
 * an interval (PITCH's SHFT, the loop's, the balloons', the stars' and PONG's PTCH) goes to the nearest degree of the
 * scale in any octave, as the plugin's shift knob does; a note (RESO's PTCH) goes to the nearest degree above ROOT.
 * The microtonal scales are kept to a tenth of a cent: the sampler plays the exact ratio, PITCH and RESO get the
 * remainder through their FINE; GRAINS has no fine tune and takes the nearest semitone. */
#if defined(AERIAOS)
typedef char scal_knob_fits[N_SCALES == 41 ? 1 : -1];   /* params12.c: MIX SCAL 0..41 */
static int scale_now(void) { return clamp(pv(PG_MIX, 8), 0, N_SCALES); }
/* an interval in tenths of a cent (either sign) -> the nearest degree in any period; *deg its place in the scale (-1: free) */
static int scale_snap_deg(int dc, int *deg)
{
    int sc = scale_now();
    if (!sc) { if (deg) *deg = -1; return dc; }
    const Scale *s = &scale_list[sc - 1];
    int per = s->period, base = dc >= 0 ? dc / per : -((-dc + per - 1) / per);
    int best = 0, bd = 1 << 30, bi = 0;
    for (int p = base - 1; p <= base + 1; p++)
        for (int i = 0; i < s->n; i++) { int c = p * per + s->dc[i], d = iabs(c - dc); if (d < bd) { bd = d; best = c; bi = i; } }
    if (deg) *deg = bi;
    return best;
}
int aos_scale_snap(int dc) { return scale_snap_deg(dc, 0); }
int aos_scale_root(void) { return clamp(pv(PG_MIX, 9), 0, 11); }
static int dc_round(int dc, int unit) { return (dc >= 0 ? dc + unit / 2 : dc - unit / 2) / unit; }
/* what a pitch knob reads as: free, its semitones; in a scale, where it landed -- semitones when it is a whole
 * number of them, else cents -- and the degree's name (5/4, B3, 7/19 ...) */
static void pitch_text(char *n, int dc)
{
    int deg, q = scale_snap_deg(dc, &deg), i = 0;
    if (deg < 0 || q % 1000 == 0) signed_text(n, q / 1000);
    else { signed_text(n, dc_round(q, 10)); while (n[i]) i++; n[i++] = 'C'; n[i] = 0; }
    if (deg >= 0) {
        const char *l = scale_list[scale_now() - 1].lab[deg];
        i = 0; while (n[i]) i++;
        n[i++] = ' '; while (*l && i < 18) n[i++] = *l++; n[i] = 0;
    }
}
/* RESO's note (MIDI) in the scale: the note it lands on and how far off the keyboard's that is (tenths of a cent) */
static int reso_snap(int midi, int *off)
{
    int root = aos_scale_root(), q = aos_scale_snap((midi - root) * 1000) + root * 1000, m = dc_round(q, 1000);
    if (off) *off = q - m * 1000;
    return m;
}
int aos_reso_snap(int midi, int *off) { return reso_snap(midi, off); }
static void reso_text(char *n, int midi)
{
    static const char *const nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int off, m = reso_snap(midi, &off), i = 0;
    const char *p = nn[((m % 12) + 12) % 12]; while (*p) n[i++] = *p++;
    num(n + i, m / 12 - 1); while (n[i]) i++;
    if (off) { n[i++] = ' '; signed_text(n + i, dc_round(off, 10)); while (n[i]) i++; n[i++] = 'C'; n[i] = 0; }
}
#endif
#if defined(AERIAOS)
uint8_t preview_scale_key;
void aos_limiter(int on);
void aos_master_eq(int band, int freq, int db);
static void scale_tick(void) { preview_scale_key = (uint8_t)(scale_now() * 12 + aos_scale_root()); aos_limiter(pv(PG_MIX, 10) != 1);   /* and MIX's LIMT: the main out's limiter */
    /* the master EQ (GAIN, FUNC layer) */
    aos_master_eq(0, 37, pv(PG_GAIN, 6) - 13); aos_master_eq(1, pv(PG_GAIN, 10), pv(PG_GAIN, 7) - 13);
    aos_master_eq(2, pv(PG_GAIN, 11), pv(PG_GAIN, 8) - 13); aos_master_eq(3, 105, pv(PG_GAIN, 9) - 13); }
#endif
/* ---- MASTER: the freeze delay on the main mix, and the tempo (AERIA OS, sadnoise, 12 Nov 2026) --------------------
 * After the chain and after the weather; the weather never moves it. (The master EQ is on GAIN's FUNC layer, the
 * limiter's switch on MIX's.) The MIX key opens the page:
 *   TIME  the delay's time in sixteenth notes at the tempo, 1..16 -- the sixteen trigs pick it at a touch
 *   FDBK  how much comes round again      HPF / LPF  a high-pass and a lowpass in the feedback
 *   SEND  how much of the mix goes in     WET  how much of the delay is heard (0: the delay is off)
 *   FUNC: TMPO  the tempo, 60..187 BPM
 *   FREEZE: the crossfader at B. Nothing more goes in, what is in it goes round for ever; a change of TIME or
 *         tempo then jumps
 *   NO    tap the tempo (the last taps' average)
 *   The crossfader (FUNC: XFRZ on): at A the delay runs, moved to B it freezes -- and thaws on the way back. */
#if defined(AERIAOS)
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen);
static uint8_t fz_frozen;
static uint32_t us_clock, tap_at[4];
static int tap_n;
static int master_bpm(void) { return 60 + pv(PG_MASTER, 6); }
static void master_fast(uint32_t us) { us_clock += us; }
static uint8_t fz_fader;   /* the crossfader holds it frozen */
static int master_frozen(void) { return fz_frozen || fz_fader; }
static void master_tick(void)
{
    int wet = pv(PG_MASTER, 5);
    /* the crossfader: past three quarters of the way to B it freezes, back under a quarter past the middle it thaws */
    if (pv(PG_MASTER, 7) == 2) { int x = xfade(); if (x > 96 && !fz_fader) { fz_fader = 1; say("FREEZE", "ON"); } else if (x < 80 && fz_fader) { fz_fader = 0; if (!fz_frozen) say("FREEZE", "OFF"); } }
    else fz_fader = 0;
    aos_freeze_delay(wet > 0 || master_frozen(), pv(PG_MASTER, 0), master_bpm(), pv(PG_MASTER, 1), pv(PG_MASTER, 2), pv(PG_MASTER, 3),
                     pv(PG_MASTER, 4), wet, master_frozen());
}
static void master_tap(void)
{
    uint32_t now_us = us_clock;
    if (tap_n && now_us - tap_at[tap_n - 1] > 2000000u) tap_n = 0;            /* two seconds apart: a new count */
    if (tap_n == 4) { for (int i = 0; i < 3; i++) tap_at[i] = tap_at[i + 1]; tap_n = 3; }
    tap_at[tap_n++] = now_us;
    if (tap_n >= 2) {
        uint32_t span = tap_at[tap_n - 1] - tap_at[0], beat = span / (uint32_t)(tap_n - 1);
        if (beat) { int bpm = (int)((60000000u + beat / 2) / beat); bpm = clamp(bpm, 60, 187); Param *p = &P(PG_MASTER, 6); p->value = p->user = (int16_t)(bpm - 60); char n[8]; num(n, bpm); say("TEMPO", n); }
    } else say("TAP", 0);
}
static int master_key(int code)
{
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + 16) { Param *p = &P(PG_MASTER, 0); p->value = p->user = (int16_t)(code - KEY_TRIG0 + 1); char n[8]; num(n, p->value); int k = 0; while (n[k]) k++; n[k++] = '/'; n[k++] = '1'; n[k++] = '6'; n[k] = 0; say("DELAY TIME", n); return 1; }
    /* (YES froze it once: too easy to do by accident -- the crossfader at B is the only freeze now) */
    if (code == KEY_NO) { master_tap(); return 1; }
    return 0;
}
static void master_say(int i, int v, char *n)
{
    int k = 0;
    if (i == 0) { num(n, v); while (n[k]) k++; n[k++] = '/'; n[k++] = '1'; n[k++] = '6'; n[k] = 0; say("DELAY TIME", n); }
    else if (i == 7) say("FADER FREEZES", v == 2 ? "ON" : "OFF");
    else if (i == 6) { num(n, 60 + v); while (n[k]) k++; n[k++] = 'B'; n[k++] = 'P'; n[k++] = 'M'; n[k] = 0; say("TEMPO", n); }
    else if (i == 2 || i == 3) {                                              /* the one-pole's corner, about */
        int c = i == 3 ? (v >= 127 ? 32768 : 600 + v * v * 2) : v * v / 2, hz = c >= 30000 ? 0 : c * 7019 / (32768 - c / 2);
        if (i == 3 && !hz) { say("DELAY LOWPASS", "OPEN"); return; }
        if (i == 2 && !v) { say("DELAY HIGH-PASS", "OFF"); return; }
        if (hz >= 1000) { num(n, hz / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + hz % 1000 / 100); n[k++] = 'K'; } else { num(n, hz); while (n[k]) k++; }
        n[k++] = 'H'; n[k++] = 'Z'; n[k] = 0; say(i == 3 ? "DELAY LOWPASS" : "DELAY HIGH-PASS", n);
    }
    else { num(n, v * 100 / 127); while (n[k]) k++; n[k++] = '%'; n[k] = 0; say(i == 1 ? "DELAY FEEDBACK" : i == 4 ? "DELAY SEND" : "DELAY WET", n); }
}
static void master_leds(uint8_t *led) { int t = pv(PG_MASTER, 0); for (int k = 0; k < 16; k++) led[k] = (uint8_t)(k + 1 == t ? 2 : k < t ? 1 : 0); }
#endif
/* ---- sound ------------------------------------------------------------------------------- */
static void drift_tick(void)
{
    if ((ticks & 7) != 0) return;
    for (int pg = 0; pg < N_PAGES; pg++)
        for (int i = 0; i < 6; i++) {
            if (!params[pg][i].drift) continue;
            if ((rnd() % (uint32_t)(20 + 400 / (1 + pv(PG_SKY, 9)))) == 0) drift_target[pg][i] = (int16_t)range(-64, 64);
            drift_now[pg][i] = (int16_t)(drift_now[pg][i] + (drift_target[pg][i] > drift_now[pg][i]) - (drift_target[pg][i] < drift_now[pg][i]));
        }
}
static int out_value(int pg, int i)
{
    Param *p = &params[pg][i];
    int v = pv(pg, i);
    if (p->drift && i < 6) v += drift_now[pg][i] * pv(PG_SKY, 8) / 160;
    return clamp(v, p->lo, p->hi);
}

/* balloons: caught by the recorders of T2-T4 (REC AB, CD, SRC3), sung by T1 */
enum { B_EMPTY, B_FILLING, B_FLOATING, B_POPPING };
typedef struct { uint8_t state, track, key; int16_t size, x, y, pop; } Balloon;
static Balloon balloon[3] = {{B_EMPTY, 1, KEY_REC_AB, 0, 24, 0, 0}, {B_EMPTY, 2, KEY_REC_CD, 0, 64, 0, 0}, {B_EMPTY, 3, KEY_REC3, 0, 104, 0, 0}};
static int balloon_voice;
static uint8_t balloon_quiet;   /* bit b: balloon b floats but does not sing (BALLOON page, trigs 1-3) */
static uint8_t balloon_strt;
#if defined(PREVIEW) && defined(AERIAOS)
void aos_balloon(int b, int state);
void aos_balloon_at(int step, int len, int b);
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level);
#define AOS_BALLOON(b, st) aos_balloon(b, st)
#else
#define AOS_BALLOON(b, st) ((void)0)
#endif
static int any_floating(void) { for (int b = 0; b < 3; b++) if (balloon[b].state == B_FLOATING) return 1; return 0; }

static void weather_fresh(void);
/* this weather alone (the knob, the up and down keys, the weather's own turning) */
static void set_weather(int w)
{
    if (w == weather && wx_on == (w == CLEAR ? 0 : 1u << w)) return;
    weather = w; wx_on = (uint8_t)(w == CLEAR ? 0 : 1u << w);
    weather_fresh();
}
/* one weather on or off, the others as they are (SKY: the trigs); SNOW and HEAT put each other out */
static void toggle_weather(int w)
{
    if (w <= CLEAR || w >= N_WEATHER) { set_weather(CLEAR); return; }
    wx_on ^= (uint8_t)(1u << w);
    if (WON(w)) { if (w == SNOW) wx_on &= (uint8_t)~(1u << HEAT); if (w == HEAT) wx_on &= (uint8_t)~(1u << SNOW); weather = w; }
    else { weather = CLEAR; for (int i = N_WEATHER - 1; i > CLEAR; i--) if (WON(i)) { weather = i; break; } }
    weather_fresh();
    char line[24]; int n = 0;
    for (int i = 1; i < N_WEATHER && n < 18; i++) if (WON(i)) { const char *p = weather_name[i]; if (n) line[n++] = '+'; for (int k = 0; k < 3 && p[k]; k++) line[n++] = p[k]; }
    line[n] = 0; say(n ? line : "CLEAR", 0);
}
static void weather_fresh(void)
{
    int w = weather;
    amount = 0;
    spell_left = (uint32_t)range(spell_s[w][0], spell_s[w][1]) * 60u * (uint32_t)pv(PG_SKY, 7) / 4u;
    P(PG_SKY, 3).value = P(PG_SKY, 3).user = (int16_t)w;
    /* a new weather takes the knobs it moves back from the musician -- but never an effect's MIX: how much of an
     * effect is heard is the musician's alone (the weather used to bring every effect in by itself: 12 Nov 2026) */
    for (int s = 0; s < N_SLOTS; s++) for (int k = 0; k < 6; k++) if (rule_of(slots[s].knob[k]) >= 0 && !knob_is_mix(slots[s].knob[k])) slots[s].user[k] = -1;
    say(weather_name[w], 0);
}


/* The stock engine passes THRU/NEIGHBOR audio only after a trig: every chain and source track
 * keeps one on the pattern's first step, its AMP envelope held fully open (HOLD and REL 127). */
static void keep_alive(void)
{
    if (pat_off <= -99 || (ticks % 60u) != 0) return;
    static const uint8_t held[] = {SAMPLE_T, HEAD_T, HEAD_T + 1, HEAD_T + 2, HEAD_T + 3};
    for (unsigned i = 0; i < sizeof held; i++) {
        int t = held[i];
        pat_trig(t, 0, 1);
        lane_put(t, 13, 127);
        lane_put(t, 14, 127);
    }
}
#if defined(PREVIEW) && defined(AERIAOS)
int aos_src_loop(void) { return src_loop; }
#endif
static void sound_tick(void)
{
    keep_alive();
    slots_sound();
#if defined(PREVIEW) && defined(AERIAOS)
    /* the balloons' long grains (aeria-os sampler.c): each floating balloon sings, unless trig 1-3 quieted it */
    for (int b = 0; b < 3; b++)
        aos_balloon_cloud(b, src_balloons && balloon[b].state == B_FLOATING && !((balloon_quiet >> b) & 1),
                          pv(PG_BALLOON, 0), pv(PG_BALLOON, 3), pv(PG_BALLOON, 4), pv(PG_BALLOON, 1),
                          pv(PG_BALLOON, 2) - 64 + 12 * (pv(PG_BALLOON, 6) - 3) + pv(PG_BALLOON, 9 + b) - 25,
                          pv(PG_BALLOON, 7) - 1, pv(PG_BALLOON, 8) - 1, pv(PG_BALLOON, 5));
#endif
    /* HEAD on T3 FX1: FDBK FTON SMPL STAR IN WARM (the order its manifest gives) */
    int smpl = src_loop ? pv(PG_LOOP, 4) : (src_balloons && any_floating()) ? pv(PG_BALLOON, 5) : 0;
    int star = src_stars ? pv(PG_STARS, 11) : 0;
    lane_put(HEAD_T, 18, pv(PG_MIX, 0));
    lane_put(HEAD_T, 19, pv(PG_MIX, 1));
    lane_put(HEAD_T, 20, smpl);
    lane_put(HEAD_T, 21, star);
    lane_put(HEAD_T, 22, pv(PG_GAIN, 2));
    lane_put(HEAD_T, 23, pv(PG_MIX, 3));
    /* the inputs: T3 THRU, MAIN slots 0 INAB, 1 VOL, 3 INCD, 4 VOL (64 = unity) */
    lane_put(HEAD_T, 0, pv(PG_MIX, 6));
    lane_put(HEAD_T, 3, pv(PG_MIX, 7));
    lane_put(HEAD_T, 1, pv(PG_GAIN, 0));
    lane_put(HEAD_T, 4, pv(PG_GAIN, 1));
    /* along the chain at unity, out of it at OUT */
    for (int t = HEAD_T; t < LAST_T; t++) lane_put(t, 15, 108);
    lane_put(LAST_T, 15, muted[LAST_T] ? 0 : pv(PG_GAIN, 3));
    lane_put(LAST_T, 16, pv(PG_GAIN, 5));
    lane_put(LAST_T, 17, pv(PG_GAIN, 4));
    /* the sources play only into HEAD (their own FX1 is TAP: silent out) */
    if (src_loop) {
        int strt = pv(PG_LOOP, 0), end = pv(PG_LOOP, 1);
        if (end <= strt) end = strt + 1;
        lane_put(SAMPLE_T, 1, strt);
        lane_put(SAMPLE_T, 2, clamp(end - strt, 1, 127));
        lane_put(SAMPLE_T, 0, pv(PG_LOOP, 2));
        lane_put(SAMPLE_T, 3, pv(PG_LOOP, 3));
        lane_put(SAMPLE_T, 12, pv(PG_LOOP, 5));
        lane_put(SAMPLE_T, 13, 127);
        lane_put(SAMPLE_T, 14, 127);
        lane_put(SAMPLE_T, 15, 108);
    } else if (src_balloons && any_floating()) {
        int every = 2 + (127 - pv(PG_BALLOON, 0)) / 16;
        if (ticks % (uint32_t)every == 0) balloon_strt = (uint8_t)clamp(pv(PG_BALLOON, 4) + (pv(PG_BALLOON, 1) ? range(-pv(PG_BALLOON, 1) / 2, pv(PG_BALLOON, 1) / 2) : 0), 0, 120);
        lane_put(SAMPLE_T, 1, balloon_strt);
        lane_put(SAMPLE_T, 0, pv(PG_BALLOON, 2));
        lane_put(SAMPLE_T, 4, pv(PG_BALLOON, 3));
        lane_put(SAMPLE_T, 5, pv(PG_BALLOON, 0));
        lane_put(SAMPLE_T, 15, 108);
    } else lane_put(SAMPLE_T, 15, 0);
    lane_put(STARS_T, 15, src_stars ? 108 : 0);
    algo = src_stars ? ALGO_STARS : ALGO_NONE;
}

/* balloons sing on T1: a trig a step, sample-locked to the balloon's recorder buffer
 * (Flex slots 128..135 are the recorders, 0-based), its start jittered inside the cloud */
static int balloon_placed = -1;
static void place_balloon(int step)
{
    if (!src_balloons || src_loop || !any_floating()) return;
    if (balloon[balloon_voice].state != B_FLOATING) for (int b = 0; b < 3; b++) if (balloon[b].state == B_FLOATING) balloon_voice = b;
    pat_lock(SAMPLE_T, step, 31, 128 + balloon[balloon_voice].track);
    pat_lock(SAMPLE_T, step, 1, balloon_strt);
    pat_trig(SAMPLE_T, step, 1);
#if defined(PREVIEW) && defined(AERIAOS)
    aos_balloon_at(step, pat_len(SAMPLE_T), balloon_voice);
#endif
    balloon_placed = step;
}
/* ---- the sky ------------------------------------------------------------------------------- */
#define MAX_CLOUDS 8
#define CLOUD_MAX_W 44
/* the cloud shapes, traced from sadnoise's drawing (Desktop, 9 Nov 2026) by tools/trace_clouds.py:
 * each one 40 columns, top = height above the base, bot = how far the bottom edge lifts, 0..63 */
#define N_SHAPES 13
#define SHAPE_N 40
static const uint8_t shape_top[N_SHAPES][SHAPE_N] = {
    {21,27,32,33,34,36,38,41,39,40,40,41,45,51,53,55,57,60,62,63,63,63,62,61,60,57,55,52,49,44,41,41,41,41,41,41,40,39,37,32},
    {21,29,32,36,39,41,43,47,50,52,53,54,54,56,57,60,61,61,62,63,63,63,62,60,57,52,48,46,45,45,45,45,43,41,39,36,32,27,21,14},
    {11,17,27,30,33,35,36,37,39,41,41,42,41,41,41,43,50,55,58,60,61,63,63,63,63,63,63,63,62,61,60,59,57,55,53,50,47,44,39,30},
    {25,37,42,47,52,55,57,58,59,59,59,59,55,52,54,57,59,61,62,63,63,63,62,57,55,53,55,58,60,61,61,61,59,57,54,51,48,40,31,24},
    {11,19,24,30,34,38,41,44,47,50,52,54,56,58,60,61,62,62,63,63,62,60,59,56,53,48,43,37,32,32,33,33,33,32,32,31,30,27,23,11},
    {7,22,26,30,33,35,37,38,38,40,44,47,49,51,53,55,56,58,59,61,61,63,63,63,62,61,59,56,54,49,45,41,40,39,37,34,30,25,19,9},
    {12,20,26,30,33,35,37,38,40,40,41,47,51,55,58,60,61,62,63,63,63,62,61,60,58,52,41,42,44,45,46,47,48,47,46,44,40,35,27,13},
    {8,18,34,42,47,50,53,56,57,58,58,58,58,58,57,56,56,57,58,60,61,62,63,63,63,63,61,56,54,56,58,60,61,61,61,60,59,57,55,47},
    {21,30,35,38,41,43,45,47,49,49,50,51,51,51,51,52,54,58,59,60,60,59,55,54,56,57,58,60,60,61,61,62,63,62,61,60,60,58,52,45},
    {10,19,23,26,28,29,30,33,37,40,43,45,46,47,48,48,48,48,50,53,56,58,60,61,61,62,63,63,63,62,57,45,37,37,37,36,34,30,27,19},
    {7,15,19,22,24,26,28,29,29,29,41,45,49,52,55,57,59,60,61,62,62,62,63,63,62,61,60,56,36,37,38,38,38,37,36,30,30,30,29,22},
    {13,22,32,40,47,52,54,57,60,60,62,62,63,63,63,62,61,59,56,52,52,53,54,52,47,40,43,44,45,44,44,42,39,31,32,33,33,32,30,22},
    {6,21,28,37,45,50,53,56,60,62,63,63,62,60,55,49,42,38,39,41,41,41,40,39,37,35,36,40,47,49,51,52,53,53,53,52,50,46,39,17},
};
static const uint8_t shape_bot[N_SHAPES][SHAPE_N] = {
    {10,3,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,2,2,3,4,6,8,14,23},
    {9,4,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,2,2,2,2,2,2,2,2,2,2,2,8},
    {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,3,3,4,6,9,16},
    {17,9,4,2,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,3,4,10},
    {4,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,3,2,1,2,1,1,2,0,0,2},
    {4,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,2,2,2,3,3,4,2,3,3,3,3,4,4,4,3,2,5},
    {4,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,3,5,7,10,14,27,28,31,38},
    {5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,4,3,3,4,4,4,5,3,4,4,6,7,9,13,17,22,34},
    {3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,3,3,4,5,7,8,10,13},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,3,4,4,5,6,10,16},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,1,0,1,1,2,4,7,16},
    {0,0,0,0,0,0,0,0,0,0,1,1,2,2,2,2,2,2,2,2,2,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,5},
};
/* height / width of each drawn cloud, x100 */
static const uint8_t shape_aspect[N_SHAPES] = {53,43,57,28,37,43,32,43,38,44,47,37,31};
typedef struct { int32_t x; int16_t y; uint8_t w, depth; int8_t top[CLOUD_MAX_W], bot[CLOUD_MAX_W]; } Cloud;
static Cloud clouds[MAX_CLOUDS];
#define MAX_DROPS 72
typedef struct { int16_t x, y; int8_t v, phase; } Drop;
static Drop drops[MAX_DROPS];
static int32_t fin_x = 40 * 256;

static int wind_now(void) { int w = pv(PG_SKY, 0) - 9; if (gust) w = w >= 0 ? w + 9 : w - 9; return w; }

static void make_cloud(Cloud *c, int x_pixels)
{
    int depth = range(1, 3);
    int w = depth == 1 ? range(14, 24) : depth == 2 ? range(20, 32) : range(28, CLOUD_MAX_W);
    c->w = (uint8_t)w; c->depth = (uint8_t)depth; c->x = x_pixels * 256;
    c->y = (int16_t)(depth == 1 ? range(12, 22) : depth == 2 ? range(20, 32) : range(28, 42));
    /* one of the traced shapes, stretched to this width; height from the drawing's own proportions (capped
     * at 16, as the clouds always were). Mirrored half the time so the sky doesn't repeat. */
    int sh = range(0, N_SHAPES - 1), flip = range(0, 1);
    int h = w * shape_aspect[sh] / 100; if (h > 16) h = 16; if (h < 4) h = 4;
    for (int i = 0; i < w; i++) {
        int k = (i * (SHAPE_N - 1) + (w - 1) / 2) / (w - 1); if (flip) k = SHAPE_N - 1 - k;
        c->top[i] = (int8_t)((shape_top[sh][k] * h + 31) / 63);
        c->bot[i] = (int8_t)((shape_bot[sh][k] * h + 31) / 63);
        if (c->bot[i] >= c->top[i]) c->bot[i] = (int8_t)(c->top[i] - 1);
    }
    for (int i = 0; i < w; i++) if (c->top[i] < 1) c->top[i] = 1;
}
static void new_sky(void)
{
    for (int i = 0; i < MAX_CLOUDS; i++) make_cloud(&clouds[i], range(-30, W));
    for (int i = 0; i < MAX_DROPS; i++) { drops[i].x = (int16_t)(range(0, W - 1) * 16); drops[i].y = (int16_t)(range(-40, WATER_Y) * 16); drops[i].v = (int8_t)range(18, 30); drops[i].phase = (int8_t)range(0, 63); }
}
static int clouds_wanted(void)
{
    static const uint8_t n[N_WEATHER] = {3, 7, 7, 8, 5, 0, 4};
    int most = n[CLEAR];
    for (int w = 1; w < N_WEATHER; w++) if (WON(w)) { if (most == n[CLEAR] || n[w] > most) most = n[w]; }   /* the cloudiest of the weathers that are on (HEAT alone: none) */
    return clamp(most + pv(PG_SKY, 1) - 5, 0, MAX_CLOUDS);
}
static void draw_cloud(const Cloud *c)
{
    int x0 = c->x >> 8, base = c->y, heavy = WON(RAIN) || WON(STORM) || WON(FOG);
    int level = heavy ? 8 : c->depth == 3 ? 8 : c->depth == 2 ? 4 : 2;
    if (WON(STORM) && c->depth == 3) level = 11;
    for (int i = 0; i < c->w; i++) {
        int x = x0 + i;
        if ((unsigned)x >= W) continue;
        int top = base - c->top[i], bot = base - c->bot[i];
        span(x, top, bot, 0);
        for (int y = top + 1; y < bot; y++) if (dither_on(x, y, level)) px(x, y);
        px(x, top); px(x, bot);
        if (i == 0 || i == c->w - 1) span(x, top, bot, 1);
        else {
            int l = c->top[i - 1], r = c->top[i + 1], h = c->top[i];
            if (l < h) span(x, base - h, base - l, 1);
            if (r < h) span(x, base - h, base - r, 1);
            int lb = c->bot[i - 1], rb = c->bot[i + 1], b = c->bot[i];   /* the bottom edge's steps, as the top's */
            if (lb > b) span(x, base - lb, base - b, 1);
            if (rb > b) span(x, base - rb, base - b, 1);
        }
    }
}
static void draw_sun_moon(void)
{
    if (WON(STORM) || WON(FOG)) return;
    if (night) {
        uint32_t s = 0x2545f491u;
        for (int i = 0; i < 40; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; if (((s >> 20) + (ticks >> 4)) % 7) px((int)(s % W), (int)((s >> 8) % (WATER_Y - 6))); }
        for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++)
            if (dx * dx + dy * dy <= 36 && (dx - 3) * (dx - 3) + (dy + 1) * (dy + 1) > 25) px(108 + dx, 12 + dy);
        return;
    }
    int big = WON(HEAT) ? 8 : 5, cx = 110, cy = 13;
    for (int dy = -big; dy <= big; dy++) for (int dx = -big; dx <= big; dx++) {
        int d = dx * dx + dy * dy;
        if (d <= big * big && (d >= (big - 1) * (big - 1) || (WON(HEAT) && dither_on(dx, dy, 6)))) px(cx + dx, cy + dy);
    }
    for (int k = 0; k < 8; k++) { int a = (k * 128 + (int)(ticks * 2)) & 1023; for (int r = big + 3; r <= big + 5; r++) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
}
static void draw_precip(void)
{
    int want = (WON(RAIN) ? 40 : WON(STORM) ? 72 : WON(SNOW) ? 48 : 0) * amount / 256, wind = wind_now();
    for (int i = 0; i < want; i++) {
        Drop *d = &drops[i];
        int x = d->x >> 4, y = d->y >> 4;
        if (WON(SNOW)) { int wob = sine[(d->phase + (int)ticks) & 63] / 64; px(x + wob, y); if (d->v > 26) { px(x + wob + 1, y); px(x + wob, y + 1); } }
        else { line(x, y, x - wind / 3, y + (WON(STORM) ? 5 : 3)); if (y >= WATER_Y - 2) { px(x - 2, WATER_Y - 1); px(x + 2, WATER_Y - 1); } }
    }
}
static void move_precip(void)
{
    int wind = wind_now();
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &drops[i];
        d->y = (int16_t)(d->y + (WON(SNOW) ? d->v / 4 : WON(STORM) ? d->v * 2 : d->v));
        d->x = (int16_t)(d->x + wind * (WON(SNOW) ? 3 : 6));
        if ((d->y >> 4) > WATER_Y) { d->y = (int16_t)(range(-20, -2) * 16); d->x = (int16_t)(range(-10, W + 10) * 16); }
        if (d->x < -160) d->x = (int16_t)((W + 8) * 16);
        if (d->x > (W + 10) * 16) d->x = -8 * 16;
    }
}
static void draw_fog(void)
{
    if (!WON(FOG)) return;
    int lvl = 5 * amount / 256;
    for (int band = 0; band < 3; band++) {
        int y0 = 14 + band * 13, off = (int)(ticks >> (2 + band)) + band * 17;
        for (int x = 0; x < W; x++) { int wob = sine[(x * 2 + off) & 63] / 40; for (int y = y0 + wob; y < y0 + wob + 7; y++) { if (dither_on(x + off, y, lvl)) px(x, y); else if (dither_on(x + off + 2, y, lvl)) px_off(x, y); } }
    }
}
static void draw_heat(void)
{
    if (!WON(HEAT)) return;
    for (int k = 0; k < 6; k++) {
        int x0 = 8 + k * 20, life = (int)((ticks + (uint32_t)k * 23) % 90);
        for (int y = 0; y < 10; y++) { int yy = WATER_Y - 2 - life / 3 - y; if (yy < 18) break; if (dither_on(x0, yy, 10 * amount / 256 + 1)) px(x0 + sine[(yy * 6 + (int)ticks * 2) & 63] / 50, yy); }
    }
}
static void draw_bolt(void)
{
    if (!bolt_t) return;
    int x = bolt_x, y = 6;
    uint32_t s = (uint32_t)bolt_x * 2654435761u;
    while (y < WATER_Y) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int nx = x + (int)(s % 9) - 4, ny = y + 4 + (int)(s % 4);
        line(x, y, nx, ny); line(x + 1, y, nx + 1, ny);
        if ((s & 31) == 3) line(nx, ny, nx + (int)((s >> 8) % 13) - 6, ny + 8);
        x = nx; y = ny;
    }
}
static void draw_water(void)
{
    int t = (int)ticks, frozen = WON(SNOW) && amount > 200, waves = pv(PG_SKY, 2) + (WON(STORM) ? 3 : 0);
    int tide = pv(PG_MIX, 11) / 32;
    for (int x = 0; x < W; x++) {
        int y = WATER_Y - tide;
        if (!frozen) y += (sine[(x * 3 + t) & 63] * waves + sine[(x * 7 - t * 2) & 63] * (waves > 0)) / 160;
        span(x, y, H - 1, 0);
        px(x, y);
        if (frozen) { if (((x + 1) & 3) == 0) px(x, y + 2); if ((x & 7) == 0) px(x, y + 4); continue; }
        uint32_t k = (uint32_t)x * 2654435761u ^ (uint32_t)(t >> 3) * 40503u;
        if ((k >> 27) == 0) px(x, y + 2 + (int)((k >> 8) % 6));
        if (!night && (x + t / 4) % 9 == 0) px(x, y + 1);
    }
    /* a little boat where the fin was (sadnoise, 10 Nov 2026): drifts with the wind (fin_x), bobs on the swell */
    int bx = fin_x >> 8, bob = sine[(t >> 1) & 63] / 64, wy = WATER_Y - tide + bob;
    if (bx > -16 && bx < W + 2) {
        line(bx, wy - 2, bx + 14, wy - 2);                  /* the deck */
        line(bx, wy - 2, bx + 3, wy + 1);                   /* the bow */
        line(bx + 14, wy - 2, bx + 11, wy + 1);             /* the stern */
        line(bx + 3, wy + 1, bx + 11, wy + 1);              /* the keel */
        for (int x = bx + 2; x <= bx + 12; x++) span(x, wy - 1, wy, 0);
        line(bx + 7, wy - 3, bx + 7, wy - 13);              /* the mast */
        line(bx + 7, wy - 13, bx + 13, wy - 4);             /* the sail */
        line(bx + 8, wy - 4, bx + 13, wy - 4);
        px(bx + 6, wy - 13); px(bx + 5, wy - 12);           /* the pennant */
        for (int i = 1; i < 10; i++) if ((i + t / 3) % 3 == 0) px(bx - i, wy + 1 + (i & 1));   /* the wake */
    }
}

/* the astrolabe */
static void draw_stars(void)
{
    disk_clear(RING_X, RING_Y, RING_R + 4);
    circle(RING_X, RING_Y, RING_R + 3);
    int n = pv(PG_STARS, 1);
    if (pv(PG_STARS, 10) == 0) {
        for (int p = 0; p < PLANETS; p++) {
            int a = planet_ring_angle(p), x = RING_X + cos10(a) * RING_R / 127, y = RING_Y + sin10(a) * RING_R / 127;
            int on = (planet_mask >> p) & 1, lit = p == planet_hit && star_flash;
            if (lit) { circle(x, y, 3); px(x, y); }
            else if (on) { px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1); }
            else px(x, y);
        }
        for (int i = 0; i < n; i++) {
            int a = (star_a[i] + star_rot) & 1023, x = RING_X + cos10(a) * (RING_R - 9) / 127, y = RING_Y + sin10(a) * (RING_R - 9) / 127;
            px(x, y); px(x - 1, y); px(x + 1, y); px(x, y - 1); px(x, y + 1);
            if (i == star_hit && star_flash) { px(x - 2, y - 2); px(x + 2, y - 2); px(x - 2, y + 2); px(x + 2, y + 2); }
            if (i > 0) { int b = (star_a[i - 1] + star_rot) & 1023, bx = RING_X + cos10(b) * (RING_R - 9) / 127, by = RING_Y + sin10(b) * (RING_R - 9) / 127; for (int k = 1; k < 8; k++) if ((k + (int)(ticks >> 3)) & 1) px(x + (bx - x) * k / 8, y + (by - y) * k / 8); }
        }
        line(RING_X, RING_Y - RING_R - 6, RING_X, RING_Y - RING_R + 2);
    } else {
        for (int p = 0; p < PLANETS; p++) {
            int on = (planet_mask >> p) & 1, x = planet_x[p], y = planet_y[p];
            if (p == planet_hit && star_flash) circle(x, y, 3);
            if (on) { px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1); } else px(x, y);
        }
        for (int i = 0; i < n; i++) {
            int x = RING_X + (bird_x[i] >> 4), y = RING_Y + (bird_y[i] >> 4), flap = ((int)ticks / 6 + i) & 1;
            px(x, y); px(x - 1, y - flap); px(x + 1, y - flap); px(x - 2, y - 1 + flap); px(x + 2, y - 1 + flap);
        }
    }
    char b[8] = "BANK ";
    num(b + 5, pv(PG_STARS, 9) + 1);
    text(2, 44, b, 1);
    text(W - 2 - text_w(pv(PG_STARS, 10) ? "BIRDS" : "RING"), 44, pv(PG_STARS, 10) ? "BIRDS" : "RING", 1);
}

static void draw_balloons(void)
{
    int wind = wind_now();
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (n->state == B_EMPTY) {
            span(n->x - 2, WATER_Y - 4, WATER_Y - 1, 1); span(n->x + 2, WATER_Y - 4, WATER_Y - 1, 1);
            px(n->x - 1, WATER_Y - 4); px(n->x, WATER_Y - 4); px(n->x + 1, WATER_Y - 4); px(n->x, WATER_Y - 6); px(n->x, WATER_Y - 5);
            continue;
        }
        int r = 3 + n->size / 40, cx = n->x, cy = n->y;
        if (n->state == B_FLOATING) { cx += sine[(int)(ticks + (uint32_t)b * 21) & 63] / 60 + wind / 3; cy += sine[(int)(ticks / 2 + (uint32_t)b * 13) & 63] / 70; }
        if (n->state == B_POPPING) {
            int k = 12 - n->pop;
            for (int a = 0; a < 1024; a += 128) { int rr = r + k; px(cx + cos10(a) * rr / 127, cy + sin10(a) * rr / 127); px(cx + cos10(a) * (rr - 2) / 127, cy + sin10(a) * (rr - 2) / 127); }
            continue;
        }
        disk_clear(cx, cy, r + 1);
        for (int a = 0; a < 1024; a += 16) px(cx + cos10(a) * r / 127, cy + sin10(a) * (r + 1) / 127);
        px(cx - r / 2, cy - r / 2); px(cx - r / 2 + 1, cy - r / 2);
        if (n->state == B_FILLING) for (int dy = -r + 2; dy < r; dy++) for (int dx = -r + 2; dx < r - 1; dx++) if (dx * dx + dy * dy < (r - 1) * (r - 1) && dither_on(cx + dx, cy + dy, 4 + (int)(ticks / 4 % 4))) px(cx + dx, cy + dy);
        if (n->state == B_FLOATING && b == balloon_voice && (ticks & 16)) { px(cx + r + 2, cy - r); px(cx + r + 3, cy - r - 1); }
        px(cx, cy + r + 2); px(cx - 1, cy + r + 2); px(cx + 1, cy + r + 2);
        int sx = cx;
        for (int y = cy + r + 3; y < WATER_Y; y++) { sx += (y & 3) == 0 ? (sine[(y * 4 + (int)ticks) & 63] > 0 ? 1 : -1) : 0; px(sx, y); }
        char d[2] = {(char)('1' + b), 0};
        if (r >= 5) text(cx - 1, cy - 2, d, 1);
    }
}
static void balloons_tick(void)
{
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (n->state == B_FILLING) { if (n->size < 240) n->size++; n->y = (int16_t)(WATER_Y - 8 - (3 + n->size / 40)); }
        else if (n->state == B_FLOATING) { int top = 14 + b * 5; if (n->y > top) n->y--; }
        else if (n->state == B_POPPING && --n->pop <= 0) n->state = B_EMPTY;
    }
}

static void bubble(int cx, int cy, int r, int fill, const char *name, int following)
{
    disk_clear(cx, cy, r);
    int level = cy + r - (2 * r * fill) / 127;
    for (int dx = -r; dx <= r; dx++) {
        int h = isqrt(r * r - dx * dx), surf = level + sine[(dx * 9 + (int)ticks * 3) & 63] / 90;
        for (int y = cy - h; y <= cy + h; y++) {
            if (y == cy - h || y == cy + h) px(cx + dx, y);
            else if (y >= surf && (y == surf || dither_on(cx + dx, y, 10))) px(cx + dx, y);
        }
    }
    px(cx - r / 2, cy - r / 2);
    if (following) { px(cx + r - 1, cy - r + 1); px(cx + r, cy - r); }
    text(cx - text_w(name) / 2, cy + r + 2, name, 1);
}
/* ---- the intro: the brush logo, then BY SADNOISE ------------------------------------------------- */
#define INTRO_LEN 270
static void draw_intro(void)
{
    int t = intro, x0 = (W - LOGO_W) / 2;
    int edge = t < 20 ? -10 : (t - 20) * (LOGO_W + 12) / 70;
    int fade = t < 195 ? 16 : t < 250 ? 16 - (t - 195) * 16 / 55 : 0;
    for (int c = 0; c < LOGO_W; c++) {
        int ragged = sine[(c * 5 + t) & 63] / 40 + sine[(c * 13) & 63] / 50;
        if (c > edge + ragged) break;
        uint32_t lo = logo_cols[c][0], hi = logo_cols[c][1];
        if (fade >= 16 && c < edge - 6) { F[(x0 + c) * 2 + 1] |= lo; F[(x0 + c) * 2] |= hi; continue; }
        int lvl = c >= edge - 6 ? 16 - (c - (edge - 6)) * 2 : fade;
        for (int y = 0; y < 64; y++) {
            uint32_t on = y < 32 ? (lo >> y) & 1u : (hi >> (y - 32)) & 1u;
            if (on && dither_on(x0 + c, y, lvl)) px(x0 + c, y);
        }
    }
    if (t > 95 && t < 252) {
        static const char by[] = "BY SADNOISE";
        char shown[sizeof by];
        int n = (t - 95) / 4, i = 0;
        for (; i < n && by[i]; i++) shown[i] = by[i];
        shown[i] = 0;
        text(64 - text_w(by) / 2, LOGO_BOTTOM + 6, shown, 1);
        if (by[i] && (t & 4)) text(64 - text_w(by) / 2 + i * 4, LOGO_BOTTOM + 6, "-", 1);
    }
    if (t > 168 && t < 176) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
    if (t >= 200) { int y = H - (t - 200) * (H - WATER_Y) / 70; for (int x = 0; x < W; x++) px(x, y + sine[(x * 3 + t) & 63] / 120); }
}


/* ---- screensavers: four to play (each plays itself when left alone), twenty-five to watch --
 * ss_start(which) begins one; ss_tick() at 60 Hz; ss_draw() paints the whole frame; ss_key / ss_enc
 * take the keys and encoders (1 = used). Games: LEFT RIGHT UP DOWN, encoder A or LEVEL, YES.
 * After three seconds without input a game runs its own player (attract), forever. */
enum { SS_OFF = 0, SS_PADDLE, SS_BRICKS, SS_ROCKS, SS_BLOCKS, SS_LIGHTHOUSE, SS_STORM, SS_SLIDE, SS_FOREST, SS_SHADOWS,
       SS_DOTS, SS_LIFE, SS_CLOCK, SS_PIPES, SS_REDCARS, SS_FROGS, SS_COLUMNS, SS_TRAIN, SS_WALKWAY, SS_ISLAND,
       SS_ELEVATOR, SS_GOLDMOSS, SS_TREBUCHT, SS_VALLEY, SS_BLUEBAR, SS_LILYPADS, SS_BOOKS, SS_CATFACE, SS_WAREHSE, SS_LASER };
#define SS_COUNT 30   /* WATERFALL and VIDEO removed (sadnoise, 9 Nov 2026); SNAKE, CORRIDOR and MEADOW removed, PIPES and
                       * the six dreams added (sadnoise, 12 Nov 2026), then ten more dreams after ISLAND the same day. The games come
                       * first and together (ss_key / ss_enc); new ones go on the end, so a saved SAVR keeps its meaning */
static const char *const ss_name[SS_COUNT] __attribute__((unused)) = {"OFF", "PADDLE", "BRICKS", "ROCKS", "BLOCKS", "LIGHTHOUSE", "STORM",
                                              "SLIDE", "FOREST", "SHADOWS", "DOTS", "LIFE", "CLOCK", "PIPES", "REDCARS", "FROGS",
                                              "COLUMNS", "TRAIN", "WALKWAY", "ISLAND",
                                              "ELEVATOR", "GOLDMOSS", "TREBUCHT", "VALLEY", "BLUEBAR", "LILYPADS", "BOOKS", "CATFACE", "WAREHSE", "LASER"};
#define SS_L 1
#define SS_R 2
#define SS_U 4
#define SS_D 8
#define SS_Y 16
#define SS_AI (ss_idle > 180)
static int ss_which, ss_t, ss_idle = 9999, ss_held, ss_fire, ss_encd;

/* ---- drawing tools ---- */
static uint32_t ss_h(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; return n ^ (n >> 15); }
static int ss_sin(int a) { a &= 1023; int i = a >> 4, f = a & 15, s0 = sine[i]; return s0 + (sine[(i + 1) & 63] - s0) * f / 16; }
static int ss_cos(int a) { return ss_sin(a + 256); }
/* the ordered-dither mask of column x at level 0..16, for both halves (32 is a multiple of 4) */
static uint32_t ss_dm(int x, int lv)
{
    if (lv >= 16) return 0xffffffffu;
    uint32_t n = 0;
    for (int r = 0; r < 4; r++) if (bayer[r * 4 + (x & 3)] < lv) n |= 1u << r;
    return n * 0x11111111u;
}
/* lines a..b of column x at dither level lv: op 0 clear, 1 set, 3 invert */
static void ss_vd(int x, int a, int b, int lv, int op)
{
    if ((unsigned)x >= W || lv <= 0) return;
    if (a < 0) a = 0;
    if (b > H - 1) b = H - 1;
    uint32_t d = ss_dm(x, lv);
    for (int half = 0; half < 2 && a <= b; half++) {
        int lo = half * 32, s = a > lo ? a : lo, e = b < lo + 31 ? b : lo + 31;
        if (s > e) continue;
        uint32_t m = bits(s - lo, e - lo) & d, *w = &F[x * 2 + (half ? 0 : 1)];
        if (op == 0) *w &= ~m; else if (op == 1) *w |= m; else *w ^= m;
    }
}
static void ss_p(int x, int y, int lv, int op) { if (dither_on(x, y, lv)) { if (op) px(x, y); else px_off(x, y); } }
static void ss_ln(int x0, int y0, int x1, int y1, int lv, int op, int th)
{
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int n = 0; n < 300; n++) {
        ss_p(x0, y0, lv, op);
        if (th > 1) { ss_p(x0 + 1, y0, lv, op); ss_p(x0, y0 + 1, lv, op); ss_p(x0 + 1, y0 + 1, lv, op); }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
/* filled ellipse (half: 1 = the top half only) */
static void ss_ell(int cx, int cy, int rx, int ry, int lv, int op, int half)
{
    for (int dx = -rx; dx <= rx; dx++) {
        int h = rx ? isqrt(ry * ry * (rx * rx - dx * dx) / (rx * rx)) : ry;
        ss_vd(cx + dx, cy - h, half ? cy : cy + h, lv, op);
    }
}
static int ss_ed(int xa, int ya, int xb, int yb, int x) { return xb == xa ? ya : ya + (yb - ya) * (x - xa) / (xb - xa); }
/* filled triangle, column by column */
static void ss_tri(int x0, int y0, int x1, int y1, int x2, int y2, int lv, int op)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    for (int x = x0 < 0 ? 0 : x0; x <= x2 && x < W; x++) {
        int a = ss_ed(x0, y0, x2, y2, x), b = (x < x1 || x1 == x2) ? ss_ed(x0, y0, x1, y1, x) : ss_ed(x1, y1, x2, y2, x);
        if (a > b) { t = a; a = b; b = t; }
        ss_vd(x, a, b, lv, op);
    }
}
static void ss_quad(int xa, int ya, int xb, int yb, int xc, int yc, int xd, int yd, int lv, int op)
{
    ss_tri(xa, ya, xb, yb, xc, yc, lv, op);
    ss_tri(xa, ya, xc, yc, xd, yd, lv, op);
}
static void ss_text2(int x, int y, const char *s)
{
    for (; *s; s++, x += 8) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (glyphs[g] & (1u << (14 - (r * 3 + c)))) { span(x + c * 2, y + r * 2, y + r * 2 + 1, 1); span(x + c * 2 + 1, y + r * 2, y + r * 2 + 1, 1); }
    }
}
static void ss_num(int x, int y, int v, int big) { char b[8]; num(b, v); if (big) ss_text2(x, y, b); else text(x, y, b, 1); }
static int ss_atan(int dx, int dy)
{
    int best = 0, bd = -0x7fffffff;
    for (int a = 0; a < 1024; a += 8) { int d = dx * ss_cos(a) + dy * ss_sin(a); if (d > bd) { bd = d; best = a; } }
    return best;
}
static int ss_wrap(int v, int span_) { while (v < 0) v += span_; while (v >= span_) v -= span_; return v; }
static int ss_wd(int d, int span_) { if (d > span_ / 2) d -= span_; if (d < -span_ / 2) d += span_; return d; }

/* ---- game state: one at a time ---- */
typedef struct { int x, y, vx, vy; int16_t s, k; } SsObj;
#define RK_N 12
static union {
    struct { int bx, by, vx, vy, py[2], sc[2], serve, aim[2]; int16_t tx[6], ty[6]; } pd;
    struct { int bx, by, vx, vy, px, stuck, lives, score, level, aim; uint8_t row[5]; } br;
    struct { int x, y, vx, vy, ang, inv, lives, score, wave, cool, dead, thr; SsObj rock[RK_N], shot[6], deb[10]; } rk;
    struct { uint16_t bd[21]; uint8_t tex[21][10]; int piece, next, rot, x, y, fall, lines, ai_r, ai_x, dead, clear, full, rep; } bl;
    struct { int bolt, next, bx; uint32_t seed; } st;
    struct { uint32_t fb[256], zb[1024], occ[13]; int x, y, z, dir, step, segs, life, style, ball, pipe, pipes, total, state, wait; } pp;
    struct { uint32_t seed; } dm;
} ss;
/* One relocation instead of hundreds: the state is reached through a const (CSE-able) call, so each function
 * loads its address once into a register instead of carrying an absolute address per access. */
typedef __typeof__(ss) SsState;
static __attribute__((noinline, const)) SsState *ssb(void) { return &ss; }

/* ---- PADDLE ---- */
#define PD (ssb()->pd)
static void pd_serve(int d) { PD.bx = 63 << 8; PD.by = range(12, 50) << 8; PD.vx = d * 280; PD.vy = range(-170, 170); PD.serve = 45; }
static void pd_tick(void)
{
    for (int i = 0; i < 2; i++) {
        int *p = &PD.py[i];
        if (i == 0 && !SS_AI) *p += ((ss_held & SS_D) ? 420 : 0) - ((ss_held & SS_U) ? 420 : 0) + ss_encd * 640;
        else {
            int toward = (PD.vx < 0) == (i == 0);
            int ty = toward ? PD.by + PD.aim[i] : (32 << 8) + (PD.by - (32 << 8)) / 3;
            *p += clamp(ty - *p, -310, 310);
        }
        *p = clamp(*p, 7 << 8, 57 << 8);
    }
    ss_encd = 0;
    if (!(ss_t & 1)) {
        for (int k = 5; k > 0; k--) { PD.tx[k] = PD.tx[k - 1]; PD.ty[k] = PD.ty[k - 1]; }
        PD.tx[0] = (int16_t)(PD.bx >> 8); PD.ty[0] = (int16_t)(PD.by >> 8);
    }
    if (PD.serve) { PD.serve--; return; }
    PD.bx += PD.vx; PD.by += PD.vy;
    if (PD.by < 1 << 8) { PD.by = (2 << 8) - PD.by; PD.vy = -PD.vy; }
    if (PD.by > 61 << 8) { PD.by = (122 << 8) - PD.by; PD.vy = -PD.vy; }
    for (int i = 0; i < 2; i++) {
        int edge = i ? 120 << 8 : 6 << 8;
        int hit = i ? PD.vx > 0 && PD.bx >= edge && PD.bx < edge + (4 << 8) : PD.vx < 0 && PD.bx <= edge && PD.bx > edge - (4 << 8);
        int off = PD.by + 128 - PD.py[i];
        if (hit && iabs(off) <= 8 << 8) {
            int sp = iabs(PD.vx) + 14;
            if (sp > 620) sp = 620;
            PD.vx = i ? -sp : sp;
            PD.vy = off * sp / (10 << 8);
            PD.bx = edge;
            PD.aim[1 - i] = range(-6, 6) * 256;
        }
    }
    if (PD.bx < -(6 << 8) || PD.bx > 134 << 8) {
        int w = PD.bx < 0;
        if (++PD.sc[w] > 9) PD.sc[0] = PD.sc[1] = 0;
        pd_serve(w ? -1 : 1);
    }
}
static void pd_draw(void)
{
    for (int y = 2; y < 62; y += 4) span(63, y, y + 1, 1);
    for (int x = 0; x < W; x++) { px(x, 0); px(x, 63); }
    ss_num(46, 4, PD.sc[0], 1);
    ss_num(72, 4, PD.sc[1], 1);
    for (int i = 0; i < 2; i++) { int x = i ? 122 : 4, y = PD.py[i] >> 8; span(x, y - 6, y + 6, 1); span(x + 1, y - 6, y + 6, 1); }
    if (PD.serve && (PD.serve & 8)) return;
    for (int k = 1; k < 6; k++) { int lv = 14 - k * 2; ss_p(PD.tx[k], PD.ty[k], lv, 1); ss_p(PD.tx[k] + 1, PD.ty[k] + 1, lv, 1); }
    int bx = PD.bx >> 8, by = PD.by >> 8;
    span(bx, by, by + 1, 1); span(bx + 1, by, by + 1, 1);
}

/* ---- BRICKS ---- */
#define BR (ssb()->br)
static void br_level(void) { for (int r = 0; r < 5; r++) BR.row[r] = 0xff; BR.stuck = 1; }
static void br_reset(void) { BR.lives = 3; BR.score = 0; BR.level = 0; BR.px = 64 << 8; br_level(); }
static void br_tick(void)
{
    if (!SS_AI) BR.px += ((ss_held & SS_R) ? 768 : 0) - ((ss_held & SS_L) ? 768 : 0) + ss_encd * 1024;
    else {
        int tx = BR.bx;
        if (BR.vy > 0 && !BR.stuck) {
            int x = BR.bx + 128 + BR.vx * (((58 << 8) - BR.by) / BR.vy);
            x = ss_wrap(x, 252 << 8);
            if (x > 126 << 8) x = (252 << 8) - x;
            tx = x - BR.aim;
        }
        BR.px += clamp(tx - BR.px, -720, 720);
    }
    ss_encd = 0;
    BR.px = clamp(BR.px, 10 << 8, 118 << 8);
    if (BR.stuck) {
        BR.bx = BR.px - 128; BR.by = 57 << 8;
        if ((ss_fire && !SS_AI) || ++BR.stuck > 80) { BR.stuck = 0; BR.vx = range(-160, 160); BR.vy = -300; BR.aim = range(-7, 7) * 256; }
        ss_fire = 0;
        return;
    }
    int sp = 330 + BR.level * 25;
    if (sp > 500) sp = 500;
    for (int st = 0; st < 2; st++) {
        int oy = BR.by;
        BR.bx += BR.vx / 2; BR.by += BR.vy / 2;
        if (BR.bx < 0) { BR.bx = -BR.bx; BR.vx = -BR.vx; }
        if (BR.bx > 126 << 8) { BR.bx = (252 << 8) - BR.bx; BR.vx = -BR.vx; }
        if (BR.by < 8 << 8) { BR.by = (16 << 8) - BR.by; BR.vy = -BR.vy; }
        int cx = (BR.bx >> 8) + 1, cy = (BR.by >> 8) + 1, r = (cy - 9) / 5, c = cx / 16;
        if (cy >= 9 && r < 5 && c < 8 && (BR.row[r] >> c & 1)) {
            BR.row[r] &= (uint8_t)~(1u << c);
            BR.score += 5 - r;
            int ocy = (oy >> 8) + 1;
            if (ocy < 9 || (ocy - 9) / 5 != r) BR.vy = -BR.vy; else BR.vx = -BR.vx;
            break;
        }
    }
    if (BR.vy > 0 && BR.by >= 58 << 8 && BR.by < 61 << 8) {
        int off = BR.bx + 128 - BR.px;
        if (iabs(off) <= 10 << 8) {
            BR.vx = off * sp / (13 << 8);
            BR.vy = -isqrt(sp * sp - BR.vx * BR.vx);
            BR.aim = range(-7, 7) * 256;
        }
    }
    if (BR.by > 66 << 8) { BR.stuck = 1; if (--BR.lives <= 0) br_reset(); }
    int left = 0;
    for (int r = 0; r < 5; r++) left |= BR.row[r];
    if (!left) { BR.level++; br_level(); }
}
static void br_draw(void)
{
    static const uint8_t lvl[5] = {16, 12, 9, 6, 4};
    ss_num(1, 1, BR.score, 0);
    for (int i = 0; i < BR.lives; i++) { span(124 - i * 4, 2, 3, 1); span(125 - i * 4, 2, 3, 1); }
    for (int x = 0; x < W; x++) px(x, 7);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 8; c++)
            if (BR.row[r] >> c & 1)
                for (int x = c * 16 + 1; x < c * 16 + 15; x++) { span(x, 9 + r * 5, 9 + r * 5, 1); ss_vd(x, 10 + r * 5, 12 + r * 5, lvl[r], 1); }
    int p = BR.px >> 8;
    for (int x = p - 9; x <= p + 9; x++) span(x, iabs(x - p) == 9 ? 61 : 60, 62, 1);
    int bx = BR.bx >> 8, by = BR.by >> 8;
    span(bx, by, by + 1, 1); span(bx + 1, by, by + 1, 1);
}

/* ---- ROCKS ---- */
#define RK (ssb()->rk)
static const uint8_t rk_rad[4] = {0, 3, 5, 9};
static void rk_add(int x, int y, int s)
{
    for (int i = 0; i < RK_N; i++) {
        SsObj *o = &RK.rock[i];
        if (o->s) continue;
        int a = range(0, 1023), sp = 50 + (3 - s) * 28 + range(0, 30);
        o->x = x; o->y = y; o->vx = ss_cos(a) * sp / 127; o->vy = ss_sin(a) * sp / 127; o->s = (int16_t)s; o->k = (int16_t)(rnd() & 1023);
        return;
    }
}
static void rk_wave(void)
{
    int n = 2 + ++RK.wave;
    if (n > 5) n = 5;
    while (n--) { int a = range(0, 1023); rk_add(ss_wrap(RK.x + ss_cos(a) * 90, 128 << 8), ss_wrap(RK.y + ss_sin(a) * 60, 64 << 8), 3); }
}
static void rk_reset(void)
{
    RK.x = 64 << 8; RK.y = 32 << 8; RK.vx = RK.vy = 0; RK.ang = 768; RK.lives = 3; RK.score = 0; RK.wave = 0; RK.inv = 90;
    for (int i = 0; i < RK_N; i++) RK.rock[i].s = 0;
    rk_wave();
}
static void rk_burst(int x, int y, int n)
{
    for (int i = 0; i < 10 && n; i++) {
        SsObj *d = &RK.deb[i];
        if (d->s > 0) continue;
        int a = range(0, 1023), sp = range(60, 220);
        d->x = x; d->y = y; d->vx = ss_cos(a) * sp / 127; d->vy = ss_sin(a) * sp / 127; d->s = (int16_t)range(14, 30); n--;
    }
}
static void rk_tick(void)
{
    int fire = 0;
    RK.thr = 0;
    if (RK.dead) {
        int clear = 1;
        for (int i = 0; i < RK_N; i++) {
            SsObj *o = &RK.rock[i];
            int dx = ss_wd(o->x - (64 << 8), 128 << 8) >> 8, dy = ss_wd(o->y - (32 << 8), 64 << 8) >> 8;
            if (o->s && dx * dx + dy * dy < 26 * 26) clear = 0;
        }
        if (RK.dead > 1 || clear || RK.lives <= 0) RK.dead--;
        if (RK.dead == 0) { if (RK.lives <= 0) rk_reset(); RK.x = 64 << 8; RK.y = 32 << 8; RK.vx = RK.vy = 0; RK.inv = 100; }
    } else {
        if (!SS_AI) {
            RK.ang += ((ss_held & SS_R) ? 14 : 0) - ((ss_held & SS_L) ? 14 : 0) + ss_encd * 24;
            RK.thr = (ss_held & SS_U) != 0;
            fire = ss_fire || ((ss_held & SS_Y) && RK.cool == 0);
        } else {
            int best = 0x7fffffff, tx = 0, ty = 0;
            for (int i = 0; i < RK_N; i++) {
                SsObj *o = &RK.rock[i];
                if (!o->s) continue;
                int dx = ss_wd(o->x - RK.x, 128 << 8) >> 8, dy = ss_wd(o->y - RK.y, 64 << 8) >> 8, d2 = dx * dx + dy * dy;
                int px_ = dx + (o->vx - RK.vx) * 24 / 256, py_ = dy + (o->vy - RK.vy) * 24 / 256, p2 = px_ * px_ + py_ * py_ - rk_rad[o->s] * 20;
                if (p2 < d2) d2 = p2 < 0 ? 0 : p2;
                if (d2 < best) { int lead = isqrt(d2) / 3; best = d2; tx = dx + o->vx * lead / 256; ty = dy + o->vy * lead / 256; }
            }
            if (best < 0x7fffffff) {
                int diff = ((ss_atan(tx, ty) - RK.ang + 512) & 1023) - 512;
                RK.ang += clamp(diff, -12, 12);
                fire = iabs(diff) < 20 && RK.cool == 0;
                RK.thr = best > 45 * 45 && (ss_t & 63) < 10;
            }
        }
        RK.ang &= 1023;
        if (RK.thr) { RK.vx += ss_cos(RK.ang) / 3; RK.vy += ss_sin(RK.ang) / 3; }
        RK.vx -= RK.vx / 48; RK.vy -= RK.vy / 48;
        RK.vx = clamp(RK.vx, -500, 500); RK.vy = clamp(RK.vy, -500, 500);
        RK.x = ss_wrap(RK.x + RK.vx, 128 << 8); RK.y = ss_wrap(RK.y + RK.vy, 64 << 8);
        if (fire && RK.cool == 0)
            for (int i = 0; i < 6; i++) {
                SsObj *b = &RK.shot[i];
                if (b->s > 0) continue;
                b->x = RK.x + ss_cos(RK.ang) * 10; b->y = RK.y + ss_sin(RK.ang) * 10;
                b->vx = RK.vx + ss_cos(RK.ang) * 6; b->vy = RK.vy + ss_sin(RK.ang) * 6; b->s = 34; RK.cool = 9;
                break;
            }
    }
    ss_fire = 0; ss_encd = 0;
    if (RK.cool) RK.cool--;
    if (RK.inv) RK.inv--;
    for (int i = 0; i < 6; i++) { SsObj *b = &RK.shot[i]; if (b->s > 0) { b->s--; b->x = ss_wrap(b->x + b->vx, 128 << 8); b->y = ss_wrap(b->y + b->vy, 64 << 8); } }
    for (int i = 0; i < 10; i++) { SsObj *d = &RK.deb[i]; if (d->s > 0) { d->s--; d->x += d->vx; d->y += d->vy; } }
    int any = 0;
    for (int i = 0; i < RK_N; i++) {
        SsObj *o = &RK.rock[i];
        if (!o->s) continue;
        any = 1;
        o->x = ss_wrap(o->x + o->vx, 128 << 8); o->y = ss_wrap(o->y + o->vy, 64 << 8); o->k = (int16_t)((o->k + 3) & 1023);
        int r = rk_rad[o->s] + 1;
        for (int j = 0; j < 6; j++) {
            SsObj *b = &RK.shot[j];
            if (b->s <= 0) continue;
            int dx = ss_wd(b->x - o->x, 128 << 8) >> 8, dy = ss_wd(b->y - o->y, 64 << 8) >> 8;
            if (dx * dx + dy * dy <= r * r) {
                int s = o->s;
                b->s = 0; o->s = 0; RK.score += (4 - s) * 10;
                rk_burst(o->x, o->y, 3);
                if (s > 1) { rk_add(o->x, o->y, s - 1); rk_add(o->x, o->y, s - 1); }
                break;
            }
        }
        if (o->s && !RK.dead && !RK.inv) {
            int dx = ss_wd(RK.x - o->x, 128 << 8) >> 8, dy = ss_wd(RK.y - o->y, 64 << 8) >> 8;
            if (dx * dx + dy * dy <= (r + 2) * (r + 2)) { RK.dead = 80; RK.lives--; rk_burst(RK.x, RK.y, 10); }
        }
    }
    if (!any) rk_wave();
}
static void rk_draw(void)
{
    for (int i = 0; i < 28; i++) { uint32_t h = ss_h((uint32_t)i * 31u + 5u); px(h & 127, (h >> 8) & 63); }
    ss_num(1, 1, RK.score, 0);
    for (int i = 0; i < RK.lives; i++) { int x = 123 - i * 5; line(x, 5, x + 2, 1); line(x + 2, 1, x + 4, 5); }
    for (int i = 0; i < RK_N; i++) {
        SsObj *o = &RK.rock[i];
        if (!o->s) continue;
        int r = rk_rad[o->s], cx = o->x >> 8, cy = o->y >> 8, lx = 0, ly = 0, fx = 0, fy = 0;
        for (int v = 0; v <= 9; v++) {
            int a = o->k + v * 1024 / 9, rr = r * (90 + (int)(ss_h((uint32_t)(i * 16 + (v % 9)) + (uint32_t)o->s * 7u) % 38u)) / 127;
            int x = cx + ss_cos(a) * rr / 127, y = cy + ss_sin(a) * rr / 127;
            if (v) line(lx, ly, x, y); else { fx = x; fy = y; }
            lx = x; ly = y;
        }
        (void)fx; (void)fy;
        if (o->s == 3) { px(cx - 2, cy - 1); px(cx + 2, cy + 2); }
    }
    for (int i = 0; i < 6; i++) { SsObj *b = &RK.shot[i]; if (b->s > 0) { px(b->x >> 8, b->y >> 8); px((b->x >> 8) + 1, b->y >> 8); } }
    for (int i = 0; i < 10; i++) { SsObj *d = &RK.deb[i]; if (d->s > 0) ss_p(d->x >> 8, d->y >> 8, d->s > 10 ? 16 : 8, 1); }
    if (RK.dead || (RK.inv && (ss_t & 4))) return;
    int x = RK.x >> 8, y = RK.y >> 8, a = RK.ang;
    int nx = x + ss_cos(a) * 5 / 127, ny = y + ss_sin(a) * 5 / 127;
    int lx = x + ss_cos(a + 400) * 4 / 127, ly = y + ss_sin(a + 400) * 4 / 127;
    int rx = x + ss_cos(a - 400) * 4 / 127, ry = y + ss_sin(a - 400) * 4 / 127;
    int bx = x - ss_cos(a) * 2 / 127, by = y - ss_sin(a) * 2 / 127;
    line(nx, ny, lx, ly); line(nx, ny, rx, ry); line(lx, ly, bx, by); line(rx, ry, bx, by);
    if (RK.thr && (ss_t & 2)) line(bx, by, x - ss_cos(a) * 7 / 127, y - ss_sin(a) * 7 / 127);
}

/* ---- BLOCKS: an eight-piece set of two to five cells, in a 10x21 well ---- */
#define BL (ssb()->bl)
#define BL_W 10
#define BL_H 21
static const uint16_t bl_shape[8] = {0x018, 0x038, 0x019, 0x01b, 0x01e, 0x039, 0x0b8, 0x03d};
static int bl_rot(int m, int r)
{
    while (r-- > 0) { int n = 0; for (int i = 0; i < 9; i++) if (m >> i & 1) n |= 1 << ((i % 3) * 3 + 2 - i / 3); m = n; }
    return m;
}
static int bl_fits(const uint16_t *bd, int m, int x, int y)
{
    for (int i = 0; i < 9; i++) {
        if (!(m >> i & 1)) continue;
        int cx = x + i % 3, cy = y + i / 3;
        if (cx < 0 || cx >= BL_W || cy >= BL_H) return 0;
        if (cy >= 0 && (bd[cy] >> cx & 1)) return 0;
    }
    return 1;
}
static int bl_cur(void) { return bl_rot(bl_shape[BL.piece], BL.rot); }
static void bl_plan(void)
{
    int best = -0x7fffffff;
    BL.ai_r = BL.rot; BL.ai_x = BL.x;
    for (int r = 0; r < 4; r++) {
        int m = bl_rot(bl_shape[BL.piece], r);
        for (int x = -2; x < BL_W; x++) {
            if (!bl_fits(BL.bd, m, x, BL.y)) continue;
            int y = BL.y;
            while (bl_fits(BL.bd, m, x, y + 1)) y++;
            uint16_t tmp[BL_H];
            int lines = 0, n = BL_H - 1;
            for (int i = 0; i < BL_H; i++) tmp[i] = BL.bd[i];
            for (int i = 0; i < 9; i++) if ((m >> i & 1) && y + i / 3 >= 0) tmp[y + i / 3] |= (uint16_t)(1u << (x + i % 3));
            for (int i = BL_H - 1; i >= 0; i--) { if (tmp[i] == 0x3ff) lines++; else tmp[n--] = tmp[i]; }
            while (n >= 0) tmp[n--] = 0;
            int agg = 0, holes = 0, bump = 0, prev = -1, top = 0;
            for (int cx = 0; cx < BL_W; cx++) {
                int h = 0;
                for (int cy = 0; cy < BL_H; cy++) if (tmp[cy] >> cx & 1) { if (!h) h = BL_H - cy; } else if (h) holes++;
                agg += h; if (h > top) top = h;
                if (prev >= 0) bump += iabs(h - prev);
                prev = h;
            }
            int sc = lines * 760 - agg * 510 - holes * 360 - bump * 184 - (top > 14 ? (top - 14) * 800 : 0);
            if (sc > best) { best = sc; BL.ai_r = r; BL.ai_x = x; }
        }
    }
}
static void bl_spawn(void)
{
    BL.piece = BL.next; BL.next = (int)(rnd() % 8u); BL.rot = 0; BL.x = 3; BL.y = -1; BL.fall = 0;
    if (!bl_fits(BL.bd, bl_cur(), BL.x, BL.y)) { BL.dead = 70; return; }
    bl_plan();
}
static void bl_reset(void)
{
    for (int y = 0; y < BL_H; y++) { BL.bd[y] = 0; for (int x = 0; x < BL_W; x++) BL.tex[y][x] = 0; }
    BL.lines = 0; BL.dead = 0; BL.clear = 0; BL.next = (int)(rnd() % 8u);
    bl_spawn();
}
static int bl_try(int dx, int dr)
{
    int m = bl_rot(bl_shape[BL.piece], (BL.rot + dr) & 3);
    for (int kick = 0; kick < 3; kick++) {
        int k = kick == 0 ? 0 : kick == 1 ? -1 : 1;
        if (dr == 0 && kick) break;
        if (bl_fits(BL.bd, m, BL.x + dx + k, BL.y)) { BL.x += dx + k; BL.rot = (BL.rot + dr) & 3; return 1; }
    }
    return 0;
}
static void bl_lock(void)
{
    int m = bl_cur();
    for (int i = 0; i < 9; i++) {
        if (!(m >> i & 1)) continue;
        int cx = BL.x + i % 3, cy = BL.y + i / 3;
        if (cy < 0) { BL.dead = 70; return; }
        BL.bd[cy] |= (uint16_t)(1u << cx); BL.tex[cy][cx] = (uint8_t)(BL.piece + 1);
    }
    BL.full = 0;
    for (int y = 0; y < BL_H; y++) if (BL.bd[y] == 0x3ff) BL.full |= 1 << y;
    if (BL.full) BL.clear = 14; else bl_spawn();
}
static void bl_tick(void)
{
    if (BL.dead) { if (--BL.dead == 0) bl_reset(); return; }
    if (BL.clear) {
        if (--BL.clear) return;
        int n = BL_H - 1;
        for (int y = BL_H - 1; y >= 0; y--) {
            if (BL.full >> y & 1) { BL.lines++; continue; }
            BL.bd[n] = BL.bd[y];
            for (int x = 0; x < BL_W; x++) BL.tex[n][x] = BL.tex[y][x];
            n--;
        }
        for (; n >= 0; n--) { BL.bd[n] = 0; for (int x = 0; x < BL_W; x++) BL.tex[n][x] = 0; }
        if (BL.lines > 999) BL.lines = 0;
        bl_spawn();
        return;
    }
    int drop = 0, every = 30 - BL.lines / 5;
    if (every < 5) every = 5;
    if (SS_AI) {
        if (!(ss_t % 3)) {
            if (BL.rot != BL.ai_r) { if (!bl_try(0, 1)) BL.ai_r = BL.rot; }
            else if (BL.x != BL.ai_x) { if (!bl_try(BL.x < BL.ai_x ? 1 : -1, 0)) BL.ai_x = BL.x; }
        }
        if (BL.rot == BL.ai_r && BL.x == BL.ai_x) every = 2;
    } else {
        if (ss_encd) { bl_try(ss_encd > 0 ? 1 : -1, 0); ss_encd = 0; }
        if (ss_held & (SS_L | SS_R)) { if (++BL.rep > 14 && !(BL.rep & 3)) bl_try((ss_held & SS_R) ? 1 : -1, 0); } else BL.rep = 0;
        if (ss_held & SS_D) every = 2;
    }
    if (++BL.fall >= every) { BL.fall = 0; drop = 1; }
    if (drop) { if (bl_fits(BL.bd, bl_cur(), BL.x, BL.y + 1)) BL.y++; else bl_lock(); }
}
static void bl_cell(int X, int Y, int t, int sz)
{
    for (int k = 0; k < sz - 1; k++) span(X + k, Y, Y + sz - 2, 1);
    if (sz > 3 && (t & 1)) px_off(X + 1, Y + 1);
}
static void bl_draw(void)
{
    int x0 = 49;
    for (int x = 0; x < W; x++) if (x < 44 || x > 84) ss_vd(x, 0, 63, (x & 4) ? 1 : 0, 1);
    for (int x = 44; x < 47; x++) span(x, 0, 63, 0);
    for (int x = 81; x < 85; x++) span(x, 0, 63, 0);
    span(47, 0, 63, 1); span(80, 0, 63, 1);
    for (int y = 0; y < BL_H; y++)
        for (int x = 0; x < BL_W; x++) {
            if (!(BL.bd[y] >> x & 1)) continue;
            if (BL.clear && (BL.full >> y & 1)) { if (BL.clear & 2) for (int k = 0; k < 3; k++) span(x0 + x * 3 + k, 1 + y * 3, 3 + y * 3, 1); continue; }
            if (BL.dead && y > BL.dead / 3) { ss_vd(x0 + x * 3, 1 + y * 3, 3 + y * 3, 6, 1); ss_vd(x0 + x * 3 + 1, 1 + y * 3, 3 + y * 3, 6, 1); continue; }
            bl_cell(x0 + x * 3, 1 + y * 3, BL.tex[y][x], 3);
        }
    if (!BL.dead && !BL.clear) {
        int m = bl_cur(), gy = BL.y;
        while (bl_fits(BL.bd, m, BL.x, gy + 1)) gy++;
        for (int i = 0; i < 9; i++) {
            if (!(m >> i & 1)) continue;
            int cx = x0 + (BL.x + i % 3) * 3, cy = 1 + (BL.y + i / 3) * 3;
            if (cy >= 0) bl_cell(cx, cy, BL.piece + 1, 3);
            int gy3 = 1 + (gy + i / 3) * 3;
            if (gy != BL.y) { px(cx + 1, gy3 + 2); }
        }
    }
    for (int x = 8; x < 36; x++) { span(x, 6, 36, 0); px(x, 6); px(x, 36); }
    for (int x = 88; x < 122; x++) { span(x, 6, 36, 0); px(x, 6); px(x, 36); }
    span(8, 6, 36, 1); span(35, 6, 36, 1); span(88, 6, 36, 1); span(121, 6, 36, 1);
    text(14, 10, "NEXT", 1);
    int m = bl_shape[BL.next];
    for (int i = 0; i < 9; i++) if (m >> i & 1) bl_cell(16 + (i % 3) * 4, 19 + (i / 3) * 4, BL.next + 1, 4);
    text(96, 10, "LINES", 1);
    ss_num(96, 19, BL.lines, 1);
}

/* ---- LIGHTHOUSE ---- */
static void lh_draw(void)
{
    int t = ss_t, hz = 40, p = (t * 5) & 1023, c = ss_cos(p), s = ss_sin(p), lx = 106, ly = 9;
    for (int i = 0; i < 46; i++) { uint32_t h = ss_h((uint32_t)i + 77u); int x = (int)(h & 127), y = (int)((h >> 8) % 36u); if (((h >> 16) + (uint32_t)t / 6) % 19u) px(x, y); }
    ss_ell(22, 10, 5, 5, 16, 1, 0); ss_ell(25, 8, 5, 5, 16, 0, 0);
    int L = iabs(c) * 150 / 127, dir = c < 0 ? -1 : 1, lv0 = s > 0 ? 10 : 5;
    if (L > 6)
        for (int k = 0; k < 6; k++) {
            int a = L * k / 6, b = L * (k + 1) / 6, wa = 1 + (4 + (s > 0 ? s / 12 : 0)) * k / 6 + a / 14, wb = 1 + (4 + (s > 0 ? s / 12 : 0)) * (k + 1) / 6 + b / 14;
            int xa = lx + dir * a, xb = lx + dir * b, ya = ly + a / 14, yb = ly + b / 14;
            ss_quad(xa, ya - wa, xb, yb - wb, xb, yb + wb, xa, ya + wa, lv0 - k * lv0 / 6, 1);
        }
    for (int y = hz; y < H; y++) {
        int d = y - hz + 2;
        for (int x = 0; x < W; x++) {
            int v = ss_sin(x * 40 / d + (y * 97 & 1023) + t * (y & 1 ? 3 : -2)) + ss_sin(x * 9 + y * 50 - t * 2) / 2;
            int lim = 150 - (iabs(x - 22) < 6 ? 60 : 0);
            if (c < 0 && L > 30 && x < lx && x > lx - L) lim -= 50 * (y - hz) / 24 + 10;
            if (v > lim) px(x, y);
        }
    }
    /* the headland */
    for (int x = 74; x < W; x++) {
        int top = x < 92 ? 63 - (x - 74) * 30 / 18 + ss_sin(x * 60) / 60 : 33 + ss_sin(x * 45) / 70;
        span(x, top, 63, 0);
        ss_vd(x, top + 1, 63, 2 + (int)(ss_h((uint32_t)x / 3u) & 1u), 1);
        px(x, top);
        if (x < 92 && (ss_t + x * 7) % 23 < 6) { px(x - 2, 63 - (x - 74) * 3 / 18); }
    }
    for (int y = 14; y <= 33; y++) {
        int hw = 2 + (y - 14) * 3 / 19, band = ((y - 14) / 4) & 1;
        for (int x = lx - hw; x <= lx + hw; x++) {
            span(x, y, y, 0);
            int lv = band ? 16 : 5;
            if (x > lx + hw / 2) lv /= 2;
            ss_p(x, y, lv, 1);
        }
    }
    for (int x = lx - 5; x <= lx + 5; x++) { px(x, 13); if (!(x & 1)) px(x, 12); }
    for (int x = lx - 3; x <= lx + 3; x++) span(x, 7, 11, 0);
    span(lx - 3, 7, 11, 1); span(lx + 3, 7, 11, 1);
    ss_tri(lx - 4, 6, lx + 4, 6, lx, 2, 16, 1); px(lx, 1);
    ss_ell(lx, ly, 2, 1, 16, 1, 0);
    if (s > 50) { int r = (s - 50) * 16 / 77; for (int k = r; k > 0; k -= 2) ss_ell(lx, ly, k, k * 3 / 4, 16 - k * 14 / (r + 1), 1, 0); }
    for (int x = 114; x < 125; x++) { span(x, 26, 33, 0); ss_vd(x, 27, 33, 4, 1); }
    ss_tri(112, 26, 126, 26, 119, 21, 8, 1);
    span(117, 29, 30, 1); span(118, 29, 30, 1);
}

/* ---- STORM ---- */
#define ST (ssb()->st)
static void st_tick(void)
{
    if (ST.bolt) ST.bolt--;
    if (--ST.next <= 0) { ST.bolt = 16; ST.seed = rnd() | 1u; ST.next = range(80, 280); ST.bx = range(14, 114); }
}
static int st_ground(int x) { return 54 + ss_sin(x * 6 + 100) / 40 + ss_sin(x * 17) / 90; }
static void st_draw(void)
{
    int t = ss_t, fl = ST.bolt > 11 ? 3 : ST.bolt > 7 ? 2 : ST.bolt > 3 ? 1 : 0;
    for (int x = 0; x < W; x++) ss_vd(x, 0, 63, fl * 3, 1);
    /* two layers of rolling cloud: puffs with a lit crown and a dark belly */
    for (int layer = 0; layer < 2; layer++) {
        int sp = layer ? t / 2 : t / 5, n = layer ? 9 : 7, step = layer ? 19 : 24;
        for (int i = 0; i < n; i++) {
            uint32_t h = ss_h((uint32_t)(i * 7 + layer * 50));
            int cx = ss_wrap(i * step + sp, n * step) - 16, cy = (layer ? 9 : 2) + (int)((h >> 3) % 11u) + ss_sin(t * 2 + i * 150) / 64;
            int rx = (layer ? 6 : 10) + (int)((h >> 9) % 12u), ry = rx * 3 / 4;
            ss_ell(cx, cy, rx, ry, 16, 0, 0);
            ss_ell(cx, cy, rx, ry, (layer ? 3 : 2) + fl * 3, 1, 0);
            ss_ell(cx - 2, cy - 2, rx - 4, ry - 3, (layer ? 5 : 3) + fl * 3, 1, 1);
            for (int a = 512 + 40; a < 1024 - 40; a += 10) ss_p(cx + ss_cos(a) * rx / 127, cy + ss_sin(a) * ry / 127, layer ? 16 : 8, 1);
        }
    }
    int wind = 2;
    for (int i = 0; i < 80; i++) {
        uint32_t h = ss_h((uint32_t)i * 13u + 1u);
        int v = 3 + (int)(h & 1u), y = (int)(((h >> 8) + (uint32_t)(t * v)) % 72u) - 6, x = (int)((h >> 16) % 150u) - (y * wind) / 4;
        x = ss_wrap(x, 150) - 10;
        if (y < 6) continue;
        int g = st_ground(x);
        if (y >= g) { if (y < g + 4) { px(x - 1, g - 1); px(x + 1, g - 1); } continue; }
        ss_ln(x, y, x - wind / 2, y + 3, 16, 1, 1);
    }
    if (ST.bolt && (ST.bolt > 10 || (ST.bolt & 2))) {
        uint32_t s = ST.seed;
        int x = ST.bx, y = 10, bx = 0, by = 0;
        for (int n = 0; n < 16 && y < st_ground(x); n++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int nx = x + (int)(s % 11u) - 5, ny = y + 3 + (int)((s >> 8) % 4u);
            ss_ln(x, y, nx, ny, 16, 1, n < 6 ? 2 : 1);
            if (n == 3) { bx = nx; by = ny; }
            x = nx; y = ny;
        }
        for (int n = 0; n < 5; n++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int nx = bx + 3 + (int)(s % 5u), ny = by + 2 + (int)((s >> 8) % 4u);
            ss_ln(bx, by, nx, ny, 16, 1, 1);
            bx = nx; by = ny;
        }
    }
    for (int x = 0; x < W; x++) {
        int g = st_ground(x);
        span(x, g, 63, 0);
        if (fl) ss_vd(x, g, g + 1, 8, 1); else px(x, g);
    }
    /* a bare tree and a cottage with a lamp in the window */
    int tg = st_ground(26);
    for (int k = 0; k < 2; k++) span(26 + k, tg - 14, tg, 0);
    ss_ln(26, tg - 9, 20, tg - 16, 0, 0, 1); ss_ln(27, tg - 11, 33, tg - 18, 0, 0, 1); ss_ln(26, tg - 13, 25, tg - 21, 0, 0, 1);
    ss_ln(22, tg - 13, 18, tg - 15, 0, 0, 1); ss_ln(31, tg - 15, 35, tg - 15, 0, 0, 1);
    if (fl) {
        ss_ln(26, tg - 9, 20, tg - 16, 16, 1, 1); ss_ln(27, tg - 11, 33, tg - 18, 16, 1, 1); ss_ln(26, tg - 13, 25, tg - 21, 16, 1, 1);
        span(25, tg - 14, tg, 1);
    }
    int hg = st_ground(96);
    for (int x = 88; x < 104; x++) { span(x, hg - 8, hg, 0); if (fl) ss_vd(x, hg - 8, hg, 4, 1); }
    span(88, hg - 8, hg, 1); span(103, hg - 8, hg, 1);
    ss_tri(86, hg - 8, 106, hg - 8, 96, hg - 15, 0, 0);
    for (int x = 86; x <= 106; x++) px(x, hg - 8 - (10 - iabs(x - 96)) * 7 / 10);
    for (int x = 99; x < 101; x++) span(100 + (x - 99), hg - 13, hg - 11, 0);
    int lamp = 12 + ss_sin(t * 9) / 60;
    for (int x = 91; x < 94; x++) ss_vd(x, hg - 6, hg - 4, lamp, 1);
    if (ST.bolt > 13) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
}

/* ---- SLIDE: down an endless white waterslide, toward the light ---- */
static int sl_path(int s) { return ss_sin(s / 4) * 4 + ss_sin(s / 7 + 300) * 2; }
static void sl_draw(void)
{
    int t = ss_t, s0 = t * 20, hz = 17 + ss_sin(s0 / 10) / 32;
    int x0 = sl_path(s0), slope = sl_path(s0 + 64) - x0;
    int far = clamp(64 + (sl_path(s0 + 682) - x0 - slope * 682 / 64) * 3 / 24, 8, 120);
    /* the light at the end, and its rays */
    for (int k = 0; k < 9; k++) {
        int a = k * 114 + t + ss_sin(t * 2 + k * 90) / 8, x1 = far + ss_cos(a) * 100 / 127, y1 = hz - 2 + ss_sin(a) * 70 / 127;
        if (y1 < hz - 2) ss_tri(far, hz + 2, x1 - 5, y1, x1 + 5, y1, 2 + (k & 1), 1);
    }
    for (int r = 20; r > 0; r -= 2) ss_ell(far, hz + 2, r + 3, r / 2 + 1, r < 5 ? 16 : 14 - r * 2 / 3, 1, 0);
    for (int i = 0; i < 30; i++) { uint32_t h = ss_h((uint32_t)i * 7u + 3u); int x = (int)(h & 127), y = (int)((h >> 8) % 64u); ss_p(x, y, (h >> 20 & 1) ? 16 : 8, 1); }
    for (int y = hz + 3; y < H; y++) {
        int d = y - hz, z = 2048 / d, sz = s0 + z;
        int c = 64 + (sl_path(sz) - x0 - slope * z / 64) * d / 24, hw = 27 * d / 24 + 1, wall = d / 4 + 1;
        int band = (sz >> 7) & 1, fog = d < 5 ? (5 - d) * 3 : 0;
        for (int x = c - hw - 4; x <= c + hw + 4; x++) {
            if ((unsigned)x >= W) continue;
            int u = (x - c) * 64 / hw, au = iabs(u), lv;
            if (au > 66) {
                /* the outside of the trough, below the lip: on the near side of a bend it shows */
                int o = (au - 66) * hw / 64;
                lv = o < wall ? 3 : 0;
                if (lv) { if (dither_on(x, y, lv)) px(x, y); else px_off(x, y); }
                continue;
            }
            if (au > 64) lv = 0;
            else if (au > 58) lv = 16;
            else if (au > 55) lv = 2;
            else if (au > 37) lv = 7 - (au - 37) / 5 + band * 3;
            else if (au > 33) lv = 0;
            else lv = 10 + band * 6 - ((au > 14 && au < 18 && ((sz >> 4) & 3) == 0) ? 8 : 0);
            lv += fog;
            if (lv > 16) lv = 16;
            if (dither_on(x, y, lv)) px(x, y); else px_off(x, y);
        }
    }
    for (int i = 0; i < 10; i++) {
        uint32_t h = ss_h((uint32_t)i * 13u + 1u);
        int z = 60 + (int)((h + (uint32_t)t * 9u) % 500u), d = 2048 / z, y = hz + d;
        int c = 64 + (sl_path(s0 + z) - x0 - slope * z / 64) * d / 24, u = (int)(h >> 12 & 63u) - 32;
        int x = c + u * (27 * d / 24) / 64;
        px_off(x, y); px_off(x, y + 1);
        if (d > 20) { px_off(x + 1, y); px_off(x, y + 2); }
    }
}

/* ---- FOREST ---- */
static void fr_tree(int x, int base, int h, int lv, int op)
{
    for (int k = 0; k < 3; k++) {
        int ty = base - h + k * h / 4, w = h / 5 + k * h / 8;
        ss_tri(x - w, ty + h / 3, x + w, ty + h / 3, x, ty, lv, op);
    }
    ss_vd(x, base - h / 4, base, lv, op);
}
static void fr_draw(void)
{
    int t = ss_t;
    for (int x = 0; x < W; x++) {
        int g = 11 - iabs(x - 72) / 20;
        for (int b = 0; b < 6; b++) ss_vd(x, b * 8, b * 8 + 7, g - b, 1);
    }
    for (int k = 0; k < 3; k++) {
        int x0 = ss_wrap(k * 61 + t / 5, 190) - 40;
        ss_quad(x0, 0, x0 + 7, 0, x0 + 40, 63, x0 + 22, 63, 13 + ss_sin(t * 2 + k * 300) / 64, 1);
    }
    int s1 = t / 8;
    for (int i = s1 / 11 - 1; i < s1 / 11 + 14; i++) {
        uint32_t h = ss_h((uint32_t)i * 3u + 1u);
        int x = i * 11 + (int)(h & 7u) - s1, hh = 16 + (int)(h >> 8 & 7u);
        fr_tree(x, 48, hh, 16, 0);
        fr_tree(x, 48, hh, 3, 1);
    }
    for (int x = 0; x < W; x++) { span(x, 48, 63, 0); ss_vd(x, 42, 47, 4, 1); ss_vd(x, 48, 63, 2, 1); }
    int s2 = t / 3;
    for (int i = s2 / 30 - 1; i < s2 / 30 + 6; i++) {
        uint32_t h = ss_h((uint32_t)i * 7u + 2u);
        int x = i * 30 + (int)(h & 15u) - s2, hh = 30 + (int)(h >> 8 & 7u);
        fr_tree(x, 58, hh, 16, 0);
    }
    for (int x = 0; x < W; x++) span(x, 56 + ss_sin(x * 13 + t) / 90, 63, 0);
    int s3 = t;
    for (int i = s3 / 56 - 1; i < s3 / 56 + 4; i++) {
        uint32_t h = ss_h((uint32_t)i * 11u + 5u);
        int x = i * 56 + (int)(h & 31u) - s3, w = 3 + (int)(h >> 8 & 3u);
        for (int k = -w; k <= w; k++) span(x + k, 0, 63, 0);
        for (int y = 0; y < H; y++) if (((y + (int)(h >> 4)) % 9) < 7) ss_p(x - w, y, 12, 1);
        ss_ln(x + w, 14 + (int)(h & 7u), x + w + 10, 5, 0, 0, 2);
        ss_ln(x - w, 24 + (int)(h & 7u), x - w - 8, 17, 0, 0, 2);
    }
    for (int i = 0; i < 18; i++) {
        uint32_t h = ss_h((uint32_t)i * 23u + 6u);
        int x = ss_wrap((int)(h & 255u) - t * 2 / 3, 160) - 16, sway = ss_sin(t * 3 + i * 90) / 70;
        for (int k = -3; k <= 3; k++) ss_ln(x, 63, x + k * 3 + sway, 58 + iabs(k), 6, 1, 1);
    }
    for (int i = 0; i < 9; i++) {
        int fx = 64 + ss_sin(t * 2 + i * 113) * 55 / 127 + ss_sin(t * 5 + i * 37) * 6 / 127;
        int fy = 38 + ss_sin(t * 3 + i * 251) * 18 / 127 + ss_sin(t * 7 + i * 71) * 3 / 127;
        int ph = (t + i * 47) % 150;
        if (ph > 90) continue;
        int b = ph < 15 ? ph : ph > 75 ? 90 - ph : 15;
        ss_ell(fx, fy, 2, 2, 16, 0, 0);
        ss_ell(fx, fy, 2, 2, b / 2, 1, 0);
        px(fx, fy);
        if (b > 10) { px(fx + 1, fy); px(fx - 1, fy); px(fx, fy + 1); px(fx, fy - 1); }
    }
}


/* ---- SHADOWS: figures in the light, dissolving into one another ---- */
static void sh_rain(int lv, int t, int shelter)
{
    for (int i = 0; i < 46; i++) {
        uint32_t h = ss_h((uint32_t)i * 19u + 7u);
        int y = (int)(((h >> 8) + (uint32_t)t * 3u) % 70u) - 6, x = ss_wrap((int)(h % 140u) - y / 3, 140) - 6;
        if (y < 56 && !(shelter && y > 18 && x > 40 && x < 79)) ss_ln(x, y, x - 1, y + 4, lv, 0, 1);
    }
    for (int k = 0; k < 4; k++) {
        uint32_t h = ss_h((uint32_t)(k + (t + k * 23) / 40 * 4));
        int r = (t + k * 23) % 40, cx = (int)(h % 120u) + 4;
        for (int a = 0; a < 512; a += 16) ss_p(cx + ss_cos(a) * r / 6 / 127, 58 + ss_sin(a) * r / 30 / 127, r < 30 ? lv : lv / 2, 0);
    }
}
static void sh_girl(int lv, int t)
{
    int gx = 52, sway = ss_sin(t * 4) / 64;
    ss_ell(gx, 34, 3, 3, lv, 0, 0);
    ss_ln(gx + 2, 33, gx + 5 + sway, 37, lv, 0, 2);
    ss_tri(gx - 2, 38, gx + 2, 38, gx, 37, lv, 0);
    ss_quad(gx - 2, 38, gx + 2, 38, gx + 7 + sway, 51, gx - 7 + sway, 51, lv, 0);
    ss_ln(gx - 2, 51, gx - 2, 57, lv, 0, 2);
    ss_ln(gx + 2, 51, gx + 3, 57, lv, 0, 2);
    ss_ln(gx + 2, 40, gx + 6, 43, lv, 0, 2);
    int ux = gx + 7, uy = 22 + ss_sin(t * 3) / 90;
    ss_ln(gx + 6, 43, ux, uy, lv, 0, 1);
    ss_ln(gx + 6, 43, gx + 5, 45, lv, 0, 1);
    ss_ell(ux, uy, 17, 10, lv, 0, 1);
    for (int k = 0; k < 4; k++) ss_ell(ux - 13 + k * 9, uy + 1, 4, 2, lv, 1, 1);
    ss_ln(ux, uy - 10, ux, uy - 12, lv, 0, 1);
    for (int i = 0; i < 6; i++) {
        int a = (t * 3 + i * 9) % 18, side = i & 1 ? 1 : -1;
        ss_p(ux + side * (17 + a / 3), uy + 1 + a * a / 30, lv, 0);
    }
}
static void sh_balloon(int lv, int t)
{
    int y = 44 - t / 9, x = 60 + t / 12 + ss_sin(t * 2) / 16;
    ss_ell(x, y, 7, 9, lv, 0, 0);
    ss_tri(x - 2, y + 10, x + 2, y + 10, x, y + 8, lv, 0);
    px(x - 3, y - 4); px(x - 3, y - 5); px(x - 2, y - 6);
    int lx = x, ly = y + 10;
    for (int k = 1; k <= 8; k++) { int nx = x + ss_sin(t * 6 - k * 90) * k / 254, ny = y + 10 + k * 3; ss_ln(lx, ly, nx, ny, lv, 0, 1); lx = nx; ly = ny; }
    for (int k = 0; k < 3; k++) {
        int bx = 140 - t / 2 + k * 14, by = 14 + k * 4, f = ss_sin(t * 20 + k * 300) / 50;
        ss_ln(bx - 3, by - f, bx, by, lv, 0, 1); ss_ln(bx, by, bx + 3, by - f, lv, 0, 1);
    }
}
static void sh_birds(int lv, int t)
{
    for (int i = 0; i < 7; i++) {
        int row = i < 4 ? i : i - 3, x = t / 2 - row * 9 + (i < 4 ? 0 : 3);
        int y = 30 + (i < 4 ? -row * 5 : row * 5) + ss_sin(t * 3 + i * 99) / 60, f = ss_sin(t * 14 + i * 130);
        int ey = y - 2 - f * 3 / 127, ty = y - f * 6 / 127;
        ss_ell(x, y, 3, 1, lv, 0, 0);
        ss_ln(x - 1, y, x - 4, ey, lv, 0, 2); ss_ln(x - 4, ey, x - 9, ty, lv, 0, 1);
        ss_ln(x - 1, y, x + 2, ey - 1, lv, 0, 2); ss_ln(x + 2, ey - 1, x + 6, ty - 1, lv, 0, 1);
        ss_ln(x + 3, y - 1, x + 5, y - 1, lv, 0, 1);
    }
}
static void sh_fin(int lv, int t)
{
    int sea = 44, fx = t / 3 + 10;
    for (int x = 0; x < W; x++) ss_vd(x, sea + ss_sin(x * 16 + t * 6) / 64, 63, lv, 0);
    if (t > 150 && t < 200) { int a = (t - 150) * 10, jx = 96 + (t - 150) / 2, jy = sea - ss_sin(a) * 14 / 127; ss_ell(jx, jy, 5, 2, lv, 0, 0); ss_tri(jx - 5, jy, jx - 9, jy - 3, jx - 9, jy + 3, lv, 0); }
    ss_tri(fx - 9, sea, fx + 2, sea, fx - 3, sea - 14, lv, 0);
    ss_tri(fx - 3, sea - 14, fx + 2, sea, fx + 1, sea - 6, lv, 0);
    for (int k = 1; k < 6; k++) { int w = k * 4; ss_p(fx - 9 - w, sea + k / 2, lv, 1); ss_p(fx - 9 - w, sea - 1 + k / 2, lv, 1); ss_p(fx + 3 - w / 2, sea + k, lv, 1); }
    for (int x = 0; x < W; x += 3) ss_p(x + (t / 4) % 3, sea + 6 + ((x * 7) & 7), lv, 1);
}
static void sh_scene(int s, int lv, int t, int lt)
{
    if (lv <= 0) return;
    if (s == 0) sh_rain(lv, t, 0);
    else if (s == 1) { sh_rain(lv, t, 1); sh_girl(lv, t); }
    else if (s == 2) sh_balloon(lv, lt);
    else if (s == 3) sh_birds(lv, lt);
    else sh_fin(lv, lt);
    if (s < 3) for (int x = 0; x < W; x++) ss_vd(x, 57 + ss_sin(x * 11) / 100, 63, lv, 0);
}
static void sh_draw(void)
{
    int t = ss_t, len = 360, s = (t / len) % 5, ph = t % len, fade = 60;
    for (int x = 0; x < W; x++) {
        int dx = iabs(x - 64), h = 34 - dx * dx / 300;
        span(x, 0, 63, 1);
        if (h < 32) { ss_vd(x, 0, 31 - h, 8, 0); ss_vd(x, 32 + h, 63, 8, 0); }
        if (dx > 56) ss_vd(x, 0, 63, (dx - 56), 0);
    }
    int lvB = ph > len - fade ? (ph - (len - fade)) * 16 / fade : 0;
    sh_scene(s, 16 - lvB, t, ph);
    sh_scene((s + 1) % 5, lvB, t, ph - len);
}

/* ---- DOTS: sadnoise's own page (sacred-web.vercel.app/dots.html, the aelrsty webring), for a 128 x 64 screen ----
 * The page: a grid of dots, each pushed off its place by two readings of a noise field that drifts sideways
 * (X += (N1 + N2) a, Y += (N1 - N2) a, N1 = noise(x + t, y), N2 = noise(y, x + t)); the push a = 2 + time / 10 grows
 * as it runs, so the lattice slowly melts; dots dim with their distance from the middle; two layers a moment apart.
 * Here: the grid every 4 pixels (the page's 14), the same push in proportion, one pixel a dot, the dimming as how
 * many of the dots show (each has its own threshold, so nothing flickers). The page grows for ever; this one
 * melts for a minute and a half, gathers itself again, and starts over with a new field. */
static int dt_noise(int x, int y, uint32_t seed)   /* x, y in 1/256 of a cell; -256..256 */
{
    int xi = x >> 8, yi = y >> 8, fx = x & 255, fy = y & 255;
    fx = fx * fx * (768 - 2 * fx) >> 16; fy = fy * fy * (768 - 2 * fy) >> 16;
    int v[4];
    for (int k = 0; k < 4; k++)
        v[k] = (int)(ss_h((uint32_t)(xi + (k & 1)) * 374761393u + (uint32_t)(yi + (k >> 1)) * 668265263u + seed) & 511u) - 256;
    int a = v[0] + ((v[1] - v[0]) * fx >> 8), b = v[2] + ((v[3] - v[2]) * fx >> 8);
    return a + ((b - a) * fy >> 8);
}
#define DT_RISE 5490   /* ticks the push grows for (90 s), then it falls back the same way */
static void dt_draw(void)
{
    int cyc = ss_t / (2 * DT_RISE), tt = ss_t % (2 * DT_RISE);
    int time = tt < DT_RISE ? tt : 2 * DT_RISE - tt;               /* ticks; the page's time is 0.111 of this */
    uint32_t seed = ss_h((uint32_t)cyc + 444u);
    int amp = 60 + time * 3 / 8;                                    /* pixels x 256: the page's 2 + time / 10, scaled between its
                                                                     * dot spacing and its width (as wide a melt, on fewer dots) */
    for (int l = 0; l < 2; l++) {
        int drift = (time + 18 * l) * 145 >> 8;                     /* the field's slide, in 1/256 of a cell */
        for (int j = 0; j <= 16; j++) for (int i = 0; i <= 32; i++) {
            int dx = i * 4 - 64, dy = j * 4 - 32, d2 = dx * dx + dy * dy, d = 0;
            while ((d + 1) * (d + 1) <= d2) d++;
            int lv = 16 - (d * 29 >> 8);                            /* 100 % in the middle, about half in the corners */
            if ((int)(ss_h((uint32_t)(i * 64 + j)) & 15u) >= lv) continue;
            int nx = i * 19 + drift, ny = j * 19;
            int n1 = dt_noise(nx, ny, seed), n2 = dt_noise(ny, nx, seed);
            int x = i * 4 + ((n1 + n2) * amp >> 16), y = j * 4 + ((n1 - n2) * amp >> 16);
            if (x >= 0 && x < W && y >= 0 && y < 64) px(x, y);
        }
    }
}
/* ---- LIFE: Conway's Game of Life (sadnoise asked, 12 Nov 2026) --------------------------------------------------
 * 64 x 32 cells, two pixels square, on a torus (the edges meet). A cell with three neighbours is born; one with two
 * or three lives on; the rest die. A generation every tenth of a second. When the world has stopped changing (still,
 * or blinking between two states) for a few seconds, or after 1,500 generations, or when everything has died, a new
 * soup is poured: about a third of the cells, at random. */
#define LF_W 64
#define LF_H 32
static uint8_t lf_g[2][LF_H][LF_W];
static int lf_cur, lf_gen, lf_stale;
static uint32_t lf_h1, lf_h2, lf_seedn;
static void lf_seed(void)
{
    lf_seedn = ss_h(lf_seedn + (uint32_t)ss_t * 2654435761u + 12345u);
    uint32_t r = lf_seedn;
    for (int y = 0; y < LF_H; y++) for (int x = 0; x < LF_W; x++) { r = ss_h(r + (uint32_t)(y * LF_W + x)); lf_g[0][y][x] = (uint8_t)((r & 0xffu) < 84u); }
    lf_cur = 0; lf_gen = 0; lf_stale = 0; lf_h1 = lf_h2 = 0;
}
static void lf_tick(void)
{
    if (ss_t % 6) return;
    uint8_t (*a)[LF_W] = lf_g[lf_cur], (*b)[LF_W] = lf_g[lf_cur ^ 1];
    uint32_t h = 2166136261u; int pop = 0;
    for (int y = 0; y < LF_H; y++) {
        const uint8_t *up = a[(y + LF_H - 1) % LF_H], *me = a[y], *dn = a[(y + 1) % LF_H];
        for (int x = 0; x < LF_W; x++) {
            int l = (x + LF_W - 1) % LF_W, r = (x + 1) % LF_W;
            int n = up[l] + up[x] + up[r] + me[l] + me[r] + dn[l] + dn[x] + dn[r];
            uint8_t v = (uint8_t)(n == 3 || (n == 2 && me[x]));
            b[y][x] = v; pop += v; h = (h ^ v) * 16777619u;
        }
    }
    lf_cur ^= 1; lf_gen++;
    lf_stale = (h == lf_h1 || h == lf_h2) ? lf_stale + 1 : 0;
    lf_h2 = lf_h1; lf_h1 = h;
    if (!pop || lf_stale > 40 || lf_gen > 1500) lf_seed();
}
static void lf_draw(void)
{
    uint8_t (*a)[LF_W] = lf_g[lf_cur];
    for (int y = 0; y < LF_H; y++) for (int x = 0; x < LF_W; x++) if (a[y][x]) { px(2 * x, 2 * y); px(2 * x + 1, 2 * y); px(2 * x, 2 * y + 1); px(2 * x + 1, 2 * y + 1); }
}
/* ---- CLOCK: the XXIIVV clock (clock.xxiivv.com, Devine Lu Linvega's entaloneralie), sadnoise asked, 12 Nov 2026 --
 * Neralie time: the day is 1,000,000 pulses, written as six digits (a beat is 86.4 seconds, a pulse 86.4 ms). The
 * face: a rectangle cut by six lines, one a digit -- the first runs across at the day's fraction of the height, the
 * second drops from it at the next digit's fraction of the width, the third crosses from that at its fraction of
 * what is left below, and so on inward, each line moving ten times as fast as the one before; a dashed line carries
 * the second needle up to the top edge. Under it the time, beats:pulses. The time of day comes from the unit's own
 * clock (the one stock's SET DATE/TIME set), read every ten seconds and carried by the ticks between. */
#if defined(AERIAOS)
int aos_clock_seconds(void);
#else
static int aos_clock_seconds(void) { return -1; }
#endif
static uint32_t ck_ms, ck_sync;   /* milliseconds since midnight; ticks until the clock is read again */
static int ck_have;
static void ck_hl(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) px(x, y); }
static void ck_vl(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) px(x, y); }
static void ck_draw(void)
{
    if (!ck_sync) { int s = aos_clock_seconds(); ck_have = s >= 0; if (s >= 0) ck_ms = (uint32_t)s * 1000u; else if (!ck_ms) ck_ms = 0; ck_sync = 610; }
    ck_sync--;
    ck_ms += (ss_t % 61 == 0) ? 17u : 16u; if (ss_t % 5 == 0) ck_ms += 2u;      /* 1000 ms in 61 draws: 16.39 a draw */
    if (ck_ms >= 86400000u) ck_ms -= 86400000u;
    uint32_t t = ck_ms * 10u / 864u;                                            /* 0 .. 999,999 */
    const int x0 = 2, x1 = 125, y0 = 2, y1 = 53;
    int n1 = y0 + (int)(t * (uint32_t)(y1 - y0) / 1000000u);
    int n2 = x0 + (int)((t % 100000u) * (uint32_t)(x1 - x0) / 100000u);
    int n3 = n1 + (int)((t % 10000u) * (uint32_t)(y1 - n1) / 10000u);
    int n4 = n2 + (int)((t % 1000u) * (uint32_t)(x1 - n2) / 1000u);
    int n5 = n3 + (int)((t % 100u) * (uint32_t)(y1 - n3) / 100u);
    int n6 = n4 + (int)((t % 10u) * (uint32_t)(x1 - n4) / 10u);
    ck_hl(x0, x1, y0); ck_hl(x0, x1, y1); ck_vl(x0, y0, y1); ck_vl(x1, y0, y1);
    ck_hl(x0, x1, n1); ck_vl(n2, n1, y1); ck_hl(n2, x1, n3); ck_vl(n4, n3, y1); ck_hl(n4, x1, n5); ck_vl(n6, n5, y1);
    for (int y = y0; y < n1; y++) if ((y & 3) < 2) px(n2, y);
    char b[12]; uint32_t v = t; for (int i = 6; i >= 0; i--) { if (i == 3) { b[i] = ':'; continue; } b[i] = (char)('0' + v % 10u); v /= 10u; } b[7] = 0;
    text(W / 2 - 14, 57, b, 1);
    if (!ck_have) text(2, 57, "NO CLOCK", 1);
}
static __attribute__((unused)) void ss_stop(void) {}

/* ---- PIPES: the old three-dimensional pipes, in one bit (sadnoise asked, 12 Nov 2026) ------------------------------
 * A lattice of 13 x 6 x 5 joints, seen obliquely (a step back goes 5 right and 4 up). A pipe grows a pixel a tick
 * from joint to joint, mostly straight on, turning at random into any free neighbour, with a ball wherever it turns,
 * starts or ends; when it is boxed in or long enough the next one starts somewhere free. After a handful of pipes
 * the picture holds, crumbles away and it all begins again. Nothing is redrawn: each slice of pipe is painted once
 * into a picture kept in the state (1 KB), behind a depth of four bits a pixel (4 KB), so that near pipes pass in
 * front of far ones whatever the order they grew in, each with a dark line around it. */
#define PP (ssb()->pp)
#define PP_NX 13
#define PP_NY 6
#define PP_NZ 5
static const int8_t pp_d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
/* across a pipe, lit side to shaded side, as dither levels: three kinds of pipe */
static const uint8_t pp_prof[3][5] = {{16, 16, 16, 7, 16}, {16, 4, 4, 4, 16}, {16, 16, 9, 9, 2}};
static const uint8_t pp_map[9] = {0, 0, 1, 1, 2, 3, 3, 4, 4};
static void pp_px(int x, int y, int d, int on, int halo)
{
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    int i = x * 64 + y, sh = (i & 7) * 4, z = (int)(PP.zb[i >> 3] >> sh & 15u);
    if (z > d || (halo && z == d)) return;
    PP.zb[i >> 3] = (PP.zb[i >> 3] & ~(15u << sh)) | ((uint32_t)d << sh);
    uint32_t *w = &PP.fb[x * 2 + (y < 32)], m = 1u << (y & 31);
    if (on) *w |= m; else *w &= ~m;
}
static int pp_sx(int x, int z) { return 6 + x * 8 + z * 5; }
static int pp_sy(int y, int z) { return 59 - y * 8 - z * 4; }
static int pp_node(int x, int y, int z) { return (z * PP_NY + y) * PP_NX + x; }
static int pp_free(int x, int y, int z)
{
    if ((unsigned)x >= PP_NX || (unsigned)y >= PP_NY || (unsigned)z >= PP_NZ) return 0;
    int n = pp_node(x, y, z);
    return !(PP.occ[n >> 5] >> (n & 31) & 1u);
}
static void pp_take(int x, int y, int z) { int n = pp_node(x, y, z); PP.occ[n >> 5] |= 1u << (n & 31); }
static void pp_ball(void)
{
    int cx = pp_sx(PP.x, PP.z), cy = pp_sy(PP.y, PP.z), d = 10 - 2 * PP.z;
    for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) {
        int r2 = dx * dx + dy * dy;
        if (r2 > 20) continue;
        if (r2 > 10) pp_px(cx + dx, cy + dy, d, 0, 1);
        else pp_px(cx + dx, cy + dy, d, r2 > 5 || dx + dy < 0 || (dx + dy < 3 && dither_on(cx + dx, cy + dy, 8)), 0);
    }
}
static void pp_slice(void)   /* the slice of the growing pipe at PP.step */
{
    const int8_t *v = pp_d[PP.dir];
    int sx = pp_sx(PP.x, PP.z), sy = pp_sy(PP.y, PP.z), i = PP.step, d = 10 - 2 * PP.z;
    const uint8_t *pf = pp_prof[PP.style];
    if (v[2]) {
        int cx = sx + v[2] * i, cy = sy - v[2] * ((4 * i + 2) / 5);
        for (int p = -4; p <= 4; p++) pp_px(cx, cy + p, d - v[2], p > -4 && p < 4 && dither_on(cx, cy + p, pf[pp_map[p + 4]]), p == -4 || p == 4);
        return;
    }
    if (PP.ball && i < 4) return;   /* inside the ball it left from */
    for (int p = -3; p <= 3; p++) {
        int x = v[0] ? sx + v[0] * i : sx + p, y = v[0] ? sy + p : sy - v[1] * i;
        pp_px(x, y, d, p > -3 && p < 3 && dither_on(x, y, pf[p + 2]), p == -3 || p == 3);
    }
}
static void pp_hold(void) { PP.state = 1; PP.wait = 200; }
static void pp_begin(void)   /* a new pipe from a free joint; none found, or enough pipes: the picture is done */
{
    if (PP.pipe < PP.pipes && PP.total < 120)
        for (int k = 0; k < 40; k++) {
            int x = range(0, PP_NX - 1), y = range(0, PP_NY - 1), z = range(0, PP_NZ - 1);
            if (!pp_free(x, y, z)) continue;
            PP.x = x; PP.y = y; PP.z = z; pp_take(x, y, z);
            PP.dir = range(0, 5); PP.step = 0; PP.segs = 0; PP.life = range(16, 38); PP.style = PP.pipe % 3; PP.pipe++;
            return;
        }
    pp_hold();
}
static void pp_reset(void)
{
    uint32_t *p = (uint32_t *)(void *)&PP;
    for (unsigned i = 0; i < sizeof PP / 4; i++) p[i] = 0;
    PP.pipes = range(4, 6);
    pp_begin();
}
static void pp_step(void)
{
    const int8_t *v;
    if (PP.step == 0) {   /* at a joint: where next? */
        int opts[6], n = 0, nd;
        for (int k = 0; k < 6; k++) if (pp_free(PP.x + pp_d[k][0], PP.y + pp_d[k][1], PP.z + pp_d[k][2])) opts[n++] = k;
        if (!n || PP.segs >= PP.life) { pp_ball(); pp_begin(); return; }
        nd = opts[rnd() % (uint32_t)n];
        v = pp_d[PP.dir];
        if (PP.segs && pp_free(PP.x + v[0], PP.y + v[1], PP.z + v[2]) && rnd() % 8u < 5u) nd = PP.dir;
        PP.ball = !PP.segs || nd != PP.dir;
        if (PP.ball) pp_ball();
        PP.dir = nd; v = pp_d[nd];
        pp_take(PP.x + v[0], PP.y + v[1], PP.z + v[2]);
    }
    v = pp_d[PP.dir];
    PP.step++;
    pp_slice();
    if (PP.step >= (v[2] ? 5 : 8)) { PP.x += v[0]; PP.y += v[1]; PP.z += v[2]; PP.step = 0; PP.segs++; PP.total++; }
}
static void pp_tick(void)
{
    if (PP.state == 0) { pp_step(); return; }
    if (--PP.wait > 0) {
        if (PP.state == 2 && PP.wait % 3 == 0)   /* crumbling: a quarter of what is left goes, each time */
            for (int i = 0; i < W * 2; i++) PP.fb[i] &= ss_h((uint32_t)(i * 3 + PP.wait * 771)) | ss_h((uint32_t)(i * 5 + PP.wait * 337 + 99));
        return;
    }
    if (PP.state == 1) { PP.state = 2; PP.wait = 48; }
    else pp_reset();
}
static void pp_draw(void) { for (int i = 0; i < W * 2; i++) F[i] = PP.fb[i]; }

/* ---- the dreams (from sadnoise's dream log, 12 Nov 2026): six scenes to watch, each a loop that is never quite
 * the same twice. Nothing is kept but a seed; every frame is drawn from the tick count. ---- */
#define DM (ssb()->dm)
static uint32_t dm_h(int cyc, int salt) { return ss_h(DM.seed + (uint32_t)cyc * 7919u + (uint32_t)salt * 104729u); }
static void dm_box(int x0, int y0, int x1, int y1, int lv)   /* cleared, then toned */
{
    for (int x = x0; x <= x1; x++) { ss_vd(x, y0, y1, 16, 0); ss_vd(x, y0, y1, lv, 1); }
}
static void dm_dark(int lv) { for (int x = 0; x < W; x++) ss_vd(x, 0, H - 1, lv, 0); }
/* a small figure, feet at (x, y), twelve pixels tall: ph the stride (0 standing), ink 1 lit / 0 dark, dress 1 a dress */
static void dm_man(int x, int y, int ph, int ink, int dress)
{
    int s = ss_sin(ph) * 5 / 254;
    if (s) { ss_ln(x, y - 4, x + s, y, 16, ink, 1); ss_ln(x, y - 4, x - s, y, 16, ink, 1); }
    else { ss_vd(x - 1, y - 4, y, 16, ink); ss_vd(x + 1, y - 4, y, 16, ink); }
    for (int k = -1; k <= 1; k++) { ss_vd(x + k, y - 8, y - 5, 16, ink); ss_vd(x + k, y - 12, y - 10, 16, ink); }
    ss_vd(x, y - 9, y - 9, 16, ink);
    if (dress) { ss_vd(x - 2, y - 4, y - 2, 16, ink); ss_vd(x + 2, y - 4, y - 2, 16, ink); ss_vd(x - 1, y - 4, y - 2, 16, ink); ss_vd(x + 1, y - 4, y - 2, 16, ink); ss_vd(x, y - 4, y - 2, 16, ink); }
}

/* ---- REDCARS: a stair of old cars down from the balcony; a van waits, a crowd rushes in, a girl goes last ---- */
#define RC_LEN 1200
#define RC_X0 22
#define RC_Y0 13
#define RC_PX 13
#define RC_PY 9
#define RC_N 5
#define RC_GROUND 61
static void rc_car(int x, int y, int k)   /* roof line at y; 19 wide, wheels down to y + 11 */
{
    int tone = (k & 3) ? 16 : 9;
    for (int c = x; c <= x + 18; c++) {
        int top = y + 3 + (c == x || c == x + 18);
        ss_vd(c, top - 1, y + 9, 16, 0); ss_vd(c, top, y + 8, tone, 1); px(c, top); px(c, y + 8);
    }
    span(x, y + 5, y + 7, 1); span(x + 18, y + 5, y + 7, 1);
    for (int c = x + 4; c <= x + 13; c++) {
        int top = y + (c == x + 4 || c == x + 13);
        ss_vd(c, top - 1, y + 2, 16, 0); px(c, top);
    }
    span(x + 4, y + 1, y + 2, 1); span(x + 13, y + 1, y + 2, 1); span(x + ((k & 4) ? 8 : 9), y + 1, y + 2, 1);
    span(x + ((k & 4) ? 8 : 9), y + 4, y + 7, 0);                               /* the door's edge */
    if (k & 8) { px(x, y + 3); px(x + 18, y + 3); px(x, y + 2); px(x + 18, y + 2); }   /* fins */
    for (int w = x + 4; w <= x + 14; w += 10) { ss_ell(w, y + 9, 3, 3, 16, 0, 0); ss_ell(w, y + 9, 2, 2, 16, 1, 0); px_off(w, y + 9); }
}
static int rc_y(int x)   /* where feet stand at x: balcony, roofs and bonnets, the ground */
{
    int u = x - (RC_X0 + 4);
    if (u < 0) return RC_Y0 - 1;
    if (u >= RC_N * RC_PX) return RC_GROUND + 1;
    return RC_Y0 + (u / RC_PX) * RC_PY + (u % RC_PX < 10 ? -1 : 2);
}
static void rc_van(int x, int open, int lamps)
{
    for (int c = x; c <= x + 25; c++) {
        int top = 43 + (c > x + 19 ? (c - x - 19) * 3 / 2 : 0) + (c == x);
        ss_vd(c, top, 57, 16, 0); ss_vd(c, top + 1, 56, 4, 1); px(c, top); px(c, 57);
    }
    span(x, 45, 57, 1); span(x + 25, 52, 57, 1);
    dm_box(x + 15, 46, x + 19, 50, 0); px(x + 20, 49); px(x + 20, 50); px_off(x + 21, 50);
    for (int c = x + 4; c <= x + 12; c++) { if (open) span(c, 46, 56, 0); px(c, 45); }
    span(x + 3, 45, 56, 1); span(x + 13, 45, 56, 1);
    if (!open) { span(x + 8, 46, 56, 1); px(x + 11, 51); px(x + 5, 51); }
    for (int w = x + 5; w <= x + 20; w += 15) { ss_ell(w, 58, 3, 3, 16, 0, 0); ss_ell(w, 58, 3, 3, 16, 1, 0); ss_ell(w, 58, 1, 1, 16, 0, 0); }
    if (lamps) { px(x + 25, 54); px(x + 26, 54); ss_tri(x + 27, 54, x + 50, 50, x + 50, 60, 4, 1); }
}
static void rc_draw(void)
{
    int cyc = ss_t / RC_LEN, t = ss_t % RC_LEN;
    uint32_t hc = dm_h(cyc, 1);
    int crowd = 6 + (int)(hc % 4u), t_girl = 470 + crowd * 24 + 250, t_go = t_girl + 80;
    for (int i = 0; i < 40; i++) {
        uint32_t h = ss_h((uint32_t)i * 13u + 3u);
        int x = 24 + (int)(h % 104u), y = (int)((h >> 8) % 44u);
        if (((h >> 16) + (uint32_t)ss_t / 8u) % 23u) px(x, y);
    }
    ss_ell(112, 9, 5, 5, 16, 1, 0); ss_ell(110, 8, 4, 4, 16, 0, 0);
    /* the house: wall, lit door, balcony */
    for (int x = 0; x < 20; x++) {
        ss_vd(x, 0, 63, 16, 0); ss_vd(x, 0, 63, 3, 1);
        for (int y = 3; y < 61; y += 4) if (((x + (y & 4) * 2) & 7) == 0) px_off(x, y), px_off(x, y + 1);
    }
    span(19, 0, 63, 1);
    dm_box(5, 1, 12, RC_Y0 - 1, 16);
    dm_box(6, 30, 11, 38, 0); for (int x = 5; x <= 12; x++) { px(x, 29); px(x, 39); } span(5, 29, 39, 1); span(12, 29, 39, 1); span(8, 30, 38, 1) ; px(9, 34); px(10, 34); px(7, 34); px(6, 34);
    for (int x = 0; x <= RC_X0 + 2; x++) { span(x, RC_Y0, RC_Y0 + 1, 1); if (x < 20) { px(x, RC_Y0 - 7); if (x % 3 == 1) span(x, RC_Y0 - 6, RC_Y0 - 1, 1); } }
    /* the ground */
    for (int x = 0; x < W; x++) { px(x, RC_GROUND); ss_vd(x, RC_GROUND + 1, 63, 3, 1); }
    /* the cars, lowest first */
    for (int i = RC_N - 1; i >= 0; i--) rc_car(RC_X0 + i * RC_PX, RC_Y0 + i * RC_PY, (int)(dm_h(cyc, 10 + i) >> 4));
    /* the van: in from the right, waits with its door open, away when the girl is in */
    int vx = 102;
    if (t < 150) vx += (150 - t) * (150 - t) / 560;
    if (t > t_go) vx += (t - t_go) * (t - t_go) / 260;
    if (vx < 132) rc_van(vx, t > 190 && t < t_girl + 45, t < 150 || t > t_go - 30);
    /* the girl: walks in under the cars, waits by the van, runs in last */
    int gx = t < 330 ? -6 + t * 102 / 330 : 96;
    if (t > t_girl) gx = 96 + (t - t_girl) / 2;
    if (gx < 107) dm_man(gx, 63 - (gx > 102 ? (gx - 102) : 0), (t < 330 || t > t_girl) ? t * (t < 330 ? 20 : 60) : (ss_sin(t * 3) > 100 ? 90 : 0), 1, 1);
    /* the walker: out of the door, down the cars, into the van */
    if (t < 60) dm_man(8, RC_Y0 - 1, 0, 0, 0);
    else {
        int u = (t - 60) * 2, wx = 8 + u / 13, sub = u % 13, x_end = RC_X0 + 4 + RC_N * RC_PX, y;
        if (wx >= x_end) { wx = x_end + (t - 60 - (x_end - 8) * 13 / 2) / 2; sub = 0; }
        y = rc_y(wx) + (rc_y(wx + 1) - rc_y(wx)) * sub / 13;
        if (wx > 102) y -= wx - 102;
        if (wx < 107) dm_man(wx, y, t * (wx >= x_end ? 60 : 24), 1, 0);
    }
    /* the crowd: a rush along the ground and up into the van */
    for (int j = 0; j < crowd; j++) {
        uint32_t h = dm_h(cyc, 30 + j);
        int tj = 470 + j * 24 + (int)(h & 15u), x = -5 + (t - tj) * 2 / 3;
        if (t < tj || x >= 107) continue;
        dm_man(x, 62 + (int)(h >> 5 & 1u) - (x > 102 ? x - 102 : 0), t * 64 + j * 300, 1, (int)(h >> 7 & 3u) == 0);
    }
}

/* ---- FROGS: a hall of dull dark frogs, hopping from square to square in formation before a throne ---- */
#define FG_HOP 132
static void fg_at(int col, int z, int *x, int *y, int *s)   /* col, z in 1/256 of a square */
{
    int s256 = 4096 * 768 / (768 + z);
    *s = s256 >> 8; *y = 20 + (42 * s256 >> 12); *x = 64 + (col * 26 / 16 * s256 >> 16);
}
static void fg_frog(int x, int y, int s, int lift, int blink, int lean)
{
    int rx = 9 * s / 16, ry = 7 * s / 16, by = y - lift, hx = rx * 3 / 4, hr = rx / 3 + 1, hy = ry * 2 / 3;
    if (lift > 1) for (int dx = -rx + 1; dx < rx; dx++) ss_p(x + dx, y, 8, 0), ss_p(x + dx, y - 1, 8, 0);   /* its shadow */
    for (int sd = -1; sd <= 1; sd += 2) ss_ell(x + sd * hx, by, hr + 1, hy + 1, 16, 1, 1);   /* a pale edge, to stand clear of the row behind */
    ss_ell(x, by, rx - 1, ry + 1, 16, 1, 1);
    for (int sd = -1; sd <= 1; sd += 2) { ss_ell(x + sd * (rx / 3 + 1), by - ry, 2, 2, 16, 1, 0); ss_ell(x + sd * hx, by, hr, hy, 16, 0, 1); }   /* haunches */
    ss_ell(x, by, rx - 2, ry, 16, 0, 1);
    if (lift > 1) for (int sd = -1; sd <= 1; sd += 2) ss_ln(x + sd * hx, by, x + sd * hx - lean, by + 1 + lift / 2, 16, 0, 2);
    else for (int sd = -1; sd <= 1; sd += 2) { px_off(x + sd * (hx + hr + 1), by); px_off(x + sd * 2, by + 1); px_off(x + sd * 3, by + 1); }
    if (s > 10) for (int dx = -rx / 3; dx <= rx / 3; dx++) if ((dx + x) & 1) px(x + dx, by - ry + 2);   /* a dull shine on its back */
    for (int sd = -1; sd <= 1; sd += 2) {                                                  /* eyes, on top */
        int ex = x + sd * (rx / 3 + 1), ey = by - ry;
        ss_ell(ex, ey, 1, 1, 16, 0, 0);
        if (!blink) px(ex, ey);
    }
}
static void fg_draw(void)
{
    static const int8_t path[2][5][2] = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    int t = ss_t, n = t / FG_HOP, ph = t % FG_HOP, k = n & 3, way = (int)(dm_h(n >> 2, 2) & 1u);
    int rest = (int)(dm_h(n, 4) % 6u) == 0;   /* now and then they stay where they are */
    /* the hall: back wall, torches, the throne on its steps */
    for (int x = 0; x < W; x++) { if ((x % 42) == 0 || (x % 42) == 2) { for (int y = (x & 2) / 2; y < 35; y += 2) px(x, y); } px(x, 35); }
    for (int i = 0; i < 2; i++) {
        int tx = i ? 106 : 22, fl = (int)(ss_h((uint32_t)(t / 5 + i * 50)) & 3u);
        ss_ell(tx, 13, 11 + (fl & 1), 10 + (fl & 1), 16, 0, 0);
        ss_ell(tx, 13, 10 + (fl & 1), 9 + (fl & 1), 2, 1, 0);
        ss_ell(tx, 13, 5, 5, 6, 1, 0);
        ss_tri(tx - 2, 14, tx + 2, 14, tx + fl - 1, 7 - (fl >> 1), 16, 1);
        span(tx, 15, 22, 1); px(tx - 1, 15); px(tx + 1, 15); px(tx - 2, 14); px(tx + 2, 14);
    }
    int lift = ph > FG_HOP - 26 && (dm_h(n + 1, 4) % 6u) ? (ph - (FG_HOP - 26)) / 6 : 0;   /* the staff rises, and falls on the beat */
    for (int x = 44; x <= 84; x++) { span(x, 28, 34, 0); px(x, 31); if (x > 48 && x < 80) px(x, 28); px(x, 34); }
    for (int x = 54; x <= 74; x++) {
        int top = 2 + iabs(x - 64) * iabs(x - 64) / 20;
        span(x, top, 27, 0); ss_vd(x, top + 1, 27, 3, 1); px(x, top);
    }
    span(54, 7, 27, 1); span(74, 7, 27, 1); span(64, 0, 1, 1); px(63, 1); px(65, 1);
    ss_quad(59, 16, 69, 16, 72, 27, 56, 27, 16, 0); ss_quad(60, 16, 68, 16, 71, 27, 57, 27, 16, 1);
    for (int y = 19; y < 27; y += 3) px_off(64, y);
    ss_ell(64, 11, 4, 4, 16, 0, 0); ss_ell(64, 11, 3, 3, 16, 1, 0);
    px_off(63, 11); px_off(65, 11);
    span(61, 5, 7, 1); span(64, 4, 7, 1); span(67, 5, 7, 1); px(62, 7); px(63, 7); px(65, 7); px(66, 7);
    ss_ln(68, 18, 77, 21 - lift, 16, 1, 1); ss_ln(60, 18, 55, 23, 16, 1, 1);
    span(78, 7 - lift, 33 - lift, 1); ss_ell(78, 5 - lift, 2, 2, 16, 1, 0); px_off(78, 5 - lift);
    /* the floor: pale squares running to the wall */
    for (int x = 0; x < W; x++) ss_vd(x, 36, 63, 13, 1);
    for (int b = 0; b <= 4; b++) { int y = 20 + 42 * 768 / (768 + b * 256 - 128); if (y < H) for (int x = 0; x < W; x++) px_off(x, y); }
    for (int cb = -3; cb <= 3; cb++) for (int y = 36; y < H; y++) px_off(64 + (cb * 26 - 13) * (y - 20) / 42, y);
    /* the frogs, far row first */
    for (int r = 2; r >= 0; r--) {
        int p = ph - 6 - r * 8, e = rest ? 0 : p <= 0 ? 0 : p >= 40 ? 256 : p * 256 / 40, air = e * (256 - e) >> 8;   /* air 0..64 */
        e = e * e * (768 - 2 * e) >> 16;
        int ox = path[way][k][0] * (256 - e) + path[way][k + 1][0] * e, oz = path[way][k][1] * (256 - e) + path[way][k + 1][1] * e;
        for (int c = 0; c < 4; c++) {
            int x, y, s;
            fg_at((c - 2) * 256 + ox, r * 256 + oz, &x, &y, &s);
            uint32_t h = ss_h((uint32_t)(c + r * 5) * 91u + (uint32_t)(t / 9));
            fg_frog(x, y, s, air * s / 150, (h % 29u) == 0, (path[way][k + 1][0] - path[way][k][0]) * 2);
        }
    }
}

/* ---- COLUMNS: far underground, rising through a mine: columns and pipes going up for ever, a bright sky far above ---- */
static int cl_edge(int v, int c) { return (ss_sin(v * 11 + c * 300) + ss_sin(v * 29 + c * 77) / 2) / 96; }
static void cl_draw(void)
{
    static const uint8_t mx[4] = {27, 45, 78, 97}, mw[4] = {6, 4, 7, 5};
    int t = ss_t, s0 = t / 12, s1 = t / 5, s2 = t / 2;
    /* the light from above, thinning as it comes down; the sky itself, far up */
    for (int x = 0; x < W; x++)
        for (int b = 0; b < 16; b++) { int lv = 15 - b - iabs(x - 64) / 14; ss_vd(x, b * 4, b * 4 + 3, lv < 2 && iabs(x - 64) < 52 ? 2 : lv, 1); }
    ss_ell(64, -1, 13, 6, 16, 1, 0);
    /* far columns: thin, slow, lost in the light */
    for (int i = 0; i < 6; i++) {
        int x = 22 + i * 15 + (int)(ss_h((uint32_t)i * 5u + 1u) % 6u);
        for (int y = 0; y < H; y++) { px_off(x, y); if (ss_wrap(y - s0 + i * 9, 17) == 0) { px_off(x - 1, y); px_off(x + 1, y); } }
    }
    /* the middle: columns of rock, ringed, and the pipes between them */
    for (int c = 0; c < 4; c++)
        for (int y = 0; y < H; y++) {
            int v = y - s1, x0 = mx[c] + cl_edge(v, c), x1 = x0 + mw[c] + cl_edge(v + 40, c + 9) / 2, ring = ss_wrap(v + c * 23, 47);
            for (int x = x0 - (ring < 4); x <= x1 + (ring < 4); x++) px_off(x, y);
            if (ring == 1 || ring == 2) { for (int x = x0; x <= x1; x++) px(x, y); continue; }
            if (y > 14) px(c < 2 ? x1 : x0, y);   /* lit on the side the sky is */
        }
    for (int k = 0; k < 3; k++) {
        int y = ss_wrap((int)(ss_h((uint32_t)k * 3u + 8u) % 96u) + s1, 96) - 14, xa = mx[k] + mw[k] + 2, xb = mx[k + 1] - 1;
        for (int x = xa - 2; x <= xb + 2; x++) { span(x, y - 1, y + 3, 0); px(x, y); }
        span(xa, y - 2, y + 4, 0); span(xb, y - 2, y + 4, 0); px(xa, y - 1); px(xb, y - 1);
        if (k != 1) { int m = (xa + xb) / 2; span(m, y - 5, y - 1, 0); for (int d = -2; d <= 2; d++) px_off(m + d, y - 5), px(m + d, y - 6); }
    }
    for (int y = 0; y < H; y++) {   /* a conduit climbing beside the third column, clipped to it */
        int v = y - s1, x = mx[2] + mw[2] + 3 + cl_edge(v, 2);
        px_off(x - 1, y); px_off(x + 1, y); if (y > 10) px(x, y); else px_off(x, y);
        if (ss_wrap(v, 13) == 0) { px_off(x, y); px_off(x - 2, y); }
    }
    /* near: the two great columns that hold the shaft, sliding past faster */
    for (int n = 0; n < 2; n++)
        for (int y = 0; y < H; y++) {
            int v = y - s2, x0 = (n ? 112 : 4) + cl_edge(v / 2, 20 + n) * 2, x1 = x0 + 10 + cl_edge(v / 2 + 90, 30 + n) * 2;
            int ring = ss_wrap(v + n * 31, 83), in = n ? x0 : x1, wall = n ? x1 + 4 + cl_edge(v + 500, 40) * 2 : x0 - 4 + cl_edge(v + 700, 41) * 2;
            for (int x = (n ? x0 - 2 : 0); x <= (n ? W - 1 : x1 + 2); x++) px_off(x, y);
            if (ring < 5) { for (int x = x0 - 1; x <= x1 + 1; x++) if (ring == 0 || ring == 4 || (ring == 2 && (x & 3) == 1)) px(x, y); continue; }
            px(in, y); if ((v & 3) != 3) px(n ? x0 + 1 : x1 - 1, y);
            for (int x = x0; x <= x1; x++) if ((ss_h((uint32_t)(x - x0) * 131u + (uint32_t)ss_wrap(v, 4096) * 7u + (uint32_t)n) & 63u) == 0) { px(x, y); px(x, y + 1); }
            if (v & 1) px(wall, y);   /* the rock wall behind */
        }
    /* dust rising in the light */
    for (int i = 0; i < 12; i++) {
        uint32_t h = ss_h((uint32_t)i * 37u + 11u);
        int y = 63 - (int)(((h >> 8) + (uint32_t)t / (3u + (h & 3u))) % 70u), x = 26 + (int)(h % 76u) + ss_sin(t * 2 + i * 150) / 40;
        if (y > 30) px(x, y); else px_off(x, y);
    }
}

/* ---- TRAIN: a packed platform; a maintenance train goes through, its white lights blinking; next train 2 min ---- */
#define TR_LEN 1260
static void tr_col(int x, int top, int bot, int lv) { ss_vd(x, top, bot, 16, 0); ss_vd(x, top, bot, lv, 1); px(x, top); }
static void tr_lamp(int x, int y, int on)
{
    px(x, y);
    if (!on) return;
    for (int k = 1; k <= 3; k++) { px(x - k, y); px(x + k, y); if (k < 3) { px(x, y - k); px(x, y + k); } }
    px(x - 1, y - 1); px(x + 1, y - 1); px(x - 1, y + 1); px(x + 1, y + 1);
    ss_ell(x, y, 7, 5, 4, 1, 0);
}
static void tr_person(int x, int y, int r, int k)   /* from behind: a pale head, a dark coat with the light on its shoulders */
{
    int sw = r * 2 + 1 + (r > 2), sy = y + r + 2;
    for (int dx = -sw; dx <= sw; dx++) {
        int a = sw - iabs(dx), top = sy + (a < 3 ? 3 - a : 0) + (a == 0);
        ss_vd(x + dx, top - 1, 63, 16, 0); px(x + dx, top);
        if (a == 0) span(x + dx, top, 63, 1);
    }
    ss_ell(x, y, r + 1, r + 1, 16, 0, 0);
    ss_ell(x, y, r, r, 16, 1, 0);
    if (r > 2 && (k & 2)) { for (int dx = -r - 2; dx <= r + 2; dx++) px(x + dx, y - 2), px_off(x + dx, y - 1); }   /* a hat's brim */
    if (r > 2 && (k & 5) == 4) { ss_ell(x, y + 2, r - 1, r - 2, 16, 0, 0); ss_ell(x, y + 2, r - 1, r - 2, 8, 1, 0); }   /* long hair */
}
static void tr_draw(void)
{
    int cyc = ss_t / TR_LEN, t = ss_t % TR_LEN;
    uint32_t hc = dm_h(cyc, 5);
    int dir = (hc & 1u) ? 1 : -1, wagons = 4 + (int)(hc >> 1 & 3u), len = 36 + wagons * 36;
    int run = (t - 240) * 3 / 4, head = dir > 0 ? run - 6 : W + 5 - run, on = t >= 240 && run < W + len + 12;
    /* the far wall and the track */
    for (int x = 0; x < W; x++) {
        ss_vd(x, 0, 2, 5, 1); px(x, 3);
        ss_vd(x, 16, 30, (x & 31) < 3 ? 6 : 2, 1);
        if ((x & 31) > 9 && (x & 31) < 25) { span(x, 20, 26, 0); if ((x & 31) == 10 || (x & 31) == 24) span(x, 20, 26, 1); else { px(x, 20); px(x, 26); } }
        px(x, 31); px(x, 42); if ((x & 3) == 0) px(x, 43);
    }
    /* the train, column by column from its nose back */
    if (on)
        for (int u = 0; u < len; u++) {
            int x = head - dir * u;
            if ((unsigned)x >= W) continue;
            if (u < 36) {   /* the engine: nose, cab, bonnet */
                int top = u < 3 ? 31 - u * 2 : u < 16 ? 19 + (u == 3 || u == 15) : u < 34 ? 25 : 37;
                if (u >= 34) { px(x, 37); continue; }
                tr_col(x, top, 38, 16);
                if (u > 5 && u < 14 && u != 9) span(x, 22, 27, 0);
                if (u > 17 && u < 32 && (u & 1)) span(x, 28, 32, 0);
                for (int y = 35; y <= 38; y++) if (((u + y) & 7) < 4) px_off(x, y);
                if (u == 6 || u == 17 || u == 28) { ss_ell(x, 40, 2, 2, 16, 1, 0); px_off(x, 40); }
            } else {
                int w = (u - 36) / 36, l = (u - 36) % 36, kind = (int)(dm_h(cyc, 60 + w) % 6u);
                if (l >= 33) { px(x, 37); continue; }
                tr_col(x, 35, 38, 16); px_off(x, 37);
                if (l == 7 || l == 26) { ss_ell(x, 40, 2, 2, 16, 1, 0); px_off(x, 40); }
                if (kind == 1 && l > 2 && l < 30) { int b = (l - 3) % 9; if (b < 8) { tr_col(x, 27 + ((l - 3) / 9 & 1) * 3, 34, 4); if (b == 0 || b == 7) span(x, 27 + ((l - 3) / 9 & 1) * 3, 34, 1); } }
                if (kind == 2 && l > 3 && l < 29) { int hgt = isqrt(169 - (l - 16) * (l - 16)) * 8 / 13; tr_col(x, 34 - hgt, 34, 7); }
                if (kind == 3 && l > 1 && l < 31) { px(x, 34); px(x, 32); px(x, 30); if (l == 6 || l == 26) span(x, 27, 34, 1); }
                if (kind == 4 && l > 3 && l < 29) { int hgt = isqrt(144 - (l - 16) * (l - 16) * 144 / 169) * 4 / 12; tr_col(x, 30 - hgt, 30 + hgt, 11); px(x, 30 + hgt); if (l == 8 || l == 24) span(x, 31, 34, 1); }
                if (kind == 5) { if (l > 12 && l < 22) tr_col(x, 28, 34, 6); if (l >= 2 && l <= 17) px(x, 30 - (17 - l) * 2 / 3), px(x, 31 - (17 - l) * 2 / 3); if (l == 2) span(x, 20, 26, 1); }
            }
        }
    if (on) {   /* its lights, over everything behind */
        int blink = t / 14;
        tr_lamp(head - dir * 9, 17, blink & 1); tr_lamp(head - dir * 30, 23, !(blink & 1));
        if ((unsigned)(head + dir * 2) < W + 30u) ss_tri(head + dir * 2, 33, head + dir * 34, 28, head + dir * 34, 41, 5, 1);
        px(head, 33); px(head, 34);
        for (int w = 0; w < wagons; w++) tr_lamp(head - dir * (36 + w * 36 + 32), 33, ((blink + w) & 3) == 0);
        /* the bell: deep, far apart: a ring spreading from the cab */
        int bp = (t - 240) % 150, bx = head - dir * 9;
        if (bp < 50) for (int a = 0; a < 1024; a += 12) ss_p(bx + ss_cos(a) * (6 + bp) / 127, 17 + ss_sin(a) * (6 + bp) / 190, 12 - bp / 5, 1);
    }
    /* the sign */
    for (int x = 28; x <= 99; x++) { span(x, 5, 15, 0); px(x, 5); px(x, 15); }
    span(28, 5, 15, 1); span(99, 5, 15, 1); span(36, 0, 4, 1); span(91, 0, 4, 1);
    if ((int)(ss_h((uint32_t)(ss_t / 7)) % 61u)) text(33, 8, "NEXT TRAIN 2 MIN", 1);
    /* the platform: its edge, and everyone on it */
    for (int x = 0; x < W; x++) { span(x, 44, 63, 0); px(x, 44); if ((x & 7) < 5) px(x, 45); }
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < (row ? 7 : 12); i++) {
            uint32_t h = ss_h((uint32_t)(i + row * 40) * 29u + hc * 8u + 1u);
            int x = row ? i * 21 + (int)(h % 5u) - 1 : i * 12 + (int)(h % 5u) - 2, y = row ? 55 + (int)(h >> 4 & 1u) : 49 + (int)(h >> 4 & 1u);
            x += ss_sin(ss_t * 2 + i * 177 + row * 400) / 90 + ((on && iabs(head - dir * len / 2 - x) < len / 2) ? dir : 0);
            tr_person(x, y, row ? 4 : 2, (int)(h >> 8 & 7u));
        }
}

/* ---- WALKWAY: a long moving walkway over shallow water, in heavy rain, the city behind; someone carried along ---- */
static void wk_rider(int x, int kind)
{
    dm_man(x, 46, 0, 1, kind == 3);
    if (kind == 1) { for (int dx = -7; dx <= 7; dx++) span(x + dx, 27, 33, 0); ss_ell(x, 32, 6, 4, 16, 1, 1); px_off(x - 3, 32); px_off(x + 3, 32); span(x + 2, 33, 39, 1); px(x, 27); }   /* an umbrella */
    if (kind == 2) { dm_box(x + 3, 42, x + 6, 46, 16); px(x + 3, 40); px(x + 3, 41); px_off(x + 4, 44); px_off(x + 5, 44); }                          /* a case */
}
static void wk_draw(void)
{
    int t = ss_t;
    /* the city */
    for (int x = 0; x < W; x++) {
        int bf = x / 9, bn = (x + 4) / 13;
        uint32_t hf = ss_h((uint32_t)bf * 31u + 5u), hn = ss_h((uint32_t)bn * 17u + 9u);
        int far = 5 + (int)(hf % 12u), near = 11 + (int)(hn % 13u);
        ss_vd(x, 0, 31, 1, 1);
        span(x, far, 31, 0); ss_vd(x, far, 31, 4, 1);
        if ((x + 4) % 13 != 12) { span(x, near, 31, 0); px(x, near); }
        if ((x + 4) % 13 == 0 || (x + 4) % 13 == 11) span(x, near, 31, 1);
        for (int y = near + 2; y < 30; y += 3) {
            uint32_t hw = ss_h((uint32_t)(x * 64 + y) + (uint32_t)bn * 977u);
            int lx = (x + 4) % 13;
            if (lx > 1 && lx < 10 && (lx & 1) == 0 && (hw & 7u) < 2u && (((hw >> 8) + (uint32_t)t / 160u) % 9u)) { px(x, y); px(x, y + 1); }
        }
        if ((hn >> 9 & 7u) == 0 && (x + 4) % 13 == 6) span(x, near - 5, near, 1);
        px(x, 31);
    }
    /* the water: shallow, restless */
    for (int y = 32; y < H; y++) {
        int d = y - 29;
        for (int x = 0; x < W; x++) {
            int v = ss_sin(x * 40 / d + (y * 97 & 1023) + t * (y & 1 ? 3 : -2)) + ss_sin(x * 9 + y * 50 - t * 2) / 2;
            if (v > 132) px(x, y);
        }
    }
    /* legs into the water, and their broken reflections */
    for (int x = 12; x < W; x += 34) {
        dm_box(x - 1, 51, x + 3, 57, 0); span(x, 51, 57, 1); span(x + 2, 51, 57, 1);
        for (int y = 58; y < 64; y++) { int wob = ss_sin(y * 150 + t * 6) / 60; px_off(x - 1 + wob, y); px_off(x + 3 + wob, y); px_off(x + 1 + wob, y); if (y & 1) { px(x + wob, y); px(x + 2 + wob, y); } else { px_off(x + wob, y); px_off(x + 2 + wob, y); } }
    }
    for (int x = 0; x < W; x++) { int wob = ss_sin(x * 30 + t * 4) / 90; span(x, 53 + wob, 54 + wob, 0); if (((x + t / 3) & 3) < 2) px(x, 54 + wob); }
    /* the walkway: rail, glass, belt */
    for (int x = 0; x < W; x++) {
        span(x, 39, 51, 0);
        if ((x & 15) == 5) span(x, 41, 46, 1);
        px(x, 47); px(x, 48); ss_vd(x, 49, 49, 8, 1); px(x, 50);
        if (((x - t / 3) & 7) == 0) { px_off(x, 47); px_off(x, 48); }
    }
    for (int x = 0; x < W; x++) { px_off(x, 39); px(x, 40); px(x, 41); px_off(x, 42); if ((x & 15) == 5) px(x, 42); }
    /* whoever is on it, standing still, carried */
    for (int k = 0; k < 2; k++) {
        int u = t / 3 + k * 97, pass = u / 190, x = u % 190 - 30;
        uint32_t h = dm_h(pass, 70 + k);
        if (k && (h & 3u)) continue;
        for (int dx = -2; dx <= 2; dx++) span(x + dx, 33, 46, 0);
        wk_rider(x, k ? (int)(h >> 4 & 3u) : (int)(h >> 4 & 1u) * 2);
        if (!(h >> 8 & 7u)) { for (int dx = -9; dx <= -5; dx++) span(x + dx, 33, 46, 0); wk_rider(x - 7, 3); }
    }
    /* the rain, and where it lands */
    for (int i = 0; i < 120; i++) {
        uint32_t h = ss_h((uint32_t)i * 19u + 7u);
        int y = (int)(((h >> 8) + (uint32_t)t * (4u + (h >> 20 & 1u))) % 78u) - 8, x = ss_wrap((int)(h % 160u) - y / 2, 160) - 8, n = 4 + (int)(h >> 22 & 3u);
        for (int k = 0; k < n; k++) px(x - k / 2, y + k);
    }
    for (int k = 0; k < 14; k++) {
        uint32_t h = ss_h((uint32_t)(k * 7 + (t + k * 11) / 24 * 16));
        int r = (t + k * 11) % 24, cx = (int)(h % 124u) + 2, cy = 55 + (int)(h >> 8 & 7u);
        if (k < 5) { px(cx - r / 6 - 1, 38 - r / 8); px(cx + r / 6 + 1, 38 - r / 8); continue; }   /* off the rail */
        for (int a = 0; a < 1024; a += 64) ss_p(cx + ss_cos(a) * (r / 3 + 1) / 127, cy + ss_sin(a) * (r / 9 + 1) / 127, r < 14 ? 16 : 8, 1);
    }
}

/* ---- ISLAND: a patch of island with one tree; someone wades out to it under a huge blanket, with bags ---- */
#define IS_LEN 1680
static void is_draw(void)
{
    int cyc = ss_t / IS_LEN, t = ss_t % IS_LEN, hz = 30, ix = 98;
    uint32_t hc = dm_h(cyc, 3);
    int mx = 18 + (int)(hc % 44u), my = 8 + (int)((hc >> 8) % 9u), sway = ss_sin(ss_t * 3) / 50;
    /* sky, moon, the water with the moon's road on it */
    for (int x = 0; x < W; x++) for (int b = 0; b < 6; b++) ss_vd(x, b * 5, b * 5 + 4, b / 2, 1);
    for (int i = 0; i < 26; i++) { uint32_t h = ss_h((uint32_t)i * 7u + hc); if (((h >> 16) + (uint32_t)ss_t / 7u) % 17u) px((int)(h & 127u), (int)((h >> 8) % 22u)); }
    ss_ell(mx, my, 8, 8, 16, 0, 0); ss_ell(mx, my, 6, 6, 16, 1, 0);
    if (hc & 0x10000u) ss_ell(mx + 3, my - 1, 5, 5, 16, 0, 0);
    for (int y = hz; y < H; y++) {
        int d = y - hz + 2;
        for (int x = 0; x < W; x++) {
            int v = ss_sin(x * 40 / d + (y * 97 & 1023) + ss_t * (y & 1 ? 2 : -1)) + ss_sin(x * 9 + y * 50 - ss_t) / 2;
            if (v > 150 - (iabs(x - mx) < 3 + d / 3 ? 75 : 0)) px(x, y);
        }
    }
    for (int x = 0; x < W; x++) if (x & 1) px(x, hz);
    /* the island and its shadow on the water */
    for (int dx = -18; dx <= 18; dx++) {
        int h = isqrt(25 * (324 - dx * dx) / 324), x = ix + dx;
        for (int y = 46; y <= 46 + h; y++) { int wob = ss_sin(y * 170 + ss_t * 5) / 64; px_off(x + wob, y); if (((x + y) & 3) == 0 && y > 46) px(x + wob, y); }
        ss_vd(x, 45 - h, 45, 16, 0); ss_vd(x, 45 - h, 45, 5, 1); px(x, 45 - h);
        if (h > 1 && (ss_h((uint32_t)x * 3u + hc) & 3u) == 0) { px(x, 44 - h); px(x + (sway > 0), 43 - h); }
    }
    /* the tree */
    int tx = ix + 4, lean = (int)(hc >> 20 & 3u) - 1;
    ss_ln(tx, 41, tx + lean, 33, 16, 1, 2); ss_ln(tx + lean, 33, tx + lean * 2 + sway / 2, 26, 16, 1, 2);
    ss_ln(tx + lean, 32, tx + lean - 5, 27, 16, 1, 1);
    for (int k = 0; k < 4; k++) {
        static const int8_t ox[4] = {-7, 6, 0, -1}, oy[4] = {2, 1, -4, 0}, rr[4] = {6, 6, 6, 8};
        int cx = tx + lean * 2 + ox[k] + sway * (4 - oy[k]) / 6, cy = 20 + oy[k];
        ss_ell(cx, cy, rr[k] + 1, rr[k] - 1, 16, 0, 0);
        ss_ell(cx, cy, rr[k], rr[k] - 2, 16, 1, 0);
        ss_ell(cx + 1, cy + 1, rr[k] - 1, rr[k] - 3, 16, 0, 0);
        ss_ell(cx + 1, cy + 1, rr[k] - 1, rr[k] - 3, 6, 1, 0);
    }
    /* the wader */
    if (t < 1260) {
        int fx, wl, feet, bob = (ss_sin(t * 9) > 0), land = t > 1080;
        if (!land) { fx = 2 + t * 74 / 1080; wl = 57 - (fx - 2) * 10 / 74; feet = wl + clamp((80 - fx) / 2, 0, 7) + bob; }
        else { fx = 76 + (t - 1080) * 13 / 180; wl = 99; feet = 47 - (fx - 76) * 6 / 13 + bob; }
        dm_box(fx - 5, feet - 20, fx + 5, feet - 1, 0);
        dm_man(fx, feet, t * 9, 1, 0);
        /* the bags, one a hand */
        ss_ln(fx - 1, feet - 7, fx - 4, feet - 5, 16, 1, 1); ss_ln(fx + 1, feet - 7, fx + 4, feet - 5, 16, 1, 1);
        dm_box(fx - 6, feet - 5, fx - 3, feet - 2 + bob, 16); dm_box(fx + 3, feet - 5, fx + 6, feet - 2 - bob + 1, 10);
        px(fx + 3, feet - 5); px(fx + 6, feet - 5); span(fx + 3, feet - 4, feet - 1 - bob, 1); span(fx + 6, feet - 4, feet - 1 - bob, 1);
        /* the blanket: a great roll across the shoulders, a corner trailing behind */
        ss_ell(fx - 1, feet - 15, 8, 5, 16, 0, 0);
        ss_ell(fx - 1, feet - 15, 7, 4, 16, 1, 0);
        ss_ell(fx, feet - 14, 6, 3, 16, 0, 0); ss_ell(fx, feet - 14, 6, 3, 7, 1, 0);
        for (int k = 0; k < 3; k++) px(fx - 5 + k * 4, feet - 15 + (k & 1));
        ss_tri(fx - 8, feet - 15, fx - 5, feet - 12, fx - 9 - bob, feet - 7, 16, 1);
        if (!land) {   /* the water closes over the legs; rings go out */
            for (int x = fx - 10; x <= fx + 7; x++) span(x, wl + 1, wl + 9, 0);
            for (int k = 0; k < 3; k++) {
                int r = (t + k * 16) % 48, w = 5 + r / 3;
                for (int x = -w; x <= w; x++) if ((x + k + r) & 1 && iabs(x) > w - 4 - k) px(fx - 2 + x, wl + 1 + k * 2 + (iabs(x) < w - 1));
            }
            for (int y = wl + 2; y < wl + 8; y++) { int wob = ss_sin(y * 200 + t * 7) / 50; if (y & 1) px(fx + wob, y), px(fx - 1 + wob, y); }
        }
    } else {   /* there: the blanket spread under the tree, the bags down, sitting */
        int sx = ix - 5;
        ss_quad(ix - 15, 43, ix + 1, 41, ix + 2, 42, ix - 14, 44, 16, 1);
        for (int x = ix - 14; x < ix; x += 3) px_off(x, 43 - (x - ix + 15) / 8);
        dm_box(ix - 17, 40, ix - 14, 42, 16); dm_box(ix + 8, 39, ix + 11, 41, 10); px(ix + 8, 39); px(ix + 11, 39); px(ix + 8, 41); px(ix + 11, 41);
        span(sx, 34, 40, 1); span(sx + 1, 36, 40, 1); ss_ln(sx + 1, 40, sx + 5, 38, 16, 1, 1); ss_ln(sx + 5, 38, sx + 6, 41, 16, 1, 1);
        px(sx - 1, 32); px(sx, 31); px(sx, 32); px(sx, 33); px(sx + 1, 32); px(sx - 1, 33); px(sx + 1, 33); px(sx - 1, 31); px(sx + 1, 31);
    }
    if (t < 64) dm_dark(16 - t / 4);
    if (t > IS_LEN - 100) dm_dark(clamp((t - (IS_LEN - 100)) / 4, 0, 16));
}

/* ---- more dreams (sadnoise's dream log again, 12 Nov 2026): ten more scenes, made the same way: a seed, and every
 * frame drawn from the tick count ---- */
static void dm_blob(int cx, int cy, int rx, int ry, int lv)   /* a soft shape: a pale edge, toned inside */
{
    ss_ell(cx, cy, rx, ry, 16, 1, 0);
    if (rx < 2 || ry < 2) return;
    ss_ell(cx, cy, rx - 1, ry - 1, 16, 0, 0);
    ss_ell(cx, cy, rx - 1, ry - 1, lv, 1, 0);
}
static int dm_get(int x, int y) { return (unsigned)x < W && (unsigned)y < H && (F[x * 2 + (y < 32)] >> (y & 31) & 1u); }
static void dm_flip(int x, int y, int dim) { if (dm_get(x, y)) px_off(x, y); else if (dim) px(x, y); }
/* dm_man with an edge of the other ink round it, to stand clear of whatever is behind */
static void dm_cut(int x, int y, int ph, int ink, int dress)
{
    dm_man(x - 1, y, ph, !ink, dress); dm_man(x + 1, y, ph, !ink, dress); dm_man(x, y - 1, ph, !ink, dress); dm_man(x, y + 1, ph, !ink, dress);
    dm_man(x, y, ph, ink, dress);
}

/* ---- ELEVATOR: a tiny glass lift, up a shaft through floors of plush toys, out into the night, through the trees ---- */
#define EL_LEN 2100
#define EL_GAP 46
static void el_toy(int x, int y, int kind, int r)   /* sitting on line y: 0 a bear, 1 a rabbit, 2 a frog */
{
    if (kind == 2) {
        int rx = r * 3 / 2, er = r / 3 + 1, eo = r * 2 / 3;
        for (int sd = -1; sd <= 1; sd += 2) dm_blob(x + sd * (rx - 2), y - 1, er + 1, 2, 12);
        dm_blob(x, y - r, rx, r, 12);
        for (int sd = -1; sd <= 1; sd += 2) { dm_blob(x + sd * eo, y - r * 2 + 1, er, er, 16); px_off(x + sd * eo, y - r * 2 + 1); }
        for (int dx = -r; dx <= r; dx++) { int my = y - r + 1 - (iabs(dx) > r - 2); px_off(x + dx, my); px_off(x + dx, my - 1); }
        return;
    }
    int hr = r * 2 / 3 + 1, hy = y - r * 2 - hr + 3, tone = kind ? 16 : 12;
    for (int sd = -1; sd <= 1; sd += 2) dm_blob(x + sd * (r - 1), y - 2, r / 3 + 1, r / 3 + 1, tone);
    dm_blob(x, y - r, r, r, tone);
    ss_ell(x, hy, hr + 1, hr + 1, 16, 0, 0);
    for (int sd = -1; sd <= 1; sd += 2) {
        if (kind) { span(x + sd * (hr / 2), hy - hr - r, hy - hr + 1, 1); span(x + sd * (hr / 2 + 1), hy - hr - r + 1, hy - hr + 1, 1); }
        else ss_ell(x + sd * (hr - 1), hy - hr + 1, hr / 3 + 1, hr / 3 + 1, 16, 1, 0);
    }
    dm_blob(x, hy, hr, hr, tone);
    for (int sd = -1; sd <= 1; sd += 2) { px_off(x + sd * (hr / 2), hy - 1); if (r > 6) px_off(x + sd * (hr / 2), hy); }
    px_off(x, hy + hr / 2);
    if (!kind) ss_ell(x, y - r + 1, r / 2, r / 2, 16, 1, 0);
}
static void el_kid(int cx, int cy, int sd, int t)   /* the floating child's head, a camera in its mouth; a hand beckons */
{
    ss_ell(cx, cy, 14, 15, 16, 0, 0);
    ss_ell(cx, cy, 13, 14, 16, 1, 0);
    for (int dx = -9; dx <= 9; dx += 3) span(cx + dx, cy - 17 + (dx & 1), cy - 13, 1);
    for (int s = -1; s <= 1; s += 2) { ss_ell(cx + s * 5, cy - 4, 2, 3, 16, 0, 0); px(cx + s * 5 + sd, cy - 5); }
    dm_box(cx - 6, cy + 3, cx + 6, cy + 11, 0);
    for (int x = cx - 5; x <= cx + 5; x++) { px(x, cy + 4); px(x, cy + 10); }
    span(cx - 5, cy + 4, cy + 10, 1); span(cx + 5, cy + 4, cy + 10, 1);
    ss_ell(cx, cy + 7, 2, 2, 16, 1, 0); px_off(cx, cy + 7);
    if ((ss_h((uint32_t)(t / 45)) % 4u) == 0 && t % 45 < 5)   /* its flash */
        for (int k = 1; k < 6; k++) { px(cx + 3 + k, cy + 5); px(cx + 3 - k, cy + 5); px(cx + 3, cy + 5 - k); px(cx + 3, cy + 5 + k); px(cx + 3 + k / 2, cy + 5 - k / 2); px(cx + 3 - k / 2, cy + 5 + k / 2); }
    int hx = cx + sd * 18, hy = cy + 8 + ss_sin(t * 30) / 42;
    dm_blob(hx, hy, 2, 2, 16); px(hx - 1, hy - 3); px(hx + 1, hy - 3); px(hx - sd * 3, hy);
}
static void el_draw(void)
{
    int cyc = ss_t / EL_LEN, t = ss_t % EL_LEN;
    uint32_t hc = dm_h(cyc, 7);
    int nf = 14 + (int)(hc % 4u), kid = 4 + (int)(hc >> 4 & 3u), kside = (int)(hc >> 8 & 1u);
    int u = t - 90, sc = u < 0 ? 0 : u < 160 ? u * u / 320 : u - 80;          /* how far it has risen */
    int roof = 47 - nf * EL_GAP + sc, e = roof - 400, ly = 22 - clamp(e / 6, 0, 8);
    int lx = 64 + (roof > 40 ? ss_sin(t * 4) * clamp(roof - 40, 0, 64) / 4096 : 0);
    if (roof > 0) {   /* outside: stars, a moon */
        int my = roof - 40 > 11 ? 11 : roof - 40;
        for (int i = 0; i < 34; i++) {
            uint32_t h = ss_h((uint32_t)i * 23u + hc);
            int y = (int)(((h >> 8) + (uint32_t)sc / 14u) % 64u);
            if (y < roof - 1 && ((h >> 16) + (uint32_t)ss_t / 9u) % 13u) px((int)(h & 127u), y);
        }
        if (my > -8) { ss_ell(101, my, 7, 7, 16, 0, 0); ss_ell(101, my, 6, 6, 16, 1, 0); ss_ell(98, my - 1, 5, 5, 16, 0, 0); }
        for (int i = 0; i < 3 && e > 0; i++) {   /* thin cloud going by */
            uint32_t h = ss_h((uint32_t)i * 41u + hc);
            ss_ell((int)(h & 127u), (int)(((h >> 8) + (uint32_t)sc / 2u) % 150u) - 20, 13, 2, 3, 1, 0);
        }
    }
    /* inside: a faint wall behind everything */
    for (int x = 0; x < W && roof < H; x++) ss_vd(x, roof < 0 ? 0 : roof, H - 1, 1, 1);
    /* the trees it climbs through */
    for (int k = 0; k < 4 && roof > 0 && roof < H + 440; k++) {
        static const uint8_t tx[4] = {11, 35, 89, 112};
        uint32_t h = dm_h(cyc, 20 + k);
        int x = tx[k] + (int)(h % 5u), w = 3 + (int)(h >> 4 & 1u) + (k == 0 || k == 3), top = roof - 320 - (int)(h >> 8 & 63u);
        span(x - 1, top, roof - 1, 0); span(x + w, top, roof - 1, 0);
        for (int dx = 0; dx < w; dx++) { span(x + dx, top + dx * 3 % 5, roof - 1, 0); ss_vd(x + dx, top + dx * 3 % 5, roof - 1, dx ? 6 : 16, 1); }
        for (int j = 0; j < 9; j++) {
            uint32_t hj = ss_h(h + (uint32_t)j * 53u);
            int cy = top + 4 + j * 30 + (int)(hj & 7u), sd = (j & 1) ? 1 : -1, cx = x + w / 2 + sd * (7 + (int)(hj >> 4 & 7u)), rx = 9 + (int)(hj >> 8 & 3u);
            if (cy < -12 || cy > H + 12 || cy > roof - 70) continue;
            ss_ln(x + w / 2, cy + 7, cx, cy, 16, 1, 1);
            ss_ell(cx, cy, rx + 1, 7, 16, 0, 0); ss_ell(cx + sd * 5, cy - 4, rx - 2, 6, 16, 0, 0);
            ss_ell(cx, cy, rx, 6, 7, 1, 0); ss_ell(cx + sd * 5, cy - 4, rx - 3, 5, 7, 1, 0);
            for (int dx = -rx + 2; dx <= rx - 2; dx++) if ((dx + j) & 1) px(cx + dx, cy + isqrt(36 * (rx * rx - dx * dx) / (rx * rx)));   /* lit from below */
        }
    }
    /* the land far below, going by */
    if (e > 0) {
        int gy = 64 - clamp(e / 4, 0, 14);
        for (int x = 0; x < W; x++) {
            int wx = x + t * 2, p = wx % 200, tp = p < 24 ? p : p > 175 ? 199 - p : 24, kind = (int)(dm_h(cyc, 300 + wx / 200) % 5u), hg, lv;
            if (kind == 0) { hg = 5 + ss_sin(wx * 9) / 40 + ss_sin(wx * 23) / 90; lv = 5; }           /* hills */
            else if (kind == 1) { hg = 12 - iabs(wx % 36 - 18) * 5 / 9; lv = 2; }                       /* mountains */
            else if (kind == 2) { int q = wx % 6; hg = 3 + (q < 3 ? q * 2 : (5 - q) * 2); lv = 9; }       /* forest */
            else if (kind == 3) { hg = 1; lv = 0; }                                                     /* the sea */
            else { hg = wx % 7 == 6 ? 1 : 2 + (int)(ss_h((uint32_t)(wx / 7) + hc) % 8u); lv = 0; }     /* a city */
            hg = 1 + (hg - 1) * tp / 24;
            int top = gy + 9 - hg;
            span(x, top, H - 1, 0); ss_vd(x, top + 1, H - 1, lv, 1); px(x, top);
            if (kind == 1 && hg > 8) px(x, top + 1);
            if (kind == 3) for (int y = top + 2; y < H; y += 3) if ((wx + y * 5) % 11 < 3) px(x, y);
            if (kind == 4) for (int y = top + 2; y < H; y += 3) if ((ss_h((uint32_t)(wx * 64 + y - gy)) & 3u) == 0) px(x, y);
        }
    }
    /* the floors, and who lives on them */
    for (int k = clamp((sc - 60) / EL_GAP, 0, nf); k <= nf; k++) {
        int fy = 47 - k * EL_GAP + sc, hit = k > 0 && fy > ly - 2;
        if (fy < -3) break;
        if (fy < H)
            for (int x = 0; x < W; x++) {
                if (hit && iabs(x - 64) < 13 + (int)(ss_h((uint32_t)(x + k * 131)) & 1u)) continue;   /* torn through */
                span(x, fy, fy + 2, k == nf); px(x, fy); if (x & 1) px(x, fy + 2);
            }
        if (hit && fy - ly < 44) {   /* stuffing in the air */
            int age = fy - ly + 2;
            for (int i = 0; i < 10; i++) {
                uint32_t h = ss_h((uint32_t)(k * 16 + i) * 7u + hc);
                px(lx + ((int)(h % 25u) - 12) * (10 + age) / 10, fy - 4 - (int)(h >> 8 & 7u) + age * age / 40);
            }
        }
        if (k < nf && fy > 2 && fy < 108)
            for (int sd = 0; sd < 2; sd++) {
                uint32_t h = dm_h(cyc, 100 + k * 2 + sd);
                int cx = sd ? 105 : 22, m = (int)(h % 7u);
                if (k == kid && sd == kside) { el_kid(cx, fy - 23 + ss_sin(t * 6) / 50, sd ? -1 : 1, t); continue; }
                if (m == 0) el_toy(cx, fy - 1, 2, 11);                                   /* the giant frog */
                else if (m < 3) el_toy(cx, fy - 1, (int)(h >> 4 & 1u), 8);
                else if (m < 5) { el_toy(cx - 10, fy - 1, (int)(h >> 4 & 15u) % 3, 5); el_toy(cx + 10, fy - 1, (int)(h >> 8 & 15u) % 3, 5 + (int)(h >> 12 & 1u)); }
                else                                                                     /* a wall of them */
                    for (int r = 0; r < 3; r++)
                        for (int c = 0; c < 4 - (r & 1); c++) {
                            int bx = cx - 16 + c * 11 + (r & 1) * 5, by = fy - 7 - r * 10;
                            ss_ell(bx, by, 6, 6, 16, 0, 0);
                            for (int sd2 = -1; sd2 <= 1; sd2 += 2) { px(bx + sd2 * 4, by - 5); px(bx + sd2 * 3, by - 5); px(bx + sd2 * 4, by - 4); if ((h >> (r * 4 + c) & 1u)) px(bx + sd2 * 4, by - 6), px(bx + sd2 * 4, by - 7); }
                            dm_blob(bx, by, 5, 5, 11 + (int)((h >> (r * 5 + c)) & 5u));
                            px_off(bx - 2, by - 1); px_off(bx + 2, by - 1); px_off(bx, by + 1); px_off(bx, by + 2);
                        }
            }
    }
    /* its two lamps, shining up */
    if (roof > ly + 2) {
        int sw = ss_sin(t * 3) / 32;
        ss_tri(lx - 8, ly - 2, lx - 34 + sw, -1, lx - 11 + sw, -1, 4, 1);
        ss_tri(lx + 8, ly - 2, lx + 11 - sw, -1, lx + 34 - sw, -1, 4, 1);
    }
    /* the lift: glass, a seat, someone in it, a dead dashboard */
    dm_box(lx - 10, ly, lx + 10, ly + 24, 0);
    for (int x = lx - 10; x <= lx + 10; x++) { px(x, ly); px(x, ly + 1); px(x, ly + 24); }
    span(lx - 10, ly, ly + 24, 1); span(lx + 10, ly, ly + 24, 1);
    span(lx - 9, ly - 2, ly - 1, 1); span(lx - 8, ly - 2, ly - 1, 1); span(lx + 8, ly - 2, ly - 1, 1); span(lx + 9, ly - 2, ly - 1, 1);
    px(lx - 7, ly + 6); px(lx - 6, ly + 5); px(lx - 5, ly + 4); px(lx - 7, ly + 9); px(lx - 6, ly + 8);
    for (int x = lx - 5; x <= lx + 1; x++) px(x, ly + 19);
    span(lx - 6, ly + 11, ly + 19, 1); span(lx - 2, ly + 20, ly + 23, 1);
    ss_ell(lx - 2, ly + 8, 2, 2, 16, 1, 0);
    span(lx - 3, ly + 11, ly + 18, 1); span(lx - 2, ly + 11, ly + 18, 1);
    for (int x = lx - 1; x <= lx + 2; x++) { px(x, ly + 17); px(x, ly + 18); }
    span(lx + 3, ly + 17, ly + 22, 1); px(lx + 4, ly + 22);
    ss_ln(lx - 1, ly + 13, lx + 3, ly + 15, 16, 1, 1);
    for (int x = lx + 5; x <= lx + 8; x++) { px(x, ly + 12); px(x, ly + 16); }
    span(lx + 5, ly + 12, ly + 16, 1); span(lx + 8, ly + 12, ly + 16, 1); span(lx + 7, ly + 17, ly + 23, 1);
    if (u > 0)   /* the air it leaves behind */
        for (int i = 0; i < 6; i++) {
            uint32_t h = ss_h((uint32_t)(i * 7 + t / 3));
            int x = lx - 8 + i * 3 + (int)(h & 1u), y = ly + 27 + (int)(h >> 2 & 7u);
            span(x, y, y + 1 + (int)(h >> 6 & 3u), 1);
        }
    if (t < 64) dm_dark(16 - t / 4);
    if (t > EL_LEN - 72) dm_dark(clamp((t - (EL_LEN - 72)) / 4, 0, 16));
}

/* ---- GOLDMOSS: a pond at night under square glittering moss; a magician says hush; it parts, and flowers ---- */
#define GM_LEN 1980
static void gm_flower(int cx, int cy, int s)   /* four petals round a dark eye */
{
    int q = s * 2 / 3;
    if (s < 2) { px(cx, cy); if (s) { px(cx - 1, cy); px(cx + 1, cy); } return; }
    ss_ell(cx, cy, s + 1, q + 1, 16, 0, 0);
    ss_ell(cx, cy, s, q, 16, 1, 0);
    px_off(cx, cy); if (s > 3) { px_off(cx - 1, cy); px_off(cx + 1, cy); }
    for (int k = 0; k < 4; k++) { int sx = (k & 1) ? 1 : -1, sy = (k & 2) ? 1 : -1; px_off(cx + sx * (s * 3 / 4), cy + sy * (q * 3 / 4 + (q < 2))); if (s > 3) px_off(cx + sx * (s * 3 / 4 - 1), cy + sy * (q * 3 / 4 - 1)); }
}
static void gm_draw(void)
{
    static const uint8_t ry[6] = {30, 34, 39, 45, 52, 60}, rw[5] = {8, 10, 12, 14, 16};
    int cyc = ss_t / GM_LEN, t = ss_t % GM_LEN;
    uint32_t hc = dm_h(cyc, 9);
    int sep = clamp((t - 480) / 2, 0, 256), grow = clamp((t - 1250) / 2, 0, 256);
    int mx = 62 + (int)(hc % 50u);
    /* stars, a moon, the far bank and what grows on it */
    for (int i = 0; i < 24; i++) { uint32_t h = ss_h((uint32_t)i * 7u + hc); if (((h >> 16) + (uint32_t)ss_t / 8u) % 15u) px((int)(h & 127u), (int)((h >> 8) % 19u)); }
    ss_ell(mx, 8, 6, 6, 16, 0, 0); ss_ell(mx, 8, 5, 5, 16, 1, 0); ss_ell(mx + 3, 7, 4, 4, 16, 0, 0);
    for (int x = 0; x < W; x++) {
        uint32_t h = ss_h((uint32_t)x * 5u + hc);
        int hg = 4 + ss_sin(x * 21 + (int)(hc & 1023u)) / 50 + (int)(h & 1u) + grow * (2 + (int)(h >> 4 & 255u) % 6) / 256;
        ss_vd(x, 28 - hg, 28, 16, 0); ss_vd(x, 28 - hg, 28, 4, 1);
        if (grow > 40 && (h >> 9 & 3u) == 0) { px(x, 28 - hg); px(x + ((h >> 12 & 1u) ? 1 : -1), 27 - hg); }
    }
    /* open water */
    for (int i = 0; i < 26; i++) {
        uint32_t h = ss_h((uint32_t)i * 13u + 5u);
        int x = (int)(h % 126u), y = 31 + (int)((h >> 8) % 32u);
        if (((h >> 16) + (uint32_t)t / 11u) % 4u == 0) { px(x, y); px(x + 1, y); }
    }
    /* the moss: squares, row on row to the far bank */
    for (int r = 0; r < 5; r++) {
        int w = rw[r], hg = ry[r + 1] - ry[r], n = W / w + 2;
        for (int c = 0; c < n; c++) {
            uint32_t h = ss_h((uint32_t)(r * 37 + c) * 11u + hc);
            int cx = 64 + (c - n / 2) * w + w / 2, cy = ry[r] + hg / 2;
            int b = clamp((t - (820 + (cx + (63 - cy) * 2) * 3 + (int)(h % 140u))) / 5, 0, 20);   /* its flowering, spreading from the magician's side */
            int hw = (w - 2) * (512 - sep) / 1024, hh = (hg - 2) * (512 - sep) / 1024;
            cx += ss_sin(t * 2 + (int)(h & 1023u)) * sep / (256 * 48); cy += ss_cos(t * 3 + (int)(h >> 10 & 1023u)) * sep / (256 * 90);
            if (b < 10) {
                int lit = ((h >> 20) + (uint32_t)t / 6u) % 17u == 0;
                hw = hw * (10 - b) / 10; hh = hh * (10 - b) / 10;
                for (int x = cx - hw; x <= cx + hw; x++) { ss_vd(x, cy - hh, cy + hh, 16, 0); ss_vd(x, cy - hh, cy + hh, lit ? 16 : 8, 1); }
                if (((h >> 24) + (uint32_t)t / 9u) % 7u == 0) { px(cx - hw + (int)(h >> 4) % (hw + 1), cy); px(cx - hw + (int)(h >> 4) % (hw + 1) + 1, cy); }
            }
            if (b > 6) gm_flower(cx, cy, (b - 6) * (w / 4 + 1) / 14);
        }
    }
    /* his bit of bank, and the magician: a finger to his lips */
    ss_ell(8, 35, 18, 6, 16, 0, 0); ss_ell(8, 35, 17, 5, 5, 1, 0);
    for (int x = 0; x < 24; x++) if (x & 1) px(x, 35 - isqrt(25 * (289 - (x - 8) * (x - 8)) / 289));
    {
        int arm = clamp((t - 300) / 4, 0, 16) - clamp((t - 1720) / 4, 0, 16), hx = 20 - 4 * arm / 16, hy = 26 - 14 * arm / 16;
        span(4, 14, 33, 1); px(3, 12); px(5, 12); px(4, 11); px(4, 13); px(4, 12);                    /* his staff */
        ss_tri(13, 12, 5, 34, 21, 34, 16, 0); ss_tri(13, 13, 7, 33, 19, 33, 16, 1);                     /* the robe, stars on it */
        px_off(11, 25); px_off(15, 29); px_off(12, 30); px_off(14, 22); px_off(10, 31); px_off(16, 26);
        ss_ell(13, 11, 4, 4, 16, 0, 0); ss_ell(13, 11, 3, 3, 10, 1, 0); px_off(14, 10); px_off(15, 10);
        ss_tri(11, 14, 15, 14, 13, 19, 16, 1);
        ss_tri(7, 8, 19, 8, 13, -5, 16, 0); ss_tri(9, 7, 17, 7, 13, -3, 16, 1);
        for (int x = 7; x <= 19; x++) { px(x, 8); px_off(x, 9); }
        ss_ln(17, 19, hx + 1, hy, 16, 0, 2); ss_ln(16, 19, hx, hy, 16, 1, 1); px(hx, hy - 1);
        if (t > 364 && t < 600)   /* hush, going out over the water */
            for (int k = 0; k < 3; k++) {
                int r = (t - 364) / 3 - k * 10;
                if (r > 5 && r < 60) for (int a = -60; a <= 190; a += 14) ss_p(17 + ss_cos(a) * r / 127, 12 + ss_sin(a) * r * 2 / 381, 16 - r / 4, 1);
            }
    }
    /* then leaves, everywhere */
    for (int i = 0; i < 7 && grow; i++) {
        uint32_t h = dm_h(cyc, 50 + i);
        int len = clamp((t - 1250 - i * 45) / 4, 0, 56), x0 = 8 + i * 18 + (int)(h % 12u), sd = (h >> 8 & 1u) ? 1 : -1;
        for (int j = 0; j < len; j++) {
            int x = x0 + sd * j / 4 + ss_sin(j * 30 + (int)(h >> 10 & 1023u)) / 20, y = 63 - j * 3 / 4;
            px_off(x - 1, y); px_off(x + 1, y); px(x, y);
            if (j % 8 == 5 && j < len - 3) { int ls = (j / 8 & 1) ? 1 : -1; ss_ell(x + ls * 4, y - 1, 4, 2, 16, 0, 0); ss_ell(x + ls * 4, y - 1, 3, 1, 16, 1, 0); px_off(x + ls * 4, y - 1); px_off(x + ls * 5, y - 1); }
        }
    }
    if (t < 64) dm_dark(16 - t / 4);
    if (t > GM_LEN - 72) dm_dark(clamp((t - (GM_LEN - 72)) / 4, 0, 16));
}

/* ---- TREBUCHT: a basement show. A trebuchet tied to a recliner, its weight swinging, someone climbing it; a wet
 * pufferfish of an instrument whose gills are notes ---- */
static void tb_note(int x, int y, int k)
{
    ss_ell(x, y, 1, 1, 16, 1, 0); span(x + 1, y - 5, y, 1);
    if (k & 1) { px(x + 2, y - 5); px(x + 3, y - 4); }
}
static void tb_draw(void)
{
    int t = ss_t, pvx = 58, pvy = 20;
    /* the basement: joists, a block wall, the floor, one bulb */
    for (int x = 0; x < W; x++) {
        ss_vd(x, 0, 2, (x % 21) < 3 ? 16 : 5, 1); px(x, 3);
        for (int j = 0; j < 6; j++) {
            if ((x & 3) == 0) px(x, 11 + j * 8);
            if (((x + (j & 1) * 8) & 15) == 0) { px(x, 6 + j * 8); px(x, 8 + j * 8); }
        }
        px(x, 55); ss_vd(x, 56, H - 1, 2, 1);
    }
    {
        int fl = (int)(ss_h((uint32_t)(t / 6)) % 13u) == 0;
        ss_ell(86, 10, 14, 10, 16, 0, 0); ss_ell(86, 10, 13, 9, 2, 1, 0); ss_ell(86, 10, 6, 5, fl ? 3 : 6, 1, 0);
        span(86, 4, 7, 1); ss_ell(86, 10, 2, 2, 16, 1, 0);
    }
    /* the recliner, rocked by the rope */
    int amp = 34 + (int)(dm_h(t * 5 >> 10, 3) % 34u), a = 548 + ss_sin(t * 5) * amp / 127;
    int ex = pvx + ss_cos(a) * 38 / 127, ey = pvy + ss_sin(a) * 38 / 127, sx = pvx - ss_cos(a) * 13 / 127, sy = pvy - ss_sin(a) * 13 / 127;
    int rk = clamp((ey - 8) / 5, -1, 3);
    ss_ln(ex, ey, 8 - rk, 33 + rk, 16, 1, 1);
    dm_box(12, 50, 26, 54, 0); for (int x = 12; x <= 26; x++) { px(x, 50); if (x & 1) px(x, 53); } span(12, 50, 54, 1); span(26, 50, 54, 1);
    ss_quad(8, 49, 14, 47, 11 - rk, 32 + rk, 5 - rk, 33 + rk, 16, 1);
    dm_box(9, 45, 28, 49, 16); for (int x = 10; x <= 27; x += 3) px_off(x, 47);
    dm_box(13, 40, 25, 42, 16); span(24, 43, 44, 1); span(25, 43, 44, 1);
    ss_ln(28, 46, 36, 46 - clamp(rk + 1, 0, 4) * 2, 16, 1, 2);
    /* the frame, the arm, the weight */
    ss_ln(pvx, pvy, 45, 54, 16, 1, 2); ss_ln(pvx, pvy, 71, 54, 16, 1, 2);
    ss_ln(50, 40, 66, 40, 16, 1, 1);
    for (int x = 40; x <= 77; x++) { px(x, 54); px(x, 55); }
    for (int y = 26; y < 52; y += 6) { int x = pvx - 13 * (y - pvy) / 34; px(x - 2, y); px(x - 3, y); px(x - 4, y); }
    ss_ln(sx, sy, ex, ey, 16, 1, 2);
    ss_ell(pvx, pvy, 2, 2, 16, 1, 0); px_off(pvx, pvy);
    {
        int b = ss_sin(t * 5 - 170) * 100 / 127, wx = sx + ss_sin(b) * 9 / 127, wy = sy + ss_cos(b) * 9 / 127;
        ss_ln(sx, sy, wx, wy, 16, 1, 1);
        dm_box(wx - 5, wy - 1, wx + 5, wy + 9, 0); dm_box(wx - 4, wy, wx + 4, wy + 8, 7);
        for (int k = -4; k <= 4; k++) { px(wx + k, wy); px(wx + k, wy + 8); px(wx - 4, wy + 4 + k); px(wx + 4, wy + 4 + k); }
    }
    /* the climber, up the leg and down again */
    {
        int pass = t / 1100, tt = t % 1100, top = 140 + (int)(dm_h(pass, 5) % 90u);
        int q = tt < 400 ? tt * top / 400 : tt < 640 ? top : tt < 1040 ? top - (tt - 640) * top / 400 : 0;
        int mx = 42 + 13 * q / 256, my = 54 - 34 * q / 256, moving = tt < 400 || (tt >= 640 && tt < 1040);
        dm_cut(mx, my, moving ? tt * 26 : 0, 1, 0);
        if (q > 0 && moving) ss_ln(mx, my - 8, mx + 3, my - 11 + (tt / 16 & 1) * 2, 16, 1, 1);
        if (q > 0 && !moving) ss_ln(mx - 1, my - 8, mx - 4, my - 12 + (ss_sin(t * 20) > 0), 16, 1, 1);
    }
    /* the pufferfish on its stand: it breathes, it drips, and notes come out of its gills */
    {
        int cx = 101, cy = 36, r = 8 + ss_sin(t * 4) / 60;
        span(cx, cy + r, 54, 1); ss_ln(cx, 49, cx - 5, 54, 16, 1, 1); ss_ln(cx, 49, cx + 5, 54, 16, 1, 1);
        for (int g = 0; g < 1024; g += 93) { int c = ss_cos(g), s = ss_sin(g); ss_ln(cx + c * r / 127, cy + s * r / 127, cx + c * (r + 3) / 127, cy + s * (r + 3) / 127, 16, 1, 1); }
        ss_ell(cx, cy, r + 1, r, 16, 0, 0); dm_blob(cx, cy, r, r - 1, 13);
        ss_ell(cx - 3, cy - 2, 2, 2, 16, 0, 0); px(cx - 3, cy - 2);
        for (int x = cx - r - 4; x <= cx - r; x++) { px(x, cy + 2); px(x, cy + 3); }
        px(cx - r - 5, cy + 1); px(cx - r - 5, cy + 4);
        for (int k = 0; k < 3; k++) { int gx = cx + 1 + k * 2, op = (t / 53) % 3 == k; span(gx, cy - 2 - op, cy + 2 + op, 0); }
        for (int k = 0; k < 4; k++) {
            int p = (t + k * 53) % 212;
            uint32_t h = dm_h((t + k * 53) / 212, 40 + k);
            if ((h & 3u) == 0 || p > 140) continue;
            tb_note(cx + 6 + p / 8 + ss_sin(p * 9 + (int)(h & 1023u)) / 30, cy - 5 - p / 5, (int)(h >> 10 & 7u));
        }
        for (int k = 0; k < 3; k++) {
            int p = (t + k * 41) % 120, dx = cx - 5 + k * 5, y0 = cy + r + 1 - (k != 1);
            if (p < 60) px(dx, y0 + (p > 40));
            else if (p < 100) { int y = y0 + (p - 60) * (p - 60) / 100; if (y > 53) y = 53; px(dx, y); px(dx, y - 1); }
            else { int s = (p - 100) / 4; px(dx - 1 - s, 53 - (s < 3)); px(dx + 1 + s, 53 - (s < 3)); }
        }
        for (int x = cx - 9; x <= cx + 9; x++) if ((x + t / 16) & 1) px(x, 57);
    }
    /* the heads of whoever came */
    for (int i = 0; i < 5; i++) {
        uint32_t h = ss_h((uint32_t)i * 29u + DM.seed);
        tr_person(10 + i * 27 + (int)(h % 9u), 60 + (int)(h >> 4 & 1u) + (ss_sin(t * 5 + i * 230) > 100), 4, (int)(h >> 8 & 7u));
    }
}

/* ---- VALLEY: from the window of a train above the ground: clear water in a valley between dark mountains, low fog,
 * the sky going down to dusk; someone wading across the shallows to their bags ---- */
#define VL_LEN 2400
static int vl_ridge(int wx, uint32_t s)   /* 0..255, smooth */
{
    int i = wx >> 4, f = wx & 15;
    int a = (int)(ss_h((uint32_t)i * 7u + s) & 255u), b = (int)(ss_h((uint32_t)(i + 1) * 7u + s) & 255u);
    f = f * f * (48 - 2 * f) >> 8;
    return (a * (16 - f) + b * f) >> 4;
}
static void vl_draw(void)
{
    int cyc = ss_t / VL_LEN, t = ss_t % VL_LEN, hz = 36;
    uint32_t hc = dm_h(cyc, 13);
    int dusk = 13 - t * 12 / VL_LEN;
    if (dusk < 9) for (int i = 0; i < 20; i++) { uint32_t h = ss_h((uint32_t)i * 7u + hc); if ((int)(h >> 20 & 7u) >= dusk - 2 && ((h >> 16) + (uint32_t)ss_t / 8u) % 9u) px(4 + (int)(h % 120u), 3 + (int)((h >> 8) % 20u)); }
    for (int x = 4; x < W - 4; x++) {
        int wf = x + t / 10, wn = x + t / 3;
        int hf = 7 + vl_ridge(wf * 2, hc) / 16 + vl_ridge(wf * 6, hc + 9u) / 48;
        int env = ss_sin(wn * 2 + (int)(hc >> 8 & 1023u)), hn = env > 0 ? env * (12 + vl_ridge(wn * 3, hc + 5u) / 11) / 127 : 0;
        ss_vd(x, 2, 58, dusk, 1);
        /* the far range, the near one darker still, and both again in the water */
        span(x, hz - hf, hz + 1 + hf * 2 / 3, 0); ss_vd(x, hz - hf + 1, hz, dusk > 4 ? 2 : 1, 1);
        if (dusk < 9) px(x, hz - hf);
        if (hn) { span(x, hz - hn, hz + 1 + hn * 2 / 3, 0); if (dusk < 9 && (x & 1)) px(x, hz - hn); }
    }
    for (int k = 0; k < 5; k++) {   /* the water, hardly moving */
        static const uint8_t wd[5] = {4, 7, 11, 16, 21};
        int d = wd[k], y = hz + d;
        for (int x = 4; x < W - 4; x++) { int xx = x + t * d / 24 + d * 37, ph = xx % 13; if (ph < 4 + d / 4 && (ss_h((uint32_t)(xx / 13 + k * 64)) & 3u) == 0) dm_flip(x, y, ph < 3); }
    }
    for (int x = 4; x < W - 4; x++) {   /* fog lying where the water meets the land */
        int w1 = ss_sin(x * 7 + t) / 50, w2 = ss_sin(x * 5 - t / 2 + 300) / 40;
        span(x, hz - 2 + w1, hz + 1, 0); ss_vd(x, hz - 2 + w1, hz + 1, dusk + 4 > 12 ? 12 : dusk + 4, 1); ss_vd(x, hz - 6 + w2, hz - 3 + w1, dusk / 2 + 1, 1);
    }
    /* the shallows, the bags, the wader */
    {
        int tt = t % 800;
        uint32_t hp = dm_h(cyc * 4 + t / 800, 21);
        int sx = 176 - tt / 2, wx = sx - 24 + tt / 20;
        if ((hp & 3u) && sx > -36 && sx < W + 36) {
            ss_ell(sx, 52, 31, 5, 16, 0, 0); ss_ell(sx, 52, 30, 4, 13, 1, 0);
            dm_box(sx + 17, 46, sx + 22, 51, 0); for (int x = sx + 18; x <= sx + 21; x++) px(x, 46); px(sx + 19, 45); px(sx + 20, 45);
            dm_box(sx + 24, 48, sx + 28, 51, 0); for (int x = sx + 25; x <= sx + 27; x++) px(x, 48);
            dm_cut(wx, 53, tt * 8, 0, 0);
            for (int k = 0; k < 2; k++) { int r = (tt + k * 20) % 40, w = 3 + r / 5; for (int x = -w; x <= w; x++) if (((x + r) & 1) && iabs(x) > w - 3) px_off(wx + x, 53 + k); }
        }
    }
    /* close to: a mast going by */
    {
        int mp = 150 - (t * 4) % 560;
        if (mp > -6 && mp < W) {
            for (int dx = 0; dx < 5; dx++) span(mp + dx, 1, 58, 0);
            ss_vd(mp - 1, 1, 58, 8, 1); ss_vd(mp + 5, 1, 58, 8, 1);
            for (int dx = -6; dx <= 10; dx++) { span(mp + dx, 4, 6, 0); px(mp + dx, 7); }
        }
    }
    /* the window */
    for (int x = 0; x < W; x++) {
        span(x, 0, 1, 0); span(x, 59, H - 1, 0);
        if (x < 4 || x >= W - 4) span(x, 0, H - 1, 0);
        else { px(x, 59); if (x > 5 && x < W - 6) px(x, 1); if ((x & 3) == 0) px(x, 61); }
    }
    span(3, 4, 59, 1); span(W - 4, 4, 59, 1); px(4, 3); px(5, 2); px(W - 5, 3); px(W - 6, 2);
    px_off(4, 2); px_off(W - 5, 2);
    if (t < 64) dm_dark(16 - t / 4);
    if (t > VL_LEN - 72) dm_dark(clamp((t - (VL_LEN - 72)) / 4, 0, 16));
}

/* ---- BLUEBAR: a bar in deep blue light, almost nothing to see: a bass horn the size of the wall, tweeters above and
 * below; the room shows itself for a moment when the bass comes through ---- */
static void bb_rect(int cx, int cy, int hw, int hh, int st, int ph)
{
    for (int x = cx - hw; x <= cx + hw; x++) if ((x + ph) % st == 0) { px(x, cy - hh); px(x, cy + hh); }
    for (int y = cy - hh; y <= cy + hh; y++) if ((y + ph) % st == 0) { px(cx - hw, y); px(cx + hw, y); }
}
static void bb_draw(void)
{
    static const uint8_t hw[8] = {3, 6, 10, 16, 24, 34, 46, 58}, hh[8] = {1, 2, 4, 6, 9, 12, 15, 17};
    int t = ss_t, bn = t / 54, bp = t % 54, cx = 64, cy = 26;
    uint32_t hb = dm_h(bn, 1), hbar = dm_h(bn / 8, 2);
    int hit = (hb % 7u) != 0 || (bn & 3) == 0, kp = hit ? bp / 3 : 99, k2 = (hbar & 1u) && (bn & 1) && bp >= 27 ? (bp - 27) / 3 : 99;
    int glow = (kp >= 6 && kp <= 10) || (k2 >= 6 && k2 <= 10);
    if (glow) for (int x = 0; x < W; x++) ss_vd(x, 0, 52, 1, 1);
    for (int i = 0; i < 30; i++) {   /* smoke in the light */
        uint32_t h = ss_h((uint32_t)i * 31u + 2u);
        if (((h >> 16) + (uint32_t)t / 12u) % 5u == 0) px((int)((h + (uint32_t)t / (16u + (h >> 20 & 7u))) & 127u), (int)((h >> 8) % 52u));
    }
    /* the horn: mouth to throat */
    for (int k = 0; k < 8; k++) {
        int st = (k == kp || k == k2) ? 1 : 2 + (k + 1 != kp && k + 1 != k2 && k > 2);
        bb_rect(cx, cy, hw[k], hh[k], st, k);
        if (k < 7) for (int j = 0, n = hw[k + 1] - hw[k]; j < n; j++) if ((j + k) % 4 == 0)
            for (int q = 0; q < 4; q++) px(cx + ((q & 1) ? 1 : -1) * (hw[k] + j), cy + ((q & 2) ? 1 : -1) * (hh[k] + j * (hh[k + 1] - hh[k]) / n));
    }
    dm_box(cx - 2, cy, cx + 2, cy, kp < 2 || k2 < 2 ? 16 : 6);
    /* the tweeters */
    for (int i = 0; i < 12; i++) {
        int x = 14 + (i % 6) * 20, y = i < 6 ? 3 : 49, on = (int)(dm_h(t / 9, 10 + i) % 7u) == 0 && (hbar >> (i + 4) & 1u);
        px(x - 3, y); px(x + 3, y); px(x, y - 3); px(x, y + 3);
        if (on || glow) { px(x - 2, y - 2); px(x + 2, y - 2); px(x - 2, y + 2); px(x + 2, y + 2); }
        if (on) { px(x, y); px(x - 1, y); px(x + 1, y); px(x, y - 1); px(x, y + 1); px(x - 5, y); px(x + 5, y); px(x - 3, y - 1); px(x + 3, y + 1); px(x - 3, y + 1); px(x + 3, y - 1); }
    }
    /* the bar, a glass on it, and who sits there, seen from behind */
    for (int x = 0; x < W; x++) { span(x, 54, H - 1, 0); if ((x & 1) || glow) px(x, 54); if ((x & 7) == 3) px(x, 58); }
    for (int d = 0; d <= 4; d++) { px(70 + d, 45 + d); px(78 - d, 45 + d); }
    for (int x = 70; x <= 78; x++) if ((x & 1) == 0 || glow) px(x, 45);
    span(74, 49, 52, 1); px(72, 53); px(73, 53); px(75, 53); px(76, 53);
    if ((int)(ss_h((uint32_t)(t / 20)) % 9u) == 0) { px(77, 43); px(77, 44); px(76, 44); px(78, 44); px(77, 45); }
    for (int x = 88; x <= 91; x++) { px(x, 53); if (glow) px(x, 44); } span(88, 46, 53, glow ? 1 : 0); span(91, 46, 53, 1); px(89, 43); px(90, 43); px(89, 44); px(90, 45);
    for (int i = 0; i < 3; i++) {
        static const uint8_t sx[3] = {16, 44, 108};
        uint32_t h = ss_h((uint32_t)i * 17u + DM.seed);
        int x = sx[i] + (int)(h % 7u), sd = x < 64 ? 1 : -1, hy = 45 + (int)(h >> 4 & 1u) + (i == (bn / 16) % 3 && bp < 9) + (ss_sin(t * 2 + i * 300) > 110);
        for (int dx = -10; dx <= 10; dx++) {
            int top = hy + 7 + (iabs(dx) > 6 ? (iabs(dx) - 6) * 2 : 0);
            span(x + dx, top, H - 1, 0);
            if (dx * sd > 1 && (glow || (dx & 3) == 0)) px(x + dx, top);
        }
        ss_ell(x, hy, 5, 5, 16, 0, 0);
        px(x + sd * 5, hy); px(x + sd * 4, hy - 3);
        if (glow) { px(x + sd * 5, hy - 1); px(x + sd * 5, hy + 1); px(x + sd * 5, hy - 2); px(x + sd * 3, hy - 4); px(x + sd * 4, hy + 3); px(x + sd * 2, hy - 5); }
    }
}

/* ---- LILYPADS: it rained, the street is a lake, and the cars go by on the lily pads. No school ---- */
static void lp_pad(int x, int y, int rx, int ry, int k)
{
    int sd = (k & 1) ? 1 : -1;
    ss_ell(x, y, rx + 1, ry + 1, 16, 0, 0);
    ss_ell(x, y, rx, ry, 16, 1, 0);
    for (int k = 1; k <= rx; k++) px_off(x + sd * k, y - (k > rx / 2 && ry > 1));
    if (rx > 4 && (k & 6) == 0) { ss_ell(x - sd * 2, y - ry - 2, 3, 2, 16, 0, 0); ss_ell(x - sd * 2, y - ry - 2, 2, 1, 16, 1, 0); px(x - sd * 2, y - ry - 4); px_off(x - sd * 2, y - ry - 2); }
}
static void lp_draw(void)
{
    int t = ss_t;
    uint32_t hs = DM.seed;
    /* a low sky; the far side of the street: houses, a lamp, the school */
    for (int x = 0; x < W; x++) ss_vd(x, 0, 28, 2, 1);
    for (int i = 0; i < 4; i++) {
        uint32_t h = ss_h((uint32_t)i * 11u + hs);
        int x0 = i < 3 ? 1 + i * 19 : 116, w = 15, top = 14 + (int)(h & 3u);
        dm_box(x0, top, x0 + w, 28, 0);
        ss_tri(x0 - 1, top, x0 + w + 1, top, x0 + w / 2, top - 6 - (int)(h >> 2 & 1u), 16, 1);
        span(x0, top, 28, 1); span(x0 + w, top, 28, 1);
        for (int k = 0; k < 2; k++) {
            int wx = x0 + 3 + k * 7, lit = (int)(((h >> (4 + k)) + (uint32_t)t / 700u) & 1u);
            if (lit) dm_box(wx, top + 3, wx + 2, top + 6, 16); else { px(wx, top + 3); px(wx + 2, top + 3); px(wx, top + 6); px(wx + 2, top + 6); }
        }
        span(x0 + 7, 24, 28, 1); span(x0 + 9, 24, 28, 1); px(x0 + 8, 23);
    }
    span(59, 9, 28, 1); px(60, 9); px(61, 9); px(61, 10); ss_ell(61, 12, 4, 3, 5, 1, 0); px(61, 11); px(60, 11); px(62, 11);
    dm_box(66, 12, 113, 28, 0);
    for (int x = 66; x <= 113; x++) { px(x, 12); if (x % 6 == 0 && x > 66 && x < 112) { px(x, 25); px(x, 26); px(x + 1, 25); px(x + 1, 26); } }
    span(66, 12, 28, 1); span(113, 12, 28, 1);
    dm_box(85, 5, 93, 11, 0); ss_tri(84, 5, 94, 5, 89, 1, 16, 1); span(85, 5, 11, 1); span(93, 5, 11, 1); px(89, 8); px(89, 9); px(88, 9); px(90, 9);
    span(109, 1, 11, 1); ss_tri(110, 2, 110, 6, 114 + (ss_sin(t * 12) > 0), 4, 16, 1);
    dm_box(69, 15, 109, 22, 16); text(72, 16, "NO SCHOOL", 0);
    for (int x = 0; x < W; x++) if (x & 1) px(x, 29);
    /* all of it again in the water, broken */
    for (int y = 31; y < 39; y += 2) {
        int wob = ss_sin(y * 150 + t * 5) / 64;
        for (int x = y >> 1 & 1; x < W; x += 2) if (dm_get(x + wob, 58 - y)) px(x, y);
    }
    /* the far pads */
    for (int i = 0; i < 6; i++) { uint32_t h = ss_h((uint32_t)i * 23u + hs); lp_pad(8 + i * 22 + (int)(h % 9u), 37 + (int)(h >> 4 & 1u), 3, 1, (int)(h >> 8 & 7u) | 2); }
    /* two lanes of pads, and the cars on them: each pad dips under a wheel */
    for (int ln = 0; ln < 2; ln++) {
        int y = ln ? 58 : 46, rx = ln ? 9 : 6, ry = ln ? 3 : 2, sp = ln ? 24 : 16;
        int u = ln ? t * 2 / 3 + 77 : t / 2, len = ln ? 290 : 230, cx = ln ? 150 - u % len : u % len - 40, dir = ln ? -1 : 1;
        uint32_t hp = dm_h(u / len, 80 + ln);
        int has = (hp & 3u) != 0;
        for (int i = 0; i * sp < W + sp; i++) {
            uint32_t h = ss_h((uint32_t)(i + ln * 20) * 13u + hs);
            int x = i * sp + (int)(h % 5u) - ln * 4, dip = 0;
            if (has) for (int wv = 4; wv <= 14; wv += 10) if (iabs(cx + wv - x) <= rx) dip = 1;
            lp_pad(x, y + dip, rx, ry, (int)(h >> 4 & 7u));
            if (dip) for (int k = 2; k < 7; k += 2) { px(x - rx - k, y + 1 + (k > 3)); px(x + rx + k, y + 1 + (k > 3)); }
        }
        if (has) {
            int bob = ss_sin(cx * 1024 / sp) > 60, front = ln ? cx : cx + 18;
            rc_car(cx, y - 11 + bob, (int)(hp >> 2 & 15u));
            px(front + dir, y - 5 + bob); px(front + dir * 2, y - 5 + bob);
            ss_tri(front + dir * 3, y - 5, front + dir * 24, y - 8, front + dir * 24, y - 1, 3, 1);
        }
    }
    /* the rain, and where it lands */
    for (int i = 0; i < 44; i++) {
        uint32_t h = ss_h((uint32_t)i * 19u + 7u);
        int y = (int)(((h >> 8) + (uint32_t)t * (3u + (h >> 20 & 1u))) % 78u) - 8, x = ss_wrap((int)(h % 150u) - y / 3, 150) - 8;
        px(x, y); px(x, y + 1); px(x - 1, y + 2); if (h >> 22 & 1u) px(x - 1, y + 3);
    }
    for (int k = 0; k < 5; k++) {
        uint32_t h = ss_h((uint32_t)(k * 7 + (t + k * 11) / 24 * 16));
        int r = (t + k * 11) % 24, cx = (int)(h % 124u) + 2, cy = 32 + (int)((h >> 8) % 31u);
        for (int a = 0; a < 1024; a += 64) ss_p(cx + ss_cos(a) * (r / 3 + 1) / 127, cy + ss_sin(a) * (r / 9 + 1) / 127, r < 14 ? 16 : 8, 1);
    }
}

/* ---- BOOKS: a bookshop's shelves. Now and then a book opens like a door and someone steps out of it, walks along
 * the shelf, and goes into another ---- */
#define BK_LEN 760
static void bk_who(int x, int y, int kind, int ph, int dir)
{
    if (kind == 4) {   /* a dog comes along behind */
        int dx = x - dir * 9, s = ss_sin(ph) > 0;
        dm_box(dx - 5, y - 8, dx + 5, y, 0);
        dm_box(dx - 3, y - 5, dx + 3, y - 4, 16);
        span(dx - 3 + s, y - 3, y, 1); span(dx + 3 - s, y - 3, y, 1);
        px(dx + dir * 4, y - 6); px(dx + dir * 4, y - 7); px(dx + dir * 5, y - 6); px(dx - dir * 4, y - 6); px(dx - dir * 4, y - 7 - s);
    }
    dm_cut(x, y, ph, 1, kind == 1);
    if (kind == 2) { dm_box(x - 3, y - 17, x + 3, y - 13, 0); for (int k = -1; k <= 1; k++) span(x + k, y - 16, y - 13, 1); px(x - 2, y - 13); px(x + 2, y - 13); }
    if (kind == 3) { ss_ln(x + dir, y - 7, x - dir * 3, y - 16, 16, 1, 1); ss_ell(x - dir * 3, y - 19, 4, 4, 16, 0, 0); dm_blob(x - dir * 3, y - 19, 3, 3, 16); px_off(x - dir * 3 - 1, y - 20); }
    if (kind == 5) { ss_ln(x + dir * 3, y - 1, x + dir * 6, y - 17, 16, 0, 2); ss_ln(x + dir * 2, y - 1, x + dir * 5, y - 17, 16, 1, 1); ss_tri(x + dir * 5, y - 17, x + dir * 5, y - 14, x + dir * 9, y - 16, 16, 1); }
}
static void bk_door(int x, int by) { for (int dx = 1; dx < 9; dx++) span(x + dx, by - 22, by - 1, 1); }
static void bk_draw(void)
{
    int cyc = ss_t / BK_LEN, t = ss_t % BK_LEN, door[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    uint32_t hs = dm_h(cyc / 4, 77);   /* the shelves are filled afresh every fourth time */
    /* the shelves */
    for (int x = 0; x < W; x++) { ss_vd(x, 0, 1, 6, 1); px(x, 2); }
    for (int r = 0; r < 2; r++) {
        int by = r ? 59 : 27, x = 1;
        for (int i = 0; x < W - 3; i++) {
            uint32_t h = ss_h((uint32_t)(i + r * 50) * 19u + hs);
            int isdoor = (i & 3) == 1 && i < 16, w = isdoor ? 9 : 4 + (int)(h % 5u), hg = isdoor ? 22 : 13 + (int)(h >> 4 & 255u) % 9, tone = 2 + (int)(h >> 12 & 255u) % 12, top = by - hg;
            if (isdoor) door[r][i >> 2] = x;
            if (!isdoor && (h >> 20) % 11u == 0) { x += w; continue; }   /* a gap */
            for (int dx = 1; dx < w; dx++) { ss_vd(x + dx, top, by - 1, dx == 1 ? 16 : tone, 1); px(x + dx, top); }
            for (int dx = 2; dx < w; dx++) {
                int bt = top + 3 + (int)(h >> 24 & 3u);
                if (tone > 7) { px_off(x + dx, bt); px_off(x + dx, bt + 2); px_off(x + dx, by - 4); }
                else { px(x + dx, bt); px(x + dx, bt + 1); px(x + dx, by - 4); }
            }
            x += w;
        }
        for (int x2 = 0; x2 < W; x2++) { px(x2, by); ss_vd(x2, by + 1, by + 2, 6, 1); px(x2, by + 3); }
    }
    /* whoever is out */
    for (int j = 0; j < 2; j++) {
        uint32_t h = dm_h(cyc, 60 + j);
        int tt = t - j * 230;
        if (tt < 0 || (j && (h & 1u))) continue;
        int r = (int)(h >> 1 & 1u) ^ j, s = (int)(h >> 2 & 3u), d = (s + 1 + (int)(h >> 4 & 15u) % 3) & 3, kind = (int)(h >> 8 & 255u) % 6;
        int by = r ? 59 : 27, xs = door[r][s] + 5, xd = door[r][d] + 5, dir = xd > xs ? 1 : -1, ta = 110 + iabs(xd - xs) * 3;
        if (tt >= 50 && tt < 170) bk_door(door[r][s], by);
        if (tt >= ta - 50 && tt < ta + 60) bk_door(door[r][d], by);
        if (tt >= 50 && tt < 110) dm_man(xs, by - 1, 0, 0, kind == 1);            /* in the doorway, dark against the light */
        else if (tt >= 110 && tt < ta) bk_who(xs + dir * (tt - 110) / 3, by, kind, tt * 22, dir);
        else if (tt >= ta && tt < ta + 40) dm_man(xd, by - 1, 0, 0, kind == 1);
    }
    if ((cyc & 3) == 0 && t < 48) dm_dark(16 - t / 3);
    if ((cyc & 3) == 3 && t > BK_LEN - 48) dm_dark(clamp((t - (BK_LEN - 48)) / 3, 0, 16));
}

/* ---- CATFACE: a cat teaching faces. It makes one; you make it back; sometimes you get it wrong first ---- */
#define CF_LEN 420
static const int8_t cf_ex[9][6] = {   /* eyes open, brows (+ cross, - sad), mouth curve, mouth open, tongue, wink */
    {3, 0, 1, 0, 0, 0}, {0, -1, 4, 0, 0, 0}, {4, -2, 0, 5, 0, 0}, {2, 3, -3, 0, 0, 0}, {3, -3, -4, 0, 0, 0},
    {3, 0, 2, 2, 1, 0}, {3, -1, 3, 0, 0, 1}, {0, 0, 0, 0, 0, 0}, {0, -2, 0, 6, 0, 0}};
static void cf_face(int cx, int cy, int cat, const int *e)   /* e: the six, in sixteenths */
{
    int eo = (e[0] + 8) / 16, mc = e[2], mo = (e[3] + 8) / 16, my = cy + (cat ? 9 : 11), mw = cat ? 6 : 7;
    if (cat) {
        int fl = e[1] > 0 ? e[1] / 12 : 0;   /* ears back when it is cross */
        ss_ell(cx, 66, 23, 15, 16, 1, 0); ss_ell(cx, 66, 22, 14, 16, 0, 0);
        for (int sd = -1; sd <= 1; sd += 2) {
            ss_tri(cx + sd * 20, cy - 7, cx + sd * (17 + fl), cy - 27 + fl * 2, cx + sd * 4, cy - 15, 16, 1);
            ss_tri(cx + sd * 17, cy - 11, cx + sd * (16 + fl), cy - 23 + fl * 2, cx + sd * 8, cy - 16, 16, 0);
        }
        ss_ell(cx, cy, 21, 18, 16, 1, 0); ss_ell(cx, cy, 20, 17, 16, 0, 0);
        for (int k = -1; k <= 1; k++) span(cx + k * 4, cy - 16, cy - 12 + (k == 0), 1);
    } else {
        span(cx - 8, cy + 18, H - 1, 1); span(cx + 8, cy + 18, H - 1, 1);
        for (int sd = -1; sd <= 1; sd += 2) { ss_ell(cx + sd * 18, cy + 1, 3, 4, 16, 1, 0); ss_ell(cx + sd * 18, cy + 1, 2, 3, 16, 0, 0); }
        ss_ell(cx, cy, 18, 21, 16, 1, 0); ss_ell(cx, cy, 17, 20, 16, 0, 0);
        for (int dx = -16; dx <= 16; dx++) { int top = cy - isqrt(400 * (289 - dx * dx) / 289) + 1, bot = cy - 13 + dx * dx / 40; if (top <= bot) ss_vd(cx + dx, top, bot, 9, 1); }
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        int ex = cx + sd * 9, ey = cy - 3, o = (sd < 0 && e[5] > 8) ? 0 : eo, by = ey - 7 - (eo > 3) * 2;
        if (o == 0) for (int dx = -4; dx <= 4; dx++) px(ex + dx, ey + (mc > 24 ? -1 : 1) * (16 - dx * dx) / 8);
        else {
            ss_ell(ex, ey, 4, o, 16, 1, 0);
            if (cat) { span(ex, ey - o + 1, ey + o - 1, 0); if (o > 3) { span(ex - 1, ey - 2, ey + 2, 0); span(ex + 1, ey - 2, ey + 2, 0); } }
            else { span(ex, ey - (o > 1), ey + (o > 1), 0); if (o > 1) { px_off(ex - 1, ey); px_off(ex + 1, ey); } }
        }
        ss_ln(cx + sd * (cat ? 5 : 4), by + e[1] / 12, cx + sd * (cat ? 12 : 14), by - e[1] / 16, 16, 1, 1);
    }
    if (cat) { px(cx - 1, cy + 3); px(cx, cy + 3); px(cx + 1, cy + 3); span(cx, cy + 4, my - 1 + (mo ? 0 : mc / 20), 1); }
    else { span(cx, cy, cy + 5, 1); px(cx + 1, cy + 5); px(cx - 1, cy + 5); }
    if (mo > 0) { ss_ell(cx, my + mo / 2, mw - 2, mo / 2 + 1, 16, 1, 0); ss_ell(cx, my + mo / 2, mw - 3, mo / 2, 16, 0, 0); }
    else for (int dx = -mw; dx <= mw; dx++) px(cx + dx, my + mc * (mw * mw / 2 - dx * dx) / (10 * mw * mw));
    if (e[4] > 8) { ss_ell(cx, my + mo + 2, 2, 2, 16, 1, 0); px_off(cx, my + mo + 1); px_off(cx, my + mo + 2); }
    if (cat) for (int sd = -1; sd <= 1; sd += 2) for (int k = 0; k < 3; k++) ss_ln(cx + sd * 12, cy + 5 + k, cx + sd * 28, cy + 1 + k * 4 - mc / 20, 16, 1, 1);
}
static void cf_draw(void)
{
    int n = ss_t / CF_LEN, t = ss_t % CF_LEN, ec[6], eh[6];
    uint32_t h = dm_h(n, 17);
    int want = 1 + (n * 3 + (int)(h % 3u)) % 8, wrong = (h >> 4 & 3u) == 0 ? 1 + (want + (int)((h >> 6) % 7u)) % 8 : 0, tg = want, fh;   /* never the same face twice running */
    int fc = t < 30 ? 0 : t < 60 ? (t - 30) * 256 / 30 : t < 350 ? 256 : t < 380 ? (380 - t) * 256 / 30 : 0;
    if (wrong && t < 240) { tg = wrong; fh = t < 130 ? 0 : t < 165 ? (t - 130) * 256 / 35 : t < 215 ? 256 : (240 - t) * 256 / 25; }
    else if (wrong) fh = t < 275 ? (t - 240) * 256 / 35 : t < 350 ? 256 : t < 380 ? (380 - t) * 256 / 30 : 0;
    else fh = t < 150 ? 0 : t < 190 ? (t - 150) * 256 / 40 : t < 350 ? 256 : t < 380 ? (380 - t) * 256 / 30 : 0;
    for (int i = 0; i < 6; i++) {
        ec[i] = (cf_ex[0][i] * (256 - fc) + cf_ex[want][i] * fc) / 16;
        eh[i] = (cf_ex[0][i] * (256 - fh) + cf_ex[tg][i] * fh) / 16;
    }
    if (fc == 0 && (ss_h((uint32_t)(ss_t / 7)) % 31u) == 0) ec[0] = 0;   /* a blink */
    if (fh == 0 && (ss_h((uint32_t)(ss_t / 7) + 99u) % 27u) == 0) eh[0] = 0;
    int shake = wrong && t > 170 && t < 215 ? ss_sin((t - 170) * 68) / 42 : 0;        /* no */
    int nod = t > 296 && t < 340 ? iabs(ss_sin((t - 296) * 35)) / 42 : 0;              /* yes */
    cf_face(33 + shake, 34 + nod + (ss_sin(ss_t * 3) > 115), 1, ec);
    cf_face(95, 30 + (ss_sin(ss_t * 2 + 400) > 110), 0, eh);
}

/* ---- WAREHSE: an abandoned warehouse in the desert. The sun through shattered windows lays shapes on the broken
 * tiles and they creep across; a buckled conduit, a walnut desk with curved legs; at night, one tungsten bulb ---- */
#define WH_LEN 2700
static void wh_draw(void)
{
    static const int8_t cab[14] = {1, 2, 2, 2, 1, 0, -1, -1, -1, -1, 0, 1, 2, 3};
    static const uint8_t gy[4] = {38, 43, 50, 58};
    int cyc = ss_t / WH_LEN, t = ss_t % WH_LEN;
    uint32_t hs = DM.seed, hc = dm_h(cyc, 19);
    int day = t < 1500 ? 16 : t < 1740 ? 16 - (t - 1500) / 15 : t < 2460 ? 0 : (t - 2460) / 15;
    int s = ((t < 2000 ? t : t - WH_LEN) - 750) / 14;                 /* where the sun is */
    int ct = 300 + (int)(hc % 900u), cd = iabs(t - ct) < 140 ? (140 - iabs(t - ct)) / 20 : 0, lit = clamp(day - cd, 0, 16);   /* a cloud */
    /* the wall and its three windows */
    for (int x = 0; x < W; x++) { ss_vd(x, 0, 33, day > 8 ? 2 : 1, 1); px(x, 34); }
    for (int w = 0; w < 3; w++) {
        int x0 = 4 + w * 32;
        dm_box(x0, 5, x0 + 24, 27, lit > 5 ? lit - 5 : 0);
        for (int k = 0; k < 3; k++) {   /* where the glass is gone */
            uint32_t h = ss_h((uint32_t)(w * 5 + k) * 7u + hs);
            int ax = x0 + 2 + (int)(h % 21u), ay = 7 + (int)((h >> 5) % 19u), bx = ax + (int)(h >> 10 & 7u) - 3, by = ay + 4 + (int)(h >> 13 & 3u), cx2 = ax + 3 + (int)(h >> 15 & 3u), cy2 = ay + (int)(h >> 17 & 3u);
            bx = clamp(bx, x0 + 1, x0 + 23); cx2 = clamp(cx2, x0 + 1, x0 + 23); by = clamp(by, 6, 26);
            ss_tri(ax, ay, bx, by, cx2, cy2, 16, lit > 2);
            if (lit <= 2) { px(ax + 1, ay + 1); if (h >> 20 & 1u) px(cx2, by - 1); }   /* stars through the holes */
        }
        span(x0 + 8, 5, 27, 0); span(x0 + 16, 5, 27, 0);
        for (int x = x0; x <= x0 + 24; x++) { px_off(x, 12); px_off(x, 20); px(x, 4); px(x, 28); }
        span(x0 - 1, 4, 28, 1); span(x0 + 25, 4, 28, 1);
    }
    /* the light on the floor, the bars of the windows in it */
    for (int w = 0; w < 3 && lit > 1; w++) {
        int xa = 4 + w * 32 + s / 5, xb = xa + 24, xc = w * 32 - 2 + s, xd = xc + 34;
        ss_quad(xa, 36, xb, 36, xd, 61, xc, 61, lit, 1);
        ss_ln(xa + 8, 36, xc + 11, 61, 16, 0, 2); ss_ln(xa + 16, 36, xc + 22, 61, 16, 0, 2);
        for (int k = 0; k < 2; k++) { int y = k ? 53 : 44; for (int x = xa + (xc - xa) * (y - 36) / 25; x <= xb + (xd - xb) * (y - 36) / 25; x++) { px_off(x, y); if (k) px_off(x, y + 1); } }
    }
    if (day < 10) { ss_ell(106, 54, 38, 9, (10 - day) * 4 / 10, 1, 0); ss_ell(106, 54, 27, 6, (10 - day) * 10 / 10, 1, 0); }   /* the bulb's pool */
    /* the tiles: dark joints in the light, faint ones in the dark; some broken */
    for (int k = 0; k < 4; k++) for (int x = 0; x < W; x++) dm_flip(x, gy[k], (x & 3) == 1);
    for (int i = -5; i <= 5; i++) for (int y = 35; y < H; y++) dm_flip(64 + i * 28 * (y - 16) / 44, y, (y & 3) == 1);
    for (int i = 0; i < 5; i++) {
        uint32_t h = ss_h((uint32_t)i * 43u + hs);
        int bx = 4 + (int)(h % 84u), by = 39 + (int)((h >> 7) % 22u), rx = 3 + (by - 34) / 6;
        ss_ell(bx, by, rx, 1 + (by - 34) / 14, 16, 0, 0);
        px(bx - 1, by); px(bx + 2, by - 1); ss_ln(bx + rx, by, bx + rx + 5, by + 2 - (int)(h >> 14 & 3u), 16, 0, 1);
    }
    {   /* a bird crosses the sun */
        uint32_t hb = dm_h(cyc * 8 + t / 600, 23);
        int tb = t % 600 - (int)(hb % 480u);
        if (lit > 8 && tb >= 0 && tb < 60) for (int dx = -4; dx <= 4; dx++) { int bx = tb * 3 - 20 + dx, by = 41 + (int)(hb >> 9 & 15u) + tb / 8 + iabs(dx) * ((tb / 5 & 1) ? 1 : -1) / 2; px_off(bx, by); px_off(bx, by + 1); }
    }
    /* the conduit along the wall, buckled where it has come away */
    {
        int sw = ss_sin(t * 3) / 80;
        for (int x = 0; x < W; x++) if (x < 31 || x > 66) { px(x, 1); px_off(x, 2); if (x % 22 == 5) { px(x, 0); px(x, 2); } }
        ss_ln(31, 2, 45, 13 + sw, 16, 0, 2); ss_ln(45, 13 + sw, 51, 8 + sw, 16, 0, 2); ss_ln(51, 8 + sw, 66, 2, 16, 0, 2);
        ss_ln(31, 1, 45, 12 + sw, 16, 1, 1); ss_ln(45, 12 + sw, 51, 7 + sw, 16, 1, 1); ss_ln(51, 7 + sw, 66, 1, 16, 1, 1);
    }
    /* the desk */
    for (int k = 0; k < 2; k++) { int x = k ? 117 : 99; span(x - 1, 48, 54, 0); span(x + 1, 48, 54, 0); span(x, 48, 54, 1); }
    dm_box(89, 39, 127, 48, 0);
    for (int x = 90; x <= 126; x++) { px(x, 40); ss_vd(x, 41, 42, 6, 1); px(x, 43); if (x > 93 && x < 123) { ss_vd(x, 44, 46, 3, 1); px(x, 47); } }
    span(94, 44, 47, 1); span(122, 44, 47, 1); span(108, 44, 46, 1); px(101, 45); px(115, 45);
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < 14; j++) {
            int sd = k ? 1 : -1, x = (k ? 123 : 93) + sd * cab[j], y = 44 + j, th = j < 4 || j > 11;
            px_off(x - 1, y); px_off(x + 1, y); px_off(x + sd * 2, y); px(x, y); if (th) px(x - sd, y);
        }
    /* the bulb over it */
    span(108, 0, 8, 1);
    if (day < 12) {
        int fl = (int)(ss_h((uint32_t)(ss_t / 5)) % 23u) == 0;
        ss_ell(108, 12, 17, 13, 16, 0, 0);
        ss_ell(108, 12, 16, 12, fl ? 1 : (12 - day) / 3, 1, 0); ss_ell(108, 12, 8, 7, fl ? 2 : (12 - day) * 3 / 4, 1, 0);
        ss_ell(108, 12, 3, 4, 16, 0, 0); ss_ell(108, 12, 2, 3, 16, 1, 0);
    } else { ss_ell(108, 12, 3, 4, 16, 0, 0); ss_ell(108, 12, 2, 3, 16, 1, 0); px_off(108, 12); px_off(108, 13); }
    /* dust in the air */
    for (int i = 0; i < 10 && day > 5; i++) {
        uint32_t h = ss_h((uint32_t)i * 37u + 11u);
        dm_flip((int)(h % 124u) + 2 + ss_sin(t + i * 90) / 30, 4 + (int)(((h >> 8) + (uint32_t)t / (9u + (h & 3u))) % 52u), 1);
    }
}

/* ---- LASER: asleep. Rolling over, a beam of light comes out of the chest, as it does ---- */
#define LZ_LEN 780
static int lz_pose(int n) { return (n & 1) ? ((n & 2) ? 1 : -1) : 0; }   /* on his back, to the wall, on his back, to the room */
static int lz_bump(int x, int c, int w, int h) { int d = w * w - (x - c) * (x - c); return d > 0 ? h * d / (w * w) : 0; }
static int lz_top(int x, int side)   /* the blanket's line: side 0 on his back .. 127 on his side */
{
    int back = 3 + lz_bump(x, 54, 12, 2) + lz_bump(x, 96, 5, 3), sd = 3 + lz_bump(x, 51, 9, 6) + lz_bump(x, 72, 10, 5) + lz_bump(x, 96, 4, 1);
    int h = (back * (127 - side) + sd * side) / 127, e = x - 42 < 104 - x ? x - 42 : 104 - x;
    return 42 - (h < e * 2 ? h : e * 2);
}
static void lz_draw(void)
{
    int cyc = ss_t / LZ_LEN, t = ss_t % LZ_LEN;
    uint32_t hc = dm_h(cyc, 29);
    int t0 = 330 + (int)(hc % 260u), r = clamp((t - t0) * 256 / 110, 0, 256);
    int a = lz_pose(cyc) * (256 - r) + lz_pose(cyc + 1) * r, sn = ss_sin(a), cs = ss_cos(a), side = iabs(sn), on = r > 0 && r < 256;
    /* the room: a window with the moon in it, a picture, a lamp by the bed */
    for (int x = 0; x < W; x++) { ss_vd(x, 0, 55, 2, 1); px(x, 56); if ((x & 3) == 0) px(x, 52); }
    dm_box(99, 8, 120, 28, 0);
    for (int x = 98; x <= 121; x++) { px(x, 7); px(x, 29); px(x, 18); } span(98, 7, 29, 1); span(121, 7, 29, 1); span(110, 7, 29, 1);
    ss_ell(115, 13, 3, 3, 16, 1, 0); ss_ell(114, 12, 2, 2, 16, 0, 0); px(103, 11); px(106, 23); px(116, 24); px(102, 21);
    for (int x = 93; x <= 96; x++) ss_vd(x, 6, 31 + (x & 1), 10, 1);
    dm_box(63, 12, 80, 24, 0); for (int x = 63; x <= 80; x++) { px(x, 12); px(x, 24); } span(63, 12, 24, 1); span(80, 12, 24, 1);
    ss_tri(66, 22, 77, 22, 72, 15, 8, 1); px(68, 16);
    dm_box(5, 44, 18, 55, 0); for (int x = 5; x <= 18; x++) { px(x, 44); px(x, 49); ss_vd(x, 45, 55, 3, 1); } span(5, 44, 55, 1); span(18, 44, 55, 1); px(11, 47); px(12, 47); px(11, 52); px(12, 52);
    span(12, 37, 43, 1); ss_quad(9, 30, 15, 30, 17, 36, 7, 36, 16, 0); ss_quad(9, 30, 15, 30, 17, 36, 7, 36, 9, 1); for (int x = 7; x <= 17; x++) px(x, 36);
    /* the bed */
    span(24, 49, 55, 1); span(25, 49, 55, 1); span(103, 49, 55, 1); span(104, 49, 55, 1);
    dm_box(19, 26, 22, 50, 16); px_off(19, 26); px_off(22, 26); dm_box(106, 35, 108, 50, 16);
    dm_box(22, 43, 106, 48, 3); for (int x = 22; x <= 106; x++) { px(x, 43); px(x, 48); }
    ss_ell(33, 40, 10, 4, 16, 0, 0); dm_blob(33, 40, 9, 3, 16);
    /* the sleeper: his head on the pillow, the rest under the blanket */
    {
        int hx = 38, hy = 35;
        ss_ell(hx, hy, 5, 5, 16, 0, 0);
        if (a < -128) dm_blob(hx, hy, 4, 4, 6);                                           /* the back of his head */
        else {
            ss_ell(hx, hy, 4, 4, 16, 1, 0);
            for (int dx = -4; dx <= -2; dx++) ss_vd(hx + dx, hy - 3, hy + 3, 16, 0), ss_vd(hx + dx, hy - 3, hy + 3, 7, 1);
            if (a > 128) { px_off(hx, hy - 1); px_off(hx + 1, hy - 1); px_off(hx + 3, hy - 1); px_off(hx + 1, hy + 2); px_off(hx + 2, hy + 2); }
            else { px(hx + 1, hy - 5); px_off(hx + 2, hy - 2); px_off(hx + 3, hy - 2); px_off(hx + 3, hy + 1); }
        }
    }
    for (int x = 43; x <= 103; x++) {
        int top = lz_top(x, side);
        span(x, top, 43, 0); ss_vd(x, top + 1, 46, 9, 1); px(x, top);
        if (x % 7 == 0) span(x, top + 1, 46, 0);
    }
    /* the cat at the foot of the bed */
    {
        int cy = lz_top(97, side) - 2, up = on || (r == 256 && t - t0 < 170);
        if (up) { ss_ell(97, cy - 3, 5, 3, 16, 0, 0); ss_ell(97, cy - 3, 4, 2, 16, 1, 0); span(94, cy - 1, cy + 1, 1); span(96, cy - 1, cy + 1, 1); span(98, cy - 1, cy + 1, 1); span(100, cy - 1, cy + 1, 1); span(101, cy - 10, cy - 4, 1); px(92, cy - 6); px(94, cy - 6); px(92, cy - 5); px(93, cy - 5); px(94, cy - 5); }
        else { ss_ell(97, cy, 5, 3, 16, 0, 0); ss_ell(97, cy, 4, 2, 16, 1, 0); px(93, cy - 3); px(95, cy - 3); px_off(94, cy); px(101, cy + 1); px(102, cy); }
    }
    /* the beam: up at the ceiling, down the wall or out into the room as he turns */
    {
        int cx = 54, cy = lz_top(54, side) - 1, len = sn ? 40 * cs / iabs(sn) : 99, ty = cy - len < 3 ? 3 : cy - len;
        if (on) {
            span(cx - 2, ty, cy, 0); span(cx + 2, ty, cy, 0); span(cx - 1, ty, cy, 1); span(cx + 1, ty, cy, 1); span(cx, ty, cy, 1);
            ss_vd(cx - 3, ty, cy, 5, 1); ss_vd(cx + 3, ty, cy, 5, 1);
            for (int k = 0; k < 7; k++) { uint32_t h = ss_h((uint32_t)(t * 8 + k)); int g = (int)(h & 1023u), l = 3 + (int)(h >> 10 & 3u); px(cx + ss_cos(g) * l / 127, ty + ss_sin(g) * l / 127); px(cx + ss_cos(g) * (l + 1) / 127, ty + ss_sin(g) * (l + 1) / 127); }
            if (sn > 0) {   /* into the room: into your eyes */
                int rr = 2 + sn * sn / 1100;
                ss_ell(cx, ty, rr, rr, 5, 1, 0); ss_ell(cx, ty, rr / 3 + 1, rr / 3 + 1, 16, 1, 0);
                for (int g = 0; g < 1024; g += 128) ss_ln(cx, ty, cx + ss_cos(g) * (rr + 5) / 127, ty + ss_sin(g) * (rr + 5) / 127, 16, 1, 1);
            }
        }
        if (r == 256 && lz_pose(cyc + 1) < 0) {   /* what it left on the wall */
            int age = t - t0 - 110;
            for (int y = 3; y < 32; y++) { px_off(cx - 1, y); px_off(cx + 1, y); if (age < 300 && (int)(ss_h((uint32_t)(y * 16 + t / 8)) % (uint32_t)(3 + age / 16)) == 0) px(cx, y); else px_off(cx, y); }
            for (int k = 0; k < 3 && age < 240; k++) { int p = (age + k * 30) % 90; px(cx + ss_sin(p * 20 + k * 300) / 40, 30 - p / 3); }
        }
        if (!on) for (int k = 0; k < 2; k++) { int p = (t / 2 + k * 40) % 80; if (p < 64) text(43 + p / 5 + k * 2, 27 - p / 4, "Z", 1); }
    }
    if (t < 32) dm_dark(16 - t / 2);
    if (t > LZ_LEN - 32) dm_dark(clamp((t - (LZ_LEN - 32)) / 2, 0, 16));
}

/* ---- the API ---- */
static __attribute__((unused)) void ss_start(int which)
{
    uint32_t *p = (uint32_t *)(void *)&ss;
    for (unsigned i = 0; i < sizeof ss / 4; i++) p[i] = 0;
    ss_which = which; ss_t = 0; ss_idle = 9999; ss_held = 0; ss_fire = 0; ss_encd = 0;
    if (which == SS_PADDLE) { PD.py[0] = PD.py[1] = 32 << 8; pd_serve(1); }
    else if (which == SS_BRICKS) br_reset();
    else if (which == SS_ROCKS) rk_reset();
    else if (which == SS_BLOCKS) bl_reset();
    else if (which == SS_STORM) ST.next = 60;
    else if (which == SS_LIFE) lf_seed();
    else if (which == SS_CLOCK) ck_sync = 0;
    else if (which == SS_PIPES) pp_reset();
    else if (which >= SS_REDCARS) DM.seed = rnd();
}
static __attribute__((unused)) void ss_tick(void)
{
    ss_t++;
    if (ss_idle < 100000) ss_idle++;
    if (ss_which == SS_PADDLE) pd_tick();
    else if (ss_which == SS_BRICKS) br_tick();
    else if (ss_which == SS_ROCKS) rk_tick();
    else if (ss_which == SS_BLOCKS) bl_tick();
    else if (ss_which == SS_STORM) st_tick();
    else if (ss_which == SS_LIFE) lf_tick();
    else if (ss_which == SS_PIPES) pp_tick();
    ss_fire = 0;
}
static __attribute__((unused)) void ss_draw(void)
{
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (ss_which == SS_PADDLE) pd_draw();
    else if (ss_which == SS_BRICKS) br_draw();
    else if (ss_which == SS_ROCKS) rk_draw();
    else if (ss_which == SS_BLOCKS) bl_draw();
    else if (ss_which == SS_LIGHTHOUSE) lh_draw();
    else if (ss_which == SS_STORM) st_draw();
    else if (ss_which == SS_SLIDE) sl_draw();
    else if (ss_which == SS_FOREST) fr_draw();
    else if (ss_which == SS_SHADOWS) sh_draw();
    else if (ss_which == SS_DOTS) dt_draw();
    else if (ss_which == SS_LIFE) lf_draw();
    else if (ss_which == SS_CLOCK) ck_draw();
    else if (ss_which == SS_PIPES) pp_draw();
    else if (ss_which == SS_REDCARS) rc_draw();
    else if (ss_which == SS_FROGS) fg_draw();
    else if (ss_which == SS_COLUMNS) cl_draw();
    else if (ss_which == SS_TRAIN) tr_draw();
    else if (ss_which == SS_WALKWAY) wk_draw();
    else if (ss_which == SS_ISLAND) is_draw();
    else if (ss_which == SS_ELEVATOR) el_draw();
    else if (ss_which == SS_GOLDMOSS) gm_draw();
    else if (ss_which == SS_TREBUCHT) tb_draw();
    else if (ss_which == SS_VALLEY) vl_draw();
    else if (ss_which == SS_BLUEBAR) bb_draw();
    else if (ss_which == SS_LILYPADS) lp_draw();
    else if (ss_which == SS_BOOKS) bk_draw();
    else if (ss_which == SS_CATFACE) cf_draw();
    else if (ss_which == SS_WAREHSE) wh_draw();
    else if (ss_which == SS_LASER) lz_draw();
}
static __attribute__((unused)) int ss_key(int code, int pressed)
{
    if (ss_which < SS_PADDLE || ss_which > SS_BLOCKS) return 0;
    if (pressed) ss_idle = 0;
    int bit = code == KEY_LEFT ? SS_L : code == KEY_RIGHT ? SS_R : code == KEY_UP ? SS_U : code == KEY_DOWN ? SS_D : code == KEY_YES ? SS_Y : 0;
    if (!bit) return 0;
    if (pressed) ss_held |= bit; else ss_held &= ~bit;
    if (!pressed) return 1;
    if (bit == SS_Y) ss_fire = 1;
    if (ss_which == SS_BLOCKS && !BL.dead && !BL.clear) {
        if (bit == SS_L || bit == SS_R) { bl_try(bit == SS_R ? 1 : -1, 0); BL.rep = 0; }
        if (bit == SS_U || bit == SS_Y) bl_try(0, 1);
    }
    return 1;
}
static __attribute__((unused)) int ss_enc(int enc, int delta)
{
    if (ss_which < SS_PADDLE || ss_which > SS_BLOCKS) return 0;
    ss_idle = 0;
    if (enc != 0 && enc != 6) return 0;
    ss_encd += delta;
    return 1;
}
/* ---- the SYSTEM page's saver settings ---- */
static const uint8_t saver_wait[8] = {1, 2, 3, 5, 10, 15, 30, 60}; /* minutes */
static const char *saver_name(int s) { return s <= SS_OFF ? "OFF" : s >= SS_COUNT ? "RANDOM" : ss_name[s]; }
/* ---- the card: AERIA's own browser (research-card.md) ------------------------------------------
 * Listing: the firmware's own opendir/readdir (the calls stock DIRSCAN makes). Loading: stock
 * 0x40022610(kind, slot, path) clears the slot, writes the path into the project's sample list and
 * posts the engine's load; its popups sit under AERIA's screen. Done: the slot's status long
 * (+8: 0 loaded, 1 empty, 2 loading, 3 error). Preview: Static slot 128, played with
 * 0x40093678(0, 128, -1, 0, 1, 1), stopped with 0x40093e9c(0). All of it in the UI task. */
#define BR_MAX 192
#if defined(PREVIEW) && defined(AERIAOS)
#define BR_NAME 64             /* AERIA OS keeps long names (longer ones go by their 8.3 name) */
#else
#define BR_NAME 28
#endif
enum { BR_OFF, BR_LOOP, BR_STARS, BR_PONG, BR_SEQ };   /* SEQ: a folder dealt to the sequencer's kit (static slots 97-112, bank 7) */   /* PONG: a folder dealt to the balls (static slots 112-127) */
static int br_mode, br_count, br_top, br_cur, br_depth, br_busy, br_preview;
static char br_path[0x104];
static char br_names[BR_MAX][BR_NAME];
static uint8_t br_dir[BR_MAX];
static char br_status[24];
/* STARS: a folder's WAVs dealt into 16 slots, one load at a time */
static int deal_left, deal_slot, deal_waiting;
static int16_t deal_order[16];

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: its own card (aeria-os/src/card.c, sampler.c) */
int aos_list(const char *path, void (*each)(const char *name, int dir));
int aos_slot_load(int kind, int slot, const char *path);
int aos_slot_status(int kind, int slot);
void aos_hear(int on);
static int ends_wav(const char *n)
{
    int k = 0;
    while (n[k]) k++;
    if (k < 4) return 0;
    const char *e = n + k - 4;
    return e[0] == '.' && (e[1] | 32) == 'w' && (e[2] | 32) == 'a' && (e[3] | 32) == 'v';
}
static void br_each(const char *name, int dir)
{
    if (br_count >= BR_MAX || (!dir && !ends_wav(name))) return;
    int j = 0;
    while (name[j] && j < BR_NAME - 1) { br_names[br_count][j] = name[j]; j++; }
    br_names[br_count][j] = 0;
    br_dir[br_count] = (uint8_t)dir;
    br_count++;
}
static int br_scan(void) { br_count = 0; return aos_list(br_path[0] ? br_path : "/", br_each) < 0 ? -1 : br_count; }
static void slot_load(int kind, int slot, const char *path) { aos_slot_load(kind, slot, path); }
static int slot_status(int kind, int slot) { return aos_slot_status(kind, slot); }
static void wav_preview_play(void) { aos_hear(1); }
static void wav_preview_stop(void) { aos_hear(0); }
static void set_path_root(void) { br_path[0] = 0; }
#elif defined(PREVIEW)
static int br_scan(void)
{
    static const char *const demo[] = {"Drum Machines", "Sample Packs", "Ambient", "kick 1.wav", "snare 2.wav", "hat.wav"};
    br_count = 6;
    for (int i = 0; i < 6; i++) { int j = 0; while (demo[i][j] && j < BR_NAME - 1) { br_names[i][j] = demo[i][j]; j++; } br_names[i][j] = 0; br_dir[i] = i < 3; }
    return 6;
}
static void slot_load(int kind, int slot, const char *path) { (void)kind; (void)slot; (void)path; }
static int slot_status(int kind, int slot) { (void)kind; (void)slot; return 0; }
static void wav_preview_play(void) {}
static void wav_preview_stop(void) {}
static void set_path_root(void) { const char *r = "/SET/AUDIO"; int i = 0; while (r[i]) { br_path[i] = r[i]; i++; } br_path[i] = 0; }
#else
/* Raw opendir/readdir through the file-function slots DIRSCAN itself calls (research-card.md 1.2 B):
 * no shared name pool to borrow, so no 64 KB save. A slot reads 0 while USB mode swaps in stubs. */
typedef int (*fs_opendir_fn)(const char *path, void *state);
typedef int (*fs_readdir_fn)(void *state, uint8_t *rec);
static uint8_t br_rec[0x140];
static int ends_wav(const char *n)
{
    int k = 0;
    while (n[k]) k++;
    if (k < 4) return 0;
    const char *e = n + k - 4;
    return e[0] == '.' && (e[1] | 32) == 'w' && (e[2] | 32) == 'a' && (e[3] | 32) == 'v';
}
static int br_scan(void)
{
    br_count = 0;
    fs_opendir_fn op = *(fs_opendir_fn volatile *)0x46c8240eu;
    fs_readdir_fn rd = *(fs_readdir_fn volatile *)0x46c8244eu;
    if (!op || !rd) return -1;
    static uint32_t st[4];
    st[0] = st[1] = st[2] = st[3] = 0;
    if (op(br_path, st) < 0) return -1;
    for (int guard = 0; guard < 4096 && br_count < BR_MAX; guard++) {
        if (rd(st, br_rec) != 0) break;
        const char *name = (const char *)br_rec;
        if (!name[0] || name[0] == '.') continue;
        int dir = (br_rec[0x10d] & 0x10) != 0;
        if (!dir && !ends_wav(name)) continue;
        int j = 0;
        while (name[j] && j < BR_NAME - 1) { br_names[br_count][j] = name[j]; j++; }
        br_names[br_count][j] = 0;
        br_dir[br_count] = (uint8_t)dir;
        br_count++;
    }
    return br_count;
}
static void slot_load(int kind, int slot, const char *path) { ((void (*)(uint32_t, uint32_t, const char *))0x40022610u)((uint32_t)kind, (uint32_t)slot, path); }
static int slot_status(int kind, int slot)
{
    uint32_t rec = (kind ? 0x46c922c4u : 0x46c90a78u) + 44u * (unsigned)slot;
    return (int)*(volatile uint32_t *)(rec + 8u);
}
static void wav_preview_play(void) { ((void (*)(uint32_t, uint32_t, int32_t, uint32_t, uint32_t, uint32_t))0x40093678u)(0, 128, -1, 0, 1, 1); }
static void wav_preview_stop(void) { ((void (*)(uint32_t))0x40093e9cu)(0); }
static void set_path_root(void)
{
    const volatile char *set = (const volatile char *)0x100f8480u;
    int i = 0;
    while (i < 0xc0 && set[i] >= ' ' && set[i] < 127) { br_path[i] = set[i]; i++; }
    const char *a = "/AUDIO";
    for (int k = 0; a[k]; k++) br_path[i++] = a[k];
    br_path[i] = 0;
}
#endif

static int path_len(void) { int n = 0; while (br_path[n]) n++; return n; }
static void path_push(const char *name)
{
    int n = path_len();
    if (n + 2 + BR_NAME >= (int)sizeof br_path) return;
    br_path[n++] = '/';
    for (int k = 0; name[k]; k++) br_path[n++] = name[k];
    br_path[n] = 0;
}
static void path_pop(void) { int n = path_len(); while (n > 0 && br_path[n - 1] != '/') n--; if (n > 0) br_path[n - 1] = 0; }
static void path_with(const char *name, char *out)
{
    int n = 0;
    for (; br_path[n]; n++) out[n] = br_path[n];
    out[n++] = '/';
    for (int k = 0; name[k] && n < 0x103; k++) out[n++] = name[k];
    out[n] = 0;
}
static int is_wav(int i) { return !br_dir[i]; }

static void browser_open(int mode)
{
    br_mode = mode; br_cur = br_top = br_depth = 0; br_preview = 0;
    set_path_root();
    br_scan();
    say(mode == BR_LOOP ? "PICK A SAMPLE" : "PICK A FOLDER", 0);
}
static void browser_close(void) { if (br_preview) { wav_preview_stop(); br_preview = 0; } br_mode = BR_OFF; }

static int wav_count(void) { int n = 0; for (int i = 0; i < br_count; i++) n += is_wav(i); return n; }

/* STARS: deal up to 16 random WAVs of the listed folder into bank b's Static slots */
static void deal_folder(int bank)
{
    int wavs[BR_MAX], n = 0;
    for (int i = 0; i < br_count; i++) if (is_wav(i)) wavs[n++] = i;
    if (!n) { say("NO WAVS HERE", 0); return; }
#if defined(PREVIEW) && defined(AERIAOS)
    /* AERIA OS: a seeded shuffle it can repeat at start, with the folder and the seed kept (sampler.c) */
    { void aos_shuffle(int16_t *idx, int n, uint32_t seed); void aos_note_deal(int bank, const char *folder, uint32_t seed);
      static int16_t order[BR_MAX]; uint32_t seed = (rnd() ^ (ticks << 8)) | 1u; if (bank == 6 && br_mode == BR_SEQ) seed = 0;
      for (int i = 0; i < n; i++) order[i] = (int16_t)i;
      aos_shuffle(order, n, seed);
      for (int k = 0; k < 16; k++) deal_order[k] = (int16_t)wavs[order[k % n]];
      aos_note_deal(bank, br_path, seed); }
#else
    for (int i = n - 1; i > 0; i--) { int j = range(0, i), t = wavs[i]; wavs[i] = wavs[j]; wavs[j] = t; }
    for (int k = 0; k < 16; k++) deal_order[k] = (int16_t)wavs[k % n];
#endif
    deal_left = 16; deal_slot = bank * 16; deal_waiting = 0;
    br_busy = 1;
}
static void deal_tick(void)
{
    if (!deal_left) return;
    int slot = deal_slot + (16 - deal_left);
    if (deal_waiting) {
        int st = slot_status(0, slot);
        if (st == 2 && deal_waiting < 600) { deal_waiting++; return; }
        deal_waiting = 0;
        deal_left--;
        if (!deal_left) { br_busy = 0; say(deal_slot == 96 ? "KIT LOADED" : "STARS LOADED", 0); }
        return;
    }
    static char full[0x104];
    path_with(br_names[deal_order[16 - deal_left]], full);
    slot_load(0, slot, full);
    deal_waiting = 1;
}

/* LOOP: the chosen WAV into Flex slot 1 (0-based 0), the sample T1 plays */
static int loop_waiting;
static void loop_load(int i)
{
    static char full[0x104];
    path_with(br_names[i], full);
    slot_load(1, 0, full);
    loop_waiting = 1;
    br_busy = 1;
    int k = 0; while (br_names[i][k] && k < 23) { br_status[k] = br_names[i][k]; k++; } br_status[k] = 0;
}
static void loop_tick(void)
{
    if (!loop_waiting) return;
    int st = slot_status(1, 0);
    if (st == 2 && loop_waiting < 1800) { loop_waiting++; return; }
    loop_waiting = 0; br_busy = 0;
    say(st == 0 ? "LOADED" : "LOAD FAILED", 0);
}
static int preview_waiting;
static void preview_tick(void)
{
    if (!preview_waiting) return;
    int st = slot_status(0, 128);
    if (st == 2 && preview_waiting < 600) { preview_waiting++; return; }
    preview_waiting = 0;
    if (st == 0) { wav_preview_play(); br_preview = 1; }
}

static void browser_key(int code)
{
    if (br_busy) return;
    if (code == KEY_UP) { if (br_cur > 0) br_cur--; }
    else if (code == KEY_DOWN) { if (br_cur < br_count - 1) br_cur++; }
    else if (code == KEY_LEFT || code == KEY_NO) {
        if (br_preview) { wav_preview_stop(); br_preview = 0; }
        if (br_depth > 0) { path_pop(); br_depth--; br_cur = br_top = 0; br_scan(); }
        else browser_close();
    } else if (code == KEY_RIGHT && br_count) {
        if (br_dir[br_cur]) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
        else { static char full[0x104]; if (br_preview) wav_preview_stop(); path_with(br_names[br_cur], full); slot_load(0, 128, full); preview_waiting = 1; }
    } else if (code == KEY_YES && br_count) {
        /* YES opens a folder or loads (a WAV for LOOP; for STARS, the folder it sits in).
         * FUNC+YES plays the WAV in the headphones; on a folder in STARS it deals that folder. */
        static char full[0x104];
        if (func_held && !br_dir[br_cur]) { if (br_preview) wav_preview_stop(); path_with(br_names[br_cur], full); slot_load(0, 128, full); preview_waiting = 1; }
        else if (br_dir[br_cur] && !(func_held && (br_mode >= BR_STARS))) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
        else if (br_mode >= BR_STARS) {
            if (br_preview) { wav_preview_stop(); br_preview = 0; }
            if (br_dir[br_cur]) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
            if (wav_count()) { deal_folder(br_mode == BR_PONG ? 7 : br_mode == BR_SEQ ? 6 : pv(PG_STARS, 9)); browser_close(); } else say("NO WAVS HERE", 0);
        } else { if (br_preview) { wav_preview_stop(); br_preview = 0; } loop_load(br_cur); browser_close(); }
    }
    if (br_cur < br_top) br_top = br_cur;
    if (br_cur >= br_top + 6) br_top = br_cur - 5;
}
static void browser_enc(int delta) { for (int i = 0; i < (delta < 0 ? -delta : delta); i++) browser_key(delta > 0 ? KEY_DOWN : KEY_UP); }

static void draw_browser(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 56, 0);
    /* the folder we are in, its last part */
    int n = path_len(), s = n;
    while (s > 0 && br_path[s - 1] != '/') s--;
    char here[30]; int k = 0;
    for (int i = s; i < n && k < 28; i++) { char c = br_path[i]; here[k++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
    here[k] = 0;
    text(2, 10, here, 1);
    for (int x = 0; x < W; x += 2) px(x, 16);
    if (br_busy) { text(30, 32, "LOADING", 1); return; }
    for (int r = 0; r < 6 && br_top + r < br_count; r++) {
        int i = br_top + r, y = 19 + r * 6;
        char line[30]; int j = 0;
        if (br_dir[i]) line[j++] = '>';
        for (int c = 0; br_names[i][c] && j < 29; c++) { char ch = br_names[i][c]; line[j++] = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; }
        line[j] = 0;
        if (i == br_cur) { for (int x = 0; x < W; x++) span(x, y - 1, y + 5, 1); text(2, y, line, 0); }
        else text(2, y, line, 1);
    }
    if (!br_count) text(2, 24, "EMPTY", 1);
    text(2, 57, br_mode != BR_LOOP ? "YES DEAL  FUNC+YES HEAR" : "YES LOAD  FUNC+YES HEAR", 1);
}

/* ---- SYSTEM: the stock routines behind PROJECT > SYSTEM, under AERIA's screen (research-card.md) ---- */
static int usb_mode;
#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: its own USB (aeria-os/src/usb.c): the card goes to the computer while this is on */
void aos_usb_disk(int on);
int aos_disk_busy(void);
static int popup_open(void) { return 0; }
static void sync_to_card(void) {}
static void usb_enter(void) { aos_usb_disk(1); usb_mode = aos_disk_busy(); say(usb_mode ? "USB DISK MODE" : "NO CARD", 0); }
static void usb_leave(void) { aos_usb_disk(0); usb_mode = 0; say("BACK", 0); }
static void usb_tick(void) { if (usb_mode && !aos_disk_busy()) { usb_mode = 0; say("EJECTED", 0); } }
static void os_upgrade(void) { say("NOT YET", 0); }
static void transport_start(void) { preview_play = 1; }
#elif defined(PREVIEW)
static int popup_open(void) { return 0; }
static void sync_to_card(void) {}
static void usb_enter(void) { usb_mode = 1; }
static void usb_leave(void) { usb_mode = 0; }
static void usb_tick(void) {}
static void os_upgrade(void) { say("OS UPGRADE", 0); }
static void transport_start(void) { preview_play = 1; }
#else
/* USB DISK MODE: the menu's YES body 0x4007eb34 (stop, cut voices, sync, then the deferred
 * USB switch, which sets 0x460e76a0 = 1); leaving: 0x4007ebf0(0) (USB off, remount, reload). */
static void usb_enter(void) { ((void (*)(void))0x4007eb34u)(); say("USB DISK MODE", 0); }
static void usb_leave(void) { ((void (*)(uint32_t))0x4007ebf0u)(0); usb_mode = 0; say("BACK", 0); }
static void usb_tick(void) { usb_mode = *(volatile uint32_t *)0x460e76a0u == 1u; }
/* a stock or base popup is open (the popup record 0x46c7d34c, flags +0x20: 0x21 open): its keys are its own */
static int popup_open(void) { return *(volatile uint32_t *)(0x46c7d34cu + 0x20u) == 0x21u; }
/* SYNC TO CARD: the project, its Parts and the base's effect map, written as stock's menu does */
static void sync_to_card(void) { ((void (*)(uint32_t))0x40022cd4u)(1); }
/* OS UPGRADE: the menu's YES body 0x40063660(0): stop, sync, then the newest .BIN on the card */
static void os_upgrade(void) { ((void (*)(uint32_t))0x40063660u)(0); }
/* the PLAY path: 0x4009b964(0), or 0x400a030c(0) in arranger mode; only from a stop */
static void transport_start(void)
{
    if (*(volatile uint32_t *)0x800065b8u != 0u) return;
    if (*(volatile uint32_t *)0x460d1aecu) ((void (*)(uint32_t))0x400a030cu)(0);
    else ((void (*)(uint32_t))0x4009b964u)(0);
}
#endif

/* ---- GUMBALLS: four balloons, one MIDI channel each; gumballs bounce inside and play notes --------
 * MIDI out through stock's midi_send 0x40010bc8(len, bytes) (research-midi.md 1): atomic, mirrored to
 * USB MIDI by the base. It spins forever if its 4 KB ring is full, so every send first checks the ring
 * count 0x400b967c and keeps to 36 bytes a tick. Off by default; switching it off sends note-offs. */
#define GB_BALLOONS 4
#define GB_BALLS 6
#define GB_R 13            /* balloon radius */
#define GB_BALL 2          /* gumball radius */
typedef struct { int16_t x, y, vx, vy; uint8_t live, note, ch; uint8_t gate; uint8_t hue; } Gum;
static Gum gums[GB_BALLOONS][GB_BALLS];
static int gum_on, gum_cleared;
static unsigned gum_bytes;
/* settings: scale, root, octave (8 = random), gravity, bounce, gate | velocity, spread, first channel */
static int gb_scale, gb_root, gb_oct = 4, gb_grav = 40, gb_bounce = 80, gb_gate = 8, gb_vel = 110, gb_spread = 1, gb_chan = 0;
static const uint16_t gb_scales[] = {0xab5, 0x5ad, 0x295, 0x4a9, 0x6ad, 0xad5, 0x56b, 0xfff, 0x555, 0x891};
static const char *const gb_scale_name[] = {"MAJOR", "MINOR", "PENTA", "MIN PENT", "DORIAN", "LYDIAN", "PHRYGIAN", "CHROMATIC", "WHOLE", "STACKED"};
#define GB_NSCALES ((int)(sizeof gb_scales / sizeof gb_scales[0]))
static const char *const gb_note_name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#if defined(PREVIEW) && defined(AERIAOS)
int aos_midi_out(unsigned st, unsigned a, unsigned b);
static int midi_out(unsigned st, unsigned a, unsigned b) { gum_bytes += 3; return aos_midi_out(st, a, b); }
#elif defined(PREVIEW)
static int midi_out(unsigned st, unsigned a, unsigned b) { (void)st; (void)a; (void)b; gum_bytes += 3; return 1; }
#else
static uint8_t gb_tx[3];
static int midi_out(unsigned st, unsigned a, unsigned b)
{
    if (gum_bytes + 3 > 36 || *(volatile uint32_t *)0x400b967cu > 3000u) return 0;
    gb_tx[0] = (uint8_t)st; gb_tx[1] = (uint8_t)(a & 127); gb_tx[2] = (uint8_t)(b & 127);
    ((void (*)(int, const void *))0x40010bc8u)(3, gb_tx);
    gum_bytes += 3;
    return 1;
}
#endif

/* a random note of the scale: degree, octave (or a random one), spread */
static int gb_note(void)
{
    uint16_t m = gb_scales[gb_scale];
    int deg[12], n = 0;
    for (int k = 0; k < 12; k++) if ((m >> k) & 1) deg[n++] = k;
    if (!n) deg[n++] = 0;
    int oct = gb_oct >= 8 ? range(2, 6) : gb_oct;
    oct += gb_spread ? range(0, gb_spread) : 0;
    return clamp(12 * (oct + 1) + gb_root + deg[range(0, n - 1)], 0, 127);
}
static int gb_cx(int b) { return 16 + b * 32; }
#define GB_CY 32

static void gum_drop(int b)
{
    for (int i = 0; i < GB_BALLS; i++) {
        Gum *g = &gums[b][i];
        if (g->live) continue;
        g->live = 1; g->gate = 0; g->note = 0xff;
        g->x = (int16_t)((gb_cx(b) + range(-4, 4)) * 16); g->y = (int16_t)((GB_CY - GB_R + 4) * 16);
        g->vx = (int16_t)range(-20, 20); g->vy = 0; g->hue = (uint8_t)range(0, 3);
        g->ch = (uint8_t)((gb_chan + b) & 15);
        return;
    }
}
static void gum_note_off(Gum *g)
{
    if (g->note != 0xff && midi_out(0x80u | g->ch, g->note, 0)) g->note = 0xff;
}
static void gum_take(int b)
{
    for (int i = GB_BALLS - 1; i >= 0; i--) if (gums[b][i].live) { gum_note_off(&gums[b][i]); gums[b][i].live = 0; return; }
}
static void gum_all_off(void)
{
    for (int b = 0; b < GB_BALLOONS; b++) for (int i = 0; i < GB_BALLS; i++) gum_note_off(&gums[b][i]);
}

static void gum_tick(void)
{
    gum_bytes = 0;
    if (!gum_cleared) { /* the first tick after load: nothing a previous version left may keep sounding */
        int ok = 1;
        for (int g = 0; g < GB_BALLOONS; g++) for (int i = 0; i < GB_BALLS; i++) if (!gums[g][i].live) gums[g][i].note = 0xff;
        for (int c = 0; c < GB_BALLOONS && ok; c++) ok = midi_out(0xb0u | ((gb_chan + c) & 15), 123, 0);
        if (ok) gum_cleared = 1;
        return;
    }
    for (int b = 0; b < GB_BALLOONS; b++)
        for (int i = 0; i < GB_BALLS; i++) {
            Gum *g = &gums[b][i];
            if (g->note != 0xff && g->gate && --g->gate == 0) gum_note_off(g);
            if (!g->live || !gum_on) continue;
            /* gravity, then the balloon's skin: reflect off the circle, losing energy each bounce */
            g->vy = (int16_t)(g->vy + 1 + gb_grav / 20);
            g->x = (int16_t)(g->x + g->vx); g->y = (int16_t)(g->y + g->vy);
            int dx = g->x - gb_cx(b) * 16, dy = g->y - GB_CY * 16, lim = (GB_R - GB_BALL - 1) * 16;
            int d2 = (dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4);
            if (d2 > (lim >> 4) * (lim >> 4)) {
                int d = isqrt(d2); if (d < 1) d = 1;
                int nx = dx / d, ny = dy / d;           /* unit normal x16 (dx is in 1/16 px, d in px) */
                int dot = (g->vx * nx + g->vy * ny) / 16;
                if (dot > 0) {
                    int speed = dot;
                    g->vx = (int16_t)((g->vx - 2 * dot * nx / 16) * gb_bounce / 100 + range(-2, 2));
                    g->vy = (int16_t)((g->vy - 2 * dot * ny / 16) * gb_bounce / 100);
                    /* the bounce plays: louder for a hard hit, softer as it dribbles */
                    if (speed > 6) {
                        gum_note_off(g);
                        int vel = clamp(speed * gb_vel / 60, 8, 127);
                        int n = gb_note();
                        if (midi_out(0x90u | g->ch, (unsigned)n, (unsigned)vel)) { g->note = (uint8_t)n; g->gate = (uint8_t)(2 + gb_gate * 3); }
                    }
                }
                g->x = (int16_t)(gb_cx(b) * 16 + dx * lim / (16 * d));
                g->y = (int16_t)(GB_CY * 16 + dy * lim / (16 * d));
            }
            /* at rest it is tossed up again, so a dropped gumball keeps playing */
            if (iabs(g->vy) < 3 && iabs(g->vx) < 3 && g->y > (GB_CY + GB_R - 6) * 16) { g->vy = (int16_t)-range(30, 55); g->vx = (int16_t)range(-18, 18); }
        }
}

static void gum_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 10, 56, 0);
    for (int b = 0; b < GB_BALLOONS; b++) {
        int cx = gb_cx(b), sway = sine[(int)(ticks / 2 + (uint32_t)b * 11) & 63] / 90;
        circle(cx + sway, GB_CY, GB_R);
        px(cx + sway, GB_CY + GB_R + 1); px(cx + sway - 1, GB_CY + GB_R + 2); px(cx + sway + 1, GB_CY + GB_R + 2);
        for (int y = GB_CY + GB_R + 3; y < 57; y++) px(cx + sway + sine[(y * 5 + (int)ticks) & 63] / 100, y);
        char c[4]; num(c, ((gb_chan + b) & 15) + 1);
        text(cx - 2, 11, c, 1);
        for (int i = 0; i < GB_BALLS; i++) {
            Gum *g = &gums[b][i];
            if (!g->live) continue;
            int gx = (g->x >> 4) + sway, gy = g->y >> 4;
            for (int dy = -GB_BALL; dy <= GB_BALL; dy++) for (int dx = -GB_BALL; dx <= GB_BALL; dx++)
                if (dx * dx + dy * dy <= GB_BALL * GB_BALL && (g->hue == 0 || dither_on(gx + dx, gy + dy, 6 + 3 * g->hue))) px(gx + dx, gy + dy);
            if (g->note != 0xff) circle(gx, gy, GB_BALL + 2);
        }
    }
    char line[24]; int i = 0;
    const char *s = gb_scale_name[gb_scale];
    while (*s) line[i++] = *s++;
    line[i++] = ' ';
    s = gb_note_name[gb_root]; while (*s) line[i++] = *s++;
    line[i++] = ' ';
    if (gb_oct >= 8) { line[i++] = 'O'; line[i++] = '?'; } else { line[i++] = 'O'; line[i++] = (char)('0' + gb_oct); }
    line[i] = 0;
    text(2, 58, line, 1);
    text(W - 2 - text_w(gum_on ? "MIDI ON" : "MIDI OFF"), 58, gum_on ? "MIDI ON" : "MIDI OFF", 1);
}

static int gum_key(int code)
{
    if (code >= 0x00 && code < 0x04) { gum_drop(code); return 1; }
    if (code >= 0x08 && code < 0x0c) { gum_take(code - 0x08); return 1; }
    if (code == KEY_YES) { gum_on = !gum_on; if (!gum_on) gum_all_off(); say("GUMBALLS", gum_on ? "ON" : "OFF"); return 1; }
    return 0;
}
static void gum_enc(int e, int d, int func)
{
    char n[8];
    int s = d > 0 ? 1 : -1;
    if (!func) {
        if (e == 0) { gb_scale = clamp(gb_scale + s, 0, GB_NSCALES - 1); say("SCALE", gb_scale_name[gb_scale]); }
        else if (e == 1) { gb_root = (gb_root + s + 12) % 12; say("ROOT", gb_note_name[gb_root]); }
        else if (e == 2) { gb_oct = clamp(gb_oct + s, 0, 8); if (gb_oct == 8) say("OCTAVE", "RANDOM"); else { num(n, gb_oct); say("OCTAVE", n); } }
        else if (e == 3) { gb_grav = clamp(gb_grav + d, 0, 127); num(n, gb_grav); say("GRAVITY", n); }
        else if (e == 4) { gb_bounce = clamp(gb_bounce + d, 30, 98); num(n, gb_bounce); say("BOUNCE", n); }
        else if (e == 5) { gb_gate = clamp(gb_gate + d, 1, 40); num(n, gb_gate); say("LENGTH", n); }
    } else {
        if (e == 0) { gb_vel = clamp(gb_vel + d, 10, 127); num(n, gb_vel); say("VELOCITY", n); }
        else if (e == 1) { gb_spread = clamp(gb_spread + s, 0, 3); num(n, gb_spread); say("SPREAD", n); }
        else if (e == 2) { gum_all_off(); gb_chan = clamp(gb_chan + s, 0, 12); num(n, gb_chan + 1); say("FIRST CH", n); for (int b = 0; b < GB_BALLOONS; b++) for (int i = 0; i < GB_BALLS; i++) gums[b][i].ch = (uint8_t)((gb_chan + b) & 15); }
    }
}

/* for the lights: how many gumballs a balloon holds, and whether one is sounding */
static int gum_count(int b) { int n = 0; for (int i = 0; i < GB_BALLS; i++) n += gums[b][i].live; return n; }
static int gum_sounding(int b) { for (int i = 0; i < GB_BALLS; i++) if (gums[b][i].note != 0xff) return 1; return 0; }

/* ---- PITCH: the input sings, AERIA plays it over MIDI ----------------------------------------------
 * CAPTURE. A stock-code site at the frame interrupt's tail, 0x4000d99a (6 bytes, stock 23c1 8000 4800 =
 * move.l %d1,0x80004800, SHA-256 9a7d1001...5f707b91; the site TUNER uses). build_aeria.py places
 * `jmp aeria_frame_stub` there. The stub replays that store, calls aeria_frame() and jumps to 0x4000d9a0,
 * the next stock instruction (the base's own jmp to its audio shim on a USB base). The interrupt restores
 * d0-d7/a0-a6 from its stack at 0x4000d9a6, so nothing the stub or C clobbers survives it. aeria_frame
 * makes no calls, no EMAC, at most ~100 instructions: with the capture off it is a load and a branch.
 * It reads the input page the frame interrupt just made (eight 0x100-byte pages at 0x80005660, index
 * 0x46104d00; C D at +0x00 + 8s, A B at +0x80 + 8s; 24-bit words, left-justified) and decimates its 16
 * samples by 4 through an 8-tap boxcar (zeros at 5.5, 11 and 16.5 kHz) into an 11,025 Hz ring.
 * Without the site (a plain build.py build) nothing fills the ring and the page says so.
 *
 * DETECTOR, in pitch_tick, about PT_BUDGET products a tick: the newest 512 samples less their mean,
 * level gate, scaled to a 2,000 peak; McLeod's normalised square difference n(t) = 2r / (m0 + mt) on a
 * [1 2 1]-decimated copy (5,512 Hz, W 128, lags 2..110 = 50 Hz..2.7 kHz) with key-maxima picking (first
 * key maximum within 0.86 of the best), stopping at 2.25x the first strong one; then at 11,025 Hz (W 256)
 * a hill climb round twice the coarse lag and again round the largest multiple of it under 224 lags,
 * each with parabolic interpolation: the period is measured over several cycles. Integer only. */

#define PT_RING 1024u
#define PT_N 512
#define PT_W 256
#define PT_CN 256
#define PT_CW 128
#define PT_CMAX 110
#define PT_LONG 224
#define PT_BUDGET 5000
#define PT_HIST 128

static volatile int16_t pt_ring[PT_RING];
static volatile uint32_t pt_wr, pt_frames;
static volatile int32_t pt_carry;
static volatile uint8_t pt_cap, pt_src;

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: the inputs come from the DSP's tap (sampler.c rec_word -> usb.c usb_tap), a sample at a time, A B C D
 * as 24-bit values; four make one 11,025 Hz sample, as the frame capture below does */
void aos_tap_want(int who, int on);
static int32_t pt_acc; static int pt_n4, pt_e4;
void aos_pitch_sample(int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (!pt_cap) return;
    unsigned src = pt_src;
    int32_t v = src == 0 ? ((a >> 9) + (b >> 9)) : src == 1 ? a >> 8 : src == 2 ? b >> 8 : src == 3 ? ((c >> 9) + (d >> 9)) : src == 4 ? c >> 8 : d >> 8;
    pt_acc += v;
    if (++pt_n4 < 4) return;
    pt_ring[pt_wr & (PT_RING - 1)] = (int16_t)((pt_carry + pt_acc) >> 3);
    pt_carry = pt_acc; pt_acc = 0; pt_n4 = 0; pt_wr = pt_wr + 1;
    if (++pt_e4 == 4) { pt_e4 = 0; pt_frames = pt_frames + 1; }
}
#endif
#ifdef PREVIEW
int32_t preview_inring[8][64];
uint32_t preview_inidx;
#define PT_IN_IDX preview_inidx
#define PT_IN_PAGE(i) ((const volatile int32_t *)preview_inring[(i) & 7u])
#else
#define PT_IN_IDX (*(volatile uint32_t *)0x46104d00u)
#define PT_IN_PAGE(i) ((const volatile int32_t *)(0x80005660u + (((i) & 7u) << 8)))
/* jmp here from 0x4000d99a: the displaced store, the capture, the next stock instruction */
__asm__(
    "	.text\n"
    "	.even\n"
    "	.globl	aeria_frame_stub\n"
    "aeria_frame_stub:\n"
    "	move.l	%d1,0x80004800\n"
    "	jsr	aeria_frame\n"
    "	jmp	0x4000d9a0\n");
#endif

/* the frame interrupt (IPL 5): source 0 A+B, 1 A, 2 B, 3 C+D, 4 C, 5 D */
__attribute__((used)) void aeria_frame(void)
{
    if (!pt_cap) return;
    unsigned src = pt_src, s = src >= 3 ? src - 3 : src;
    const volatile int32_t *p = PT_IN_PAGE(PT_IN_IDX) + (src >= 3 ? 0 : 32) + (s == 2 ? 1 : 0);
    int32_t carry = pt_carry;
    uint32_t w = pt_wr;
    for (int k = 0; k < 4; k++) {
        int32_t b = 0;
        for (int j = 0; j < 4; j++, p += 2) b += s ? p[0] >> 16 : (p[0] >> 17) + (p[1] >> 17);
        pt_ring[w++ & (PT_RING - 1)] = (int16_t)((carry + b) >> 3);
        carry = b;
    }
    pt_carry = carry;
    pt_wr = w;
    pt_frames++;
}

/* 11,025 x 256 / f(n), n = 24..108: a note's period in Q8 samples */
static const int32_t pt_per[85] = {
    86303, 81460, 76888, 72572, 68499, 64655, 61026, 57601, 54368, 51316, 48436, 45718, 43152, 40730, 38444, 36286,
    34250, 32327, 30513, 28800, 27184, 25658, 24218, 22859, 21576, 20365, 19222, 18143, 17125, 16164, 15256, 14400,
    13592, 12829, 12109, 11429, 10788, 10182, 9611, 9072, 8562, 8082, 7628, 7200, 6796, 6415, 6055, 5715, 5394, 5091,
    4805, 4536, 4281, 4041, 3814, 3600, 3398, 3207, 3027, 2857, 2697, 2546, 2403, 2268, 2141, 2020, 1907, 1800, 1699,
    1604, 1514, 1429, 1348, 1273, 1201, 1134, 1070, 1010, 954, 900, 849, 802, 757, 714, 674};
/* the gate: peak for -66..-18 dBFS in 3 dB steps */
static const uint16_t pt_gates[17] = {16, 23, 33, 46, 65, 92, 130, 184, 260, 368, 519, 734, 1036, 1464, 2067, 2920, 4125};

/* settings */
enum { PM_NOTE, PM_SNAP, PM_CHORD, PM_SEVENTH, PM_ARP, PM_SCALE, PM_JUMP, PM_COUNT };
static const char *const pt_mode_name[PM_COUNT] = {"NOTE", "SNAP", "CHORD", "7TH", "ARP", "SCALE", "JUMP"};
static const char *const pt_src_name[6] = {"A+B", "A", "B", "C+D", "C", "D"};
static int pt_on, pt_mode, pt_scale, pt_root, pt_oct, pt_gate = 6, pt_chan, pt_vel, pt_rate = 6, pt_stab = 2, pt_bend, pt_clar = 60;

/* detector state */
static int16_t pt_x[PT_N], pt_c[PT_CN], pt_nc[PT_CMAX + 2];
static int pt_state, pt_lag, pt_neg, pt_lobe, pt_lmax, pt_ltau, pt_stop, pt_nk;
static int16_t pt_kt[16], pt_kv[16];
static int32_t pt_e0, pt_et, pt_fe0;
static uint32_t pt_seen, pt_start, pt_quiet, pt_view = 0x80000000u;
static int pt_nosite, pt_gate_open, pt_peak, pt_lastpeak, pt_ccoarse;

/* results: the latest estimate and what is played */
static int pt_valid, pt_x100, pt_hz10, pt_conf, pt_unv, pt_cand, pt_cnt, pt_out = -1, pt_med[3], pt_nmed;
static int16_t pt_hist[PT_HIST];
static int pt_hpos;
static uint32_t pt_estimates, pt_noteons;

/* ---- the detector ---------------------------------------------------------------------------------- */
static int pt_log2(unsigned v) { int n = 0; while (v > 1) { v >>= 1; n++; } return n; }
static int pt_nsdf(int32_t r, int32_t a, int32_t b)
{
    int32_t den = ((a >> 1) + (b >> 1)) >> 15;
    int32_t n = r / (den + 1);
    return n > 32767 ? 32767 : n < -32767 ? -32767 : (int)n;
}
/* n at full rate (W 256) for one lag */
static int pt_full(int lag)
{
    int32_t r = 0, e = 0;
    const int16_t *a = pt_x, *b = pt_x + lag;
    for (int i = 0; i < PT_W; i++) { r += a[i] * b[i]; e += b[i] * b[i]; }
    return pt_nsdf(r, pt_fe0, e);
}
/* parabolic vertex offset in Q8 (-128..128) of three values with the middle highest */
static int pt_parab(int a, int b, int c)
{
    int d = a - 2 * b + c;
    if (d >= 0) return 0;
    return clamp((a - c) * 128 / d, -128, 128);
}
/* hill climb from lag l at full rate: Q8 lag of the peak; *val its n */
static int pt_climb(int l, int *val)
{
    l = clamp(l, 3, PT_N - PT_W - 2);
    int a = pt_full(l - 1), b = pt_full(l), c = pt_full(l + 1);
    for (int step = 0; step < 6; step++) {
        if (a > b && l > 3) { l--; c = b; b = a; a = pt_full(l - 1); }
        else if (c > b && l < PT_N - PT_W - 2) { l++; a = b; b = c; c = pt_full(l + 1); }
        else break;
    }
    *val = b;
    return l * 256 + pt_parab(a, b, c);
}

static void pt_key_push(int t, int v) { if (pt_nk < 16) { pt_kt[pt_nk] = (int16_t)t; pt_kv[pt_nk] = (int16_t)v; pt_nk++; } }

/* the snapshot: 1 voiced and ready for lags, 0 nothing to analyse */
static int pt_snapshot(void)
{
    uint32_t w = pt_wr;
    int32_t sum = 0;
    for (int i = 0; i < PT_N; i++) { int v = pt_ring[(w - PT_N + (uint32_t)i) & (PT_RING - 1)]; pt_x[i] = (int16_t)v; sum += v; }
    int mean = sum / PT_N, peak = 1;
    for (int i = 0; i < PT_N; i++) { int v = clamp(pt_x[i] - mean, -32767, 32767); pt_x[i] = (int16_t)v; if (iabs(v) > peak) peak = iabs(v); }
    pt_lastpeak = pt_peak; pt_peak = peak;
    int thr = pt_gates[pt_gate];
    pt_gate_open = peak >= (pt_gate_open ? thr / 2 : thr);
    if (!pt_gate_open) return 0;
    int32_t scale = (2000 << 12) / peak;
    for (int i = 0; i < PT_N; i++) pt_x[i] = (int16_t)((pt_x[i] * scale) >> 12);
    for (int k = 0; k < PT_CN; k++) {
        int a = pt_x[k ? 2 * k - 1 : 0], b = pt_x[2 * k], c = pt_x[2 * k + 1];
        pt_c[k] = (int16_t)((a + 2 * b + c) >> 2);
    }
    pt_e0 = pt_et = pt_fe0 = 0;
    for (int i = 0; i < PT_CW; i++) { pt_e0 += pt_c[i] * pt_c[i]; pt_et += pt_c[i + 1] * pt_c[i + 1]; }
    for (int i = 0; i < PT_W; i++) pt_fe0 += pt_x[i] * pt_x[i];
    pt_lag = 1; pt_neg = pt_lobe = pt_nk = 0; pt_lmax = 0; pt_ltau = 0; pt_stop = PT_CMAX;
    pt_nc[0] = 32767;
    return 1;
}

/* one coarse lag; 1 when the lag search is over */
static int pt_coarse_step(void)
{
    int t = pt_lag;
    int32_t r = 0;
    const int16_t *a = pt_c, *b = pt_c + t;
    for (int i = 0; i < PT_CW; i++) r += a[i] * b[i];
    int n = pt_nsdf(r, pt_e0, pt_et);
    pt_nc[t] = (int16_t)n;
    pt_et += pt_c[t + PT_CW] * pt_c[t + PT_CW] - pt_c[t] * pt_c[t];
    if (!pt_neg) { if (n <= 0) pt_neg = 1; }
    else if (n > 0) { if (!pt_lobe || n > pt_lmax) { pt_lmax = n; pt_ltau = t; } pt_lobe = 1; }
    else if (pt_lobe) {
        pt_key_push(pt_ltau, pt_lmax);
        if (pt_lmax >= 26214 && pt_stop == PT_CMAX) pt_stop = clamp(pt_ltau * 2 + pt_ltau / 4 + 2, t, PT_CMAX);
        pt_lobe = 0;
    }
    pt_lag = t + 1;
    if (pt_lag <= pt_stop) return 0;
    if (pt_lobe && pt_ltau < t) pt_key_push(pt_ltau, pt_lmax); /* a lobe cut off past its peak */
    pt_nc[pt_lag] = pt_nc[t];
    return 1;
}

/* the coarse pick, then the full-rate refinement: the period in Q8, 0 none */
static int pt_finish(void)
{
    int best = 0, k = -1;
    for (int i = 0; i < pt_nk; i++) if (pt_kv[i] > best) best = pt_kv[i];
    for (int i = 0; i < pt_nk && k < 0; i++) if (pt_kv[i] >= best * 86 / 100) k = i;
    if (k < 0 || best * 100 < pt_clar * 32767) return 0;
    int t = pt_kt[k];
    int tc = t * 256 + pt_parab(pt_nc[t - 1], pt_nc[t], pt_nc[t + 1]);
    pt_ccoarse = tc;
    int v1, v2;
    int tf = pt_climb((2 * tc + 128) >> 8, &v1);
    if (v1 * 100 < pt_clar * 32767) return 0;
    int m = (PT_LONG << 8) / tf;
    if (m >= 2) {
        int tm = pt_climb((m * tf + 128) >> 8, &v2);
        int per = tm / m;
        if (v2 >= v1 - 4915 && iabs(per - tf) < tf / 16) tf = per;
    }
    pt_conf = v1;
    return tf;
}

/* period (Q8) -> note x 100 (cents within) */
static int pt_note100(int per)
{
    if (per >= pt_per[0] || per <= pt_per[84]) return -1;
    int lo = 0, hi = 84;
    while (hi - lo > 1) { int mid = (lo + hi) / 2; if (pt_per[mid] >= per) lo = mid; else hi = mid; }
    return (24 + lo) * 100 + (pt_per[lo] - per) * 100 / (pt_per[lo] - pt_per[hi]);
}

/* ---- MIDI: what is wanted, and what sounds -------------------------------------------------------- */
static uint8_t pt_snd[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, pt_sch[8], pt_stale;
static uint8_t pt_want[8];
static int pt_nwant, pt_velo = 100, pt_cc_ch = -1, pt_cleared, pt_bent = 8192, pt_arp_t, pt_arp_i;
static uint8_t pt_chord[8];
static int pt_nchord;

/* nearest note of the scale, ties downward */
static int pt_snap(int note)
{
    uint16_t m = gb_scales[pt_scale];
    for (int d = 0; d < 12; d++) {
        int lo = note - d, hi = note + d;
        if ((m >> ((unsigned)(lo + 132 - pt_root) % 12u)) & 1u) return lo;
        if ((m >> ((unsigned)(hi + 132 - pt_root) % 12u)) & 1u) return hi;
    }
    return note;
}
/* the chord of `count` notes stacked in thirds of the scale over a scale note (chromatic: a dominant seventh) */
static void pt_build_chord(int root, int count)
{
    uint16_t m = gb_scales[pt_scale];
    int deg[12], n = 0;
    for (int k = 0; k < 12; k++) if ((m >> k) & 1) deg[n++] = k;
    pt_nchord = 0;
    if (n >= 12 || n < 3) {
        static const uint8_t dom[4] = {0, 4, 7, 10};
        for (int i = 0; i < count; i++) pt_chord[pt_nchord++] = (uint8_t)clamp(root + dom[i], 0, 127);
        return;
    }
    int pc = (root - pt_root + 132) % 12, at = 0;
    for (int i = 0; i < n; i++) if (deg[i] == pc) at = i;
    for (int i = 0; i < count; i++) {
        int j = at + 2 * i, oct = j / n;
        pt_chord[pt_nchord++] = (uint8_t)clamp(root + deg[j % n] - deg[at] + 12 * oct, 0, 127);
    }
}
static void pt_want_one(int n) { pt_want[0] = (uint8_t)clamp(n, 0, 127); pt_nwant = 1; }

/* a new note (onset 1: struck again; 0: the pitch moved) */
static void pt_play(int note, int onset)
{
    int base = note + 12 * pt_oct;
    if (onset) for (int i = 0; i < 8; i++) if (pt_snd[i] != 0xff) pt_stale |= (uint8_t)(1u << i);
    pt_velo = pt_vel ? pt_vel : clamp(127 + 12 * (pt_log2((unsigned)pt_peak) - 14), 24, 127);
    if (pt_mode == PM_NOTE) pt_want_one(base);
    else if (pt_mode == PM_SNAP) pt_want_one(pt_snap(base));
    else if (pt_mode == PM_CHORD || pt_mode == PM_SEVENTH || pt_mode == PM_ARP) {
        pt_build_chord(pt_snap(base), pt_mode == PM_CHORD ? 3 : 4);
        if (pt_mode == PM_ARP) { pt_arp_i = 0; pt_arp_t = 0; pt_want_one(pt_chord[0]); }
        else { for (int i = 0; i < pt_nchord; i++) pt_want[i] = pt_chord[i]; pt_nwant = pt_nchord; }
    } else if (pt_mode == PM_SCALE) pt_want_one(pt_snap(base + range(-7, 7)));
    else pt_want_one(pt_snap(base) + 12 * range(-1, 2));
}

/* send offs (stale, unwanted, wrong channel), a pending CC123, then ons; the budget may stop it: next tick */
static void pt_sync(void)
{
    for (int i = 0; i < 8; i++) {
        if (pt_snd[i] == 0xff) continue;
        int keep = pt_on && !((pt_stale >> i) & 1) && pt_sch[i] == pt_chan;
        for (int j = 0; keep && j < pt_nwant; j++) if (pt_want[j] == pt_snd[i]) keep = 2;
        if (keep == 2) continue;
        if (!midi_out(0x80u | pt_sch[i], pt_snd[i], 0)) return;
        pt_snd[i] = 0xff; pt_stale &= (uint8_t)~(1u << i);
    }
    if (pt_cc_ch >= 0) { if (!midi_out(0xb0u | (unsigned)pt_cc_ch, 123, 0)) return; pt_cc_ch = -1; }
    if (!pt_on) return;
    for (int j = 0; j < pt_nwant; j++) {
        int have = 0, free = -1;
        for (int i = 0; i < 8; i++) { if (pt_snd[i] == pt_want[j]) have = 1; else if (pt_snd[i] == 0xff && free < 0) free = i; }
        if (have || free < 0) continue;
        if (pt_bend && pt_mode == PM_NOTE && pt_bent != 8192) { if (!midi_out(0xe0u | (unsigned)pt_chan, 0, 64)) return; pt_bent = 8192; }
        if (!midi_out(0x90u | (unsigned)pt_chan, pt_want[j], (unsigned)pt_velo)) return;
        pt_snd[free] = pt_want[j]; pt_sch[free] = (uint8_t)pt_chan; pt_noteons++;
    }
}
static void pt_release(void) { pt_nwant = 0; pt_out = -1; pt_cnt = 0; pt_nmed = 0; }
static void pt_panic(void) { pt_release(); pt_stale = 0xff; pt_cc_ch = pt_chan; }

/* one estimate: per (Q8) or 0 for none */
static void pt_estimate(int per)
{
    int x100 = per ? pt_note100(per) : -1;
    pt_estimates++;
    if (x100 < 3100 || x100 > 9650) x100 = -1;
    pt_valid = x100 >= 0;
    if (!pt_valid) {
        if (++pt_unv >= 2 || !pt_gate_open) pt_release();
        return;
    }
    pt_unv = 0;
    pt_x100 = x100;
    pt_hz10 = 28224000 / per;
    /* the median of the last three, a candidate note that must hold, and a 65-cent hysteresis */
    if (pt_nmed < 3) pt_med[pt_nmed++] = x100; else { pt_med[0] = pt_med[1]; pt_med[1] = pt_med[2]; pt_med[2] = x100; }
    int m = x100;
    if (pt_nmed == 3) { int a = pt_med[0], b = pt_med[1], c = pt_med[2]; m = a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b); }
    else if (pt_nmed == 2) m = (pt_med[0] + pt_med[1]) / 2;
    int cand = (m + 50) / 100;
    pt_cnt = cand == pt_cand ? pt_cnt + 1 : 1;
    pt_cand = cand;
    int struck = pt_peak > pt_lastpeak * 5 / 2 && pt_lastpeak > 0;
    if (pt_out < 0) { if (pt_cnt >= pt_stab) { pt_out = cand; pt_play(cand, 1); } }
    else if (cand != pt_out && iabs(m - pt_out * 100) > 65 && pt_cnt >= pt_stab) { pt_out = cand; pt_play(cand, pt_mode == PM_SCALE || pt_mode == PM_JUMP); }
    else if (struck) pt_play(pt_out, 1);
    if (pt_bend && pt_mode == PM_NOTE && pt_out >= 0) {
        int b = clamp(8192 + (m - pt_out * 100) * 4096 / 100, 0, 16383);
        if (iabs(b - pt_bent) > 40 && midi_out(0xe0u | (unsigned)pt_chan, (unsigned)(b & 127), (unsigned)(b >> 7))) pt_bent = b;
    }
}

void pitch_tick(void)
{
    if (!pt_cleared) { if (midi_out(0xb0u | (unsigned)pt_chan, 123, 0)) pt_cleared = 1; return; }
    int active = pt_on || ticks - pt_view < 30u;
    if (active && !pt_cap) { pt_start = pt_frames; pt_seen = pt_frames; pt_quiet = ticks; pt_state = 0; }
    pt_cap = (uint8_t)active;
#if defined(PREVIEW) && defined(AERIAOS)
    aos_tap_want(2, active);
#endif
    if (!active) { pt_release(); pt_sync(); return; }
    if (pt_frames != pt_seen) { pt_seen = pt_frames; pt_quiet = ticks; pt_nosite = 0; }
    else if (ticks - pt_quiet > 20u) pt_nosite = 1;
    /* the arpeggio walks the chord up and down, an octave over the top */
    if (pt_mode == PM_ARP && pt_out >= 0 && pt_nchord && ++pt_arp_t >= pt_rate) {
        static const uint8_t walk[8] = {0, 1, 2, 3, 4, 3, 2, 1};
        pt_arp_t = 0; pt_arp_i = (pt_arp_i + 1) & 7;
        int w = walk[pt_arp_i];
        pt_want_one(w < pt_nchord ? pt_chord[w] : pt_chord[0] + 12);
        pt_stale = 0xff;
    }
    int budget = PT_BUDGET;
    while (budget > 0 && pt_frames - pt_start >= PT_N / 4) {
        if (pt_state == 0) {
            budget -= PT_N * 2;
            if (pt_snapshot()) pt_state = 1; else { pt_estimate(0); break; }
        } else if (pt_state == 1) {
            budget -= PT_CW;
            if (pt_coarse_step()) pt_state = 2;
        } else {
            budget -= 12 * PT_W * 2;
            pt_estimate(pt_finish());
            pt_state = 0;
            break;
        }
    }
    pt_sync();
    if ((ticks & 1) == 0) { pt_hist[pt_hpos] = (int16_t)(pt_valid ? pt_x100 : 0); pt_hpos = (pt_hpos + 1) % PT_HIST; }
}

/* ---- the page --------------------------------------------------------------------------------------- */
static void pt_big(int x, int y, const char *s, int k)
{
    for (; *s; s++, x += 4 * k) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
            if (glyphs[g] & (1u << (14 - (r * 3 + c)))) for (int dy = 0; dy < k; dy++) for (int dx = 0; dx < k; dx++) px(x + c * k + dx, y + r * k + dy);
    }
}
/* decimal with an optional tenth */
static void pt_fmt(char *o, int v, int tenths)
{
    char t[8]; int n = 0, i = 0;
    if (v < 0) { o[i++] = '-'; v = -v; }
    int whole = tenths ? v / 10 : v;
    do { t[n++] = (char)('0' + whole % 10); whole /= 10; } while (whole && n < 6);
    while (n) o[i++] = t[--n];
    if (tenths) { o[i++] = '.'; o[i++] = (char)('0' + v % 10); }
    o[i] = 0;
}
static int pt_cat(char *o, int i, const char *s) { while (*s && i < 30) o[i++] = *s++; o[i] = 0; return i; }
static void pt_bar(int x, int y, int w, int v, int max)
{
    for (int i = 0; i < w; i++) { px(x + i, y); px(x + i, y + 3); }
    span(x, y, y + 3, 1); span(x + w - 1, y, y + 3, 1);
    int f = clamp(v, 0, max) * (w - 2) / (max ? max : 1);
    for (int i = 1; i <= f; i++) span(x + i, y + 1, y + 2, 1);
}

/* little notes that float up from the kite when one plays */
static int16_t pt_fx[6], pt_fy[6];
static uint8_t pt_flife[6];
static uint32_t pt_last_on;

void pitch_draw(void)
{
    pt_view = ticks;
    for (int x = 0; x < W; x++) span(x, 9, H - 1, 0);
    char s[32];
    int on = pt_out >= 0 && pt_valid;
    if (pt_valid) {
        int n = (pt_x100 + 50) / 100, cents = pt_x100 - n * 100;
        pt_big(3, 11, gb_note_name[n % 12], 3);
        pt_fmt(s, n / 12 - 1, 0);
        pt_big(gb_note_name[n % 12][1] ? 28 : 16, 17, s, 2);
        pt_fmt(s, pt_hz10, 1); { int i = 0; while (s[i]) i++; pt_cat(s, i, " HZ"); }
        text(46, 10, s, 1);
        s[0] = cents >= 0 ? '+' : '-'; pt_fmt(s + 1, iabs(cents), 0);
        text(W - 2 - text_w(s), 10, s, 1);
        /* the cents needle */
        for (int x = 46; x < W - 2; x += 2) px(x, 19);
        span(86, 16, 22, 1);
        int nx = 86 + cents * 38 / 50;
        span(nx - 1, 17, 21, 1); span(nx, 16, 22, 1); span(nx + 1, 17, 21, 1);
    } else {
        text(5, 14, pt_gate_open ? "..." : "-", 1);
        text(46, 10, pt_nosite ? "NO INPUT TAP" : pt_gate_open ? "LISTENING" : "QUIET", 1);
        if (pt_nosite) text(46, 17, "BUILD WITH THE SITE", 1);
    }
    /* clarity and level */
    text(46, 25, "CL", 1); pt_bar(55, 25, 30, pt_valid ? pt_conf : 0, 32767);
    text(88, 25, "IN", 1); pt_bar(97, 25, 29, pt_log2((unsigned)pt_peak), 15);
    { int g = 97 + pt_log2(pt_gates[pt_gate]) * 27 / 15; px(g, 24); px(g, 29); }
    /* the trail: a semitone a pixel round the note being sung; the kite at its head */
    int centre = pt_valid ? pt_x100 : 6000;
    for (int i = PT_HIST - 1, prev = -1; i >= 0; i--) {
        int v = pt_hist[(pt_hpos + i) % PT_HIST], x = 4 + i * 100 / PT_HIST;
        if (!v) { prev = -1; continue; }
        int y = clamp(43 - (v - centre) / 100, 31, 55);
        if (prev >= 0) line(x, y, x + 1, prev); else px(x, y);
        prev = y;
    }
    int ky = 43, kx = 106 + sine[(int)(ticks * 2) & 63] / 60;
    int last = pt_hist[(pt_hpos + PT_HIST - 1) % PT_HIST];
    if (last) ky = clamp(43 - (last - centre) / 100, 33, 52);
    for (int d = -4; d <= 4; d++) { int w = 3 - iabs(d) * 3 / 4; if (on) for (int x = -w; x <= w; x++) px(kx + x, ky + d); else { px(kx - w, ky + d); px(kx + w, ky + d); } }
    for (int i = 0; i < 9; i++) px(kx - 1 - i + sine[(i * 6 + (int)ticks * 3) & 63] / 90, ky + 5 + i);
    line(kx - 14, ky, kx - 4, ky);
    if (pt_noteons != pt_last_on) {
        for (int i = 0; i < 6; i++) if (!pt_flife[i]) { pt_fx[i] = (int16_t)(kx + range(-3, 3)); pt_fy[i] = (int16_t)(ky - 6); pt_flife[i] = (uint8_t)range(20, 40); break; }
    }
    pt_last_on = pt_noteons;
    for (int i = 0; i < 6; i++) {
        if (!pt_flife[i]) continue;
        pt_flife[i]--;
        if ((ticks & 1) == 0) pt_fy[i]--;
        if ((ticks & 3) == 0) pt_fx[i] = (int16_t)(pt_fx[i] - 1);
        int x = pt_fx[i], y = pt_fy[i];
        if (y < 31) { pt_flife[i] = 0; continue; }
        px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1); span(x + 2, y - 4, y + 1, 1); px(x + 3, y - 3);
    }
    /* what plays */
    if (pt_on && pt_nwant) {
        int i = 0; s[0] = 0;
        for (int j = 0; j < pt_nwant && j < 4; j++) { i = pt_cat(s, i, gb_note_name[pt_want[j] % 12]); i = pt_cat(s, i, " "); }
        text(4, 50, s, 1);
    }
    int i = pt_cat(s, 0, pt_mode_name[pt_mode]);
    if (pt_mode != PM_NOTE) { i = pt_cat(s, i, " "); i = pt_cat(s, i, gb_note_name[pt_root]); i = pt_cat(s, i, " "); i = pt_cat(s, i, gb_scale_name[pt_scale]); }
    text(2, 58, s, 1);
    pt_cat(s, 0, "CH"); pt_fmt(s + 2, pt_chan + 1, 0);
    i = 0; while (s[i]) i++;
    pt_cat(s, i, pt_on ? " ON" : " OFF");
    text(W - 2 - text_w(s), 58, s, 1);
}

static void pt_switch(int on)
{
    pt_on = on;
    if (!on) { pt_release(); pt_cc_ch = pt_chan; }
    say("PITCH MIDI", on ? "ON" : "OFF");
}

int pitch_key(int code)
{
    if (code == KEY_YES) { pt_switch(!pt_on); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + PM_COUNT) { pt_mode = code - KEY_TRIG0; pt_release(); pt_stale = 0xff; say("PITCH", pt_mode_name[pt_mode]); return 1; }
    if (code == KEY_TRIG0 + 15) { pt_panic(); say("PITCH", "ALL OFF"); return 1; }
    return 0;
}

void pitch_enc(int e, int d, int func)
{
    char n[12];
    int s = d > 0 ? 1 : -1;
    if (!func) {
        if (e == 0) { pt_mode = clamp(pt_mode + s, 0, PM_COUNT - 1); pt_release(); pt_stale = 0xff; say("MODE", pt_mode_name[pt_mode]); }
        else if (e == 1) { pt_scale = clamp(pt_scale + s, 0, GB_NSCALES - 1); say("SCALE", gb_scale_name[pt_scale]); }
        else if (e == 2) { pt_root = (pt_root + s + 12) % 12; say("ROOT", gb_note_name[pt_root]); }
        else if (e == 3) { pt_oct = clamp(pt_oct + s, -3, 3); n[0] = pt_oct < 0 ? '-' : '+'; pt_fmt(n + 1, iabs(pt_oct), 0); say("OCTAVE", n); if (pt_out >= 0) pt_play(pt_out, 1); }
        else if (e == 4) { pt_gate = clamp(pt_gate + s, 0, 16); pt_fmt(n, -66 + 3 * pt_gate, 0); pt_cat(n, 3, " DB"); say("GATE", n); }
        else if (e == 5) { pt_chan = clamp(pt_chan + s, 0, 15); pt_fmt(n, pt_chan + 1, 0); say("CHANNEL", n); }
    } else {
        if (e == 0) { pt_vel = clamp(pt_vel + d, 0, 127); if (pt_vel) pt_fmt(n, pt_vel, 0); else pt_cat(n, 0, "INPUT"); say("VELOCITY", n); }
        else if (e == 1) { pt_rate = clamp(pt_rate + s, 1, 24); pt_fmt(n, pt_rate, 0); say("ARP TICKS", n); }
        else if (e == 2) { pt_stab = clamp(pt_stab + s, 1, 5); pt_fmt(n, pt_stab, 0); say("STEADY", n); }
        else if (e == 3) { pt_src = (uint8_t)clamp(pt_src + s, 0, 5); say("LISTEN", pt_src_name[pt_src]); }
        else if (e == 4) { pt_bend = d > 0; say("BEND", pt_bend ? "ON" : "OFF"); }
        else if (e == 5) { pt_clar = clamp(pt_clar + s * 5, 30, 95); pt_fmt(n, pt_clar, 0); say("CLARITY", n); }
    }
}
/* ---- AERIA OS bring-up: the output test run from INSIDE the stock firmware ------------------------------------
 * SYSTEM page, trig 16. RAM-booted AERIA OS sets every DSP register exactly as stock does (dumped and compared
 * 9 Nov 2026) and its outputs stay silent, but a RAM boot passes through a soft reset, which clears every ColdFire
 * peripheral stock had set up. This runs AERIA OS's own DSP tone test without that reset: interrupts off, the
 * ColdFire's peripherals as stock left them, both DSP cores parked (Modwerk's host command $0F), AERIA OS's DSP
 * programs loaded, the tone on every output slot, input peaks logged. First it snapshots the ColdFire's clock, bus,
 * timer, SSI, interrupt and DMA registers. Then a soft reset hands back (stock reloads the DSPs as after any test).
 * Battery RAM 0x100FB800: 'STKT', the snapshot (1 KB), then the heartbeats. */
/* generated from dsp/st0.asm by dsp/mkinc.py: 282 words at P:$33000 */

static const uint32_t dsp_st0[282] = {0x05f439u, 0x000300u, 0x09f4a4u, 0x003fffu, 0x0abd6eu, 0x0abe6eu, 0x07b494u, 0x000040u, 0x44f400u, 0x123456u, 0x04c420u, 0x07b496u, 0xf00f00u, 0x07b495u, 0x037901u, 0x07b498u, 0x900f00u, 0x07b497u, 0x017901u, 0x07b49bu, 0x0000ffu, 0x07b49cu, 0x000000u, 0x07b499u, 0x0000ffu, 0x07b49au, 0x000000u, 0x07b49fu, 0xffffffu, 0x07b49eu, 0xffffffu, 0x07f418u, 0xaa0000u, 0x07f434u, 0x000040u, 0x200013u, 0x04cec0u, 0x07f436u, 0xf40f00u, 0x07f435u, 0x037d01u, 0x07f438u, 0xf40f00u, 0x07f437u, 0x017d01u, 0x07f43bu, 0x00000fu, 0x07f43cu, 0x000000u, 0x07f439u, 0x0000ffu, 0x07f43au, 0x000000u, 0x07f43fu, 0xffffffu, 0x07f43eu, 0xffffffu, 0x200013u, 0x563f00u, 0x01b38du, 0x03303bu, 0x44f400u, 0x000007u, 0x209600u, 0x01b38fu, 0x033040u, 0x04cec0u, 0x22cf00u, 0x45f400u, 0x000001u, 0x20006cu, 0x21b600u, 0x0d1042u, 0x000004u, 0x0d10c0u, 0x000004u, 0x0a3f80u, 0x033040u, 0x200013u, 0x563300u, 0x563f00u, 0x563100u, 0x563200u, 0x563400u, 0x563600u, 0x563700u, 0x563800u, 0x563900u, 0x563a00u, 0x563b00u, 0x563c00u, 0x44f400u, 0x000008u, 0x443000u, 0x44f400u, 0x180000u, 0x443500u, 0x65f400u, 0x000000u, 0x47f400u, 0x015888u, 0x0a83a3u, 0x0330fau, 0x0a8380u, 0x03306bu, 0x084406u, 0x443000u, 0x56b000u, 0x45f400u, 0x000010u, 0x200065u, 0x0d1049u, 0x000021u, 0x0193cfu, 0x033090u, 0x56b300u, 0x45b000u, 0x46f400u, 0x000010u, 0x200055u, 0x20001bu, 0x20ae00u, 0x46f400u, 0x000019u, 0x200055u, 0x0d104au, 0x000009u, 0x56b300u, 0x46f400u, 0x000010u, 0x200050u, 0x200065u, 0x0d1042u, 0x000003u, 0x57b500u, 0x04cf20u, 0x56b300u, 0x46f400u, 0x000001u, 0x200050u, 0x46f400u, 0x000007u, 0x200056u, 0x543300u, 0x01b388u, 0x0330acu, 0x01b386u, 0x03309bu, 0x200013u, 0x563200u, 0x56b700u, 0x45f400u, 0x000001u, 0x200060u, 0x543700u, 0x044ec8u, 0x200026u, 0x62b200u, 0x72f400u, 0x000038u, 0x000000u, 0x45ea00u, 0x200065u, 0x0d104fu, 0x000003u, 0x546a00u, 0x205a00u, 0x224e00u, 0x45f400u, 0x000003u, 0x200066u, 0x543200u, 0x01b38fu, 0x033065u, 0x01b38du, 0x0330b5u, 0x56b600u, 0x45f400u, 0x000001u, 0x200060u, 0x543600u, 0x56b400u, 0x45f400u, 0x000001u, 0x200060u, 0x45f400u, 0x000190u, 0x200065u, 0x0d1049u, 0x000006u, 0x200013u, 0x57b500u, 0x20003eu, 0x553500u, 0x543400u, 0x57b000u, 0x45f400u, 0x000009u, 0x20006du, 0x0d104au, 0x00000au, 0x56b100u, 0x45b000u, 0x200065u, 0x0d104au, 0x000006u, 0x20001bu, 0x0d10c0u, 0x000004u, 0x56b100u, 0x57b500u, 0x04cfc0u, 0x45f400u, 0x000001u, 0x200060u, 0x45f400u, 0x000007u, 0x200066u, 0x543100u, 0x205d00u, 0x22ae00u, 0x200075u, 0x0d1049u, 0x00001au, 0x65f400u, 0x000000u, 0x0a8381u, 0x0330f8u, 0x62bc00u, 0x72f400u, 0x000038u, 0x000000u, 0x56ea00u, 0x0c1ec4u, 0x218f00u, 0x224e00u, 0x0c1eacu, 0x218400u, 0x20004au, 0x08cd07u, 0x200013u, 0x566a00u, 0x205a00u, 0x224e00u, 0x45f400u, 0x000003u, 0x200066u, 0x543c00u, 0x0a3f80u, 0x033065u, 0x07f435u, 0x000000u, 0x07f437u, 0x000000u, 0x07b495u, 0x000000u, 0x07b497u, 0x000000u, 0x0a8406u, 0x0a8407u, 0x0a8426u, 0x0abd4eu, 0x0abe4eu, 0x45f400u, 0x000001u, 0x0a8380u, 0x033109u, 0x084406u, 0x208e00u, 0x0a8380u, 0x03310du, 0x085006u, 0x221100u, 0x0a8380u, 0x033111u, 0x085886u, 0x200064u, 0x0d104au, 0x000004u, 0x0a3f80u, 0x033111u, 0x0ae180u};
/* generated from dsp/st1.asm by dsp/mkinc.py: 36 words at P:$34000 */

static const uint32_t dsp_st1[36] = {0x05f439u, 0x000300u, 0x09f4a4u, 0x003fffu, 0x0abd6eu, 0x0abe6eu, 0x200013u, 0x563f00u, 0x0a83a3u, 0x03400cu, 0x0a3f80u, 0x034008u, 0x0a8406u, 0x0a8407u, 0x0a8426u, 0x0abd4eu, 0x0abe4eu, 0x45f400u, 0x000001u, 0x0a8380u, 0x034013u, 0x084406u, 0x208e00u, 0x0a8380u, 0x034017u, 0x085006u, 0x221100u, 0x0a8380u, 0x03401bu, 0x085886u, 0x200064u, 0x0d104au, 0x000004u, 0x0a3f80u, 0x03401bu, 0x0ae180u};
#ifndef PREVIEW
/* v40: AERIA OS's own image (aeria-os/build/aeria-os.bin), launched warm from inside stock: no soft reset */
static const uint32_t aos_img[1852] = {0x4fefffe4u,0x46fc2700u,0x2e7c47e0u,0x000041f9u,0x400020f0u,0x43f94000u,0x29e4b1c9u,0x64044298u,0x60f8207cu,0x40000000u,0x203c4000u,0x044c223cu,0x00000100u,0x20c05381u,0x66fa203cu,0x40000000u,0x4e7b0801u,0x4eb94000u,0x08a660feu,0x46fc2700u,0x2017222fu,0x00042e7cu,0x47e00000u,0x2f012f00u,0x4eb94000u,0x084860feu,0x2039100fu,0x90080c80u,0x00000fffu,0x620e2040u,0xd1fc100fu,0x9000116fu,0x00070718u,0x528023c0u,0x100f9008u,0x4e752f0au,0x2f024878u,0x00ee45fau,0xffd04e92u,0x75af0013u,0x2f024e92u,0x508f23c2u,0x100f9004u,0x241f245fu,0x4e752f02u,0x740f202fu,0x000c2200u,0xe889206fu,0x000843f9u,0x40001798u,0xc082c282u,0x10b11800u,0xd3c04200u,0x11510001u,0x11400002u,0x241f4e75u,0x2f0a202fu,0x000891c8u,0x24402252u,0x22095281u,0x2481246fu,0x000c12b2u,0x88005288u,0xb1ef0010u,0x66e6245fu,0x4e75122fu,0x00072079u,0xfc07c00cu,0x1039fc06u,0x400444c0u,0x662a13c1u,0xfc06400cu,0x2039100fu,0xa7180c80u,0x000007ffu,0x620c2040u,0xd1fc100fu,0x90001141u,0x171c5280u,0x23c0100fu,0xa7186016u,0x2039fc07u,0xc00c9088u,0x0c80000au,0x122063bcu,0x52b9100fu,0x90144e75u,0x4fefffecu,0x48d71c0cu,0x428245f9u,0x4000214du,0x47f94000u,0x213c49fau,0xff924aafu,0x0018660cu,0x73b22800u,0x71b32800u,0xb081671au,0x70208082u,0x71802f00u,0x4e941632u,0x28007183u,0x2f004e94u,0x17832800u,0x508f5282u,0x700bb082u,0x66cc4cd7u,0x1c0c4fefu,0x00144e75u,0x2f0a41efu,0x000823d0u,0x100f9050u,0x4ab94000u,0x20f8663au,0x4ab94000u,0x20f46612u,0x48780060u,0x45faff30u,0x4e924878u,0x00024e92u,0x508f2239u,0xfc07c00cu,0x2039fc07u,0xc00c9081u,0x0c80003cu,0x6cbf63f0u,0x708013c0u,0xfc0a0000u,0x60fe4ab9u,0x400020f0u,0x665c4200u,0x720813c0u,0xfc0a400cu,0x33c12000u,0x00002239u,0xfc07c00cu,0x2039fc07u,0xc00c9081u,0x0c800014u,0x243f63f0u,0x30392000u,0x00082200u,0x70087381u,0x23c1100fu,0x90703239u,0x20000008u,0x08010000u,0x6700ff7au,0x32392000u,0x001c52b9u,0x100f9684u,0x53804a80u,0x66e06000u,0xff647001u,0x720813c0u,0xfc0a400cu,0x33c12000u,0x00002239u,0xfc07c00cu,0x2039fc07u,0xc00c9081u,0x0c80000au,0x121f63f0u,0x70083239u,0x20000008u,0x08010000u,0x6700ff70u,0x32392000u,0x001c5380u,0x4a8066e6u,0x6000ff60u,0x226f0004u,0x727f202fu,0x0008b289u,0x6532723fu,0xb280652cu,0x22004a80u,0x6c025e81u,0xe6812041u,0x72079288u,0xef892041u,0x7207c081u,0x7201d1fcu,0x400025e2u,0xe1a91030u,0x98008081u,0x11809800u,0x4e754fefu,0xffe048d7u,0x3c1c266fu,0x002c49f9u,0x40002092u,0x4bfaffaau,0x1013660au,0x4cd73c1cu,0x4fef0020u,0x4e752200u,0x0681ffffu,0xffbf7581u,0x528b7619u,0xb6826546u,0x710179f4u,0x0a00262fu,0x00284282u,0x700e95cau,0x90822f40u,0x001c202fu,0x001c908au,0x0104670eu,0x2f03206fu,0x00284870u,0xa8004e95u,0x508f528au,0x7003b08au,0x66e05682u,0x5283720fu,0xb28266ccu,0x58af0024u,0x609a2200u,0x0681ffffu,0xffd07381u,0x7409b481u,0x650a0680u,0xffffffeau,0x710060a2u,0x7100762bu,0xb6806710u,0x722db280u,0x670e743au,0xb48066ccu,0x7026608au,0x70246086u,0x70256082u,0x2f02202fu,0x00082079u,0xfc07c00cu,0x32392000u,0x00087406u,0xc2824a41u,0x672c2200u,0x42414841u,0x738133c1u,0x20000014u,0x2200e089u,0x71807381u,0x33c12000u,0x001833c0u,0x2000001cu,0x700152b9u,0x100f9064u,0x60182239u,0xfc07c00cu,0x92880c81u,0x00284880u,0x63b652b9u,0x100f9068u,0x4280241fu,0x4e752239u,0xfc07c00cu,0x598f1039u,0xfc064004u,0x08000000u,0x671a71b9u,0xfc06400cu,0x2f002f40u,0x00044ebau,0xfc40202fu,0x0004588fu,0x588f4e75u,0x2039fc07u,0xc00c9081u,0x0c8000c9u,0x6a8063cau,0x70ff60e8u,0x7212202fu,0x0004e2a8u,0x41ef0008u,0x718023c0u,0x100f9010u,0x23d0100fu,0x90144878u,0x00fa4ebau,0xfd4c202fu,0x00042200u,0xd2af000cu,0x2040b288u,0x66024e75u,0x10ef000bu,0x60f42f0au,0x202f0008u,0x4281206fu,0x000cb2afu,0x00106604u,0x245f4e75u,0x45f01800u,0x22401392u,0x18005281u,0x60e84e56u,0xff9c207cu,0x100f9000u,0x48d73cfcu,0x4298b1fcu,0x100fb11cu,0x66f6203cu,0x414f5331u,0x23c0100fu,0x9000223cu,0x100f9000u,0x42802240u,0x2041d3fcu,0xfc0a4000u,0xd1c01151u,0x1f1c5280u,0x0c800000u,0x010066e6u,0x424171b9u,0xfc064004u,0x76037a0bu,0x74ff23c0u,0x100f901cu,0x33c1fc07u,0xc00023c2u,0xfc07c004u,0x42b9fc07u,0xc00c13c3u,0xfc07c003u,0x33c5fc07u,0xc0002239u,0x100fb7f0u,0x0c815741u,0x524d57c0u,0x44807180u,0x23c04000u,0x20f442b9u,0x100fb7f0u,0x23c0100fu,0x902c0c81u,0x5741524du,0x67000142u,0x43f94000u,0x18127e01u,0x701013c7u,0xfc0a4075u,0x13c0fc0au,0x40794280u,0x73b10800u,0x52802041u,0xd1fcfc0au,0x40004210u,0x720ab280u,0x66ea207cu,0xfc0a4022u,0x74201210u,0x76407abfu,0x82821081u,0x12108283u,0x108113c5u,0xfc0a4052u,0x2079fc07u,0xc00c2239u,0xfc07c00cu,0x92880c81u,0x00000527u,0x63f0207cu,0xfc0a4022u,0x7ebf1210u,0xc2871081u,0x722013c1u,0xfc0a403au,0x2279fc07u,0xc00c207cu,0xfc07c00cu,0x22109289u,0x0c810000u,0x010763eeu,0x1239fc0au,0x403a0801u,0x00066700u,0x049a74dfu,0x13c2fc0au,0x40522050u,0x2239fc07u,0xc00c9288u,0x0c810000u,0x010763f0u,0x1239fc0au,0x403a0801u,0x00066600u,0x046a7602u,0x23c3100fu,0x902071b9u,0xfc0a403au,0x7e337220u,0x23c0100fu,0x90247043u,0x74307640u,0x7a1013c7u,0xfc0a4069u,0x7edd13c0u,0xfc0a4066u,0x13c1fc06u,0x40087013u,0x13c2fc06u,0x40087207u,0x420213c3u,0xfc064008u,0x13c5fc06u,0x4008760du,0x7a0513c7u,0xfc064004u,0x13c0fc06u,0x400013c1u,0xfc064000u,0x13c2fc06u,0x401413c2u,0xfc064018u,0x13c3fc06u,0x401c13c5u,0xfc064008u,0x26394000u,0x20f44a83u,0x6600011eu,0x48780001u,0x47faf9f0u,0x4e932439u,0xfc07c00cu,0x588f2039u,0xfc07c00cu,0x90820c80u,0x00c96a7fu,0x630003d4u,0x48780002u,0x74054e93u,0x48780060u,0x45fafa3cu,0x4e924878u,0x00024e92u,0x48780070u,0x4e9242a7u,0x4e924fefu,0x001449fau,0xfd224e94u,0x52804a80u,0x67065382u,0x4a8266f2u,0x48780060u,0x4e9242a7u,0x4e92508fu,0x2439fc07u,0xc00c2039u,0xfc07c00cu,0x90820c80u,0x03ef147fu,0x63000396u,0x48780003u,0x283c100fu,0x90004e93u,0x48780074u,0x4e9242a7u,0x4e924fefu,0x000c4282u,0x4e9472ffu,0xb2806716u,0x2044d1c2u,0x11400708u,0x52827a0au,0x23c2100fu,0x9048ba82u,0x66e24878u,0x00604e92u,0x42a74e92u,0x508f2439u,0xfc07c00cu,0x2039fc07u,0xc00c9082u,0x0c800064u,0xb53f6300u,0x03584878u,0x00044e93u,0x588f2e03u,0x52834878u,0x003f7f87u,0x4e922f07u,0x4e92508fu,0x7058b083u,0x66e84878u,0x00014ebau,0xf9c04878u,0x00b74e92u,0x487800ffu,0x4e924878u,0x00054e93u,0x4fef0010u,0x72112e3cu,0x00000180u,0x343c0a00u,0x363c07e7u,0x3a3c8080u,0x207cfc0au,0x400913c1u,0xfc0a4060u,0x72fb33c2u,0xfc0a406au,0x33c3fc0au,0x406e33c5u,0xfc0a4072u,0x740423c7u,0xfc008020u,0x1010c081u,0x1080207cu,0xfc0a4021u,0x10108082u,0x10804ab9u,0x400020f4u,0x66000184u,0x48780040u,0x263c100fu,0xb4002f3cu,0xfc0a0000u,0x2a3c100fu,0xb800240eu,0x0682ffffu,0xffe845fau,0xf8b42f02u,0x2d43ffe8u,0x4e924878u,0x00102f3cu,0xfc0c4000u,0x2f024e92u,0x48780060u,0x2f3cfc00u,0x80002f02u,0x4e924fefu,0x00244878u,0x00402f3cu,0xfc0bc010u,0x2f024e92u,0x48780010u,0x2f3cfc07u,0x00002f02u,0x4e924878u,0x00102f3cu,0xfc074000u,0x2f024e92u,0x4fef0024u,0x48780010u,0x2f3cfc07u,0x80002f02u,0x4e924878u,0x00102f3cu,0xfc07c000u,0x2f024e92u,0x48780008u,0x2f3cfc08u,0x00002f02u,0x4e924fefu,0x00244878u,0x00082f3cu,0xfc084000u,0x2f024e92u,0x48780008u,0x2f3cfc08u,0x80002f02u,0x4e924878u,0x00082f3cu,0xfc08c000u,0x2f024e92u,0x4fef0024u,0x48780010u,0x2f3cfc09u,0x40002f02u,0x4e924878u,0x00402f3cu,0xfc048000u,0x2f024e92u,0x48780040u,0x2f3cfc04u,0xc0002f02u,0x4e924fefu,0x00244878u,0x00402f3cu,0xfc044000u,0x2f024e92u,0x48780038u,0x2f3cfc05u,0xc0002f02u,0x4e924878u,0x00402f3cu,0xfc000000u,0x2f024e92u,0x4fef0024u,0x48780100u,0x2f3cfc0au,0x40002f02u,0x2d45ffe8u,0x4e924878u,0x00802f3cu,0xfc040000u,0x2f024e92u,0x48780100u,0x2f3cfc00u,0x40002f02u,0x4e924fefu,0x00244878u,0x00402f3cu,0xfc0b8000u,0x2f024e92u,0x48780200u,0x2f3cfc0bu,0x00002f02u,0x4e924fefu,0x00187010u,0x420713c7u,0xfc0a400cu,0x32392000u,0x00080801u,0x00006712u,0x32392000u,0x001c52b9u,0x100f9060u,0x53804a80u,0x66e2307cu,0x00814240u,0x45faf9e6u,0x33c82000u,0x000033c0u,0x20000004u,0x4878011au,0x4e92588fu,0x4a806700u,0x014c2f3cu,0x00033000u,0x4e92588fu,0x4a806700u,0x013c47f9u,0x400018bau,0x2f134e92u,0x588f4a80u,0x6700012au,0x588bb7fcu,0x40001d22u,0x66ea223cu,0x52454753u,0x93c923c1u,0x100fb800u,0x42b9100fu,0xb8042239u,0xfc07c00cu,0x30392000u,0x00080800u,0x00006700u,0x00c03039u,0x20000014u,0x34392000u,0x00183639u,0x2000001cu,0x73c95289u,0xe5894840u,0x4240e18au,0x77832041u,0xd1fc100fu,0xb8080280u,0x00ff0000u,0x02820000u,0xff008082u,0x80832080u,0x23c9100fu,0xb804b3fcu,0x00000132u,0x66a46000u,0x00867a01u,0x23c5100fu,0x90205380u,0x4a806600u,0xfb346000u,0xfb8e1039u,0xfc064004u,0x08000000u,0x6700fc10u,0x71b9fc06u,0x400c2f00u,0x4ebaf5c2u,0x6000fbfeu,0x1039fc06u,0x40040800u,0x00006700u,0xfc4e71b9u,0xfc06400cu,0x2f004ebau,0xf5a4588fu,0x6000fc3cu,0x1039fc06u,0x40040800u,0x00006700u,0xfc8c71b9u,0xfc06400cu,0x2f004ebau,0xf584588fu,0x6000fc7au,0x2039fc07u,0xc00c9081u,0x0c800192u,0xd5006300u,0xff242239u,0xfc07c00cu,0x2039fc07u,0xc00c9081u,0x0c80000au,0x121f63f0u,0x30392000u,0x00082400u,0x758223c2u,0x100f906cu,0x2239100fu,0x90684a81u,0x57c07100u,0x448023c0u,0x400020f8u,0x4a81666cu,0x76017010u,0x13c3fc0au,0x400c3239u,0x20000008u,0x08010000u,0x670c3239u,0x2000001cu,0x53804a80u,0x66e83a3cu,0x00814247u,0x33c52000u,0x000033c7u,0x20000004u,0x48780024u,0x4e92588fu,0x4a80670au,0x2f3c0003u,0x40004e92u,0x588f47f9u,0x4000182au,0x4a80670au,0xb7fc4000u,0x18ba6600u,0x015c4201u,0x13c1fc0au,0x400c23c0u,0x400020f0u,0x41f94000u,0x20f023d0u,0x100f9028u,0x48780006u,0x4ebaf4d4u,0x588f2239u,0xfc07c00cu,0x2039fc07u,0xc00c9081u,0x0c800192u,0xd4ff63f0u,0x4280223cu,0x100f9000u,0x22402041u,0xd3fcfc0au,0x4000d1c0u,0x1151201cu,0x52800c80u,0x00000100u,0x66e62839u,0xfc07c00cu,0x42aeffd4u,0x42aeffd8u,0x2d44ffdcu,0x42aeffc4u,0x47faf4f8u,0x764145fau,0xf45449f9u,0x40002104u,0x1039fc06u,0x40040800u,0x00006708u,0x53834a83u,0x660000d0u,0x2239fc07u,0xc00c243cu,0x000203a0u,0x92844c42u,0x1001d3aeu,0xffc4262eu,0xffc423c3u,0x100f9080u,0x4ab94000u,0x20f8677au,0x420513c5u,0xfc0a400cu,0x30392000u,0x00080800u,0x00006766u,0x30392000u,0x00143439u,0x20000018u,0x36392000u,0x001c4840u,0x4240e18au,0x77830280u,0x00ff0000u,0x02820000u,0xff008082u,0x80832639u,0x100f9074u,0x0c830000u,0x00bf621au,0x2403e58au,0x43f94000u,0x20e42042u,0xd1fc100fu,0x90002140u,0x00842151u,0x03845283u,0x23c3100fu,0x907423c0u,0x100f9078u,0x81b9100fu,0x907c52b9u,0x100f9018u,0x4ab94000u,0x21146700u,0x016c4ab9u,0x40002110u,0x67000162u,0x48780001u,0x4ebaf4c2u,0x2f1b4e92u,0x588f6000u,0xfe901439u,0xfc06400cu,0x71822f00u,0x4e92588fu,0x20394000u,0x21004a80u,0x66182202u,0x7a200281u,0x000000e0u,0xba816600u,0x00b87e02u,0x23c74000u,0x20fc2200u,0x52811982u,0x080023c1u,0x40002100u,0xb2b94000u,0x20fc6d00u,0xfed81014u,0x742052b9u,0x100f900cu,0x22000281u,0x000000f0u,0xb4816600u,0x00ac44c0u,0x6b707c0fu,0x4281cc80u,0x41f94000u,0x21341a39u,0x40002105u,0x14306800u,0x2006e788u,0x7f85bb82u,0x75822242u,0x24090302u,0x67100307u,0x670c23c0u,0x400020e8u,0x23c0100fu,0x904c5281u,0x52807408u,0xb48166e0u,0x11856800u,0x7a017e01u,0x71b94000u,0x213952b9u,0x100f903cu,0xea88ca80u,0x71b94000u,0x213ae488u,0xce8023c5u,0x40002114u,0x23c74000u,0x211042b9u,0x40002100u,0x6000fe42u,0x22027a40u,0x02810000u,0x00f0ba81u,0x6700ff3cu,0x7e70be81u,0x670a42b9u,0x400020fcu,0x6000fe22u,0x720a23c1u,0x400020fcu,0x6000ff28u,0x7430b481u,0x66227a0fu,0x7e06c085u,0xbe8065bau,0x73394000u,0x210541f9u,0x40002118u,0xd3b00c00u,0x52b9100fu,0x904060a2u,0x71807240u,0xb280669au,0x71b94000u,0x210523c0u,0x400020ecu,0x52b9100fu,0x90446086u,0x20394000u,0x20e0b0aeu,0xffc46212u,0x06800000u,0x07d023c0u,0x400020e0u,0x42b94000u,0x215e242eu,0xffc40c82u,0x00009c40u,0x63084878u,0x00026000u,0xfe74263cu,0x000203a0u,0x4c031800u,0x2a2effc4u,0xd8810c85u,0x000007cfu,0x63122005u,0x0680ffffu,0xf8300c80u,0x00006d5fu,0x630001d8u,0x70094ab9u,0x400020f8u,0x671eb0b9u,0x400020e4u,0x67164201u,0x23c04000u,0x20e413c1u,0xfc0a400cu,0x2f004ebau,0xf4d8588fu,0x207c100fu,0x900c23d0u,0x100f9058u,0x207cfc07u,0xc00c2010u,0x90aeffd8u,0x0c800050u,0x91006300u,0x00ba2d50u,0xffd843f9u,0x400025e2u,0x428091c8u,0x42011381u,0x88005288u,0xb1fc0000u,0x008066f0u,0x5280d3c8u,0x7408b480u,0x66e4262eu,0xffc40c83u,0x0000012bu,0x63000170u,0x0c830000u,0x05776200u,0x017e2403u,0x0682ffffu,0xfed4c4fcu,0x007a2a3cu,0x000004b0u,0x4c452002u,0x45f94000u,0x1d224286u,0xb4866738u,0x26064285u,0x06830000u,0x00097e1fu,0xbe856d00u,0x01382012u,0xeaa80800u,0x0000670au,0x2f052f03u,0x4ebaf336u,0x508f5285u,0x7040b085u,0x66dc5286u,0x508a726eu,0xb28666c4u,0x202effc4u,0x243c0000u,0x03e8763fu,0x4c420000u,0x96802f03u,0x4878007fu,0x4ebaf306u,0x508f2a39u,0x400020f4u,0x2d45ffccu,0x4a856600u,0x034e4ab9u,0x4000215eu,0x662c43f9u,0x40002162u,0x428091c8u,0x7e017210u,0x13878800u,0x5288b288u,0x66f25280u,0x43e90010u,0x7408b480u,0x66e47601u,0x23c34000u,0x215e4282u,0x42857c04u,0x20022a45u,0xe98849f9u,0x40002162u,0xdbfc4000u,0x25e22445u,0x4283d5fcu,0x400021e2u,0xd9c07010u,0x80827180u,0x2d40ffe0u,0x2e03e687u,0x93c94a34u,0x780057c0u,0x2d47ffd0u,0x71004480u,0x4a806600u,0x028a4a86u,0x6f0002ccu,0x2f2effe0u,0x4e935386u,0x20037180u,0x2f004e93u,0x508f91c8u,0x1e358800u,0x71872f00u,0x2d48ffc8u,0x4e93588fu,0x206effc8u,0x72081587u,0x88005288u,0xb28866e0u,0x4207206eu,0xffd01987u,0x88006000u,0x026e2e3cu,0x000007d0u,0x4c470000u,0x41f94000u,0x181c71b0u,0x08006000u,0xfe164282u,0x6000feaeu,0x22050681u,0xffffffe0u,0x202a0004u,0xe2a86000u,0xfebe242eu,0xffc40c82u,0x00000578u,0x6700fed2u,0x24394000u,0x20f84a82u,0x6700014au,0x7608b6b9u,0x400020e4u,0x67000148u,0x41f94000u,0x17d34280u,0x4a300800u,0x66000142u,0xe5882f08u,0x7a404878u,0x003445fau,0xf1fe5380u,0x20006a02u,0x5280e280u,0x9a802f05u,0x4e924fefu,0x000c4a82u,0x674a7e08u,0x24394000u,0x20e4be82u,0x673e0802u,0x00046700u,0x010e203cu,0x400017f3u,0x2f004878u,0x00084878u,0x001e4e92u,0x4fef000cu,0x700f7209u,0xc082b280u,0x660000f6u,0x48794000u,0x18014878u,0x00184878u,0x00384e92u,0x4fef000cu,0x48794000u,0x1805263cu,0x4b455920u,0x48780002u,0x48780031u,0x4e922d43u,0xffe84fefu,0x000c49fau,0xef3e2039u,0x400020e8u,0x4a806d00u,0x01262f00u,0x7406486eu,0xffec4e94u,0x508f4beeu,0xffe8723au,0x70587e20u,0x1d8128eau,0x22394000u,0x20ec1d80u,0x28e92002u,0x56801d87u,0x28e84a81u,0x6d000100u,0x2f014875u,0x08004e94u,0x5a82508fu,0x42051d85u,0x28e8240eu,0x0682ffffu,0xffe42f0du,0x4878003au,0x48780002u,0x4e922e39u,0x100f9074u,0x7f872f07u,0x2f024e94u,0x48794000u,0x180e4878u,0x003a4878u,0x00624e92u,0x4fef001cu,0x2e824878u,0x003a4878u,0x00724e92u,0x4fef000cu,0x6000fd7eu,0x41f94000u,0x17bb6000u,0xfec241f9u,0x400017a9u,0x6000feb8u,0x52806000u,0xfeb4203cu,0x400017fau,0x6000fef2u,0x76079bcdu,0xc48341f9u,0x40002092u,0x760e7ff0u,0x2a342c03u,0x42820d07u,0x674041edu,0x00147a04u,0x2d48ffd0u,0x204241e8u,0x0038202eu,0xffd09085u,0x2d40ffccu,0x99cc2f2eu,0xffcc4870u,0xc8002d48u,0xffc84ebau,0xf034528cu,0x508f206eu,0xffc87004u,0xb08c66e2u,0x53854a85u,0x66d05386u,0x5882720cu,0xb28266b2u,0x5783588du,0x74ffb483u,0x66a46000u,0xfeb07a2du,0x74051d45u,0xffec6000u,0xfede762du,0x58821d83u,0x08e86000u,0xff041035u,0x98001e32u,0x98005289u,0x72083040u,0x2008bf80u,0x4a0057c0u,0x71004480u,0xb2896600u,0xfd547187u,0x3e087387u,0xb2806600u,0xfd4e5083u,0x508d508au,0x0c830000u,0x00806600u,0xfd245282u,0xda837008u,0xb0826600u,0xfcf0207cu,0xfc07c00cu,0x201090aeu,0xffdc0c80u,0x00b54640u,0x6312202eu,0xffd4720fu,0x5280c280u,0x2d50ffdcu,0x2d41ffd4u,0x42807e01u,0x43f94000u,0x213441f9u,0x4000214du,0xb0aeffd4u,0x674c7407u,0xc4802200u,0xe68173b1u,0x1800e4a1u,0x7401c282u,0x2a007607u,0xda852c05u,0xe686ca83u,0x26071430u,0x6800ebabu,0x4a816726u,0x84831182u,0x68005280u,0x7a10ba80u,0x66c24aaeu,0xffcc6600u,0xf89042a7u,0x4ebaedd6u,0x588f6000u,0xf8847201u,0x60c24683u,0xc48360d6u,0x30313233u,0x34353637u,0x38394142u,0x43444546u,0x00415544u,0x494f2054u,0x4553543au,0x20515549u,0x45540041u,0x5544494fu,0x3a204453u,0x50204449u,0x44204e4fu,0x54204c4fu,0x41440041u,0x5544494fu,0x20544553u,0x543a2054u,0x4f4e4520u,0x4f4e2041u,0x4c4c204fu,0x55545055u,0x54530045u,0x53414920u,0x31004553u,0x41492030u,0x00414c4cu,0x00414552u,0x4941204fu,0x53004453u,0x50007b83u,0x787c7d7eu,0x7f808182u,0x09090909u,0x09090909u,0x09090909u,0x09090005u,0xf4390000u,0x03000009u,0xf4a40000u,0x3fff000au,0xbd6e000au,0xbe6e0020u,0x00130056u,0x3f00000au,0x83a30003u,0x400c000au,0x3f800003u,0x4008000au,0x8406000au,0x8407000au,0x8426000au,0xbd4e000au,0xbe4e0045u,0xf4000000u,0x0001000au,0x83800003u,0x40130008u,0x44060020u,0x8e00000au,0x83800003u,0x40170008u,0x50060022u,0x1100000au,0x83800003u,0x401b0008u,0x58860020u,0x0064000du,0x104a0000u,0x0004000au,0x3f800003u,0x401b000au,0xe1800005u,0xf4390000u,0x03000009u,0xf4a40000u,0x3fff000au,0xbd6e000au,0xbe6e0007u,0xb4940000u,0x00400044u,0xf4000012u,0x34560004u,0xc4200007u,0xb49600f0u,0x0f000007u,0xb4950003u,0x79010007u,0xb4980090u,0x0f000007u,0xb4970001u,0x79010007u,0xb49b0000u,0x00ff0007u,0xb49c0000u,0x00000007u,0xb4990000u,0x00ff0007u,0xb49a0000u,0x00000007u,0xb49f00ffu,0xffff0007u,0xb49e00ffu,0xffff0007u,0xf41800aau,0x00000007u,0xf4340000u,0x00400020u,0x00130004u,0xcec00007u,0xf43600f4u,0x0f000007u,0xf4350003u,0x7d010007u,0xf43800f4u,0x0f000007u,0xf4370001u,0x7d010007u,0xf43b0000u,0x000f0007u,0xf43c0000u,0x00000007u,0xf4390000u,0x00ff0007u,0xf43a0000u,0x00000007u,0xf43f00ffu,0xffff0007u,0xf43e00ffu,0xffff0020u,0x00130056u,0x3f000001u,0xb38d0003u,0x303b0044u,0xf4000000u,0x00070020u,0x96000001u,0xb38f0003u,0x30400004u,0xcec00022u,0xcf000045u,0xf4000000u,0x00010020u,0x006c0021u,0xb600000du,0x10420000u,0x0004000du,0x10c00000u,0x0004000au,0x3f800003u,0x30400020u,0x00130056u,0x33000056u,0x3f000056u,0x31000056u,0x32000056u,0x34000056u,0x36000056u,0x37000056u,0x38000056u,0x39000056u,0x3a000056u,0x3b000056u,0x3c000044u,0xf4000000u,0x00090044u,0x30000044u,0xf4000018u,0x00000044u,0x35000065u,0xf4000000u,0x00000047u,0xf4000001u,0x5888000au,0x83a30003u,0x30fa000au,0x83800003u,0x306b0008u,0x44060044u,0x30000056u,0xb0000045u,0xf4000000u,0x00100020u,0x0065000du,0x10490000u,0x00210001u,0x93cf0003u,0x30900056u,0xb3000045u,0xb0000046u,0xf4000000u,0x00100020u,0x00550020u,0x001b0020u,0xae000046u,0xf4000000u,0x00190020u,0x0055000du,0x104a0000u,0x00090056u,0xb3000046u,0xf4000000u,0x00100020u,0x00500020u,0x0065000du,0x10420000u,0x00030057u,0xb5000004u,0xcf200056u,0xb3000046u,0xf4000000u,0x00010020u,0x00500046u,0xf4000000u,0x00070020u,0x00560054u,0x33000001u,0xb3880003u,0x30ac0001u,0xb3860003u,0x309b0020u,0x00130056u,0x32000056u,0xb7000045u,0xf4000000u,0x00010020u,0x00600054u,0x37000004u,0x4ec80020u,0x00260062u,0xb2000072u,0xf4000000u,0x00380000u,0x00000045u,0xea000020u,0x0065000du,0x104f0000u,0x00030054u,0x6a000020u,0x5a000022u,0x4e000045u,0xf4000000u,0x00030020u,0x00660054u,0x32000001u,0xb38f0003u,0x30650001u,0xb38d0003u,0x30b50056u,0xb6000045u,0xf4000000u,0x00010020u,0x00600054u,0x36000056u,0xb4000045u,0xf4000000u,0x00010020u,0x00600045u,0xf4000000u,0x01900020u,0x0065000du,0x10490000u,0x00060020u,0x00130057u,0xb5000020u,0x003e0055u,0x35000054u,0x34000057u,0xb0000045u,0xf4000000u,0x00090020u,0x006d000du,0x104a0000u,0x000a0056u,0xb1000045u,0xb0000020u,0x0065000du,0x104a0000u,0x00060020u,0x001b000du,0x10c00000u,0x00040056u,0xb1000057u,0xb5000004u,0xcfc00045u,0xf4000000u,0x00010020u,0x00600045u,0xf4000000u,0x00070020u,0x00660054u,0x31000020u,0x5d000022u,0xae000020u,0x0075000du,0x10490000u,0x001a0065u,0xf4000000u,0x0000000au,0x83810003u,0x30f80062u,0xbc000072u,0xf4000000u,0x00380000u,0x00000056u,0xea00000cu,0x1ec40021u,0x8f000022u,0x4e00000cu,0x1eac0021u,0x84000020u,0x004a0008u,0xcd070020u,0x00130056u,0x6a000020u,0x5a000022u,0x4e000045u,0xf4000000u,0x00030020u,0x00660054u,0x3c00000au,0x3f800003u,0x30650007u,0xf4350000u,0x00000007u,0xf4370000u,0x00000007u,0xb4950000u,0x00000007u,0xb4970000u,0x0000000au,0x8406000au,0x8407000au,0x8426000au,0xbd4e000au,0xbe4e0045u,0xf4000000u,0x0001000au,0x83800003u,0x31090008u,0x44060020u,0x8e00000au,0x83800003u,0x310d0008u,0x50060022u,0x1100000au,0x83800003u,0x31110008u,0x58860020u,0x0064000du,0x104a0000u,0x0004000au,0x3f800003u,0x3111000au,0xe1807c00u,0x00000000u,0x00007e00u,0x00000000u,0x00007f00u,0x00000000u,0x00007f80u,0x00000000u,0x00007f80u,0x00000000u,0x00003f80u,0x00000000u,0x00003f80u,0x00000000u,0x00007f80u,0x00000000u,0x00007f00u,0x00000000u,0x00007f00u,0x00000000u,0x0000fe00u,0x00000000u,0x0000fe00u,0x00000000u,0x0000fc00u,0x00000000u,0x0000fc00u,0x00000000u,0x0000fe00u,0x00000000u,0x00007e00u,0x00000000u,0x00007f00u,0x00000000u,0x00007f80u,0x00000000u,0x00003fc0u,0x00000000u,0x00003fe0u,0x00000000u,0x0000fff8u,0x00000000u,0x0000fffcu,0x00000000u,0x0001fffeu,0x00000000u,0x0003fffeu,0x00000000u,0x0003ffffu,0x00000000u,0x0003fc3fu,0x80000000u,0x0007fc1fu,0x80000000u,0x0007f80fu,0xc0000000u,0x0003f807u,0xc0000000u,0x00e3f807u,0xe0000000u,0x07fffc07u,0xe0000000u,0x0fffff07u,0xf0000000u,0x1fffffffu,0xf0000000u,0x1fffffffu,0xf0000000u,0x0fffffffu,0xf0000000u,0x0fe07fffu,0xf0000000u,0x0fe0ffffu,0xf0000000u,0x07f8ffffu,0xf8000000u,0x03ffffffu,0xfc000000u,0x01ffffffu,0xfe000000u,0x007fffffu,0xff000000u,0x001fffffu,0xff800000u,0x0007fbffu,0xff800000u,0x000ffbffu,0xffc00000u,0x000ff3ffu,0xffe00000u,0x000ff3ffu,0xf1c00000u,0x000ff3ffu,0xf0000000u,0x000ff1ffu,0xf0000000u,0x000ff1ffu,0xf0000000u,0x000ff0ffu,0xf0000000u,0x000ff0ffu,0xff000000u,0x000ff03fu,0xff800000u,0x000ff80fu,0xffe00000u,0x0007fc3fu,0xfff00000u,0x0007ffffu,0xfff00000u,0x0003ffffu,0xffc00000u,0x0003ffffu,0xff000000u,0x00017fffu,0xff000000u,0x00001ffcu,0xff800000u,0x00000000u,0x7f800000u,0x00000000u,0x3f800000u,0x00000000u,0x7f800000u,0x00003ff0u,0x7f800000u,0x01f87fffu,0xff800000u,0x01fe7fffu,0xff800000u,0x01ff7fffu,0xff000000u,0x01ff3fffu,0xff000000u,0x00fe1fffu,0xff000000u,0x00003fffu,0xff800000u,0x00003fffu,0xffc00000u,0x00003fffu,0xfff00000u,0x00003fffu,0xcff00000u,0x00003fffu,0xc3f80000u,0x00003fdfu,0xc1f00000u,0x00007fffu,0xe0800000u,0x0000ffffu,0xe0000000u,0x0007ffffu,0xe0000000u,0x001fffffu,0xe0000000u,0x003fffffu,0xe0000000u,0x007fffffu,0xc0000000u,0x007fc3ffu,0xf0000000u,0x007f807fu,0xf8000000u,0x007f801fu,0xfe000000u,0x007f8007u,0xff000000u,0x007f8003u,0xff000000u,0x007f8000u,0x7f800000u,0x003fe000u,0x00000000u,0x003ff000u,0x00000000u,0x001ffc00u,0x00000000u,0x000ffe00u,0x00000000u,0x0007ff80u,0x00000000u,0x00017fc0u,0x00000000u,0x00003fe0u,0x00000000u,0x00000ff0u,0x00000000u,0x000007f8u,0x00000000u,0x000003fcu,0x00000000u,0x000003fcu,0x00000000u,0x000001fcu,0x00000000u,0x000001feu,0x00000000u,0x000001feu,0x00000000u,0x000003ffu,0x00000000u,0x000007ffu,0x00000000u,0x000007feu,0x00000000u,0x000007fcu,0x00000000u,0x000007f8u,0x00000000u,0x000007f8u,0x00000000u,0x000007f8u,0x00000000u,0x000003f8u,0x00000000u,0x000001fcu,0x00000000u,0x00000038u,0x00000000u,0x00002bedu,0x6bae3923u,0x6b6e79a7u,0x79a4396bu,0x5bed7497u,0x126a5badu,0x49275fedu,0x6b6d2b6au,0x6ba42b73u,0x6bad388eu,0x74925b6fu,0x5b6a5bfdu,0x5aad5a92u,0x72a77b6fu,0x2c9762a7u,0x628e5bc9u,0x798e39efu,0x72927befu,0x7bce05d0u,0x01c00410u,0x000007d0u,0x00000063u,0xffffffffu,0xffffffffu};
#define ST_REG8(a)  (*(volatile uint8_t  *)(a))
#define ST_REG16(a) (*(volatile uint16_t *)(a))
#define ST_REG32(a) (*(volatile uint32_t *)(a))
#define ST_NOW() ST_REG32(0xFC07C00Cu)
#define ST_MS(n) ((uint32_t)(n) * 132000u)
static void st_wait(uint32_t t) { uint32_t t0 = ST_NOW(); while (ST_NOW() - t0 < t) {} }
static int st_put(uint32_t w)
{
    uint32_t t0 = ST_NOW();
    while (!(ST_REG16(0x20000008u) & 0x06)) if (ST_NOW() - t0 > ST_MS(20)) return 0;
    ST_REG16(0x20000014u) = (uint16_t)((w >> 16) & 0xFF);
    ST_REG16(0x20000018u) = (uint16_t)((w >> 8) & 0xFF);
    ST_REG16(0x2000001Cu) = (uint16_t)(w & 0xFF);
    return 1;
}
static int st_park(int core)
{
    ST_REG8(0xFC0A400Cu) = (uint8_t)core;
    ST_REG16(0x20000004u) = 0x008F;
    uint32_t t0 = ST_NOW();
    while (ST_REG16(0x20000004u) & 0x80) if (ST_NOW() - t0 > ST_MS(50)) return 0;
    st_wait(ST_MS(2));
    for (int i = 0; i < 16 && (ST_REG16(0x20000008u) & 1); i++) { (void)ST_REG16(0x2000001Cu); st_wait(ST_MS(1)); }
    return 1;
}
static int st_upload(int core, const uint32_t *p, unsigned n, uint32_t org)
{
    ST_REG8(0xFC0A400Cu) = (uint8_t)core;
    for (int i = 0; i < 16 && (ST_REG16(0x20000008u) & 1); i++) (void)ST_REG16(0x2000001Cu);
    ST_REG16(0x20000000u) = 0x81;
    ST_REG16(0x20000004u) = 0;
    if (!st_put(n) || !st_put(org)) return 0;
    for (unsigned i = 0; i < n; i++) if (!st_put(p[i])) return 0;
    return 1;
}
static void st_tx(uint8_t b)
{
    uint32_t t0 = ST_NOW();
    while (!(ST_REG8(0xFC064004u) & 0x04)) if (ST_NOW() - t0 > ST_MS(5)) return;
    ST_REG8(0xFC06400Cu) = b;
}
static void st_snap(volatile uint8_t **o, uint32_t base, unsigned n, unsigned skip_from, unsigned skip_to)
{
    for (unsigned i = 0; i < n; i++) *(*o)++ = (i >= skip_from && i < skip_to) ? 0xEE : ST_REG8(base + i);
}
__attribute__((unused)) static void warm_launch(void)
{
    __asm__ volatile ("move.w #0x2700,%%sr" ::: "memory");
    ST_REG8(0xFC044019u) = 0x40;
    st_wait(ST_MS(5));
    st_park(0); st_park(1);
    ST_REG8(0xFC0A400Cu) = 0;
    ST_REG32(0x100FB7F0u) = 0x5741524Du;   /* 'WARM': AERIA OS skips the panel */
    volatile uint32_t *dst = (volatile uint32_t *)0x40000400u;
    for (unsigned i = 0; i < sizeof aos_img / 4; i++) dst[i] = aos_img[i];
    /* push and invalidate every line of both caches (512 sets x 4 ways covers the V4e's), then fetch fresh */
    for (uint32_t set = 0; set < 512; set++)
        for (uint32_t way = 0; way < 4; way++) {
            uint32_t a = set << 4 | way;
            __asm__ volatile ("move.l %0,%%a0\n\tcpushl %%bc,(%%a0)" :: "d"(a) : "a0", "memory");
        }
    __asm__ volatile ("nop\n\tjmp 0x40000400" ::: "memory");
    for (;;) {}
}
/* v45: AERIA OS's own start-up steps (aeria-os/src/main.c), run inside stock before the tone test */
static void aos_steps(void)
{
    /* timer_init */
    ST_REG16(0xFC07C000u) = 0; ST_REG32(0xFC07C004u) = 0xFFFFFFFFu; ST_REG32(0xFC07C00Cu) = 0; ST_REG8(0xFC07C003u) = 3; ST_REG16(0xFC07C000u) = 0x000B;
    /* panel_lines_and_uart */
    static const uint8_t zero_at[] = {0x7B, 0x83, 0x78, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82};
    ST_REG8(0xFC0A4075u) = 1; ST_REG8(0xFC0A4079u) = 0x10;
    for (unsigned i = 0; i < sizeof zero_at; i++) ST_REG8(0xFC0A4000u + zero_at[i]) = 0;
    ST_REG8(0xFC0A4022u) = ST_REG8(0xFC0A4022u) | 0x20;
    ST_REG8(0xFC0A4022u) = ST_REG8(0xFC0A4022u) | 0x40;
    ST_REG8(0xFC0A4052u) = 0xBF; st_wait(132u * 10u);
    ST_REG8(0xFC0A4022u) = ST_REG8(0xFC0A4022u) & (uint8_t)~0x40;
    for (int i = 0; i < 10; i++) { ST_REG8(0xFC0A403Au) = 0x20; st_wait(264u); if (!(ST_REG8(0xFC0A403Au) & 0x40)) continue; ST_REG8(0xFC0A4052u) = 0xDF; st_wait(264u); if (!(ST_REG8(0xFC0A403Au) & 0x40)) break; }
    ST_REG8(0xFC0A4069u) = 0x33; ST_REG8(0xFC0A4066u) = 0x43;
    ST_REG8(0xFC064008u) = 0x20; ST_REG8(0xFC064008u) = 0x30; ST_REG8(0xFC064008u) = 0x40; ST_REG8(0xFC064008u) = 0x10;
    ST_REG8(0xFC064004u) = 0xDD; ST_REG8(0xFC064000u) = 0x13; ST_REG8(0xFC064000u) = 0x07; ST_REG8(0xFC064014u) = 0;
    ST_REG8(0xFC064018u) = 0; ST_REG8(0xFC06401Cu) = 13; ST_REG8(0xFC064008u) = 0x05;
    /* the panel exchange, the LED levels, the backlight */
    st_wait(ST_MS(100));
    st_tx(0x60); st_tx(0x02); st_tx(0x70); st_tx(0x00); st_wait(ST_MS(300)); st_tx(0x60); st_tx(0x00); st_wait(ST_MS(500));
    st_tx(0x74); st_tx(0x00); st_wait(ST_MS(300)); st_tx(0x60); st_tx(0x00); st_wait(ST_MS(50));
    for (int id = 0; id < 0x58; id++) { st_tx(0x3F); st_tx((uint8_t)id); }
    for (int r = 0; r < 11; r++) { st_tx((uint8_t)(0x20 | r)); st_tx(0); }
    st_tx(0xB7); st_tx(0xFF);
    while (ST_REG8(0xFC064004u) & 1) (void)ST_REG8(0xFC06400Cu);
    /* the pins AERIA OS sets before its DSP boot */
    ST_REG8(0xFC0A4060u) = 0x11; ST_REG16(0xFC0A406Au) = 0x0A00; ST_REG16(0xFC0A406Eu) = 0x07E7; ST_REG16(0xFC0A4072u) = 0x8080;
    ST_REG32(0xFC008020u) = 0x00000180u;
    ST_REG8(0xFC0A4009u) = ST_REG8(0xFC0A4009u) & (uint8_t)~0x04; ST_REG8(0xFC0A4021u) = ST_REG8(0xFC0A4021u) | 0x04;
}
__attribute__((unused)) static void stock_audio_test(void)
{
    volatile uint32_t *L = (volatile uint32_t *)0x100FB800u;
    __asm__ volatile ("move.w #0x2700,%%sr" ::: "memory");
    for (int i = 0; i < 512; i++) L[i] = 0;
    L[0] = 0x53544B54u;
    /* the snapshot, bytes: CCM 64, PLL 16, FlexBus 96, SSI 0x10-0x4f, DTIM0-3 16 each, PIT0-3 8 each, EPORT 16,
     * INTC0 64, INTC1 64, eDMA 64, DSPI 0x00-0x37, SCM 64 -- 772 bytes from L+4 */
    volatile uint8_t *o = (volatile uint8_t *)(L + 4);
    st_snap(&o, 0xFC0A0000u, 64, 0, 0);
    st_snap(&o, 0xFC0C4000u, 16, 0, 0);
    st_snap(&o, 0xFC008000u, 96, 0, 0);
    st_snap(&o, 0xFC0BC010u, 64, 0, 0);
    st_snap(&o, 0xFC070000u, 16, 0, 0); st_snap(&o, 0xFC074000u, 16, 0, 0); st_snap(&o, 0xFC078000u, 16, 0, 0); st_snap(&o, 0xFC07C000u, 16, 0, 0);
    st_snap(&o, 0xFC080000u, 8, 0, 0); st_snap(&o, 0xFC084000u, 8, 0, 0); st_snap(&o, 0xFC088000u, 8, 0, 0); st_snap(&o, 0xFC08C000u, 8, 0, 0);
    st_snap(&o, 0xFC094000u, 16, 0, 0);
    st_snap(&o, 0xFC048000u, 64, 0, 0); st_snap(&o, 0xFC04C000u, 64, 0, 0);
    st_snap(&o, 0xFC044000u, 64, 0, 0);
    st_snap(&o, 0xFC05C000u, 56, 0, 0);
    st_snap(&o, 0xFC000000u, 64, 0, 0);
    { volatile uint8_t *x = (volatile uint8_t *)0x100FA400u;   /* v38: GPIO 256, SCM2 128, XBS 256, SDRAMC 64, USB 512 */
      st_snap(&x, 0xFC0A4000u, 256, 0, 0); st_snap(&x, 0xFC040000u, 128, 0, 0); st_snap(&x, 0xFC004000u, 256, 0, 0);
      st_snap(&x, 0xFC0B8000u, 64, 0, 0); st_snap(&x, 0xFC0B0000u, 512, 0, 0); }
    L[1] = 1;
    /* the ColdFire's own DMA off the host port, then both cores parked and AERIA OS's programs in */
    st_wait(ST_MS(5));
    ST_REG8(0xFC044019u) = 0x40;            /* CERQ: clear every channel's request enable (CAER) */
    aos_steps();
    /* v46: the USB controller back to its reset state (USBCMD RST), as after a restart */
    ST_REG32(0xFC0B0140u) = 0x00000002u; st_wait(ST_MS(50));
    L[2] = (uint32_t)st_park(0) | (uint32_t)st_park(1) << 1;
    int ok0 = st_upload(0, dsp_st0, sizeof dsp_st0 / 4, 0x33000u);
    int ok1 = st_upload(1, dsp_st1, sizeof dsp_st1 / 4, 0x34000u);
    L[3] = (uint32_t)ok0 | (uint32_t)ok1 << 1 | 0x100u;
    ST_REG8(0xFC0A400Cu) = 0;
    /* 4 s silence, 8 s the tone on every slot, 4 s silence; peaks 4 a second */
    volatile uint32_t *beat = L + 256, *ph = L + 384;
    uint32_t n = 0, t0 = ST_NOW(), ms = 0, prev = t0, cmd = 99;
    while (ms < 16000) {
        uint32_t d = ST_NOW() - prev, whole = d / 132000u; ms += whole; prev += whole * 132000u;
        uint32_t want = ms < 4000 ? 8 : ms < 12000 ? 9 : 8;
        if (want != cmd && ok0) { cmd = want; st_put(cmd); }
        if (ST_REG16(0x20000008u) & 1) {
            uint32_t w = (uint32_t)(ST_REG16(0x20000014u) & 0xFF) << 16 | (uint32_t)(ST_REG16(0x20000018u) & 0xFF) << 8 | (ST_REG16(0x2000001Cu) & 0xFF);
            if (n < 128) { beat[n] = w; ph[n] = cmd; }
            n++; L[2] = (L[2] & 0xFFu) | n << 8;
        }
    }
    /* hand back: HF0 to both cores (each becomes a boot ROM again), drain, soft reset into the flashed OS */
    ST_REG8(0xFC0A400Cu) = 1; ST_REG16(0x20000000u) = 0x08; st_wait(ST_MS(5));
    for (int i = 0; i < 8 && (ST_REG16(0x20000008u) & 1); i++) (void)ST_REG16(0x2000001Cu);
    ST_REG8(0xFC0A400Cu) = 0; ST_REG16(0x20000000u) = 0x08; st_wait(ST_MS(10));
    for (int i = 0; i < 8 && (ST_REG16(0x20000008u) & 1); i++) (void)ST_REG16(0x2000001Cu);
    L[3] |= 0x200u;
    ST_REG8(0xFC0A0000u) = 0x80;
    for (;;) {}
}
#else
__attribute__((unused)) static void stock_audio_test(void) {}
__attribute__((unused)) static void warm_launch(void) {}
#endif
/* ---- OUT: where each effect sounds, and the quad panner (AERIA OS, sadnoise, 10 Nov 2026) ----------------------
 * Every chain slot has an OUT setting:
 *   CHAIN  the effect sits in the chain as always (the default)
 *   MAIN   it takes the chain's sound, plays to the MAIN outs only, and the chain goes on without it (a branch)
 *   CUE    the same, to the CUE outs ("grains to cue, everything else to main")
 *   QUAD   the same, placed between all four outputs by its own panner: X left..right, Y main..cue
 * Each panner moves by its MODE: FIX (stays), DRIFT (wanders), ORBIT (circles), FIGURE 8, PING (left-right),
 * DIVE (main-cue), JUMP (leaps), at SPD over DPTH around X/Y. GRAINS can SCATTER instead: every burst of grains
 * lands on a random output. MIDI: each slot's panner takes two CCs -- X on CC 20 + 2 x slot, Y on 21 + 2 x slot
 * (slots 1..7: CC 20/21 .. 32/33) -- on the channel CHAN (OMNI: any).
 * The OUT page: CUE opens it. Trigs 1-7 pick the slot; knobs ROUT X Y MODE SPD DPTH; FUNC: SCAT CHAN. */
#if defined(AERIAOS)
static int layer(void);
static uint8_t led_want[16];   /* ui12.c's (a tentative definition: the same object) */
static int midi_mod;           /* the MIDI page's module: 0 gumballs, 1 pitch, 2 marbles, 3 pong (ui12.c) */
static int midi_mod_get(void) { return midi_mod; }
static void midi_mod_set(int m);
void aos_route(int s, int on, const uint32_t g[4]);
void aos_cue_chain(int on);
enum { RT_CHAIN, RT_MAIN, RT_CUE, RT_QUAD, N_ROUTES };
enum { PN_FIX, PN_DRIFT, PN_ORBIT, PN_FIG8, PN_PING, PN_DIVE, PN_JUMP, N_PMODES };
static const char *const route_name[N_ROUTES] = {"CHAIN", "MAIN", "CUE", "QUAD"};
static const char *const pmode_name[N_PMODES] = {"FIX", "DRIFT", "ORBIT", "FIG 8", "PING", "DIVE", "JUMP"};
typedef struct { uint8_t route, mode, scat, x, y, spd, dpth; } PanSet;
static PanSet pan_set[N_SLOTS] = {{0, 0, 0, 64, 0, 40, 60}, {0, 0, 0, 64, 0, 40, 60}, {0, 0, 0, 64, 0, 40, 60}, {0, 0, 0, 64, 0, 40, 60},
                                  {0, 0, 0, 64, 0, 40, 60}, {0, 0, 0, 64, 0, 40, 60}, {0, 0, 0, 64, 0, 40, 60}};
static uint8_t pan_chan;               /* 0 omni, 1..16 */
static int out_open;                   /* the OUT page is showing */
static int pan_px[N_SLOTS], pan_py[N_SLOTS];        /* where each panner is now, 0..127 */
static int pan_dx[N_SLOTS], pan_dy[N_SLOTS];        /* DRIFT's wander, Q4 */
static uint16_t pan_ph[N_SLOTS];                    /* ORBIT/FIG8/PING/DIVE phase, 0..1023 a turn */
static uint16_t pan_hold[N_SLOTS];                  /* JUMP/SCATTER countdown, ticks */
static int pan_jx[N_SLOTS], pan_jy[N_SLOTS];        /* JUMP/SCATTER target */

/* equal power between main and cue (Y), balance between left and right (X): unity in the middle */
static uint32_t q23(int num, int den) { return num <= 0 ? 0 : num >= den ? 0x7FFFFFu : (uint32_t)(((uint32_t)num << 15) / (uint32_t)den) << 8; }
static void pan_gains(int x, int y, uint32_t g[4])
{
    int a = y * 256 / 127;                                   /* 0..256: a quarter turn */
    int fr = sine[((a >> 4) + 16) & 63], bk = sine[(a >> 4) & 63];   /* cos, sin: 0..127 */
    int gl = x <= 64 ? 127 : (127 - x) * 127 / 63, gr = x >= 64 ? 127 : x * 127 / 64;
    g[0] = q23(gl * fr, 127 * 127); g[1] = q23(gr * fr, 127 * 127);
    g[2] = q23(gl * bk, 127 * 127); g[3] = q23(gr * bk, 127 * 127);
}
static int slot_is_grains(int s) { return slots[s].row > 0 && slots[s].name[0] && slots[s].name[6] == 'G'; }   /* "AERIA GRAINS" */
static void pan_tick(void)
{
    int any_back = 0;
    for (int s = 0; s < N_SLOTS; s++) {
        PanSet *p = &pan_set[s];
        uint32_t g[4] = {0x7FFFFFu, 0x7FFFFFu, 0, 0};
        if (p->route == RT_CHAIN || slots[s].row <= 0) { aos_route(s, 0, g); pan_px[s] = p->x; pan_py[s] = p->y; continue; }
        int bx = p->x, by = p->route == RT_MAIN ? 0 : p->route == RT_CUE ? 127 : p->y, d = p->dpth, sp = 1 + p->spd / 4;
        int x = bx, y = by;
        if (p->scat && slot_is_grains(s)) {
            /* a burst at the grains' density (their DENS knob: 1 to 4 grains a window) lands on a random output */
            int dens = slot_value(s, 1);
            if (!pan_hold[s]--) { pan_hold[s] = (uint16_t)(4 + (127 - dens) / 6); pan_jx[s] = (rnd() & 1) ? 127 : 0; pan_jy[s] = p->route == RT_QUAD ? ((rnd() & 1) ? 127 : 0) : by; }
            x = pan_jx[s]; y = pan_jy[s];
        } else switch (p->mode) {
        case PN_DRIFT:
            pan_dx[s] = clamp(pan_dx[s] + range(-sp, sp), -32 * 16, 32 * 16); pan_dy[s] = clamp(pan_dy[s] + range(-sp, sp), -32 * 16, 32 * 16);
            pan_dx[s] -= pan_dx[s] / 64; pan_dy[s] -= pan_dy[s] / 64;
            x = bx + pan_dx[s] * d / (32 * 16); y = by + pan_dy[s] * d / (32 * 16); break;
        case PN_ORBIT: pan_ph[s] += (uint16_t)sp; x = bx + cos10(pan_ph[s]) * d / 127; y = by + sin10(pan_ph[s]) * d / 127; break;
        case PN_FIG8: pan_ph[s] += (uint16_t)sp; x = bx + sin10(pan_ph[s]) * d / 127; y = by + sin10(2 * pan_ph[s]) * d / 254; break;
        case PN_PING: pan_ph[s] += (uint16_t)sp; x = bx + sin10(pan_ph[s]) * d / 127; break;
        case PN_DIVE: pan_ph[s] += (uint16_t)sp; y = by + sin10(pan_ph[s]) * d / 127; break;
        case PN_JUMP:
            if (!pan_hold[s]--) { pan_hold[s] = (uint16_t)(2 + (127 - p->spd)); pan_jx[s] = bx + range(-d, d); pan_jy[s] = by + range(-d, d); }
            x = pan_jx[s]; y = pan_jy[s]; break;
        default: break;
        }
        if (p->route != RT_QUAD) y = by;
        x = clamp(x, 0, 127); y = clamp(y, 0, 127);
        pan_px[s] = x; pan_py[s] = y;
        pan_gains(x, y, g);
        aos_route(s, 1, g);
        if (y > 0) any_back = 1;
    }
    aos_cue_chain(!any_back);
}

/* MIDI in: the panners' CCs */
static void out_midi(unsigned st, unsigned a, unsigned b)
{
    if ((st & 0xF0) != 0xB0 || (pan_chan && (st & 15) != (unsigned)(pan_chan - 1))) return;
    if (a < 20 || a >= 20 + 2 * N_SLOTS) return;
    int s = (int)(a - 20) / 2;
    if ((a - 20) & 1) pan_set[s].y = (uint8_t)b; else pan_set[s].x = (uint8_t)b;
}

/* the page */
static void out_clear(int y0, int y1) { for (int x = 0; x < W; x++) span(x, y0, y1, 0); }
static void out_draw(void)
{
    out_clear(9, 63);
    /* the field: a square, MAIN along the top, CUE along the bottom */
    int fx = 3, fy = 12, fs = 40;
    for (int i = 0; i <= fs; i++) { if (i % 2 == 0) { px(fx + i, fy); px(fx + i, fy + fs); px(fx, fy + i); px(fx + fs, fy + i); } }
    for (int s = 0; s < N_SLOTS; s++) {
        if (pan_set[s].route == RT_CHAIN || slots[s].row <= 0) continue;
        int cx = fx + pan_px[s] * fs / 127, cy = fy + pan_py[s] * fs / 127;
        int r = s == sel_slot ? 3 : 2;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) px(cx + dx, cy + dy);
        if (s == sel_slot) for (int k = -5; k <= 5; k += 2) { px(cx + k, cy - 5); px(cx + k, cy + 5); }
        char n[3]; n[0] = (char)('1' + s); n[1] = 0;
        text(cx + 4, cy - 2, n, 1);
    }
    text(fx + 2, fy + 2, "MAIN", 1);
    text(fx + 2, fy + fs - 6, "CUE", 1);
    /* the slot */
    PanSet *p = &pan_set[sel_slot];
    char t[24]; int k = 0;
    t[k++] = (char)('1' + sel_slot); t[k++] = ' ';
    const char *nm = slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY";
    if (nm[0] == 'A' && nm[5] == ' ') nm += 6;   /* "AERIA " */
    for (int i = 0; nm[i] && k < 16; i++) t[k++] = nm[i] >= 'a' && nm[i] <= 'z' ? (char)(nm[i] - 32) : nm[i];
    t[k] = 0;
    text(50, 11, t, 1);
    if (!layer()) {
        const char *lab[6] = {"ROUT", "X", "Y", "MODE", "SPD", "DPTH"};
        char v[6][8];
        for (int i = 0; i < 6; i++) v[i][0] = 0;
        int k2 = 0; for (const char *c = route_name[p->route]; *c; c++) v[0][k2++] = *c; v[0][k2] = 0;
        num(v[1], p->x); num(v[2], p->y);
        k2 = 0; for (const char *c = p->scat && slot_is_grains(sel_slot) ? "SCAT" : pmode_name[p->mode]; *c; c++) v[3][k2++] = *c; v[3][k2] = 0;
        num(v[4], p->spd); num(v[5], p->dpth);
        for (int i = 0; i < 6; i++) { int y = 19 + (i % 3) * 13, x = 50 + (i / 3) * 39; text(x, y, lab[i], 1); text(x, y + 6, v[i], 1); }
    } else {
        text(50, 19, "SCAT", 1); text(50, 25, slot_is_grains(sel_slot) ? (p->scat ? "ON" : "OFF") : "-", 1);
        char c[8]; if (pan_chan) num(c, pan_chan); else { c[0] = 'O'; c[1] = 'M'; c[2] = 'N'; c[3] = 'I'; c[4] = 0; }
        text(89, 19, "CHAN", 1); text(89, 25, c, 1);
        if (!slot_is_grains(sel_slot)) text(50, 44, "SCAT: GRAINS", 1);
        char cc[16] = "CC "; num(cc + 3, 20 + 2 * sel_slot); int l = 0; while (cc[l]) l++; cc[l++] = '/'; num(cc + l, 21 + 2 * sel_slot);
        text(50, 34, "X/Y", 1); text(64, 34, cc, 1);
    }
}
static int out_key(int code, int pressed)
{
    if (!pressed) return 1;
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + N_SLOTS) { sel_slot = code - KEY_TRIG0; say(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", route_name[pan_set[sel_slot].route]); return 1; }
    if (code == KEY_YES) { PanSet *p = &pan_set[sel_slot]; p->route = (uint8_t)(p->route == RT_CHAIN ? RT_QUAD : RT_CHAIN); say("OUT", route_name[p->route]); return 1; }
    return 0;
}
static void out_enc(int e, int d)
{
    PanSet *p = &pan_set[sel_slot];
    int st = d > 0 ? 1 : -1;
    if (!layer()) switch (e) {
        case 0: p->route = (uint8_t)clamp(p->route + st, 0, N_ROUTES - 1); say("OUT", route_name[p->route]); break;
        case 1: p->x = (uint8_t)clamp(p->x + d, 0, 127); break;
        case 2: p->y = (uint8_t)clamp(p->y + d, 0, 127); break;
        case 3: p->mode = (uint8_t)clamp(p->mode + st, 0, N_PMODES - 1); say("PAN", pmode_name[p->mode]); break;
        case 4: p->spd = (uint8_t)clamp(p->spd + d, 0, 127); break;
        case 5: p->dpth = (uint8_t)clamp(p->dpth + d, 0, 127); break;
    } else switch (e) {
        case 0: if (slot_is_grains(sel_slot)) { p->scat = (uint8_t)(st > 0); say("SCATTER", p->scat ? "ON" : "OFF"); } break;
        case 3: pan_chan = (uint8_t)clamp(pan_chan + st, 0, 16); break;
        default: break;
    }
}
static void out_leds(void)
{
    for (int s = 0; s < N_SLOTS; s++) led_want[s] = s == sel_slot ? 2 : pan_set[s].route != RT_CHAIN ? 1 : 0;
}
#endif
/* ---- the grain cloud: GRAINS' window drawn as a cloud (AERIA OS, sadnoise, 10 Nov 2026) --------------------------
 * The top edge of the cloud IS the grain window: a grain fades in and out along the cloud's skyline. The clouds
 * are the ones traced from sadnoise's drawing (sky.c's shapes). REC opens it (any page; it shapes every GRAINS).
 *   SHAP  which cloud (1..13)        SKEW  the peak earlier (fast attack) or later (fast release)
 *   PUFF  how much of the cloud's bumps the window keeps (0 a smooth hill .. 127 every bump)
 *   SOFT  smoothing                  EDGE  the fade at both ends (never quite 0: no grain clicks)
 *   FLIP  the cloud mirrored
 * The window goes to the DSP as 65 points (aeria-os/dsp: GRAINS reads it at r7+$80). */
#if defined(AERIAOS)
void aos_grain_window(const uint32_t w[65]);
typedef struct { uint8_t shape, skew, puff, soft, edge, flip; } CloudSet;
static CloudSet cloud = {0, 64, 64, 30, 12, 0};
static int cloud_open;
static int16_t cloud_w[65];   /* 0..1000 */
static void cloud_compute(void)
{
    const uint8_t *top = shape_top[cloud.shape % N_SHAPES];
    int raw[65], sm[65];
    for (int i = 0; i <= 64; i++) {
        /* SKEW: the time axis bent so the peak moves; then the cloud's top edge read there (40 points, linear) */
        int t = i * 1000 / 64, k = cloud.skew - 64;                      /* -64..63 */
        int u = k >= 0 ? t + (int)((long)k * t * (1000 - t) / (64L * 1000)) : t + (int)((long)k * t * (1000 - t) / (64L * 1000));
        u = clamp(u, 0, 1000);
        if (cloud.flip) u = 1000 - u;
        int pos = u * (SHAPE_N - 1), j = pos / 1000, f = pos % 1000;
        int a = top[j], b = top[j + 1 < SHAPE_N ? j + 1 : j];
        raw[i] = (a * (1000 - f) + b * f) / 1000 * 1000 / 63;          /* 0..1000 */
    }
    /* PUFF: between a smooth hill (the shape's own broad curve) and every bump */
    for (int i = 0; i <= 64; i++) {
        int s = 0, n = 0;
        for (int k = -10; k <= 10; k++) { int j = clamp(i + k, 0, 64); s += raw[j]; n++; }
        int hill = s / n;
        sm[i] = hill + (raw[i] - hill) * cloud.puff / 64;
    }
    /* SOFT: a running average; then normalised to its peak */
    int r = cloud.soft / 16, peak = 1;
    for (int i = 0; i <= 64; i++) {
        int s = 0, n = 0;
        for (int k = -r; k <= r; k++) { int j = clamp(i + k, 0, 64); s += sm[j]; n++; }
        raw[i] = clamp(s / n, 0, 1000);
        if (raw[i] > peak) peak = raw[i];
    }
    /* EDGE: a fade at both ends, at least one point */
    int e = 1 + cloud.edge * 20 / 127;
    for (int i = 0; i <= 64; i++) {
        int v = raw[i] * 1000 / peak;
        int d = i < 64 - i ? i : 64 - i;
        if (d < e) v = v * d / e;
        cloud_w[i] = (int16_t)clamp(v, 0, 1000);
    }
    uint32_t w[65];
    for (int i = 0; i <= 64; i++) w[i] = cloud_w[i] >= 1000 ? 0x7FFFFFu : (uint32_t)cloud_w[i] * 8388u;
    aos_grain_window(w);
}
static void cloud_tick(void) { static CloudSet last = {255, 0, 0, 0, 0, 0}; if (last.shape != cloud.shape || last.skew != cloud.skew || last.puff != cloud.puff || last.soft != cloud.soft || last.edge != cloud.edge || last.flip != cloud.flip) { last = cloud; cloud_compute(); } }
static void cloud_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    /* the cloud, filled with sky.c's dither, the window its skyline */
    int x0 = 8, x1 = W - 8, base = 50, hgt = 36;
    for (int x = x0; x <= x1; x++) {
        int i = (x - x0) * 64 / (x1 - x0), y = base - cloud_w[i] * hgt / 1000;
        px(x, y); if (x > x0 && x < x1) px(x, y + 1);
        for (int yy = y + 2; yy < base; yy++) if (dither_on(x, yy, 3)) px(x, yy);
        if ((x & 1) == 0) px(x, base + 1);
    }
    /* the cloud's underside, puffed, as sky.c draws them */
    for (int x = x0; x <= x1; x += 1) { int k = (x - x0) * (SHAPE_N - 1) / (x1 - x0); int b = shape_bot[cloud.shape % N_SHAPES][k] * 6 / 63; for (int yy = base; yy <= base + b; yy++) if (dither_on(x, yy, 2)) px(x, yy); }
    const char *lab[6] = {"SHAP", "SKEW", "PUFF", "SOFT", "EDGE", "FLIP"};
    int v[6] = {cloud.shape + 1, cloud.skew, cloud.puff, cloud.soft, cloud.edge, cloud.flip};
    for (int i = 0; i < 6; i++) text(2 + i * 21, 57, lab[i], 1);
    char n[8]; num(n, v[0]); text(W - 2 - text_w(n), 10, n, 1);
}
static void cloud_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    switch (e) {
    case 0: cloud.shape = (uint8_t)((cloud.shape + N_SHAPES + st) % N_SHAPES); break;
    case 1: cloud.skew = (uint8_t)clamp(cloud.skew + d, 0, 127); break;
    case 2: cloud.puff = (uint8_t)clamp(cloud.puff + d, 0, 127); break;
    case 3: cloud.soft = (uint8_t)clamp(cloud.soft + d, 0, 127); break;
    case 4: cloud.edge = (uint8_t)clamp(cloud.edge + d, 0, 127); break;
    case 5: cloud.flip = (uint8_t)(st > 0); break;
    }
    static const char *const l[6] = {"CLOUD", "SKEW", "PUFF", "SOFT", "EDGE", "FLIP"};
    int v[6] = {cloud.shape + 1, cloud.skew, cloud.puff, cloud.soft, cloud.edge, cloud.flip};
    static char n[8]; if (e >= 0 && e < 6) { num(n, v[e]); say(l[e], n); }
}
#endif
/* ---- MARBLES: a random sequencer for MIDI, after Mutable Instruments Marbles (sadnoise, 10 Nov 2026) --------------
 * After Émilie Gillet's Marbles (open source, MIT; its manual's t/x/DEJA VU ideas, written anew here for AERIA OS).
 *   t  three gates from one clock (RATE: 30..300 BPM, 16ths, or EXT = MIDI clock):
 *      COIN    t2 every step; each step a weighted coin sends t1 or t3 (BIAS: t1 .. t3)
 *      DIVIDE  t2 every step; t1 and t3 on divisions/multiples BIAS picks (/4../2, x2..x4)
 *      DRUMS   t1 kick, t2 hats, t3 snare from 8 grooves (BIAS picks the groove)
 *      INDEP   each t fires on its own coin: t1 1-BIAS, t2 1/2, t3 BIAS
 *      JITR    each gate late by up to half a step, at random
 *   x  three random notes: SPRD (0 a fixed note .. 64 natural .. 127 the extremes), XBIA (where), STEP (0 glides
 *      through every semitone .. 127 only the scale's root and fifth), SCAL, RNGE (1..4 octaves) from ROOT
 *   DEJA VU  how often a random choice repeats the loop of the last LEN choices (0 never .. 127 locked)
 * Each t plays its x as a MIDI note on its own channel (CHAN, CHAN+1, CHAN+2); XMAP (trigs 9-14) rearranges which
 * x goes to which t. YES starts/stops. Gates last half a step. */
#if defined(AERIAOS)
int aos_midi_out(unsigned st, unsigned a, unsigned b);
enum { MT_COIN, MT_DIVIDE, MT_DRUMS, MT_INDEP, N_MTMODES };
static const char *const mt_name[N_MTMODES] = {"COIN", "DIVIDE", "DRUMS", "INDEP"};
static const char *const mx_scale_name[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
static const uint16_t mx_scale[] = {0xab5, 0x5ad, 0x295, 0x6ad, 0xfff};
typedef struct { uint8_t on, rate, tbias, jitr, tmode, deja, len, sprd, xbias, step, scal, rnge, chan, root, xmap; } MarbSet;
static MarbSet mb = {0, 60, 64, 0, 0, 64, 8, 64, 64, 64, 0, 2, 1, 48, 0};
static const uint8_t mb_perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static const uint16_t mb_drums[8][3] = {   /* 16-step masks: kick, hats, snare */
    {0x1111, 0x5555, 0x1010 << 0 | 0x0101 << 2}, {0x1091, 0xffff, 0x0808 << 1}, {0x8421, 0x5555, 0x0404 << 2},
    {0x1131, 0xaaaa, 0x1010 << 2}, {0x0911, 0x7777, 0x2020}, {0x1111, 0xdddd, 0x0410}, {0x9249, 0x5555, 0x2424}, {0x1001, 0xffff, 0x0f0f}};
static uint32_t mb_us, mb_step_us, mb_pending_us[3];
static uint8_t mb_pending[3], mb_note[3] = {0xff, 0xff, 0xff};
static uint32_t mb_gate_us[3];
static uint32_t mb_step;
static uint8_t mb_ring_t[16], mb_ring_x[3][16];
static uint8_t mb_ring_pos;
static uint8_t mb_lastx[3];
static uint32_t mb_flash[3];
static int mb_ext, mb_clocks;
/* a random byte with DEJA VU: the loop of the last LEN draws, replayed with probability DEJA/127 */
static uint8_t mb_draw(uint8_t *ring)
{
    int pos = mb_ring_pos % (mb.len ? mb.len : 1);
    if ((int)(rnd() % 127u) < mb.deja) return ring[pos];
    uint8_t v = (uint8_t)rnd();
    ring[pos] = v;
    return v;
}
static int mb_x_note(int k)
{
    int u = (int)mb_draw(mb_ring_x[k]) * 2 - 255;   /* -255..255 */
    int v2 = (int)mb_draw(mb_ring_x[k]) * 2 - 255;
    int sp = mb.sprd, val;
    if (sp <= 64) val = (u + v2) / 2 * sp / 64;                          /* narrow .. a natural spread */
    else { int s = u < 0 ? -1 : 1, a = iabs(u); val = s * (a + (255 - a) * (sp - 64) / 63); }   /* pushed outwards */
    int pos = clamp((mb.xbias * 255 / 127 - 128) / 2 + val / 2 + 128, 0, 255);          /* 0..255 */
    int span_st = mb.rnge * 12, n = pos * span_st / 255;
    if (mb.step > 16) {   /* quantise to the scale; near the top only the root and fifth */
        uint16_t m = mb.step > 110 ? 0x081 : mx_scale[mb.scal % 5];
        int oct = n / 12, deg = n % 12;
        while (deg > 0 && !((m >> deg) & 1)) deg--;
        n = oct * 12 + deg;
    }
    return clamp(mb.root + n, 0, 127);
}
static void mb_fire(int t)
{
    int x = mb_perm[mb.xmap % 6][t];
    int note = mb_x_note(x), ch = (mb.chan - 1 + t) & 15;
    if (mb_note[t] != 0xff) aos_midi_out(0x80u | (unsigned)ch, mb_note[t], 0);
    aos_midi_out(0x90u | (unsigned)ch, (unsigned)note, 100u);
    mb_note[t] = (uint8_t)note; mb_lastx[x] = (uint8_t)note;
    mb_gate_us[t] = mb_step_us / 2 + 1;
    mb_flash[t] = 12;
}
static void mb_tick_step(void)
{
    int fire[3] = {0, 0, 0};
    uint8_t r = mb_draw(mb_ring_t);
    int b = mb.tbias * 255 / 127, st = (int)(mb_step & 15);
    switch (mb.tmode) {
    case MT_COIN: fire[1] = 1; if (r < b) fire[2] = 1; else fire[0] = 1; break;
    case MT_DIVIDE: { static const uint8_t d1[5] = {4, 3, 2, 1, 1}, d3[5] = {1, 1, 2, 3, 4}; int k = mb.tbias * 4 / 127;
        fire[1] = 1; fire[0] = (mb_step % d1[k]) == 0; fire[2] = (mb_step % d3[k]) == 0; break; }
    case MT_DRUMS: { const uint16_t *g = mb_drums[mb.tbias * 7 / 127]; for (int k = 0; k < 3; k++) fire[k] = (g[k] >> st) & 1; break; }
    default: fire[0] = r > b; fire[1] = (rnd() & 1); fire[2] = (uint8_t)rnd() < b; break;
    }
    for (int k = 0; k < 3; k++) if (fire[k]) {
        uint32_t late = mb.jitr ? (rnd() % (mb_step_us / 2 + 1)) * mb.jitr / 127 : 0;
        mb_pending[k] = 1; mb_pending_us[k] = late;
    }
    mb_ring_pos++; mb_step++;
}
static void marbles_fast(uint32_t us)
{
    if (!mb.on) return;
    if (!mb_ext) {
        mb_step_us = 15000000u / (uint32_t)(30 + mb.rate * 2);       /* a 16th at 30..284 BPM */
        mb_us += us;
        if (mb_us >= mb_step_us) { mb_us -= mb_step_us; if (mb_us > mb_step_us) mb_us = 0; mb_tick_step(); }
    }
    for (int k = 0; k < 3; k++) if (mb_pending[k]) { if (mb_pending_us[k] <= us) { mb_pending[k] = 0; mb_fire(k); } else mb_pending_us[k] -= us; }
    for (int k = 0; k < 3; k++) if (mb_gate_us[k]) {
        if (mb_gate_us[k] <= us) { mb_gate_us[k] = 0; if (mb_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((mb.chan - 1 + k) & 15), mb_note[k], 0); mb_note[k] = 0xff; } }
        else mb_gate_us[k] -= us;
    }
}
static void marbles_tick(void)
{
    for (int k = 0; k < 3; k++) if (mb_flash[k]) mb_flash[k]--;
}
static void marbles_stop(void) { for (int k = 0; k < 3; k++) if (mb_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((mb.chan - 1 + k) & 15), mb_note[k], 0); mb_note[k] = 0xff; } }
static void marbles_midi(unsigned st, unsigned a, unsigned b)
{
    (void)a; (void)b;
    if (st == 0xF8 && mb.rate == 0) { mb_ext = 1; mb_step_us = 100000u; if (mb.on && (++mb_clocks % 6) == 0) mb_tick_step(); }
    if (st == 0xFA && mb.rate == 0) mb_clocks = 0;
    if (mb.rate) mb_ext = 0;
}
static void marbles_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    if (!mb.on) text(40, 10, "YES STARTS", 1);
    /* three rails: a marble drops on each gate, its height the note */
    for (int k = 0; k < 3; k++) {
        int x = 14 + k * 20;
        for (int y = 18; y < 54; y += 2) px(x, y);
        int y = 52 - (mb_lastx[mb_perm[mb.xmap % 6][k]] % 36);
        int r = mb_flash[k] ? 3 : 2;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) px(x + dx, y + dy);
        char t[3] = {'T', (char)('1' + k), 0}; text(x - 3, 56, t, 1);
    }
    const char *lab[6], *val[6]; char nb[6][8];
    if (!layer()) {
        lab[0] = "RATE"; lab[1] = "BIAS"; lab[2] = "JITR"; lab[3] = "MODE"; lab[4] = "DEJA"; lab[5] = "LEN";
        if (mb.rate) num(nb[0], 30 + mb.rate * 2); else { nb[0][0] = 'E'; nb[0][1] = 'X'; nb[0][2] = 'T'; nb[0][3] = 0; }
        num(nb[1], mb.tbias); num(nb[2], mb.jitr); num(nb[4], mb.deja); num(nb[5], mb.len);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = nb[2]; val[3] = mt_name[mb.tmode]; val[4] = nb[4]; val[5] = nb[5];
    } else {
        lab[0] = "SPRD"; lab[1] = "XBIA"; lab[2] = "STEP"; lab[3] = "SCAL"; lab[4] = "RNGE"; lab[5] = "CHAN";
        num(nb[0], mb.sprd); num(nb[1], mb.xbias); num(nb[2], mb.step); num(nb[4], mb.rnge); num(nb[5], mb.chan);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = nb[2]; val[3] = mx_scale_name[mb.scal % 5]; val[4] = nb[4]; val[5] = nb[5];
    }
    for (int i = 0; i < 6; i++) { int x = 74 + (i % 2) * 27, y = 19 + (i / 2) * 13; text(x, y, lab[i], 1); text(x, y + 6, val[i], 1); }
}
static int marbles_key(int code)
{
    if (code == KEY_YES) { mb.on = !mb.on; if (!mb.on) marbles_stop(); say("MARBLES", mb.on ? "ON" : "OFF"); return 1; }
    if (code >= KEY_TRIG0 + 8 && code < KEY_TRIG0 + 14) { mb.xmap = (uint8_t)(code - KEY_TRIG0 - 8); char t[8] = {'X', '>', 'T', ' ', 0}; say("XMAP", t); return 1; }
    return 0;
}
static void marbles_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    if (!layer()) switch (e) {
        case 0: mb.rate = (uint8_t)clamp(mb.rate + d, 0, 127); break;
        case 1: mb.tbias = (uint8_t)clamp(mb.tbias + d, 0, 127); break;
        case 2: mb.jitr = (uint8_t)clamp(mb.jitr + d, 0, 127); break;
        case 3: mb.tmode = (uint8_t)clamp(mb.tmode + st, 0, N_MTMODES - 1); break;
        case 4: mb.deja = (uint8_t)clamp(mb.deja + d, 0, 127); break;
        case 5: mb.len = (uint8_t)clamp(mb.len + st, 1, 16); break;
    } else switch (e) {
        case 0: mb.sprd = (uint8_t)clamp(mb.sprd + d, 0, 127); break;
        case 1: mb.xbias = (uint8_t)clamp(mb.xbias + d, 0, 127); break;
        case 2: mb.step = (uint8_t)clamp(mb.step + d, 0, 127); break;
        case 3: mb.scal = (uint8_t)clamp(mb.scal + st, 0, 4); break;
        case 4: mb.rnge = (uint8_t)clamp(mb.rnge + st, 1, 4); break;
        case 5: marbles_stop(); mb.chan = (uint8_t)clamp(mb.chan + st, 1, 14); break;
    }
}
static void marbles_leds(void)
{
    for (int k = 0; k < 3; k++) led_want[k] = mb_flash[k] ? 3 : mb.on ? 1 : 0;
    for (int k = 0; k < 6; k++) led_want[8 + k] = k == mb.xmap ? 2 : 0;
}
#endif
/* ---- PONG: balls bounce round the screen like the old DVD logo; each wall a ball hits plays (sadnoise, 10 Nov 2026) --
 * YES adds a ball (up to 8), NO takes the last one away, trigs 1-8 mute a ball. FUNC+YES opens the card: a folder's
 * WAVs are dealt to the balls (ball n plays sample n, round the folder). Each wall is a different note of the scale --
 * left the root, then top, right and bottom higher by SPRD -- and each ball sits a little higher than the one before.
 *   SPD  how fast        SIZE the balls       PTCH the samples' pitch (64 as recorded)
 *   SPRD the walls' spread      DECY how long a hit rings      MODE SAMPLE, MIDI or BOTH
 *   FUNC: CHAN (MIDI channel) ROOT (MIDI note of the root) SCAL VEL LEVL (sample level) GRAV (falls a little)
 * MIDI: a note on CHAN at every hit, off after DECY. */
#if defined(AERIAOS)
void aos_pong(int slot, int ptch, int dec, int level);
int aos_slot_ready(int slot);
#define PONG_BALLS 8
#define PONG_BANK 7                    /* static slots 112..127 */
enum { PM_SAMPLE, PM_MIDI, PM_BOTH, N_PONGMODES };
static const char *const pong_mode_name[N_PONGMODES] = {"SAMPLE", "MIDI", "BOTH"};
typedef struct { uint8_t spd, size, ptch, sprd, decy, mode, chan, root, scal, vel, levl, grav, nballs, muted; } PongSet;
static PongSet pg = {50, 2, 64, 40, 50, 0, 1, 48, 0, 100, 100, 0, 0, 0};
typedef struct { int x, y, vx, vy; uint8_t flash; } Ball;
static Ball ball[PONG_BALLS];
static uint8_t pong_note[PONG_BALLS] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint32_t pong_off_us[PONG_BALLS];
#define PX0 3
#define PX1 124
#define PY0 11
#define PY1 54
static void pong_launch(int k)
{
    Ball *b = &ball[k];
    b->x = (PX0 + 6 + (int)(rnd() % (unsigned)(PX1 - PX0 - 12))) << 8; b->y = (PY0 + 6 + (int)(rnd() % (unsigned)(PY1 - PY0 - 12))) << 8;
    int a = (int)(rnd() % 1024u);
    b->vx = cos10(a) * 2; b->vy = sin10(a) * 2;
    if (iabs(b->vx) < 60) b->vx = b->vx < 0 ? -60 : 60;
    if (iabs(b->vy) < 60) b->vy = b->vy < 0 ? -60 : 60;
}
static int pong_scale_note(int deg)
{
    static const uint16_t sc[] = {0xab5, 0x5ad, 0x295, 0x6ad, 0xfff};
    uint16_t m = sc[pg.scal % 5];
    int deg_n[12], n = 0;
    for (int i = 0; i < 12; i++) if ((m >> i) & 1) deg_n[n++] = i;
    return (deg / n) * 12 + deg_n[deg % n];
}
static void pong_hit(int k, int wall)
{
    Ball *b = &ball[k];
    b->flash = 6;
    if ((pg.muted >> k) & 1) return;
    int deg = wall * (1 + pg.sprd / 24) + k;
    int semis = pong_scale_note(deg);
    if (pg.mode != PM_MIDI) {
        int slot = PONG_BANK * 16 + (k % 16);
        if (aos_slot_ready(slot)) aos_pong(slot, clamp(pg.ptch + semis, 0, 127), pg.decy, pg.levl);
    }
    if (pg.mode != PM_SAMPLE) {
        unsigned ch = (unsigned)((pg.chan - 1) & 15);
        if (pong_note[k] != 0xff) aos_midi_out(0x80u | ch, pong_note[k], 0);
        int note = clamp(pg.root + semis, 0, 127);
        aos_midi_out(0x90u | ch, (unsigned)note, pg.vel);
        pong_note[k] = (uint8_t)note;
        pong_off_us[k] = 20000u + (uint32_t)pg.decy * pg.decy * 60u;
    }
}
static void pong_tick(void)
{
    int sp = 20 + pg.spd * 3, r = pg.size;
    for (int k = 0; k < pg.nballs; k++) {
        Ball *b = &ball[k];
        if (b->flash) b->flash--;
        if (pg.grav) b->vy += pg.grav / 8 + 1;
        b->x += b->vx * sp / 256; b->y += b->vy * sp / 256;
        int x = b->x >> 8, y = b->y >> 8;
        if (x - r <= PX0) { b->x = (PX0 + r + 1) << 8; b->vx = iabs(b->vx); pong_hit(k, 0); }
        if (x + r >= PX1) { b->x = (PX1 - r - 1) << 8; b->vx = -iabs(b->vx); pong_hit(k, 2); }
        if (y - r <= PY0) { b->y = (PY0 + r + 1) << 8; b->vy = iabs(b->vy); pong_hit(k, 1); }
        if (y + r >= PY1) { b->y = (PY1 - r - 1) << 8; b->vy = -iabs(b->vy); if (pg.grav) b->vy = b->vy * 15 / 16 - 30; pong_hit(k, 3); }
    }
}
static void pong_fast(uint32_t us)
{
    for (int k = 0; k < PONG_BALLS; k++) if (pong_note[k] != 0xff) {
        if (pong_off_us[k] <= us) { aos_midi_out(0x80u | (unsigned)((pg.chan - 1) & 15), pong_note[k], 0); pong_note[k] = 0xff; }
        else pong_off_us[k] -= us;
    }
}
static void pong_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    for (int x = PX0; x <= PX1; x++) { px(x, PY0); px(x, PY1); }
    for (int y = PY0; y <= PY1; y++) { px(PX0, y); px(PX1, y); }
    for (int k = 0; k < pg.nballs; k++) {
        int x = ball[k].x >> 8, y = ball[k].y >> 8, r = pg.size + (ball[k].flash ? 1 : 0);
        int muted = (pg.muted >> k) & 1;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r && (!muted || ((dx + dy) & 1))) px(x + dx, y + dy);
    }
    if (!pg.nballs) text(30, 30, "YES ADDS A BALL", 1);
    const char *lab[6], *val[6]; char nb[6][8];
    if (!layer()) {
        lab[0] = "SPD"; lab[1] = "SIZE"; lab[2] = "PTCH"; lab[3] = "SPRD"; lab[4] = "DECY"; lab[5] = "MODE";
        num(nb[0], pg.spd); num(nb[1], pg.size); num(nb[2], pg.ptch); num(nb[3], pg.sprd); num(nb[4], pg.decy);
        for (int i = 0; i < 5; i++) val[i] = nb[i]; val[5] = pong_mode_name[pg.mode];
    } else {
        static const char *const sn[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
        lab[0] = "CHAN"; lab[1] = "ROOT"; lab[2] = "SCAL"; lab[3] = "VEL"; lab[4] = "LEVL"; lab[5] = "GRAV";
        num(nb[0], pg.chan); num(nb[1], pg.root); num(nb[3], pg.vel); num(nb[4], pg.levl); num(nb[5], pg.grav);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = sn[pg.scal % 5]; val[3] = nb[3]; val[4] = nb[4]; val[5] = nb[5];
    }
    for (int i = 0; i < 6; i++) text(2 + i * 21, 57, lab[i], 1);
    (void)val;
}
static void browser_open(int mode);
static int pong_key(int code)
{
    if (code == KEY_YES && func_held) { browser_open(BR_PONG); return 1; }
    if (code == KEY_YES) { if (pg.nballs < PONG_BALLS) { pong_launch(pg.nballs); pg.nballs++; } char n[4]; num(n, pg.nballs); say("BALLS", n); return 1; }
    if (code == KEY_NO) { if (pg.nballs) { pg.nballs--; int k = pg.nballs; if (pong_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((pg.chan - 1) & 15), pong_note[k], 0); pong_note[k] = 0xff; } } char n[4]; num(n, pg.nballs); say("BALLS", n); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + PONG_BALLS) { int k = code - KEY_TRIG0; pg.muted ^= (uint8_t)(1u << k); say("BALL", (pg.muted >> k) & 1 ? "MUTED" : "ON"); return 1; }
    return 0;
}
static void pong_say(int e)
{
    static const char *const l1[6] = {"SPEED", "SIZE", "PITCH", "SPREAD", "DECAY", "MODE"}, *const l2[6] = {"CHANNEL", "ROOT", "SCALE", "VELOCITY", "LEVEL", "GRAVITY"};
    static const char *const sn[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
    int v1[6] = {pg.spd, pg.size, pg.ptch, pg.sprd, pg.decy, 0}, v2[6] = {pg.chan, pg.root, 0, pg.vel, pg.levl, pg.grav};
    static char n[8];
    if (!layer()) { if (e == 5) say(l1[e], pong_mode_name[pg.mode]); else if (e == 2) { static char pn[24]; pitch_text(pn, (pg.ptch - 64) * 1000); say(l1[e], pn); } else { num(n, v1[e]); say(l1[e], n); } }
    else { if (e == 2) say(l2[e], sn[pg.scal % 5]); else { num(n, v2[e]); say(l2[e], n); } }
}
static void pong_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    if (!layer()) switch (e) {
        case 0: pg.spd = (uint8_t)clamp(pg.spd + d, 0, 127); break;
        case 1: pg.size = (uint8_t)clamp(pg.size + st, 1, 4); break;
        case 2: pg.ptch = (uint8_t)clamp(pg.ptch + st, 16, 112); break;
        case 3: pg.sprd = (uint8_t)clamp(pg.sprd + d, 0, 127); break;
        case 4: pg.decy = (uint8_t)clamp(pg.decy + d, 0, 127); break;
        case 5: pg.mode = (uint8_t)clamp(pg.mode + st, 0, N_PONGMODES - 1); break;
    } else switch (e) {
        case 0: pg.chan = (uint8_t)clamp(pg.chan + st, 1, 16); break;
        case 1: pg.root = (uint8_t)clamp(pg.root + st, 0, 115); break;
        case 2: pg.scal = (uint8_t)clamp(pg.scal + st, 0, 4); break;
        case 3: pg.vel = (uint8_t)clamp(pg.vel + d, 1, 127); break;
        case 4: pg.levl = (uint8_t)clamp(pg.levl + d, 0, 127); break;
        case 5: pg.grav = (uint8_t)clamp(pg.grav + d, 0, 127); break;
    }
    if (e >= 0 && e < 6) pong_say(e);
}
static void pong_leds(void)
{
    for (int k = 0; k < PONG_BALLS; k++) led_want[k] = k >= pg.nballs ? 0 : ((pg.muted >> k) & 1) ? 0 : ball[k].flash ? 3 : 1;
}
#endif
/* ---- the weather as sound (AERIA OS, sadnoise, 10 Nov 2026), after Pinkie Pie's Balloon Shop (Weather.h) ---------
 * On the chain's output, after the main feedback's tap (the engine's wthr):
 *   RAIN, STORM  a 2-pole lowpass eases in to about 6 kHz over a few seconds (and out again)
 *   CLOUDY, STORM  thunder: now and then the lowpass is pulled down and rolls back up; every lightning strike does too
 *   HELIUM (MIX, FUNC layer)  a 2-pole high-pass up to about 200 Hz
 *   STORM  each strike lends a random effect the chain does not have to an empty slot for 45-90 s
 * WSND (MIX) turns the weather's sound off (the knob weather stays). */
#if defined(AERIAOS)
void aos_weather_filters(int lp, int hp);   /* cutoff indices 0..127 (20 Hz .. 20 kHz, log); -1 off */
static int wx_lp = 127 << 8, wx_hp = 0, wx_roll;
static int lend_slot = -1, lend_row;
static uint32_t lend_until, roll_next;
static void wx_tick(void)
{
    if (!pv(PG_MIX, 5)) { aos_weather_filters(-1, -1); wx_lp = 127 << 8; wx_hp = 0; return; }
    int rain = WON(RAIN) || WON(STORM);
    int lp_t = rain ? 105 : 127;                                   /* index 105: about 6 kHz */
    /* thunder: the strikes (thunder 200 falling) and, under clouds, rolls of their own */
    if ((WON(CLOUDY) || WON(STORM)) && ticks > roll_next) { wx_roll = 120 + (int)(rnd() % 80u); roll_next = ticks + 61u * (uint32_t)range(15, 50); }
    int dip = thunder > wx_roll ? thunder : wx_roll;
    if (wx_roll) wx_roll = wx_roll > 2 ? wx_roll - 2 : 0;
    if (dip) lp_t = lp_t - dip * 60 / 200;                         /* down to about 700 Hz at a strike */
    wx_lp += (lp_t * 256 - wx_lp) / (dip ? 6 : 120);               /* thunder bites fast, rain eases */
    int hp_t = pv(PG_MIX, 4) ? 42 : 0;                             /* index 42: about 200 Hz */
    wx_hp += (hp_t * 256 - wx_hp) / 90;
    int lp = wx_lp >> 8, hp = wx_hp >> 8;
    aos_weather_filters(lp >= 126 ? -1 : lp, hp <= 1 ? -1 : hp);
    /* lightning lends an effect: a strike in a storm, an empty slot, an effect the chain has not got (one with no
     * buffer, so it always fits); it goes back when its time is up, unless the musician kept it */
    if (lend_slot >= 0 && ticks > lend_until) {
        if (slots[lend_slot].row == lend_row) slot_pick(lend_slot, 0);
        lend_slot = -1;
    }
    if (WON(STORM) && thunder == 200 && lend_slot < 0 && picking < 0) {
        int empty = -1;
        for (int s = N_SLOTS - 1; s >= 0; s--) if (slots[s].row <= 0) { empty = s; break; }
        int cand[FX_COUNT], n = 0;
        for (int r = 1; r <= FX_COUNT; r++) {
            int have = 0;
            for (int s = 0; s < N_SLOTS; s++) if (slots[s].row == r) have = 1;
            if (!have && !fx_info[r - 1].has_y) cand[n++] = r;
        }
        if (empty >= 0 && n) {
            lend_row = cand[rnd() % (uint32_t)n]; lend_slot = empty;
            lend_until = ticks + 61u * (uint32_t)range(45, 90);
            slot_pick(empty, lend_row);
            say("LIGHTNING LENDS", fx_info[lend_row - 1].name + 6);
        }
    }
}

/* ---- more weather (Balloon Shop 1.2; AERIA OS, 12 Nov 2026): weathers that are a sound of their own ---------------
 * WIND (a drifting pitch wobble, gusts, a lean), HAIL (stones of the sound itself bouncing), AURORA (an octave up
 * into four slow curtains of reverb), the TWISTER (one event: the sound climbs a fifth and spins) -- and FOG now
 * blurs and darkens. Each is an effect of its own (aeria-fx wind / fog / twister / hail / aurora) that its weather
 * puts into an empty slot of the chain while it lasts, its AMT faded in and out, and takes away again. WIND and FOG
 * are never on together (the later one stays), nor HAIL and AURORA. With WIND and CLOUDY a twister may come by
 * itself, about every two and a half minutes. An effect the musician put there by hand is left alone.
 * SKY page: trig 9 WIND, 10 HAIL, 11 AURORA, 12 TWISTER. */
enum { XW_WIND, XW_HAIL, XW_AURORA, XW_N };
enum { XF_WIND, XF_FOG, XF_TWISTER, XF_HAIL, XF_AURORA, XF_N };
static uint8_t xw_on;                    /* a bit a weather of the three */
static const char *const xf_name[XF_N] = {"AERIA WIND", "AERIA FOG", "AERIA TWISTER", "AERIA HAIL", "AERIA AURORA"};
static const uint16_t xf_tail[XF_N] = {30, 30, 30, 120, 720};   /* ticks it is left ringing after its fade (hail's stones, the aurora's tail) */
static int8_t xf_slot[XF_N] = {-1, -1, -1, -1, -1};
static uint8_t xf_amt[XF_N], xf_row[XF_N], xf_ready;
static uint16_t xf_gone[XF_N];
static uint32_t tw_until, tw_roll;
static int xf_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void xw_toggle(int w)
{
    xw_on ^= (uint8_t)(1u << w);
    int on = (xw_on >> w) & 1;
    if (on && w == XW_WIND && WON(FOG)) toggle_weather(FOG);          /* wind blows the fog away */
    if (on && w == XW_HAIL) xw_on &= (uint8_t)~(1u << XW_AURORA);
    if (on && w == XW_AURORA) xw_on &= (uint8_t)~(1u << XW_HAIL);
    say(w == XW_WIND ? "WIND" : w == XW_HAIL ? "HAIL" : "AURORA", on ? "ON" : "OFF");
}
static void xw_twister(void) { if (ticks > tw_until) { tw_until = ticks + 61u * 24u; say("TWISTER", 0); } }
static void xw_tick(void)
{
    if (!xf_ready) {
        if (ticks < 90) return;
        for (int i = 0; i < XF_N; i++) for (int r = 1; r <= FX_COUNT; r++) if (xf_same(fx_info[r - 1].name, xf_name[i])) xf_row[i] = (uint8_t)r;
        /* what a weather left in the chain when the power went: its again, to fade and take away */
        for (int i = 0; i < XF_N; i++) for (int s = 0; s < N_SLOTS; s++) if (xf_row[i] && slots[s].row == xf_row[i]) { xf_slot[i] = (int8_t)s; xf_amt[i] = (uint8_t)clamp(slots[s].user[0], 0, 127); }
        xf_ready = 1;
    }
    static uint8_t fog_was;
    if (WON(FOG) && !fog_was) xw_on &= (uint8_t)~(1u << XW_WIND);     /* ... and fog stills the wind */
    fog_was = (uint8_t)WON(FOG);
    int sound = pv(PG_MIX, 5) != 0;                                    /* WSND: the weather's sound */
    if (sound && (xw_on & 1) && WON(CLOUDY) && ticks > tw_roll) { tw_roll = ticks + 61u; if (rnd() % 150u == 0) xw_twister(); }
    int want[XF_N] = {sound && (xw_on >> XW_WIND) & 1, sound && WON(FOG), sound && ticks < tw_until, sound && (xw_on >> XW_HAIL) & 1, sound && (xw_on >> XW_AURORA) & 1};
    for (int i = 0; i < XF_N; i++) {
        int s = xf_slot[i], row = xf_row[i];
        if (!row) continue;
        if (s >= 0 && slots[s].row != row) { s = xf_slot[i] = -1; xf_amt[i] = 0; }   /* the musician took the slot for something else */
        if (s < 0) {
            if (!want[i] || picking >= 0) continue;
            int have = 0, empty = -1;
            for (int k = 0; k < N_SLOTS; k++) { if (slots[k].row == row) have = 1; if (slots[k].row <= 0 && k != lend_slot) empty = k; }
            if (have || empty < 0) continue;                            /* there by hand already, or no room: the sky still shows it */
            slot_pick(empty, row);
            if (slots[empty].row != row && slots[empty].pending != row) continue;   /* the DSP had no room for it */
            xf_slot[i] = (int8_t)empty; xf_amt[i] = 0; xf_gone[i] = 0;
            continue;
        }
        if (slots[s].pending >= 0) continue;                            /* still being put in */
        int target = want[i] ? 127 : 0;
        if (i == XF_TWISTER) target = want[i] && tw_until - ticks > 61u * 22u ? 127 : 0;   /* its knob is a trigger: up for the first two seconds */
        if (xf_amt[i] < target) xf_amt[i] = (uint8_t)clamp(xf_amt[i] + 6, 0, 127); else if (xf_amt[i] > target) xf_amt[i] = (uint8_t)clamp(xf_amt[i] - 6, 0, 127);
        slots[s].user[0] = xf_amt[i]; slots[s].value[0] = xf_amt[i];
        if (!want[i] && !xf_amt[i]) { if (++xf_gone[i] > xf_tail[i]) { slot_pick(s, 0); xf_slot[i] = -1; } } else xf_gone[i] = 0;
    }
}
/* the sky's share: wind streaks, hailstones, the aurora's curtains, the twister's funnel */
static void xw_draw(void)
{
    int t = (int)ticks;
    if (xw_on & (1u << XW_WIND)) for (int i = 0; i < 7; i++) {
        uint32_t h = (uint32_t)i * 2654435761u; int y = 12 + (int)((h >> 8) % 30u), len = 6 + (int)((h >> 16) % 10u);
        int x = (int)((h % 160u) + (uint32_t)t * (2u + (uint32_t)(i & 1))) % 170 - 20;
        for (int k = 0; k < len; k++) if ((k + t / 2) % 4) px(x + k, y + (sine[(x + k + t) & 63] > 180 ? 1 : 0));
    }
    if (xw_on & (1u << XW_HAIL)) for (int i = 0; i < 10; i++) {
        uint32_t h = (uint32_t)(i + 3) * 40503u * 2654435761u; int per = 40 + (int)((h >> 20) % 30u), ph = (t + (int)(h % 97u)) % per;
        int x = (int)((h >> 8) % (uint32_t)W), fall = WATER_Y - 12, y;
        if (ph < per / 2) y = 10 + ph * fall * 2 / per;                  /* down */
        else { int b = ph - per / 2, hgt = 8 - b * 16 / per; y = WATER_Y - 2 - (hgt > 0 ? hgt : 0) * (per - ph) * 2 / per; }   /* one small bounce */
        px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1);
    }
    if (xw_on & (1u << XW_AURORA)) for (int c = 0; c < 4; c++) {
        int cx = 16 + c * 32 + sine[(t / 3 + c * 17) & 63] / 40, lv = 3 + sine[(t / 5 + c * 23) & 63] / 50;
        for (int x = cx - 9; x <= cx + 9; x++) { int top = 9 + sine[(x * 5 + t / 2 + c * 9) & 63] / 60, len = 12 + sine[(x * 3 - t / 4 + c * 31) & 63] / 28;
            for (int y = top; y < top + len && y < WATER_Y - 8; y++) if (dither_on(x, y, clamp(lv - (y - top) * lv / (len + 1), 1, 8))) px(x, y); }
    }
    if (ticks < tw_until) {
        int u = (int)(tw_until - ticks), x0 = W + 10 - (61 * 24 - u) * (W + 30) / (61 * 24);
        for (int y = 12; y < WATER_Y; y += 1) { int wdt = 2 + (WATER_Y - y) * 9 / (WATER_Y - 12), sway = sine[(y * 4 + t * 5) & 63] / 40 - 3;
            int a = x0 + sway - wdt, b = x0 + sway + wdt; px(a, y); px(b, y); if (((y + t / 2) & 3) == 0) for (int x = a + 1; x < b; x += 2) px(x, y); }
    }
}
#endif
/* the sequencer's data and its on/off, for the parts that come before seqmode.c */
#if defined(AERIAOS) && (defined(__m68k__) || defined(SEQ_HOST))
#include "seq_host.h"
#define SEQ_UI 1
static uint8_t seq_on;
#endif
/* ---- AERIA OS: the saved state and the presets (sadnoise, 10 Nov 2026) ----------------------------------------
 * A snapshot is every page parameter (the musician's setting, or -1: follow the weather), the chain (which effect
 * in each slot, its knobs, each slot's own knob settings and bypasses), the weather, auto, the crossfader's weather,
 * day/night, the sources and the planets, with a checksum. AERIA OS keeps the state and 16 presets in battery RAM:
 * the state a couple of seconds after anything changes, a preset when the musician saves it.
 *   PROJ: the presets. A trig loads that preset; FUNC + trig saves the sound into it; NO (or PROJ) closes.
 * Only in AERIA OS: the Modwerk module build has no battery RAM of its own to keep this in. */
#if defined(AERIAOS)
static uint8_t led_want[16];   /* ui12.c's (a tentative definition: the same object) */
#define SNAP_MAGIC 0x41455338u   /* 'AES8': saved since the weather leaves MIX alone (the dry start holds) */
#define SNAP_MAGIC_V7 0x41455337u   /* 'AES7': a page more (MASTER); kept in the bigger store */
#define SNAP_MAGIC_V6 0x41455336u   /* 'AES6': AES4's layout; saved since untouched effects start dry (slot_gentle) */
#define SNAP_MAGIC_V5 0x41455335u   /* a build of 12 Nov 2026 that lost the dry start on the way: same as AES4 */
#define SNAP_MAGIC_V4 0x41455334u   /* 'AES4': AES3 + the chain slots' FUNC-layer knobs */
#define SNAP_MAGIC_V3 0x41455333u   /* 'AES3': AES2 + OUT/panners, the grain cloud, MARBLES, PONG, the MIDI module */
#define SNAP_MAGIC_V2 0x41455332u
/* The store (12 Nov 2026): 1 KB a snapshot at 0x100E0000 (slot 0 the state, 1..16 the presets). Until then they
 * were 512 bytes each at 0x100F9800, full, and a page shorter: what is found there is brought over once, its page
 * rows moved up to make room for the new page's, its version kept (so what was saved before the dry start still
 * gets it). In the new store every version has the new layout. */
#define SNAP_STRIDE 1024u
#define SNAP_OLD_BASE ((uintptr_t)fake_old_store)
#define SNAP_OLD_STRIDE 512u
#ifndef SNAP_BASE
#define SNAP_BASE 0x100E0000u
#endif
#define N_PRESETS 16
typedef struct {
    uint32_t magic, sum;
    int8_t user[N_PAGES][12];
    uint8_t value[N_PAGES][12];
    uint8_t live_fx[16];
    uint8_t part_fx[8][12];
    int8_t slot_user[N_SLOTS][6];
    int16_t weather, auto_weather, xf_right, night, src_loop, src_stars, src_balloons;
    uint16_t planet_mask, slot_bypass;
    /* v3 (10 Nov 2026) */
    PanSet pan[N_SLOTS];
    uint8_t pan_chan, midi_mod;
    CloudSet cloud;
    MarbSet marb;
    PongSet pong;
} Snap;
/* v4 (10 Nov 2026): the chain slots' FUNC-layer knobs, 42 bytes. The snapshot is full (512 bytes a preset, and the
 * presets sit back to back), so they live in the rows of two pages that have no knobs of their own -- CHAIN and
 * SOURCE, 24 bytes each across value[] and user[] -- which a v3 snapshot left at 0. The layout is v3's. */
#define SNAP_V3_SIZE ((unsigned)sizeof(Snap))
static uint8_t *snap_p2(Snap *s, int i, int k)
{
    int n = i * 6 + k;
    return n < 12 ? &s->value[PG_CHAIN][n] : n < 24 ? (uint8_t *)&s->user[PG_CHAIN][n - 12] : n < 36 ? &s->value[PG_SOURCE][n - 24] : (uint8_t *)&s->user[PG_SOURCE][n - 36];
}
#define SNAP_V2_SIZE ((unsigned)__builtin_offsetof(Snap, pan))
typedef char snap_fits[sizeof(Snap) <= SNAP_STRIDE ? 1 : -1];
static Snap snap_now;
static uint32_t snap_sum_n(const Snap *s, unsigned size)
{
    const uint8_t *b = (const uint8_t *)s + 8;
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 8; i < size; i++) h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t snap_sum(const Snap *s) { return snap_sum_n(s, sizeof(Snap)); }
static void snap_take(Snap *s)
{
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof(Snap); i++) b[i] = 0;
    for (int p = 0; p < N_PAGES; p++) for (int k = 0; k < 12; k++) {
        s->user[p][k] = (int8_t)(params[p][k].user < 0 ? -1 : params[p][k].user > 127 ? 127 : params[p][k].user);
        s->value[p][k] = (uint8_t)(params[p][k].value & 0x7f);
    }
    for (int i = 0; i < 16; i++) s->live_fx[i] = preview_live_fx[i];
    for (int t = 0; t < 8; t++) for (int k = 0; k < 12; k++) s->part_fx[t][k] = preview_part_fx[t][k];
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) s->slot_user[i][k] = (int8_t)(slots[i].user[k] < 0 ? -1 : slots[i].user[k] > 127 ? 127 : slots[i].user[k]);
    s->weather = (int16_t)weather; s->auto_weather = (int16_t)auto_weather; s->xf_right = (int16_t)(0x100 | wx_on);   /* (the field the weather crossfade used: now which weathers are on) */ s->night = (int16_t)night;
    s->src_loop = (int16_t)src_loop; s->src_stars = (int16_t)src_stars; s->src_balloons = (int16_t)src_balloons;
    s->planet_mask = planet_mask; s->slot_bypass = slot_bypass;
    for (int i = 0; i < N_SLOTS; i++) s->pan[i] = pan_set[i];
    s->pan_chan = pan_chan; s->midi_mod = (uint8_t)midi_mod_get(); s->cloud = cloud; s->marb = mb; s->pong = pg;
    s->marb.on = 0;   /* a restart never starts MARBLES by itself */
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) *snap_p2(s, i, k) = preview_slot_p2[i][k];
    s->magic = SNAP_MAGIC; s->sum = snap_sum(s);
}
/* 3: a v3 snapshot; 2: an AES2 one (the part before the panners is the same layout); 0: none */
static int snap_version(const volatile Snap *s)
{
    if (s->sum != snap_sum((const Snap *)s)) return 0;
    return s->magic == SNAP_MAGIC ? 8 : s->magic == SNAP_MAGIC_V7 ? 7 : s->magic == SNAP_MAGIC_V6 ? 6 : (s->magic == SNAP_MAGIC_V4 || s->magic == SNAP_MAGIC_V5) ? 4 : s->magic == SNAP_MAGIC_V3 ? 3 : s->magic == SNAP_MAGIC_V2 ? 2 : 0;
}
static int snap_valid(const volatile Snap *s) { return snap_version(s) != 0; }
#if defined(AERIAOS)
/* An effect nobody has turned a knob of starts DRY (its MIX held at 0), and PITCH at no shift: the rig the OS starts
 * with had DIST and RING fully wet and PITCH an octave up, and the weather moving them -- every sound came out
 * mangled until each was found and turned down (sadnoise, 12 Nov 2026). Turning MIX up brings an effect in. */
static void slot_gentle(int i)
{
    int row = preview_live_fx[slot_fx(i) * 8 + slot_track(i)];
    if (row <= 0 || row > FX_COUNT) return;
    for (int k = 1; k < 6; k++) if (slots[i].user[k] >= 0 && !knob_is_mix(fx_info[row - 1].knob[k])) return;   /* a knob the musician set: theirs (PITCH's SHFT, knob 0, was held by this very rule) */
    const FxInfo *fi = &fx_info[row - 1];
    for (int k = 0; k < 6; k++) {
        const char *n = fi->knob[k];
        if (knob_is_mix(n)) slots[i].user[k] = slots[i].value[k] = 0;
        if (fi->org == 0x34000u && k == 0) slots[i].user[k] = slots[i].value[k] = 24;
    }
}
#endif
static void snap_apply(const Snap *s)
{
    for (int p = 0; p < N_PAGES; p++) for (int k = 0; k < 12; k++) {
        if (!params[p][k].name[0]) continue;
        if (s->value[p][k] < params[p][k].lo) continue;   /* saved before this knob existed: its default stays */
        params[p][k].user = s->user[p][k];
        params[p][k].value = (int16_t)clamp(s->value[p][k], params[p][k].lo, params[p][k].hi);
    }
    for (int i = 0; i < 16; i++) preview_live_fx[i] = s->live_fx[i] <= FX_COUNT ? s->live_fx[i] : 0;
    for (int t = 0; t < 8; t++) for (int k = 0; k < 12; k++) preview_part_fx[t][k] = s->part_fx[t][k] & 0x7f;
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) slots[i].user[k] = s->slot_user[i][k];
#if defined(AERIAOS)
    if (s->magic != SNAP_MAGIC) for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);   /* saved before effects started dry (or while a weather change still undid it) */
#endif
    weather = clamp(s->weather, 0, N_WEATHER - 1); auto_weather = s->auto_weather != 0; wx_on = (uint8_t)((s->xf_right & 0x100) ? (s->xf_right & 0x7e) : (weather == CLEAR ? 0 : 1u << weather));
    if (WON(SNOW) && WON(HEAT)) wx_on &= (uint8_t)~(1u << HEAT);
    night = s->night != 0; src_loop = s->src_loop != 0; src_stars = s->src_stars != 0; src_balloons = s->src_balloons != 0;
    planet_mask = s->planet_mask; slot_bypass = s->slot_bypass;
    /* the FUNC-layer knobs: a snapshot from before they were saved gets each slot's effect's own defaults */
    for (int i = 0; i < N_SLOTS; i++) {
        int row = preview_live_fx[slot_fx(i) * 8 + slot_track(i)];
        for (int k = 0; k < 6; k++) {
            int d = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def2[k] : 0, c = row > 0 && row <= FX_COUNT ? fx_info[row - 1].count2[k] : 0;
            int v = *snap_p2((Snap *)(uintptr_t)s, i, k);
            preview_slot_p2[i][k] = (uint8_t)(s->magic != SNAP_MAGIC_V3 && s->magic != SNAP_MAGIC_V2 && v < (c ? c : 1) ? v : d);
        }
    }
    if (s->magic != SNAP_MAGIC_V2) {   /* the panners, the grain cloud, MARBLES, PONG: from AES3 on */
        for (int i = 0; i < N_SLOTS; i++) { pan_set[i] = s->pan[i]; if (pan_set[i].route >= N_ROUTES) pan_set[i].route = 0; if (pan_set[i].mode >= N_PMODES) pan_set[i].mode = 0; }
        pan_chan = s->pan_chan <= 16 ? s->pan_chan : 0; midi_mod_set(s->midi_mod & 3); cloud = s->cloud; cloud.shape %= N_SHAPES;
        mb = s->marb; mb.on = 0; if (!mb.len) mb.len = 8; if (!mb.chan) mb.chan = 1; if (!mb.rnge) mb.rnge = 2;
        pg = s->pong; if (pg.nballs > PONG_BALLS) pg.nballs = 0; for (int k = 0; k < pg.nballs; k++) pong_launch(k); if (!pg.chan) pg.chan = 1; if (!pg.size) pg.size = 2;
    }
}
static volatile Snap *snap_slot(int i) { return (volatile Snap *)(SNAP_BASE + SNAP_STRIDE * (unsigned)i); }
static void snap_store(int i, const Snap *s)
{
    volatile uint8_t *d = (volatile uint8_t *)snap_slot(i);
    const uint8_t *b = (const uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) d[k] = b[k];
}
static void snap_fetch(int i, Snap *s)
{
    const volatile uint8_t *d = (const volatile uint8_t *)snap_slot(i);
    uint8_t *b = (uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) b[k] = d[k];
}

/* the old store's snapshot i, if there is a good one, in today's layout (see SNAP_STRIDE) */
static int snap_import_old(int i, Snap *s)
{
    const volatile uint8_t *o = (const volatile uint8_t *)(SNAP_OLD_BASE + SNAP_OLD_STRIDE * (unsigned)i);
    const unsigned rows_old = (N_PAGES - 1) * 12u;
    unsigned old_size = (unsigned)sizeof(Snap) - 24u;
    uint32_t magic = (uint32_t)o[0] << 24 | (uint32_t)o[1] << 16 | (uint32_t)o[2] << 8 | o[3], sum = (uint32_t)o[4] << 24 | (uint32_t)o[5] << 16 | (uint32_t)o[6] << 8 | o[7];
    if (magic == SNAP_MAGIC_V2) old_size = SNAP_V2_SIZE - 24u;                /* the first kind ended before the panners */
    else if (magic != SNAP_MAGIC_V6 && magic != SNAP_MAGIC_V5 && magic != SNAP_MAGIC_V4 && magic != SNAP_MAGIC_V3) return 0;
    uint32_t h = 0x811c9dc5u;
    for (unsigned k = 8; k < old_size; k++) h = (h ^ o[k]) * 16777619u;
    if (h != sum) return 0;
    uint8_t *n = (uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) n[k] = 0;
    for (unsigned k = 0; k < rows_old; k++) { n[8 + k] = o[8 + k]; n[8 + rows_old + 12 + k] = o[8 + rows_old + k]; }      /* the settings, then the values */
    for (unsigned k = 8 + 2 * rows_old; k < old_size; k++) n[k + 24] = o[k];                                              /* all that follows the pages */
    for (int k = 0; k < 12; k++) { s->user[N_PAGES - 1][k] = (int8_t)params[N_PAGES - 1][k].user; s->value[N_PAGES - 1][k] = (uint8_t)params[N_PAGES - 1][k].value; }
    s->magic = magic; s->sum = snap_sum(s);
    return 1;
}
static void snap_bring_over(void)
{
    static Snap t;
    for (int i = 0; i <= N_PRESETS; i++) if (!snap_valid(snap_slot(i)) && snap_import_old(i, &t)) snap_store(i, &t);
}
/* the state: restored once at start, written when it has changed and then held still for two seconds */
static uint32_t state_sum, state_changed_at;
static Snap state_loaded;
static int state_had;
static int state_restored;
static void scenes_load(void);
void aeria_state_restore(void)
{
    snap_bring_over();
    scenes_load();
    if (snap_valid(snap_slot(0))) { snap_fetch(0, &state_loaded); snap_apply(&state_loaded); state_sum = state_loaded.sum; state_had = 1; }
#if defined(AERIAOS)
    else for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);
#endif
    state_restored = 1;
}
/* AERIA's slot set-up (tick 30) rebuilds each slot from the chain and its settings: the slots' own knob settings
 * go back on after it */
static void aeria_state_reapply_slots(void)
{
#if defined(AERIAOS)
    if (!state_had) { for (int i = 0; i < N_SLOTS; i++) slot_gentle(i); return; }      /* a first start: the rig comes up dry */
#endif
    if (!state_had) return;
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) slots[i].user[k] = state_loaded.slot_user[i][k];
#if defined(AERIAOS)
    if (state_loaded.magic != SNAP_MAGIC) for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);   /* saved before effects started dry */
#endif
}
/* the scenes: all sixteen and which two the fader holds, in battery RAM beside the snapshots; written a little after
 * they change */
#define SCENE_STORE ((uintptr_t)fake_scenes)
#define SCENE_MAGIC 0x41455343u   /* 'AESC' */
static uint32_t scene_dirty_at;
static void scenes_load(void)
{
    scenes_init();
    const volatile uint8_t *b = (const volatile uint8_t *)SCENE_STORE;
    uint32_t magic = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    if (magic != SCENE_MAGIC || b[4] != N_SCENES || ((unsigned)b[5] << 8 | b[6]) != SCENE_IDS) return;
    scene_a = (uint8_t)(b[7] % N_SCENES); scene_b = (uint8_t)(b[8] % N_SCENES);
    for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) scene_val[s][i] = (int8_t)b[16 + s * SCENE_IDS + i];
}
static void scenes_save(void)
{
    volatile uint8_t *b = (volatile uint8_t *)SCENE_STORE;
    b[0] = 'A'; b[1] = 'E'; b[2] = 'S'; b[3] = 'C'; b[4] = N_SCENES; b[5] = (uint8_t)(SCENE_IDS >> 8); b[6] = (uint8_t)SCENE_IDS; b[7] = scene_a; b[8] = scene_b;
    for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) b[16 + s * SCENE_IDS + i] = (uint8_t)scene_val[s][i];
}
void aeria_state_tick(void)
{
    if (scene_dirty) { scene_dirty = 0; scene_dirty_at = ticks | 1u; }
    if (scene_dirty_at && ticks - scene_dirty_at > 120u) { scene_dirty_at = 0; scenes_save(); }
    if (!state_restored || (ticks % 30u) != 0) return;
    snap_take(&snap_now);
    if (snap_now.sum == state_sum) { state_changed_at = 0; return; }
    if (!state_changed_at) { state_changed_at = ticks | 1u; return; }
    if (ticks - state_changed_at < 120u) return;
    snap_store(0, &snap_now);
    state_sum = snap_now.sum; state_changed_at = 0;
}

/* the presets page */
static int preset_mode, preset_cur = -1;
/* ---- PROJECTS (sadnoise, 12 Nov 2026): everything needed to play a piece again, under a name ---------------------
 * A project is the whole sound (every knob, the chain, the weathers, the master), the sixteen scenes and which samples
 * are loaded (the loop and the stars' folders; the sequences will join them). Sixteen of them, in battery RAM
 * (0x10040000, 32 KB each: room for the sequences). The working state is still kept by itself from moment to moment,
 * so switching the unit off and on brings back exactly what was there; a project is for coming back to a piece.
 *   PROJ          opens the page; NO or PROJ closes it
 *   a trig        picks a project (its name is shown)        YES          loads the picked one
 *   FUNC + YES    saves the sound into the picked one -- and asks for its name:
 * Typing a name is made for the sixteen trigs: 1-13 are the letters two by two (AB CD EF .. YZ) -- one press the
 * first, a second press at once the other --, 14 the digits (press again: the next), 15 space - _, 16 rubs out.
 * Any knob turns the letter under the cursor through them all; LEFT and RIGHT move the cursor; YES saves, NO gives up.
 * A new letter starts where the last one ended as soon as another trig is pressed or a moment has passed. */
#define PROJ_BASE ((uintptr_t)fake_proj)
#define PROJ_STRIDE 0x8000u
#define PROJ_MAGIC 0x4145504Au   /* 'AEPJ' */
#define N_PROJ 16
#define LOADED_AT ((uintptr_t)fake_loaded)
#define LOADED_SIZE 0x300u
void aos_samples_reload(void);
static int proj_sel, proj_cur = -1, naming;
static char name_buf[13];
static int name_at, name_last = -1, name_taps;
static uint32_t name_tick;
static volatile uint8_t *proj_at(int i) { return (volatile uint8_t *)(PROJ_BASE + PROJ_STRIDE * (unsigned)i); }
static int proj_saved(int i) { const volatile uint8_t *b = proj_at(i); return b[0] == 'A' && b[1] == 'E' && b[2] == 'P' && b[3] == 'J'; }
static void proj_name(int i, char *out) { const volatile uint8_t *b = proj_at(i); int k = 0; if (proj_saved(i)) for (; k < 12 && b[8 + k]; k++) out[k] = (char)b[8 + k]; out[k] = 0; }
static void proj_save(int i, const char *name)
{
    volatile uint8_t *b = proj_at(i);
    static Snap t; snap_take(&t);
    b[0] = 0;                                                        /* not a project while it is being written */
    for (int k = 0; k < 13; k++) b[8 + k] = (uint8_t)(k < 12 ? name[k] : 0);
    const uint8_t *sp = (const uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) b[0x40 + k] = sp[k];
    const volatile uint8_t *ld = (const volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) b[0x800 + k] = ld[k];
    b[0x1000] = scene_a; b[0x1001] = scene_b;
    for (int sc = 0; sc < N_SCENES; sc++) for (int q = 0; q < SCENE_IDS; q++) b[0x1010 + sc * SCENE_IDS + q] = (uint8_t)scene_val[sc][q];
#ifdef SEQ_UI
    { const volatile uint8_t *q = (const volatile uint8_t *)SEQ_BASE;       /* the sequences: the head, then every step */
      for (unsigned k = 0; k < SEQ_HEAD_SIZE; k++) b[0x1C00 + k] = q[k];
      for (unsigned k = 0; k < SEQ_STEPS_SIZE; k++) { b[0x2000 + k] = q[SEQ_HEAD_SIZE + k]; if (!(k & 255)) aos_breathe(); } }
    b[7] = 1;
#endif
    b[4] = 1; b[5] = (uint8_t)(SCENE_IDS >> 8); b[6] = (uint8_t)SCENE_IDS; b[1] = 'E'; b[2] = 'P'; b[3] = 'J'; b[0] = 'A';
    proj_cur = i;
}
static void proj_load(int i)
{
    if (!proj_saved(i)) return;
    const volatile uint8_t *b = proj_at(i);
    static Snap t; uint8_t *tp = (uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) tp[k] = b[0x40 + k];
    if (!snap_version(&t)) { say("PROJECT", "DAMAGED"); return; }
    snap_apply(&t);
    state_loaded = t; state_had = 1; aeria_state_reapply_slots();
    volatile uint8_t *ld = (volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) ld[k] = b[0x800 + k];
    aos_samples_reload();
    if (((unsigned)b[5] << 8 | b[6]) == SCENE_IDS) {
        scene_a = (uint8_t)(b[0x1000] % N_SCENES); scene_b = (uint8_t)(b[0x1001] % N_SCENES);
        for (int sc = 0; sc < N_SCENES; sc++) for (int q = 0; q < SCENE_IDS; q++) scene_val[sc][q] = (int8_t)b[0x1010 + sc * SCENE_IDS + q];
        scene_dirty = 1;
    }
#ifdef SEQ_UI
    if (b[7] == 1 && b[0x1C00] == 'A' && b[0x1C01] == 'E' && b[0x1C02] == 'S' && b[0x1C03] == 'Q') {
        volatile uint8_t *q = (volatile uint8_t *)SEQ_BASE;
        aos_seq_play(0);
        for (unsigned k = 0; k < SEQ_HEAD_SIZE; k++) q[k] = b[0x1C00 + k];
        for (unsigned k = 0; k < SEQ_STEPS_SIZE; k++) { q[SEQ_HEAD_SIZE + k] = b[0x2000 + k]; if (!(k & 255)) aos_breathe(); }
        seq_on = SQH->mode == 1; aos_seq_mode(0); if (seq_on) aos_seq_mode(1);
    }
#endif
    proj_cur = i;
}
/* the presets of before (snapshots 1..16) become projects once, named PRESET n */
static void proj_from_presets(void)
{
    for (int i = 0; i < N_PROJ; i++) if (!proj_saved(i) && snap_valid(snap_slot(1 + i))) {
        volatile uint8_t *b = proj_at(i); static Snap t; snap_fetch(1 + i, &t);
        char nm[13] = "PRESET "; num(nm + 7, i + 1);
        for (int k = 0; k < 13; k++) b[8 + k] = (uint8_t)nm[k];
        const uint8_t *sp = (const uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) b[0x40 + k] = sp[k];
        const volatile uint8_t *ld = (const volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) b[0x800 + k] = ld[k];
        b[5] = 0; b[6] = 0; b[4] = 1; b[1] = 'E'; b[2] = 'P'; b[3] = 'J'; b[0] = 'A';
    }
}
/* ---- typing a name ---- */
static const char name_extra[] = " -_";
static void name_put(char c) { if (name_at > 11) name_at = 11; name_buf[name_at] = c; int n = 0; while (n < 12 && name_buf[n]) n++; if (name_at >= n) name_buf[name_at + 1] = 0; }
static void name_step(void) { if (name_last >= 0) { if (name_at < 11) name_at++; name_last = -1; name_taps = 0; } }
static void name_trig(int k)
{
    if (k == 15) {                                                   /* rub out */
        name_last = -1; name_taps = 0;
        if (name_at > 0 && !name_buf[name_at]) name_at--;
        for (int q = name_at; q < 12; q++) name_buf[q] = name_buf[q + 1];
        name_buf[12] = 0; return;
    }
    if (k != name_last) name_step(); 
    else if (ticks - name_tick > 50u) name_step();
    name_taps = (k == name_last) ? name_taps + 1 : 0;
    name_last = k; name_tick = ticks;
    char c = k < 13 ? (char)('A' + 2 * k + (name_taps & 1)) : k == 13 ? (char)('0' + name_taps % 10) : name_extra[name_taps % 3];
    name_put(c);
}
static void name_turn(int d)
{
    static const char all[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";
    name_last = -1; name_taps = 0;
    char c = name_buf[name_at] ? name_buf[name_at] : 'A'; int q = 0;
    for (int k = 0; all[k]; k++) if (all[k] == c) q = k;
    q = (q + (d > 0 ? 1 : -1) + 39) % 39;
    name_put(all[q]);
}
static int preset_key(int code, int pressed)
{
    if (!pressed) return preset_mode;
    if (naming) {
        if (code >= KEY_TRIG0 && code < KEY_TRIG0 + 16) { name_trig(code - KEY_TRIG0); return 1; }
        if (code == KEY_LEFT) { name_last = -1; if (name_at > 0) name_at--; return 1; }
        if (code == KEY_RIGHT) { name_last = -1; if (name_at < 11 && name_buf[name_at]) name_at++; return 1; }
        if (code == KEY_NO) { naming = 0; say("NOT SAVED", 0); return 1; }
        if (code == KEY_YES) {
            int n = 0; while (n < 12 && name_buf[n]) n++;
            while (n > 0 && name_buf[n - 1] == ' ') name_buf[--n] = 0;
            if (!n) { char d[13] = "PROJECT "; num(d + 8, proj_sel + 1); for (int k = 0; k < 13; k++) name_buf[k] = d[k]; }
            proj_save(proj_sel, name_buf); naming = 0; say("SAVED", name_buf); return 1;
        }
        return code != KEY_FUNC;
    }
    if (code == KEY_PROJ) { preset_mode = !preset_mode; if (preset_mode) { proj_from_presets(); if (proj_cur >= 0) proj_sel = proj_cur; } say(preset_mode ? "PROJECTS" : page_name[page], 0); return 1; }
    if (!preset_mode) return 0;
    if (code == KEY_NO) { preset_mode = 0; say(page_name[page], 0); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + N_PROJ) { proj_sel = code - KEY_TRIG0; return 1; }
    if (code == KEY_YES && func_held) {                              /* save here: its name first */
        for (int k = 0; k < 13; k++) name_buf[k] = 0;
        proj_name(proj_sel, name_buf);
        name_at = 0; while (name_at < 11 && name_buf[name_at]) name_at++;
        name_last = -1; name_taps = 0; naming = 1; return 1;
    }
    if (code == KEY_YES) { if (proj_saved(proj_sel)) { char n[13]; proj_name(proj_sel, n); proj_load(proj_sel); say("LOADED", n); preset_mode = 0; } else say("EMPTY", "FUNC+YES SAVES"); return 1; }
    return code != KEY_FUNC;   /* the page holds every other key while it is open */
}
static int preset_enc(int d) { if (naming) { name_turn(d); return 1; } return preset_mode; }
static void preset_draw(void)
{
    for (int x = 4; x < W - 4; x++) span(x, 6, 58, 0);
    for (int x = 4; x < W - 4; x++) { px(x, 6); px(x, 58); }
    if (naming) {
        text(8, 9, "NAME", 1); char n[4]; num(n, proj_sel + 1); text(30, 9, n, 1);
        text(W - 8 - text_w("YES SAVES"), 9, "YES SAVES", 1);
        for (int k = 0; k < 12; k++) {                                /* the name, twice the size, a line under the cursor */
            int x = 10 + k * 9; char c[2] = {name_buf[k] ? name_buf[k] : ' ', 0};
            if (c[0] != ' ') { int g = glyph_of(c[0]); if (g >= 0) for (int r = 0; r < 5; r++) for (int q = 0; q < 3; q++) if (glyphs[g] & (1u << (14 - (r * 3 + q)))) { px(x + 2 * q, 18 + 2 * r); px(x + 2 * q + 1, 18 + 2 * r); px(x + 2 * q, 19 + 2 * r); px(x + 2 * q + 1, 19 + 2 * r); } }
            if (k == name_at && ((ticks >> 4) & 1)) for (int q = 0; q < 7; q++) px(x + q, 30);
            else if (!name_buf[k] || k > name_at) px(x + 3, 30);
        }
        text(8, 36, "AB CD EF GH IJ KL MN OP", 1);
        text(8, 44, "QR ST UV WX YZ 09 _  DEL", 1);
        text(8, 52, "KNOB: ANY LETTER", 1);
        return;
    }
    text(8, 9, "PROJECTS", 1);
    for (int i = 0; i < N_PROJ; i++) {
        int bx = 10 + (i % 8) * 14, by = 17 + (i / 8) * 12;
        for (int x = bx; x < bx + 12; x++) { px(x, by); px(x, by + 9); }
        for (int y = by; y <= by + 9; y++) { px(bx, y); px(bx + 11, y); }
        if (proj_saved(i)) for (int x = bx + 2; x < bx + 10; x++) for (int y = by + 2; y < by + 8; y++) if (i == proj_cur || ((x + y) & 1)) px(x, y);
        if (i == proj_sel) { for (int x = bx - 1; x < bx + 13; x++) { px(x, by - 1); px(x, by + 10); } for (int y = by - 1; y <= by + 10; y++) { px(bx - 1, y); px(bx + 12, y); } }
    }
    char n[13], num2[4]; num(num2, proj_sel + 1); text(8, 43, num2, 1);
    if (proj_saved(proj_sel)) { proj_name(proj_sel, n); text(22, 43, n, 1); text(8, 51, "YES LOADS  FUNC+YES SAVES", 1); }
    else { text(22, 43, "EMPTY", 1); text(8, 51, "FUNC+YES SAVES HERE", 1); }
}
static void preset_leds(void)
{
    if (naming) { for (int k = 0; k < 16; k++) led_want[k] = k == name_last ? 2 : 1; return; }
    for (int k = 0; k < 16; k++) led_want[k] = k == proj_sel ? 2 : proj_saved(k) ? 1 : 0;
}
#endif
/* ---- SEQUENCER: AERIA OS's second face (sadnoise, 12 Nov 2026) -----------------------------------------------------
 * FUNC + MIDI goes between the multi-effect (the sky) and the sequencer. Eight tracks, each its own channel into the
 * DSP: a sample or a machine of its own (SUB, FM, NOIS, PLUK; MIDI alone), a high-pass and a low-pass that are always there (resonance
 * fixed at 20 %), one effect, its place in the stereo field. The sound side is aeria-os/src/seq.c; the data seq.h.
 *
 *   T1-T8         the track           FUNC + T  mute it        CUE + T  solo it
 *   trigs         GRID (REC dark): the sixteen steps of the page -- a tap sets or clears one; HOLD one (or several)
 *                 and turn a knob: that step's own value (a parameter lock); while held the knobs are the step's
 *                 VEL MICR PROB COND RTCH CURV, YES turns it backward, and a page key (SRC AMP FX1 MIDI) brings that
 *                 page's knobs to lock.
 *                 LIVE (REC lit): 1-8 play the tracks (written into the pattern while it runs), 9-16 mute them
 *   LEFT / RIGHT  the page of sixteen steps (a track is up to 64 long)     UP / DOWN  the track
 *   PLAY / STOP   run / stop (STOP again: silence)        REC  GRID / LIVE
 *   SRC AMP LFO FX1 FX2   the knob pages: SRC (sample), AMP, MOD, FLT (filters, delay send), FX (the track's effect;
 *                 YES opens the list)          PAGE  PTN: lengths, swing, tempo, the ducking
 *   PATTERN + trig 1-4   the pattern (it changes at the bar); several in a row while held: a chain
 *   BANK          the mixer (meters; trigs 1-8 mute, 9-16 solo)
 *   LEVEL         the track's volume       TEMPO  tap        FUNC + PLAY  clear the track        FUNC + YES (SRC)  load a kit
 *   MIX           the master page (freeze delay), any page key comes back */
#if defined(AERIAOS) && (defined(__m68k__) || defined(SEQ_HOST))
static int layer(void);
static void upper(const char *in, char *out, int max);
enum { SV_GRID, SV_MIXER, SV_MASTER };
enum { SQP_SRC, SQP_AMP, SQP_MOD, SQP_FLT, SQP_GEN, SQP_MIDI, SQP_FX, SQP_PTN, SQP_STEP, SQP_N };   /* SRC..MIDI can be locked on a step */
static const char *const sq_page_name[SQP_N] = {"SRC", "AMP", "MOD", "FLT", "GEN", "MIDI", "FX", "PTN", "STEP"};
enum { SP_NONE = -1, SP_LEN = -2, SP_MLEN = -3, SP_SWNG = -4, SP_TMPO = -5, SP_DSRC = -6, SP_DREL = -7,
       SP_VEL = -8, SP_MICR = -9, SP_PROB = -10, SP_COND = -11, SP_RTCH = -12, SP_SREV = -13, SP_CURV = -14 };
enum { SP_SYNC = -3 - 100, SP_PCCH = -4 - 100 };   /* (kept apart from the step's own: these are the pattern page's kind) */
typedef struct { const char *lab; int8_t tp; uint8_t lo, hi; } SqP;
static const SqP sq_par[SQP_N][12] = {
    {{"SMPL", TP_SMPL, 0, 127}, {"PTCH", TP_PTCH, 28, 100}, {"STRT", TP_STRT, 0, 127}, {"REV", TP_REV, 0, 1}, {"MACH", TP_MACH, 0, SQM_N - 1}, {"SLIP", TP_SLIP, 0, 127}, {"RTIO", TP_FMR, 1, 127}, {"INDX", TP_FMI, 0, 127}, {"IDEC", TP_FMD, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"ATK", TP_ATK, 0, 127}, {"DEC", TP_DEC, 0, 127}, {"VOL", TP_VOL, 0, 127}, {"PAN", TP_PAN, 0, 127}, {"DRIV", TP_DRIV, 0, 127}, {"BITS", TP_BITS, 0, 12}, {"PENV", TP_PENV, 0, 127}, {"PDEC", TP_PDEC, 0, 127}, {"DUCK", TP_DUCK, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"WAVE", TP_MODW, 0, 2}, {"DEST", TP_LFOD, 0, 7}, {"RATE", TP_LFOR, 0, 127}, {"DPTH", TP_LFOA, 0, 127}, {"PENV", TP_PENV, 0, 127}, {"PDEC", TP_PDEC, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"HP", TP_HP, 0, 127}, {"LP", TP_LP, 0, 127}, {"DLY", TP_DLY, 0, 127}, {"OUT", TP_OUT, 0, 2}, {"DUCK", TP_DUCK, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"DENS", TP_DENS, 0, 127}, {"DIR", TP_DIR, 0, 4}, {"ROT", TP_ROT, 0, 63}, {"WARP", TP_WARP, 0, 127}, {"RATE", TP_RATE, 0, SQ_RATES - 1}, {"SHUF", TP_SHUF, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"MCH", TP_MCH, 0, 16}, {"NOTE", TP_MNOT, 0, 127}, {"CHRD", TP_CHRD, 0, SQ_CHORDS - 1}, {"GATE", TP_GATE, 1, 127}, {"PROG", TP_PROG, 0, 128}, {"", SP_NONE, 0, 0},
     {"SYNC", SP_SYNC, 0, 2}, {"PCCH", SP_PCCH, 0, 16}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"LEN", SP_LEN, 1, 64}, {"PLEN", SP_MLEN, 1, 64}, {"SWNG", SP_SWNG, 50, 80}, {"TMPO", SP_TMPO, 60, 187}, {"DSRC", SP_DSRC, 0, 7}, {"DREL", SP_DREL, 0, 127}, {"FREE", TP_FREE, 0, 1}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"VEL", SP_VEL, 1, 127}, {"MICR", SP_MICR, 0, 46}, {"PROB", SP_PROB, 1, 100}, {"COND", SP_COND, 0, SQ_COND_N - 1}, {"RTCH", SP_RTCH, 0, 13}, {"CURV", SP_CURV, 0, 14}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
};
/* a ratchet's hits: 1..8, then rolls so fast they turn into a pitch */
static const uint8_t sq_rtch[14] = {1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32, 48, 64};
static int sq_rtch_i(int r) { int i = 0; while (i < 13 && sq_rtch[i] < r) i++; return i; }
static uint8_t sq_page, sq_view, sq_rec, sq_spage, sq_pat_held, sq_cue_held, sq_fxlist, sq_fxrow, sq_turned, sq_lockpage = SQP_STEP, sq_restored;
static uint16_t sq_held;                 /* the trigs held in GRID */
static uint8_t sq_chain_n, sq_chain[8], sq_tempo_held;
static SqStep sq_clip[SQ_TRACKS][SQ_STEPS]; static uint8_t sq_clip_len[SQ_TRACKS], sq_clip_kind;   /* copied: 1 a track, 2 a pattern */
static int sq_sel(void) { return SQH->sel & 7; }
static int sq_pat(void) { return SQH->pattern & 3; }
static int sq_len(int t) { int l = SQH->len[sq_pat()][t & 7]; return l >= 1 && l <= SQ_STEPS ? l : 16; }
static void sq_tempo_set(int bpm) { Param *p = &P(PG_MASTER, 6); p->value = p->user = (int16_t)(clamp(bpm, 60, 187) - 60); }

/* ---- what a value reads as ---- */
static void sq_cat(char *n, const char *s) { int k = 0; while (n[k]) k++; while (*s && k < 11) n[k++] = *s++; n[k] = 0; }
static int sq_hz(int i) { uint32_t f = 20u << 8; for (int k = 0; k < i; k++) f += ((f >> 6) * 3663u) >> 10; return (int)(f >> 8); }
static void sq_cond_text(int c, char *n)
{
    static const char *const sp[8] = {"FILL", "!FILL", "1ST", "!1ST", "PRE", "!PRE", "NEI", "!NEI"};
    if (!c) { n[0] = '-'; n[1] = 0; return; }
    if (c <= SQ_COND_AB) { int b = 2, k = c - 1; while (k >= b) { k -= b; b++; } n[0] = (char)('1' + k); n[1] = ':'; n[2] = (char)('0' + b); n[3] = 0; return; }
    n[0] = 0; sq_cat(n, sp[clamp(c - SQC_FILL, 0, 7)]);
}
static void sq_text(int tp, int v, char *n)
{
    n[0] = 0;
    switch (tp) {
    case TP_SMPL: num(n, v / 16 + 1); sq_cat(n, "."); { char d[4]; num(d, v % 16 + 1); sq_cat(n, d); } break;
    case TP_PTCH: if (v > 64) { n[0] = '+'; num(n + 1, v - 64); } else num(n, v - 64); break;
    case TP_PAN: if (v == 64) sq_cat(n, "C"); else { n[0] = v < 64 ? 'L' : 'R'; num(n + 1, v < 64 ? 64 - v : v - 64); } break;
    case TP_HP: if (!v) sq_cat(n, "OFF"); else { int h = sq_hz(v); if (h >= 1000) { num(n, h / 1000); sq_cat(n, "K"); { char d[4]; num(d, h % 1000 / 100); sq_cat(n, d); } } else num(n, h); } break;
    case TP_LP: if (v >= 127) sq_cat(n, "OPEN"); else { int h = sq_hz(v); if (h >= 1000) { num(n, h / 1000); sq_cat(n, "K"); { char d[4]; num(d, h % 1000 / 100); sq_cat(n, d); } } else num(n, h); } break;
    case TP_REV: case TP_FREE: case SP_SREV: sq_cat(n, v ? "ON" : "OFF"); break;
    case TP_MACH: { static const char *const m[SQM_N] = {"SMPL", "SUB", "FM", "NOIS", "PLUK", "MIDI"}; sq_cat(n, m[clamp(v, 0, SQM_N - 1)]); } break;
    case TP_OUT: sq_cat(n, v == 1 ? "CUE" : v == 2 ? "BOTH" : "MAIN"); break;
    case TP_LFOD: { static const char *const d[8] = {"OFF", "LP", "HP", "VOL", "PAN", "PTCH", "STRT", "INDX"}; sq_cat(n, d[clamp(v, 0, 7)]); } break;
    case TP_MODW: { static const char *const w[3] = {"TRI", "RND", "ENV"}; sq_cat(n, w[clamp(v, 0, 2)]); } break;
    case TP_DIR: { static const char *const d[5] = {"FWD", "REV", "PONG", "RND", "DRNK"}; sq_cat(n, d[clamp(v, 0, 4)]); } break;
    case TP_RATE: { static const char *const r[SQ_RATES] = {"1/4", "1/3", "1/2", "2/3", "3/4", "X1", "4/3", "3/2", "X2", "X3", "X4"}; sq_cat(n, r[clamp(v, 0, SQ_RATES - 1)]); } break;
    case TP_WARP: if (v == 64) sq_cat(n, "-"); else { n[0] = v > 64 ? '+' : '-'; num(n + 1, v > 64 ? v - 64 : 64 - v); } break;
    case TP_FMR: num(n, v / 16); sq_cat(n, "."); { char d[4]; num(d, (v % 16) * 10 / 16); sq_cat(n, d); } break;
    case TP_DENS: num(n, (v + 1) * 100 / 128); sq_cat(n, "%"); break;
    case SP_RTCH: num(n, sq_rtch[clamp(v, 0, 13)]); break;
    case SP_CURV: if (v == 7) sq_cat(n, "EVEN"); else { sq_cat(n, v > 7 ? "IN" : "OUT"); char d[4]; num(d, v > 7 ? v - 7 : 7 - v); sq_cat(n, d); } break;
    case TP_MCH: if (!v) sq_cat(n, "OFF"); else num(n, v); break;
    case TP_BITS: if (!v) sq_cat(n, "OFF"); else num(n, 16 - v); break;
    case TP_SHUF: if (!v) sq_cat(n, "PTN"); else { num(n, 50 + v * 30 / 127); sq_cat(n, "%"); } break;
    case TP_DEC: if (v >= 127) sq_cat(n, "FULL"); else num(n, v); break;
    case SP_SWNG: num(n, v); sq_cat(n, "%"); break;
    case SP_DSRC: n[0] = 'T'; num(n + 1, v + 1); break;
    case TP_CHRD: { static const char *const c[SQ_CHORDS] = {"NOTE", "OCT", "5TH", "MAJ", "MIN", "SUS2", "SUS4", "MAJ7", "MIN7", "DOM7", "DIM", "AUG", "MIN9", "4THS", "CLUS"}; sq_cat(n, c[clamp(v, 0, SQ_CHORDS - 1)]); } break;
    case TP_GATE: { int g = v ? v : 4; if (g % 8 == 0) num(n, g / 8); else { num(n, g); sq_cat(n, "/8"); } } break;
    case TP_PROG: case SP_PCCH: if (!v) sq_cat(n, "OFF"); else num(n, v); break;
    case SP_SYNC: sq_cat(n, v == 1 ? "SEND" : v == 2 ? "RECV" : "OFF"); break;
    case TP_MNOT: { static const char *const nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; sq_cat(n, nn[v % 12]); char d[4]; num(d, v / 12 - 1); sq_cat(n, d); } break;
    case SP_MICR: if (v > 23) { n[0] = '+'; num(n + 1, v - 23); } else num(n, v - 23); break;
    case SP_PROB: num(n, v); sq_cat(n, "%"); break;
    case SP_COND: sq_cond_text(v, n); break;
    default: num(n, v);
    }
}

/* ---- the effect of a track: which, on which core, its knobs ---- */
/* Measured on the unit (12 Nov 2026), eight pitched tracks sounding on every step with all sixteen filters on: core 0
 * is at 64-80 % before any effect, so the effects go to core 1 first (1,257 instructions a sample of them ran there
 * with none late) and core 0 keeps room for the two that can only run there (519 together: 95 % in the worst frame). */
#define SQ_ROOM0 560
#define SQ_ROOM1 1300
static int sq_fx_place(int t, int row)   /* 0, 1: the core; -1: no room; -2: that effect is on another track */
{
    if (row <= 0) return 0;
    const SqHead *h = SQH;
    int use0 = 0, use1 = 0;
    for (int k = 0; k < SQ_TRACKS; k++) if (k != t && h->fx_row[k] && h->fx_row[k] <= FX_COUNT) {
        if (h->fx_row[k] == row && fx_info[row - 1].has_y) return -2;
        /* the effects in the shared window (PITCH, SMEAR) and the ones written for one place keep their state where the code says */
        if (h->fx_core[k] == 1) use1 += fx_cost[h->fx_row[k] - 1]; else use0 += fx_cost[h->fx_row[k] - 1];
    }
    int c = fx_cost[row - 1], pref = fx_core[row - 1];
    if (pref != 0 && use1 + c <= SQ_ROOM1) return 1;
    if (pref != 1 && use0 + c <= SQ_ROOM0) return 0;
    return -1;
}
static void sq_fx_pick(int t, int row)
{
    SqHead *h = SQH;
    int c = sq_fx_place(t, row);
    if (c == -2) { say("ON ANOTHER", "TRACK"); return; }
    if (c < 0) { say("NO ROOM", "DSP FULL"); return; }
    h->fx_row[t] = 0;
    if (row > 0) {
        const FxInfo *fi = &fx_info[row - 1];
        for (int k = 0; k < 6; k++) { h->fx_knob[t][k] = fi->def[k]; h->fx_knob[t][6 + k] = fi->def2[k]; }
        h->fx_core[t] = (uint8_t)c; h->fx_row[t] = (uint8_t)row;
        say(fi->name + 6, 0);
    } else say("NO EFFECT", 0);
}
static void sq_fx_text(int t, int k, int v, char *n)
{
    int row = SQH->fx_row[t]; const char *s = 0;
    if (row > 0 && row <= FX_COUNT) {
        const FxInfo *fi = &fx_info[row - 1];
        if (k < 6) { if (fi->text[k] && v < fi->count[k]) s = fi->text[k][v]; }
        else if (fi->text2[k - 6] && v < fi->count2[k - 6]) s = fi->text2[k - 6][v];
    }
    if (!s) { num(n, v); return; }
    int i = 0; while (s[i] && i < 10) { n[i] = s[i]; i++; } n[i] = 0;
}

/* ---- reading and writing a knob ---- */
static int sq_lockable(int pg) { return pg == SQP_SRC || pg == SQP_AMP || pg == SQP_FLT || pg == SQP_MIDI; }   /* MOD and GEN are the whole track's */
static int sq_shown_page(void) { return sq_held ? sq_lockpage : sq_page; }
static int sq_first_held(void) { for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) return sq_spage * 16 + k; return -1; }
static int sq_lock_of(const SqStep *s, int tp) { for (int k = 0; k < SQ_LOCKS; k++) if (s->lp[k] == tp + 1) return s->lv[k]; return -1; }
static int sq_get(int pg, int i)
{
    const SqHead *h = SQH; int t = sq_sel();
    const SqP *p = &sq_par[pg][i];
    int st = sq_first_held();
    const SqStep *s = st >= 0 ? SQ_STEP(sq_pat(), t, st) : 0;
    if (pg == SQP_FX) return h->fx_knob[t][i];
    switch (p->tp) {
    case SP_NONE: return 0;
    case SP_LEN: return sq_len(t);
    case SP_MLEN: return h->mlen[sq_pat()] ? h->mlen[sq_pat()] : 16;
    case SP_SWNG: return clamp(h->swing, 50, 80);
    case SP_TMPO: return master_bpm();
    case SP_DSRC: return h->duck_src & 7;
    case SP_DREL: return h->duck_rel;
    case SP_SYNC: return h->sync > 2 ? 0 : h->sync;
    case SP_PCCH: return h->pcch > 16 ? 0 : h->pcch;
    case SP_VEL: return s ? (s->vel ? s->vel : 100) : 100;
    case SP_MICR: return s ? s->micro + 23 : 23;
    case SP_PROB: return s && s->prob ? s->prob : 100;
    case SP_COND: return s ? s->cond : 0;
    case SP_RTCH: return s ? sq_rtch_i(s->ratch) : 0;
    case SP_CURV: return s ? SQ_CURVE(s->flags) + 7 : 7;
    case SP_SREV: return s ? (s->flags & SF_REV) != 0 : 0;
    }
    if (s) { int l = sq_lock_of(s, p->tp); if (l >= 0) return l; }
    return *sq_tp(t, p->tp);
}
static void sq_say(int pg, int i, int v)
{
    static char n[14]; const SqP *p = &sq_par[pg][i];
    if (pg == SQP_FX) { int row = SQH->fx_row[sq_sel()]; if (row <= 0 || row > FX_COUNT) return; sq_fx_text(sq_sel(), i, v, n); say(i < 6 ? fx_info[row - 1].knob[i] : fx_info[row - 1].knob2[i - 6], n); return; }
    sq_text(p->tp, v, n); say(p->lab, n);
}
/* a step's own value for a track parameter: one of its three locks */
static int sq_lock_set(SqStep *s, int tp, int v)
{
    int free_k = -1;
    for (int k = 0; k < SQ_LOCKS; k++) { if (s->lp[k] == tp + 1) { s->lv[k] = (uint8_t)v; return 1; } if (!s->lp[k] && free_k < 0) free_k = k; }
    if (free_k < 0) return 0;
    s->lp[free_k] = (uint8_t)(tp + 1); s->lv[free_k] = (uint8_t)v;
    return 1;
}
static void sq_turn(int pg, int i, int delta)
{
    SqHead *h = SQH; int t = sq_sel();
    if (pg == SQP_FX) {
        int row = h->fx_row[t]; if (row <= 0 || row > FX_COUNT) { say("NO EFFECT", "YES PICKS"); return; }
        const FxInfo *fi = &fx_info[row - 1];
        if (i >= 6 && !fi->knob2[i - 6][0]) return;
        int c = i < 6 ? fi->count[i] : fi->count2[i - 6]; if (c < 2) c = 128;
        int v = clamp(h->fx_knob[t][i] + (c <= 16 ? (delta > 0 ? 1 : -1) : delta), 0, c - 1);
        h->fx_knob[t][i] = (uint8_t)v; sq_say(pg, i, v);
        return;
    }
    const SqP *p = &sq_par[pg][i];
    if (p->tp == SP_NONE) return;
    int small = p->hi - p->lo <= 16;
    int v = clamp(sq_get(pg, i) + (small ? (delta > 0 ? 1 : -1) : delta), p->lo, p->hi);
    if ((p->tp <= SP_VEL && p->tp > -100) || (sq_held && p->tp >= 0 && sq_lockable(pg))) {
        /* the held steps: their own attributes, or a lock */
        if (!sq_held) return;
        sq_turned = 1;
        for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) {
            SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k);
            s->flags |= SF_ON;
            switch (p->tp) {
            case SP_VEL: s->vel = (uint8_t)v; break;
            case SP_MICR: s->micro = (int8_t)(v - 23); break;
            case SP_PROB: s->prob = (uint8_t)(v >= 100 ? 0 : v); break;
            case SP_COND: s->cond = (uint8_t)v; break;
            case SP_RTCH: s->ratch = (uint8_t)(v <= 0 ? 0 : sq_rtch[v]); break;
            case SP_CURV: s->flags = (uint8_t)((s->flags & 0x0F) | (((v - 7) & 15) << 4)); break;
            case SP_SREV: if (v) s->flags |= SF_REV; else s->flags &= (uint8_t)~SF_REV; break;
            default: if (!sq_lock_set(s, p->tp, v)) { say("3 LOCKS", "A STEP"); return; }
            }
        }
        sq_say(pg, i, v);
        return;
    }
    switch (p->tp) {
    case SP_LEN: h->len[sq_pat()][t] = (uint8_t)v; if (sq_spage * 16 >= v) sq_spage = (uint8_t)((v - 1) / 16); break;
    case SP_MLEN: h->mlen[sq_pat()] = (uint8_t)v; break;
    case SP_SWNG: h->swing = (uint8_t)v; break;
    case SP_TMPO: sq_tempo_set(v); break;
    case SP_DSRC: h->duck_src = (uint8_t)v; break;
    case SP_DREL: h->duck_rel = (uint8_t)v; break;
    case SP_SYNC: h->sync = (uint8_t)v; break;
    case SP_PCCH: h->pcch = (uint8_t)v; break;
    default:
        *sq_tp(t, p->tp) = (uint8_t)v;
        /* LIVE and running: the knob's move is written into the step going by */
        if (sq_rec && aos_seq_playing()) { SqStep *s = SQ_STEP(sq_pat(), t, aos_seq_step(t)); if (s->flags & SF_ON) sq_lock_set(s, p->tp, v); }
    }
    sq_say(pg, i, v);
}

/* ---- in and out of the sequencer ---- */
static void browser_open(int mode);
static void seq_enter(int on)
{
    seq_on = (uint8_t)(on != 0);
    aos_seq_mode(seq_on);
    sq_view = SV_GRID; sq_held = 0; sq_fxlist = 0; sq_pat_held = 0; func_latch = 0;
    for (int id = 32; id < 126; id++) set_led(id, 0);
    say(seq_on ? "SEQUENCER" : "AERIA", 0);
}
static void seq_ui_tick(void)
{
    if (!sq_restored && ticks > 45) {                              /* as it was left: battery RAM remembers */
        sq_restored = 1;
        const SqHead *h = SQH;
        if (h->magic[0] == 'A' && h->magic[1] == 'E' && h->magic[2] == 'S' && h->magic[3] == 'Q' && h->mode == 1) { seq_on = 1; aos_seq_mode(1); }
    }
    aos_seq_tempo(master_bpm() * 10);
    aos_seq_cushion(br_mode || preset_mode);
    if (seq_on && !aos_seq_is()) aos_seq_mode(1);
}

/* ---- keys ---- */
static void sq_step_tap(int n)
{
    SqStep *s = SQ_STEP(sq_pat(), sq_sel(), n);
    if (s->flags & SF_ON) { for (unsigned k = 0; k < sizeof(SqStep); k++) ((uint8_t *)s)[k] = 0; }
    else { s->flags = SF_ON; s->vel = 100; }
}
static int seq_key(int code, int pressed)
{
    if (code == KEY_MIDI && func_held) { if (pressed) seq_enter(!seq_on); return 1; }
    if (!seq_on) return 0;
    if (code == KEY_MIDI && !pressed) return 1;
    if (preset_mode || code == KEY_PROJ || br_mode || code == KEY_FUNC || code == KEY_SCENEA || code == KEY_SCENEB) return 0;
    SqHead *h = SQH; int t = sq_sel();
    if (sq_view == SV_MASTER) {
        if (pressed && (code == 0x30 || code == KEY_SRC || code == KEY_AMP || code == KEY_LFO || code == KEY_FX1 || code == KEY_FX2 || code == KEY_PAGE || code == KEY_BANK || code == KEY_ARR || code == KEY_MIDI)) { sq_view = SV_GRID; say("SEQUENCER", 0); return 1; }
        else if (code == KEY_PLAY || code == KEY_STOP) { /* the transport still runs */ }
        else return 0;
    }
    if (code == KEY_YES && !pressed) { aos_seq_fill(0); return 1; }
    if (code == KEY_PATTERN) { sq_pat_held = (uint8_t)pressed; if (pressed) { sq_chain_n = 0; say("PATTERN", "TRIG 1-4"); } else if (sq_chain_n > 1) { h->chain_n = sq_chain_n; for (int k = 0; k < 8; k++) h->chain[k] = sq_chain[k]; say("CHAIN", "SET"); } return 1; }
    if (code == KEY_CUE) { sq_cue_held = (uint8_t)pressed; return 1; }
    if (code == 0x18) { sq_tempo_held = (uint8_t)pressed; if (pressed) master_tap(); return 1; }   /* TEMPO: tap it; hold it and turn LEVEL */
    /* the list of effects */
    if (sq_fxlist) {
        if (!pressed) return 1;
        if (code == KEY_UP && sq_fxrow > 0) sq_fxrow--;
        else if (code == KEY_DOWN && sq_fxrow < FX_COUNT) sq_fxrow++;
        else if (code == KEY_YES) { sq_fx_pick(t, sq_fxrow); sq_fxlist = 0; }
        else if (code == KEY_NO) sq_fxlist = 0;
        return 1;
    }
    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (sq_pat_held) {
            if (pressed && k < SQ_PATTERNS) { h->pattern = (uint8_t)k; h->chain_n = 0; if (sq_chain_n < 8) sq_chain[sq_chain_n++] = (uint8_t)k; static char n[2]; n[0] = (char)('A' + k); n[1] = 0; say("PATTERN", n); }
            return 1;
        }
        if (sq_view == SV_MIXER) {
            if (pressed) { if (k < 8) h->mutes ^= (uint8_t)(1u << k); else h->solo ^= (uint8_t)(1u << (k - 8)); }
            return 1;
        }
        if (sq_rec) {
            if (pressed) { if (k < 8) aos_seq_hit(k, 100, 1); else h->mutes ^= (uint8_t)(1u << (k - 8)); }
            return 1;
        }
        int n = sq_spage * 16 + k;
        if (n >= sq_len(t)) return 1;
        if (pressed) { if (!sq_held) { sq_turned = 0; sq_lockpage = SQP_STEP; } sq_held |= (uint16_t)(1u << k); }
        else {
            if ((sq_held >> k) & 1) { sq_held &= (uint16_t)~(1u << k); if (!sq_turned) sq_step_tap(n); }
        }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) {
        int k = code - KEY_TRACK0;
        if (!pressed) return 1;
        static char n[4]; n[0] = 'T'; n[1] = (char)('1' + k); n[2] = 0;
        if (func_held) { h->mutes ^= (uint8_t)(1u << k); say(n, (h->mutes >> k) & 1 ? "MUTED" : "ON"); }
        else if (sq_cue_held) { h->solo ^= (uint8_t)(1u << k); say(n, (h->solo >> k) & 1 ? "SOLO" : "SOLO OFF"); }
        else { h->sel = (uint8_t)k; sq_held = 0; if (sq_spage * 16 >= sq_len(k)) sq_spage = 0; if (!aos_seq_playing()) aos_seq_hit(k, 100, 0); }   /* stopped: the track is heard as it is picked */
        return 1;
    }
    if (!pressed) return 1;
    switch (code) {
    case KEY_PLAY:
        if (func_held) { for (int n = 0; n < SQ_STEPS; n++) { SqStep *s = SQ_STEP(sq_pat(), t, n); for (unsigned k = 0; k < sizeof(SqStep); k++) ((uint8_t *)s)[k] = 0; } say("TRACK", "CLEARED"); }
        else { aos_seq_play(1); say("PLAY", 0); }
        return 1;
    case KEY_STOP:
        if (func_held) {                                           /* paste what was copied */
            int p = sq_pat();
            if (sq_clip_kind == 1) { for (int n = 0; n < SQ_STEPS; n++) *SQ_STEP(p, t, n) = sq_clip[0][n]; h->len[p][t] = sq_clip_len[0]; say("TRACK", "PASTED"); }
            else if (sq_clip_kind == 2) { for (int k = 0; k < SQ_TRACKS; k++) { for (int n = 0; n < SQ_STEPS; n++) *SQ_STEP(p, k, n) = sq_clip[k][n]; h->len[p][k] = sq_clip_len[k]; aos_breathe(); } say("PATTERN", "PASTED"); }
            else say("NOTHING", "COPIED");
            return 1;
        }
        if (aos_seq_playing()) { aos_seq_play(0); say("STOP", 0); } else { aos_seq_mode(0); aos_seq_mode(1); say("SILENCE", 0); } return 1;
    case KEY_REC:
        if (func_held) {                                           /* copy: the track, or with PATTERN held the whole pattern */
            int p = sq_pat();
            if (sq_pat_held) { for (int k = 0; k < SQ_TRACKS; k++) { for (int n = 0; n < SQ_STEPS; n++) sq_clip[k][n] = *SQ_STEP(p, k, n); sq_clip_len[k] = (uint8_t)sq_len(k); aos_breathe(); } sq_clip_kind = 2; say("PATTERN", "COPIED"); }
            else { for (int n = 0; n < SQ_STEPS; n++) sq_clip[0][n] = *SQ_STEP(p, t, n); sq_clip_len[0] = (uint8_t)sq_len(t); sq_clip_kind = 1; say("TRACK", "COPIED"); }
            return 1;
        }
        sq_rec = !sq_rec; sq_held = 0; say(sq_rec ? "LIVE" : "GRID", sq_rec ? "PADS 1-8" : 0); return 1;
    case 0x18: master_tap(); return 1;
    case KEY_SRC: case KEY_AMP: case KEY_LFO: case KEY_FX1: case KEY_FX2: case KEY_PAGE: case KEY_ARR: case KEY_MIDI: {
        int pg = code == KEY_SRC ? SQP_SRC : code == KEY_AMP ? SQP_AMP : code == KEY_LFO ? SQP_MOD : code == KEY_FX1 ? SQP_FLT : code == KEY_FX2 ? SQP_FX : code == KEY_ARR ? SQP_GEN : code == KEY_MIDI ? SQP_MIDI : SQP_PTN;
        if (sq_held && sq_lockable(pg)) { sq_lockpage = (uint8_t)pg; sq_turned = 1; say("LOCK", sq_page_name[pg]); }
        else if (sq_page == pg && sq_view == SV_GRID) { func_latch = !func_latch; say(sq_page_name[pg], func_latch ? "2" : "1"); }   /* the same key again: its other six knobs */
        else { sq_page = (uint8_t)pg; sq_view = SV_GRID; func_latch = 0; say(sq_page_name[pg], 0); }
        return 1; }
    case KEY_BANK: sq_view = sq_view == SV_MIXER ? SV_GRID : SV_MIXER; say(sq_view == SV_MIXER ? "MIXER" : "GRID", 0); return 1;
    case 0x30: sq_view = SV_MASTER; page = PG_MASTER; say("MASTER", 0); return 1;
    case KEY_LEFT: case KEY_RIGHT:
        if (sq_held) {                                             /* a held step: earlier, later */
            sq_turned = 1;
            for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); s->flags |= SF_ON; s->micro = (int8_t)clamp(s->micro + (code == KEY_RIGHT ? 1 : -1), -23, 23); sq_say(SQP_STEP, 1, s->micro + 23); }
        } else { int pages = (sq_len(t) + 15) / 16; sq_spage = (uint8_t)((sq_spage + (code == KEY_RIGHT ? 1 : pages - 1)) % pages); }
        return 1;
    case KEY_UP: case KEY_DOWN: h->sel = (uint8_t)((t + (code == KEY_DOWN ? 1 : 7)) & 7); sq_held = 0; if (sq_spage * 16 >= sq_len(h->sel)) sq_spage = 0; return 1;
    case KEY_YES:
        if (sq_held) {                                             /* a held step: backward, or forward again */
            sq_turned = 1;
            for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); s->flags |= SF_ON; s->flags ^= SF_REV; say("STEP", s->flags & SF_REV ? "BACKWARD" : "FORWARD"); }
            return 1;
        }
        if (func_held && sq_page == SQP_SRC) { browser_open(BR_SEQ); return 1; }
        if (sq_page == SQP_FX) { sq_fxlist = 1; sq_fxrow = h->fx_row[t] <= FX_COUNT ? h->fx_row[t] : 0; return 1; }
        aos_seq_fill(1); say("FILL", 0);
        return 1;
    case KEY_NO:
        if (sq_held) { for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); for (int q = 0; q < SQ_LOCKS; q++) s->lp[q] = 0; } sq_turned = 1; say("LOCKS", "CLEARED"); }
        return 1;
    }
    return 1;
}
static int seq_enc(int e, int delta)
{
    if (!seq_on || preset_mode || br_mode) return 0;
    if (sq_view == SV_MASTER) return e == 6;                       /* the master page's own knobs; LEVEL does nothing there */
    if (scene_held) return 1;                                      /* scenes belong to the sky's knobs */
    if (e == 6 && sq_tempo_held) { sq_tempo_set(master_bpm() + (delta > 0 ? 1 : -1)); static char b[6]; num(b, master_bpm()); say("TEMPO", b); return 1; }
    if (sq_fxlist) { if (e == 0 || e == 6) sq_fxrow = (uint8_t)clamp(sq_fxrow + (delta > 0 ? 1 : -1), 0, FX_COUNT); return 1; }
    if (e == 6) {                                                  /* LEVEL: the track's volume */
        SqHead *h = SQH; int t = sq_sel(), v = clamp(h->trk[t][TP_VOL] + delta, 0, 127);
        h->trk[t][TP_VOL] = (uint8_t)v; static char n[6]; num(n, v); say("VOLUME", n);
        return 1;
    }
    if (e < 0 || e > 5) return 1;
    { int pg = sq_shown_page(); sq_turn(pg, e + (layer() && pg != SQP_STEP ? 6 : 0), delta); }
    return 1;
}

/* ---- the screen ---- */
static void sq_box(int x0, int y0, int x1, int y1) { for (int x = x0; x <= x1; x++) span(x, y0, y1, 1); }
static void seq_draw(void)
{
    const SqHead *h = SQH;
    int t = sq_sel(), pat = sq_pat(), playing = aos_seq_playing();
    char n[16];
    /* the top line: track, pattern (and the one coming), page of steps, tempo, the eight meters */
    n[0] = 'T'; n[1] = (char)('1' + t); n[2] = 0; text(1, 1, n, 1);
    n[0] = (char)('A' + (playing ? aos_seq_pat() : pat)); n[1] = 0; if (aos_seq_pat() != pat && playing) { n[1] = '>'; n[2] = (char)('A' + pat); n[3] = 0; } text(12, 1, n, 1);
    if (h->chain_n) text(26, 1, "CH", 1);
    { int pages = (sq_len(t) + 15) / 16; n[0] = (char)('1' + sq_spage); n[1] = '/'; n[2] = (char)('0' + pages); n[3] = 0; text(37, 1, n, 1); }
    num(n, aos_seq_ext() ? (aos_seq_bpm10() + 5) / 10 : master_bpm()); text(52, 1, n, 1);
    if (aos_seq_ext()) { px(50, 1); px(50, 3); px(50, 5); }           /* a clock from outside has the tempo */
    text(67, 1, sq_rec ? "LIVE" : playing ? "PLAY" : "", 1);
    if (layer() && !sq_held) text(86, 1, "2", 1);
    if (playing && (aos_seq_bar() & 3) == 0) sq_box(90, 1, 92, 5);
    for (int k = 0; k < 8; k++) {
        int x = 94 + k * 4, lv = aos_seq_peak(k) * 7 / 16, muted = ((h->mutes >> k) & 1) || (h->solo && !((h->solo >> k) & 1));
        if (muted) px(x + 1, 3); else { px(x, 7); px(x + 1, 7); px(x + 2, 7); if (lv) sq_box(x, 7 - lv, x + 2, 7); }
        if (k == t) { px(x, 0); px(x + 1, 0); px(x + 2, 0); }
    }
    if (sq_view == SV_MIXER) {
        /* eight strips: the meter, the volume's mark, M or S */
        for (int k = 0; k < 8; k++) {
            int x = 2 + k * 16, lv = aos_seq_peak(k) * 30 / 16, vol = h->trk[k][TP_VOL] * 30 / 127;
            for (int y = 12; y <= 42; y++) { px(x, y); px(x + 9, y); }
            if (lv) sq_box(x + 2, 42 - lv, x + 7, 42);
            for (int xx = x - 1; xx <= x + 10; xx++) px(xx, 42 - vol);
            n[0] = (char)('1' + k); n[1] = 0; text(x + 3, 45, n, 1);
            if ((h->mutes >> k) & 1) text(x + 3, 52, "M", 1);
            if ((h->solo >> k) & 1) text(x + 3, 58, "S", 1);
            if (k == t) for (int xx = x; xx <= x + 9; xx++) px(xx, 10);
        }
        return;
    }
    /* the grid: eight rows of sixteen, the track in hand the tall one */
    int y = 10;
    for (int r = 0; r < 8; r++) {
        if (r & 1) aos_breathe();                                  /* a long drawing: the stream is fed meanwhile */
        int tall = r == t, hh = tall ? 6 : 2, len = sq_len(r), now = playing ? aos_seq_step(r) : -1;
        for (int c = 0; c < 16; c++) {
            int nstep = sq_spage * 16 + c, x = c * 8;
            if (nstep >= len) continue;
            const SqStep *s = SQ_STEP(pat, r, nstep);
            int on = s->flags & SF_ON;
            if (on) {
                int dx = tall ? (s->micro > 3 ? 1 : s->micro < -3 ? -1 : 0) : 0;
                sq_box(x + dx, y, x + 6 + dx, y + hh - 1);
                if (tall) {
                    if (s->lp[0] || s->lp[1] || s->lp[2]) { span(x + 3 + dx, y + 2, y + 3, 0); }                      /* locks: a hole */
                    if ((s->prob && s->prob < 100) || s->cond) { span(x + 6 + dx, y, y + 1, 0); span(x + 5 + dx, y, y, 0); }   /* a chance or a condition: a cut corner */
                    if (s->ratch > 1) { span(x + 2 + dx, y + hh - 1, y + hh - 1, 0); span(x + 4 + dx, y + hh - 1, y + hh - 1, 0); }   /* a ratchet: teeth */
                    if (s->flags & SF_REV) span(x + dx, y, y, 0);
                }
            } else px(x + 3, y + hh - 1);
            if (tall && !(c & 3) && !on) px(x + 3, y + hh - 2);
            if (nstep == now) { for (int xx = x; xx <= x + 6; xx++) span(xx, y, y + hh - 1, 3); }
            if (tall && ((sq_held >> c) & 1) && (ticks & 8)) for (int xx = x; xx <= x + 6; xx++) span(xx, y, y + hh - 1, 3);
        }
        y += hh + 1;
    }
    /* the list of effects */
    if (sq_fxlist) {
        for (int x = 20; x < 108; x++) span(x, 9, 56, 0);
        for (int x = 20; x < 108; x++) { px(x, 9); px(x, 56); }
        for (int yy = 9; yy <= 56; yy++) { px(20, yy); px(107, yy); }
        int top = clamp(sq_fxrow - 3, 0, FX_COUNT - 6 < 0 ? 0 : FX_COUNT - 6);
        for (int i = 0; i < 7 && top + i <= FX_COUNT; i++) {
            int r = top + i, yy = 12 + i * 6;
            const char *nm = r ? fx_info[r - 1].name + 6 : "NONE";
            upper(nm, n, 12);
            if (r == sq_fxrow) { for (int x = 22; x < 106; x++) span(x, yy - 1, yy + 5, 1); text(26, yy, n, 0); } else text(26, yy, n, 1);
            if (r && sq_fx_place(t, r) < 0 && r != sq_fxrow) text(98, yy, "-", 1);
        }
        return;
    }
    /* the line between: the page, the track's sound, a held step */
    int pg = sq_shown_page(), lay = layer() && pg != SQP_STEP ? 6 : 0;
    text(1, 40, sq_page_name[pg], 1);
    if (sq_held) { int st = sq_first_held(); text(22, 40, "STEP", 1); num(n, st + 1); text(40, 40, n, 1);
        const SqStep *s = SQ_STEP(pat, t, st); int x = 52;
        for (int k = 0; k < SQ_LOCKS; k++) if (s->lp[k] && s->lp[k] <= TP_ALL) { for (int a = 0; a < SQP_FX; a++) for (int b = 0; b < 12; b++) if (sq_par[a][b].tp == s->lp[k] - 1) { text(x, 40, sq_par[a][b].lab, 1); x += text_w(sq_par[a][b].lab) + 4; a = SQP_FX; break; } } }
    else if (pg == SQP_FX) { int row = h->fx_row[t]; upper(row > 0 && row <= FX_COUNT ? fx_info[row - 1].name + 6 : "NONE  YES PICKS", n, 15); text(22, 40, n, 1); }
    else { if (h->trk[t][TP_MACH]) { sq_text(TP_MACH, h->trk[t][TP_MACH], n); text(22, 40, n, 1); } else { text(22, 40, "SMPL", 1); sq_text(TP_SMPL, h->trk[t][TP_SMPL], n); text(42, 40, n, 1); if (!aos_slot_ready(h->trk[t][TP_SMPL])) text(62, 40, "EMPTY", 1); }
           num(n, sq_len(t)); text(W - 1 - text_w(n) - 16, 40, "LEN", 1); text(W - 1 - text_w(n), 40, n, 1); }
    for (int x = 0; x < W; x += 2) px(x, 47);
    aos_breathe();
    /* the six knobs: name and value */
    for (int i = 0; i < 6; i++) {
        const char *lab; int v = sq_get(pg, i + lay);
        if (pg == SQP_FX) { int row = h->fx_row[t]; if (row <= 0 || row > FX_COUNT) break; lab = lay ? fx_info[row - 1].knob2[i] : fx_info[row - 1].knob[i]; if (!lab || !lab[0]) continue; sq_fx_text(t, i + lay, v, n); }
        else { const SqP *p = &sq_par[pg][i + lay]; if (p->tp == SP_NONE) continue; lab = p->lab; sq_text(p->tp, v, n); }
        char l4[5]; int k = 0; while (lab[k] && k < 4) { l4[k] = lab[k] >= 'a' && lab[k] <= 'z' ? (char)(lab[k] - 32) : lab[k]; k++; } l4[k] = 0;
        n[5] = 0;
        text(1 + i * 21, 50, l4, 1); text(1 + i * 21, 57, n, 1);
        /* a value that is the step's own: underlined */
        if (sq_held && sq_lockable(pg)) { const SqStep *s = SQ_STEP(pat, t, sq_first_held()); if (sq_lock_of(s, sq_par[pg][i + lay].tp) >= 0) for (int x = 1 + i * 21; x < 1 + i * 21 + 15; x++) px(x, 63); }
    }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 38, 46, 1);
        text(x, 40, toast, 0);
    }
}
/* the lamps, at once and in steps (no fades here): a trig is dark, a beat's mark (1), a note (2) or the playhead (3) */
static void seq_leds(void)
{
    const SqHead *h = SQH; int t = sq_sel(), playing = aos_seq_playing();
    static const uint8_t page_lamp[SQP_N] = {66, 68, 86, 80, 64, 70, 82, 0, 0};
    if (sq_pat_held) { for (int k = 0; k < SQ_PATTERNS; k++) led_want[k] = k == sq_pat() ? 3 : 1; }
    else if (sq_view == SV_MIXER || sq_rec) {
        int mixer = sq_view == SV_MIXER;
        for (int k = 0; k < 8; k++) {
            int muted = (h->mutes >> k) & 1;
            if (mixer) { led_want[k] = muted ? 0 : 2; led_want[8 + k] = (h->solo >> k) & 1 ? 3 : 0; }
            else { led_want[k] = aos_seq_peak(k) > 6 ? 3 : 1; led_want[8 + k] = muted ? 0 : 2; }
        }
    } else {
        int len = sq_len(t), now = playing ? aos_seq_step(t) : -1;
        for (int k = 0; k < 16; k++) {
            int n = sq_spage * 16 + k;
            if (n >= len) continue;
            led_want[k] = (SQ_STEP(sq_pat(), t, n)->flags & SF_ON) ? 2 : (k & 3) == 0 ? 1 : 0;
            if (n == now) led_want[k] = 3;
            if ((sq_held >> k) & 1) led_want[k] = (ticks & 8) ? 3 : 1;
        }
    }
    leds_clear_trigs();
    for (int k = 0; k < 16; k++) set_led(2 * k, led_want[k]);
    /* the tracks: the one in hand full, one sounding bright, a muted one dark */
    for (int k = 0; k < 8; k++) { int muted = ((h->mutes >> k) & 1) || (h->solo && !((h->solo >> k) & 1)); set_led(40 + 2 * k, muted ? 0 : k == t ? 3 : aos_seq_peak(k) > 5 ? 2 : 1); }
    for (int a = 0; a < SQP_N; a++) if (page_lamp[a]) set_led(page_lamp[a], sq_page == a && sq_view == SV_GRID ? 3 : 1);
    set_led(90, playing ? 3 : 1); set_led(88, playing ? 1 : 2); set_led(84, sq_rec ? 3 : 1);
    set_led(38, playing && (aos_seq_bar() & 3) == 0 ? 3 : 0);     /* TEMPO: the beat */
}
#endif
/* ---- dream visits: now and then something from sadnoise's dream log drifts into the default sky and water --------
 * (sadnoise, 12 Nov 2026: "have some of those dreams sometimes randomly influence the visual of the default sky and
 * water background"). Every 4 to 12 minutes (the first 2 to 5 minutes after the start) one small motif comes into
 * the scene for 30 to 60 seconds and leaves again: it fades in and out through the dither, and rises, sinks or
 * crosses as it likes. Everything keeps to the waterline and the two edges, so the middle is left to the knobs.
 * dream_sky_tick() each UI tick, dream_sky_draw() after the water is drawn. Ten bytes of state; a frame costs a
 * few hundred pixels at most while a visit is on and nothing otherwise. */
#if defined(AERIAOS)
#define DREAM_SKY 1
enum { DV_NONE = 0, DV_ISLAND, DV_TRAIN, DV_REDCARS, DV_FROGS, DV_COLUMNS, DV_WALKWAY, DV_LAMP, DV_DOOR, DV_PORTRAIT,
       DV_TORPEDO, DV_KINDS };
#define DV_FIRST_LO 125     /* seconds: the first visit */
#define DV_FIRST_HI 300
#define DV_GAP_LO 240       /* seconds between one visit's end and the next */
#define DV_GAP_HI 720
#define DV_LEN_LO 30        /* seconds a visit lasts */
#define DV_LEN_HI 60
#define DV_TPS 61           /* UI ticks a second */
static struct { uint16_t wait, t, len; uint8_t kind, last, flip, var; } dv;
static int saver_on;        /* (ui12.c's own, declared here too: no visit starts under a screensaver) */
/* what a frame works with, set at the top of dream_sky_draw */
static int16_t dv_wl, dv_wv, dv_tk;   /* the waterline, the waves (-1 frozen), the tick the water was drawn with */
static int8_t dv_lv, dv_clip;         /* the fade, 0..16; 1: nothing is drawn at or under the water's surface */

static int dv_wy(int x)   /* the water's surface at screen column x, as draw_water has it */
{
    int y = dv_wl;
    if (dv_wv >= 0) y += (sine[(x * 3 + dv_tk) & 63] * dv_wv + sine[(x * 7 - dv_tk * 2) & 63] * (dv_wv > 0)) / 160;
    return y;
}
static int dv_sx(int x) { return dv.flip ? W - 1 - x : x; }
static int dv_wyl(int x) { return dv_wy(dv_sx(x)); }
static void dv_tone(int x, int y, int lv)   /* a pixel of tone lv (16 solid), through the fade */
{
    x = dv_sx(x);
    if (dv_clip && y >= dv_wy(x)) return;
    if (dither_on(x, y, lv * dv_lv >> 4)) px(x, y);
}
static void dv_px(int x, int y) { dv_tone(x, y, 16); }
static void dv_off(int x, int y)
{
    x = dv_sx(x);
    if (dv_clip && y >= dv_wy(x)) return;
    if (dv_lv > 7) px_off(x, y);
}
static void dv_h(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) dv_px(x, y); }
static void dv_v(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) dv_px(x, y); }
static void dv_clear(int x0, int y0, int x1, int y1) { for (int x = x0; x <= x1; x++) for (int y = y0; y <= y1; y++) dv_off(x, y); }
/* rows of bits, the leftmost the highest of w; a set bit lit, a clear one dark (opaque) or left alone */
static void dv_spr(const uint16_t *rows, int n, int w, int x, int y, int opaque)
{
    for (int r = 0; r < n; r++) for (int c = 0; c < w; c++) {
        if (rows[r] >> (w - 1 - c) & 1) dv_px(x + c, y + r); else if (opaque) dv_off(x + c, y + r);
    }
}
/* a small figure, feet at (x, y), seven pixels tall; step 0 standing, else the stride */
static void dv_man(int x, int y, int step)
{
    dv_clear(x - 1, y - 6, x + 1, y);
    dv_px(x, y - 6); dv_px(x, y - 5); dv_px(x - 1, y - 6); dv_px(x - 1, y - 5);
    dv_h(x - 1, x + 1, y - 4); dv_v(x, y - 3, y - 2);
    if (!step) { dv_v(x - 1, y - 1, y); dv_v(x + 1, y - 1, y); }
    else if (step & 1) { dv_px(x - 1, y - 1); dv_px(x - 2, y); dv_px(x + 1, y - 1); dv_px(x + 1, y); }
    else { dv_px(x, y - 1); dv_px(x, y); dv_px(x + 1, y - 1); }
}
/* a shimmer on the water under something: columns x0..x1, rows from the waterline down */
static void dv_glint(int x0, int x1, int depth, int lv)
{
    for (int x = x0; x <= x1; x++) {
        uint32_t k = (uint32_t)x * 2654435761u ^ (uint32_t)(dv_tk >> 3) * 40503u;
        int y = dv_wyl(x) + 2 + (int)((k >> 9) % (uint32_t)depth);
        if ((k >> 29) < 3) dv_tone(x, y, lv);
    }
}

/* ISLAND: a small island with one tree rises near one side; someone wades to it with a bundle; it sinks again */
static void dv_island(int t, int len)
{
    static const int8_t frond[18][2] = {{-1, -1}, {-2, -2}, {-3, -2}, {-4, -1}, {-5, 0}, {-6, 2}, {1, -1}, {2, -2}, {3, -2}, {4, -1}, {5, 0}, {6, 2},
                                        {-1, 1}, {-2, 2}, {-3, 4}, {1, 1}, {2, 2}, {3, 4}};
    int cx = 20, rem = len - t, down = t < 240 ? (240 - t) / 9 : rem < 240 ? (240 - rem) / 9 : 0, base = dv_wl + down;
    dv_clip = 1;
    for (int dx = -13; dx <= 13; dx++) {
        int top = base - 6 + dx * dx / 28;
        for (int y = top; y <= base + 9; y++) { dv_off(cx + dx, y); if (y == top) dv_px(cx + dx, y); else dv_tone(cx + dx, y, 5); }
    }
    int tx = cx + 2, ty = base - 18, sway = sine[(dv_tk >> 2) & 63] / 90;
    for (int k = 0; k < 13; k++) dv_px(tx - (k * k) / 48, ty + k);
    for (int i = 0; i < 18; i++) dv_px(tx + frond[i][0] + (frond[i][1] > 0 ? sway : 0), ty + frond[i][1]);
    /* the wader: in from the far side, up to the waist, a bundle on a stick; out of the water under the tree */
    int far = W + 6, near = cx + 10, wx = t * 8 >= len * 5 ? near : far - t * (far - near) * 8 / (len * 5), there = wx == near;
    int fy = there ? base - 4 : dv_wl + 3 + ((t >> 4) & 1);
    dv_man(wx, fy, there ? 0 : 1 + ((t >> 4) & 1));
    dv_px(wx + 1, fy - 5); dv_px(wx + 2, fy - 6); dv_px(wx + 3, fy - 7); dv_px(wx + 4, fy - 7); dv_px(wx + 3, fy - 8); dv_px(wx + 4, fy - 8);
    dv_clip = 0;
    if (!there) for (int i = 2; i < 9; i++) if ((i + t / 5) % 3 == 0) dv_px(wx + i, dv_wyl(wx + i) + 1 + (i & 1));   /* a wake */
    if (!down) dv_glint(cx - 11, cx + 11, 4, 16);
}

/* TRAIN: the maintenance cargo train, a row of small blinking lights along the horizon, left to right */
static void dv_train(int t, int len)
{
    int n = 7 + (dv.var & 1), trainw = n * 9 + 10, x0 = -trainw + t * (W + trainw) / len, y = dv_wl - 7;
    for (int x = (-t / 6) & 3; x < W; x += 4) dv_px(x, y + 2);               /* the line it runs on */
    for (int i = 0; i < n; i++) {
        int x = x0 + i * 9, on = ((dv_tk >> 3) + i * 3) & 4;
        if (x < -9 || x >= W) continue;
        dv_clear(x, y - 3, x + 7, y + 1);
        dv_h(x, x + 6, y); dv_px(x + 1, y + 1); dv_px(x + 5, y + 1);
        dv_px(x + 3, y - 1);
        if (on) { dv_px(x + 3, y - 2); dv_px(x + 3, y - 3); dv_px(x + 2, y - 2); dv_px(x + 4, y - 2); }
        if (on && (((dv_tk >> 2) + i) & 1)) dv_tone(x + 3, dv_wyl(x + 3) + 3 + (i & 1), night ? 16 : 8);
    }
    int ex = x0 + n * 9;                                                     /* the engine pulls */
    if (ex > -10 && ex < W) {
        dv_clear(ex, y - 5, ex + 8, y + 1);
        dv_h(ex, ex + 7, y - 4); dv_h(ex, ex + 8, y); dv_v(ex, y - 4, y); dv_v(ex + 7, y - 4, y); dv_px(ex + 8, y - 1);
        dv_px(ex + 2, y + 1); dv_px(ex + 6, y + 1); dv_px(ex + 2, y - 2); dv_px(ex + 3, y - 2); dv_px(ex + 5, y - 2);
        for (int k = 2; k < (night ? 16 : 9); k += 2) dv_tone(ex + 8 + k, y - 1, 16 - k);   /* its lamp */
    }
}

/* REDCARS: a stair of old cars down from high on one edge to the water, and someone stepping down them */
#define DV_CARS 4
#define DV_CAR_DX 10
#define DV_CAR_DY 7
static void dv_redcars(int t, int len)
{
    static const uint16_t car[6] = {0x1F0, 0x248, 0xFFE, 0x1FFF, 0x1FFF, 0x60C};
    int x_end = -3 + DV_CARS * DV_CAR_DX + 3, walk = len - 720;
    for (int i = 0; i < DV_CARS; i++) {
        int x = -3 + i * DV_CAR_DX, y = dv_wl - 6 - (DV_CARS - 1 - i) * DV_CAR_DY;
        int drop = t < 60 + i * 50 ? (60 + i * 50 - t) / 3 : 0;          /* each one down into its place, the top one first */
        if (i == DV_CARS - 1) y += dv_wyl(x + 6) - dv_wl;
        dv_spr(car, 6, 13, x, y - drop, 1);
    }
    dv_glint(x_end - 12, x_end, 3, 12);
    if (t < 320) return;
    int wx = -3 + (t - 320) * (x_end + 8) / walk, i = clamp((wx + 1) / DV_CAR_DX, 0, DV_CARS - 1);   /* roof to roof */
    int fy = dv_wl - 7 - (DV_CARS - 1 - i) * DV_CAR_DY;
    if (wx >= x_end) { fy = dv_wl + (wx - x_end) * 2; dv_clip = 1; }      /* and off the last one, into the water */
    dv_man(wx, fy, 1 + ((t >> 4) & 1));
    dv_clip = 0;
}

/* FROGS: four frogs come up at the waterline and hop in formation, one square at a time; then they go under */
#define DV_FROG_N 4
#define DV_FROG_HOPS 5
static void dv_frogs(int t, int len)
{
    static const uint16_t sit[5] = {0xC6, 0x17D, 0x1FF, 0xFE, 0x1C7}, air[5] = {0xC6, 0x17D, 0x1FF, 0xFE, 0x101};
    int per = (len - 560) / DV_FROG_HOPS, rem = len - t;
    for (int k = 0; k < DV_FROG_N; k++) {
        int tk = t - 280 - k * 7, n = tk < 0 ? 0 : tk / per, ph = tk < 0 ? 99 : tk % per, x, lift = 0, down = 0;
        if (n >= DV_FROG_HOPS) { n = DV_FROG_HOPS; ph = 99; }
        x = 35 - k * 11 + n * 8 + (tk < 0 || n >= DV_FROG_HOPS ? 0 : ph < 24 ? ph / 3 : 8);
        if (ph < 24) lift = ph * (24 - ph) / 24;
        if (t < 200) down = (200 - t) / 14;
        if (rem < 200 + k * 20) down = (200 + k * 20 - rem) / 12;
        int y = dv_wyl(x + 4) - 5 - lift + down;
        dv_clip = down > 0;
        dv_spr(lift > 1 ? air : sit, 5, 9, x, y, 1);
        dv_clip = 0;
        if (lift > 1) { dv_px(x - 1, y + 5); dv_px(x + 9, y + 5); }                        /* its legs out behind */
        if (down > 1 && down < 9) { dv_px(x - 2 - down / 3, dv_wyl(x - 2) + 1); dv_px(x + 10 + down / 3, dv_wyl(x + 10) + 1); }   /* rings */
        else if (!down) dv_glint(x + 1, x + 7, 3, 16);
    }
}

/* COLUMNS: three thin columns rise slowly out of the water at the edges, to the top of the screen, and sink */
static void dv_columns(int t, int len)
{
    static const uint8_t at[3] = {5, 15, 119};
    int rise = len * 3 / 8, rem = len - t;
    for (int k = 0; k < 3; k++) {
        int x = at[k], full = dv_wl + 6 - (k == 2 ? 15 : k * (dv.var & 3) * 3), d = k * 70, up;   /* (the one on the right stops under the weather's name) */
        up = t - d < 0 ? 0 : t - d >= rise ? full : (t - d) * full / rise;
        if (rem < rise) up = rem * full / rise;
        if (up < 2) continue;
        int top = dv_wl + 3 - up, bot = dv_wyl(x + 2);
        for (int c = 0; c < 5; c++) {
            int sx = dv_sx(x + c);
            span(sx, top, bot, 0);
            if (c == 0 || c == 4) span(sx, top, bot, 1);
        }
        for (int y = top + 3 + (top & 1); y < bot; y += 2) dv_px(x + 2, y);            /* the fluting */
        for (int c = -1; c <= 5; c++) { dv_px(x + c, top); dv_px(x + c, top + 1); dv_off(x + c, top - 1); }   /* the capital */
        dv_px(x - 1, bot); dv_px(x + 5, bot);
        for (int j = 2; j < 8; j += 2) dv_tone(x + ((j + (dv_tk >> 4)) & 3), bot + j, 16 - j);   /* and in the water */
    }
}

/* WALKWAY: a thin flat moving walkway just over the water, and someone carried along it */
static void dv_walkway(int t, int len)
{
    int rem = len - t, xs = rem < 280 ? W - rem / 2 : 0, xe = t < 280 ? t / 2 : W - 1, y = dv_wl - 6;
    for (int x = xs; x <= xe; x++) {
        dv_px(x, y);
        if (((x - t / 3) & 7) < 2) dv_px(x, y + 1);                           /* the belt runs */
        if (x % 26 == 9) { int b = dv_wyl(x); for (int yy = y + 2; yy < b; yy += 2) dv_px(x, yy); dv_tone(x, b + 2, 8); }
    }
    int ride = len - 560, fx = -4 + (t - 280) * (W + 8) / ride;
    if (t >= 280 && fx < W + 3) { dv_man(fx, y - 1, 0); dv_tone(fx, dv_wyl(fx) + 2 + ((dv_tk >> 4) & 1), 8); }
}

/* LAMP: a lamp post standing in the water; its glow charges up, then holds steady, with small static sparks */
static void dv_lamp(int t, int len)
{
    int x = 14, wy = dv_wyl(x), top = dv_wl - 19, cy = top + 3, charge = len / 3, steady = t >= charge;
    int r = steady ? (night ? 8 : 7) : 2 + t * 5 / charge, lv = steady ? (night ? 7 : 5) : 2 + t * 3 / charge + ((dv_tk >> 3) & 1);
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {      /* the glow */
        int d = dx * dx + dy * dy;
        if (d <= r * r && d > 8) dv_tone(x + dx, cy + dy, lv - d * lv / (r * r + 8));
    }
    dv_clear(x - 3, top - 1, x + 3, top + 6);
    dv_h(x - 1, x + 1, top); dv_h(x - 2, x + 2, top + 1); dv_h(x - 1, x + 1, top + 5);   /* the lantern */
    dv_v(x - 2, top + 2, top + 4); dv_v(x + 2, top + 2, top + 4);
    for (int yy = top + 2; yy <= top + 4; yy++) for (int xx = x - 1; xx <= x + 1; xx++) dv_tone(xx, yy, steady ? 16 : (xx == x && yy == cy) ? 16 : t * 12 / charge);
    dv_v(x, top + 6, wy); dv_px(x - 1, top + 6); dv_px(x + 1, top + 6); dv_h(x - 1, x + 1, wy - 1);   /* the post */
    if (steady) for (int i = 0; i < 3; i++) {                                 /* static */
        uint32_t k = ((uint32_t)(dv_tk / 7) + (uint32_t)i * 977u) * 2654435761u;
        int sx = x + (int)((k >> 8) % 23u) - 11, sy = cy + (int)((k >> 16) % 17u) - 9;
        if ((k >> 28) < 9 && (iabs(sx - x) > 4 || iabs(sy - cy) > 4)) { dv_px(sx, sy); if (k & 1) dv_px(sx + 1, sy - 1); else dv_px(sx - 1, sy - 1); }
    }
    for (int j = 2; j < 9; j++) if (steady || j < r) {                        /* and its light on the water */
        int s = sine[(j * 9 + dv_tk) & 63];
        dv_tone(x + (s > 60) - (s < -60), wy + j, 15 - j);
    }
}

/* DOOR: a door standing on the water with a glow about it, opening a little, its light lying on the water */
static void dv_door(int t, int len)
{
    int x = 9, w = 9, bot = dv_wyl(x + 4), top = bot - 18, glow = 5 + sine[(dv_tk >> 1) & 63] / 64 + (night != 0);
    int ajar = t > len / 2 ? clamp((t - len / 2) / 80, 0, 3) : 0;
    for (int yy = top - 5; yy <= bot; yy++) for (int xx = x - 5; xx <= x + w + 5; xx++) {
        int ox = xx < x ? x - xx : xx > x + w ? xx - x - w : 0, oy = yy < top ? top - yy : 0, o = ox > oy ? ox : oy;
        if (o == 0) {
            int edge = xx == x || xx == x + w || yy == top;
            dv_off(xx, yy);
            if (edge || xx - x <= ajar) dv_px(xx, yy);
        } else if (o > 1 && ox + oy < 8) dv_tone(xx, yy, glow - o);
    }
    dv_px(x + w - 2, top + 10); dv_px(x + 3, top + 3); dv_px(x + w - 3, top + 3);   /* the handle, two small panes */
    for (int j = 1; j < 9; j++) {                                            /* the light on the water */
        int hw = 2 + j / 2, wob = sine[(j * 11 + dv_tk) & 63] / 70;
        for (int xx = x + 4 - hw; xx <= x + 5 + hw; xx++) dv_tone(xx + wob, bot + j, 11 - j);
    }
}

/* PORTRAIT: the grandmother's portrait, a small oval, only in the water: a reflection of nothing above */
static void dv_portrait(int t, int len)
{
    static const uint16_t cameo[9] = {0x1F0, 0x60C, 0x842, 0x10E1, 0x10E1, 0x1041, 0xBFA, 0x7FC, 0x1F0};
    int x = 12 + t * 14 / len, top = dv_wl + 1 + (dv_wl < 54), rip = (dv_tk >> 3) % 14;
    for (int r = 0; r < 9; r++) dv_spr(cameo + r, 1, 13, x + (r == rip) - (r == rip - 1), top + r, 1);
    dv_glint(x - 3, x - 1, 6, 8); dv_glint(x + 13, x + 15, 6, 8);
}

/* TORPEDO: a smiling torpedo passing under the surface, bubbles behind it */
static void dv_torpedo(int t, int len)
{
    static const uint16_t body[5] = {0x3FF8, 0xFFEE, 0xFFBB, 0xFFC6, 0x3FF8};   /* 16 wide, the nose to the right */
    int x = -24 + t * (W + 48) / len, y = dv_wl + 3 + (dv_wl < 54) + ((dv_tk >> 5) & 1), spin = (dv_tk >> 1) & 1;
    dv_clear(x - 2, y - 1, x + 16, y + 5);
    dv_spr(body, 5, 16, x, y, 1);
    dv_px(x - 1, y); dv_px(x - 1, y + 4); dv_px(x - 1, y + 1); dv_px(x - 1, y + 3);   /* fins, the screw */
    dv_px(x - 2, y + (spin ? 1 : 3)); dv_px(x - 2, y + 2);
    for (int i = 0; i < 4; i++) { int a = (t / 5 + i * 3) % 7; dv_tone(x - 6 - i * 6 - a, y + 2 - a / 2 - (i & 1), 16 - a * 2); }
}

static void dream_sky_tick(void)
{
    if (dv.kind) {
        if (++dv.t >= dv.len) { dv.last = dv.kind; dv.kind = DV_NONE; dv.wait = (uint16_t)(range(DV_GAP_LO, DV_GAP_HI) * DV_TPS); }
        return;
    }
    if (!dv.wait) dv.wait = (uint16_t)(range(DV_FIRST_LO, DV_FIRST_HI) * DV_TPS);   /* the first one */
    if (dv.wait > 1) { dv.wait--; return; }
    if (saver_on || hidden || bolt_t) return;                                    /* not under a screensaver, not in a flash */
    /* one of the others, never the one that came last */
    dv.kind = (uint8_t)(dv.last ? 1 + (dv.last - 1 + range(1, DV_KINDS - 2)) % (DV_KINDS - 1) : range(1, DV_KINDS - 1));
    dv.t = 0; dv.len = (uint16_t)(range(DV_LEN_LO, DV_LEN_HI) * DV_TPS);
    dv.var = (uint8_t)rnd();
    dv.flip = (uint8_t)((fin_x >> 8) < W / 2 - 7);                              /* the side the boat is not on */
    if (dv.kind == DV_TRAIN || dv.kind == DV_COLUMNS) dv.flip = 0;
    else if (dv.kind == DV_TORPEDO || dv.kind == DV_WALKWAY) dv.flip = dv.var >> 7;
}
static void dream_sky_draw(void)
{
    if (!dv.kind || dv.t >= dv.len) return;
    int t = dv.t, len = dv.len, rem = len - 1 - t, e = (t < rem ? t : rem) >> 3;
    dv_lv = (int8_t)(e > 16 ? 16 : e); dv_clip = 0;
    dv_tk = (int16_t)(ticks & 0x3fff);                                       /* (the water's phases only use it mod 64) */
    dv_wl = (int16_t)(WATER_Y - pv(PG_MIX, 11) / 32);
    dv_wv = (int16_t)(WON(SNOW) && amount > 200 ? -1 : pv(PG_SKY, 2) + (WON(STORM) ? 3 : 0));
    switch (dv.kind) {
    case DV_ISLAND: dv_island(t, len); break;
    case DV_TRAIN: dv_train(t, len); break;
    case DV_REDCARS: dv_redcars(t, len); break;
    case DV_FROGS: dv_frogs(t, len); break;
    case DV_COLUMNS: dv_columns(t, len); break;
    case DV_WALKWAY: dv_walkway(t, len); break;
    case DV_LAMP: dv_lamp(t, len); break;
    case DV_DOOR: dv_door(t, len); break;
    case DV_PORTRAIT: dv_portrait(t, len); break;
    default: dv_torpedo(t, len); break;
    }
}
#endif
static int midi_pitch; /* the MIDI page: gumballs or the pitch tracker */
#ifdef AERIAOS
static void midi_mod_set(int m) { midi_mod = m & 3; midi_pitch = midi_mod == 1; }
static const char *module_title(void)
{
    static const char *const mn[4] = {"GUMBALLS", "PITCH", "MARBLES", "PONG"};
    return out_open ? "OUT" : cloud_open ? "GRAIN CLOUD" : page == PG_MIDI ? mn[midi_mod & 3] : page_name[page];
}
#define MODULE_TITLE module_title()
#else
#define MODULE_TITLE (page == PG_MIDI && midi_pitch ? "PITCH" : page_name[page])
#endif
static int layer(void) { return func_held || func_latch; }
static void label(int x, const char *s) { for (int i = x - 2; i < x + text_w(s) + 2; i++) span(i, 0, 8, 0); text(x, 2, s, 1); }
static void panel_clear(int y0, int y1) { for (int x = 0; x < W; x++) span(x, y0, y1, 0); }
static void upper(const char *in, char *out, int max) { int i = 0; for (; in[i] && i < max; i++) out[i] = in[i] >= 'a' && in[i] <= 'z' ? (char)(in[i] - 32) : in[i]; out[i] = 0; }

/* ---- CHAIN: eleven balloons on one string; the selected one's knobs below --------------------- */
static void draw_chain(void)
{
    panel_clear(10, 47);
    int y = 16;
    for (int x = 2; x < W - 2; x++) if (((x + (int)(ticks >> 2)) & 3) == 0) px(x, y);
    for (int s = 0; s < N_SLOTS; s++) {
        Slot *sl = &slots[s];
        int cx = 10 + s * 18, bob = sine[(int)(ticks + (uint32_t)s * 9) & 63] / 64, r = s == sel_slot ? 5 : 4, cy = y + bob;
        disk_clear(cx, cy, r + 1);
        if (sl->row <= 0) { for (int a = 0; a < 1024; a += 128) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
        else {
            circle(cx, cy, r);
            if (sl->pending < 0 && !((slot_bypass >> s) & 1)) for (int dy = 1; dy <= r; dy++) for (int dx = -r + 1; dx < r; dx++) if (dx * dx + dy * dy < r * r && dither_on(cx + dx, cy + dy, 8)) px(cx + dx, cy + dy);
        }
        if (sl->pending >= 0 && (ticks & 8)) circle(cx, cy, r + 2);
        if (s == sel_slot) for (int dx = -2; dx <= 2; dx++) px(cx + dx, cy + r + 3);
    }
    Slot *sl = &slots[sel_slot];
    if (sl->row <= 0) { text(30, 30, "EMPTY  YES: SHELF", 1); return; }
#if defined(AERIAOS)
    if (layer() && sl->row <= FX_COUNT) {          /* FUNC: the effect's second page (modes, freeze, makeup ...) */
        const FxInfo *fi = &fx_info[sl->row - 1]; int any = 0;
        for (int k = 0; k < 6; k++) {
            if (!fi->knob2[k][0]) continue;
            int c = fi->count2[k] > 1 ? fi->count2[k] - 1 : 127;
            bubble(11 + k * 21, 33, 7, preview_slot_p2[sel_slot][k] * 127 / c, fi->knob2[k], 0); any = 1;
        }
        if (!any) text(34, 30, "NO MORE KNOBS", 1);
        return;
    }
#endif
    for (int k = 0; k < 6; k++) {
        if (!sl->knob[k][0]) continue;
        int c = sl->count[k] > 1 ? sl->count[k] - 1 : 127;
        bubble(11 + k * 21, 33, 7, slot_value(sel_slot, k) * 127 / c, sl->knob[k], sl->user[k] < 0 && rule_of(sl->knob[k]) >= 0);
    }
}

/* the shelf: every effect this slot can hold */
static void draw_shelf(void)
{
    panel_clear(9, 56);
    int fx2 = slot_fx(sel_slot);
    shelf_build(fx2);
    int n = shelf_n;
    text(2, 10, "SHELF", 1);
    char where[8] = "SLOT ";
    num(where + 5, sel_slot + 1);
    text(W - 2 - text_w(where), 10, where, 1);
    for (int x = 0; x < W; x += 2) px(x, 16);
    int top = shelf_row - 2 < 0 ? 0 : shelf_row - 2;
    for (int r = 0; r < 6 && top + r < n; r++) {
        int i = top + r, yy = 19 + r * 6;
        char nm[14];
        list_name(fx2, shelf_rows[i], nm);
        if (shelf_rows[i] == 0) { nm[0] = '-'; nm[1] = 0; }
        if (i == shelf_row) { for (int x = 0; x < W; x++) span(x, yy - 1, yy + 5, 1); text(4, yy, nm, 0); }
        else text(4, yy, nm, 1);
    }
    text(2, 57, "YES PUT ON  NO BACK", 1);
}

static void draw_source(void)
{
    panel_clear(10, 47);
    static const char *const names[3] = {"LOOP", "STARS", "BALLOONS"};
    int on[3] = {src_loop, src_stars, src_balloons};
    for (int i = 0; i < 3; i++) {
        int y = 13 + i * 9;
        char n[2] = {(char)('1' + i), 0};
        text(18, y, n, 1);
        text(28, y, names[i], 1);
        text(72, y, on[i] ? "ON" : "OFF", 1);
        if (on[i]) for (int x = 70; x < 84; x++) px(x, y + 6);
    }
    text(4, 41, "FUNC+1 2: PICK FROM CARD", 1);
}

static void draw_system(void)
{
    panel_clear(10, 61);
    if (usb_mode) { text(20, 20, "USB DISK MODE", 1); text(14, 32, "NO: BACK TO THE SKY", 1); return; }
    text(4, 11, "1 USB DISK MODE", 1);
    text(4, 17, "2 OS UPGRADE  FUNC+YES", 1);
    text(4, 24, "A SOUND", 1); text(52, 24, pv(PG_SYSTEM, 0) == 0 ? "OFF" : pv(PG_SYSTEM, 0) == 1 ? "KNOBS" : "LANE", 1);
    text(4, 30, "B INTRO", 1); text(52, 30, pv(PG_SYSTEM, 1) ? "ON" : "OFF", 1);
    text(4, 36, "C LIGHTS", 1); text(52, 36, pv(PG_SYSTEM, 2) ? "ON" : "OFF", 1);
    text(4, 42, "D AUTOPLAY", 1); text(52, 42, pv(PG_SYSTEM, 3) ? "ON" : "OFF", 1);
    text(4, 48, "E SAVER", 1); text(52, 48, saver_name(pv(PG_SYSTEM, 4)), 1);
    char w[10]; num(w, saver_wait[pv(PG_SYSTEM, 5)]); int n = 0; while (w[n]) n++; w[n++] = ' '; w[n++] = 'M'; w[n++] = 'I'; w[n++] = 'N'; w[n] = 0;
    text(4, 54, "F WAIT", 1); text(52, 54, w, 1);
    text(96, 54, "YES TRY", 1);
}

static int param_page(int pg) { return pg == PG_SKY || pg == PG_LOOP || pg == PG_STARS || pg == PG_BALLOON || pg == PG_MIX || pg == PG_GAIN || pg == PG_MASTER; }
#ifdef AERIAOS
/* ---- the loop's waveform (sadnoise asked, 12 Nov 2026): the sample as an island on AERIA's sea -----------------
 * Each column of the sample is a piece of skyline as tall as it is loud, standing on the waterline; the part the
 * loop plays is solid and mirrored in the water (a dithered reflection that ripples), the rest a faint shore.
 * START and END are two buoys with pennants; a little boat sails the waterline where the loop is playing. */
const uint8_t *aos_loop_wave(void);
int aos_loop_at(void);
static void loop_wave_draw(void)
{
    const int y0 = 7, base = 16, top = 9, y1 = 21;           /* the picture: rows 7..21, the waterline at 16 */
    panel_clear(y0, y1);
    const uint8_t *w = aos_loop_wave();
    int a = pv(PG_LOOP, 0), b = pv(PG_LOOP, 1);
    if (b <= a) b = a + 1;
    if (b > 127) b = 127;
    if (!w) { text(34, 11, "NO LOOP LOADED", 1); return; }
    for (int x = 0; x < W; x++) {
        int h = 1 + w[x] * (base - top) / 255, in = x >= a && x < b;
        if (in) {
            span(x, base - h, base, 1);
            /* the reflection: shorter, dithered, each row swaying a pixel with the tide */
            for (int d = 1; d <= (h * 2) / 3 && base + d <= y1; d++) { int sx = x + (((int)(ticks >> 4) + d) & 3) / 3; if (dither_on(sx, base + d, 7 - d)) px(x, base + d); }
        } else {
            for (int y = base - h; y <= base; y++) if (dither_on(x, y, 5)) px(x, y);
        }
        if ((x & 3) == ((int)(ticks >> 5) & 3)) px(x, base + 1);                       /* the waterline's glint */
    }
    /* the buoys: a pole from the water up past the skyline, a pennant pointing into the loop */
    for (int k = 0; k < 2; k++) {
        int x = k ? b : a, dir = k ? -1 : 1;
        for (int y = y0; y <= base + 2; y++) { px_off(x - 1 < 0 ? 0 : x - 1, y); px_off(x + 1 > W - 1 ? W - 1 : x + 1, y); px(x, y); }
        px(x + dir, y0); px(x + 2 * dir, y0 + 1); px(x + dir, y0 + 1); px(x + dir, y0 + 2);
        px(x - 1, base + 3); px(x + 1, base + 3);
    }
    /* the boat, where the loop is */
    int at = aos_loop_at();
    if (at >= 0) {
        int x = clamp(at, 2, W - 3), bob = ((ticks >> 4) & 1);
        for (int dx = -2; dx <= 2; dx++) { px_off(x + dx, base - 1 + bob); px_off(x + dx, base - 2 + bob); px_off(x + dx, base - 3 + bob); px_off(x + dx, base - 4 + bob); }
        for (int dx = -2; dx <= 2; dx++) px(x + dx, base - 1 + bob);
        px(x - 1, base + bob); px(x, base + bob); px(x + 1, base + bob);
        px(x, base - 2 + bob); px(x, base - 3 + bob); px(x, base - 4 + bob); px(x + 1, base - 3 + bob);
    }
}
#endif
static void draw_page(void)
{
    if (br_mode) { draw_browser(); return; }
    if (usb_mode && page == PG_SYSTEM) { draw_system(); return; }
#ifdef AERIAOS
    if (ticks > ui_until && !shelf_open && !func_held && !out_open && !cloud_open && !(page == PG_MIDI && midi_mod >= 2)) return;
#else
    if (ticks > ui_until && !shelf_open && !func_held) return; /* idle: the landscape alone */
#endif
#ifdef AERIAOS
    if (out_open) { out_draw(); return; }
    if (cloud_open) { cloud_draw(); return; }
    if (page == PG_MIDI && midi_mod == 2) { marbles_draw(); return; }
    if (page == PG_MIDI && midi_mod == 3) { pong_draw(); return; }
#endif
    if (page == PG_CHAIN && setup_busy()) { panel_clear(10, 47); text(26, 22, "FILLING THE SHELF", 1); for (int i = 0; i < N_SETUP; i++) px(30 + i * 5, 32), px(30 + i * 5, 33), (void)0; for (int i = 0; i <= setup_at && i < N_SETUP; i++) { span(29 + i * 5, 30, 35, 1); span(30 + i * 5, 30, 35, 1); } return; }
    if (page == PG_CHAIN) { if (shelf_open) draw_shelf(); else draw_chain(); return; }
    if (page == PG_SOURCE) { draw_source(); return; }
    if (page == PG_SYSTEM) { draw_system(); return; }
    if (page == PG_MIDI) { if (midi_pitch) pitch_draw(); else gum_draw(); return; }
    if (!param_page(page)) return;
    panel_clear(22, 47);
    for (int x = 0; x < W; x += 2) { px(x, 22); px(x, 47); }
    int row = layer() ? 6 : 0;
    for (int i = 0; i < 6; i++) {
        Param *p = &params[page][row + i];
        if (!p->name[0]) continue;
        int sv = p->hi - p->lo ? p->hi - p->lo : 1;
        bubble(11 + i * 21, 32, 7, (out_value(page, row + i) - p->lo) * 127 / sv, p->name, 0);
    }
    if (row) text(W / 2 - 7, 24, "FUNC", 1);
#ifdef AERIAOS
    if (page == PG_LOOP) loop_wave_draw();
    if (page == PG_MIX || page == PG_GAIN) {   /* the levels: A B C D, then main L R */
        int aos_meter(int i);
        static const char *const ml[6] = {"A", "B", "C", "D", "L", "R"};
        for (int x = 0; x < W; x++) span(x, 48, 55, 0);
        for (int i = 0; i < 6; i++) {
            int x = 4 + i * 21 + (i >= 4 ? 4 : 0), lv = aos_meter(i);
            text(x, 49, ml[i], 1);
            for (int k = 0; k < 16; k++) { int xx = x + 5 + k; if (k < lv) span(xx, 49, 53, 1); else if ((k & 3) == 0) px(xx, 53); }
        }
    }
#endif
}

static void draw_hud(void)
{
    char title[14];
#ifdef AERIAOS
    if (page == PG_CHAIN && !shelf_open && !br_mode && !out_open && !cloud_open) upper(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", title, 12);
#else
    if (page == PG_CHAIN && !shelf_open && !br_mode) upper(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", title, 12);
#endif
    else upper(br_mode ? (br_mode == BR_LOOP ? "LOOP" : br_mode == BR_PONG ? "PONG" : br_mode == BR_SEQ ? "KIT" : "STARS") : MODULE_TITLE, title, 12);
    int showing = br_mode || shelf_open || usb_mode || ticks <= ui_until || func_held;
    if (showing) label(2, title);
    static char wxn[12];
    { int n = 0, cnt = 0; for (int i = 1; i < N_WEATHER; i++) cnt += WON(i);
      if (cnt <= 1) { const char *p = weather_name[cnt ? weather : CLEAR]; while (p[n] && n < 11) { wxn[n] = p[n]; n++; } }
      else for (int i = 1; i < N_WEATHER && n < 10; i++) if (WON(i)) { if (n) wxn[n++] = '+'; wxn[n++] = weather_name[i][0]; wxn[n++] = weather_name[i][1]; }
      if (xw_on && n < 9) { if (n) wxn[n++] = '+'; wxn[n++] = xw_on & 1 ? 'W' : xw_on & 2 ? 'H' : 'A'; wxn[n++] = xw_on & 1 ? 'I' : xw_on & 2 ? 'A' : 'U'; }
      wxn[n] = 0; }
    const char *w = wxn;
    label(W - 2 - text_w(w) - (auto_weather ? 8 : 0), w);
    if (auto_weather) label(W - 5, "A");
#ifdef AERIAOS
    if (showing && !br_mode && !out_open && !cloud_open && (page == PG_CHAIN || page == PG_MIX)) {
#else
    if (showing && !br_mode && (page == PG_CHAIN || page == PG_MIX)) {
#endif
        static const char *const ab[5] = {"-", "AB", "A", "B", "A+B"}, *const cd[5] = {"-", "CD", "C", "D", "C+D"};
        for (int i = 0; i < 60; i++) span(i, 57, 63, 0);
        text(1, 58, ab[pv(PG_MIX, 6)], 1); text(18, 58, cd[pv(PG_MIX, 7)], 1);
        if (pv(PG_MIX, 0)) { char f[8] = "FB "; num(f + 3, pv(PG_MIX, 0)); text(34, 58, f, 1); }
    }
    if (pv(PG_SYSTEM, 0) != 1) { const char *m = pv(PG_SYSTEM, 0) == 0 ? "SND OFF" : "LANE"; for (int i = W - text_w(m) - 3; i < W; i++) span(i, 57, 63, 0); text(W - text_w(m) - 1, 58, m, 1); }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 48, 56, 1);
        text(x, 50, toast, 0);
    }
}

/* ---- LEDs: red and white, undulating (sadnoise, 9 Nov 2026) -----------------------------------------------
 * Every known lamp: channel A white, channel B red (B5 palette entries through stock's builder); REC 1-3 left to
 * the recorder. The trig row breathes: two slow waves roll across it, white crests and red troughs, the level
 * eased a step at a time so it moves smoothly. A trig with something to say rises out of the wave:
 *   1 (was red)   red, bright, breathing with the wave
 *   2 (was green) white, full
 *   3 (flash)     white full over red full */
#ifndef PREVIEW
#define led_palette ((void (*)(int, int, int, int, int))0x40013368u)   /* (id, register 0-2, r, g, b) */
#define led_level ((void (*)(int, int))0x400135b0u)                  /* (id, level 0-15), cached */
#else
static void led_palette(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void led_level(int a, int b) { (void)a; (void)b; }
#endif
/* the A ids of every lamp in the measured map (aeria-os/HARDWARE.md 3.4), not REC 1-3 */
static const uint8_t white_lamps[] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30, 40,42,44,46,48,50,52,54,
                                      32,34,38, 64,66,68,70, 80,82,86, 84, 88,90, 102, 120,122};
static uint32_t white_at;
static void leds_white(void)
{
#ifndef PREVIEW
    /* stock's own colour level for the panel's LED intensity setting, as Modwerk's boot animation uses it */
    uint32_t in = *(volatile uint32_t *)0x800000d0u; int w = (int)((const uint32_t *)0x400abc26u)[in > 2 ? 2 : in];
#else
    int w = 255;
#endif
    for (unsigned i = 0; i < sizeof white_lamps; i++)
        for (int reg = 0; reg < 3; reg++) {
            led_palette(white_lamps[i], reg, w, w, w);       /* A: white */
            led_palette(white_lamps[i] + 1, reg, w, w, w);   /* B: white too (sadnoise: all white) */
        }
}
static uint8_t led_want[16];
/* all white (sadnoise, 9 Nov 2026): a trig's state is its brightness -- 1 dim, 2 bright, 3 full */
static void leds_out(void)
{
    static const uint8_t level_of[4] = {0, 4, 15, 15};
    if (!white_at || ticks - white_at > 3600u) { leds_white(); white_at = ticks | 1u; }   /* again each minute, in case stock re-colours */
    leds_clear_trigs();
    for (int k = 0; k < 16; k++) if (led_want[k]) { led_level(2 * k, level_of[led_want[k] & 3]); set_led(2 * k, 3); }
}
#ifdef AERIAOS
#define INTRO_FROM 0     /* AERIA OS: nothing drew before it -- the whole intro, from the first brush stroke */
#else
#define INTRO_FROM 20    /* the AERIA base's startup has drawn the logo's outline already */
#endif
static void leds_tick(void)
{
    for (int k = 0; k < 16; k++) led_want[k] = 0;
    if (intro < INTRO_LEN) {
        int t = intro;
        if (t < 90) led_want[clamp(t * 16 / 90, 0, 15)] = 2;
        else if (t < 168) { for (int k = 0; k <= (t - 95) / 4 && k < 11; k++) led_want[k] = 1; }
        else if (t < 176) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 3); set_led(39, 3); }
        else for (int k = 0; k < 16; k++) if ((k * 7 + t / 3) % 5 == 0) led_want[k] = 2;
        leds_out();
        return;
    }
    if (!pv(PG_SYSTEM, 2)) return;
#ifdef SEQ_UI
    if (seq_on && !br_mode && !preset_mode && sq_view != SV_MASTER) { seq_leds(); return; }
#endif
    if (page == PG_CHAIN) {
        /* 1-11 the slots: green the selected, red holding an effect, dark empty; 12-16 weather tools */
        for (int s = 0; s < N_SLOTS; s++) led_want[s] = s == sel_slot ? 2 : (slots[s].row > 0 && !((slot_bypass >> s) & 1)) ? 1 : 0;
        if (slots[sel_slot].pending >= 0 && (ticks & 8)) led_want[sel_slot] = 3;
        led_want[11] = 1; led_want[12] = 1; led_want[13] = 1; /* shelf, weather down, weather up */
        led_want[14] = auto_weather ? 2 : 0;
        led_want[15] = thunder ? 3 : 0;
        if (thunder > 150) for (int s = 0; s < N_SLOTS; s++) led_want[s] = 3;
    } else if (page == PG_SKY) {
        for (int i = 1; i < N_WEATHER; i++) if (WON(i)) led_want[i] = 2;
        if (!wx_on) led_want[CLEAR] = 2;
        led_want[7] = auto_weather ? 2 : (uint8_t)((ticks >> 5) & 1);
        for (int i = 0; i < XW_N; i++) led_want[8 + i] = (xw_on >> i) & 1 ? 2 : 1;
        led_want[11] = ticks < tw_until ? 3 : 1;
    } else if (page == PG_SOURCE) {
        led_want[0] = src_loop ? 2 : 1; led_want[1] = src_stars ? 2 : 1; led_want[2] = src_balloons ? 2 : 1;
    } else if (page == PG_STARS) {
        for (int p = 0; p < PLANETS; p++) if ((planet_mask >> p) & 1) led_want[p] = 1;
        if (planet_hit >= 0 && star_flash) led_want[planet_hit] = 2;
    } else if (page == PG_BALLOON) {
        for (int b = 0; b < 3; b++) led_want[b] = balloon[b].state == B_FLOATING ? ((balloon_quiet >> b) & 1 ? 1 : 2) : (balloon[b].state == B_FILLING && (ticks & 8)) ? 1 : 0;
    } else if (page == PG_MASTER) {
        master_leds(led_want);
    } else if (page == PG_LOOP) {
        int a = pv(PG_LOOP, 0) * 16 / 128, b = pv(PG_LOOP, 1) * 16 / 128;
        for (int k = 0; k < 16; k++) if (k >= a && k <= b) led_want[k] = 1;
    } else if (page == PG_MIDI && midi_pitch) {
        led_want[pt_mode] = 2; led_want[15] = 1;
    } else if (page == PG_MIDI) {
        /* 1-4 drop (green holding gumballs, yellow while one sings), 9-12 take one out (red) */
        for (int g = 0; g < GB_BALLOONS; g++) {
            int n = gum_count(g);
            led_want[g] = n ? (gum_on && gum_sounding(g) ? 3 : 2) : 0;
            led_want[8 + g] = n ? 1 : 0;
        }
    } else if (page == PG_GAIN) {
        /* 1-8 the input gain, 9-16 the output: green to unity, yellow above */
        int in = pv(PG_GAIN, 0) * 8 / 128, out = pv(PG_GAIN, 3) * 8 / 128;
        for (int k = 0; k < 8; k++) { if (k <= in) led_want[k] = k >= 4 ? 3 : 2; if (k <= out) led_want[8 + k] = k >= 6 ? 3 : 2; }
    }
#ifdef AERIAOS
    if (out_open) out_leds();
    else if (page == PG_MIDI && midi_mod == 2) marbles_leds();
    else if (page == PG_MIDI && midi_mod == 3) pong_leds();
    if (preset_mode) preset_leds();
#endif
    if (gust) for (int k = 0; k < 16; k++) if (((int)(ticks >> 1) - k) % 16 == 0) led_want[k] = 3;
    if (flash) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 2); set_led(39, 2); }
    leds_out();
}

/* ---- diagnostics, cycling every 2 s: tag in the top nibble ------------------------------------ */
static uint32_t diag_word(void)
{
    int w = (int)(ticks / 120u) % 3;
    if (w == 0)
        return 0x10000000u | (uint32_t)playing() << 27 | (uint32_t)rig_loaded() << 26 | (uint32_t)((pat_off + 100) & 0x3f) << 20
               | (master_step() & 0xffu) << 12 | (stars_total & 0xffu) << 4 | (uint32_t)(src_loop | src_stars << 1 | src_balloons << 2);
    if (w == 1) return 0x30000000u | (uint32_t)page << 20 | (uint32_t)sel_slot << 16 | (uint32_t)(slots[sel_slot].id & 0xff) << 8 | (uint32_t)(picking + 1);
    return 0x40000000u | (uint32_t)(list_count(0) & 0xff) << 16 | (uint32_t)(list_count(1) & 0xff) << 8 | (uint32_t)(slots[0].id & 0xff);
}

/* ---- hooks --------------------------------------------------------------------------------------- */
static void strike(void) { bolt_x = range(16, W - 16); bolt_t = 9; flash = 3; thunder = 200; }
static int autoplayed, setup_done;
/* the screensaver: after WAIT minutes without a key or a knob, SAVR (or a random one) takes the screen */
static int saver_on;
static uint32_t last_input;
static void saver_go(void)
{
    int s = pv(PG_SYSTEM, 4);
    if (s <= SS_OFF || s >= SS_COUNT) s = range(SS_PADDLE, SS_COUNT - 1);
    ss_start(s); saver_on = 1;
}

/* AERIA ROSE in the chain: its rose window hangs in the sky. FOLD (knob 3) sets the foils; the eight panes
 * glint in turn, as the resonator's modes do when ROTA moves the light round. */
static int rose_slot(void)
{
    for (int k = 0; k < N_SLOTS; k++) {
        const char *n = slots[k].name; const char *w = "AERIA ROSE"; int i = 0;
        if (slots[k].row <= 0 || ((slot_bypass >> k) & 1)) continue;
        while (w[i] && n[i] == w[i]) i++;
        if (!w[i]) return k;
    }
    return -1;
}
static void draw_rose_window(void)
{
    int s = rose_slot();
    if (s < 0) return;
    int n = slot_value(s, 2) + 3, cx = 20, cy = 24, r = 13;
    disk_clear(cx, cy, r + 1);
    circle(cx, cy, r); circle(cx, cy, r / 3);
    for (int k = 0; k < n; k++) {
        int a = k * 1024 / n + (int)(ticks / 3);
        int fx = cx + cos10(a) * (r * 6 / 10) / 127, fy = cy + sin10(a) * (r * 6 / 10) / 127;
        int pr = (r * 6 / 10) * 3 / n; circle(fx, fy, pr < 2 ? 2 : pr > 4 ? 4 : pr);
        for (int t = r / 3; t < r; t++) px(cx + cos10(a + 512 / n) * t / 127, cy + sin10(a + 512 / n) * t / 127);
    }
    /* the eight panes: light walks round them */
    int lit = (int)(ticks / 12) & 7;
    for (int k = 0; k < 8; k++) {
        int a = k * 128, d = (k - lit) & 7, lv = d == 0 ? 14 : d == 1 || d == 7 ? 8 : 3;
        int px0 = cx + cos10(a - 256) * (r - 3) / 127, py0 = cy + sin10(a - 256) * (r - 3) / 127;
        if (lv > 3) { px(px0, py0); if (lv > 8) { px(px0 + 1, py0); px(px0, py0 + 1); px(px0 + 1, py0 + 1); } }
    }
}

/* the tick's own profile (dev): timer counts per section, summed in battery RAM nothing else uses (0x100fb000),
 * read with `device.mjs mem 0x100fb000 48` -- [0] 'PROF', [1] ticks, [2..] sections */
#ifdef PREVIEW
#define PROF_AT(k) ((void)0)
#else
static uint32_t prof_t;
#define PROF_AT(k) do { uint32_t n_ = *(volatile uint32_t *)0xFC07C00Cu; ((volatile uint32_t *)0x100FB000u)[2 + (k)] += n_ - prof_t; prof_t = n_; } while (0)
#endif
void module_tick(struct modwerk_runtime_api *api)
{
#ifndef PREVIEW
    /* AERIA OS bring-up: snapshot the GPIO block (0xFC0A4000-0xFC0A40FF) while stock makes sound, into battery RAM
     * at 0x100FB100 (read with `device.mjs mem 0x100fb100 256`), every 10 s */
    if ((ticks % 600u) == 300u) { volatile uint8_t *g = (volatile uint8_t *)0xFC0A4000u, *o = (volatile uint8_t *)0x100FB100u; for (int i = 0; i < 256; i++) o[i] = g[i]; }
    ((volatile uint32_t *)0x100FB000u)[0] = 0x50524F46u; ((volatile uint32_t *)0x100FB000u)[1]++;
    prof_t = *(volatile uint32_t *)0xFC07C00Cu;
#endif
    /* the AERIA base's startup has drawn the logo's outline (aeria-boot), so the intro goes straight to the
     * brush painting it in, without the blank lead. FUNC+YES on SYSTEM still plays it whole. */
    if (!ready) { new_sky(); new_constellation(); slots_init(); spell_left = 3600u; ready = 1; intro = pv(PG_SYSTEM, 1) ? INTRO_FROM : INTRO_LEN; }
    ticks++;
    if (ticks == 30 || (ticks % 300) == 0) for (int s = 0; s < N_SLOTS; s++) if (slots[s].pending < 0 && (slots[s].id != live_fx(slot_track(s), slot_fx(s)) || ticks == 30)) { slot_refresh(s); slots[s].pending = -1; }
#ifdef AERIAOS
    if (ticks == 2) aeria_state_restore();
    if (ticks == 31) aeria_state_reapply_slots();
    aeria_state_tick();
#ifdef SEQ_UI
    seq_ui_tick();
#endif
#ifdef DREAM_SKY
    dream_sky_tick();
#endif
#endif
    if (intro < INTRO_LEN) intro++;
    if (amount < 256) amount += 2;
    if (flash) flash--;
    if (bolt_t) bolt_t--;
    if (star_flash) star_flash--;
    if (gust) gust--;
    if (thunder) thunder = thunder > 3 ? thunder - 3 : 0;
    night = pv(PG_SKY, 10);
    write_mode = pv(PG_SYSTEM, 0);
    PROF_AT(0);
    usb_tick();
    PROF_AT(1);
    step_auto();
    PROF_AT(2);
    if (WON(STORM) && amount > 128 && (rnd() % 420) == 0) strike();
    int want = clouds_wanted(), wind = wind_now();
    for (int i = 0; i < MAX_CLOUDS; i++) {
        Cloud *c = &clouds[i];
        c->x += wind * (16 + 24 * c->depth) + (wind == 0 ? 4 : 0);
        int x = c->x >> 8;
        if (x > W + 2 && wind >= 0) make_cloud(c, -c->w - range(0, i >= want ? 200 : 30));
        if (x < -CLOUD_MAX_W - 2 && wind < 0) make_cloud(c, W + range(0, i >= want ? 200 : 30));
    }
    move_precip();
    balloons_tick();
    birds_tick();
    drift_tick();
    picks_tick();
    PROF_AT(3);
    if (!setup_done && ticks > 120 && rig_loaded()) { setup_done = 1; setup_start(); }
    setup_tick();
    deal_tick();
    loop_tick();
    preview_tick();
    PROF_AT(4);
    if (!usb_mode) { gum_tick(); pitch_tick(); }
#ifdef AERIAOS
    pan_tick(); cloud_tick(); scale_tick(); master_tick(); marbles_tick(); pong_tick();
#endif
    PROF_AT(5);
    if (saver_on) { if (usb_mode || br_mode) { saver_on = 0; ss_stop(); } else ss_tick(); }
    else if (pv(PG_SYSTEM, 4) != SS_OFF && intro >= INTRO_LEN && !usb_mode && !br_mode && !shelf_open &&
             ticks - last_input > (uint32_t)saver_wait[pv(PG_SYSTEM, 5)] * 3600u) saver_go();
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -40 * 256;
    if ((fin_x >> 8) < -40) fin_x = (W + 20) * 256;
    if (!hidden) leds_tick();
    PROF_AT(6);
    /* the active track stays on the chain's head (FRACT and stock draw for the active track) */
    if (!hidden && !usb_mode && (ticks & 15) == 0 && current_track() != HEAD_T) set_current_track(HEAD_T);
    /* the effects run as soon as the sky opens: start the transport once */
    if (!autoplayed && ticks > 600 && pv(PG_SYSTEM, 3) && rig_loaded() && !playing() && !usb_mode) { transport_start(); autoplayed = 1; }
    PROF_AT(7);
    sequencer_tick();
    PROF_AT(8);
    if (!usb_mode) sound_tick();
#ifdef AERIAOS
    wx_tick();
    xw_tick();
#endif
    PROF_AT(9);
    api->value = diag_word();
}

static void module_draw_body(unsigned char *frame);
void module_draw(unsigned char *frame)
{
#ifndef PREVIEW
    uint32_t t0 = *(volatile uint32_t *)0xFC07C00Cu;
    module_draw_body(frame);
    ((volatile uint32_t *)0x100FB000u)[12] += *(volatile uint32_t *)0xFC07C00Cu - t0;
    ((volatile uint32_t *)0x100FB000u)[13]++;
#else
    module_draw_body(frame);
#ifdef AERIAOS
    if (preset_mode && ready && !hidden && intro >= INTRO_LEN && !saver_on) preset_draw();
#endif
#endif
}

static void module_draw_body(unsigned char *frame)
{
    if (hidden || !ready) return;
    F = (uint32_t *)frame;
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (intro < INTRO_LEN) { draw_intro(); return; }
    if (saver_on) { ss_draw(); return; }
#ifdef SEQ_UI
    if (seq_on && !br_mode && sq_view != SV_MASTER) { seq_draw(); return; }
#endif
    draw_sun_moon();
    int want = clouds_wanted();
    for (int depth = 1; depth <= 3; depth++) for (int i = 0; i < want; i++) if (clouds[i].depth == depth) draw_cloud(&clouds[i]);
    draw_rose_window();
    draw_heat();
    draw_fog();
    draw_precip();
#ifdef AERIAOS
    xw_draw();
#endif
    draw_bolt();
    draw_water();
#ifdef DREAM_SKY
    dream_sky_draw();                                          /* a visit from the dream log, now and then */
#endif
    if (page == PG_STARS && !br_mode) draw_stars();
    if (src_balloons || page == PG_BALLOON) draw_balloons();
    draw_page();
    draw_hud();
    if (flash) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
}

static void show_page(int pg) { page = pg; shelf_open = 0; page_until = (int)ticks + 240; say(page_name[pg], 0); }
static void choose_weather(int w) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; set_weather(w); }
static void toggle_auto(void) { auto_weather = !auto_weather; P(PG_SKY, 4).value = P(PG_SKY, 4).user = (int16_t)auto_weather; say("AUTO", auto_weather ? "ON" : "OFF"); }

/* bypass: the slot gives its place to NONE and remembers what it held */
static int16_t bypass_row[N_SLOTS];
static void toggle_bypass(int s)
{
    if (picking >= 0) return;
    if ((slot_bypass >> s) & 1) { slot_bypass &= (uint16_t)~(1u << s); if (bypass_row[s] > 0) slot_pick(s, bypass_row[s]); }
    else if (slots[s].row > 0) { bypass_row[s] = slots[s].row; slot_bypass |= (uint16_t)(1u << s); slot_pick(s, 0); say(slots[s].name, "OFF"); }
}
static void select_slot(int s) { sel_slot = s; shelf_open = 0; page = PG_CHAIN; say(slots[s].row > 0 ? slots[s].name : "EMPTY", 0); }

int module_key(int code, int pressed)
{
    last_input = ticks;
    if (saver_on && !hidden) {
        /* a game takes its keys; any other press (or NO) wakes the sky, and the press is spent */
        if (code == KEY_PLAY || code == KEY_STOP) return 0;
        if (pressed && code == KEY_NO) { saver_on = 0; ss_stop(); ui_until = 0; return 1; }
        if (ss_key(code, pressed)) return 1;
        if (pressed && code != KEY_FUNC) { saver_on = 0; ss_stop(); ui_until = 0; }
        return 1;
    }
    if (pressed && code != KEY_PLAY && code != KEY_STOP) ui_until = ticks + UI_SHOW;
    if (code == KEY_FUNC) { func_held = pressed; if (pressed) page_until = (int)ticks + 240; }
    if (code == KEY_NO && pressed && func_held && !usb_mode) { hidden = !hidden; if (!hidden) show_page(page); return 1; }
    if (hidden) return 0;
    if (popup_open() && !usb_mode) return 0; /* a stock or base message: its keys */
    if (usb_mode) { if (pressed && code == KEY_NO) usb_leave(); return 1; }
#ifdef SEQ_UI
    if (seq_key(code, pressed)) return 1;                      /* FUNC + MIDI: the sequencer; in it, its own keys */
#endif
#ifdef AERIAOS
    if (code == KEY_CUE && !br_mode) { if (pressed) { out_open = !out_open; cloud_open = 0; shelf_open = 0; say(out_open ? "OUT" : page_name[page], 0); } return 1; }
    if (code == KEY_REC && !br_mode) { if (pressed) { cloud_open = !cloud_open; out_open = 0; say(cloud_open ? "GRAIN CLOUD" : page_name[page], 0); } return 1; }
#endif
    if (code == KEY_PLAY || code == KEY_STOP || code == KEY_REC || code == KEY_CUE || code == 0x18) return br_mode ? 1 : 0;
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) { if (pressed) held_tracks |= 1u << (code - KEY_TRACK0); else held_tracks &= ~(1u << (code - KEY_TRACK0)); }
    if (code == KEY_SCENEA || code == KEY_SCENEB) {
        scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0;
        if (pressed) { char n[4]; num(n, (scene_held == 1 ? scene_a : scene_b) + 1); say(scene_held == 1 ? "SCENE A IS" : "SCENE B IS", n); }
        return 1;
    }
    if (scene_held && pressed && code == KEY_NO) {              /* the scene forgets all it holds */
        int sc = scene_held == 1 ? scene_a : scene_b; char n[4]; num(n, sc + 1);
        for (int i = 0; i < SCENE_IDS; i++) scene_val[sc][i] = -1;
        scene_dirty = 1; say("SCENE EMPTY", n); return 1;
    }
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (code != n->key) continue;
        if (func_held) { if (pressed && n->state != B_EMPTY && n->state != B_FILLING) { n->state = B_POPPING; n->pop = 12; AOS_BALLOON(b, 0); } return 1; }
        set_current_track(n->track);
        if (pressed) {
            if (n->state == B_FILLING) { n->state = B_FLOATING; balloon_voice = b; AOS_BALLOON(b, 2); say("BALLOON", b == 0 ? "1 UP" : b == 1 ? "2 UP" : "3 UP"); }
            else { n->state = B_FILLING; n->size = 0; n->y = WATER_Y - 12; AOS_BALLOON(b, 1); say("CATCHING", b == 0 ? "A B" : b == 1 ? "C D" : "SKY"); }
        }
        return 0;
    }
#ifdef AERIAOS
    if (intro >= INTRO_LEN && preset_key(code, pressed)) return 1;
    if ((out_open || cloud_open) && pressed && code == KEY_NO && !func_held) { out_open = cloud_open = 0; say(page_name[page], 0); return 1; }
    if (out_open && out_key(code, pressed)) return 1;
    if (page == PG_MIDI && midi_mod >= 2 && pressed && !out_open && !cloud_open && (midi_mod == 2 ? marbles_key(code) : pong_key(code))) return 1;
    if (page == PG_MASTER && pressed && !out_open && !cloud_open && !br_mode && master_key(code)) return 1;
#endif
    if (!pressed) return 1;
    if (intro < INTRO_LEN) { intro = INTRO_LEN; return 1; }
    if (br_mode) { browser_key(code); return 1; }

    /* the shelf is open: UP/DOWN or LEVEL choose, YES puts it on, NO goes back */
    if (shelf_open) {
        shelf_build(slot_fx(sel_slot));
        if (code == KEY_UP && shelf_row > 0) shelf_row--;
        else if (code == KEY_DOWN && shelf_row < shelf_n - 1) shelf_row++;
        else if (code == KEY_YES) { shelf_open = 0; slot_bypass &= (uint16_t)~(1u << sel_slot); slot_pick(sel_slot, shelf_rows[shelf_row]); }
        else if (code == KEY_NO) shelf_open = 0;
        return 1;
    }

    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (scene_held) {                                        /* which of the sixteen scenes this side holds */
            char n[4]; num(n, k + 1);
            if (scene_held == 1) scene_a = (uint8_t)k; else scene_b = (uint8_t)k;
            scene_dirty = 1; say(scene_held == 1 ? "SCENE A IS" : "SCENE B IS", n);
        } else if (page == PG_CHAIN) {
            if (k < N_SLOTS) { if (func_held) toggle_bypass(k); else select_slot(k); }
            else if (k == 11 || k == 7) { shelf_open = 1; shelf_build(slot_fx(sel_slot)); shelf_row = shelf_index_of(slots[sel_slot].row); }
            else if (k == 12) choose_weather(clamp(weather - 1, 0, N_WEATHER - 1));
            else if (k == 13) choose_weather(clamp(weather + 1, 0, N_WEATHER - 1));
            else if (k == 14) toggle_auto();
            else { strike(); say("LIGHTNING", 0); }
        } else if (page == PG_SOURCE) {
            if (k == 0) { if (func_held) browser_open(BR_LOOP); else { src_loop = !src_loop; say("LOOP", src_loop ? "ON" : "OFF"); } }
            else if (k == 1) { if (func_held) browser_open(BR_STARS); else { src_stars = !src_stars; say("STARS", src_stars ? "ON" : "OFF"); } }
            else if (k == 2) { src_balloons = !src_balloons; say("BALLOONS", src_balloons ? "ON" : "OFF"); }
        } else if (page == PG_SYSTEM) {
            if (k == 0) usb_enter();
            else if (k == 1) say("OS UPGRADE", "FUNC+YES");
        } else if (page == PG_STARS) { planet_mask ^= (uint16_t)(1u << k); say("PLANET", (planet_mask >> k) & 1 ? "ON" : "OFF"); }
        else if (page == PG_MIDI) { if (midi_pitch) pitch_key(code); else gum_key(code); }
        else if (page == PG_LOOP) { Param *p = &P(PG_LOOP, func_held ? 1 : 0); p->value = p->user = (int16_t)(k * 127 / 15); }
        else if (page == PG_BALLOON) { if (k < 3 && balloon[k].state == B_FLOATING) { balloon_quiet ^= (uint8_t)(1u << k); balloon_voice = k; say(k == 0 ? "BALLOON 1" : k == 1 ? "BALLOON 2" : "BALLOON 3", (balloon_quiet >> k) & 1 ? "QUIET" : "SINGING"); } else if (k >= 8) { Param *p = &P(PG_BALLOON, 4); p->value = p->user = (int16_t)((k - 8) * 127 / 7); } }
        else if (k >= 8 && k <= 11) { if (k == 11) xw_twister(); else xw_toggle(k - 8); }
        else { if (k < 7) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; toggle_weather(k); } else if (k == 7) toggle_auto(); else if (k == 15) { strike(); say("LIGHTNING", 0); } }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) {
        int k = code - KEY_TRACK0;
        if (k < 4 && (held_tracks & (k < 2 ? 0x3u : 0xcu)) == (k < 2 ? 0x3u : 0xcu)) {
            Param *p = &P(PG_MIX, k < 2 ? 6 : 7);
            p->value = p->user = (int16_t)(pv(PG_MIX, k < 2 ? 6 : 7) == 1 ? 4 : 1);
            say(k < 2 ? "A B" : "C D", p->value == 1 ? "STEREO" : "MONO");
            return 1;
        }
        if (k < N_SLOTS) { if (func_held) toggle_bypass(k); else select_slot(k); }
        return 1;
    }
    if (code == KEY_SRC) { show_page(PG_CHAIN); return 1; }
    if (code == KEY_AMP) { show_page(PG_SKY); return 1; }
    if (code == KEY_LFO) { show_page(PG_SOURCE); return 1; }
    if (code == KEY_FX1) { show_page(page == PG_MIX ? PG_GAIN : PG_MIX); return 1; }
    if (code == 0x30) { show_page(PG_MASTER); return 1; }                       /* the MIX key: the master's freeze delay and the tempo */
    if (code == KEY_FX2 || code == KEY_PROJ) { show_page(PG_SYSTEM); return 1; }
    if (code == KEY_PART) { show_page(PG_BALLOON); return 1; }
    if (code == KEY_AED) { show_page(PG_LOOP); return 1; }
    if (code == KEY_ARR) { show_page(PG_STARS); return 1; }
    if (code >= KEY_PUSH0 && code < KEY_PUSH0 + 6) {
        int k = code - KEY_PUSH0;
        if (page == PG_CHAIN) { slots[sel_slot].user[k] = -1; say(slots[sel_slot].knob[k], "SKY"); }
    }
    else if (code == KEY_LEVEL_PUSH) toggle_auto();
    else if (code == KEY_LEFT || code == KEY_RIGHT) {
        if (page == PG_CHAIN) select_slot((sel_slot + (code == KEY_RIGHT ? 1 : N_SLOTS - 1)) % N_SLOTS);
        else show_page((page + (code == KEY_RIGHT ? 1 : N_PAGES - 1)) % N_PAGES);
    }
    else if (code == KEY_PAGE) { func_latch = !func_latch; page_until = (int)ticks + 240; say("LAYER", func_latch ? "2" : "1"); }
    else if (code == KEY_UP || code == KEY_DOWN) choose_weather(clamp(weather + (code == KEY_UP ? 1 : -1), 0, N_WEATHER - 1));
    else if (code == KEY_PATTERN) { Param *p = &P(PG_STARS, 10); p->value = p->user = (int16_t)!p->value; say("STARS", p->value ? "BIRDS" : "RING"); }
    else if (code == KEY_BANK) { if (page == PG_CHAIN) { shelf_open = 1; shelf_build(slot_fx(sel_slot)); shelf_row = shelf_index_of(slots[sel_slot].row); } else { Param *p = &P(PG_STARS, 9); p->value = p->user = (int16_t)((p->value + 1) % 8); char n[6]; num(n, p->value + 1); say("BANK", n); } }
#ifdef AERIAOS
    else if (code == KEY_MIDI) { if (func_held) { gust = 120; say("GUST", 0); } else if (page == PG_MIDI) { midi_mod = (midi_mod + 1) % 4; midi_pitch = midi_mod == 1; static const char *const mn[4] = {"GUMBALLS", "PITCH", "MARBLES", "PONG"}; say(mn[midi_mod], 0); } else show_page(PG_MIDI); }
#else
    else if (code == KEY_MIDI) { if (func_held) { gust = 120; say("GUST", 0); } else if (page == PG_MIDI) { midi_pitch = !midi_pitch; say(midi_pitch ? "PITCH" : "GUMBALLS", 0); } else show_page(PG_MIDI); }
#endif
    else if (code == KEY_YES && page == PG_MIDI && !func_held) { if (midi_pitch) pitch_key(code); else gum_key(code); }
    else if (code == KEY_YES && func_held && page == PG_SYSTEM) os_upgrade();
    else if (code == KEY_YES && func_held) intro = 0;
    else if (code == KEY_YES && page == PG_SYSTEM) saver_go();
    else if (code == KEY_YES) { if (page == PG_CHAIN && slots[sel_slot].row <= 0) { shelf_open = 1; shelf_row = 0; } else { Param *p = &P(PG_SKY, 10); p->value = p->user = (int16_t)!p->value; say(p->value ? "NIGHT" : "DAY", 0); } }
    else if (code == KEY_NO) { if (page == PG_STARS) { new_constellation(); say("NEW STARS", 0); } else { new_sky(); say("NEW SKY", 0); } }
    return 1;
}

int module_enc(int encoder, int delta)
{
    last_input = ticks;
    if (saver_on && !hidden) { if (!ss_enc(encoder, delta)) { saver_on = 0; ss_stop(); } return 1; }
    ui_until = ticks + UI_SHOW;
    if (hidden || usb_mode) return hidden ? 0 : 1;
    if (popup_open()) return 0;
    if (intro < INTRO_LEN) return 1;
    if (br_mode) { if (encoder == 6 || encoder == 0) browser_enc(delta); return 1; }
#ifdef SEQ_UI
    if (seq_enc(encoder, delta)) return 1;
#endif
    if (shelf_open) { shelf_build(slot_fx(sel_slot)); if (encoder == 6 || encoder == 0) shelf_row = clamp(shelf_row + (delta > 0 ? 1 : -1), 0, shelf_n - 1); return 1; }
    if (encoder == 6) { choose_weather(clamp(weather + (delta > 0 ? 1 : -1), 0, N_WEATHER - 1)); return 1; }
    if (encoder < 0 || encoder > 5) return 1;
#ifdef AERIAOS
    if (preset_enc(delta)) return 1;                            /* the project page: a name's letters; else it holds the knobs */
    if (out_open) { out_enc(encoder, delta); return 1; }
    if (cloud_open) { cloud_enc(encoder, delta); return 1; }
    if (page == PG_MIDI && midi_mod == 2) { marbles_enc(encoder, delta); return 1; }
    if (page == PG_MIDI && midi_mod == 3) { pong_enc(encoder, delta); return 1; }
#endif
    char n[24];
    if (page == PG_CHAIN) {
        Slot *sl = &slots[sel_slot];
#if defined(AERIAOS)
        if (layer() && sl->row > 0 && sl->row <= FX_COUNT && encoder < 6) {
            const FxInfo *fi = &fx_info[sl->row - 1];
            if (!fi->knob2[encoder][0]) return 1;
            int c2 = fi->count2[encoder] >= 2 ? fi->count2[encoder] : 128;
            int v2 = clamp(preview_slot_p2[sel_slot][encoder] + (c2 <= 16 ? (delta > 0 ? 1 : -1) : delta), 0, c2 - 1);
            preview_slot_p2[sel_slot][encoder] = (uint8_t)v2;
            slot_text(sel_slot, 6 + encoder, v2, n); say(fi->knob2[encoder], n);
            return 1;
        }
#endif
        if (sl->row <= 0 || !sl->knob[encoder][0]) return 1;
        if (scene_held) {                                        /* the knob's place in the scene, not its own */
            int sc = scene_held == 1 ? scene_a : scene_b, id = N_PAGES * 12 + sel_slot * 6 + encoder;
            int cur = scene_val[sc][id] >= 0 ? scene_val[sc][id] : slot_base(sel_slot, encoder);
            int v = clamp(cur + delta, 0, sl->count[encoder] - 1);
            scene_val[sc][id] = (int8_t)v; scene_dirty = 1;
            n[0] = scene_held == 1 ? 'A' : 'B'; n[1] = ' '; slot_text(sel_slot, encoder, v, n + 2); say(sl->knob[encoder], n);
            return 1;
        }
        int v = clamp(slot_base(sel_slot, encoder) + delta, 0, sl->count[encoder] - 1);
        sl->user[encoder] = sl->value[encoder] = (int16_t)v;
        slot_text(sel_slot, encoder, v, n);
#if defined(AERIAOS)
        if (scale_now() && sl->row <= FX_COUNT) {          /* in a scale, a pitch knob reads as where it lands */
            uint32_t org = fx_info[sl->row - 1].org;
            if (org == 0x34000u && encoder == 0) pitch_text(n, (v - 24) * 1000);
            else if (org == 0x35000u && encoder == 4) signed_text(n, dc_round(aos_scale_snap((v - 24) * 1000), 1000));
            else if (org == 0x36000u && encoder == 0) reso_text(n, 30 + v);
        }
#endif
        say(sl->knob[encoder], n);
        return 1;
    }
    if (page == PG_MIDI) { if (midi_pitch) pitch_enc(encoder, delta, layer()); else gum_enc(encoder, delta, layer()); return 1; }
    if (page == PG_SYSTEM) {
        if (encoder > 5) return 1;
        Param *p = &P(PG_SYSTEM, encoder);
        p->value = p->user = (int16_t)clamp(pv(PG_SYSTEM, encoder) + (delta > 0 ? 1 : -1), p->lo, p->hi);
        if (encoder == 4) say("SAVER", saver_name(p->value));
        if (encoder == 0) for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff;
        return 1;
    }
    if (!param_page(page)) return 1;
    int i = (layer() ? 6 : 0) + encoder;
    Param *p = &params[page][i];
    if (!p->name[0]) return 1;
    if (scene_held && p->hi <= 127) {                           /* the knob's place in the scene, not its own */
        int sc = scene_held == 1 ? scene_a : scene_b, id = page * 12 + i;
        int cur = scene_val[sc][id] >= 0 ? scene_val[sc][id] : pv_base(page, i);
        int sv = clamp(cur + delta, p->lo, p->hi);
        scene_val[sc][id] = (int8_t)sv; scene_dirty = 1;
        n[0] = scene_held == 1 ? 'A' : 'B'; n[1] = ' '; num(n + 2, sv); say(p->name, n);
        return 1;
    }
    int v = clamp(pv_base(page, i) + delta, p->lo, p->hi);
    p->user = p->value = (int16_t)v;
    if (page == PG_SKY) { if (i == 3) set_weather(v); if (i == 4) auto_weather = v; }
    page_until = (int)ticks + 240;
    static const char *const in_names[5] = {"-", "A B", "A", "B", "A+B"};
    static const char *const cd_names[5] = {"-", "C D", "C", "D", "C+D"};
    if (page == PG_SKY && (i == 3 || i == 6)) say(p->name, weather_name[v]);
    else if (page == PG_MIX && i == 6) say("IN AB", in_names[v]);
    else if (page == PG_MIX && i == 7) say("IN CD", cd_names[v]);
    else if (page == PG_GAIN && i >= 6 && i <= 9) { static const char *const bn[4] = {"EQ LOW", "EQ LO MID", "EQ HI MID", "EQ HIGH"}; signed_text(n, v - 13); int k = 0; while (n[k]) k++; n[k++] = 'D'; n[k++] = 'B'; n[k] = 0; say(bn[i - 6], n); }
    else if (page == PG_GAIN && i >= 10) {       /* a centre: 20 Hz x 1000^(v/127), shown to two figures */
        static const uint16_t hz[128] = {20,21,22,24,25,26,28,29,31,33,34,36,38,41,43,45,48,50,53,56,59,63,66,70,74,78,82,87,92,97,102,108,114,120,127,134,142,150,158,167,176,186,196,207,219,231,244,258,272,287,303,320,338,357,377,398,421,444,469,495,523,552,583,615,650,686,725,765,808,853,901,951,1004,1060,1120,1182,1248,1318,1392,1470,1552,1638,1730,1827,1929,2037,2150,2271,2398,2532,2673,2823,2980,3147,3323,3509,3705,3912,4130,4361,4605,4862,5134,5421,5724,6044,6382,6739,7116,7513,7933,8377,8845,9339,9862,10413,10995,11609,12258,12944,13667,14431,15238,16089,16989,17938,18941,20000};   /* 20 Hz x 1000^(v/127) */
        int f = hz[v & 127], k = 0;
        if (f >= 1000) { num(n, f / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + f % 1000 / 100); n[k++] = 'K'; } else { num(n, f); while (n[k]) k++; }
        n[k++] = 'H'; n[k++] = 'Z'; n[k] = 0; say(i == 10 ? "LO MID AT" : "HI MID AT", n);
    }
    else if (page == PG_GAIN && i != 5) { int u = i == 3 ? 108 : i == 4 ? 127 : 64; n[0] = v >= u ? '+' : '-'; num(n + 1, iabs(v - u)); say(p->name, n); }
    else if (page == PG_BALLOON && (i == 6 || i >= 9)) { int u = i == 6 ? 3 : 25; n[0] = v > u ? '+' : v < u ? '-' : ' '; num(n + 1, iabs(v - u)); say(i == 6 ? "OCTAVE" : i == 9 ? "TUNE 1" : i == 10 ? "TUNE 2" : "TUNE 3", n); }
    else if (page == PG_BALLOON && i == 0) { int ms = (441 + v * v * 11) * 10 / 441, k = 0; if (ms >= 1000) { num(n, ms / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + ms % 1000 / 100); n[k++] = 'S'; } else { num(n, ms); while (n[k]) k++; n[k++] = 'M'; n[k++] = 'S'; } n[k] = 0; say("GRAIN SIZE", n); }
    else if (page == PG_BALLOON && (i == 7 || i == 8)) { num(n, v - 1); say(i == 7 ? "RANDOM OCT" : "FADE", n); }
    else if ((page == PG_LOOP && i == 2) || (page == PG_STARS && i == 6) || (page == PG_BALLOON && i == 2)) { pitch_text(n, (v - 64) * 1000); say("PITCH", n); }
    else if (page == PG_MASTER) master_say(i, v, n);
    else if (page == PG_MIX && i == 10) say("LIMITER", v == 1 ? "OFF" : "ON");
    else if (page == PG_MIX && i == 8) say("SCALE", v ? scale_list[v - 1].name : "OFF");
    else if (page == PG_MIX && i == 9) { static const char *const rn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; say("ROOT", rn[v % 12]); }
    else if (p->hi == 1) say(p->name, v ? "ON" : "OFF");
    else if (page == PG_SKY && i == 0) { num(n, v - 9); say("WIND", n); }
    else { num(n, v); say(p->name, n); }
    return 1;
}

#ifdef AERIAOS
/* AERIA OS's main loop: precise time between ticks (MIDI modules), and every MIDI message that comes in */
void module_fast(uint32_t us) { marbles_fast(us); pong_fast(us); master_fast(us); }
void module_midi(unsigned st, unsigned a, unsigned b) { out_midi(st, a, b); marbles_midi(st, a, b); }
#endif
