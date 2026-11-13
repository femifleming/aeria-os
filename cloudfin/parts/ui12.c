static int midi_pitch; /* the MIDI page: gumballs or the pitch tracker */
#ifdef AERIAOS
static void midi_mod_set(int m) { midi_mod = m & 3; midi_pitch = midi_mod == 1; }
static const char *module_title(void)
{
    static const char *const mn[4] = {"GUMBALLS", "PITCH", "MARBLES", "PONG"};
    return out_open ? "OUT" : cloud_open ? "GRAIN CLOUD" : page == PG_MIDI ? mn[midi_mod & 3] : page_name[page];
}
#define MODULE_TITLE module_title()
#else
#define MODULE_TITLE (page == PG_MIDI && midi_pitch ? "PITCH" : page_name[page])
#endif
static int layer(void) { return func_held || func_latch; }
static void label(int x, const char *s) { for (int i = x - 2; i < x + text_w(s) + 2; i++) span(i, 0, 8, 0); text(x, 2, s, 1); }
static void panel_clear(int y0, int y1) { for (int x = 0; x < W; x++) span(x, y0, y1, 0); }
static void upper(const char *in, char *out, int max) { int i = 0; for (; in[i] && i < max; i++) out[i] = in[i] >= 'a' && in[i] <= 'z' ? (char)(in[i] - 32) : in[i]; out[i] = 0; }

/* ---- CHAIN: eleven balloons on one string; the selected one's knobs below --------------------- */
static void draw_chain(void)
{
    panel_clear(10, 47);
    int y = 16;
    for (int x = 2; x < W - 2; x++) if (((x + (int)(ticks >> 2)) & 3) == 0) px(x, y);
    for (int s = 0; s < N_SLOTS; s++) {
        Slot *sl = &slots[s];
        int cx = 10 + s * 18, bob = sine[(int)(ticks + (uint32_t)s * 9) & 63] / 64, r = s == sel_slot ? 5 : 4, cy = y + bob;
        disk_clear(cx, cy, r + 1);
        if (sl->row <= 0) { for (int a = 0; a < 1024; a += 128) px(cx + cos10(a) * r / 127, cy + sin10(a) * r / 127); }
        else {
            circle(cx, cy, r);
            if (sl->pending < 0 && !((slot_bypass >> s) & 1)) for (int dy = 1; dy <= r; dy++) for (int dx = -r + 1; dx < r; dx++) if (dx * dx + dy * dy < r * r && dither_on(cx + dx, cy + dy, 8)) px(cx + dx, cy + dy);
        }
        if (sl->pending >= 0 && (ticks & 8)) circle(cx, cy, r + 2);
        if (s == sel_slot) for (int dx = -2; dx <= 2; dx++) px(cx + dx, cy + r + 3);
    }
    Slot *sl = &slots[sel_slot];
    if (sl->row <= 0) { text(30, 30, "EMPTY  YES: SHELF", 1); return; }
#if defined(AERIAOS)
    if (layer() && sl->row <= FX_COUNT) {          /* FUNC: the effect's second page (modes, freeze, makeup ...) */
        const FxInfo *fi = &fx_info[sl->row - 1]; int any = 0;
        for (int k = 0; k < 6; k++) {
            if (!fi->knob2[k][0]) continue;
            int c = fi->count2[k] > 1 ? fi->count2[k] - 1 : 127;
            bubble(11 + k * 21, 33, 7, preview_slot_p2[sel_slot][k] * 127 / c, fi->knob2[k], 0); any = 1;
        }
        if (!any) text(34, 30, "NO MORE KNOBS", 1);
        return;
    }
#endif
    for (int k = 0; k < 6; k++) {
        if (!sl->knob[k][0]) continue;
        int c = sl->count[k] > 1 ? sl->count[k] - 1 : 127;
        bubble(11 + k * 21, 33, 7, slot_value(sel_slot, k) * 127 / c, sl->knob[k], sl->user[k] < 0 && rule_of(sl->knob[k]) >= 0);
    }
}

/* the shelf: every effect this slot can hold */
static void draw_shelf(void)
{
    panel_clear(9, 56);
    int fx2 = slot_fx(sel_slot);
    shelf_build(fx2);
    int n = shelf_n;
    text(2, 10, "SHELF", 1);
    char where[8] = "SLOT ";
    num(where + 5, sel_slot + 1);
    text(W - 2 - text_w(where), 10, where, 1);
    for (int x = 0; x < W; x += 2) px(x, 16);
    int top = shelf_row - 2 < 0 ? 0 : shelf_row - 2;
    for (int r = 0; r < 6 && top + r < n; r++) {
        int i = top + r, yy = 19 + r * 6;
        char nm[14];
        list_name(fx2, shelf_rows[i], nm);
        if (shelf_rows[i] == 0) { nm[0] = '-'; nm[1] = 0; }
        if (i == shelf_row) { for (int x = 0; x < W; x++) span(x, yy - 1, yy + 5, 1); text(4, yy, nm, 0); }
        else text(4, yy, nm, 1);
    }
    text(2, 57, "YES PUT ON  NO BACK", 1);
}

static void draw_source(void)
{
    panel_clear(10, 47);
    static const char *const names[3] = {"LOOP", "STARS", "BALLOONS"};
    int on[3] = {src_loop, src_stars, src_balloons};
    for (int i = 0; i < 3; i++) {
        int y = 13 + i * 9;
        char n[2] = {(char)('1' + i), 0};
        text(18, y, n, 1);
        text(28, y, names[i], 1);
        text(72, y, on[i] ? "ON" : "OFF", 1);
        if (on[i]) for (int x = 70; x < 84; x++) px(x, y + 6);
    }
    text(4, 41, "FUNC+1 2: PICK FROM CARD", 1);
}

static void draw_system(void)
{
    panel_clear(10, 61);
    if (usb_mode) { text(20, 20, "USB DISK MODE", 1); text(14, 32, "NO: BACK TO THE SKY", 1); return; }
    text(4, 11, "1 USB DISK MODE", 1);
    text(4, 17, "2 OS UPGRADE  FUNC+YES", 1);
    text(4, 24, "A SOUND", 1); text(52, 24, pv(PG_SYSTEM, 0) == 0 ? "OFF" : pv(PG_SYSTEM, 0) == 1 ? "KNOBS" : "LANE", 1);
    text(4, 30, "B INTRO", 1); text(52, 30, pv(PG_SYSTEM, 1) ? "ON" : "OFF", 1);
    text(4, 36, "C LIGHTS", 1); text(52, 36, pv(PG_SYSTEM, 2) ? "ON" : "OFF", 1);
    text(4, 42, "D AUTOPLAY", 1); text(52, 42, pv(PG_SYSTEM, 3) ? "ON" : "OFF", 1);
    text(4, 48, "E SAVER", 1); text(52, 48, saver_name(pv(PG_SYSTEM, 4)), 1);
    char w[10]; num(w, saver_wait[pv(PG_SYSTEM, 5)]); int n = 0; while (w[n]) n++; w[n++] = ' '; w[n++] = 'M'; w[n++] = 'I'; w[n++] = 'N'; w[n] = 0;
    text(4, 54, "F WAIT", 1); text(52, 54, w, 1);
    text(96, 54, "YES TRY", 1);
}

static int param_page(int pg) { return pg == PG_SKY || pg == PG_LOOP || pg == PG_STARS || pg == PG_BALLOON || pg == PG_MIX || pg == PG_GAIN || pg == PG_MASTER; }
#ifdef AERIAOS
/* ---- the loop's waveform (sadnoise asked, 12 Nov 2026): the sample as an island on AERIA's sea -----------------
 * Each column of the sample is a piece of skyline as tall as it is loud, standing on the waterline; the part the
 * loop plays is solid and mirrored in the water (a dithered reflection that ripples), the rest a faint shore.
 * START and END are two buoys with pennants; a little boat sails the waterline where the loop is playing. */
const uint8_t *aos_loop_wave(void);
void aos_loop_restart(void);
int aos_loop_at(void);
static void loop_wave_draw(void)
{
    const int y0 = 7, base = 16, top = 9, y1 = 21;           /* the picture: rows 7..21, the waterline at 16 */
    panel_clear(y0, y1);
    const uint8_t *w = aos_loop_wave();
    int a = pv(PG_LOOP, 0), b = pv(PG_LOOP, 1);
    if (b <= a) b = a + 1;
    if (b > 127) b = 127;
    if (!w) { text(34, 11, "NO LOOP LOADED", 1); return; }
    for (int x = 0; x < W; x++) {
        int h = 1 + w[x] * (base - top) / 255, in = x >= a && x < b;
        if (in) {
            span(x, base - h, base, 1);
            /* the reflection: shorter, dithered, each row swaying a pixel with the tide */
            for (int d = 1; d <= (h * 2) / 3 && base + d <= y1; d++) { int sx = x + (((int)(ticks >> 4) + d) & 3) / 3; if (dither_on(sx, base + d, 7 - d)) px(x, base + d); }
        } else {
            for (int y = base - h; y <= base; y++) if (dither_on(x, y, 5)) px(x, y);
        }
        if ((x & 3) == ((int)(ticks >> 5) & 3)) px(x, base + 1);                       /* the waterline's glint */
    }
    /* the buoys: a pole from the water up past the skyline, a pennant pointing into the loop */
    for (int k = 0; k < 2; k++) {
        int x = k ? b : a, dir = k ? -1 : 1;
        for (int y = y0; y <= base + 2; y++) { px_off(x - 1 < 0 ? 0 : x - 1, y); px_off(x + 1 > W - 1 ? W - 1 : x + 1, y); px(x, y); }
        px(x + dir, y0); px(x + 2 * dir, y0 + 1); px(x + dir, y0 + 1); px(x + dir, y0 + 2);
        px(x - 1, base + 3); px(x + 1, base + 3);
    }
    /* the boat, where the loop is */
    int at = aos_loop_at();
    if (at >= 0) {
        int x = clamp(at, 2, W - 3), bob = ((ticks >> 4) & 1);
        for (int dx = -2; dx <= 2; dx++) { px_off(x + dx, base - 1 + bob); px_off(x + dx, base - 2 + bob); px_off(x + dx, base - 3 + bob); px_off(x + dx, base - 4 + bob); }
        for (int dx = -2; dx <= 2; dx++) px(x + dx, base - 1 + bob);
        px(x - 1, base + bob); px(x, base + bob); px(x + 1, base + bob);
        px(x, base - 2 + bob); px(x, base - 3 + bob); px(x, base - 4 + bob); px(x + 1, base - 3 + bob);
    }
}
#endif
static void draw_page(void)
{
    if (br_mode) { draw_browser(); return; }
    if (usb_mode && page == PG_SYSTEM) { draw_system(); return; }
#ifdef AERIAOS
    if (ticks > ui_until && !shelf_open && !func_held && !out_open && !cloud_open && !(page == PG_MIDI && midi_mod >= 2)) return;
#else
    if (ticks > ui_until && !shelf_open && !func_held) return; /* idle: the landscape alone */
#endif
#ifdef AERIAOS
    if (out_open) { out_draw(); return; }
    if (cloud_open) { cloud_draw(); return; }
    if (page == PG_MIDI && midi_mod == 2) { marbles_draw(); return; }
    if (page == PG_MIDI && midi_mod == 3) { pong_draw(); return; }
#endif
    if (page == PG_CHAIN && setup_busy()) { panel_clear(10, 47); text(26, 22, "FILLING THE SHELF", 1); for (int i = 0; i < N_SETUP; i++) px(30 + i * 5, 32), px(30 + i * 5, 33), (void)0; for (int i = 0; i <= setup_at && i < N_SETUP; i++) { span(29 + i * 5, 30, 35, 1); span(30 + i * 5, 30, 35, 1); } return; }
    if (page == PG_CHAIN) { if (shelf_open) draw_shelf(); else draw_chain(); return; }
    if (page == PG_SOURCE) { draw_source(); return; }
    if (page == PG_SYSTEM) { draw_system(); return; }
    if (page == PG_MIDI) { if (midi_pitch) pitch_draw(); else gum_draw(); return; }
    if (!param_page(page)) return;
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
#ifdef AERIAOS
    if (page == PG_LOOP) loop_wave_draw();
    if (page == PG_MIX || page == PG_GAIN) {   /* the levels: A B C D, then main L R */
        int aos_meter(int i);
        static const char *const ml[6] = {"A", "B", "C", "D", "L", "R"};
        for (int x = 0; x < W; x++) span(x, 48, 55, 0);
        for (int i = 0; i < 6; i++) {
            int x = 4 + i * 21 + (i >= 4 ? 4 : 0), lv = aos_meter(i);
            text(x, 49, ml[i], 1);
            for (int k = 0; k < 16; k++) { int xx = x + 5 + k; if (k < lv) span(xx, 49, 53, 1); else if ((k & 3) == 0) px(xx, 53); }
        }
    }
#endif
}

static void draw_hud(void)
{
    char title[14];
#ifdef AERIAOS
    if (page == PG_CHAIN && !shelf_open && !br_mode && !out_open && !cloud_open) upper(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", title, 12);
#else
    if (page == PG_CHAIN && !shelf_open && !br_mode) upper(slots[sel_slot].row > 0 ? slots[sel_slot].name : "EMPTY", title, 12);
#endif
    else upper(br_mode ? (br_mode == BR_LOOP ? "LOOP" : br_mode == BR_PONG ? "PONG" : br_mode == BR_SEQ ? "KIT" : "STARS") : MODULE_TITLE, title, 12);
    int showing = br_mode || shelf_open || usb_mode || ticks <= ui_until || func_held;
    if (showing) label(2, title);
    static char wxn[12];
    { int n = 0, cnt = 0; for (int i = 1; i < N_WEATHER; i++) cnt += WON(i);
      if (cnt <= 1) { const char *p = weather_name[cnt ? weather : CLEAR]; while (p[n] && n < 11) { wxn[n] = p[n]; n++; } }
      else for (int i = 1; i < N_WEATHER && n < 10; i++) if (WON(i)) { if (n) wxn[n++] = '+'; wxn[n++] = weather_name[i][0]; wxn[n++] = weather_name[i][1]; }
      if (xw_on && n < 9) { if (n) wxn[n++] = '+'; wxn[n++] = xw_on & 1 ? 'W' : xw_on & 2 ? 'H' : 'A'; wxn[n++] = xw_on & 1 ? 'I' : xw_on & 2 ? 'A' : 'U'; }
      wxn[n] = 0; }
    const char *w = wxn;
    label(W - 2 - text_w(w) - (auto_weather ? 8 : 0), w);
    if (auto_weather) label(W - 5, "A");
#ifdef AERIAOS
    if (showing && !br_mode && !out_open && !cloud_open && (page == PG_CHAIN || page == PG_MIX)) {
#else
    if (showing && !br_mode && (page == PG_CHAIN || page == PG_MIX)) {
#endif
        static const char *const ab[5] = {"-", "AB", "A", "B", "A+B"}, *const cd[5] = {"-", "CD", "C", "D", "C+D"};
        for (int i = 0; i < 60; i++) span(i, 57, 63, 0);
        text(1, 58, ab[pv(PG_MIX, 6)], 1); text(18, 58, cd[pv(PG_MIX, 7)], 1);
    }
    if (pv(PG_SYSTEM, 0) != 1) { const char *m = pv(PG_SYSTEM, 0) == 0 ? "SND OFF" : "LANE"; for (int i = W - text_w(m) - 3; i < W; i++) span(i, 57, 63, 0); text(W - text_w(m) - 1, 58, m, 1); }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 48, 56, 1);
        text(x, 50, toast, 0);
    }
}

/* ---- LEDs: red and white, undulating (sadnoise, 9 Nov 2026) -----------------------------------------------
 * Every known lamp: channel A white, channel B red (B5 palette entries through stock's builder); REC 1-3 left to
 * the recorder. The trig row breathes: two slow waves roll across it, white crests and red troughs, the level
 * eased a step at a time so it moves smoothly. A trig with something to say rises out of the wave:
 *   1 (was red)   red, bright, breathing with the wave
 *   2 (was green) white, full
 *   3 (flash)     white full over red full */
#ifndef PREVIEW
#define led_palette ((void (*)(int, int, int, int, int))0x40013368u)   /* (id, register 0-2, r, g, b) */
#define led_level ((void (*)(int, int))0x400135b0u)                  /* (id, level 0-15), cached */
#else
static void led_palette(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void led_level(int a, int b) { (void)a; (void)b; }
#endif
/* the A ids of every lamp in the measured map (aeria-os/HARDWARE.md 3.4), not REC 1-3 */
static const uint8_t white_lamps[] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30, 40,42,44,46,48,50,52,54,
                                      32,34,38, 64,66,68,70, 80,82,86, 84, 88,90, 102, 120,122};
static uint32_t white_at;
static void leds_white(void)
{
#ifndef PREVIEW
    /* stock's own colour level for the panel's LED intensity setting, as Modwerk's boot animation uses it */
    uint32_t in = *(volatile uint32_t *)0x800000d0u; int w = (int)((const uint32_t *)0x400abc26u)[in > 2 ? 2 : in];
#else
    int w = 255;
#endif
    for (unsigned i = 0; i < sizeof white_lamps; i++)
        for (int reg = 0; reg < 3; reg++) {
            led_palette(white_lamps[i], reg, w, w, w);       /* A: white */
            led_palette(white_lamps[i] + 1, reg, w, w, w);   /* B: white too (sadnoise: all white) */
        }
}
static uint8_t led_want[16];
/* all white (sadnoise, 9 Nov 2026): a trig's state is its brightness -- 1 dim, 2 bright, 3 full */
static void leds_out(void)
{
    static const uint8_t level_of[4] = {0, 4, 15, 15};
    if (!white_at || ticks - white_at > 3600u) { leds_white(); white_at = ticks | 1u; }   /* again each minute, in case stock re-colours */
    leds_clear_trigs();
    for (int k = 0; k < 16; k++) if (led_want[k]) { led_level(2 * k, level_of[led_want[k] & 3]); set_led(2 * k, 3); }
}
#ifdef AERIAOS
#define INTRO_FROM 0     /* AERIA OS: nothing drew before it -- the whole intro, from the first brush stroke */
#else
#define INTRO_FROM 20    /* the AERIA base's startup has drawn the logo's outline already */
#endif
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
#ifdef SEQ_UI
    if (mod_open) { mod_leds(); return; }
    if (seq_on && !br_mode && !preset_mode && sq_view != SV_MASTER) { seq_leds(); return; }
#endif
    if (page == PG_CHAIN) {
        /* 1-11 the slots: green the selected, red holding an effect, dark empty; 12-16 weather tools */
        for (int s = 0; s < N_SLOTS; s++) led_want[s] = s == sel_slot ? 2 : (slots[s].row > 0 && !((slot_bypass >> s) & 1)) ? 1 : 0;
        if (slots[sel_slot].pending >= 0 && (ticks & 8)) led_want[sel_slot] = 3;
        led_want[11] = 1; led_want[12] = 1; led_want[13] = 1; /* shelf, weather down, weather up */
        led_want[14] = auto_weather ? 2 : 0;
        led_want[15] = thunder ? 3 : 0;
        if (thunder > 150) for (int s = 0; s < N_SLOTS; s++) led_want[s] = 3;
    } else if (page == PG_SKY) {
        for (int i = 1; i < N_WEATHER; i++) if (WON(i)) led_want[i] = 2;
        if (!wx_on) led_want[CLEAR] = 2;
        led_want[7] = auto_weather ? 2 : (uint8_t)((ticks >> 5) & 1);
        for (int i = 0; i < XW_N; i++) led_want[8 + i] = (xw_on >> i) & 1 ? 2 : 1;
        led_want[11] = ticks < tw_until ? 3 : 1;
    } else if (page == PG_SOURCE) {
        led_want[0] = src_loop ? 2 : 1; led_want[1] = src_stars ? 2 : 1; led_want[2] = src_balloons ? 2 : 1;
    } else if (page == PG_STARS) {
        for (int p = 0; p < PLANETS; p++) if ((planet_mask >> p) & 1) led_want[p] = 1;
        if (planet_hit >= 0 && star_flash) led_want[planet_hit] = 2;
    } else if (page == PG_BALLOON) {
        for (int b = 0; b < 3; b++) led_want[b] = balloon[b].state == B_FLOATING ? ((balloon_quiet >> b) & 1 ? 1 : 2) : (balloon[b].state == B_FILLING && (ticks & 8)) ? 1 : 0;
    } else if (page == PG_MASTER) {
        master_leds(led_want);
    } else if (page == PG_LOOP) {
        int a = pv(PG_LOOP, 0) * 16 / 128, b = pv(PG_LOOP, 1) * 16 / 128;
        for (int k = 0; k < 16; k++) if (k >= a && k <= b) led_want[k] = 1;
    } else if (page == PG_MIDI && midi_pitch) {
        led_want[pt_mode] = 2; led_want[15] = 1;
    } else if (page == PG_MIDI) {
        /* 1-4 drop (green holding gumballs, yellow while one sings), 9-12 take one out (red) */
        for (int g = 0; g < GB_BALLOONS; g++) {
            int n = gum_count(g);
            led_want[g] = n ? (gum_on && gum_sounding(g) ? 3 : 2) : 0;
            led_want[8 + g] = n ? 1 : 0;
        }
    } else if (page == PG_GAIN) {
        /* 1-8 the input gain, 9-16 the output: green to unity, yellow above */
        int in = pv(PG_GAIN, 0) * 8 / 128, out = pv(PG_GAIN, 3) * 8 / 128;
        for (int k = 0; k < 8; k++) { if (k <= in) led_want[k] = k >= 4 ? 3 : 2; if (k <= out) led_want[8 + k] = k >= 6 ? 3 : 2; }
    }
#ifdef AERIAOS
    if (out_open) out_leds();
    else if (page == PG_MIDI && midi_mod == 2) marbles_leds();
    else if (page == PG_MIDI && midi_mod == 3) pong_leds();
    if (preset_mode) preset_leds();
#endif
    if (gust) for (int k = 0; k < 16; k++) if (((int)(ticks >> 1) - k) % 16 == 0) led_want[k] = 3;
    if (flash) { for (int k = 0; k < 16; k++) led_want[k] = 3; set_led(38, 2); set_led(39, 2); }
    leds_out();
}

/* ---- diagnostics, cycling every 2 s: tag in the top nibble ------------------------------------ */
static uint32_t diag_word(void)
{
    int w = (int)(ticks / 120u) % 3;
    if (w == 0)
        return 0x10000000u | (uint32_t)playing() << 27 | (uint32_t)rig_loaded() << 26 | (uint32_t)((pat_off + 100) & 0x3f) << 20
               | (master_step() & 0xffu) << 12 | (stars_total & 0xffu) << 4 | (uint32_t)(src_loop | src_stars << 1 | src_balloons << 2);
    if (w == 1) return 0x30000000u | (uint32_t)page << 20 | (uint32_t)sel_slot << 16 | (uint32_t)(slots[sel_slot].id & 0xff) << 8 | (uint32_t)(picking + 1);
    return 0x40000000u | (uint32_t)(list_count(0) & 0xff) << 16 | (uint32_t)(list_count(1) & 0xff) << 8 | (uint32_t)(slots[0].id & 0xff);
}

/* ---- hooks --------------------------------------------------------------------------------------- */
static void strike(void)
{
    bolt_x = range(16, W - 16); bolt_t = 9; flash = 3; thunder = 200;
#if defined(AERIAOS)
    wx_struck = 1;
    for (int sl = 0; sl < N_SLOTS; sl++) for (int k = 0; k < 6; k++) storm_at[sl][k] = 0;   /* a strike throws every knob at once */
#endif
}
static int autoplayed, setup_done;
/* the screensaver: after WAIT minutes without a key or a knob, SAVR (or a random one) takes the screen */
static int saver_on;
static uint32_t last_input;
static void saver_go(void)
{
    int s = pv(PG_SYSTEM, 4);
    if (s <= SS_OFF || s >= SS_COUNT) s = range(SS_PADDLE, SS_COUNT - 1);
    ss_start(s); saver_on = 1;
}

/* AERIA ROSE in the chain: its rose window hangs in the sky. FOLD (knob 3) sets the foils; the eight panes
 * glint in turn, as the resonator's modes do when ROTA moves the light round. */
static int rose_slot(void)
{
    for (int k = 0; k < N_SLOTS; k++) {
        const char *n = slots[k].name; const char *w = "AERIA ROSE"; int i = 0;
        if (slots[k].row <= 0 || ((slot_bypass >> k) & 1)) continue;
        while (w[i] && n[i] == w[i]) i++;
        if (!w[i]) return k;
    }
    return -1;
}
static void draw_rose_window(void)
{
    int s = rose_slot();
    if (s < 0) return;
    int n = slot_value(s, 2) + 3, cx = 20, cy = 24, r = 13;
    disk_clear(cx, cy, r + 1);
    circle(cx, cy, r); circle(cx, cy, r / 3);
    for (int k = 0; k < n; k++) {
        int a = k * 1024 / n + (int)(ticks / 3);
        int fx = cx + cos10(a) * (r * 6 / 10) / 127, fy = cy + sin10(a) * (r * 6 / 10) / 127;
        int pr = (r * 6 / 10) * 3 / n; circle(fx, fy, pr < 2 ? 2 : pr > 4 ? 4 : pr);
        for (int t = r / 3; t < r; t++) px(cx + cos10(a + 512 / n) * t / 127, cy + sin10(a + 512 / n) * t / 127);
    }
    /* the eight panes: light walks round them */
    int lit = (int)(ticks / 12) & 7;
    for (int k = 0; k < 8; k++) {
        int a = k * 128, d = (k - lit) & 7, lv = d == 0 ? 14 : d == 1 || d == 7 ? 8 : 3;
        int px0 = cx + cos10(a - 256) * (r - 3) / 127, py0 = cy + sin10(a - 256) * (r - 3) / 127;
        if (lv > 3) { px(px0, py0); if (lv > 8) { px(px0 + 1, py0); px(px0, py0 + 1); px(px0 + 1, py0 + 1); } }
    }
}

/* the tick's own profile (dev): timer counts per section, summed in battery RAM nothing else uses (0x100fb000),
 * read with `device.mjs mem 0x100fb000 48` -- [0] 'PROF', [1] ticks, [2..] sections */
#ifdef PREVIEW
#define PROF_AT(k) ((void)0)
#else
static uint32_t prof_t;
#define PROF_AT(k) do { uint32_t n_ = *(volatile uint32_t *)0xFC07C00Cu; ((volatile uint32_t *)0x100FB000u)[2 + (k)] += n_ - prof_t; prof_t = n_; } while (0)
#endif
void module_tick(struct modwerk_runtime_api *api)
{
#ifndef PREVIEW
    /* AERIA OS bring-up: snapshot the GPIO block (0xFC0A4000-0xFC0A40FF) while stock makes sound, into battery RAM
     * at 0x100FB100 (read with `device.mjs mem 0x100fb100 256`), every 10 s */
    if ((ticks % 600u) == 300u) { volatile uint8_t *g = (volatile uint8_t *)0xFC0A4000u, *o = (volatile uint8_t *)0x100FB100u; for (int i = 0; i < 256; i++) o[i] = g[i]; }
    ((volatile uint32_t *)0x100FB000u)[0] = 0x50524F46u; ((volatile uint32_t *)0x100FB000u)[1]++;
    prof_t = *(volatile uint32_t *)0xFC07C00Cu;
#endif
    /* the AERIA base's startup has drawn the logo's outline (aeria-boot), so the intro goes straight to the
     * brush painting it in, without the blank lead. FUNC+YES on SYSTEM still plays it whole. */
    if (!ready) { new_sky(); new_constellation(); slots_init(); spell_left = 3600u; ready = 1; intro = pv(PG_SYSTEM, 1) ? INTRO_FROM : INTRO_LEN; }
    ticks++;
    if (ticks == 30 || (ticks % 300) == 0) for (int s = 0; s < N_SLOTS; s++) if (slots[s].pending < 0 && (slots[s].id != live_fx(slot_track(s), slot_fx(s)) || ticks == 30)) { slot_refresh(s); slots[s].pending = -1; }
#ifdef AERIAOS
    if (ticks == 2) aeria_state_restore();
    if (ticks == 31) aeria_state_reapply_slots();
    aeria_state_tick();
#ifdef SEQ_UI
    seq_ui_tick();
#endif
#ifdef DREAM_SKY
    dream_sky_tick();
#endif
#endif
    if (intro < INTRO_LEN) intro++;
    if (amount < 256) amount += 2;
    if (flash) flash--;
    if (bolt_t) bolt_t--;
    if (star_flash) star_flash--;
    if (gust) gust--;
    if (thunder) thunder = thunder > 3 ? thunder - 3 : 0;
    night = pv(PG_SKY, 10);
    write_mode = pv(PG_SYSTEM, 0);
    PROF_AT(0);
    usb_tick();
    PROF_AT(1);
    step_auto();
    PROF_AT(2);
    if (WON(STORM) && amount > 128 && (rnd() % 420) == 0) strike();
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
    picks_tick();
    PROF_AT(3);
    if (!setup_done && ticks > 120 && rig_loaded()) { setup_done = 1; setup_start(); }
    setup_tick();
    deal_tick();
    loop_tick();
    preview_tick();
    PROF_AT(4);
    if (!usb_mode) { gum_tick(); pitch_tick(); }
#ifdef AERIAOS
    pan_tick(); mod_tick(); cloud_tick(); scale_tick(); master_tick(); marbles_tick(); pong_tick();
#endif
    PROF_AT(5);
    if (saver_on) { if (usb_mode || br_mode) { saver_on = 0; ss_stop(); } else ss_tick(); }
    else if (pv(PG_SYSTEM, 4) != SS_OFF && intro >= INTRO_LEN && !usb_mode && !br_mode && !shelf_open &&
             ticks - last_input > (uint32_t)saver_wait[pv(PG_SYSTEM, 5)] * 3600u) saver_go();
    fin_x += 70 + wind * 10;
    if ((fin_x >> 8) > W + 20) fin_x = -40 * 256;
    if ((fin_x >> 8) < -40) fin_x = (W + 20) * 256;
    if (!hidden) leds_tick();
    PROF_AT(6);
    /* the active track stays on the chain's head (FRACT and stock draw for the active track) */
    if (!hidden && !usb_mode && (ticks & 15) == 0 && current_track() != HEAD_T) set_current_track(HEAD_T);
    /* the effects run as soon as the sky opens: start the transport once */
    if (!autoplayed && ticks > 600 && pv(PG_SYSTEM, 3) && rig_loaded() && !playing() && !usb_mode) { transport_start(); autoplayed = 1; }
    PROF_AT(7);
    sequencer_tick();
    PROF_AT(8);
    if (!usb_mode) sound_tick();
#ifdef AERIAOS
    wx_tick();
    xw_tick();
#endif
    PROF_AT(9);
    api->value = diag_word();
}

static void module_draw_body(unsigned char *frame);
void module_draw(unsigned char *frame)
{
#ifndef PREVIEW
    uint32_t t0 = *(volatile uint32_t *)0xFC07C00Cu;
    module_draw_body(frame);
    ((volatile uint32_t *)0x100FB000u)[12] += *(volatile uint32_t *)0xFC07C00Cu - t0;
    ((volatile uint32_t *)0x100FB000u)[13]++;
#else
    module_draw_body(frame);
#ifdef AERIAOS
    if (preset_mode && ready && !hidden && intro >= INTRO_LEN && !saver_on) preset_draw();
#endif
#endif
}

static void module_draw_body(unsigned char *frame)
{
    if (hidden || !ready) return;
    F = (uint32_t *)frame;
    for (int i = 0; i < W * 2; i++) F[i] = 0;
    if (intro < INTRO_LEN) { draw_intro(); return; }
    if (saver_on) { ss_draw(); return; }
#ifdef SEQ_UI
    if (mod_open) { mod_draw(); return; }
    if (seq_on && !br_mode && sq_view != SV_MASTER) { seq_draw(); return; }
#endif
    draw_sun_moon();
    int want = clouds_wanted();
    for (int depth = 1; depth <= 3; depth++) for (int i = 0; i < want; i++) if (clouds[i].depth == depth) draw_cloud(&clouds[i]);
    draw_rose_window();
    draw_heat();
    draw_fog();
    draw_precip();
#ifdef AERIAOS
    xw_draw();
#endif
    draw_bolt();
    draw_water();
#ifdef DREAM_SKY
    dream_sky_draw();                                          /* a visit from the dream log, now and then */
#endif
    if (page == PG_STARS && !br_mode) draw_stars();
    if (src_balloons || page == PG_BALLOON) draw_balloons();
    draw_page();
    draw_hud();
    if (flash) for (int x = 0; x < W; x++) span(x, 0, H - 1, 3);
}

static void show_page(int pg) { page = pg; shelf_open = 0; page_until = (int)ticks + 240; say(page_name[pg], 0); }
static void choose_weather(int w) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; set_weather(w); }
static void toggle_auto(void) { auto_weather = !auto_weather; P(PG_SKY, 4).value = P(PG_SKY, 4).user = (int16_t)auto_weather; say("AUTO", auto_weather ? "ON" : "OFF"); }

/* bypass: the slot gives its place to NONE and remembers what it held */
static int16_t bypass_row[N_SLOTS];
static void toggle_bypass(int s)
{
    if (picking >= 0) return;
    if ((slot_bypass >> s) & 1) { slot_bypass &= (uint16_t)~(1u << s); if (bypass_row[s] > 0) slot_pick(s, bypass_row[s]); }
    else if (slots[s].row > 0) { bypass_row[s] = slots[s].row; slot_bypass |= (uint16_t)(1u << s); slot_pick(s, 0); say(slots[s].name, "OFF"); }
}
static void select_slot(int s) { sel_slot = s; shelf_open = 0; page = PG_CHAIN; say(slots[s].row > 0 ? slots[s].name : "EMPTY", 0); }

int module_key(int code, int pressed)
{
    last_input = ticks;
    if (saver_on && !hidden) {
        /* a game takes its keys; any other press (or NO) wakes the sky, and the press is spent */
        if (code == KEY_PLAY || code == KEY_STOP) return 0;
        if (pressed && code == KEY_NO) { saver_on = 0; ss_stop(); ui_until = 0; return 1; }
        if (ss_key(code, pressed)) return 1;
        if (pressed && code != KEY_FUNC) { saver_on = 0; ss_stop(); ui_until = 0; }
        return 1;
    }
    if (pressed && code != KEY_PLAY && code != KEY_STOP) ui_until = ticks + UI_SHOW;
    if (code == KEY_FUNC) { func_held = pressed; if (pressed) page_until = (int)ticks + 240; }
    if (code == KEY_NO && pressed && func_held && !usb_mode) { hidden = !hidden; if (!hidden) show_page(page); return 1; }
    if (hidden) return 0;
    if (popup_open() && !usb_mode) return 0; /* a stock or base message: its keys */
    if (usb_mode) { if (pressed && code == KEY_NO) usb_leave(); return 1; }
#ifdef AERIAOS
    if (code == KEY_ARR && func_held) { if (pressed) { mod_open = !mod_open; mod_learn = -1; say(mod_open ? "MOD" : "MOD CLOSED", 0); } return 1; }   /* the modulation matrix, in both faces */
    if (mod_open) return mod_key(code, pressed);
#endif
#ifdef SEQ_UI
    if (seq_key(code, pressed)) return 1;                      /* FUNC + MIDI: the sequencer; in it, its own keys */
#endif
#ifdef AERIAOS
    if (code == KEY_CUE && !br_mode) { if (pressed) { out_open = !out_open; cloud_open = 0; shelf_open = 0; say(out_open ? "OUT" : page_name[page], 0); } return 1; }
    if (code == KEY_REC && !br_mode) { if (pressed) { cloud_open = !cloud_open; out_open = 0; say(cloud_open ? "GRAIN CLOUD" : page_name[page], 0); } return 1; }
#endif
#ifdef AERIAOS
    /* the transport in the sky (12 Nov 2026): on LOOP, PLAY starts the loop from its start and STOP stops it; on any
     * other page STOP takes every effect dry (each MIX held at 0) and clears the sky (every weather off, AUTO off) */
    if ((code == KEY_PLAY || code == KEY_STOP) && !br_mode && !seq_on) {
        if (!pressed) return 1;
        if (page == PG_LOOP) {
            if (code == KEY_PLAY) { src_loop = 1; aos_loop_restart(); say("LOOP", "PLAY"); } else { src_loop = 0; say("LOOP", "STOP"); }
            return 1;
        }
        if (code == KEY_STOP) {
            for (int s = 0; s < N_SLOTS; s++) if (slots[s].row > 0) for (int k = 0; k < 6; k++) if (knob_is_mix(slots[s].knob[k])) slots[s].user[k] = 0;
            xw_on = 0; auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; toggle_weather(CLEAR);
            say("STOP", "DRY + CLEAR");
            return 1;
        }
        return 0;
    }
#endif
    if (code == KEY_PLAY || code == KEY_STOP || code == KEY_REC || code == KEY_CUE || code == 0x18) return br_mode ? 1 : 0;
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) { if (pressed) held_tracks |= 1u << (code - KEY_TRACK0); else held_tracks &= ~(1u << (code - KEY_TRACK0)); }
    if (code == KEY_SCENEA || code == KEY_SCENEB) {
        scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0;
        if (pressed) { char n[4]; num(n, (scene_held == 1 ? scene_a : scene_b) + 1); say(scene_held == 1 ? "SCENE A IS" : "SCENE B IS", n); }
        return 1;
    }
    if (scene_held && pressed && code == KEY_NO) {              /* the scene forgets all it holds */
        int sc = scene_held == 1 ? scene_a : scene_b; char n[4]; num(n, sc + 1);
        for (int i = 0; i < SCENE_IDS; i++) scene_val[sc][i] = -1;
        scene_dirty = 1; say("SCENE EMPTY", n); return 1;
    }
    for (int b = 0; b < 3; b++) {
        Balloon *n = &balloon[b];
        if (code != n->key) continue;
        if (func_held) { if (pressed && n->state != B_EMPTY && n->state != B_FILLING) { n->state = B_POPPING; n->pop = 12; AOS_BALLOON(b, 0); } return 1; }
        set_current_track(n->track);
        if (pressed) {
            if (n->state == B_FILLING) { n->state = B_FLOATING; balloon_voice = b; AOS_BALLOON(b, 2); say("BALLOON", b == 0 ? "1 UP" : b == 1 ? "2 UP" : "3 UP"); }
            else { n->state = B_FILLING; n->size = 0; n->y = WATER_Y - 12; AOS_BALLOON(b, 1); say("CATCHING", b == 0 ? "A B" : b == 1 ? "C D" : "SKY"); }
        }
        return 0;
    }
#ifdef AERIAOS
    if (intro >= INTRO_LEN && preset_key(code, pressed)) return 1;
    if ((out_open || cloud_open) && pressed && code == KEY_NO && !func_held) { out_open = cloud_open = 0; say(page_name[page], 0); return 1; }
    if (out_open && out_key(code, pressed)) return 1;
    if (page == PG_MIDI && midi_mod >= 2 && pressed && !out_open && !cloud_open && (midi_mod == 2 ? marbles_key(code) : pong_key(code))) return 1;
    if (page == PG_MASTER && pressed && !out_open && !cloud_open && !br_mode && master_key(code)) return 1;
#endif
    if (!pressed) return 1;
    if (intro < INTRO_LEN) { intro = INTRO_LEN; return 1; }
    if (br_mode) { browser_key(code); return 1; }

    /* the shelf is open: UP/DOWN or LEVEL choose, YES puts it on, NO goes back */
    if (shelf_open) {
        shelf_build(slot_fx(sel_slot));
        if (code == KEY_UP && shelf_row > 0) shelf_row--;
        else if (code == KEY_DOWN && shelf_row < shelf_n - 1) shelf_row++;
        else if (code == KEY_YES) { shelf_open = 0; slot_bypass &= (uint16_t)~(1u << sel_slot); slot_pick(sel_slot, shelf_rows[shelf_row]); }
        else if (code == KEY_NO) shelf_open = 0;
        return 1;
    }

    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (scene_held) {                                        /* which of the sixteen scenes this side holds */
            char n[4]; num(n, k + 1);
            if (scene_held == 1) scene_a = (uint8_t)k; else scene_b = (uint8_t)k;
            scene_dirty = 1; say(scene_held == 1 ? "SCENE A IS" : "SCENE B IS", n);
        } else if (page == PG_CHAIN) {
            if (k < N_SLOTS) { if (func_held) toggle_bypass(k); else select_slot(k); }
            else if (k == 11 || k == 7) { shelf_open = 1; shelf_build(slot_fx(sel_slot)); shelf_row = shelf_index_of(slots[sel_slot].row); }
            else if (k == 12) choose_weather(clamp(weather - 1, 0, N_WEATHER - 1));
            else if (k == 13) choose_weather(clamp(weather + 1, 0, N_WEATHER - 1));
            else if (k == 14) toggle_auto();
            else { strike(); say("LIGHTNING", 0); }
        } else if (page == PG_SOURCE) {
            if (k == 0) { if (func_held) browser_open(BR_LOOP); else { src_loop = !src_loop; say("LOOP", src_loop ? "ON" : "OFF"); } }
            else if (k == 1) { if (func_held) browser_open(BR_STARS); else { src_stars = !src_stars; say("STARS", src_stars ? "ON" : "OFF"); } }
            else if (k == 2) { src_balloons = !src_balloons; say("BALLOONS", src_balloons ? "ON" : "OFF"); }
        } else if (page == PG_SYSTEM) {
            if (k == 0) usb_enter();
            else if (k == 1) say("OS UPGRADE", "FUNC+YES");
        } else if (page == PG_STARS) { planet_mask ^= (uint16_t)(1u << k); say("PLANET", (planet_mask >> k) & 1 ? "ON" : "OFF"); }
        else if (page == PG_MIDI) { if (midi_pitch) pitch_key(code); else gum_key(code); }
        else if (page == PG_LOOP) { Param *p = &P(PG_LOOP, func_held ? 1 : 0); p->value = p->user = (int16_t)(k * 127 / 15); }
        else if (page == PG_BALLOON) { if (k < 3 && balloon[k].state == B_FLOATING) { balloon_quiet ^= (uint8_t)(1u << k); balloon_voice = k; say(k == 0 ? "BALLOON 1" : k == 1 ? "BALLOON 2" : "BALLOON 3", (balloon_quiet >> k) & 1 ? "QUIET" : "SINGING"); } else if (k >= 8) { Param *p = &P(PG_BALLOON, 4); p->value = p->user = (int16_t)((k - 8) * 127 / 7); } }
        else if (k >= 8 && k <= 11) { if (k == 11) xw_twister(); else xw_toggle(k - 8); }
        else { if (k == 0) xw_on = 0;                              /* CLEAR clears the new weathers too */
               if (k < 7) { auto_weather = 0; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 0; toggle_weather(k); } else if (k == 7) toggle_auto(); else if (k == 15) { strike(); say("LIGHTNING", 0); } }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) {
        int k = code - KEY_TRACK0;
        if (k < 4 && (held_tracks & (k < 2 ? 0x3u : 0xcu)) == (k < 2 ? 0x3u : 0xcu)) {
            Param *p = &P(PG_MIX, k < 2 ? 6 : 7);
            p->value = p->user = (int16_t)(pv(PG_MIX, k < 2 ? 6 : 7) == 1 ? 4 : 1);
            say(k < 2 ? "A B" : "C D", p->value == 1 ? "STEREO" : "MONO");
            return 1;
        }
        if (k < N_SLOTS) { if (func_held) toggle_bypass(k); else select_slot(k); }
        return 1;
    }
    if (code == KEY_SRC) { show_page(PG_CHAIN); return 1; }
    if (code == KEY_AMP) { show_page(PG_SKY); return 1; }
    if (code == KEY_LFO) { show_page(PG_SOURCE); return 1; }
    if (code == KEY_FX1) { show_page(page == PG_MIX ? PG_GAIN : PG_MIX); return 1; }
    if (code == 0x30) { show_page(PG_MASTER); return 1; }                       /* the MIX key: the master's freeze delay and the tempo */
    if (code == KEY_FX2 || code == KEY_PROJ) { show_page(PG_SYSTEM); return 1; }
    if (code == KEY_PART) { show_page(PG_BALLOON); return 1; }
    if (code == KEY_AED) { show_page(PG_LOOP); return 1; }
    if (code == KEY_ARR) { show_page(PG_STARS); return 1; }
    if (code >= KEY_PUSH0 && code < KEY_PUSH0 + 6) {
        int k = code - KEY_PUSH0;
        if (page == PG_CHAIN) { slots[sel_slot].user[k] = -1; say(slots[sel_slot].knob[k], "SKY"); }
    }
    else if (code == KEY_LEVEL_PUSH) toggle_auto();
    else if (code == KEY_LEFT || code == KEY_RIGHT) {
        if (page == PG_CHAIN) select_slot((sel_slot + (code == KEY_RIGHT ? 1 : N_SLOTS - 1)) % N_SLOTS);
        else show_page((page + (code == KEY_RIGHT ? 1 : N_PAGES - 1)) % N_PAGES);
    }
    else if (code == KEY_PAGE) { func_latch = !func_latch; page_until = (int)ticks + 240; say("LAYER", func_latch ? "2" : "1"); }
    else if (code == KEY_UP || code == KEY_DOWN) choose_weather(clamp(weather + (code == KEY_UP ? 1 : -1), 0, N_WEATHER - 1));
    else if (code == KEY_PATTERN) { Param *p = &P(PG_STARS, 10); p->value = p->user = (int16_t)!p->value; say("STARS", p->value ? "BIRDS" : "RING"); }
    else if (code == KEY_BANK) { if (page == PG_CHAIN) { shelf_open = 1; shelf_build(slot_fx(sel_slot)); shelf_row = shelf_index_of(slots[sel_slot].row); } else { Param *p = &P(PG_STARS, 9); p->value = p->user = (int16_t)((p->value + 1) % 8); char n[6]; num(n, p->value + 1); say("BANK", n); } }
#ifdef AERIAOS
    else if (code == KEY_MIDI) { if (func_held) { gust = 120; say("GUST", 0); } else if (page == PG_MIDI) { midi_mod = (midi_mod + 1) % 4; midi_pitch = midi_mod == 1; static const char *const mn[4] = {"GUMBALLS", "PITCH", "MARBLES", "PONG"}; say(mn[midi_mod], 0); } else show_page(PG_MIDI); }
#else
    else if (code == KEY_MIDI) { if (func_held) { gust = 120; say("GUST", 0); } else if (page == PG_MIDI) { midi_pitch = !midi_pitch; say(midi_pitch ? "PITCH" : "GUMBALLS", 0); } else show_page(PG_MIDI); }
#endif
    else if (code == KEY_YES && page == PG_MIDI && !func_held) { if (midi_pitch) pitch_key(code); else gum_key(code); }
    else if (code == KEY_YES && func_held && page == PG_SYSTEM) os_upgrade();
    else if (code == KEY_YES && func_held) intro = 0;
    else if (code == KEY_YES && page == PG_SYSTEM) saver_go();
    else if (code == KEY_YES) { if (page == PG_CHAIN && slots[sel_slot].row <= 0) { shelf_open = 1; shelf_row = 0; } else { Param *p = &P(PG_SKY, 10); p->value = p->user = (int16_t)!p->value; say(p->value ? "NIGHT" : "DAY", 0); } }
    else if (code == KEY_NO) { if (page == PG_STARS) { new_constellation(); say("NEW STARS", 0); } else { new_sky(); say("NEW SKY", 0); } }
    return 1;
}

int module_enc(int encoder, int delta)
{
    last_input = ticks;
    if (saver_on && !hidden) { if (!ss_enc(encoder, delta)) { saver_on = 0; ss_stop(); } return 1; }
    ui_until = ticks + UI_SHOW;
    if (hidden || usb_mode) return hidden ? 0 : 1;
    if (popup_open()) return 0;
    if (intro < INTRO_LEN) return 1;
    if (br_mode) { if (encoder == 6 || encoder == 0) browser_enc(delta); return 1; }
#ifdef AERIAOS
    if (mod_open) { mod_enc(encoder, delta); return 1; }
    if (mod_learn >= 0 && !seq_on && encoder >= 0 && encoder < 6) {      /* MOD learning: this knob is the route's destination */
        if (page == PG_CHAIN && !layer() && slots[sel_slot].row > 0 && slots[sel_slot].knob[encoder][0]) { mod_learn_take((uint16_t)(MK_SLOT << 12 | (sel_slot * 6 + encoder))); return 1; }
        if (param_page(page) && page != PG_CHAIN) { int i = (layer() ? 6 : 0) + encoder; if (params[page][i].name[0]) { mod_learn_take((uint16_t)(MK_PAGE << 12 | (page * 12 + i))); return 1; } }
    }
#endif
#ifdef SEQ_UI
    if (seq_enc(encoder, delta)) return 1;
#endif
    if (shelf_open) { shelf_build(slot_fx(sel_slot)); if (encoder == 6 || encoder == 0) shelf_row = clamp(shelf_row + (delta > 0 ? 1 : -1), 0, shelf_n - 1); return 1; }
    if (encoder == 6) { choose_weather(clamp(weather + (delta > 0 ? 1 : -1), 0, N_WEATHER - 1)); return 1; }
    if (encoder < 0 || encoder > 5) return 1;
#ifdef AERIAOS
    if (preset_enc(delta)) return 1;                            /* the project page: a name's letters; else it holds the knobs */
    if (out_open) { out_enc(encoder, delta); return 1; }
    if (cloud_open) { cloud_enc(encoder, delta); return 1; }
    if (page == PG_MIDI && midi_mod == 2) { marbles_enc(encoder, delta); return 1; }
    if (page == PG_MIDI && midi_mod == 3) { pong_enc(encoder, delta); return 1; }
#endif
    char n[24];
    if (page == PG_CHAIN) {
        Slot *sl = &slots[sel_slot];
#if defined(AERIAOS)
        if (layer() && sl->row > 0 && sl->row <= FX_COUNT && encoder < 6) {
            const FxInfo *fi = &fx_info[sl->row - 1];
            if (!fi->knob2[encoder][0]) return 1;
            int c2 = fi->count2[encoder] >= 2 ? fi->count2[encoder] : 128;
            int v2 = clamp(preview_slot_p2[sel_slot][encoder] + (c2 <= 16 ? (delta > 0 ? 1 : -1) : delta), 0, c2 - 1);
            preview_slot_p2[sel_slot][encoder] = (uint8_t)v2;
            slot_text(sel_slot, 6 + encoder, v2, n); say(fi->knob2[encoder], n);
            return 1;
        }
#endif
        if (sl->row <= 0 || !sl->knob[encoder][0]) return 1;
        if (scene_held) {                                        /* the knob's place in the scene, not its own */
            int sc = scene_held == 1 ? scene_a : scene_b, id = N_PAGES * 12 + sel_slot * 6 + encoder;
            int cur = scene_val[sc][id] >= 0 ? scene_val[sc][id] : slot_base(sel_slot, encoder);
            int v = clamp(cur + delta, 0, sl->count[encoder] - 1);
            scene_val[sc][id] = (int8_t)v; scene_dirty = 1;
            n[0] = scene_held == 1 ? 'A' : 'B'; n[1] = ' '; slot_text(sel_slot, encoder, v, n + 2); say(sl->knob[encoder], n);
            return 1;
        }
        int v = clamp(slot_base(sel_slot, encoder) + delta, 0, sl->count[encoder] - 1);
        sl->user[encoder] = sl->value[encoder] = (int16_t)v;
        slot_text(sel_slot, encoder, v, n);
#if defined(AERIAOS)
        if (scale_now() && sl->row <= FX_COUNT) {          /* in a scale, a pitch knob reads as where it lands */
            uint32_t org = fx_info[sl->row - 1].org;
            if (org == 0x34000u && encoder == 0) pitch_text(n, (v - 24) * 1000);
            else if (org == 0x35000u && encoder == 4) signed_text(n, dc_round(aos_scale_snap((v - 24) * 1000), 1000));
            else if (org == 0x36000u && encoder == 0) reso_text(n, 30 + v);
        }
#endif
        say(sl->knob[encoder], n);
        return 1;
    }
    if (page == PG_MIDI) { if (midi_pitch) pitch_enc(encoder, delta, layer()); else gum_enc(encoder, delta, layer()); return 1; }
    if (page == PG_SYSTEM) {
        if (encoder > 5) return 1;
        Param *p = &P(PG_SYSTEM, encoder);
        p->value = p->user = (int16_t)clamp(pv(PG_SYSTEM, encoder) + (delta > 0 ? 1 : -1), p->lo, p->hi);
        if (encoder == 4) say("SAVER", saver_name(p->value));
        if (encoder == 0) for (int i = 0; i < 8; i++) for (int k = 0; k < 32; k++) sent[i][k] = 0xff;
        return 1;
    }
    if (!param_page(page)) return 1;
    int i = (layer() ? 6 : 0) + encoder;
    Param *p = &params[page][i];
    if (!p->name[0]) return 1;
    if (scene_held && p->hi <= 127) {                           /* the knob's place in the scene, not its own */
        int sc = scene_held == 1 ? scene_a : scene_b, id = page * 12 + i;
        int cur = scene_val[sc][id] >= 0 ? scene_val[sc][id] : pv_base(page, i);
        int sv = clamp(cur + delta, p->lo, p->hi);
        scene_val[sc][id] = (int8_t)sv; scene_dirty = 1;
        n[0] = scene_held == 1 ? 'A' : 'B'; n[1] = ' '; num(n + 2, sv); say(p->name, n);
        return 1;
    }
    int v = clamp(pv_base(page, i) + delta, p->lo, p->hi);
    p->user = p->value = (int16_t)v;
    if (page == PG_SKY) { if (i == 3) set_weather(v); if (i == 4) auto_weather = v; }
    page_until = (int)ticks + 240;
    static const char *const in_names[5] = {"OFF", "AB STEREO", "A MONO", "B MONO", "A+B MONO"};
    static const char *const cd_names[5] = {"OFF", "CD STEREO", "C MONO", "D MONO", "C+D MONO"};
    if (page == PG_SKY && (i == 3 || i == 6)) say(p->name, weather_name[v]);
    else if (page == PG_MIX && i == 6) say("IN AB", in_names[v]);
    else if (page == PG_MIX && i == 7) say("IN CD", cd_names[v]);
    else if (page == PG_GAIN && i >= 6 && i <= 9) { static const char *const bn[4] = {"EQ LOW", "EQ LO MID", "EQ HI MID", "EQ HIGH"}; signed_text(n, v - 13); int k = 0; while (n[k]) k++; n[k++] = 'D'; n[k++] = 'B'; n[k] = 0; say(bn[i - 6], n); }
    else if (page == PG_GAIN && i >= 10) {       /* a centre: 20 Hz x 1000^(v/127), shown to two figures */
        static const uint16_t hz[128] = {20,21,22,24,25,26,28,29,31,33,34,36,38,41,43,45,48,50,53,56,59,63,66,70,74,78,82,87,92,97,102,108,114,120,127,134,142,150,158,167,176,186,196,207,219,231,244,258,272,287,303,320,338,357,377,398,421,444,469,495,523,552,583,615,650,686,725,765,808,853,901,951,1004,1060,1120,1182,1248,1318,1392,1470,1552,1638,1730,1827,1929,2037,2150,2271,2398,2532,2673,2823,2980,3147,3323,3509,3705,3912,4130,4361,4605,4862,5134,5421,5724,6044,6382,6739,7116,7513,7933,8377,8845,9339,9862,10413,10995,11609,12258,12944,13667,14431,15238,16089,16989,17938,18941,20000};   /* 20 Hz x 1000^(v/127) */
        int f = hz[v & 127], k = 0;
        if (f >= 1000) { num(n, f / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + f % 1000 / 100); n[k++] = 'K'; } else { num(n, f); while (n[k]) k++; }
        n[k++] = 'H'; n[k++] = 'Z'; n[k] = 0; say(i == 10 ? "LO MID AT" : "HI MID AT", n);
    }
    else if (page == PG_GAIN && i != 5) { int u = i == 3 ? 108 : i == 4 ? 127 : 64; n[0] = v >= u ? '+' : '-'; num(n + 1, iabs(v - u)); say(p->name, n); }
    else if (page == PG_BALLOON && (i == 6 || i >= 9)) { int u = i == 6 ? 3 : 25; n[0] = v > u ? '+' : v < u ? '-' : ' '; num(n + 1, iabs(v - u)); say(i == 6 ? "OCTAVE" : i == 9 ? "TUNE 1" : i == 10 ? "TUNE 2" : "TUNE 3", n); }
    else if (page == PG_BALLOON && i == 0) { int ms = (441 + v * v * 11) * 10 / 441, k = 0; if (ms >= 1000) { num(n, ms / 1000); while (n[k]) k++; n[k++] = '.'; n[k++] = (char)('0' + ms % 1000 / 100); n[k++] = 'S'; } else { num(n, ms); while (n[k]) k++; n[k++] = 'M'; n[k++] = 'S'; } n[k] = 0; say("GRAIN SIZE", n); }
    else if (page == PG_BALLOON && (i == 7 || i == 8)) { num(n, v - 1); say(i == 7 ? "RANDOM OCT" : "FADE", n); }
    else if ((page == PG_LOOP && i == 2) || (page == PG_STARS && i == 6) || (page == PG_BALLOON && i == 2)) { pitch_text(n, (v - 64) * 1000); say("PITCH", n); }
    else if (page == PG_MASTER) master_say(i, v, n);
    else if (page == PG_MIX && i == 10) say("LIMITER", v == 1 ? "OFF" : "ON");
    else if (page == PG_MIX && i == 8) say("SCALE", v ? scale_list[v - 1].name : "OFF");
    else if (page == PG_MIX && i == 9) { static const char *const rn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; say("ROOT", rn[v % 12]); }
    else if (p->hi == 1) say(p->name, v ? "ON" : "OFF");
    else if (page == PG_SKY && i == 0) { num(n, v - 9); say("WIND", n); }
    else { num(n, v); say(p->name, n); }
    return 1;
}

#ifdef AERIAOS
/* AERIA OS's main loop: precise time between ticks (MIDI modules), and every MIDI message that comes in */
void module_fast(uint32_t us) { marbles_fast(us); pong_fast(us); master_fast(us); }
void module_midi(unsigned st, unsigned a, unsigned b) { out_midi(st, a, b); marbles_midi(st, a, b); }
#endif
