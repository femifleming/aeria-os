#!/usr/bin/env python3
"""Generates echo.asm (AERIA ECHO: stereo just-intonation multi-tap delay).

    python3 gen.py                 full build  -> echo.asm
                                   (Y ring 16,384, X ring 16,384 at X:$4000)
    python3 gen.py --test          harness builds -> echo_test.asm, echo_fullx.asm
    python3 gen.py --yring N --xring N --xbase HEX --out NAME.asm

The three builds differ ONLY in the generator constants YN (left ring words),
XN (right ring words) and XRING (absolute X base of the right ring); the
tap clamp DMAX = min(YN, XN) - 8 and the per-set T ceilings follow from them.
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, snap_close, softclip  # noqa: E402

FS = 44100
S = 16384.0                     # "A-units": a time held as samples/16384 in Q23 (512 LSB per sample)
T_MIN, T_MAX = 0.010, 0.220     # TIME knob range, seconds (log)
F_MIN, F_MAX = 300.0, 18000.0   # TONE knob range, Hz (log)
FB_MAX = 0.95
FADE = 0.080                    # crossfade, seconds
MARGIN = 8                      # ring words never reached by a tap

SETS = [
    ('JUST', [1, 9 / 8, 5 / 4, 4 / 3, 3 / 2, 5 / 3]),          # the plugin's default stack
    ('MINR', [1, 9 / 8, 6 / 5, 4 / 3, 3 / 2, 8 / 5]),          # 5-limit minor
    ('PYTH', [1, 9 / 8, 81 / 64, 4 / 3, 3 / 2, 27 / 16]),      # 3-limit
    ('SEPT', [1, 8 / 7, 7 / 6, 21 / 16, 7 / 5, 3 / 2]),        # 7-limit (7/4 folded: 8/7, 7/6, 21/16)
    ('HARM', [1, 9 / 8, 5 / 4, 11 / 8, 3 / 2, 13 / 8]),        # harmonics 8:9:10:11:12:13
    ('UNIS', [1, 1, 1, 1, 1, 1]),                              # unison / slapback
]
for _n, _r in SETS:
    assert _r == sorted(_r) and _r[0] >= 1 and _r[-1] <= 1.6875, _n

W = (0.62, 0.62 * 0.8, 0.62 * 0.64)     # tap weights (plugin: 0.62 (y0 + 0.8 y1 + 0.64 y2))
DC_LEAK = 1 - math.exp(-2 * math.pi * 10 / FS)

# ---- r7 map (all per-sample slots below $40: one-word displaced moves) ----
YBASE, YPTR, CLR, XPTR = 0x00, 0x01, 0x02, 0x03
MIXT, MIXC, WIDT, WIDC = 0x04, 0x05, 0x06, 0x07
FB, TCOEF, WCOEF, FX, FADING, TMP, GOLD, GNEW = 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
TGT, CURA, CURN, NXTN, NXTA = 0x10, 0x16, 0x1c, 0x22, 0x28
FILT = (0x2e, 0x34)             # per side: +0/1 wet lp hi/lo, +2/3 tone lp hi/lo, +4/5 dc hi/lo
TABLE = 0x40                    # 6 sets x 8 words: T ceiling, six ratio/2, pad
STATE_WORDS = TABLE + 8 * len(SETS)
ONE = '#>$7fffff'


def sl(n):
    return f'x:(r7+${n:02x})'


def dmax(yn, xn):
    return min(yn, xn) - MARGIN


def tmax_word(yn, xn, ratios):
    """Largest T (A-units, Q23 integer) with T * max ratio <= DMAX."""
    return min((1 << 23) - 1, math.floor(dmax(yn, xn) / max(ratios) / S * (1 << 23)))


def lit(v):
    return f'#>${v & 0xffffff:06x}'


def exp_sub(label, endlabel):
    """b = 2^-y for a = y/32 (Q23, y in [0,32)); clobbers a b x0 x1 y0.
    asmgen.e2_sub with one more Taylor term (relative error < 1e-5, so TIME
    lands within a fraction of a sample of the formula at the ring's end)."""
    return [
        f'{label}:',
        ins('move', 'a1', 'b'),
        ins('asr', '#$12', 'a', 'a', c='a1 = n = floor(y)'),
        ins('add', '#>$1', 'a'),
        ins('move', 'a1', 'x1', c='n + 1 (never 0)'),
        ins('and', '#>$03ffff', 'b'),
        ins('asl', '#$5', 'b', 'b', c='f in Q23'),
        ins('move', 'b1', 'x0'),
        ins('move', q(math.log(2) / 2), 'y0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('move', 'b', 'y0', c='g = f ln2 / 2'),
        ins('move', q(1 / 240), 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('neg', 'b'),
        ins('add', q(1 / 48), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('sub', q(1 / 12), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('add', q(0.25), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('sub', q(0.5), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('add', q(0.5), 'b', c='e^-g / 2'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'x0', 'x0', 'b'),
        ins('asl', '#$2', 'b', 'b', c='2^-f in (0.5, 1]'),
        ins('do', 'x1', f'>{endlabel}'),
        ins('asr', 'b'),
        f'{endlabel}:',
        ins('asl', 'b'),
        ins('rts'),
    ]


def knob_down(slot, const):
    """a = max(0, 127/128 - knob) * const  (the exponent argument y/32)."""
    return [ins('move', '#>$7f0000', 'a'), ins('move', f'x:(r6+${slot:x})' if slot else 'x:(r6)', 'x0'),
            ins('move', '#>$0', 'x1'), ins('sub', 'x0', 'a'), ins('tlt', 'x1', 'a'),
            ins('move', 'a', 'x0'), ins('move', q(const), 'y0'), ins('mpy', 'y0', 'x0', 'a')]


def to_n(dst):
    """a = a time in A-units -> x:(dst) = -(whole samples, rounded)."""
    return [ins('add', '#>$100', 'a'), ins('asr', '#$9', 'a', 'a'), ins('move', 'a1', 'a'),
            ins('neg', 'a'), ins('move', 'a1', sl(dst))]


def tozero(label):
    """a (48-bit) truncated toward zero at the 24-bit boundary: a negative
    value gets 1 LSB - 2^-24 LSB added, so the floor a move takes is the
    ceiling. Clobbers y0 y1."""
    return [ins('move', '#>$0', 'y1'), ins('move', '#>$ffffff', 'y0'), ins('tst', 'a'),
            ins('bge', label), ins('add', 'y', 'a'), f'{label}:']


def build(yn=16384, xn=16384, xring=0x4000, out='echo.asm'):
    YM, XM = yn - 1, xn - 1
    DM = dmax(yn, xn)
    ycalls, xcalls = math.ceil(yn / 128), math.ceil(xn / 128)
    assert ycalls <= 128 and xcalls <= 128 and xring % 0x4000 == 0 and yn <= 16384 and xn <= 16384
    inc = round((1 << 23) / (FADE * FS))
    c_time = (128 / 127) * math.log2(T_MAX / T_MIN) / 32
    c_tone = (128 / 127) * math.log2(F_MAX / F_MIN) / 32
    k1 = 2 * math.pi * F_MAX / FS / math.log(2) / 32
    kw = 2 * math.pi * 20000 / FS / math.log(2) / 32
    # sin(pi u / 2) / 2 ~ u (c1 + c3 u^2 + c5 u^4), scaled so u = +-1 gives +-1/2 exactly
    n_ = 1.5706268 - 0.6432292 + 0.0727102
    c1, c3, c5 = 1.5706268 / n_ / 2, 0.6432292 / n_ / 2, 0.0727102 / n_ / 2

    H = f'''; CYCLES_FORWARD_BRANCHES
; AERIA ECHO -- stereo just-intonation multi-tap delay (Cloudfin StereoDelay,
; fixed point). Two hard-panned lines, both fed the mono sum, three taps each;
; wet = 0.62 (y0 + 0.8 y1 + 0.64 y2) through a wet lowpass at 1.5 x TONE; each
; side regenerates from its longest tap through the tone lowpass, a 10 Hz DC
; blocker and a soft clipper times FDBK.
;
;  TIME  base time T, {T_MIN * 1000:.0f} .. {T_MAX * 1000:.0f} ms (log); T x the set's largest ratio is
;        held to {DM} samples (the rings), keeping the ratios intact
;  FDBK  regeneration 0 .. {FB_MAX}
;  TONE  feedback lowpass {F_MIN:.0f} Hz .. {F_MAX / 1000:.0f} kHz (log); wet lowpass at 1.5 x, <= 20 kHz
;  RATO  tap-ratio set [L0 R0 L1 R1 L2 R2]: {' '.join(n for n, _ in SETS)}
;  WDTH  127 = hard-panned (the plugin), 0 = both sides summed to mono
;  MIX   0 = exact dry
;
; Seamless taps: when any tap's target moves by more than max(0.75 sample,
; 0.2 %) all six open a second read head at the new time and crossfade
; (sin^2, {FADE * 1000:.0f} ms); a later change waits for the fade to end. Tap times are
; whole samples (static between fades, so nothing is interpolated).
;
; LEFT line: Y ring of {yn} words (m = ${YM:04x}) at the instance buffer base.
; RIGHT line: X ring of {xn} words (m = ${XM:04x}) at the absolute base X:${xring:04x}
; (generator constant XRING). Lines hold (mono + regen) / 2.
;
; r7: 00 y base 01 y ptr 02 clear 03 x ptr 04/05 mix t/s 06/07 cross t/s
;   08 fdbk 09 tone coef 0a wet coef 0b fade x 0c fading 0d tmp
;   0e/0f fade gains old/new 10.. targets (A-units) 16.. current (A-units)
;   1c.. current -samples 22.. next -samples 28.. next (A-units)
;   2e..33 left, 34..39 right: wet lp hi/lo, tone lp hi/lo, dc hi/lo
;   40..{STATE_WORDS - 1:02x} ratio table: per set T ceiling, six ratio/2, pad
'''
    L = [H]
    # ================================================================ init
    L += ['init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
          ins('do', f'#{TABLE}', '>ecizero'), ins('move', 'a', 'x:(r4)+'), 'ecizero:', ins('nop')]
    for name, ratios in SETS:
        L += [f'; {name}', ins('move', lit(tmax_word(yn, xn, ratios)), 'a'), ins('move', 'a', 'x:(r4)+')]
        for r in ratios:
            L += [ins('move', q(r / 2), 'a'), ins('move', 'a', 'x:(r4)+')]
        L += [ins('clr', 'a'), ins('move', 'a', 'x:(r4)+')]
    L += [ins('move', 'x:>$213', 'r4'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', sl(YBASE)),
          ins('move', 'a', sl(YPTR)), ins('move', lit(xring), 'a'), ins('move', 'a', sl(XPTR)),
          ins('move', '#>$80', 'a'), ins('move', 'a', sl(CLR)), ins('rts')]

    # ================================================================ per block
    L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$1', 'n0'),
          '; FDBK',
          ins('move', 'x:(r6+$1)', 'x0'), ins('move', q(FB_MAX * 128 / 127), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'a', sl(FB)),
          '; TONE: f = 18 k * 60^-(1 - knob); coef = 1 - e^-w; wet at 1.5 f <= 20 k']
    L += knob_down(2, c_tone) + [
          ins('bsr', 'ecexp'), ins('move', 'b', 'x0'), ins('move', 'x0', sl(TMP)),
          ins('move', q(k1), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'ecexp'),
          ins('move', 'b', 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('move', 'a', sl(TCOEF)),
          ins('move', sl(TMP), 'x0'), ins('move', q(1.5 * k1), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', q(kw), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('bsr', 'ecexp'),
          ins('move', 'b', 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('move', 'a', sl(WCOEF)),
          '; RATO: r4 -> the set\'s row (index held to the table)',
          ins('move', 'x:(r6+$3)', 'x0'), ins('move', '#>$7f0000', 'a'), ins('and', 'x0', 'a'),
          ins('asr', '#$d', 'a', 'a'), ins('move', 'a1', 'x0'), ins('move', 'x0', 'a'),
          ins('move', lit(8 * (len(SETS) - 1)), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
          ins('add', lit(TABLE), 'a'), ins('move', 'r7', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r4'),
          '; TIME: T = 220 ms * 22^-(1 - knob), held to the set\'s ceiling; six targets']
    L += knob_down(0, c_time) + [
          ins('bsr', 'ecexp'), ins('move', 'b', 'x0'), ins('move', q(T_MAX * FS / S), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', 'x:(r4)+', 'x0'), ins('cmp', 'x0', 'a'),
          ins('tgt', 'x0', 'a'), ins('move', 'a', 'y0')]
    for k in range(6):
        L += [ins('move', 'x:(r4)+', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'),
              ins('move', 'a', sl(TGT + k))]
    L += ['; MIX, WDTH (cross = 1 - width)',
          ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'), ins('blt', 'ecmset'),
          ins('move', ONE, 'a'), 'ecmset:', ins('move', 'a', sl(MIXT))]
    L += knob_down(4, 64 / 127) + [ins('asl', 'a'), ins('move', 'a', sl(WIDT))]
    L += snap_close(MIXT, MIXC, 'ecmsnap') + snap_close(WIDT, WIDC, 'ecwsnap')
    L += ['; bounded clearing: 128 dry calls, 128 words of each ring a call; taps snap',
          ins('move', sl(CLR), 'a'), ins('tst', 'a'), ins('beq', 'ecready'),
          ins('sub', '#>$1', 'a'), ins('move', 'a', sl(CLR))]
    for k in range(6):
        L += [ins('move', sl(TGT + k), 'a'), ins('move', 'a', sl(CURA + k))] + to_n(CURN + k)
    L += [ins('clr', 'a'), ins('move', 'a', sl(FADING)), ins('move', 'a', sl(FX)),
          ins('move', sl(WIDT), 'a'), ins('move', 'a', sl(WIDC)),
          ins('move', sl(CLR), 'a'), ins('cmp', lit(ycalls), 'a'), ins('bge', 'ecnoclry'),
          ins('asl', '#$7', 'a', 'a'), ins('move', sl(YBASE), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'),
          ins('move', lit(YM), 'm4'), ins('clr', 'a'), ins('do', '#128', '>ecclry'),
          ins('move', 'a', 'y:(r4)+'), 'ecclry:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
          'ecnoclry:',
          ins('move', sl(CLR), 'a'), ins('cmp', lit(xcalls), 'a'), ins('bge', 'ecnoclrx'),
          ins('asl', '#$7', 'a', 'a'), ins('add', lit(xring), 'a'), ins('move', 'a', 'r4'),
          ins('move', lit(XM), 'm4'), ins('clr', 'a'), ins('do', '#128', '>ecclrx'),
          ins('move', 'a', 'x:(r4)+'), 'ecclrx:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
          'ecnoclrx:', ins('rts')]
    L += ['ecready:', '; ---- seamless-tap state machine (block rate) ----',
          ins('move', sl(FADING), 'a'), ins('tst', 'a'), ins('beq', 'eccheck'),
          ins('move', sl(FX), 'a'), ins('cmp', ONE, 'a'), ins('blt', 'ecrun'),
          '; fade complete: the new heads become the current ones']
    for k in range(6):
        L += [ins('move', sl(NXTA + k), 'a'), ins('move', 'a', sl(CURA + k)),
              ins('move', sl(NXTN + k), 'a'), ins('move', 'a', sl(CURN + k))]
    L += [ins('clr', 'a'), ins('move', 'a', sl(FADING)),
          'eccheck:', '; any |target - current| > max(0.75 sample, 0.2 %) ?']
    for k in range(6):
        L += [ins('move', sl(TGT + k), 'a'), ins('move', sl(CURA + k), 'x0'), ins('sub', 'x0', 'a'),
              ins('abs', 'a'), ins('move', q(0.002), 'y0'), ins('mpy', 'y0', 'x0', 'b'),
              ins('move', '#>$180', 'x1'), ins('cmp', 'x1', 'b'), ins('tlt', 'x1', 'b'),
              ins('move', 'b', 'x1'), ins('cmp', 'x1', 'a'), ins('bgt', 'ecstart')]
    L += [ins('bra', 'ecrun'), 'ecstart:']
    for k in range(6):
        L += [ins('move', sl(TGT + k), 'a'), ins('move', 'a', sl(NXTA + k))] + to_n(NXTN + k)
    L += [ins('clr', 'a'), ins('move', 'a', sl(FX)), ins('move', '#>$1', 'a'), ins('move', 'a', sl(FADING)),
          'ecrun:', ins('move', lit(YM), 'm5'), ins('move', lit(XM), 'm4'),
          ins('move', sl(YPTR), 'r5'), ins('move', sl(XPTR), 'r4'),
          ins('move', sl(FADING), 'a'), ins('tst', 'a'), ins('bne', 'ecfade')]

    # ================================================================ per sample
    def side(c, fading, tag):
        """One line: three taps (x1, x0, y0 = longest), wet lowpass, regen, push."""
        mem, r, n = (('y', 'r5', 'n5'), ('x', 'r4', 'n4'))[c]
        rd = f'{mem}:({r}+{n})'
        f0 = FILT[c]
        o = [f'; ======== {"left (Y ring)" if c == 0 else "right (X ring)"} ========']
        if fading:
            o += [ins('move', sl(GOLD), 'y1'), ins('move', sl(GNEW), 'y0')]
            for j, dst in enumerate(('x1', 'b', None)):
                k = c + 2 * j
                o += [ins('move', sl(CURN + k), n), ins('move', rd, 'x0'), ins('mpy', 'x0', 'y1', 'a'),
                      ins('move', sl(NXTN + k), n), ins('move', rd, 'x0'), ins('mac', 'y0', 'x0', 'a')]
                if dst:
                    o += [ins('move', 'a', dst)]
            o += [ins('move', 'b', 'x0'), ins('move', 'a', 'y0')]
        else:
            for j, dst in enumerate(('x1', 'x0', 'y0')):
                o += [ins('move', sl(CURN + c + 2 * j), n), ins('move', rd, dst)]
        o += ['; wet = 2 (w0 t0 + w1 t1 + w2 t2), limited, then the wet lowpass (48-bit state)',
              ins('move', q(W[0]), 'y1'), ins('mpy', 'y1', 'x1', 'a'),
              ins('move', q(W[1]), 'y1'), ins('mac', 'x0', 'y1', 'a'),
              ins('move', q(W[2]), 'y1'), ins('mac', 'y1', 'y0', 'a'),
              ins('asl', 'a'), ins('move', 'a', 'x0'),
              ins('move', sl(f0), 'b'), ins('move', sl(f0 + 1), 'b0'), ins('move', sl(WCOEF), 'y1'),
              ins('mac', 'x0', 'y1', 'b'), ins('move', sl(f0), 'x0'), ins('mac', '-x0', 'y1', 'b'),
              ins('move', 'b1', sl(f0)), ins('move', 'b0', sl(f0 + 1)),
              '; regen: tone lowpass on the longest tap, dc blocker on its 48-bit output',
              ins('move', sl(f0 + 2), 'b'), ins('move', sl(f0 + 3), 'b0'),
              ins('move', sl(f0 + 4), 'a'), ins('move', sl(f0 + 5), 'a0'), ins('sub', 'b', 'a'),
              ins('move', sl(TCOEF), 'y1'), ins('mac', 'y1', 'y0', 'b'),
              ins('move', sl(f0 + 2), 'x0'), ins('mac', '-x0', 'y1', 'b'),
              ins('move', 'b1', sl(f0 + 2)), ins('move', 'b0', sl(f0 + 3)), ins('add', 'b', 'a'),
              ins('move', sl(f0 + 4), 'x0'), ins('move', q(DC_LEAK), 'y1'), ins('mac', '-x0', 'y1', 'a'),
              ins('move', 'a0', sl(f0 + 5)), ins('move', 'a', sl(f0 + 4))]
        o += tozero(f'ec{tag}{c}za') + [
              ins('move', 'a', 'x0'), ins('move', sl(FB), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
              ins('asl', 'a', c='full scale again')] + softclip('a', 'a') + [
              '; push (mono + regen) / 2, truncated toward zero',
              ins('move', 'x:(r0)', 'b'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'b'),
              ins('asr', 'b'), ins('add', 'b', 'a'), ins('asr', 'a')]
        o += tozero(f'ec{tag}{c}zb') + [ins('move', 'a', f'{mem}:({r})')]
        return o

    def loop(fading, tag, endlabel):
        o = [ins('do', 'n7', f'>{endlabel}')]
        o += smooth(MIXT, MIXC, 7) + smooth(WIDT, WIDC, 7)
        if fading:
            o += ['; fade gain: x += 1/len (saturating); new = sin^2(pi x / 2), old = 1 - new',
                  ins('move', sl(FX), 'a'), ins('add', lit(inc), 'a'), ins('move', 'a', sl(FX)),
                  ins('move', sl(FX), 'a'), ins('sub', q(0.5), 'a'), ins('asl', 'a'), ins('move', 'a', 'x0'),
                  ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x1'),
                  ins('move', q(c5), 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('sub', q(c3), 'a'),
                  ins('move', 'a', 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('add', q(c1), 'a'),
                  ins('move', 'a', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(0.5), 'a'),
                  ins('move', 'a', 'x0'), ins('move', 'x0', sl(GNEW)),
                  ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('move', 'a', sl(GOLD))]
        o += side(0, fading, tag) + side(1, fading, tag)
        fl, fr = FILT
        o += ['; ---- width, mix, advance ----',
              ins('move', sl(fr), 'a'), ins('move', sl(fl), 'x0'), ins('sub', 'x0', 'a'), ins('asr', 'a'),
              ins('move', 'a', 'x1'), ins('move', sl(WIDC), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
              ins('move', 'a', 'x1', c='cross (R - L) / 2'),
              ins('move', 'x0', 'a'), ins('add', 'x1', 'a')] + mix(mixslot=MIXC) + [
              ins('move', sl(fr), 'a'), ins('sub', 'x1', 'a')] + mix(mixslot=MIXC) + [
              ins('move', '(r5)+'), ins('move', '(r4)+')]
        o += [f'{endlabel}:', ins('nop')]
        return o

    L += loop(False, 'p', 'ecplend') + [ins('bra', 'ecexit'), 'ecfade:'] + loop(True, 'f', 'ecfdend')
    L += ['ecexit:', ins('move', 'r5', sl(YPTR)), ins('move', 'r4', sl(XPTR)),
          ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm5'), ins('rts'),
          '; ---- subroutine (after its callers) ----'] + exp_sub('ecexp', 'ecxpend')
    Path(__file__).with_name(out).write_text(finish(L))
    return dict(out=out, yn=yn, xn=xn, xring=xring, dmax=DM, state=STATE_WORDS)


# harness builds: where the X ring sits under dsp_host (see README / test.py)
TEST_XRING = 0x10000
BUILDS = {
    'echo.asm': dict(yn=16384, xn=16384, xring=0x4000),
    'echo_test.asm': dict(yn=15870, xn=16384, xring=TEST_XRING),
    'echo_fullx.asm': dict(yn=16384, xn=16384, xring=TEST_XRING),
}

if __name__ == '__main__':
    a = sys.argv[1:]
    if '--test' in a:
        todo = ['echo_test.asm', 'echo_fullx.asm']
    elif '--all' in a:
        todo = list(BUILDS)
    elif '--out' in a:
        kw = dict(out=a[a.index('--out') + 1])
        if '--yring' in a:
            kw['yn'] = int(a[a.index('--yring') + 1], 0)
        if '--xring' in a:
            kw['xn'] = int(a[a.index('--xring') + 1], 0)
        if '--xbase' in a:
            kw['xring'] = int(a[a.index('--xbase') + 1], 16)
        print('wrote', build(**kw))
        todo = []
    else:
        todo = ['echo.asm']
    for name in todo:
        print('wrote', build(out=name, **BUILDS[name]))
