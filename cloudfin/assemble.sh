#!/bin/sh
# AERIA: the parts, in order, into aeria12.c
cd "$(dirname "$0")/parts" && cat head12.c params12.c pattern.c shelf.c lorenz.c scales_table.c scales.c master.c sound12.c sky.c intro.c screensavers.c saver_menu.c browser.c system.c gumball.c pitch.c stocktest.c out.c cloud.c marbles.c pong.c wx.c seqfwd.c mod.c persist.c seqmode.c dreamsky.c ui12.c > ../aeria12.c
