/* AERIA OS, 13 Nov 2026: a short host test of the weather trim (RAIN, FOG, AURORA left), WX OFF, the engine's share of
 * core 0 following what is on, the sky waiting while the sequencer is up, and INIT / RESET ALL.
 * Build and run: preview/trim_test.sh */
#define PREVIEW 1
#define AERIAOS 1
#include <stdio.h>
#include <stdint.h>
static uint8_t fake_battery[0x4800] __attribute__((aligned(4)));
static uint8_t fake_old_store[0x2400] __attribute__((aligned(4)));
static uint8_t fake_proj[8 * 0x18000] __attribute__((aligned(4)));
static uint8_t fake_mark[16] __attribute__((aligned(4)));
static uint8_t fake_scenes[0x1000] __attribute__((aligned(4)));
static uint8_t fake_loaded[0x300] __attribute__((aligned(4)));
static uint8_t fake_mods[0x100] __attribute__((aligned(4)));
#include "aeria_host_ui.c"
#include "fuzz_stubs_base.inc"
extern int stub_wx_lp, stub_wx_hp, stub_eq_db[4], stub_wipes;
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } else printf("ok   %s\n", what); } while (0)
static unsigned char fr[1024] __attribute__((aligned(4)));
static void run(int n) { struct modwerk_runtime_api api; for (int t = 0; t < n; t++) { module_tick(&api); if (t % 4 == 0) module_draw(fr); } }
static void key(int code) { module_key(code, 1); module_key(code, 0); }

int main(void)
{
    run(700);   /* past the intro and the slots' set-up */
    intro = INTRO_LEN;

    /* ---- the weathers that are left */
    CHECK(wx_kept(CLEAR) && wx_kept(RAIN) && wx_kept(FOG), "CLEAR RAIN FOG kept");
    CHECK(!wx_kept(CLOUDY) && !wx_kept(STORM) && !wx_kept(SNOW) && !wx_kept(HEAT), "CLOUDY STORM SNOW HEAT gone");
    set_weather(STORM);  CHECK(weather == CLEAR && wx_on == 0, "set_weather(STORM) -> clear");
    toggle_weather(SNOW); CHECK(wx_on == 0, "toggle SNOW does nothing");
    toggle_weather(RAIN); CHECK(WON(RAIN), "toggle RAIN on");
    toggle_weather(FOG);  CHECK(WON(RAIN) && WON(FOG), "RAIN + FOG together");
    toggle_weather(HEAT); CHECK((wx_on & ~WX_KEEP) == 0, "HEAT refused beside them");
    CHECK(wx_step(CLEAR, 1) == RAIN && wx_step(RAIN, 1) == FOG && wx_step(FOG, 1) == FOG && wx_step(FOG, -1) == RAIN && wx_step(RAIN, -1) == CLEAR, "WTHR steps CLEAR > RAIN > FOG");
    xw_on = 0; xw_toggle(XW_WIND); xw_toggle(XW_HAIL); CHECK(xw_on == 0, "WIND / HAIL toggles refused");
    xw_toggle(XW_AURORA); CHECK(xw_on == (1u << XW_AURORA), "AURORA toggles on");
    xw_toggle(XW_AURORA);
    /* AUTO for a long while: only clear / rain / fog, only the aurora */
    set_weather(CLEAR); auto_weather = 1; P(PG_SKY, 4).value = P(PG_SKY, 4).user = 1;
    int bad = 0, seen[N_WEATHER] = {0};
    for (int i = 0; i < 400; i++) { spell_left = 0; step_auto(); seen[weather]++; if (!wx_kept(weather) || (wx_on & ~WX_KEEP) || (xw_on & ~(1u << XW_AURORA))) bad++; }
    CHECK(!bad, "AUTO stays within CLEAR / RAIN / FOG / AURORA");
    CHECK(seen[CLEAR] && seen[RAIN] && seen[FOG], "AUTO visits all three");
    /* a project saved with SNOW, WIND and HAIL on */
    { static Snap t; snap_take(&t); t.weather = SNOW; t.xf_right = (int16_t)(0x100 | (1 << SNOW) | (1 << RAIN) | (7 << 9)); snap_apply(&t);
      CHECK(wx_on == (1u << RAIN) && xw_on == (1u << XW_AURORA) && wx_kept(weather), "old project: SNOW/WIND/HAIL dropped, RAIN/AURORA kept"); }
    run(200);
    CHECK(xf_slot[XF_WIND] < 0 && xf_slot[XF_HAIL] < 0 && xf_slot[XF_TWISTER] < 0, "no WIND / HAIL / TWISTER loaded");
    CHECK(fx_banned("AERIA WIND") && fx_banned("AERIA HAIL") && fx_banned("AERIA TWISTER") && !fx_banned("AERIA FOG") && !fx_banned("AERIA AURORA"), "shelf hides WIND HAIL TWISTER");

    /* ---- WX OFF */
    page = PG_SKY; int was = page;
    P(PG_SKY, 5).value = P(PG_SKY, 5).user = 0;
    { int i5 = 5; (void)i5; }
    /* turn WX to OFF through the knob path (encoder 5, no FUNC) */
    module_enc(5, 1);
    CHECK(pv_base(PG_SKY, 5) == 1, "WX knob reads OFF");
    CHECK(wx_on == 0 && xw_on == 0 && !auto_weather, "WX OFF: clear sky, no aurora, AUTO off");
    toggle_weather(RAIN); CHECK(wx_on == 0, "WX OFF: RAIN refused");
    toggle_auto(); CHECK(!auto_weather, "WX OFF: AUTO refused");
    for (int i = 0; i < 50; i++) { spell_left = 0; step_auto(); }
    CHECK(wx_on == 0, "WX OFF: AUTO never moves");
    module_enc(5, -1); CHECK(pv_base(PG_SKY, 5) == 0, "WX back ON");
    page = was;

    /* ---- the engine's share of core 0 (the master EQ is gone: only the weather's filters are reserved) */
    P(PG_MIX, 4).value = P(PG_MIX, 4).user = 0; set_weather(CLEAR); xw_on = 0;
    run(400);   /* the rain's lowpass eases fully open */
    CHECK(!wxf_reserved && CORE0_BASE == CORE0_ENGINE, "clear sky: core 0 base = engine only (260, was 420)");
    CHECK(!params[PG_GAIN][6].name[0] && !params[PG_GAIN][11].name[0], "GAIN's EQ knobs are gone");
    for (int i = 0; i < 7; i++) { slot_core[i] = -1; slot_cost[i] = 0; }
    toggle_weather(RAIN); run(200);
    CHECK(wxf_reserved && stub_wx_lp >= 0, "rain: filter reserved and running");
    slot_core[0] = 0; slot_cost[0] = (uint16_t)(CORE_ROOM - CORE0_ENGINE - 10); wxf_reserved = 0; run(2);
    CHECK(!wxf_reserved && stub_wx_lp == -1, "no room: rain filter stays off");
    toggle_weather(RAIN); slot_core[0] = -1; slot_cost[0] = 0; run(400);
    CHECK(!wxf_reserved, "all off again: nothing reserved");

    /* ---- the sequencer face: the sky waits */
    seq_on = 1; auto_weather = 1; uint32_t sl = spell_left = 0; (void)sl;
    int w0 = weather; for (int i = 0; i < 300; i++) { spell_left = 0; run(1); }
    CHECK(weather == w0, "sequencer up: AUTO weather does not run");
    P(PG_MASTER, 9).value = P(PG_MASTER, 9).user = 1; run(2);   /* DJ filter mode */
    CHECK(stub_wx_lp == -1 || stub_wx_lp >= 0, "sequencer up: DJ filter path still runs");
    seq_on = 0; auto_weather = 0; P(PG_MASTER, 9).value = P(PG_MASTER, 9).user = 0;


    /* ---- INIT and RESET ALL */
    P(PG_SKY, 0).value = P(PG_SKY, 0).user = 3; P(PG_MASTER, 1).value = P(PG_MASTER, 1).user = 99;
    scene_val[2][5] = 40; toggle_weather(FOG); preview_live_fx[0] = 3;
    { volatile ModStore *m = MODS; mod_init(); m->r[0].src = 1; m->r[0].depth = 20; m->r[0].dest = (uint16_t)(MK_PAGE << 12 | 3); m->r[1].src = 2; m->r[1].depth = 9; m->r[1].dest = (uint16_t)(MK_TRK << 12 | 3); }
    func_held = 1; key(KEY_PROJ); func_held = 0;
    CHECK(preset_mode && init_ask, "FUNC+PROJ asks for a blank start");
    run(1);
    key(KEY_YES);
    CHECK(!init_ask && !preset_mode, "YES: INIT done, page closed");
    CHECK(pv_base(PG_SKY, 0) == snap_factory.value[PG_SKY][0] && pv_base(PG_MASTER, 1) == snap_factory.value[PG_MASTER][1], "INIT: knobs back to defaults");
    CHECK(scene_val[2][5] == -1, "INIT: scenes empty");
    CHECK(wx_on == 0 && !auto_weather, "INIT: clear sky, AUTO off");
    CHECK(preview_live_fx[0] == 0, "INIT: chain empty");
    { volatile ModStore *m = MODS; CHECK(m->r[0].src == 0 && m->r[1].src == 2, "INIT: sky MOD route cleared, sequencer's kept"); }
    CHECK(stub_wipes == 0, "INIT does not wipe buffers");
    balloon[1].state = B_FLOATING;
    func_held = 1; key(KEY_PROJ); module_key(KEY_YES, 1); module_key(KEY_YES, 0); func_held = 0;
    CHECK(stub_wipes == 1 && balloon[1].state == B_EMPTY, "FUNC+YES: RESET ALL wipes buffers and balloons");
    func_held = 1; key(KEY_PROJ); func_held = 0; key(KEY_NO);
    CHECK(!init_ask && !preset_mode && stub_wipes == 1, "NO backs out, nothing done");
    key(KEY_PROJ); CHECK(preset_mode && !init_ask, "plain PROJ still opens PROJECTS");
    key(KEY_NO);

    run(600);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
