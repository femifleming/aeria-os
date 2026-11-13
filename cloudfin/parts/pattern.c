/* ---- the pattern: recognise it, then place one step ahead ------------------------------ */
/* Bank-file TRAC data (ot_bank.py): masks at +0, 0xaa at +0x40..0x47, LENGTH +0x50.
 * The RAM record's origin is found, not assumed: the eight 0xaa bytes must sit at
 * the same offset on all eight tracks, with sane lengths, or nothing is written. */
static int pat_off = -100; /* -100 unknown, -99 refused */
static void pattern_probe(void)
{
    if (pat_off != -100) return;
#ifndef PREVIEW
    if (!bank_base()) return; /* no project yet: ask again next tick */
#endif
    for (int off = -48; off <= 48; off++) {
        int good = 1;
        for (int t = 0; t < 8 && good; t++) {
            volatile uint8_t *r = pattern_track(t) + off;
            for (int i = 0x40; i < 0x48; i++) if (r[i] != 0xaa) { good = 0; break; }
            if (good && (r[0x50] == 0 || r[0x50] > 64)) good = 0;
        }
        if (good) { pat_off = off; return; }
    }
    pat_off = -99;
}
static volatile uint8_t *pat(int t) { return pattern_track(t) + pat_off; }
static int pat_len(int t) { int n = pat_off > -99 ? pat(t)[0x50] : 16; return n >= 1 && n <= 64 ? n : 16; }
static void pat_trig(int t, int step, int on)
{
    if (pat_off <= -99) return;
    volatile uint8_t *m = pat(t) + 7 - (step >> 3);
    if (on) *m = (uint8_t)(*m | (1u << (step & 7))); else *m = (uint8_t)(*m & ~(1u << (step & 7)));
}
static void pat_lock(int t, int step, int slot, int v)
{
    if (pat_off <= -99) return;
    pat(t)[PAT_LOCKS + 32u * (unsigned)step + (unsigned)slot] = (uint8_t)v;
}
static void pat_clear_step(int t, int step)
{
    pat_trig(t, step, 0);
    static const uint8_t slots[] = {0, 1, 2, 12, 13, 14, 31};
    for (unsigned i = 0; i < sizeof slots; i++) pat_lock(t, step, slots[i], NOLOCK);
}

/* ---- STARS: an astrolabe of samples ------------------------------------------------------- */
#define PLANETS 16
static uint16_t star_a[8];
static int16_t bird_x[8], bird_y[8], bird_vx[8], bird_vy[8];
static uint8_t planet_x[PLANETS], planet_y[PLANETS];
static uint16_t planet_mask = 0xffff;
static int star_rot, planet_rot, star_flash, star_hit = -1, planet_hit = -1;
#define RING_X 64
#define RING_Y 30
#define RING_R 22

static void new_constellation(void)
{
    for (int i = 0; i < 8; i++) star_a[i] = (uint16_t)range(0, 1023);
    for (int i = 0; i < 8; i++) { bird_x[i] = (int16_t)(range(-14, 14) * 16); bird_y[i] = (int16_t)(range(-14, 14) * 16); bird_vx[i] = (int16_t)range(-8, 8); bird_vy[i] = (int16_t)range(-8, 8); }
    for (int p = 0; p < PLANETS; p++) {
        int a = range(0, 1023), r = range(4, RING_R - 3);
        planet_x[p] = (uint8_t)(RING_X + cos10(a) * r / 127);
        planet_y[p] = (uint8_t)(RING_Y + sin10(a) * r / 127);
    }
}
static int planet_ring_angle(int p) { return (p * 64 + planet_rot) & 1023; }

/* which planet (0..15) sings at this step, or -1 */
static int stars_decide(void)
{
    int n = pv(PG_STARS, 1);
    if (pv(PG_STARS, 10) == 0) { /* ring: a star crossing the needle (top, angle 768) */
        int adv = 4 + pv(PG_STARS, 0) * 3;
        star_rot = (star_rot + adv) & 1023;
        planet_rot = (planet_rot - adv * 3 / 7) & 1023;
        for (int i = 0; i < n; i++) {
            int a = (star_a[i] + star_rot - 768) & 1023;
            if (a < adv) {
                star_hit = i;
                int best = -1, bd = 2048;
                for (int p = 0; p < PLANETS; p++) {
                    if (!(planet_mask & (1u << p))) continue;
                    int d = (planet_ring_angle(p) - 768) & 1023;
                    if (d > 512) d = 1024 - d;
                    if (d < bd) { bd = d; best = p; }
                }
                return best;
            }
        }
        return -1;
    }
    for (int i = 0; i < n; i++)
        for (int p = 0; p < PLANETS; p++) {
            if (!(planet_mask & (1u << p))) continue;
            int dx = RING_X + (bird_x[i] >> 4) - planet_x[p], dy = RING_Y + (bird_y[i] >> 4) - planet_y[p];
            if (dx * dx + dy * dy < 12) { star_hit = i; return p; }
        }
    return -1;
}
static void birds_tick(void)
{
    int n = pv(PG_STARS, 1), spd = 2 + pv(PG_STARS, 0) / 16;
    for (int i = 0; i < n; i++) {
        if ((rnd() & 31) == 0) { bird_vx[i] = (int16_t)clamp(bird_vx[i] + range(-4, 4), -spd, spd); bird_vy[i] = (int16_t)clamp(bird_vy[i] + range(-4, 4), -spd, spd); }
        bird_x[i] = (int16_t)(bird_x[i] + bird_vx[i]);
        bird_y[i] = (int16_t)(bird_y[i] + bird_vy[i]);
        int x = bird_x[i] >> 4, y = bird_y[i] >> 4;
        if (x * x + y * y > (RING_R - 2) * (RING_R - 2)) { bird_vx[i] = (int16_t)(-bird_vx[i] - (x > 0) + (x < 0)); bird_vy[i] = (int16_t)(-bird_vy[i] - (y > 0) + (y < 0)); bird_x[i] = (int16_t)(bird_x[i] * 15 / 16); bird_y[i] = (int16_t)(bird_y[i] * 15 / 16); }
    }
}

/* ---- the sequencer side: one step ahead ---------------------------------------------------------- */
static uint32_t stars_total;
static int last_step = -1, star_placed = -1, player_placed = -1, pad_pending = -1;

static void place_star(int step)
{
    int p = stars_decide();
    planet_hit = -1;
    if (p < 0 || muted[T_STARS] || (int)(rnd() % 128u) > pv(PG_STARS, 5)) return;
    int slot = pv(PG_STARS, 9) * PLANETS + p;
    int prnd = pv(PG_STARS, 2), arnd = pv(PG_STARS, 3), drnd = pv(PG_STARS, 4);
    int ptch = clamp(pv(PG_STARS, 6) + (prnd ? range(-prnd / 4, prnd / 4) : 0), 0, 127);
    int atk = clamp(pv(PG_STARS, 7) + (arnd ? range(0, arnd / 2) : 0), 0, 127);
    int dec = clamp(pv(PG_STARS, 8) + (drnd ? range(-drnd / 2, drnd / 2) : 0), 0, 127);
    pat_lock(T_STARS, step, 31, slot);
    pat_lock(T_STARS, step, 0, ptch);
    pat_lock(T_STARS, step, 12, atk);
    pat_lock(T_STARS, step, 13, dec);      /* HOLD: how long it rings */
    pat_lock(T_STARS, step, 14, dec);      /* REL */
    pat_trig(T_STARS, step, 1);
#if defined(PREVIEW) && defined(AERIAOS)
    { void aos_star_at(int step, int len, int slot, int ptch, int atk, int dec); aos_star_at(step, pat_len(T_STARS), slot, ptch, atk, dec); }
#endif
    star_placed = step;
    planet_hit = p;
    stars_total++;
}
__attribute__((unused)) static void place_player(int step)
{
    if (pad_pending < 0) return;
    pat_lock(T_PLAYER, step, 1, pad_pending * 127 / 15);
    pat_lock(T_PLAYER, step, 31, pv(PG_PLAYER, 9));
    pat_trig(T_PLAYER, step, 1);
    player_placed = step;
    pad_pending = -1;
}
static void sequencer_tick(void)
{
    pattern_probe();
    if (!playing() || !rig_loaded()) { last_step = -1; return; }
    int len = pat_len(T_STARS);
    int s = (int)(master_step() % (unsigned)len);
    if (s == last_step) return;
    last_step = s;
    if (star_placed >= 0 && star_placed != (s + 1) % len) { pat_clear_step(T_STARS, star_placed); star_placed = -1; }
    if (player_placed >= 0 && player_placed != (s + 1) % len) { pat_clear_step(T_PLAYER, player_placed); player_placed = -1; }
    if (planet_hit >= 0) star_flash = 10;
    int next = (s + 1) % len;
    if (algo == ALGO_STARS) place_star(next); else planet_hit = -1;
    if (balloon_placed >= 0 && balloon_placed != next) { pat_clear_step(SAMPLE_T, balloon_placed); balloon_placed = -1; }
    place_balloon(next);
}

