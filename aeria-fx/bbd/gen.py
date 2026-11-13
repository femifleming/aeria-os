#!/usr/bin/env python3
"""Generates bbd.asm and bbd_test.asm (AERIA BBD: Chloaelis' bucket-brigade delay, DSP.h BBDLine inside DelayCell,
in 24-bit fixed point).

    python3 bbd/gen.py      (host, no dependencies; test.py imports this and rebuilds)

bbd.asm       the hardware build: the rings start at the word X:$213 points to, used as an X address (core 1 X:$2000)
bbd_test.asm  the same source with the rings at X:$10000 (dsp_host hands out a Y base); the init lines differ

Per side (dual mono, hard panned: left in -> left delay -> left out, as the plugin):
  s = x + fb -> 2:1 compressor (ref 0.22, gain <= 10) -> anti-alias 4-pole Butterworth (6.8 kHz) -> 4,096 buckets
  clocked at 4096/time (the input interpolated at each clock edge; bucket noise) -> read on clock edges through the
  charge-transfer one-pole (0.86) and held -> + clock feedthrough (+-0.0012 alternating) -> reconstruction 4-pole
  (6.2 kHz) -> + output noise -> 1:2 expander (gain <= 6) -> DC block (9 Hz) -> wet; fb = 1.05 regen hpf(wet).
NOT ported (the owner: no saturation): bucketSat's tanh (kept: its slope at 0, 0.9775), the output tanh, the regen
tanh. In their place: the compressor and expander detectors take peaks at once (the plugin: 4 / 6 ms attack, whose
onset overshoot the bucket clipper swallowed; the releases are the plugin's, 45 / 60 ms, so the pumping is too), and a
gain rider on the wet (g = min(1, C/env), env a peak with 100 ms release, C = 0.95 FS): self-oscillation (FDBK past
about 110) settles at the ceiling instead of clipping, and the wet never passes C.
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, finish, e2_sub  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = 44100.0
NB = 4096                       # buckets a side
T_MIN, T_MAX = 0.047, 1.0       # TIME range, s (47 ms: at most 2 clock edges a sample, so 2 unrolled ticks a head)
OCT = math.log2(T_MAX / T_MIN)  # octaves of TIME (4.41)
CMAX = NB / (T_MIN * FS)        # clock edges a sample at T_MIN (1.976)
assert CMAX < 2.0
REF = 0.22
G_MAX_C, G_MAX_E = 10.0, 6.0
FL4 = REF / (4 * G_MAX_C ** 2)  # compressor detector floor in s/4 units (gain <= 10)
C_REL = 1 - math.exp(-1 / (0.045 * FS))
E_REL = 1 - math.exp(-1 / (0.060 * FS))
R_REL = 1 - math.exp(-1 / (0.100 * FS))   # wet rider release
CEIL = 0.95                     # wet ceiling, FS
BUCKET_GAIN = 0.85 * 1.15       # bucketSat's slope at 0 (0.85 tanh(1.15 v + 0.06) - 0.85 x 0.0599)
CTI = 0.86
LEAK = 0.99985                  # a frozen bucket, a tick
NOISE_TICK = 0.0009             # (rnd - 0.5) 0.0009 into a bucket
NOISE_OUT = 0.0004
FEED = 0.0012
DC_K = 2 * math.pi * 9.0 / FS
FB_K = 1.05                     # the plugin's 1.05 tanh(regen f): slope 1.05 regen
REGEN_MAX = 1.15
HPF_HZ, HPF_Q = 320.0, 0.6
AA_HZ, RC_HZ = 6800.0, 6200.0
Q1, Q2 = 0.54, 1.31             # the plugin's two sections (a 4-pole Butterworth)
TONE_REF = 100                  # TONE 100 = the plugin's filters; 24 knob steps an octave
TONE_STEP = 24
GLIDE_HZ = 9.0                  # the plugin's clock glide (onePoleCoef 9 Hz): here a call, ramped a sample
XF_S = 0.05                     # FADE crossfade (the plugin: 50 ms)
WAIT_PAD = 96                   # the plugin's waitN = time sr + 96
THR = 2.5e-4                    # line activity (-72 dBFS): quiet for HOLD after the line input dies -> artefacts off
HOLD = int(T_MAX * FS) + 4410
GATE_S = 0.010                  # artefact gate ramp
LFO_LO, LFO_HI = 0.02, 20.0     # the plugin's LFO>Time rate range
LFO_R = 0.13 / 0.11             # the right LFO runs faster (the plugin's defaults 0.11 / 0.13 Hz)
TEST_XBASE = 0x10000
HW_XBASE = 0x2000


def fit(fn, lo, hi, deg):
    """least-squares polynomial (power basis) on [lo, hi] -> (coefs, max relative error)."""
    xs = [lo + (hi - lo) * i / 400 for i in range(401)]
    n = deg + 1
    M = [[sum(x ** (i + j) for x in xs) for j in range(n)] for i in range(n)]
    b = [sum(fn(x) * x ** i for x in xs) for i in range(n)]
    for i in range(n):
        p = M[i][i]
        M[i] = [v / p for v in M[i]]
        b[i] /= p
        for j in range(n):
            if j != i:
                f = M[j][i]
                M[j] = [u - f * v for u, v in zip(M[j], M[i])]
                b[j] -= f * b[i]
    err = max(abs(sum(c * x ** k for k, c in enumerate(b)) / fn(x) - 1) for x in xs)
    return b, err


# sqrt(REF)/sqrt(m) / 4 on [0.25, 1): the compressor gain's mantissa (quartered: coefficients and Horner sums < 1)
RS_C, RS_ERR = fit(lambda m: math.sqrt(REF) / math.sqrt(m) / 4, 0.25, 1.0, 3)
assert RS_ERR < 0.015 and all(abs(c) < 1 for c in RS_C), (RS_C, RS_ERR)
# sin(pi u / 2) / 2 on [-1, 1] (odd quintic): the crossfade gains
SIN_C = None


def _sinfit():
    xs = [i / 400 for i in range(401)]
    rows = [[x, x ** 3, x ** 5] for x in xs]
    ys = [0.5 * math.sin(math.pi * x / 2) for x in xs]
    M = [[sum(r[i] * r[j] for r in rows) for j in range(3)] for i in range(3)]
    b = [sum(r[i] * y for r, y in zip(rows, ys)) for i in range(3)]
    for i in range(3):
        p = M[i][i]
        M[i] = [v / p for v in M[i]]
        b[i] /= p
        for j in range(3):
            if j != i:
                f = M[j][i]
                M[j] = [u - f * v for u, v in zip(M[j], M[i])]
                b[j] -= f * b[i]
    return b


SIN_C = _sinfit()


# ------------------------------------------------------------------ laws (the knobs) and the float reference
def lp_coefs(fc, qq):
    """the plugin's Biquad::lowpass (RBJ)."""
    fc = min(max(fc, 10.0), FS * 0.45)
    w = 2 * math.pi * fc / FS
    c, al = math.cos(w), math.sin(w) / (2 * qq)
    n = 1 / (1 + al)
    return ((1 - c) * 0.5 * n, (1 - c) * n, (1 - c) * 0.5 * n, -2 * c * n, (1 - al) * n)


def hp_coefs(fc, qq):
    w = 2 * math.pi * fc / FS
    c, al = math.cos(w), math.sin(w) / (2 * qq)
    n = 1 / (1 + al)
    return ((1 + c) * 0.5 * n, -(1 + c) * n, (1 + c) * 0.5 * n, -2 * c * n, (1 - al) * n)


def tone_scale(v):
    return 2 ** ((v - TONE_REF) / TONE_STEP)


def time_of(v, skew=64, right=False, tri=0.0, mod=0):
    """TIME knob (+ SKEW on the right, + MOD x the LFO's triangle) -> seconds."""
    norm = min(1.0, (v + mod * tri) / 127)
    y = norm * OCT + ((skew - 64) / 64 if right else 0)
    y = min(OCT, max(0.0, y))
    return T_MIN * 2 ** y


def regen_of(v):
    return REGEN_MAX * v / 127


def lfo_hz(v):
    return LFO_LO * (LFO_HI / LFO_LO) ** (v / 127)


# ------------------------------------------------------------------ r7 layout
COMMON = """CLR FIRST MIXT MIXC KFBT TONS TJOB RNG RND FRZM TMOD MODK RATK SIDE SC0 SC1 SC2 SC3 SC4""".split()
SECT = ('AA1', 'AA2', 'RC1', 'RC2', 'HPF')
CO = {nm: i for i, nm in enumerate(COMMON)}
COEF = {s: len(COMMON) + 5 * k for k, s in enumerate(SECT)}       # B0/2 B1/2 B2/2 A1/2 A2/2
assert COEF['HPF'] + 5 <= 64
SIDE = """CW PHW CA PHA CB PHB IW IA IB CUR PREV CTA CTB CENV S4 EENV DCX DCH DCL RENV FB4 WET
GA GB DGA DGB GART DGG GEND PQ KNT KNO FEED NFEED NZ FROZ KFB DCW DCA
MODE PCNT CNEW CEQ XF WAIT H LFO BON""".split()
SD = {nm: i for i, nm in enumerate(SIDE)}
HIST = {'AA': len(SIDE), 'RC': len(SIDE) + 6, 'HP': len(SIDE) + 12}  # x1 x2 | u1 u2 | y1 y2 ; HP: x1 x2 y1 y2
SIDE_WORDS = len(SIDE) + 16
assert SIDE_WORDS <= 64, SIDE_WORDS
SIDE_AT = (0x40, 0x80)          # r7 + : left, right
SREG = ('r1', 'r2')
ONE = '#>$7fffff'
QTR = '#>$200000'               # one clock edge in the phase words (phases are kept / 4)


def cs(nm):
    return f'x:(r7+${CO[nm]:02x})'


def cc(sec, k):
    return f'x:(r7+${COEF[sec] + k:02x})'


def sd(R, nm, k=0):
    off = SD[nm] if isinstance(nm, str) else nm
    return f'x:({R}+${off + k:02x})'


def lit(v):
    return f'#>${int(v) & 0xffffff:06x}'


class Asm:
    def __init__(self, pre):
        self.L, self.n, self.pre = [], 0, pre

    def __call__(self, op, *args, c=''):
        self.L.append(ins(op, *args, c=c))

    def lab(self, tag):
        self.n += 1
        return f'{self.pre}{tag}{self.n:03d}'

    def put(self, label):
        self.L.append(label + ':')

    def com(self, text):
        self.L.append('; ' + text)

    def raw(self, lines):
        self.L += lines if isinstance(lines, list) else [lines]


# ------------------------------------------------------------------ per-sample pieces
def biquad(A, R, sec, hin, hout, last):
    """DF1 section on x0 (in) -> x0 (out, rounded); coefs /2 at r7; histories (2 words each) in the side block.
    Shifts the input history; the output history is shifted by the next section (or here when last)."""
    A('move', cc(sec, 0), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', sd(R, hin, 0), 'y1')
    A('move', cc(sec, 1), 'y0')
    A('mac', 'y1', 'y0', 'a')
    A('move', sd(R, hin, 1), 'x1')
    A('move', cc(sec, 2), 'y0')
    A('mac', 'x1', 'y0', 'a')
    A('move', 'y1', sd(R, hin, 1))
    A('move', 'x0', sd(R, hin, 0))
    A('move', sd(R, hout, 0), 'y1')
    A('move', cc(sec, 3), 'y0')
    A('mac', '-y1', 'y0', 'a')
    A('move', sd(R, hout, 1), 'x1')
    A('move', cc(sec, 4), 'y0')
    A('mac', '-x1', 'y0', 'a')
    A('asl', 'a')
    A('rnd', 'a')
    A('move', 'a', 'x0')
    if last:
        A('move', 'y1', sd(R, hout, 1))
        A('move', 'x0', sd(R, hout, 0))


def peak_env(A, R, slot, rho):
    """a = |v|: slot = max(|v|, rho slot), also left in b."""
    A('move', sd(R, slot), 'x1')
    A('move', q(rho), 'y0')
    A('mpy', 'x1', 'y0', 'b')
    A('max', 'a', 'b')
    A('move', 'b', sd(R, slot))


def tick_read(A, R, end, pend):
    """one clock edge of a reader: a = its phase/4, r4 its bucket, x1 its charge-transfer state.
    pend: y1 = the old-clock buckets still to read before the rate switches (0 = none pending)."""
    A('cmp', QTR, 'a')
    A('blt', end)
    A('sub', QTR, 'a')
    if pend:
        nm = A.lab('rm')
        A('move', 'y1', 'b')
        A('tst', 'b')
        A('beq', nm)
        A('sub', '#>$1', 'b')
        A('move', 'b', 'y1')
        A('bne', nm)
        A('move', sd(R, 'CNEW'), 'b', c='the last old-clock bucket: the new rate from the next sample')
        A('move', 'b', sd(R, 'CA'))
        A.put(nm)
    A('move', 'x:(r4)+', 'x0')
    A('move', q(CTI), 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('move', q(1 - CTI), 'y0')
    A('mac', 'x1', 'y0', 'b')
    A('move', 'b', 'x1')


def side_sample(A, R, c):
    """One sample of one side. x:(r0) in; x:(r0)+ out."""
    h = HIST['AA']
    A.com(f'======== {"left" if c == 0 else "right"} side ========')
    A.com('the line input s/4 = x/4 + fb/4')
    A('move', 'x:(r0)', 'a')
    A('asr', '#$2', 'a', 'a')
    A('move', sd(R, 'FB4'), 'x0')
    A('add', 'x0', 'a')
    A('move', 'a', sd(R, 'S4'))
    A('abs', 'a')
    A.com('compressor detector: a peak at once, 45 ms release')
    peak_env(A, R, 'CENV', 1 - C_REL)
    A.com('gain sqrt(ref / max(env, floor)) = mantissa poly x 2^(k/2), applied to s/4 -> c/2')
    A('move', q(FL4), 'x0')
    A('cmp', 'x0', 'b')
    A('tlt', 'x0', 'b')
    A('clb', 'b', 'a', c='a1 = t <= 0: env 2^-t in [0.5, 1)')
    A('move', 'a1', 'y1')
    A('and', '#$1', 'a')
    A('add', 'y1', 'a')
    A('move', 'a1', 'x0', c='t rounded up to even')
    A('normf', 'x0', 'b', c='m in [0.25, 1)')
    A('move', 'x0', 'a')
    A('asr', 'a')
    A('sub', '#>$2', 'a', c='shift = t/2 - 2 (the poly is quartered)')
    A('move', 'a1', 'x1')
    A('move', 'b', 'x0')
    A('move', q(RS_C[3]), 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('add', q(RS_C[2]), 'b')
    A('move', 'b', 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('add', q(RS_C[1]), 'b')
    A('move', 'b', 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('add', q(RS_C[0]), 'b')
    A('move', 'b', 'y0')
    A('move', sd(R, 'S4'), 'x0')
    A('mpy', 'y0', 'x0', 'b')
    A('normf', 'x1', 'b', c='c/2 (|c| <= sqrt(ref env))')
    A('move', 'b', 'x0')
    A.com('anti-alias filter (2 sections; bucketSat\'s slope folded into the first)')
    biquad(A, R, 'AA1', h, h + 2, False)
    biquad(A, R, 'AA2', h + 2, h + 4, True)
    A('move', 'x0', sd(R, 'CUR'))
    A.com('reader A (the heard head): edges at its own rate; after a clock jump it keeps the old rate up to the '
          'last old-clock bucket')
    end = A.lab('ra')
    A('move', sd(R, 'IA'), 'r4')
    A('move', sd(R, 'CTA'), 'x1')
    A('move', sd(R, 'PCNT'), 'y1')
    A('move', sd(R, 'PHA'), 'a')
    A('move', sd(R, 'CA'), 'x0')
    A('add', 'x0', 'a')
    tick_read(A, R, end, True)
    tick_read(A, R, end, True)
    A.put(end)
    A('move', 'a', sd(R, 'PHA'))
    A('move', 'x1', sd(R, 'CTA'))
    A('move', 'y1', sd(R, 'PCNT'))
    A('move', 'r4', sd(R, 'IA'))
    A.com('reader B (only while a FADE crossfade runs)')
    end, e2 = A.lab('rb'), A.lab('rc')
    A('move', sd(R, 'BON'), 'a')
    A('tst', 'a')
    A('beq', end)
    A('move', sd(R, 'IB'), 'r4')
    A('move', sd(R, 'CTB'), 'x1')
    A('move', sd(R, 'PHB'), 'a')
    A('move', sd(R, 'CB'), 'x0')
    A('add', 'x0', 'a')
    tick_read(A, R, e2, False)
    tick_read(A, R, e2, False)
    A.put(e2)
    A('move', 'a', sd(R, 'PHB'))
    A('move', 'x1', sd(R, 'CTB'))
    A('move', 'r4', sd(R, 'IB'))
    A.put(end)
    A.com('the writer (the chip clock): the input interpolated at each edge + bucket noise; frozen: buckets leak in place')
    frz, done, e3, e4 = A.lab('wf'), A.lab('wd'), A.lab('we'), A.lab('wg')
    A('move', sd(R, 'IW'), 'r4')
    A('move', sd(R, 'PHW'), 'a')
    A('move', sd(R, 'CW'), 'x0')
    A('add', 'x0', 'a')
    A('move', sd(R, 'FROZ'), 'b')
    A('tst', 'b')
    A('bne', frz)
    A('move', sd(R, 'CUR'), 'x1')
    A('move', 'x1', 'b')
    A('move', sd(R, 'PREV'), 'x0')
    A('sub', 'x0', 'b')
    A('move', 'b', 'y1', c='d = cur - prev')
    for _ in range(2):
        A('cmp', QTR, 'a')
        A('blt', e3)
        A('sub', QTR, 'a')
        A('move', 'a', 'x0')
        A('move', sd(R, 'PQ'), 'y0')
        A('mpy', 'y0', 'x0', 'b')
        A('asl', '#$6', 'b', 'b', c='t = (phase - k)/c: how long before this sample the edge fell')
        A('move', 'b', 'x0')
        A('move', 'x1', 'b')
        A('mac', '-x0', 'y1', 'b', c='cur - t d')
        A('move', sd(R, 'NZ'), 'x0')
        A('add', 'x0', 'b')
        A('move', 'b', 'x:(r4)+')
    A.put(e3)
    A('move', 'x1', sd(R, 'PREV'))
    A('bra', done)
    A.put(frz)
    for _ in range(2):
        A('cmp', QTR, 'a')
        A('blt', e4)
        A('sub', QTR, 'a')
        A('move', 'x:(r4)', 'x0')
        A('move', q(LEAK), 'y0')
        A('mpy', 'y0', 'x0', 'b')
        A('move', 'b', 'x:(r4)+')
    A.put(e4)
    A('move', sd(R, 'CUR'), 'x1')
    A('move', 'x1', sd(R, 'PREV'))
    A.put(done)
    A('move', 'a', sd(R, 'PHW'))
    A('move', 'r4', sd(R, 'IW'))
    A.com('held (the heads crossfaded) + clock feedthrough, its sign the writer index\'s parity')
    A('move', sd(R, 'CTA'), 'x0')
    A('move', sd(R, 'GA'), 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('move', sd(R, 'CTB'), 'x0')
    A('move', sd(R, 'GB'), 'y0')
    A('mac', 'y0', 'x0', 'b')
    A('move', 'r4', 'a')
    A('move', sd(R, 'FEED'), 'x0')
    A('move', sd(R, 'NFEED'), 'y0')
    A('and', '#$1', 'a')
    A('teq', 'y0', 'a')
    A('tne', 'x0', 'a')
    A('add', 'a', 'b')
    A('move', 'b', 'x0')
    A.com('reconstruction filter')
    h = HIST['RC']
    biquad(A, R, 'RC1', h, h + 2, False)
    biquad(A, R, 'RC2', h + 2, h + 4, True)
    A('move', 'x0', 'b')
    A('move', cs('RND'), 'x0')
    A('move', sd(R, 'KNO'), 'y0')
    A('mac', '-y0', 'x0', 'b', c='+ output noise')
    A('move', 'b', 'x0', c='y (c/2 units)')
    A('move', 'x0', 'a')
    A('abs', 'a')
    A.com('expander detector: a peak at once, 60 ms release; gain/8 = env/(8 ref) in [0.0002/(8 ref), 6/8]')
    peak_env(A, R, 'EENV', 1 - E_REL)
    A('move', 'b', 'x1')
    A('move', q(1 / (8 * REF)), 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('asl', 'a')
    A('move', q(G_MAX_E / 8), 'y0')
    A('cmp', 'y0', 'a')
    A('tgt', 'y0', 'a')
    A('move', q(0.0002 / REF / 8), 'y0')
    A('cmp', 'y0', 'a')
    A('tlt', 'y0', 'a')
    A('move', 'a', 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('asl', 'a', c='y/8 = (c/2)(gain/8) 2')
    A.com('DC block (9 Hz), 48-bit state; out rounded')
    A('move', 'a', 'x0')
    A('move', 'x0', 'a')
    A('move', sd(R, 'DCX'), 'y0')
    A('sub', 'y0', 'a')
    A('move', 'x0', sd(R, 'DCX'))
    A('move', sd(R, 'DCH'), 'b')
    A('move', sd(R, 'DCL'), 'b0')
    A('add', 'b', 'a')
    A('move', sd(R, 'DCH'), 'x1')
    A('move', q(DC_K), 'y0')
    A('mac', '-x1', 'y0', 'a')
    A('move', 'a1', sd(R, 'DCH'))
    A('move', 'a0', sd(R, 'DCL'))
    A('rnd', 'a')
    A.com('artefact gate (ramped a sample)')
    A('move', 'a', 'x0')
    A('move', sd(R, 'GART'), 'b')
    A('move', 'b', 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', sd(R, 'DGG'), 'x1')
    A('add', 'x1', 'b')
    A('move', 'b', sd(R, 'GART'))
    A.com('wet rider: g = min(1, C/env), env a peak with 100 ms release')
    one, dn, dv = A.lab('gn'), A.lab('gd'), A.lab('gv')
    A('move', 'a', 'x0')
    A('abs', 'a')
    peak_env(A, R, 'RENV', 1 - R_REL)
    A('move', q(CEIL / 8), 'x1')
    A('cmp', 'x1', 'b')
    A('ble', one)
    A('move', 'b', 'y1')
    A('move', 'x1', 'a')
    A('andi', '#$fe', 'ccr')
    A('do', '#13', f'>{dv}')
    A('div', 'y1', 'a')
    A.put(dv)
    A('nop')
    A('move', 'a0', 'b')
    A('asl', '#$b', 'b', 'b', c='g = C/env (12 bits)')
    A('move', 'b', 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('bra', dn)
    A.put(one)
    A('move', 'x0', 'a')
    A.put(dn)
    A('move', 'a', 'x0')
    A('move', 'x0', sd(R, 'WET'))
    A.com('feedback high-pass (the identity when HPF is off); fb/4 = (kfb/2) hpf(wet/8) 4')
    biquad(A, R, 'HPF', HIST['HP'], HIST['HP'] + 2, True)
    A('move', sd(R, 'KFB'), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('asl', '#$2', 'a', 'a')
    A('move', 'a', sd(R, 'FB4'))
    A.com('crossfade gains and the SLEW clock ramp a sample')
    for g, dg in (('GA', 'DGA'), ('GB', 'DGB'), ('CW', 'DCW'), ('CA', 'DCA')):
        A('move', sd(R, g), 'a')
        A('move', sd(R, dg), 'x0')
        A('add', 'x0', 'a')
        A('move', 'a', sd(R, g))
    A.com('mix: out = dry + m (wet - dry)')
    A('move', sd(R, 'WET'), 'a')
    A('asl', '#$3', 'a', 'a')
    A('move', cs('MIXC'), 'y1')
    A('move', 'x:(r0)', 'b')
    A('sub', 'b', 'a')
    A('asr', 'a')
    A('move', 'a', 'x0')
    A('mpy', 'x0', 'y1', 'a')
    A('asl', 'a')
    A('add', 'b', 'a')
    A('move', 'a', 'x:(r0)+')


# ------------------------------------------------------------------ block-rate pieces
def knob7(A, word, hi):
    """a = a FUNC knob (bits 22..16 if hi else 14..8 of r6+word) as v/128 in Q23 (clean)."""
    A('move', f'x:(r6+${word:x})', 'a')
    if not hi:
        A('lsl', '#8', 'a')
    A('and', '#>$7f0000', 'a')
    A('move', 'a1', 'a')


def side_block(A, kg, kxf, gstep):
    """bdside: r3 = the side's block, SIDE = 1 (left) / 2 (right). Clock target, SLEW / FADE, gate, gains."""
    R = 'r3'
    after = A.lab('fa')
    A.put('bdside')
    A.com('LFO (triangle), 0.02 x 1000^(RATE/127) Hz (the right x 1.18)')
    A('move', '#>$7f0000', 'a')
    A('move', cs('RATK'), 'x0')
    A('sub', 'x0', 'a')
    A('move', 'a', 'x0')
    A('move', q(math.log2(LFO_HI / LFO_LO) * 128 / 127 / 32), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('bsr', '>bdexp')
    A('move', 'b', 'x0')
    A('move', q(LFO_HI * 16 / FS), 'y0')
    A('move', cs('SIDE'), 'a')
    A('cmp', '#>$1', 'a')
    lab = A.lab('lr')
    A('beq', lab)
    A('move', q(LFO_HI * 16 / FS * LFO_R), 'y0')
    A.put(lab)
    A('mpy', 'y0', 'x0', 'a')
    A('move', sd(R, 'LFO'), 'x0')
    A('add', 'x0', 'a')
    A('and', '#>$7fffff', 'a')
    A('move', 'a1', sd(R, 'LFO'))
    A('move', 'a1', 'a')
    A('sub', q(0.5), 'a')
    A('asl', 'a')
    A('abs', 'a')
    A('move', 'a', 'x0')
    A('move', ONE, 'a')
    A('sub', 'x0', 'a', c='tri in [0, 1]')
    A('move', 'a', 'x0')
    A('move', cs('MODK'), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', 'a', 'x0')
    A('move', q(128 / 127 / 2), 'y0')
    A('mpy', 'y0', 'x0', 'b')
    A('move', 'x:(r6)', 'x0')
    A('mac', 'y0', 'x0', 'b', c='norm/2 = (TIME + MOD tri) 128/127 / 2')
    A('move', q(0.5), 'x0')
    A('cmp', 'x0', 'b')
    A('tgt', 'x0', 'b')
    A('move', 'b', 'x0')
    A('move', q(OCT * 2 / 32), 'y0')
    A('mpy', 'y0', 'x0', 'a', c='y/32 = norm OCT/32')
    A('move', cs('SIDE'), 'b')
    A('cmp', '#>$1', 'b')
    lab = A.lab('sk')
    A('beq', lab)
    A('move', 'x:(r6+$1)', 'b')
    A('sub', q(0.5), 'b')
    A('asr', '#$4', 'b', 'b', c='(SKEW - 64)/64 octaves, /32')
    A('add', 'b', 'a')
    A.put(lab)
    A('move', '#>$0', 'x0')
    A('cmp', 'x0', 'a')
    A('tlt', 'x0', 'a')
    A('move', q(OCT / 32), 'x0')
    A('cmp', 'x0', 'a')
    A('tgt', 'x0', 'a')
    A('bsr', '>bdexp')
    A('move', 'b', 'x0')
    A('move', q(CMAX / 4), 'y0')
    A('mpy', 'y0', 'x0', 'a', c='c_t/4 = (cmax/4) 2^-y')
    A('move', 'a', cs('SC0'))
    A.com('freeze: FRZ, or this side\'s bit of the ColdFire\'s word r6+6')
    A('move', 'x:(r6+$6)', 'a')
    A('move', cs('SIDE'), 'x0')
    A('and', 'x0', 'a')
    A('move', cs('FRZM'), 'x0')
    A('or', 'x0', 'a')
    A('move', 'a1', sd(R, 'FROZ'))
    A.com('first call: everything snaps to the knobs')
    lab = A.lab('fs')
    A('move', cs('FIRST'), 'a')
    A('tst', 'a')
    A('beq', lab)
    A('move', cs('SC0'), 'a')
    for nm in ('CW', 'CA', 'CEQ', 'CNEW'):
        A('move', 'a', sd(R, nm))
    A('move', ONE, 'a')
    A('move', 'a', sd(R, 'GA'))
    A('move', lit(HOLD + 16), 'a')
    A('move', 'a', sd(R, 'H'))
    A('move', cs('KFBT'), 'a')
    A('move', 'a', sd(R, 'KFB'))
    A.put(lab)
    A('clr', 'a')
    for nm in ('DCW', 'DCA', 'DGA', 'DGB'):
        A('move', 'a', sd(R, nm))
    A.com('SLEW and locked (no transition, no pending switch): the clock glides (writer and head A together)')
    fade = A.lab('fd')
    A('move', sd(R, 'MODE'), 'a')
    A('move', sd(R, 'PCNT'), 'x0')
    A('or', 'x0', 'a')
    A('bne', fade)
    A('move', cs('TMOD'), 'a')
    A('tst', 'a')
    A('bne', fade)
    A('move', cs('SC0'), 'a')
    A('move', sd(R, 'CW'), 'x0')
    A('sub', 'x0', 'a')
    A('move', 'a', 'x1')
    A('move', q(kg / 16), 'y0')
    A('mpy', 'x1', 'y0', 'a', c='a sample: (target - c) kg / 16')
    A('move', 'a', sd(R, 'DCW'))
    A('move', 'a', sd(R, 'DCA'))
    A('asl', '#$4', 'a', 'a')
    A('move', sd(R, 'CW'), 'x0')
    A('add', 'x0', 'a')
    A('move', 'a', sd(R, 'CEQ'))
    A('move', 'a', sd(R, 'CNEW'))
    A('bra', after)
    # ---- the FADE machine
    A.put(fade)
    st1, st2, idle = A.lab('mx'), A.lab('mw'), A.lab('mi')
    A('move', sd(R, 'MODE'), 'a')
    A('tst', 'a')
    A('beq', idle)
    A('cmp', '#>$2', 'a')
    A('beq', st2)
    A('bra', st1)
    A.put(idle)
    A.com('idle: start when the target is > 1 % off the heard time (FADE, not frozen, no switch pending)')
    A('move', sd(R, 'PCNT'), 'a')
    A('tst', 'a')
    A('bne', after)
    A('move', sd(R, 'FROZ'), 'a')
    A('tst', 'a')
    A('bne', after)
    A('move', cs('TMOD'), 'a')
    A('tst', 'a')
    A('beq', after)
    A('move', sd(R, 'CEQ'), 'x0')
    A('move', q(0.01), 'y0')
    A('mpy', 'y0', 'x0', 'b', c='1 %')
    A('move', cs('SC0'), 'a')
    A('sub', 'x0', 'a')
    A('move', 'a', 'y1')
    A('abs', 'a')
    A('cmp', 'b', 'a')
    A('ble', after)
    A('move', 'y1', 'a')
    A('tst', 'a')
    lng = A.lab('lg')
    A('blt', lng)
    A.com('shortening: head B N = 4096 c_A/c_t buckets back on the old clock; crossfade')
    A('move', sd(R, 'CA'), 'a')
    A('move', cs('SC0'), 'x0')
    A('bsr', '>bddiv', c='b = c_A/c_t < 1')
    A('move', 'b1', 'a')
    A('asr', '#$b', 'a', 'a', c='N_B = 4096 c_A/c_t buckets')
    A('move', 'a1', 'x0')
    A('move', sd(R, 'IW'), 'a')
    A('sub', 'x0', 'a')
    A('and', '#>$000fff', 'a')
    A('move', 'a1', 'x0')
    A('move', sd(R, 'IW'), 'a')
    A('and', '#>$fff000', 'a', c='the ring base')
    A('or', 'x0', 'a')
    A('move', 'a1', sd(R, 'IB'))
    for s_, d_ in (('PHA', 'PHB'), ('CA', 'CB'), ('CTA', 'CTB')):
        A('move', sd(R, s_), 'a')
        A('move', 'a', sd(R, d_))
    A('clr', 'a')
    A('move', 'a', sd(R, 'XF'))
    A('move', '#>$1', 'a')
    A('move', 'a', sd(R, 'MODE'))
    A('move', 'a', sd(R, 'BON'))
    A('bra', after)
    A.put(lng)
    A.com('lengthening: the clock jumps now; head A keeps the old rate for the N_A old buckets; wait time + 96')
    A('move', cs('SC0'), 'a')
    A('move', 'a', sd(R, 'CW'))
    A('move', 'a', sd(R, 'CNEW'))
    A('bsr', '>bdnpend')
    A('move', q(2 ** -7), 'a')
    A('move', cs('SC0'), 'x0')
    A('bsr', '>bddiv', c='b = 2^-7/(c_t/4)')
    A('move', 'b1', 'a')
    A('asr', '#$6', 'a', 'a', c='wait = 1024/(c_t/4) samples')
    A('add', lit(WAIT_PAD), 'a')
    A('move', 'a1', sd(R, 'WAIT'))
    A('move', '#>$2', 'a')
    A('move', 'a', sd(R, 'MODE'))
    A('bra', after)
    A.put(st2)
    A.com('waiting for the ring to refill at the new clock; then head B at the full length, locked to the writer')
    A('move', sd(R, 'WAIT'), 'a')
    A('sub', '#>$10', 'a')
    A('move', 'a', sd(R, 'WAIT'))
    A('bgt', after)
    for s_, d_ in (('IW', 'IB'), ('PHW', 'PHB'), ('CW', 'CB'), ('CTA', 'CTB')):
        A('move', sd(R, s_), 'a')
        A('move', 'a', sd(R, d_))
    A('clr', 'a')
    A('move', 'a', sd(R, 'XF'))
    A('move', '#>$3', 'a')
    A('move', 'a', sd(R, 'MODE'))
    A('move', '#>$1', 'a')
    A('move', 'a', sd(R, 'BON'))
    A('bra', after)
    A.put(st1)
    A.com(f'crossfading (MODE 1 shortening, 3 lengthening): x += 16/({XF_S * 1000:.0f} ms); A = cos^2, B = sin^2, '
          'ramped a sample')
    fin = A.lab('xe')
    A('move', sd(R, 'XF'), 'a')
    A('add', q(kxf), 'a')
    A('move', 'a', sd(R, 'XF'))
    A('cmp', ONE, 'a')
    A('bge', fin)
    A('move', 'a', 'x0')
    A('bsr', '>bdsin')
    A('asl', 'a')
    A('move', 'a', 'x0')
    A('mpy', 'x0', 'x0', 'a', c='B = sin^2, A = cos^2 (amplitudes sum to 1: the heads meet before the expander)')
    A('move', 'a', 'x0')
    A('move', sd(R, 'GB'), 'b')
    A('sub', 'b', 'a')
    A('asr', '#$4', 'a', 'a')
    A('move', 'a', sd(R, 'DGB'))
    A('move', ONE, 'a')
    A('sub', 'x0', 'a')
    A('move', sd(R, 'GA'), 'b')
    A('sub', 'b', 'a')
    A('asr', '#$4', 'a', 'a')
    A('move', 'a', sd(R, 'DGA'))
    A('bra', after)
    A.put(fin)
    A.com('crossfade over: B becomes A')
    for s_, d_ in (('IB', 'IA'), ('PHB', 'PHA'), ('CB', 'CA'), ('CTB', 'CTA'), ('CB', 'CNEW')):
        A('move', sd(R, s_), 'a')
        A('move', 'a', sd(R, d_))
    A('move', ONE, 'a')
    A('move', 'a', sd(R, 'GA'))
    A('clr', 'a')
    A('move', 'a', sd(R, 'GB'))
    A('move', 'a', sd(R, 'BON'))
    A('move', sd(R, 'MODE'), 'b')
    A('move', 'a', sd(R, 'MODE'))
    A('cmp', '#>$3', 'b')
    lab = A.lab('xl')
    A('beq', lab)
    A.com('after a shortening crossfade: the clock jumps (1/2048 slow: the head can never pass the writer)')
    A('move', cs('SC0'), 'x0')
    A('move', q(1 - 1 / 2048), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', 'a', sd(R, 'CW'))
    A('move', 'a', sd(R, 'CNEW'))
    A('bsr', '>bdnpend')
    A('move', cs('SC0'), 'a')
    A('move', 'a', sd(R, 'CEQ'))
    A('bra', after)
    A.put(lab)
    A('move', sd(R, 'CW'), 'a')
    A('move', 'a', sd(R, 'CEQ'))
    A.put(after)
    A.com('writer period: PQ = (1/64)/(c_w/4)')
    A('move', q(1 / 64), 'a')
    A('move', sd(R, 'CW'), 'x0')
    A('bsr', '>bddiv')
    A('move', 'b', sd(R, 'PQ'))
    A.com('activity: the compressor detector over -72 dBFS (or frozen) keeps the artefacts on; 1.1 s quiet -> off')
    A('move', sd(R, 'H'), 'a')
    A('add', '#>$10', 'a')
    A('move', lit(HOLD + 16), 'x0')
    A('cmp', 'x0', 'a')
    A('tgt', 'x0', 'a')
    A('move', sd(R, 'CENV'), 'b')
    A('move', q(THR / 4), 'x0')
    A('cmp', 'x0', 'b')
    lab = A.lab('ga')
    A('blt', lab)
    A('clr', 'a')
    A.put(lab)
    A('move', sd(R, 'FROZ'), 'b')
    A('tst', 'b')
    lab = A.lab('gz')
    A('beq', lab)
    A('clr', 'a')
    A.put(lab)
    A('move', 'a', sd(R, 'H'))
    A('move', lit(HOLD), 'x0')
    A('move', ONE, 'b')
    A('move', '#>$0', 'y0')
    A('cmp', 'x0', 'a')
    A('tgt', 'y0', 'b', c='the gate target')
    A('move', sd(R, 'GART'), 'x0')
    A('sub', 'x0', 'b')
    A('move', q(gstep), 'y0')
    A('cmp', 'y0', 'b')
    A('tgt', 'y0', 'b')
    A('move', q(-gstep), 'y0')
    A('cmp', 'y0', 'b')
    A('tlt', 'y0', 'b')
    A('move', 'b', 'a')
    A('asr', '#$4', 'a', 'a')
    A('move', 'a', sd(R, 'DGG'))
    A('add', 'x0', 'b')
    A('move', 'b', sd(R, 'GEND'))
    A('move', 'b', 'x0')
    for nm, amp in (('KNT', NOISE_TICK / 2 / 2), ('KNO', NOISE_OUT / 2 / 2), ('FEED', FEED / 2)):
        A('move', q(amp), 'y0')
        A('mpy', 'y0', 'x0', 'a')
        A('move', 'a', sd(R, nm))
    A('neg', 'a')
    A('move', 'a', sd(R, 'NFEED'))
    A.com('feedback gain glides to FDBK (0 while frozen: the plugin cuts the loop)')
    A('move', cs('KFBT'), 'a')
    A('move', sd(R, 'FROZ'), 'b')
    A('tst', 'b')
    lab = A.lab('kz')
    A('beq', lab)
    A('clr', 'a')
    A.put(lab)
    A('move', sd(R, 'KFB'), 'b')
    A('sub', 'b', 'a')
    A('asr', '#$3', 'a', 'a')
    A('add', 'a', 'b')
    A('move', 'b', sd(R, 'KFB'))
    A('rts')
    A.put('bdnpend')
    A.com('PCNT = the buckets head A still reads at the old rate = (IW - IA) mod 4096 (0 -> 4096)')
    A('move', sd(R, 'IW'), 'a')
    A('move', sd(R, 'IA'), 'x0')
    A('sub', 'x0', 'a')
    A('sub', '#>$1', 'a')
    A('and', '#>$000fff', 'a')
    A('add', '#>$1', 'a')
    A('move', 'a1', sd(R, 'PCNT'))
    A('rts')


def tone_job(A):
    """bdtone: retunes filter section TJOB (0..3) from TONS; on the first call all four."""
    hmax = math.pi * AA_HZ / FS * tone_scale(127)          # h = pi fc / fs at TONE 127 (AA); RC x 6200/6800
    assert hmax < 1.1 and AA_HZ * tone_scale(127) < 0.45 * FS
    A.put('bdtone')
    one = A.lab('tj')
    A('move', cs('FIRST'), 'a')
    A('tst', 'a')
    A('beq', one)
    for k in range(4):
        A('move', lit(k), 'a')
        A('move', 'a', cs('TJOB'))
        A('bsr', '>bdsect')
    A('rts')
    A.put(one)
    A('move', cs('TJOB'), 'a')
    A('add', '#>$1', 'a')
    A('and', '#>$3', 'a')
    A('move', 'a1', cs('TJOB'))
    A.put('bdsect')
    A.com('h/2 = (hmax/2) 2^-y, y = (127 - TONE)/24 (x 6200/6800 for RC); 1/(2Q); gain; destination')
    A('move', '#>$7f0000', 'a')
    A('move', cs('TONS'), 'x0')
    A('sub', 'x0', 'a')
    A('move', '#>$0', 'x0')
    A('cmp', 'x0', 'a')
    A('tlt', 'x0', 'a')
    A('move', 'a', 'x0')
    A('move', q(128 / TONE_STEP / 32), 'y0')
    A('mpy', 'y0', 'x0', 'a', c='y/32')
    A('bsr', '>bdexp')
    A('move', 'b', 'x0')
    A('move', cs('TJOB'), 'a')
    A('move', q(hmax / 2), 'y0')
    A('move', q(hmax / 2 * RC_HZ / AA_HZ), 'y1')
    A('cmp', '#>$2', 'a')
    lab = A.lab('ta')
    A('blt', lab)
    A('move', 'y1', 'y0')
    A.put(lab)
    A('mpy', 'y0', 'x0', 'a')
    A('move', 'a', cs('SC1'), c='hh = h/2')
    A.com('per section: 1/(2Q), the gain (bucketSat slope on AA1), where the five words go')
    A('move', cs('TJOB'), 'a')
    for k, (iq, g, sec) in enumerate(((1 / (2 * Q1), BUCKET_GAIN, 'AA1'), (1 / (2 * Q2), 1.0, 'AA2'),
                                      (1 / (2 * Q1), 1.0, 'RC1'), (1 / (2 * Q2), 1.0, 'RC2'))):
        nxt = A.lab('tk')
        A('cmp', lit(k), 'a')
        A('bne', nxt)
        A('move', q(iq), 'x0')
        A('move', 'x0', cs('SC2'))
        A('move', q(g), 'x0')
        A('move', 'x0', cs('SC3'))
        A('move', 'r7', 'b')
        A('add', lit(COEF[sec]), 'b')
        A('move', 'b1', 'r4')
        A.put(nxt)
    A.com('S2 = sin(h)/2 = hh (1 - hh^2 (2/3 - hh^2 (2/15 - hh^2 4/315))); C = cos h (series in u = hh^2)')
    A('move', cs('SC1'), 'x0')
    A('mpy', 'x0', 'x0', 'a')
    A('move', 'a', 'x1', c='u')
    A('move', q(4 / 315), 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('neg', 'a')
    A('add', q(2 / 15), 'a')
    A('move', 'a', 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('neg', 'a')
    A('add', q(2 / 3), 'a')
    A('move', 'a', 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('neg', 'a')
    A('add', ONE, 'a')
    A('move', 'a', 'y0')
    A('mpy', 'y0', 'x0', 'a', c='S2 = sin h / 2')
    A('move', 'a', cs('SC4'))
    A.com('C = 1 - 2u + (2/3)u^2 - (4/45)u^3 + (2/315)u^4')
    A('move', q(2 / 315), 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('sub', q(4 / 45), 'a')
    A('move', 'a', 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('add', q(2 / 3), 'a')
    A('move', 'a', 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('move', 'a', 'y0')
    A('mpy', 'x1', 'y0', 'a', c='u (2/3 u - ...)')
    A('move', 'x1', 'b')
    A('asl', 'b', c='2u')
    A('sub', 'b', 'a')
    A('add', ONE, 'a', c='C = cos h')
    A('move', 'a', 'x0')
    A('move', 'x0', cs('SC1'), c='(hh no longer needed) C')
    A.com('P = S2 C = sin(w)/4; al = P (2/Q) = (P/(2Q)) 4; cos w = C^2 - 4 S2^2; s2 = sin^2 h = 4 S2^2')
    A('move', cs('SC4'), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', 'a', 'x1', c='P')
    A('move', cs('SC2'), 'y0')
    A('mpy', 'x1', 'y0', 'a')
    A('asl', '#$2', 'a', 'a', c='al')
    A('move', 'a', cs('SC2'), c='(1/(2Q) no longer needed) al')
    A('add', ONE, 'a')
    A('asr', 'a', c='(1 + al)/2')
    A('move', 'a', 'y1')
    A('move', q(0.25), 'a')
    A('move', 'y1', 'x0')
    A('bsr', '>bddiv', c='b = nh = n/2 = 0.5/(1 + al)')
    A('move', 'b', 'y1', c='nh')
    A('move', cs('SC4'), 'x0')
    A('mpy', 'x0', 'x0', 'a')
    A('asl', '#$2', 'a', 'a')
    A('move', 'a', 'x1', c='s2 = sin^2 h')
    A('move', cs('SC1'), 'x0')
    A('mpy', 'x0', 'x0', 'b', c='C^2')
    A('sub', 'a', 'b', c='cos w = C^2 - s2')
    A('move', 'b', 'x0')
    A('mpy', 'x0', 'y1', 'b', c='cos w n/2')
    A('asl', 'b')
    A('neg', 'b')
    A('move', 'b', 'x:(r4+$3)', c='A1/2 = -cos w n')
    A('move', ONE, 'b')
    A('move', cs('SC2'), 'x0')
    A('sub', 'x0', 'b')
    A('move', 'b', 'x0')
    A('mpy', 'x0', 'y1', 'b', c='A2/2 = (1 - al) n/2')
    A('move', 'b', 'x:(r4+$4)')
    A('move', 'x1', 'x0')
    A('mpy', 'x0', 'y1', 'b', c='s2 n/2 = b0/2')
    A('move', 'b', 'x0')
    A('move', cs('SC3'), 'y0')
    A('mpy', 'y0', 'x0', 'b', c='x the section gain')
    A('move', 'b', 'x:(r4)')
    A('move', 'b', 'x:(r4+$2)')
    A('asl', 'b')
    A('move', 'b', 'x:(r4+$1)', c='B1/2 = b0')
    A('rts')


def build(xbase=HW_XBASE, test=False):
    A = Asm('bd')
    kg = 1 - math.exp(-2 * math.pi * GLIDE_HZ * 16 / FS)
    kxf = 16 / (XF_S * FS)
    gstep = 16 / (GATE_S * FS)
    head = f"""; CYCLES_FORWARD_BRANCHES
; AERIA BBD -- Chloaelis' bucket-brigade delay (DSP.h BBDLine in DelayCell) in 24-bit fixed point. Generated by bbd/gen.py.
; Dual mono, hard panned. Per side: s = x + fb -> 2:1 compressor -> anti-alias 4-pole (6.8 kHz x TONE) -> {NB} buckets
; clocked at {NB}/time (the input interpolated at each edge; bucket noise) -> read on edges through the charge-transfer
; one-pole ({CTI}) and held -> + clock feedthrough -> reconstruction 4-pole (6.2 kHz x TONE) -> + noise -> 1:2 expander
; -> DC block -> wet rider (C = {CEIL} FS) -> fb = 1.05 regen hpf(wet). No tanh anywhere (see gen.py).
;  knob 0 TIME  {T_MIN * 1000:.0f} ms x {T_MAX / T_MIN:.2f}^(v/127)          knob 1 SKEW  right time x 2^((v-64)/64)
;  knob 2 FDBK  regen {REGEN_MAX} v/127 (x 1.05 in the loop)   knob 3 TONE  filters x 2^((v-{TONE_REF})/{TONE_STEP})
;  knob 4 MOD   LFO>Time depth (the plugin's, in knob space)   knob 5 MIX   0 = exact dry
;  FUNC r6+$c: HPF (22..16) FRZ (14..8); r6+$d: RFRZ (22..16, the ColdFire's) TMOD (14..8: 0 SLEW, 1 FADE);
;  r6+$e: RATE (22..16) LFO 0.02 .. 20 Hz. r6+6: the ColdFire's random-freeze word (bit 0 left, bit 1 right).
; Rings in X, {NB} words a side (left at the base, right at base + $1000), base a multiple of $2000:
;  {'X:$10000 (TEST BUILD)' if test else 'the word X:$213 points to, used as an X address (core 1 X:$2000)'}
; FADE (time changes without bends, one ring a side): shortening -- a second head at the new time on the old clock,
; crossfaded ({XF_S * 1000:.0f} ms), then the clock jumps and the head keeps the old rate until the last old-clock bucket;
; lengthening -- the clock jumps, the heard head keeps the old rate up to that bucket, and once the ring has refilled at
; the new clock (time + 96 samples, as the plugin's waitN) a head at the full length is crossfaded in.
; common r7: {' '.join(f'{v:02x} {k}' for k, v in CO.items())}  coefs {' '.join(f'{v:02x} {k}' for k, v in COEF.items())}
; sides r7+$40 / r7+$80 (r1 / r2 in the loop, r3 in bdside): {' '.join(f'{v:02x} {k}' for k, v in SD.items())}
; histories {HIST}
"""
    A.raw(head.rstrip('\n').split('\n'))
    # ================================================================ init
    A.put('init')
    A('move', '#>$ffffff', 'm4')
    A('move', 'r7', 'r4')
    A('clr', 'a')
    A('do', '#$c0', '>bdzero')
    A('move', 'a', 'x:(r4)+')
    A.put('bdzero')
    A('nop')
    if test:
        A('move', lit(xbase), 'a')
    else:
        A('move', 'x:>$213', 'r4')
        A('nop')
        A('move', 'x:(r4)', 'a')
    for c in range(2):
        A('move', 'r7', 'b')
        A('add', lit(SIDE_AT[c]), 'b')
        A('move', 'b1', 'r4')
        for nm in ('IW', 'IA'):
            A('move', 'a', f'x:(r4+${SD[nm]:02x})')
        A('add', '#>$1000', 'a')
    A('move', '#>$10', 'a')
    A('move', 'a', cs('CLR'))
    A('move', '#>$1', 'a')
    A('move', 'a', cs('FIRST'))
    A('move', '#>$12345', 'a')
    A('move', 'a', cs('RNG'))
    A('rts')

    # ================================================================ proc
    A.put('proc')
    A('move', '#>$ffffff', 'm4')
    A('move', 'r7', 'a')
    A('add', '#>$40', 'a')
    A('move', 'a1', 'r1')
    A('add', '#>$40', 'a')
    A('move', 'a1', 'r2')
    A.com('bounded clearing: 16 dry calls, 256 words of each ring a call')
    A('move', cs('CLR'), 'a')
    A('tst', 'a')
    A('beq', 'bdready')
    A('sub', '#>$1', 'a')
    A('move', 'a', cs('CLR'))
    A('asl', '#$8', 'a', 'a')
    A('move', 'a1', 'x0')
    for c in range(2):
        lab = A.lab('cz')
        A('move', sd(SREG[c], 'IW'), 'a', c='(the ring base: the writer has not moved yet)')
        A('add', 'x0', 'a')
        A('move', 'a1', 'r4')
        A('clr', 'a')
        A('do', '#256', f'>{lab}')
        A('move', 'a', 'x:(r4)+')
        A.put(lab)
        A('nop')
    A('rts')
    A.put('bdready')
    A.com('MIX target (127 = all wet), snapped when within 256 LSB')
    lab = A.lab('mt')
    A('move', 'x:(r6+$5)', 'a')
    A('move', '#>$7f0000', 'x0')
    A('cmp', 'x0', 'a')
    A('blt', lab)
    A('move', ONE, 'a')
    A.put(lab)
    A('move', 'a', cs('MIXT'))
    lab = A.lab('ms')
    A('move', cs('MIXC'), 'b')
    A('sub', 'b', 'a')
    A('abs', 'a')
    A('cmp', '#>$000100', 'a')
    A('bge', lab)
    A('move', cs('MIXT'), 'a')
    A('move', 'a', cs('MIXC'))
    A.put(lab)
    A.com('FDBK: kfb/2 = 1.05 x 1.15 v/127 / 2 (each side glides to it)')
    A('move', 'x:(r6+$2)', 'x0')
    A('move', q(FB_K * REGEN_MAX * 128 / 127 / 2), 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('move', 'a', cs('KFBT'))
    A.com('TONE glide (1/8 a call; snapped on the first call), one filter section retuned a call')
    lab = A.lab('tf')
    A('move', 'x:(r6+$3)', 'a')
    A('move', cs('TONS'), 'b')
    A('sub', 'b', 'a')
    A('asr', '#$3', 'a', 'a')
    A('add', 'a', 'b')
    A('move', cs('FIRST'), 'a')
    A('tst', 'a')
    A('beq', lab)
    A('move', 'x:(r6+$3)', 'b')
    A.put(lab)
    A('move', 'b', cs('TONS'))
    A('bsr', '>bdtone')
    A.com('HPF: the 320 Hz coefficients or the identity')
    knob7(A, 0xc, True)
    lab, lab2 = A.lab('hp'), A.lab('hq')
    A('tst', 'a')
    A('beq', lab)
    for k, v in enumerate(hp_coefs(HPF_HZ, HPF_Q)):
        A('move', q(v / 2), 'a')
        A('move', 'a', cc('HPF', k))
    A('bra', lab2)
    A.put(lab)
    for k, v in enumerate((1.0, 0, 0, 0, 0)):
        A('move', q(v / 2), 'a')
        A('move', 'a', cc('HPF', k))
    A.put(lab2)
    A.com('FRZ (manual, both sides), TMOD, MOD, RATE')
    knob7(A, 0xc, False)
    A('move', 'a', cs('FRZM'))
    knob7(A, 0xd, False)
    A('move', 'a', cs('TMOD'))
    A('move', 'x:(r6+$4)', 'a')
    A('move', 'a', cs('MODK'))
    knob7(A, 0xe, True)
    A('move', 'a', cs('RATK'))
    for c in range(2):
        A('move', SREG[c], 'r3')
        A('move', '#>$%x' % (1 << c), 'a')
        A('move', 'a', cs('SIDE'))
        A('bsr', '>bdside')
    A('move', '#>$0', 'a')
    A('move', 'a', cs('FIRST'))

    # ================================================================ the sample loop
    A('move', '#>$fff', 'm4')
    A('do', 'n7', '>bdloop')
    A.com('mix smoothing; the noise word (24-bit LCG)')
    A('move', cs('MIXT'), 'a')
    A('move', cs('MIXC'), 'b')
    A('sub', 'b', 'a')
    A('asr', '#$7', 'a', 'a')
    A('add', 'a', 'b')
    A('move', 'b', cs('MIXC'))
    A('move', cs('RNG'), 'x0')
    A('move', '#>$0343fd', 'y0')
    A('mpy', 'y0', 'x0', 'a')
    A('asr', 'a')
    A('move', 'a0', 'x0')
    A('move', 'x0', 'a')
    A('add', '#>$269ec3', 'a')
    A('move', 'a1', cs('RNG'))
    A('move', 'a1', cs('RND'), c='a signed uniform word')
    for c in range(2):
        R = SREG[c]
        A('move', cs('RND'), 'x0')
        A('move', sd(R, 'KNT'), 'y0')
        A('mpy', 'y0', 'x0', 'a')
        A('move', 'a', sd(R, 'NZ'))
    for c in range(2):
        side_sample(A, SREG[c], c)
    A.put('bdloop')
    A('nop')
    A('move', '#>$ffffff', 'm4')
    for c in range(2):
        R = SREG[c]
        A('move', sd(R, 'GEND'), 'a')
        A('move', 'a', sd(R, 'GART'))
    A('rts')

    # ================================================================ subroutines (after their callers)
    side_block(A, kg, kxf, gstep)
    tone_job(A)
    A.raw(e2_sub('bdexp', 'bdxpend'))
    A.raw(['bddiv:', '; a1 / x0 (0 <= a1 < x0) -> b (Q23)',
           ins('move', 'a1', 'a'), ins('andi', '#$fe', 'ccr'),
           ins('do', '#24', '>bddvend'), ins('div', 'x0', 'a'), 'bddvend:', ins('nop'),
           ins('move', 'a0', 'b'), ins('rts')])
    A.raw(['bdsin:', '; a = sin(pi x0 / 2) / 2 for x0 in [-1, 1] (odd quintic); clobbers x1 y0',
           ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x1'), ins('move', q(SIN_C[2]), 'y0'),
           ins('mpy', 'x1', 'y0', 'a'), ins('add', q(SIN_C[1]), 'a'), ins('move', 'a', 'y0'),
           ins('mpy', 'x1', 'y0', 'a'), ins('add', q(SIN_C[0]), 'a'), ins('move', 'a', 'y0'),
           ins('mpy', 'y0', 'x0', 'a'), ins('rts')])
    return finish(A.L)


if __name__ == '__main__':
    (HERE / 'bbd.asm').write_text(build())
    (HERE / 'bbd_test.asm').write_text(build(TEST_XBASE, True))
    print('wrote bbd.asm, bbd_test.asm; rsqrt poly error %.2e' % RS_ERR)
