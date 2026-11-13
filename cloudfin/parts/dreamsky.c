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
