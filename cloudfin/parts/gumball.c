/* ---- GUMBALLS: four balloons, one MIDI channel each; gumballs bounce inside and play notes --------
 * MIDI out through stock's midi_send 0x40010bc8(len, bytes) (research-midi.md 1): atomic, mirrored to
 * USB MIDI by the base. It spins forever if its 4 KB ring is full, so every send first checks the ring
 * count 0x400b967c and keeps to 36 bytes a tick. Off by default; switching it off sends note-offs. */
#define GB_BALLOONS 4
#define GB_BALLS 6
#define GB_R 13            /* balloon radius */
#define GB_BALL 2          /* gumball radius */
typedef struct { int16_t x, y, vx, vy; uint8_t live, note, ch; uint8_t gate; uint8_t hue; } Gum;
static Gum gums[GB_BALLOONS][GB_BALLS];
static int gum_on, gum_cleared;
static unsigned gum_bytes;
/* settings: scale, root, octave (8 = random), gravity, bounce, gate | velocity, spread, first channel */
static int gb_scale, gb_root, gb_oct = 4, gb_grav = 40, gb_bounce = 80, gb_gate = 8, gb_vel = 110, gb_spread = 1, gb_chan = 0;
static const uint16_t gb_scales[] = {0xab5, 0x5ad, 0x295, 0x4a9, 0x6ad, 0xad5, 0x56b, 0xfff, 0x555, 0x891};
static const char *const gb_scale_name[] = {"MAJOR", "MINOR", "PENTA", "MIN PENT", "DORIAN", "LYDIAN", "PHRYGIAN", "CHROMATIC", "WHOLE", "STACKED"};
#define GB_NSCALES ((int)(sizeof gb_scales / sizeof gb_scales[0]))
static const char *const gb_note_name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#if defined(PREVIEW) && defined(AERIAOS)
int aos_midi_out(unsigned st, unsigned a, unsigned b);
static int midi_out(unsigned st, unsigned a, unsigned b) { gum_bytes += 3; return aos_midi_out(st, a, b); }
#elif defined(PREVIEW)
static int midi_out(unsigned st, unsigned a, unsigned b) { (void)st; (void)a; (void)b; gum_bytes += 3; return 1; }
#else
static uint8_t gb_tx[3];
static int midi_out(unsigned st, unsigned a, unsigned b)
{
    if (gum_bytes + 3 > 36 || *(volatile uint32_t *)0x400b967cu > 3000u) return 0;
    gb_tx[0] = (uint8_t)st; gb_tx[1] = (uint8_t)(a & 127); gb_tx[2] = (uint8_t)(b & 127);
    ((void (*)(int, const void *))0x40010bc8u)(3, gb_tx);
    gum_bytes += 3;
    return 1;
}
#endif

/* a random note of the scale: degree, octave (or a random one), spread */
static int gb_note(void)
{
    uint16_t m = gb_scales[gb_scale];
    int deg[12], n = 0;
    for (int k = 0; k < 12; k++) if ((m >> k) & 1) deg[n++] = k;
    if (!n) deg[n++] = 0;
    int oct = gb_oct >= 8 ? range(2, 6) : gb_oct;
    oct += gb_spread ? range(0, gb_spread) : 0;
    return clamp(12 * (oct + 1) + gb_root + deg[range(0, n - 1)], 0, 127);
}
static int gb_cx(int b) { return 16 + b * 32; }
#define GB_CY 32

static void gum_drop(int b)
{
    for (int i = 0; i < GB_BALLS; i++) {
        Gum *g = &gums[b][i];
        if (g->live) continue;
        g->live = 1; g->gate = 0; g->note = 0xff;
        g->x = (int16_t)((gb_cx(b) + range(-4, 4)) * 16); g->y = (int16_t)((GB_CY - GB_R + 4) * 16);
        g->vx = (int16_t)range(-20, 20); g->vy = 0; g->hue = (uint8_t)range(0, 3);
        g->ch = (uint8_t)((gb_chan + b) & 15);
        return;
    }
}
static void gum_note_off(Gum *g)
{
    if (g->note != 0xff && midi_out(0x80u | g->ch, g->note, 0)) g->note = 0xff;
}
static void gum_take(int b)
{
    for (int i = GB_BALLS - 1; i >= 0; i--) if (gums[b][i].live) { gum_note_off(&gums[b][i]); gums[b][i].live = 0; return; }
}
static void gum_all_off(void)
{
    for (int b = 0; b < GB_BALLOONS; b++) for (int i = 0; i < GB_BALLS; i++) gum_note_off(&gums[b][i]);
}

static void gum_tick(void)
{
    gum_bytes = 0;
    if (!gum_cleared) { /* the first tick after load: nothing a previous version left may keep sounding */
        int ok = 1;
        for (int g = 0; g < GB_BALLOONS; g++) for (int i = 0; i < GB_BALLS; i++) if (!gums[g][i].live) gums[g][i].note = 0xff;
        for (int c = 0; c < GB_BALLOONS && ok; c++) ok = midi_out(0xb0u | ((gb_chan + c) & 15), 123, 0);
        if (ok) gum_cleared = 1;
        return;
    }
    for (int b = 0; b < GB_BALLOONS; b++)
        for (int i = 0; i < GB_BALLS; i++) {
            Gum *g = &gums[b][i];
            if (g->note != 0xff && g->gate && --g->gate == 0) gum_note_off(g);
            if (!g->live || !gum_on) continue;
            /* gravity, then the balloon's skin: reflect off the circle, losing energy each bounce */
            g->vy = (int16_t)(g->vy + 1 + gb_grav / 20);
            g->x = (int16_t)(g->x + g->vx); g->y = (int16_t)(g->y + g->vy);
            int dx = g->x - gb_cx(b) * 16, dy = g->y - GB_CY * 16, lim = (GB_R - GB_BALL - 1) * 16;
            int d2 = (dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4);
            if (d2 > (lim >> 4) * (lim >> 4)) {
                int d = isqrt(d2); if (d < 1) d = 1;
                int nx = dx / d, ny = dy / d;           /* unit normal x16 (dx is in 1/16 px, d in px) */
                int dot = (g->vx * nx + g->vy * ny) / 16;
                if (dot > 0) {
                    int speed = dot;
                    g->vx = (int16_t)((g->vx - 2 * dot * nx / 16) * gb_bounce / 100 + range(-2, 2));
                    g->vy = (int16_t)((g->vy - 2 * dot * ny / 16) * gb_bounce / 100);
                    /* the bounce plays: louder for a hard hit, softer as it dribbles */
                    if (speed > 6) {
                        gum_note_off(g);
                        int vel = clamp(speed * gb_vel / 60, 8, 127);
                        int n = gb_note();
                        if (midi_out(0x90u | g->ch, (unsigned)n, (unsigned)vel)) { g->note = (uint8_t)n; g->gate = (uint8_t)(2 + gb_gate * 3); }
                    }
                }
                g->x = (int16_t)(gb_cx(b) * 16 + dx * lim / (16 * d));
                g->y = (int16_t)(GB_CY * 16 + dy * lim / (16 * d));
            }
            /* at rest it is tossed up again, so a dropped gumball keeps playing */
            if (iabs(g->vy) < 3 && iabs(g->vx) < 3 && g->y > (GB_CY + GB_R - 6) * 16) { g->vy = (int16_t)-range(30, 55); g->vx = (int16_t)range(-18, 18); }
        }
}

static void gum_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 10, 56, 0);
    for (int b = 0; b < GB_BALLOONS; b++) {
        int cx = gb_cx(b), sway = sine[(int)(ticks / 2 + (uint32_t)b * 11) & 63] / 90;
        circle(cx + sway, GB_CY, GB_R);
        px(cx + sway, GB_CY + GB_R + 1); px(cx + sway - 1, GB_CY + GB_R + 2); px(cx + sway + 1, GB_CY + GB_R + 2);
        for (int y = GB_CY + GB_R + 3; y < 57; y++) px(cx + sway + sine[(y * 5 + (int)ticks) & 63] / 100, y);
        char c[4]; num(c, ((gb_chan + b) & 15) + 1);
        text(cx - 2, 11, c, 1);
        for (int i = 0; i < GB_BALLS; i++) {
            Gum *g = &gums[b][i];
            if (!g->live) continue;
            int gx = (g->x >> 4) + sway, gy = g->y >> 4;
            for (int dy = -GB_BALL; dy <= GB_BALL; dy++) for (int dx = -GB_BALL; dx <= GB_BALL; dx++)
                if (dx * dx + dy * dy <= GB_BALL * GB_BALL && (g->hue == 0 || dither_on(gx + dx, gy + dy, 6 + 3 * g->hue))) px(gx + dx, gy + dy);
            if (g->note != 0xff) circle(gx, gy, GB_BALL + 2);
        }
    }
    char line[24]; int i = 0;
    const char *s = gb_scale_name[gb_scale];
    while (*s) line[i++] = *s++;
    line[i++] = ' ';
    s = gb_note_name[gb_root]; while (*s) line[i++] = *s++;
    line[i++] = ' ';
    if (gb_oct >= 8) { line[i++] = 'O'; line[i++] = '?'; } else { line[i++] = 'O'; line[i++] = (char)('0' + gb_oct); }
    line[i] = 0;
    text(2, 58, line, 1);
    text(W - 2 - text_w(gum_on ? "MIDI ON" : "MIDI OFF"), 58, gum_on ? "MIDI ON" : "MIDI OFF", 1);
}

static int gum_key(int code)
{
    if (code >= 0x00 && code < 0x04) { gum_drop(code); return 1; }
    if (code >= 0x08 && code < 0x0c) { gum_take(code - 0x08); return 1; }
    if (code == KEY_YES) { gum_on = !gum_on; if (!gum_on) gum_all_off(); say("GUMBALLS", gum_on ? "ON" : "OFF"); return 1; }
    return 0;
}
static void gum_enc(int e, int d, int func)
{
    char n[8];
    int s = d > 0 ? 1 : -1;
    if (!func) {
        if (e == 0) { gb_scale = clamp(gb_scale + s, 0, GB_NSCALES - 1); say("SCALE", gb_scale_name[gb_scale]); }
        else if (e == 1) { gb_root = (gb_root + s + 12) % 12; say("ROOT", gb_note_name[gb_root]); }
        else if (e == 2) { gb_oct = clamp(gb_oct + s, 0, 8); if (gb_oct == 8) say("OCTAVE", "RANDOM"); else { num(n, gb_oct); say("OCTAVE", n); } }
        else if (e == 3) { gb_grav = clamp(gb_grav + d, 0, 127); num(n, gb_grav); say("GRAVITY", n); }
        else if (e == 4) { gb_bounce = clamp(gb_bounce + d, 30, 98); num(n, gb_bounce); say("BOUNCE", n); }
        else if (e == 5) { gb_gate = clamp(gb_gate + d, 1, 40); num(n, gb_gate); say("LENGTH", n); }
    } else {
        if (e == 0) { gb_vel = clamp(gb_vel + d, 10, 127); num(n, gb_vel); say("VELOCITY", n); }
        else if (e == 1) { gb_spread = clamp(gb_spread + s, 0, 3); num(n, gb_spread); say("SPREAD", n); }
        else if (e == 2) { gum_all_off(); gb_chan = clamp(gb_chan + s, 0, 12); num(n, gb_chan + 1); say("FIRST CH", n); for (int b = 0; b < GB_BALLOONS; b++) for (int i = 0; i < GB_BALLS; i++) gums[b][i].ch = (uint8_t)((gb_chan + b) & 15); }
    }
}

/* for the lights: how many gumballs a balloon holds, and whether one is sounding */
static int gum_count(int b) { int n = 0; for (int i = 0; i < GB_BALLS; i++) n += gums[b][i].live; return n; }
static int gum_sounding(int b) { for (int i = 0; i < GB_BALLS; i++) if (gums[b][i].note != 0xff) return 1; return 0; }
