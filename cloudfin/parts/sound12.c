/* ---- sound ------------------------------------------------------------------------------- */
static void drift_tick(void)
{
    if ((ticks & 7) != 0) return;
    for (int pg = 0; pg < N_PAGES; pg++)
        for (int i = 0; i < 6; i++) {
            if (!params[pg][i].drift) continue;
            if ((rnd() % (uint32_t)(20 + 400 / (1 + pv(PG_SKY, 9)))) == 0) drift_target[pg][i] = (int16_t)range(-64, 64);
            drift_now[pg][i] = (int16_t)(drift_now[pg][i] + (drift_target[pg][i] > drift_now[pg][i]) - (drift_target[pg][i] < drift_now[pg][i]));
        }
}
static int out_value(int pg, int i)
{
    Param *p = &params[pg][i];
    int v = pv(pg, i);
    if (p->drift && i < 6) v += drift_now[pg][i] * pv(PG_SKY, 8) / 160;
    return clamp(v, p->lo, p->hi);
}

/* balloons: caught by the recorders of T2-T4 (REC AB, CD, SRC3), sung by T1 */
enum { B_EMPTY, B_FILLING, B_FLOATING, B_POPPING };
typedef struct { uint8_t state, track, key; int16_t size, x, y, pop; } Balloon;
static Balloon balloon[3] = {{B_EMPTY, 1, KEY_REC_AB, 0, 24, 0, 0}, {B_EMPTY, 2, KEY_REC_CD, 0, 64, 0, 0}, {B_EMPTY, 3, KEY_REC3, 0, 104, 0, 0}};
static int balloon_voice;
static uint8_t balloon_quiet;   /* bit b: balloon b floats but does not sing (BALLOON page, trigs 1-3) */
static uint8_t balloon_strt;
#if defined(PREVIEW) && defined(AERIAOS)
void aos_balloon(int b, int state);
void aos_balloon_at(int step, int len, int b);
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level);
#define AOS_BALLOON(b, st) aos_balloon(b, st)
#else
#define AOS_BALLOON(b, st) ((void)0)
#endif
static int any_floating(void) { for (int b = 0; b < 3; b++) if (balloon[b].state == B_FLOATING) return 1; return 0; }

static void weather_fresh(void);
/* this weather alone (the knob, the up and down keys, the weather's own turning) */
static void set_weather(int w)
{
    if (w == weather && wx_on == (w == CLEAR ? 0 : 1u << w)) return;
    weather = w; wx_on = (uint8_t)(w == CLEAR ? 0 : 1u << w);
    weather_fresh();
}
/* one weather on or off, the others as they are (SKY: the trigs); SNOW and HEAT put each other out */
static void toggle_weather(int w)
{
    if (w <= CLEAR || w >= N_WEATHER) { set_weather(CLEAR); return; }
    wx_on ^= (uint8_t)(1u << w);
    if (WON(w)) { if (w == SNOW) wx_on &= (uint8_t)~(1u << HEAT); if (w == HEAT) wx_on &= (uint8_t)~(1u << SNOW); weather = w; }
    else { weather = CLEAR; for (int i = N_WEATHER - 1; i > CLEAR; i--) if (WON(i)) { weather = i; break; } }
    weather_fresh();
    char line[24]; int n = 0;
    for (int i = 1; i < N_WEATHER && n < 18; i++) if (WON(i)) { const char *p = weather_name[i]; if (n) line[n++] = '+'; for (int k = 0; k < 3 && p[k]; k++) line[n++] = p[k]; }
    line[n] = 0; say(n ? line : "CLEAR", 0);
}
static void weather_fresh(void)
{
    int w = weather;
    amount = 0;
    spell_left = (uint32_t)range(spell_s[w][0], spell_s[w][1]) * 60u * (uint32_t)pv(PG_SKY, 7) / 4u;
    P(PG_SKY, 3).value = P(PG_SKY, 3).user = (int16_t)w;
    /* a new weather takes the knobs it moves back from the musician -- but never an effect's MIX: how much of an
     * effect is heard is the musician's alone (the weather used to bring every effect in by itself: 12 Nov 2026) */
    for (int s = 0; s < N_SLOTS; s++) for (int k = 0; k < 6; k++) if (rule_of(slots[s].knob[k]) >= 0 && !knob_is_mix(slots[s].knob[k])) slots[s].user[k] = -1;
    say(weather_name[w], 0);
}


/* The stock engine passes THRU/NEIGHBOR audio only after a trig: every chain and source track
 * keeps one on the pattern's first step, its AMP envelope held fully open (HOLD and REL 127). */
static void keep_alive(void)
{
    if (pat_off <= -99 || (ticks % 60u) != 0) return;
    static const uint8_t held[] = {SAMPLE_T, HEAD_T, HEAD_T + 1, HEAD_T + 2, HEAD_T + 3};
    for (unsigned i = 0; i < sizeof held; i++) {
        int t = held[i];
        pat_trig(t, 0, 1);
        lane_put(t, 13, 127);
        lane_put(t, 14, 127);
    }
}
#if defined(PREVIEW) && defined(AERIAOS)
int aos_src_loop(void) { return src_loop; }
#endif
static void sound_tick(void)
{
    keep_alive();
    slots_sound();
#if defined(PREVIEW) && defined(AERIAOS)
    /* the balloons' long grains (aeria-os sampler.c): each floating balloon sings, unless trig 1-3 quieted it */
    for (int b = 0; b < 3; b++)
        aos_balloon_cloud(b, src_balloons && balloon[b].state == B_FLOATING && !((balloon_quiet >> b) & 1),
                          pv(PG_BALLOON, 0), pv(PG_BALLOON, 3), pv(PG_BALLOON, 4), pv(PG_BALLOON, 1),
                          pv(PG_BALLOON, 2) - 64 + 12 * (pv(PG_BALLOON, 6) - 3) + pv(PG_BALLOON, 9 + b) - 25,
                          pv(PG_BALLOON, 7) - 1, pv(PG_BALLOON, 8) - 1, pv(PG_BALLOON, 5));
#endif
    /* HEAD on T3 FX1: FDBK FTON SMPL STAR IN WARM (the order its manifest gives) */
    int smpl = src_loop ? pv(PG_LOOP, 4) : (src_balloons && any_floating()) ? pv(PG_BALLOON, 5) : 0;
    int star = src_stars ? pv(PG_STARS, 11) : 0;
    lane_put(HEAD_T, 18, 0);                 /* no main feedback (12 Nov 2026) */
    lane_put(HEAD_T, 19, 90);
    lane_put(HEAD_T, 20, smpl);
    lane_put(HEAD_T, 21, star);
    lane_put(HEAD_T, 22, pv(PG_GAIN, 2));
    lane_put(HEAD_T, 23, 0);                 /* no soft clip */
    /* the inputs: T3 THRU, MAIN slots 0 INAB, 1 VOL, 3 INCD, 4 VOL (64 = unity) */
    lane_put(HEAD_T, 0, pv(PG_MIX, 6));
    lane_put(HEAD_T, 3, pv(PG_MIX, 7));
    lane_put(HEAD_T, 1, pv(PG_GAIN, 0));
    lane_put(HEAD_T, 4, pv(PG_GAIN, 1));
    /* along the chain at unity, out of it at OUT */
    for (int t = HEAD_T; t < LAST_T; t++) lane_put(t, 15, 108);
    lane_put(LAST_T, 15, muted[LAST_T] ? 0 : pv(PG_GAIN, 3));
    lane_put(LAST_T, 16, pv(PG_GAIN, 5));
    lane_put(LAST_T, 17, pv(PG_GAIN, 4));
    /* the sources play only into HEAD (their own FX1 is TAP: silent out) */
    if (src_loop) {
        int strt = pv(PG_LOOP, 0), end = pv(PG_LOOP, 1);
        if (end <= strt) end = strt + 1;
        lane_put(SAMPLE_T, 1, strt);
        lane_put(SAMPLE_T, 2, clamp(end - strt, 1, 127));
        lane_put(SAMPLE_T, 0, pv(PG_LOOP, 2));
        lane_put(SAMPLE_T, 3, pv(PG_LOOP, 3));
        lane_put(SAMPLE_T, 12, pv(PG_LOOP, 5));
        lane_put(SAMPLE_T, 13, 127);
        lane_put(SAMPLE_T, 14, 127);
        lane_put(SAMPLE_T, 15, 108);
    } else if (src_balloons && any_floating()) {
        int every = 2 + (127 - pv(PG_BALLOON, 0)) / 16;
        if (ticks % (uint32_t)every == 0) balloon_strt = (uint8_t)clamp(pv(PG_BALLOON, 4) + (pv(PG_BALLOON, 1) ? range(-pv(PG_BALLOON, 1) / 2, pv(PG_BALLOON, 1) / 2) : 0), 0, 120);
        lane_put(SAMPLE_T, 1, balloon_strt);
        lane_put(SAMPLE_T, 0, pv(PG_BALLOON, 2));
        lane_put(SAMPLE_T, 4, pv(PG_BALLOON, 3));
        lane_put(SAMPLE_T, 5, pv(PG_BALLOON, 0));
        lane_put(SAMPLE_T, 15, 108);
    } else lane_put(SAMPLE_T, 15, 0);
    lane_put(STARS_T, 15, src_stars ? 108 : 0);
    algo = src_stars ? ALGO_STARS : ALGO_NONE;
}

/* balloons sing on T1: a trig a step, sample-locked to the balloon's recorder buffer
 * (Flex slots 128..135 are the recorders, 0-based), its start jittered inside the cloud */
static int balloon_placed = -1;
static void place_balloon(int step)
{
    if (!src_balloons || src_loop || !any_floating()) return;
    if (balloon[balloon_voice].state != B_FLOATING) for (int b = 0; b < 3; b++) if (balloon[b].state == B_FLOATING) balloon_voice = b;
    pat_lock(SAMPLE_T, step, 31, 128 + balloon[balloon_voice].track);
    pat_lock(SAMPLE_T, step, 1, balloon_strt);
    pat_trig(SAMPLE_T, step, 1);
#if defined(PREVIEW) && defined(AERIAOS)
    aos_balloon_at(step, pat_len(SAMPLE_T), balloon_voice);
#endif
    balloon_placed = step;
}
