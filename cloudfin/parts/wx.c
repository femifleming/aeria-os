/* ---- the weather as sound (AERIA OS, sadnoise, 10 Nov 2026), after Pinkie Pie's Balloon Shop (Weather.h) ---------
 * On the chain's output, after the main feedback's tap (the engine's wthr):
 *   RAIN, STORM  a 2-pole lowpass eases in to about 6 kHz over a few seconds (and out again)
 *   CLOUDY, STORM  thunder: now and then the lowpass is pulled down and rolls back up; every lightning strike does too
 *   HELIUM (MIX, FUNC layer)  a 2-pole high-pass up to about 200 Hz
 *   STORM  each strike lends a random effect the chain does not have to an empty slot for 45-90 s
 * WSND (MIX) turns the weather's sound off (the knob weather stays). */
#if defined(AERIAOS)
void aos_weather_filters(int lp, int hp);   /* cutoff indices 0..127 (20 Hz .. 20 kHz, log); -1 off */
static int wx_lp = 127 << 8, wx_hp = 0, wx_roll;
static int16_t lend_row[N_SLOTS];        /* what the lightning lent each slot (0: nothing) */
static uint32_t lend_until[N_SLOTS], lend_at[N_SLOTS], roll_next;
static uint8_t wx_struck;                /* a strike the lending has not answered yet (strike() sets it) */
static void wx_tick(void)
{
    /* WSND off: no filters, no thunder in the sound -- but the lightning still lends (that is the chain, not the weather's own sound) */
    int xlp, xhp; xf_filters(&xlp, &xhp);                          /* the crossfader as a DJ filter (master.c) */
    if (!pv(PG_MIX, 5)) { aos_weather_filters(xlp >= 126 ? -1 : xlp, xhp <= 1 ? -1 : xhp); wx_lp = 127 << 8; wx_hp = 0; goto lends; }
    int rain = WON(RAIN) || WON(STORM);
    int lp_t = rain ? 105 : 127;                                   /* index 105: about 6 kHz */
    /* thunder: the strikes (thunder 200 falling) and, under clouds, rolls of their own */
    if ((WON(CLOUDY) || WON(STORM)) && ticks > roll_next) { wx_roll = 120 + (int)(rnd() % 80u); roll_next = ticks + 61u * (uint32_t)range(15, 50); }
    int dip = thunder > wx_roll ? thunder : wx_roll;
    if (wx_roll) wx_roll = wx_roll > 2 ? wx_roll - 2 : 0;
    if (dip) lp_t = lp_t - dip * 60 / 200;                         /* down to about 700 Hz at a strike */
    wx_lp += (lp_t * 256 - wx_lp) / (dip ? 6 : 120);               /* thunder bites fast, rain eases */
    int hp_t = pv(PG_MIX, 4) ? 42 : 0;                             /* index 42: about 200 Hz */
    wx_hp += (hp_t * 256 - wx_hp) / 90;
    int lp = wx_lp >> 8, hp = wx_hp >> 8;
    if (xlp < lp) lp = xlp;
    if (xhp > hp) hp = xhp;
    { aos_weather_filters(lp >= 126 ? -1 : lp, hp <= 1 ? -1 : hp); }
lends:
    /* lightning lends effects, as in Balloon Shop: every strike of a storm drops an effect the chain has not got
     * into an empty slot for 45 to 90 s, and it goes again when its time is up (unless the musician kept it by
     * turning one of its knobs) -- several at once, so in a storm effects keep dropping in and out */
    /* a loan that found no room (the loader said so) frees its slot for the next strike */
    for (int sl = 0; sl < N_SLOTS; sl++) if (lend_row[sl] && picking < 0 && slots[sl].row != lend_row[sl] && ticks > lend_at[sl] + 244u) lend_row[sl] = 0;
    for (int sl = 0; sl < N_SLOTS; sl++) if (lend_row[sl] && ticks > lend_until[sl] && picking < 0) {
        int held = 0;
        for (int k = 0; k < 6; k++) if (slots[sl].user[k] >= 0) held = 1;
        if (slots[sl].row == lend_row[sl] && !held) { slot_pick(sl, 0); say("STORM TAKES", fx_info[lend_row[sl] - 1].name + 6); }
        lend_row[sl] = 0;
    }
    if (!WON(STORM)) wx_struck = 0;
    if (WON(STORM) && wx_struck && picking < 0) {
        wx_struck = 0;
        int empty = -1;
        for (int sl = N_SLOTS - 1; sl >= 0; sl--) if (slots[sl].row <= 0 && !lend_row[sl]) { empty = sl; break; }
        int cand[FX_COUNT], n = 0;
        for (int r = 1; r <= FX_COUNT; r++) {
            int have = 0; const char *nm = fx_info[r - 1].name;
            for (int sl = 0; sl < N_SLOTS; sl++) if (slots[sl].row == r) have = 1;
            if (name_has(nm, "WIND") || name_has(nm, "FOG") || name_has(nm, "TWIS") || name_has(nm, "HAIL") || name_has(nm, "AURO") || name_has(nm, "WARM")) continue;   /* their own weathers bring those */
            if (!have && !fx_info[r - 1].has_y && !fx_banned(nm)) cand[n++] = r;
        }
        if (empty >= 0 && n) {
            lend_row[empty] = (int16_t)cand[rnd() % (uint32_t)n];
            lend_until[empty] = ticks + 61u * (uint32_t)range(45, 90); lend_at[empty] = ticks;
            slot_pick(empty, lend_row[empty]);
            say("LIGHTNING LENDS", fx_info[lend_row[empty] - 1].name + 6);
        }
    }
}

/* ---- more weather (Balloon Shop 1.2; AERIA OS, 12 Nov 2026): weathers that are a sound of their own ---------------
 * WIND (a drifting pitch wobble, gusts, a lean), HAIL (stones of the sound itself bouncing), AURORA (an octave up
 * into four slow curtains of reverb), the TWISTER (one event: the sound climbs a fifth and spins) -- and FOG now
 * blurs and darkens. Each is an effect of its own (aeria-fx wind / fog / twister / hail / aurora) that its weather
 * puts into an empty slot of the chain while it lasts, its AMT faded in and out, and takes away again. WIND and FOG
 * are never on together (the later one stays), nor HAIL and AURORA. With WIND and CLOUDY a twister may come by
 * itself, about every two and a half minutes. An effect the musician put there by hand is left alone.
 * SKY page: trig 9 WIND, 10 HAIL, 11 AURORA, 12 TWISTER. */
enum { XW_WIND, XW_HAIL, XW_AURORA, XW_N };
enum { XF_WIND, XF_FOG, XF_TWISTER, XF_HAIL, XF_AURORA, XF_N };
static uint8_t xw_on;                    /* a bit a weather of the three */
static const char *const xf_name[XF_N] = {"AERIA WIND", "AERIA FOG", "AERIA TWISTER", "AERIA HAIL", "AERIA AURORA"};
static const uint16_t xf_tail[XF_N] = {30, 30, 30, 120, 720};   /* ticks it is left ringing after its fade (hail's stones, the aurora's tail) */
static int8_t xf_slot[XF_N] = {-1, -1, -1, -1, -1};
static uint8_t xf_amt[XF_N], xf_row[XF_N], xf_ready, xf_fail[XF_N], xf_set[XF_N];
static uint16_t xf_gone[XF_N];
static uint32_t tw_until, tw_roll;
static int xf_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void xw_toggle(int w)
{
    xw_on ^= (uint8_t)(1u << w);
    int on = (xw_on >> w) & 1;
    if (on && w == XW_WIND && WON(FOG)) toggle_weather(FOG);          /* wind blows the fog away */
    if (on && w == XW_HAIL) xw_on &= (uint8_t)~(1u << XW_AURORA);
    if (on && w == XW_AURORA) xw_on &= (uint8_t)~(1u << XW_HAIL);
    say(w == XW_WIND ? "WIND" : w == XW_HAIL ? "HAIL" : "AURORA", on ? "ON" : "OFF");
}
/* AUTO's share: with a new spell of weather the wind, the hail or the aurora may come along (after Balloon Shop's
 * auto weather: wind out of a clear or cloudy sky, hail only out of a storm, the aurora on a clear night) */
static void xw_auto(int w)
{
    uint32_t r = rnd() % 100u;
    xw_on = 0;
    if (w == CLEAR) { if (r < 15) xw_on = 1u << XW_WIND; else if (r < 27) xw_on = 1u << XW_AURORA; }
    else if (w == CLOUDY || w == RAIN) { if (r < 12) xw_on = 1u << XW_WIND; }
    else if (w == STORM) { if (r < 25) xw_on = 1u << XW_HAIL; else if (r < 45) xw_on = 1u << XW_WIND; }
}
static void xw_twister(void) { if (ticks > tw_until) { tw_until = ticks + 61u * 24u; say("TWISTER", 0); } }
static void xw_tick(void)
{
    if (!xf_ready) {
        if (ticks < 90) return;
        for (int i = 0; i < XF_N; i++) for (int r = 1; r <= FX_COUNT; r++) if (xf_same(fx_info[r - 1].name, xf_name[i])) xf_row[i] = (uint8_t)r;
        /* what a weather left in the chain when the power went: its again, to fade and take away */
        for (int i = 0; i < XF_N; i++) for (int s = 0; s < N_SLOTS; s++) if (xf_row[i] && slots[s].row == xf_row[i]) { xf_slot[i] = (int8_t)s; xf_amt[i] = (uint8_t)clamp(slots[s].user[0], 0, 127); xf_set[i] = 1; }
        xf_ready = 1;
    }
    static uint8_t fog_was;
    if (WON(FOG) && !fog_was) xw_on &= (uint8_t)~(1u << XW_WIND);     /* ... and fog stills the wind */
    fog_was = (uint8_t)WON(FOG);
    int sound = pv(PG_MIX, 5) != 0;                                    /* WSND: the weather's sound */
    if (sound && (xw_on & 1) && WON(CLOUDY) && ticks > tw_roll) { tw_roll = ticks + 61u; if (rnd() % 150u == 0) xw_twister(); }
    int want[XF_N] = {sound && (xw_on >> XW_WIND) & 1, sound && WON(FOG), sound && ticks < tw_until, sound && (xw_on >> XW_HAIL) & 1, sound && (xw_on >> XW_AURORA) & 1};
    for (int i = 0; i < XF_N; i++) {
        int s = xf_slot[i], row = xf_row[i];
        if (!row) continue;
        if (!want[i]) xf_fail[i] = 0;                                   /* asked again only when the weather comes again */
        if (s >= 0 && slots[s].pending >= 0) continue;                  /* still being put in */
        if (s >= 0 && slots[s].row != row) { if (!xf_set[i]) xf_fail[i] = 1; s = xf_slot[i] = -1; xf_amt[i] = 0; }   /* it did not fit (said once), or the musician took the slot */
        if (s < 0) {
            if (!want[i] || picking >= 0 || xf_fail[i]) continue;
            int have = 0, empty = -1;
            for (int k = 0; k < N_SLOTS; k++) { if (slots[k].row == row) have = 1; if (slots[k].row <= 0 && !lend_row[k]) empty = k; }
            if (have || empty < 0) continue;                            /* there by hand already, or no room: the sky still shows it */
            slot_pick(empty, row);
            if (slots[empty].row != row && slots[empty].pending != row) continue;   /* the DSP had no room for it */
            xf_slot[i] = (int8_t)empty; xf_amt[i] = 0; xf_gone[i] = 0; xf_set[i] = 0;
            continue;
        }
        if (!xf_set[i]) {                                               /* in: its own knobs as its maker set them, not as the sky would */
            for (int k = 1; k < 6; k++) slots[s].user[k] = slots[s].value[k] = fx_info[row - 1].def[k];
            xf_set[i] = 1;
        }
        int target = want[i] ? 127 : 0;
        if (i == XF_TWISTER) target = want[i] && tw_until - ticks > 61u * 22u ? 127 : 0;   /* its knob is a trigger: up for the first two seconds */
        if (xf_amt[i] < target) xf_amt[i] = (uint8_t)clamp(xf_amt[i] + 6, 0, 127); else if (xf_amt[i] > target) xf_amt[i] = (uint8_t)clamp(xf_amt[i] - 6, 0, 127);
        slots[s].user[0] = xf_amt[i]; slots[s].value[0] = xf_amt[i];
        if (!want[i] && !xf_amt[i]) { if (++xf_gone[i] > xf_tail[i] && picking < 0) { slot_pick(s, 0); xf_slot[i] = -1; } } else xf_gone[i] = 0;
    }
}
/* the sky's share: wind streaks, hailstones, the aurora's curtains, the twister's funnel */
static void xw_draw(void)
{
    int t = (int)ticks;
    if (xw_on & (1u << XW_WIND)) for (int i = 0; i < 7; i++) {
        uint32_t h = (uint32_t)i * 2654435761u; int y = 12 + (int)((h >> 8) % 30u), len = 6 + (int)((h >> 16) % 10u);
        int x = (int)((h % 160u) + (uint32_t)t * (2u + (uint32_t)(i & 1))) % 170 - 20;
        for (int k = 0; k < len; k++) if ((k + t / 2) % 4) px(x + k, y + (sine[(x + k + t) & 63] > 180 ? 1 : 0));
    }
    if (xw_on & (1u << XW_HAIL)) for (int i = 0; i < 10; i++) {
        uint32_t h = (uint32_t)(i + 3) * 40503u * 2654435761u; int per = 40 + (int)((h >> 20) % 30u), ph = (t + (int)(h % 97u)) % per;
        int x = (int)((h >> 8) % (uint32_t)W), fall = WATER_Y - 12, y;
        if (ph < per / 2) y = 10 + ph * fall * 2 / per;                  /* down */
        else { int b = ph - per / 2, hgt = 8 - b * 16 / per; y = WATER_Y - 2 - (hgt > 0 ? hgt : 0) * (per - ph) * 2 / per; }   /* one small bounce */
        px(x, y); px(x + 1, y); px(x, y + 1); px(x + 1, y + 1);
    }
    if (xw_on & (1u << XW_AURORA)) for (int c = 0; c < 4; c++) {
        int cx = 16 + c * 32 + sine[(t / 3 + c * 17) & 63] / 40, lv = 3 + sine[(t / 5 + c * 23) & 63] / 50;
        for (int x = cx - 9; x <= cx + 9; x++) { int top = 9 + sine[(x * 5 + t / 2 + c * 9) & 63] / 60, len = 12 + sine[(x * 3 - t / 4 + c * 31) & 63] / 28;
            for (int y = top; y < top + len && y < WATER_Y - 8; y++) if (dither_on(x, y, clamp(lv - (y - top) * lv / (len + 1), 1, 8))) px(x, y); }
    }
    if (ticks < tw_until) {
        int u = (int)(tw_until - ticks), x0 = W + 10 - (61 * 24 - u) * (W + 30) / (61 * 24);
        for (int y = 12; y < WATER_Y; y += 1) { int wdt = 2 + (WATER_Y - y) * 9 / (WATER_Y - 12), sway = sine[(y * 4 + t * 5) & 63] / 40 - 3;
            int a = x0 + sway - wdt, b = x0 + sway + wdt; px(a, y); px(b, y); if (((y + t / 2) & 3) == 0) for (int x = a + 1; x < b; x += 2) px(x, y); }
    }
}
#endif
