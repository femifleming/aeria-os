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

