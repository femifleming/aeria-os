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

/* balloons */
enum { B_EMPTY, B_FILLING, B_FLOATING, B_POPPING };
typedef struct { uint8_t state, track, key; int16_t size, x, y, pop; } Balloon;
static Balloon balloon[3] = {{B_EMPTY, 1, KEY_REC_AB, 0, 24, 0, 0}, {B_EMPTY, 2, KEY_REC_CD, 0, 64, 0, 0}, {B_EMPTY, 3, KEY_REC3, 0, 104, 0, 0}};
static int balloon_voice;
static uint8_t balloon_strt;

static int algo_track(int a) { return a == ALGO_GRAINS ? T_GRAIN : a == ALGO_STARS ? T_STARS : a == ALGO_PLAYER ? T_PLAYER : a == ALGO_BALLOON ? T_BALLOON : -1; }

static void sound_tick(void)
{
    /* the chain: each knob glides to its weather (and toward the crossfader's) */
    int xf = pv(PG_SKY, 5) ? xfade() : -1;
    for (int i = 0; i < N_FX; i++)
        for (int k = 0; k < 6; k++) {
            if (fx_user[i][k] >= 0) continue;
            int want = want_of(weather, i, k);
            if (xf > 0) { int b = want_of(xf_right, i, k); if (b >= 0) want = (want >= 0 ? want : fx_val[i][k]) + ((b - (want >= 0 ? want : fx_val[i][k])) * xf / 127); }
            if (i == 0 && k == 1 && thunder && want >= 0) want = want * (256 - thunder) / 256;
            if (want < 0) continue;
            int v = fx_val[i][k];
            fx_val[i][k] = (int16_t)(v < want ? v + 1 : v > want ? v - 1 : want);
        }
    for (int i = 0; i < N_FX; i++)
        for (int k = 0; k < 6; k++) {
            if (!fx[i].knob[k][0]) continue;
            int v = ((bypass >> i) & 1) ? fx[i].neutral[k] : fx_value(i, k);
            lane_put(fx_track(i), fx_slot(i, k), clamp(v, 0, fx[i].hi[k]));
        }
    /* the inputs: T5 THRU, MAIN slots 0 INAB, 1 VOL, 3 INCD, 4 VOL (64 = unity) */
    lane_put(CHAIN_T, 0, pv(PG_MIX, 0));
    lane_put(CHAIN_T, 3, pv(PG_MIX, 1));
    lane_put(CHAIN_T, 1, pv(PG_MIX, 2));
    lane_put(CHAIN_T, 4, pv(PG_MIX, 2));
    /* levels along the chain and out of it */
    lane_put(CHAIN_T + 0, 15, pv(PG_MIX, 6));
    lane_put(CHAIN_T + 1, 15, pv(PG_MIX, 7));
    lane_put(CHAIN_T + 2, 15, pv(PG_MIX, 8));
    lane_put(CHAIN_T + 3, 15, muted[CHAIN_T + 3] ? 0 : pv(PG_MIX, 3));
    lane_put(CHAIN_T + 3, 16, pv(PG_MIX, 9));
    lane_put(CHAIN_T + 3, 17, pv(PG_MIX, 5));
    /* the algorithms: only the chosen one sounds */
    if (!pv(PG_GRAIN, 9)) {
        static uint32_t frac;
        int wind = pv(PG_SKY, 0) - 9 + (gust ? (pv(PG_SKY, 0) >= 9 ? 9 : -9) : 0);
        frac += (uint32_t)(iabs(wind) * pv(PG_GRAIN, 8) / 16);
        if (frac >= 256) {
            frac -= 256;
            Param *pos = &P(PG_GRAIN, 0);
            int b = pv(PG_GRAIN, 0) + (wind > 0 ? 1 : -1);
            b = b > 127 ? 0 : b < 0 ? 127 : b;
            pos->value = pos->user = (int16_t)b;
        }
    }
    for (int a = ALGO_GRAINS; a < N_ALGO; a++) {
        int t = algo_track(a), pg = a == ALGO_GRAINS ? PG_GRAIN : a == ALGO_STARS ? PG_STARS : a == ALGO_PLAYER ? PG_PLAYER : PG_BALLOON;
        int on = algo == a && !muted[t];
        if (a == ALGO_BALLOON) { int any = 0; for (int b = 0; b < 3; b++) any |= balloon[b].state == B_FLOATING; on = on && any; }
        lane_put(t, 15, on ? pv(PG_MIX, 4) : 0);
        if (algo != a) continue;
        for (int i = 0; i < 12; i++) {
            Param *p = &params[pg][i];
            if (p->mask) lane_put(t, p->slot, out_value(pg, i));
        }
    }
    if (algo == ALGO_BALLOON) {
        int every = 2 + (127 - pv(PG_BALLOON, 0)) / 16;
        if (ticks % (uint32_t)every == 0) balloon_strt = (uint8_t)clamp(pv(PG_BALLOON, 4) + (pv(PG_BALLOON, 1) ? range(-pv(PG_BALLOON, 1) / 2, pv(PG_BALLOON, 1) / 2) : 0), 0, 120);
        lane_put(T_BALLOON, 1, balloon_strt);
    }
}

