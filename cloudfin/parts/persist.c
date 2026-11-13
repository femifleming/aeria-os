/* ---- AERIA OS: the saved state and the presets (sadnoise, 10 Nov 2026) ----------------------------------------
 * A snapshot is every page parameter (the musician's setting, or -1: follow the weather), the chain (which effect
 * in each slot, its knobs, each slot's own knob settings and bypasses), the weather, auto, the crossfader's weather,
 * day/night, the sources and the planets, with a checksum. AERIA OS keeps the state and 16 presets in battery RAM:
 * the state a couple of seconds after anything changes, a preset when the musician saves it.
 *   PROJ: the presets. A trig loads that preset; FUNC + trig saves the sound into it; NO (or PROJ) closes.
 * Only in AERIA OS: the Modwerk module build has no battery RAM of its own to keep this in. */
#if defined(AERIAOS)
static uint8_t led_want[16];   /* ui12.c's (a tentative definition: the same object) */
#define SNAP_MAGIC 0x41455338u   /* 'AES8': saved since the weather leaves MIX alone (the dry start holds) */
#define SNAP_MAGIC_V7 0x41455337u   /* 'AES7': a page more (MASTER); kept in the bigger store */
#define SNAP_MAGIC_V6 0x41455336u   /* 'AES6': AES4's layout; saved since untouched effects start dry (slot_gentle) */
#define SNAP_MAGIC_V5 0x41455335u   /* a build of 12 Nov 2026 that lost the dry start on the way: same as AES4 */
#define SNAP_MAGIC_V4 0x41455334u   /* 'AES4': AES3 + the chain slots' FUNC-layer knobs */
#define SNAP_MAGIC_V3 0x41455333u   /* 'AES3': AES2 + OUT/panners, the grain cloud, MARBLES, PONG, the MIDI module */
#define SNAP_MAGIC_V2 0x41455332u
/* The store (12 Nov 2026): 1 KB a snapshot at 0x100E0000 (slot 0 the state, 1..16 the presets). Until then they
 * were 512 bytes each at 0x100F9800, full, and a page shorter: what is found there is brought over once, its page
 * rows moved up to make room for the new page's, its version kept (so what was saved before the dry start still
 * gets it). In the new store every version has the new layout. */
#define SNAP_STRIDE 1024u
#define SNAP_OLD_BASE 0x100F9800u
#define SNAP_OLD_STRIDE 512u
#ifndef SNAP_BASE
#define SNAP_BASE 0x100E0000u
#endif
#define N_PRESETS 16
typedef struct {
    uint32_t magic, sum;
    int8_t user[N_PAGES][12];
    uint8_t value[N_PAGES][12];
    uint8_t live_fx[16];
    uint8_t part_fx[8][12];
    int8_t slot_user[N_SLOTS][6];
    int16_t weather, auto_weather, xf_right, night, src_loop, src_stars, src_balloons;
    uint16_t planet_mask, slot_bypass;
    /* v3 (10 Nov 2026) */
    PanSet pan[N_SLOTS];
    uint8_t pan_chan, midi_mod;
    CloudSet cloud;
    MarbSet marb;
    PongSet pong;
} Snap;
/* v4 (10 Nov 2026): the chain slots' FUNC-layer knobs, 42 bytes. The snapshot is full (512 bytes a preset, and the
 * presets sit back to back), so they live in the rows of two pages that have no knobs of their own -- CHAIN and
 * SOURCE, 24 bytes each across value[] and user[] -- which a v3 snapshot left at 0. The layout is v3's. */
#define SNAP_V3_SIZE ((unsigned)sizeof(Snap))
static uint8_t *snap_p2(Snap *s, int i, int k)
{
    int n = i * 6 + k;
    return n < 12 ? &s->value[PG_CHAIN][n] : n < 24 ? (uint8_t *)&s->user[PG_CHAIN][n - 12] : n < 36 ? &s->value[PG_SOURCE][n - 24] : (uint8_t *)&s->user[PG_SOURCE][n - 36];
}
#define SNAP_V2_SIZE ((unsigned)__builtin_offsetof(Snap, pan))
typedef char snap_fits[sizeof(Snap) <= SNAP_STRIDE ? 1 : -1];
static Snap snap_now;
static uint32_t snap_sum_n(const Snap *s, unsigned size)
{
    const uint8_t *b = (const uint8_t *)s + 8;
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 8; i < size; i++) h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t snap_sum(const Snap *s) { return snap_sum_n(s, sizeof(Snap)); }
static void snap_take(Snap *s)
{
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof(Snap); i++) b[i] = 0;
    for (int p = 0; p < N_PAGES; p++) for (int k = 0; k < 12; k++) {
        s->user[p][k] = (int8_t)(params[p][k].user < 0 ? -1 : params[p][k].user > 127 ? 127 : params[p][k].user);
        s->value[p][k] = (uint8_t)(params[p][k].value & 0x7f);
    }
    for (int i = 0; i < 16; i++) s->live_fx[i] = preview_live_fx[i];
    for (int t = 0; t < 8; t++) for (int k = 0; k < 12; k++) s->part_fx[t][k] = preview_part_fx[t][k];
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) s->slot_user[i][k] = (int8_t)(slots[i].user[k] < 0 ? -1 : slots[i].user[k] > 127 ? 127 : slots[i].user[k]);
    s->weather = (int16_t)weather; s->auto_weather = (int16_t)auto_weather; s->xf_right = (int16_t)(0x100 | wx_on | (xw_on & 7) << 9);   /* (the field the weather crossfade used: now which weathers are on) */ s->night = (int16_t)night;
    s->src_loop = (int16_t)src_loop; s->src_stars = (int16_t)src_stars; s->src_balloons = (int16_t)src_balloons;
    s->planet_mask = planet_mask; s->slot_bypass = slot_bypass;
    for (int i = 0; i < N_SLOTS; i++) s->pan[i] = pan_set[i];
    s->pan_chan = pan_chan; s->midi_mod = (uint8_t)midi_mod_get(); s->cloud = cloud; s->marb = mb; s->pong = pg;
    s->marb.on = 0;   /* a restart never starts MARBLES by itself */
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) *snap_p2(s, i, k) = preview_slot_p2[i][k];
    s->magic = SNAP_MAGIC; s->sum = snap_sum(s);
}
/* 3: a v3 snapshot; 2: an AES2 one (the part before the panners is the same layout); 0: none */
static int snap_version(const volatile Snap *s)
{
    if (s->sum != snap_sum((const Snap *)s)) return 0;
    return s->magic == SNAP_MAGIC ? 8 : s->magic == SNAP_MAGIC_V7 ? 7 : s->magic == SNAP_MAGIC_V6 ? 6 : (s->magic == SNAP_MAGIC_V4 || s->magic == SNAP_MAGIC_V5) ? 4 : s->magic == SNAP_MAGIC_V3 ? 3 : s->magic == SNAP_MAGIC_V2 ? 2 : 0;
}
static int snap_valid(const volatile Snap *s) { return snap_version(s) != 0; }
#if defined(AERIAOS)
/* An effect nobody has turned a knob of starts DRY (its MIX held at 0), and PITCH at no shift: the rig the OS starts
 * with had DIST and RING fully wet and PITCH an octave up, and the weather moving them -- every sound came out
 * mangled until each was found and turned down (sadnoise, 12 Nov 2026). Turning MIX up brings an effect in. */
static void slot_gentle(int i)
{
    int row = preview_live_fx[slot_fx(i) * 8 + slot_track(i)];
    if (row <= 0 || row > FX_COUNT) return;
    for (int k = 1; k < 6; k++) if (slots[i].user[k] >= 0 && !knob_is_mix(fx_info[row - 1].knob[k])) return;   /* a knob the musician set: theirs (PITCH's SHFT, knob 0, was held by this very rule) */
    const FxInfo *fi = &fx_info[row - 1];
    for (int k = 0; k < 6; k++) {
        const char *n = fi->knob[k];
        if (knob_is_mix(n)) slots[i].user[k] = slots[i].value[k] = 0;
        if (fi->org == 0x34000u && k == 0) slots[i].user[k] = slots[i].value[k] = 24;
    }
}
#endif
static void snap_apply(const Snap *s)
{
    for (int p = 0; p < N_PAGES; p++) for (int k = 0; k < 12; k++) {
        if (!params[p][k].name[0]) continue;
        if (s->value[p][k] < params[p][k].lo) continue;   /* saved before this knob existed: its default stays */
        params[p][k].user = s->user[p][k];
        params[p][k].value = (int16_t)clamp(s->value[p][k], params[p][k].lo, params[p][k].hi);
    }
    for (int i = 0; i < 16; i++) preview_live_fx[i] = s->live_fx[i] <= FX_COUNT ? s->live_fx[i] : 0;
    for (int t = 0; t < 8; t++) for (int k = 0; k < 12; k++) preview_part_fx[t][k] = s->part_fx[t][k] & 0x7f;
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) slots[i].user[k] = s->slot_user[i][k];
#if defined(AERIAOS)
    if (s->magic != SNAP_MAGIC) for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);   /* saved before effects started dry (or while a weather change still undid it) */
#endif
    weather = clamp(s->weather, 0, N_WEATHER - 1); auto_weather = s->auto_weather != 0; wx_on = (uint8_t)((s->xf_right & 0x100) ? (s->xf_right & 0x7e) : (weather == CLEAR ? 0 : 1u << weather));
    xw_on = (uint8_t)((s->xf_right & 0x100) ? (s->xf_right >> 9) & 7 : 0);   /* WIND, HAIL, AURORA (12 Nov 2026) */
    if (WON(SNOW) && WON(HEAT)) wx_on &= (uint8_t)~(1u << HEAT);
    night = s->night != 0; src_loop = s->src_loop != 0; src_stars = s->src_stars != 0; src_balloons = s->src_balloons != 0;
    planet_mask = s->planet_mask; slot_bypass = s->slot_bypass;
    /* the FUNC-layer knobs: a snapshot from before they were saved gets each slot's effect's own defaults */
    for (int i = 0; i < N_SLOTS; i++) {
        int row = preview_live_fx[slot_fx(i) * 8 + slot_track(i)];
        for (int k = 0; k < 6; k++) {
            int d = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def2[k] : 0, c = row > 0 && row <= FX_COUNT ? fx_info[row - 1].count2[k] : 0;
            int v = *snap_p2((Snap *)(uintptr_t)s, i, k);
            preview_slot_p2[i][k] = (uint8_t)(s->magic != SNAP_MAGIC_V3 && s->magic != SNAP_MAGIC_V2 && v < (c ? c : 1) ? v : d);
        }
    }
    if (s->magic != SNAP_MAGIC_V2) {   /* the panners, the grain cloud, MARBLES, PONG: from AES3 on */
        for (int i = 0; i < N_SLOTS; i++) { pan_set[i] = s->pan[i]; if (pan_set[i].route >= N_ROUTES) pan_set[i].route = 0; if (pan_set[i].mode >= N_PMODES) pan_set[i].mode = 0; }
        pan_chan = s->pan_chan <= 16 ? s->pan_chan : 0; midi_mod_set(s->midi_mod & 3); cloud = s->cloud; cloud.shape %= N_SHAPES;
        mb = s->marb; mb.on = 0; if (!mb.len) mb.len = 8; if (!mb.chan) mb.chan = 1; if (!mb.rnge) mb.rnge = 2;
        pg = s->pong; if (pg.nballs > PONG_BALLS) pg.nballs = 0; for (int k = 0; k < pg.nballs; k++) pong_launch(k); if (!pg.chan) pg.chan = 1; if (!pg.size) pg.size = 2;
    }
}
static volatile Snap *snap_slot(int i) { return (volatile Snap *)(SNAP_BASE + SNAP_STRIDE * (unsigned)i); }
static void snap_store(int i, const Snap *s)
{
    volatile uint8_t *d = (volatile uint8_t *)snap_slot(i);
    const uint8_t *b = (const uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) d[k] = b[k];
}
static void snap_fetch(int i, Snap *s)
{
    const volatile uint8_t *d = (const volatile uint8_t *)snap_slot(i);
    uint8_t *b = (uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) b[k] = d[k];
}

/* the old store's snapshot i, if there is a good one, in today's layout (see SNAP_STRIDE) */
static int snap_import_old(int i, Snap *s)
{
    const volatile uint8_t *o = (const volatile uint8_t *)(SNAP_OLD_BASE + SNAP_OLD_STRIDE * (unsigned)i);
    const unsigned rows_old = (N_PAGES - 1) * 12u;
    unsigned old_size = (unsigned)sizeof(Snap) - 24u;
    uint32_t magic = (uint32_t)o[0] << 24 | (uint32_t)o[1] << 16 | (uint32_t)o[2] << 8 | o[3], sum = (uint32_t)o[4] << 24 | (uint32_t)o[5] << 16 | (uint32_t)o[6] << 8 | o[7];
    if (magic == SNAP_MAGIC_V2) old_size = SNAP_V2_SIZE - 24u;                /* the first kind ended before the panners */
    else if (magic != SNAP_MAGIC_V6 && magic != SNAP_MAGIC_V5 && magic != SNAP_MAGIC_V4 && magic != SNAP_MAGIC_V3) return 0;
    uint32_t h = 0x811c9dc5u;
    for (unsigned k = 8; k < old_size; k++) h = (h ^ o[k]) * 16777619u;
    if (h != sum) return 0;
    uint8_t *n = (uint8_t *)s;
    for (unsigned k = 0; k < sizeof(Snap); k++) n[k] = 0;
    for (unsigned k = 0; k < rows_old; k++) { n[8 + k] = o[8 + k]; n[8 + rows_old + 12 + k] = o[8 + rows_old + k]; }      /* the settings, then the values */
    for (unsigned k = 8 + 2 * rows_old; k < old_size; k++) n[k + 24] = o[k];                                              /* all that follows the pages */
    for (int k = 0; k < 12; k++) { s->user[N_PAGES - 1][k] = (int8_t)params[N_PAGES - 1][k].user; s->value[N_PAGES - 1][k] = (uint8_t)params[N_PAGES - 1][k].value; }
    s->magic = magic; s->sum = snap_sum(s);
    return 1;
}
static void snap_bring_over(void)
{
    static Snap t;
    for (int i = 0; i <= N_PRESETS; i++) if (!snap_valid(snap_slot(i)) && snap_import_old(i, &t)) snap_store(i, &t);
}
/* the state: restored once at start, written when it has changed and then held still for two seconds */
static uint32_t state_sum, state_changed_at;
static Snap state_loaded;
static int state_had;
static int state_restored;
static void scenes_load(void);
void aeria_state_restore(void)
{
    snap_bring_over();
    scenes_load();
    if (snap_valid(snap_slot(0))) { snap_fetch(0, &state_loaded); snap_apply(&state_loaded); state_sum = state_loaded.sum; state_had = 1; }
#if defined(AERIAOS)
    else for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);
#endif
    state_restored = 1;
}
/* AERIA's slot set-up (tick 30) rebuilds each slot from the chain and its settings: the slots' own knob settings
 * go back on after it */
static void aeria_state_reapply_slots(void)
{
#if defined(AERIAOS)
    if (!state_had) { for (int i = 0; i < N_SLOTS; i++) slot_gentle(i); return; }      /* a first start: the rig comes up dry */
#endif
    if (!state_had) return;
    for (int i = 0; i < N_SLOTS; i++) for (int k = 0; k < 6; k++) slots[i].user[k] = state_loaded.slot_user[i][k];
#if defined(AERIAOS)
    if (state_loaded.magic != SNAP_MAGIC) for (int i = 0; i < N_SLOTS; i++) slot_gentle(i);   /* saved before effects started dry */
#endif
}
/* the scenes: all sixteen and which two the fader holds, in battery RAM beside the snapshots; written a little after
 * they change */
#define SCENE_STORE 0x100E8000u
#define SCENE_MAGIC 0x41455343u   /* 'AESC' */
static uint32_t scene_dirty_at;
static void scenes_load(void)
{
    scenes_init();
    const volatile uint8_t *b = (const volatile uint8_t *)SCENE_STORE;
    uint32_t magic = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    if (magic != SCENE_MAGIC || b[4] != N_SCENES || ((unsigned)b[5] << 8 | b[6]) != SCENE_IDS) return;
    scene_a = (uint8_t)(b[7] % N_SCENES); scene_b = (uint8_t)(b[8] % N_SCENES);
    for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) scene_val[s][i] = (int8_t)b[16 + s * SCENE_IDS + i];
}
static void scenes_save(void)
{
    volatile uint8_t *b = (volatile uint8_t *)SCENE_STORE;
    b[0] = 'A'; b[1] = 'E'; b[2] = 'S'; b[3] = 'C'; b[4] = N_SCENES; b[5] = (uint8_t)(SCENE_IDS >> 8); b[6] = (uint8_t)SCENE_IDS; b[7] = scene_a; b[8] = scene_b;
    for (int s = 0; s < N_SCENES; s++) for (int i = 0; i < SCENE_IDS; i++) b[16 + s * SCENE_IDS + i] = (uint8_t)scene_val[s][i];
}
void aeria_state_tick(void)
{
    if (scene_dirty) { scene_dirty = 0; scene_dirty_at = ticks | 1u; }
    if (scene_dirty_at && ticks - scene_dirty_at > 120u) { scene_dirty_at = 0; scenes_save(); }
    if (!state_restored || (ticks % 30u) != 0) return;
    snap_take(&snap_now);
    if (snap_now.sum == state_sum) { state_changed_at = 0; return; }
    if (!state_changed_at) { state_changed_at = ticks | 1u; return; }
    if (ticks - state_changed_at < 120u) return;
    snap_store(0, &snap_now);
    state_sum = snap_now.sum; state_changed_at = 0;
}

/* the presets page */
static int preset_mode, preset_cur = -1;
/* ---- PROJECTS (sadnoise, 12 Nov 2026): everything needed to play a piece again, under a name ---------------------
 * A project is the whole sound (every knob, the chain, the weathers, the master), the sixteen scenes and which samples
 * are loaded (the loop and the stars' folders; the sequences will join them). Sixteen of them, in battery RAM
 * (0x10040000, 32 KB each: room for the sequences). The working state is still kept by itself from moment to moment,
 * so switching the unit off and on brings back exactly what was there; a project is for coming back to a piece.
 *   PROJ          opens the page; NO or PROJ closes it
 *   a trig        picks a project (its name is shown)        YES          loads the picked one
 *   FUNC + YES    saves the sound into the picked one -- and asks for its name:
 * Typing a name is made for the sixteen trigs: 1-13 are the letters two by two (AB CD EF .. YZ) -- one press the
 * first, a second press at once the other --, 14 the digits (press again: the next), 15 space - _, 16 rubs out.
 * Any knob turns the letter under the cursor through them all; LEFT and RIGHT move the cursor; YES saves, NO gives up.
 * A new letter starts where the last one ended as soon as another trig is pressed or a moment has passed. */
#define PROJ_BASE 0x10040000u
#define PROJ_STRIDE 0x8000u
#define PROJ_MAGIC 0x4145504Au   /* 'AEPJ' */
#define N_PROJ 16
#define LOADED_AT 0x100FBA00u    /* aeria-os sampler.c: which samples are loaded */
#define LOADED_SIZE 0x300u
void aos_samples_reload(void);
static int proj_sel, proj_cur = -1, naming;
static char name_buf[13];
static int name_at, name_last = -1, name_taps;
static uint32_t name_tick;
static volatile uint8_t *proj_at(int i) { return (volatile uint8_t *)(PROJ_BASE + PROJ_STRIDE * (unsigned)i); }
static int proj_saved(int i) { const volatile uint8_t *b = proj_at(i); return b[0] == 'A' && b[1] == 'E' && b[2] == 'P' && b[3] == 'J'; }
static void proj_name(int i, char *out) { const volatile uint8_t *b = proj_at(i); int k = 0; if (proj_saved(i)) for (; k < 12 && b[8 + k]; k++) out[k] = (char)b[8 + k]; out[k] = 0; }
static void proj_save(int i, const char *name)
{
    volatile uint8_t *b = proj_at(i);
    static Snap t; snap_take(&t);
    b[0] = 0;                                                        /* not a project while it is being written */
    for (int k = 0; k < 13; k++) b[8 + k] = (uint8_t)(k < 12 ? name[k] : 0);
    const uint8_t *sp = (const uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) b[0x40 + k] = sp[k];
    const volatile uint8_t *ld = (const volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) b[0x800 + k] = ld[k];
    b[0x1000] = scene_a; b[0x1001] = scene_b;
    for (int sc = 0; sc < N_SCENES; sc++) for (int q = 0; q < SCENE_IDS; q++) b[0x1010 + sc * SCENE_IDS + q] = (uint8_t)scene_val[sc][q];
#ifdef SEQ_UI
    { const volatile uint8_t *q = (const volatile uint8_t *)SEQ_BASE;       /* the sequences: the head, then every step */
      for (unsigned k = 0; k < SEQ_HEAD_SIZE; k++) b[0x1C00 + k] = q[k];
      for (unsigned k = 0; k < SEQ_STEPS_SIZE; k++) { b[0x2000 + k] = q[SEQ_HEAD_SIZE + k]; if (!(k & 255)) aos_breathe(); } }
    b[7] = 1;
#endif
    b[4] = 1; b[5] = (uint8_t)(SCENE_IDS >> 8); b[6] = (uint8_t)SCENE_IDS; b[1] = 'E'; b[2] = 'P'; b[3] = 'J'; b[0] = 'A';
    proj_cur = i;
}
static void proj_load(int i)
{
    if (!proj_saved(i)) return;
    const volatile uint8_t *b = proj_at(i);
    static Snap t; uint8_t *tp = (uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) tp[k] = b[0x40 + k];
    if (!snap_version(&t)) { say("PROJECT", "DAMAGED"); return; }
    snap_apply(&t);
    state_loaded = t; state_had = 1; aeria_state_reapply_slots();
    volatile uint8_t *ld = (volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) ld[k] = b[0x800 + k];
    aos_samples_reload();
    if (((unsigned)b[5] << 8 | b[6]) == SCENE_IDS) {
        scene_a = (uint8_t)(b[0x1000] % N_SCENES); scene_b = (uint8_t)(b[0x1001] % N_SCENES);
        for (int sc = 0; sc < N_SCENES; sc++) for (int q = 0; q < SCENE_IDS; q++) scene_val[sc][q] = (int8_t)b[0x1010 + sc * SCENE_IDS + q];
        scene_dirty = 1;
    }
#ifdef SEQ_UI
    if (b[7] == 1 && b[0x1C00] == 'A' && b[0x1C01] == 'E' && b[0x1C02] == 'S' && b[0x1C03] == 'Q') {
        volatile uint8_t *q = (volatile uint8_t *)SEQ_BASE;
        aos_seq_play(0);
        for (unsigned k = 0; k < SEQ_HEAD_SIZE; k++) q[k] = b[0x1C00 + k];
        for (unsigned k = 0; k < SEQ_STEPS_SIZE; k++) { q[SEQ_HEAD_SIZE + k] = b[0x2000 + k]; if (!(k & 255)) aos_breathe(); }
        seq_on = SQH->mode == 1; aos_seq_mode(0); if (seq_on) aos_seq_mode(1);
    }
#endif
    proj_cur = i;
}
/* the presets of before (snapshots 1..16) become projects once, named PRESET n */
static void proj_from_presets(void)
{
    for (int i = 0; i < N_PROJ; i++) if (!proj_saved(i) && snap_valid(snap_slot(1 + i))) {
        volatile uint8_t *b = proj_at(i); static Snap t; snap_fetch(1 + i, &t);
        char nm[13] = "PRESET "; num(nm + 7, i + 1);
        for (int k = 0; k < 13; k++) b[8 + k] = (uint8_t)nm[k];
        const uint8_t *sp = (const uint8_t *)&t; for (unsigned k = 0; k < sizeof(Snap); k++) b[0x40 + k] = sp[k];
        const volatile uint8_t *ld = (const volatile uint8_t *)LOADED_AT; for (unsigned k = 0; k < LOADED_SIZE; k++) b[0x800 + k] = ld[k];
        b[5] = 0; b[6] = 0; b[4] = 1; b[1] = 'E'; b[2] = 'P'; b[3] = 'J'; b[0] = 'A';
    }
}
/* ---- typing a name ---- */
static const char name_extra[] = " -_";
static void name_put(char c) { if (name_at > 11) name_at = 11; name_buf[name_at] = c; int n = 0; while (n < 12 && name_buf[n]) n++; if (name_at >= n) name_buf[name_at + 1] = 0; }
static void name_step(void) { if (name_last >= 0) { if (name_at < 11) name_at++; name_last = -1; name_taps = 0; } }
static void name_trig(int k)
{
    if (k == 15) {                                                   /* rub out */
        name_last = -1; name_taps = 0;
        if (name_at > 0 && !name_buf[name_at]) name_at--;
        for (int q = name_at; q < 12; q++) name_buf[q] = name_buf[q + 1];
        name_buf[12] = 0; return;
    }
    if (k != name_last) name_step(); 
    else if (ticks - name_tick > 50u) name_step();
    name_taps = (k == name_last) ? name_taps + 1 : 0;
    name_last = k; name_tick = ticks;
    char c = k < 13 ? (char)('A' + 2 * k + (name_taps & 1)) : k == 13 ? (char)('0' + name_taps % 10) : name_extra[name_taps % 3];
    name_put(c);
}
static void name_turn(int d)
{
    static const char all[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";
    name_last = -1; name_taps = 0;
    char c = name_buf[name_at] ? name_buf[name_at] : 'A'; int q = 0;
    for (int k = 0; all[k]; k++) if (all[k] == c) q = k;
    q = (q + (d > 0 ? 1 : -1) + 39) % 39;
    name_put(all[q]);
}
static int preset_key(int code, int pressed)
{
    if (!pressed) return preset_mode;
    if (naming) {
        if (code >= KEY_TRIG0 && code < KEY_TRIG0 + 16) { name_trig(code - KEY_TRIG0); return 1; }
        if (code == KEY_LEFT) { name_last = -1; if (name_at > 0) name_at--; return 1; }
        if (code == KEY_RIGHT) { name_last = -1; if (name_at < 11 && name_buf[name_at]) name_at++; return 1; }
        if (code == KEY_NO) { naming = 0; say("NOT SAVED", 0); return 1; }
        if (code == KEY_YES) {
            int n = 0; while (n < 12 && name_buf[n]) n++;
            while (n > 0 && name_buf[n - 1] == ' ') name_buf[--n] = 0;
            if (!n) { char d[13] = "PROJECT "; num(d + 8, proj_sel + 1); for (int k = 0; k < 13; k++) name_buf[k] = d[k]; }
            proj_save(proj_sel, name_buf); naming = 0; say("SAVED", name_buf); return 1;
        }
        return code != KEY_FUNC;
    }
    if (code == KEY_PROJ) { preset_mode = !preset_mode; if (preset_mode) { proj_from_presets(); if (proj_cur >= 0) proj_sel = proj_cur; } say(preset_mode ? "PROJECTS" : page_name[page], 0); return 1; }
    if (!preset_mode) return 0;
    if (code == KEY_NO) { preset_mode = 0; say(page_name[page], 0); return 1; }
    if (code >= KEY_TRIG0 && code < KEY_TRIG0 + N_PROJ) { proj_sel = code - KEY_TRIG0; return 1; }
    if (code == KEY_YES && func_held) {                              /* save here: its name first */
        for (int k = 0; k < 13; k++) name_buf[k] = 0;
        proj_name(proj_sel, name_buf);
        name_at = 0; while (name_at < 11 && name_buf[name_at]) name_at++;
        name_last = -1; name_taps = 0; naming = 1; return 1;
    }
    if (code == KEY_YES) { if (proj_saved(proj_sel)) { char n[13]; proj_name(proj_sel, n); proj_load(proj_sel); say("LOADED", n); preset_mode = 0; } else say("EMPTY", "FUNC+YES SAVES"); return 1; }
    return code != KEY_FUNC;   /* the page holds every other key while it is open */
}
static int preset_enc(int d) { if (naming) { name_turn(d); return 1; } return preset_mode; }
static void preset_draw(void)
{
    for (int x = 4; x < W - 4; x++) span(x, 6, 58, 0);
    for (int x = 4; x < W - 4; x++) { px(x, 6); px(x, 58); }
    if (naming) {
        text(8, 9, "NAME", 1); char n[4]; num(n, proj_sel + 1); text(30, 9, n, 1);
        text(W - 8 - text_w("YES SAVES"), 9, "YES SAVES", 1);
        for (int k = 0; k < 12; k++) {                                /* the name, twice the size, a line under the cursor */
            int x = 10 + k * 9; char c[2] = {name_buf[k] ? name_buf[k] : ' ', 0};
            if (c[0] != ' ') { int g = glyph_of(c[0]); if (g >= 0) for (int r = 0; r < 5; r++) for (int q = 0; q < 3; q++) if (glyphs[g] & (1u << (14 - (r * 3 + q)))) { px(x + 2 * q, 18 + 2 * r); px(x + 2 * q + 1, 18 + 2 * r); px(x + 2 * q, 19 + 2 * r); px(x + 2 * q + 1, 19 + 2 * r); } }
            if (k == name_at && ((ticks >> 4) & 1)) for (int q = 0; q < 7; q++) px(x + q, 30);
            else if (!name_buf[k] || k > name_at) px(x + 3, 30);
        }
        text(8, 36, "AB CD EF GH IJ KL MN OP", 1);
        text(8, 44, "QR ST UV WX YZ 09 _  DEL", 1);
        text(8, 52, "KNOB: ANY LETTER", 1);
        return;
    }
    text(8, 9, "PROJECTS", 1);
    for (int i = 0; i < N_PROJ; i++) {
        int bx = 10 + (i % 8) * 14, by = 17 + (i / 8) * 12;
        for (int x = bx; x < bx + 12; x++) { px(x, by); px(x, by + 9); }
        for (int y = by; y <= by + 9; y++) { px(bx, y); px(bx + 11, y); }
        if (proj_saved(i)) for (int x = bx + 2; x < bx + 10; x++) for (int y = by + 2; y < by + 8; y++) if (i == proj_cur || ((x + y) & 1)) px(x, y);
        if (i == proj_sel) { for (int x = bx - 1; x < bx + 13; x++) { px(x, by - 1); px(x, by + 10); } for (int y = by - 1; y <= by + 10; y++) { px(bx - 1, y); px(bx + 12, y); } }
    }
    char n[13], num2[4]; num(num2, proj_sel + 1); text(8, 43, num2, 1);
    if (proj_saved(proj_sel)) { proj_name(proj_sel, n); text(22, 43, n, 1); text(8, 51, "YES LOADS  FUNC+YES SAVES", 1); }
    else { text(22, 43, "EMPTY", 1); text(8, 51, "FUNC+YES SAVES HERE", 1); }
}
static void preset_leds(void)
{
    if (naming) { for (int k = 0; k < 16; k++) led_want[k] = k == name_last ? 2 : 1; return; }
    for (int k = 0; k < 16; k++) led_want[k] = k == proj_sel ? 2 : proj_saved(k) ? 1 : 0;
}
#endif
