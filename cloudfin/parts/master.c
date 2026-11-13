/* ---- MASTER: the freeze delay on the main mix, and the tempo (AERIA OS, sadnoise, 12 Nov 2026) --------------------
 * After the chain and after the weather; the weather never moves it. (The master EQ is on GAIN's FUNC layer, the
 * limiter's switch on MIX's.) The MIX key opens the page:
 *   TIME  the delay's time in sixteenth notes at the tempo, 1..16 -- the sixteen trigs pick it at a touch
 *   FDBK  how much comes round again      HPF / LPF  a high-pass and a lowpass in the feedback
 *   SEND  how much of the mix goes in     WET  how much of the delay is heard (0: the delay is off)
 *   FUNC: TMPO  the tempo, 60..187 BPM
 *   FREEZE: the crossfader at B. Nothing more goes in, what is in it goes round for ever; a change of TIME or
 *         tempo then jumps
 *   NO    tap the tempo (the last taps' average)
 *   The crossfader (FUNC: XFRZ on): at A the delay runs, moved to B it freezes -- and thaws on the way back. */
#if defined(AERIAOS)
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen);
static uint8_t fz_frozen;
static uint32_t us_clock, tap_at[4];
static int tap_n;
static int master_bpm(void) { return 60 + pv(PG_MASTER, 6); }
static void master_fast(uint32_t us) { us_clock += us; }
static uint8_t fz_fader;   /* the crossfader holds it frozen */
/* XMOD (12 Nov 2026): what the crossfader does besides freezing the delay.
 *   0 SCEN  the scenes (as before)
 *   1 DJF   a DJ filter on the main mix: left of the middle a low-pass comes down, right of it a high-pass goes up
 *   2 WET   every effect's MIX scaled by the fader: A all dry, B as set
 *   3 MOD   the MOD page's routes scaled by the fader: A still, B full */
static int xf_mode(void) { Param *p = &params[PG_MASTER][9]; int v = p->user >= 0 ? p->user : p->value; return v & 3; }
static int xf_scale(void) { return xfade(); }                                    /* 0..127 */
static void xf_filters(int *lp, int *hp)
{
    *lp = 127; *hp = 0;
    if (xf_mode() != 1) return;
    int x = xfade();
    if (x < 56) *lp = 127 - (56 - x) * 85 / 56;                                   /* down to index 42: about 200 Hz */
    if (x > 72) *hp = (x - 72) * 101 / 55;                                        /* up to index 101: about 5 kHz */
}
static uint8_t fz_rand;   /* RFRZ holds it frozen for a while */
static int16_t fz_rand_t; /* ticks to its next turn */
static int master_frozen(void) { return fz_frozen || fz_fader || fz_rand; }
/* RFRZ (Chloaelis's random freeze): a slow random on/off; the knob is the chance of a hold. Holds last 0.4 s to
 * about 4.6 s, longer the higher the knob; between them 0.3 to 3 s, shorter the higher the knob */
static void master_rfrz(void)
{
    int r = pv(PG_MASTER, 8);                                                   /* 0..127 */
    if (!r) { if (fz_rand) { fz_rand = 0; if (!master_frozen()) say("FREEZE", "OFF"); } fz_rand_t = 0; return; }
    if (--fz_rand_t > 0) return;
    if (fz_rand) { fz_rand = 0; fz_rand_t = (int16_t)(18 + range(0, 152 * (140 - r) / 127)); }
    else if (range(0, 126) < r) { fz_rand = 1; fz_rand_t = (int16_t)(24 + range(0, 213 * (40 + r) / 127)); }
    else fz_rand_t = (int16_t)(15 + range(0, 73));
}
static void master_tick(void)
{
    int wet = pv(PG_MASTER, 5);
    /* the crossfader: past three quarters of the way to B it freezes, back under a quarter past the middle it thaws */
    if (pv(PG_MASTER, 7) == 2) { int x = xfade(); if (x > 96 && !fz_fader) { fz_fader = 1; say("FREEZE", "ON"); } else if (x < 80 && fz_fader) { fz_fader = 0; if (!fz_frozen) say("FREEZE", "OFF"); } }
    else fz_fader = 0;
    master_rfrz();
    aos_freeze_delay(wet > 0 || master_frozen(), pv(PG_MASTER, 0), master_bpm(), pv(PG_MASTER, 1), pv(PG_MASTER, 2), pv(PG_MASTER, 3),
                     pv(PG_MASTER, 4), wet, master_frozen());
}
static void master_tap(void)
{
    uint32_t now_us = us_clock;
    if (tap_n && now_us - tap_at[tap_n - 1] > 2000000u) tap_n = 0;            /* two seconds apart: a new count */
    if (tap_n == 4) { for (int i = 0; i < 3; i++) tap_at[i] = tap_at[i + 1]; tap_n = 3; }
    tap_at[tap_n++] = now_us;
    if (tap_n >= 2) {
        uint32_t span = tap_at[tap_n - 1] - tap_at[0], beat = span / (uint32_t)(tap_n - 1);
        if (beat) { int bpm = (int)((60000000u + beat / 2) / beat); bpm = clamp(bpm, 60, 187); Param *p = &P(PG_MASTER, 6); p->value = p->user = (int16_t)(bpm - 60); char n[8]; num(n, bpm); say("TEMPO", n); }
    } else say("TAP", 0);
}
static int master_key(int code)
{
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + 16) { Param *p = &P(PG_MASTER, 0); p->value = p->user = (int16_t)(code - KEY_TRIG0 + 1); char n[8]; num(n, p->value); int k = 0; while (n[k]) k++; n[k++] = '/'; n[k++] = '1'; n[k++] = '6'; n[k] = 0; say("DELAY TIME", n); return 1; }
    /* (YES froze it once: too easy to do by accident -- the crossfader at B is the only freeze now) */
    if (code == KEY_NO) { master_tap(); return 1; }
    return 0;
}
static void master_say(int i, int v, char *n)
{
    int k = 0;
    if (i == 0) { num(n, v); while (n[k]) k++; n[k++] = '/'; n[k++] = '1'; n[k++] = '6'; n[k] = 0; say("DELAY TIME", n); }
    else if (i == 7) say("FADER FREEZES", v == 2 ? "ON" : "OFF");
    else if (i == 9) { static const char *const xm[4] = {"SCENES", "DJ FILTER", "EFFECTS WET", "MOD DEPTH"}; say("CROSSFADER", xm[v & 3]); }
    else if (i == 6) { num(n, 60 + v); while (n[k]) k++; n[k++] = 'B'; n[k++] = 'P'; n[k++] = 'M'; n[k] = 0; say("TEMPO", n); }
    else if (i == 2 || i == 3) {                                              /* the one-pole's corner, about */
        int c = i == 3 ? (v >= 127 ? 32768 : 600 + v * v * 2) : v * v / 2, hz = c >= 30000 ? 0 : c * 7019 / (32768 - c / 2);
        if (i == 3 && !hz) { say("DELAY LOWPASS", "OPEN"); return; }
        if (i == 2 && !v) { say("DELAY HIGH-PASS", "OFF"); return; }
        if (hz >= 1000) { num(n, hz / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + hz % 1000 / 100); n[k++] = 'K'; } else { num(n, hz); while (n[k]) k++; }
        n[k++] = 'H'; n[k++] = 'Z'; n[k] = 0; say(i == 3 ? "DELAY LOWPASS" : "DELAY HIGH-PASS", n);
    }
    else { num(n, v * 100 / 127); while (n[k]) k++; n[k++] = '%'; n[k] = 0; say(i == 1 ? "DELAY FEEDBACK" : i == 4 ? "DELAY SEND" : "DELAY WET", n); }
}
static void master_leds(uint8_t *led) { int t = pv(PG_MASTER, 0); for (int k = 0; k < 16; k++) led[k] = (uint8_t)(k + 1 == t ? 2 : k < t ? 1 : 0); }
#endif
