#!/usr/bin/env python3
"""Generates reso.asm (AERIA RESO: waveguide / complex / detuned resonator)."""
import math
import os
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, softclip, mix, finish, e2_sub, snap_close  # noqa: E402

P0 = 44100 / (440 * 2 ** ((30 - 69) / 12))        # F#1 period, 953.53 samples
WM, XM = 2 * math.pi / P0 * 2 ** 6.125, 6.125     # w = WM * 2^-(XM - x)
E_WAVE = math.log2(100) / (0.02 * 44100) * 1024 / 32   # E/32 per (P/1024 * aD)
E_CPLX = math.log2(100) / (0.02 * 44100) / 32          # E/32 per aD
IN_C = math.log2(1.5 * math.sqrt(2 * math.log(2) * math.log2(100) / (0.02 * 44100) * P0)) / 32
NORM4 = 1.5 * math.sqrt(2 * math.log(2) * math.log2(100) / (0.02 * 44100)) / 4
SPREAD = (0.5, 0.5 * 0.9981, 0.5 * 0.9957)
QZ = int(os.environ.get('QZ', '1'))   # quality pass: magnitude truncation on the recursive states, 48-bit DTND DC mean
OUTF = os.environ.get('OUT')
SK = float(os.environ.get('SK', '0.5'))
QZL = int(os.environ.get('QZL', '1'))   # DTND: toward-zero on the loop lowpass state too   # DTND side balance term (0 = as before)


def mtz(acc, lab):
    """toward zero: a negative acc gets +1 LSB on a1 before it is stored (floor -> ceil), so |stored| <= |exact|:
    a decaying loop can only lose energy to its rounding, and reaches exact 0 (no limit cycle, no stuck DC)"""
    return [ins('tst', acc), ins('bge', lab), ins('add', '#>$1', acc), f'{lab}:'] if QZ else []


SIDE = 0.5      # DTND stereo: left = v + SIDE (u1 - u2), right = v - SIDE (u1 - u2)

H = '''; CYCLES_FORWARD_BRANCHES
; AERIA RESO -- StoreFX waveguide (Karplus), complex resonator and detuned
; (allpass-dispersed) waveguide as one effect, MODE on page-2 slot 6.
;
;  WAVE  stereo Karplus-Strong: a pair ring, linear-interpolated read at
;        P - h, a two-point damping FIR (h = 0..0.5 from TONE, its group
;        delay compensated), per-pass gain 2^-E for a -40 dB fall time,
;        input gain 1.5 sqrt(1 - g^2) (StoreFX's), soft clip out.
;        R is detuned up by DTUN (0..25 cents) for width.
;  CPLX  stereo complex one-pole resonator (re/im rotation by r e^{jw}),
;        input through a TONE lowpass, scaled by 1.5 sqrt(1 - r^2)/4,
;        output 4*im soft clipped. R detuned by DTUN.
;  DTND  one waveguide (mono in) with three Schroeder allpasses (g 0.3) in
;        the loop at P * 0.5 * DTUN * (1, .9981, .9957) + 1 (StoreFX's
;        detuned resonator), TONE one-pole lowpass in the loop. Stereo out:
;        with v the loop signal after the third allpass (the old mono out)
;        and u1, u2 the same string after the first and the second,
;          left = v + 0.5 (u1 - u2),  right = v - 0.5 (u1 - u2):
;        u1 - u2 is what the second allpass changes, so each partial leans
;        left or right by its own phase across that allpass (the partials
;        alternate between the sides), the lows -- where a short allpass
;        changes nothing -- stay in the middle, and left + right = 2 v:
;        the mono sum IS the old output. The spread grows with DTUN (the
;        allpass length); DTUN 0 leaves 1-sample allpasses: mono but for
;        the top octave.
;
; Pitch: PTCH 0..72 = F#1 (46.25 Hz) .. F#7, FINE +-0.5 st (clamped at the
; bottom). DECY: fall time to -40 dB, 20 ms .. 20 s (log). Periods and
; coefficients per block through one 2^-y routine (the half-angle
; exponential squared, then shifts), smoothed per sample (P in 48 bits).
;
; Buffer: four 2,048-word segments (m = $7ff) at base, +$800, +$1000,
; +$1800 = 8,192 words, far below the Sidechain window (+$3DFE).
; MODE change re-arms the 68-call clear (dry while clearing).
;
; Quality pass (12 Nov 2026): every recursive state (WAVE and DTND line writes, CPLX re/im and its
; input lowpass, DTND loop lowpass; not CPLX re/im: those are 48-bit) is stored truncated TOWARD ZERO
; (a negative value gets +1 LSB before the floor), so a decaying loop only loses energy to rounding and
; ends in exact 0; the DTND DC mean is 48-bit; CPLX's re/im are 48-bit and its output is rounded. Before, CPLX could sing on for ever at up to -62 dBFS (a truncation limit
; cycle) and WAVE/DTND held up to -78 dBFS of DC.
;
; r7: 00 base 01 main ptr 02 clear count 03 mode+1  10/11 mix t/s
;   1c tmp sin 1d P/1024 or r 1e x/32 1f aD = 2^-(9.966 DECY)
;   WAVE 04 DL t 05/06 DL s  07 DR t 08/09 DR s  0a/0b g  0c/0d in
;        0e/0f h  12/13 previous tap L/R
;   CPLX 04/05 crL 06/07 ciL 08/09 crR 0a/0b ciR 0c/0d norm/4 0e/0f tone
;        14 reL 15 imL 16 reR 17 imR 18 lpL 19 lpR  12 13 1a 1b their low words (reL imL reR imR)
;   DTND 04 P t 05/06 P s 07/08 det 0a/0b g 0c/0d in 0e/0f tone 1a lp 19 v 18 dc mean (14 its low word)
;        12 u1, then the side signal 0.6 x 0.5 (u1 - u2)  13 u2, then the right output before its clip
;        (g and tone glide once per call in this mode; P, det, in and mix per sample)
'''


def tone_coef(slot, tag):
    return [ins('move', 'x:(r6+$3)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
            ins('beq', f'rzto{tag}'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'),
            ins('move', 'a', 'x0'), ins('move', q(0.97), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('add', q(0.03), 'a'), ins('bra', f'rztp{tag}'),
            f'rzto{tag}:', ins('move', '#>$7fffff', 'a'),
            f'rztp{tag}:', ins('move', 'a', f'x:(r7+${slot:02x})')]


def period(xslot_add_dtun=False):
    """a <- P/1024 for x/32 at $1e (+ DTUN*25 cents)."""
    out = [ins('move', 'x:(r7+$1e)', 'a')]
    if xslot_add_dtun:
        out += [ins('move', 'x:(r6+$4)', 'x0'), ins('move', q(25 / 1200 / 32), 'y0'),
                ins('mac', 'y0', 'x0', 'a')]
    out += [ins('bsr', 'rzexp'), ins('move', 'b', 'x0'), ins('move', q(P0 / 1024), 'y0'),
            ins('mpy', 'y0', 'x0', 'a')]
    return out


def in_gain(slot, tag):
    """2^-(x/2 + 4.983 DECY - log2 c), 1 when the exponent is negative."""
    return [ins('move', 'x:(r7+$1e)', 'a'), ins('asr', 'a'), ins('move', 'x:(r6+$2)', 'x0'),
            ins('move', q(math.log2(1000) / 2 / 32), 'y0'), ins('mac', 'y0', 'x0', 'a'),
            ins('sub', q(IN_C), 'a'), ins('move', '#>$7fffff', 'b'), ins('tst', 'a'),
            ins('blt', f'rzin{tag}'), ins('bsr', 'rzexp'),
            f'rzin{tag}:', ins('move', 'b', f'x:(r7+${slot:02x})')]


def split(src_slot, shift_int=13, n='n5', base='r5', reg='r1', pair=True, extra=None):
    """frac -> y1, r1 -> tap at int delay (pair: stride 2).  value = D/1024."""
    out = [ins('move', f'x:(r7+${src_slot:02x})', 'a')] if src_slot is not None else []
    if extra:
        out += extra
    out += [ins('move', 'a1', 'b'), ins('asr', f'#${shift_int:x}', 'a', 'a'),
            ins('move', 'a1', 'a', c='drop the fraction asr left in a0'),
            ins('and', '#>$001fff', 'b'), ins('asl', '#$a', 'b', 'b'), ins('move', 'b1', 'y1')]
    if pair:
        out += [ins('asl', 'a')]
    out += [ins('neg', 'a'), ins('move', 'a1', n), ins('lua', f'({base}+{n})', reg)]
    return out


def lerp(reg='r1', nreg='n1'):
    """a <- tap D + frac*(tap D+1 - tap D); frac in y1; x0 = tap D."""
    return [ins('move', f'y:({reg})+{nreg}', 'x0'), ins('move', f'y:({reg})', 'a'),
            ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'), ins('mpy', 'y1', 'x1', 'a'),
            ins('add', 'x0', 'a')]


L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#64', '>rzizero'), ins('move', 'a', 'x:(r4)+'), 'rzizero:', ins('nop'),
     ins('move', 'x:>$213', 'r4'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', 'x:(r7+$00)'),
     ins('move', 'a', 'x:(r7+$01)'), ins('rts')]

SUB = e2_sub('rzexp', 'rzxpend')

# ---- sin/cos: a = w -> a = r cos w, b = r sin w (r at $1d) -------------------
SUB += ['rzsico:', ins('move', 'a', 'x1'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'),
      ins('move', 'a', 'y0', c='w^2'), ins('move', q(1 / 120), 'x0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', q(1 / 6), 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
      ins('move', 'x1', 'b'), ins('sub', 'a', 'b', c='sin w'), ins('move', 'b', 'x:(r7+$1c)'),
      ins('move', q(1 / 720), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 24), 'b'),
      ins('sub', 'a', 'b'), ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', q(0.5), 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
      ins('mpy', 'y0', 'x0', 'a', c='1 - cos w'), ins('move', 'a', 'y1'),
      ins('move', 'x:(r7+$1d)', 'x0'), ins('mpy', '-x0', 'y1', 'a'), ins('add', 'x0', 'a', c='r cos w'),
      ins('move', 'x:(r7+$1c)', 'y1'), ins('mpy', 'x0', 'y1', 'b', c='r sin w'), ins('rts')]

# ---- proc: mode, mode change, common decode ----------------------------------
L += ['proc:', ins('move', '#>$ffffff', 'm4'),
      ins('move', 'x:(r6+$c)', 'a'), ins('asr', '#$10', 'a', 'a'), ins('move', '#>$2', 'x0'),
      ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('add', '#>$1', 'a'),
      ins('move', 'x:(r7+$03)', 'x0'), ins('cmp', 'x0', 'a'), ins('beq', 'rzsame'),
      ins('move', 'a', 'x:(r7+$03)'), ins('lua', '(r7+$4)', 'r4'), ins('clr', 'a'),
      ins('do', '#60', '>rzmzero'), ins('move', 'a', 'x:(r4)+'), 'rzmzero:', ins('nop'),
      ins('move', '#>$44', 'a'), ins('move', 'a', 'x:(r7+$02)'),
      'rzsame:',
      # x/32 = PTCH/3 + (FINE - 1/2)/384, clamped >= 0
      ins('move', 'x:(r6)', 'x0'), ins('move', q(1 / 3), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'x:(r6+$1)', 'b'), ins('sub', q(0.5), 'b'), ins('move', 'b', 'x0'),
      ins('move', q(1 / 384), 'y0'), ins('mac', 'y0', 'x0', 'a'), ins('move', '#>$0', 'x0'),
      ins('tst', 'a'), ins('tmi', 'x0', 'a'), ins('move', 'a', 'x:(r7+$1e)'),
      ins('move', 'x:(r6+$2)', 'x0'), ins('move', q(math.log2(1000) / 32), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'rzexp'), ins('move', 'b', 'x:(r7+$1f)', c='aD'),
      # mix
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'rzmset'), ins('move', '#>$7fffff', 'a'), 'rzmset:', ins('move', 'a', 'x:(r7+$10)')] + snap_close(0x10, 0x11, 'rzmsnap') + [
      ins('move', 'x:(r7+$03)', 'a'), ins('move', '#>$2', 'x0'), ins('cmp', 'x0', 'a'),
      ins('blt', 'rzbwave'), ins('beq', 'rzbcplx'), ins('bra', 'rzbdtnd')]


def clearing(snap, loop_label):
    """count down; while counting: snap smoothers, clear 128 words, rts."""
    out = [ins('move', 'x:(r7+$02)', 'a'), ins('tst', 'a'), ins('beq', loop_label),
           ins('sub', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$02)')]
    for t, s in snap:
        if s is None:
            out += [ins('clr', 'b'), ins('move', 'b', f'x:(r7+${t:02x})')]
        else:
            out += [ins('move', f'x:(r7+${t:02x})', 'b'), ins('move', 'b', f'x:(r7+${s:02x})')]
    lab = loop_label.replace('run', 'clr')
    out += [ins('cmp', '#>$000040', 'a'), ins('bge', f'{lab}n'),
            ins('asl', '#$7', 'a', 'a'), ins('move', 'x:(r7+$00)', 'x0'), ins('add', 'x0', 'a'),
            ins('move', 'a', 'r4'), ins('clr', 'a'),
            ins('do', '#128', f'>{lab}l'), ins('move', 'a', 'y:(r4)+'), f'{lab}l:', ins('nop'),
            f'{lab}n:', ins('rts')]
    return out


def setup_regs(aps):
    out = [ins('move', '#>$7ff', 'm1'), ins('move', '#>$7ff', 'm5'), ins('move', '#>$1', 'n0'),
           ins('move', 'x:(r7+$01)', 'r5')]
    for k, r in enumerate(aps):
        out += [ins('move', 'x:(r7+$01)', 'a'), ins('add', f'#>${0x800 * (k + 1):x}', 'a'),
                ins('move', 'a1', r), ins('move', '#>$7ff', 'm' + r[1])]
    return out


def restore(aps):
    out = [ins('move', 'r5', 'x:(r7+$01)'), ins('move', '#>$ffffff', 'm1'),
           ins('move', '#>$ffffff', 'm5')]
    for r in aps:
        out.append(ins('move', '#>$ffffff', 'm' + r[1]))
    return out + [ins('rts')]


# ======================= WAVE ===============================================
L += ['rzbwave:'] + period() + [ins('move', 'a', 'x:(r7+$1d)', c='PL/1024'),
      # h = 0.5 (1 - TONE), D = P - h
      ins('move', 'x:(r6+$3)', 'x0'), ins('move', '#>$7fffff', 'b'), ins('sub', 'x0', 'b'),
      ins('asr', 'b'), ins('move', 'b', 'x:(r7+$0e)'), ins('asr', '#$a', 'b', 'b'),
      ins('sub', 'b', 'a'), ins('move', 'a', 'x:(r7+$04)')]
L += period(True) + [ins('move', 'x:(r7+$0e)', 'b'), ins('asr', '#$a', 'b', 'b'), ins('sub', 'b', 'a'),
                     ins('move', 'a', 'x:(r7+$07)'),
                     ins('move', 'x:(r7+$1d)', 'x0'), ins('move', 'x:(r7+$1f)', 'y0'),
                     ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(E_WAVE), 'y0'),
                     ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'rzexp'), ins('move', 'b', 'x:(r7+$0a)', c='g')]
L += in_gain(0x0c, 'w')
L += clearing([(0x04, 0x05), (0x06, None), (0x07, 0x08), (0x09, None), (0x0a, 0x0b), (0x0c, 0x0d),
               (0x0e, 0x0f)], 'rzwrun')
L += ['rzwrun:'] + setup_regs([]) + [ins('move', '#>$fffffe', 'n1'), ins('do', 'n7', '>rzwend')]
L += smooth(0x04, 0x05, 7, 0x06) + smooth(0x07, 0x08, 7, 0x09) + smooth(0x0a, 0x0b, 7) \
    + smooth(0x0c, 0x0d, 7) + smooth(0x0e, 0x0f, 7) + smooth(0x10, 0x11, 7)
for ch, (dslot, prev) in enumerate(((0x05, 0x12), (0x08, 0x13))):
    L += [f'; ---- WAVE {"LR"[ch]} ----'] + split(dslot) + lerp() + [
        ins('move', f'x:(r7+${prev:02x})', 'b'), ins('move', 'a', f'x:(r7+${prev:02x})'),
        ins('sub', 'a', 'b'), ins('move', 'b', 'x1'), ins('move', 'x:(r7+$0f)', 'y1'),
        ins('mpy', 'y1', 'x1', 'b'), ins('add', 'a', 'b', c='y = o + h (o_prev - o)'),
        ins('move', 'x:(r7+$0b)', 'x0'), ins('move', 'b', 'y1'), ins('mpy', 'x0', 'y1', 'a'),
        ins('move', 'x:(r7+$0d)', 'x0'), ins('move', 'x:(r0)', 'y1'), ins('mac', 'x0', 'y1', 'a')] + mtz('a', f'rzqw{ch}') + [
        ins('move', 'a', 'y:(r5)+', c='line <- in x + g y')] + softclip('b', 'a') + mix()
L += ['rzwend:', ins('nop')] + restore([])

# ======================= CPLX ===============================================
L += ['rzbcplx:', ins('move', 'x:(r7+$1f)', 'x0'), ins('move', q(E_CPLX), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'rzexp'), ins('move', 'b', 'x:(r7+$1d)', c='r'),
      ins('move', 'x:(r6+$2)', 'x0'), ins('move', q(math.log2(1000) / 2 / 32), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'rzexp'), ins('move', 'b', 'x0'),
      ins('move', q(NORM4), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x:(r7+$0c)')]
L += tone_coef(0x0e, 'c')
for ch, (crs, cis) in enumerate(((0x04, 0x06), (0x08, 0x0a))):
    L += [ins('move', q(XM / 32), 'a'), ins('move', 'x:(r7+$1e)', 'x0'), ins('sub', 'x0', 'a')]
    if ch:
        L += [ins('move', 'x:(r6+$4)', 'x0'), ins('move', q(25 / 1200 / 32), 'y0'),
              ins('mac', '-y0', 'x0', 'a')]
    L += [ins('bsr', 'rzexp'), ins('move', 'b', 'x0'), ins('move', q(WM), 'y0'),
          ins('mpy', 'y0', 'x0', 'a', c='w'), ins('bsr', 'rzsico'),
          ins('move', 'a', f'x:(r7+${crs:02x})'), ins('move', 'b', f'x:(r7+${cis:02x})')]
L += clearing([(0x04, 0x05), (0x06, 0x07), (0x08, 0x09), (0x0a, 0x0b), (0x0c, 0x0d), (0x0e, 0x0f)],
              'rzcrun')
L += ['rzcrun:', ins('move', '#>$1', 'n0'), ins('do', 'n7', '>rzcend')]
for t in (0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10):
    L += smooth(t, t + 1, 7)
for ch, (cr, ci, re, im, lp, rl, il) in enumerate(((0x05, 0x07, 0x14, 0x15, 0x18, 0x12, 0x13),
                                                    (0x09, 0x0b, 0x16, 0x17, 0x19, 0x1a, 0x1b))):
    L += [f'; ---- CPLX {"LR"[ch]} ----', ins('move', 'x:(r0)', 'a'),
          ins('move', f'x:(r7+${lp:02x})', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', 'x:(r7+$0f)', 'y1'), ins('mpy', 'y1', 'x1', 'a'), ins('add', 'x0', 'a')] + mtz('a', f'rzqc{ch}l') + [
          ins('move', 'a', f'x:(r7+${lp:02x})')]
    if QZ:
        L += ['; 48-bit re/im (quality pass): the low words (raw a0/b0, used >> 1 as positive Q23) carry what',
              '; the products below the LSB leave, so a ring this close to r = 1 cannot sit in a truncation limit cycle',
              ins('move', f'x:(r7+${rl:02x})', 'b'), ins('lsr', '#$1', 'b'), ins('move', 'b1', 'x0', c='re lo / 2'),
              ins('move', f'x:(r7+${il:02x})', 'b'), ins('lsr', '#$1', 'b'), ins('move', 'b1', 'x1', c='im lo / 2'),
              ins('move', f'x:(r7+${cr:02x})', 'y0'), ins('move', f'x:(r7+${ci:02x})', 'y1'),
              ins('mpy', 'y0', 'x0', 'a'), ins('mac', '-y1', 'x1', 'a'), ins('asr', '#$17', 'a', 'a', c='(cr re lo - ci im lo) 2^-23'),
              ins('mpy', 'x0', 'y1', 'b'), ins('mac', 'x1', 'y0', 'b'), ins('asr', '#$17', 'b', 'b', c='(ci re lo + cr im lo) 2^-23'),
              ins('move', f'x:(r7+${lp:02x})', 'x0'), ins('move', 'x:(r7+$0d)', 'y1'),
              ins('mac', 'x0', 'y1', 'a', c='+ u = norm/4 * lp(x)'),
              ins('move', f'x:(r7+${re:02x})', 'x0'), ins('mac', 'y0', 'x0', 'a', c='+ cr re'),
              ins('move', f'x:(r7+${im:02x})', 'x1'), ins('move', f'x:(r7+${ci:02x})', 'y1'),
              ins('mac', '-y1', 'x1', 'a', c='- ci im'),
              ins('mac', 'x0', 'y1', 'b', c='+ ci re'), ins('mac', 'x1', 'y0', 'b', c='+ cr im'),
              ins('move', 'a', f'x:(r7+${re:02x})', c='limited, as before (a loud tone at the note fills it)'),
              ins('move', 'a0', f'x:(r7+${rl:02x})'),
              ins('move', 'b', f'x:(r7+${im:02x})'), ins('move', 'b0', f'x:(r7+${il:02x})'),
              ins('asl', '#$2', 'b', 'b'), ins('rnd', 'b', c='rounded out: a tail below 1/4 LSB reads 0')] + softclip('b', 'a') + mix()
    else:
        L += [ins('move', 'a', 'x0'),
          ins('move', 'x:(r7+$0d)', 'y1'), ins('mpy', 'x0', 'y1', 'a', c='u = norm/4 * lp(x)'),
          ins('move', f'x:(r7+${re:02x})', 'x0'), ins('move', f'x:(r7+${cr:02x})', 'y0'),
          ins('mac', 'y0', 'x0', 'a', c='+ cr re'),
          ins('move', f'x:(r7+${im:02x})', 'x1'), ins('move', f'x:(r7+${ci:02x})', 'y1'),
          ins('mac', '-y1', 'x1', 'a', c='- ci im'),
          ins('mpy', 'x0', 'y1', 'b', c='ci re'), ins('mac', 'x1', 'y0', 'b', c='+ cr im'),
          ins('move', 'a', f'x:(r7+${re:02x})'), ins('move', 'b', f'x:(r7+${im:02x})'),
          ins('asl', '#$2', 'b', 'b')] + softclip('b', 'a') + mix()
L += ['rzcend:', ins('nop'), ins('rts')]

# ======================= DTND ===============================================
L += ['rzbdtnd:'] + period() + [ins('move', 'a', 'x:(r7+$04)'), ins('move', 'a', 'x:(r7+$1d)'),
      ins('move', 'x:(r6+$4)', 'a'), ins('move', 'a', 'x:(r7+$07)'),
      ins('move', 'x:(r7+$1d)', 'x0'), ins('move', 'x:(r7+$1f)', 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(E_WAVE), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'rzexp'), ins('move', 'b', 'x:(r7+$0a)')]
L += in_gain(0x0c, 'd') + tone_coef(0x0e, 'd')
L += clearing([(0x04, 0x05), (0x06, None), (0x07, 0x08), (0x0a, 0x0b), (0x0c, 0x0d), (0x0e, 0x0f)],
              'rzdrun')
APS = ['r2', 'r3', 'r4']
L += ['rzdrun:', '; loop gain and loop lowpass coefficient glide here, once per call (2^-3: the same ~3 ms)'] \
    + smooth(0x0a, 0x0b, 3) + smooth(0x0e, 0x0f, 3) \
    + setup_regs(APS) + [ins('move', '#>$ffffff', 'n1'), ins('do', 'n7', '>rzdend')]
L += smooth(0x04, 0x05, 7, 0x06) + smooth(0x07, 0x08, 7) + smooth(0x0c, 0x0d, 7) + smooth(0x10, 0x11, 7)
L += ['; ---- DTND: main read at P, gain, loop lowpass ----'] + split(0x05, pair=False) + lerp() + [
    ins('move', 'a', 'x0'), ins('move', 'x:(r7+$0b)', 'y1'), ins('mpy', 'x0', 'y1', 'a', c='g o'),
    ins('move', 'x:(r7+$1a)', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
    ins('move', 'x:(r7+$0f)', 'y1'), ins('mpy', 'y1', 'x1', 'a'), ins('add', 'x0', 'a')] + (mtz('a', 'rzqdl') if QZL else []) + [
    ins('move', 'a', 'x:(r7+$1a)', c='loop lowpass state'), ins('move', 'a', 'x:(r7+$19)', c='v'),
    ins('move', 'x:(r7+$05)', 'x0'), ins('move', 'x:(r7+$08)', 'y0'), ins('mpy', 'y0', 'x0', 'b'),
    ins('move', 'b', 'x:(r7+$1c)', c='P det / 1024')]
for k, r in enumerate(APS):
    L += [f'; allpass {k + 1}', ins('move', 'x:(r7+$1c)', 'x0'), ins('move', q(SPREAD[k]), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('add', q(1 / 1024), 'a', c='+1 sample')] \
        + split(None, pair=False, n='n' + r[1], base=r) + lerp() + [
          ins('move', 'a', 'x0', c='d'), ins('move', 'x:(r7+$19)', 'a'), ins('move', q(0.3), 'y1'),
          ins('mac', '-x0', 'y1', 'a', c='in = v - .3 d'), ins('move', 'a', f'y:({r})+'),
          ins('move', 'a', 'x1'), ins('move', 'x0', 'a'), ins('mac', 'x1', 'y1? ', 'a')]
    L[-1] = ins('mac', 'y1', 'x1', 'a', c='v = d + .3 in')
    L += [ins('move', 'a', 'x:(r7+$19)')]
    if k < 2:
        L += [ins('move', 'a', f'x:(r7+${0x12 + k:02x})', c=f'u{k + 1}: the string after allpass {k + 1}')]
if SK:
    k1, k2 = 0.6 * SIDE, 0.6 * SIDE * SK
    L += ['; side = 0.6 SIDE ((u1 - u2) + SK (v - u2)) (quality pass: the SK term). u1 - u2 alone correlates with v',
          '; (on average Re{H3 H2} - Re{H3} = g^2 - g < 0 over the partials: the right side was 1.5-2.5 dB louder on',
          '; noise); v - u2 correlates the other way (1 - g). SK = 0.5, measured. As three products: no DC in it',
          '; (the weights sum to 0), and L + R is still 2 v.',
          ins('move', 'x:(r7+$12)', 'x0'), ins('move', q(k1), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
          ins('move', 'x:(r7+$19)', 'x0'), ins('move', q(k2), 'y1'), ins('mac', 'x0', 'y1', 'a'),
          ins('move', 'x:(r7+$13)', 'x0'), ins('move', q(-k1 - k2), 'y1'), ins('mac', 'x0', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$12)')]
else:
    L += ['; side = 0.6 * SIDE * (u1 - u2) (no DC in it: both taps carry the same)',
          ins('move', 'x:(r7+$12)', 'a'), ins('move', 'x:(r7+$13)', 'x0'), ins('sub', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(0.6 * SIDE), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$12)')]
L += ['; DC block on v (leaky mean, 2^-12, 1.7 Hz): the loop gain at DC is 1/(1 - g)',
      ins('move', 'x:(r7+$19)', 'a'), ins('move', 'x:(r7+$18)', 'b')] + ([ins('move', 'x:(r7+$14)', 'b0', c='48-bit mean')] if QZ else []) + [
      ins('sub', 'b', 'a'),
      ins('move', 'a', 'x:(r7+$19)'), ins('asr', '#$c', 'a', 'a'), ins('add', 'a', 'b')] + (
      [ins('move', 'b1', 'x:(r7+$18)'), ins('move', 'b0', 'x:(r7+$14)')] if QZ else [ins('move', 'b', 'x:(r7+$18)')])
L += ['; main write: in * mono + v; out = soft clip(0.6 v +- side)',
      ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'a'), ins('asr', 'a'),
      ins('move', 'a', 'x0'), ins('move', 'x:(r7+$0d)', 'y1'), ins('mpy', 'x0', 'y1', 'a'),
      ins('move', 'x:(r7+$19)', 'x0'), ins('add', 'x0', 'a')] + mtz('a', 'rzqdw') + [ins('move', 'a', 'y:(r5)+'),
      ins('move', q(0.6), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
      ins('move', 'x:(r7+$12)', 'x1'), ins('move', 'a', 'b'), ins('sub', 'x1', 'b'),
      ins('move', 'b', 'x:(r7+$13)', c='right, before its clip'), ins('add', 'x1', 'a')] + softclip('a', 'a') + mix() + [
      ins('move', 'x:(r7+$13)', 'a')] + softclip('a', 'a') + mix()
L += ['rzdend:', ins('nop')] + restore(APS)

L += ['; ---- subroutines (after their callers: dsp_asm encodes no backward bsr) ----'] + SUB
Path(OUTF or Path(__file__).with_name('reso.asm')).write_text(finish(L))
print('wrote reso.asm')
