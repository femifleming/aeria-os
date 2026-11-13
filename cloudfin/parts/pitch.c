
/* ---- PITCH: the input sings, AERIA plays it over MIDI ----------------------------------------------
 * CAPTURE. A stock-code site at the frame interrupt's tail, 0x4000d99a (6 bytes, stock 23c1 8000 4800 =
 * move.l %d1,0x80004800, SHA-256 9a7d1001...5f707b91; the site TUNER uses). build_aeria.py places
 * `jmp aeria_frame_stub` there. The stub replays that store, calls aeria_frame() and jumps to 0x4000d9a0,
 * the next stock instruction (the base's own jmp to its audio shim on a USB base). The interrupt restores
 * d0-d7/a0-a6 from its stack at 0x4000d9a6, so nothing the stub or C clobbers survives it. aeria_frame
 * makes no calls, no EMAC, at most ~100 instructions: with the capture off it is a load and a branch.
 * It reads the input page the frame interrupt just made (eight 0x100-byte pages at 0x80005660, index
 * 0x46104d00; C D at +0x00 + 8s, A B at +0x80 + 8s; 24-bit words, left-justified) and decimates its 16
 * samples by 4 through an 8-tap boxcar (zeros at 5.5, 11 and 16.5 kHz) into an 11,025 Hz ring.
 * Without the site (a plain build.py build) nothing fills the ring and the page says so.
 *
 * DETECTOR, in pitch_tick, about PT_BUDGET products a tick: the newest 512 samples less their mean,
 * level gate, scaled to a 2,000 peak; McLeod's normalised square difference n(t) = 2r / (m0 + mt) on a
 * [1 2 1]-decimated copy (5,512 Hz, W 128, lags 2..110 = 50 Hz..2.7 kHz) with key-maxima picking (first
 * key maximum within 0.86 of the best), stopping at 2.25x the first strong one; then at 11,025 Hz (W 256)
 * a hill climb round twice the coarse lag and again round the largest multiple of it under 224 lags,
 * each with parabolic interpolation: the period is measured over several cycles. Integer only. */

#define PT_RING 1024u
#define PT_N 512
#define PT_W 256
#define PT_CN 256
#define PT_CW 128
#define PT_CMAX 110
#define PT_LONG 224
#define PT_BUDGET 5000
#define PT_HIST 128

static volatile int16_t pt_ring[PT_RING];
static volatile uint32_t pt_wr, pt_frames;
static volatile int32_t pt_carry;
static volatile uint8_t pt_cap, pt_src;

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: the inputs come from the DSP's tap (sampler.c rec_word -> usb.c usb_tap), a sample at a time, A B C D
 * as 24-bit values; four make one 11,025 Hz sample, as the frame capture below does */
void aos_tap_want(int who, int on);
static int32_t pt_acc; static int pt_n4, pt_e4;
void aos_pitch_sample(int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (!pt_cap) return;
    unsigned src = pt_src;
    int32_t v = src == 0 ? ((a >> 9) + (b >> 9)) : src == 1 ? a >> 8 : src == 2 ? b >> 8 : src == 3 ? ((c >> 9) + (d >> 9)) : src == 4 ? c >> 8 : d >> 8;
    pt_acc += v;
    if (++pt_n4 < 4) return;
    pt_ring[pt_wr & (PT_RING - 1)] = (int16_t)((pt_carry + pt_acc) >> 3);
    pt_carry = pt_acc; pt_acc = 0; pt_n4 = 0; pt_wr = pt_wr + 1;
    if (++pt_e4 == 4) { pt_e4 = 0; pt_frames = pt_frames + 1; }
}
#endif
#ifdef PREVIEW
int32_t preview_inring[8][64];
uint32_t preview_inidx;
#define PT_IN_IDX preview_inidx
#define PT_IN_PAGE(i) ((const volatile int32_t *)preview_inring[(i) & 7u])
#else
#define PT_IN_IDX (*(volatile uint32_t *)0x46104d00u)
#define PT_IN_PAGE(i) ((const volatile int32_t *)(0x80005660u + (((i) & 7u) << 8)))
/* jmp here from 0x4000d99a: the displaced store, the capture, the next stock instruction */
__asm__(
    "	.text\n"
    "	.even\n"
    "	.globl	aeria_frame_stub\n"
    "aeria_frame_stub:\n"
    "	move.l	%d1,0x80004800\n"
    "	jsr	aeria_frame\n"
    "	jmp	0x4000d9a0\n");
#endif

/* the frame interrupt (IPL 5): source 0 A+B, 1 A, 2 B, 3 C+D, 4 C, 5 D */
__attribute__((used)) void aeria_frame(void)
{
    if (!pt_cap) return;
    unsigned src = pt_src, s = src >= 3 ? src - 3 : src;
    const volatile int32_t *p = PT_IN_PAGE(PT_IN_IDX) + (src >= 3 ? 0 : 32) + (s == 2 ? 1 : 0);
    int32_t carry = pt_carry;
    uint32_t w = pt_wr;
    for (int k = 0; k < 4; k++) {
        int32_t b = 0;
        for (int j = 0; j < 4; j++, p += 2) b += s ? p[0] >> 16 : (p[0] >> 17) + (p[1] >> 17);
        pt_ring[w++ & (PT_RING - 1)] = (int16_t)((carry + b) >> 3);
        carry = b;
    }
    pt_carry = carry;
    pt_wr = w;
    pt_frames++;
}

/* 11,025 x 256 / f(n), n = 24..108: a note's period in Q8 samples */
static const int32_t pt_per[85] = {
    86303, 81460, 76888, 72572, 68499, 64655, 61026, 57601, 54368, 51316, 48436, 45718, 43152, 40730, 38444, 36286,
    34250, 32327, 30513, 28800, 27184, 25658, 24218, 22859, 21576, 20365, 19222, 18143, 17125, 16164, 15256, 14400,
    13592, 12829, 12109, 11429, 10788, 10182, 9611, 9072, 8562, 8082, 7628, 7200, 6796, 6415, 6055, 5715, 5394, 5091,
    4805, 4536, 4281, 4041, 3814, 3600, 3398, 3207, 3027, 2857, 2697, 2546, 2403, 2268, 2141, 2020, 1907, 1800, 1699,
    1604, 1514, 1429, 1348, 1273, 1201, 1134, 1070, 1010, 954, 900, 849, 802, 757, 714, 674};
/* the gate: peak for -66..-18 dBFS in 3 dB steps */
static const uint16_t pt_gates[17] = {16, 23, 33, 46, 65, 92, 130, 184, 260, 368, 519, 734, 1036, 1464, 2067, 2920, 4125};

/* settings */
enum { PM_NOTE, PM_SNAP, PM_CHORD, PM_SEVENTH, PM_ARP, PM_SCALE, PM_JUMP, PM_COUNT };
static const char *const pt_mode_name[PM_COUNT] = {"NOTE", "SNAP", "CHORD", "7TH", "ARP", "SCALE", "JUMP"};
static const char *const pt_src_name[6] = {"A+B", "A", "B", "C+D", "C", "D"};
static int pt_on, pt_mode, pt_scale, pt_root, pt_oct, pt_gate = 6, pt_chan, pt_vel, pt_rate = 6, pt_stab = 2, pt_bend, pt_clar = 60;

/* detector state */
static int16_t pt_x[PT_N], pt_c[PT_CN], pt_nc[PT_CMAX + 2];
static int pt_state, pt_lag, pt_neg, pt_lobe, pt_lmax, pt_ltau, pt_stop, pt_nk;
static int16_t pt_kt[16], pt_kv[16];
static int32_t pt_e0, pt_et, pt_fe0;
static uint32_t pt_seen, pt_start, pt_quiet, pt_view = 0x80000000u;
static int pt_nosite, pt_gate_open, pt_peak, pt_lastpeak, pt_ccoarse;

/* results: the latest estimate and what is played */
static int pt_valid, pt_x100, pt_hz10, pt_conf, pt_unv, pt_cand, pt_cnt, pt_out = -1, pt_med[3], pt_nmed;
static int16_t pt_hist[PT_HIST];
static int pt_hpos;
static uint32_t pt_estimates, pt_noteons;

/* ---- the detector ---------------------------------------------------------------------------------- */
static int pt_log2(unsigned v) { int n = 0; while (v > 1) { v >>= 1; n++; } return n; }
static int pt_nsdf(int32_t r, int32_t a, int32_t b)
{
    int32_t den = ((a >> 1) + (b >> 1)) >> 15;
    int32_t n = r / (den + 1);
    return n > 32767 ? 32767 : n < -32767 ? -32767 : (int)n;
}
/* n at full rate (W 256) for one lag */
static int pt_full(int lag)
{
    int32_t r = 0, e = 0;
    const int16_t *a = pt_x, *b = pt_x + lag;
    for (int i = 0; i < PT_W; i++) { r += a[i] * b[i]; e += b[i] * b[i]; }
    return pt_nsdf(r, pt_fe0, e);
}
/* parabolic vertex offset in Q8 (-128..128) of three values with the middle highest */
static int pt_parab(int a, int b, int c)
{
    int d = a - 2 * b + c;
    if (d >= 0) return 0;
    return clamp((a - c) * 128 / d, -128, 128);
}
/* hill climb from lag l at full rate: Q8 lag of the peak; *val its n */
static int pt_climb(int l, int *val)
{
    l = clamp(l, 3, PT_N - PT_W - 2);
    int a = pt_full(l - 1), b = pt_full(l), c = pt_full(l + 1);
    for (int step = 0; step < 6; step++) {
        if (a > b && l > 3) { l--; c = b; b = a; a = pt_full(l - 1); }
        else if (c > b && l < PT_N - PT_W - 2) { l++; a = b; b = c; c = pt_full(l + 1); }
        else break;
    }
    *val = b;
    return l * 256 + pt_parab(a, b, c);
}

static void pt_key_push(int t, int v) { if (pt_nk < 16) { pt_kt[pt_nk] = (int16_t)t; pt_kv[pt_nk] = (int16_t)v; pt_nk++; } }

/* the snapshot: 1 voiced and ready for lags, 0 nothing to analyse */
static int pt_snapshot(void)
{
    uint32_t w = pt_wr;
    int32_t sum = 0;
    for (int i = 0; i < PT_N; i++) { int v = pt_ring[(w - PT_N + (uint32_t)i) & (PT_RING - 1)]; pt_x[i] = (int16_t)v; sum += v; }
    int mean = sum / PT_N, peak = 1;
    for (int i = 0; i < PT_N; i++) { int v = clamp(pt_x[i] - mean, -32767, 32767); pt_x[i] = (int16_t)v; if (iabs(v) > peak) peak = iabs(v); }
    pt_lastpeak = pt_peak; pt_peak = peak;
    int thr = pt_gates[pt_gate];
    pt_gate_open = peak >= (pt_gate_open ? thr / 2 : thr);
    if (!pt_gate_open) return 0;
    int32_t scale = (2000 << 12) / peak;
    for (int i = 0; i < PT_N; i++) pt_x[i] = (int16_t)((pt_x[i] * scale) >> 12);
    for (int k = 0; k < PT_CN; k++) {
        int a = pt_x[k ? 2 * k - 1 : 0], b = pt_x[2 * k], c = pt_x[2 * k + 1];
        pt_c[k] = (int16_t)((a + 2 * b + c) >> 2);
    }
    pt_e0 = pt_et = pt_fe0 = 0;
    for (int i = 0; i < PT_CW; i++) { pt_e0 += pt_c[i] * pt_c[i]; pt_et += pt_c[i + 1] * pt_c[i + 1]; }
    for (int i = 0; i < PT_W; i++) pt_fe0 += pt_x[i] * pt_x[i];
    pt_lag = 1; pt_neg = pt_lobe = pt_nk = 0; pt_lmax = 0; pt_ltau = 0; pt_stop = PT_CMAX;
    pt_nc[0] = 32767;
    return 1;
}

/* one coarse lag; 1 when the lag search is over */
static int pt_coarse_step(void)
{
    int t = pt_lag;
    int32_t r = 0;
    const int16_t *a = pt_c, *b = pt_c + t;
    for (int i = 0; i < PT_CW; i++) r += a[i] * b[i];
    int n = pt_nsdf(r, pt_e0, pt_et);
    pt_nc[t] = (int16_t)n;
    pt_et += pt_c[t + PT_CW] * pt_c[t + PT_CW] - pt_c[t] * pt_c[t];
    if (!pt_neg) { if (n <= 0) pt_neg = 1; }
    else if (n > 0) { if (!pt_lobe || n > pt_lmax) { pt_lmax = n; pt_ltau = t; } pt_lobe = 1; }
    else if (pt_lobe) {
        pt_key_push(pt_ltau, pt_lmax);
        if (pt_lmax >= 26214 && pt_stop == PT_CMAX) pt_stop = clamp(pt_ltau * 2 + pt_ltau / 4 + 2, t, PT_CMAX);
        pt_lobe = 0;
    }
    pt_lag = t + 1;
    if (pt_lag <= pt_stop) return 0;
    if (pt_lobe && pt_ltau < t) pt_key_push(pt_ltau, pt_lmax); /* a lobe cut off past its peak */
    pt_nc[pt_lag] = pt_nc[t];
    return 1;
}

/* the coarse pick, then the full-rate refinement: the period in Q8, 0 none */
static int pt_finish(void)
{
    int best = 0, k = -1;
    for (int i = 0; i < pt_nk; i++) if (pt_kv[i] > best) best = pt_kv[i];
    for (int i = 0; i < pt_nk && k < 0; i++) if (pt_kv[i] >= best * 86 / 100) k = i;
    if (k < 0 || best * 100 < pt_clar * 32767) return 0;
    int t = pt_kt[k];
    int tc = t * 256 + pt_parab(pt_nc[t - 1], pt_nc[t], pt_nc[t + 1]);
    pt_ccoarse = tc;
    int v1, v2;
    int tf = pt_climb((2 * tc + 128) >> 8, &v1);
    if (v1 * 100 < pt_clar * 32767) return 0;
    int m = (PT_LONG << 8) / tf;
    if (m >= 2) {
        int tm = pt_climb((m * tf + 128) >> 8, &v2);
        int per = tm / m;
        if (v2 >= v1 - 4915 && iabs(per - tf) < tf / 16) tf = per;
    }
    pt_conf = v1;
    return tf;
}

/* period (Q8) -> note x 100 (cents within) */
static int pt_note100(int per)
{
    if (per >= pt_per[0] || per <= pt_per[84]) return -1;
    int lo = 0, hi = 84;
    while (hi - lo > 1) { int mid = (lo + hi) / 2; if (pt_per[mid] >= per) lo = mid; else hi = mid; }
    return (24 + lo) * 100 + (pt_per[lo] - per) * 100 / (pt_per[lo] - pt_per[hi]);
}

/* ---- MIDI: what is wanted, and what sounds -------------------------------------------------------- */
static uint8_t pt_snd[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, pt_sch[8], pt_stale;
static uint8_t pt_want[8];
static int pt_nwant, pt_velo = 100, pt_cc_ch = -1, pt_cleared, pt_bent = 8192, pt_arp_t, pt_arp_i;
static uint8_t pt_chord[8];
static int pt_nchord;

/* nearest note of the scale, ties downward */
static int pt_snap(int note)
{
    uint16_t m = gb_scales[pt_scale];
    for (int d = 0; d < 12; d++) {
        int lo = note - d, hi = note + d;
        if ((m >> ((unsigned)(lo + 132 - pt_root) % 12u)) & 1u) return lo;
        if ((m >> ((unsigned)(hi + 132 - pt_root) % 12u)) & 1u) return hi;
    }
    return note;
}
/* the chord of `count` notes stacked in thirds of the scale over a scale note (chromatic: a dominant seventh) */
static void pt_build_chord(int root, int count)
{
    uint16_t m = gb_scales[pt_scale];
    int deg[12], n = 0;
    for (int k = 0; k < 12; k++) if ((m >> k) & 1) deg[n++] = k;
    pt_nchord = 0;
    if (n >= 12 || n < 3) {
        static const uint8_t dom[4] = {0, 4, 7, 10};
        for (int i = 0; i < count; i++) pt_chord[pt_nchord++] = (uint8_t)clamp(root + dom[i], 0, 127);
        return;
    }
    int pc = (root - pt_root + 132) % 12, at = 0;
    for (int i = 0; i < n; i++) if (deg[i] == pc) at = i;
    for (int i = 0; i < count; i++) {
        int j = at + 2 * i, oct = j / n;
        pt_chord[pt_nchord++] = (uint8_t)clamp(root + deg[j % n] - deg[at] + 12 * oct, 0, 127);
    }
}
static void pt_want_one(int n) { pt_want[0] = (uint8_t)clamp(n, 0, 127); pt_nwant = 1; }

/* a new note (onset 1: struck again; 0: the pitch moved) */
static void pt_play(int note, int onset)
{
    int base = note + 12 * pt_oct;
    if (onset) for (int i = 0; i < 8; i++) if (pt_snd[i] != 0xff) pt_stale |= (uint8_t)(1u << i);
    pt_velo = pt_vel ? pt_vel : clamp(127 + 12 * (pt_log2((unsigned)pt_peak) - 14), 24, 127);
    if (pt_mode == PM_NOTE) pt_want_one(base);
    else if (pt_mode == PM_SNAP) pt_want_one(pt_snap(base));
    else if (pt_mode == PM_CHORD || pt_mode == PM_SEVENTH || pt_mode == PM_ARP) {
        pt_build_chord(pt_snap(base), pt_mode == PM_CHORD ? 3 : 4);
        if (pt_mode == PM_ARP) { pt_arp_i = 0; pt_arp_t = 0; pt_want_one(pt_chord[0]); }
        else { for (int i = 0; i < pt_nchord; i++) pt_want[i] = pt_chord[i]; pt_nwant = pt_nchord; }
    } else if (pt_mode == PM_SCALE) pt_want_one(pt_snap(base + range(-7, 7)));
    else pt_want_one(pt_snap(base) + 12 * range(-1, 2));
}

/* send offs (stale, unwanted, wrong channel), a pending CC123, then ons; the budget may stop it: next tick */
static void pt_sync(void)
{
    for (int i = 0; i < 8; i++) {
        if (pt_snd[i] == 0xff) continue;
        int keep = pt_on && !((pt_stale >> i) & 1) && pt_sch[i] == pt_chan;
        for (int j = 0; keep && j < pt_nwant; j++) if (pt_want[j] == pt_snd[i]) keep = 2;
        if (keep == 2) continue;
        if (!midi_out(0x80u | pt_sch[i], pt_snd[i], 0)) return;
        pt_snd[i] = 0xff; pt_stale &= (uint8_t)~(1u << i);
    }
    if (pt_cc_ch >= 0) { if (!midi_out(0xb0u | (unsigned)pt_cc_ch, 123, 0)) return; pt_cc_ch = -1; }
    if (!pt_on) return;
    for (int j = 0; j < pt_nwant; j++) {
        int have = 0, free = -1;
        for (int i = 0; i < 8; i++) { if (pt_snd[i] == pt_want[j]) have = 1; else if (pt_snd[i] == 0xff && free < 0) free = i; }
        if (have || free < 0) continue;
        if (pt_bend && pt_mode == PM_NOTE && pt_bent != 8192) { if (!midi_out(0xe0u | (unsigned)pt_chan, 0, 64)) return; pt_bent = 8192; }
        if (!midi_out(0x90u | (unsigned)pt_chan, pt_want[j], (unsigned)pt_velo)) return;
        pt_snd[free] = pt_want[j]; pt_sch[free] = (uint8_t)pt_chan; pt_noteons++;
    }
}
static void pt_release(void) { pt_nwant = 0; pt_out = -1; pt_cnt = 0; pt_nmed = 0; }
static void pt_panic(void) { pt_release(); pt_stale = 0xff; pt_cc_ch = pt_chan; }

/* one estimate: per (Q8) or 0 for none */
static void pt_estimate(int per)
{
    int x100 = per ? pt_note100(per) : -1;
    pt_estimates++;
    if (x100 < 3100 || x100 > 9650) x100 = -1;
    pt_valid = x100 >= 0;
    if (!pt_valid) {
        if (++pt_unv >= 2 || !pt_gate_open) pt_release();
        return;
    }
    pt_unv = 0;
    pt_x100 = x100;
    pt_hz10 = 28224000 / per;
    /* the median of the last three, a candidate note that must hold, and a 65-cent hysteresis */
    if (pt_nmed < 3) pt_med[pt_nmed++] = x100; else { pt_med[0] = pt_med[1]; pt_med[1] = pt_med[2]; pt_med[2] = x100; }
    int m = x100;
    if (pt_nmed == 3) { int a = pt_med[0], b = pt_med[1], c = pt_med[2]; m = a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b); }
    else if (pt_nmed == 2) m = (pt_med[0] + pt_med[1]) / 2;
    int cand = (m + 50) / 100;
    pt_cnt = cand == pt_cand ? pt_cnt + 1 : 1;
    pt_cand = cand;
    int struck = pt_peak > pt_lastpeak * 5 / 2 && pt_lastpeak > 0;
    if (pt_out < 0) { if (pt_cnt >= pt_stab) { pt_out = cand; pt_play(cand, 1); } }
    else if (cand != pt_out && iabs(m - pt_out * 100) > 65 && pt_cnt >= pt_stab) { pt_out = cand; pt_play(cand, pt_mode == PM_SCALE || pt_mode == PM_JUMP); }
    else if (struck) pt_play(pt_out, 1);
    if (pt_bend && pt_mode == PM_NOTE && pt_out >= 0) {
        int b = clamp(8192 + (m - pt_out * 100) * 4096 / 100, 0, 16383);
        if (iabs(b - pt_bent) > 40 && midi_out(0xe0u | (unsigned)pt_chan, (unsigned)(b & 127), (unsigned)(b >> 7))) pt_bent = b;
    }
}

void pitch_tick(void)
{
    if (!pt_cleared) { if (midi_out(0xb0u | (unsigned)pt_chan, 123, 0)) pt_cleared = 1; return; }
    int active = pt_on || ticks - pt_view < 30u;
    if (active && !pt_cap) { pt_start = pt_frames; pt_seen = pt_frames; pt_quiet = ticks; pt_state = 0; }
    pt_cap = (uint8_t)active;
#if defined(PREVIEW) && defined(AERIAOS)
    aos_tap_want(2, active);
#endif
    if (!active) { pt_release(); pt_sync(); return; }
    if (pt_frames != pt_seen) { pt_seen = pt_frames; pt_quiet = ticks; pt_nosite = 0; }
    else if (ticks - pt_quiet > 20u) pt_nosite = 1;
    /* the arpeggio walks the chord up and down, an octave over the top */
    if (pt_mode == PM_ARP && pt_out >= 0 && pt_nchord && ++pt_arp_t >= pt_rate) {
        static const uint8_t walk[8] = {0, 1, 2, 3, 4, 3, 2, 1};
        pt_arp_t = 0; pt_arp_i = (pt_arp_i + 1) & 7;
        int w = walk[pt_arp_i];
        pt_want_one(w < pt_nchord ? pt_chord[w] : pt_chord[0] + 12);
        pt_stale = 0xff;
    }
    int budget = PT_BUDGET;
    while (budget > 0 && pt_frames - pt_start >= PT_N / 4) {
        if (pt_state == 0) {
            budget -= PT_N * 2;
            if (pt_snapshot()) pt_state = 1; else { pt_estimate(0); break; }
        } else if (pt_state == 1) {
            budget -= PT_CW;
            if (pt_coarse_step()) pt_state = 2;
        } else {
            budget -= 12 * PT_W * 2;
            pt_estimate(pt_finish());
            pt_state = 0;
            break;
        }
    }
    pt_sync();
    if ((ticks & 1) == 0) { pt_hist[pt_hpos] = (int16_t)(pt_valid ? pt_x100 : 0); pt_hpos = (pt_hpos + 1) % PT_HIST; }
}

/* ---- the page --------------------------------------------------------------------------------------- */
static void pt_big(int x, int y, const char *s, int k)
{
    for (; *s; s++, x += 4 * k) {
        int g = glyph_of(*s);
        if (g < 0) continue;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
            if (glyphs[g] & (1u << (14 - (r * 3 + c)))) for (int dy = 0; dy < k; dy++) for (int dx = 0; dx < k; dx++) px(x + c * k + dx, y + r * k + dy);
    }
}
/* decimal with an optional tenth */
static void pt_fmt(char *o, int v, int tenths)
{
    char t[8]; int n = 0, i = 0;
    if (v < 0) { o[i++] = '-'; v = -v; }
    int whole = tenths ? v / 10 : v;
    do { t[n++] = (char)('0' + whole % 10); whole /= 10; } while (whole && n < 6);
    while (n) o[i++] = t[--n];
    if (tenths) { o[i++] = '.'; o[i++] = (char)('0' + v % 10); }
    o[i] = 0;
}
static int pt_cat(char *o, int i, const char *s) { while (*s && i < 30) o[i++] = *s++; o[i] = 0; return i; }
static void pt_bar(int x, int y, int w, int v, int max)
{
    for (int i = 0; i < w; i++) { px(x + i, y); px(x + i, y + 3); }
    span(x, y, y + 3, 1); span(x + w - 1, y, y + 3, 1);
    int f = clamp(v, 0, max) * (w - 2) / (max ? max : 1);
    for (int i = 1; i <= f; i++) span(x + i, y + 1, y + 2, 1);
}

/* little notes that float up from the kite when one plays */
static int16_t pt_fx[6], pt_fy[6];
static uint8_t pt_flife[6];
static uint32_t pt_last_on;

void pitch_draw(void)
{
    pt_view = ticks;
    for (int x = 0; x < W; x++) span(x, 9, H - 1, 0);
    char s[32];
    int on = pt_out >= 0 && pt_valid;
    if (pt_valid) {
        int n = (pt_x100 + 50) / 100, cents = pt_x100 - n * 100;
        pt_big(3, 11, gb_note_name[n % 12], 3);
        pt_fmt(s, n / 12 - 1, 0);
        pt_big(gb_note_name[n % 12][1] ? 28 : 16, 17, s, 2);
        pt_fmt(s, pt_hz10, 1); { int i = 0; while (s[i]) i++; pt_cat(s, i, " HZ"); }
        text(46, 10, s, 1);
        s[0] = cents >= 0 ? '+' : '-'; pt_fmt(s + 1, iabs(cents), 0);
        text(W - 2 - text_w(s), 10, s, 1);
        /* the cents needle */
        for (int x = 46; x < W - 2; x += 2) px(x, 19);
        span(86, 16, 22, 1);
        int nx = 86 + cents * 38 / 50;
        span(nx - 1, 17, 21, 1); span(nx, 16, 22, 1); span(nx + 1, 17, 21, 1);
    } else {
        text(5, 14, pt_gate_open ? "..." : "-", 1);
        text(46, 10, pt_nosite ? "NO INPUT TAP" : pt_gate_open ? "LISTENING" : "QUIET", 1);
        if (pt_nosite) text(46, 17, "BUILD WITH THE SITE", 1);
    }
    /* clarity and level */
    text(46, 25, "CL", 1); pt_bar(55, 25, 30, pt_valid ? pt_conf : 0, 32767);
    text(88, 25, "IN", 1); pt_bar(97, 25, 29, pt_log2((unsigned)pt_peak), 15);
    { int g = 97 + pt_log2(pt_gates[pt_gate]) * 27 / 15; px(g, 24); px(g, 29); }
    /* the trail: a semitone a pixel round the note being sung; the kite at its head */
    int centre = pt_valid ? pt_x100 : 6000;
    for (int i = PT_HIST - 1, prev = -1; i >= 0; i--) {
        int v = pt_hist[(pt_hpos + i) % PT_HIST], x = 4 + i * 100 / PT_HIST;
        if (!v) { prev = -1; continue; }
        int y = clamp(43 - (v - centre) / 100, 31, 55);
        if (prev >= 0) line(x, y, x + 1, prev); else px(x, y);
        prev = y;
    }
    int ky = 43, kx = 106 + sine[(int)(ticks * 2) & 63] / 60;
    int last = pt_hist[(pt_hpos + PT_HIST - 1) % PT_HIST];
    if (last) ky = clamp(43 - (last - centre) / 100, 33, 52);
    for (int d = -4; d <= 4; d++) { int w = 3 - iabs(d) * 3 / 4; if (on) for (int x = -w; x <= w; x++) px(kx + x, ky + d); else { px(kx - w, ky + d); px(kx + w, ky + d); } }
    for (int i = 0; i < 9; i++) px(kx - 1 - i + sine[(i * 6 + (int)ticks * 3) & 63] / 90, ky + 5 + i);
    line(kx - 14, ky, kx - 4, ky);
    if (pt_noteons != pt_last_on) {
        for (int i = 0; i < 6; i++) if (!pt_flife[i]) { pt_fx[i] = (int16_t)(kx + range(-3, 3)); pt_fy[i] = (int16_t)(ky - 6); pt_flife[i] = (uint8_t)range(20, 40); break; }
    }
    pt_last_on = pt_noteons;
    for (int i = 0; i < 6; i++) {
        if (!pt_flife[i]) continue;
        pt_flife[i]--;
        if ((ticks & 1) == 0) pt_fy[i]--;
        if ((ticks & 3) == 0) pt_fx[i] = (int16_t)(pt_fx[i] - 1);
        int x = pt_fx[i], y = pt_fy[i];
        if (y < 31) { pt_flife[i] = 0; continue; }
        px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1); span(x + 2, y - 4, y + 1, 1); px(x + 3, y - 3);
    }
    /* what plays */
    if (pt_on && pt_nwant) {
        int i = 0; s[0] = 0;
        for (int j = 0; j < pt_nwant && j < 4; j++) { i = pt_cat(s, i, gb_note_name[pt_want[j] % 12]); i = pt_cat(s, i, " "); }
        text(4, 50, s, 1);
    }
    int i = pt_cat(s, 0, pt_mode_name[pt_mode]);
    if (pt_mode != PM_NOTE) { i = pt_cat(s, i, " "); i = pt_cat(s, i, gb_note_name[pt_root]); i = pt_cat(s, i, " "); i = pt_cat(s, i, gb_scale_name[pt_scale]); }
    text(2, 58, s, 1);
    pt_cat(s, 0, "CH"); pt_fmt(s + 2, pt_chan + 1, 0);
    i = 0; while (s[i]) i++;
    pt_cat(s, i, pt_on ? " ON" : " OFF");
    text(W - 2 - text_w(s), 58, s, 1);
}

static void pt_switch(int on)
{
    pt_on = on;
    if (!on) { pt_release(); pt_cc_ch = pt_chan; }
    say("PITCH MIDI", on ? "ON" : "OFF");
}

int pitch_key(int code)
{
    if (code == KEY_YES) { pt_switch(!pt_on); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + PM_COUNT) { pt_mode = code - KEY_TRIG0; pt_release(); pt_stale = 0xff; say("PITCH", pt_mode_name[pt_mode]); return 1; }
    if (code == KEY_TRIG0 + 15) { pt_panic(); say("PITCH", "ALL OFF"); return 1; }
    return 0;
}

void pitch_enc(int e, int d, int func)
{
    char n[12];
    int s = d > 0 ? 1 : -1;
    if (!func) {
        if (e == 0) { pt_mode = clamp(pt_mode + s, 0, PM_COUNT - 1); pt_release(); pt_stale = 0xff; say("MODE", pt_mode_name[pt_mode]); }
        else if (e == 1) { pt_scale = clamp(pt_scale + s, 0, GB_NSCALES - 1); say("SCALE", gb_scale_name[pt_scale]); }
        else if (e == 2) { pt_root = (pt_root + s + 12) % 12; say("ROOT", gb_note_name[pt_root]); }
        else if (e == 3) { pt_oct = clamp(pt_oct + s, -3, 3); n[0] = pt_oct < 0 ? '-' : '+'; pt_fmt(n + 1, iabs(pt_oct), 0); say("OCTAVE", n); if (pt_out >= 0) pt_play(pt_out, 1); }
        else if (e == 4) { pt_gate = clamp(pt_gate + s, 0, 16); pt_fmt(n, -66 + 3 * pt_gate, 0); pt_cat(n, 3, " DB"); say("GATE", n); }
        else if (e == 5) { pt_chan = clamp(pt_chan + s, 0, 15); pt_fmt(n, pt_chan + 1, 0); say("CHANNEL", n); }
    } else {
        if (e == 0) { pt_vel = clamp(pt_vel + d, 0, 127); if (pt_vel) pt_fmt(n, pt_vel, 0); else pt_cat(n, 0, "INPUT"); say("VELOCITY", n); }
        else if (e == 1) { pt_rate = clamp(pt_rate + s, 1, 24); pt_fmt(n, pt_rate, 0); say("ARP TICKS", n); }
        else if (e == 2) { pt_stab = clamp(pt_stab + s, 1, 5); pt_fmt(n, pt_stab, 0); say("STEADY", n); }
        else if (e == 3) { pt_src = (uint8_t)clamp(pt_src + s, 0, 5); say("LISTEN", pt_src_name[pt_src]); }
        else if (e == 4) { pt_bend = d > 0; say("BEND", pt_bend ? "ON" : "OFF"); }
        else if (e == 5) { pt_clar = clamp(pt_clar + s * 5, 30, 95); pt_fmt(n, pt_clar, 0); say("CLARITY", n); }
    }
}
