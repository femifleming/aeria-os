#!/usr/bin/env python3
"""Generates comp.asm (AERIA COMP: stereo-linked compander, port of Cloudfin's Compander.h)."""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, finish, e2_sub, snap_close  # noqa: E402

DB = 20 * math.log10(2)            # dB per log2 unit, 6.0206
OFF = 9                            # gain word = 2^-(g + OFF): room for +54 dB
YMAX = 120 / DB / 32               # detector floor, -120 dBFS
EXPMAX = 80 / DB / 32              # expansion floor, -80 dB
UPMAX = 30 / DB / 32               # upward cap, +30 dB
K_THR = 60 / DB * 128 / 127 / 32
K_RAT = math.log2(20) * 128 / 127 / 32
K_MAK = 24 / DB * 128 / 127 / 32
K_ATK = math.log2(4000) * 128 / 127 / 32
Y_ATK = math.log2(0.05e-3 * 44100) / 32
K_REL = math.log2(400) * 128 / 127 / 32
Y_REL = math.log2(5e-3 * 44100) / 32


def fit_log():
    """q(z) = -log2((1 + z)/2)/32 on z in [0,1], degree 5, exact at both ends
    (continuous across octaves). Least squares on Chebyshev nodes."""
    import numpy as np
    n = 400
    z = 0.5 - 0.5 * np.cos(np.pi * (np.arange(n) + 0.5) / n)
    f = (1 - np.log2(1 + z)) / 32
    # q = (1 - z)/32 + z (1 - z) (a0 + a1 z + a2 z^2 + a3 z^3)
    base = (1 - z) / 32
    A = np.stack([z * (1 - z) * z ** k for k in range(4)], axis=1)
    a, *_ = np.linalg.lstsq(A, f - base, rcond=None)
    c = np.zeros(6)
    c[0] = 1 / 32
    c[1] = -1 / 32
    for k in range(4):
        c[k + 1] += a[k]
        c[k + 2] -= a[k]
    zz = np.linspace(0, 1, 20001)
    err = np.max(np.abs(np.polyval(c[::-1], zz) - (1 - np.log2(1 + zz)) / 32)) * 32
    assert err < 5e-5, err
    assert np.polyval(c[::-1], -1.0) > -0.3      # env = 0 lands on the floor clamp
    return [float(v) for v in c], float(err)


C, LOGERR = fit_log()

H = f'''; CYCLES_FORWARD_BRANCHES
; AERIA COMP -- stereo-linked compander (Cloudfin Compander.h in fixed point).
; No buffer: state only in the r7 block. Everything per sample.
;
;  pk  = max(|L|, |R|);  env += (pk > env ? atk : rel) (pk - env), 48-bit env
;  Y   = -log2(env)/32: five forward-branch normalise stages (16 8 4 2 1) on
;        the 48-bit env, then a degree-5 polynomial in z = 2M - 1
;        (max error {LOGERR:.1e} log2 units = {LOGERR * DB:.4f} dB); floor -120 dBFS.
;  d   = Y - Yt  (Yt = -log2(threshold)/32;  d < 0 above the threshold)
;  above:  g = -d s,  s = 1 - 1/ratio                     (attenuation, log2/32)
;  below:  g = clamp(8 d kb, -30 dB, +80 dB) * fade
;          BELW right: kb = 7 bel / 8, fade = 1        (downward expansion)
;          BELW left : kb = -(1 - 1/ru)/8, ru = 1 + 9 |bel|, fade =
;                      clamp((dBFS + 100)/20, 0, 1)    (upward compression)
;  w   = g + ({OFF} - makeup)/32;  G = 2^-frac(32 w) * T[int(32 w)]
;        T[n] = 2^-n (n < {OFF}: output = sat(x G << {OFF}))
;        T[n] = 2^-(n - {OFF}) (n >= {OFF}: output = x G)
;  The x{1 << OFF} path is asl 8 in 56 bits, limit, x2, limit: no wrap.
;  MIX 0 = exact dry, MIX 127 = exact wet (dry + m (wet - dry) between).
;
; r7: 00/01 env hi/lo  02 atk  03 rel  04 Yt  05 s  06 kb  07 fa  08 fb
;     09 ({OFF} - makeup)/32  0a/0b mix t/s  0c path (n - {OFF})
;     0d-0f THR / RATI / MAKE glided (used by the loop)  10-12 their per-sample steps  13 first-call flag
;     14 MIX step  15/16 BELW kb glided / step
;     20..37 T[0..23]
'''
ENV, ENVL, ATK, REL, YT, SL, KB, FA, FB, CO, MT, MS, PATH = range(13)
YTC, SLC, COC, YTI, SLI, COI, FIRST, MI, KBC, KBI = range(13, 23)   # glided THR / RATI / MAKE / MIX / BELW: current, per-sample step; first-call flag
GLIDE = ((YT, YTC, YTI), (SL, SLC, SLI), (CO, COC, COI), (MT, MS, MI), (KB, KBC, KBI))
TBL = 0x20


def s(slot):
    return f'x:(r7+${slot:02x})'


L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#64', '>cpizero'), ins('move', 'a', 'x:(r4)+'), 'cpizero:', ins('nop'),
     ins('lua', f'(r7+${TBL:02x})', 'r4'), ins('move', '#>$7fffff', 'x0'),
     ins('move', 'x0', 'x:(r4)+', c='T[0] = 1'), ins('move', '#>$400000', 'a'),
     ins('do', f'#{OFF - 1}', '>cpitba'), ins('move', 'a', 'x:(r4)+'), ins('asr', 'a'), 'cpitba:',
     ins('move', 'x0', 'x:(r4)+', c=f'T[{OFF}] = 1'), ins('move', '#>$400000', 'a'),
     ins('do', f'#{23 - OFF}', '>cpitbb'), ins('move', 'a', 'x:(r4)+'), ins('asr', 'a'), 'cpitbb:',
     ins('nop'), ins('move', '#>$1', 'a'), ins('move', 'a', s(FIRST), c='first call: no glide'), ins('rts')]

# ---------------------------------------------------------------- knobs ----
L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm5'),
      ins('move', '#>$1', 'n0'),
      # MIX
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'cpmset'), ins('move', '#>$7fffff', 'a'), 'cpmset:', ins('move', 'a', s(MT))]
L += ['; THR: Yt = 60 dB * v/127 in log2/32',
      ins('move', 'x:(r6)', 'x0'), ins('move', q(K_THR), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'a', s(YT)),
      '; RATI: s = 1 - 20^-(v/127); 0 -> exactly 1:1',
      ins('move', 'x:(r6+$1)', 'a'), ins('tst', 'a'), ins('beq', 'cprset'),
      ins('move', 'a', 'x0'), ins('move', q(K_RAT), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('bsr', 'cpexp'), ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'),
      ins('move', '#>$0', 'x0'), ins('tst', 'a'), ins('tmi', 'x0', 'a'),
      'cprset:', ins('move', 'a', s(SL)),
      '; BELW: 64 off, right expansion, left upward',
      ins('move', q(1 / 8), 'a'), ins('move', 'a', s(FA)), ins('clr', 'a'), ins('move', 'a', s(FB)),
      ins('move', 'x:(r6+$2)', 'a'), ins('sub', q(0.5), 'a', c='(v - 64)/128'),
      ins('beq', 'cpbset'), ins('blt', 'cpbup'),
      ins('move', 'a', 'x0'), ins('move', q(128 / 63 * 7 / 8 / 2), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('asl', 'a', c='kb = 7 bel / 8, bel = (v - 64)/63'), ins('bra', 'cpbset'),
      'cpbup:', ins('neg', 'a'), ins('move', 'a', 'x0'), ins('move', q(9 / 16), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a', c='9 b / 16, b = (64 - v)/64'),
      ins('move', 'a', 'b'), ins('add', q(1 / 16), 'b'), ins('move', 'b', 'x0', c='(1 + 9 b)/16'),
      ins('move', 'a1', 'a', c='clean 48-bit dividend'),
      ins('andi', '#$fe', 'ccr'), ins('do', '#24', '>cpbdiv'), ins('div', 'x0', 'a'), 'cpbdiv:', ins('nop'),
      ins('move', 'a0', 'b', c='1 - 1/ru'), ins('asr', '#$3', 'b', 'b'), ins('move', 'b1', 'b'),
      ins('neg', 'b'), ins('move', 'b', 'a', c='kb = -(1 - 1/ru)/8'),
      ins('move', q(5 / 16), 'b'), ins('move', 'b', s(FA)),
      ins('move', q(-DB * 32 / 20 / 16), 'b'),
      ins('move', 'b', s(FB), c='fade = 16 (5/16 - (32 DB/20/16) Y)'),
      'cpbset:', ins('move', 'a', s(KB)),
      '; MAKE (page 2, knob 6): (OFF - 24 dB * v/127)/32',
      ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$ff0000', 'a'), ins('move', 'a', 'x0'),
      ins('move', q(K_MAK), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(OFF / 32), 'b'),
      ins('sub', 'a', 'b'), ins('move', 'b', s(CO)),
      '; ATK: u = 1/(T fs), T = 0.05 ms * 4000^(v/127); coef = 1 - e^-u',
      ins('move', 'x:(r6+$3)', 'x0'), ins('move', q(K_ATK), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('add', q(Y_ATK), 'a'), ins('bsr', 'cpexp'), ins('bsr', 'cpser'), ins('move', 'b', s(ATK)),
      '; REL: T = 5 ms * 400^(v/127)',
      ins('move', 'x:(r6+$4)', 'x0'), ins('move', q(K_REL), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('add', q(Y_REL), 'a'), ins('bsr', 'cpexp'), ins('bsr', 'cpser'), ins('move', 'b', s(REL)),
      '; THR / RATI / MAKE / MIX / BELW glide: a piecewise-linear one-pole, 1/128 of the distance a sample (tau 2.9 ms),',
      '; the step set once a call (no zipper, no click on a jump); the first call lands on the targets',
      ins('move', s(FIRST), 'a'), ins('tst', 'a'), ins('beq', 'cpgfirst')]
for t, c_, i_ in GLIDE:
    L += [ins('move', s(t), 'a'), ins('move', 'a', s(c_))]
L += [ins('clr', 'a'), ins('move', 'a', s(FIRST)), 'cpgfirst:']
for k, (t, c_, i_) in enumerate(GLIDE):
    L += snap_close(t, c_, f'cpgsnap{k}')
    L += [ins('move', s(t), 'a'), ins('move', s(c_), 'b'), ins('sub', 'b', 'a'), ins('asr', '#$7', 'a', 'a'),
          ins('move', 'a1', s(i_))]
L += [ins('lua', f'(r7+${TBL:02x})', 'r4')]

# ---------------------------------------------------------- sample loop ----
L += [ins('do', 'n7', '>cpend')]
for t, c_, i_ in GLIDE:
    L += [ins('move', s(c_), 'a'), ins('move', s(i_), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', s(c_))]
L += ['; ---- peak of the pair, envelope (48 bits) ----',
      ins('move', 'x:(r0)', 'a'), ins('abs', 'a'),
      ins('move', 'x:(r0+n0)', 'b'), ins('abs', 'b'), ins('cmp', 'b', 'a'), ins('tlt', 'b', 'a', c='pk'),
      ins('move', s(ENV), 'b'), ins('move', s(ENVL), 'b0'), ins('sub', 'b', 'a'),
      ins('move', 'a', 'x1', c='pk - env'), ins('move', s(REL), 'y1'), ins('move', s(ATK), 'y0'),
      ins('tst', 'a'), ins('move', 'y1', 'a'), ins('tgt', 'y0', 'a'), ins('move', 'a', 'y0'),
      ins('mpy', 'x1', 'y0', 'a'), ins('add', 'a', 'b'),
      ins('move', '#>$0', 'x0'), ins('tst', 'b'), ins('tmi', 'x0', 'b'),
      ins('move', 'b1', s(ENV)), ins('move', 'b0', s(ENVL)),
      '; ---- Y = -log2(env)/32: normalise b to [0.5, 1), count in a ----',
      ins('clr', 'a')]
for k in (16, 8, 4, 2, 1):
    L += [ins('cmp', f'#>${1 << (23 - k):06x}', 'b'), ins('bge', f'cpn{k:02d}'),
          ins('asl', f'#${k:x}', 'b', 'b'), ins('add', f'#>${k << 18:06x}', 'a'), f'cpn{k:02d}:']
L += [ins('asl', 'b'), ins('move', '#>$800000', 'x0'), ins('add', 'x0', 'b'),
      ins('move', 'b', 'x0', c='z = 2M - 1'), ins('move', q(C[5]), 'y0'), ins('mpy', 'y0', 'x0', 'b')]
for k in (4, 3, 2, 1):
    L += [ins('add' if C[k] >= 0 else 'sub', q(abs(C[k])), 'b'), ins('move', 'b', 'y0'),
          ins('mpy', 'y0', 'x0', 'b')]
L += [ins('add', q(C[0]), 'b'), ins('add', 'b', 'a'),
      ins('move', q(YMAX), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a', c='floor -120 dBFS'),
      '; ---- gain computer ----',
      ins('move', 'a', 'x1', c='Y'), ins('move', s(YTC), 'x0'), ins('sub', 'x0', 'a'),
      ins('move', 'a', 'x0', c='d'),
      ins('move', s(FB), 'y0'), ins('move', s(FA), 'b'), ins('mac', 'x1', 'y0', 'b'),
      ins('asl', '#$4', 'b', 'b'), ins('move', 'b', 'y1'), ins('move', 'y1', 'b'),
      ins('move', '#>$0', 'y0'), ins('tst', 'b'), ins('tmi', 'y0', 'b'), ins('move', 'b', 'y1', c='fade'),
      ins('move', s(KBC), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('asl', '#$3', 'b', 'b'),
      ins('move', q(EXPMAX), 'x1'), ins('cmp', 'x1', 'b'), ins('tgt', 'x1', 'b'),
      ins('move', q(-UPMAX), 'x1'), ins('cmp', 'x1', 'b'), ins('tlt', 'x1', 'b'),
      ins('move', 'b', 'x1'), ins('mpy', 'y1', 'x1', 'b', c='below: g'),
      ins('move', s(SLC), 'y1'), ins('mpy', 'x0', 'y1', 'a'), ins('neg', 'a'),
      ins('move', 'a', 'y0', c='above: g = -d s'),
      ins('move', 'x0', 'a'), ins('tst', 'a'), ins('tlt', 'y0', 'b'),
      ins('move', s(COC), 'x0'), ins('add', 'x0', 'b', c='w'),
      ins('move', '#>$0', 'x0'), ins('tst', 'b'), ins('tmi', 'x0', 'b'),
      ins('move', q(23.999 / 32), 'x0'), ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b'),
      '; ---- G = 2^-frac * T[int] ----',
      ins('move', 'b', 'a'), ins('move', 'a1', 'b'),
      ins('asr', '#$12', 'a', 'a'), ins('move', 'a1', 'n4', c='n'), ins('move', 'a1', 'x1'),
      ins('and', '#>$03ffff', 'b'), ins('asl', '#$5', 'b', 'b'), ins('move', 'b1', 'x0'),
      ins('move', q(math.log(2) / 2), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('move', 'b', 'y0'),
      ins('move', q(1 / 48), 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(1 / 12), 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.25), 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(0.5), 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.5), 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'x0', 'x0', 'b'), ins('asl', '#$2', 'b', 'b'),
      ins('move', 'b', 'x0', c='2^-frac in (0.5, 1]'),
      ins('lua', '(r4+n4)', 'r5'), ins('move', 'x1', 'a'), ins('sub', f'#>${OFF:x}', 'a'),
      ins('move', 'a', s(PATH), c='n - OFF: >= 0 is the no-shift path'),
      ins('move', 'x:(r5)', 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('move', 'b', 'y1', c='G')]
for ch in 'LR':
    L += [f'; ---- {ch} ----',
          ins('move', 'x:(r0)', 'x0'), ins('mpy', 'x0', 'y1', 'a'), ins('move', 'a', 'x1', c='x G'),
          ins('asl', '#$8', 'a', 'a'), ins('move', 'a', 'y0', c='limit'), ins('move', 'y0', 'a'),
          ins('asl', 'a'), ins('move', 'a', 'y0', c=f'sat(x G << {OFF})'),
          ins('move', s(PATH), 'b'), ins('tst', 'b'), ins('move', 'y0', 'a'), ins('tge', 'x1', 'a'),
          ins('move', 'a', 'x1', c='wet'),
          ins('move', s(MS), 'y0'), ins('move', 'x:(r0)', 'b'), ins('sub', 'b', 'a'), ins('asr', 'a'),
          ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'), ins('add', 'b', 'a'),
          ins('move', 'y0', 'b'), ins('cmp', '#>$7fffff', 'b'), ins('teq', 'x1', 'a', c='MIX 127: wet exactly'),
          ins('move', 'a', 'x:(r0)+')]
L += ['cpend:', ins('nop'), ins('rts')]

# ---------------------------------------------------------- subroutines ----
L += ['; ---- subroutines (after their callers) ----',
      '; b = 1 - e^-u for b = u: u (1 - u/2 (1 - u/3 (1 - u/4)))',
      'cpser:', ins('move', 'b', 'x0'), ins('move', q(0.25), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1'), ins('move', q(1 / 3), 'y0'),
      ins('mpy', 'x1', 'y0', 'a'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asr', 'a'),
      ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'y0'),
      ins('mpy', 'y0', 'x0', 'b'), ins('rts')]
L += e2_sub('cpexp', 'cpxend')
Path(__file__).with_name('comp.asm').write_text(finish(L))
print(f'wrote comp.asm (log2 poly max err {LOGERR:.2e} log2 units = {LOGERR * DB:.5f} dB)')
