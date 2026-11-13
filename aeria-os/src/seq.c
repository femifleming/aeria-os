/* ---- AERIA OS: the sequencer's sound side (sadnoise, 12 Nov 2026) ---------------------------------------------------
 * Eight tracks, each its own channel into the DSP: a mono voice made here (a sample from the static slots, or SUB: a
 * sine with a falling pitch), sent as its own word of the stream (eight a sample; engine.asm stmseq), and there
 * given the track's high-pass and low-pass (2-pole, resonance fixed at 20 %), the effect in the track's slot, and its
 * place among the outputs (the slot's routing block). The freeze delay takes each track's DLY send.
 *
 * The clock counts samples: a step fires at its own sample inside the 32-sample block being made (the voice's
 * delay), so swing, the forty-eighths of micro-timing and ratchets are exact. Each track has its own length and
 * place (polymetre); FREE tracks are not brought back to their first step when the pattern changes.
 * The data is seq.h's, in battery RAM. */
#include "seq.h"
#include "seq_table.inc"
typedef struct {
    const int32_t *q; int32_t st;        /* the first frame and the stride (2 forward, -2 backward) */
    uint32_t n, pos, frac, step, ph;
    int32_t env, env_inc, env_dec, gain, pe, pe_rate;
    uint32_t delay;
    uint32_t mph, ratio, noise;          /* FM: the modulator's phase, its frequency against the carrier's (16.16); NOIS: its shift register */
    int32_t fmi, fmk, fmk_rate, hold;    /* FM: the index (0..65535 is 4 pi), how much of it is left (Q16) and how fast that falls; NOIS: the held value */
    uint8_t on, mach;
} TV;
int aos_midi_out(unsigned st, unsigned a, unsigned b);
/* MIDI leaves when its sound is heard: a block is made some 17 ms before it reaches the converter (the stream's
 * cushion), so what the block sends waits in a queue until then -- notes beside their samples, the clock even */
static struct { uint32_t due; uint8_t st, a, b; } mq[256];
static uint16_t mq_w, mq_r;
static uint32_t mq_base;                 /* when the block being made will be heard (timer ticks) */
static void mq_put(uint32_t offs, unsigned st, unsigned a, unsigned b)
{
    if ((uint16_t)(mq_w - mq_r) >= 255) { aos_midi_out(mq[mq_r & 255].st, mq[mq_r & 255].a, mq[mq_r & 255].b); mq_r++; }
    mq[mq_w & 255].due = mq_base + offs * 2993u; mq[mq_w & 255].st = (uint8_t)st; mq[mq_w & 255].a = (uint8_t)a; mq[mq_w & 255].b = (uint8_t)b; mq_w++;
}
static void seq_midi_flush(void)
{
    uint32_t t = now();
    while (mq_r != mq_w && (int32_t)(t - mq[mq_r & 255].due) >= 0) { aos_midi_out(mq[mq_r & 255].st, mq[mq_r & 255].a, mq[mq_r & 255].b); mq_r++; }
}
static void mq_now(void) { while (mq_r != mq_w) { aos_midi_out(mq[mq_r & 255].st, mq[mq_r & 255].a, mq[mq_r & 255].b); mq_r++; } }
/* the MIDI notes sounding: each ends by itself */
#define SQ_MNOTES 32
static struct { uint8_t ch, note; uint16_t left; } mnote[SQ_MNOTES];
static const int8_t sq_chord[SQ_CHORDS][4] = {{0, 0, 0, 0}, {0, 12, 0, 0}, {0, 7, 0, 0}, {0, 4, 7, 0}, {0, 3, 7, 0}, {0, 2, 7, 0}, {0, 5, 7, 0}, {0, 4, 7, 11},
    {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 3, 6, 0}, {0, 4, 8, 0}, {0, 3, 7, 14}, {0, 5, 10, 15}, {0, 1, 2, 3}};
static void mnotes_off(void) { mq_now(); for (int q = 0; q < SQ_MNOTES; q++) if (mnote[q].left) { aos_midi_out(0x80u | mnote[q].ch, mnote[q].note, 0); mnote[q].left = 0; } }
static TV tv[SQ_TRACKS][2];
/* PLUK's strings (Karplus-Strong): a line as long as the note's period, fed back through a two-point average; a pluck
 * is a burst of noise written into it -- on a string still ringing it is added, as a second stroke would be */
#define KS_N 4096
static int32_t ks_buf[SQ_TRACKS][KS_N];
static struct { uint32_t w, per; int32_t last, fb; } ks[SQ_TRACKS];   /* where it writes; the period (samples << 8); the average's memory; the feedback (Q15) */              /* [0] the note, [1] the one it cut off, fading for 2 ms */
static struct {
    uint8_t playing, pat, fill, chain_at;
    uint32_t step_len;                   /* samples << 8 */
    int32_t t_next[SQ_TRACKS], g_next;   /* until the track's next step / the pattern's next step, samples << 8 from this block's start */
    uint8_t pos[SQ_TRACKS], now_step[SQ_TRACKS], prev[SQ_TRACKS], g_pos, skip[SQ_TRACKS];
    uint8_t idx[SQ_TRACKS];              /* the step that comes next (pos counts the loop; DIR and ROT say which step that is) */
    int32_t t_loop[SQ_TRACKS];           /* until the track's loop comes round, by the plain clock: WARP bends the steps inside it only */
    int32_t dur[SQ_TRACKS];              /* the coming step's own length (RATE, WARP) */
    uint16_t loops[SQ_TRACKS], slip[SQ_TRACKS];
    int32_t rat_t[SQ_TRACKS], rat_gap[SQ_TRACKS];
    uint32_t rat_mul[SQ_TRACKS];         /* each gap times this (16.16): 65536 even, less a bounce coming to rest, more one letting go */
    uint8_t rat_n[SQ_TRACKS], rat_vel[SQ_TRACKS], rat_rev[SQ_TRACKS], rat_p[SQ_TRACKS][TP_ALL];
    uint8_t lkp[SQ_TRACKS][TP_ALL]; uint32_t lk[SQ_TRACKS][2];   /* the last note's parameters, and which of them its step locked */
    int8_t modv[SQ_TRACKS], rndv[SQ_TRACKS];   /* the modulation now (-127..127); the value RND holds since the track's last note */
    int32_t duck;                        /* Q12 */
    int32_t tgain[SQ_TRACKS];            /* Q12: volume, mute, the modulation */
    uint8_t send[SQ_TRACKS], drive[SQ_TRACKS], bits[SQ_TRACKS], duck_d[SQ_TRACKS];
    int16_t hp_sent[SQ_TRACKS], lp_sent[SQ_TRACKS];
    uint16_t lfo[SQ_TRACKS];
    uint32_t peak[SQ_TRACKS];
    int32_t clk_next;                    /* until the next MIDI clock (24 a quarter note) */
    uint8_t prog_sent[SQ_TRACKS];
    uint32_t ext_at, ext_per; uint32_t ext_n;   /* a clock from outside: when its last tick came, the ticks' spacing (smoothed, timer ticks), how many since the start */
    uint8_t ui_route[SQ_TRACKS];
} sq;
int aos_midi_out(unsigned st, unsigned a, unsigned b);
static void route_write(int s, int on, const uint32_t g[4]);

static void seq_trk2_defaults(int t)
{
    uint8_t *q = SQH->trk2[t];
    for (int k = 0; k < 16; k++) q[k] = 0;
    q[TP_DENS - SQ_TP] = 127; q[TP_RATE - SQ_TP] = 5; q[TP_WARP - SQ_TP] = 64; q[TP_Y - SQ_TP] = 64; q[TP_FMR - SQ_TP] = 16; q[TP_FMI - SQ_TP] = 40; q[TP_FMD - SQ_TP] = 50;
}
void aos_seq_defaults(void)
{
    SqHead *h = SQH;
    volatile uint8_t *b = (volatile uint8_t *)SEQ_BASE;
    for (uint32_t i = 0; i < SEQ_HEAD_SIZE + SEQ_STEPS_SIZE; i++) b[i] = 0;
    h->ver = 2; h->swing = 50; h->duck_rel = 40;
    for (int p = 0; p < SQ_PATTERNS; p++) { h->mlen[p] = 16; for (int t = 0; t < SQ_TRACKS; t++) h->len[p][t] = 16; }
    for (int t = 0; t < SQ_TRACKS; t++) {
        uint8_t *p = h->trk[t];
        p[TP_SMPL] = (uint8_t)(96 + t); p[TP_PTCH] = 64; p[TP_DEC] = 127; p[TP_VOL] = 100; p[TP_LP] = 127; p[TP_PAN] = 64;
        p[TP_PDEC] = 40; p[TP_LFOR] = 20; p[TP_MNOT] = (uint8_t)(36 + t);
        seq_trk2_defaults(t);
    }
    h->magic[1] = 'E'; h->magic[2] = 'S'; h->magic[3] = 'Q'; h->magic[0] = 'A';
}
static int seq_valid(void)
{
    SqHead *h = SQH;
    if (!(h->magic[0] == 'A' && h->magic[1] == 'E' && h->magic[2] == 'S' && h->magic[3] == 'Q')) return 0;
    if (h->ver == 1) { for (int t = 0; t < SQ_TRACKS; t++) seq_trk2_defaults(t); h->ver = 2; }   /* a sequence of before the second block */
    return h->ver == 2;
}

/* ---- a voice into its track ---- */
static inline int32_t sq_sin(uint32_t ph)   /* a sine from a parabola, rounded: -32767..32767 */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x;
    int32_t y = (x * (32768 - ax)) >> 13, ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
#define SQ_ENV() do { \
        if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; } \
        else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; } } while (0)
__attribute__((optimize("O2"))) static void tv_render(TV *v, int32_t *out)
{
    uint32_t step = v->step;
    if (v->pe > 0) { step += mul16(step, (uint32_t)v->pe); v->pe -= (v->pe >> 12) * v->pe_rate + 16; }
    const uint32_t si = step >> 16, sf = step & 0xFFFF;
    int32_t env = v->env, e12 = env >> 12;
    const int32_t g = v->gain;
    int32_t e = (((e12 * e12) >> 12) * g) >> 12;                              /* the envelope squared: a curve, not a line */
    int i = 0;
    if (v->delay) { uint32_t k = v->delay < 32 ? v->delay : 32; v->delay -= k; i = (int)k; }
    if (v->mach == SQM_SUB) {                                                  /* SUB: a sine */
        uint32_t ph = v->ph;
        for (; i < 32; i++) {
            if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            out[i] += ((sq_sin(ph) << 4) * e) >> 8;
            ph += step;
        }
        v->ph = ph; v->env = env;
        return;
    }
    if (v->mach == SQM_FM) {
        /* two sines: the modulator, RATIO times the carrier's frequency, pushes the carrier's phase by up to INDX
         * (4 pi at the top); the index has its own fall (IDEC): a bright stroke that dulls, or a hat that stays bright */
        uint32_t ph = v->ph, mph = v->mph;
        const uint32_t minc = mul16(step, v->ratio);
        const int32_t k = (int32_t)(((uint32_t)v->fmi * ((uint32_t)v->fmk >> 4)) >> 12);   /* 0..65535 */
        if (v->fmk_rate) { v->fmk -= (v->fmk >> 12) * v->fmk_rate + 4; if (v->fmk < 0) v->fmk = 0; }
        for (; i < 32; i++) {
            if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            uint32_t dev = (uint32_t)(sq_sin(mph) * k) << 2;
            out[i] += ((sq_sin(ph + dev) << 4) * e) >> 8;
            ph += step; mph += minc;
        }
        v->ph = ph; v->mph = mph; v->env = env;
        return;
    }
    if (v->mach == SQM_NOIS) {                                                 /* noise, a new value each turn of the phase: PTCH low a rattle, high a hiss */
        uint32_t ph = v->ph, n = v->noise; int32_t h = v->hold;
        const uint32_t ninc = step > 0x04000000u ? 0xFFFFFFFFu : step << 6;
        for (; i < 32; i++) {
            if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            uint32_t p2 = ph + ninc;
            if (p2 < ph || ninc == 0xFFFFFFFFu) { n ^= n << 13; n ^= n >> 17; n ^= n << 5; h = (int32_t)(n & 0xFFFF) - 32768; }
            ph = p2;
            out[i] += ((h << 4) * e) >> 8;
        }
        v->ph = ph; v->noise = n; v->hold = h; v->env = env;
        return;
    }
    if (v->mach == SQM_PLUK) {
        const int t = (int)v->ratio;                                           /* its track (the string is the track's) */
        int32_t *b = ks_buf[t];
        uint32_t w = ks[t].w; int32_t last = ks[t].last; const int32_t fb = ks[t].fb;
        const uint32_t ni = ks[t].per >> 8; const int32_t fr = (int32_t)(ks[t].per & 255);
        for (; i < 32; i++) {
            if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
            int32_t a = b[(w - ni) & (KS_N - 1)], c = b[(w - ni - 1) & (KS_N - 1)];
            int32_t y = a + ((((c - a) >> 4) * fr) >> 4);
            int32_t lp = (y >> 1) + (last >> 1); last = y;
            b[w & (KS_N - 1)] = ((lp >> 8) * fb) >> 7;
            w++;
            out[i] += ((y >> 4) * e) >> 8;
        }
        ks[t].w = w; ks[t].last = last; v->env = env;
        return;
    }
    if (v->mach != SQM_SMPL) { v->on = 0; return; }
    const int32_t *q = v->q; const int32_t st = v->st; const uint32_t n = v->n;
    uint32_t pos = v->pos, frac = v->frac;
    const int plain = !sf && !frac;
    for (; i < 32; i++) {
        if (pos >= n) { v->on = 0; break; }
        const int32_t *f0 = q + (int32_t)pos * st;
        int32_t a = (f0[0] >> 1) + (f0[1] >> 1);
        if (!plain && pos + 1 < n) { int32_t b = (f0[st] >> 1) + (f0[st + 1] >> 1); a += (((b - a) >> 5) * (int32_t)(frac >> 6)) >> 5; }
        if (v->env_inc) { env += v->env_inc; if (env >= 1 << 24) { env = 1 << 24; v->env_inc = 0; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
        else if (v->env_dec) { env -= v->env_dec; if (env <= 0) { v->on = 0; break; } e12 = env >> 12; e = (((e12 * e12) >> 12) * g) >> 12; }
        out[i] += ((a >> 4) * e) >> 8;
        frac += sf; pos += si + (frac >> 16); frac &= 0xFFFF;
    }
    v->pos = pos; v->frac = frac; v->env = env;
}

/* a note on track t: p its parameters (the step's locks already in), vel 1..127, after delay samples */
static void sq_trigger(int t, const uint8_t *p, int vel, int rev, uint32_t delay)
{
    TV *v = &tv[t][0];
    TV nv;
    int mod = sq.modv[t], dest = p[TP_LFOD];
    int semi = clamp_i((int)p[TP_PTCH] - 64 + (dest == 5 ? mod * 24 / 127 : 0), -36, 36);
    nv.mach = p[TP_MACH] < SQM_N ? p[TP_MACH] : 0; nv.ph = 0; nv.q = 0; nv.st = 2; nv.n = 0; nv.mph = 0; nv.hold = 0;
    nv.noise = rnd_u() | 1u; nv.ratio = 65536; nv.fmi = 0; nv.fmk = 65535 << 4; nv.fmk_rate = 0;
    if (nv.mach == SQM_MIDI) { /* no sound of its own */ }
    else if (nv.mach != SQM_SMPL) {
        nv.step = mul16(5356567u, pitch_step(semi));                          /* 55 Hz at the middle, as a phase step */
        if (nv.mach == SQM_PLUK) {
            /* the string: INDX how bright the pluck (its noise through a one-pole), IDEC how long it rings */
            uint32_t per = div_u(0xFFFFFFFFu, (nv.step >> 8) + 1u);              /* samples << 8 */
            if (per < (4u << 8)) per = 4u << 8; else if (per > ((KS_N - 4u) << 8)) per = (KS_N - 4u) << 8;
            ks[t].per = per; ks[t].fb = 32000 + (int)p[TP_FMD] * 6;            /* 0.977 .. 0.9998 a turn */
            uint32_t n = per >> 8, w = ks[t].w, r = rnd_u() | 1u; int32_t z = 0, bright = 2 + (int)p[TP_FMI] * 2;   /* Q8 */
            int ringing = v->on && v->mach == SQM_PLUK;
            if (!ringing) { for (uint32_t k = 0; k < KS_N; k++) ks_buf[t][k] = 0; ks[t].last = 0; }
            for (uint32_t k = 0; k < n; k++) {
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                int32_t x = ((int32_t)(r & 0xFFFF) - 32768) << 7;              /* half of full scale */
                z += ((x - z) * bright) >> 8;
                ks_buf[t][(w - n + k) & (KS_N - 1)] += z;
            }
            nv.ratio = (uint32_t)t;
        }
        if (nv.mach == SQM_FM) {
            nv.ratio = (uint32_t)p[TP_FMR] * 4096u;                           /* in sixteenths: 16 is 1:1, 32 2:1, between them inharmonic */
            nv.fmi = clamp_i(p[TP_FMI] + (dest == 7 ? mod / 2 : 0), 0, 127) * 516;
            int d = p[TP_FMD]; nv.fmk_rate = d >= 127 ? 0 : (int32_t)div_u(4096u, 2u + (uint32_t)(d * d) / 12u); if (d < 127 && nv.fmk_rate < 1) nv.fmk_rate = 1;
        }
    } else {
        const Smp *s = &st_smp[p[TP_SMPL] & 127];
        if (s->status || s->frames < 4) return;
        int strt = clamp_i(p[TP_STRT] + (dest == 6 ? mod / 2 : 0), 0, 127);
        uint32_t n = s->frames, start = (uint32_t)strt * (n >> 7) + (uint32_t)sq.slip[t] * (n >> 10);
        start %= n;
        if (n - start < 2) start = 0;
        nv.n = n - start;
        if (rev) { nv.q = s->d + 2 * (n - 1 - start); nv.st = -2; nv.n = n - start; if (!start) { nv.q = s->d + 2 * (n - 1); nv.n = n; } }
        else nv.q = s->d + 2 * start;
        nv.step = mul16(s->step, pitch_step(semi));
    }
    nv.pos = 0; nv.frac = 0;
    nv.gain = (vel * 4129) >> 7;
    { int atk = p[TP_ATK], dec = p[TP_DEC];
      if (nv.mach != SQM_SMPL && dec >= 127) dec = 126;                        /* a tone of its own would never end */
      nv.env = atk ? 0 : 1 << 24; nv.env_inc = atk ? env_step(atk) : 0; nv.env_dec = dec >= 127 ? 0 : env_step(dec < 2 ? 2 : dec); }
    nv.pe = p[TP_PENV] ? (int32_t)(cents_step((int)p[TP_PENV] * 250) - 65536u) : 0;
    { int d = p[TP_PDEC]; nv.pe_rate = (int32_t)div_u(4096u, 4u + (uint32_t)(d * d) / 24u); if (nv.pe_rate < 1) nv.pe_rate = 1; }
    nv.delay = delay; nv.on = nv.mach != SQM_MIDI;
    if (v->on && v->mach != SQM_PLUK) { tv[t][1] = *v; tv[t][1].env_inc = 0; tv[t][1].env_dec = (1 << 24) / 96; tv[t][1].delay = 0; }
    *v = nv;
    if (SQH->duck_src == (uint8_t)t) sq.duck = 4096;
    if (p[TP_MCH]) {
        /* MIDI: the track's note (and its chord: up to four notes), each with its own end -- a long GATE lets them overlap */
        unsigned ch = (unsigned)((p[TP_MCH] - 1) & 15);
        const int8_t *cv = sq_chord[p[TP_CHRD] < SQ_CHORDS ? p[TP_CHRD] : 0];
        uint32_t gate = ((uint32_t)(sq.dur[t] > 0 ? sq.dur[t] : (int32_t)sq.step_len) >> 13) * (uint32_t)(p[TP_GATE] ? p[TP_GATE] : 4) >> 3;   /* blocks */
        if (gate < 2) gate = 2; else if (gate > 60000u) gate = 60000u;
        for (int k = 0; k < 4; k++) {
            if (k && !cv[k]) break;
            unsigned note = (unsigned)clamp_i((int)p[TP_MNOT] + semi + cv[k], 0, 127);
            int slot = -1;
            for (int q = 0; q < SQ_MNOTES; q++) { if (mnote[q].left && mnote[q].ch == ch && mnote[q].note == note) { slot = q; mq_put(delay, 0x80u | ch, note, 0); break; } if (slot < 0 && !mnote[q].left) slot = q; }
            if (slot < 0) { slot = 0; mq_put(delay, 0x80u | mnote[0].ch, mnote[0].note, 0); }
            mq_put(delay, 0x90u | ch, note, (unsigned)vel);
            mnote[slot].ch = (uint8_t)ch; mnote[slot].note = (uint8_t)note; mnote[slot].left = (uint16_t)gate;
        }
    }
}
static int sq_muted(int t) { const SqHead *h = SQH; return ((h->mutes >> t) & 1) || (h->solo && !((h->solo >> t) & 1)); }
static int sq_cond(int t, int c)
{
    if (!c) return 1;
    if (c <= SQ_COND_AB) { int b = 2, k = c - 1; while (k >= b) { k -= b; b++; } return (int)(sq.loops[t] % (unsigned)b) == k; }
    switch (c) {
    case SQC_FILL: return sq.fill; case SQC_NFILL: return !sq.fill;
    case SQC_FIRST: return sq.loops[t] == 0; case SQC_NFIRST: return sq.loops[t] != 0;
    case SQC_PRE: return sq.prev[t]; case SQC_NPRE: return !sq.prev[t];
    case SQC_NEI: return sq.prev[(t + 7) & 7]; case SQC_NNEI: return !sq.prev[(t + 7) & 7];
    }
    return 1;
}
/* the track's parameters with a step's locks in; what was locked holds (for the filters, the level, the mix) until
 * the track's next note */
extern int16_t aos_mod_tp[SQ_TRACKS][TP_ALL];
static void sq_params(int t, const SqStep *s, uint8_t *p)
{
    const SqHead *h = SQH;
    for (int k = 0; k < SQ_TP; k++) p[k] = h->trk[t][k];
    for (int k = 0; k < 16; k++) p[SQ_TP + k] = h->trk2[t][k];
    sq.lk[t][0] = sq.lk[t][1] = 0;
    if (s) for (int k = 0; k < SQ_LOCKS; k++) if (s->lp[k] && s->lp[k] <= TP_ALL) {
        int id = s->lp[k] - 1; p[id] = s->lv[k]; sq.lk[t][id >> 5] |= 1u << (id & 31);
    }
    for (int k = 0; k < TP_ALL; k++) sq.lkp[t][k] = p[k];
    for (int k = 0; k < TP_ALL; k++) if (aos_mod_tp[t][k]) { int v = p[k] + aos_mod_tp[t][k]; p[k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); }   /* MOD on top */
}
int16_t aos_mod_tp[SQ_TRACKS][TP_ALL], aos_mod_fxk[SQ_TRACKS][6];   /* the MOD page's offsets (the UI's mod.c), 0..127 units */
static int sq_p(int t, int id)
{
    int v = ((sq.lk[t][id >> 5] >> (id & 31)) & 1) ? sq.lkp[t][id] : *sq_tp(t, id);
    if (id < TP_ALL && aos_mod_tp[t][id]) { v += aos_mod_tp[t][id]; v = v < 0 ? 0 : v > 127 ? 127 : v; }
    return v;
}
/* the cellular automaton (after norns' less-concepts): an elementary 1-D automaton, one generation per loop of the
 * track, its cells under the steps. RULE 1..127 is rule 2 x RULE (the odd rules flicker everything on and off);
 * SEED places the first row; MODE: 0 GATE (the cells are the steps), 1 AND (the grid's steps, only where a cell
 * lives), 2 XOR (the cells flip the grid), 3 PTCH (the grid's steps, raised by the living cells around them) */
static uint32_t ca_row[SQ_TRACKS][2];
static uint8_t ca_rule_was[SQ_TRACKS], ca_seed_was[SQ_TRACKS];
static int ca_get(int t, int n) { n &= 63; return (int)(ca_row[t][n >> 5] >> (n & 31)) & 1; }
static void ca_seed(int t)
{
    int sd = SQH->trk2[t][TP_CSD - SQ_TP], len = SQH->len[sq.pat][t]; if (len < 1 || len > 64) len = 16;
    ca_row[t][0] = ca_row[t][1] = 0;
    if (!sd) { int m = len / 2; ca_row[t][m >> 5] |= 1u << (m & 31); }
    else for (int k = 0; k < 7; k++) if ((sd >> k) & 1) { int m = (len / 2 - 3 + k + len) % len; ca_row[t][m >> 5] |= 1u << (m & 31); }
    ca_rule_was[t] = SQH->trk2[t][TP_CAR - SQ_TP]; ca_seed_was[t] = (uint8_t)sd;
}
static void ca_next(int t)
{
    int rule = 2 * SQH->trk2[t][TP_CAR - SQ_TP], len = SQH->len[sq.pat][t]; if (len < 1 || len > 64) len = 16;
    uint32_t nr[2] = {0, 0};
    for (int n = 0; n < len; n++) {
        int v = ca_get(t, (n + len - 1) % len) << 2 | ca_get(t, n) << 1 | ca_get(t, (n + 1) % len);
        if ((rule >> v) & 1) nr[n >> 5] |= 1u << (n & 31);
    }
    if (!nr[0] && !nr[1]) { ca_seed(t); return; }                                /* died out: it starts again */
    ca_row[t][0] = nr[0]; ca_row[t][1] = nr[1];
}
static void ca_tick_loop(int t)
{
    if (!SQH->trk2[t][TP_CAR - SQ_TP]) return;
    if (ca_rule_was[t] != SQH->trk2[t][TP_CAR - SQ_TP] || ca_seed_was[t] != SQH->trk2[t][TP_CSD - SQ_TP]) ca_seed(t); else ca_next(t);
}
int aos_seq_ca(int t, int n)
{
    if (t < 0 || t >= SQ_TRACKS || !SQH->trk2[t][TP_CAR - SQ_TP]) return -1;
    if (ca_rule_was[t] != SQH->trk2[t][TP_CAR - SQ_TP] || ca_seed_was[t] != SQH->trk2[t][TP_CSD - SQ_TP]) ca_seed(t);
    return ca_get(t, n);
}
static void sq_fire(int t, int n, uint32_t delay)
{
    SqStep s = *SQ_STEP(sq.pat, t, n);
    int ca_up = 0;
    if (SQH->trk2[t][TP_CAR - SQ_TP]) {
        int c = aos_seq_ca(t, n), m = SQH->trk2[t][TP_CAM - SQ_TP] & 3;
        if (m == 0) { if (c) s.flags |= SF_ON; else s.flags &= (uint8_t)~SF_ON; }
        else if (m == 1) { if (!c) s.flags &= (uint8_t)~SF_ON; }
        else if (m == 2) { if (c) s.flags ^= SF_ON; }
        else { static const int8_t deg[6] = {0, 3, 5, 7, 10, 12}; int len = SQH->len[sq.pat][t], k = 0; if (len < 1 || len > 64) len = 16; for (int i = -2; i <= 2; i++) k += ca_get(t, (n + i + len) % len); ca_up = deg[k]; }
    }
    if (!(s.flags & SF_ON)) return;
    if (sq.skip[t] == (uint8_t)(n + 1)) { sq.skip[t] = 0; return; }           /* a pad just played and wrote this step */
    { int pr = (s.prob ? s.prob : 100) * (SQH->trk2[t][TP_DENS - SQ_TP] + 1) / 128;   /* DENS thins the whole track */
      if (pr < 100 && (int)(rnd_u() % 100u) >= pr) { sq.prev[t] = 0; return; } }
    int ok = sq_cond(t, s.cond);
    sq.prev[t] = (uint8_t)ok;
    if (!ok || sq_muted(t)) return;
    uint8_t p[TP_ALL];
    sq_params(t, &s, p);
    if (ca_up) p[TP_PTCH] = (uint8_t)clamp_i(p[TP_PTCH] + ca_up, 0, 127);
    sq.rndv[t] = (int8_t)((int)(rnd_u() % 255u) - 127);
    int vel = s.vel ? s.vel : 100, rev = (s.flags & SF_REV) || p[TP_REV];
    sq_trigger(t, p, vel, rev, delay);
    if (s.ratch > 1) {
        /* a ratchet: r hits in the step's time. Its curve bends them: each gap the one before times a ratio, so they
         * crowd together like a ball coming to rest (or spread out), up to hits so close they are a pitch */
        int r = s.ratch > 64 ? 64 : s.ratch, cv = SQ_CURVE(s.flags);
        uint32_t L = (uint32_t)(sq.dur[t] > 0 ? sq.dur[t] : (int32_t)sq.step_len) >> 8;   /* samples */
        uint32_t ratio = 65536u, g0;
        if (cv) {
            ratio = cents_step(-(cv > 0 ? cv : -cv) * (r > 16 ? 600 : r > 8 ? 1200 : 2400));   /* 0.87 .. 0.38 a hit (gentler for many hits) */
            uint32_t rn = 65536u; for (int k = 0; k < r; k++) rn = mul16(rn, ratio);
            g0 = div_u(L * ((65536u - ratio) >> 4), ((65536u - rn) >> 4) + 1u);          /* the first gap of a row that sums to the step */
            if (cv < 0) { for (int k = 1; k < r; k++) g0 = mul16(g0, ratio); ratio = div_u(0xFFFFFFFFu, ratio); }   /* the other way: the smallest first */
        } else g0 = div_u(L, (uint32_t)r);
        if (g0 < 48) g0 = 48;
        sq.rat_n[t] = (uint8_t)(r - 1); sq.rat_gap[t] = (int32_t)(g0 << 8); sq.rat_mul[t] = ratio; sq.rat_t[t] = (int32_t)(delay << 8) + sq.rat_gap[t];
        sq.rat_vel[t] = (uint8_t)vel; sq.rat_rev[t] = (uint8_t)rev;
        for (int k = 0; k < TP_ALL; k++) sq.rat_p[t][k] = p[k];
    } else sq.rat_n[t] = 0;
}
/* which step the track's count c is: its direction, then turned round by ROT */
static int sq_which(int t, int c, int len)
{
    const uint8_t *q = SQH->trk2[t];
    int i = c;
    switch (q[TP_DIR - SQ_TP]) {
    case 1: i = len - 1 - c; break;                                           /* backward */
    case 2: i = (sq.loops[t] & 1) ? len - 1 - c : c; break;                   /* there and back */
    case 3: i = (int)(rnd_u() % (uint32_t)len); break;                        /* any */
    case 4: i = sq.idx[t] + ((rnd_u() & 1) ? 1 : -1); break;                  /* a drunk's walk */
    }
    i += q[TP_ROT - SQ_TP];
    i %= len; if (i < 0) i += len;
    return i;
}
/* the track's clock: the tempo's sixteenth times RATE; inside a loop WARP makes the steps grow or shrink (the first to
 * the last by up to four times either way) while the loop as a whole keeps its length */
static const uint8_t sq_rate_n[SQ_RATES] = {1, 1, 1, 2, 3, 1, 4, 3, 2, 3, 4}, sq_rate_d[SQ_RATES] = {4, 3, 2, 3, 4, 1, 3, 2, 1, 1, 1};
static int32_t sq_track_step(int t)
{
    int r = SQH->trk2[t][TP_RATE - SQ_TP]; if (r >= SQ_RATES) r = 5;
    return (int32_t)div_u(sq.step_len * sq_rate_d[r], sq_rate_n[r]);
}
/* the clock's curve (after norns' clockabout): over each loop of a track the steps speed up or slow down along a
 * shape, and the loop still takes its plain time, so step 1 always lands on time. A step's weight in Q12 */
static int32_t sq_curve_w(int k, int len)
{
    const SqHead *h = SQH; int a = h->camt, sh = h->curv;
    if (!sh || !a || len < 2) return 4096;
    int x = (2 * k + 1) * 4096 / (2 * len);                                      /* 0..4096 across the loop */
    int g;
    switch (sh) {
    case 1: g = 4096 - 2 * x; break;                                             /* ACCEL: long steps first, short at the end */
    case 2: g = 2 * x - 4096; break;                                             /* DECEL */
    case 3: { int d = 2 * x - 4096; g = 2 * (d < 0 ? -d : d) - 4096; } break;   /* ARCH: slow at both ends, fast in the middle */
    case 4: { int d = 2 * x - 4096; g = 4096 - 2 * (d < 0 ? -d : d); } break;   /* BULGE: fast ends, slow middle */
    case 5: { int tt = x * 4 % 16384; int tri = tt < 8192 ? tt - 4096 : 12288 - tt; g = tri; } break;   /* WAVE: two push-pulls a loop */
    default: { uint32_t r = (uint32_t)(k + 1) * 2654435761u ^ (uint32_t)h->cseed * 40503u; r ^= r >> 15; r *= 2246822519u; r ^= r >> 13; g = (int)(r % 8193u) - 4096; } break;   /* RAND: a fixed random set (cseed) */
    }
    int w = 4096 + g * a / 141;                                                  /* amount 127: about +-90 % */
    return w < 400 ? 400 : w;
}
static int32_t sq_step_dur(int t, int c, int len, int32_t sl)
{
    if (SQH->curv && SQH->camt && len >= 2) {
        int32_t sum = 0; for (int k = 0; k < len; k++) sum += sq_curve_w(k, len);
        uint32_t avg = (uint32_t)(sum / len), ratio = div_u((uint32_t)sq_curve_w(c, len) << 16, avg ? avg : 1u);   /* this step's share, Q16 */
        sl = (int32_t)mul16((uint32_t)sl, ratio);
    }
    int w = (int)SQH->trk2[t][TP_WARP - SQ_TP] - 64;
    if (!w || len < 2) return sl;
    uint32_t ratio = cents_step(w * 375 / (len - 1));                         /* 2^(2 w / 64) from the first step to the last */
    uint32_t cur = 65536u, sum = 0, at = 65536u;
    for (int k = 0; k < len; k++) { if (k == c) at = cur; sum += cur >> 4; cur = mul16(cur, ratio); }
    uint32_t avg = div_u(sum, (uint32_t)len) >> 4;                            /* the mean, Q8 */
    return (int32_t)(div_u(mul16((uint32_t)sl, at), avg ? avg : 1u) << 8);
}
/* ---- resampling: the main output, as it is heard, into a sample slot (a bank of its own: 6.1, about 11 s) ---- */
static struct { uint8_t state; int16_t slot; uint32_t skip, left; Smp *s; } rs;   /* state 1 armed, 2 recording */
void aos_resample(int slot, int steps)
{
    if (slot < 0 || slot >= 128 || !seq_mode || !sq.step_len) return;
    if (rs.state) { rs.state = 0; aos_tap_want(8, 0); return; }                 /* again: give it up */
    rs.slot = (int16_t)slot; rs.left = (sq.step_len >> 8) * (uint32_t)(steps < 1 ? 16 : steps);
    if (rs.left > SZ_BANK / 8 - 64) rs.left = SZ_BANK / 8 - 64;
    rs.state = 1;
    if (!sq.playing) aos_seq_play(1);
}
int aos_resampling(void) { return rs.state; }
static void resample_begin(void)
{
    Smp *s = &st_smp[rs.slot];
    voices_stop(s);
    for (int t = 0; t < SQ_TRACKS; t++) for (int k = 0; k < 2; k++) if (tv[t][k].on && tv[t][k].mach == SQM_SMPL && tv[t][k].q >= s->d && s->frames && tv[t][k].q < s->d + 2 * s->frames) tv[t][k].on = 0;
    s->d = (int32_t *)(POOL_STARS + SZ_BANK * (uint32_t)(rs.slot / 16)); s->frames = 0; s->step = 0x10000; s->status = 2;
    bank_used[rs.slot / 16] = SZ_BANK;                                           /* the bank is this one recording now */
    rs.s = s; rs.skip = seq_ahead_now + 110;                                     /* what is on its way to the converter and back is not the bar yet */
    rs.state = 2; aos_tap_want(8, 1);
}
static void resample_frame(int32_t l, int32_t r)
{
    if (rs.state != 2) return;
    if (rs.skip) { rs.skip--; return; }
    Smp *s = rs.s;
    s->d[2 * s->frames] = l; s->d[2 * s->frames + 1] = r; s->frames++;
    if (!--rs.left) { s->status = 0; rs.state = 0; aos_tap_want(8, 0); }
}
static void fz_in(int ch, int32_t v);
/* one block of 32 samples: the clock, then every track's voices, its drive, level and sends */
__attribute__((optimize("O2"))) static void seq_block(void)
{
    const SqHead *h = SQH;
    const int32_t B = 32 << 8;
    mq_base = now() + seq_ahead_now * 2993u + US(1500);
    if (sq.playing && sq.step_len) {
        int32_t sl = (int32_t)sq.step_len;
        while (sq.g_next < B) {
            if (++sq.g_pos >= (h->mlen[sq.pat] ? h->mlen[sq.pat] : 16)) {
                sq.g_pos = 0;
                if (rs.state == 1) resample_begin();
                int want = h->pattern & 3;
                if (h->chain_n) { sq.chain_at = (uint8_t)((sq.chain_at + 1) % (h->chain_n > 8 ? 8 : h->chain_n)); want = h->chain[sq.chain_at] & 3; SQH->pattern = (uint8_t)want; }
                if (want != sq.pat) {
                    sq.pat = (uint8_t)want;
                    if (h->pcch) mq_put(0, 0xC0u | (unsigned)((h->pcch - 1) & 15), (unsigned)want, 0);
                    for (int t = 0; t < SQ_TRACKS; t++) if (!h->trk[t][TP_FREE]) {
                        int len = h->len[sq.pat][t]; if (len < 1 || len > SQ_STEPS) len = 16;
                        int32_t ts = sq_track_step(t);
                        sq.pos[t] = 0; sq.loops[t] = 0; sq.t_next[t] = sq.g_next; sq.rat_n[t] = 0; sq.idx[t] = 0; sq.idx[t] = (uint8_t)sq_which(t, 0, len);
                        sq.t_loop[t] = sq.g_next + ts * len; sq.dur[t] = sq_step_dur(t, 0, len, ts);
                    }
                }
            }
            sq.g_next += sl;
        }
        sq.g_next -= B;
        for (int t = 0; t < SQ_TRACKS; t++) {
            int len = h->len[sq.pat][t]; if (len < 1 || len > SQ_STEPS) len = 16;
            int sw = h->trk[t][TP_SHUF] ? 50 + h->trk[t][TP_SHUF] * 30 / 127 : h->swing;
            const int32_t ts = sq_track_step(t);
            for (int guard = 0; guard < 8; guard++) {
                int c = sq.pos[t]; if (c >= len) c = 0;
                int n = sq.idx[t]; if (n >= len) n = 0;
                const SqStep *s = SQ_STEP(sq.pat, t, n);
                int32_t off = 0;
                if ((c & 1) && sw > 50) off = (ts / 50) * (sw - 50);
                if (s->flags & SF_ON) off += (ts / 48) * s->micro;
                int32_t at = sq.t_next[t] + off;
                if (at >= B) break;
                if (at < 0) at = 0;
                sq.now_step[t] = (uint8_t)n;
                if (sq.dur[t] <= 0) sq.dur[t] = ts;
                sq_fire(t, n, (uint32_t)at >> 8);
                sq.t_next[t] += sq.dur[t];
                if (++c >= len) {
                    c = 0; sq.loops[t]++; ca_tick_loop(t); sq.slip[t] = (uint16_t)(sq.slip[t] + h->trk[t][TP_SLIP]); if (!h->trk[t][TP_SLIP]) sq.slip[t] = 0;
                    sq.t_next[t] = sq.t_loop[t]; sq.t_loop[t] += ts * len;       /* the loop keeps the plain clock's time, whatever WARP did inside it */
                }
                sq.pos[t] = (uint8_t)c;
                sq.idx[t] = (uint8_t)sq_which(t, c, len);
                sq.dur[t] = sq_step_dur(t, c, len, ts);
            }
            sq.t_next[t] -= B; sq.t_loop[t] -= B;
            while (sq.rat_n[t] && sq.rat_t[t] < B) {
                if (h->trk[t][TP_SLIP]) sq.slip[t] = (uint16_t)(sq.slip[t] + h->trk[t][TP_SLIP]);   /* the loop point walks while the roll runs */
                sq_trigger(t, sq.rat_p[t], sq.rat_vel[t], sq.rat_rev[t], (uint32_t)(sq.rat_t[t] < 0 ? 0 : sq.rat_t[t]) >> 8);
                sq.rat_gap[t] = (int32_t)mul16((uint32_t)sq.rat_gap[t], sq.rat_mul[t]); if (sq.rat_gap[t] < (48 << 8)) sq.rat_gap[t] = 48 << 8;
                sq.rat_t[t] += sq.rat_gap[t]; sq.rat_n[t]--;
                if (sq.rat_t[t] < B) sq.rat_t[t] = B;                            /* at most one a block (a voice has one start) */
            }
            if (sq.rat_n[t]) sq.rat_t[t] -= B;
        }
    }
    for (int q = 0; q < SQ_MNOTES; q++) if (mnote[q].left && !--mnote[q].left) mq_put(0, 0x80u | mnote[q].ch, mnote[q].note, 0);
    /* the clock out: 24 to the quarter note, six a step, always (so an echo elsewhere keeps time while this is stopped) */
    if ((h->sync & 1) && sq.step_len) {
        int32_t ck = (int32_t)(sq.step_len / 6u);
        while (sq.clk_next < B) { mq_put((uint32_t)(sq.clk_next < 0 ? 0 : sq.clk_next) >> 8, 0xF8, 0, 0); sq.clk_next += ck; }
        sq.clk_next -= B;
    }
    if (sq.duck > 0) sq.duck -= ((sq.duck * (4 + (128 - (h->duck_rel > 127 ? 127 : h->duck_rel)))) >> 12) + 1;
    if (sq.duck < 0) sq.duck = 0;
    int32_t dly[32]; int any_send = 0;
    for (int i = 0; i < 32; i++) dly[i] = 0;
    for (int t = 0; t < SQ_TRACKS; t++) {
        int32_t *o = trk_buf[t];
        for (int i = 0; i < 32; i++) o[i] = 0;
        if (!tv[t][0].on && !tv[t][1].on) continue;
        if (tv[t][0].on) tv_render(&tv[t][0], o);
        if (tv[t][1].on) tv_render(&tv[t][1], o);
        int32_t lvl = sq.tgain[t];
        if (sq.duck_d[t] && sq.duck) lvl -= (((lvl * sq.duck_d[t]) >> 7) * sq.duck) >> 12;
        if (lvl < 0) lvl = 0;
        const int drv = sq.drive[t], send = sq.send[t];
        const int32_t mask = sq.bits[t] ? ~((1 << (8 + (sq.bits[t] > 14 ? 14 : sq.bits[t]))) - 1) : -1;
        uint32_t pk = 0;
        for (int i = 0; i < 32; i++) {
            int32_t x = o[i];
            if (drv) {
                x = ((x >> 3) * (16 + drv)) >> 1;
                if (x > 0x400000) { x = 0x400000 + ((x - 0x400000) >> 3); if (x > 0x7FFFFF) x = 0x7FFFFF; }
                else if (x < -0x400000) { x = -0x400000 + ((x + 0x400000) >> 3); if (x < -0x7FFFFF) x = -0x7FFFFF; }
            }
            x &= mask;
            x = ((x >> 4) * lvl) >> 8;
            if (x > 0x7FFFFF) x = 0x7FFFFF; else if (x < -0x7FFFFF) x = -0x7FFFFF;
            o[i] = x;
            uint32_t ax = (uint32_t)(x < 0 ? -x : x); if (ax > pk) pk = ax;
            if (send) dly[i] += (x >> 7) * send;
        }
        if (send) any_send = 1;
        if (pk > sq.peak[t]) sq.peak[t] = pk;
    }
    if (fz.on) for (int i = 0; i < 32; i++) { int32_t x = any_send ? dly[i] : 0; if (x > 0x7FFFFF) x = 0x7FFFFF; else if (x < -0x7FFFFF) x = -0x7FFFFF; fz_in(0, x); fz_in(1, x); }
}

/* ---- each UI tick: the tempo, what the DSP holds for each track (filters, place), the levels ---- */
static void seq_filter(int t, int hp, int lp)
{
    uint32_t blk = 0x8900u + 32u * (uint32_t)t;
    if (hp != sq.hp_sent[t]) {
        if (hp > 0) { if (sq.hp_sent[t] <= 0) for (int k = 0; k < 4; k++) dsp_write(blk + 0x10u + (uint32_t)k, 0, 0); for (int k = 0; k < 5; k++) dsp_write(blk + (uint32_t)k, sq_hp[hp][k], 0); }
        dsp_write(blk + 0xAu, hp > 0, 0);
        sq.hp_sent[t] = (int16_t)hp;
    }
    if (lp != sq.lp_sent[t]) {
        if (lp < 127) { if (sq.lp_sent[t] < 0 || sq.lp_sent[t] >= 127) for (int k = 0; k < 4; k++) dsp_write(blk + 0x14u + (uint32_t)k, 0, 0); for (int k = 0; k < 5; k++) dsp_write(blk + 5u + (uint32_t)k, sq_lp[lp][k], 0); }
        dsp_write(blk + 0xBu, lp < 127, 0);
        sq.lp_sent[t] = (int16_t)lp;
    }
}
static const uint16_t sq_pan_tab[17] = {0, 3212, 6393, 9512, 12540, 15447, 18205, 20788, 23170, 25330, 27246, 28899, 30274, 31357, 32138, 32610, 32768};   /* sin, a quarter turn */
static void seq_tick(void)
{
    if (!seq_mode) return;
    if (!seq_valid()) aos_seq_defaults();
    const SqHead *h = SQH;
    for (int t = 0; t < SQ_TRACKS; t++) {
        const uint8_t *p = h->trk[t];
        /* the modulation: a slow triangle (about 30 s .. 8 a second), a new chance value at each of the track's notes,
         * or the ducking track's envelope (its stroke opens this track's filter, say) */
        sq.lfo[t] = (uint16_t)(sq.lfo[t] + 2 + (uint32_t)p[TP_LFOR] * p[TP_LFOR] / 2u);
        int tri = sq.lfo[t] < 32768 ? (int)sq.lfo[t] - 16384 : 49152 - (int)sq.lfo[t];   /* -16384..16384 */
        int wave = h->trk2[t][TP_MODW - SQ_TP];
        int src = wave == 1 ? sq.rndv[t] : wave == 2 ? (int)(sq.duck * 127 / 4096) : tri * 127 / 16384;
        int mod = p[TP_LFOD] ? src * p[TP_LFOA] / 127 : 0;                               /* -127..127 */
        sq.modv[t] = (int8_t)clamp_i(mod, -127, 127);
        int hp = sq_p(t, TP_HP), lp = sq_p(t, TP_LP), vol = sq_p(t, TP_VOL), pan = sq_p(t, TP_PAN), dl = sq_p(t, TP_DLY);
        if (p[TP_LFOD] == 1) lp += mod / 2; else if (p[TP_LFOD] == 2) hp += mod / 2; else if (p[TP_LFOD] == 3) vol += mod / 2; else if (p[TP_LFOD] == 4) pan += mod / 2;
        seq_filter(t, clamp_i(hp, 0, 127), clamp_i(lp, 0, 127));
        vol = clamp_i(vol, 0, 127); pan = clamp_i(pan, 0, 127);
        sq.tgain[t] = sq_muted(t) ? 0 : (vol * vol * 65) >> 8;                 /* a square law: 100 is about -4 dB, 127 full */
        sq.send[t] = (uint8_t)clamp_i(dl, 0, 127); sq.drive[t] = 0; sq.bits[t] = 0;   /* no drive and no bit reduction (12 Nov 2026: the owner wants no distortion) */ sq.duck_d[t] = (uint8_t)(h->duck_src == t ? 0 : sq_p(t, TP_DUCK) & 127);
        if (!sq.ui_route[t]) {
            int i = pan >> 3, f = pan & 7;                                    /* equal power: 64 is 0.71 each side */
            uint32_t r = (uint32_t)(sq_pan_tab[i] + (sq_pan_tab[i + 1 > 16 ? 16 : i + 1] - sq_pan_tab[i]) * f / 8);
            uint32_t l = (uint32_t)(sq_pan_tab[16 - i] + (sq_pan_tab[16 - i - 1 < 0 ? 0 : 16 - i - 1] - sq_pan_tab[16 - i]) * f / 8);
            uint32_t gl = l * 255u > 0x7FFFFFu ? 0x7FFFFFu : l * 255u, gr = r * 255u > 0x7FFFFFu ? 0x7FFFFFu : r * 255u;
            int out = sq_p(t, TP_OUT);                                              /* the main pair, the cue pair, or both */
            uint32_t g[4] = {out == 1 ? 0 : gl, out == 1 ? 0 : gr, out ? gl : 0, out ? gr : 0};
            if (out == 3) {                                                   /* QUAD: Y places it between the front pair and the back */
                int y = sq_p(t, TP_Y); if (!y && !h->trk2[t][TP_Y - SQ_TP]) y = 64;
                if (p[TP_LFOD] == 8) y = clamp_i(y + mod / 2, 0, 127);
                int yi = y >> 3, yf = y & 7;
                uint32_t bk = (uint32_t)(sq_pan_tab[yi] + (sq_pan_tab[yi + 1 > 16 ? 16 : yi + 1] - sq_pan_tab[yi]) * yf / 8);
                uint32_t fr = (uint32_t)(sq_pan_tab[16 - yi] + (sq_pan_tab[16 - yi - 1 < 0 ? 0 : 16 - yi - 1] - sq_pan_tab[16 - yi]) * yf / 8);
                g[0] = (gl >> 15) * fr; g[1] = (gr >> 15) * fr; g[2] = (gl >> 15) * bk; g[3] = (gr >> 15) * bk;
            }
            route_write(t, 1, g);
        }
        sq.peak[t] -= sq.peak[t] / 8;
        { int pr = h->trk2[t][TP_PROG - SQ_TP], mc = p[TP_MCH];                 /* the track's program, when it is set or changed */
          if (pr && mc && sq.prog_sent[t] != pr) { sq.prog_sent[t] = (uint8_t)pr; aos_midi_out(0xC0u | (unsigned)((mc - 1) & 15), (unsigned)(pr - 1), 0); } }
    }
}
int aos_seq_ext(void) { return sq.ext_per && now() - sq.ext_at < MS(500); }
int aos_seq_bpm10(void) { return sq.step_len ? (int)div_u(1693440000u, sq.step_len) : 1200; }
void aos_seq_tempo(int bpm10)
{
    if (aos_seq_ext()) return;                                                 /* a clock from outside has the tempo */
    bpm10 = bpm10 < 300 ? 300 : bpm10 > 3000 ? 3000 : bpm10;
    sq.step_len = div_u(1693440000u, (uint32_t)bpm10);                         /* 44,100 x 60 / 4 x 256 x 10 */
}
void aos_seq_mode(int on)
{
    on = on != 0;
    if (on && !seq_valid()) aos_seq_defaults();
    if (on == seq_mode) return;
    for (int t = 0; t < SQ_TRACKS; t++) {
        sq.hp_sent[t] = sq.lp_sent[t] = -2; sq.ui_route[t] = 0; sq.prog_sent[t] = 0;
        sq.lk[t][0] = sq.lk[t][1] = 0;
        tv[t][0].on = tv[t][1].on = 0;
        dsp_write(0x280u + 8u * (uint32_t)t + 7u, 0x220u + 9u * (uint32_t)t, 0);   /* every track has its routing block, slot or none */
        if (on) { dsp_write(0x8900u + 32u * (uint32_t)t + 0xAu, 0, 0); dsp_write(0x8900u + 32u * (uint32_t)t + 0xBu, 0, 0); }
        else { static const uint32_t g0[4] = {0, 0, 0, 0}; route_write(t, 0, g0); }
    }
    mnotes_off();
    if (!on) sq.playing = 0;
    if (!sq.step_len) aos_seq_tempo(1200);
    seq_mode = on;
    SQH->mode = (uint8_t)on;
}
int aos_seq_is(void) { return seq_mode; }
void aos_seq_play(int on)
{
    if (on == 2 && seq_valid() && sq.step_len) {                               /* on from where it stopped (MIDI's Continue) */
        if (!sq.playing) { mq_base = now() + seq_ahead_now * 2993u + US(1000); if (SQH->sync & 1) mq_put(0, 0xFB, 0, 0); sq.playing = 1; }
        return;
    }
    if (on) {
        if (!seq_valid()) aos_seq_defaults();
        if (SQH->chain_n) SQH->pattern = SQH->chain[0] & 3;
        sq.pat = SQH->pattern & 3;
        for (int t = 0; t < SQ_TRACKS; t++) {
            int len = SQH->len[sq.pat][t]; if (len < 1 || len > SQ_STEPS) len = 16;
            int32_t ts = sq_track_step(t);
            if (SQH->trk2[t][TP_CAR - SQ_TP]) ca_seed(t);
            sq.pos[t] = 0; sq.t_next[t] = 0; sq.loops[t] = 0; sq.prev[t] = 0; sq.rat_n[t] = 0; sq.slip[t] = 0; sq.skip[t] = 0; sq.now_step[t] = 0;
            sq.idx[t] = 0; sq.idx[t] = (uint8_t)sq_which(t, 0, len); sq.t_loop[t] = ts * len; sq.dur[t] = sq_step_dur(t, 0, len, ts);
        }
        sq.g_pos = 0; sq.g_next = (int32_t)sq.step_len; sq.chain_at = 0; sq.clk_next = 0;
        mq_base = now() + seq_ahead_now * 2993u + US(1000);                   /* just before the first step is heard */
        if (SQH->pcch) mq_put(0, 0xC0u | (unsigned)((SQH->pcch - 1) & 15), sq.pat, 0);
        if (SQH->sync & 1) mq_put(0, 0xFA, 0, 0);                             /* start */
        for (int t = 0; t < SQ_TRACKS; t++) sq.prog_sent[t] = 0;
        sq.playing = 1;
    } else {
        if (sq.playing && (SQH->sync & 1)) aos_midi_out(0xFC, 0, 0);          /* stop */
        sq.playing = 0;
        for (int t = 0; t < SQ_TRACKS; t++) sq.rat_n[t] = 0;
        mnotes_off();
    }
}
int aos_seq_playing(void) { return sq.playing; }
int aos_seq_step(int t) { return sq.now_step[t & 7]; }
int aos_seq_bar(void) { return sq.g_pos; }
int aos_seq_pat(void) { return sq.pat; }
void aos_seq_fill(int on) { sq.fill = (uint8_t)(on != 0); }
int aos_seq_peak(int t)
{
    uint32_t v = sq.peak[t & 7]; int lv = 0;
    while (v > 0x3FF && lv < 16) { v = v * 2 / 3; lv++; }
    return lv;
}
void aos_seq_mod(int t, int which, int v) { (void)t; (void)which; (void)v; }
/* a pad. Recording while it plays: the hit goes to the nearest step, with how early or late it was */
static void seq_hit(int t, int vel, int rec, int note);
void aos_seq_hit(int t, int vel, int rec) { seq_hit(t, vel, rec, -1); }
static uint8_t seq_live;
void aos_seq_live(int on) { seq_live = (uint8_t)on; }
static void seq_hit(int t, int vel, int rec, int note)      /* note: -1 a pad, else a MIDI note (60 the track's own pitch) */
{
    if (!seq_mode || t < 0 || t >= SQ_TRACKS || !seq_valid()) return;
    uint8_t p[TP_ALL];
    sq_params(t, 0, p);
    if (note >= 0) p[TP_PTCH] = (uint8_t)clamp_i((int)p[TP_PTCH] + note - 60, 28, 100);
    mq_base = now() + seq_ahead * 2993u;                                        /* a pad: heard, and sent, a cushion from now */
    if (!sq_muted(t)) sq_trigger(t, p, vel < 1 ? 100 : vel, p[TP_REV], 0);
    if (rec && sq.playing && sq.step_len) {
        int len = SQH->len[sq.pat][t]; if (len < 1 || len > SQ_STEPS) len = 16;
        /* what was heard when the pad was hit left here a moment ago (the stream's way to the converter): the hit
         * belongs that much earlier */
        int32_t sl = sq_track_step(t), tn = sq.t_next[t] + (int32_t)(((uint32_t)(uint16_t)(stream_written - stream_taken) + 96u) << 8);
        int n = sq.idx[t], micro, back = 0;
        if (tn < 0) tn = 0;
        while (tn >= sl && back < 8) { tn -= sl; n = (n + len - 1) % len; back++; }
        if (tn < sl / 2) { micro = -(int)div_u((uint32_t)tn * 48u, (uint32_t)sl); if (!back) sq.skip[t] = (uint8_t)(n + 1); }
        else { n = (n + len - 1) % len; micro = (int)div_u((uint32_t)(sl - tn) * 48u, (uint32_t)sl); }
        SqStep *s = SQ_STEP(sq.pat, t, n);
        s->flags |= SF_ON; s->vel = (uint8_t)(vel < 1 ? 100 : vel); s->micro = (int8_t)clamp_i(micro, -23, 23);
        if (note >= 0) {                                                        /* its pitch: a lock on the step */
            int k = 0;
            while (k < SQ_LOCKS && s->lp[k] && s->lp[k] != TP_PTCH + 1) k++;
            if (k < SQ_LOCKS) { s->lp[k] = TP_PTCH + 1; s->lv[k] = p[TP_PTCH]; }
        }
    }
}
/* a note from MIDI IN: channels 1..8 are tracks 1..8 (never a MIDI track: with a cable from OUT to IN it would play itself) */
static void seq_midi_note(unsigned ch, unsigned note, unsigned vel)
{
    if (!seq_mode || !seq_valid() || SQH->nin || ch >= SQ_TRACKS || !vel) return;
    if (SQH->trk[ch][TP_MACH] == SQM_MIDI) return;
    seq_hit((int)ch, (int)vel, seq_live, (int)note);
}
/* the UI's long work (a project copied, the card read): more ahead meanwhile, and a breath for the stream inside loops */
void aos_seq_cushion(int big) { seq_ahead = big ? 1280u : 768u; }
static void dsp_poll(void);
void aos_breathe(void) { if (dsp_running) { dsp_poll(); stream_pump(); } }
/* ---- MIDI from outside (midi.c hands these over): the clock, start / continue / stop, a program change ---- */
static void seq_midi_rt(unsigned c)
{
    if (!seq_mode || !seq_valid() || !(SQH->sync & 2)) return;
    if (c == 0xFA) { sq.ext_n = 0; aos_seq_play(1); return; }
    if (c == 0xFB) { if (!sq.playing) aos_seq_play(2); return; }
    if (c == 0xFC) { aos_seq_play(0); return; }
    if (c != 0xF8) return;
    uint32_t t = now(), dt = t - sq.ext_at;
    sq.ext_at = t;
    if (dt > MS(500)) { sq.ext_per = 0; sq.ext_n = 0; return; }                  /* the first after a silence: nothing to measure yet */
    sq.ext_per = sq.ext_per ? sq.ext_per + ((int32_t)(dt - sq.ext_per) >> 3) : dt;
    /* six ticks a step: timer ticks -> samples << 8 (132 ticks a microsecond; 44.1 samples a millisecond) */
    { uint32_t sl = div_u(sq.ext_per * 6u, 2993u) << 8; sl += div_u((sq.ext_per * 6u) % 2993u << 8, 2993u); if (sl > (200u << 8) && sl < (60000u << 8)) sq.step_len = sl; }
    /* every sixth tick is a step's start there: lean a little toward it, so the two never drift apart */
    if (sq.playing && sq.ext_n % 6u == 0 && sq.step_len) {
        int32_t sl = (int32_t)sq.step_len, into = sl - sq.g_next, err = into > sl / 2 ? into - sl : into;
        int32_t sh = err / 8;
        sq.g_next += sh; for (int k = 0; k < SQ_TRACKS; k++) { sq.t_next[k] += sh; sq.t_loop[k] += sh; }
    }
    sq.ext_n++;
}
static void seq_midi_pc(unsigned ch, unsigned prog)
{
    if (!seq_mode || !seq_valid()) return;
    if (SQH->pcch && ch == (unsigned)((SQH->pcch - 1) & 15) && prog < SQ_PATTERNS) { SQH->pattern = (uint8_t)prog; SQH->chain_n = 0; }
}
