/* SPDX-License-Identifier: MIT
 * AERIA by sadnoise -- a sky that is an effects processor, for the Octatrack MKII.
 *
 * The sky owns the screen, every key, every encoder and the trig LEDs. Under it
 * the stock engine runs the AERIA project: T5 THRU takes inputs A B (and C D),
 * T6-T8 are NEIGHBOR machines, so their eight stock effects form one chain:
 *   FILTER > DELAY > CHORUS > DARK REV > COMB > LOOP (a feedback delay) > LO-FI > PLATE
 * T1-T4 hold the algorithms (GRAINS, STARS, PLAYER, BALLOON); only the one
 * chosen in the ALGO menu sounds. Every change goes through stock's page-1
 * writer, as a knob turn or a MIDI CC would.
 *
 *   SRC CHAIN   trig/track 1-8 pick an effect (FUNC: bypass), A-F its knobs,
 *               trig 9-15 the weather, 16 auto, LEFT/RIGHT the next effect
 *   AMP SKY  LFO ALGO  FX1 MIX  FX2/PROJ SYSTEM  PART BALLOON  AED PLAYER  ARR STARS
 *   LEVEL weather  push LEVEL auto  push A-F give a knob back to the sky
 *   SCENE A/B + trig: the crossfader's two weathers   MIDI a gust   PAGE FUNC layer
 *   AB CD SRC3 catch a balloon (again: let go; FUNC+: pop)
 *   YES day/night  NO new sky  FUNC+YES intro  FUNC+NO the stock screens
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
static int current_track(void) { return preview_track; }
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
 * (type 0), T5 is THRU (2), T6-T8 NEIGHBOR (3). Nothing is placed in any other project. */
static int rig_loaded(void)
{
    uint32_t base = *(volatile uint32_t *)0x46c82456u;
    if (base < 0x40000000u || base > 0x47f00000u) return 0;
    volatile uint8_t *part = (volatile uint8_t *)(base + 0x8ed80u + (unsigned)(*(volatile uint8_t *)0x100b14cfu & 3u) * 6322u);
    if (part[0x22 + 1] != 0 || part[0x22 + 4] != 2) return 0;
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

/* ---- the effect chain: T5 THRU takes the inputs, T6-T8 NEIGHBOR each take the
 * track before; two stock effects per track, eight in series --------------------- */
#define CHAIN_T 4
#define N_FX 8
typedef struct {
    char name[10];
    char knob[6][5];      /* "" = an empty slot the writer refuses */
    uint8_t hi[6];
    uint8_t def[6];
    uint8_t neutral[6];   /* what bypass writes */
} Fx;
static const Fx fx[N_FX] = {
    {"FILTER", {"BASE", "WDTH", "Q", "DPTH", "ATK", "DEC"}, {127, 127, 127, 127, 127, 127}, {0, 127, 0, 64, 0, 64}, {0, 127, 0, 64, 0, 64}},
    {"DELAY", {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {127, 127, 127, 127, 127, 127}, {47, 40, 90, 0, 127, 50}, {47, 0, 0, 0, 127, 0}},
    {"CHORUS", {"DEL", "DEP", "SPD", "FB", "WID", "MIX"}, {127, 127, 127, 127, 127, 127}, {64, 64, 13, 0, 127, 30}, {64, 64, 13, 0, 127, 0}},
    {"DARK REV", {"TIME", "SHVG", "SHVF", "HP", "LP", "MIX"}, {127, 127, 127, 127, 127, 127}, {60, 0, 127, 0, 127, 35}, {60, 0, 127, 0, 127, 0}},
    {"COMB", {"PTCH", "TUNE", "LP", "FB", "", "MIX"}, {107, 127, 127, 127, 127, 127}, {26, 64, 127, 96, 127, 0}, {26, 64, 127, 96, 127, 0}},
    {"LOOP", {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {127, 127, 127, 127, 127, 127}, {90, 100, 70, 0, 100, 20}, {90, 0, 0, 0, 127, 0}},
    {"LO-FI", {"DIST", "NOIS", "AMF", "SRR", "BRR", "AMD"}, {127, 127, 127, 127, 127, 127}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
    {"PLATE", {"TIME", "DAMP", "GATE", "HP", "LP", "MIX"}, {127, 127, 127, 127, 127, 127}, {60, 30, 127, 0, 127, 30}, {60, 30, 127, 0, 127, 0}},
};
static const char fx_letter[N_FX] = {'F', 'D', 'C', 'R', 'M', 'L', 'X', 'P'};
static int16_t fx_val[N_FX][6], fx_user[N_FX][6];
static uint8_t bypass;
static int sel_fx;
static int fx_track(int i) { return CHAIN_T + i / 2; }
static int fx_slot(int i, int k) { return ((i & 1) ? 24 : 18) + k; }
static int fx_value(int i, int k) { return fx_user[i][k] >= 0 ? fx_user[i][k] : fx_val[i][k]; }

/* What each weather wants: {effect, knob, value}, ending with {255}. */
typedef struct { uint8_t fx, k, v; } Want;
static const Want weather_wants[N_WEATHER][16] = {
    /* CLEAR  */ {{0, 1, 127}, {0, 2, 0}, {1, 1, 30}, {1, 5, 30}, {2, 5, 25}, {3, 0, 50}, {3, 5, 25}, {4, 5, 0}, {5, 5, 0}, {6, 0, 0}, {6, 1, 0}, {7, 5, 20}, {255, 0, 0}},
    /* CLOUDY */ {{0, 1, 110}, {0, 2, 10}, {1, 1, 45}, {1, 5, 45}, {2, 5, 45}, {3, 0, 70}, {3, 4, 100}, {3, 5, 40}, {4, 5, 10}, {5, 5, 20}, {5, 1, 80}, {6, 0, 0}, {6, 1, 0}, {7, 5, 30}, {255, 0, 0}},
    /* RAIN   */ {{0, 1, 80}, {0, 2, 20}, {1, 0, 30}, {1, 1, 60}, {1, 5, 55}, {2, 5, 30}, {3, 0, 90}, {3, 4, 80}, {3, 5, 60}, {4, 3, 100}, {4, 5, 20}, {5, 5, 35}, {6, 1, 20}, {7, 5, 40}, {255, 0, 0}},
    /* STORM  */ {{0, 1, 70}, {0, 2, 50}, {1, 1, 100}, {1, 5, 80}, {2, 5, 20}, {3, 0, 110}, {3, 5, 70}, {4, 3, 110}, {4, 5, 30}, {5, 1, 115}, {5, 5, 70}, {6, 0, 60}, {6, 1, 30}, {7, 5, 50}, {255, 0, 0}},
    /* SNOW   */ {{0, 1, 60}, {0, 2, 0}, {1, 0, 90}, {1, 1, 70}, {1, 5, 50}, {2, 2, 6}, {2, 5, 40}, {3, 0, 127}, {3, 4, 60}, {3, 5, 100}, {4, 5, 0}, {5, 1, 120}, {5, 5, 60}, {6, 3, 30}, {7, 0, 127}, {7, 5, 80}},
    /* HEAT   */ {{0, 0, 34}, {0, 1, 100}, {1, 1, 40}, {1, 5, 30}, {2, 1, 110}, {2, 2, 80}, {2, 5, 100}, {3, 5, 30}, {4, 1, 80}, {4, 5, 15}, {5, 5, 0}, {6, 2, 60}, {6, 5, 50}, {7, 5, 20}, {255, 0, 0}},
    /* FOG    */ {{0, 1, 50}, {0, 2, 0}, {1, 1, 60}, {1, 4, 60}, {1, 5, 50}, {2, 5, 60}, {3, 0, 100}, {3, 4, 50}, {3, 5, 90}, {4, 5, 0}, {5, 1, 100}, {5, 5, 40}, {6, 4, 20}, {7, 4, 60}, {7, 5, 70}, {255, 0, 0}},
};
static int want_of(int w, int i, int k)
{
    for (int n = 0; n < 16 && weather_wants[w][n].fx != 255; n++)
        if (weather_wants[w][n].fx == i && weather_wants[w][n].k == k) return weather_wants[w][n].v;
    return -1;
}

/* ---- the other pages ------------------------------------------------------------- */
typedef struct {
    char name[5];
    uint8_t mask, slot;   /* tracks and lane slot; mask 0 = AERIA's own */
    uint8_t lo, hi;
    int16_t value, user;  /* user -1 follows the sky */
    uint8_t drift;
} Param;

#define GR 0x01u
#define ST 0x02u
#define PL 0x04u
#define BL 0x08u
#define OWN 0, 0
#define EMPTY {"", 0, 0, 0, 0, 0, 0, 0}

enum { PG_CHAIN, PG_SKY, PG_ALGO, PG_GRAIN, PG_STARS, PG_PLAYER, PG_BALLOON, PG_MIX, PG_SYSTEM, N_PAGES };
static const char *const page_name[N_PAGES] = {"CHAIN", "SKY", "ALGO", "GRAINS", "STARS", "PLAYER", "BALLOON", "MIX", "SYSTEM"};
enum { ALGO_NONE, ALGO_GRAINS, ALGO_STARS, ALGO_PLAYER, ALGO_BALLOON, N_ALGO };
static const char *const algo_name[N_ALGO] = {"NONE", "GRAINS", "STARS", "PLAYER", "BALLOON"};
static int algo;

static Param params[N_PAGES][12] = {
    /* CHAIN: drawn from fx[] */
    {EMPTY},
    /* SKY */
    {{"WIND", OWN, 0, 18, 12, 12, 0}, {"CLDS", OWN, 0, 7, 5, 5, 0}, {"WAVE", OWN, 0, 6, 2, 2, 0},
     {"WTHR", OWN, 0, N_WEATHER - 1, CLOUDY, CLOUDY, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, {"XFAD", OWN, 0, 1, 1, 1, 0},
     {"XF B", OWN, 0, N_WEATHER - 1, STORM, STORM, 0}, {"SPEL", OWN, 1, 8, 4, 4, 0}, {"DRFT", OWN, 0, 127, 20, 20, 0},
     {"RATE", OWN, 1, 127, 30, 30, 0}, {"NITE", OWN, 0, 1, 0, 0, 0}, {"LITE", OWN, 0, 1, 1, 1, 0}},
    /* ALGO: a menu */
    {EMPTY},
    /* GRAINS: FRACT on T1 */
    {{"POS", GR, 0, 0, 127, 18, 18, 0}, {"SIZE", GR, 1, 0, 127, 86, 86, 1}, {"REVS", GR, 2, 0, 127, 127, 127, 0},
     {"SPRD", GR, 3, 0, 127, 0, 0, 1}, {"SHPE", GR, 4, 0, 127, 64, 64, 1}, {"PTCH", GR, 5, 0, 127, 64, 64, 0},
     {"ATK", GR, 12, 0, 127, 0, 0, 0}, {"REL", GR, 14, 0, 127, 127, 127, 0}, {"SCAN", OWN, 0, 127, 64, 64, 0},
     {"FRZ", OWN, 0, 1, 0, 0, 0}, {"BAL", GR, 16, 0, 127, 64, 64, 1}, {"HOLD", GR, 13, 0, 127, 127, 127, 0}},
    /* STARS: one-shots on T2 */
    {{"SPD", OWN, 0, 127, 24, 24, 0}, {"NUM", OWN, 1, 8, 4, 4, 0}, {"PRND", OWN, 0, 127, 0, 0, 0},
     {"ARND", OWN, 0, 127, 0, 0, 0}, {"DRND", OWN, 0, 127, 20, 20, 0}, {"CHNC", OWN, 0, 127, 127, 127, 0},
     {"PTCH", OWN, 0, 127, 64, 64, 0}, {"ATK", OWN, 0, 127, 0, 0, 0}, {"DEC", OWN, 0, 127, 96, 96, 0},
     {"BANK", OWN, 0, 7, 0, 0, 0}, {"MODE", OWN, 0, 1, 0, 0, 0}, {"BAL", ST, 16, 0, 127, 64, 64, 1}},
    /* PLAYER: a sample on T3 */
    {{"STRT", PL, 1, 0, 127, 0, 0, 0}, {"LEN", PL, 2, 0, 127, 127, 127, 0}, {"RATE", PL, 3, 0, 127, 127, 127, 0},
     {"PTCH", PL, 0, 0, 127, 64, 64, 0}, {"RTRG", PL, 4, 0, 127, 0, 0, 0}, {"RTIM", PL, 5, 0, 127, 79, 79, 0},
     {"ATK", PL, 12, 0, 127, 0, 0, 0}, {"HOLD", PL, 13, 0, 127, 127, 127, 0}, {"REL", PL, 14, 0, 127, 127, 127, 0},
     {"SLOT", OWN, 0, 127, 0, 0, 0}, {"QNT", OWN, 0, 1, 1, 1, 0}, {"BAL", PL, 16, 0, 127, 64, 64, 1}},
    /* BALLOON: Flex on T4 */
    {{"GRN", BL, 5, 0, 127, 40, 40, 0}, {"SPRY", OWN, 0, 127, 30, 30, 0}, {"PTCH", BL, 0, 0, 127, 64, 64, 0},
     {"RPT", BL, 4, 0, 127, 127, 127, 0}, {"POS", OWN, 0, 127, 20, 20, 0}, {"RATE", BL, 3, 0, 127, 127, 127, 0},
     {"ATK", BL, 12, 0, 127, 0, 0, 0}, {"REL", BL, 14, 0, 127, 127, 127, 0}, {"LEN", BL, 2, 0, 127, 127, 127, 0},
     {"BAL", BL, 16, 0, 127, 64, 64, 1}, {"ROT", OWN, 0, 127, 0, 0, 0}, {"HOLD", BL, 13, 0, 127, 127, 127, 0}},
    /* MIX: what goes in, how loud things come out */
    {{"INAB", OWN, 0, 4, 1, 1, 0}, {"INCD", OWN, 0, 4, 1, 1, 0}, {"GAIN", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"ALGO", OWN, 0, 127, 100, 100, 0}, {"CUE", OWN, 0, 127, 127, 127, 0},
     {"VOL1", OWN, 0, 127, 108, 108, 0}, {"VOL2", OWN, 0, 127, 108, 108, 0}, {"VOL3", OWN, 0, 127, 108, 108, 0},
     {"BAL", OWN, 0, 127, 64, 64, 0}, {"DRY", OWN, 0, 1, 0, 0, 0}, {"TIDE", OWN, 0, 127, 0, 0, 0}},
    /* SYSTEM */
    {{"SND", OWN, 0, 2, 1, 1, 0}, {"INTR", OWN, 0, 1, 1, 1, 0}, {"LITE", OWN, 0, 1, 1, 1, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
};
#define P(page, i) (params[page][i])
static int pv(int page, int i) { Param *p = &params[page][i]; return p->user >= 0 ? p->user : p->value; }

/* ---- state -------------------------------------------------------------------------- */
static int muted[8];
static int night, hidden, func_held, func_latch, ready, page, page_until, intro;
static int scene_held;
static uint32_t held_tracks;
static int16_t drift_now[N_PAGES][6], drift_target[N_PAGES][6];

static void set_weather(int w)
{
    if (w == weather) return;
    weather = w;
    amount = 0;
    spell_left = (uint32_t)range(spell_s[w][0], spell_s[w][1]) * 60u * (uint32_t)pv(PG_SKY, 7) / 4u;
    P(PG_SKY, 3).value = P(PG_SKY, 3).user = (int16_t)w;
    for (int i = 0; i < N_FX; i++) for (int k = 0; k < 6; k++) if (want_of(w, i, k) >= 0) fx_user[i][k] = -1;
    say(weather_name[w], 0);
}
static void step_auto(void)
{
    if (!auto_weather || spell_left-- > 0) return;
    int pick = range(0, 19), w = 0;
    for (; w < N_WEATHER - 1; w++) { pick -= next_w[weather][w]; if (pick < 0) break; }
    set_weather(w == weather ? CLOUDY : w);
}
static void fx_init(void)
{
    for (int i = 0; i < N_FX; i++) for (int k = 0; k < 6; k++) { fx_val[i][k] = fx[i].def[k]; fx_user[i][k] = -1; }
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
    star_placed = step;
    planet_hit = p;
    stars_total++;
}
static void place_player(int step)
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
    if (algo == ALGO_PLAYER) place_player(next);
}

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

/* balloons */
enum { B_EMPTY, B_FILLING, B_FLOATING, B_POPPING };
typedef struct { uint8_t state, track, key; int16_t size, x, y, pop; } Balloon;
static Balloon balloon[3] = {{B_EMPTY, 1, KEY_REC_AB, 0, 24, 0, 0}, {B_EMPTY, 2, KEY_REC_CD, 0, 64, 0, 0}, {B_EMPTY, 3, KEY_REC3, 0, 104, 0, 0}};
static int balloon_voice;
static uint8_t balloon_strt;

static int algo_track(int a) { return a == ALGO_GRAINS ? T_GRAIN : a == ALGO_STARS ? T_STARS : a == ALGO_PLAYER ? T_PLAYER : a == ALGO_BALLOON ? T_BALLOON : -1; }

static void sound_tick(void)
{
    /* the chain: each knob glides to its weather (and toward the crossfader's) */
    int xf = pv(PG_SKY, 5) ? xfade() : -1;
    for (int i = 0; i < N_FX; i++)
        for (int k = 0; k < 6; k++) {
            if (fx_user[i][k] >= 0) continue;
            int want = want_of(weather, i, k);
            if (xf > 0) { int b = want_of(xf_right, i, k); if (b >= 0) want = (want >= 0 ? want : fx_val[i][k]) + ((b - (want >= 0 ? want : fx_val[i][k])) * xf / 127); }
            if (i == 0 && k == 1 && thunder && want >= 0) want = want * (256 - thunder) / 256;
            if (want < 0) continue;
            int v = fx_val[i][k];
            fx_val[i][k] = (int16_t)(v < want ? v + 1 : v > want ? v - 1 : want);
        }
    for (int i = 0; i < N_FX; i++)
        for (int k = 0; k < 6; k++) {
            if (!fx[i].knob[k][0]) continue;
            int v = ((bypass >> i) & 1) ? fx[i].neutral[k] : fx_value(i, k);
            lane_put(fx_track(i), fx_slot(i, k), clamp(v, 0, fx[i].hi[k]));
        }
    /* the inputs: T5 THRU, MAIN slots 0 INAB, 1 VOL, 3 INCD, 4 VOL (64 = unity) */
    lane_put(CHAIN_T, 0, pv(PG_MIX, 0));
    lane_put(CHAIN_T, 3, pv(PG_MIX, 1));
    lane_put(CHAIN_T, 1, pv(PG_MIX, 2));
    lane_put(CHAIN_T, 4, pv(PG_MIX, 2));
    /* levels along the chain and out of it */
    lane_put(CHAIN_T + 0, 15, pv(PG_MIX, 6));
    lane_put(CHAIN_T + 1, 15, pv(PG_MIX, 7));
    lane_put(CHAIN_T + 2, 15, pv(PG_MIX, 8));
    lane_put(CHAIN_T + 3, 15, muted[CHAIN_T + 3] ? 0 : pv(PG_MIX, 3));
    lane_put(CHAIN_T + 3, 16, pv(PG_MIX, 9));
    lane_put(CHAIN_T + 3, 17, pv(PG_MIX, 5));
    /* the algorithms: only the chosen one sounds */
    if (!pv(PG_GRAIN, 9)) {
        static uint32_t frac;
        int wind = pv(PG_SKY, 0) - 9 + (gust ? (pv(PG_SKY, 0) >= 9 ? 9 : -9) : 0);
        frac += (uint32_t)(iabs(wind) * pv(PG_GRAIN, 8) / 16);
        if (frac >= 256) {
            frac -= 256;
            Param *pos = &P(PG_GRAIN, 0);
            int b = pv(PG_GRAIN, 0) + (wind > 0 ? 1 : -1);
            b = b > 127 ? 0 : b < 0 ? 127 : b;
            pos->value = pos->user = (int16_t)b;
        }
    }
    for (int a = ALGO_GRAINS; a < N_ALGO; a++) {
        int t = algo_track(a), pg = a == ALGO_GRAINS ? PG_GRAIN : a == ALGO_STARS ? PG_STARS : a == ALGO_PLAYER ? PG_PLAYER : PG_BALLOON;
        int on = algo == a && !muted[t];
        if (a == ALGO_BALLOON) { int any = 0; for (int b = 0; b < 3; b++) any |= balloon[b].state == B_FLOATING; on = on && any; }
        lane_put(t, 15, on ? pv(PG_MIX, 4) : 0);
        if (algo != a) continue;
        for (int i = 0; i < 12; i++) {
            Param *p = &params[pg][i];
            if (p->mask) lane_put(t, p->slot, out_value(pg, i));
        }
    }
    if (algo == ALGO_BALLOON) {
        int every = 2 + (127 - pv(PG_BALLOON, 0)) / 16;
        if (ticks % (uint32_t)every == 0) balloon_strt = (uint8_t)clamp(pv(PG_BALLOON, 4) + (pv(PG_BALLOON, 1) ? range(-pv(PG_BALLOON, 1) / 2, pv(PG_BALLOON, 1) / 2) : 0), 0, 120);
        lane_put(T_BALLOON, 1, balloon_strt);
    }
}

/* ---- the sky ------------------------------------------------------------------------------- */
#define MAX_CLOUDS 8
#define CLOUD_MAX_W 44
typedef struct { int32_t x; int16_t y; uint8_t w, depth; int8_t top[CLOUD_MAX_W]; } Cloud;
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
    for (int i = 0; i < w; i++) c->top[i] = 0;
    for (int p = 0, n = 3 + w / 10; p < n; p++) {
        int cx = range(2, w - 3), mid = w / 2 - iabs(cx - w / 2), r = 3 + range(0, 3) + mid * 3 / 4;
        if (r > 4 + w / 3) r = 4 + w / 3;
        for (int dx = -r; dx <= r; dx++) {
            int col = cx + dx;
            if (col < 0 || col >= w) continue;
            int h = -r / 4 + isqrt(r * r - dx * dx);
            if (h > 16) h = 16;
            if (h > c->top[col]) c->top[col] = (int8_t)h;
        }
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
    return clamp(n[weather] + pv(PG_SKY, 1) - 5, 0, MAX_CLOUDS);
}
static void draw_cloud(const Cloud *c)
{
    int x0 = c->x >> 8, base = c->y, heavy = weather == RAIN || weather == STORM || weather == FOG;
    int level = heavy ? 8 : c->depth == 3 ? 8 : c->depth == 2 ? 4 : 2;
    if (weather == STORM && c->depth == 3) level = 11;
    for (int i = 0; i < c->w; i++) {
        int x = x0 + i;
        if ((unsigned)x >= W) continue;
        int top = base - c->top[i];
        span(x, top, base, 0);
        for (int y = top + 1; y < base; y++) if (dither_on(x, y, level)) px(x, y);
        px(x, top); px(x, base);
        if (i == 0 || i == c->w - 1) span(x, top, base, 1);
        else {
            int l = c->top[i - 1], r = c->top[i + 1], h = c->top[i];
            if (l < h) span(x, base - h, base - l, 1);
            if (r < h) span(x, base - h, base - r, 1);
        }
    }
}
static void draw_sun_moon(void)
{
    if (weather == STORM || weather == FOG) return;
    if (night) {
        uint32_t s = 0x2545f491u;
        for (int i = 0; i < 40; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; if (((s >> 20) + (ticks >> 4)) % 7) px((int)(s % W), (int)((s >> 8) % (WATER_Y - 6))); }
        for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++)
            if (dx * dx + dy * dy <= 36 && (dx - 3) * (dx - 3) + (dy + 1) * (dy + 1) > 25) px(108 + dx, 12 + dy);
        return;
    }
    int big = weather == HEAT ? 8 : 5, cx = 110, cy = 13;
    for (int dy = -big; dy <= big; dy++) for (int dx = -big; dx <= big; dx++) {
        int d = dx * dx + dy * dy;
        if (d <= big * big && (d >= (big - 1) * (big - 1) || (weather == HEAT && dither_on(dx, dy, 6)))) px(cx + dx, cy + dy);
    }
    for (int k = 0; k < 8; k++) { int a = (k * 128 + (int)(ticks * 2)) & 1023; for (int r = big + 3; r <= big + 5; r++) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
}
static void draw_precip(void)
{
    int want = (weather == RAIN ? 40 : weather == STORM ? 72 : weather == SNOW ? 48 : 0) * amount / 256, wind = wind_now();
    for (int i = 0; i < want; i++) {
        Drop *d = &drops[i];
        int x = d->x >> 4, y = d->y >> 4;
        if (weather == SNOW) { int wob = sine[(d->phase + (int)ticks) & 63] / 64; px(x + wob, y); if (d->v > 26) { px(x + wob + 1, y); px(x + wob, y + 1); } }
        else { line(x, y, x - wind / 3, y + (weather == STORM ? 5 : 3)); if (y >= WATER_Y - 2) { px(x - 2, WATER_Y - 1); px(x + 2, WATER_Y - 1); } }
    }
}
static void move_precip(void)
{
    int wind = wind_now();
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &drops[i];
        d->y = (int16_t)(d->y + (weather == SNOW ? d->v / 4 : weather == STORM ? d->v * 2 : d->v));
        d->x = (int16_t)(d->x + wind * (weather == SNOW ? 3 : 6));
        if ((d->y >> 4) > WATER_Y) { d->y = (int16_t)(range(-20, -2) * 16); d->x = (int16_t)(range(-10, W + 10) * 16); }
        if (d->x < -160) d->x = (int16_t)((W + 8) * 16);
        if (d->x > (W + 10) * 16) d->x = -8 * 16;
    }
}
static void draw_fog(void)
{
    if (weather != FOG) return;
    int lvl = 5 * amount / 256;
    for (int band = 0; band < 3; band++) {
        int y0 = 14 + band * 13, off = (int)(ticks >> (2 + band)) + band * 17;
        for (int x = 0; x < W; x++) { int wob = sine[(x * 2 + off) & 63] / 40; for (int y = y0 + wob; y < y0 + wob + 7; y++) { if (dither_on(x + off, y, lvl)) px(x, y); else if (dither_on(x + off + 2, y, lvl)) px_off(x, y); } }
    }
}
static void draw_heat(void)
{
    if (weather != HEAT) return;
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
    int t = (int)ticks, frozen = weather == SNOW && amount > 200, waves = pv(PG_SKY, 2) + (weather == STORM ? 3 : 0);
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
    if (frozen) return;
    int fx = fin_x >> 8, rise = 5 + sine[(t >> 1) & 63] / 40;
    for (int i = 0; i <= 9; i++) { int h = i < 7 ? i * rise / 7 : rise - (i - 7) * rise / 2; if (h < 0) h = 0; span(fx + i, WATER_Y - tide - h, WATER_Y - tide, 1); }
    for (int i = 1; i < 14; i++) if ((i + t / 3) % 3 == 0) px(fx - i, WATER_Y - tide + 1 + (i & 1));
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

static int layer(void) { return func_held || func_latch; }
static void label(int x, const char *s) { for (int i = x - 2; i < x + text_w(s) + 2; i++) span(i, 0, 8, 0); text(x, 2, s, 1); }
static void panel_clear(int y0, int y1) { for (int x = 0; x < W; x++) span(x, y0, y1, 0); }

/* the chain: eight balloons on one string, the signal flowing left to right */
static void draw_chain(void)
{
    panel_clear(10, 47);
    int y = 16;
    for (int x = 2; x < W - 2; x++) if (((x + (int)(ticks >> 2)) & 3) == 0) px(x, y); /* the signal on the string */
    for (int i = 0; i < N_FX; i++) {
        int cx = 9 + i * 15, bob = sine[(int)(ticks + (uint32_t)i * 9) & 63] / 64, r = i == sel_fx ? 5 : 4;
        int cy = y + bob;
        disk_clear(cx, cy, r + 1);
        if ((bypass >> i) & 1) { for (int a = 0; a < 1024; a += 64) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
        else {
            circle(cx, cy, r);
            /* how hard it works: the wet knob fills it */
            int k = fx[i].knob[5][0] ? 5 : 0, fill = fx_value(i, k) * (2 * r) / 128;
            for (int dy = r - fill; dy <= r; dy++) for (int dx = -r + 1; dx < r; dx++) if (dx * dx + dy * dy < r * r && dither_on(cx + dx, cy + dy, 8)) px(cx + dx, cy + dy);
        }
        char l[2] = {fx_letter[i], 0};
        if (i == sel_fx) { for (int dx = -2; dx <= 2; dx++) px(cx + dx, cy + r + 3); }
        for (int dy = -3; dy <= 3; dy++) for (int dx = -2; dx <= 2; dx++) px_off(cx + dx, cy + dy);
        text(cx - 1, cy - 2, l, 1);
    }
    /* the selected effect's six knobs */
    const Fx *f = &fx[sel_fx];
    for (int k = 0; k < 6; k++) {
        if (!f->knob[k][0]) continue;
        bubble(11 + k * 21, 33, 7, fx_value(sel_fx, k) * 127 / (f->hi[k] ? f->hi[k] : 127), f->knob[k], fx_user[sel_fx][k] < 0 && want_of(weather, sel_fx, k) >= 0);
    }
}

static void draw_algo(void)
{
    panel_clear(10, 47);
    for (int a = 0; a < N_ALGO; a++) {
        int y = 12 + a * 7;
        if (a == algo) { for (int x = 30; x < 98; x++) span(x, y - 1, y + 5, 1); text(34, y, algo_name[a], 0); }
        else text(34, y, algo_name[a], 1);
        char n[2] = {(char)('1' + a), 0};
        text(24, y, n, 1);
    }
}

static void draw_system(void)
{
    panel_clear(10, 56);
    text(4, 12, "USB DISK OR OS UPGRADE:", 1);
    text(4, 19, "FUNC+NO  THEN  PROJECT", 1);
    text(4, 26, "SYSTEM  ON THE OCTATRACK", 1);
    text(4, 35, "A SOUND", 1); text(52, 35, pv(PG_SYSTEM, 0) == 0 ? "OFF" : pv(PG_SYSTEM, 0) == 1 ? "KNOBS" : "LANE", 1);
    text(4, 42, "B INTRO", 1); text(52, 42, pv(PG_SYSTEM, 1) ? "ON" : "OFF", 1);
    text(4, 49, "C LIGHTS", 1); text(52, 49, pv(PG_SYSTEM, 2) ? "ON" : "OFF", 1);
}

static int page_param_pages(int pg) { return pg == PG_SKY || pg == PG_GRAIN || pg == PG_STARS || pg == PG_PLAYER || pg == PG_BALLOON || pg == PG_MIX; }
static void draw_page(void)
{
    if (page == PG_CHAIN) { draw_chain(); return; }
    if (page == PG_ALGO) { draw_algo(); return; }
    if (page == PG_SYSTEM) { draw_system(); return; }
    if (!page_param_pages(page)) return;
    if (ticks > (uint32_t)page_until && !func_held && page != PG_MIX) return;
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
}

static void draw_hud(void)
{
    label(2, page == PG_CHAIN ? fx[sel_fx].name : page_name[page]);
    const char *w = weather_name[weather];
    label(W - 2 - text_w(w) - (auto_weather ? 8 : 0), w);
    if (auto_weather) label(W - 5, "A");
    if (page == PG_CHAIN || page == PG_MIX) {
        static const char *const ab[5] = {"-", "AB", "A", "B", "A+B"}, *const cd[5] = {"-", "CD", "C", "D", "C+D"};
        for (int i = 0; i < 40; i++) span(i, 57, 63, 0);
        text(1, 58, ab[pv(PG_MIX, 0)], 1); text(18, 58, cd[pv(PG_MIX, 1)], 1);
    }
    if (algo) { const char *a = algo_name[algo]; for (int i = 0; i < text_w(a) + 3; i++) span(i, 57, 63, 0); text(1, 58, a, 1); }
    if (pv(PG_SYSTEM, 0) != 1) { const char *m = pv(PG_SYSTEM, 0) == 0 ? "SND OFF" : "LANE"; for (int i = W - text_w(m) - 3; i < W; i++) span(i, 57, 63, 0); text(W - text_w(m) - 1, 58, m, 1); }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 48, 56, 1);
        text(x, 50, toast, 0);
    }
}

/* ---- LEDs: trig k red 2k, green 2k+1; tempo 38/39 ------------------------------------------------- */
static uint8_t led_want[16];
static void leds_out(void)
{
    for (int k = 0; k < 16; k++) { if (led_want[k] & 1) set_led(2 * k, 3); if (led_want[k] & 2) set_led(2 * k + 1, 3); }
}
static void weather_leds(int first)
{
    led_want[first + weather] = 2;
    if (scene_held) led_want[first + (scene_held == 1 ? weather : xf_right)] = 3;
    if (first + 7 < 16) led_want[first + 7] = auto_weather ? 2 : (uint8_t)((ticks >> 5) & 1);
}
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
    if (page == PG_CHAIN) {
        /* 1-8 the effects: green the one on the knobs, red working, dark bypassed; 9-16 the weather */
        for (int i = 0; i < N_FX; i++) led_want[i] = i == sel_fx ? 2 : ((bypass >> i) & 1) ? 0 : 1;
        if (thunder > 150) for (int i = 0; i < N_FX; i++) led_want[i] = 3;
        weather_leds(8);
    } else if (page == PG_ALGO) {
        for (int a = 0; a < N_ALGO; a++) led_want[a] = a == algo ? 2 : 1;
    } else if (page == PG_GRAIN) {
        led_want[clamp(pv(PG_GRAIN, 0) * 16 / 128, 0, 15)] = 3;
    } else if (page == PG_STARS) {
        for (int p = 0; p < PLANETS; p++) if ((planet_mask >> p) & 1) led_want[p] = 1;
        if (planet_hit >= 0 && star_flash) led_want[planet_hit] = 2;
    } else if (page == PG_PLAYER) {
        for (int k = 0; k < 16; k += 4) led_want[k] = 1;
    } else if (page == PG_BALLOON) {
        for (int b = 0; b < 3; b++) led_want[b] = balloon[b].state == B_FLOATING ? (b == balloon_voice ? 2 : 1) : (balloon[b].state == B_FILLING && (ticks & 8)) ? 1 : 0;
    } else weather_leds(0);
    if (gust) for (int k = 0; k < 16; k++) if (((int)(ticks >> 1) - k) % 16 == 0) led_want[k] = 3;
    if (flash) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 2); set_led(39, 2); }
    leds_out();
}

/* ---- diagnostics, cycling every 2 s: tag in the top nibble ------------------------------------ */
static uint32_t diag_word(void)
{
    if (((ticks / 120u) & 1) == 0)
        return 0x10000000u | (uint32_t)playing() << 27 | (uint32_t)rig_loaded() << 26 | (uint32_t)((pat_off + 100) & 0x3f) << 20
               | (master_step() & 0xffu) << 12 | (stars_total & 0xffu) << 4 | (uint32_t)algo;
    return 0x30000000u | (uint32_t)page << 16 | (uint32_t)sel_fx << 8 | bypass;
}

/* ---- hooks --------------------------------------------------------------------------------------- */
static void strike(void) { bolt_x = range(16, W - 16); bolt_t = 9; flash = 3; thunder = 200; }

void module_tick(struct modwerk_runtime_api *api)
{
    if (!ready) { new_sky(); new_constellation(); fx_init(); spell_left = 3600u; ready = 1; if (!pv(PG_SYSTEM, 1)) intro = INTRO_LEN; }
    ticks++;
    if (intro < INTRO_LEN) intro++;
    if (amount < 256) amount += 2;
    if (flash) flash--;
    if (bolt_t) bolt_t--;
    if (star_flash) star_flash--;
    if (gust) gust--;
    if (thunder) thunder = thunder > 3 ? thunder - 3 : 0;
    night = pv(PG_SKY, 10);
    write_mode = pv(PG_SYSTEM, 0);
    step_auto();
    if (weather == STORM && amount > 128 && (rnd() % 420) == 0) strike();
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
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -40 * 256;
    if ((fin_x >> 8) < -40) fin_x = (W + 20) * 256;
    if (!hidden) leds_tick();
    /* Keep the active track on the chain's head: FRACT draws its own screen
     * whenever its track is active, and the chain is where every edit lands.
     * A balloon key moves it to its recorder's track only for the press. */
    if (!hidden && (ticks & 15) == 0 && current_track() != CHAIN_T) set_current_track(CHAIN_T);
    sequencer_tick();
    sound_tick();
    api->value = diag_word();
}

void module_draw(unsigned char *frame)
{
    if (hidden || !ready) return;
    F = (uint32_t *)frame;
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (intro < INTRO_LEN) { draw_intro(); return; }
    draw_sun_moon();
    int want = clouds_wanted();
    for (int depth = 1; depth <= 3; depth++) for (int i = 0; i < want; i++) if (clouds[i].depth == depth) draw_cloud(&clouds[i]);
    draw_heat();
    draw_fog();
    draw_precip();
    draw_bolt();
    draw_water();
    if (page == PG_STARS) draw_stars();
    if (algo == ALGO_BALLOON || page == PG_BALLOON) draw_balloons();
    draw_page();
    draw_hud();
    if (flash) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
}

static void show_page(int pg) { page = pg; page_until = (int)ticks + 240; say(pg == PG_CHAIN ? fx[sel_fx].name : page_name[pg], 0); }
static int algo_page(void) { return algo == ALGO_GRAINS ? PG_GRAIN : algo == ALGO_STARS ? PG_STARS : algo == ALGO_PLAYER ? PG_PLAYER : algo == ALGO_BALLOON ? PG_BALLOON : PG_ALGO; }
static void choose_weather(int w) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; set_weather(w); }
static void toggle_auto(void) { auto_weather = !auto_weather; P(PG_SKY, 4).value = P(PG_SKY, 4).user = (int16_t)auto_weather; say("AUTO", auto_weather ? "ON" : "OFF"); }
static void select_fx(int i, int toggle_bypass)
{
    if (toggle_bypass) { bypass ^= (uint8_t)(1u << i); say(fx[i].name, (bypass >> i) & 1 ? "OFF" : "ON"); }
    else { sel_fx = i; say(fx[i].name, (bypass >> i) & 1 ? "OFF" : 0); }
    page = PG_CHAIN;
}

int module_key(int code, int pressed)
{
    if (code == KEY_FUNC) { func_held = pressed; if (pressed) page_until = (int)ticks + 240; }
    if (code == KEY_NO && pressed && func_held) { hidden = !hidden; if (!hidden) show_page(page); return 1; }
    if (hidden) return 0;
    if (code == KEY_PLAY || code == KEY_STOP || code == KEY_REC || code == KEY_CUE || code == 0x18) return 0;
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) { if (pressed) held_tracks |= 1u << (code - KEY_TRACK0); else held_tracks &= ~(1u << (code - KEY_TRACK0)); }
    if (code == KEY_SCENEA || code == KEY_SCENEB) { scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0; if (pressed) say("XF", weather_name[xf_right]); return 1; }
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (code != n->key) continue;
        if (func_held) { if (pressed && n->state != B_EMPTY && n->state != B_FILLING) { n->state = B_POPPING; n->pop = 12; say("POP", 0); } return 1; }
        set_current_track(n->track);
        if (pressed) {
            if (n->state == B_FILLING) { n->state = B_FLOATING; balloon_voice = b; say("BALLOON", b == 0 ? "1 UP" : b == 1 ? "2 UP" : "3 UP"); }
            else { n->state = B_FILLING; n->size = 0; n->y = WATER_Y - 12; say("CATCHING", b == 0 ? "A B" : b == 1 ? "C D" : "SKY"); }
        }
        return 0;
    }
    if (!pressed) return 1;
    if (intro < INTRO_LEN) { intro = INTRO_LEN; return 1; }

    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (scene_held && k % 8 < N_WEATHER) {
            int w = k % 8;
            if (scene_held == 2) { xf_right = w; P(PG_SKY, 6).value = P(PG_SKY, 6).user = (int16_t)w; say("XF B", weather_name[w]); }
            else choose_weather(w);
        } else if (page == PG_CHAIN) {
            if (k < 8) select_fx(k, func_held);
            else if (k < 15) choose_weather(k - 8);
            else toggle_auto();
        } else if (page == PG_ALGO) {
            if (k < N_ALGO) { algo = k; say("ALGO", algo_name[k]); if (k) show_page(algo_page()); }
        } else if (page == PG_GRAIN) { Param *p = &P(PG_GRAIN, 0); p->value = p->user = (int16_t)(k * 127 / 15); say("PLACE", 0); }
        else if (page == PG_STARS) { planet_mask ^= (uint16_t)(1u << k); say("PLANET", (planet_mask >> k) & 1 ? "ON" : "OFF"); }
        else if (page == PG_PLAYER) { pad_pending = k; }
        else if (page == PG_BALLOON) { if (k < 3 && balloon[k].state == B_FLOATING) { balloon_voice = k; say("SINGING", k == 0 ? "1" : k == 1 ? "2" : "3"); } else if (k >= 8) { Param *p = &P(PG_BALLOON, 4); p->value = p->user = (int16_t)((k - 8) * 127 / 7); } }
        else {
            if (k < 7) choose_weather(k);
            else if (k == 7) toggle_auto();
            else if (k == 15) { strike(); say("LIGHTNING", 0); }
        }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) {
        int k = code - KEY_TRACK0;
        /* T1+T2 together: A B stereo / mono; T3+T4: C D */
        if (k < 4 && (held_tracks & (k < 2 ? 0x3u : 0xcu)) == (k < 2 ? 0x3u : 0xcu)) {
            Param *p = &P(PG_MIX, k < 2 ? 0 : 1);
            p->value = p->user = (int16_t)(pv(PG_MIX, k < 2 ? 0 : 1) == 1 ? 4 : 1);
            say(k < 2 ? "A B" : "C D", p->value == 1 ? "STEREO" : "MONO");
            return 1;
        }
        select_fx(k, func_held);
        return 1;
    }
    if (code == KEY_SRC) { show_page(PG_CHAIN); return 1; }
    if (code == KEY_AMP) { show_page(PG_SKY); return 1; }
    if (code == KEY_LFO) { show_page(page == PG_ALGO ? algo_page() : PG_ALGO); return 1; }
    if (code == KEY_FX1) { show_page(PG_MIX); return 1; }
    if (code == KEY_FX2 || code == KEY_PROJ) { show_page(PG_SYSTEM); return 1; }
    if (code == KEY_PART) { show_page(PG_BALLOON); return 1; }
    if (code == KEY_AED) { show_page(PG_PLAYER); return 1; }
    if (code == KEY_ARR) { show_page(PG_STARS); return 1; }
    if (code >= KEY_PUSH0 && code < KEY_PUSH0 + 6) {
        int k = code - KEY_PUSH0;
        if (page == PG_CHAIN) { fx_user[sel_fx][k] = -1; if (want_of(weather, sel_fx, k) < 0) fx_val[sel_fx][k] = fx[sel_fx].def[k]; say(fx[sel_fx].knob[k], "SKY"); }
        else if (page_param_pages(page)) { Param *p = &params[page][(layer() ? 6 : 0) + k]; if (p->mask) { p->user = -1; say(p->name, "SKY"); } }
    }
    else if (code == KEY_LEVEL_PUSH) toggle_auto();
    else if (code == KEY_LEFT || code == KEY_RIGHT) {
        if (page == PG_CHAIN) select_fx((sel_fx + (code == KEY_RIGHT ? 1 : N_FX - 1)) % N_FX, 0);
        else show_page((page + (code == KEY_RIGHT ? 1 : N_PAGES - 1)) % N_PAGES);
    }
    else if (code == KEY_PAGE) { func_latch = !func_latch; page_until = (int)ticks + 240; say("LAYER", func_latch ? "2" : "1"); }
    else if (code == KEY_UP || code == KEY_DOWN) {
        int d = code == KEY_UP ? 1 : -1;
        if (page == PG_PLAYER) { Param *p = &P(PG_PLAYER, 9); p->value = p->user = (int16_t)clamp(p->value + d, 0, 127); char n[6]; num(n, p->value + 1); say("SAMPLE", n); }
        else if (page == PG_ALGO) { algo = clamp(algo - d, 0, N_ALGO - 1); say("ALGO", algo_name[algo]); }
        else choose_weather(clamp(weather + d, 0, N_WEATHER - 1));
    } else if (code == KEY_PATTERN) { Param *p = &P(PG_STARS, 10); p->value = p->user = (int16_t)!p->value; say("STARS", p->value ? "BIRDS" : "RING"); }
    else if (code == KEY_BANK) { Param *p = &P(PG_STARS, 9); p->value = p->user = (int16_t)((p->value + 1) % 8); char n[6]; num(n, p->value + 1); say("BANK", n); }
    else if (code == KEY_MIDI) { gust = 120; say("GUST", 0); }
    else if (code == KEY_YES && func_held) intro = 0;
    else if (code == KEY_YES) { if (page == PG_ALGO && algo) show_page(algo_page()); else { Param *p = &P(PG_SKY, 10); p->value = p->user = (int16_t)!p->value; say(p->value ? "NIGHT" : "DAY", 0); } }
    else if (code == KEY_NO) { if (page == PG_STARS) { new_constellation(); say("NEW STARS", 0); } else { new_sky(); say("NEW SKY", 0); } }
    return 1;
}

int module_enc(int encoder, int delta)
{
    if (hidden) return 0;
    if (intro < INTRO_LEN) return 1;
    if (encoder == 6) { choose_weather(clamp(weather + (delta > 0 ? 1 : -1), 0, N_WEATHER - 1)); return 1; }
    if (encoder < 0 || encoder > 5) return 1;
    char n[8];
    if (page == PG_CHAIN) {
        const Fx *f = &fx[sel_fx];
        if (!f->knob[encoder][0]) return 1;
        int v = clamp(fx_value(sel_fx, encoder) + delta, 0, f->hi[encoder]);
        fx_user[sel_fx][encoder] = fx_val[sel_fx][encoder] = (int16_t)v;
        num(n, v); say(f->knob[encoder], n);
        return 1;
    }
    if (page == PG_ALGO) { if (encoder == 0) { algo = clamp(algo + (delta > 0 ? 1 : -1), 0, N_ALGO - 1); say("ALGO", algo_name[algo]); } return 1; }
    if (page == PG_SYSTEM) {
        if (encoder > 2) return 1;
        Param *p = &P(PG_SYSTEM, encoder);
        int v = clamp(pv(PG_SYSTEM, encoder) + (delta > 0 ? 1 : -1), p->lo, p->hi);
        p->value = p->user = (int16_t)v;
        if (encoder == 0) for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff;
        return 1;
    }
    if (!page_param_pages(page)) return 1;
    int i = (layer() ? 6 : 0) + encoder;
    Param *p = &params[page][i];
    if (!p->name[0]) return 1;
    int v = clamp(pv(page, i) + delta, p->lo, p->hi);
    p->user = p->value = (int16_t)v;
    if (page == PG_SKY) { if (i == 3) set_weather(v); if (i == 4) auto_weather = v; if (i == 6) xf_right = v; }
    page_until = (int)ticks + 240;
    static const char *const in_names[5] = {"-", "A B", "A", "B", "A+B"};
    static const char *const cd_names[5] = {"-", "C D", "C", "D", "C+D"};
    if (page == PG_SKY && (i == 3 || i == 6)) say(p->name, weather_name[v]);
    else if (page == PG_MIX && i == 0) say("IN AB", in_names[v]);
    else if (page == PG_MIX && i == 1) say("IN CD", cd_names[v]);
    else if (p->hi == 1) say(p->name, v ? "ON" : "OFF");
    else if (page == PG_SKY && i == 0) { num(n, v - 9); say("WIND", n); }
    else { num(n, v); say(p->name, n); }
    return 1;
}
