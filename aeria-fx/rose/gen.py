#!/usr/bin/env python3
"""Generates rose.asm and rose_table.json (AERIA ROSE: rose-window / Chladni modal resonator).

The geometry -> frequency mapping lives here (sets()); the DSP only reads a P table of
log2(ratio)/32 words, 8 per (TUNE, SHAP, FOLD). See README.md for the mapping in prose.
Runs on the host (no dependencies): python3 rose/gen.py
"""
import json
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, softclip, mix, finish, e2_sub, snap_close  # noqa: E402

FS = 44100.0
FMAX = 4000.0                      # modes above this fold down by octaves (runtime, root-dependent)
WMAX = 2 * math.pi * FMAX / FS     # 0.5699 rad
C1 = 440 * 2 ** ((24 - 69) / 12)   # 32.703 Hz, ROOT 0
YR0 = math.log2(FMAX / C1)         # 6.935 octaves from C1 up to FMAX
NROOT = 61                         # C1 .. C6
T60_MIN, T60_MAX = 0.05, 12.0      # DECY range (seconds to -60 dB)
LOG2_1000 = math.log2(1000)
Y0MIN = -math.log2(LOG2_1000 / (T60_MIN * FS))          # -log2(d) at DECY 0   (7.79)
KDEC = math.log2(T60_MAX / T60_MIN) / (127 / 128)        # per unit knob fraction
YDEC_LO, YDEC_HI = 2.7, 15.8      # per-mode clamp of -log2(d): d <= 0.154 .. >= 1.75e-5 (T60 <= 13 s)
LOG2_2LN2 = math.log2(2 * math.log(2))
CIN = 3                            # input gain c = 2^-3 (internal headroom), output x8
INCMAX = 3.0 * 2 * 16 / FS         # ROTA top: 3 Hz, phase circle = 2.0, per 16-frame block
NM = 8
import os
LEAKT = int(os.environ.get('LEAKT', 256))   # state words below this move LEAKK LSB toward 0, one mode per call
LEAKK = int(os.environ.get('LEAKK', 4))
SHAPES = ('HARM', 'PRIM', 'CIRC', 'SQRE', 'ROSE', 'GOTH')
FOLDS = tuple(range(3, 13))
TUNES = ('OPEN', 'CLSE')


# ----------------------------------------------------------------------------- geometry
def primes(n):
    out, k = [], 2
    while len(out) < n:
        if all(k % p for p in out if p * p <= k):
            out.append(k)
        k += 1
    return out


def besselj(m, x, n=400):
    """J_m(x) = 1/pi * int_0^pi cos(m t - x sin t) dt (trapezoid: spectrally exact here)."""
    h = math.pi / n
    s = 0.5 * (math.cos(0) + math.cos(m * math.pi))
    for i in range(1, n):
        t = i * h
        s += math.cos(m * t - x * math.sin(t))
    return s * h / math.pi


def bessel_zeros(m, count):
    out, x, step = [], max(0.5, m * 0.9), 0.05
    f0 = besselj(m, x)
    while len(out) < count:
        x1 = x + step
        f1 = besselj(m, x1)
        if f0 == 0 or (f0 < 0) != (f1 < 0):
            a, b, fa = x, x1, f0
            for _ in range(60):
                c = 0.5 * (a + b)
                fc = besselj(m, c)
                if (fa < 0) != (fc < 0):
                    b = c
                else:
                    a, fa = c, fc
            out.append(0.5 * (a + b))
        x, f0 = x1, f1
    return out


def dedupe(cands, n, tol=0.01, reduce=None):
    out, info = [], []
    for r, tag in cands:
        if reduce:
            while r >= reduce:
                r /= 2
        if all(abs(math.log(r / o)) > tol for o in out):
            out.append(r)
            info.append(tag)
        if len(out) == n:
            break
    if len(out) < n:
        raise SystemExit(f'only {len(out)} distinct ratios: {out}')
    pairs = sorted(zip(out, info))
    return [p[0] for p in pairs], [p[1] for p in pairs]


GOTHIC = {   # just-intonation chord stacks (ratios over ROOT), one per FOLD
    3: ('organum, 3-limit', (1, 3/2, 2, 3, 4, 9/2, 6, 8)),
    4: ('major, 5-limit', (1, 5/4, 3/2, 2, 5/2, 3, 4, 5)),
    5: ('minor, 5-limit', (1, 6/5, 3/2, 2, 12/5, 3, 4, 24/5)),
    6: ('major 7th', (1, 5/4, 3/2, 15/8, 2, 5/2, 3, 15/4)),
    7: ('harmonic 7th, 7-limit', (1, 5/4, 3/2, 7/4, 2, 5/2, 3, 7/2)),
    8: ('quartal, 3-limit', (1, 4/3, 16/9, 2, 8/3, 32/9, 4, 16/3)),
    9: ('major 9th', (1, 5/4, 3/2, 15/8, 9/4, 3, 15/4, 9/2)),
    10: ('minor 9th', (1, 6/5, 3/2, 9/5, 9/4, 12/5, 3, 18/5)),
    11: ('undecimal (11-limit) otonal', (1, 9/8, 5/4, 11/8, 3/2, 7/4, 2, 11/4)),
    12: ('13-limit otonal, harmonics 8..15', (1, 9/8, 5/4, 11/8, 3/2, 13/8, 7/4, 15/8)),
}


def candidates(shape, n):
    """Ordered (ratio, geometry tag) candidates; the first 8 distinct are the OPEN set."""
    if shape == 'HARM':
        # harmonics an n-fold rose couples: 1 and j*n +- 1 (r = cos(n t) is two phasors at n-1, n+1)
        hs = [1] + sorted({j * n + d for j in range(1, 40) for d in (-1, 1) if j * n + d > 1})
        return [(h, f'h{h}') for h in hs]
    if shape == 'PRIM':
        ps = primes(n + 40)
        return [(1, 'root')] + [(p, f'p{p}') for p in ps[n - 3:]]
    if shape == 'CIRC':
        ms = (0, n, 2 * n, 3 * n)
        modes = [(j, m, s + 1) for m in ms for s, j in enumerate(bessel_zeros(m, 8))]
        modes.sort()
        j01 = modes[0][0]
        return [((j / j01) ** 2, f'm{m}s{s}') for j, m, s in modes]
    if shape == 'SQRE':
        vals = {}
        for a in range(1, 30):
            for b in range(1, a + 1):
                vals.setdefault(a * a + b * b, (a, b))
        # the plate fundamental (1,1) is ROOT; FOLD climbs the ladder of a^2 + b^2 (more nodal lines)
        ladder = [2] + sorted(vals)[n - 2:]
        return [(v / 2, f'{vals[v][0]},{vals[v][1]}') for v in ladder]
    if shape == 'ROSE':
        m2 = 2 * n
        chords = [math.sin(k * math.pi / m2) / math.sin(math.pi / m2) for k in range(1, n + 1)]
        prods = {1.0: 'c1'}
        frontier = dict(prods)
        for _ in range(6):
            nxt = {}
            for v, t in frontier.items():
                for k, c in enumerate(chords[1:], 2):
                    w = v * c
                    if w < 40 and not any(abs(math.log(w / u)) < 1e-9 for u in prods):
                        nxt[w] = (t + f'*c{k}') if t != 'c1' else f'c{k}'
            prods.update(nxt)
            frontier = nxt
        return sorted(prods.items())
    if shape == 'GOTH':
        rs = GOTHIC[n][1]
        return [(r, 'ji') for r in rs] + [(r * 2, 'ji') for r in rs] + [(r * 4, 'ji') for r in rs]
    raise ValueError(shape)


def sets():
    """{(tune, shape, fold): (ratios[8], tags[8])}"""
    out = {}
    for s in SHAPES:
        for n in FOLDS:
            c = candidates(s, n)
            out[('OPEN', s, n)] = dedupe(c, NM)
            # CLSE: every candidate octave-reduced into [1, 4); a set that runs short (the JI
            # stacks) continues with products of its own first 8 tones (its combination tones)
            base = [r for r, _ in c[:NM]]
            ext = sorted((a * b, 'x') for i, a in enumerate(base) for b in base[i:] if a > 1 and b > 1)
            ext += sorted((a * b * d, 'x') for a in base for b in base for d in base if min(a, b, d) > 1)
            out[('CLSE', s, n)] = dedupe(c + ext, NM, reduce=4.0)
    return out


SETS = sets()
TABLE = []
for t in TUNES:
    for s in SHAPES:
        for n in FOLDS:
            for r in SETS[(t, s, n)][0]:
                v = round(math.log2(r) / 32 * (1 << 23))
                TABLE.append(v & 0xffffff)
assert len(TABLE) == 2 * 6 * 10 * NM

# ----------------------------------------------------------------------------- DSP source
H = f'''; AERIA ROSE -- a modal resonator tuned by rose-window geometry and cymatics.
; Eight two-pole (coupled-form, complex one-pole) resonators, mono in, stereo out.
; Mode k's frequency is ROOT x ratio[TUNE][SHAP][FOLD][k] (P table: log2(ratio)/32,
; table first, its base named by the package literal), folded down by octaves
; above {FMAX:.0f} Hz. No delay buffer.
;
; Per main call (a != 0 at entry; the a = 0 split sub-call only runs the loop):
; a 16-call round robin, block 2k: mode k's frequency (2^-y, octave fold, sin/cos
; Taylor to w^7/w^8) and the input gain; block 2k+1: mode k's decay
; (r = 2^-d, d = 2^-(Y0 - beta*octaves), clamped) -> targets r cos w, r sin w,
; and its ROTA pane gain. Half the modes glide toward their targets each call
; (1/16 step), so SHAP / FOLD / ROOT changes retune smoothly, never click.
;
; Sample: mono = (L+R)/2 -> DC block -> u = g*mono; per mode
;   re' = u + rc re - rs im,  im' = rs re + rc im   (|r| < 1 - 2^-16.6)
; A = sum even G_k im_k, B = sum odd; L = A + B - s B, R = A + B - s A (s = SPRD);
; x8, soft clip (|y| <= 2/3), dry/wet; MIX 0 is exact dry.
;
; r7: 00 cnt 01/02 mix t/s 03 u 04 wet R 05/06 gin t/s 07/08 s t/s 09 rota phase
;     0a rota depth 0b dc lp 0c Yr/32 0d Y0/32 0e beta 10 1-cos 11 sin 12 oct/32
;     13 call flag 14 SPRD step a sample 15 tmp
; r1 = r7+$40: mode k at +8k: 0 re 1 im 2 rc 3 rs 4 G 5 rc' 6 rs' 7 G'
'''

L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#128', '>rszero'), ins('move', 'a', 'x:(r4)+'), 'rszero:', ins('nop'), ins('rts')]


def knob_int(src, mask, maxv):
    """a <- integer field (bits 16-23) of a knob word, clamped to maxv."""
    out = [ins('move', src, 'a')]
    if mask:
        out.append(ins('and', '#>$ff0000', 'a'))
    out += [ins('asr', '#$10', 'a', 'a'), ins('move', 'a1', 'a', c='drop the fraction asr left in a0'),
            ins('move', f'#>${maxv:x}', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a')]
    return out


L += ['proc:', ins('move', 'a', 'x:(r7+$13)', c='call flag: 0 = the split sub-call'),
      ins('move', '#>$ffffff', 'm1'), ins('move', '#>$ffffff', 'm2'), ins('move', '#>$ffffff', 'm3'),
      ins('move', '#>$ffffff', 'm4'),
      ins('move', 'r7', 'r1'), ins('move', '#>$40', 'n1'), ins('move', '(r1)+n1', c='r1 = mode block'),
      ins('move', '#>$1', 'n0'),
      # mix target
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'rsmset'), ins('move', '#>$7fffff', 'a'), 'rsmset:', ins('move', 'a', 'x:(r7+$01)')]
L += snap_close(0x01, 0x02, 'rsmsnap')
L += [ins('move', 'x:(r7+$13)', 'a'), ins('tst', 'a'), ins('beq', 'rsrun'),
      '; ---- knobs (main call only) ----',
      # ROOT -> Yr/32 = YR0/32 - word/3
      ins('move', 'x:(r6)', 'x0'), ins('move', q(1 / 3), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', q(YR0 / 32), 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x:(r7+$0c)'),
      # DECY -> Y0/32
      ins('move', 'x:(r6+$3)', 'x0'), ins('move', q(KDEC / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('add', q(Y0MIN / 32), 'a'), ins('move', 'a', 'x:(r7+$0d)'),
      # BRIT -> beta = 1 - 2x
      ins('move', 'x:(r6+$4)', 'x0'), ins('move', q(0.5), 'a'), ins('sub', 'x0', 'a'), ins('asl', 'a'),
      ins('move', 'a', 'x:(r7+$0e)'),
      # SPRD -> m = 1 - x
      ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$ff0000', 'a'), ins('move', '#>$7f0000', 'x0'),
      ins('cmp', 'x0', 'a'), ins('bne', 'rssset'), ins('move', '#>$7fffff', 'a'),
      'rssset:', ins('move', 'a', 'x:(r7+$07)', c='s = SPRD (127 = 1)'),
      # ROTA -> phase += INCMAX x^4 ; depth = 0.9 min(8x, 1)
      ins('move', 'x:(r6+$d)', 'a'), ins('and', '#>$ff0000', 'a'), ins('move', 'a', 'x0'),
      ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'y0'), ins('mpy', 'y0', 'y0', 'a'), ins('move', 'a', 'x0'),
      ins('move', q(INCMAX), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'x:(r7+$09)', 'b'),
      ins('add', 'a', 'b'), ins('move', 'b1', 'x:(r7+$09)', c='raw: wraps around the circle'),
      ins('move', 'x:(r6+$d)', 'a'), ins('and', '#>$ff0000', 'a'), ins('asl', '#$3', 'a', 'a'),
      ins('move', 'a', 'x0', c='limits at 1'), ins('move', q(0.9), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$0a)'),
      # round robin: cnt = (cnt + 1) & 15, r3 = mode block of k = cnt >> 1
      ins('move', 'x:(r7+$00)', 'a'), ins('add', '#>$1', 'a'), ins('and', '#>$00000f', 'a'),
      ins('move', 'a', 'x:(r7+$00)'), ins('move', 'a', 'b'), ins('and', '#>$00000e', 'b'),
      ins('asl', '#$2', 'b', 'b'), ins('move', 'b1', 'n3'), ins('move', 'r1', 'r3'), ins('move', '(r3)+n3'),
      ins('and', '#>$000001', 'a'), ins('bne', 'rsph1'),
      '; ---- even block: mode k frequency, input gain ----']
L += knob_int('x:(r6+$e)', True, 1) + [ins('move', 'a', 'b'), ins('asl', 'a'), ins('add', 'a', 'b'),
                                       ins('asl', 'b', c='6 TUNE')]
L += knob_int('x:(r6+$1)', False, 5) + [ins('add', 'a', 'b', c='v = 6 TUNE + SHAP'), ins('move', 'b', 'a'),
                                        ins('asl', '#$3', 'b', 'b'), ins('asl', 'a'), ins('add', 'a', 'b', c='10 v')]
L += knob_int('x:(r6+$2)', False, 9) + [
    ins('add', 'a', 'b'), ins('asl', '#$3', 'b', 'b'),
    ins('move', 'x:(r7+$00)', 'a'), ins('asr', 'a'), ins('move', 'a1', 'a'), ins('add', 'a', 'b', c='+ k'),
    ins('move', '#>$fab1e0', 'x0', c='the P table base (the package literal)'), ins('add', 'x0', 'b'),
    ins('move', 'b1', 'r2'), ins('move', 'p:(r2)', 'x0', c='log2(ratio)/32'),
    ins('move', 'x:(r7+$0c)', 'a'), ins('sub', 'x0', 'a', c='y = Yr - log2 ratio (/32)'),
    ins('tst', 'a'), ins('bge', 'rsfok'),
    ins('and', '#>$03ffff', 'a', c='above FMAX: keep the fraction (octave fold)'), ins('move', 'a1', 'a'),
    'rsfok:', ins('move', 'x:(r7+$0c)', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x:(r7+$12)', c='octaves over root'),
    ins('bsr', 'rsexp'), ins('move', 'b', 'x0'), ins('move', q(WMAX), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='w'),
    ins('bsr', 'rssico'),
    # input gain g = c sqrt(2 ln2 d0) = 2^-((Y0 - log2(2 ln2))/2 + CIN)
    ins('move', 'x:(r7+$0d)', 'a'), ins('asr', 'a'), ins('add', q((CIN - LOG2_2LN2 / 2) / 32), 'a'),
    ins('bsr', 'rsexp'), ins('move', 'b', 'x:(r7+$05)'),
    ins('bra', 'rsglide'),
    'rsph1:', '; ---- odd block: mode k decay, coefficients, pane gain ----',
    ins('move', 'x:(r7+$12)', 'x0'), ins('move', 'x:(r7+$0e)', 'y0'), ins('move', 'x:(r7+$0d)', 'a'),
    ins('mac', '-y0', 'x0', 'a', c='y = Y0 - beta * octaves'),
    ins('move', q(YDEC_LO / 32), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'),
    ins('move', q(YDEC_HI / 32), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
    ins('bsr', 'rsexp', c='b = d'),
    ins('move', 'b', 'x0'), ins('move', q(math.log(2)), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1', c='u = d ln2'),
    ins('move', 'a', 'x0'), ins('move', q(1 / 6), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(0.5), 'b'),
    ins('sub', 'a', 'b'), ins('move', 'b', 'x0'), ins('mpy', 'x1', 'x0', 'a'), ins('move', 'a', 'x0'),
    ins('mpy', 'x1', 'x0', 'a'), ins('move', 'x1', 'b'), ins('sub', 'a', 'b', c='1 - r = u - u^2/2 + u^3/6'),
    ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'), ins('move', 'a', 'y1', c='r'),
    ins('move', '#>$7fffff', 'a'), ins('move', 'x:(r7+$10)', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x0'),
    ins('mpy', 'x0', 'y1', 'a'), ins('move', 'a', 'x:(r3+$5)', c="rc'"),
    ins('move', 'x:(r7+$11)', 'x0'), ins('mpy', 'x0', 'y1', 'a'), ins('move', 'a', 'x:(r3+$6)', c="rs'"),
    # pane gain: delta = phase - k/4 (circle = 2.0, raw wrap), t = 1 - |delta|,
    # G = 0.5 (1 - D) + D t^4
    ins('move', 'n3', 'a'), ins('asl', '#$12', 'a', 'a'), ins('move', 'a1', 'x0'),
    ins('move', 'x:(r7+$09)', 'a'), ins('sub', 'x0', 'a'), ins('move', 'a1', 'x0'), ins('move', 'x0', 'a'),
    ins('abs', 'a'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
    ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'y0'), ins('mpy', 'y0', 'y0', 'a'), ins('move', 'a', 'x0'),
    ins('move', 'x:(r7+$0a)', 'y0'), ins('mpy', 'y0', 'x0', 'a', c='D t^4'), ins('move', q(0.5), 'b'),
    ins('move', q(0.5), 'x1'), ins('mac', '-x1', 'y0', 'b'), ins('add', 'a', 'b'),
    ins('move', 'b', 'x:(r3+$7)', c="G'"),
    'rsglide:', '; ---- glide half the modes (cnt & 1 picks which) toward their targets ----',
    ins('move', 'x:(r7+$00)', 'a'), ins('and', '#>$000001', 'a'), ins('asl', '#$5', 'a', 'a'),
    ins('move', 'a1', 'n4'), ins('move', 'r1', 'r4'), ins('move', '(r4)+n4'), ins('move', '#>$8', 'n4'),
    ins('do', '#4', '>rsgend')]
for cur, tgt in ((2, 5), (3, 6), (4, 7)):
    L += [ins('move', f'x:(r4+${tgt:x})', 'a'), ins('move', f'x:(r4+${cur:x})', 'b'), ins('sub', 'b', 'a'),
          ins('asr', '#$5' if cur == 4 else '#$4', 'a', 'a', c='pane gain: 1/32 (smaller steps a call)' if cur == 4 else ''), ins('add', 'a', 'b'), ins('move', 'b', f'x:(r4+${cur:x})')]
L += [ins('move', '(r4)+n4'), 'rsgend:', ins('nop')]
L += ['; ---- mode k (r3, this call\'s round-robin mode): its state words below the threshold move',
      ';      1 LSB toward 0 (every 8th call): the rounded recursion\'s limit cycles end in exact silence',
      ins('move', f'#>${LEAKK:06x}', 'x0'), ins('move', f'#>${(-LEAKK) & 0xffffff:06x}', 'x1'), ins('move', f'#>${LEAKT:06x}', 'y0'), ins('move', '#>$0', 'y1')]
for st in (0, 1):
    L += [ins('move', f'x:(r3+${st:x})', 'a'), ins('clr', 'b'), ins('tst', 'a'), ins('tgt', 'x0', 'b'), ins('tlt', 'x1', 'b'),
          ins('cmpm', 'y0', 'a'), ins('tge', 'y1', 'b', c=f'only below {LEAKT} LSB'),
          ins('cmpm', 'x0', 'a'), ins('tlt', 'a', 'b', c=f'below {LEAKK}: to exactly 0 (no overshoot)'),
          ins('sub', 'b', 'a'), ins('move', 'a', f'x:(r3+${st:x})')]
L += smooth(0x05, 0x06, 3)
L += snap_close(0x07, 0x08, 'rsssnap')
L += ['; SPRD: per-sample linear steps (1/128 of the distance a sample: no per-call jump in the L/R mix)',
      ins('move', 'x:(r7+$07)', 'a'), ins('move', 'x:(r7+$08)', 'b'), ins('sub', 'b', 'a'), ins('asr', '#$7', 'a', 'a'),
      ins('move', 'a1', 'x:(r7+$14)')]

# ---------------------------------------------------------------- the sample loop
L += ['rsrun:', ins('do', 'n7', '>rsend')]
L += smooth(0x01, 0x02, 7)
L += [ins('move', 'x:(r7+$08)', 'a'), ins('move', 'x:(r7+$14)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'x:(r7+$08)')]
L += [ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'a'), ins('asr', 'a', c='mono'),
      ins('move', 'x:(r7+$0b)', 'b'), ins('sub', 'b', 'a'), ins('move', 'a', 'y0'), ins('asr', '#$a', 'a', 'a'),
      ins('add', 'a', 'b'), ins('move', 'b', 'x:(r7+$0b)', c='DC block, 7 Hz'),
      ins('move', 'x:(r7+$06)', 'x0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$03)', c='u')]
for k in range(NM):
    o = 8 * k
    L += [f'; mode {k}']
    if k:
        L.append(ins('move', 'x:(r7+$03)', 'a'))
    L += [ins('move', f'x:(r1+${o:02x})', 'x0'), ins('move', f'x:(r1+${o + 2:02x})', 'y0'),
          ins('mac', 'y0', 'x0', 'a'), ins('move', f'x:(r1+${o + 1:02x})', 'x1'),
          ins('move', f'x:(r1+${o + 3:02x})', 'y1'), ins('macr', '-y1', 'x1', 'a'),
          ins('mpy', 'x0', 'y1', 'b'), ins('macr', 'x1', 'y0', 'b'),
          ins('move', 'a', f'x:(r1+${o:02x})'), ins('move', 'b', f'x:(r1+${o + 1:02x})')]
for acc, ks in (('a', (0, 2, 4, 6)), ('b', (1, 3, 5, 7))):
    for j, k in enumerate(ks):
        L += [ins('move', f'x:(r1+${8 * k + 1:02x})', 'x0'), ins('move', f'x:(r1+${8 * k + 4:02x})', 'y0'),
              ins('mpy' if j == 0 else 'mac', 'y0', 'x0', acc)]
L += [ins('move', 'a', 'x0'), ins('move', 'b', 'x1'), ins('move', 'x0', 'a'), ins('add', 'x1', 'a'),
      ins('move', 'a', 'b', c='A + B from the 24-bit A, B: L and R identical at SPRD 0'),
      ins('move', 'x:(r7+$08)', 'y0'),
      ins('mac', '-x1', 'y0', 'a', c='L = A + B - s B'), ins('mac', '-y0', 'x0', 'b', c='R = A + B - s A'),
      ins('move', 'b', 'x:(r7+$04)'), ins('asl', '#$3', 'a', 'a')]
L += softclip('a', 'a') + mix(mixslot=0x02)
L += [ins('move', 'x:(r7+$04)', 'a'), ins('asl', '#$3', 'a', 'a')] + softclip('a', 'a') + mix(mixslot=0x02)
L += ['rsend:', ins('nop'), ins('rts')]

# ---------------------------------------------------------------- subroutines
L += ['; ---- subroutines (after their callers: dsp_asm encodes no backward bsr) ----']
L += e2_sub('rsexp', 'rsxpend')
L += ['rssico:', '; a = w -> x:$10 = 1 - cos w, x:$11 = sin w (Taylor to w^7 / w^8)',
      ins('move', 'a', 'x1'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'y0', c='W = w^2'),
      ins('move', q(1 / 5040), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 120), 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 6), 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
      ins('move', 'x1', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x:(r7+$11)'),
      ins('move', q(1 / 40320), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 720), 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 24), 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(0.5), 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x:(r7+$10)'), ins('rts')]

here = Path(__file__).resolve().parent
(here / 'rose.asm').write_text(finish(L))
doc = dict(fs=FS, fmax=FMAX, root_c1=C1, nroot=NROOT, shapes=SHAPES, folds=FOLDS, tunes=TUNES,
           table=TABLE, gothic={str(k): v[0] for k, v in GOTHIC.items()},
           sets={f'{t}/{s}/{n}': dict(ratios=[round(r, 6) for r in SETS[(t, s, n)][0]], tags=SETS[(t, s, n)][1])
                 for t in TUNES for s in SHAPES for n in FOLDS})
(here / 'rose_table.json').write_text(json.dumps(doc, indent=1))
print('wrote rose.asm, rose_table.json (%d table words)' % len(TABLE))

# ----------------------------------------------------------------------------- ui_notes.md
FIG = {
    'HARM': 'rose r = cos(n t): pane k is harmonic h (tag hN); 1 and j*n +- 1 only',
    'PRIM': 'star / prime: pane k is prime harmonic p (tag pN); FOLD slides the window up the primes',
    'CIRC': 'circular Chladni plate: tag mMsS = M nodal diameters, S nodal circles; f ~ j(M,S)^2',
    'SQRE': 'square Chladni plate: tag A,B = mode (A,B); figure cos(A pi x)cos(B pi y) - cos(B pi x)cos(A pi y); f ~ A^2 + B^2',
    'ROSE': 'rose window tracery: tag cK = chord spanning K sides of the regular 2n-gon (side = 1); products = chords of chords',
    'GOTH': 'just-intonation chord stack (ratios over ROOT); the FOLD names the chord below',
}
U = ['# AERIA ROSE -- UI notes (generated by gen.py; do not edit)', '',
     'For the AERIA C UI: what to draw and which pane to light. Everything here comes from the same',
     '`SETS` the DSP table is built from, so the drawing and the sound cannot disagree.', '',
     '## Knob slots', '',
     '| slot | name | count | delivered to the DSP as | what |', '|---|---|---|---|---|',
     '| 0 | ROOT | 61 | x:(r6+$0) = value<<16 | C1 (32.70 Hz) .. C6, semitones; ROOT = ratio 1 |',
     '| 1 | SHAP | 6 | x:(r6+$1) | 0 HARM 1 PRIM 2 CIRC 3 SQRE 4 ROSE 5 GOTH |',
     '| 2 | FOLD | 10 | x:(r6+$2) | value + 3 = symmetry order n (3..12) |',
     '| 3 | DECY | 128 | x:(r6+$3) | T60 = 0.05 s * 240^(v/127), 50 ms .. 12 s |',
     '| 4 | BRIT | 128 | x:(r6+$4) | beta = 1 - 2 v/128: decay rate x 2^(beta * octaves above ROOT) |',
     '| 5 | MIX | 128 | x:(r6+$5) | dry/wet, 0 = exact dry |',
     '| 6 (p2) | SPRD | 128 | x:(r6+$c) bits 16-23 | 0 mono .. 127 even panes L / odd panes R |',
     '| 7 | (blank) | | | |',
     '| 8 (p2) | ROTA | 128 | x:(r6+$d) bits 16-23 | 0 off; depth 0.9 min(1, 8x), rate 3 Hz * x^4 (x = v/128) |',
     '| 9 | (blank) | | | |',
     '| 10 (p2) | TUNE | 2 | x:(r6+$e) bits 16-23 | 0 OPEN, 1 CLSE |',
     '| 11 | (blank) | | | |', '',
     '## Panes', '',
     '- Eight modes = eight panes. Pane k (k = 0..7) sits at angle k * 45 degrees round the window,',
     '  clockwise from the top. Ratios are sorted ascending, so pane 0 is always the lowest (ROOT, ratio 1',
     '  except where the family has no 1, which never happens here: every set starts at 1).',
     '- Stereo: even panes feed the left bus, odd panes the right (SPRD crossfades from mono).',
     '- Sounding frequency of pane k: f = ROOT_Hz * ratio, then halved while f > 4000 Hz (the DSP folds',
     '  above 4 kHz by octaves, root-dependent). Light a pane by that f if you draw a spectrum.',
     '- ROTA: the light centre runs round the circle at rate = 3 Hz * x^4 (one turn per 1/rate s) from',
     '  the angle it had; pane k gain = 0.5 (1 - D) + D t^4, t = 1 - |angle - k*45deg| / 180deg,',
     '  D = 0.9 min(1, 8x). The DSP phase is not published; a UI animation at the same rate will drift',
     '  from the sound in phase, never in speed.',
     '- Figure per family:', '']
U += [f'  - **{s}**: {FIG[s]}' for s in SHAPES]
U += ['', '## GOTH chord names', '', '| FOLD | chord |', '|---|---|']
U += [f'| {n} | {GOTHIC[n][0]} |' for n in FOLDS]
U += ['', '## P table layout (what the DSP reads)', '',
      'word index = ((TUNE * 6 + SHAP) * 10 + (FOLD - 3)) * 8 + k, value = round(log2(ratio) / 32 * 2^23),',
      f'{len(TABLE)} words, placed before the code (the package\'s ptable).', '',
      '## Ratio table (ratio over ROOT, geometry tag), per TUNE x SHAP x FOLD, panes 0..7', '']
for t in TUNES:
    U += [f'### TUNE {t}' + (' (ratios as the geometry gives)' if t == 'OPEN' else
                              ' (octave-reduced into [1, 4), duplicates within 1% skipped, then combination tones)'), '']
    for s in SHAPES:
        U += [f'#### {s}', '', '| n | ' + ' | '.join(f'pane {k}' for k in range(NM)) + ' |',
              '|---|' + '---|' * NM]
        for n in FOLDS:
            rs, tg = SETS[(t, s, n)]
            U.append(f'| {n} | ' + ' | '.join(f'{r:.4f} `{g}`' for r, g in zip(rs, tg)) + ' |')
        U.append('')
(here / 'ui_notes.md').write_text('\n'.join(U) + '\n')
print('wrote ui_notes.md')
