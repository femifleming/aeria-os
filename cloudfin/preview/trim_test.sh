#!/bin/sh
# AERIA OS (13 Nov 2026): the short host test of the weather trim, WX OFF, core 0's share, sky/sequencer and
# INIT / RESET ALL. parts/ in assemble.sh's order into preview/aeria_host_ui.c, every fixed battery-RAM address
# pointed at an array of the harness's (as manual_shots.sh does; MOD's store too). Usage: preview/trim_test.sh [out]
cd "$(dirname "$0")/.." || exit 1
OUT=${1:-preview/trim_test}
(cd parts && cat head12.c params12.c pattern.c shelf.c lorenz.c scales_table.c scales.c master.c sound12.c sky.c intro.c screensavers.c saver_menu.c browser.c system.c gumball.c pitch.c stocktest.c out.c cloud.c marbles.c pong.c wx.c seqfwd.c mod.c persist.c seqmode.c dreamsky.c ui12.c) | sed \
  -e 's/^#define SNAP_OLD_BASE .*/#define SNAP_OLD_BASE ((uintptr_t)fake_old_store)/' \
  -e 's/^#define SNAP_BASE .*/#define SNAP_BASE ((uintptr_t)fake_battery)/' \
  -e 's/^#define PROJ_BASE .*/#define PROJ_BASE ((uintptr_t)fake_proj)/' \
  -e 's/^#define PROJ_MARK .*/#define PROJ_MARK ((uintptr_t)fake_mark)/' \
  -e 's/^#define SCENE_STORE .*/#define SCENE_STORE ((uintptr_t)fake_scenes)/' \
  -e 's/^#define LOADED_AT .*/#define LOADED_AT ((uintptr_t)fake_loaded)/' \
  -e 's/^#define MODS .*/#define MODS ((volatile ModStore *)fake_mods)/' > preview/aeria_host_ui.c
clang -O1 -g -w -fsanitize=address,undefined -I preview -I . -I ../aeria-os/src preview/trim_test.c preview/aos_stubs_trim.c -o "$OUT" && "$OUT"
