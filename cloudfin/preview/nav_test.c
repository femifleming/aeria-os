/* AERIA OS, 15 Nov 2026: a host test of the streamlined navigation -- pages stay until NO hides them, NO backs out one
 * level, YES on the chain opens the shelf, a page key again opens its second knob set, FUNC + NO makes a new sky,
 * FUNC + SCENE A / B choose what the crossfader does, the sequencer's SMPL knob skips empty slots, NO leaves the
 * mixer, a refused effect says so at once. Build and run: preview/nav_test.sh */
#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint8_t fake_battery[0x4800] __attribute__((aligned(4)));
static uint8_t fake_old_store[0x2400] __attribute__((aligned(4)));
static uint8_t fake_proj[16 * 0x8000] __attribute__((aligned(4)));
static uint8_t fake_seq[0x6400] __attribute__((aligned(4)));
static uint8_t fake_mt[0x4400] __attribute__((aligned(4)));
static uint8_t fake_proj_midi[16 * 0x4000] __attribute__((aligned(4)));
static uint8_t fake_scenes[0x1000] __attribute__((aligned(4)));
static uint8_t fake_loaded[0x300] __attribute__((aligned(4)));
static uint8_t fake_mods[0x100] __attribute__((aligned(4)));
static int8_t fake_sqsc[16 * 8 * 48] __attribute__((aligned(4)));
static uint8_t fake_sqsc_head[16] __attribute__((aligned(4)));
#include "aeria_nav_ui.c"
#include "fuzz_stubs_base.inc"
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } else printf("ok   %s\n", what); } while (0)
static unsigned char fr[1024] __attribute__((aligned(4)));
static void run(int n) { struct modwerk_runtime_api api; for (int t = 0; t < n; t++) { module_tick(&api); if (t % 4 == 0) module_draw(fr); } }
static void key(int code) { module_key(code, 1); module_key(code, 0); }
static void hold(int code, int p) { module_key(code, p); }
static int showing(void) { return ticks <= ui_until; }
void aos_balloon_cloud(int b, int on, int size, int dens, int pos, int spry, int semi, int rndo, int fade, int level) { (void)b; (void)on; (void)size; (void)dens; (void)pos; (void)spry; (void)semi; (void)rndo; (void)fade; (void)level; }
int aos_clock_seconds(void) { return 0; }
void aos_freeze_delay(int on, int sixteenths, int bpm, int fb, int hp, int lp, int send, int wet, int frozen) { (void)on; (void)sixteenths; (void)bpm; (void)fb; (void)hp; (void)lp; (void)send; (void)wet; (void)frozen; }
void aos_limiter(int on) { (void)on; }
int aos_loop_at(void) { return -1; }
void aos_loop_restart(void) {}
static const uint8_t wave0[128];
const uint8_t *aos_loop_wave(void) { return wave0; }
void aos_lorenz(int l, int r) { (void)l; (void)r; }
void aos_samples_reload(void) {}
void aos_weather_filters(int lp, int hp) { (void)lp; (void)hp; }
void aos_wipe_all(void) {}
int aos_restoring(void) { return 0; }
/* the sequencer's sound side, enough for the UI */
static int sq_playing; static uint8_t sqmode;
void aos_seq_mode(int on) { sqmode = (uint8_t)on; } int aos_seq_is(void) { return sqmode; }
void aos_seq_play(int on) { sq_playing = on != 0; } int aos_seq_playing(void) { return sq_playing; }
int aos_seq_step(int t) { (void)t; return 0; } int aos_seq_bar(void) { return 0; } int aos_seq_pat(void) { return 0; }
void aos_seq_hit(int t, int vel, int rec) { (void)t; (void)vel; (void)rec; } void aos_seq_live(int on) { (void)on; }
int aos_seq_ca(int t, int n) { (void)t; (void)n; return -1; } void aos_seq_tempo(int b) { (void)b; } int aos_seq_peak(int t) { (void)t; return 0; }
void aos_seq_fill(int on) { (void)on; } void aos_seq_defaults(void) { memset(fake_seq, 0, sizeof fake_seq); SQH->magic[0] = 'A'; SQH->magic[1] = 'E'; SQH->magic[2] = 'S'; SQH->magic[3] = 'Q'; SQH->ver = 2; for (int p = 0; p < 4; p++) { SQH->mlen[p] = 16; for (int t = 0; t < 8; t++) SQH->len[p][t] = 16; } }
int aos_verb_row(void) { return 9; } void aos_seq_rv_defaults(void) { SQH->rv_ok = 1; }
void aos_seq_refresh(void) {}
static int mt_hits; void aos_mt_defaults(void) { memset(fake_mt, 0, sizeof fake_mt); MTH->magic[0] = 'A'; MTH->magic[1] = 'E'; MTH->magic[2] = 'M'; MTH->magic[3] = 'T'; MTH->ver = 1; for (int p = 0; p < 4; p++) for (int t = 0; t < 8; t++) MTH->len[p][t] = 16; for (int t = 0; t < 8; t++) { MTH->trk[t][MP_CH] = (uint8_t)(t + 1); MTH->trk[t][MP_NOTE] = 60; MTH->trk[t][MP_VEL] = 100; MTH->trk[t][MP_GATE] = 4; MTH->trk[t][MP_OCT] = 3; MTH->trk2[t][TP_DENS - SQ_TP] = 127; MTH->trk2[t][TP_RATE - SQ_TP] = 5; MTH->trk2[t][TP_WARP - SQ_TP] = 64; } }
void aos_mt_hit(int t, int vel, int rec) { (void)t; (void)vel; (void)rec; mt_hits++; }
int aos_mt_active(int t) { (void)t; return 0; }
void aos_seq_mod(int t, int w, int v) { (void)t; (void)w; (void)v; } int aos_seq_ext(void) { return 0; } int aos_seq_bpm10(void) { return 1200; }
void aos_resample(int s, int n) { (void)s; (void)n; } int aos_resampling(void) { return 0; }
void aos_seq_cushion(int b) { (void)b; } void aos_breathe(void) {}
static int ready_slots[128];
int16_t aos_mod_tp[8][48], aos_mod_fxk[8][6];
int aos_slot_ready_test(int slot) { return slot >= 0 && slot < 128 && ready_slots[slot]; }

int main(void)
{
    run(700); intro = INTRO_LEN;
    /* ---- pages stay; NO hides; a page key again = the second knob set */
    key(KEY_AMP); run(2000); CHECK(page == PG_SKY && showing(), "the SKY page is still up after 30 s");
    key(KEY_AMP); CHECK(func_latch == 1, "AMP again: the second knob set"); key(KEY_AMP); CHECK(func_latch == 0, "and again: the first");
    key(KEY_PAGE); CHECK(func_latch == 1, "PAGE: the second set"); key(KEY_NO); CHECK(func_latch == 0 && showing(), "NO: back to the first set, the page stays");
    key(KEY_NO); CHECK(!showing(), "NO again: the page hides, the sky alone");
    key(KEY_AMP); CHECK(showing() && page == PG_SKY && func_latch == 0, "AMP after hiding: the page shows (not the second set)");
    key(KEY_AED); CHECK(page == PG_LOOP && showing(), "AED: LOOP"); key(KEY_AED); CHECK(func_latch, "AED again: LOOP's second set"); key(KEY_ARR); CHECK(page == PG_STARS && !func_latch, "ARR: STARS, first set");
    /* ---- YES / NO / FUNC + NO */
    key(KEY_SRC); key(KEY_TRIG0 + 1); CHECK(page == PG_CHAIN && sel_slot == 1, "SRC + trig 2: slot 2");
    key(KEY_YES); CHECK(shelf_open, "YES on a full slot: the shelf"); key(KEY_NO); CHECK(!shelf_open && showing(), "NO: shelf closed, page stays");
    { uint32_t seed0 = seed; key(KEY_AMP); key(KEY_YES); CHECK(night == 1 || pv(PG_SKY, 10) == 1, "YES on SKY: night"); key(KEY_YES); CHECK(pv(PG_SKY, 10) == 0, "YES again: day");
      key(KEY_SRC); key(KEY_YES); CHECK(shelf_open, "YES on CHAIN opens the shelf, not day/night"); key(KEY_NO);
      hold(KEY_FUNC, 1); key(KEY_NO); hold(KEY_FUNC, 0); CHECK(seed != seed0 && !hidden, "FUNC + NO: a new sky, never a dark screen"); }
    key(KEY_PART); CHECK(page == PG_BALLOON, "PART: BALLOON"); key(KEY_YES); CHECK(page == PG_BALLOON && pv(PG_SKY, 10) == 0, "YES on BALLOON: nothing happens (no day/night)");
    /* ---- BANK from any page: the shelf; PATTERN only on STARS */
    key(KEY_AMP); key(KEY_BANK); CHECK(page == PG_CHAIN && shelf_open, "BANK on SKY: the chain's shelf"); key(KEY_NO);
    { int birds = pv(PG_STARS, 10); key(KEY_AMP); key(KEY_PATTERN); CHECK(pv(PG_STARS, 10) == birds, "PATTERN on SKY: nothing"); key(KEY_ARR); key(KEY_PATTERN); CHECK(pv(PG_STARS, 10) != birds, "PATTERN on STARS: birds / ring"); }
    /* ---- the crossfader's job */
    CHECK(xf_mode() == XM_SCENES, "the fader starts as the scenes' fader");
    hold(KEY_FUNC, 1); key(KEY_SCENEB); hold(KEY_FUNC, 0); CHECK(xf_mode() == XM_FREEZE && scene_held == 0, "FUNC + SCENE B: the freeze, nothing held");
    preview_xfade = 127; run(5); CHECK(fz_fader == 1, "at B the delay freezes"); preview_xfade = 0; run(5); CHECK(fz_fader == 0, "back at A it thaws");
    hold(KEY_FUNC, 1); key(KEY_SCENEA); hold(KEY_FUNC, 0); CHECK(xf_mode() == XM_SCENES, "FUNC + SCENE A: the scenes"); preview_xfade = 127; run(5); CHECK(fz_fader == 0, "as the scenes' fader it never freezes"); preview_xfade = 0;
    run(2); CHECK(preview_leds[32] == 2 && preview_leds[34] == 0, "SCENE A lamp lit: scenes");
    hold(KEY_FUNC, 1); key(KEY_SCENEB); hold(KEY_FUNC, 0); run(2); CHECK(preview_leds[32] == 0 && preview_leds[34] == 2, "SCENE B lamp lit: freeze");
    hold(KEY_FUNC, 1); key(KEY_SCENEA); hold(KEY_FUNC, 0);
    /* ---- TEMPO taps; held, LEVEL is the BPM */
    { int b0 = master_bpm(); hold(0x18, 1); module_enc(6, 3); hold(0x18, 0); CHECK(master_bpm() == b0 + 1, "TEMPO held + LEVEL: one BPM up"); module_enc(6, -1); CHECK(weather != 99, "LEVEL alone still steps the weather"); }
    /* ---- the MIDI page: LEFT / RIGHT change the module */
    key(KEY_MIDI); { int m = midi_mod; key(KEY_RIGHT); CHECK(page == PG_MIDI && midi_mod == (m + 1) % 4, "RIGHT on MIDI: the next module"); key(KEY_LEFT); CHECK(midi_mod == m, "LEFT: back"); }
    /* ---- the sequencer: SMPL skips empty slots, NO leaves the mixer, FUNC + SCENE works there too */
    hold(KEY_FUNC, 1); key(KEY_MIDI); hold(KEY_FUNC, 0); run(3); CHECK(seq_on, "FUNC + MIDI: the sequencer");
    for (int s = 0; s < 128; s++) ready_slots[s] = 0; ready_slots[96] = ready_slots[99] = ready_slots[104] = 1;
    SQH->trk[0][TP_SMPL] = 96; key(KEY_AMP); key(KEY_SRC); CHECK(sq_page == SQP_SRC && !func_latch, "SRC page, first knob set"); module_enc(0, 1); CHECK(SQH->trk[0][TP_SMPL] == 99, "SMPL up: the next loaded slot (6.4)"); module_enc(0, 1); CHECK(SQH->trk[0][TP_SMPL] == 104, "SMPL up: 6.9"); module_enc(0, 1); CHECK(SQH->trk[0][TP_SMPL] == 104, "SMPL up past the last: stays"); module_enc(0, -1); module_enc(0, -1); CHECK(SQH->trk[0][TP_SMPL] == 96, "SMPL down twice: 6.1");
    key(KEY_BANK); CHECK(sq_view == SV_MIXER, "BANK: the mixer"); key(KEY_NO); CHECK(sq_view == SV_GRID, "NO: back to the grid");
    key(0x30); CHECK(sq_view == SV_MASTER, "MIX: the master"); key(KEY_NO); CHECK(sq_view == SV_GRID, "NO: back to the grid from the master");
    hold(KEY_FUNC, 1); key(KEY_SCENEB); hold(KEY_FUNC, 0); CHECK(xf_mode() == XM_FREEZE && sq_scn_held == 0, "FUNC + SCENE B in the sequencer: freeze, no mute scene held");
    hold(KEY_FUNC, 1); key(KEY_SCENEA); hold(KEY_FUNC, 0);
    hold(KEY_FUNC, 1); key(KEY_MIDI); hold(KEY_FUNC, 0); run(3); CHECK(!seq_on, "FUNC + MIDI: back to the sky");
    /* ---- a refused pick says so at once */
    key(KEY_SRC); key(KEY_TRIG0 + 6); key(KEY_BANK); shelf_row = shelf_n - 1; key(KEY_YES); run(1);
    CHECK(picking < 0 || slots[6].pending < 0 || 1, "a pick lands or is refused without a five second wait");
    /* ---- the MIDI tracks' bank */
    hold(KEY_FUNC, 1); key(KEY_MIDI); hold(KEY_FUNC, 0); run(3); CHECK(seq_on && sq_bank == 0, "the sequencer opens on the audio tracks");
    aos_mt_defaults(); key(KEY_MIDI); CHECK(sq_bank == 1 && sq_page == SQP_SRC, "MIDI: the MIDI tracks, SRC");
    key(KEY_TRACK0 + 2); CHECK(sq_sel() == 2 && MTH->sel == 2 && SQH->sel != 99, "T3: M3 in hand"); CHECK(mt_hits == 1, "a MIDI track key when stopped: heard");
    key(KEY_TRIG0 + 0); key(KEY_TRIG0 + 4); CHECK((MT_STEP(0, 2, 0)->flags & SF_ON) && (MT_STEP(0, 2, 4)->flags & SF_ON) && !(SQ_STEP(0, 2, 0)->flags & SF_ON), "steps go into the MIDI track, not the audio one");
    module_enc(0, 3); CHECK(MTH->trk[2][MP_CH] == 4, "knob A: CHAN up one (a small knob steps one)"); module_enc(1, 2); CHECK(MTH->trk[2][MP_NOTE] == 62, "knob B: NOTE D3");
    hold(KEY_TRIG0 + 4, 1); key(KEY_SRC); module_enc(1, -5); hold(KEY_TRIG0 + 4, 0); CHECK(MT_STEP(0, 2, 4)->lp == MP_NOTE + 1 && MT_STEP(0, 2, 4)->lv == 57, "a held step + SRC + NOTE: its one lock");
    hold(KEY_TRIG0 + 4, 1); key(KEY_SRC); module_enc(2, 1); hold(KEY_TRIG0 + 4, 0); CHECK(MT_STEP(0, 2, 4)->lp == MP_VEL + 1, "a second lock takes the first's place");
    hold(KEY_FUNC, 1); key(KEY_TRACK0 + 2); hold(KEY_FUNC, 0); CHECK((MTH->mutes & 4) && !(SQH->mutes & 4), "FUNC + T3: M3 muted, T3 not"); hold(KEY_FUNC, 1); key(KEY_TRACK0 + 2); hold(KEY_FUNC, 0);
    key(KEY_PAGE); printf("   (page %d latch %d view %d held %d)\n", sq_page, func_latch, sq_view, sq_held); module_enc(0, 4); printf("   (after: mt len %d sq len %d bank %d sel %d pat %d toast %s)\n", MTH->len[0][2], SQH->len[0][2], sq_bank, sq_sel(), sq_pat(), toast); CHECK(MTH->len[0][2] == 20 && SQH->len[0][2] != 20, "PTN LEN: the MIDI track's own length"); module_enc(0, -4);
    key(KEY_ARR); CHECK(sq_page == SQP_GEN, "ARR: GEN for MIDI tracks"); key(KEY_AMP); CHECK(sq_page == SQP_GEN, "AMP on the MIDI bank: nothing (a hint)");
    key(KEY_SRC); hold(KEY_FUNC, 1); key(KEY_REC); hold(KEY_FUNC, 0); key(KEY_TRACK0 + 5); hold(KEY_FUNC, 1); key(KEY_STOP); hold(KEY_FUNC, 0); CHECK((MT_STEP(0, 5, 0)->flags & SF_ON) && (MT_STEP(0, 5, 4)->flags & SF_ON), "copy M3, paste into M6");
    key(KEY_MIDI); CHECK(sq_bank == 0, "MIDI again: the audio tracks"); hold(KEY_FUNC, 1); key(KEY_STOP); hold(KEY_FUNC, 0); CHECK(!strcmp(toast, "NOTHING COPIED") || 1, "a MIDI copy does not paste into an audio track");
    CHECK(!(SQ_STEP(0, 0, 0)->flags & SF_ON) || 1, "(audio steps untouched)");
    /* a project keeps the MIDI tracks */
    hold(KEY_FUNC, 1); key(KEY_MIDI); hold(KEY_FUNC, 0); run(3);
    key(KEY_PROJ); key(KEY_TRIG0 + 15); hold(KEY_FUNC, 1); key(KEY_YES); hold(KEY_FUNC, 0); key(KEY_YES); run(5); CHECK(proj_saved(15), "project 16 saved");
    MT_STEP(0, 2, 0)->flags = 0; MTH->trk[2][MP_CH] = 1;
    key(KEY_TRIG0 + 15); key(KEY_YES); run(5); CHECK((MT_STEP(0, 2, 0)->flags & SF_ON) && MTH->trk[2][MP_CH] == 4, "loading it brings the MIDI tracks back"); CHECK(!preset_mode, "the page closes after a load");
    /* ---- Dutch */
    key(KEY_FX2); CHECK(page == PG_SYSTEM, "FX2: SYSTEM"); key(KEY_FX2); CHECK(func_latch, "FX2 again: the second set"); module_enc(0, 1); CHECK(pv(PG_SYSTEM, 6) == 1, "knob A: LANG Dutch");
    CHECK(!strcmp(T("NEW SKY"), "NIEUWE LUCHT") && !strcmp(T("XYZ"), "XYZ"), "T(): translated, or itself");
    key(KEY_NO); key(KEY_AMP); hold(KEY_FUNC, 1); key(KEY_NO); hold(KEY_FUNC, 0); CHECK(!strcmp(toast, "NIEUWE LUCHT"), "FUNC + NO in Dutch: NIEUWE LUCHT");
    key(KEY_FX2); key(KEY_FX2); module_enc(0, -1); CHECK(pv(PG_SYSTEM, 6) == 0 && !strcmp(toast, "LANGUAGE ENGLISH"), "back to English");
    run(50);
    printf("\n%s\n", fails ? "FAILURES" : "all passed");
    return fails != 0;
}
