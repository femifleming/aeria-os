/* SPDX-License-Identifier: MIT
 * AERIA by sadnoise -- a sky that is an instrument, for the Octatrack MKII.
 *
 * The sky owns the screen, every key, every encoder and the trig LEDs. Under it
 * the stock engine runs eight voices (rig.json):
 *   T1 GRAINS  FRACT granular          T2 STARS   one-shots, the constellation
 *   T3 PLAYER  a sample, loop points   T4 BALLOON the caught balloons
 *   T5-T8      inputs A B C D (THRU), linkable as stereo pairs
 * AERIA writes their live parameters (the live lane and the DSP halfwords, as
 * SYNTIZ does) and places STARS/PLAYER trigs one step ahead in the working
 * pattern (bank-file layout, ot_bank.py), only after recognising that layout in
 * RAM. Saved projects are never written.
 *
 * Keys (FUNC adds a second layer everywhere):
 *   SRC SKY  AMP GRAIN  LFO STARS  FX1 RAIN  FX2 SPACE  PART BALLOON  AED PLAYER
 *   ARR ECHO  PROJ MIX   LEFT/RIGHT page   PAGE latch the FUNC layer
 *   A-F the page's bubbles (FUNC+A-F the second row)   push A-F: back to the sky
 *   LEVEL weather   push LEVEL auto weather   UP/DOWN weather (PLAYER: sample)
 *   trigs: SKY-type pages 1-7 weather, 8 auto, 9-15 grain place, 16 lightning
 *          GRAIN 16 places   STARS 16 planets on/off   PLAYER 16 pads
 *          BALLOON 1-3 which balloon sings, 9-16 places
 *   T1-T4 mute input A-D (T1+T2, T3+T4 together: stereo link)  T5-T8 mute a
 *   voice (FUNC+T5-T8: that voice skips the weather, straight out)
 *   AB CD SRC3 catch a balloon (again: let go; FUNC+: pop)
 *   SCENE A/B held + trig 1-7: the crossfader's two weathers
 *   PATTERN stars ring/birds   BANK next sample bank   MIDI a gust of wind
 *   YES day/night  NO new sky  FUNC+YES the intro  FUNC+NO the stock screens
 *   PLAY STOP REC CUE TEMPO pass through.
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
int preview_leds[64];
uint32_t preview_xfade = 0, preview_step = 0, preview_play = 1;
int preview_track;
uint8_t preview_pattern[8][2330 + 100];
static void set_led(int id, int n) { if (id >= 0 && id < 64) preview_leds[id] = n; }
static int write_mode = 1;
static uint8_t sent[8][32];
static void lane_put(int t, int slot, int v) { if (!write_mode) return; preview_lane[t][slot] = (uint8_t)v; preview_dsp[t][slot] = (uint16_t)(v << 8); }
static int xfade(void) { return (int)preview_xfade; }
static void set_current_track(int t) { preview_track = t; }
static unsigned master_step(void) { return preview_step; }
static int playing(void) { return preview_play == 1; }
static volatile uint8_t *pattern_track(int t) { return &preview_pattern[t][50]; }
static int rig_loaded(void) { return 1; }
static uint32_t bank_base(void) { return 1; }
#else
typedef void (*led_fn)(int id, int ticks);
static void set_led(int id, int n) { ((led_fn)0x40013784u)(id, n); }
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
 * (type 0) and T5-T8 are THRU (type 2). Nothing is placed in any other project. */
static int rig_loaded(void)
{
    uint32_t base = *(volatile uint32_t *)0x46c82456u;
    if (base < 0x40000000u || base > 0x47f00000u) return 0;
    volatile uint8_t *part = (volatile uint8_t *)(base + 0x8ed80u + (unsigned)(*(volatile uint8_t *)0x100b14cfu & 3u) * 6322u);
    if (part[0x22 + 1] != 0) return 0;
    for (int t = 4; t < 8; t++) if (part[0x22 + t] != 2) return 0;
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
    0x5bc9, 0x798e, 0x39ef, 0x7292, 0x7bef, 0x7bce, 0x05d0, 0x01c0, 0x0410};
static int glyph_of(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return c == '+' ? 36 : c == '-' ? 37 : c == ':' ? 38 : -1;
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
    if (v >= 100) out[i++] = (char)('0' + v / 100);
    if (v >= 10) out[i++] = (char)('0' + v / 10 % 10);
    out[i++] = (char)('0' + v % 10);
    out[i] = 0;
}

/* ---- weather ---------------------------------------------------------------------- */
enum { CLEAR, CLOUDY, RAIN, STORM, SNOW, HEAT, FOG, N_WEATHER };
static const char *const weather_name[N_WEATHER] = {"CLEAR", "CLOUDY", "RAIN", "STORM", "SNOW", "HEAT", "FOG"};
static int weather = CLOUDY, auto_weather, xf_right = STORM, amount;
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

