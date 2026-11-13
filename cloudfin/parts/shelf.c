/* ---- the shelf: which effect sits in each chain slot -----------------------------------------
 * The chain: T3 THRU (inputs; FX1 = HEAD, the mixer and main feedback), T4-T8 NEIGHBOR.
 * Eleven slots the musician fills from the shelf: T3 FX2, then FX1 and FX2 of T4..T8.
 * The effect lists, names and knob names are read from the firmware's own choosers
 * (the base repoints the setters' lists: research-fx.md section 2), so every installed
 * effect appears by its own name with its own knobs. */
#define N_SLOTS 7
static int slot_track(int s) { return s == 0 ? HEAD_T : HEAD_T + 1 + (s - 1) / 2; }
static int slot_fx(int s) { return s == 0 ? 1 : (s - 1) & 1; } /* 0 FX1, 1 FX2 */

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: the effects are AERIA OS's own (aeria-os/src/fx_table.inc, from the aeria-fx manifests); row 0 is the
 * empty slot. main.c watches preview_live_fx and loads each slot's effect into the DSP engine. */
#include "fx_table.inc"
uint8_t preview_live_fx[16];
uint8_t preview_part_fx[8][12];
uint8_t preview_slot_p2[7][6];   /* each chain slot's FUNC-layer knobs (the effect's page 2): AERIA OS sends them to the DSP */
static int list_count(int fx2) { (void)fx2; return FX_COUNT + 1; }
static int list_id(int fx2, int row) { (void)fx2; return row; }
static void list_name(int fx2, int row, char *out) { (void)fx2; const char *n = row > 0 && row <= FX_COUNT ? fx_info[row - 1].name : "NONE"; int i = 0; while (n[i] && i < 12) { out[i] = n[i]; i++; } out[i] = 0; }
static void list_knob(int fx2, int row, int k, char *out) { (void)fx2; const char *n = row > 0 && row <= FX_COUNT ? fx_info[row - 1].knob[k] : ""; int i = 0; while (n[i] && i < 4) { out[i] = n[i]; i++; } out[i] = 0; }
static int list_count_of(int fx2, int row, int k) { (void)fx2; int c = row > 0 && row <= FX_COUNT ? fx_info[row - 1].count[k] : 128; return c >= 2 && c <= 128 ? c : 128; }
static int live_fx(int t, int fx2) { return preview_live_fx[fx2 * 8 + t]; }
static int part_knob(int t, int fx2, int k) { return preview_part_fx[t][fx2 * 6 + k]; }
/* ---- which DSP core runs a slot's effect (12 Nov 2026) ----------------------------------------------------------
 * A core has about 1,760 instructions for each sample (measured: the old rig's five effects, 1,127 of them, took
 * 64 % of core 0). Core 0 also runs the engine and takes the samples' stream (about 610); core 1 only the effects.
 * An effect bound to a core's own memory (GRAINS, RESO: core 0; VERB, ECHO: core 1) goes there; the others go
 * where there is more room. When it fits nowhere the pick is refused: NO ROOM, DSP FULL. */
#ifndef CORE_ROOM
#define CORE_ROOM 1700
#endif
#define CORE0_BASE 710     /* the engine's own share of core 0 -- with the time it spends sorting the host's words counted (the meter
                            * left that out until 12 Nov 2026: 1,081 of effects then read 93 % in the worst frame) */
#define CORE1_BASE 250     /* core 1 is the weaker in practice: VERB + ECHO (1,404) run clean, VERB + ECHO + SMEAR (1,673) drop
                            * blocks (measured on the unit with USB recording on, 12 Nov 2026) -- so its room ends near 1,450 */
static int8_t slot_core[7] = {-1, -1, -1, -1, -1, -1, -1};
static uint16_t slot_cost[7];
static uint8_t slot_any[7];   /* the slot's effect could run on either core */
static int place_slot_of(int t, int fx2) { for (int s = 0; s < 7; s++) if ((s == 0 ? 4 : 5 + (s - 1) / 2) == t && (s == 0 ? 1 : (s - 1) & 1) == fx2) return s; return -1; }
/* the core for `row` in slot s: 0 or 1, or -1 when there is no room. An effect that may run on either core goes to
 * the emptier one; one bound to a core that is full has the movable effects there sent across to make room (main.c
 * sees their core change and moves them). commit: remember the outcome; otherwise only say whether it would fit. */
static int place_effect(int s, int row, int commit)
{
    if (s < 0 || s >= 7) return 0;
    if (row <= 0 || row > FX_COUNT) { if (commit) { slot_core[s] = -1; slot_cost[s] = 0; slot_any[s] = 0; } return 0; }
    int8_t core[7]; int load[2] = {CORE0_BASE, CORE1_BASE}, cost = fx_cost[row - 1], want = fx_core[row - 1];
    for (int i = 0; i < 7; i++) { core[i] = i == s ? -1 : slot_core[i]; if (core[i] >= 0) load[core[i]] += slot_cost[i]; }
    /* one big effect alone on core 0 may take a little more than the shared room (the FFT SMEAR, 1,137: so it can
     * stand beside VERB on core 1); nothing else joins it there */
#define ROOM_OF(k) ((k) == 0 && load[0] == CORE0_BASE ? CORE0_BASE + 1150 : CORE_ROOM)
    int c = want < 2 ? want : (load[1] <= load[0] ? 1 : 0);
    if (load[c] + cost > ROOM_OF(c) && want == 2 && load[c ^ 1] + cost <= ROOM_OF(c ^ 1)) c ^= 1;
    for (int i = 0; i < 7 && load[c] + cost > ROOM_OF(c); i++)              /* make room: movable effects go across */
        if (core[i] == c && slot_any[i] && load[c ^ 1] + slot_cost[i] <= ROOM_OF(c ^ 1)) { core[i] = (int8_t)(c ^ 1); load[c] -= slot_cost[i]; load[c ^ 1] += slot_cost[i]; }
    int fits = load[c] + cost <= ROOM_OF(c);
    if (commit) {
        if (fits) for (int i = 0; i < 7; i++) if (i != s) slot_core[i] = core[i];
        slot_core[s] = (int8_t)c; slot_cost[s] = (uint16_t)cost; slot_any[s] = (uint8_t)(want == 2);
    }
    return fits ? c : -1;
}
int aos_place(int s, int row) { place_effect(s, row, 1); return s >= 0 && s < 7 ? slot_core[s] : 0; }   /* main.c, when a slot's effect changes */
int aos_slot_core(int s) { return s >= 0 && s < 7 ? slot_core[s] : -1; }                                 /* ... and each pass: has it been moved? */
int aos_core_load(int core) { int l = core ? CORE1_BASE : CORE0_BASE; for (int i = 0; i < 7; i++) if (slot_core[i] == core) l += slot_cost[i]; return l * 100 / 1760; }
static int fx_is(int row, const char *n) { const char *p = fx_info[row - 1].name + 6; while (*n && *p == *n) { p++; n++; } return !*n && !*p; }
/* two effects that keep their sound in the same memory (12 Nov 2026: Chloaelis' delays) -- the pair either way round */
static int fx_pair(int a, int b, const char *x, const char *y) { return (fx_is(a, x) && fx_is(b, y)) || (fx_is(a, y) && fx_is(b, x)); }
static int fx_clash(int a, int b)
{
    return fx_pair(a, b, "WIND", "FOG") || fx_pair(a, b, "HAIL", "AURORA")
        || fx_pair(a, b, "TAPE", "ECHO")                                     /* core 1's Y:$8000-$bfff and X:$4000-$7fff */
        || fx_pair(a, b, "BBD", "HAIL") || fx_pair(a, b, "BBD", "AURORA")    /* core 1's X:$2000-$3fff */
        || fx_pair(a, b, "PT2399", "TWISTER");                               /* core 1's X:$8000-$8fff */
    /* BBD with PT2399 (780 + 970 on core 1) is kept apart by the room alone: CORE1_BASE + both > CORE_ROOM, SQ_ROOM1 */
}
static int pick_effect(int t, int fx2, int row)
{
    if (place_effect(place_slot_of(t, fx2), row, 0) < 0) return 0;   /* no core has the room */
    /* an effect with a delay buffer has one buffer: a second copy would share it, so it is refused */
    if (row > 0 && row <= FX_COUNT && fx_info[row - 1].has_y)
        for (int i = 0; i < 16; i++) if (i != fx2 * 8 + t && preview_live_fx[i] == row) return 0;
    /* ... and two effects that keep their sound in the same memory are never in the chain together
     * (WIND and FOG; HAIL and AURORA; BBD and HAIL or AURORA; PT2399 and TWISTER; TAPE and ECHO: fx_clash) */
    if (row > 0 && row <= FX_COUNT)
        for (int i = 0; i < 16; i++) { int o = preview_live_fx[i]; if (i != fx2 * 8 + t && o > 0 && o <= FX_COUNT && fx_clash(row, o)) return 0; }
    preview_live_fx[fx2 * 8 + t] = (uint8_t)row;
    for (int k = 0; k < 6; k++) preview_part_fx[t][fx2 * 6 + k] = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def[k] : 64;
    return 1;
}
#elif defined(PREVIEW)
static uint8_t preview_live_fx[16];
static const char *const preview_names[] = {"NONE", "FILTER", "CHORUS", "COMB FILTER", "PHASER", "LO-FI", "DELAY", "DARK REV", "PLATE REV", "AERIA PITCH", "AERIA SMEAR"};
static const char *const preview_knobs[11][6] = {{"", "", "", "", "", ""}, {"BASE", "WDTH", "Q", "DPTH", "ATK", "DEC"}, {"DEL", "DEP", "SPD", "FB", "WID", "MIX"},
    {"PTCH", "TUNE", "LP", "FB", "", "MIX"}, {"CNTR", "DEP", "SPD", "FB", "WID", "MIX"}, {"DIST", "NOIS", "AMF", "SRR", "BRR", "AMD"},
    {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {"TIME", "SHVG", "SHVF", "HP", "LP", "MIX"}, {"TIME", "DAMP", "GATE", "HP", "LP", "MIX"},
    {"SHFT", "FINE", "GRN", "FDBK", "TONE", "MIX"}, {"SIZE", "HOLD", "DIFF", "TONE", "MOD", "MIX"}};
static uint8_t preview_part_fx[8][12];
static int list_count(int fx2) { return fx2 ? 11 : 6; }
static int list_id(int fx2, int row) { return fx2 ? row : row; }
static void list_name(int fx2, int row, char *out) { (void)fx2; const char *n = preview_names[row]; int i = 0; while (n[i] && i < 12) { out[i] = n[i]; i++; } out[i] = 0; }
static void list_knob(int fx2, int row, int k, char *out) { (void)fx2; const char *n = preview_knobs[row][k]; int i = 0; while (n[i] && i < 4) { out[i] = n[i]; i++; } out[i] = 0; }
static int list_count_of(int fx2, int row, int k) { (void)fx2; (void)row; (void)k; return 128; }
static int live_fx(int t, int fx2) { return preview_live_fx[fx2 * 8 + t]; }
static int part_knob(int t, int fx2, int k) { return preview_part_fx[t][fx2 * 6 + k]; }
static int pick_effect(int t, int fx2, int row) { preview_live_fx[fx2 * 8 + t] = (uint8_t)list_id(fx2, row); for (int k = 0; k < 6; k++) preview_part_fx[t][fx2 * 6 + k] = 64; return 1; }
#else
#define LIST(s) ((const volatile uint32_t *)*(volatile uint32_t *)((s) ? 0x40052496u : 0x40052706u))
static uint32_t list_desc(int fx2, int row) { return LIST(fx2)[row]; }
static int list_count(int fx2)
{
    const volatile uint32_t *l = LIST(fx2);
    int n = 0;
    while (n < 128 && l[n] >= 0x40000000u && l[n] < 0x48000000u) n++;
    return n;
}
static int list_id(int fx2, int row) { return (int)(*(const volatile uint32_t *)list_desc(fx2, row) & 0xffu); }
static void list_name(int fx2, int row, char *out)
{
    const volatile char *n = (const volatile char *)(list_desc(fx2, row) + 9);
    int i = 0;
    while (i < 12 && n[i] >= ' ' && n[i] < 127) { out[i] = n[i] >= 'a' && n[i] <= 'z' ? (char)(n[i] - 32) : n[i]; i++; }
    out[i] = 0;
}
/* a chooser row already points at the page descriptor P (stock's descriptor + 0x38, read on the unit):
 * id +0, names +0x16 + 6k, counts +0x9a + 4k, enable nibbles +0x18e */
static int list_knob_enabled(int fx2, int row, int k)
{
    uint32_t p = list_desc(fx2, row);
    uint32_t en = *(const volatile uint32_t *)(p + 0x18eu);
    return (int)((en >> (4 * k)) & 1u);
}
static void list_knob(int fx2, int row, int k, char *out)
{
    out[0] = 0;
    if (!list_knob_enabled(fx2, row, k)) return;
    const volatile char *n = (const volatile char *)(list_desc(fx2, row) + 0x16u + 6u * (unsigned)k);
    int i = 0;
    while (i < 4 && n[i] > ' ' && n[i] < 127) { out[i] = n[i] >= 'a' && n[i] <= 'z' ? (char)(n[i] - 32) : n[i]; i++; }
    out[i] = 0;
}
static int list_count_of(int fx2, int row, int k)
{
    uint32_t c = *(const volatile uint32_t *)(list_desc(fx2, row) + 0x9au + 4u * (unsigned)k);
    return c >= 2 && c <= 128 ? (int)c : 128;
}
static int live_fx(int t, int fx2) { return *(volatile uint8_t *)(0x80000ec4u + 8u * (unsigned)fx2 + (unsigned)t); }
/* the working Part's page-1 knob bytes: +0x11a + 24t, FX1 at +12, FX2 at +18 (PARAM_PAGES.md 5g) */
static int part_knob(int t, int fx2, int k)
{
    uint32_t base = bank_base();
    if (!base) return 64;
    volatile uint8_t *part = (volatile uint8_t *)(base + 0x8ed80u + (unsigned)(*(volatile uint8_t *)0x100b14cfu & 3u) * 6322u);
    return part[0x11a + 24 * t + (fx2 ? 18 : 12) + k] & 0x7f;
}
/* The base-guarded stock setter (research-fx.md Recipe A): the DSP loader admits,
 * uploads and charges the effect, or refuses it (DSP FULL) and nothing changes. */
static int pick_effect(int t, int fx2, int row)
{
    volatile uint8_t *cur = (volatile uint8_t *)0x80000000u;
    volatile uint32_t *cursor = (volatile uint32_t *)(fx2 ? 0x460d5ca8u : 0x460d5c94u);
    uint8_t was_t = *cur;
    uint32_t was_c = *cursor;
    *cur = (uint8_t)t;
    *cursor = (uint32_t)row;
    ((void (*)(void))(fx2 ? 0x40052474u : 0x400526e4u))();
    *cur = was_t;
    *cursor = was_c;
    return 1;
}
#endif

static int row_of_id(int fx2, int id)
{
    int n = list_count(fx2);
    for (int r = 0; r < n; r++) if (list_id(fx2, r) == id) return r;
    return -1;
}
__attribute__((unused)) static int row_of_name(int fx2, const char *name)
{
    int n = list_count(fx2);
    char buf[14];
    for (int r = 0; r < n; r++) {
        list_name(fx2, r, buf);
        int i = 0;
        while (name[i] && buf[i] == name[i]) i++;
        if (!name[i] && !buf[i]) return r;
    }
    return -1;
}

/* what AERIA knows about each slot */
typedef struct {
    int16_t id, row;          /* the effect now in it */
    char name[14];
    char knob[6][5];
    uint8_t count[6];
    int16_t value[6], user[6]; /* user -1 follows the weather */
    uint8_t role[6];
    int16_t pending;          /* row asked for, -1 none */
    uint16_t wait;
} Slot;
static Slot slots[N_SLOTS];
static uint16_t slot_bypass;
static int sel_slot = 1, shelf_open, shelf_row;
/* the shelf offers only AERIA's own effects (and "-", empty): never a stock one. TAP and HEAD are AERIA's
 * plumbing with fixed places, so they are not on it either. shelf_row indexes this filtered list. */
static int shelf_rows[48], shelf_n;
static int fx_banned(const char *name);
static void shelf_build(int fx2)
{
    int n = list_count(fx2);
    char nm[14];
    shelf_n = 0;
    shelf_rows[shelf_n++] = 0;
    for (int r = 1; r < n && shelf_n < 48; r++) {
        list_name(fx2, r, nm);
        if (!(nm[0] == 'A' && nm[1] == 'E' && nm[2] == 'R' && nm[3] == 'I' && nm[4] == 'A' && nm[5] == ' ')) continue;
        if ((nm[6] == 'T' && nm[7] == 'A' && nm[8] == 'P') || (nm[6] == 'H' && nm[7] == 'E' && nm[8] == 'A' && nm[9] == 'D')) continue;
        if (fx_banned(nm)) continue;
        shelf_rows[shelf_n++] = r;
    }
}
static int shelf_index_of(int row) { for (int i = 0; i < shelf_n; i++) if (shelf_rows[i] == row) return i; return 0; }

/* ---- the weather speaks to knobs by what they are ------------------------------------------
 * role: what a knob does; each weather sets how much of each role it wants, and a knob's
 * target is lo + (hi - lo) * amount / 127. Unknown knobs stay where the musician left them. */
enum { R_NONE, R_SPACE, R_TIME, R_ECHO, R_SEND, R_OPEN, R_DARK, R_GRIT, R_SHIM, R_RESO, R_RATE, R_PITCH, R_SMEAR, R_FREEZE, N_ROLES };
typedef struct { char knob[5]; uint8_t role, lo, hi; } Rule;
static const Rule rules[] = {
    {"MIX", R_SPACE, 0, 120}, {"WET", R_SPACE, 0, 120}, {"TIME", R_TIME, 30, 127}, {"DECY", R_TIME, 30, 127}, {"DCAY", R_TIME, 30, 127},
    {"SIZE", R_TIME, 30, 127}, {"FB", R_ECHO, 0, 115}, {"FDBK", R_ECHO, 0, 115}, {"SEND", R_SEND, 0, 110}, {"VOL", R_SEND, 20, 110},
    {"WDTH", R_OPEN, 40, 127}, {"LP", R_OPEN, 40, 127}, {"TONE", R_OPEN, 30, 110}, {"DAMP", R_DARK, 0, 110}, {"HP", R_DARK, 0, 50},
    {"DIST", R_GRIT, 0, 90}, {"DRV", R_GRIT, 0, 100}, {"DRIV", R_GRIT, 0, 100}, {"NOIS", R_GRIT, 0, 50}, {"BITS", R_GRIT, 0, 80},
    {"DEP", R_SHIM, 20, 120}, {"DPTH", R_SHIM, 20, 120}, {"SPD", R_RATE, 4, 90}, {"RATE", R_RATE, 4, 90},
    {"RES", R_RESO, 0, 35}, {"Q", R_RESO, 0, 25},   /* low resonance (sadnoise, 10 Nov 2026) */ {"HOLD", R_SMEAR, 0, 127}, {"SMR", R_SMEAR, 0, 127}, {"DIFF", R_SMEAR, 20, 127},
    {"FRZ", R_FREEZE, 0, 127},   /* the FFT SMEAR's freeze: the snow's (12 Nov 2026) */
    /* more knobs that follow the weather (12 Nov 2026, late): PITCH's grain, GRAINS' density, the widths, SMEAR's tilt
     * and gate, ROSE's brightness, RESO's detune, VERB's movement, RING's follower */
    {"GRN", R_TIME, 40, 127}, {"DENS", R_SHIM, 30, 120}, {"WIDE", R_SHIM, 50, 127}, {"TILT", R_OPEN, 30, 100}, {"BRIT", R_OPEN, 30, 110},
    {"GATE", R_DARK, 0, 60}, {"DTUN", R_SHIM, 0, 60}, {"MOD", R_SHIM, 0, 100}, {"FOLW", R_RATE, 0, 100},
    {"GRIT", R_GRIT, 30, 100},   /* PT2399's CVSD step (64 = the plugin's): storms roughen it (12 Nov 2026) */
};
#define N_RULES ((int)(sizeof rules / sizeof rules[0]))
/* per weather, how much of each role (0..127): SPACE TIME ECHO SEND OPEN DARK GRIT SHIM RESO RATE PITCH SMEAR FREEZE */
static const uint8_t weather_roles[N_WEATHER][N_ROLES] = {
    /* CLEAR  */ {0, 25, 40, 25, 30, 120, 10, 0, 25, 20, 30, 0, 0, 0},
    /* CLOUDY */ {0, 45, 60, 45, 45, 95, 35, 10, 45, 30, 25, 0, 20, 0},
    /* RAIN   */ {0, 60, 75, 60, 60, 70, 60, 25, 40, 50, 40, 0, 35, 0},
    /* STORM  */ {0, 75, 90, 95, 85, 75, 50, 85, 30, 80, 90, 0, 40, 0},
    /* SNOW   */ {0, 100, 127, 60, 50, 50, 85, 15, 40, 20, 5, 0, 110, 118},
    /* HEAT   */ {0, 30, 45, 35, 30, 95, 20, 50, 120, 30, 80, 0, 105, 0},
    /* FOG    */ {0, 90, 100, 60, 50, 40, 90, 10, 60, 30, 10, 0, 90, 0}};
static int name_has(const char *name, const char *part)
{
    for (int i = 0; name[i]; i++) { int k = 0; while (part[k] && name[i + k] == part[k]) k++; if (!part[k]) return 1; }
    return 0;
}
/* effects the owner does not want offered (12 Nov 2026: no clipping or distortion): kept in the firmware so saved
 * rows still mean the same, but never on a list, never lent, never set up */
static int fx_banned(const char *name) { return name_has(name, "DIST") || name_has(name, "BBD") || name_has(name, "PT2399") || name_has(name, "TAPE"); }   /* BBD PT2399 TAPE: held back until a DSP crash seen with them on 13 Nov 2026 is found */
static int rule_of(const char *knob)
{
    for (int r = 0; r < N_RULES; r++) {
        int i = 0;
        while (knob[i] && rules[r].knob[i] == knob[i]) i++;
        if (!knob[i] && !rules[r].knob[i]) return r;
    }
    return -1;
}
static int slot_target(int s, int k, int w)
{
    Slot *sl = &slots[s];
    int r = rule_of(sl->knob[k]);
    if (r < 0) return -1;
    int amt = weather_roles[w][rules[r].role];
    int v = rules[r].lo + (rules[r].hi - rules[r].lo) * amt / 127;
    /* DIST's RATE is its sample rate (127 = untouched), not a modulation speed: it follows the weather's grit,
     * inverted, and stays near clean -- as a speed it decimated the input in every weather (10 Nov 2026) */
    if (rules[r].role == R_RATE && name_has(sl->name, "DIST")) v = 127 - weather_roles[w][R_GRIT] * 30 / 127;
    return clamp(v, 0, sl->count[k] - 1);
}

/* read a slot's effect from the firmware: its name, knobs, and the Part's values */
static void slot_refresh(int s)
{
    Slot *sl = &slots[s];
    int t = slot_track(s), fx2 = slot_fx(s);
    sl->id = (int16_t)live_fx(t, fx2);
    sl->row = (int16_t)row_of_id(fx2, sl->id);
    if (sl->row < 0) { sl->name[0] = 0; for (int k = 0; k < 6; k++) sl->knob[k][0] = 0; return; }
    list_name(fx2, sl->row, sl->name);
    for (int k = 0; k < 6; k++) {
        list_knob(fx2, sl->row, k, sl->knob[k]);
        sl->count[k] = (uint8_t)(list_count_of(fx2, sl->row, k) > 127 ? 128 : list_count_of(fx2, sl->row, k));
        sl->value[k] = (int16_t)part_knob(t, fx2, k);
        sl->user[k] = -1;
    }
    for (int k = 0; k < 6; k++) sent[t][(fx2 ? 24 : 18) + k] = 0xff;
}
static void slots_init(void) { for (int s = 0; s < N_SLOTS; s++) { slots[s].pending = -1; slot_refresh(s); } }
static int knob_is_mix(const char *n) { return n[0] == 'M' && n[1] == 'I' && n[2] == 'X' && !n[3]; }
static int slot_base(int s, int k) { return slots[s].user[k] >= 0 ? slots[s].user[k] : slots[s].value[k]; }
static int slot_value(int s, int k) { return scene_mix(N_PAGES * 12 + s * 6 + k, slot_base(s, k)); }   /* with the scenes */
/* a knob's value as the musician reads it: pitch in semitones either side of 0, fine tune in cents, a root as its note */
static void signed_text(char *n, int v) { n[0] = v > 0 ? '+' : v < 0 ? '-' : ' '; num(n + 1, v < 0 ? -v : v); }
static void slot_text(int s, int k, int v, char *n)   /* k 0..5 the knobs, 6..11 the FUNC layer's */
{
    const char *t = 0;
#if defined(AERIAOS)
    int row = slots[s].row;
    if (row > 0 && row <= FX_COUNT) {
        const FxInfo *fi = &fx_info[row - 1];
        if (k < 6) { if (fi->text[k] && v >= 0 && v < fi->count[k]) t = fi->text[k][v]; }
        else if (fi->text2[k - 6] && v >= 0 && v < fi->count2[k - 6]) t = fi->text2[k - 6][v];
    }
#endif
    (void)s; (void)k;
    if (!t) { num(n, v); return; }
    int i = 0; while (t[i] && i < 10) { n[i] = t[i]; i++; } n[i] = 0;
}

/* one pick at a time; the loader may take a few ticks to upload, or refuse (DSP FULL) */
static int picking = -1;
static void slot_pick(int s, int row)
{
    if (picking >= 0) return;
    Slot *sl = &slots[s];
    sl->pending = (int16_t)row;
    sl->wait = 0;
    picking = s;
    int ok = pick_effect(slot_track(s), slot_fx(s), row);
#if defined(AERIAOS)
    if (ok) for (int k = 0; k < 6; k++) preview_slot_p2[s][k] = row > 0 && row <= FX_COUNT ? fx_info[row - 1].def2[k] : 0;
#endif
    (void)ok;
}
static void picks_tick(void)
{
    if (picking < 0) return;
    Slot *sl = &slots[picking];
    int want = list_id(slot_fx(picking), sl->pending);
    if (live_fx(slot_track(picking), slot_fx(picking)) == want) {
        slot_refresh(picking);
        say(sl->name, "IN");
        sl->pending = -1; picking = -1;
    } else if (++sl->wait > 300) { /* five seconds: the loader said no */
        sl->pending = -1; picking = -1;
        say("NO ROOM", "DSP FULL");
    }
}

/* the storm (12 Nov 2026, late): every knob the weather may move -- all of an effect's, not only the ones a rule
 * names -- is thrown somewhere new every few seconds, each by itself, and all of them at once at a strike. A
 * knob with a rule stays inside the rule's range (feedback and resonance keep their ceilings); the others take
 * their whole range. MIX and a knob the musician holds are left alone, as in every weather. */
static int16_t storm_to[N_SLOTS][6];
static uint32_t storm_at[N_SLOTS][6];
static int storm_target(int s, int k)
{
    Slot *sl = &slots[s];
    if (ticks >= storm_at[s][k] || storm_to[s][k] < 0) {
        int r = rule_of(sl->knob[k]), lo = 0, hi = sl->count[k] - 1;
        if (r >= 0) { lo = rules[r].lo; hi = rules[r].hi; if (rules[r].role == R_RATE && name_has(sl->name, "DIST")) { lo = 97; hi = 127; } }
        if (hi > sl->count[k] - 1) hi = sl->count[k] - 1;
        if (lo > hi) lo = hi;
        storm_to[s][k] = (int16_t)range(lo, hi);
        storm_at[s][k] = ticks + (uint32_t)range(61 * 2, 61 * 9);
    }
    return storm_to[s][k];
}
static int mod_slot_of(int s, int k);   /* mod.c */
static int xf_mode(void); static int xf_scale(void);   /* master.c */
/* the weather and the musician, written into each slot */
static void slots_sound(void)
{
    int xf = -1;   /* no crossfade between weathers any more (12 Nov 2026) */
    for (int s = 0; s < N_SLOTS; s++) {
        Slot *sl = &slots[s];
        if (sl->row <= 0 || sl->pending >= 0) continue;
        int t = slot_track(s), fx2 = slot_fx(s);
        for (int k = 0; k < 6; k++) {
            if (!sl->knob[k][0]) continue;
            if (sl->user[k] < 0 && !knob_is_mix(sl->knob[k])) {      /* the weather moves a knob nobody holds -- except MIX */
                /* the knob's place under every weather that is on, averaged (none on: CLEAR's) */
                int want = -1, sum = 0, cnt = 0;
                for (int w = 1; w < N_WEATHER; w++) if (WON(w)) { int t1 = slot_target(s, k, w); if (t1 >= 0) { sum += t1; cnt++; } }
                want = cnt ? (sum + cnt / 2) / cnt : slot_target(s, k, CLEAR);
                if (WON(STORM)) want = storm_target(s, k); else storm_to[s][k] = -1;
#if defined(AERIAOS)
                /* a knob no weather has a rule for sits at the effect's own setting, and goes back there after a storm */
                if (want < 0 && sl->row > 0 && sl->row <= FX_COUNT) want = fx_info[sl->row - 1].def[k];
#endif
                if (want >= 0 && xf > 0) { int b = slot_target(s, k, xf_right); if (b >= 0) want += (b - want) * xf / 127; }
                if (want >= 0 && thunder && rule_of(sl->knob[k]) >= 0 && rules[rule_of(sl->knob[k])].role == R_OPEN) want = want * (256 - thunder) / 256;
                if (want >= 0) sl->value[k] = (int16_t)(sl->value[k] < want ? sl->value[k] + 1 : sl->value[k] > want ? sl->value[k] - 1 : want);
            }
            int v = slot_value(s, k) + mod_slot_of(s, k);
            if (knob_is_mix(sl->knob[k]) && xf_mode() == 2) v = v * xf_scale() / 127;   /* the crossfader as everything's wet */
            if ((slot_bypass >> s) & 1) { int r = rule_of(sl->knob[k]); if (r >= 0 && (rules[r].role == R_SPACE || rules[r].role == R_SEND)) v = 0; }
            lane_put(t, (fx2 ? 24 : 18) + k, clamp(v, 0, sl->count[k] - 1));
        }
    }
}

/* ---- first start: put HEAD, TAP and the default chain in place, by name, one pick at a time ----
 * Names match the chooser rows (desc+9) by prefix, case aside. A name that is not installed is
 * skipped; a slot already holding it is left alone. */
/* the chain's set-up ends by rebuilding every slot, which lets go of every knob: the saved holds (and the dry start)
 * go back on after it -- they used to be lost at every start (found 12 Nov 2026) */
#if defined(AERIAOS)
static void aeria_state_reapply_slots(void);
static void slots_reheld(void) { aeria_state_reapply_slots(); }
#else
static void slots_reheld(void) {}
#endif
typedef struct { uint8_t track, fx2; char name[13]; } Want_fx;
static const Want_fx setup_list[] = {
    {0, 0, "AERIA TAP"}, {1, 0, "AERIA TAP"}, {4, 0, "AERIA HEAD"},
    /* pitch > disto > grains > ring > smear > filter: three long-buffer effects fill this core's FX2 buffers */
    {4, 1, "AERIA PITCH"}, {5, 0, "NONE"}, {5, 1, "AERIA GRAIN"},
    {6, 0, "AERIA RING"}, {6, 1, "AERIA SMEAR"}, {7, 0, "NONE"}, {7, 1, "NONE"},   /* no stock effect anywhere */
};
#define N_SETUP ((int)(sizeof setup_list / sizeof setup_list[0]))
static int setup_at = -1, setup_wait, setup_want;
static int row_of_prefix(int fx2, const char *want)
{
    int n = list_count(fx2);
    char buf[14];
    if (want[0] == 'N' && want[1] == 'O' && want[2] == 'N' && want[3] == 'E' && !want[4]) return 0;
    for (int r = 1; r < n; r++) {
        list_name(fx2, r, buf);
        int i = 0;
        while (want[i] && buf[i] == want[i]) i++;
        if (!want[i]) return r;
    }
    return -1;
}
static void setup_start(void) { setup_at = 0; setup_wait = 0; setup_want = -1; }
static int setup_busy(void) { return setup_at >= 0 && setup_at < N_SETUP; }
static void setup_tick(void)
{
    if (!setup_busy() || picking >= 0) return;
    const Want_fx *w = &setup_list[setup_at];
    if (setup_want >= 0) { /* waiting for the last pick to land */
        if (live_fx(w->track, w->fx2) == setup_want || ++setup_wait > 300) { setup_at++; setup_want = -1; setup_wait = 0; }
        if (!setup_busy()) { slots_init(); slots_reheld(); sync_to_card(); say("CHAIN READY", 0); }
        return;
    }
    int row = row_of_prefix(w->fx2, w->name);
    if (row < 0 || live_fx(w->track, w->fx2) == list_id(w->fx2, row)) { setup_at++; if (!setup_busy()) { slots_init(); slots_reheld(); sync_to_card(); say("CHAIN READY", 0); } return; }
    setup_want = list_id(w->fx2, row);
    pick_effect(w->track, w->fx2, row);
}
