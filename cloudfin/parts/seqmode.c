/* ---- SEQUENCER: AERIA OS's second face (sadnoise, 12 Nov 2026) -----------------------------------------------------
 * FUNC + MIDI goes between the multi-effect (the sky) and the sequencer. Eight tracks, each its own channel into the
 * DSP: a sample or a machine of its own (SUB, FM, NOIS, PLUK; MIDI alone), a high-pass and a low-pass that are always there (resonance
 * fixed at 20 %), one effect, its place in the stereo field. The sound side is aeria-os/src/seq.c; the data seq.h.
 *
 *   T1-T8         the track           FUNC + T  mute it        CUE + T  solo it
 *   trigs         GRID (REC dark): the sixteen steps of the page -- a tap sets or clears one; HOLD one (or several)
 *                 and turn a knob: that step's own value (a parameter lock); while held the knobs are the step's
 *                 VEL MICR PROB COND RTCH CURV, YES turns it backward, and a page key (SRC AMP FX1 MIDI) brings that
 *                 page's knobs to lock.
 *                 LIVE (REC lit): 1-8 play the tracks (written into the pattern while it runs), 9-16 mute them
 *   LEFT / RIGHT  the page of sixteen steps (a track is up to 64 long)     UP / DOWN  the track
 *   PLAY / STOP   run / stop (STOP again: silence)        REC  GRID / LIVE
 *   SRC AMP LFO FX1 FX2   the knob pages: SRC (sample), AMP, MOD, FLT (filters, delay send), FX (the track's effect;
 *                 YES opens the list)          PAGE  PTN: lengths, swing, tempo, the ducking
 *   PATTERN + trig 1-4   the pattern (it changes at the bar); several in a row while held: a chain
 *   BANK          the mixer (meters; trigs 1-8 mute, 9-16 solo)      FUNC + BANK  resample the main output into sample 6.1
 *   SCENE A / B   held + a trig: that mute scene (with FUNC: the mutes as they are go into it)
 *   LEVEL         the track's volume       TEMPO  tap        FUNC + PLAY  clear the track        FUNC + YES (SRC)  load a kit
 *   MIX           the master page (freeze delay), any page key comes back */
#if defined(AERIAOS) && (defined(__m68k__) || defined(SEQ_HOST))
static int layer(void);
static void upper(const char *in, char *out, int max);
enum { SV_GRID, SV_MIXER, SV_MASTER };
enum { SQP_SRC, SQP_AMP, SQP_MOD, SQP_FLT, SQP_GEN, SQP_MIDI, SQP_FX, SQP_PTN, SQP_STEP, SQP_N };   /* SRC..MIDI can be locked on a step */
static const char *const sq_page_name[SQP_N] = {"SRC", "AMP", "MOD", "FLT", "GEN", "MIDI", "FX", "PTN", "STEP"};
enum { SP_NONE = -1, SP_LEN = -2, SP_MLEN = -3, SP_SWNG = -4, SP_TMPO = -5, SP_DSRC = -6, SP_DREL = -7,
       SP_VEL = -8, SP_MICR = -9, SP_PROB = -10, SP_COND = -11, SP_RTCH = -12, SP_SREV = -13, SP_CURV = -14 };
enum { SP_SYNC = -3 - 100, SP_PCCH = -4 - 100, SP_NIN = -5 - 100, SP_CLK = -6 - 100, SP_CAMT = -7 - 100, SP_CSED = -8 - 100 };   /* (kept apart from the step's own: these are the pattern page's kind) */
typedef struct { const char *lab; int8_t tp; uint8_t lo, hi; } SqP;
static const SqP sq_par[SQP_N][12] = {
    {{"SMPL", TP_SMPL, 0, 127}, {"PTCH", TP_PTCH, 28, 100}, {"STRT", TP_STRT, 0, 127}, {"REV", TP_REV, 0, 1}, {"MACH", TP_MACH, 0, SQM_N - 1}, {"SLIP", TP_SLIP, 0, 127}, {"RTIO", TP_FMR, 1, 127}, {"INDX", TP_FMI, 0, 127}, {"IDEC", TP_FMD, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"ATK", TP_ATK, 0, 127}, {"DEC", TP_DEC, 0, 127}, {"VOL", TP_VOL, 0, 127}, {"PAN", TP_PAN, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"PENV", TP_PENV, 0, 127}, {"PDEC", TP_PDEC, 0, 127}, {"DUCK", TP_DUCK, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"WAVE", TP_MODW, 0, 2}, {"DEST", TP_LFOD, 0, 8}, {"RATE", TP_LFOR, 0, 127}, {"DPTH", TP_LFOA, 0, 127}, {"PENV", TP_PENV, 0, 127}, {"PDEC", TP_PDEC, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"HP", TP_HP, 0, 127}, {"LP", TP_LP, 0, 127}, {"DLY", TP_DLY, 0, 127}, {"OUT", TP_OUT, 0, 3}, {"DUCK", TP_DUCK, 0, 127}, {"Y", TP_Y, 1, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"DENS", TP_DENS, 0, 127}, {"DIR", TP_DIR, 0, 4}, {"ROT", TP_ROT, 0, 63}, {"WARP", TP_WARP, 0, 127}, {"RATE", TP_RATE, 0, SQ_RATES - 1}, {"SHUF", TP_SHUF, 0, 127}, {"RULE", TP_CAR, 0, 127}, {"SEED", TP_CSD, 0, 127}, {"CAMD", TP_CAM, 0, 3}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"MCH", TP_MCH, 0, 16}, {"NOTE", TP_MNOT, 0, 127}, {"CHRD", TP_CHRD, 0, SQ_CHORDS - 1}, {"GATE", TP_GATE, 1, 127}, {"PROG", TP_PROG, 0, 128}, {"", SP_NONE, 0, 0},
     {"SYNC", SP_SYNC, 0, 2}, {"PCCH", SP_PCCH, 0, 16}, {"NIN", SP_NIN, 0, 1}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"LEN", SP_LEN, 1, 64}, {"PLEN", SP_MLEN, 1, 64}, {"SWNG", SP_SWNG, 50, 80}, {"TMPO", SP_TMPO, 60, 187}, {"DSRC", SP_DSRC, 0, 7}, {"DREL", SP_DREL, 0, 127}, {"FREE", TP_FREE, 0, 1}, {"CLK", SP_CLK, 0, 6}, {"CAMT", SP_CAMT, 0, 127}, {"CSED", SP_CSED, 0, 127}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
    {{"VEL", SP_VEL, 1, 127}, {"MICR", SP_MICR, 0, 46}, {"PROB", SP_PROB, 1, 100}, {"COND", SP_COND, 0, SQ_COND_N - 1}, {"RTCH", SP_RTCH, 0, 13}, {"CURV", SP_CURV, 0, 14}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}, {"", SP_NONE, 0, 0}},
};
/* a ratchet's hits: 1..8, then rolls so fast they turn into a pitch */
static const uint8_t sq_rtch[14] = {1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32, 48, 64};
static int sq_rtch_i(int r) { int i = 0; while (i < 13 && sq_rtch[i] < r) i++; return i; }
static uint8_t sq_page, sq_view, sq_rec, sq_spage, sq_pat_held, sq_cue_held, sq_fxlist, sq_fxrow, sq_turned, sq_lockpage = SQP_STEP, sq_restored;
static uint16_t sq_held;                 /* the trigs held in GRID */
static uint8_t sq_chain_n, sq_chain[8], sq_tempo_held, sq_scn_held;
static SqStep sq_clip[SQ_TRACKS][SQ_STEPS]; static uint8_t sq_clip_len[SQ_TRACKS], sq_clip_kind;   /* copied: 1 a track, 2 a pattern */
static int sq_sel(void) { return SQH->sel & 7; }
static int sq_pat(void) { return SQH->pattern & 3; }
static int sq_len(int t) { int l = SQH->len[sq_pat()][t & 7]; return l >= 1 && l <= SQ_STEPS ? l : 16; }
static void sq_tempo_set(int bpm) { Param *p = &P(PG_MASTER, 6); p->value = p->user = (int16_t)(clamp(bpm, 60, 187) - 60); }

/* ---- what a value reads as ---- */
static void sq_cat(char *n, const char *s) { int k = 0; while (n[k]) k++; while (*s && k < 11) n[k++] = *s++; n[k] = 0; }
static int sq_hz(int i) { uint32_t f = 20u << 8; for (int k = 0; k < i; k++) f += ((f >> 6) * 3663u) >> 10; return (int)(f >> 8); }
static void sq_cond_text(int c, char *n)
{
    static const char *const sp[8] = {"FILL", "!FILL", "1ST", "!1ST", "PRE", "!PRE", "NEI", "!NEI"};
    if (!c) { n[0] = '-'; n[1] = 0; return; }
    if (c <= SQ_COND_AB) { int b = 2, k = c - 1; while (k >= b) { k -= b; b++; } n[0] = (char)('1' + k); n[1] = ':'; n[2] = (char)('0' + b); n[3] = 0; return; }
    n[0] = 0; sq_cat(n, sp[clamp(c - SQC_FILL, 0, 7)]);
}
static void sq_text(int tp, int v, char *n)
{
    n[0] = 0;
    switch (tp) {
    case TP_SMPL: num(n, v / 16 + 1); sq_cat(n, "."); { char d[4]; num(d, v % 16 + 1); sq_cat(n, d); } break;
    case TP_PTCH: if (v > 64) { n[0] = '+'; num(n + 1, v - 64); } else num(n, v - 64); break;
    case TP_PAN: if (v == 64) sq_cat(n, "C"); else { n[0] = v < 64 ? 'L' : 'R'; num(n + 1, v < 64 ? 64 - v : v - 64); } break;
    case TP_HP: if (!v) sq_cat(n, "OFF"); else { int h = sq_hz(v); if (h >= 1000) { num(n, h / 1000); sq_cat(n, "K"); { char d[4]; num(d, h % 1000 / 100); sq_cat(n, d); } } else num(n, h); } break;
    case TP_LP: if (v >= 127) sq_cat(n, "OPEN"); else { int h = sq_hz(v); if (h >= 1000) { num(n, h / 1000); sq_cat(n, "K"); { char d[4]; num(d, h % 1000 / 100); sq_cat(n, d); } } else num(n, h); } break;
    case TP_REV: case TP_FREE: case SP_SREV: sq_cat(n, v ? "ON" : "OFF"); break;
    case TP_MACH: { static const char *const m[SQM_N] = {"SMPL", "SUB", "FM", "NOIS", "PLUK", "MIDI"}; sq_cat(n, m[clamp(v, 0, SQM_N - 1)]); } break;
    case TP_OUT: sq_cat(n, v == 1 ? "CUE" : v == 2 ? "BOTH" : v == 3 ? "QUAD" : "MAIN"); break;
    case TP_Y: if (!v) v = 64; if (v == 64) sq_cat(n, "MID"); else { n[0] = v < 64 ? 'F' : 'B'; num(n + 1, v < 64 ? 64 - v : v - 64); } break;
    case TP_LFOD: { static const char *const d[9] = {"OFF", "LP", "HP", "VOL", "PAN", "PTCH", "STRT", "INDX", "Y"}; sq_cat(n, d[clamp(v, 0, 8)]); } break;
    case TP_MODW: { static const char *const w[3] = {"TRI", "RND", "ENV"}; sq_cat(n, w[clamp(v, 0, 2)]); } break;
    case TP_DIR: { static const char *const d[5] = {"FWD", "REV", "PONG", "RND", "DRNK"}; sq_cat(n, d[clamp(v, 0, 4)]); } break;
    case TP_RATE: { static const char *const r[SQ_RATES] = {"1/4", "1/3", "1/2", "2/3", "3/4", "X1", "4/3", "3/2", "X2", "X3", "X4"}; sq_cat(n, r[clamp(v, 0, SQ_RATES - 1)]); } break;
    case TP_WARP: if (v == 64) sq_cat(n, "-"); else { n[0] = v > 64 ? '+' : '-'; num(n + 1, v > 64 ? v - 64 : 64 - v); } break;
    case TP_FMR: num(n, v / 16); sq_cat(n, "."); { char d[4]; num(d, (v % 16) * 10 / 16); sq_cat(n, d); } break;
    case TP_DENS: num(n, (v + 1) * 100 / 128); sq_cat(n, "%"); break;
    case SP_RTCH: num(n, sq_rtch[clamp(v, 0, 13)]); break;
    case SP_CURV: if (v == 7) sq_cat(n, "EVEN"); else { sq_cat(n, v > 7 ? "IN" : "OUT"); char d[4]; num(d, v > 7 ? v - 7 : 7 - v); sq_cat(n, d); } break;
    case TP_MCH: if (!v) sq_cat(n, "OFF"); else num(n, v); break;
    case TP_BITS: if (!v) sq_cat(n, "OFF"); else num(n, 16 - v); break;
    case TP_SHUF: if (!v) sq_cat(n, "PTN"); else { num(n, 50 + v * 30 / 127); sq_cat(n, "%"); } break;
    case TP_DEC: if (v >= 127) sq_cat(n, "FULL"); else num(n, v); break;
    case SP_SWNG: num(n, v); sq_cat(n, "%"); break;
    case SP_CLK: { static const char *const c[7] = {"OFF", "ACCEL", "DECEL", "ARCH", "BULGE", "WAVE", "RAND"}; sq_cat(n, c[clamp(v, 0, 6)]); } break;
    case TP_CAR: if (!v) sq_cat(n, "OFF"); else { n[0] = 'R'; num(n + 1, 2 * v); } break;
    case TP_CAM: { static const char *const c[4] = {"GATE", "AND", "XOR", "PTCH"}; sq_cat(n, c[v & 3]); } break;
    case SP_DSRC: n[0] = 'T'; num(n + 1, v + 1); break;
    case TP_CHRD: { static const char *const c[SQ_CHORDS] = {"NOTE", "OCT", "5TH", "MAJ", "MIN", "SUS2", "SUS4", "MAJ7", "MIN7", "DOM7", "DIM", "AUG", "MIN9", "4THS", "CLUS"}; sq_cat(n, c[clamp(v, 0, SQ_CHORDS - 1)]); } break;
    case TP_GATE: { int g = v ? v : 4; if (g % 8 == 0) num(n, g / 8); else { num(n, g); sq_cat(n, "/8"); } } break;
    case TP_PROG: case SP_PCCH: if (!v) sq_cat(n, "OFF"); else num(n, v); break;
    case SP_SYNC: sq_cat(n, v == 1 ? "SEND" : v == 2 ? "RECV" : "OFF"); break;
    case SP_NIN: sq_cat(n, v ? "ON" : "OFF"); break;
    case TP_MNOT: { static const char *const nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; sq_cat(n, nn[v % 12]); char d[4]; num(d, v / 12 - 1); sq_cat(n, d); } break;
    case SP_MICR: if (v > 23) { n[0] = '+'; num(n + 1, v - 23); } else num(n, v - 23); break;
    case SP_PROB: num(n, v); sq_cat(n, "%"); break;
    case SP_COND: sq_cond_text(v, n); break;
    default: num(n, v);
    }
}

/* ---- the effect of a track: which, on which core, its knobs ---- */
/* Measured on the unit (12 Nov 2026), eight pitched tracks sounding on every step with all sixteen filters on: core 0
 * is at 64-80 % before any effect, so the effects go to core 1 first (1,257 instructions a sample of them ran there
 * with none late) and core 0 keeps room for the two that can only run there (519 together: 95 % in the worst frame). */
#define SQ_ROOM0 1060   /* 560 until 13 Nov 2026: the engine in core 0's private P took the torture test's worst frame from 89 % to 50 % */
#define SQ_ROOM1 1300
static int sq_fx_place(int t, int row)   /* 0, 1: the core; -1: no room; -2: that effect is on another track */
{
    if (row <= 0) return 0;
    const SqHead *h = SQH;
    int use0 = 0, use1 = 0;
    for (int k = 0; k < SQ_TRACKS; k++) if (k != t && h->fx_row[k] && h->fx_row[k] <= FX_COUNT) {
        if (h->fx_row[k] == row && fx_info[row - 1].has_y) return -2;
        if (fx_clash(row, h->fx_row[k])) return -2;                    /* WIND and FOG, HAIL and AURORA: one memory between them */
        /* the effects in the shared window (PITCH, SMEAR) and the ones written for one place keep their state where the code says */
        if (h->fx_core[k] == 1) use1 += fx_cost[h->fx_row[k] - 1]; else use0 += fx_cost[h->fx_row[k] - 1];
    }
    int c = fx_cost[row - 1], pref = fx_core[row - 1];
    if (pref != 0 && use1 + c <= SQ_ROOM1) return 1;
    if (pref != 1 && use0 + c <= SQ_ROOM0) return 0;
    return -1;
}
/* the effects offered: every row but the ones the owner does not want (fx_banned: no distortion) */
static int sq_fx_ok(int r) { return r == 0 || (r >= 1 && r <= FX_COUNT && !fx_banned(fx_info[r - 1].name)); }
static int sq_fx_move(int r, int d) { int q = r; do { q += d; } while (q > 0 && q <= FX_COUNT && !sq_fx_ok(q)); return q < 0 || q > FX_COUNT ? r : q; }
static void sq_fx_pick(int t, int row)
{
    if (!sq_fx_ok(row)) return;
    SqHead *h = SQH;
    int c = sq_fx_place(t, row);
    if (c == -2) { say("ON ANOTHER", "TRACK"); return; }
    if (c < 0) { say("NO ROOM", "DSP FULL"); return; }
    h->fx_row[t] = 0;
    if (row > 0) {
        const FxInfo *fi = &fx_info[row - 1];
        for (int k = 0; k < 6; k++) { h->fx_knob[t][k] = fi->def[k]; h->fx_knob[t][6 + k] = fi->def2[k]; }
        h->fx_core[t] = (uint8_t)c; h->fx_row[t] = (uint8_t)row;
        say(fi->name + 6, 0);
    } else say("NO EFFECT", 0);
}
static void sq_fx_text(int t, int k, int v, char *n)
{
    int row = SQH->fx_row[t]; const char *s = 0;
    if (row > 0 && row <= FX_COUNT) {
        const FxInfo *fi = &fx_info[row - 1];
        if (k < 6) { if (fi->text[k] && v < fi->count[k]) s = fi->text[k][v]; }
        else if (fi->text2[k - 6] && v < fi->count2[k - 6]) s = fi->text2[k - 6][v];
    }
    if (!s) { num(n, v); return; }
    int i = 0; while (s[i] && i < 10) { n[i] = s[i]; i++; } n[i] = 0;
}

/* ---- reading and writing a knob ---- */
static int sq_lockable(int pg) { return pg == SQP_SRC || pg == SQP_AMP || pg == SQP_FLT || pg == SQP_MIDI; }   /* MOD and GEN are the whole track's */
static int sq_shown_page(void) { return sq_held ? sq_lockpage : sq_page; }
static int sq_first_held(void) { for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) return sq_spage * 16 + k; return -1; }
static int sq_lock_of(const SqStep *s, int tp) { for (int k = 0; k < SQ_LOCKS; k++) if (s->lp[k] == tp + 1) return s->lv[k]; return -1; }
static int sq_get(int pg, int i)
{
    const SqHead *h = SQH; int t = sq_sel();
    const SqP *p = &sq_par[pg][i];
    int st = sq_first_held();
    const SqStep *s = st >= 0 ? SQ_STEP(sq_pat(), t, st) : 0;
    if (pg == SQP_FX) return h->fx_knob[t][i];
    switch (p->tp) {
    case SP_NONE: return 0;
    case SP_LEN: return sq_len(t);
    case SP_MLEN: return h->mlen[sq_pat()] ? h->mlen[sq_pat()] : 16;
    case SP_SWNG: return clamp(h->swing, 50, 80);
    case SP_CLK: return h->curv > 6 ? 0 : h->curv;
    case SP_CAMT: return h->camt & 127;
    case SP_CSED: return h->cseed & 127;
    case SP_TMPO: return master_bpm();
    case SP_DSRC: return h->duck_src & 7;
    case SP_DREL: return h->duck_rel;
    case SP_SYNC: return h->sync > 2 ? 0 : h->sync;
    case SP_PCCH: return h->pcch > 16 ? 0 : h->pcch;
    case SP_NIN: return !h->nin;
    case SP_VEL: return s ? (s->vel ? s->vel : 100) : 100;
    case SP_MICR: return s ? s->micro + 23 : 23;
    case SP_PROB: return s && s->prob ? s->prob : 100;
    case SP_COND: return s ? s->cond : 0;
    case SP_RTCH: return s ? sq_rtch_i(s->ratch) : 0;
    case SP_CURV: return s ? SQ_CURVE(s->flags) + 7 : 7;
    case SP_SREV: return s ? (s->flags & SF_REV) != 0 : 0;
    }
    if (s) { int l = sq_lock_of(s, p->tp); if (l >= 0) return l; }
    return *sq_tp(t, p->tp);
}
static void sq_say(int pg, int i, int v)
{
    static char n[14]; const SqP *p = &sq_par[pg][i];
    if (pg == SQP_FX) { int row = SQH->fx_row[sq_sel()]; if (row <= 0 || row > FX_COUNT) return; sq_fx_text(sq_sel(), i, v, n); say(i < 6 ? fx_info[row - 1].knob[i] : fx_info[row - 1].knob2[i - 6], n); return; }
    sq_text(p->tp, v, n); say(p->lab, n);
}
/* a step's own value for a track parameter: one of its three locks */
static int sq_lock_set(SqStep *s, int tp, int v)
{
    int free_k = -1;
    for (int k = 0; k < SQ_LOCKS; k++) { if (s->lp[k] == tp + 1) { s->lv[k] = (uint8_t)v; return 1; } if (!s->lp[k] && free_k < 0) free_k = k; }
    if (free_k < 0) return 0;
    s->lp[free_k] = (uint8_t)(tp + 1); s->lv[free_k] = (uint8_t)v;
    return 1;
}
static void sq_turn(int pg, int i, int delta)
{
    SqHead *h = SQH; int t = sq_sel();
    if (pg == SQP_FX) {
        int row = h->fx_row[t]; if (row <= 0 || row > FX_COUNT) { say("NO EFFECT", "YES PICKS"); return; }
        const FxInfo *fi = &fx_info[row - 1];
        if (i >= 6 && !fi->knob2[i - 6][0]) return;
        int c = i < 6 ? fi->count[i] : fi->count2[i - 6]; if (c < 2) c = 128;
        int v = clamp(h->fx_knob[t][i] + (c <= 16 ? (delta > 0 ? 1 : -1) : delta), 0, c - 1);
        h->fx_knob[t][i] = (uint8_t)v; sq_say(pg, i, v);
        return;
    }
    const SqP *p = &sq_par[pg][i];
    if (p->tp == SP_NONE) return;
    int small = p->hi - p->lo <= 16;
    int v = clamp(sq_get(pg, i) + (small ? (delta > 0 ? 1 : -1) : delta), p->lo, p->hi);
    if ((p->tp <= SP_VEL && p->tp > -100) || (sq_held && p->tp >= 0 && sq_lockable(pg))) {
        /* the held steps: their own attributes, or a lock */
        if (!sq_held) return;
        sq_turned = 1;
        for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) {
            SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k);
            s->flags |= SF_ON;
            switch (p->tp) {
            case SP_VEL: s->vel = (uint8_t)v; break;
            case SP_MICR: s->micro = (int8_t)(v - 23); break;
            case SP_PROB: s->prob = (uint8_t)(v >= 100 ? 0 : v); break;
            case SP_COND: s->cond = (uint8_t)v; break;
            case SP_RTCH: s->ratch = (uint8_t)(v <= 0 ? 0 : sq_rtch[v]); break;
            case SP_CURV: s->flags = (uint8_t)((s->flags & 0x0F) | (((v - 7) & 15) << 4)); break;
            case SP_SREV: if (v) s->flags |= SF_REV; else s->flags &= (uint8_t)~SF_REV; break;
            default: if (!sq_lock_set(s, p->tp, v)) { say("3 LOCKS", "A STEP"); return; }
            }
        }
        sq_say(pg, i, v);
        return;
    }
    switch (p->tp) {
    case SP_LEN: h->len[sq_pat()][t] = (uint8_t)v; if (sq_spage * 16 >= v) sq_spage = (uint8_t)((v - 1) / 16); break;
    case SP_MLEN: h->mlen[sq_pat()] = (uint8_t)v; break;
    case SP_SWNG: h->swing = (uint8_t)v; break;
    case SP_CLK: h->curv = (uint8_t)v; break;
    case SP_CAMT: h->camt = (uint8_t)v; break;
    case SP_CSED: h->cseed = (uint8_t)v; break;
    case SP_TMPO: sq_tempo_set(v); break;
    case SP_DSRC: h->duck_src = (uint8_t)v; break;
    case SP_DREL: h->duck_rel = (uint8_t)v; break;
    case SP_SYNC: h->sync = (uint8_t)v; break;
    case SP_PCCH: h->pcch = (uint8_t)v; break;
    case SP_NIN: h->nin = (uint8_t)!v; break;
    default:
        *sq_tp(t, p->tp) = (uint8_t)v;
        /* LIVE and running: the knob's move is written into the step going by */
        if (sq_rec && aos_seq_playing()) { SqStep *s = SQ_STEP(sq_pat(), t, aos_seq_step(t)); if (s->flags & SF_ON) sq_lock_set(s, p->tp, v); }
    }
    sq_say(pg, i, v);
}

/* ---- in and out of the sequencer ---- */
static void browser_open(int mode);
static void seq_enter(int on)
{
    seq_on = (uint8_t)(on != 0);
    aos_seq_mode(seq_on);
    sq_view = SV_GRID; sq_held = 0; sq_fxlist = 0; sq_pat_held = 0; func_latch = 0;
    for (int id = 32; id < 126; id++) set_led(id, 0);
    say(seq_on ? "SEQUENCER" : "AERIA", 0);
}
static void seq_ui_tick(void)
{
    if (!sq_restored && ticks > 45) {                              /* as it was left: battery RAM remembers */
        sq_restored = 1;
        const SqHead *h = SQH;
        if (h->magic[0] == 'A' && h->magic[1] == 'E' && h->magic[2] == 'S' && h->magic[3] == 'Q' && h->mode == 1) { seq_on = 1; aos_seq_mode(1); }
    }
    aos_seq_tempo(master_bpm() * 10);
    aos_seq_cushion(br_mode || preset_mode);
    aos_seq_live(sq_rec);
    if (seq_on && !aos_seq_is()) aos_seq_mode(1);
}

/* ---- keys ---- */
static void sq_step_tap(int n)
{
    SqStep *s = SQ_STEP(sq_pat(), sq_sel(), n);
    if (s->flags & SF_ON) { for (unsigned k = 0; k < sizeof(SqStep); k++) ((uint8_t *)s)[k] = 0; }
    else { s->flags = SF_ON; s->vel = 100; }
}
static int seq_key(int code, int pressed)
{
    if (code == KEY_MIDI && func_held) { if (pressed) seq_enter(!seq_on); return 1; }
    if (!seq_on) return 0;
    if (code == KEY_MIDI && !pressed) return 1;
    if (preset_mode || code == KEY_PROJ || br_mode || code == KEY_FUNC) return 0;
    SqHead *h = SQH; int t = sq_sel();
    if (sq_view == SV_MASTER) {
        if (pressed && (code == 0x30 || code == KEY_SRC || code == KEY_AMP || code == KEY_LFO || code == KEY_FX1 || code == KEY_FX2 || code == KEY_PAGE || code == KEY_BANK || code == KEY_ARR || code == KEY_MIDI)) { sq_view = SV_GRID; say("SEQUENCER", 0); return 1; }
        else if (code == KEY_PLAY || code == KEY_STOP) { /* the transport still runs */ }
        else return 0;
    }
    if (code == KEY_YES && !pressed) { aos_seq_fill(0); return 1; }
    if (code == KEY_PATTERN) { sq_pat_held = (uint8_t)pressed; if (pressed) { sq_chain_n = 0; say("PATTERN", "TRIG 1-4"); } else if (sq_chain_n > 1) { h->chain_n = sq_chain_n; for (int k = 0; k < 8; k++) h->chain[k] = sq_chain[k]; say("CHAIN", "SET"); } return 1; }
    if (code == KEY_CUE) { sq_cue_held = (uint8_t)pressed; return 1; }
    /* the scene keys here: mute scenes. Hold one and a trig calls that scene's mutes; with FUNC it keeps the mutes as they are now */
    if (sq_scn_held && pressed && code == KEY_NO) {                 /* the scene forgets the track knobs it holds */
        int sc = scene_held == 1 ? scene_a : scene_b;
        for (int t2 = 0; t2 < SQ_TRACKS; t2++) for (int tp = 0; tp < TP_ALL; tp++) *sqsc(sc, t2, tp) = -1;
        static char n[4]; num(n, sc + 1); say("SCENE EMPTY", n); return 1;
    }
    if (code == KEY_SCENEA || code == KEY_SCENEB) { if (sq_view == SV_MASTER) return 0; sq_scn_held = (uint8_t)pressed; scene_held = pressed ? (code == KEY_SCENEA ? 1 : 2) : 0; if (pressed) say("MUTE SCENES", "TRIG 1-16"); return 1; }
    if (code == 0x18) { sq_tempo_held = (uint8_t)pressed; if (pressed) master_tap(); return 1; }   /* TEMPO: tap it; hold it and turn LEVEL */
    /* the list of effects */
    if (sq_fxlist) {
        if (!pressed) return 1;
        if (code == KEY_UP) sq_fxrow = (uint8_t)sq_fx_move(sq_fxrow, -1);
        else if (code == KEY_DOWN) sq_fxrow = (uint8_t)sq_fx_move(sq_fxrow, 1);
        else if (code == KEY_YES) { sq_fx_pick(t, sq_fxrow); sq_fxlist = 0; }
        else if (code == KEY_NO) sq_fxlist = 0;
        return 1;
    }
    if (code < KEY_TRIG0 + 16) {
        int k = code - KEY_TRIG0;
        if (sq_scn_held) {
            if (!pressed) return 1;
            static char sn[4]; num(sn, k + 1);
            if (func_held) { h->mscn[k] = h->mutes; h->mscn_ok[k >> 3] |= (uint8_t)(1u << (k & 7)); say("MUTES KEPT IN", sn); }
            else if ((h->mscn_ok[k >> 3] >> (k & 7)) & 1) { h->mutes = h->mscn[k]; say("MUTE SCENE", sn); }
            else say("EMPTY", "FUNC KEEPS");
            return 1;
        }
        if (sq_pat_held) {
            if (pressed && k < SQ_PATTERNS) { h->pattern = (uint8_t)k; h->chain_n = 0; if (sq_chain_n < 8) sq_chain[sq_chain_n++] = (uint8_t)k; static char n[2]; n[0] = (char)('A' + k); n[1] = 0; say("PATTERN", n); }
            return 1;
        }
        if (sq_view == SV_MIXER) {
            if (pressed) { if (k < 8) h->mutes ^= (uint8_t)(1u << k); else h->solo ^= (uint8_t)(1u << (k - 8)); }
            return 1;
        }
        if (sq_rec) {
            if (pressed) { if (k < 8) aos_seq_hit(k, 100, 1); else h->mutes ^= (uint8_t)(1u << (k - 8)); }
            return 1;
        }
        int n = sq_spage * 16 + k;
        if (n >= sq_len(t)) return 1;
        if (pressed) { if (!sq_held) { sq_turned = 0; sq_lockpage = SQP_STEP; } sq_held |= (uint16_t)(1u << k); }
        else {
            if ((sq_held >> k) & 1) { sq_held &= (uint16_t)~(1u << k); if (!sq_turned) sq_step_tap(n); }
        }
        return 1;
    }
    if (code >= KEY_TRACK0 && code < KEY_TRACK0 + 8) {
        int k = code - KEY_TRACK0;
        if (!pressed) return 1;
        static char n[4]; n[0] = 'T'; n[1] = (char)('1' + k); n[2] = 0;
        if (func_held) { h->mutes ^= (uint8_t)(1u << k); say(n, (h->mutes >> k) & 1 ? "MUTED" : "ON"); }
        else if (sq_cue_held) { h->solo ^= (uint8_t)(1u << k); say(n, (h->solo >> k) & 1 ? "SOLO" : "SOLO OFF"); }
        else { h->sel = (uint8_t)k; sq_held = 0; if (sq_spage * 16 >= sq_len(k)) sq_spage = 0; if (!aos_seq_playing()) aos_seq_hit(k, 100, 0); }   /* stopped: the track is heard as it is picked */
        return 1;
    }
    if (!pressed) return 1;
    switch (code) {
    case KEY_PLAY:
        if (func_held) { for (int n = 0; n < SQ_STEPS; n++) { SqStep *s = SQ_STEP(sq_pat(), t, n); for (unsigned k = 0; k < sizeof(SqStep); k++) ((uint8_t *)s)[k] = 0; } say("TRACK", "CLEARED"); }
        else { aos_seq_play(1); say("PLAY", 0); }
        return 1;
    case KEY_STOP:
        if (func_held) {                                           /* paste what was copied */
            int p = sq_pat();
            if (sq_clip_kind == 1) { for (int n = 0; n < SQ_STEPS; n++) *SQ_STEP(p, t, n) = sq_clip[0][n]; h->len[p][t] = sq_clip_len[0]; say("TRACK", "PASTED"); }
            else if (sq_clip_kind == 2) { for (int k = 0; k < SQ_TRACKS; k++) { for (int n = 0; n < SQ_STEPS; n++) *SQ_STEP(p, k, n) = sq_clip[k][n]; h->len[p][k] = sq_clip_len[k]; aos_breathe(); } say("PATTERN", "PASTED"); }
            else say("NOTHING", "COPIED");
            return 1;
        }
        if (aos_seq_playing()) { aos_seq_play(0); say("STOP", 0); } else { aos_seq_mode(0); aos_seq_mode(1); say("SILENCE", 0); } return 1;
    case KEY_REC:
        if (func_held) {                                           /* copy: the track, or with PATTERN held the whole pattern */
            int p = sq_pat();
            if (sq_pat_held) { for (int k = 0; k < SQ_TRACKS; k++) { for (int n = 0; n < SQ_STEPS; n++) sq_clip[k][n] = *SQ_STEP(p, k, n); sq_clip_len[k] = (uint8_t)sq_len(k); aos_breathe(); } sq_clip_kind = 2; say("PATTERN", "COPIED"); }
            else { for (int n = 0; n < SQ_STEPS; n++) sq_clip[0][n] = *SQ_STEP(p, t, n); sq_clip_len[0] = (uint8_t)sq_len(t); sq_clip_kind = 1; say("TRACK", "COPIED"); }
            return 1;
        }
        sq_rec = !sq_rec; sq_held = 0; say(sq_rec ? "LIVE" : "GRID", sq_rec ? "PADS 1-8" : 0); return 1;
    case 0x18: master_tap(); return 1;
    case KEY_SRC: case KEY_AMP: case KEY_LFO: case KEY_FX1: case KEY_FX2: case KEY_PAGE: case KEY_ARR: case KEY_MIDI: {
        int pg = code == KEY_SRC ? SQP_SRC : code == KEY_AMP ? SQP_AMP : code == KEY_LFO ? SQP_MOD : code == KEY_FX1 ? SQP_FLT : code == KEY_FX2 ? SQP_FX : code == KEY_ARR ? SQP_GEN : code == KEY_MIDI ? SQP_MIDI : SQP_PTN;
        if (sq_held && sq_lockable(pg)) { sq_lockpage = (uint8_t)pg; sq_turned = 1; say("LOCK", sq_page_name[pg]); }
        else if (sq_page == pg && sq_view == SV_GRID) { func_latch = !func_latch; say(sq_page_name[pg], func_latch ? "2" : "1"); }   /* the same key again: its other six knobs */
        else { sq_page = (uint8_t)pg; sq_view = SV_GRID; func_latch = 0; say(sq_page_name[pg], 0); }
        return 1; }
    case KEY_BANK:
        if (func_held) {                                           /* the main output into sample 6.1, one pattern long from its next start */
            if (aos_resampling()) { aos_resample(80, 0); say("RESAMPLE", "OFF"); }
            else { aos_resample(80, h->mlen[sq_pat()] ? h->mlen[sq_pat()] : 16); say("RESAMPLE 6.1", "AT THE BAR"); }
            return 1;
        }
        sq_view = sq_view == SV_MIXER ? SV_GRID : SV_MIXER; say(sq_view == SV_MIXER ? "MIXER" : "GRID", 0); return 1;
    case 0x30: sq_view = SV_MASTER; page = PG_MASTER; say("MASTER", 0); return 1;
    case KEY_LEFT: case KEY_RIGHT:
        if (sq_held) {                                             /* a held step: earlier, later */
            sq_turned = 1;
            for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); s->flags |= SF_ON; s->micro = (int8_t)clamp(s->micro + (code == KEY_RIGHT ? 1 : -1), -23, 23); sq_say(SQP_STEP, 1, s->micro + 23); }
        } else { int pages = (sq_len(t) + 15) / 16; sq_spage = (uint8_t)((sq_spage + (code == KEY_RIGHT ? 1 : pages - 1)) % pages); }
        return 1;
    case KEY_UP: case KEY_DOWN: h->sel = (uint8_t)((t + (code == KEY_DOWN ? 1 : 7)) & 7); sq_held = 0; if (sq_spage * 16 >= sq_len(h->sel)) sq_spage = 0; return 1;
    case KEY_YES:
        if (sq_held) {                                             /* a held step: backward, or forward again */
            sq_turned = 1;
            for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); s->flags |= SF_ON; s->flags ^= SF_REV; say("STEP", s->flags & SF_REV ? "BACKWARD" : "FORWARD"); }
            return 1;
        }
        if (func_held && sq_page == SQP_SRC) { browser_open(BR_SEQ); return 1; }
        if (sq_page == SQP_FX) { sq_fxlist = 1; sq_fxrow = h->fx_row[t] <= FX_COUNT && sq_fx_ok(h->fx_row[t]) ? h->fx_row[t] : 0; return 1; }
        aos_seq_fill(1); say("FILL", 0);
        return 1;
    case KEY_NO:
        if (sq_held) { for (int k = 0; k < 16; k++) if ((sq_held >> k) & 1) { SqStep *s = SQ_STEP(sq_pat(), t, sq_spage * 16 + k); for (int q = 0; q < SQ_LOCKS; q++) s->lp[q] = 0; } sq_turned = 1; say("LOCKS", "CLEARED"); }
        return 1;
    }
    return 1;
}
static const char *sq_tp_label(int tp) { for (int p = 0; p < SQP_N; p++) for (int i = 0; i < 12; i++) if (sq_par[p][i].tp == tp && sq_par[p][i].lab[0]) return sq_par[p][i].lab; return "?"; }
static int seq_enc(int e, int delta)
{
    if (!seq_on || preset_mode || br_mode) return 0;
    if (sq_view == SV_MASTER) return e == 6;                       /* the master page's own knobs; LEVEL does nothing there */
    if (scene_held && e >= 0 && e < 6) {                            /* a track knob into the scene that end holds */
        int pg = sq_shown_page(), i = e + (layer() && pg != SQP_STEP ? 6 : 0), t = sq_sel();
        if (pg >= SQP_N || sq_par[pg][i].tp < 0 || sq_par[pg][i].tp >= TP_ALL) return 1;
        const SqP *p = &sq_par[pg][i]; int sc = scene_held == 1 ? scene_a : scene_b;
        volatile int8_t *v = sqsc(sc, t, p->tp);
        int cur = *v >= 0 ? *v : *sq_tp(t, p->tp), nv = clamp(cur + (p->hi - p->lo <= 16 ? (delta > 0 ? 1 : -1) : delta), p->lo, p->hi);
        *v = (int8_t)nv;
        static char n[20]; sq_text(p->tp, nv, n); say(scene_held == 1 ? "SCENE A" : "SCENE B", n);
        return 1;
    }
    if (scene_held) return 1;
    if (e == 6 && sq_tempo_held) { sq_tempo_set(master_bpm() + (delta > 0 ? 1 : -1)); static char b[6]; num(b, master_bpm()); say("TEMPO", b); return 1; }
    if (sq_fxlist) { if (e == 0 || e == 6) sq_fxrow = (uint8_t)sq_fx_move(sq_fxrow, delta > 0 ? 1 : -1); return 1; }
    if (e == 6) {                                                  /* LEVEL: the track's volume */
        SqHead *h = SQH; int t = sq_sel(), v = clamp(h->trk[t][TP_VOL] + delta, 0, 127);
        h->trk[t][TP_VOL] = (uint8_t)v; static char n[6]; num(n, v); say("VOLUME", n);
        return 1;
    }
    if (e < 0 || e > 5) return 1;
    if (mod_learn >= 0) {                                          /* MOD learning: this knob is the route's destination */
        int pg = sq_shown_page(), i = e + (layer() && pg != SQP_STEP ? 6 : 0), t = sq_sel();
        if (pg == SQP_FX && !layer()) { mod_learn_take((uint16_t)(MK_TFX << 12 | (t * 6 + e))); return 1; }
        if (pg < SQP_N && sq_par[pg][i].tp >= 0 && sq_par[pg][i].tp < TP_ALL) { mod_learn_take((uint16_t)(MK_TRK << 12 | (t * TP_ALL + sq_par[pg][i].tp))); return 1; }
    }
    { int pg = sq_shown_page(); sq_turn(pg, e + (layer() && pg != SQP_STEP ? 6 : 0), delta); }
    return 1;
}

/* ---- the screen ---- */
static void sq_box(int x0, int y0, int x1, int y1) { for (int x = x0; x <= x1; x++) span(x, y0, y1, 1); }
static void seq_draw(void)
{
    const SqHead *h = SQH;
    int t = sq_sel(), pat = sq_pat(), playing = aos_seq_playing();
    char n[16];
    /* the top line: track, pattern (and the one coming), page of steps, tempo, the eight meters */
    n[0] = 'T'; n[1] = (char)('1' + t); n[2] = 0; text(1, 1, n, 1);
    n[0] = (char)('A' + (playing ? aos_seq_pat() : pat)); n[1] = 0; if (aos_seq_pat() != pat && playing) { n[1] = '>'; n[2] = (char)('A' + pat); n[3] = 0; } text(12, 1, n, 1);
    if (h->chain_n) text(26, 1, "CH", 1);
    { int pages = (sq_len(t) + 15) / 16; n[0] = (char)('1' + sq_spage); n[1] = '/'; n[2] = (char)('0' + pages); n[3] = 0; text(37, 1, n, 1); }
    num(n, aos_seq_ext() ? (aos_seq_bpm10() + 5) / 10 : master_bpm()); text(52, 1, n, 1);
    if (aos_seq_ext()) { px(50, 1); px(50, 3); px(50, 5); }           /* a clock from outside has the tempo */
    text(67, 1, aos_resampling() == 2 ? "RSMP" : aos_resampling() ? "ARM" : sq_rec ? "LIVE" : playing ? "PLAY" : "", 1);
    if (layer() && !sq_held) text(86, 1, "2", 1);
    if (playing && (aos_seq_bar() & 3) == 0) sq_box(90, 1, 92, 5);
    for (int k = 0; k < 8; k++) {
        int x = 94 + k * 4, lv = aos_seq_peak(k) * 7 / 16, muted = ((h->mutes >> k) & 1) || (h->solo && !((h->solo >> k) & 1));
        if (muted) px(x + 1, 3); else { px(x, 7); px(x + 1, 7); px(x + 2, 7); if (lv) sq_box(x, 7 - lv, x + 2, 7); }
        if (k == t) { px(x, 0); px(x + 1, 0); px(x + 2, 0); }
    }
    if (sq_view == SV_MIXER) {
        /* eight strips: the meter, the volume's mark, M or S */
        for (int k = 0; k < 8; k++) {
            int x = 2 + k * 16, lv = aos_seq_peak(k) * 30 / 16, vol = h->trk[k][TP_VOL] * 30 / 127;
            for (int y = 12; y <= 42; y++) { px(x, y); px(x + 9, y); }
            if (lv) sq_box(x + 2, 42 - lv, x + 7, 42);
            for (int xx = x - 1; xx <= x + 10; xx++) px(xx, 42 - vol);
            n[0] = (char)('1' + k); n[1] = 0; text(x + 3, 45, n, 1);
            if ((h->mutes >> k) & 1) text(x + 3, 52, "M", 1);
            if ((h->solo >> k) & 1) text(x + 3, 58, "S", 1);
            if (k == t) for (int xx = x; xx <= x + 9; xx++) px(xx, 10);
        }
        return;
    }
    /* the grid: eight rows of sixteen, the track in hand the tall one */
    int y = 10;
    for (int r = 0; r < 8; r++) {
        if (r & 1) aos_breathe();                                  /* a long drawing: the stream is fed meanwhile */
        int tall = r == t, hh = tall ? 6 : 2, len = sq_len(r), now = playing ? aos_seq_step(r) : -1;
        for (int c = 0; c < 16; c++) {
            int nstep = sq_spage * 16 + c, x = c * 8;
            if (nstep >= len) continue;
            const SqStep *s = SQ_STEP(pat, r, nstep);
            int on = s->flags & SF_ON;
            if (on) {
                int dx = tall ? (s->micro > 3 ? 1 : s->micro < -3 ? -1 : 0) : 0;
                sq_box(x + dx, y, x + 6 + dx, y + hh - 1);
                if (tall) {
                    if (s->lp[0] || s->lp[1] || s->lp[2]) { span(x + 3 + dx, y + 2, y + 3, 0); }                      /* locks: a hole */
                    if ((s->prob && s->prob < 100) || s->cond) { span(x + 6 + dx, y, y + 1, 0); span(x + 5 + dx, y, y, 0); }   /* a chance or a condition: a cut corner */
                    if (s->ratch > 1) { span(x + 2 + dx, y + hh - 1, y + hh - 1, 0); span(x + 4 + dx, y + hh - 1, y + hh - 1, 0); }   /* a ratchet: teeth */
                    if (s->flags & SF_REV) span(x + dx, y, y, 0);
                }
            } else px(x + 3, y + hh - 1);
            if (tall && !(c & 3) && !on) px(x + 3, y + hh - 2);
            if (nstep == now) { for (int xx = x; xx <= x + 6; xx++) span(xx, y, y + hh - 1, 3); }
            if (tall && ((sq_held >> c) & 1) && (ticks & 8)) for (int xx = x; xx <= x + 6; xx++) span(xx, y, y + hh - 1, 3);
        }
        y += hh + 1;
    }
    /* the list of effects */
    if (sq_fxlist) {
        for (int x = 20; x < 108; x++) span(x, 9, 56, 0);
        for (int x = 20; x < 108; x++) { px(x, 9); px(x, 56); }
        for (int yy = 9; yy <= 56; yy++) { px(20, yy); px(107, yy); }
        int vis[FX_COUNT + 1], nv = 0, at = 0;
        for (int r = 0; r <= FX_COUNT; r++) if (sq_fx_ok(r)) { if (r == sq_fxrow) at = nv; vis[nv++] = r; }
        int top = clamp(at - 3, 0, nv - 7 < 0 ? 0 : nv - 7);
        for (int i = 0; i < 7 && top + i < nv; i++) {
            int r = vis[top + i], yy = 12 + i * 6;
            const char *nm = r ? fx_info[r - 1].name + 6 : "NONE";
            upper(nm, n, 12);
            if (r == sq_fxrow) { for (int x = 22; x < 106; x++) span(x, yy - 1, yy + 5, 1); text(26, yy, n, 0); } else text(26, yy, n, 1);
            if (r && sq_fx_place(t, r) < 0 && r != sq_fxrow) text(98, yy, "-", 1);
        }
        return;
    }
    /* the line between: the page, the track's sound, a held step */
    int pg = sq_shown_page(), lay = layer() && pg != SQP_STEP ? 6 : 0;
    text(1, 40, sq_page_name[pg], 1);
    if (sq_held) { int st = sq_first_held(); text(22, 40, "STEP", 1); num(n, st + 1); text(40, 40, n, 1);
        const SqStep *s = SQ_STEP(pat, t, st); int x = 52;
        for (int k = 0; k < SQ_LOCKS; k++) if (s->lp[k] && s->lp[k] <= TP_ALL) { for (int a = 0; a < SQP_FX; a++) for (int b = 0; b < 12; b++) if (sq_par[a][b].tp == s->lp[k] - 1) { text(x, 40, sq_par[a][b].lab, 1); x += text_w(sq_par[a][b].lab) + 4; a = SQP_FX; break; } } }
    else if (pg == SQP_FX) { int row = h->fx_row[t]; upper(row > 0 && row <= FX_COUNT ? fx_info[row - 1].name + 6 : "NONE  YES PICKS", n, 15); text(22, 40, n, 1); }
    else { if (h->trk[t][TP_MACH]) { sq_text(TP_MACH, h->trk[t][TP_MACH], n); text(22, 40, n, 1); } else { text(22, 40, "SMPL", 1); sq_text(TP_SMPL, h->trk[t][TP_SMPL], n); text(42, 40, n, 1); if (!aos_slot_ready(h->trk[t][TP_SMPL])) text(62, 40, "EMPTY", 1); }
           num(n, sq_len(t)); text(W - 1 - text_w(n) - 16, 40, "LEN", 1); text(W - 1 - text_w(n), 40, n, 1); }
    for (int x = 0; x < W; x += 2) px(x, 47);
    aos_breathe();
    /* the six knobs: name and value */
    for (int i = 0; i < 6; i++) {
        const char *lab; int v = sq_get(pg, i + lay);
        if (pg == SQP_FX) { int row = h->fx_row[t]; if (row <= 0 || row > FX_COUNT) break; lab = lay ? fx_info[row - 1].knob2[i] : fx_info[row - 1].knob[i]; if (!lab || !lab[0]) continue; sq_fx_text(t, i + lay, v, n); }
        else { const SqP *p = &sq_par[pg][i + lay]; if (p->tp == SP_NONE) continue; lab = p->lab; sq_text(p->tp, v, n); }
        char l4[5]; int k = 0; while (lab[k] && k < 4) { l4[k] = lab[k] >= 'a' && lab[k] <= 'z' ? (char)(lab[k] - 32) : lab[k]; k++; } l4[k] = 0;
        n[5] = 0;
        text(1 + i * 21, 50, l4, 1); text(1 + i * 21, 57, n, 1);
        /* a value that is the step's own: underlined */
        if (sq_held && sq_lockable(pg)) { const SqStep *s = SQ_STEP(pat, t, sq_first_held()); if (sq_lock_of(s, sq_par[pg][i + lay].tp) >= 0) for (int x = 1 + i * 21; x < 1 + i * 21 + 15; x++) px(x, 63); }
    }
    if (ticks < toast_until) {
        int x = (W - text_w(toast)) / 2;
        for (int i = x - 3; i < x + text_w(toast) + 3; i++) span(i, 38, 46, 1);
        text(x, 40, toast, 0);
    }
}
/* the lamps, at once and in steps (no fades here): a trig is dark, a beat's mark (1), a note (2) or the playhead (3) */
static void seq_leds(void)
{
    const SqHead *h = SQH; int t = sq_sel(), playing = aos_seq_playing();
    static const uint8_t page_lamp[SQP_N] = {66, 68, 86, 80, 64, 70, 82, 0, 0};
    /* the four lights above PAGE: the track's pages of sixteen steps -- the one on the trigs bright, the one playing lit, the others it has faint */
    { int pages = (sq_len(t) + 15) / 16, pp = playing ? aos_seq_step(t) / 16 : -1;
      for (int k = 0; k < 4; k++) set_led(72 + 2 * k, k >= pages ? 0 : k == sq_spage ? 3 : k == pp ? 2 : 1); }
    if (sq_scn_held) { for (int k = 0; k < 16; k++) led_want[k] = (h->mscn_ok[k >> 3] >> (k & 7)) & 1 ? (h->mscn[k] == h->mutes ? 3 : 2) : 0; }
    else if (sq_pat_held) { for (int k = 0; k < SQ_PATTERNS; k++) led_want[k] = k == sq_pat() ? 3 : 1; }
    else if (sq_view == SV_MIXER || sq_rec) {
        int mixer = sq_view == SV_MIXER;
        for (int k = 0; k < 8; k++) {
            int muted = (h->mutes >> k) & 1;
            if (mixer) { led_want[k] = muted ? 0 : 2; led_want[8 + k] = (h->solo >> k) & 1 ? 3 : 0; }
            else { led_want[k] = aos_seq_peak(k) > 6 ? 3 : 1; led_want[8 + k] = muted ? 0 : 2; }
        }
    } else {
        int len = sq_len(t), now = playing ? aos_seq_step(t) : -1;
        for (int k = 0; k < 16; k++) {
            int n = sq_spage * 16 + k;
            if (n >= len) continue;
            led_want[k] = (SQ_STEP(sq_pat(), t, n)->flags & SF_ON) ? 3 : 1;      /* a step that is on: bright; off: dim */
            { int c = aos_seq_ca(t, n); if (c >= 0 && (h->trk2[t][TP_CAM - SQ_TP] & 3) == 0) led_want[k] = c ? 3 : 1; }   /* GATE: the automaton's living cells are the steps */
            if (n == now) led_want[k] = 2;                                      /* the playhead: between the two */
            if ((sq_held >> k) & 1) led_want[k] = (ticks & 8) ? 3 : 1;
        }
    }
    leds_clear_trigs();
    for (int k = 0; k < 16; k++) set_led(2 * k, led_want[k]);
    /* the tracks: the one in hand full, one sounding bright, a muted one dark */
    for (int k = 0; k < 8; k++) { int muted = ((h->mutes >> k) & 1) || (h->solo && !((h->solo >> k) & 1)); set_led(40 + 2 * k, muted ? 0 : k == t ? 3 : aos_seq_peak(k) > 5 ? 2 : 1); }
    for (int a = 0; a < SQP_N; a++) if (page_lamp[a]) set_led(page_lamp[a], sq_page == a && sq_view == SV_GRID ? 3 : 1);
    set_led(90, playing ? 3 : 1); set_led(88, playing ? 1 : 2); set_led(84, sq_rec ? 3 : 1);
    set_led(38, playing && (aos_seq_bar() & 3) == 0 ? 3 : 0);     /* TEMPO: the beat */
}
#endif
