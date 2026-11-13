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
int preview_leds[64];
uint32_t preview_xfade = 0, preview_step = 0, preview_play = 1;
int preview_track;
uint8_t preview_pattern[8][2330 + 100];
static void set_led(int id, int n) { if (id >= 0 && id < 64) preview_leds[id] = n; }
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
    0x0002, 0x2922, 0x224a, 0x0007, 0x2400, 0x2aab, 0x12a4, 0x5f7d};
static int glyph_of(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    static const char more[] = "+-:.()_'&/#";
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

enum { PG_CHAIN, PG_SKY, PG_SOURCE, PG_LOOP, PG_STARS, PG_BALLOON, PG_MIX, PG_SYSTEM, PG_MIDI, PG_GAIN, N_PAGES };
static const char *const page_name[N_PAGES] = {"CHAIN", "SKY", "SOURCE", "LOOP", "STARS", "BALLOON", "MIX", "SYSTEM", "GUMBALLS", "GAIN"};

static Param params[N_PAGES][12] = {
    /* CHAIN: the shelf */
    {EMPTY},
    /* SKY */
    {{"WIND", OWN, 0, 18, 12, 12, 0}, {"CLDS", OWN, 0, 7, 5, 5, 0}, {"WAVE", OWN, 0, 6, 2, 2, 0},
     {"WTHR", OWN, 0, N_WEATHER - 1, CLOUDY, CLOUDY, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, {"XFAD", OWN, 0, 1, 1, 1, 0},
     {"XF B", OWN, 0, N_WEATHER - 1, STORM, STORM, 0}, {"SPEL", OWN, 1, 8, 4, 4, 0}, {"DRFT", OWN, 0, 127, 20, 20, 0},
     {"RATE", OWN, 1, 127, 30, 30, 0}, {"NITE", OWN, 0, 1, 0, 0, 0}, {"LITE", OWN, 0, 1, 1, 1, 0}},
    /* SOURCE: a menu */
    {EMPTY},
    /* LOOP: Flex slot 1 on T1, between START and END */
    {{"STRT", T1M, 1, 0, 127, 0, 0, 0}, {"END", OWN, 1, 127, 127, 127, 0}, {"PTCH", T1M, 0, 0, 127, 64, 64, 0},
     {"RATE", T1M, 3, 0, 127, 127, 127, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"ATK", T1M, 12, 0, 127, 0, 0, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* STARS: one-shots on T2 */
    {{"SPD", OWN, 0, 127, 24, 24, 0}, {"NUM", OWN, 1, 8, 4, 4, 0}, {"PRND", OWN, 0, 127, 0, 0, 0},
     {"ARND", OWN, 0, 127, 0, 0, 0}, {"DRND", OWN, 0, 127, 20, 20, 0}, {"CHNC", OWN, 0, 127, 127, 127, 0},
     {"PTCH", OWN, 0, 127, 64, 64, 0}, {"ATK", OWN, 0, 127, 0, 0, 0}, {"DEC", OWN, 0, 127, 96, 96, 0},
     {"BANK", OWN, 0, 7, 0, 0, 0}, {"MODE", OWN, 0, 1, 0, 0, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}},
    /* BALLOON: the caught sounds, played as a cloud on T1 */
    {{"GRN", T1M, 5, 0, 127, 40, 40, 0}, {"SPRY", OWN, 0, 127, 30, 30, 0}, {"PTCH", T1M, 0, 0, 127, 64, 64, 0},
     {"RPT", T1M, 4, 0, 127, 127, 127, 0}, {"POS", OWN, 0, 127, 20, 20, 0}, {"SEND", OWN, 0, 127, 100, 100, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* MIX: HEAD (T3 FX1): main feedback, its tone, the sources' sends, input level, warmth; then routing */
    {{"FDBK", OWN, 0, 127, 0, 0, 0}, {"FTON", OWN, 0, 127, 90, 90, 0}, EMPTY,
     {"WARM", OWN, 0, 127, 20, 20, 0}, EMPTY, EMPTY,
     {"INAB", OWN, 0, 4, 1, 1, 0}, {"INCD", OWN, 0, 4, 1, 1, 0}, EMPTY,
     EMPTY, EMPTY, {"TIDE", OWN, 0, 127, 0, 0, 0}},
    /* SYSTEM */
    {{"SND", OWN, 0, 2, 1, 1, 0}, {"INTR", OWN, 0, 1, 1, 1, 0}, {"LITE", OWN, 0, 1, 1, 1, 0},
     {"AUTO", OWN, 0, 1, 1, 1, 0}, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* GUMBALLS: MIDI, its own menu */
    {EMPTY},
    /* GAIN: input A/B and C/D (THRU VOL, 64 = unity), the drive into the chain (HEAD IN),
     * the chain's output (T8 AMP VOL, 108 = unity), the cue send and the balance */
    {{"AB", OWN, 0, 127, 64, 64, 0}, {"CD", OWN, 0, 127, 64, 64, 0}, {"DRIV", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"CUE", OWN, 0, 127, 127, 127, 0}, {"BAL", OWN, 0, 127, 64, 64, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
};
#define P(page, i) (params[page][i])
static int pv(int page, int i) { Param *p = &params[page][i]; return p->user >= 0 ? p->user : p->value; }

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

#ifdef PREVIEW
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

/* ---- the weather speaks to knobs by what they are ------------------------------------------
 * role: what a knob does; each weather sets how much of each role it wants, and a knob's
 * target is lo + (hi - lo) * amount / 127. Unknown knobs stay where the musician left them. */
enum { R_NONE, R_SPACE, R_TIME, R_ECHO, R_SEND, R_OPEN, R_DARK, R_GRIT, R_SHIM, R_RESO, R_RATE, R_PITCH, R_SMEAR, N_ROLES };
typedef struct { char knob[5]; uint8_t role, lo, hi; } Rule;
static const Rule rules[] = {
    {"MIX", R_SPACE, 0, 120}, {"WET", R_SPACE, 0, 120}, {"TIME", R_TIME, 30, 127}, {"DECY", R_TIME, 30, 127}, {"DCAY", R_TIME, 30, 127},
    {"SIZE", R_TIME, 30, 127}, {"FB", R_ECHO, 0, 115}, {"FDBK", R_ECHO, 0, 115}, {"SEND", R_SEND, 0, 110}, {"VOL", R_SEND, 20, 110},
    {"WDTH", R_OPEN, 40, 127}, {"LP", R_OPEN, 40, 127}, {"TONE", R_OPEN, 30, 110}, {"DAMP", R_DARK, 0, 110}, {"HP", R_DARK, 0, 50},
    {"DIST", R_GRIT, 0, 90}, {"DRV", R_GRIT, 0, 100}, {"DRIV", R_GRIT, 0, 100}, {"NOIS", R_GRIT, 0, 50}, {"BITS", R_GRIT, 0, 80},
    {"DEP", R_SHIM, 20, 120}, {"DPTH", R_SHIM, 20, 120}, {"SPD", R_RATE, 4, 90}, {"RATE", R_RATE, 4, 90},
    {"RES", R_RESO, 0, 110}, {"Q", R_RESO, 0, 80}, {"HOLD", R_SMEAR, 0, 127}, {"SMR", R_SMEAR, 0, 127}, {"DIFF", R_SMEAR, 20, 127},
};
#define N_RULES ((int)(sizeof rules / sizeof rules[0]))
/* per weather, how much of each role (0..127): SPACE TIME ECHO SEND OPEN DARK GRIT SHIM RESO RATE PITCH SMEAR */
static const uint8_t weather_roles[N_WEATHER][N_ROLES] = {
    /* CLEAR  */ {0, 25, 40, 25, 30, 120, 10, 0, 25, 20, 30, 0, 0},
    /* CLOUDY */ {0, 45, 60, 45, 45, 95, 35, 10, 45, 30, 25, 0, 20},
    /* RAIN   */ {0, 60, 75, 60, 60, 70, 60, 25, 40, 50, 40, 0, 35},
    /* STORM  */ {0, 75, 90, 95, 85, 75, 50, 85, 30, 80, 90, 0, 40},
    /* SNOW   */ {0, 100, 127, 60, 50, 50, 85, 15, 40, 20, 5, 0, 110},
    /* HEAT   */ {0, 30, 45, 35, 30, 95, 20, 50, 120, 30, 80, 0, 60},
    /* FOG    */ {0, 90, 100, 60, 50, 40, 90, 10, 60, 30, 10, 0, 90}};
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
static int slot_value(int s, int k) { return slots[s].user[k] >= 0 ? slots[s].user[k] : slots[s].value[k]; }

/* one pick at a time; the loader may take a few ticks to upload, or refuse (DSP FULL) */
static int picking = -1;
static void slot_pick(int s, int row)
{
    if (picking >= 0) return;
    Slot *sl = &slots[s];
    sl->pending = (int16_t)row;
    sl->wait = 0;
    picking = s;
    pick_effect(slot_track(s), slot_fx(s), row);
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
    int xf = pv(PG_SKY, 5) ? xfade() : -1;
    for (int s = 0; s < N_SLOTS; s++) {
        Slot *sl = &slots[s];
        if (sl->row <= 0 || sl->pending >= 0) continue;
        int t = slot_track(s), fx2 = slot_fx(s);
        for (int k = 0; k < 6; k++) {
            if (!sl->knob[k][0]) continue;
            if (sl->user[k] < 0) {
                int want = slot_target(s, k, weather);
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
typedef struct { uint8_t track, fx2; char name[13]; } Want_fx;
static const Want_fx setup_list[] = {
    {0, 0, "AERIA TAP"}, {1, 0, "AERIA TAP"}, {4, 0, "AERIA HEAD"},
    /* pitch > disto > grains > ring > smear > filter: three long-buffer effects fill this core's FX2 buffers */
    {4, 1, "AERIA PITCH"}, {5, 0, "AERIA DIST"}, {5, 1, "AERIA GRAIN"},
    {6, 0, "AERIA RING"}, {6, 1, "AERIA SMEAR"}, {7, 0, "FILTER"}, {7, 1, "NONE"},
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
        if (!setup_busy()) { slots_init(); sync_to_card(); say("CHAIN READY", 0); }
        return;
    }
    int row = row_of_prefix(w->fx2, w->name);
    if (row < 0 || live_fx(w->track, w->fx2) == list_id(w->fx2, row)) { setup_at++; if (!setup_busy()) { slots_init(); sync_to_card(); say("CHAIN READY", 0); } return; }
    setup_want = list_id(w->fx2, row);
    pick_effect(w->track, w->fx2, row);
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

/* balloons: caught by the recorders of T2-T4 (REC AB, CD, SRC3), sung by T1 */
enum { B_EMPTY, B_FILLING, B_FLOATING, B_POPPING };
typedef struct { uint8_t state, track, key; int16_t size, x, y, pop; } Balloon;
static Balloon balloon[3] = {{B_EMPTY, 1, KEY_REC_AB, 0, 24, 0, 0}, {B_EMPTY, 2, KEY_REC_CD, 0, 64, 0, 0}, {B_EMPTY, 3, KEY_REC3, 0, 104, 0, 0}};
static int balloon_voice;
static uint8_t balloon_strt;
static int any_floating(void) { for (int b = 0; b < 3; b++) if (balloon[b].state == B_FLOATING) return 1; return 0; }

static void set_weather(int w)
{
    if (w == weather) return;
    weather = w;
    amount = 0;
    spell_left = (uint32_t)range(spell_s[w][0], spell_s[w][1]) * 60u * (uint32_t)pv(PG_SKY, 7) / 4u;
    P(PG_SKY, 3).value = P(PG_SKY, 3).user = (int16_t)w;
    for (int s = 0; s < N_SLOTS; s++) for (int k = 0; k < 6; k++) if (rule_of(slots[s].knob[k]) >= 0) slots[s].user[k] = -1;
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
static void sound_tick(void)
{
    keep_alive();
    slots_sound();
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
    balloon_placed = step;
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


/* ---- screensavers: six to play (each plays itself when left alone), seven to watch ----------------
 * ss_start(which) begins one; ss_tick() at 60 Hz; ss_draw() paints the whole frame; ss_key / ss_enc
 * take the keys and encoders (1 = used). Games: LEFT RIGHT UP DOWN, encoder A or LEVEL, YES.
 * After three seconds without input a game runs its own player (attract), forever. */
enum { SS_OFF = 0, SS_PADDLE, SS_BRICKS, SS_ROCKS, SS_SNAKE, SS_BLOCKS, SS_CORRIDOR, SS_LIGHTHOUSE,
       SS_STORM, SS_SLIDE, SS_MEADOW, SS_FOREST, SS_WATERFALL, SS_SHADOWS };
#define SS_COUNT 14
static const char *const ss_name[SS_COUNT] __attribute__((unused)) = {"OFF", "PADDLE", "BRICKS", "ROCKS", "SNAKE", "BLOCKS", "CORRIDOR",
                                              "LIGHTHOUSE", "STORM", "SLIDE", "MEADOW", "FOREST", "WATERFALL", "SHADOWS"};
#define SS_L 1
#define SS_R 2
#define SS_U 4
#define SS_D 8
#define SS_Y 16
#define SS_AI (ss_idle > 180)
static int ss_which, ss_t, ss_idle = 9999, ss_held, ss_fire, ss_encd;
static const int8_t ss_dx[4] = {1, 0, -1, 0}, ss_dy[4] = {0, 1, 0, -1};

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
    struct { uint16_t body[512]; uint32_t occ[16]; int head, len, dir, ndir, food, grow, human, dead; } sn;
    struct { uint16_t bd[21]; uint8_t tex[21][10]; int piece, next, rot, x, y, fall, lines, ai_r, ai_x, dead, clear, full, rep; } bl;
    struct { int x, y, ang, dir, cx, cy, walk; } co;
    struct { int bolt, next, bx; uint32_t seed; } st;
} ss;

/* ---- PADDLE ---- */
#define PD ss.pd
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
#define BR ss.br
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
#define RK ss.rk
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

/* ---- SNAKE: a 32x16 field of 4-pixel cells; the idle player rides a closed tour with shortcuts ---- */
#define SN ss.sn
static int sn_ord(int c) { int x = c & 31, y = c >> 5; if (x == 0) return y ? 512 - y : 0; return 1 + y * 31 + ((y & 1) ? 31 - x : x - 1); }
static int sn_occ(int c) { return (int)(SN.occ[c >> 5] >> (c & 31)) & 1; }
static void sn_set(int c, int on) { if (on) SN.occ[c >> 5] |= 1u << (c & 31); else SN.occ[c >> 5] &= ~(1u << (c & 31)); }
static int sn_step(int c, int d) { int x = (c & 31) + ss_dx[d], y = (c >> 5) + ss_dy[d]; return ((unsigned)x >= 32 || (unsigned)y >= 16) ? -1 : y * 32 + x; }
static void sn_food(void)
{
    for (int n = 0; n < 3000; n++) { int c = (int)(rnd() & 511); if (!sn_occ(c)) { SN.food = c; return; } }
    SN.food = -1;
}
static void sn_reset(void)
{
    for (int i = 0; i < 16; i++) SN.occ[i] = 0;
    SN.head = 2; SN.len = 3;
    for (int i = 0; i < 3; i++) { SN.body[i] = (uint16_t)i; sn_set(i, 1); }
    SN.dir = SN.ndir = 0; SN.grow = 0; SN.dead = 0; SN.human = 0;
    sn_food();
}
static void sn_tick(void)
{
    if (SS_AI && SN.human) sn_reset();
    if (!SS_AI) SN.human = 1;
    if (SN.dead) { if (--SN.dead == 0) sn_reset(); return; }
    if (ss_t % (SS_AI ? (SN.len > 160 ? 1 : 2) : 5)) return;
    int hc = SN.body[SN.head], tail = SN.body[(SN.head - SN.len + 1) & 511], d = -1;
    if (SS_AI) {
        int oh = sn_ord(hc), dt = (sn_ord(tail) - oh + 512) & 511, df = SN.food >= 0 ? (sn_ord(SN.food) - oh + 512) & 511 : 511, bestd = 0, cyc = -1;
        for (int k = 0; k < 4; k++) {
            int n = sn_step(hc, k);
            if (n < 0 || sn_occ(n)) continue;
            int dn = (sn_ord(n) - oh + 512) & 511;
            if (dn == 1) cyc = k;
            if (SN.len < 380 && dn < dt - SN.grow - 4 && dn <= df && dn > bestd) { bestd = dn; d = k; }
        }
        if (d < 0) d = cyc;
        if (d < 0) { SN.dead = 60; return; }
    } else {
        d = SN.ndir;
        if (((d + 2) & 3) == SN.dir) d = SN.dir;
    }
    int n = sn_step(hc, d);
    if (!SN.grow) sn_set(tail, 0);
    if (n < 0 || sn_occ(n)) { SN.dead = 90; return; }
    SN.dir = d;
    SN.head = (SN.head + 1) & 511;
    SN.body[SN.head] = (uint16_t)n;
    sn_set(n, 1);
    if (SN.grow) { SN.grow--; SN.len++; }
    if (n == SN.food) { SN.grow += 3; sn_food(); }
    if (SN.len >= 470 || SN.food < 0) SN.dead = 120;
}
static void sn_draw(void)
{
    int show = !SN.dead || (SN.dead & 8);
    for (int i = 0; show && i < SN.len; i++) {
        int c = SN.body[(SN.head - i) & 511], x = (c & 31) * 4, y = (c >> 5) * 4;
        int lv = i == SN.len - 1 ? 8 : 16;
        for (int k = 0; k < 3; k++) ss_vd(x + k, y, y + 2, lv, 1);
        if (i == 0) { px_off(x + 1, y + 1); continue; }
        int p = SN.body[(SN.head - i + 1) & 511];
        if (p == c + 1) span(x + 3, y, y + 2, 1);
        else if (p == c - 1) span(x - 1, y, y + 2, 1);
        else if (p == c + 32) for (int k = 0; k < 3; k++) px(x + k, y + 3);
        else if (p == c - 32) for (int k = 0; k < 3; k++) px(x + k, y - 1);
    }
    if (SN.food >= 0) {
        int x = (SN.food & 31) * 4 + 1, y = (SN.food >> 5) * 4 + 1;
        px(x, y); px(x - 1, y); px(x + 1, y); px(x, y - 1); px(x, y + 1);
        if (ss_t & 16) px_off(x, y);
    }
}

/* ---- BLOCKS: an eight-piece set of two to five cells, in a 10x21 well ---- */
#define BL ss.bl
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

/* ---- CORRIDOR: a raycast walk through a castle that is not on any map ---- */
#define CO ss.co
static const char co_map[16][17] = {
    "################", "#......W.......#", "#.P.P..#.##B##.#", "#......#.#...#.#",
    "###.####.#.P.#.#", "#...B....#...#.#", "#.#.#.##W##.##.#", "#.#...#......#.#",
    "#.###.#.P..P.#.#", "#...#.#......#.#", "###.#.##.#####.#", "#...#....B.....#",
    "#.#####.##.###.#", "#.....W........#", "#.P.P.#.P.P.P..#", "################"};
static int co_free(int x, int y) { return (unsigned)x < 16 && (unsigned)y < 16 && co_map[y][x] == '.'; }
static void co_pick(void)
{
    int opts[4], n = 0, back = (CO.dir + 2) & 3;
    if (co_free(CO.cx + ss_dx[CO.dir], CO.cy + ss_dy[CO.dir]) && (rnd() % 3u)) { return; }
    for (int k = 0; k < 4; k++) if (k != back && co_free(CO.cx + ss_dx[k], CO.cy + ss_dy[k])) opts[n++] = k;
    CO.dir = n ? opts[rnd() % (uint32_t)n] : back;
}
static void co_tick(void)
{
    int want = CO.dir * 256, diff = ((want - CO.ang + 512) & 1023) - 512;
    if (diff) { CO.ang = (CO.ang + clamp(diff, -9, 9)) & 1023; if (iabs(diff) > 120) return; }
    int tx = (CO.cx + ss_dx[CO.dir]) * 256 + 128, ty = (CO.cy + ss_dy[CO.dir]) * 256 + 128;
    CO.x += clamp(tx - CO.x, -5, 5); CO.y += clamp(ty - CO.y, -5, 5);
    CO.walk++;
    if (CO.x == tx && CO.y == ty) { CO.cx += ss_dx[CO.dir]; CO.cy += ss_dy[CO.dir]; co_pick(); }
}
static void co_draw(void)
{
    int hz = 32 + ss_sin(CO.walk * 12) / 100;
    int dxr = ss_cos(CO.ang) * 2, dyr = ss_sin(CO.ang) * 2, plx = -dyr * 2 / 3, ply = dxr * 2 / 3;
    for (int y = hz + 1; y < H; y++) {
        int rowd = 8192 / (y - hz);
        int fx = (CO.x << 4) + rowd * (dxr - plx) / 16, fy = (CO.y << 4) + rowd * (dyr - ply) / 16;
        int sx = rowd * plx / 1024, sy = rowd * ply / 1024, lv = (y - hz) * 9 / 32 + 1;
        for (int x = 0; x < W; x++, fx += sx, fy += sy) {
            int on = ((fx >> 12) + (fy >> 12)) & 1, edge = ((fx >> 8) & 15) == 0 || ((fy >> 8) & 15) == 0;
            if (!edge && dither_on(x, y, on ? lv : lv / 2)) px(x, y);
        }
    }
    for (int y = 0; y < hz; y++) if (!(y & 3)) for (int x = (y & 4) ? 0 : 2; x < W; x += 4) if (dither_on(x, y, (hz - y) / 12)) px(x, y);
    for (int x = 0; x < W; x++) {
        int cam = (2 * x - 127) * 2, rx = dxr + plx * cam / 256, ry = dyr + ply * cam / 256;
        int mx = CO.x >> 8, my = CO.y >> 8;
        int ddx = iabs(rx) > 0 ? 65536 / iabs(rx) : 0x100000, ddy = iabs(ry) > 0 ? 65536 / iabs(ry) : 0x100000;
        if (ddx > 0x100000) ddx = 0x100000;
        if (ddy > 0x100000) ddy = 0x100000;
        int stx = rx < 0 ? -1 : 1, sty = ry < 0 ? -1 : 1;
        int sdx = (rx < 0 ? (CO.x & 255) : 256 - (CO.x & 255)) * (ddx >> 4) >> 4;
        int sdy = (ry < 0 ? (CO.y & 255) : 256 - (CO.y & 255)) * (ddy >> 4) >> 4;
        int side = 0, cell = '#';
        for (int n = 0; n < 40; n++) {
            if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; } else { sdy += ddy; my += sty; side = 1; }
            if ((unsigned)mx >= 16 || (unsigned)my >= 16) break;
            cell = co_map[my][mx];
            if (cell != '.') break;
        }
        int perp = side ? sdy - ddy : sdx - ddx;
        if (perp < 24) perp = 24;
        int lh = 16384 / perp, top = hz - lh / 2, bot = hz + lh / 2;
        int u = (side ? CO.x + perp * rx / 256 : CO.y + perp * ry / 256) & 255;
        int lv = 16 - perp / 120 - (side ? 4 : 0);
        if (lv < 1) lv = 1;
        span(x, top, bot, 0);
        if (cell == 'P') {
            int l = lv * ss_sin(u * 2) / 127;
            ss_vd(x, top + lh / 8, bot - lh / 8, l, 1);
            ss_vd(x, top, top + lh / 12, lv, 1); ss_vd(x, bot - lh / 12, bot, lv, 1);
            continue;
        }
        if (lh < 12) { ss_vd(x, top, bot, lv, 1); continue; }
        for (int c = 0; c < 4; c++) {
            int ya = top + lh * c / 4, yb = top + lh * (c + 1) / 4 - 1;
            if (((u + (c & 1) * 64) & 127) < 7) continue;
            ss_vd(x, ya + 1, yb, lv - (int)(ss_h((uint32_t)(mx * 16 + my + c * 41 + ((u + (c & 1) * 64) >> 7) * 7)) & 3u), 1);
        }
        int du = iabs(u - 128);
        if (cell == 'W' && du < 50) {
            int ya = top + lh * 3 / 10 - (lh * (50 * 50 - du * du) / (50 * 50)) / 8, yb = top + lh * 3 / 4;
            span(x, ya, yb, 0);
            if ((du % 24) < 3) ss_vd(x, ya, yb, lv, 1); else ss_vd(x, ya, yb, 2, 1);
            span(x, yb + 1, yb + 1, 1);
        }
        if (cell == 'B' && du < 44) {
            int ya = top + lh / 8, yb = top + lh * 5 / 8 + lh * (44 - du) / 200;
            span(x, ya, yb, 0);
            ss_vd(x, ya, yb, lv > 12 ? 16 : lv + 4, 1);
            if (du < 14) { int m = top + lh * 3 / 8, hh = lh * (14 - du) / 120; span(x, m - hh, m + hh, 0); }
            if (du > 40) span(x, ya, yb, 0);
        }
    }
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
#define ST ss.st
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

/* ---- MEADOW ---- */
static int md_hill(int x, int base, int sc, int t)
{
    int u = x + t;
    return base + ss_sin(u * 5 + sc * 100) / (20 + sc * 6) + ss_sin(u * 13 + sc * 333) / 70 + ss_sin(u * 2 + sc * 77) / 30;
}
static void md_cloud(int cx, int cy, int sz)
{
    ss_ell(cx, cy + 2, sz * 2, sz / 2 + 1, 9, 1, 0);
    ss_ell(cx - sz, cy, sz, sz * 3 / 4, 16, 1, 1);
    ss_ell(cx + sz / 3, cy - sz / 3, sz + 1, sz, 16, 1, 1);
    ss_ell(cx + sz + 2, cy + 1, sz * 2 / 3, sz / 2 + 1, 16, 1, 1);
}
static void md_draw(void)
{
    int t = ss_t, sx = 92, sy = 12;
    for (int x = 0; x < W; x++) for (int b = 0; b < 10; b++) ss_vd(x, b * 4, b * 4 + 3, b / 3, 1);
    ss_ell(sx, sy, 7, 7, 16, 1, 0);
    md_cloud(sx - 6 + ss_sin(t / 2) / 30, sy - 7, 8);
    for (int i = 0; i < 3; i++) md_cloud(ss_wrap(i * 61 + t / 7, 190) - 30, 5 + i * 5, 4 + (i & 1) * 2);
    for (int k = 0; k < 6; k++) {
        int xe = sx - 130 + k * 44 + ss_sin(t + k * 200) / 12, w = 8 + (k & 1) * 6;
        ss_tri(sx, sy + 6, xe - w, 44, xe + w, 44, 6 + ss_sin(t * 3 + k * 170) / 50, 1);
    }
    for (int x = 0; x < W; x++) {
        int h1 = md_hill(x, 33, 0, t / 12), h2 = md_hill(x, 42, 1, t / 5);
        span(x, h1, 63, 0); ss_vd(x, h1, 63, 4, 1); px(x, h1);
        span(x, h2, 63, 0); ss_vd(x, h2, 63, 10, 1); px(x, h2);
        if (!(x % 3)) ss_ln(x, h2, x + ss_sin(x * 9 - t * 4) / 100, h2 - 2, 16, 1, 1);
    }
    for (int x = 0; x < W; x++) span(x, md_hill(x, 51, 2, t / 2), 63, 0);
    for (int i = 0; i < 64; i++) {
        uint32_t h = ss_h((uint32_t)i * 5u + 9u);
        int x = ss_wrap((int)(h % 140u) - t / 2, 140) - 6;
        int base = md_hill(x, 51, 2, t / 2) + 1 + (int)((h >> 8) % 12u) * (i % 3 == 0);
        int hgt = 3 + (int)((h >> 12) % 6u), sway = (ss_sin(x * 8 - t * 6) * 2 + ss_sin(x * 3 - t * 3)) * hgt / 700;
        int mx = x + sway / 3, my = base - hgt / 2;
        ss_ln(x, base, mx, my, 16, 1, 1);
        ss_ln(mx, my, x + sway, base - hgt, 16, 1, 1);
        if (!(h >> 20 & 7)) { px(x + sway, base - hgt - 1); px(x + sway - 1, base - hgt); px(x + sway + 1, base - hgt); px_off(x + sway, base - hgt); }
    }
    for (int k = 0; k < 2; k++) {
        int bx = ss_wrap(t / 3 + k * 70, 170) - 20, by = 22 + k * 5 + ss_sin(t * 3 + k * 300) / 60, f = ss_sin(t * 24 + k * 200) / 60;
        ss_ln(bx - 3, by - f, bx, by, 0, 0, 1); ss_ln(bx, by, bx + 3, by - f, 0, 0, 1);
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

/* ---- WATERFALL ---- */
static int wf_edge(int y, int side) { return side ? 81 + ss_sin(y * 23 + 90) / 60 + y / 16 : 46 - ss_sin(y * 19) / 60 - y / 16; }
static void wf_draw(void)
{
    int t = ss_t;
    for (int x = 0; x < W; x++) for (int b = 0; b < 4; b++) ss_vd(x, b * 4, b * 4 + 3, b, 1);
    ss_ell(108, 6, 3, 3, 16, 1, 0);
    for (int i = 0; i < 14; i++) { uint32_t h = ss_h((uint32_t)i + 300u); px((int)(h & 127u), (int)((h >> 8) % 12u)); }
    for (int y = 10; y < 52; y++) {
        int l = wf_edge(y, 0), r = wf_edge(y, 1);
        for (int x = l + 1; x < r; x++) {
            uint32_t h = ss_h((uint32_t)x * 5u);
            int f = y + y * y / 24 + (int)(h & 63u), lv;
            if ((h >> 8 & 7u) < 2) lv = 5;
            else lv = ((f - t * 3) % 44 + 44) % 44 < 34 ? 16 : 6;
            if (x - l < 2 || r - x < 2) lv = 8;
            if (y < 13) lv = 16;
            ss_p(x, y, lv, 1);
        }
    }
    for (int x = 0; x < W; x++) {
        int top = 11 + ss_sin(x * 31) / 64 + (x < 40 || x > 88 ? 3 - iabs(x - 64) / 40 : 0);
        int l = wf_edge(30, 0), r = wf_edge(30, 1);
        if (x < 38 || x > 90) span(x, top, 51, 0);
        else for (int y = top; y < 52; y++) {
            if (x > wf_edge(y, 0) && x < wf_edge(y, 1)) continue;
            px_off(x, y);
            if (x == wf_edge(y, 0) || x == wf_edge(y, 1)) ss_p(x, y, 10, 1);
        }
        px(x, top);
        if (x < l - 1 || x > r + 1)
            for (int k = 0; k < 4; k++) {
                int y = 19 + k * 8 + ss_sin(x * 20 + k * 300) / 40 + (x > 64 ? 3 : 0);
                if (((x + k * 13) / 6) % 3) { px(x, y); if (k & 1) ss_p(x, y + 1, 6, 1); }
                if (!((x * 7 + k * 5) % 23)) ss_ln(x, y, x + 1, y + 6, 6, 1, 1);
            }
        if (x < 42 || x > 86) ss_vd(x, top - 3 - ((x * 7) & 3), top - 1, 8, 1);
    }
    for (int k = 0; k < 5; k++) { int vx = k < 3 ? 40 - k * 5 : 88 + (k - 3) * 6; ss_ln(vx, 12, vx + ss_sin(t * 2 + k * 200) / 100, 20 + k * 3, 8, 1, 1); }
    for (int x = 0; x < W; x++) span(x, 52, 63, 0);
    for (int x = 46; x < 82; x++) for (int y = 53; y < H; y++) if (((y * 3 + t / 2) & 7) < 3) ss_p(x, y, 4, 1);
    for (int k = 0; k < 4; k++) {
        int r = (t / 2 + k * 15) % 60, rx = 22 + r, ry = 2 + r / 8;
        int lv = 12 - r / 5;
        for (int a = 0; a < 512; a += 10) ss_p(64 + ss_cos(a) * rx / 127, 53 + ss_sin(a) * ry / 127, lv, 1);
    }
    for (int i = 0; i < 26; i++) {
        uint32_t h = ss_h((uint32_t)i * 17u + 4u);
        int age = (t + (int)(h % 64u)) % 64, dir = (int)(h >> 8 & 31u) - 15;
        int x = 64 + dir * age / 10, y = 52 - age / 4 - (int)(h >> 14 & 3u);
        ss_ell(x, y, 3 + age / 12, 2 + age / 20, 8 - age / 8, 1, 0);
    }
    for (int i = 0; i < 12; i++) {
        uint32_t h = ss_h((uint32_t)i * 29u + 8u);
        int age = (t + (int)(h % 30u)) % 30, vx = (int)(h >> 8 & 15u) - 8;
        px(64 + vx * age / 6, 52 - age * (30 - age) / 18);
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

/* ---- the API ---- */
static __attribute__((unused)) void ss_start(int which)
{
    uint32_t *p = (uint32_t *)(void *)&ss;
    for (unsigned i = 0; i < sizeof ss / 4; i++) p[i] = 0;
    ss_which = which; ss_t = 0; ss_idle = 9999; ss_held = 0; ss_fire = 0; ss_encd = 0;
    if (which == SS_PADDLE) { PD.py[0] = PD.py[1] = 32 << 8; pd_serve(1); }
    else if (which == SS_BRICKS) br_reset();
    else if (which == SS_ROCKS) rk_reset();
    else if (which == SS_SNAKE) sn_reset();
    else if (which == SS_BLOCKS) bl_reset();
    else if (which == SS_CORRIDOR) { CO.cx = 1; CO.cy = 1; CO.x = CO.y = 384; CO.dir = 0; CO.ang = 0; }
    else if (which == SS_STORM) ST.next = 60;
}
static __attribute__((unused)) void ss_tick(void)
{
    ss_t++;
    if (ss_idle < 100000) ss_idle++;
    if (ss_which == SS_PADDLE) pd_tick();
    else if (ss_which == SS_BRICKS) br_tick();
    else if (ss_which == SS_ROCKS) rk_tick();
    else if (ss_which == SS_SNAKE) sn_tick();
    else if (ss_which == SS_BLOCKS) bl_tick();
    else if (ss_which == SS_CORRIDOR) co_tick();
    else if (ss_which == SS_STORM) st_tick();
    ss_fire = 0;
}
static __attribute__((unused)) void ss_draw(void)
{
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (ss_which == SS_PADDLE) pd_draw();
    else if (ss_which == SS_BRICKS) br_draw();
    else if (ss_which == SS_ROCKS) rk_draw();
    else if (ss_which == SS_SNAKE) sn_draw();
    else if (ss_which == SS_BLOCKS) bl_draw();
    else if (ss_which == SS_CORRIDOR) co_draw();
    else if (ss_which == SS_LIGHTHOUSE) lh_draw();
    else if (ss_which == SS_STORM) st_draw();
    else if (ss_which == SS_SLIDE) sl_draw();
    else if (ss_which == SS_MEADOW) md_draw();
    else if (ss_which == SS_FOREST) fr_draw();
    else if (ss_which == SS_WATERFALL) wf_draw();
    else if (ss_which == SS_SHADOWS) sh_draw();
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
    if (ss_which == SS_SNAKE) SN.ndir = bit == SS_R ? 0 : bit == SS_D ? 1 : bit == SS_L ? 2 : bit == SS_U ? 3 : SN.ndir;
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
/* ---- the card: AERIA's own browser (research-card.md) ------------------------------------------
 * Listing: the firmware's own opendir/readdir (the calls stock DIRSCAN makes). Loading: stock
 * 0x40022610(kind, slot, path) clears the slot, writes the path into the project's sample list and
 * posts the engine's load; its popups sit under AERIA's screen. Done: the slot's status long
 * (+8: 0 loaded, 1 empty, 2 loading, 3 error). Preview: Static slot 128, played with
 * 0x40093678(0, 128, -1, 0, 1, 1), stopped with 0x40093e9c(0). All of it in the UI task. */
#define BR_MAX 192
#define BR_NAME 28
enum { BR_OFF, BR_LOOP, BR_STARS };
static int br_mode, br_count, br_top, br_cur, br_depth, br_busy, br_preview;
static char br_path[0x104];
static char br_names[BR_MAX][BR_NAME];
static uint8_t br_dir[BR_MAX];
static char br_status[24];
/* STARS: a folder's WAVs dealt into 16 slots, one load at a time */
static int deal_left, deal_slot, deal_waiting;
static int16_t deal_order[16];

#ifdef PREVIEW
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
    for (int i = n - 1; i > 0; i--) { int j = range(0, i), t = wavs[i]; wavs[i] = wavs[j]; wavs[j] = t; }
    for (int k = 0; k < 16; k++) deal_order[k] = (int16_t)wavs[k % n];
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
        if (!deal_left) { br_busy = 0; say("STARS LOADED", 0); }
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
        else if (br_dir[br_cur] && !(func_held && br_mode == BR_STARS)) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
        else if (br_mode == BR_STARS) {
            if (br_preview) { wav_preview_stop(); br_preview = 0; }
            if (br_dir[br_cur]) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
            if (wav_count()) { deal_folder(pv(PG_STARS, 9)); browser_close(); } else say("NO WAVS HERE", 0);
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
    text(2, 57, br_mode == BR_STARS ? "YES DEAL  FUNC+YES HEAR" : "YES LOAD  FUNC+YES HEAR", 1);
}

/* ---- SYSTEM: the stock routines behind PROJECT > SYSTEM, under AERIA's screen (research-card.md) ---- */
static int usb_mode;
#ifdef PREVIEW
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

#ifdef PREVIEW
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
    int fx2 = slot_fx(sel_slot), n = list_count(fx2);
    text(2, 10, "SHELF", 1);
    char where[8] = "SLOT ";
    num(where + 5, sel_slot + 1);
    text(W - 2 - text_w(where), 10, where, 1);
    for (int x = 0; x < W; x += 2) px(x, 16);
    int top = shelf_row - 2 < 0 ? 0 : shelf_row - 2;
    for (int r = 0; r < 6 && top + r < n; r++) {
        int i = top + r, yy = 19 + r * 6;
        char nm[14];
        list_name(fx2, i, nm);
        if (i == 0) { nm[0] = '-'; nm[1] = 0; }
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
    panel_clear(10, 56);
    if (usb_mode) { text(20, 20, "USB DISK MODE", 1); text(14, 32, "NO: BACK TO THE SKY", 1); return; }
    text(4, 12, "1 USB DISK MODE", 1);
    text(4, 19, "2 OS UPGRADE  FUNC+YES", 1);
    text(4, 28, "A SOUND", 1); text(52, 28, pv(PG_SYSTEM, 0) == 0 ? "OFF" : pv(PG_SYSTEM, 0) == 1 ? "KNOBS" : "LANE", 1);
    text(4, 35, "B INTRO", 1); text(52, 35, pv(PG_SYSTEM, 1) ? "ON" : "OFF", 1);
    text(4, 42, "C LIGHTS", 1); text(52, 42, pv(PG_SYSTEM, 2) ? "ON" : "OFF", 1);
    text(4, 49, "D AUTOPLAY", 1); text(52, 49, pv(PG_SYSTEM, 3) ? "ON" : "OFF", 1);
}

static int param_page(int pg) { return pg == PG_SKY || pg == PG_LOOP || pg == PG_STARS || pg == PG_BALLOON || pg == PG_MIX || pg == PG_GAIN; }
static void draw_page(void)
{
    if (br_mode) { draw_browser(); return; }
    if (usb_mode && page == PG_SYSTEM) { draw_system(); return; }
    if (ticks > ui_until && !shelf_open && !func_held) return; /* idle: the landscape alone */
    if (page == PG_CHAIN && setup_busy()) { panel_clear(10, 47); text(26, 22, "FILLING THE SHELF", 1); for (int i = 0; i < N_SETUP; i++) px(30 + i * 5, 32), px(30 + i * 5, 33), (void)0; for (int i = 0; i <= setup_at && i < N_SETUP; i++) { span(29 + i * 5, 30, 35, 1); span(30 + i * 5, 30, 35, 1); } return; }
    if (page == PG_CHAIN) { if (shelf_open) draw_shelf(); else draw_chain(); return; }
    if (page == PG_SOURCE) { draw_source(); return; }
    if (page == PG_SYSTEM) { draw_system(); return; }
    if (page == PG_MIDI) { gum_draw(); return; }
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
}

static void draw_hud(void)
{
    char title[14];
    if (page == PG_CHAIN && !shelf_open && !br_mode) upper(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", title, 12);
    else upper(br_mode ? (br_mode == BR_LOOP ? "LOOP" : "STARS") : page_name[page], title, 12);
    int showing = br_mode || shelf_open || usb_mode || ticks <= ui_until || func_held;
    if (showing) label(2, title);
    const char *w = weather_name[weather];
    label(W - 2 - text_w(w) - (auto_weather ? 8 : 0), w);
    if (auto_weather) label(W - 5, "A");
    if (showing && !br_mode && (page == PG_CHAIN || page == PG_MIX)) {
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

/* ---- LEDs: trig k red 2k, green 2k+1; tempo 38/39 ------------------------------------------------- */
static uint8_t led_want[16];
static void leds_out(void)
{
    leds_clear_trigs();
    for (int k = 0; k < 16; k++) { if (led_want[k] & 1) set_led(2 * k, 3); if (led_want[k] & 2) set_led(2 * k + 1, 3); }
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
        /* 1-11 the slots: green the selected, red holding an effect, dark empty; 12-16 weather tools */
        for (int s = 0; s < N_SLOTS; s++) led_want[s] = s == sel_slot ? 2 : (slots[s].row > 0 && !((slot_bypass >> s) & 1)) ? 1 : 0;
        if (slots[sel_slot].pending >= 0 && (ticks & 8)) led_want[sel_slot] = 3;
        led_want[11] = 1; led_want[12] = 1; led_want[13] = 1; /* shelf, weather down, weather up */
        led_want[14] = auto_weather ? 2 : 0;
        led_want[15] = thunder ? 3 : 0;
        if (thunder > 150) for (int s = 0; s < N_SLOTS; s++) led_want[s] = 3;
    } else if (page == PG_SKY) {
        led_want[weather] = 2;
        if (scene_held) led_want[scene_held == 1 ? weather : xf_right] = 3;
        led_want[7] = auto_weather ? 2 : (uint8_t)((ticks >> 5) & 1);
    } else if (page == PG_SOURCE) {
        led_want[0] = src_loop ? 2 : 1; led_want[1] = src_stars ? 2 : 1; led_want[2] = src_balloons ? 2 : 1;
    } else if (page == PG_STARS) {
        for (int p = 0; p < PLANETS; p++) if ((planet_mask >> p) & 1) led_want[p] = 1;
        if (planet_hit >= 0 && star_flash) led_want[planet_hit] = 2;
    } else if (page == PG_BALLOON) {
        for (int b = 0; b < 3; b++) led_want[b] = balloon[b].state == B_FLOATING ? (b == balloon_voice ? 2 : 1) : (balloon[b].state == B_FILLING && (ticks & 8)) ? 1 : 0;
    } else if (page == PG_LOOP) {
        int a = pv(PG_LOOP, 0) * 16 / 128, b = pv(PG_LOOP, 1) * 16 / 128;
        for (int k = 0; k < 16; k++) if (k >= a && k <= b) led_want[k] = 1;
    }
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

void module_tick(struct modwerk_runtime_api *api)
{
    if (!ready) { new_sky(); new_constellation(); slots_init(); spell_left = 3600u; ready = 1; if (!pv(PG_SYSTEM, 1)) intro = INTRO_LEN; }
    ticks++;
    if (ticks == 30 || (ticks % 300) == 0) for (int s = 0; s < N_SLOTS; s++) if (slots[s].pending < 0 && (slots[s].id != live_fx(slot_track(s), slot_fx(s)) || ticks == 30)) { slot_refresh(s); slots[s].pending = -1; }
    if (intro < INTRO_LEN) intro++;
    if (amount < 256) amount += 2;
    if (flash) flash--;
    if (bolt_t) bolt_t--;
    if (star_flash) star_flash--;
    if (gust) gust--;
    if (thunder) thunder = thunder > 3 ? thunder - 3 : 0;
    night = pv(PG_SKY, 10);
    write_mode = pv(PG_SYSTEM, 0);
    usb_tick();
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
    picks_tick();
    if (!setup_done && ticks > 120 && rig_loaded()) { setup_done = 1; setup_start(); }
    setup_tick();
    deal_tick();
    loop_tick();
    preview_tick();
    if (!usb_mode) gum_tick();
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -40 * 256;
    if ((fin_x >> 8) < -40) fin_x = (W + 20) * 256;
    if (!hidden) leds_tick();
    /* the active track stays on the chain's head (FRACT and stock draw for the active track) */
    if (!hidden && !usb_mode && (ticks & 15) == 0 && current_track() != HEAD_T) set_current_track(HEAD_T);
    /* the effects run as soon as the sky opens: start the transport once */
    if (!autoplayed && ticks > 600 && pv(PG_SYSTEM, 3) && rig_loaded() && !playing() && !usb_mode) { transport_start(); autoplayed = 1; }
    sequencer_tick();
    if (!usb_mode) sound_tick();
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
    if (pressed && code != KEY_PLAY && code != KEY_STOP) ui_until = ticks + UI_SHOW;
    if (code == KEY_FUNC) { func_held = pressed; if (pressed) page_until = (int)ticks + 240; }
    if (code == KEY_NO && pressed && func_held && !usb_mode) { hidden = !hidden; if (!hidden) show_page(page); return 1; }
    if (hidden) return 0;
    if (popup_open() && !usb_mode) return 0; /* a stock or base message: its keys */
    if (usb_mode) { if (pressed && code == KEY_NO) usb_leave(); return 1; }
    if (code == KEY_PLAY || code == KEY_STOP || code == KEY_REC || code == KEY_CUE || code == 0x18) return br_mode ? 1 : 0;
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) { if (pressed) held_tracks |= 1u << (code - KEY_TRACK0); else held_tracks &= ~(1u << (code - KEY_TRACK0)); }
    if (code == KEY_SCENEA || code == KEY_SCENEB) { scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0; if (pressed) say("XF", weather_name[xf_right]); return 1; }
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (code != n->key) continue;
        if (func_held) { if (pressed && n->state != B_EMPTY && n->state != B_FILLING) { n->state = B_POPPING; n->pop = 12; } return 1; }
        set_current_track(n->track);
        if (pressed) {
            if (n->state == B_FILLING) { n->state = B_FLOATING; balloon_voice = b; say("BALLOON", b == 0 ? "1 UP" : b == 1 ? "2 UP" : "3 UP"); }
            else { n->state = B_FILLING; n->size = 0; n->y = WATER_Y - 12; say("CATCHING", b == 0 ? "A B" : b == 1 ? "C D" : "SKY"); }
        }
        return 0;
    }
    if (!pressed) return 1;
    if (intro < INTRO_LEN) { intro = INTRO_LEN; return 1; }
    if (br_mode) { browser_key(code); return 1; }

    /* the shelf is open: UP/DOWN or LEVEL choose, YES puts it on, NO goes back */
    if (shelf_open) {
        int n = list_count(slot_fx(sel_slot));
        if (code == KEY_UP && shelf_row > 0) shelf_row--;
        else if (code == KEY_DOWN && shelf_row < n - 1) shelf_row++;
        else if (code == KEY_YES) { shelf_open = 0; slot_bypass &= (uint16_t)~(1u << sel_slot); slot_pick(sel_slot, shelf_row); }
        else if (code == KEY_NO) shelf_open = 0;
        return 1;
    }

    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (scene_held && k < N_WEATHER) {
            if (scene_held == 2) { xf_right = k; P(PG_SKY, 6).value = P(PG_SKY, 6).user = (int16_t)k; say("XF B", weather_name[k]); }
            else choose_weather(k);
        } else if (page == PG_CHAIN) {
            if (k < N_SLOTS) { if (func_held) toggle_bypass(k); else select_slot(k); }
            else if (k == 11 || k == 7) { shelf_open = 1; shelf_row = slots[sel_slot].row > 0 ? slots[sel_slot].row : 0; }
            else if (k == 12) choose_weather(clamp(weather - 1, 0, N_WEATHER - 1));
            else if (k == 13) choose_weather(clamp(weather + 1, 0, N_WEATHER - 1));
            else if (k == 14) toggle_auto();
            else { strike(); say("LIGHTNING", 0); }
        } else if (page == PG_SOURCE) {
            if (k == 0) { if (func_held) browser_open(BR_LOOP); else { src_loop = !src_loop; if (src_loop) src_balloons = 0; say("LOOP", src_loop ? "ON" : "OFF"); } }
            else if (k == 1) { if (func_held) browser_open(BR_STARS); else { src_stars = !src_stars; say("STARS", src_stars ? "ON" : "OFF"); } }
            else if (k == 2) { src_balloons = !src_balloons; if (src_balloons) src_loop = 0; say("BALLOONS", src_balloons ? "ON" : "OFF"); }
        } else if (page == PG_SYSTEM) {
            if (k == 0) usb_enter();
            else if (k == 1) say("OS UPGRADE", "FUNC+YES");
        } else if (page == PG_STARS) { planet_mask ^= (uint16_t)(1u << k); say("PLANET", (planet_mask >> k) & 1 ? "ON" : "OFF"); }
        else if (page == PG_MIDI) gum_key(code);
        else if (page == PG_LOOP) { Param *p = &P(PG_LOOP, func_held ? 1 : 0); p->value = p->user = (int16_t)(k * 127 / 15); }
        else if (page == PG_BALLOON) { if (k < 3 && balloon[k].state == B_FLOATING) { balloon_voice = k; say("SINGING", k == 0 ? "1" : k == 1 ? "2" : "3"); } else if (k >= 8) { Param *p = &P(PG_BALLOON, 4); p->value = p->user = (int16_t)((k - 8) * 127 / 7); } }
        else { if (k < 7) choose_weather(k); else if (k == 7) toggle_auto(); else if (k == 15) { strike(); say("LIGHTNING", 0); } }
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
    else if (code == KEY_BANK) { if (page == PG_CHAIN) { shelf_open = 1; shelf_row = slots[sel_slot].row > 0 ? slots[sel_slot].row : 0; } else { Param *p = &P(PG_STARS, 9); p->value = p->user = (int16_t)((p->value + 1) % 8); char n[6]; num(n, p->value + 1); say("BANK", n); } }
    else if (code == KEY_MIDI) { if (func_held) { gust = 120; say("GUST", 0); } else show_page(PG_MIDI); }
    else if (code == KEY_YES && page == PG_MIDI && !func_held) gum_key(code);
    else if (code == KEY_YES && func_held && page == PG_SYSTEM) os_upgrade();
    else if (code == KEY_YES && func_held) intro = 0;
    else if (code == KEY_YES) { if (page == PG_CHAIN && slots[sel_slot].row <= 0) { shelf_open = 1; shelf_row = 0; } else { Param *p = &P(PG_SKY, 10); p->value = p->user = (int16_t)!p->value; say(p->value ? "NIGHT" : "DAY", 0); } }
    else if (code == KEY_NO) { if (page == PG_STARS) { new_constellation(); say("NEW STARS", 0); } else { new_sky(); say("NEW SKY", 0); } }
    return 1;
}

int module_enc(int encoder, int delta)
{
    ui_until = ticks + UI_SHOW;
    if (hidden || usb_mode) return hidden ? 0 : 1;
    if (popup_open()) return 0;
    if (intro < INTRO_LEN) return 1;
    if (br_mode) { if (encoder == 6 || encoder == 0) browser_enc(delta); return 1; }
    if (shelf_open) { if (encoder == 6 || encoder == 0) shelf_row = clamp(shelf_row + (delta > 0 ? 1 : -1), 0, list_count(slot_fx(sel_slot)) - 1); return 1; }
    if (encoder == 6) { choose_weather(clamp(weather + (delta > 0 ? 1 : -1), 0, N_WEATHER - 1)); return 1; }
    if (encoder < 0 || encoder > 5) return 1;
    char n[8];
    if (page == PG_CHAIN) {
        Slot *sl = &slots[sel_slot];
        if (sl->row <= 0 || !sl->knob[encoder][0]) return 1;
        int v = clamp(slot_value(sel_slot, encoder) + delta, 0, sl->count[encoder] - 1);
        sl->user[encoder] = sl->value[encoder] = (int16_t)v;
        num(n, v); say(sl->knob[encoder], n);
        return 1;
    }
    if (page == PG_MIDI) { gum_enc(encoder, delta, layer()); return 1; }
    if (page == PG_SYSTEM) {
        if (encoder > 3) return 1;
        Param *p = &P(PG_SYSTEM, encoder);
        p->value = p->user = (int16_t)clamp(pv(PG_SYSTEM, encoder) + (delta > 0 ? 1 : -1), p->lo, p->hi);
        if (encoder == 0) for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff;
        return 1;
    }
    if (!param_page(page)) return 1;
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
    else if (page == PG_MIX && i == 6) say("IN AB", in_names[v]);
    else if (page == PG_MIX && i == 7) say("IN CD", cd_names[v]);
    else if (page == PG_GAIN && i != 5) { int u = i == 3 ? 108 : i == 4 ? 127 : 64; n[0] = v >= u ? '+' : '-'; num(n + 1, iabs(v - u)); say(p->name, n); }
    else if (p->hi == 1) say(p->name, v ? "ON" : "OFF");
    else if (page == PG_SKY && i == 0) { num(n, v - 9); say("WIND", n); }
    else { num(n, v); say(p->name, n); }
    return 1;
}
