
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
