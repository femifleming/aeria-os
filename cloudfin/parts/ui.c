static int layer(void) { return func_held || func_latch; }
static void label(int x, const char *s) { for (int i = x - 2; i < x + text_w(s) + 2; i++) span(i, 0, 8, 0); text(x, 2, s, 1); }
static void panel_clear(int y0, int y1) { for (int x = 0; x < W; x++) span(x, y0, y1, 0); }

/* the chain: eight balloons on one string, the signal flowing left to right */
static void draw_chain(void)
{
    panel_clear(10, 47);
    int y = 16;
    for (int x = 2; x < W - 2; x++) if (((x + (int)(ticks >> 2)) & 3) == 0) px(x, y); /* the signal on the string */
    for (int i = 0; i < N_FX; i++) {
        int cx = 9 + i * 15, bob = sine[(int)(ticks + (uint32_t)i * 9) & 63] / 64, r = i == sel_fx ? 5 : 4;
        int cy = y + bob;
        disk_clear(cx, cy, r + 1);
        if ((bypass >> i) & 1) { for (int a = 0; a < 1024; a += 64) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
        else {
            circle(cx, cy, r);
            /* how hard it works: the wet knob fills it */
            int k = fx[i].knob[5][0] ? 5 : 0, fill = fx_value(i, k) * (2 * r) / 128;
            for (int dy = r - fill; dy <= r; dy++) for (int dx = -r + 1; dx < r; dx++) if (dx * dx + dy * dy < r * r && dither_on(cx + dx, cy + dy, 8)) px(cx + dx, cy + dy);
        }
        char l[2] = {fx_letter[i], 0};
        if (i == sel_fx) { for (int dx = -2; dx <= 2; dx++) px(cx + dx, cy + r + 3); }
        for (int dy = -3; dy <= 3; dy++) for (int dx = -2; dx <= 2; dx++) px_off(cx + dx, cy + dy);
        text(cx - 1, cy - 2, l, 1);
    }
    /* the selected effect's six knobs */
    const Fx *f = &fx[sel_fx];
    for (int k = 0; k < 6; k++) {
        if (!f->knob[k][0]) continue;
        bubble(11 + k * 21, 33, 7, fx_value(sel_fx, k) * 127 / (f->hi[k] ? f->hi[k] : 127), f->knob[k], fx_user[sel_fx][k] < 0 && want_of(weather, sel_fx, k) >= 0);
    }
}

static void draw_algo(void)
{
    panel_clear(10, 47);
    for (int a = 0; a < N_ALGO; a++) {
        int y = 12 + a * 7;
        if (a == algo) { for (int x = 30; x < 98; x++) span(x, y - 1, y + 5, 1); text(34, y, algo_name[a], 0); }
        else text(34, y, algo_name[a], 1);
        char n[2] = {(char)('1' + a), 0};
        text(24, y, n, 1);
    }
}

static void draw_system(void)
{
    panel_clear(10, 56);
    text(4, 12, "USB DISK OR OS UPGRADE:", 1);
    text(4, 19, "FUNC+NO  THEN  PROJECT", 1);
    text(4, 26, "SYSTEM  ON THE OCTATRACK", 1);
    text(4, 35, "A SOUND", 1); text(52, 35, pv(PG_SYSTEM, 0) == 0 ? "OFF" : pv(PG_SYSTEM, 0) == 1 ? "KNOBS" : "LANE", 1);
    text(4, 42, "B INTRO", 1); text(52, 42, pv(PG_SYSTEM, 1) ? "ON" : "OFF", 1);
    text(4, 49, "C LIGHTS", 1); text(52, 49, pv(PG_SYSTEM, 2) ? "ON" : "OFF", 1);
}

static int page_param_pages(int pg) { return pg == PG_SKY || pg == PG_GRAIN || pg == PG_STARS || pg == PG_PLAYER || pg == PG_BALLOON || pg == PG_MIX; }
static void draw_page(void)
{
    if (page == PG_CHAIN) { draw_chain(); return; }
    if (page == PG_ALGO) { draw_algo(); return; }
    if (page == PG_SYSTEM) { draw_system(); return; }
    if (!page_param_pages(page)) return;
    if (ticks > (uint32_t)page_until && !func_held && page != PG_MIX) return;
    panel_clear(22, 47);
    for (int x = 0; x < W; x += 2) { px(x, 22); px(x, 47); }
    int row = layer() ? 6 : 0;
    for (int i = 0; i < 6; i++) {
        Param *p = &params[page][row + i];
        if (!p->name[0]) continue;
        int sv = p->hi - p->lo ? p->hi - p->lo : 1;
        bubble(11 + i * 21, 32, 7, (out_value(page, row + i) - p->lo) * 127 / sv, p->name, 0);
    }
    if (row) text(W / 2 - 7, 24, "FUNC", 1);
}

static void draw_hud(void)
{
    label(2, page == PG_CHAIN ? fx[sel_fx].name : page_name[page]);
    const char *w = weather_name[weather];
    label(W - 2 - text_w(w) - (auto_weather ? 8 : 0), w);
    if (auto_weather) label(W - 5, "A");
    if (algo) { const char *a = algo_name[algo]; for (int i = 0; i < text_w(a) + 3; i++) span(i, 57, 63, 0); text(1, 58, a, 1); }
    if (pv(PG_SYSTEM, 0) != 1) { const char *m = pv(PG_SYSTEM, 0) == 0 ? "SND OFF" : "LANE"; for (int i = W - text_w(m) - 3; i < W; i++) span(i, 57, 63, 0); text(W - text_w(m) - 1, 58, m, 1); }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 48, 56, 1);
        text(x, 50, toast, 0);
    }
}

/* ---- LEDs: trig k red 2k, green 2k+1; tempo 38/39 ------------------------------------------------- */
static uint8_t led_want[16];
static void leds_out(void)
{
    for (int k = 0; k < 16; k++) { if (led_want[k] & 1) set_led(2 * k, 3); if (led_want[k] & 2) set_led(2 * k + 1, 3); }
}
static void weather_leds(int first)
{
    led_want[first + weather] = 2;
    if (scene_held) led_want[first + (scene_held == 1 ? weather : xf_right)] = 3;
    if (first + 7 < 16) led_want[first + 7] = auto_weather ? 2 : (uint8_t)((ticks >> 5) & 1);
}
static void leds_tick(void)
{
    for (int k = 0; k < 16; k++) led_want[k] = 0;
    if (intro < INTRO_LEN) {
        int t = intro;
        if (t < 90) led_want[clamp(t * 16 / 90, 0, 15)] = 2;
        else if (t < 168) { for (int k = 0; k <= (t - 95) / 4 && k < 11; k++) led_want[k] = 1; }
        else if (t < 176) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 3); set_led(39, 3); }
        else for (int k = 0; k < 16; k++) if ((k * 7 + t / 3) % 5 == 0) led_want[k] = 2;
        leds_out();
        return;
    }
    if (!pv(PG_SYSTEM, 2)) return;
    if (page == PG_CHAIN) {
        /* 1-8 the effects: green the one on the knobs, red working, dark bypassed; 9-16 the weather */
        for (int i = 0; i < N_FX; i++) led_want[i] = i == sel_fx ? 2 : ((bypass >> i) & 1) ? 0 : 1;
        if (thunder > 150) for (int i = 0; i < N_FX; i++) led_want[i] = 3;
        weather_leds(8);
    } else if (page == PG_ALGO) {
        for (int a = 0; a < N_ALGO; a++) led_want[a] = a == algo ? 2 : 1;
    } else if (page == PG_GRAIN) {
        led_want[clamp(pv(PG_GRAIN, 0) * 16 / 128, 0, 15)] = 3;
    } else if (page == PG_STARS) {
        for (int p = 0; p < PLANETS; p++) if ((planet_mask >> p) & 1) led_want[p] = 1;
        if (planet_hit >= 0 && star_flash) led_want[planet_hit] = 2;
    } else if (page == PG_PLAYER) {
        for (int k = 0; k < 16; k += 4) led_want[k] = 1;
    } else if (page == PG_BALLOON) {
        for (int b = 0; b < 3; b++) led_want[b] = balloon[b].state == B_FLOATING ? (b == balloon_voice ? 2 : 1) : (balloon[b].state == B_FILLING && (ticks & 8)) ? 1 : 0;
    } else weather_leds(0);
    if (gust) for (int k = 0; k < 16; k++) if (((int)(ticks >> 1) - k) % 16 == 0) led_want[k] = 3;
    if (flash) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 2); set_led(39, 2); }
    leds_out();
}

/* ---- diagnostics, cycling every 2 s: tag in the top nibble ------------------------------------ */
static uint32_t stars_total;
static uint32_t diag_word(void)
{
    if (((ticks / 120u) & 1) == 0)
        return 0x10000000u | (uint32_t)playing() << 27 | (uint32_t)rig_loaded() << 26 | (uint32_t)((pat_off + 100) & 0x3f) << 20
               | (master_step() & 0xffu) << 12 | (stars_total & 0xffu) << 4 | (uint32_t)algo;
    return 0x30000000u | (uint32_t)page << 16 | (uint32_t)sel_fx << 8 | bypass;
}

/* ---- hooks --------------------------------------------------------------------------------------- */
static void strike(void) { bolt_x = range(16, W - 16); bolt_t = 9; flash = 3; thunder = 200; }

void module_tick(struct modwerk_runtime_api *api)
{
    if (!ready) { new_sky(); new_constellation(); fx_init(); spell_left = 3600u; ready = 1; if (!pv(PG_SYSTEM, 1)) intro = INTRO_LEN; }
    ticks++;
    if (intro < INTRO_LEN) intro++;
    if (amount < 256) amount += 2;
    if (flash) flash--;
    if (bolt_t) bolt_t--;
    if (star_flash) star_flash--;
    if (gust) gust--;
    if (thunder) thunder = thunder > 3 ? thunder - 3 : 0;
    night = pv(PG_SKY, 10);
    write_mode = pv(PG_SYSTEM, 0);
    step_auto();
    if (weather == STORM && amount > 128 && (rnd() % 420) == 0) strike();
    int want = clouds_wanted(), wind = wind_now();
    for (int i = 0; i < MAX_CLOUDS; i++) {
        Cloud *c = &clouds[i];
        c->x += wind * (16 + 24 * c->depth) + (wind == 0 ? 4 : 0);
        int x = c->x >> 8;
        if (x > W + 2 && wind >= 0) make_cloud(c, -c->w - range(0, i >= want ? 200 : 30));
        if (x < -CLOUD_MAX_W - 2 && wind < 0) make_cloud(c, W + range(0, i >= want ? 200 : 30));
    }
    move_precip();
    balloons_tick();
    birds_tick();
    drift_tick();
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -40 * 256;
    if ((fin_x >> 8) < -40) fin_x = (W + 20) * 256;
    if (!hidden) leds_tick();
    sequencer_tick();
    sound_tick();
    api->value = diag_word();
}

void module_draw(unsigned char *frame)
{
    if (hidden || !ready) return;
    F = (uint32_t *)frame;
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (intro < INTRO_LEN) { draw_intro(); return; }
    draw_sun_moon();
    int want = clouds_wanted();
    for (int depth = 1; depth <= 3; depth++) for (int i = 0; i < want; i++) if (clouds[i].depth == depth) draw_cloud(&clouds[i]);
    draw_heat();
    draw_fog();
    draw_precip();
    draw_bolt();
    draw_water();
    if (page == PG_STARS) draw_stars();
    if (algo == ALGO_BALLOON || page == PG_BALLOON) draw_balloons();
    draw_page();
    draw_hud();
    if (flash) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
}

static void show_page(int pg) { page = pg; page_until = (int)ticks + 240; say(pg == PG_CHAIN ? fx[sel_fx].name : page_name[pg], 0); }
static int algo_page(void) { return algo == ALGO_GRAINS ? PG_GRAIN : algo == ALGO_STARS ? PG_STARS : algo == ALGO_PLAYER ? PG_PLAYER : algo == ALGO_BALLOON ? PG_BALLOON : PG_ALGO; }
static void choose_weather(int w) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; set_weather(w); }
static void toggle_auto(void) { auto_weather = !auto_weather; P(PG_SKY, 4).value = P(PG_SKY, 4).user = (int16_t)auto_weather; say("AUTO", auto_weather ? "ON" : "OFF"); }
static void select_fx(int i, int toggle_bypass)
{
    if (toggle_bypass) { bypass ^= (uint8_t)(1u << i); say(fx[i].name, (bypass >> i) & 1 ? "OFF" : "ON"); }
    else { sel_fx = i; say(fx[i].name, (bypass >> i) & 1 ? "OFF" : 0); }
    page = PG_CHAIN;
}

int module_key(int code, int pressed)
{
    if (code == KEY_FUNC) { func_held = pressed; if (pressed) page_until = (int)ticks + 240; }
    if (code == KEY_NO && pressed && func_held) { hidden = !hidden; if (!hidden) show_page(page); return 1; }
    if (hidden) return 0;
    if (code == KEY_PLAY || code == KEY_STOP || code == KEY_REC || code == KEY_CUE || code == 0x18) return 0;
    if (code == KEY_SCENEA || code == KEY_SCENEB) { scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0; if (pressed) say("XF", weather_name[xf_right]); return 1; }
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (code != n->key) continue;
        if (func_held) { if (pressed && n->state != B_EMPTY && n->state != B_FILLING) { n->state = B_POPPING; n->pop = 12; say("POP", 0); } return 1; }
        set_current_track(n->track);
        if (pressed) {
            if (n->state == B_FILLING) { n->state = B_FLOATING; balloon_voice = b; say("BALLOON", b == 0 ? "1 UP" : b == 1 ? "2 UP" : "3 UP"); }
            else { n->state = B_FILLING; n->size = 0; n->y = WATER_Y - 12; say("CATCHING", b == 0 ? "A B" : b == 1 ? "C D" : "SKY"); }
        }
        return 0;
    }
    if (!pressed) return 1;
    if (intro < INTRO_LEN) { intro = INTRO_LEN; return 1; }

    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (scene_held && k % 8 < N_WEATHER) {
            int w = k % 8;
            if (scene_held == 2) { xf_right = w; P(PG_SKY, 6).value = P(PG_SKY, 6).user = (int16_t)w; say("XF B", weather_name[w]); }
            else choose_weather(w);
        } else if (page == PG_CHAIN) {
            if (k < 8) select_fx(k, func_held);
            else if (k < 15) choose_weather(k - 8);
            else toggle_auto();
        } else if (page == PG_ALGO) {
            if (k < N_ALGO) { algo = k; say("ALGO", algo_name[k]); if (k) show_page(algo_page()); }
        } else if (page == PG_GRAIN) { Param *p = &P(PG_GRAIN, 0); p->value = p->user = (int16_t)(k * 127 / 15); say("PLACE", 0); }
        else if (page == PG_STARS) { planet_mask ^= (uint16_t)(1u << k); say("PLANET", (planet_mask >> k) & 1 ? "ON" : "OFF"); }
        else if (page == PG_PLAYER) { pad_pending = k; }
        else if (page == PG_BALLOON) { if (k < 3 && balloon[k].state == B_FLOATING) { balloon_voice = k; say("SINGING", k == 0 ? "1" : k == 1 ? "2" : "3"); } else if (k >= 8) { Param *p = &P(PG_BALLOON, 4); p->value = p->user = (int16_t)((k - 8) * 127 / 7); } }
        else {
            if (k < 7) choose_weather(k);
            else if (k == 7) toggle_auto();
            else if (k == 15) { strike(); say("LIGHTNING", 0); }
        }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) { select_fx(code - KEY_TRACK0, func_held); return 1; }
    if (code == KEY_SRC) { show_page(PG_CHAIN); return 1; }
    if (code == KEY_AMP) { show_page(PG_SKY); return 1; }
    if (code == KEY_LFO) { show_page(page == PG_ALGO ? algo_page() : PG_ALGO); return 1; }
    if (code == KEY_FX1) { show_page(PG_MIX); return 1; }
    if (code == KEY_FX2 || code == KEY_PROJ) { show_page(PG_SYSTEM); return 1; }
    if (code == KEY_PART) { show_page(PG_BALLOON); return 1; }
    if (code == KEY_AED) { show_page(PG_PLAYER); return 1; }
    if (code == KEY_ARR) { show_page(PG_STARS); return 1; }
    if (code >= KEY_PUSH0 && code < KEY_PUSH0 + 6) {
        int k = code - KEY_PUSH0;
        if (page == PG_CHAIN) { fx_user[sel_fx][k] = -1; if (want_of(weather, sel_fx, k) < 0) fx_val[sel_fx][k] = fx[sel_fx].def[k]; say(fx[sel_fx].knob[k], "SKY"); }
        else if (page_param_pages(page)) { Param *p = &params[page][(layer() ? 6 : 0) + k]; if (p->mask) { p->user = -1; say(p->name, "SKY"); } }
    }
    else if (code == KEY_LEVEL_PUSH) toggle_auto();
    else if (code == KEY_LEFT || code == KEY_RIGHT) {
        if (page == PG_CHAIN) select_fx((sel_fx + (code == KEY_RIGHT ? 1 : N_FX - 1)) % N_FX, 0);
        else show_page((page + (code == KEY_RIGHT ? 1 : N_PAGES - 1)) % N_PAGES);
    }
    else if (code == KEY_PAGE) { func_latch = !func_latch; page_until = (int)ticks + 240; say("LAYER", func_latch ? "2" : "1"); }
    else if (code == KEY_UP || code == KEY_DOWN) {
        int d = code == KEY_UP ? 1 : -1;
        if (page == PG_PLAYER) { Param *p = &P(PG_PLAYER, 9); p->value = p->user = (int16_t)clamp(p->value + d, 0, 127); char n[6]; num(n, p->value + 1); say("SAMPLE", n); }
        else if (page == PG_ALGO) { algo = clamp(algo - d, 0, N_ALGO - 1); say("ALGO", algo_name[algo]); }
        else choose_weather(clamp(weather + d, 0, N_WEATHER - 1));
    } else if (code == KEY_PATTERN) { Param *p = &P(PG_STARS, 10); p->value = p->user = (int16_t)!p->value; say("STARS", p->value ? "BIRDS" : "RING"); }
    else if (code == KEY_BANK) { Param *p = &P(PG_STARS, 9); p->value = p->user = (int16_t)((p->value + 1) % 8); char n[6]; num(n, p->value + 1); say("BANK", n); }
    else if (code == KEY_MIDI) { gust = 120; say("GUST", 0); }
    else if (code == KEY_YES && func_held) intro = 0;
    else if (code == KEY_YES) { if (page == PG_ALGO && algo) show_page(algo_page()); else { Param *p = &P(PG_SKY, 10); p->value = p->user = (int16_t)!p->value; say(p->value ? "NIGHT" : "DAY", 0); } }
    else if (code == KEY_NO) { if (page == PG_STARS) { new_constellation(); say("NEW STARS", 0); } else { new_sky(); say("NEW SKY", 0); } }
    return 1;
}

int module_enc(int encoder, int delta)
{
    if (hidden) return 0;
    if (intro < INTRO_LEN) return 1;
    if (encoder == 6) { choose_weather(clamp(weather + (delta > 0 ? 1 : -1), 0, N_WEATHER - 1)); return 1; }
    if (encoder < 0 || encoder > 5) return 1;
    char n[8];
    if (page == PG_CHAIN) {
        const Fx *f = &fx[sel_fx];
        if (!f->knob[encoder][0]) return 1;
        int v = clamp(fx_value(sel_fx, encoder) + delta, 0, f->hi[encoder]);
        fx_user[sel_fx][encoder] = fx_val[sel_fx][encoder] = (int16_t)v;
        num(n, v); say(f->knob[encoder], n);
        return 1;
    }
    if (page == PG_ALGO) { if (encoder == 0) { algo = clamp(algo + (delta > 0 ? 1 : -1), 0, N_ALGO - 1); say("ALGO", algo_name[algo]); } return 1; }
    if (page == PG_SYSTEM) {
        if (encoder > 2) return 1;
        Param *p = &P(PG_SYSTEM, encoder);
        int v = clamp(pv(PG_SYSTEM, encoder) + (delta > 0 ? 1 : -1), p->lo, p->hi);
        p->value = p->user = (int16_t)v;
        if (encoder == 0) for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff;
        return 1;
    }
    if (!page_param_pages(page)) return 1;
    int i = (layer() ? 6 : 0) + encoder;
    Param *p = &params[page][i];
    if (!p->name[0]) return 1;
    int v = clamp(pv(page, i) + delta, p->lo, p->hi);
    p->user = p->value = (int16_t)v;
    if (page == PG_SKY) { if (i == 3) set_weather(v); if (i == 4) auto_weather = v; if (i == 6) xf_right = v; }
    page_until = (int)ticks + 240;
    static const char *const in_names[5] = {"-", "A B", "A", "B", "A+B"};
    static const char *const cd_names[5] = {"-", "C D", "C", "D", "C+D"};
    if (page == PG_SKY && (i == 3 || i == 6)) say(p->name, weather_name[v]);
    else if (page == PG_MIX && i == 0) say("IN AB", in_names[v]);
    else if (page == PG_MIX && i == 1) say("IN CD", cd_names[v]);
    else if (p->hi == 1) say(p->name, v ? "ON" : "OFF");
    else if (page == PG_SKY && i == 0) { num(n, v - 9); say("WIND", n); }
    else { num(n, v); say(p->name, n); }
    return 1;
}
