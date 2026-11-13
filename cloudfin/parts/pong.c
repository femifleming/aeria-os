/* ---- PONG: balls bounce round the screen like the old DVD logo; each wall a ball hits plays (sadnoise, 10 Nov 2026) --
 * YES adds a ball (up to 8), NO takes the last one away, trigs 1-8 mute a ball. FUNC+YES opens the card: a folder's
 * WAVs are dealt to the balls (ball n plays sample n, round the folder). Each wall is a different note of the scale --
 * left the root, then top, right and bottom higher by SPRD -- and each ball sits a little higher than the one before.
 *   SPD  how fast        SIZE the balls       PTCH the samples' pitch (64 as recorded)
 *   SPRD the walls' spread      DECY how long a hit rings      MODE SAMPLE, MIDI or BOTH
 *   FUNC: CHAN (MIDI channel) ROOT (MIDI note of the root) SCAL VEL LEVL (sample level) GRAV (falls a little)
 * MIDI: a note on CHAN at every hit, off after DECY. */
#if defined(AERIAOS)
void aos_pong(int slot, int ptch, int dec, int level);
int aos_slot_ready(int slot);
#define PONG_BALLS 8
#define PONG_BANK 7                    /* static slots 112..127 */
enum { PM_SAMPLE, PM_MIDI, PM_BOTH, N_PONGMODES };
static const char *const pong_mode_name[N_PONGMODES] = {"SAMPLE", "MIDI", "BOTH"};
typedef struct { uint8_t spd, size, ptch, sprd, decy, mode, chan, root, scal, vel, levl, grav, nballs, muted; } PongSet;
static PongSet pg = {50, 2, 64, 40, 50, 0, 1, 48, 0, 100, 100, 0, 0, 0};
typedef struct { int x, y, vx, vy; uint8_t flash; } Ball;
static Ball ball[PONG_BALLS];
static uint8_t pong_note[PONG_BALLS] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint32_t pong_off_us[PONG_BALLS];
#define PX0 3
#define PX1 124
#define PY0 11
#define PY1 54
static void pong_launch(int k)
{
    Ball *b = &ball[k];
    b->x = (PX0 + 6 + (int)(rnd() % (unsigned)(PX1 - PX0 - 12))) << 8; b->y = (PY0 + 6 + (int)(rnd() % (unsigned)(PY1 - PY0 - 12))) << 8;
    int a = (int)(rnd() % 1024u);
    b->vx = cos10(a) * 2; b->vy = sin10(a) * 2;
    if (iabs(b->vx) < 60) b->vx = b->vx < 0 ? -60 : 60;
    if (iabs(b->vy) < 60) b->vy = b->vy < 0 ? -60 : 60;
}
static int pong_scale_note(int deg)
{
    static const uint16_t sc[] = {0xab5, 0x5ad, 0x295, 0x6ad, 0xfff};
    uint16_t m = sc[pg.scal % 5];
    int deg_n[12], n = 0;
    for (int i = 0; i < 12; i++) if ((m >> i) & 1) deg_n[n++] = i;
    return (deg / n) * 12 + deg_n[deg % n];
}
static void pong_hit(int k, int wall)
{
    Ball *b = &ball[k];
    b->flash = 6;
    if ((pg.muted >> k) & 1) return;
    int deg = wall * (1 + pg.sprd / 24) + k;
    int semis = pong_scale_note(deg);
    if (pg.mode != PM_MIDI) {
        int slot = PONG_BANK * 16 + (k % 16);
        if (aos_slot_ready(slot)) aos_pong(slot, clamp(pg.ptch + semis, 0, 127), pg.decy, pg.levl);
    }
    if (pg.mode != PM_SAMPLE) {
        unsigned ch = (unsigned)((pg.chan - 1) & 15);
        if (pong_note[k] != 0xff) aos_midi_out(0x80u | ch, pong_note[k], 0);
        int note = clamp(pg.root + semis, 0, 127);
        aos_midi_out(0x90u | ch, (unsigned)note, pg.vel);
        pong_note[k] = (uint8_t)note;
        pong_off_us[k] = 20000u + (uint32_t)pg.decy * pg.decy * 60u;
    }
}
static void pong_tick(void)
{
    int sp = 20 + pg.spd * 3, r = pg.size;
    for (int k = 0; k < pg.nballs; k++) {
        Ball *b = &ball[k];
        if (b->flash) b->flash--;
        if (pg.grav) b->vy += pg.grav / 8 + 1;
        b->x += b->vx * sp / 256; b->y += b->vy * sp / 256;
        int x = b->x >> 8, y = b->y >> 8;
        if (x - r <= PX0) { b->x = (PX0 + r + 1) << 8; b->vx = iabs(b->vx); pong_hit(k, 0); }
        if (x + r >= PX1) { b->x = (PX1 - r - 1) << 8; b->vx = -iabs(b->vx); pong_hit(k, 2); }
        if (y - r <= PY0) { b->y = (PY0 + r + 1) << 8; b->vy = iabs(b->vy); pong_hit(k, 1); }
        if (y + r >= PY1) { b->y = (PY1 - r - 1) << 8; b->vy = -iabs(b->vy); if (pg.grav) b->vy = b->vy * 15 / 16 - 30; pong_hit(k, 3); }
    }
}
static void pong_fast(uint32_t us)
{
    for (int k = 0; k < PONG_BALLS; k++) if (pong_note[k] != 0xff) {
        if (pong_off_us[k] <= us) { aos_midi_out(0x80u | (unsigned)((pg.chan - 1) & 15), pong_note[k], 0); pong_note[k] = 0xff; }
        else pong_off_us[k] -= us;
    }
}
static void pong_draw(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 63, 0);
    for (int x = PX0; x <= PX1; x++) { px(x, PY0); px(x, PY1); }
    for (int y = PY0; y <= PY1; y++) { px(PX0, y); px(PX1, y); }
    for (int k = 0; k < pg.nballs; k++) {
        int x = ball[k].x >> 8, y = ball[k].y >> 8, r = pg.size + (ball[k].flash ? 1 : 0);
        int muted = (pg.muted >> k) & 1;
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r && (!muted || ((dx + dy) & 1))) px(x + dx, y + dy);
    }
    if (!pg.nballs) text(30, 30, "YES ADDS A BALL", 1);
    const char *lab[6], *val[6]; char nb[6][8];
    if (!layer()) {
        lab[0] = "SPD"; lab[1] = "SIZE"; lab[2] = "PTCH"; lab[3] = "SPRD"; lab[4] = "DECY"; lab[5] = "MODE";
        num(nb[0], pg.spd); num(nb[1], pg.size); num(nb[2], pg.ptch); num(nb[3], pg.sprd); num(nb[4], pg.decy);
        for (int i = 0; i < 5; i++) val[i] = nb[i]; val[5] = pong_mode_name[pg.mode];
    } else {
        static const char *const sn[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
        lab[0] = "CHAN"; lab[1] = "ROOT"; lab[2] = "SCAL"; lab[3] = "VEL"; lab[4] = "LEVL"; lab[5] = "GRAV";
        num(nb[0], pg.chan); num(nb[1], pg.root); num(nb[3], pg.vel); num(nb[4], pg.levl); num(nb[5], pg.grav);
        val[0] = nb[0]; val[1] = nb[1]; val[2] = sn[pg.scal % 5]; val[3] = nb[3]; val[4] = nb[4]; val[5] = nb[5];
    }
    for (int i = 0; i < 6; i++) text(2 + i * 21, 57, lab[i], 1);
    (void)val;
}
static void browser_open(int mode);
static int pong_key(int code)
{
    if (code == KEY_YES && func_held) { browser_open(BR_PONG); return 1; }
    if (code == KEY_YES) { if (pg.nballs < PONG_BALLS) { pong_launch(pg.nballs); pg.nballs++; } char n[4]; num(n, pg.nballs); say("BALLS", n); return 1; }
    if (code == KEY_NO) { if (pg.nballs) { pg.nballs--; int k = pg.nballs; if (pong_note[k] != 0xff) { aos_midi_out(0x80u | (unsigned)((pg.chan - 1) & 15), pong_note[k], 0); pong_note[k] = 0xff; } } char n[4]; num(n, pg.nballs); say("BALLS", n); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + PONG_BALLS) { int k = code - KEY_TRIG0; pg.muted ^= (uint8_t)(1u << k); say("BALL", (pg.muted >> k) & 1 ? "MUTED" : "ON"); return 1; }
    return 0;
}
static void pong_say(int e)
{
    static const char *const l1[6] = {"SPEED", "SIZE", "PITCH", "SPREAD", "DECAY", "MODE"}, *const l2[6] = {"CHANNEL", "ROOT", "SCALE", "VELOCITY", "LEVEL", "GRAVITY"};
    static const char *const sn[] = {"MAJOR", "MINOR", "PENTA", "DORIAN", "CHROM"};
    int v1[6] = {pg.spd, pg.size, pg.ptch, pg.sprd, pg.decy, 0}, v2[6] = {pg.chan, pg.root, 0, pg.vel, pg.levl, pg.grav};
    static char n[8];
    if (!layer()) { if (e == 5) say(l1[e], pong_mode_name[pg.mode]); else if (e == 2) { static char pn[24]; pitch_text(pn, (pg.ptch - 64) * 1000); say(l1[e], pn); } else { num(n, v1[e]); say(l1[e], n); } }
    else { if (e == 2) say(l2[e], sn[pg.scal % 5]); else { num(n, v2[e]); say(l2[e], n); } }
}
static void pong_enc(int e, int d)
{
    int st = d > 0 ? 1 : -1;
    if (!layer()) switch (e) {
        case 0: pg.spd = (uint8_t)clamp(pg.spd + d, 0, 127); break;
        case 1: pg.size = (uint8_t)clamp(pg.size + st, 1, 4); break;
        case 2: pg.ptch = (uint8_t)clamp(pg.ptch + st, 16, 112); break;
        case 3: pg.sprd = (uint8_t)clamp(pg.sprd + d, 0, 127); break;
        case 4: pg.decy = (uint8_t)clamp(pg.decy + d, 0, 127); break;
        case 5: pg.mode = (uint8_t)clamp(pg.mode + st, 0, N_PONGMODES - 1); break;
    } else switch (e) {
        case 0: pg.chan = (uint8_t)clamp(pg.chan + st, 1, 16); break;
        case 1: pg.root = (uint8_t)clamp(pg.root + st, 0, 115); break;
        case 2: pg.scal = (uint8_t)clamp(pg.scal + st, 0, 4); break;
        case 3: pg.vel = (uint8_t)clamp(pg.vel + d, 1, 127); break;
        case 4: pg.levl = (uint8_t)clamp(pg.levl + d, 0, 127); break;
        case 5: pg.grav = (uint8_t)clamp(pg.grav + d, 0, 127); break;
    }
    if (e >= 0 && e < 6) pong_say(e);
}
static void pong_leds(void)
{
    for (int k = 0; k < PONG_BALLS; k++) led_want[k] = k >= pg.nballs ? 0 : ((pg.muted >> k) & 1) ? 0 : ball[k].flash ? 3 : 1;
}
#endif
