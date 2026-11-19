#!/bin/sh
# AERIA OS (15 Nov 2026): the host test of the streamlined navigation. parts/ in assemble.sh's order into
# preview/aeria_nav_ui.c, the battery-RAM addresses pointed at the harness's arrays (and the sequencer through seq_host.h).
cd "$(dirname "$0")/.." || exit 1
sed -e 's/^#define SEQ_BASE .*/#define SEQ_BASE ((uintptr_t)fake_seq)/' -e 's/^#define MT_BASE .*/#define MT_BASE ((uintptr_t)fake_mt)/' ../aeria-os/src/seq.h > preview/seq_host.h
(cd parts && cat head12.c params12.c pattern.c shelf.c lorenz.c scales_table.c scales.c master.c sound12.c sky.c intro.c screensavers.c saver_menu.c browser.c system.c gumball.c pitch.c stocktest.c out.c cloud.c marbles.c pong.c wx.c seqfwd.c mod.c persist.c seqmode.c dreamsky.c ui12.c) | sed \
  -e 's/^#define SNAP_OLD_BASE .*/#define SNAP_OLD_BASE ((uintptr_t)fake_old_store)/' \
  -e 's/^#define SNAP_BASE .*/#define SNAP_BASE ((uintptr_t)fake_battery)/' \
  -e 's/^#define PROJ_BASE .*/#define PROJ_BASE ((uintptr_t)fake_proj)/' -e 's/^#define PROJ_MARK .*/#define PROJ_MARK ((uintptr_t)fake_mark)/' \
  -e 's/^#define SCENE_STORE .*/#define SCENE_STORE ((uintptr_t)fake_scenes)/' \
  -e 's/^#define LOADED_AT .*/#define LOADED_AT ((uintptr_t)fake_loaded)/' \
  -e 's/^#define MODS .*/#define MODS ((volatile ModStore *)fake_mods)/' \
  -e 's/^#define SQSC_HEAD .*/#define SQSC_HEAD ((volatile uint8_t *)fake_sqsc_head)/' \
  -e 's/^#define SQSC .*/#define SQSC ((volatile int8_t *)fake_sqsc)/' \
  -e 's/^#include "seq.h"/#include "seq_host.h"/' \
  -e 's/aos_slot_ready(/aos_slot_ready_test(/g' > preview/aeria_nav_ui.c
clang -O1 -g -w -DSEQ_HOST -fsanitize=address,undefined -I preview -I . -I ../aeria-os/src preview/nav_test.c -o preview/nav_test -lm && ./preview/nav_test
