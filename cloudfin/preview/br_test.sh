#!/bin/sh
# AERIA OS (15 Nov 2026): the KIT browser's keys in the sequencer. parts/ in assemble.sh's order into
# preview/aeria_br_ui.c, the battery-RAM addresses (and the sequencer, through seq_host.h) pointed at the harness's arrays.
cd "$(dirname "$0")/.." || exit 1
sed -e 's/^#define SEQ_BASE .*/#define SEQ_BASE ((uintptr_t)fake_seq)/' -e 's/^#define MT_BASE .*/#define MT_BASE ((uintptr_t)fake_mt)/' ../aeria-os/src/seq.h > preview/seq_host.h
(cd parts && cat head12.c params12.c pattern.c shelf.c lorenz.c scales_table.c scales.c master.c sound12.c sky.c intro.c screensavers.c saver_menu.c browser.c system.c gumball.c pitch.c stocktest.c out.c cloud.c marbles.c pong.c wx.c seqfwd.c mod.c persist.c seqmode.c dreamsky.c ui12.c) | sed \
  -e 's/^#define SNAP_OLD_BASE .*/#define SNAP_OLD_BASE ((uintptr_t)fake_old_store)/' \
  -e 's/^#define SNAP_BASE .*/#define SNAP_BASE ((uintptr_t)fake_battery)/' \
  -e 's/^#define PROJ_BASE .*/#define PROJ_BASE ((uintptr_t)fake_proj)/' -e 's/^#define PROJ_MARK .*/#define PROJ_MARK ((uintptr_t)fake_mark)/' \
  -e 's/^#define SCENE_STORE .*/#define SCENE_STORE ((uintptr_t)fake_scenes)/' \
  -e 's/^#define LOADED_AT .*/#define LOADED_AT ((uintptr_t)fake_loaded)/' \
  -e 's/^#define MODS .*/#define MODS ((volatile ModStore *)fake_mods)/' \
  -e 's/^#include "seq.h"/#include "seq_host.h"/' > preview/aeria_br_ui.c
sed -e "s/#include \"aeria_manual_ui.c\"/#include \"aeria_br_ui.c\"/" preview/manual_shots.c > preview/br_stubs.c
clang -O1 -g -w -DSEQ_HOST -fsanitize=address,undefined -I preview -I . -I ../aeria-os/src preview/br_test.c -o preview/br_test -lm && ./preview/br_test
