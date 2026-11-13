#!/usr/bin/env python3
"""Generates vowel.asm (AERIA VOWL: vowel formant filter, a fixed-point port of
Cloudfin's FormantFilter.h: five parallel TPT state-variable bandpasses per channel).

No P table and no buffer: the 75 vowel constants are written into the r7 block by init.
Runs on the host (no dependencies): python3 vowel/gen.py
Importing this module (test.py does) only builds the constants; nothing is written.
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, e2_sub, snap_close  # noqa: E402

FS = 44100.0
F, V = 5, 5
# tenor formants (Hz), relative levels (dB) and bandwidths (Hz): FormantFilter.h, verbatim
FR = ((650, 1080, 2650, 2900, 3250),    # a
      (400, 1700, 2600, 3200, 3580),    # e
      (290, 1870, 2800, 3250, 3540),    # i
      (400, 800, 2600, 2800, 3000),     # o
      (350, 600, 2700, 2900, 3300))     # u
DB = ((0, -6, -7, -8, -22),
      (0, -14, -12, -14, -20),
      (0, -15, -18, -20, -30),
      (0, -10, -12, -12, -26),
      (0, -20, -17, -14, -26))
BW = ((80, 90, 120, 130, 140),
      (70, 80, 100, 120, 120),
      (40, 90, 100, 120, 120),
      (40, 80, 100, 120, 120),
      (40, 60, 100, 120, 120))
VOWELS = 'aeiou'

RES_LO, RES_HI = 0.1, 8.0
QMIN, QMAX = 0.5, 120.0            # the plugin's clamp on hz / bandwidth
KRES = math.log2(RES_HI / RES_LO) * 128 / 127      # log2(q) = log2(0.1) + KRES * (knob / 128)
FREF = 16384.0                     # w/2 = W0H * 2^-(14 - log2 f)
W0H = math.pi * FREF / FS / 2      # 0.5836
XSHIFT = 2                         # input headroom: the filters run on x / 4
GSHIFT = 5                         # level-match gain is held as gain / 32
POW_TC = 0.25                      # plugin: 250 ms power, 80 ms gain
GAIN_COEF = 1 - math.exp(-1 / (0.08 * FS))
EPS_ROOT = 66                      # (66 / 2^23)^2 = 6.19e-11 = 0.99e-9 / 16 (plugin: 1e-9 on full scale)
YG_MAX = GSHIFT - math.log2(0.1)   # gain floor 0.1 -> y = 8.32; gain cap 32 -> y = 0
PSMOOTH = 6                        # knob glide: 2^-6 per block of 16 (23 ms; plugin: 30 ms ramp)
WIDE_CAP = 2 ** (1 / 12) - 1       # WIDE: a formant moves by at most a semitone (5.9 % of its frequency) ...
WIDE_X = 2.0                       # ... and by at most WIDE_X half-bandwidths (the two sides' peaks 2 bandwidths apart)


# ------------------------------------------------------------------ reference (float) model
def knob_vowel(k):
    return min(4.0, k * 4 / 127)


def knob_semis(k):
    return 24.0 if k >= 127 else (k - 64) * 0.375


def knob_res(k):
    return RES_LO * (RES_HI / RES_LO) ** (k / 127)


def formants(vowel_knob, shift_knob=64, res_knob=67):
    """[(hz, Q, amp)] x 5 exactly as FormantFilter::updateCoefs computes them."""
    v, mul, qq = knob_vowel(vowel_knob), 2 ** (knob_semis(shift_knob) / 12), knob_res(res_knob)
    i0 = min(int(v), V - 2)
    t = v - i0
    out = []
    for f in range(F):
        hz = math.exp(math.log(FR[i0][f]) + t * (math.log(FR[i0 + 1][f]) - math.log(FR[i0][f]))) * mul
        b = (BW[i0][f] + t * (BW[i0 + 1][f] - BW[i0][f])) * math.sqrt(mul) / qq
        a = 10 ** ((DB[i0][f] + t * (DB[i0 + 1][f] - DB[i0][f])) / 20)
        out.append((hz, min(QMAX, max(QMIN, hz / b)), a))
    return out


# ------------------------------------------------------------------ constants in the r7 block
# row = vowel, 3 words per formant: log2(hz)/16, bandwidth/256, |dB| * log2(10)/20 / 32
TABLE = []
for v in range(V):
    for f in range(F):
        TABLE += [math.log2(FR[v][f]) / 16, BW[v][f] / 256, -DB[v][f] * math.log2(10) / 20 / 32]
ROW = 3 * F


def _logfit(deg=5):
    """c[] with sum c_k u^k ~ 0.5 log2(1 + u) on [0, 1] (interpolation at Chebyshev nodes)."""
    n = deg + 1
    xs = [0.5 + 0.5 * math.cos(math.pi * (2 * i + 1) / (2 * n)) for i in range(n)]
    m = [[x ** k for k in range(n)] + [0.5 * math.log2(1 + x)] for x in xs]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(m[r][i]))
        m[i], m[p] = m[p], m[i]
        for r in range(n):
            if r != i:
                fac = m[r][i] / m[i][i]
                m[r] = [a - fac * b for a, b in zip(m[r], m[i])]
    c = [m[i][n] / m[i][i] for i in range(n)]
    err = max(abs(sum(ck * (j / 2000) ** k for k, ck in enumerate(c)) - 0.5 * math.log2(1 + j / 2000))
              for j in range(2001))
    return c, err


LOGC, LOGERR = _logfit()
assert LOGERR < 2e-5 and all(abs(c) < 0.999 for c in LOGC), (LOGC, LOGERR)

# ------------------------------------------------------------------ r7 slots
FLAG, MIXT, MIXS = 0x00, 0x01, 0x02
VT, VS, ST, SS, RT, RS = 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
TT, YS, YQ, LH, BWI = 0x09, 0x0a, 0x0b, 0x0c, 0x0d
SN, OC, PP, S2 = 0x0e, 0x0f, 0x10, 0x12
GT, GS, GSLO = 0x13, 0x14, 0x15
PIN, PINLO, POUT, POUTLO = 0x16, 0x17, 0x18, 0x19
XL, XR, OL, OR_, LG = 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
SIDE, FIRST, WS, HH = 0x11, 0x1a, 0x8e, 0x8f   # the side this call tunes (0 L, 1 R), first-call flag, -+WIDE, h
TAB, COEF, STATE = 0x20, 0x70, 0x90     # table base (r3 walks it), r1, r2
V1 = 5 * F                              # r1 + 25 + f: the block's v1 per formant
COEFR = STATE + 2 * F * 5               # r5: the right side's own coefficient block (5 x 5)
USED = COEFR + 5 * F
assert TAB + len(TABLE) <= COEF and COEF + V1 + F <= WS < HH < STATE and 2 * F * 5 <= 64 and USED <= 0x100

H = f'''; CYCLES_FORWARD_BRANCHES
; AERIA VOWL -- vowel formant filter (Cloudfin FormantFilter.h in 24-bit fixed point).
; Five parallel TPT state-variable bandpasses per channel; VOWL morphs a > e > i > o > u
; (log frequency, dB level, linear bandwidth), SHFT moves every formant -24..+24
; semitones (bandwidth x sqrt of the ratio), RES 0.1..8 narrows the bands, and an RMS
; level matcher (0.1 .. 32x, 250 ms power, 80 ms gain) holds the wet at the dry loudness.
;
; WIDE: every formant sits lower on the left and higher on the right, by the same fraction d of its
; frequency: d = WIDE x min({WIDE_CAP:.4f} (a semitone), {WIDE_X:g} kh) -- kh = 1/(2Q), so the two sides' peaks are
; never more than {WIDE_X:g} bandwidths apart, however narrow RES makes them. Around each formant the two
; sides then differ in phase by up to +-{math.degrees(math.atan(WIDE_X)):.0f} degrees (that is the width); a tone exactly between the two
; peaks still sums in mono to within {-20 * math.log10(2 / math.sqrt(1 + WIDE_X ** 2)):.1f} dB of either side (2 / sqrt(1 + x^2), x <= {WIDE_X:g}), and noise
; through a formant correlates 1 / (1 + x^2) between the sides. Each side has its own coefficient
; block; a call tunes ONE side (they take turns: each is retuned every second call, so the cost
; is that of the one shared set this effect had). The states, the level matcher (one gain for both
; sides: the image does not wander with level) and the knob glides are as before.
; WIDE 0: both blocks hold the same numbers (L = R on equal inputs once the knobs are at rest).
; Per call (block of n7 frames), all five formants of one side, as the plugin does every 16 samples:
;   knobs glide 2^-{PSMOOTH} per call; v = VOWL * 4/127; row i0 = int(v), t = frac(v)
;   log2 f = lerp(row, row+1; t) + semis/12          (table word = log2(hz)/16)
;   h = w/2 = {W0H:.4f} * 2^-(14 - log2 f);  sin h, cos h by Taylor (h <= 0.51)
;   sw = 2 sh ch, cw = 1 - 2 sh^2;  P = sw cw, S2 = sw^2
;   kh = k/2 = lerp(bw)/256 * 2^(7 - log2 q - log2 f0 - semis/24), clamped 1/240 .. 1
;   1/D, D = 1 + k P, by three Newton steps;  a2 = P/D, a3 = S2/D, c1 = a3 + k a2
;       (the plugin's g a1, g a2 and 1 - a1 with g = tan w: no coefficient exceeds 1)
;   amp = 2^-(lerp(|dB|) log2(10)/20)
;   gain target: 2^-(5 - (log2(pin+eps) - log2(pout+eps))/2), y clamped 0 .. {YG_MAX:.3f}
; Per sample, per channel, per formant, on x/4 with the states scaled by kh
; (s = kh ic, so every state stays near the input's size at any Q) and held in 48 bits.
; Each state product is taken in double precision: the multiplicand is split into its
; rounded high word and signed low word, the low-word products are shifted down 24 bits
; (a 24-bit multiplicand leaves a dead band of ~Q LSB that rings on after the input stops):
;   v3 = kh x/4 - s2;  s2 += 2 (a2 s1 + a3 v3);  t = a2 v3 - c1 s1;  v1 = s1 + t;  s1 += 2 t
;   wet/4 = 2 sum amp v1;  out = sat(wet/4 * gain/32 * 128);  dry/wet; MIX 0 is exact dry.
;
;  (the first call tunes the left side and copies it to the right, so neither starts empty)
; r7: 00 snap flag 01/02 mix t/s 03/04 vowl t/s 05/06 shft t/s 07/08 res t/s 09 t
;     0a ys 0b yq 0c lf/2 0d bw 0e sin h 0f 1-cos h 10 P 11 side 12 S2 1a first call
;     8e -WIDE (left) / +WIDE (right) for this call's side  8f h
;     13 gain target 14/15 gain (48 bit) 16/17 pin 18/19 pout (48 bit)
;     1b/1c x/4 L R 1d/1e wet/4 L R 1f log tmp
; r7+${TAB:02x}..${TAB + len(TABLE) - 1:02x}: vowel constants (row = vowel, 3 per formant), written by init
; r1 = r7+${COEF:02x}: LEFT formant f at +5f: c1 a2 a3 kh amp; +25+f: v1 of the current channel
; r5 = r7+${COEFR:02x}: RIGHT formant f at +5f: c1 a2 a3 kh amp
; r2 = r7+${STATE:02x}: channel c formant f at +25c+5f: s1 rounded, s1 hi lo, s2 hi lo
'''

_n = [0]


def lab():
    _n[0] += 1
    return f'vw{_n[0]:03d}'


def lerp(off):
    """a = row[off] + t (nextrow[off] - row[off]). Clobbers b x0 y0."""
    return [ins('move', f'x:(r3+${off:02x})', 'a'), ins('move', f'x:(r3+${off + ROW:02x})', 'b'),
            ins('sub', 'a', 'b'), ins('move', 'b', 'x0'), ins('move', f'x:(r7+${TT:02x})', 'y0'),
            ins('mac', 'y0', 'x0', 'a')]


def r7(slot):
    return f'x:(r7+${slot:02x})'


def build():
    L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
         ins('do', f'#{USED}', '>vwzero'), ins('move', 'a', 'x:(r4)+'), 'vwzero:', ins('nop'),
         ins('move', f'#>${TAB:x}', 'n4'), ins('move', 'r7', 'r4'), ins('nop'), ins('move', '(r4)+n4')]
    for i, val in enumerate(TABLE):
        L += [ins('move', q(val), 'a'), ins('move', 'a', 'x:(r4)+')]
    L += [ins('move', '#>$1', 'a'), ins('move', 'a', r7(FLAG)),
          ins('move', q(2.0 ** -GSHIFT), 'a'), ins('move', 'a', r7(GT)), ins('move', 'a', r7(GS)),
          ins('rts')]

    # ------------------------------------------------------------ block: knobs
    L += ['proc:', ins('move', '#>$ffffff', 'm1'), ins('move', '#>$ffffff', 'm2'),
          ins('move', '#>$ffffff', 'm3'), ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm5'),
          ins('move', f'#>${COEF:x}', 'n1'), ins('move', f'#>${COEFR:x}', 'n5'),
          ins('move', 'r7', 'r1'), ins('move', 'r7', 'r5'), ins('move', '(r1)+n1'), ins('move', '(r5)+n5'),
          ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
          ins('bne', 'vwmset'), ins('move', '#>$7fffff', 'a'), 'vwmset:', ins('move', 'a', r7(MIXT))]
    L += snap_close(MIXT, MIXS, 'vwmsnap')
    L += [ins('move', 'x:(r6)', 'a'), ins('move', 'a', r7(VT)),
          ins('move', 'x:(r6+$1)', 'a'), ins('cmp', 'x0', 'a', c='x0 = 127: SHFT top is +24 exactly'),
          ins('bne', 'vwsset'), ins('move', '#>$7fffff', 'a'), 'vwsset:', ins('move', 'a', r7(ST)),
          ins('move', 'x:(r6+$2)', 'a'), ins('move', 'a', r7(RT)),
          ins('move', r7(FLAG), 'a'), ins('tst', 'a'), ins('beq', 'vwglide')]
    for t in (VT, ST, RT):
        L += [ins('move', r7(t), 'b'), ins('move', 'b', r7(t + 1))]
    L += [ins('clr', 'a'), ins('move', 'a', r7(FLAG)),
          ins('move', '#>$1', 'a'), ins('move', 'a', r7(SIDE), c='first call: the left side (the toggle below makes it 0)'),
          ins('move', 'a', r7(FIRST)), 'vwglide:']
    L += smooth(VT, VS, PSMOOTH) + smooth(ST, SS, PSMOOTH) + smooth(RT, RS, PSMOOTH)
    L += ['; v/4 = knob * 128/127 (limited at 1): row = top 2 bits, t = the rest',
          ins('move', r7(VS), 'x0'), ins('move', 'x0', 'a'), ins('move', q(1 / 127), 'y0'),
          ins('mac', 'y0', 'x0', 'a'), ins('move', '#>$7fffff', 'x0'), ins('cmp', 'x0', 'a'),
          ins('tgt', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', 'x0', 'a', c='24 bits, a0 clear'),
          ins('asr', '#$15', 'a', 'a'), ins('move', 'a1', 'a', c='i0 = 0..3'),
          ins('move', 'a', 'b'), ins('asl', '#$4', 'b', 'b'), ins('sub', 'a', 'b', c='15 i0'),
          ins('move', 'b1', 'n3'), ins('move', 'r7', 'r3'), ins('nop'), ins('move', '(r3)+n3'),
          ins('move', f'#>${TAB:x}', 'n3'), ins('move', 'x0', 'b'), ins('and', '#>$1fffff', 'b'),
          ins('move', '(r3)+n3', c='r3 = row i0'),
          ins('asl', '#$2', 'b', 'b'), ins('move', 'b1', r7(TT), c='t'),
          '; this call\'s side: toggle; r2 = its coefficient block; ws = -WIDE (left) / +WIDE (right)',
          ins('move', '#>$1', 'b'), ins('move', r7(SIDE), 'x0'), ins('sub', 'x0', 'b'), ins('move', 'b', r7(SIDE)),
          ins('move', 'x:(r6+$4)', 'a'),
          ins('move', 'r5', 'r2'), ins('tst', 'b'), ins('bne', 'vwright'),
          ins('neg', 'a'), ins('move', 'r1', 'r2'), 'vwright:', ins('move', 'a', r7(WS)),
          '; ys = 14/32 - oct/32, yq = (log2 q - 3)/32 + oct/64   (oct = 4 (SHFT - 1/2))',
          ins('move', r7(SS), 'a'), ins('sub', q(0.5), 'a'), ins('asr', '#$3', 'a', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(14 / 32), 'b'), ins('sub', 'x0', 'b'),
          ins('move', 'b', r7(YS)),
          ins('move', r7(SS), 'a'), ins('sub', q(0.5), 'a'), ins('asr', '#$4', 'a', 'a'),
          ins('move', r7(RS), 'x0'), ins('move', q(KRES / 32), 'y0'), ins('mac', 'y0', 'x0', 'a'),
          ins('sub', q((3 - math.log2(RES_LO)) / 32), 'a'), ins('move', 'a', r7(YQ))]

    # ------------------------------------------------------------ block: five formants
    for f in range(F):
        o = 3 * f
        L += [f'; ---- formant {f + 1} ----', ins('lua', f'(r2+${5 * f:x})', 'r4')]
        L += lerp(o) + [ins('asr', 'a'), ins('move', 'a', 'x0'), ins('move', 'x0', r7(LH), c='log2(f0)/32'),
                        ins('move', r7(YS), 'a'), ins('sub', 'x0', 'a'), ins('bsr', 'vwexp'),
                        ins('move', 'b', 'x0'), ins('move', q(W0H), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='h = w/2'),
                        ins('move', 'a', r7(HH))]
        L += lerp(o + 1) + [ins('move', 'a', r7(BWI)),
                            ins('move', r7(YQ), 'a'), ins('move', r7(LH), 'x0'), ins('add', 'x0', 'a'),
                            ins('bsr', 'vwexp'),
                            ins('move', 'b', 'x0'), ins('move', r7(BWI), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
                            ins('asl', '#$4', 'a', 'a', c='kh = k/2'),
                            ins('move', '#>$7fffff', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a', c='Q >= 0.5'),
                            ins('move', q(1 / (2 * QMAX)), 'x0'), ins('cmp', 'x0', 'a'),
                            ins('tlt', 'x0', 'a', c='Q <= 120'),
                            ins('move', 'a', 'x0'), ins('move', 'x0', 'x:(r4+$3)'),
                            '; this side\'s h: h (1 + ws min(cap, x kh)), then the coefficients',
                            ins('asl', 'a') if WIDE_X == 2.0 else None,
                            ins('move', q(WIDE_CAP), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a'),
                            ins('move', 'a', 'x1'), ins('move', r7(WS), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
                            ins('move', 'a', 'x1'), ins('move', r7(HH), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
                            ins('add', 'y0', 'a'), ins('bsr', 'vwsico'),
                            ins('move', 'x:(r4+$3)', 'x0'), ins('bsr', 'vwdiv')]
        L += lerp(o + 2) + [ins('bsr', 'vwexp'), ins('move', 'b', 'x:(r4+$4)', c='amp (limits at 1)')]

    # ------------------------------------------------------------ block: level-match target
    L += ['; ---- first call: the right side starts as a copy of the left; r2 = the states ----',
          ins('move', r7(FIRST), 'a'), ins('tst', 'a'), ins('beq', 'vwnocopy'),
          ins('clr', 'a'), ins('move', 'a', r7(FIRST)), ins('move', 'r1', 'r3'), ins('move', 'r5', 'r4'),
          ins('do', f'#{5 * F}', '>vwcopy'), ins('move', 'x:(r3)+', 'x0'), ins('move', 'x0', 'x:(r4)+'),
          'vwcopy:', ins('nop'), 'vwnocopy:',
          ins('move', f'#>${STATE:x}', 'n2'), ins('move', 'r7', 'r2'), ins('nop'), ins('move', '(r2)+n2')]
    L += ['; ---- level match target (LEVL 0: unity) ----',
          ins('move', q(2.0 ** -GSHIFT), 'a'), ins('move', 'a', r7(GT)),
          ins('move', 'x:(r6+$3)', 'a'), ins('tst', 'a'), ins('beq', 'vwlvend')]
    for hi, lo in ((PIN, PINLO), (POUT, POUTLO)):
        L += [ins('move', r7(hi), 'b'), ins('move', r7(lo), 'b0'),
              ins('move', f'#>${EPS_ROOT:x}', 'x0'), ins('mpy', 'x0', 'x0', 'a', c='eps'), ins('add', 'b', 'a'),
              ins('bsr', 'vwlog')]
        if hi == PIN:
            L += [ins('move', 'b', r7(LG))]
    L += [ins('move', r7(LG), 'x0'), ins('sub', 'x0', 'b'), ins('add', q(GSHIFT / 32), 'b'),
          ins('move', '#>$0', 'x0'), ins('tst', 'b'), ins('tlt', 'x0', 'b', c='gain <= 32'),
          ins('move', q(YG_MAX / 32), 'x0'), ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b', c='gain >= 0.1'),
          ins('move', 'b', 'x0'), ins('move', 'x0', 'a'), ins('bsr', 'vwexp'),
          ins('move', 'b', r7(GT), c='gain/32 (limits at 1)'), 'vwlvend:']

    # ------------------------------------------------------------ the sample loop
    L += [ins('do', 'n7', '>vwend')]
    L += smooth(MIXT, MIXS, 7)
    L += ['; gain glide, 80 ms, 48-bit state',
          ins('move', r7(GT), 'a'), ins('move', r7(GS), 'b'), ins('move', r7(GSLO), 'b0'),
          ins('sub', 'b', 'a'), ins('move', 'a', 'x0'), ins('move', q(GAIN_COEF), 'y0'),
          ins('mac', 'y0', 'x0', 'b'), ins('move', 'b1', r7(GS)), ins('move', 'b0', r7(GSLO))]
    for ch in range(2):
        L += [f'; ---- {"LR"[ch]} ----',
              ins('move', 'x:(r0)', 'a'), ins('asr', f'#${XSHIFT:x}', 'a', 'a'), ins('rnd', 'a'),
              ins('move', 'a', r7(XL + ch), c='x/4')]
        for f in range(F):
            c, s = 5 * f, 25 * ch + 5 * f
            cb = ('r1', 'r5')[ch]
            kh, a2, a3, c1 = (f'x:({cb}+${c + k:02x})' for k in (3, 1, 2, 0))
            s1r, s1h, s1l, s2h, s2l = (f'x:(r2+${s + k:02x})' for k in range(5))
            L += [f'; formant {f + 1}',
                  ins('move', r7(XL + ch), 'x1'), ins('move', kh, 'x0'),
                  ins('move', s2h, 'b'), ins('move', s2l, 'b0'), ins('neg', 'b'),
                  ins('mac', 'x1', 'x0', 'b', c='v3 = kh x - s2 (48 bit)'),
                  ins('move', 'b0', 'y1', c='its low word, signed'), ins('rnd', 'b'), ins('move', 'b', 'x1'),
                  ins('move', s1l, 'x0'), ins('move', a2, 'y0'), ins('mpy', 'y0', 'x0', 'a'),
                  ins('move', a3, 'y0'), ins('mac', 'y1', 'y0', 'a'),
                  ins('asr', '#$18', 'a', 'a', c='low-word products'),
                  ins('move', s1r, 'x0'), ins('mac', 'x1', 'y0', 'a'), ins('move', a2, 'y0'),
                  ins('mac', 'y0', 'x0', 'a', c='a2 s1 + a3 v3'), ins('asl', 'a'),
                  ins('move', s2h, 'b'), ins('move', s2l, 'b0'), ins('add', 'b', 'a'),
                  ins('move', 'a', s2h), ins('move', 'a0', s2l, c='s2 += 2 (a2 s1 + a3 v3)'),
                  ins('mpy', 'y1', 'y0', 'a'), ins('move', s1l, 'x0'), ins('move', c1, 'y0'),
                  ins('mac', '-y0', 'x0', 'a'), ins('asr', '#$18', 'a', 'a'),
                  ins('move', s1r, 'x0'), ins('mac', '-y0', 'x0', 'a'), ins('move', a2, 'y0'),
                  ins('mac', 'x1', 'y0', 'a', c='t = a2 v3 - c1 s1'),
                  ins('move', s1h, 'b'), ins('move', s1l, 'b0'),
                  ins('add', 'a', 'b', c='v1 = s1 + t'), ins('add', 'b', 'a', c='s1 += 2 t'),
                  ins('rnd', 'b'), ins('move', 'b', f'x:(r1+${V1 + f:02x})'),
                  ins('move', 'a', s1h), ins('move', 'a0', s1l), ins('rnd', 'a'),
                  ins('move', 'a', s1r, c='rounded copy: the multiplicand')]
        for f in range(F):
            L += [ins('move', f'x:(r1+${V1 + f:02x})', 'x0'), ins('move', f'x:({("r1", "r5")[ch]}+${5 * f + 4:02x})', 'y0'),
                  ins('mpy' if f == 0 else 'mac', 'y0', 'x0', 'a')]
        L += [ins('asl', 'a'), ins('rnd', 'a'), ins('move', 'a', 'x1'),
              ins('move', 'x1', r7(OL + ch), c='wet/4 before the gain'),
              ins('move', r7(GS), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
              ins('asl', f'#${XSHIFT + GSHIFT:x}', 'a', 'a'),
              ins('move', 'a', 'x1', c='limits'), ins('move', 'x1', 'a')]
        L += mix(mixslot=MIXS)
    for (u, w), (hi, lo) in (((XL, XR), (PIN, PINLO)), ((OL, OR_), (POUT, POUTLO))):
        L += [f'; power, 250 ms: p += (e - p)(2^-13 - 2^-15), 48 bit',
              ins('move', r7(u), 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', r7(w), 'y0'),
              ins('mac', 'y0', 'y0', 'a'), ins('move', r7(hi), 'b'), ins('move', r7(lo), 'b0'),
              ins('sub', 'b', 'a'), ins('asr', '#$d', 'a', 'a'), ins('add', 'a', 'b'),
              ins('asr', '#$2', 'a', 'a'), ins('sub', 'a', 'b'),
              ins('move', 'b1', r7(hi)), ins('move', 'b0', r7(lo))]
    L += ['vwend:', ins('nop'), ins('rts')]

    # ------------------------------------------------------------ subroutines (leaves, after callers)
    L += ['; ---- subroutines ----'] + e2_sub('vwexp', 'vwxpend')
    L += ['vwsico:', '; a = h -> P = sin w cos w, S2 = sin^2 w  (w = 2h)',
          ins('move', 'a', 'x1'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'y0', c='h^2'),
          ins('move', q(1 / 5040), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 120), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 6), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
          ins('move', 'x1', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', r7(SN), c='sin h'),
          ins('move', q(1 / 40320), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 720), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 24), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(0.5), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a', c='1 - cos h'),
          ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0', c='cos h'),
          ins('move', r7(SN), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'), ins('move', 'a', 'x1', c='sin w'),
          ins('mpy', 'y0', 'y0', 'b'), ins('asl', 'b'), ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'),
          ins('move', 'a', 'x0', c='cos w'),
          ins('mpy', 'x1', 'x0', 'a'), ins('move', 'a', r7(PP)),
          ins('move', 'x1', 'y0'), ins('mpy', 'y0', 'y0', 'a'), ins('move', 'a', r7(S2)), ins('rts')]
    L += ['vwdiv:', '; x0 = kh -> (r4) c1, a2, a3: P, S2 over D = 1 + 2 kh P',
          ins('move', r7(PP), 'y0'), ins('move', q(0.5), 'a'), ins('mac', 'y0', 'x0', 'a'),
          ins('move', 'a', 'x1', c='d = D/2 in [0.5, 1)'),
          ins('move', q(0.9571), 'a'), ins('add', q(0.5), 'a'), ins('sub', 'x1', 'a'), ins('move', 'a', 'y0', c='r ~ 1/D')]
    for _ in range(3):
        L += [ins('move', q(0.5), 'a'), ins('mac', '-x1', 'y0', 'a'), ins('add', q(0.5), 'a'),
              ins('move', 'a', 'x0', c='1 - d r'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'),
              ins('move', 'a', 'y0', c='r = 2 r (1 - d r)')]
    L += [ins('move', r7(PP), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', 'x1', 'x:(r4+$1)', c='a2 = P/D'),
          ins('move', r7(S2), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x:(r4+$2)', c='a3 = S2/D'),
          ins('move', 'x:(r4+$3)', 'y0'), ins('mpy', 'x1', 'y0', 'b'), ins('asl', 'b'), ins('add', 'a', 'b'),
          ins('move', 'b', 'x:(r4)', c='c1 = 1 - a1 = a3 + k a2'), ins('rts')]
    L += ['vwlog:', '; a = p (48 bit, > 0) -> b = log2(p)/64 + const (Q23): p = m 2^-e, m in [0.5, 1)',
          ins('clr', 'b')]
    for s in (16, 16, 8, 4, 2, 1):
        lb = lab()
        L += [ins('move', f'#>${1 << (23 - s):06x}', 'x0'), ins('cmp', 'x0', 'a'), ins('bge', lb),
              ins('asl', f'#${s:x}', 'a', 'a'), ins('add', f'#>${s:x}', 'b'), f'{lb}:']
    L += [ins('move', 'b1', 'x1', c='e'), ins('sub', q(0.5), 'a'), ins('asl', 'a'), ins('move', 'a', 'y0', c='u = 2m - 1'),
          ins('move', q(LOGC[5]), 'x0'), ins('mpy', 'y0', 'x0', 'b')]
    for k in (4, 3, 2, 1):
        L += [ins('add' if LOGC[k] >= 0 else 'sub', q(abs(LOGC[k])), 'b'), ins('move', 'b', 'x0'),
              ins('mpy', 'y0', 'x0', 'b')]
    L += [ins('add' if LOGC[0] >= 0 else 'sub', q(abs(LOGC[0])), 'b', c='0.5 log2(1 + u)'),
          ins('asr', '#$5', 'b', 'b'), ins('move', 'x1', 'a'), ins('asl', '#$11', 'a', 'a'),
          ins('sub', 'a', 'b', c='- e/64'), ins('rts')]
    return finish(L)


if __name__ == '__main__':
    Path(__file__).with_name('vowel.asm').write_text(build())
    print(f'wrote vowel.asm (r7 words used: {USED}; log fit error {LOGERR:.2e})')
