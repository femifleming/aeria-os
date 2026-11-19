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
     {"WTHR", OWN, 0, N_WEATHER - 1, CLEAR, CLEAR, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, {"WX", OWN, 0, 1, 0, 0, 0},   /* WX (13 Nov 2026): 1 = the weather OFF altogether (0, as old projects hold, is on) */   /* (XFAD and XF B, the crossfade between two weathers, are gone: the fader is the scenes' now) */
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
     {"LANG", OWN, 0, 1, 0, 0, 0} /* 0 English, 1 Dutch (15 Nov 2026) */, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* GUMBALLS: MIDI, its own menu */
    {EMPTY},
    /* GAIN: input A/B and C/D (THRU VOL, 64 = unity), the drive into the chain (HEAD IN),
     * the chain's output (T8 AMP VOL, 108 = unity), the cue send and the balance */
    {{"AB", OWN, 0, 127, 64, 64, 0}, {"CD", OWN, 0, 127, 64, 64, 0}, {"DRIV", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"CUE", OWN, 0, 127, 127, 127, 0}, {"BAL", OWN, 0, 127, 64, 64, 0},
     /* (FUNC layer: the master EQ was here, 12-13 Nov 2026; removed at the owner's request. Saved values there are ignored.) */
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
    /* MASTER (12 Nov 2026): the freeze delay on the main mix, after the chain and never moved by the weather. TIME in
     * sixteenth notes at TMPO (the trigs pick it too), its feedback with a high-pass and a lowpass, how much goes in
     * and how much comes out; YES freezes it, NO taps the tempo. FUNC layer: TMPO (BPM). */
    {{"TIME", OWN, 1, 16, 4, 4, 0}, {"FDBK", OWN, 0, 127, 50, 50, 0}, {"HPF", OWN, 0, 127, 10, 10, 0},
     {"LPF", OWN, 0, 127, 100, 100, 0}, {"SEND", OWN, 0, 127, 100, 100, 0}, {"WET", OWN, 0, 127, 0, 0, 0},
     {"TMPO", OWN, 0, 127, 60, 60, 0}, EMPTY /* (XFRZ: folded into XMOD = FREEZE, 15 Nov 2026) */, {"RFRZ", OWN, 0, 127, 0, 0, 0}, {"XMOD", OWN, 0, 4, 0, 0, 0}, EMPTY, EMPTY},   /* XMOD: what the crossfader does: SCEN, DJF (a DJ filter), WET (every effect dry at A), MOD (MOD depth 0 at A) */   /* RFRZ: Chloaelis's random freeze, the chance of a hold (12 Nov 2026) */   /* XFRZ 2: the crossfader freezes the delay at B (1: it does not) */
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
/* ---- language (sadnoise, 15 Nov 2026): English or Dutch -- SYSTEM, the second knob set, A: LANG ---------------------
 * Every toast, label and page text goes through T(): the table is looked up by the whole string, so anything not in
 * it (knob names, effect names, numbers, the panel's own words) stays as it is. The font has no accents; Dutch does
 * without them. */
typedef struct { const char *en, *nl; } Tr;
static const Tr tr_nl[] = {
    {"NEW SKY", "NIEUWE LUCHT"}, {"NEW STARS", "NIEUWE STERREN"}, {"NIGHT", "NACHT"}, {"DAY", "DAG"},
    {"CHAIN", "KETEN"}, {"SKY", "LUCHT"}, {"SOURCE", "BRON"}, {"STARS", "STERREN"}, {"BALLOON", "BALLON"}, {"BALLOONS", "BALLONNEN"},
    {"SYSTEM", "SYSTEEM"}, {"GUMBALLS", "KAUWGOM"}, {"GAIN", "NIVEAU"}, {"SHELF", "KAST"}, {"EMPTY", "LEEG"},
    {"EMPTY  YES: SHELF", "LEEG  YES: KAST"}, {"YES PUT ON  NO BACK", "YES PLAATS  NO TERUG"}, {"FILLING THE SHELF", "KAST WORDT GEVULD"},
    {"CHAIN READY", "KETEN KLAAR"}, {"NO ROOM", "GEEN RUIMTE"}, {"DSP FULL", "DSP VOL"}, {"ON ANOTHER", "OP EEN ANDER"}, {"TRACK", "SPOOR"},
    {"NO EFFECT", "GEEN EFFECT"}, {"YES PICKS", "YES KIEST"}, {"NONE", "GEEN"}, {"NONE  YES PICKS", "GEEN  YES KIEST"},
    {"WEATHER", "WEER"}, {"CLEAR", "HELDER"}, {"CLOUDY", "BEWOLKT"}, {"RAIN", "REGEN"}, {"SNOW", "SNEEUW"}, {"HEAT", "HITTE"}, {"FOG", "MIST"},
    {"HAIL", "HAGEL"}, {"TWISTER", "WINDHOOS"}, {"ALL OFF", "ALLES UIT"}, {"GUST", "WINDVLAAG"}, {"LIGHTNING LENDS", "BLIKSEM LEENT"}, {"STORM TAKES", "STORM NEEMT"},
    {"LAYER", "LAAG"}, {"KNOBS", "KNOPPEN"}, {"TAP", "TIK"}, {"FREEZE", "BEVRIEZEN"}, {"FROZEN", "BEVROREN"}, {"EFFECTS WET", "EFFECTEN NAT"}, {"MOD DEPTH", "MOD DIEPTE"},
    {"DELAY TIME", "DELAY TIJD"}, {"DELAY HIGH-PASS", "DELAY HPF"}, {"DELAY LOWPASS", "DELAY LPF"}, {"DELAY WET", "DELAY NAT"},
    {"SCALE", "TOONLADDER"}, {"ROOT", "GRONDTOON"}, {"OCTAVE", "OCTAAF"}, {"RANDOM", "WILLEKEURIG"}, {"RANDOM OCT", "WILLEK OCT"},
    {"TUNE 1", "STEM 1"}, {"TUNE 2", "STEM 2"}, {"TUNE 3", "STEM 3"}, {"GRAIN SIZE", "KORREL"}, {"PITCH", "TOON"}, {"VELOCITY", "AANSLAG"},
    {"LENGTH", "LENGTE"}, {"SPEED", "SNELHEID"}, {"SIZE", "GROOTTE"}, {"SPREAD", "SPREIDING"}, {"DECAY", "UITSTERVEN"}, {"MODE", "MODUS"},
    {"CHANNEL", "KANAAL"}, {"FIRST CH", "EERSTE KAN"}, {"GRAVITY", "ZWAARTE"}, {"BOUNCE", "STUITER"}, {"LEVEL", "NIVEAU"},
    {"BALLS", "BALLEN"}, {"BALL", "BAL"}, {"MUTED", "GEDEMPT"}, {"YES ADDS A BALL", "YES VOEGT BAL TOE"}, {"MARBLES", "KNIKKERS"}, {"YES STARTS", "YES START"},
    {"MIDI ON", "MIDI AAN"}, {"MIDI OFF", "MIDI UIT"}, {"LISTEN", "LUISTER"}, {"LISTENING", "LUISTERT"}, {"STEADY", "STABIEL"}, {"CLARITY", "HELDERHEID"},
    {"ARP TICKS", "ARP TIKKEN"}, {"NO INPUT TAP", "GEEN INGANG"}, {"NO CLOCK", "GEEN KLOK"}, {"PITCH MIDI", "TOON MIDI"},
    {"PROJECTS", "PROJECTEN"}, {"SAVED", "OPGESLAGEN"}, {"NOT SAVED", "NIET OPGESLAGEN"}, {"LOADED", "GELADEN"}, {"LOAD FAILED", "LADEN MISLUKT"},
    {"LOADING", "LADEN"}, {"NAME", "NAAM"}, {"YES SAVES", "YES SLAAT OP"}, {"YES LOADS  FUNC+YES SAVES", "YES LAADT  FUNC+YES SLAAT OP"},
    {"FUNC+YES SAVES HERE", "FUNC+YES SLAAT HIER OP"}, {"FUNC+YES SAVES", "FUNC+YES SLAAT OP"}, {"KNOB: ANY LETTER", "KNOP: ELKE LETTER"},
    {"DAMAGED", "BESCHADIGD"}, {"BLANK START", "LEGE START"}, {"BLANK SOUND", "LEEG GELUID"}, {"RESET ALL", "ALLES RESET"}, {"DONE", "KLAAR"},
    {"YES       INIT: SOUND ONLY", "YES       INIT: ALLEEN GELUID"}, {"FUNC+YES  RESET ALL: SOUND,", "FUNC+YES  ALLES RESET: GELUID,"},
    {"  BUFFERS AND SAMPLES", "  BUFFERS EN SAMPLES"}, {"SEQUENCES ARE KEPT", "SEQUENTIES BLIJVEN"}, {"NO        BACK", "NO        TERUG"},
    {"USB DISK MODE", "USB SCHIJF MODUS"}, {"NO CARD", "GEEN KAART"}, {"BACK", "TERUG"}, {"EJECTED", "UITGEWORPEN"}, {"NOT YET", "NOG NIET"},
    {"1 USB DISK MODE", "1 USB SCHIJF MODUS"}, {"NO: BACK TO THE SKY", "NO: TERUG NAAR DE LUCHT"}, {"A SOUND", "A GELUID"}, {"C LIGHTS", "C LAMPEN"},
    {"F WAIT", "F WACHT"}, {"YES TRY", "YES PROBEER"}, {"LANGUAGE", "TAAL"}, {"ENGLISH", "ENGELS"},
    {"PICK A SAMPLE", "KIES EEN SAMPLE"}, {"PICK A FOLDER", "KIES EEN MAP"}, {"NO WAVS HERE", "GEEN WAVS HIER"},
    {"YES LOAD  FUNC+YES HEAR", "YES LAAD  FUNC+YES HOOR"}, {"YES DEAL  FUNC+YES HEAR", "YES DEEL  FUNC+YES HOOR"}, {"YES ONE  FUNC+YES KIT  > HEAR", "YES EEN  FUNC+YES KIT  > HOOR"},
    {"SLICES", "PLAKJES"}, {"TRACK HAS", "SPOOR HEEFT"}, {"TO SLICE", "TE SNIJDEN"}, {"IS EMPTY", "IS LEEG"}, {"BROUGHT OVER", "OVERGEZET"}, {"AUDIO TRACKS", "AUDIOSPOREN"}, {"ONLY", "ALLEEN"},
    {"THE MIX", "DE MIX"}, {"BANK 6", "BANK 6"}, {"TRIG 1-8", "TRIG 1-8"}, {"3 LOCKS", "3 LOCKS"},
    {"SONG", "SONG"}, {"SONG ON", "SONG AAN"}, {"SONG OFF", "SONG UIT"}, {"ROW ADDED", "RIJ ERBIJ"}, {"ROW REMOVED", "RIJ WEG"}, {"SONG FULL", "SONG VOL"},
    {"TIMES", "KEER"}, {"NONE", "GEEN"}, {"MUTE SCENE", "MUTE SCENE"}, {"ROW PATTERN", "RIJ PATROON"}, {"EMPTY: YES ADDS A ROW", "LEEG: YES VOEGT EEN RIJ TOE"},
    {"YES ADD  FUNC+YES DEL", "YES ERBIJ  FUNC+YES WEG"}, {"PTN   TIMES  MUTES  ON/OFF", "PTN   KEER   MUTES  AAN/UIT"}, {"ALL", "ALLES"}, {"KIT LOADED", "KIT GELADEN"}, {"STARS LOADED", "STERREN GELADEN"},
    {"CATCHING", "VANGT"}, {"1 UP", "1 OP"}, {"2 UP", "2 OP"}, {"3 UP", "3 OP"}, {"BALLOON 1", "BALLON 1"}, {"BALLOON 2", "BALLON 2"}, {"BALLOON 3", "BALLON 3"},
    {"QUIET", "STIL"}, {"SINGING", "ZINGT"}, {"STEREO", "STEREO"}, {"LOOP PLAY", "LOOP SPEELT"}, {"DRY + CLEAR", "DROOG + HELDER"},
    {"GRID", "RASTER"}, {"CLEARED", "GEWIST"}, {"COPIED", "GEKOPIEERD"}, {"PASTED", "GEPLAKT"}, {"NOTHING", "NIETS"}, {"PATTERN", "PATROON"},
    {"MUTES KEPT IN", "MUTES BEWAARD IN"}, {"FUNC KEEPS", "FUNC BEWAART"}, {"SCENE EMPTY", "SCENE LEEG"}, {"STEP", "STAP"},
    {"BACKWARD", "ACHTERUIT"}, {"FORWARD", "VOORUIT"}, {"LOCKS CLEARED", "LOCKS GEWIST"}, {"A STEP", "PER STAP"}, {"SILENCE", "STILTE"},
    {"AT THE BAR", "BIJ DE MAAT"}, {"VERB ON", "VERB OP"}, {"A TRACK", "EEN SPOOR"}, {"MOD CLOSED", "MOD DICHT"}, {"TURN A KNOB", "DRAAI EEN KNOP"},
    {"TO MOVE", "OM TE BEWEGEN"}, {"LEARN: YES", "LEREN: YES"}, {"YES LEARN  FUNC+YES CLR", "YES LEER  FUNC+YES WIS"}, {"MOD TO", "MOD NAAR"},
    {"GRAIN CLOUD", "KORRELWOLK"}, {"CLOUD", "WOLK"}, {"SKEW", "SCHEEF"}, {"PUFF", "PLUIS"}, {"SOFT", "ZACHT"}, {"EDGE", "RAND"}, {"FLIP", "SPIEGEL"},
    {"SCATTER", "VERSTROOI"}, {"EACH", "ELK"}, {"OPEN", "OPEN"}, {"FULL", "VOL"}, {"EVEN", "GELIJK"}, {"NEXT", "VOLGENDE"}, {"SET", "GEZET"},
    {"MAJOR", "MAJEUR"}, {"MINOR", "MINEUR"}, {"DORIAN", "DORISCH"}, {"LYDIAN", "LYDISCH"}, {"PHRYGIAN", "FRYGISCH"}, {"CHROMATIC", "CHROMATISCH"},
    {"WHOLE", "HEELTOON"}, {"STACKED", "GESTAPELD"}, {"NOTE", "NOOT"}, {"CHORD", "AKKOORD"}, {"JUMP", "SPRONG"},
    {"SINE", "SINUS"}, {"SAW", "ZAAG"}, {"RAMP", "HELLING"}, {"SQR", "BLOK"}, {"FWD", "VOOR"}, {"REV", "ACHTER"}, {"DRNK", "DRONK"},
    {"ACCEL", "VERSNEL"}, {"DECEL", "VERTRAAG"}, {"ARCH", "BOOG"}, {"BULGE", "BUIK"}, {"WAVE", "GOLF"}, {"RAND", "WILLEK"},
    {"COIN", "MUNT"}, {"DIVIDE", "DEEL"}, {"INDEP", "ONAFH"}, {"AND", "EN"}, {"FLOCK X", "ZWERM X"}, {"FLOCK Y", "ZWERM Y"}, {"FLOCK SPRD", "ZWERM SPRD"},
    {"BIRD 1", "VOGEL 1"}, {"BIRD 2", "VOGEL 2"}, {"FIX", "VAST"}, {"ORBIT", "BAAN"}, {"DIVE", "DUIK"}, {"BOTH", "BEIDE"},
    {"NO LOOP LOADED", "GEEN LOOP GELADEN"}, {"NO MORE KNOBS", "GEEN KNOPPEN MEER"}, {"FUNC+1 2: PICK FROM CARD", "FUNC+1 2: KIES VAN KAART"},
    {"FILTERS OFF", "FILTERS UIT"}, {"PLANET", "PLANEET"}, {"BIRDS", "VOGELS"}, {"STAR", "STER"}, {"SOLO OFF", "SOLO UIT"}, {"VOLUME", "VOLUME"},
    {"WEATHER OFF", "WEER UIT"}, {"OFF", "UIT"}, {"ON", "AAN"}, {"SND OFF", "GLD UIT"}, {"IN AB", "IN AB"},
    {"A B STEREO", "A B STEREO"}, {"MONO", "MONO"}, {"AB STEREO", "AB STEREO"}, {"CD STEREO", "CD STEREO"}, {"TEMPO", "TEMPO"},
    {"PADS 1-8", "PADS 1-8"}, {"TRIG 1-16", "TRIG 1-16"}, {"TRIG 1-4", "TRIG 1-4"}, {"MUTE SCENE", "MUTE SCENE"}, {"MUTE SCENES", "MUTE SCENES"},
    {"RESAMPLE", "RESAMPLE"}, {"SEQUENCER", "SEQUENCER"}, {"MIXER", "MIXER"}, {"LIMITER", "LIMITER"}, {"CROSSFADER", "CROSSFADER"},
    {"SCENES", "SCENES"}, {"DJ FILTER", "DJ FILTER"}, {"CHAIN SET", "REEKS GEZET"}, {"PAGE: BACK", "PAGE: TERUG"}, {"LAYER 2", "LAAG 2"},
    {"DUTCH", "NEDERLANDS"},
};
#define N_TR ((int)(sizeof tr_nl / sizeof tr_nl[0]))
static int lang_nl(void) { return pv(PG_SYSTEM, 6) == 1; }
static const char *T(const char *s)
{
    if (!s || !s[0] || !lang_nl()) return s;
    char c = s[0];
    for (int i = 0; i < N_TR; i++) {
        const char *e = tr_nl[i].en;
        if (e[0] != c) continue;
        int k = 1;
        while (e[k] && e[k] == s[k]) k++;
        if (!e[k] && !s[k]) return tr_nl[i].nl;
    }
    return s;
}


/* sources: off by default */
static int src_loop, src_stars, src_balloons;

/* ---- state -------------------------------------------------------------------------- */
static int muted[8];
static int night, hidden, func_held, func_latch, ready, page, page_until, intro;
static uint32_t ui_until; /* the page shows until then; after, only the sky */
#define UI_SHOW 0x40000000u   /* (15 Nov 2026) a page stays up until NO hides it: no more vanishing after ten seconds */
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
    if (!auto_weather || pv_base(PG_SKY, 5) || spell_left-- > 0) return;
    /* 13 Nov 2026: AUTO moves between the weathers that are left (clear, rain, fog; the aurora comes with a clear sky) */
    static const uint8_t pool[3] = {CLEAR, RAIN, FOG};
    int w = weather;
    for (int i = 0; i < 8 && w == weather; i++) w = pool[range(0, 2)];
    set_weather(w);
#if defined(AERIAOS)
    xw_auto(weather);
#endif
}

