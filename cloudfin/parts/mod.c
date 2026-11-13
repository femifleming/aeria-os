/* ---- MOD: the modulation matrix (AERIA OS, sadnoise, 12 Nov 2026) ------------------------------------------------
 * After Chloaelis: three LFOs, the Lorenz attractor and the flock (boids), each free to move any number of knobs, in
 * the sky and in the sequencer alike. [FUNC] + [ARR] opens the MOD page (both faces):
 *   TRIG 1..16  a route (lit: it moves something; brightest: the one in hand)
 *   A ROUT  the route      B SRC  its source      C DPTH  how far (-63..63: either way; 63 = the knob's whole range)
 *   D E F   the source's own three settings (LFO: RATE SHAP PHAS; LORENZ: SPD CHAS SHAP; FLOCK: SPD COHE SCAT)
 *   YES     learn: the page closes; the next knob you turn, on any page, is the route's destination
 *   FUNC + YES  the route is cleared        NO or FUNC + ARR  closes
 * Destinations: a page knob of the sky (CHAIN's effects' knobs, SKY, LOOP, STARS, BALLOON, MIX, GAIN, MASTER), a
 * sequencer track's parameter (SRC AMP MOD FLT GEN MIDI pages) or a track's effect knob. A knob you hold, or that the
 * weather moves, is moved further from where it is. Everything is kept in battery RAM at 0x100EC000.
 * Sources give -1000..1000 at the UI's tick (61 a second). */
#if defined(AERIAOS)
enum { MS_OFF, MS_LFO1, MS_LFO2, MS_LFO3, MS_LZX, MS_LZY, MS_LZZ, MS_FLKX, MS_FLKY, MS_FLKS, MS_BRD1, MS_BRD2, MS_N };
static const char *const ms_name[MS_N] = {"OFF", "LFO 1", "LFO 2", "LFO 3", "LRNZ X", "LRNZ Y", "LRNZ Z", "FLOCK X", "FLOCK Y", "FLOCK SPRD", "BIRD 1", "BIRD 2"};
enum { MK_NONE, MK_PAGE, MK_SLOT, MK_TRK, MK_TFX };
#define N_ROUTES 16
typedef struct { uint8_t src; int8_t depth; uint16_t dest; } ModRoute;     /* dest: kind << 12 | index */
typedef struct { uint8_t magic[4], ver, pad[3]; ModRoute r[N_ROUTES]; uint8_t lfo[3][3], lz[3], flk[3], pad2[5]; } ModStore;
#define MODS ((volatile ModStore *)0x100EC000u)
void aos_lorenz(int l, int r);
#ifdef SEQ_UI
static void sqsc_apply(void);
#endif
static int mod_open, mod_sel, mod_learn = -1;
static uint8_t led_want[16];   /* (ui12.c's) */
static void strcat_s(char *d, const char *s) { int i = 0; while (d[i]) i++; while (*s && i < 15) d[i++] = *s++; d[i] = 0; }
static int ms_val[MS_N];
static uint32_t lfo_ph[3]; static int lfo_sh[3], lfo_sh_to[3];
static void mod_init(void)
{
    volatile ModStore *m = MODS;
    if (m->magic[0] == 'A' && m->magic[1] == 'E' && m->magic[2] == 'M' && m->magic[3] == 'D' && m->ver == 1) return;
    for (unsigned i = 0; i < sizeof(ModStore); i++) ((volatile uint8_t *)m)[i] = 0;
    for (int i = 0; i < 3; i++) { m->lfo[i][0] = (uint8_t)(40 + 15 * i); m->lfo[i][1] = (uint8_t)i; }
    m->lz[0] = 44; m->lz[1] = 38; m->lz[2] = 76;
    m->flk[0] = 50; m->flk[1] = 64; m->flk[2] = 40;
    m->magic[0] = 'A'; m->magic[1] = 'E'; m->magic[2] = 'M'; m->magic[3] = 'D'; m->ver = 1;
}
/* ---- the sources ---- */
static uint32_t pow2_q8(int e) { uint32_t w = 1u << (e >> 8); return w + (w * (uint32_t)(e & 255)) / 256u; }   /* 2^(e/256), between octaves a line */
static const char *const lfo_shape[7] = {"TRI", "SINE", "SAW", "RAMP", "SQR", "S+H", "SMTH"};
static int lfo_out(int i)
{
    volatile ModStore *m = MODS;
    uint32_t fm = 20u * pow2_q8(m->lfo[i][0] * 10 * 256 / 127);     /* milli-Hz: 0.02 .. 20 Hz */
    uint32_t was = lfo_ph[i];
    lfo_ph[i] += fm * 70409u;                                        /* 2^32 / 61000 */
    uint32_t p = lfo_ph[i] + ((uint32_t)m->lfo[i][2] << 25);         /* PHAS: where it starts against the others */
    int x = (int)(p >> 22);                                          /* 0..1023 */
    int tri = x < 512 ? x * 4 - 1024 : 3072 - x * 4;                 /* -1024..1024 */
    if (tri > 1000) tri = 1000;
    if (tri < -1000) tri = -1000;
    if (lfo_ph[i] < was) { lfo_sh_to[i] = range(-1000, 1000); if (m->lfo[i][1] == 5) lfo_sh[i] = lfo_sh_to[i]; }
    switch (m->lfo[i][1]) {
    case 1: { int a = tri; return a * (2000 - (a < 0 ? -a : a)) / 1000; }      /* a parabola: close to a sine */
    case 2: return x * 2000 / 1023 - 1000;
    case 3: return 1000 - x * 2000 / 1023;
    case 4: return x < 512 ? 1000 : -1000;
    case 5: return lfo_sh[i];
    case 6: lfo_sh[i] += (lfo_sh_to[i] - lfo_sh[i]) / 8; return lfo_sh[i];
    default: return tri;
    }
}
/* the flock: eight birds on a square of -1000..1000, after Chloaelis's boids (cohesion, separation, alignment, a
 * little wander, walls that turn them) */
#define N_BIRDS 8
static int brd_x[N_BIRDS], brd_y[N_BIRDS], brd_vx[N_BIRDS], brd_vy[N_BIRDS], brd_ready;
static void flock_tick(void)
{
    volatile ModStore *m = MODS;
    if (!brd_ready) { for (int i = 0; i < N_BIRDS; i++) { brd_x[i] = range(-600, 600); brd_y[i] = range(-600, 600); brd_vx[i] = range(-8, 8); brd_vy[i] = range(-8, 8); } brd_ready = 1; }
    int spd = 2 + m->flk[0] / 4, coh = m->flk[1], sep = m->flk[2];
    int cx = 0, cy = 0, ax = 0, ay = 0;
    for (int i = 0; i < N_BIRDS; i++) { cx += brd_x[i]; cy += brd_y[i]; ax += brd_vx[i]; ay += brd_vy[i]; }
    cx /= N_BIRDS; cy /= N_BIRDS; ax /= N_BIRDS; ay /= N_BIRDS;
    for (int i = 0; i < N_BIRDS; i++) {
        int dx = (cx - brd_x[i]) * coh / 6000, dy = (cy - brd_y[i]) * coh / 6000;     /* cohesion */
        for (int j = 0; j < N_BIRDS; j++) if (j != i) {                               /* separation */
            int ex = brd_x[i] - brd_x[j], ey = brd_y[i] - brd_y[j], d = (ex < 0 ? -ex : ex) + (ey < 0 ? -ey : ey);
            if (d < 60 + sep * 3) { dx += ex * sep / 2000; dy += ey * sep / 2000; }
        }
        dx += (ax - brd_vx[i]) / 8; dy += (ay - brd_vy[i]) / 8;                       /* alignment */
        dx += range(-2, 2); dy += range(-2, 2);                                       /* wander */
        if (brd_x[i] > 850) dx -= 4;
        if (brd_x[i] < -850) dx += 4;
        if (brd_y[i] > 850) dy -= 4;
        if (brd_y[i] < -850) dy += 4;
        brd_vx[i] = clamp(brd_vx[i] + dx, -spd, spd); brd_vy[i] = clamp(brd_vy[i] + dy, -spd, spd);
        brd_x[i] = clamp(brd_x[i] + brd_vx[i], -1000, 1000); brd_y[i] = clamp(brd_y[i] + brd_vy[i], -1000, 1000);
    }
    int sp = 0;
    for (int i = 0; i < N_BIRDS; i++) { int ex = brd_x[i] - cx, ey = brd_y[i] - cy; sp += (ex < 0 ? -ex : ex) + (ey < 0 ? -ey : ey); }
    sp /= N_BIRDS;
    ms_val[MS_FLKX] = cx; ms_val[MS_FLKY] = cy; ms_val[MS_FLKS] = clamp(sp * 2000 / 1000 - 1000, -1000, 1000);
    ms_val[MS_BRD1] = brd_x[0]; ms_val[MS_BRD2] = brd_y[1];
}
/* ---- where they go ---- */
static int16_t mod_slot[N_SLOTS][6];
#ifdef SEQ_UI
extern int16_t aos_mod_tp[SQ_TRACKS][TP_ALL], aos_mod_fxk[SQ_TRACKS][6];
#endif
static int mod_slot_of(int s, int k) { return mod_slot[s][k]; }
static int mod_range(uint16_t dest)
{
    int k = dest >> 12, i = dest & 0xFFF;
    if (k == MK_PAGE && i < N_PAGES * 12) { const Param *p = &params[i / 12][i % 12]; return p->name[0] ? p->hi - p->lo : 0; }
    if (k == MK_SLOT && i < N_SLOTS * 6) { const Slot *sl = &slots[i / 6]; return sl->row > 0 && sl->knob[i % 6][0] ? sl->count[i % 6] - 1 : 0; }
    if (k == MK_TRK || k == MK_TFX) return 127;
    return 0;
}
static void mod_tick(void)
{
    volatile ModStore *m = MODS;
    mod_init();
    for (int i = 0; i < 3; i++) ms_val[MS_LFO1 + i] = lfo_out(i);
    /* a FILT in the chain (or on a track) owns the attractor's settings: its FUNC knobs LZSP CHAS SHAP are MOD's
     * LRNZ settings too, so there is one attractor and both places show it */
    for (int s = 0; s < N_SLOTS; s++) if (slots[s].row > 0 && slots[s].row <= FX_COUNT && name_has(fx_info[slots[s].row - 1].name, "FILT") && !seq_on) {
        for (int k = 0; k < 3; k++) m->lz[k] = preview_slot_p2[s][k];
        break;
    }
#ifdef SEQ_UI
    if (seq_on) for (int t = 0; t < SQ_TRACKS; t++) { int r = SQH->fx_row[t]; if (r > 0 && r <= FX_COUNT && name_has(fx_info[r - 1].name, "FILT")) { for (int k = 0; k < 3; k++) m->lz[k] = SQH->fx_knob[t][6 + k]; break; } }
#endif
    lz_speed = m->lz[0]; lz_chaos = m->lz[1]; lz_shape = m->lz[2];                     /* the attractor itself runs in pan_tick */
    aos_lorenz(lz_out[0], lz_out[1]);                                                 /* FILT's two cutoffs follow it (main.c) */
    ms_val[MS_LZX] = lz_out[0]; ms_val[MS_LZY] = lz_out[1];
    { int z = lz_z >> 12, rho = 25 + m->lz[1] * m->lz[1] / 104; ms_val[MS_LZZ] = clamp((z - (rho - 1)) * 1000 / (rho > 0 ? rho : 1) * 2, -1000, 1000); }
    flock_tick();
    for (int i = 0; i < N_PAGES * 12; i++) mod_pv[i] = 0;
    for (int s = 0; s < N_SLOTS; s++) for (int k = 0; k < 6; k++) mod_slot[s][k] = 0;
#ifdef SEQ_UI
    for (int t = 0; t < SQ_TRACKS; t++) { for (int k = 0; k < TP_ALL; k++) aos_mod_tp[t][k] = 0; for (int k = 0; k < 6; k++) aos_mod_fxk[t][k] = 0; }
#endif
#ifdef SEQ_UI
    if (seq_on) sqsc_apply();
    if (seq_on && xf_mode() == 1) {                       /* the crossfader as a DJ filter, in the sequencer: every track's own filters */
        int x = xfade(), lp = x < 56 ? (56 - x) * 100 / 56 : 0, hp = x > 72 ? (x - 72) * 100 / 55 : 0;
        for (int t = 0; t < SQ_TRACKS; t++) {
            if (lp) aos_mod_tp[t][TP_LP] = (int16_t)(aos_mod_tp[t][TP_LP] - lp);
            if (hp) aos_mod_tp[t][TP_HP] = (int16_t)(aos_mod_tp[t][TP_HP] + hp);
        }
    }
#endif
    for (int r = 0; r < N_ROUTES; r++) {
        ModRoute q = {m->r[r].src, m->r[r].depth, m->r[r].dest};
        if (!q.src || q.src >= MS_N || !q.depth || !(q.dest >> 12)) continue;
        int rg = mod_range(q.dest); if (!rg) continue;
        int off = ms_val[q.src] * q.depth / 63 * rg / 1000, k = q.dest >> 12, i = q.dest & 0xFFF;
        if (xf_mode() == 3) off = off * xf_scale() / 127;                       /* the crossfader as MOD's depth */
        if (k == MK_PAGE) mod_pv[i] = (int16_t)(mod_pv[i] + off);
        else if (k == MK_SLOT) mod_slot[i / 6][i % 6] = (int16_t)(mod_slot[i / 6][i % 6] + off);
#ifdef SEQ_UI
        else if (k == MK_TRK && i < SQ_TRACKS * TP_ALL) aos_mod_tp[i / TP_ALL][i % TP_ALL] = (int16_t)(aos_mod_tp[i / TP_ALL][i % TP_ALL] + off);
        else if (k == MK_TFX && i < SQ_TRACKS * 6) aos_mod_fxk[i / 6][i % 6] = (int16_t)(aos_mod_fxk[i / 6][i % 6] + off);
#endif
    }
}
/* ---- the crossfader's scenes for the sequencer's tracks (12 Nov 2026): a track knob turned with SCENE A or B held
 * goes into that scene (the same scene numbers as the sky's); the crossfader blends between them. Kept at battery
 * RAM 0x100ED000 (not in projects). */
#ifdef SEQ_UI
#define SQSC ((volatile int8_t *)0x100ED010u)                     /* [16][8 x TP_ALL], -1: not held */
static void sqsc_init(void)
{
    volatile uint8_t *h = (volatile uint8_t *)0x100ED000u;
    if (h[0] == 'A' && h[1] == 'E' && h[2] == 'S' && h[3] == 'C' && h[4] == TP_ALL) return;
    for (int i = 0; i < N_SCENES * SQ_TRACKS * TP_ALL; i++) SQSC[i] = -1;
    h[0] = 'A'; h[1] = 'E'; h[2] = 'S'; h[3] = 'C'; h[4] = TP_ALL;
}
static volatile int8_t *sqsc(int sc, int t, int tp) { return &SQSC[(sc * SQ_TRACKS + t) * TP_ALL + tp]; }
static void sqsc_apply(void)
{
    sqsc_init();
    if (xf_mode()) return;
    int x = xfade();
    for (int t = 0; t < SQ_TRACKS; t++) for (int tp = 0; tp < TP_ALL; tp++) {
        int a = *sqsc(scene_a, t, tp), b = *sqsc(scene_b, t, tp);
        if (a < 0 && b < 0) continue;
        int base = *sq_tp(t, tp), va = a < 0 ? base : a, vb = b < 0 ? base : b;
        aos_mod_tp[t][tp] = (int16_t)(aos_mod_tp[t][tp] + va + (vb - va) * x / 127 - base);
    }
}
#endif
/* the destination's name, for the page */
#ifdef SEQ_UI
static const char *sq_tp_label(int tp);
#endif
static void mod_dest_name(uint16_t dest, char *n)
{
    int k = dest >> 12, i = dest & 0xFFF; n[0] = 0;
    static const char *const pg_short[N_PAGES] = {"CHN", "SKY", "SRC", "LOOP", "STAR", "BALN", "MIX", "SYS", "MIDI", "GAIN", "MSTR"};
    if (k == MK_PAGE) { strcat_s(n, pg_short[i / 12]); strcat_s(n, " "); strcat_s(n, params[i / 12][i % 12].name); }
    else if (k == MK_SLOT) { n[0] = 'S'; n[1] = (char)('1' + i / 6); n[2] = ' '; n[3] = 0; strcat_s(n, slots[i / 6].row > 0 ? slots[i / 6].knob[i % 6] : "-"); }
#ifdef SEQ_UI
    else if (k == MK_TRK) { n[0] = 'T'; n[1] = (char)('1' + i / TP_ALL); n[2] = ' '; n[3] = 0; strcat_s(n, sq_tp_label(i % TP_ALL)); }
    else if (k == MK_TFX) { n[0] = 'T'; n[1] = (char)('1' + i / 6); n[2] = ' '; n[3] = 'F'; n[4] = 'X'; n[5] = (char)('A' + i % 6); n[6] = 0; }
#endif
    else strcat_s(n, "LEARN: YES");
}
/* learn: the knob turned now is the route's destination (called first by the sky's and the sequencer's knob handlers) */
static int mod_learn_take(uint16_t dest)
{
    if (mod_learn < 0 || !(dest >> 12)) return 0;
    volatile ModStore *m = MODS;
    m->r[mod_learn].dest = dest;
    if (!m->r[mod_learn].src) m->r[mod_learn].src = MS_LFO1;
    if (!m->r[mod_learn].depth) m->r[mod_learn].depth = 20;
    static char n[16]; mod_dest_name(dest, n); say("MOD TO", n);
    mod_learn = -1;
    return 1;
}
static int mod_param_of(int which, int *lo, int *hi, const char **lab)
{
    volatile ModStore *m = MODS; int src = m->r[mod_sel].src;
    *lo = 0; *hi = 127;
    if (src >= MS_LFO1 && src <= MS_LFO3) { static const char *const l[3] = {"RATE", "SHAP", "PHAS"}; *lab = l[which]; if (which == 1) *hi = 6; return 0 * 0 + (src - MS_LFO1) * 3 + which; }
    if (src >= MS_LZX && src <= MS_LZZ) { static const char *const l[3] = {"SPD", "CHAS", "SHAP"}; *lab = l[which]; return 9 + which; }
    if (src >= MS_FLKX) { static const char *const l[3] = {"SPD", "COHE", "SCAT"}; *lab = l[which]; return 12 + which; }
    *lab = ""; return -1;
}
static volatile uint8_t *mod_param_ptr(int id) { volatile ModStore *m = MODS; return id < 9 ? &m->lfo[id / 3][id % 3] : id < 12 ? &m->lz[id - 9] : &m->flk[id - 12]; }
static void mod_enc(int e, int delta)
{
    volatile ModStore *m = MODS; volatile ModRoute *r = &m->r[mod_sel]; static char n[16];
    int st = delta > 0 ? 1 : -1;
    if (e == 0 || e == 6) { mod_sel = clamp(mod_sel + st, 0, N_ROUTES - 1); num(n, mod_sel + 1); say("ROUTE", n); return; }
    if (e == 1) { r->src = (uint8_t)clamp(r->src + st, 0, MS_N - 1); say("SOURCE", ms_name[r->src]); return; }
    if (e == 2) { r->depth = (int8_t)clamp(r->depth + delta, -63, 63); signed_text(n, r->depth); say("DEPTH", n); return; }
    int lo, hi; const char *lab; int id = mod_param_of(e - 3, &lo, &hi, &lab);
    if (id < 0) return;
    volatile uint8_t *p = mod_param_ptr(id);
    *p = (uint8_t)clamp(*p + (hi <= 16 ? st : delta), lo, hi);
    if (id < 9 && id % 3 == 1) say(lab, lfo_shape[*p]); else { num(n, *p); say(lab, n); }
}
static int mod_key(int code, int pressed)
{
    if (!pressed) return 1;
    volatile ModStore *m = MODS;
    if (code < KEY_TRIG0 + 16) { mod_sel = code - KEY_TRIG0; static char n[4]; num(n, mod_sel + 1); say("ROUTE", n); return 1; }
    if (code == KEY_YES) {
        if (func_held) { m->r[mod_sel].src = 0; m->r[mod_sel].depth = 0; m->r[mod_sel].dest = 0; say("ROUTE", "CLEARED"); }
        else { mod_learn = mod_sel; mod_open = 0; say("TURN A KNOB", "TO MOVE"); }
        return 1;
    }
    if (code == KEY_NO || code == KEY_ARR) { mod_open = 0; return 1; }
    return 1;
}
static void mod_leds(void)
{
    volatile ModStore *m = MODS;
    for (int k = 0; k < 16; k++) led_want[k] = k == mod_sel ? 3 : (m->r[k].src && m->r[k].dest >> 12 && m->r[k].depth) ? 1 : 0;
}
static void mod_draw(void)
{
    volatile ModStore *m = MODS; char n[24];
    for (int x = 0; x < W; x++) span(x, 0, 63, 0);
    text(1, 1, "MOD", 1);
    /* the routes around the one in hand */
    int top = clamp(mod_sel - 2, 0, N_ROUTES - 5);
    for (int i = 0; i < 5; i++) {
        int r = top + i, y = 9 + i * 7; ModRoute q = {m->r[r].src, m->r[r].depth, m->r[r].dest};
        num(n, r + 1); if (r == mod_sel) for (int x = 0; x < W; x++) span(x, y - 1, y + 5, 1);
        int inv = r == mod_sel ? 0 : 1;
        text(1, y, n, inv); text(12, y, ms_name[q.src], inv);
        if (q.src) { mod_dest_name(q.dest, n); text(56, y, n, inv); signed_text(n, q.depth); text(W - 1 - text_w(n), y, n, inv); }
    }
    /* the source's live value, as a bar, and its three settings */
    int src = m->r[mod_sel].src;
    if (src) { int v = ms_val[src]; int c = 64, w = v * 60 / 1000; for (int x = c; x != c + w; x += w > 0 ? 1 : -1) px(x, 46); px(c, 45); px(c, 47); }
    if (!src) { text(1, 52, "A ROUT B SRC C DPTH", 1); text(1, 58, "YES LEARN  FUNC+YES CLR", 1); }   /* the help, until a source is chosen */
    else {
        /* the route's own knobs across the bottom: A ROUT, B SRC, C DPTH, then the source's D E F (fixed 13 Nov 2026:
         * they were drawn over the help line) */
        static const char *const lab3[3] = {"ROUT", "SRC", "DPTH"};
        for (int e = 0; e < 6; e++) {
            int x = 1 + e * 21; const char *lab = 0; char v[8] = "";
            if (e < 3) { lab = lab3[e]; if (e == 0) num(v, mod_sel + 1); else if (e == 1) { int k = 0; const char *q = ms_name[src]; while (q[k] && k < 4 && q[k] != ' ') { v[k] = q[k]; k++; } if (src >= MS_LFO1 && src <= MS_LFO3) v[k++] = (char)('1' + src - MS_LFO1); v[k] = 0; } else signed_text(v, m->r[mod_sel].depth); }
            else { int lo, hi; int id = mod_param_of(e - 3, &lo, &hi, &lab); if (id < 0) continue; int pvv = *mod_param_ptr(id); if (id < 9 && id % 3 == 1) { int k = 0; while (lfo_shape[pvv][k]) { v[k] = lfo_shape[pvv][k]; k++; } v[k] = 0; } else num(v, pvv); }
            text(x, 52, lab, 1); text(x, 58, v, 1);
        }
    }
}
#endif
