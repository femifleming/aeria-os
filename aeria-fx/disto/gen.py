#!/usr/bin/env python3
"""Generates disto.asm (AERIA DIST: atan-like drive / clipper / degrade)."""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, e2_sub, snap_close  # noqa: E402

H = '''; CYCLES_FORWARD_BRANCHES
; AERIA DIST -- StoreFX disto, clipper and degrade as one effect, MODE on
; page-2 slot 6. No buffer: state only in the r7 block.
;
;  DIST  s = sign(x) * G|x| / (G|x| + 1), G = 1/d, d = 0.4 - 0.3999 DRIV
;        (StoreFX: atan2(x, d) * 2/pi -- same value at G|x| = 1, same
;        limits). Computed as U/(U + 2^-14) with U = |x| G/2^14 kept in
;        48 bits, one 24-step fractional divide (unrolled, no rep), then
;        the one-pole "slope" lowpass (TONE: 0 dark .. 127 open).
;  CLIP  y = clamp(x, +-t) * 0.33/t, t = 0.25 .. 0.001 (log, DRIV), then
;        two one-pole lowpasses at 100 Hz .. 15 kHz (TONE, log; open at 127).
;  DGRD  bits 24 .. 2 (DRIV, rounded quantiser: add half, mask), sample-and-
;        hold at RATE 1 .. 0.01 of 44.1 kHz (log), TONE post lowpass.
;  LEVL  0 .. 2x output (64 = 1x).  MIX 0 = exact dry.
;
; r7: 03 mode+1  04/05 drive t/s  06/07 tone t/s  08/09 level t/s
;     0a/0b mix t/s  0c/0d rate t/s  0e mask 0f half  10 S&H phase (-1..0)
;     11/12 held L/R  13/14 lp1 L/R  15/16 lp2 L/R  17 temp  18 snap flag
'''


def tone_onepole(slot, tag, lo=0.03):
    """coef = lo + (1-lo) TONE^2, 1.0 at 127."""
    return [ins('move', 'x:(r6+$1)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
            ins('beq', f'dsto{tag}'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'),
            ins('move', 'a', 'x0'), ins('move', q(1 - lo), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('add', q(lo), 'a'), ins('bra', f'dstp{tag}'),
            f'dsto{tag}:', ins('move', '#>$7fffff', 'a'),
            f'dstp{tag}:', ins('move', 'a', f'x:(r7+${slot:02x})')]


def lp(state, coef_slot, src='a'):
    """a <- state += coef (src - state)."""
    out = [] if src == 'a' else [ins('move', src, 'a')]
    return out + [ins('move', f'x:(r7+${state:02x})', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
                  ins('move', f'x:(r7+${coef_slot:02x})', 'y1'), ins('mpy', 'y1', 'x1', 'a'),
                  ins('add', 'x0', 'a'), ins('move', 'a', f'x:(r7+${state:02x})')]


def level():
    """a <- a * level (level word = LEVL/128, x2)."""
    return [ins('move', 'a', 'x0'), ins('move', 'x:(r7+$09)', 'y1'), ins('mpy', 'x0', 'y1', 'a'),
            ins('asl', 'a')]


L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#64', '>dsizero'), ins('move', 'a', 'x:(r4)+'), 'dsizero:', ins('nop'), ins('rts')]

L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$1', 'n0'),
      ins('move', 'x:(r6+$c)', 'a'), ins('asr', '#$10', 'a', 'a'), ins('move', '#>$2', 'x0'),
      ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('add', '#>$1', 'a'),
      ins('move', 'x:(r7+$03)', 'x0'), ins('cmp', 'x0', 'a'), ins('beq', 'dssame'),
      ins('move', 'a', 'x:(r7+$03)'), ins('lua', '(r7+$10)', 'r4'), ins('clr', 'a'),
      ins('do', '#8', '>dsmzero'), ins('move', 'a', 'x:(r4)+'), 'dsmzero:', ins('nop'),
      ins('move', '#>$800000', 'a'), ins('move', 'a', 'x:(r7+$10)', c='phase -1: latch at once'),
      ins('move', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$18)', c='snap smoothers this call'),
      'dssame:',
      # level: LEVL/128 (x2 in the loop); 127 -> 2x - 1 LSB
      ins('move', 'x:(r6+$4)', 'a'), ins('move', 'a', 'x:(r7+$08)'),
      # mix
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'dsmset'), ins('move', '#>$7fffff', 'a'), 'dsmset:', ins('move', 'a', 'x:(r7+$0a)')] + snap_close(0x0a, 0x0b, 'dsmsnap') + [
      ins('move', 'x:(r7+$03)', 'a'), ins('move', '#>$2', 'x0'), ins('cmp', 'x0', 'a'),
      ins('blt', 'dsbdist'), ins('beq', 'dsbclip'), ins('bra', 'dsbdgrd')]


def snap(pairs, run):
    out = [ins('move', 'x:(r7+$18)', 'a'), ins('tst', 'a'), ins('beq', run)]
    for t in pairs:
        out += [ins('move', f'x:(r7+${t:02x})', 'b'), ins('move', 'b', f'x:(r7+${t + 1:02x})')]
    return out + [ins('clr', 'a'), ins('move', 'a', 'x:(r7+$18)')]


# ======================= DIST ===============================================
# G/2^14 = (2^-14)/d, d = 0.4 - 0.3999 DRIV
L += ['dsbdist:', ins('move', 'x:(r6)', 'x0'), ins('move', q(0.3999), 'y0'), ins('mpy', '-y0', 'x0', 'a'),
      ins('add', q(0.4), 'a'), ins('move', 'a', 'x0'), ins('move', '#>$000200', 'a'),
      ins('andi', '#$fe', 'ccr'), ins('do', '#24', '>dsddiv'), ins('div', 'x0', 'a'), 'dsddiv:', ins('nop'),
      ins('move', 'a0', 'x:(r7+$04)')]
# slope coefficient: c = 0.01 + 0.99 TONE, open at 127
L += [ins('move', 'x:(r6+$1)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('beq', 'dsdto'), ins('move', 'a', 'x0'), ins('move', q(0.99), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('add', q(0.01), 'a'), ins('bra', 'dsdtp'), 'dsdto:', ins('move', '#>$7fffff', 'a'),
      'dsdtp:', ins('move', 'a', 'x:(r7+$06)')]
L += snap((0x04, 0x06, 0x08), 'dsdrun') + ['dsdrun:', ins('do', 'n7', '>dsdend')]
L += smooth(0x04, 0x05, 7) + smooth(0x06, 0x07, 7) + smooth(0x08, 0x09, 7) + smooth(0x0a, 0x0b, 7)
for ch, st in enumerate((0x13, 0x14)):
    L += [f'; ---- DIST {"LR"[ch]} ----', ins('move', f'x:(r0)' if ch == 0 else 'x:(r0)', 'x0'),
          ins('move', 'x:(r7+$05)', 'y0'), ins('mpy', 'y0', 'x0', 'a', c='U = x G / 2^14, 48 bits'),
          ins('abs', 'a'), ins('move', 'a', 'b'), ins('add', '#>$000200', 'b'),
          ins('move', 'b', 'x1', c='U + 2^-14'), ins('andi', '#$fe', 'ccr')]
    L += [ins('div', 'x1', 'a')] * 24
    L += [ins('move', 'a0', 'y0'), ins('move', 'y0', 'b'), ins('neg', 'b'), ins('move', 'b', 'x1'),
          ins('move', 'x0', 'a'), ins('tst', 'a'), ins('tpl', 'y0', 'b'), ins('tmi', 'x1', 'b')]
    L += lp(st, 0x07, 'b') + level() + mix(mixslot=0x0b)
L += ['dsdend:', ins('nop'), ins('rts')]

# ======================= CLIP ===============================================
# 1/(1024 t) = 2^-(8 - 7.966 DRIV);  w/4 = 0.534 * 2^-(7.229 (1 - TONE)); c = 1 - 2^-(w 1.4427)
L += ['dsbclip:', ins('move', 'x:(r6)', 'x0'), ins('move', q(math.log2(250) / 32), 'y0'),
      ins('mpy', '-y0', 'x0', 'a'), ins('add', q(8 / 32), 'a'), ins('bsr', 'dsexp'),
      ins('move', 'b', 'x:(r7+$04)'),
      ins('move', 'x:(r6+$1)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('beq', 'dscto'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
      ins('move', q(math.log2(150) / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'dsexp'),
      ins('move', 'b', 'x0'), ins('move', q(2 * math.pi * 15000 / 44100 / 4), 'y0'),
      ins('mpy', 'y0', 'x0', 'a', c='w/4'), ins('move', 'a', 'x0'),
      ins('move', q(4 / math.log(2) / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'dsexp'),
      ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'), ins('bra', 'dsctp'),
      'dscto:', ins('move', '#>$7fffff', 'a'), 'dsctp:', ins('move', 'a', 'x:(r7+$06)')]
L += snap((0x04, 0x06, 0x08), 'dscrun') + ['dscrun:', ins('do', 'n7', '>dscend')]
L += smooth(0x04, 0x05, 7) + smooth(0x06, 0x07, 7) + smooth(0x08, 0x09, 7) + smooth(0x0a, 0x0b, 7)
for ch, (s1, s2) in enumerate(((0x13, 0x15), (0x14, 0x16))):
    L += [f'; ---- CLIP {"LR"[ch]} ----', ins('move', 'x:(r0)', 'x0'), ins('move', 'x:(r7+$05)', 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$8', 'a', 'a', c='x / (4t), up to 250x, in 56 bits'),
          ins('move', q(0.25), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a'),
          ins('move', q(-0.25), 'x1'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(0.66), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
          ins('asl', 'a', c='clamp * 1.32 = clamp(x, t) * 0.33 / t')]
    L += lp(s1, 0x07) + lp(s2, 0x07) + level() + mix(mixslot=0x0b)
L += ['dscend:', ins('nop'), ins('rts')]

# ======================= DGRD ===============================================
# n = floor(23 DRIV) (bits 24 - n), LSBq = 2^-(23 - n), mask = -LSBq, half = LSBq/2
# rate = 2^-(6.644 (1 - RATE)), 1 - 1 LSB at 127
L += ['dsbdgrd:', ins('move', 'x:(r6)', 'x0'), ins('move', q(23 / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('asr', '#$12', 'a', 'a'), ins('move', 'a1', 'a'), ins('asl', '#$12', 'a', 'a'),
      ins('move', 'a', 'x0'), ins('move', q(23 / 32), 'a'), ins('sub', 'x0', 'a'), ins('bsr', 'dsexp'),
      ins('move', 'b', 'a'), ins('asr', 'a'), ins('move', 'a1', 'x:(r7+$0f)', c='half'),
      ins('neg', 'b'), ins('move', 'b1', 'x:(r7+$0e)', c='mask'),
      ins('move', 'x:(r6+$2)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('beq', 'dsgro'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
      ins('move', q(math.log2(100) / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'dsexp'),
      ins('move', 'b', 'a'), ins('bra', 'dsgrp'), 'dsgro:', ins('move', '#>$7fffff', 'a'),
      'dsgrp:', ins('move', 'a', 'x:(r7+$0c)')]
L += tone_onepole(0x06, 'g')
L += snap((0x06, 0x08, 0x0c), 'dsgrun') + ['dsgrun:', ins('do', 'n7', '>dsgend')]
L += smooth(0x06, 0x07, 7) + smooth(0x08, 0x09, 7) + smooth(0x0a, 0x0b, 7) + smooth(0x0c, 0x0d, 7)
L += ['; phase in [-1, 0): wraps (and latches) when it reaches 0',
      ins('move', 'x:(r7+$10)', 'a'), ins('move', 'x:(r7+$0d)', 'x0'), ins('add', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$17)'), ins('move', 'a', 'b'), ins('move', '#>$800000', 'x0'),
      ins('add', 'x0', 'b'), ins('move', 'b', 'x1'), ins('tst', 'a'), ins('tge', 'x1', 'a'),
      ins('move', 'a', 'x:(r7+$10)')]
for ch, (held, st) in enumerate(((0x11, 0x13), (0x12, 0x14))):
    L += [f'; ---- DGRD {"LR"[ch]} ----', ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r7+$0f)', 'x0'),
          ins('add', 'x0', 'a'), ins('move', 'x:(r7+$0e)', 'x0'), ins('and', 'x0', 'a'),
          ins('move', 'a', 'y1', c='quantised (limited if +FS rounding overflowed)'),
          ins('move', f'x:(r7+${held:02x})', 'b'), ins('move', 'x:(r7+$17)', 'a'), ins('tst', 'a'),
          ins('tge', 'y1', 'b'), ins('move', 'b', f'x:(r7+${held:02x})')]
    L += lp(st, 0x07, 'b') + level() + mix(mixslot=0x0b)
L += ['dsgend:', ins('nop'), ins('rts')]

L += ['; ---- subroutines (after their callers) ----'] + e2_sub('dsexp', 'dsxpend')
Path(__file__).with_name('disto.asm').write_text(finish(L))
print('wrote disto.asm')
