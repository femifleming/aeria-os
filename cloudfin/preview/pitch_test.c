/* Host test of AERIA's PITCH: synthetic input, written into the stock input-page layout (A at +0x80 + 8s,
 * B at +0x84 + 8s, 24-bit left-justified), one aeria_frame() per 16 samples (the frame interrupt), pitch_tick()
 * every 735 samples (the 60 Hz UI tick). Reports per-note accuracy, cents error and latency.
 *   cc -O1 -I preview -I . preview/pitch_test.c -lm -o preview/pitch_test && ./preview/pitch_test
 * (run from ~/octa/cloudfin after ./assemble.sh) */
#define PREVIEW 1
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "../aeria12.c"

#define SR 44100.0
static double ph, vph, lp, rng_state = 1;
static uint32_t idx;
static long sample_no;
static unsigned ons_total, ons_wrong;
static int target_note;
static double frand(void) { rng_state = fmod(rng_state * 16807.0, 2147483647.0); return rng_state / 2147483647.0 * 2 - 1; }

enum { SINE, SAW, VOICE, NOISE, SILENT, FUZZ };
static const char *const kind_name[] = {"sine", "saw", "voice", "noise", "silence", "fuzz"};

/* one sample of the test signal, -1..1 */
static double gen(int kind, double f, double amp)
{
    if (kind == SILENT) return 0;
    if (kind == NOISE) return amp * frand();
    double fv = f;
    if (kind == VOICE) { vph += 5.5 / SR; fv = f * pow(2, 30.0 / 1200 * sin(2 * M_PI * vph)) * (1 + 0.002 * frand()); }
    ph += fv / SR; ph -= floor(ph);
    if (kind == SINE) return amp * sin(2 * M_PI * ph);
    if (kind == FUZZ) { double v = 6 * (sin(2 * M_PI * ph) + 0.5 * sin(4 * M_PI * ph + 1)); return amp * (v > 1 ? 1 : v < -1 ? -1 : v); }
    if (kind == SAW) return amp * (2 * ph - 1);
    /* voice: harmonics shaped by two formants (an "ah": 730 and 1090 Hz) over a -12 dB/oct source, plus breath */
    double s = 0, norm = 0;
    for (int h = 1; h * f < 5000; h++) {
        double fh = h * f, g = 1.0 / (h * h) * 4;
        g *= 1 + 6 / (1 + pow((fh - 730) / 90, 2)) + 4 / (1 + pow((fh - 1090) / 110, 2));
        norm += g;
    }
    for (int h = 1; h * fv < 5000; h++) {
        double fh = h * fv, g = 1.0 / (h * h) * 4;
        g *= 1 + 6 / (1 + pow((fh - 730) / 90, 2)) + 4 / (1 + pow((fh - 1090) / 110, 2));
        s += g * sin(2 * M_PI * ph * h);
    }
    lp = lp * 0.9 + 0.1 * frand();
    return amp * (s / norm + 0.02 * lp);
}

/* the frame interrupt's input page, then pitch_tick at 60 Hz */
static void feed(double v, double v_b)
{
    static int s;
    int32_t a = (int32_t)lrint(fmax(-1, fmin(1, v)) * 8388607.0) * 256, b = (int32_t)lrint(fmax(-1, fmin(1, v_b)) * 8388607.0) * 256;
    uint32_t page = (idx + 1) & 7;
    preview_inring[page][32 + 2 * s] = a;
    preview_inring[page][33 + 2 * s] = b;
    preview_inring[page][0 + 2 * s] = 0x12345600; /* C, D: must not leak into A+B */
    preview_inring[page][1 + 2 * s] = -0x12345600;
    if (++s == 16) { s = 0; idx = page; preview_inidx = idx; aeria_frame(); }
    sample_no++;
    if (sample_no % 735 == 0) {
        uint32_t before = pt_noteons;
        uint8_t snap[8]; for (int i = 0; i < 8; i++) snap[i] = pt_snd[i];
        ticks++; gum_bytes = 0; pitch_tick();
        if (pt_noteons != before)
            for (int i = 0; i < 8; i++) if (pt_snd[i] != 0xff && pt_snd[i] != snap[i]) { ons_total++; if (target_note >= 0 && pt_snd[i] != target_note) ons_wrong++; }
    }
}

typedef struct { int n, good, oct, est; double cents_abs, cents_sum; long latency, first_est; } Stat;

/* silence, a held note, silence: what the detector and the output make of it */
static Stat run_note(int kind, int note, double amp, double secs)
{
    Stat st = {0};
    double f = 440 * pow(2, (note - 69) / 12.0);
    target_note = -1;
    for (int i = 0; i < (int)(SR * 0.4); i++) feed(0, 0);
    target_note = note;
    long start = sample_no, steady = start + (long)(SR * 0.25);
    st.latency = st.first_est = -1;
    uint32_t est = pt_estimates;
    for (long i = 0; i < (long)(SR * secs); i++) {
        double v = gen(kind, f, amp);
        feed(v, v);
        if (st.latency < 0) for (int k = 0; k < 8; k++) if (pt_snd[k] == note) st.latency = sample_no - start;
        if (pt_estimates != est) {
            est = pt_estimates;
            if (pt_valid && st.first_est < 0) st.first_est = sample_no - start;
            if (sample_no >= steady) {
                st.n++;
                if (pt_valid) {
                    st.est++;
                    double cents = pt_x100 - note * 100 - (kind == VOICE ? 0 : 0);
                    int nn = (pt_x100 + 50) / 100;
                    if (nn == note) st.good++;
                    else if ((nn - note) % 12 == 0) st.oct++;
                    st.cents_abs += fabs(cents); st.cents_sum += cents;
                }
            }
        }
    }
    target_note = -1;
    for (int i = 0; i < (int)(SR * 0.3); i++) feed(0, 0);
    return st;
}

static const char *nname(int n) { static char b[8]; snprintf(b, sizeof b, "%s%d", gb_note_name[n % 12], n / 12 - 1); return b; }

int main(void)
{
    static const int notes[] = {35, 40, 43, 45, 48, 52, 57, 60, 64, 69, 72, 76, 81, 84, 88};
    struct modwerk_runtime_api api; (void)api;
    F = 0;
    pitch_key(KEY_YES); /* on: NOTE mode, gate -48 dBFS, A+B */
    for (int i = 0; i < 735 * 3; i++) feed(0, 0);
    int kinds[] = {SINE, SAW, VOICE, FUZZ};
    double amps[] = {0.25, 0.25, 0.5, 1.0};
    int all_n = 0, all_good = 0;
    printf("PITCH host test: 44.1 kHz input -> frame tap (11,025 Hz ring) -> detector at 60 Hz ticks, budget %d products/tick\n", PT_BUDGET);
    printf("%-6s %-6s %7s | %6s %5s %4s %8s %8s | %9s %9s\n", "signal", "note", "Hz", "est", "ok%", "oct", "|cents|", "bias", "first est", "note on");
    for (int k = 0; k < 4; k++) {
        for (unsigned i = 0; i < sizeof notes / sizeof notes[0]; i++) {
            unsigned w0 = ons_wrong, t0 = ons_total;
            Stat s = run_note(kinds[k], notes[i], amps[k], 1.2);
            double hz = 440 * pow(2, (notes[i] - 69) / 12.0);
            printf("%-6s %-6s %7.1f | %6d %4.0f%% %4d %8.1f %+8.1f | %6.0f ms %6.0f ms  ons %u wrong %u\n", kind_name[kinds[k]], nname(notes[i]), hz, s.est,
                   s.n ? 100.0 * s.good / s.n : 0, s.oct, s.est ? s.cents_abs / s.est : 0, s.est ? s.cents_sum / s.est : 0,
                   s.first_est < 0 ? -1 : s.first_est * 1000 / SR, s.latency < 0 ? -1 : s.latency * 1000 / SR, ons_total - t0, ons_wrong - w0);
            if (notes[i] >= 40 && notes[i] <= 84) { all_n += s.n; all_good += s.good; }
        }
    }
    printf("E2..C6 overall: %d/%d estimates on the right note (%.1f%%)\n", all_good, all_n, all_n ? 100.0 * all_good / all_n : 0);

    /* a legato melody (no gaps), then plucks that decay: transitions and retriggers */
    {
        static const int mel[] = {52, 55, 57, 59, 60, 64, 62, 60, 57, 55, 52, 45, 40, 47, 52, 64, 76, 72, 69, 64};
        for (int k = 0; k < 2; k++) {
            int kind = k ? VOICE : SAW;
            unsigned t0 = ons_total, w0 = ons_wrong;
            long lat_sum = 0; int lat_n = 0, missed = 0;
            for (unsigned i = 0; i < sizeof mel / sizeof mel[0]; i++) {
                target_note = mel[i];
                long start = sample_no, lat = -1;
                double f = 440 * pow(2, (mel[i] - 69) / 12.0);
                for (long j = 0; j < (long)(SR * 0.3); j++) {
                    double v = gen(kind, f, k ? 0.5 : 0.25); feed(v, v);
                    if (lat < 0) for (int q = 0; q < 8; q++) if (pt_snd[q] == mel[i]) lat = sample_no - start;
                }
                if (lat < 0) missed++; else { lat_sum += lat; lat_n++; }
            }
            target_note = -1;
            for (int i = 0; i < (int)(SR * 0.3); i++) feed(0, 0);
            printf("legato %-5s 20 notes x 300 ms: %u note-ons, %u wrong, %d missed, mean change latency %.0f ms\n", kind_name[kind],
                   ons_total - t0, ons_wrong - w0, missed, lat_n ? lat_sum * 1000.0 / lat_n / SR : -1);
        }
        unsigned t0 = ons_total, w0 = ons_wrong;
        int still = 0;
        for (int p = 0; p < 6; p++) {
            int n = 45 + 5 * p; target_note = n;
            double f = 440 * pow(2, (n - 69) / 12.0);
            for (long j = 0; j < (long)(SR * 2.0); j++) { double v = gen(SAW, f, 0.6 * exp(-j / (SR * 0.25))); feed(v, v); }
            for (int q = 0; q < 8; q++) still += pt_snd[q] != 0xff;
        }
        target_note = -1;
        printf("6 plucks (saw, 250 ms decay, 2 s apart): %u note-ons (want 6), %u wrong, %d still sounding at the next pluck (want 0)\n",
               ons_total - t0, ons_wrong - w0, still);
        for (int i = 0; i < (int)(SR * 0.3); i++) feed(0, 0);
    }

    /* the gate and noise: nothing should play */
    unsigned t0 = ons_total;
    Stat q = run_note(SINE, 69, 0.0006, 1.0);   /* -64 dBFS, under the -48 dB gate */
    printf("quiet A4 at -64 dBFS: %d valid estimates, %u note-ons (want 0)\n", q.est, ons_total - t0);
    t0 = ons_total;
    Stat nz = run_note(NOISE, 60, 0.3, 1.5);
    printf("white noise -10 dBFS: %d/%d estimates voiced, %u note-ons\n", nz.est, nz.n, ons_total - t0);

    /* source select: tone on B only, listening to A: nothing; listening to B: found */
    pt_src = 1; t0 = ons_total;
    for (long i = 0; i < (long)(SR * 0.8); i++) feed(0, gen(SINE, 220, 0.3));
    printf("tone on B, LISTEN A: %u note-ons (want 0)\n", ons_total - t0);
    pt_src = 2; t0 = ons_total;
    for (long i = 0; i < (long)(SR * 0.8); i++) feed(0, gen(SINE, 220, 0.3));
    printf("tone on B, LISTEN B: %u note-ons, out %s\n", ons_total - t0, pt_out >= 0 ? nname(pt_out) : "-");
    for (int i = 0; i < (int)(SR * 0.3); i++) feed(0, 0);
    pt_src = 0;

    /* modes over a sung E4 in C major */
    static const int modes[] = {PM_SNAP, PM_CHORD, PM_SEVENTH, PM_ARP, PM_SCALE, PM_JUMP};
    for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; m++) {
        pitch_key(KEY_TRIG0 + modes[m]);
        printf("mode %-6s E4+30c:", pt_mode_name[pt_mode]);
        double f = 440 * pow(2, (64.3 - 69) / 12.0);
        int last[8] = {-1, -1, -1, -1, -1, -1, -1, -1}, shown = 0;
        for (long i = 0; i < (long)(SR * 0.9); i++) {
            double v = gen(VOICE, f, 0.5); feed(v, v);
            int same = 1; for (int k = 0; k < 8; k++) if (last[k] != pt_snd[k]) same = 0;
            if (!same && shown < 10) { printf(" ["); for (int k = 0; k < 8; k++) { last[k] = pt_snd[k]; if (pt_snd[k] != 0xff) printf(" %s", nname(pt_snd[k])); } printf(" ]"); shown++; }
        }
        for (int i = 0; i < (int)(SR * 0.3); i++) feed(0, 0);
        int left = 0; for (int k = 0; k < 8; k++) left += pt_snd[k] != 0xff;
        printf("  after silence: %d sounding\n", left);
    }
    /* off: everything released, CC123 queued and sent */
    pitch_key(KEY_TRIG0 + PM_CHORD);
    for (long i = 0; i < (long)(SR * 0.5); i++) { double v = gen(SINE, 330, 0.3); feed(v, v); }
    int before = 0; for (int k = 0; k < 8; k++) before += pt_snd[k] != 0xff;
    pitch_key(KEY_YES);
    for (int i = 0; i < 735 * 2; i++) feed(0, 0);
    int after = 0; for (int k = 0; k < 8; k++) after += pt_snd[k] != 0xff;
    printf("switch OFF while a chord sounds: %d sounding -> %d, CC123 pending %d\n", before, after, pt_cc_ch >= 0);

    /* draw the page once, to a PGM, for a look */
    static unsigned char fr[1024] __attribute__((aligned(4)));
    pitch_key(KEY_YES); pitch_key(KEY_TRIG0 + PM_SEVENTH);
    for (long i = 0; i < (long)(SR * 0.6); i++) { double v = gen(VOICE, 196, 0.5); feed(v, v); if (sample_no % 735 == 0) { F = (uint32_t *)fr; for (int j = 0; j < 256; j++) F[j] = 0; pitch_draw(); } }
    FILE *o = fopen("preview/pitch_page.pgm", "wb");
    if (o) {
        fprintf(o, "P5 %d %d 255\n", 128 * 3, 64 * 3);
        for (int y = 0; y < 64 * 3; y++) for (int x = 0; x < 128 * 3; x++) { int X = x / 3, Y = y / 3; fputc(((F[X * 2 + (Y < 32)] >> (Y & 31)) & 1) ? 225 : 28, o); }
        fclose(o);
    }
    return 0;
}
