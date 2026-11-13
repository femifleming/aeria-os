#!/bin/sh
# The manual's screenshots: parts/ concatenated (assemble.sh's order) into preview/aeria_manual_ui.c, then
# manual_shots.c compiled and run on the host. The firmware reads battery RAM at fixed addresses; in the copy (never
# in parts/) each is pointed at an array of the harness's: the old preset store, the projects (0x10040000), the scenes
# (0x100E8000), the note of what is loaded (0x100FBA00), and -- through preview/seq_host.h, a copy of
# aeria-os/src/seq.h made here -- the sequencer (0x100C0000). SEQ_HOST lets the sequencer's UI compile off the unit.
# Usage: preview/manual_shots.sh <dir for the 128x64 PGMs>
cd "$(dirname "$0")/.." || exit 1
sed 's/^#define SEQ_BASE .*/#define SEQ_BASE ((uintptr_t)fake_seq)/' ../aeria-os/src/seq.h > preview/seq_host.h
(cd parts && cat head12.c params12.c pattern.c shelf.c scales_table.c scales.c master.c sound12.c sky.c intro.c screensavers.c saver_menu.c browser.c system.c gumball.c pitch.c stocktest.c out.c cloud.c marbles.c pong.c wx.c seqfwd.c persist.c seqmode.c dreamsky.c ui12.c) | sed -e 's/^#define SNAP_OLD_BASE .*/#define SNAP_OLD_BASE ((uintptr_t)fake_old_store)/' -e 's/^#define PROJ_BASE .*/#define PROJ_BASE ((uintptr_t)fake_proj)/' -e 's/^#define SCENE_STORE .*/#define SCENE_STORE ((uintptr_t)fake_scenes)/' -e 's/^#define LOADED_AT .*/#define LOADED_AT ((uintptr_t)fake_loaded)/' -e 's/^#include "seq.h"/#include "seq_host.h"/' > preview/aeria_manual_ui.c
clang -O1 -w -DSEQ_HOST -I preview -I . -I ../aeria-os/src preview/manual_shots.c -o preview/manual_shots -lm && ./preview/manual_shots "${1:-preview}" $2
