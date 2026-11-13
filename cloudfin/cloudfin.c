/* SPDX-License-Identifier: MIT
 * CLOUDFIN sky: a full-screen world for the Octatrack MKII screen.
 *
 * Clouds drift across a 128x64 sky with parallax, water ripples along the
 * bottom and a fin glides through it. While the sky is shown it owns every key
 * and encoder; FUNC+NO hands the unit back to the stock screens (and again
 * brings the sky back), so PROJECT > SYSTEM (USB DISK MODE, OS UPGRADE) is
 * always one gesture away.
 *
 *   A  wind (speed and direction)      B  how many clouds
 *   C  waves                           D  cloud height
 *   YES  day / night                   NO  new sky (reshuffle clouds)
 *   FUNC+NO  show / hide the sky
 *
 * Frame layout (examples/hello.c): 128 columns of 8 bytes; read as two
 * big-endian words, screen line y is bit y of the 64-bit column (bit 0 at the
 * top): word 1 holds lines 0-31, word 0 lines 32-63.
 * No C library; no 64-bit arithmetic. */
#include "modwerk_module.h"

#define W 128
#define H 64
#define MAX_CLOUDS 7
#define CLOUD_MAX_W 44
#define WATER_Y 54

#define KEY_YES 0x31
#define KEY_NO 0x32
#define KEY_FUNC 0x2d

typedef struct {
    int32_t x;          /* left edge, 1/256 pixel */
    int16_t y;          /* flat base line */
    uint8_t w, depth;   /* width in pixels; depth 1 (far) .. 3 (near) */
    int8_t top[CLOUD_MAX_W]; /* height above the base for each column */
} Cloud;

static Cloud clouds[MAX_CLOUDS];
static uint32_t seed = 0x9e3779b9u, ticks, toast_until;
static int32_t fin_x = 40 << 8;
static int wind = 3, count = 5, waves = 2, height = 0;
static int night, hidden, func_held, ready;
static char toast[16];
#define NO_VALUE (-100)

/* 64-step sine, -127..127 */
static const int8_t sine[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12};

static uint32_t rnd(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}

static int range(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

static int isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

/* A cloud's silhouette: puffs of different sizes along a flat base. */
static void make_cloud(Cloud *c, int x_pixels)
{
    int depth = range(1, 3);
    /* far clouds small and high, near clouds big and low */
    int w = depth == 1 ? range(14, 24) : depth == 2 ? range(20, 32) : range(28, CLOUD_MAX_W);
    int puffs = 3 + w / 10;
    c->w = (uint8_t)w;
    c->depth = (uint8_t)depth;
    c->x = x_pixels * 256;
    c->y = (int16_t)(depth == 1 ? range(12, 22) : depth == 2 ? range(20, 32) : range(28, 42));
    for (int i = 0; i < w; i++) c->top[i] = 0;
    for (int p = 0; p < puffs; p++) {
        /* puffs grow toward the middle: a cumulus, flat underneath */
        int cx = range(2, w - 3);
        int mid = w / 2 - (cx > w / 2 ? cx - w / 2 : w / 2 - cx);
        int r = 3 + range(0, 3) + mid * 3 / 4;
        if (r > 4 + w / 3) r = 4 + w / 3;
        int cy = -r / 4; /* centre a little under the base keeps the bottom flat */
        for (int dx = -r; dx <= r; dx++) {
            int col = cx + dx;
            if (col < 0 || col >= w) continue;
            int h = cy + isqrt(r * r - dx * dx);
            if (h > 16) h = 16;
            if (h > c->top[col]) c->top[col] = (int8_t)h;
        }
    }
    for (int i = 0; i < w; i++) if (c->top[i] < 1) c->top[i] = 1;
}

static void new_sky(void)
{
    for (int i = 0; i < MAX_CLOUDS; i++) make_cloud(&clouds[i], range(-30, W));
    ready = 1;
}

/* ---- pixels ------------------------------------------------------------ */

static uint32_t *col_word(uint32_t *f, int x, int y) { return &f[x * 2 + (y < 32 ? 1 : 0)]; }

static void px_set(uint32_t *f, int x, int y)
{
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    *col_word(f, x, y) |= 1u << (y & 31);
}

static void px_clear(uint32_t *f, int x, int y)
{
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    *col_word(f, x, y) &= ~(1u << (y & 31));
}

static uint32_t bits(int a, int b) /* lines a..b inside one word, 0..31 */
{
    uint32_t hi = b >= 31 ? 0xffffffffu : (1u << (b + 1)) - 1u;
    return hi & ~((1u << a) - 1u);
}

/* Apply op to lines a..b of column x: 0 clear, 1 set, 2 set with a pattern. */
static void span(uint32_t *f, int x, int a, int b, int op, uint32_t pattern)
{
    if ((unsigned)x >= W) return;
    if (a < 0) a = 0;
    if (b > H - 1) b = H - 1;
    if (a > b) return;
    for (int half = 0; half < 2; half++) {
        int lo = half ? 32 : 0, hi = lo + 31;
        int s = a > lo ? a : lo, e = b < hi ? b : hi;
        if (s > e) continue;
        uint32_t m = bits(s - lo, e - lo);
        uint32_t *word = &f[x * 2 + (half ? 0 : 1)];
        if (op == 0) *word &= ~m;
        else if (op == 1) *word |= m;
        else *word = (*word & ~m) | (m & pattern);
    }
}

/* ---- tiny 3x5 font (uppercase, digits, + - :) ---------------------------- */

static const uint16_t glyphs[] = {
    /* A-Z, 0-9, + - : (generated from row strings) */
    0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79a7, 0x79a4, 0x396b, 0x5bed, 0x7497, 0x126a,
    0x5bad, 0x4927, 0x5fed, 0x6b6d, 0x2b6a, 0x6ba4, 0x2b73, 0x6bad, 0x388e, 0x7492,
    0x5b6f, 0x5b6a, 0x5bfd, 0x5aad, 0x5a92, 0x72a7, 0x7b6f, 0x2c97, 0x62a7, 0x628e,
    0x5bc9, 0x798e, 0x39ef, 0x7292, 0x7bef, 0x7bce, 0x05d0, 0x01c0, 0x0410};

static int glyph_of(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    if (ch == '+') return 36;
    if (ch == '-') return 37;
    if (ch == ':') return 38;
    return -1;
}

static int text(uint32_t *f, int x, int y, const char *s, int ink)
{
    for (; *s; s++, x += 4) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        uint16_t bitsof = glyphs[g];
        for (int row = 0; row < 5; row++)
            for (int c = 0; c < 3; c++)
                if (bitsof & (1u << (14 - (row * 3 + c)))) {
                    if (ink) px_set(f, x + c, y + row); else px_clear(f, x + c, y + row);
                }
    }
    return x;
}

static void say(const char *label, int value)
{
    int i = 0;
    while (*label && i < 8) toast[i++] = *label++;
    if (value == NO_VALUE) { toast[i] = 0; toast_until = ticks + 90; return; }
    toast[i++] = ' ';
    if (value < 0) { toast[i++] = '-'; value = -value; } else if (value > 0) toast[i++] = '+';
    if (value >= 10) toast[i++] = (char)('0' + value / 10);
    toast[i++] = (char)('0' + value % 10);
    toast[i] = 0;
    toast_until = ticks + 90;
}

/* ---- the world ---------------------------------------------------------- */

static void draw_cloud(uint32_t *f, const Cloud *c, int lift)
{
    int x0 = c->x >> 8, base = c->y - lift;
    /* far clouds are a light dither, near ones denser; outline always solid */
    uint32_t dither_a = c->depth == 3 ? 0x55555555u : c->depth == 2 ? 0x11111111u : 0x01010101u;
    uint32_t dither_b = c->depth == 3 ? 0xaaaaaaaau : c->depth == 2 ? 0x44444444u : 0x10101010u;
    for (int i = 0; i < c->w; i++) {
        int x = x0 + i;
        if ((unsigned)x >= W) continue;
        int top = base - c->top[i];
        span(f, x, top, base, 0, 0);                       /* clouds hide what is behind */
        span(f, x, top + 1, base - 1, 2, (x & 1) ? dither_a : dither_b);
        px_set(f, x, top);
        px_set(f, x, base);
        if (i == 0 || i == c->w - 1) span(f, x, top, base, 1, 0);
        else {
            /* side walls where the silhouette steps */
            int l = c->top[i - 1], r = c->top[i + 1], h = c->top[i];
            if (l < h) span(f, x, base - h, base - l, 1, 0);
            if (r < h) span(f, x, base - h, base - r, 1, 0);
        }
    }
}

static void draw_sky(uint32_t *f)
{
    if (night) {
        /* stars: fixed, twinkling */
        uint32_t s = 0x2545f491u;
        for (int i = 0; i < 40; i++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int x = (int)(s % W), y = (int)((s >> 8) % (WATER_Y - 6));
            if (((s >> 20) + (ticks >> 4)) % 7) px_set(f, x, y);
        }
        /* crescent moon */
        for (int dy = -6; dy <= 6; dy++)
            for (int dx = -6; dx <= 6; dx++)
                if (dx * dx + dy * dy <= 36 && (dx - 3) * (dx - 3) + (dy + 1) * (dy + 1) > 25)
                    px_set(f, 108 + dx, 9 + dy);
    } else {
        /* sun with slowly turning rays */
        for (int dy = -5; dy <= 5; dy++)
            for (int dx = -5; dx <= 5; dx++) {
                int d = dx * dx + dy * dy;
                if (d <= 25 && d >= 16) px_set(f, 110 + dx, 10 + dy);
            }
        for (int k = 0; k < 8; k++) {
            int a = (k * 8 + (int)(ticks >> 3)) & 63;
            int cx = sine[(a + 16) & 63], cy = sine[a];
            for (int r = 8; r <= 10; r++)
                px_set(f, 110 + cx * r / 127, 10 + cy * r / 127);
        }
    }
}

static void draw_water(uint32_t *f)
{
    int t = (int)ticks;
    for (int x = 0; x < W; x++) {
        int a = (x * 3 + t) & 63, b = (x * 7 - t * 2) & 63;
        int y = WATER_Y + (sine[a] * waves + sine[b] * (waves > 0)) / 160;
        span(f, x, y, H - 1, 0, 0);
        px_set(f, x, y);
        /* depth: sparse sparkles drifting with the waves */
        uint32_t k = (uint32_t)(x * 2654435761u) ^ (uint32_t)((t >> 3) * 40503u);
        if ((k >> 27) == 0) px_set(f, x, y + 2 + (int)((k >> 8) % (unsigned)(H - y - 2 > 0 ? H - y - 2 : 1)));
        if (!night && (x + t / 4) % 9 == 0) px_set(f, x, y + 1);
    }
    /* the fin: a dorsal fin cutting the surface, rising and sinking */
    int fx = fin_x >> 8;
    int rise = 5 + sine[(t >> 1) & 63] / 40; /* 2..8 pixels */
    int surface = WATER_Y;
    for (int i = 0; i <= 9; i++) {
        int x = fx + i;
        int h = i < 7 ? (i * rise) / 7 : rise - (i - 7) * rise / 2;
        if (h < 0) h = 0;
        span(f, x, surface - h, surface, 0, 0);
        span(f, x, surface - h, surface, 1, 0);
    }
    /* wake */
    for (int i = 1; i < 14; i++)
        if ((i + t / 3) % 3 == 0) px_set(f, fx - i, surface + 1 + (i & 1));
}

void module_draw(unsigned char *frame)
{
    if (hidden || !ready) return;
    uint32_t *f = (uint32_t *)frame;
    for (int i = 0; i < W * 2; i++) f[i] = 0;
    draw_sky(f);
    /* far clouds first, near clouds over them */
    for (int depth = 1; depth <= 3; depth++)
        for (int i = 0; i < count; i++)
            if (clouds[i].depth == depth) draw_cloud(f, &clouds[i], height);
    draw_water(f);
    text(f, 2, 2, "CLOUDFIN", 1);
    if (ticks < toast_until) {
        int w = 0;
        while (toast[w]) w++;
        int x = (W - w * 4) / 2;
        for (int i = x - 2; i < x + w * 4 + 1; i++) span(f, i, 27, 35, 1, 0);
        text(f, x, 29, toast, 0);
    }
}

void module_tick(struct modwerk_runtime_api *api)
{
    if (!ready) new_sky();
    ticks++;
    for (int i = 0; i < count; i++) {
        Cloud *c = &clouds[i];
        c->x += wind * (16 + 24 * c->depth);
        int x = c->x >> 8;
        if (wind >= 0 && x > W + 2) make_cloud(c, -c->w - range(0, 30));
        if (wind < 0 && x < -CLOUD_MAX_W - 2) make_cloud(c, W + range(0, 30));
    }
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -(40 << 8);
    if ((fin_x >> 8) < -40) fin_x = (W + 20) << 8;
    api->value = (uint32_t)hidden << 31 | (uint32_t)night << 30 | (ticks & 0xffffff);
}

int module_key(int code, int pressed)
{
    if (code == KEY_FUNC) func_held = pressed;
    if (code == KEY_NO && pressed && func_held) {
        hidden = !hidden;
        if (!hidden) say("SKY", NO_VALUE);
        return 1;
    }
    if (hidden) return 0;
    if (!pressed) return 1;
    if (code == KEY_YES) { night = !night; say(night ? "NIGHT" : "DAY", NO_VALUE); }
    else if (code == KEY_NO && !func_held) { new_sky(); say("NEW SKY", NO_VALUE); }
    return 1;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int module_enc(int encoder, int delta)
{
    if (hidden) return 0;
    switch (encoder) {
    case 0: wind = clamp(wind + delta, -9, 9); say("WIND", wind); break;
    case 1: count = clamp(count + delta, 0, MAX_CLOUDS); say("CLOUDS", count); break;
    case 2: waves = clamp(waves + delta, 0, 6); say("WAVES", waves); break;
    case 3: height = clamp(height + delta, -8, 12); say("HEIGHT", height); break;
    default: break;
    }
    return 1;
}
