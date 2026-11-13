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
enum { PN_FIX, PN_DRIFT, PN_ORBIT, PN_FIG8, PN_PING, PN_DIVE, PN_JUMP, PN_LRNZ, N_PMODES };
static const char *const route_name[N_ROUTES] = {"CHAIN", "MAIN", "CUE", "QUAD"};
static const char *const pmode_name[N_PMODES] = {"FIX", "DRIFT", "ORBIT", "FIG 8", "PING", "DIVE", "JUMP", "LRNZ"};
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
static int lz_speed = 44, lz_chaos = 38, lz_shape = 76;   /* FILT's FUNC knobs set them while a FILT is in the chain */
static void pan_tick(void)
{
    lz_tick(lz_speed, lz_chaos, lz_shape);
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
        case PN_LRNZ: x = bx + lz_out[0] * d / 1000; y = by + lz_out[1] * d / 1000; break;   /* along the Lorenz attractor (Chloaeilius flies its echoes on it) */
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
