/* ---- the effect chain: T5 THRU takes the inputs, T6-T8 NEIGHBOR each take the
 * track before; two stock effects per track, eight in series --------------------- */
#define CHAIN_T 4
#define N_FX 8
typedef struct {
    char name[10];
    char knob[6][5];      /* "" = an empty slot the writer refuses */
    uint8_t hi[6];
    uint8_t def[6];
    uint8_t neutral[6];   /* what bypass writes */
} Fx;
static const Fx fx[N_FX] = {
    {"FILTER", {"BASE", "WDTH", "Q", "DPTH", "ATK", "DEC"}, {127, 127, 127, 127, 127, 127}, {0, 127, 0, 64, 0, 64}, {0, 127, 0, 64, 0, 64}},
    {"DELAY", {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {127, 127, 127, 127, 127, 127}, {47, 40, 90, 0, 127, 50}, {47, 0, 0, 0, 127, 0}},
    {"CHORUS", {"DEL", "DEP", "SPD", "FB", "WID", "MIX"}, {127, 127, 127, 127, 127, 127}, {64, 64, 13, 0, 127, 30}, {64, 64, 13, 0, 127, 0}},
    {"DARK REV", {"TIME", "SHVG", "SHVF", "HP", "LP", "MIX"}, {127, 127, 127, 127, 127, 127}, {60, 0, 127, 0, 127, 35}, {60, 0, 127, 0, 127, 0}},
    {"COMB", {"PTCH", "TUNE", "LP", "FB", "", "MIX"}, {107, 127, 127, 127, 127, 127}, {26, 64, 127, 96, 127, 0}, {26, 64, 127, 96, 127, 0}},
    {"LOOP", {"TIME", "FB", "VOL", "BASE", "WDTH", "SEND"}, {127, 127, 127, 127, 127, 127}, {90, 100, 70, 0, 100, 20}, {90, 0, 0, 0, 127, 0}},
    {"LO-FI", {"DIST", "NOIS", "AMF", "SRR", "BRR", "AMD"}, {127, 127, 127, 127, 127, 127}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
    {"PLATE", {"TIME", "DAMP", "GATE", "HP", "LP", "MIX"}, {127, 127, 127, 127, 127, 127}, {60, 30, 127, 0, 127, 30}, {60, 30, 127, 0, 127, 0}},
};
static const char fx_letter[N_FX] = {'F', 'D', 'C', 'R', 'M', 'L', 'X', 'P'};
static int16_t fx_val[N_FX][6], fx_user[N_FX][6];
static uint8_t bypass;
static int sel_fx;
static int fx_track(int i) { return CHAIN_T + i / 2; }
static int fx_slot(int i, int k) { return ((i & 1) ? 24 : 18) + k; }
static int fx_value(int i, int k) { return fx_user[i][k] >= 0 ? fx_user[i][k] : fx_val[i][k]; }

/* What each weather wants: {effect, knob, value}, ending with {255}. */
typedef struct { uint8_t fx, k, v; } Want;
static const Want weather_wants[N_WEATHER][16] = {
    /* CLEAR  */ {{0, 1, 127}, {0, 2, 0}, {1, 1, 30}, {1, 5, 30}, {2, 5, 25}, {3, 0, 50}, {3, 5, 25}, {4, 5, 0}, {5, 5, 0}, {6, 0, 0}, {6, 1, 0}, {7, 5, 20}, {255, 0, 0}},
    /* CLOUDY */ {{0, 1, 110}, {0, 2, 10}, {1, 1, 45}, {1, 5, 45}, {2, 5, 45}, {3, 0, 70}, {3, 4, 100}, {3, 5, 40}, {4, 5, 10}, {5, 5, 20}, {5, 1, 80}, {6, 0, 0}, {6, 1, 0}, {7, 5, 30}, {255, 0, 0}},
    /* RAIN   */ {{0, 1, 80}, {0, 2, 20}, {1, 0, 30}, {1, 1, 60}, {1, 5, 55}, {2, 5, 30}, {3, 0, 90}, {3, 4, 80}, {3, 5, 60}, {4, 3, 100}, {4, 5, 20}, {5, 5, 35}, {6, 1, 20}, {7, 5, 40}, {255, 0, 0}},
    /* STORM  */ {{0, 1, 70}, {0, 2, 50}, {1, 1, 100}, {1, 5, 80}, {2, 5, 20}, {3, 0, 110}, {3, 5, 70}, {4, 3, 110}, {4, 5, 30}, {5, 1, 115}, {5, 5, 70}, {6, 0, 60}, {6, 1, 30}, {7, 5, 50}, {255, 0, 0}},
    /* SNOW   */ {{0, 1, 60}, {0, 2, 0}, {1, 0, 90}, {1, 1, 70}, {1, 5, 50}, {2, 2, 6}, {2, 5, 40}, {3, 0, 127}, {3, 4, 60}, {3, 5, 100}, {4, 5, 0}, {5, 1, 120}, {5, 5, 60}, {6, 3, 30}, {7, 0, 127}, {7, 5, 80}},
    /* HEAT   */ {{0, 0, 34}, {0, 1, 100}, {1, 1, 40}, {1, 5, 30}, {2, 1, 110}, {2, 2, 80}, {2, 5, 100}, {3, 5, 30}, {4, 1, 80}, {4, 5, 15}, {5, 5, 0}, {6, 2, 60}, {6, 5, 50}, {7, 5, 20}, {255, 0, 0}},
    /* FOG    */ {{0, 1, 50}, {0, 2, 0}, {1, 1, 60}, {1, 4, 60}, {1, 5, 50}, {2, 5, 60}, {3, 0, 100}, {3, 4, 50}, {3, 5, 90}, {4, 5, 0}, {5, 1, 100}, {5, 5, 40}, {6, 4, 20}, {7, 4, 60}, {7, 5, 70}, {255, 0, 0}},
};
static int want_of(int w, int i, int k)
{
    for (int n = 0; n < 16 && weather_wants[w][n].fx != 255; n++)
        if (weather_wants[w][n].fx == i && weather_wants[w][n].k == k) return weather_wants[w][n].v;
    return -1;
}

/* ---- the other pages ------------------------------------------------------------- */
typedef struct {
    char name[5];
    uint8_t mask, slot;   /* tracks and lane slot; mask 0 = AERIA's own */
    uint8_t lo, hi;
    int16_t value, user;  /* user -1 follows the sky */
    uint8_t drift;
} Param;

#define GR 0x01u
#define ST 0x02u
#define PL 0x04u
#define BL 0x08u
#define OWN 0, 0
#define EMPTY {"", 0, 0, 0, 0, 0, 0, 0}

enum { PG_CHAIN, PG_SKY, PG_ALGO, PG_GRAIN, PG_STARS, PG_PLAYER, PG_BALLOON, PG_MIX, PG_SYSTEM, N_PAGES };
static const char *const page_name[N_PAGES] = {"CHAIN", "SKY", "ALGO", "GRAINS", "STARS", "PLAYER", "BALLOON", "MIX", "SYSTEM"};
enum { ALGO_NONE, ALGO_GRAINS, ALGO_STARS, ALGO_PLAYER, ALGO_BALLOON, N_ALGO };
static const char *const algo_name[N_ALGO] = {"NONE", "GRAINS", "STARS", "PLAYER", "BALLOON"};
static int algo;

static Param params[N_PAGES][12] = {
    /* CHAIN: drawn from fx[] */
    {EMPTY},
    /* SKY */
    {{"WIND", OWN, 0, 18, 12, 12, 0}, {"CLDS", OWN, 0, 7, 5, 5, 0}, {"WAVE", OWN, 0, 6, 2, 2, 0},
     {"WTHR", OWN, 0, N_WEATHER - 1, CLOUDY, CLOUDY, 0}, {"AUTO", OWN, 0, 1, 0, 0, 0}, {"XFAD", OWN, 0, 1, 1, 1, 0},
     {"XF B", OWN, 0, N_WEATHER - 1, STORM, STORM, 0}, {"SPEL", OWN, 1, 8, 4, 4, 0}, {"DRFT", OWN, 0, 127, 20, 20, 0},
     {"RATE", OWN, 1, 127, 30, 30, 0}, {"NITE", OWN, 0, 1, 0, 0, 0}, {"LITE", OWN, 0, 1, 1, 1, 0}},
    /* ALGO: a menu */
    {EMPTY},
    /* GRAINS: FRACT on T1 */
    {{"POS", GR, 0, 0, 127, 18, 18, 0}, {"SIZE", GR, 1, 0, 127, 86, 86, 1}, {"REVS", GR, 2, 0, 127, 127, 127, 0},
     {"SPRD", GR, 3, 0, 127, 0, 0, 1}, {"SHPE", GR, 4, 0, 127, 64, 64, 1}, {"PTCH", GR, 5, 0, 127, 64, 64, 0},
     {"ATK", GR, 12, 0, 127, 0, 0, 0}, {"REL", GR, 14, 0, 127, 127, 127, 0}, {"SCAN", OWN, 0, 127, 64, 64, 0},
     {"FRZ", OWN, 0, 1, 0, 0, 0}, {"BAL", GR, 16, 0, 127, 64, 64, 1}, {"HOLD", GR, 13, 0, 127, 127, 127, 0}},
    /* STARS: one-shots on T2 */
    {{"SPD", OWN, 0, 127, 24, 24, 0}, {"NUM", OWN, 1, 8, 4, 4, 0}, {"PRND", OWN, 0, 127, 0, 0, 0},
     {"ARND", OWN, 0, 127, 0, 0, 0}, {"DRND", OWN, 0, 127, 20, 20, 0}, {"CHNC", OWN, 0, 127, 127, 127, 0},
     {"PTCH", OWN, 0, 127, 64, 64, 0}, {"ATK", OWN, 0, 127, 0, 0, 0}, {"DEC", OWN, 0, 127, 96, 96, 0},
     {"BANK", OWN, 0, 7, 0, 0, 0}, {"MODE", OWN, 0, 1, 0, 0, 0}, {"BAL", ST, 16, 0, 127, 64, 64, 1}},
    /* PLAYER: a sample on T3 */
    {{"STRT", PL, 1, 0, 127, 0, 0, 0}, {"LEN", PL, 2, 0, 127, 127, 127, 0}, {"RATE", PL, 3, 0, 127, 127, 127, 0},
     {"PTCH", PL, 0, 0, 127, 64, 64, 0}, {"RTRG", PL, 4, 0, 127, 0, 0, 0}, {"RTIM", PL, 5, 0, 127, 79, 79, 0},
     {"ATK", PL, 12, 0, 127, 0, 0, 0}, {"HOLD", PL, 13, 0, 127, 127, 127, 0}, {"REL", PL, 14, 0, 127, 127, 127, 0},
     {"SLOT", OWN, 0, 127, 0, 0, 0}, {"QNT", OWN, 0, 1, 1, 1, 0}, {"BAL", PL, 16, 0, 127, 64, 64, 1}},
    /* BALLOON: Flex on T4 */
    {{"GRN", BL, 5, 0, 127, 40, 40, 0}, {"SPRY", OWN, 0, 127, 30, 30, 0}, {"PTCH", BL, 0, 0, 127, 64, 64, 0},
     {"RPT", BL, 4, 0, 127, 127, 127, 0}, {"POS", OWN, 0, 127, 20, 20, 0}, {"RATE", BL, 3, 0, 127, 127, 127, 0},
     {"ATK", BL, 12, 0, 127, 0, 0, 0}, {"REL", BL, 14, 0, 127, 127, 127, 0}, {"LEN", BL, 2, 0, 127, 127, 127, 0},
     {"BAL", BL, 16, 0, 127, 64, 64, 1}, {"ROT", OWN, 0, 127, 0, 0, 0}, {"HOLD", BL, 13, 0, 127, 127, 127, 0}},
    /* MIX: what goes in, how loud things come out */
    {{"INAB", OWN, 0, 4, 1, 1, 0}, {"INCD", OWN, 0, 4, 1, 1, 0}, {"GAIN", OWN, 0, 127, 64, 64, 0},
     {"OUT", OWN, 0, 127, 108, 108, 0}, {"ALGO", OWN, 0, 127, 100, 100, 0}, {"CUE", OWN, 0, 127, 127, 127, 0},
     {"VOL1", OWN, 0, 127, 108, 108, 0}, {"VOL2", OWN, 0, 127, 108, 108, 0}, {"VOL3", OWN, 0, 127, 108, 108, 0},
     {"BAL", OWN, 0, 127, 64, 64, 0}, {"DRY", OWN, 0, 1, 0, 0, 0}, {"TIDE", OWN, 0, 127, 0, 0, 0}},
    /* SYSTEM */
    {{"SND", OWN, 0, 2, 1, 1, 0}, {"INTR", OWN, 0, 1, 1, 1, 0}, {"LITE", OWN, 0, 1, 1, 1, 0},
     EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY},
};
#define P(page, i) (params[page][i])
static int pv(int page, int i) { Param *p = &params[page][i]; return p->user >= 0 ? p->user : p->value; }

/* ---- state -------------------------------------------------------------------------- */
static int muted[8];
static int night, hidden, func_held, func_latch, ready, page, page_until, intro;
static int scene_held;
static int16_t drift_now[N_PAGES][6], drift_target[N_PAGES][6];

static void set_weather(int w)
{
    if (w == weather) return;
    weather = w;
    amount = 0;
    spell_left = (uint32_t)range(spell_s[w][0], spell_s[w][1]) * 60u * (uint32_t)pv(PG_SKY, 7) / 4u;
    P(PG_SKY, 3).value = P(PG_SKY, 3).user = (int16_t)w;
    for (int i = 0; i < N_FX; i++) for (int k = 0; k < 6; k++) if (want_of(w, i, k) >= 0) fx_user[i][k] = -1;
    say(weather_name[w], 0);
}
static void step_auto(void)
{
    if (!auto_weather || spell_left-- > 0) return;
    int pick = range(0, 19), w = 0;
    for (; w < N_WEATHER - 1; w++) { pick -= next_w[weather][w]; if (pick < 0) break; }
    set_weather(w == weather ? CLOUDY : w);
}
static void fx_init(void)
{
    for (int i = 0; i < N_FX; i++) for (int k = 0; k < 6; k++) { fx_val[i][k] = fx[i].def[k]; fx_user[i][k] = -1; }
}

