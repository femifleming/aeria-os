/* ---- pages ------------------------------------------------------------------------------------ */
typedef struct {
    char name[5];
    uint8_t mask, slot;   /* tracks and lane slot; mask 0 = AERIA's own */
    uint8_t lo, hi;
    int16_t value, user;
    uint8_t drift;
} Param;
#define OWN 0, 0
#define EMPTY {"", 0, 0, 0, 0, 0, 0, 0}
#define T1M 0x01u
#define T2M 0x02u

enum { PG_CHAIN, PG_SKY, PG_SOURCE, PG_LOOP, PG_STARS, PG_BALLOON, PG_MIX, PG_SYSTEM, PG_MIDI, PG_GAIN, PG_MASTER, N_PAGES };
static const char *const page_name[N_PAGES] = {"CHAIN", "SKY", "SOURCE", "LOOP", "STARS", "BALLOON", "MIX", "SYSTEM", "GUMBALLS", "GAIN", "MASTER"};

static Param params[N_PAGES][12] = {
    /* CHAIN: the shelf */
    {EMPTY},
    /* SKY */
    {{"WIND", OWN, 0, 18, 12, 12, 0}, {"CLDS", OWN, 0, 7, 5, 5, 0}, {"WAVE", OWN, 0, 6, 2, 2, 0},
     {"WTHR", OWN, 0, N_WEATHER - 1, CLOUDY, CLOUDY, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, EMPTY,   /* (XFAD and XF B, the crossfade between two weathers, are gone: the fader is the scenes' now) */
     EMPTY, {"SPEL", OWN, 1, 8, 4, 4, 0}, {"DRFT", OWN, 0, 127, 20, 20, 0},
     {"RATE", OWN, 1, 127, 30, 30, 0}, {"NITE", OWN, 0, 1, 0, 0, 0}, {"LITE", OWN, 0, 1, 1, 1, 0}},
    /* SOURCE: a menu */
    {EMPTY},
    /* LOOP: Flex slot 1 on T1, between START and END */
    {{"STRT", T1M, 1, 0, 127, 0, 0, 0}, {"END", OWN, 1, 127, 127, 127, 0}, {"PTCH", T1M, 0, 16, 112, 64, 64, 0},
     {"RATE", T1M, 3, 0, 127, 127, 127, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"ATK", T1M, 12, 0, 127, 0, 0, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* STARS: one-shots on T2 */
    {{"SPD", OWN, 0, 127, 24, 24, 0}, {"NUM", OWN, 1, 8, 4, 4, 0}, {"PRND", OWN, 0, 127, 0, 0, 0},
     {"ARND", OWN, 0, 127, 0, 0, 0}, {"DRND", OWN, 0, 127, 20, 20, 0}, {"CHNC", OWN, 0, 127, 127, 127, 0},
     {"PTCH", OWN, 16, 112, 64, 64, 0}, {"ATK", OWN, 0, 127, 0, 0, 0}, {"DEC", OWN, 0, 127, 96, 96, 0},
     {"BANK", OWN, 0, 7, 0, 0, 0}, {"MODE", OWN, 0, 1, 0, 0, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}},
    /* BALLOON: the caught sounds, played as a cloud on T1 */
    /* (AERIA OS: every floating balloon sings its own long grains; the FUNC layer's six start at 1, so a preset
     * saved before they existed -- it holds 0 there -- keeps their defaults: persist.c) */
    {{"SIZE", T1M, 5, 0, 127, 60, 60, 0}, {"SPRY", OWN, 0, 127, 30, 30, 0}, {"PTCH", T1M, 0, 16, 112, 64, 64, 0},
     {"DENS", T1M, 4, 0, 127, 40, 40, 0}, {"POS", OWN, 0, 127, 20, 20, 0}, {"SEND", OWN, 0, 127, 100, 100, 0},
     {"OCT", OWN, 1, 5, 3, 3, 0}, {"RNDO", OWN, 1, 127, 1, 1, 0}, {"FADE", OWN, 1, 127, 1, 1, 0},
     {"TUN1", OWN, 1, 49, 25, 25, 0}, {"TUN2", OWN, 1, 49, 25, 25, 0}, {"TUN3", OWN, 1, 49, 25, 25, 0}},
    /* MIX: HEAD (T3 FX1): main feedback, its tone, the sources' sends, input level, warmth; then routing */
    /* (12 Nov 2026: the main feedback, its tone and its soft clip, WARM, are gone at the owner's wish) */
    {EMPTY, EMPTY, EMPTY,
     EMPTY, {"HELI", OWN, 0, 1, 0, 0, 0}, {"WSND", OWN, 0, 1, 1, 1, 0},
     {"INAB", OWN, 0, 4, 1, 1, 0}, {"INCD", OWN, 0, 4, 1, 1, 0}, {"SCAL", OWN, 0, 41, 0, 0, 0},
     {"ROOT", OWN, 0, 11, 0, 0, 0}, {"LIMT", OWN, 1, 2, 2, 2, 0}, {"TIDE", OWN, 0, 127, 0, 0, 0}},   /* LIMT 2 on, 1 off (0: a preset from before it) */
    /* SYSTEM */
    {{"SND", OWN, 0, 2, 1, 1, 0}, {"INTR", OWN, 0, 1, 1, 1, 0}, {"LITE", OWN, 0, 1, 1, 1, 0},
     {"AUTO", OWN, 0, 1, 1, 1, 0}, {"SAVR", OWN, 0, 30, 30, 30, 0}, {"WAIT", OWN, 0, 7, 3, 3, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* GUMBALLS: MIDI, its own menu */
    {EMPTY},
    /* GAIN: input A/B and C/D (THRU VOL, 64 = unity), the drive into the chain (HEAD IN),
     * the chain's output (T8 AMP VOL, 108 = unity), the cue send and the balance */
    {{"AB", OWN, 0, 127, 64, 64, 0}, {"CD", OWN, 0, 127, 64, 64, 0}, {"DRIV", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"CUE", OWN, 0, 127, 127, 127, 0}, {"BAL", OWN, 0, 127, 64, 64, 0},
     /* FUNC layer: the master EQ (12 Nov 2026), after everything and never moved by the weather. Four gains 1..25
      * (13 = flat, a dB a step) and the two middle bands' centres (an index on 20 Hz .. 20 kHz); the shelves sit at
      * 150 Hz and 6 kHz. (They start at 1 so a preset from before them, which holds 0 there, keeps them flat.) */
     {"LOW", OWN, 1, 25, 13, 13, 0}, {"LMID", OWN, 1, 25, 13, 13, 0}, {"HMID", OWN, 1, 25, 13, 13, 0},
     {"HIGH", OWN, 1, 25, 13, 13, 0}, {"LMF", OWN, 1, 127, 55, 55, 0}, {"HMF", OWN, 1, 127, 89, 89, 0}},
    /* MASTER (12 Nov 2026): the freeze delay on the main mix, after the chain and never moved by the weather. TIME in
     * sixteenth notes at TMPO (the trigs pick it too), its feedback with a high-pass and a lowpass, how much goes in
     * and how much comes out; YES freezes it, NO taps the tempo. FUNC layer: TMPO (BPM). */
    {{"TIME", OWN, 1, 16, 4, 4, 0}, {"FDBK", OWN, 0, 127, 50, 50, 0}, {"HPF", OWN, 0, 127, 10, 10, 0},
     {"LPF", OWN, 0, 127, 100, 100, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"WET", OWN, 0, 127, 0, 0, 0},
     {"TMPO", OWN, 0, 127, 60, 60, 0}, {"XFRZ", OWN, 1, 2, 2, 2, 0}, {"RFRZ", OWN, 0, 127, 0, 0, 0}, {"XMOD", OWN, 0, 3, 0, 0, 0}, EMPTY, EMPTY},   /* XMOD: what the crossfader does: SCEN, DJF (a DJ filter), WET (every effect dry at A), MOD (MOD depth 0 at A) */   /* RFRZ: Chloaelis's random freeze, the chance of a hold (12 Nov 2026) */   /* XFRZ 2: the crossfader freezes the delay at B (1: it does not) */
};
#define P(page, i) (params[page][i])
/* ---- scenes (sadnoise, 12 Nov 2026), as on the Octatrack: the crossfader moves the sound between two of them -------
 * There are sixteen scenes; A holds one and B another (hold SCENE A or SCENE B and press a trig to choose which).
 * A scene is a set of knob positions: hold SCENE A (or B) and turn any knob, and that position is the scene's for
 * that knob -- the knob's own setting stays as it was. With the fader at A every knob in scene A's set stands at
 * the scene's position, at B at scene B's, and in between it glides; a knob only one of them holds glides between
 * that and its own setting. Hold a scene key and press NO: that scene forgets everything.
 * A knob is a page's (page x 12 + its place) or a chain slot's (132 + slot x 6 + knob). */
#define N_SCENES 16
#define SCENE_IDS (N_PAGES * 12 + 7 * 6)
static int8_t scene_val[N_SCENES][SCENE_IDS];   /* -1: the scene does not hold this knob */
static uint8_t scene_a, scene_b = 1, scene_dirty, scene_ready;
static void scenes_init(void) { if (scene_ready) return; for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) scene_val[s][i] = -1; scene_ready = 1; }
static int xf_mode(void);
static int scene_mix(int id, int base)
{
    if (!scene_ready || xf_mode()) return base;              /* scenes only while the crossfader is SCEN */
    int a = scene_val[scene_a][id], b = scene_val[scene_b][id];
    if (a < 0 && b < 0) return base;
    int va = a < 0 ? base : a, vb = b < 0 ? base : b, x = xfade();
    return va + ((vb - va) * x + (vb >= va ? 63 : -63)) / 127;
}
static int pv_base(int page, int i) { Param *p = &params[page][i]; return p->user >= 0 ? p->user : p->value; }
static int16_t mod_pv[N_PAGES * 12];   /* MOD's offsets (mod.c), knob units */
static int pv(int page, int i) { int v = scene_mix(page * 12 + i, pv_base(page, i)); if (mod_pv[page * 12 + i]) { const Param *p = &params[page][i]; v += mod_pv[page * 12 + i]; if (v < p->lo) v = p->lo; if (v > p->hi) v = p->hi; } return v; }

/* sources: off by default */
static int src_loop, src_stars, src_balloons;

/* ---- state -------------------------------------------------------------------------- */
static int muted[8];
static int night, hidden, func_held, func_latch, ready, page, page_until, intro;
static uint32_t ui_until; /* the page shows until then; after, only the sky */
#define UI_SHOW (60u * 10u)
static int scene_held;
static uint32_t held_tracks, stars_total;
static int16_t drift_now[N_PAGES][6], drift_target[N_PAGES][6];
static int algo; /* kept for the stars/player code paths: 2 = stars running */
enum { ALGO_NONE, ALGO_GRAINS, ALGO_STARS, ALGO_PLAYER, ALGO_BALLOON };
#define PG_PLAYER PG_LOOP
/* T5 THRU + T6-T8 NEIGHBOR (one DSP core: NEIGHBOR cannot sit on T1 or T5); sources on the other core.
 * T1 is the sample source, so stock's file preview (it borrows T1's voice) never touches the inputs. */
#define HEAD_T 4           /* T5: the inputs, HEAD in FX1 */
#define LAST_T 7           /* T8: the chain's end */
#define SAMPLE_T 0         /* T1: the loop or the balloons */
#define STARS_T 1          /* T2 */

static void set_weather(int w);
static void sync_to_card(void);
static int balloon_placed;
static void place_balloon(int step);
#if defined(AERIAOS)
static void xw_auto(int w);
#endif
static void step_auto(void)
{
    if (!auto_weather || spell_left-- > 0) return;
    int pick = range(0, 19), w = 0;
    for (; w < N_WEATHER - 1; w++) { pick -= next_w[weather][w]; if (pick < 0) break; }
    set_weather(w == weather ? CLOUDY : w);
#if defined(AERIAOS)
    xw_auto(weather);
#endif
}

