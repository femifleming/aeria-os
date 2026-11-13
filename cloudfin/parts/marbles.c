/* ---- MARBLES: a random sequencer for MIDI, after Mutable Instruments Marbles (sadnoise, 10 Nov 2026) --------------
 * After Émilie Gillet's Marbles (open source, MIT; its manual's t/x/DEJA VU ideas, written anew here for AERIA OS).
 *   t  three gates from one clock (RATE: 30..300 BPM, 16ths, or EXT = MIDI clock):
 *      COIN    t2 every step; each step a weighted coin sends t1 or t3 (BIAS: t1 .. t3)
 *      DIVIDE  t2 every step; t1 and t3 on divisions/multiples BIAS picks (/4../2, x2..x4)
 *      DRUMS   t1 kick, t2 hats, t3 snare from 8 grooves (BIAS picks the groove)
 *      INDEP   each t fires on its own coin: t1 1-BIAS, t2 1/2, t3 BIAS
 *      JITR    each gate late by up to half a step, at random
 *   x  three random notes: SPRD (0 a fixed note .. 64 natural .. 127 the extremes), XBIA (where), STEP (0 glides
 *      through every semitone .. 127 only the scale's root and fifth), SCAL, RNGE (1..4 octaves) from ROOT
 *   DEJA VU  how often a random choice repeats the loop of the last LEN choices (0 never .. 127 locked)
 * Each t plays its x as a MIDI note on its own channel (CHAN, CHAN+1, CHAN+2); XMAP (trigs 9-14) rearranges which
 * x goes to which t. YES starts/stops. Gates last half a step. */
#if defined(AERIAOS)
int aos_midi_out(unsigned st, unsigned a, unsigned b);
enum { MT_COIN, MT_DIVIDE, MT_DRUMS, MT_INDEP, N_MTMODES };
static const char *const mt_name[N_MTMODES] = {"COIN", "DIVIDE", "DRUMS", "INDEP"};
static const char *const mx_scale_name[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
static const uint16_t mx_scale[] = {0xab5, 0x5ad, 0x295, 0x6ad, 0xfff};
typedef struct { uint8_t on, rate, tbias, jitr, tmode, deja, len, sprd, xbias, step, scal, rnge, chan, root, xmap; } MarbSet;
static MarbSet mb = {0, 60, 64, 0, 0, 64, 8, 64, 64, 64, 0, 2, 1, 48, 0};
static const uint8_t mb_perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static const uint16_t mb_drums[8][3] = {   /* 16-step masks: kick, hats, snare */
    {0x1111, 0x5555, 0x1010 << 0 | 0x0101 << 2}, {0x1091, 0xffff, 0x0808 << 1}, {0x8421, 0x5555, 0x0404 << 2},
    {0x1131, 0xaaaa, 0x1010 << 2}, {0x0911, 0x7777, 0x2020}, {0x1111, 0xdddd, 0x0410}, {0x9249, 0x5555, 0x2424}, {0x1001, 0xffff, 0x0f0f}};
static uint32_t mb_us, mb_step_us, mb_pending_us[3];
static uint8_t mb_pending[3], mb_note[3] = {0xff, 0xff, 0xff};
static uint32_t mb_gate_us[3];
static uint32_t mb_step;
static uint8_t mb_ring_t[16], mb_ring_x[3][16];
static uint8_t mb_ring_pos;
static uint8_t mb_lastx[3];
static uint32_t mb_flash[3];
static int mb_ext, mb_clocks;
/* a random byte with DEJA VU: the loop of the last LEN draws, replayed with probability DEJA/127 */
static uint8_t mb_draw(uint8_t *ring)
{
    int pos = mb_ring_pos % (mb.len ? mb.len : 1);
    if ((int)(rnd() % 127u) < mb.deja) return ring[pos];
    uint8_t v = (uint8_t)rnd();
    ring[pos] = v;
    return v;
}
static int mb_x_note(int k)
{
    int u = (int)mb_draw(mb_ring_x[k]) * 2 - 255;   /* -255..255 */
    int v2 = (int)mb_draw(mb_ring_x[k]) * 2 - 255;
    int sp = mb.sprd, val;
    if (sp <= 64) val = (u + v2) / 2 * sp / 64;                          /* narrow .. a natural spread */
    else { int s = u < 0 ? -1 : 1, a = iabs(u); val = s * (a + (255 - a) * (sp - 64) / 63); }   /* pushed outwards */
    int pos = clamp((mb.xbias * 255 / 127 - 128) / 2 + val / 2 + 128, 0, 255);          /* 0..255 */
    int span_st = mb.rnge * 12, n = pos * span_st / 255;
    if (mb.step > 16) {   /* quantise to the scale; near the top only the root and fifth */
        uint16_t m = mb.step > 110 ? 0x081 : mx_scale[mb.scal % 5];
        int oct = n / 12, deg = n % 12;
        while (deg > 0 && !((m >> deg) & 1)) deg--;
        n = oct * 12 + deg;
    }
    return clamp(mb.root + n, 0, 127);
}
static void mb_fire(int t)
{
    int x = mb_perm[mb.xmap % 6][t];
    int note = mb_x_note(x), ch = (mb.chan - 1 + t) & 15;
    if (mb_note[t] != 0xff) aos_midi_out(0x80u | (unsigned)ch, mb_note[t], 0);
    aos_midi_out(0x90u | (unsigned)ch, (unsigned)note, 100u);
    mb_note[t] = (uint8_t)note; mb_lastx[x] = (uint8_t)note;
    mb_gate_us[t] = mb_step_us / 2 + 1;
    mb_flash[t] = 12;
}
static void mb_tick_step(void)
{
    int fire[3] = {0, 0, 0};
    uint8_t r = mb_draw(mb_ring_t);
    int b = mb.tbias * 255 / 127, st = (int)(mb_step & 15);
    switch (mb.tmode) {
    case MT_COIN: fire[1] = 1; if (r < b) fire[2] = 1; else fire[0] = 1; break;
    case MT_DIVIDE: { static const uint8_t d1[5] = {4, 3, 2, 1, 1}, d3[5] = {1, 1, 2, 3, 4}; int k = mb.tbias * 4 / 127;
        fire[1] = 1; fire[0] = (mb_step % d1[k]) == 0; fire[2] = (mb_step % d3[k]) == 0; break; }
    case MT_DRUMS: { const uint16_t *g = mb_drums[mb.tbias * 7 / 127]; for (int k = 0; k < 3; k++) fire[k] = (g[k] >> st) & 1; break; }
    default: fire[0] = r > b; fire[1] = (rnd() & 1); fire[2] = (uint8_t)rnd() < b; break;
    }
    for (int k = 0; k < 3; k++) if (fire[k]) {
        uint32_t late = mb.jitr ? (rnd() % (mb_step_us / 2 + 1)) * mb.jitr / 127 : 0;
        mb_pending[k] = 1; mb_pending_us[k] = late;
    }
    mb_ring_pos++; mb_step++;
}
static void marbles_fast(uint32_t us)
{
    if (!mb.on) return;
    if (!mb_ext) {
        mb_step_us = 15000000u / (uint32_t)(30 + mb.rate * 2);       /* a 16th at 30..284 BPM */
        mb_us += us;
        if (mb_us >= mb_step_us) { mb_us -= mb_step_us; if (mb_us > mb_step_us) mb_us = 0; mb_tick_step(); }
    }
    for (int k = 0; k < 3; k++) if (mb_pending[k]) { if (mb_pending_us[k] <= us) { mb_pending[k] = 0; mb_fire(k); } else mb_pending_us[k] -= us; }
    for (int k = 0; k < 3; k++) if (mb_gate_us[k]) {
        if (mb_gate_us[k] <= us) { mb_gate_us[k] = 0; if (mb_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((mb.chan - 1 + k) & 15), mb_note[k], 0); mb_note[k] = 0xff; } }
        else mb_gate_us[k] -= us;
    }
}
static void marbles_tick(void)
{
    for (int k = 0; k < 3; k++) if (mb_flash[k]) mb_flash[k]--;
}
static void marbles_stop(void) { for (int k = 0; k < 3; k++) if (mb_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((mb.chan - 1 + k) & 15), mb_note[k], 0); mb_note[k] = 0xff; } }
static void marbles_midi(unsigned st, unsigned a, unsigned b)
{
    (void)a; (void)b;
    if (st == 0xF8 && mb.rate == 0) { mb_ext = 1; mb_step_us = 100000u; if (mb.on && (++mb_clocks % 6) == 0) mb_tick_step(); }
    if (st == 0xFA && mb.rate == 0) mb_clocks = 0;
    if (mb.rate) mb_ext = 0;
}
static void marbles_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    if (!mb.on) text(40, 10, "YES STARTS", 1);
    /* three rails: a marble drops on each gate, its height the note */
    for (int k = 0; k < 3; k++) {
        int x = 14 + k * 20;
        for (int y = 18; y < 54; y += 2) px(x, y);
        int y = 52 - (mb_lastx[mb_perm[mb.xmap % 6][k]] % 36);
        int r = mb_flash[k] ? 3 : 2;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) px(x + dx, y + dy);
        char t[3] = {'T', (char)('1' + k), 0}; text(x - 3, 56, t, 1);
    }
    const char *lab[6], *val[6]; char nb[6][8];
    if (!layer()) {
        lab[0] = "RATE"; lab[1] = "BIAS"; lab[2] = "JITR"; lab[3] = "MODE"; lab[4] = "DEJA"; lab[5] = "LEN";
        if (mb.rate) num(nb[0], 30 + mb.rate * 2); else { nb[0][0] = 'E'; nb[0][1] = 'X'; nb[0][2] = 'T'; nb[0][3] = 0; }
        num(nb[1], mb.tbias); num(nb[2], mb.jitr); num(nb[4], mb.deja); num(nb[5], mb.len);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = nb[2]; val[3] = mt_name[mb.tmode]; val[4] = nb[4]; val[5] = nb[5];
    } else {
        lab[0] = "SPRD"; lab[1] = "XBIA"; lab[2] = "STEP"; lab[3] = "SCAL"; lab[4] = "RNGE"; lab[5] = "CHAN";
        num(nb[0], mb.sprd); num(nb[1], mb.xbias); num(nb[2], mb.step); num(nb[4], mb.rnge); num(nb[5], mb.chan);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = nb[2]; val[3] = mx_scale_name[mb.scal % 5]; val[4] = nb[4]; val[5] = nb[5];
    }
    for (int i = 0; i < 6; i++) { int x = 74 + (i % 2) * 27, y = 19 + (i / 2) * 13; text(x, y, lab[i], 1); text(x, y + 6, val[i], 1); }
}
static int marbles_key(int code)
{
    if (code == KEY_YES) { mb.on = !mb.on; if (!mb.on) marbles_stop(); say("MARBLES", mb.on ? "ON" : "OFF"); return 1; }
    if (code >= KEY_TRIG0 + 8 && code < KEY_TRIG0 + 14) { mb.xmap = (uint8_t)(code - KEY_TRIG0 - 8); char t[8] = {'X', '>', 'T', ' ', 0}; say("XMAP", t); return 1; }
    return 0;
}
static void marbles_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    if (!layer()) switch (e) {
        case 0: mb.rate = (uint8_t)clamp(mb.rate + d, 0, 127); break;
        case 1: mb.tbias = (uint8_t)clamp(mb.tbias + d, 0, 127); break;
        case 2: mb.jitr = (uint8_t)clamp(mb.jitr + d, 0, 127); break;
        case 3: mb.tmode = (uint8_t)clamp(mb.tmode + st, 0, N_MTMODES - 1); break;
        case 4: mb.deja = (uint8_t)clamp(mb.deja + d, 0, 127); break;
        case 5: mb.len = (uint8_t)clamp(mb.len + st, 1, 16); break;
    } else switch (e) {
        case 0: mb.sprd = (uint8_t)clamp(mb.sprd + d, 0, 127); break;
        case 1: mb.xbias = (uint8_t)clamp(mb.xbias + d, 0, 127); break;
        case 2: mb.step = (uint8_t)clamp(mb.step + d, 0, 127); break;
        case 3: mb.scal = (uint8_t)clamp(mb.scal + st, 0, 4); break;
        case 4: mb.rnge = (uint8_t)clamp(mb.rnge + st, 1, 4); break;
        case 5: marbles_stop(); mb.chan = (uint8_t)clamp(mb.chan + st, 1, 14); break;
    }
}
static void marbles_leds(void)
{
    for (int k = 0; k < 3; k++) led_want[k] = mb_flash[k] ? 3 : mb.on ? 1 : 0;
    for (int k = 0; k < 6; k++) led_want[8 + k] = k == mb.xmap ? 2 : 0;
}
#endif
