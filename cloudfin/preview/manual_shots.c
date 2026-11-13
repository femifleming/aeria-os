/* Screenshots for the AERIA OS manual: every page, each weather, every screensaver, one PGM (128x64) each.
 * Build and run: preview/manual_shots.sh <out-dir>  -- it concatenates parts/ (assemble.sh's order) into
 * preview/aeria_manual_ui.c (aeria12.c itself is left alone), compiles this with PREVIEW and AERIAOS defined,
 * and runs it. The UI is driven through module_key / module_enc / module_tick as the panel would drive it;
 * what the hardware supplies (card listing, meters, input for PITCH) is stubbed below.
 * aeria-os/manual/img/*.png are made from the PGMs by manual_png.py (4x nearest neighbour). */
#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
static uint8_t fake_battery[0x4800] __attribute__((aligned(4)));   /* the store: 17 snapshots of 1 KB */
static uint8_t fake_old_store[0x2400] __attribute__((aligned(4)));  /* the older store it looks in once (empty here) */
#define SNAP_BASE ((uintptr_t)fake_battery)
/* 12 Nov 2026: more battery RAM the UI reads at fixed addresses; manual_shots.sh points each at one of these in its copy */
static uint8_t fake_proj[16 * 0x8000] __attribute__((aligned(4)));   /* PROJ_BASE 0x10040000: sixteen projects of 32 KB */
static uint8_t fake_seq[0x6400] __attribute__((aligned(4)));         /* SEQ_BASE 0x100C0000: the sequencer's head and steps */
static uint8_t fake_scenes[0x1000] __attribute__((aligned(4)));      /* SCENE_STORE 0x100E8000 */
static uint8_t fake_loaded[0x300] __attribute__((aligned(4)));       /* LOADED_AT 0x100FBA00 */
#include "aeria_manual_ui.c"
void aos_route(int s, int on, const uint32_t g[4]) { (void)s; (void)on; (void)g; }
void aos_cue_chain(int on) { (void)on; }
void aos_grain_window(const uint32_t w[65]) { (void)w; }
int aos_midi_out(unsigned st, unsigned a, unsigned b) { (void)st; (void)a; (void)b; return 1; }
void aos_pong(int slot, int ptch, int dec, int level) { (void)slot; (void)ptch; (void)dec; (void)level; }
int aos_slot_ready(int slot) { (void)slot; return 1; }
void aos_balloon(int b, int st) { (void)b; (void)st; }
void aos_balloon_at(int a, int b, int c) { (void)a; (void)b; (void)c; }
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level) { (void)b; (void)on; (void)size; (void)dens; (void)pos; (void)spry; (void)semi; (void)rndo; (void)fade; (void)level; }
void aos_star_at(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void aos_weather_filters(int lp, int hp) { (void)lp; (void)hp; }
/* a small card: /AERIA/LOOPS and /AERIA/STARS/<folders> */
int aos_list(const char *p, void (*each)(const char *, int))
{
    if (!strcmp(p, "/")) { each("AERIA", 1); each("Field recordings", 1); each("Drum Machines", 1); each("piano room.wav", 0); return 4; }
    if (!strcmp(p, "/AERIA")) { each("LOOPS", 1); each("STARS", 1); each("INTRO.WAV", 0); return 3; }
    if (!strcmp(p, "/AERIA/LOOPS")) { each("harbour at night.wav", 0); each("glass harmonica.wav", 0); each("tape choir 02.wav", 0); each("rain on a tin roof.wav", 0); each("slow organ.wav", 0); each("music box.wav", 0); each("wind chimes.wav", 0); return 7; }
    if (!strcmp(p, "/Drum Machines")) { each("Dusty kit", 1); each("Found sounds", 1); each("Glass and wood", 1); each("Rubber 606", 1); return 4; }
    if (!strncmp(p, "/Drum Machines/", 15)) { each("01 kick.wav", 0); each("02 clap.wav", 0); each("03 hat closed.wav", 0); each("04 rim.wav", 0); each("05 tom.wav", 0); each("06 stab.wav", 0); each("07 door.wav", 0); each("08 pad.wav", 0); each("09 hat open.wav", 0); return 9; }
    if (!strcmp(p, "/AERIA/STARS")) { each("Bells", 1); each("Kalimba", 1); each("Glass", 1); each("Voices", 1); return 4; }
    each("bell 01.wav", 0); each("bell 02.wav", 0); each("bell 03.wav", 0); each("bell 04.wav", 0); return 4;
}
int aos_slot_load(int k, int s, const char *p) { (void)k; (void)s; (void)p; return 1; }
int aos_slot_status(int k, int s) { (void)k; (void)s; return 0; }
void aos_hear(int on) { (void)on; }
void aos_tap_want(int w, int on) { (void)w; (void)on; }
int aos_meter(int i) { static const int lv[6] = {11, 7, 3, 0, 13, 12}; return lv[i]; }
void aos_shuffle(int16_t *i, int n, uint32_t s) { (void)i; (void)n; (void)s; }
void aos_note_deal(int b, const char *f, uint32_t s) { (void)b; (void)f; (void)s; }
/* 12 Nov 2026: the master (freeze delay, EQ, limiter) is sound only; the LOOP page's picture and the CLOCK
 * screensaver need something to draw */
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen) { (void)on; (void)sixteenths; (void)bpm; (void)fb; (void)hp; (void)lp; (void)send; (void)wet; (void)frozen; }
void aos_limiter(int on) { (void)on; }
void aos_master_eq(int band, int freq, int db) { (void)band; (void)freq; (void)db; }
static int loop_shown;   /* 0: no loop loaded */
const uint8_t *aos_loop_wave(void)   /* a made-up sample: four swells with a quiet tail, 128 columns */
{
    static uint8_t w[128];
    if (!loop_shown) return 0;
    for (int x = 0; x < 128; x++) {
        double ph = fmod(x / 32.0, 1.0), env = exp(-2.2 * ph) * (0.7 + 0.3 * sin(x * 0.11 + 1.0)), rip = 0.7 + 0.3 * sin(x * 1.9) * sin(x * 0.37);
        double v = (0.12 + 0.95 * env * rip) * (x > 116 ? (128 - x) / 12.0 : 1.0);
        w[x] = (uint8_t)(v > 1 ? 255 : v < 0 ? 0 : v * 255);
    }
    return w;
}
int aos_loop_at(void) { return loop_shown ? 61 : -1; }
/* 12 Nov 2026: projects reload the samples they name; nothing to load here */
void aos_samples_reload(void) { }
/* ---- the sequencer's sound side (aeria-os/src/seq.c), stood in for: its data is real (battery RAM, mapped in main),
 * the transport is a flag and a step the harness sets, the meters what the harness says ---- */
static int h_seq, h_play, h_at, h_pat, h_fill, h_peak[8];
void aos_seq_defaults(void)   /* as seq.c's */
{
    SqHead *h = SQH;
    memset((void *)(uintptr_t)SEQ_BASE, 0, SEQ_HEAD_SIZE + SEQ_STEPS_SIZE);
    h->ver = 2; h->swing = 50; h->duck_rel = 40;
    for (int p = 0; p < SQ_PATTERNS; p++) { h->mlen[p] = 16; for (int t = 0; t < SQ_TRACKS; t++) h->len[p][t] = 16; }
    for (int t = 0; t < SQ_TRACKS; t++) {
        uint8_t *p = h->trk[t];
        p[TP_SMPL] = (uint8_t)(96 + t); p[TP_PTCH] = 64; p[TP_DEC] = 127; p[TP_VOL] = 100; p[TP_LP] = 127; p[TP_PAN] = 64;
        p[TP_PDEC] = 40; p[TP_LFOR] = 20; p[TP_MNOT] = (uint8_t)(36 + t);
        { uint8_t *q = h->trk2[t]; q[TP_DENS - SQ_TP] = 127; q[TP_RATE - SQ_TP] = 5; q[TP_WARP - SQ_TP] = 64; q[TP_FMR - SQ_TP] = 16; q[TP_FMI - SQ_TP] = 40; q[TP_FMD - SQ_TP] = 50; }   /* seq.c's seq_trk2_defaults */
    }
    h->magic[0] = 'A'; h->magic[1] = 'E'; h->magic[2] = 'S'; h->magic[3] = 'Q';
}
void aos_seq_mode(int on) { on = on != 0; if (on && SQH->magic[0] != 'A') aos_seq_defaults(); h_seq = on; if (!on) h_play = 0; SQH->mode = (uint8_t)on; }
int aos_seq_is(void) { return h_seq; }
void aos_seq_play(int on) { h_play = on != 0; if (on) { h_at = 0; h_pat = SQH->pattern & 3; } }
int aos_seq_playing(void) { return h_play; }
int aos_seq_step(int t) { int l = SQH->len[h_pat][t & 7]; return h_at % (l >= 1 && l <= 64 ? l : 16); }
int aos_seq_bar(void) { int l = SQH->mlen[h_pat]; return h_at % (l ? l : 16); }
int aos_seq_pat(void) { return h_pat; }
void aos_seq_hit(int t, int vel, int rec) { (void)vel; (void)rec; h_peak[t & 7] = 14; }
void aos_seq_tempo(int bpm10) { (void)bpm10; }
int aos_seq_peak(int t) { return h_play || h_peak[t & 7] > 12 ? h_peak[t & 7] : 0; }
void aos_seq_fill(int on) { h_fill = on; }
void aos_seq_mod(int t, int which, int v) { (void)t; (void)which; (void)v; }
void aos_seq_cushion(int big) { (void)big; }
void aos_seq_live(int on) { (void)on; }
static int h_ext;   /* 1: a clock from outside has the tempo (the top line shows it) */
int aos_seq_ext(void) { return h_ext; }
int aos_seq_bpm10(void) { return 1240; }
void aos_breathe(void) { }
int aos_clock_seconds(void) { return 15 * 3600 + 30 * 60; }   /* no real clock here: the picture is made at 15:30:00 */
static int disk_on;
void aos_usb_disk(int on) { disk_on = on; }
int aos_disk_busy(void) { return disk_on; }

static unsigned char fr[1024] __attribute__((aligned(4)));
static const char *outdir;
static int n_shot, tone_on;
static double tone_ph;
static void run(int n)
{
    struct modwerk_runtime_api api;
    for (int i = 0; i < n; i++) {
        if (tone_on) for (int k = 0; k < 735; k++) { int32_t v = (int32_t)(sin(tone_ph) * 2.0e6); tone_ph += 2 * M_PI * 220.0 / 44100.0; aos_pitch_sample(v, 0, 0, 0); }
#ifdef DREAM_SKY
        if (!dv.kind) dv.wait = 60000;   /* no dream visit drifts into a picture by chance: extra_shots() sets them */
#endif
        module_tick(&api); module_fast(16393); if (i % 8 == 0) preview_step++;
    }
}
static void key(int c) { module_key(c, 1); module_key(c, 0); }
static void down(int c) { module_key(c, 1); }
static void up(int c) { module_key(c, 0); }
static void enc(int e, int d) { while (d > 0) { module_enc(e, 1); d--; } while (d < 0) { module_enc(e, -1); d++; } }
static void shot(const char *name)
{
    module_draw(fr); const uint32_t *w = (const uint32_t *)fr;
    char path[512]; snprintf(path, sizeof path, "%s/%s.pgm", outdir, name);
    FILE *o = fopen(path, "wb"); if (!o) { perror(path); return; }
    fprintf(o, "P5 128 64 255\n");
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) fputc(((w[x * 2 + (y < 32 ? 1 : 0)] >> (y & 31)) & 1) ? 255 : 0, o);
    fclose(o); n_shot++;
}
static void put(int slot, int shelf_row_)   /* fill a chain slot through the shelf, as the musician does */
{
    key(0x22); key(slot); key(0x2f); enc(0, -20); enc(0, shelf_row_); key(0x31); run(3);
}
static void saver(int s, int ticks_, const char *name) { seed = 0x2f6b9d13u + (uint32_t)s * 77u;   /* the same game every run */
    ss_start(s); saver_on = 1; run(ticks_); shot(name); saver_on = 0; ss_stop(); }
enum { K_SRC = 0x22, K_AMP, K_LFO, K_FX1, K_FX2, K_FUNC = 0x2d, K_YES = 0x31, K_NO = 0x32, K_CUE = 0x2a, K_REC = 0x29, K_MIDI = 0x35,
       K_PROJ = 0x1c, K_PART, K_AED, K_ARR, K_BANK = 0x2f, K_AB = 0x2b, K_CD = 0x2c, K_R3 = 0x36, K_UP = 0x33, K_DOWN = 0x20, K_LEFT = 0x34, K_RIGHT = 0x21, K_PATTERN = 0x2e, K_MIX = 0x30, K_PLAY = 0x28, K_STOP = 0x27 };
/* a sequence that looks like music: a kick on 1 5 9 13, a clap, hats with locks, a ratchet, chances and conditions ... */
static SqStep *dstep(int p, int t, int n, int vel) { SqStep *s = SQ_STEP(p, t, n - 1); memset(s, 0, sizeof *s); s->flags = SF_ON; s->vel = (uint8_t)vel; return s; }
static void demo_sequence(void)
{
    aos_seq_defaults();
    SqHead *h = SQH; SqStep *s;
    for (int p = 0; p < 2; p++) {
        for (int n = 1; n <= 13; n += 4) dstep(p, 0, n, 110);                              /* T1 kick */
        dstep(p, 1, 5, 100); dstep(p, 1, 13, 100); s = dstep(p, 1, 16, 60); s->prob = 40;   /* T2 clap, a ghost now and then */
        /* T3 hats: the track in hand */
        dstep(p, 2, 3, 90);
        s = dstep(p, 2, 4, 50); s->prob = 50;
        s = dstep(p, 2, 7, 100); s->lp[0] = TP_LP + 1; s->lv[0] = 96; s->lp[1] = TP_DLY + 1; s->lv[1] = 90;
        s = dstep(p, 2, 11, 90); s->micro = 9;
        s = dstep(p, 2, 12, 70); s->cond = 2;                                             /* 2:2 */
        s = dstep(p, 2, 15, 100); s->ratch = 3;
        dstep(p, 3, 4, 90); dstep(p, 3, 8, 90); dstep(p, 3, 11, 70);                        /* T4 rim, twelve steps long */
        dstep(p, 4, 1, 110); dstep(p, 4, 4, 100); s = dstep(p, 4, 7, 100); s->lp[0] = TP_PTCH + 1; s->lv[0] = 67; dstep(p, 4, 12, 100);   /* T5 SUB */
        dstep(p, 5, 3, 90); s = dstep(p, 5, 10, 90); s->cond = 7;                           /* T6 stab; 1:4 */
        dstep(p, 6, 6, 80); s = dstep(p, 6, 22, 80); s->flags |= SF_REV; dstep(p, 6, 30, 70);   /* T7 a found sound, two bars */
        s = dstep(p, 7, 1, 80); s->cond = SQC_FIRST;                                      /* T8 pad, once */
        h->len[p][3] = 12; h->len[p][6] = 32;
    }
    dstep(1, 0, 15, 90); dstep(1, 1, 8, 70);                                             /* B: a little more */
    h->swing = 58; h->duck_src = 0; h->duck_rel = 48;
    h->trk[0][TP_DEC] = 70; h->trk[0][TP_PENV] = 30;
    h->trk[2][TP_HP] = 64; h->trk[2][TP_DEC] = 40; h->trk[2][TP_DLY] = 30; h->trk[2][TP_PAN] = 76; h->trk[2][TP_SHUF] = 40;
    h->trk[2][TP_LFOD] = 1; h->trk[2][TP_LFOA] = 30;
    h->trk[4][TP_MACH] = 1; h->trk[4][TP_PENV] = 50; h->trk[4][TP_DEC] = 60; h->trk[4][TP_DUCK] = 90;
    h->trk[5][TP_DUCK] = 60; h->trk[5][TP_LP] = 100; h->trk[6][TP_SLIP] = 12; h->trk[6][TP_OUT] = 1;
    h->trk[7][TP_DUCK] = 100; h->trk[7][TP_ATK] = 60; h->trk[7][TP_FREE] = 1; h->trk[7][TP_VOL] = 80;
    sq_tempo_set(124);
}
static int fx_row_named(const char *nm) { for (int r = 1; r <= FX_COUNT; r++) if (!strcmp(fx_info[r - 1].name + 6, nm)) return r; return 0; }
#include "manual_shots_extra.inc"
int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : ".";
    /* the intro: the brush, BY SADNOISE, the sea rising */
    run(60); shot("intro1"); run(80); shot("intro2"); run(85); shot("intro3");
    run(120);
    /* the default chain, put on from the shelf: PITCH DIST GRAINS RING SMEAR, slots 6 and 7 empty */
    put(0, 1); put(1, 2); put(2, 3); put(3, 4); put(4, 6);
    /* the sky alone in each weather (the page fades after ten seconds without a key) */
    static const char *const wn[7] = {"sky_clear", "sky_cloudy", "sky_rain", "sky_storm", "sky_snow", "sky_heat", "sky_fog"};
    for (int w = 0; w < 7; w++) {
        key(K_AMP); key(0); if (w) key(w); run(700);        /* trig 1 clears the sky; a trig turns that weather on by itself */
        if (w == 3) { strike(); run(5); }
        shot(wn[w]);
    }
    run(5600);   /* the effect a storm's lightning lent to an empty slot has gone back by now */
    key(K_AMP); key(0); key(K_YES); run(700); shot("sky_night");            /* CLEAR at night */
    key(K_AMP); key(K_YES); key(2); key(6); shot("sky_toggle");              /* day; RAIN and FOG both on: the popup names them */
    run(6); shot("sky_fog_in");                                             /* ... and a moment later the fog's own effect is in the chain (12 Nov 2026, 15:52) */
    run(698); shot("sky_rain_fog");
    key(K_AMP); key(0); key(1); run(200);                                   /* CLOUDY alone again */
    shot("sky_page"); down(K_FUNC); run(2); shot("sky_page_func"); up(K_FUNC);
    /* CHAIN, as the unit's first start leaves it (persist.c's slot_gentle): every effect dry, PITCH at no shift.
     * (Here, after the weather tour above: a change of weather hands every MIX back to the weather.) */
    for (int i = 0; i < 5; i++) slot_gentle(i);
    key(K_SRC); key(0); run(30); run(90); shot("chain");
    enc(0, 3); run(4); shot("chain_popup_pitch");                            /* SHFT in semitones */
    run(90); enc(5, 64); run(4); shot("chain_popup_mix"); run(90);           /* MIX brought in from dry */
    /* scenes: SCENE A held, a knob turned -- the scene's value, not the knob's own; B holds scene 2; the fader between */
    down(KEY_SCENEA); run(4); shot("scene_hold"); enc(5, -40); enc(0, -4); run(4); shot("scene_knob"); up(KEY_SCENEA);
    down(KEY_SCENEB); key(2); run(4); shot("scene_pick"); enc(5, 60); enc(0, 7); up(KEY_SCENEB);
    preview_xfade = 64; run(90); shot("scene_fade"); preview_xfade = 0;
    down(KEY_SCENEA); key(K_NO); up(KEY_SCENEA); down(KEY_SCENEB); key(K_NO); key(1); up(KEY_SCENEB); run(90);
    key(2); run(90); enc(0, 6); run(4); shot("chain_popup_ms");              /* GRAINS SIZE in ms */
    /* FUNC on CHAIN: the effect's second page */
    run(90); down(K_FUNC); run(2); shot("chain_func_grains"); enc(2, 40); run(4); shot("chain_func_popup"); enc(2, -40); up(K_FUNC);
    key(1); run(90); down(K_FUNC); enc(0, 1); run(4); shot("chain_func_dist"); enc(0, -1); up(K_FUNC);
    key(0); run(90); down(K_FUNC); run(2); shot("chain_func_none"); up(K_FUNC); key(2);
    run(90); key(K_BANK); run(3); shot("shelf"); enc(0, 4); run(2); shot("shelf2"); key(K_NO);
    run(90); key(5); run(90); shot("chain_empty");
    put(5, 10); key(K_SRC); key(5); run(90); enc(3, 2); run(4); shot("chain_echo");   /* ECHO: RATO */
    put(6, 7); key(K_SRC); key(6); run(100); shot("chain_rose"); down(K_FUNC); run(2); shot("chain_func_rose"); up(K_FUNC);             /* ROSE: its window in the sky */
    key(K_SRC); key(6); key(K_BANK); enc(0, -20); key(K_YES); run(5);        /* slot 7 empty again */
    key(K_SRC); key(5); key(K_BANK); enc(0, -20); key(K_YES); run(5);        /* slot 6 empty again */
    down(K_FUNC); key(3); up(K_FUNC); run(20); key(K_SRC); key(1); run(90); shot("chain_bypass"); down(K_FUNC); key(3); up(K_FUNC); run(20);
    /* SOURCE, LOOP, STARS */
    key(K_LFO); key(0); key(1); run(90); shot("source");
    down(K_FUNC); key(0); up(K_FUNC); run(90); shot("browser_root");
    key(K_YES); run(2); key(K_YES); run(2); key(K_DOWN); key(K_DOWN); run(90); shot("browser_loops"); key(K_YES); run(120);
    down(K_FUNC); key(K_LFO); key(1); up(K_FUNC); key(K_YES); key(K_DOWN); key(K_YES); key(K_DOWN); run(90); shot("browser_stars"); key(K_NO); key(K_NO); key(K_NO); key(K_NO); run(3);
    loop_shown = 1; key(K_AED); key(2); down(K_FUNC); key(11); up(K_FUNC); run(90); shot("loop"); enc(2, -12); run(4); shot("loop_popup");
    key(K_ARR); run(260); shot("stars"); run(700); shot("sky_stars"); key(K_ARR); run(90); down(K_FUNC); run(2); shot("stars_func"); up(K_FUNC);
    key(K_PATTERN); run(200); key(K_ARR); run(90); shot("stars_birds"); key(K_PATTERN);
    /* BALLOONS */
    key(K_LFO); key(2); key(K_PART); key(K_AB); run(170); shot("balloon_filling");
    key(K_AB); key(K_CD); run(240); key(K_CD); key(K_R3); run(120); key(K_R3); run(150); key(K_PART); key(1); run(90); shot("balloon");
    enc(0, 30); run(4); shot("balloon_popup");
    down(K_FUNC); run(90); shot("balloon_func"); enc(3, 7); run(4); shot("balloon_func_popup"); enc(3, -7); up(K_FUNC);
    key(1); run(700); shot("sky_balloons");
    /* MIX, GAIN */
    key(K_FX1); enc(0, 24); run(90); shot("mix"); down(K_FUNC); run(2); shot("mix_func"); enc(2, 18); run(4); shot("mix_func_scale"); up(K_FUNC);
    /* in a scale (BHAIRAV): where a pitch knob lands */
    key(K_SRC); key(0); run(90); enc(0, 4); run(4); shot("chain_popup_scale"); enc(0, -4);
    key(K_AED); run(90); enc(2, 7); run(4); shot("loop_popup_scale"); enc(2, -7);
    key(K_FX1); down(K_FUNC); enc(2, -50); up(K_FUNC); run(90);                 /* the scale OFF again */
    key(K_FX1); run(90); shot("gain"); enc(3, 4); run(4); shot("gain_popup"); enc(3, -4);
    run(90); down(K_FUNC); run(2); shot("gain_func"); enc(0, 3); run(4); shot("gain_func_popup"); enc(0, -3); enc(4, 6); run(4); shot("gain_func_hz"); enc(4, -6); up(K_FUNC);
    /* MASTER (the MIX key): the freeze delay */
    key(K_MIX); enc(5, 80); run(90); shot("master"); key(5); run(4); shot("master_time"); key(3);
    run(90); down(K_FUNC); run(2); shot("master_func"); enc(0, 30); run(4); shot("master_tempo"); enc(0, -30); up(K_FUNC);
    run(90); preview_xfade = 127; run(4); shot("master_freeze"); preview_xfade = 0; run(4); enc(5, -127); run(90);   /* the crossfader at B */
    key(K_FX1); enc(0, -24); run(100);
    /* OUT */
    key(K_CUE); key(2); enc(0, 2); run(2); key(1); enc(0, 3); enc(3, 2); enc(5, 30); run(95); shot("out");
    key(2); run(90); down(K_FUNC); enc(0, 1); run(90); shot("out_func"); up(K_FUNC); key(K_CUE); run(90);
    /* GRAIN CLOUD */
    key(K_REC); run(90); shot("cloud"); enc(0, 4); enc(1, -40); run(90); shot("cloud2"); enc(0, 4); enc(1, 60); enc(2, 63); run(90); shot("cloud3"); key(K_REC); run(90);
    /* the MIDI modules */
    key(K_MIDI); key(0); key(0); key(0); key(1); key(1); key(2); key(3); key(3); key(K_YES); run(150); shot("gumballs");
    key(K_MIDI); key(K_YES); tone_on = 1; run(240); shot("pitch"); key(2); run(120); shot("pitch_chord"); tone_on = 0; key(0); key(K_YES); run(30);
    key(K_MIDI); key(K_YES); run(200); shot("marbles"); down(K_FUNC); run(8); shot("marbles_func"); up(K_FUNC); key(K_YES);
    key(K_MIDI); key(K_YES); key(K_YES); key(K_YES); key(K_YES); run(95); shot("pong"); down(K_FUNC); run(5); shot("pong_func"); up(K_FUNC);
    /* SYSTEM, USB DISK MODE, presets */
    key(K_FX2); run(90); shot("system"); key(0); run(90); shot("usb_disk"); key(K_NO); run(90);
    /* PROJECTS: an empty page; project 1 saved and named with the trigs; a second one; the page with both */
    key(K_SRC); key(0); run(90); key(K_PROJ); run(90); shot("projects_empty");
    down(K_FUNC); key(K_YES); up(K_FUNC);
    key(1); key(1); key(10); key(0); key(0); key(14); key(8); key(8); key(7); run(55); key(7); key(6); run(20); shot("project_name");   /* DUB ROOM */
    key(K_YES); run(4);
    key(4); down(K_FUNC); key(K_YES); up(K_FUNC); key(8); key(8); key(0); key(4); key(6); key(6); key(K_YES);                          /* RAIN, in 5 */
    key(0); run(90); shot("projects"); key(K_PROJ); run(90);
    /* ---- the SEQUENCER (FUNC + MIDI) ---- */
    demo_sequence(); sq_fx_pick(7, fx_row_named("WIND"));   /* an effect with its own memory on T8: the list marks it as taken (not SMEAR: the FFT one all but fills the second core, and ECHO is picked below) */
    { static const int pk[8] = {13, 0, 9, 0, 11, 0, 4, 6}; for (int i = 0; i < 8; i++) h_peak[i] = pk[i]; }
    down(K_FUNC); key(K_MIDI); up(K_FUNC); run(4);
    key(0x12); key(K_SRC); run(90); shot("seq_grid");                                    /* T3 in hand, stopped, the SRC knobs */
    key(K_PLAY); h_at = 8; run(90); shot("seq_play");
    key(K_STOP);
    down(K_FUNC); run(90); shot("seq_src_func"); up(K_FUNC);
    down(6); run(92); shot("seq_step"); enc(3, 2); run(4); shot("seq_step_cond"); enc(3, -2);     /* step 7 held: its own knobs */
    key(K_FX1); run(90); enc(1, -6); run(4); shot("seq_lock"); up(6); run(90);                    /* ... and a page key: that page's knobs lock */
    key(K_AMP); run(90); shot("seq_amp"); down(K_FUNC); run(2); shot("seq_amp_func"); up(K_FUNC);
    key(K_LFO); run(90); shot("seq_mod");
    key(K_FX1); run(90); shot("seq_flt");
    key(K_FX2); run(90); shot("seq_fx_none");
    key(K_YES); sq_fxrow = (uint8_t)fx_row_named("ECHO"); run(4); shot("seq_fx_list"); key(K_YES); run(4); shot("seq_fx_pick"); run(90);
    key(KEY_PAGE); run(90); shot("seq_ptn");
    key(K_PLAY); h_at = 6; key(K_BANK); key(5); run(90); shot("seq_mixer"); key(5); key(K_BANK);
    key(K_REC); run(94); shot("seq_live"); key(K_REC); key(K_STOP);
    key(0x16); key(K_RIGHT); key(K_SRC); run(90); shot("seq_page2"); key(K_LEFT); key(0x12);       /* T7: two bars, its second page */
    down(K_PATTERN); run(4); shot("seq_pattern"); key(0); key(0); key(1); key(0); up(K_PATTERN); run(4); shot("seq_chain"); run(90);
    down(K_PATTERN); key(0); up(K_PATTERN); run(90);
    key(K_SRC); down(K_FUNC); key(K_YES); up(K_FUNC); key(K_DOWN); key(K_DOWN); key(K_YES); run(90); shot("seq_kit");
    key(K_YES); run(2); shot("seq_kit_files"); key(K_YES); run(200);
    down(K_FUNC); key(K_MIDI); up(K_FUNC); run(90); key(K_SRC); key(0); run(90);                    /* back to the sky */
    extra_shots();   /* manual_shots_extra.inc: the sequencer's newer pages, the quick starts, dream visits */
    /* the screensavers: the games a while into their own play */
    saver(SS_PADDLE, 4200, "ss_paddle"); saver(SS_BRICKS, 1800, "ss_bricks"); saver(SS_ROCKS, 2400, "ss_rocks");
    saver(SS_BLOCKS, 2700, "ss_blocks");
    saver(SS_LIGHTHOUSE, 300, "ss_lighthouse"); saver(SS_STORM, 300, "ss_storm"); saver(SS_SLIDE, 300, "ss_slide");
    saver(SS_FOREST, 300, "ss_forest"); saver(SS_SHADOWS, 500, "ss_shadows");
    saver(SS_PIPES, 760, "ss_pipes"); saver(SS_REDCARS, 640, "ss_redcars"); saver(SS_FROGS, 420, "ss_frogs");
    saver(SS_COLUMNS, 200, "ss_columns"); saver(SS_TRAIN, 440, "ss_train"); saver(SS_WALKWAY, 300, "ss_walkway");
    saver(SS_ISLAND, 600, "ss_island");
    saver(SS_ELEVATOR, 410, "ss_elevator"); saver(SS_GOLDMOSS, 1150, "ss_goldmoss"); saver(SS_TREBUCHT, 300, "ss_trebucht");
    saver(SS_VALLEY, 900, "ss_valley"); saver(SS_BLUEBAR, 14, "ss_bluebar"); saver(SS_LILYPADS, 100, "ss_lilypads");
    saver(SS_BOOKS, 80, "ss_books"); saver(SS_CATFACE, 300, "ss_catface"); saver(SS_WAREHSE, 1400, "ss_warehse");
    saver(SS_LASER, 2160, "ss_laser");
    saver(SS_DOTS, 300, "ss_dots"); saver(SS_DOTS, 4000, "ss_dots_melted");
    saver(SS_LIFE, 420, "ss_life"); saver(SS_CLOCK, 30, "ss_clock");
    more_weather_shots();   /* manual_shots_extra.inc: the FFT SMEAR, WIND HAIL AURORA TWISTER -- last, so every picture before it stays as it was */
    printf("ok %d shots\n", n_shot);
    return 0;
}
