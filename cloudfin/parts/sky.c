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
