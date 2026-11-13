#!/usr/bin/env python3
"""Generates tape.asm (AERIA TAPE: Chloaelis' tape delay, DSP.h TapeLine + DelayCell, in 24-bit fixed point).

    python3 tape/gen.py               full build  -> tape.asm   (Y ring at the base word, X ring at X:$4000)
    python3 tape/gen.py --test        harness builds -> tape_test.asm (Y window 15,870 words, X ring at X:$10000)

Two hard-panned tape lines (left in -> left line -> left out, right -> right), dual mono, as Chloaelis.
Each line: record head (pre-emphasis; NO magnetic saturation) > a loop of oxide that wears > playback head
(gap loss that follows the wear and the damage, 95 Hz head bump, de-emphasis, dropouts) with wow and flutter,
hiss (fades, never steps) and the crackle of a worn splice, an 8 Hz DC blocker; regeneration through the
optional 320 Hz feedback high-pass, bounded by gain-riding (no tanh, no clipping).

The tape runs at 22,050 Hz (half rate): every second frame the input is decimated (polyphase IIR halfband),
one tape sample is processed per side, and the result is interpolated back (the same halfband). That is
what fits 0.73 s a side into ECHO's memory (16K words a side); the tape's own bandwidth is then ~9 kHz.
The plugin's code is sample-rate aware: every coefficient below is its formula at sr = 22,050.
"""
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins as _ins, q, finish, snap_close  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = 44100
SR = 22050                      # the tape's rate
ONE = '#>$7fffff'
SCALE = 8                       # the loop runs at 1/8 (headroom for pre-emphasis + regen)

# ---------------------------------------------------------------- the plugin's constants at SR
def opc(fc, sr=SR):
    return 1 - math.exp(-2 * math.pi * fc / sr)


PRE_A = opc(2400)               # pre-emphasis / de-emphasis one-pole
HISS_A = opc(11000)
HISS_HA = opc(900)
WOW_A = opc(0.55)
SLEW_A = opc(1.6)               # the capstan servo
XF_STEP = 1 / (0.07 * SR)       # fade-mode crossfade, 70 ms
WOW_DEPTH = 0.0021 * SR         # samples (~0.5 % when new)
FLUT_DEPTH = 0.000028 * SR
REC_GAIN = 1.15 * (1 - math.tanh(0.04) ** 2)   # the slope of the plugin's tanh(1.15 e + 0.04) at 0 (saturation dropped)
FB_GAIN = 1.05                  # the plugin's 1.05 tanh(f regen): its slope at 0
BUMP_F, BUMP_Q, BUMP_DB = 95.0, 0.9, 2.5
HPF_F, HPF_Q = 320.0, 0.6
DC_R = 1 - 2 * math.pi * 8 / SR
CRK_DECAY = 0.92 ** (48000 / SR)            # the plugin's 0.92 a sample at 48 kHz, per 22 kHz sample
GAP_HZ = 15000.0
KBLOCK = 256                    # damage block, samples (the plugin: 512 at 48 kHz = 10.7 ms; here 11.6 ms)
PLUGIN_NB = int(1.75 * 48000 + 16) // 512 + 1    # 165 blocks on the plugin's tape
SPLICE = round(600 * SR / 48000)                 # 276: the plugin's 600 samples at 48 kHz
LOOP_MIN = round(256 * SR / 48000)               # 118
LIFE = 600.0                    # seconds from new to ruined at WEAR 64 (the plugin's tapeLifeSec)
CEIL = 0.3                      # gain-riding: g = min(1, CEIL / (k env)), env = the wet's peak envelope (0.3 s release);
                                # a singing loop settles where g k |H| = 1 (env ~ CEIL |H(f_osc)|) and its peaks stay under 0.9 FS
RIDE_REL = 0.3                  # the gain-riding envelope's release, s (long: a noise-like singing loop's peaks stay under the envelope)
UPD = 8                         # a side's controls are updated every UPD calls (sides alternate)
TPU = 8 * UPD                   # ticks between a side's updates
RSH = int(math.log2(TPU))
WEAR_EVERY = 32                 # the age, the life gate and sqrt(age) every 32 calls

# knobs
T_MIN, T_MAX = 0.030, 0.700     # TIME (log)
WOW_UNIT = 32                   # WOW 32 = the plugin's depth, 127 = x 3.97
FB_KNEE, FB_KNEE_V, FB_TOP = 0.6, 115, 1.15     # FDBK: linear to 0.6 at 115, then to the plugin's 1.15 at 127


def regen_of(v):
    return FB_KNEE * v / FB_KNEE_V if v <= FB_KNEE_V else FB_KNEE + (FB_TOP - FB_KNEE) * (v - FB_KNEE_V) / (127 - FB_KNEE_V)


def time_of(v, skew=64, right=False):
    t = T_MIN * (T_MAX / T_MIN) ** (v / 127)
    if right:
        t *= 2 ** ((skew - 64) / 64)
    return min(T_MAX, t)


def life_of(v):
    """seconds from new to ruined; None = OFF"""
    return None if v == 0 else LIFE * 2 ** ((64 - v) / 16)


# ---------------------------------------------------------------- halfband (polyphase IIR, elliptic)
def hb_coefs(n, tr):
    """Valenzuela-Constantinides / hiir: n allpass coefficients, transition tr (fraction of the full rate)."""
    k = math.tan((1 - tr * 2) * math.pi / 4) ** 2
    kk = (1 - k * k) ** 0.25
    e = 0.5 * (1 - kk) / (1 + kk)
    e4 = e ** 4
    qq = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))
    order = 2 * n + 1
    out = []
    for c in range(1, n + 1):
        i, j, num = 0, 1, 0.0
        while True:
            t = qq ** (i * (i + 1)) * math.sin((i * 2 + 1) * c * math.pi / order) * j
            num += t; j = -j; i += 1
            if abs(t) < 1e-100: break
        i, j, den = 1, -1, 0.0
        while True:
            t = qq ** (i * i) * math.cos(i * 2 * c * math.pi / order) * j
            den += t; j = -j; i += 1
            if abs(t) < 1e-100: break
        ww = num * qq ** 0.25 / (den + 0.5)
        w2 = ww * ww
        x = math.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2)
        out.append((1 - x) / (1 + x))
    return out


HB_N, HB_TR = 3, 0.06
HB = hb_coefs(HB_N, HB_TR)      # path 0 gets coefficients 0 and 2, path 1 gets 1
HBQ = [round(c * (1 << 23)) / (1 << 23) for c in HB]
P0, P1 = HBQ[0::2], HBQ[1::2]


def hb_response(f, fs=FS):
    z = complex(math.cos(2 * math.pi * f / fs), math.sin(2 * math.pi * f / fs))
    z2 = z * z
    a0 = a1 = 1
    for c in P0:
        a0 *= (c + 1 / z2) / (1 + c / z2)
    for c in P1:
        a1 *= (c + 1 / z2) / (1 + c / z2)
    return 0.5 * (a0 + a1 / z)


def hb_delay():
    """group delay of the halfband at low frequency, full-rate samples"""
    f, df = 50.0, 1.0
    p1 = math.atan2(hb_response(f).imag, hb_response(f).real)
    p2 = math.atan2(hb_response(f + df).imag, hb_response(f + df).real)
    return -(p2 - p1) / (2 * math.pi * df / FS)


LAT = 2 * hb_delay() / 2        # decimator + interpolator, in tape samples (2 x full-rate delay / 2)


# ---------------------------------------------------------------- the bump / HPF as TPT state-variable filters
def svf(fc, qf, sr=SR):
    g = math.tan(math.pi * fc / sr)
    k = 1 / qf
    a1 = 1 / (1 + g * (g + k))
    return g, k, a1, g * a1, g * g * a1


BUMP_AMP = 10 ** (BUMP_DB / 40)
B_G, B_K, B_A1, B_A2, B_A3 = svf(BUMP_F, BUMP_Q * BUMP_AMP)     # RBJ peak = x + (A^2 - 1) k bp, bp's Q = A Q
B_KB = (BUMP_AMP ** 2 - 1) * B_K
B_F = 2 * math.sin(math.pi * BUMP_F / SR)     # the DSP's bump is a Chamberlin SVF (same fc, same q = 1/(A Q))
H_G, H_K, H_A1, H_A2, H_A3 = svf(HPF_F, HPF_Q)
H_F = 2 * math.sin(math.pi * HPF_F / SR)       # the DSP's HPF is a Chamberlin SVF's high-pass output


# flutter: each side's two capstan / pinch-roller rates (the plugin draws 6.1 + 0.8 r and 11.3 + 1.5 r Hz) and phases
_r = random.Random(0x7a9e)
FLUT = [(6.1 + _r.random() * 0.8, 11.3 + _r.random() * 1.5, _r.random(), _r.random()) for _ in range(2)]

# ---------------------------------------------------------------- memory
DMG_STRIDE = 64                 # the two damage maps sit after the Y ring: left at +0, right at +64


class Build:
    def __init__(self, yring, ywin, xring, xbase, out):
        assert yring % KBLOCK == 0 and xring % KBLOCK == 0 and yring == xring
        self.yring, self.xring, self.xbase, self.out = yring, xring, xbase, out
        self.nb = (yring // KBLOCK, xring // KBLOCK)
        assert max(self.nb) <= DMG_STRIDE
        self.ywords = yring + 2 * DMG_STRIDE              # Y used from the base: ring + both damage maps
        self.yclr = math.ceil(self.ywords / 128)
        self.xclr = math.ceil(xring / 128)
        assert self.yclr * 128 <= ywin and self.xclr * 128 <= 16384, (self.yclr, ywin)

    def ring(self, side):
        return (self.yring, self.xring)[side]

    def dmax(self, side):
        return self.ring(side) - WOB_MAX - 8


WOB_MAX = math.ceil(WOW_DEPTH * 2.6 * 127 / WOW_UNIT + FLUT_DEPTH * 3 * 1.35 * 1.5 * 127 / WOW_UNIT) + 4
BUILDS = {
    'tape.asm': dict(yring=16128, ywin=16384, xring=16128, xbase=0x4000),
    'tape_test.asm': dict(yring=15616, ywin=15870, xring=15616, xbase=0x10000),   # rings of one size: one mask (m1)
}

# ---------------------------------------------------------------- r7 map
GL = """PH CLR MIXS CM1 DCM1 SG DSG REGS WOWS AGEH AGEL LG EAGE WEAR HG HLV HPON FRZK TMOD RFPV SEED T1 T2 T3 T4 HL
HGL FIRST YB SBL SBR CBL CBR WY WX SGT CMT CNT MIXP T5""".split()
G = {n: i for i, n in enumerate(GL)}
assert len(GL) <= 0x40
SS = """RH RL SH SL RBH RBL SBH SBL GA DGA GB DGB FB PRE GP1 GP2 BS1 BS2 DEM HHP CRK DCX DCY HS1 HS2 ENV RG DRG
GAPA DROP KFB DKFB HPG DHP TMP2 MODE FR TMPS Q QEND QSPL LLEN INVX C1D C2D XA O1
D0A D0B D0D D1A D1B I0A I0B I0D I1A I1B""".split()
S = {n: i for i, n in enumerate(SS)}
assert len(SS) <= 64, len(SS)
SIDE = (0x40, 0x80)             # per-sample state of each side (r3 points here)
CS = """W1 W2 WT WTM FP1 FP2 FI1 FI2 FAMP DAH DAL DSH DSL DT DB XF FADING FROZEN RAMP
SIDEF RBASE RING DMGB NB DMAXA CLHI FLKR WPOS ENVS WOBH WOBL""".split()
C = {n: i for i, n in enumerate(CS)}
CBASE = (0xc0, 0xe0)            # per-call state and constants of each side (r2 points here)
assert len(CS) <= 32


def g_(n):
    return f'x:(r7+${G[n]:02x})'


def s_(n):
    return f'x:(r3+${S[n]:02x})'


def c_(n):
    return f'x:(r2+${C[n]:02x})'


OK_PAIRS = {('x0', 'x0'), ('y0', 'y0'), ('x1', 'x0'), ('y1', 'y0'), ('x0', 'y1'),
            ('y0', 'x0'), ('x1', 'y0'), ('y1', 'x1')}


def ins(op, *args, c=''):
    if op in ('mpy', 'mac', 'mpyr', 'macr') and len(args) == 3:
        a, b, d = args
        neg = a.startswith('-')
        a = a.lstrip('-')
        if (a, b) not in OK_PAIRS:
            a, b = b, a
        assert (a, b) in OK_PAIRS, args
        args = (('-' if neg else '') + a, b, d)
    return _ins(op, *args, c=c)


def lit(v):
    return f'#>${v & 0xffffff:06x}'


_lab = [0]


def lab(stem):
    _lab[0] += 1
    return f'tq{stem}{_lab[0]:03d}x'


def tozero(acc):
    """acc truncated toward zero at the 24-bit boundary (a negative value gets 1 LSB - 2^-24 added). Clobbers y."""
    l = lab('tz')
    return [ins('move', '#>$0', 'y1'), ins('move', '#>$ffffff', 'y0'), ins('tst', acc),
            ins('bge', l), ins('add', 'y', acc), f'{l}:']


def clamp(acc, lo, hi, reg='x0'):
    return [ins('move', hi, reg), ins('cmp', reg, acc), ins('tgt', reg, acc),
            ins('move', lo, reg), ins('cmp', reg, acc), ins('tlt', reg, acc)]


def onepole(state, coef, src='a', dst='b', creg='y0'):
    """state += (src - state) * coef, the new state left in dst. Clobbers x0 creg."""
    return [ins('move', state, dst), ins('sub', dst, src), ins('move', src, 'x0'), ins('move', coef, creg),
            ins('mac', 'x0', creg, dst), ins('move', dst, state)]


def knob_down(word, const):
    """a = max(0, 127/128 - knob) * const (the exponent argument y/32)"""
    return [ins('move', '#>$7f0000', 'a'), ins('move', word, 'x0'), ins('move', '#>$0', 'x1'), ins('sub', 'x0', 'a'),
            ins('tlt', 'x1', 'a'), ins('move', 'a', 'x0'), ins('move', q(const), 'y0'), ins('mpy', 'y0', 'x0', 'a')]


def side_branch(lx):
    """b = the side flag; branch to lx on the right side"""
    return [ins('move', c_('SIDEF'), 'b'), ins('tst', 'b'), ins('bne', lx)]


# ---------------------------------------------------------------- subroutines
def exp_sub():
    """b = 2^-y for a = y/32 (Q23, y in [0,32)); clobbers a b x0 x1 y0 (echo/gen.py's, 5 Taylor terms)."""
    return ['tqexp:',
            ins('move', 'a1', 'b'), ins('asr', '#$12', 'a', 'a'), ins('add', '#>$1', 'a'), ins('move', 'a1', 'x1'),
            ins('and', '#>$03ffff', 'b'), ins('asl', '#$5', 'b', 'b'), ins('move', 'b1', 'x0'),
            ins('move', q(math.log(2) / 2), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('move', 'b', 'y0'),
            ins('move', q(1 / 240), 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('neg', 'b'), ins('add', q(1 / 48), 'b'),
            ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(1 / 12), 'b'),
            ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.25), 'b'),
            ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(0.5), 'b'),
            ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.5), 'b'),
            ins('move', 'b', 'x0'), ins('mpy', 'x0', 'x0', 'b'), ins('asl', '#$2', 'b', 'b'),
            ins('do', 'x1', '>tqxpend'), ins('asr', 'b'), 'tqxpend:', ins('asl', 'b'), ins('rts')]


def div_sub():
    """b = a1 / x0 (0 <= a1 < x0, positive), 24 div steps. Clobbers a."""
    return ['tqdiv:', ins('move', 'a1', 'a'), ins('andi', '#$fe', 'ccr'),
            ins('do', '#24', '>tqdvend'), ins('div', 'x0', 'a'), 'tqdvend:', ins('nop'),
            ins('move', 'a0', 'b'), ins('rts')]


# sin(pi v / 2) / 2 ~ v (c1 + c3 v^2 + c5 v^4), v = +-1 -> +-1/2
_n = 1.5706268 - 0.6432292 + 0.0727102
SC1, SC3, SC5 = 1.5706268 / _n / 2, 0.6432292 / _n / 2, 0.0727102 / _n / 2


def tsin_sub():
    """b = sin(2 pi phi)/2 for a = phi in [0, 1); falls into tqhsin. Clobbers a b x0 x1 y0."""
    return ['tqtsin:', ins('sub', q(0.5), 'a'), ins('asl', 'a', c='t = 2 phi - 1; sin(2 pi phi) = -sin(pi t)'),
            ins('cmp', q(0.5), 'a'), ins('ble', 'tqtsa'),
            ins('move', 'a', 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('bra', 'tqtsb'),
            'tqtsa:', ins('cmp', q(-0.5), 'a'), ins('bge', 'tqtsb'),
            ins('move', 'a', 'x0'), ins('move', '#>$800000', 'a'), ins('sub', 'x0', 'a'),
            'tqtsb:', ins('neg', 'a'), ins('asl', 'a', c='v = -2t: falls into tqhsin')]


def hsin_sub():
    """b = sin(pi v/2)/2 for a = v in [-1, 1]. Clobbers a b x0 x1 y0."""
    return ['tqhsin:', ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x1'),
            ins('move', q(SC5), 'y0'), ins('mpy', 'x1', 'y0', 'b'), ins('sub', q(SC3), 'b'), ins('move', 'b', 'y0'),
            ins('mpy', 'x1', 'y0', 'b'), ins('add', q(SC1), 'b'), ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x0', 'b'),
            ins('rts')]


def herm_subs():
    """a = R (a1 = whole-sample offset from the write pointer, a0 = the fraction, unsigned) -> a = the plugin's
    4-point Hermite (Catmull-Rom) read there, formed / 4. Clobbers b x0 x1 y0 y1 r1 n4/n5. r2 = the side's block."""
    L = ['tqherm:'] + side_branch('tqhrmx')
    for side in (0, 1):
        mem, r, n = (('y', 'r4', 'n4'), ('x', 'r5', 'n5'))[side]
        L += [('tqhrmy', 'tqhrmx')[side] + ':', ins('move', 'a0', 'b'), ins('lsr', 'b'), ins('move', 'b1', s_('FR'), c='f'),
              ins('move', 'a1', n), ins('lua', f'({r})+{n}', 'r1'), ins('nop'),
              ins('move', f'{mem}:(r1)+', 'x0', c='x[-1]'), ins('move', f'{mem}:(r1)+', 'x1', c='x[0]'),
              ins('move', f'{mem}:(r1)+', 'y0', c='x[1]'), ins('move', f'{mem}:(r1)', 'y1', c='x[2]')]
        if side == 0:
            L += [ins('bra', 'tqhrmc')]
    L += ['tqhrmc:',
          ins('move', 'y1', 'a'), ins('sub', 'x0', 'a'), ins('asr', 'a'), ins('move', 'x1', 'b'), ins('sub', 'y0', 'b'),
          ins('add', 'b', 'a'), ins('asr', 'b'), ins('add', 'b', 'a'), ins('asr', '#$2', 'a', 'a', c='c3/4'),
          ins('move', 'y0', 'b'), ins('sub', 'x0', 'b'), ins('asr', '#$3', 'b', 'b'), ins('move', 'b', s_('TMPS'), c='c1/4'),
          ins('move', 'x0', 'b'), ins('add', 'y0', 'b'), ins('asr', 'b'), ins('sub', 'x1', 'b'), ins('asr', '#$2', 'b', 'b'),
          ins('sub', 'a', 'b', c='c2/4 = ((x[-1] + x[1])/2 - x[0])/4 - c3/4'),
          ins('move', 'a', 'x0'), ins('move', s_('FR'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', 'b', 'a'),
          ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', s_('TMPS'), 'b'), ins('add', 'b', 'a'),
          ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'x1', 'b'), ins('asr', '#$2', 'b', 'b'),
          ins('add', 'b', 'a'), ins('asl', '#$2', 'a', 'a'), ins('rts')]
    return L


def lin_subs():
    """a = R as for tqherm -> a = the linear read between x[0] and x[1]. Clobbers b x0 x1 y0 r1."""
    L = ['tqlrd:'] + side_branch('tqlinx')
    for side in (0, 1):
        mem, r, n = (('y', 'r4', 'n4'), ('x', 'r5', 'n5'))[side]
        L += [('tqliny', 'tqlinx')[side] + ':', ins('add', '#>$1', 'a'), ins('move', 'a0', 'b'), ins('lsr', 'b'),
              ins('move', 'b1', 'y0'), ins('move', 'a1', n), ins('lua', f'({r})+{n}', 'r1'), ins('nop'),
              ins('move', f'{mem}:(r1)+', 'x0'), ins('move', f'{mem}:(r1)', 'b')]
        if side == 0:
            L += [ins('bra', 'tqlinc')]
    return L + ['tqlinc:', ins('sub', 'x0', 'b'), ins('move', 'b', 'x1'), ins('move', 'x0', 'a'), ins('mac', 'x1', 'y0', 'a'),
                ins('rts')]


def dis_subs():
    """frozen: the plugin's disintegration at the loop position Q (an offset from the write pointer):
    s += (prev - s)(0.03 wear + 0.3 d); s *= 1 - (0.0009 wear + 0.012 d), d = the damage of Q's block (not
    interpolated, as the plugin). Clobbers a b x0 x1 y0 y1 r1."""
    L = ['tqdisd:', ins('move', s_('Q'), 'a')] + side_branch('tqdisx')
    for side in (0, 1):
        mem, r, n = (('y', 'r4', 'n4'), ('x', 'r5', 'n5'))[side]
        L += [('tqdisy', 'tqdisx')[side] + ':', ins('move', 'a1', n), ins('lua', f'({r})+{n}', 'r1'), ins('nop'),
              ins('move', 'r1', 'a'), ins('move', c_('RBASE'), 'x0'), ins('sub', 'x0', 'a'), ins('asr', '#$8', 'a', 'a'),
              ins('move', c_('DMGB'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r1'), ins('nop'),
              ins('move', 'y:(r1)', 'x0'), ins('move', g_('LG'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1', c='d'),
              ins('lua', f'({r})+{n}', 'r1'), ins('move', s_('C1D'), 'a'), ins('move', q(0.3), 'y0'),
              ins('mac', 'x1', 'y0', 'a'), ins('move', 'a', 'y1'),
              ins('move', s_('C2D'), 'b'), ins('move', q(0.012), 'y0'), ins('mac', 'x1', 'y0', 'b'), ins('move', 'b', 'x1'),
              ins('move', f'{mem}:(r1)-', 'x0'), ins('move', f'{mem}:(r1)+', 'a', c='prev'), ins('move', 'x0', 'b'),
              ins('sub', 'b', 'a'), ins('move', 'a', 'y0'), ins('mac', 'y1', 'y0', 'b'), ins('move', 'b', 'y0'),
              ins('mac', '-y0', 'x1', 'b'), ins('move', 'b', f'{mem}:(r1)'), ins('rts')]
    return L


def rand_sub():
    """a = x0 = uniform [0,1) Q23 from the global LCG (wxlib's: s = 214013 s + 2531011 mod 2^24, top 23 bits).
    Clobbers a b x0 y0."""
    return ['tqrand:', ins('move', g_('SEED'), 'x0'), ins('move', '#>$0343fd', 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('asr', 'a'), ins('move', 'a0', 'x0'), ins('move', 'x0', 'a'), ins('add', '#>$269ec3', 'a'),
            ins('move', 'a1', g_('SEED')), ins('move', 'a1', 'b'), ins('lsr', 'b'), ins('move', 'b1', 'x0'),
            ins('move', 'x0', 'a'), ins('rts')]


def sqrt_sub():
    """b = sqrt(x1) (x1 Q23 >= 0), 12 bits, restoring. Clobbers a b x0 y1."""
    return ['tqsqrt:', ins('clr', 'b'), ins('move', '#>$400000', 'y1'), ins('do', '#12', '>tqsqend'),
            ins('move', 'b', 'a'), ins('add', 'y1', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'),
            ins('cmp', 'x1', 'a'), ins('tle', 'x0', 'b'), ins('move', 'y1', 'a'), ins('asr', 'a'), ins('move', 'a', 'y1'),
            'tqsqend:', ins('nop'), ins('rts')]


def dmg_sub():
    """a = a ring offset (0..ring-1, integer in a1) -> b = the plugin's dmgAt (linear between blocks) on the
    side's damage map (Y). Clobbers a b x0 x1 y0 r1."""
    return ['tqdmg:', ins('move', 'a1', 'x1'), ins('asr', '#$8', 'a', 'a'), ins('move', 'a1', 'b', c='block i'),
            ins('move', c_('DMGB'), 'a'), ins('add', 'b', 'a'), ins('move', 'a1', 'r1'),
            ins('move', 'x1', 'a'), ins('and', '#>$0000ff', 'a'), ins('asl', '#$f', 'a', 'a'), ins('move', 'a1', 'y0', c='f'),
            ins('move', 'y:(r1)+', 'x0', c='dmg[i]'),
            ins('move', 'b', 'a'), ins('add', '#>$1', 'a'), ins('move', c_('NB'), 'x1'), ins('cmp', 'x1', 'a'), ins('blt', 'tqdmw'),
            ins('move', c_('DMGB'), 'r1'), ins('nop'),
            'tqdmw:', ins('move', 'y:(r1)', 'b'), ins('sub', 'x0', 'b'), ins('move', 'b', 'x1'),
            ins('move', 'x0', 'b'), ins('mac', 'x1', 'y0', 'b'), ins('rts')]


def flake_sub():
    """the plugin's addFlake on the side's damage map: b = rand(nb), len = 1 + rand(1 + int(5 age)),
    s = (0.15 + 0.75 rand) sqrt(age); d = min(1, max(d, s w) + 0.08 s w), w = sin(pi (k + 0.5)/len).
    Clobbers a b x0 x1 y0 r1."""
    return ['tqflk:', ins('bsr', '>tqrand'), ins('move', c_('NB'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('move', 'a1', g_('T1'), c='b = floor(nb rand)'),
            ins('bsr', '>tqrand'), ins('move', g_('EAGE'), 'x1'), ins('move', q(5 / 8), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
            ins('asr', '#$14', 'a', 'a'), ins('move', 'a1', 'y0', c='int(5 age)'), ins('move', 'y0', 'a'), ins('add', '#>$1', 'a'),
            ins('move', 'a1', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', '#>$1', 'a'), ins('move', 'a1', g_('T2'), c='len 1..6'),
            ins('bsr', '>tqrand'), ins('move', q(0.75), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(0.15), 'a'),
            ins('move', 'a', 'x0'), ins('move', g_('WEAR'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('T3'), c='s'),
            ins('move', g_('T2'), 'a'), ins('asl', 'a'), ins('move', 'a1', 'x0'), ins('move', '#>$1', 'a'), ins('bsr', '>tqdiv'),
            ins('move', 'b', g_('T4'), c='1/(2 len)'), ins('move', 'b', g_('T5'), c="v = (k + 0.5)/len"),
            ins('move', c_('DMGB'), 'a'), ins('move', g_('T1'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r1'),
            ins('move', g_('T2'), 'x1'), ins('do', 'x1', '>tqfke'),
            ins('move', g_('T5'), 'a'), ins('sub', q(0.5), 'a'), ins('asl', 'a'), ins('abs', 'a'), ins('neg', 'a'),
            ins('add', ONE, 'a', c='u = 1 - |2v - 1|: sin(pi v) = sin(pi u/2)'), ins('bsr', '>tqhsin'), ins('asl', 'b'),
            ins('move', 'b', 'x0'), ins('move', g_('T3'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1', c='s w'),
            ins('move', 'y:(r1)', 'a'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a', c='max(d, s w)'),
            ins('move', q(0.08), 'y0'), ins('mac', 'x1', 'y0', 'a'), ins('move', 'a', 'y:(r1)+', c='<= 1: the move limits'),
            ins('move', g_('T5'), 'a'), ins('move', g_('T4'), 'x0'), ins('add', 'x0', 'a'), ins('add', 'x0', 'a'),
            ins('move', 'a', g_('T5')),
            ins('move', g_('T1'), 'a'), ins('add', '#>$1', 'a'), ins('move', c_('NB'), 'x0'), ins('cmp', 'x0', 'a'),
            ins('blt', 'tqfkn'), ins('clr', 'a'), ins('move', c_('DMGB'), 'r1'),
            'tqfkn:', ins('move', 'a', g_('T1')), 'tqfke:', ins('nop'), ins('rts')]


def rel_sub():
    """a (A-units, 48 bits) = a head's delay -> clamped to [8, ring - 4] samples, then -(2 + d) as whole.fraction
    (the Hermite's x[-1]). Clobbers x0."""
    return ['tqrel:'] + clamp('a', q(8 / 16384), c_('CLHI')) + [ins('neg', 'a'), ins('sub', '#>$400', 'a'),
                                                                 ins('asr', '#$9', 'a', 'a'), ins('rts')]


# ---------------------------------------------------------------- per call, per side (one routine, r2/r3 = the side)
def side_sub():
    L = ['; ======== a side, once a call (r2 = its call block, r3 = its sample block) ========', 'tqside:']
    # -- the time target
    c_time = (128 / 127) * math.log2(T_MAX / T_MIN) / 32
    L += ['; TIME: T = 700 ms x (30/700)^(1 - v/127), the right side x 2^((SKEW - 64)/64), held to 700 ms']
    L += knob_down('x:(r6)', c_time)
    L += [ins('move', c_('SIDEF'), 'b'), ins('tst', 'b'), ins('beq', 'tqsnsk'),
          ins('move', 'x:(r6+$1)', 'b'), ins('sub', q(0.5), 'b'), ins('asr', '#$4', 'b', 'b'),
          ins('sub', 'b', 'a', c='y/32 - (s - 64)/64/32'), ins('move', '#>$0', 'x0'), ins('cmp', 'x0', 'a'),
          ins('tlt', 'x0', 'a'), 'tqsnsk:',
          ins('bsr', '>tqexp'), ins('move', 'b', 'x0'), ins('move', q(T_MAX * SR / 16384), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('sub', q(LAT / 16384), 'a', c='minus the converters\' latency'),
          ins('move', c_('DMAXA'), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('move', 'a', c_('DT')),
          ins('move', g_('FIRST'), 'b'), ins('tst', 'b'), ins('beq', 'tqsnofz1'),
          ins('move', 'a', c_('DAH')), ins('move', 'a', c_('DSH')), ins('move', 'a', c_('DB')), ins('clr', 'b'),
          ins('move', 'b', c_('DAL')), ins('move', 'b', c_('DSL')), 'tqsnofz1:']
    # -- freeze: FRZ, or the ColdFire's word (r6+6 left, r6+7 right)
    L += ['; freeze', ins('move', 'r6', 'a'), ins('move', c_('SIDEF'), 'x0'), ins('add', 'x0', 'a'), ins('add', '#>$6', 'a'),
          ins('move', 'a1', 'r1'), ins('nop'), ins('move', 'x:(r1)', 'a'), ins('move', g_('FRZK'), 'x0'), ins('or', 'x0', 'a'),
          ins('move', c_('FROZEN'), 'b'), ins('tst', 'a'), ins('beq', 'tqsfof'),
          ins('tst', 'b'), ins('bne', 'tqsfdn'),
          '; freeze starts: the loop is the last dA samples (>= 118) before the write head',
          ins('move', ONE, 'b'), ins('move', 'b', c_('FROZEN')), ins('clr', 'b'), ins('move', 'b', c_('FADING')),
          ins('move', 'b', s_('FB'), c='the plugin: fb = 0 while frozen'),
          ins('move', c_('DAH'), 'a'), ins('add', '#>$100', 'a'), ins('asr', '#$9', 'a', 'a'), ins('move', 'a1', 'x0', c='round(dA)'),
          ins('move', 'x0', 'a'), ins('move', lit(LOOP_MIN), 'x1'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a'),
          ins('move', 'a1', s_('LLEN')), ins('move', 'x0', 'b'), ins('neg', 'b'), ins('sub', '#>$1', 'b'),
          ins('move', 'b1', s_('Q'), c='Q = loopStart (an offset from w)'), ins('add', 'a', 'b'), ins('move', 'b1', s_('QEND')),
          '; splice X = min(276, len/4); 2^23/X',
          ins('asr', '#$2', 'a', 'a'), ins('move', lit(SPLICE), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a'),
          ins('move', 'a1', 'x0'), ins('move', s_('QEND'), 'b'), ins('sub', 'x0', 'b'), ins('move', 'b1', s_('QSPL')),
          ins('move', '#>$1', 'a'), ins('bsr', '>tqdiv'), ins('move', 'b', s_('INVX')),
          ins('clr', 'b'), ins('move', 'b', s_('RBH')), ins('move', 'b', s_('RBL')), ins('bra', 'tqsfdn'),
          'tqsfof:', ins('tst', 'b'), ins('beq', 'tqsfdn'),
          '; freeze ends: the head goes back to w - 1 - dA - wob at once (the plugin)',
          ins('clr', 'b'), ins('move', 'b', c_('FROZEN')), ins('move', ONE, 'b'), ins('move', 'b', s_('GA')),
          ins('move', 'b', c_('RAMP')), ins('clr', 'b'), ins('move', 'b', s_('GB')), ins('move', 'b', s_('DGA')),
          ins('move', 'b', s_('DGB')),
          'tqsfdn:',
          ins('move', c_('FROZEN'), 'a'), ins('tst', 'a'), ins('beq', 'tqsfrz2'),
          '; frozen: the read is Q - wob/2 (Q steps a sample a tick); wob/2 ramps in RB/SB',
          ins('move', c_('WOBH'), 'a'), ins('move', c_('WOBL'), 'a0'), ins('asr', '#$a', 'a', 'a', c='wob/2 as whole.fraction'),
          ins('move', s_('RBH'), 'b'), ins('move', s_('RBL'), 'b0'), ins('sub', 'b', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'),
          ins('move', 'a1', s_('SBH')), ins('move', 'a0', s_('SBL')),
          ins('move', lit(2), 'a'), ins('move', 'a', s_('MODE')),
          ins('move', g_('WEAR'), 'x0'), ins('move', q(0.03), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', s_('C1D')),
          ins('move', q(0.0009), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', s_('C2D')),
          ins('clr', 'a'), ins('move', 'a', s_('KFB')), ins('move', 'a', s_('DKFB')), ins('rts'), 'tqsfrz2:']
    # -- heads
    a8s = 1 - (1 - SLEW_A) ** TPU
    L += [ins('move', g_('TMOD'), 'a'), ins('tst', 'a'), ins('bne', 'tqsfade'),
          '; SLEW: dS += (dT - dS) a; dA += (dS - dA) a  (a for 8 ticks; 48 bits)',
          ins('clr', 'b'), ins('move', 'b', c_('FADING')),
          ins('move', c_('DT'), 'a'), ins('move', c_('DSH'), 'b'), ins('move', c_('DSL'), 'b0'), ins('sub', 'b', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(a8s), 'y0'), ins('mac', 'y0', 'x0', 'b'),
          ins('move', 'b1', c_('DSH')), ins('move', 'b0', c_('DSL')),
          ins('move', c_('DAH'), 'a'), ins('move', c_('DAL'), 'a0'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
          ins('mac', 'y0', 'x0', 'a'), ins('move', 'a1', c_('DAH')), ins('move', 'a0', c_('DAL')),
          ins('bra', 'tqsm0'),
          'tqsfade:', '; FADE: a second head at the new time, crossfaded over 70 ms (cos / sin)',
          ins('move', c_('FADING'), 'a'), ins('tst', 'a'), ins('bne', 'tqsfg'),
          ins('move', c_('DT'), 'a'), ins('move', c_('DAH'), 'x0'), ins('sub', 'x0', 'a'), ins('abs', 'a'),
          ins('move', q(0.01), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(4 / 16384), 'b'), ins('move', 'b', 'x1'),
          ins('cmp', 'x1', 'a'), ins('ble', 'tqsfh'),
          ins('move', c_('DT'), 'a'), ins('move', 'a', c_('DB')), ins('move', ONE, 'a'), ins('move', 'a', c_('FADING')),
          ins('clr', 'a'), ins('move', 'a', c_('XF')), ins('move', 'a', s_('GB')), ins('move', ONE, 'a'), ins('move', 'a', s_('GA')),
          '; the new head starts at its own target',
          ins('move', c_('DB'), 'a'), ins('move', c_('WOBH'), 'x0'), ins('add', 'x0', 'a'), ins('bsr', '>tqrel'),
          ins('move', 'a1', s_('RBH')), ins('move', 'a0', s_('RBL')), ins('bra', 'tqsfg'),
          'tqsfh:', ins('move', c_('DAH'), 'a'), ins('move', 'a', c_('DSH')), ins('clr', 'a'), ins('move', 'a', c_('DSL')),
          ins('bra', 'tqsm0'),
          'tqsfg:', ins('move', c_('XF'), 'a'), ins('cmp', ONE, 'a'), ins('blt', 'tqsxf'),
          '; the fade ended last call: the new head is the head',
          ins('move', c_('DB'), 'a'), ins('move', 'a', c_('DAH')), ins('move', 'a', c_('DSH')), ins('clr', 'a'),
          ins('move', 'a', c_('DAL')), ins('move', 'a', c_('DSL')), ins('move', 'a', c_('FADING')), ins('move', 'a', s_('GB')),
          ins('move', 'a', s_('DGB')), ins('move', 'a', s_('DGA')), ins('move', ONE, 'a'), ins('move', 'a', s_('GA')),
          ins('move', s_('RBH'), 'a'), ins('move', 'a', s_('RH')), ins('move', s_('RBL'), 'a'), ins('move', 'a', s_('RL')),
          ins('bra', 'tqsm0'),
          'tqsxf:', ins('add', q(TPU * XF_STEP), 'a'), ins('cmp', ONE, 'a'), ins('blt', 'tqsxe'), ins('move', ONE, 'a'),
          'tqsxe:', ins('move', 'a', c_('XF')),
          ins('move', 'a', 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('bsr', '>tqhsin'), ins('asl', 'b'),
          ins('move', s_('GA'), 'x0'), ins('sub', 'x0', 'b'), ins('asr', f'#${RSH:x}', 'b', 'b'), ins('move', 'b', s_('DGA'), c='to cos'),
          ins('move', c_('XF'), 'a'), ins('bsr', '>tqhsin'), ins('asl', 'b'),
          ins('move', s_('GB'), 'x0'), ins('sub', 'x0', 'b'), ins('asr', f'#${RSH:x}', 'b', 'b'), ins('move', 'b', s_('DGB'), c='to sin'),
          '; head B ramps to dB + wob',
          ins('move', c_('DB'), 'a'), ins('move', c_('WOBH'), 'x0'), ins('move', c_('WOBL'), 'a0'), ins('add', 'x0', 'a'), ins('bsr', '>tqrel'),
          ins('move', s_('RBH'), 'b'), ins('move', s_('RBL'), 'b0'), ins('sub', 'b', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'),
          ins('move', 'a1', s_('SBH')), ins('move', 'a0', s_('SBL')),
          ins('move', lit(1), 'a'), ins('bra', 'tqsmd'),
          'tqsm0:', ins('clr', 'a'),
          'tqsmd:', ins('move', 'a', s_('MODE')),
          '; head A ramps to -(1 + max(8, dA + wob)) (whole.fraction, from the write pointer after it moves)',
          ins('move', c_('DAH'), 'a'), ins('move', c_('DAL'), 'a0'), ins('move', c_('WOBH'), 'b'), ins('move', c_('WOBL'), 'b0'),
          ins('add', 'b', 'a'), ins('bsr', '>tqrel'),
          ins('move', c_('RAMP'), 'b'), ins('tst', 'b'), ins('beq', 'tqsrmp'),
          ins('move', 'a1', s_('RH')), ins('move', 'a0', s_('RL')), ins('clr', 'b'), ins('move', 'b', c_('RAMP')),
          'tqsrmp:', ins('move', s_('RH'), 'b'), ins('move', s_('RL'), 'b0'), ins('sub', 'b', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'),
          ins('move', 'a1', s_('SH')), ins('move', 'a0', s_('SL')),
          '; feedback: k/2 = 1.05 regen / 2; gain-riding g = min(1, C / (k env)), env = the wet\'s peak envelope',
          ins('move', c_('ENVS'), 'x0'), ins('move', q(math.exp(-TPU / (RIDE_REL * SR))), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', s_('ENV'), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'), ins('move', 'a', c_('ENVS'),
                                                                                       c='env = max(peak, env x release)'),
          ins('clr', 'b'), ins('move', 'b', s_('ENV')),
          ins('move', 'a', 'x0'), ins('move', g_('REGS'), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='k env / 2'),
          ins('move', ONE, 'b'), ins('move', q(CEIL / SCALE / 2), 'x0'), ins('cmp', 'x0', 'a'), ins('ble', 'tqsrg'),
          ins('move', 'a', 'x1'), ins('move', 'x0', 'a'), ins('move', 'x1', 'x0'), ins('bsr', '>tqdiv'),
          'tqsrg:', ins('move', 'b', 'x0'), ins('move', g_('REGS'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', s_('KFB'), 'b'), ins('sub', 'b', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'),
          ins('move', 'a', s_('DKFB'), c='k g / 2: ramped tick by tick'),
          '; the HPF crossfades in / out over the same ticks (it switches without a click)',
          ins('move', g_('HPON'), 'a'), ins('tst', 'a'), ins('beq', 'tqshpo'), ins('move', ONE, 'a'), 'tqshpo:',
          ins('move', s_('HPG'), 'b'), ins('sub', 'b', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'), ins('move', 'a', s_('DHP')),
          ins('move', g_('HPON'), 'a'), ins('tst', 'a'), ins('bne', 'tqshpx'),
          ins('move', s_('HPG'), 'a'), ins('cmp', q(1 / 512), 'a'), ins('bgt', 'tqshpx'),
          ins('clr', 'a'), ins('move', 'a', s_('HPG')), ins('move', 'a', s_('DHP'), c='off: exactly 0, the HPF skipped'),
          'tqshpx:', ins('move', s_('HPG'), 'b'), ins('tst', 'b'), ins('bne', 'tqshpy'), ins('move', s_('DHP'), 'b'),
          ins('move', 'b', s_('HPG'), c='from 0: the first step now (the tick skips the HPF while HPG is 0)'),
          'tqshpy:', ins('rts')]
    # -- damage under the heads
    L += ['; ---- wow and flutter (for the next heads), on a call of their own ----', 'tqswob:']
    # -- wow: a new target every 0.5 .. 2.3 s, two one-poles at 0.55 Hz
    a8w = 1 - (1 - WOW_A) ** TPU
    L += ['; wow', ins('move', c_('WTM'), 'a'), ins('sub', q(16 * UPD / FS / 4), 'a'), ins('move', 'a', c_('WTM')), ins('tst', 'a'),
          ins('bgt', 'tqswt'), ins('bsr', '>tqrand'), ins('sub', q(0.5), 'a'), ins('asl', 'a'), ins('move', 'a', c_('WT')),
          ins('bsr', '>tqrand'), ins('move', q(1.8 / 4), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(0.5 / 4), 'a'),
          ins('move', 'a', c_('WTM')), 'tqswt:',
          ins('move', c_('WT'), 'a')] + onepole(c_('W1'), q(a8w)) + [ins('move', 'b', 'a')] + onepole(c_('W2'), q(a8w))
    # -- flutter
    L += ['; flutter: two sines, an amplitude that wanders',
          ins('move', c_('FP1'), 'a'), ins('move', c_('FI1'), 'x0'), ins('add', 'x0', 'a'),
          ins('and', '#>$7fffff', 'a'), ins('move', 'a1', c_('FP1')),
          ins('move', c_('FP2'), 'a'), ins('move', c_('FI2'), 'x0'), ins('add', 'x0', 'a'),
          ins('and', '#>$7fffff', 'a'), ins('move', 'a1', c_('FP2')),
          ins('bsr', '>tqrand'), ins('sub', q(0.5), 'a'), ins('move', 'a', 'x0'),
          ins('move', q(0.5 / math.sqrt(16 * UPD * 48000 / FS)), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('add', q(0.5), 'a', c='(0.5 + rand)/2, scaled as the plugin\'s per-sample draws average')]
    L += onepole(c_('FAMP'), q(1 - (1 - 0.0004) ** (16 * UPD * 48000 / FS)))
    L += [ins('move', c_('FP2'), 'a'), ins('bsr', '>tqtsin'), ins('move', 'b', 'x0'), ins('move', q(0.35), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('T1')),
          ins('move', c_('FP1'), 'a'), ins('bsr', '>tqtsin'), ins('move', g_('T1'), 'a'), ins('add', 'b', 'a'),
          ins('move', 'a', 'x0', c='(s1 + 0.35 s2)/2'), ins('move', c_('FAMP'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'a', 'x0', c='famp s / 4'),
          ins('move', g_('WEAR'), 'y0'), ins('move', q(0.25), 'a'), ins('move', q(0.5), 'y1'), ins('mac', 'y0', 'y1', 'a'),
          ins('move', 'a', 'y0', c='(1 + 2 wear)/4'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', q(FLUT_DEPTH / 16384 * 64), 'y0'), ins('mpy', 'y0', 'x0', 'b', c='4 flutter, A-units'),
          ins('move', g_('WEAR'), 'y0'), ins('move', q(0.25), 'a'), ins('move', q(0.4), 'y1'), ins('mac', 'y0', 'y1', 'a'),
          ins('move', 'a', 'x0', c='(1 + 1.6 wear)/4'), ins('move', c_('W2'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(WOW_DEPTH / 16384 * 16), 'y0'), ins('mac', 'y0', 'x0', 'b'),
          ins('move', 'b', 'x0', c='4 (wow + flutter), A-units'), ins('move', g_('WOWS'), 'y0', c='W/4'),
          ins('mpy', 'y0', 'x0', 'b', c='wob, A-units (48 bits)'),
          ins('move', 'b1', c_('WOBH')), ins('move', 'b0', c_('WOBL')), ins('rts'), '; ---- the damage, on another call ----',
          'tqsdmg:']
    L += [ '; damage at the write head: record gain 1.148 (1 - 0.8 d)(1 - 0.18 wear), ramped over the call',
          ins('move', c_('WPOS'), 'a'), ins('move', c_('RBASE'), 'x0'), ins('sub', 'x0', 'a'), ins('bsr', '>tqdmg'),
          ins('move', 'b', 'x0'), ins('move', g_('LG'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', ONE, 'a'), ins('move', q(0.8), 'y0'), ins('mac', '-y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', g_('WEAR'), 'y0'), ins('move', ONE, 'a'), ins('move', q(0.18), 'y1'), ins('mac', '-y0', 'y1', 'a'),
          ins('move', 'a', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(REC_GAIN / 2), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', s_('RG'), 'x0'), ins('sub', 'x0', 'a'), ins('asr', f'#${RSH:x}', 'a', 'a'),
          ins('move', 'a', s_('DRG')),
          '; damage at the read head (w - 1 - dA; frozen: the loop position)',
          ins('move', c_('FROZEN'), 'a'), ins('tst', 'a'), ins('beq', 'tqsrp'),
          ins('move', s_('Q'), 'a'), ins('bra', 'tqsrq'),
          'tqsrp:', ins('move', c_('DAH'), 'a'), ins('add', '#>$100', 'a'), ins('asr', '#$9', 'a', 'a'), ins('neg', 'a'),
          ins('sub', '#>$1', 'a'),
          'tqsrq:', ins('move', c_('WPOS'), 'x0'), ins('add', 'x0', 'a'), ins('move', c_('RBASE'), 'x0'), ins('sub', 'x0', 'a'),
          ins('bge', 'tqsrw'), ins('move', c_('RING'), 'x0'), ins('add', 'x0', 'a'), 'tqsrw:',
          ins('bsr', '>tqdmg'),
          ins('move', 'b', 'x0'), ins('move', g_('LG'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('T2'), c='dr'),
          ins('move', 'a', 'x0'), ins('move', ONE, 'a'), ins('move', q(0.4), 'y0'), ins('mac', '-y0', 'x0', 'a'),
          ins('move', 'a', s_('DROP')),
          '; gap loss: fc = 15 kHz (1 - 0.8 wear)(1 - 0.55 dr); a = wc/(1 + wc)',
          ins('move', ONE, 'a'), ins('move', q(0.55), 'y0'), ins('mac', '-y0', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', g_('WEAR'), 'y0'), ins('move', ONE, 'a'), ins('move', q(0.8), 'x0'), ins('mac', '-y0', 'x0', 'a'),
          ins('move', 'a', 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('move', 'a', 'x0'),
          ins('move', q(2 * math.pi * GAP_HZ / SR / 8), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='wc/8'),
          ins('move', 'a', 'y1'), ins('add', q(1 / 8), 'a'), ins('move', 'a', 'x0'), ins('move', 'y1', 'a'),
          ins('bsr', '>tqdiv'), ins('move', 'b', s_('GAPA')),
          '; crackle: wear > 0.25, chance 0.00003 w^2 (1 + 8 dr) a 48 kHz sample',
          ins('move', g_('WEAR'), 'a'), ins('cmp', q(0.25), 'a'), ins('ble', 'tqsnc'),
          ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', g_('T2'), 'x0'), ins('move', q(1 / 9), 'a'), ins('move', q(8 / 9), 'y0'), ins('mac', 'y0', 'x0', 'a'),
          ins('move', 'a', 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('move', 'a', 'x0'),
          ins('move', q(0.00003 * 9 * 16 * UPD * 48000 / FS), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('T1')),
          ins('bsr', '>tqrand'), ins('move', g_('T1'), 'x1'), ins('cmp', 'x1', 'a'), ins('bge', 'tqsnc'),
          ins('bsr', '>tqrand'), ins('sub', q(0.5), 'a'), ins('move', 'a', 'x0'), ins('move', g_('WEAR'), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', g_('HGL'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(0.02 * 2 * 2 / SCALE), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'a', s_('CRK')),
          'tqsnc:',
          '; flakes: (0.01 + 0.12 age^2) a second on the plugin\'s 165-block tape; here x nb/165',
          ins('move', g_('EAGE'), 'a'), ins('cmp', q(0.001), 'a'), ins('ble', 'tqsnk'),
          ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(0.12 / 0.13), 'y0'),
          ins('move', q(0.01 / 0.13), 'a'), ins('mac', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', c_('FLKR'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('asr', '#$6', 'a', 'a'), ins('move', 'a', g_('T1')),
          ins('bsr', '>tqrand'), ins('move', g_('T1'), 'x1'), ins('cmp', 'x1', 'a'), ins('bge', 'tqsnk'),
          ins('bsr', '>tqflk'),
          'tqsnk:', ins('rts')]
    return L


# ---------------------------------------------------------------- per tick, per side (one routine)
def allpass(c, xs, ys, src='x0'):
    """y = c (x - ys) + xs; xs = x; ys = y. x in src (x0); y left in a and x0. Clobbers y0 x1."""
    return [ins('move', xs, 'a'), ins('move', q(c), 'y0'), ins('mac', src, 'y0', 'a'), ins('move', ys, 'x1'),
            ins('mac', '-x1', 'y0', 'a'), ins('move', src, xs), ins('move', 'a', ys), ins('move', 'a', 'x0')]


def allpass2(c1, c2, xs, ys1, ys2):
    """two chained stages: the second's last input IS the first's last output (ys1), kept in x1 (one store fewer).
    x in x0; y left in a and x0. Clobbers y0 x1."""
    return [ins('move', xs, 'a'), ins('move', q(c1), 'y0'), ins('mac', 'x0', 'y0', 'a'), ins('move', ys1, 'x1'),
            ins('mac', '-x1', 'y0', 'a'), ins('move', 'x0', xs), ins('move', 'a', ys1), ins('move', 'a', 'x0'),
            ins('move', 'x1', 'a'), ins('move', q(c2), 'y0'), ins('mac', 'x0', 'y0', 'a'), ins('move', ys2, 'x1'),
            ins('mac', '-x1', 'y0', 'a'), ins('move', 'a', ys2), ins('move', 'a', 'x0')]


def svf_bp(xreg, s1, s2, a1, a2, a3):
    """TPT SVF on x (the slot xreg): -> b = v1 (band), a = v2 (low); states updated. Clobbers x0 x1 y0 y1."""
    return [ins('move', s2, 'x1'), ins('move', xreg, 'a'), ins('sub', 'x1', 'a'), ins('move', 'a', 'y1', c='v3 = x - ic2'),
            ins('move', s1, 'x0'), ins('move', q(a1), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('move', q(a2), 'y0'),
            ins('mac', 'y1', 'y0', 'b', c='v1 = a1 ic1 + a2 v3'),
            ins('move', 'x1', 'a'), ins('mac', 'y0', 'x0', 'a'), ins('move', q(a3), 'y0'),
            ins('mac', 'y1', 'y0', 'a', c='v2 = ic2 + a2 ic1 + a3 v3'),
            ins('move', 'b', 'y1'), ins('asl', 'b'), ins('sub', 'x0', 'b'), ins('move', 'b', s1), ins('move', 'y1', 'b'),
            ins('move', 'a', 'y0'), ins('asl', 'a'), ins('sub', 'x1', 'a'), ins('move', 'a', s2), ins('move', 'y0', 'a')]


def mixout(inw, wreg):
    """out = dry + (cos - 1) dry + sin 8 wet (wet at 1/8 in wreg, not x0/y0)."""
    return [ins('move', inw, 'a'), ins('move', 'a', 'x0'), ins('move', g_('CM1'), 'y0'), ins('mac', 'y0', 'x0', 'a'),
            ins('move', g_('SG'), 'y0'), ins('mpy', 'y0', wreg, 'b'), ins('asl', '#$3', 'b', 'b'), ins('add', 'b', 'a'),
            ins('move', 'a', inw)]


def tick_sub():
    inw = 'x:(r0+n0)'
    L = ['; ======== a side, one tape sample (r2/r3 = the side, n0 = 0 left / 1 right, m1 = its ring) ========', 'tqtick:',
         ins('move', s_('MODE'), 'b'), ins('cmp', lit(2), 'b'), ins('beq', 'tqtfz', c='frozen: nothing is recorded'),
         '; decimate: d = (A0(x[2n]) + A1(x[2n-1]))/2 at 1/8',
         ins('move', inw, 'a'), ins('asr', '#$3', 'a', 'a'), ins('move', 'a', 'x0')]
    L += allpass2(P0[0], P0[1], s_('D0A'), s_('D0B'), s_('D0D')) + [ins('move', 'a', s_('TMPS'))]
    L += [ins('move', s_('XA'), 'x0')] + allpass(P1[0], s_('D1A'), s_('D1B'))
    L += [ins('move', s_('TMPS'), 'b'), ins('add', 'b', 'a'), ins('asr', 'a', c='d'),
          '; record head: u = d + fb; pre-emphasis; x 1.148 (1 - 0.8 d)(1 - 0.18 wear); no saturation',
          ins('move', s_('FB'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'x1', c='u')]
    L += onepole(s_('PRE'), q(PRE_A)) + [
          ins('move', 'x1', 'a'), ins('move', q(0.6 * (1 - PRE_A)), 'y0'),
          ins('mac', 'y0', 'x0', 'a', c='e = u + 0.6 (u - pre\'), pre\' = pre + a (u - pre)'), ins('move', 'a', 'x0'),
          ins('move', s_('RG'), 'b'), ins('move', s_('DRG'), 'y0'), ins('add', 'y0', 'b'), ins('move', 'b', s_('RG')),
          ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a')]
    L += side_branch('tqtwx') + [ins('move', 'a', 'y:(r4)+'), ins('bra', 'tqtwd'), 'tqtwx:', ins('move', 'a', 'x:(r5)+'),
          'tqtwd:', '; playback head A',
          ins('move', s_('RH'), 'a'), ins('move', s_('RL'), 'a0'), ins('move', s_('SH'), 'b'), ins('move', s_('SL'), 'b0'),
          ins('add', 'b', 'a'), ins('move', 'a1', s_('RH')), ins('move', 'a0', s_('RL')), ins('bsr', '>tqherm'),
          ins('move', s_('MODE'), 'b'), ins('tst', 'b'), ins('beq', 'tqtout'),
          '; fade: head B, the gains (cos / sin) ramped',
          ins('move', s_('GA'), 'b'), ins('move', s_('DGA'), 'x0'), ins('add', 'x0', 'b'), ins('move', 'b', s_('GA')),
          ins('move', 'b', 'x0'), ins('move', 'a', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', s_('TMP2')),
          ins('move', s_('RBH'), 'a'), ins('move', s_('RBL'), 'a0'), ins('move', s_('SBH'), 'b'), ins('move', s_('SBL'), 'b0'),
          ins('add', 'b', 'a'), ins('move', 'a1', s_('RBH')), ins('move', 'a0', s_('RBL')),
          ins('bsr', '>tqlrd', c='the incoming head reads linearly (70 ms; it saves 26 instructions while fading)'),
          ins('move', s_('GB'), 'b'), ins('move', s_('DGB'), 'x0'), ins('add', 'x0', 'b'), ins('move', 'b', s_('GB')),
          ins('move', 'b', 'x0'), ins('move', 'a', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', s_('TMP2'), 'b'),
          ins('add', 'b', 'a'), ins('bra', 'tqtout'),
          'tqtfz:', '; frozen: Q steps round the loop; read at Q - wob/2; the splice crossfades the last X samples',
          ins('move', s_('Q'), 'a'), ins('add', '#>$1', 'a'), ins('move', s_('QEND'), 'x0'), ins('cmp', 'x0', 'a'),
          ins('blt', 'tqtqw'), ins('move', s_('LLEN'), 'x0'), ins('sub', 'x0', 'a'), 'tqtqw:', ins('move', 'a1', s_('Q')),
          ins('bsr', '>tqdisd'),
          ins('move', s_('RBH'), 'a'), ins('move', s_('RBL'), 'a0'), ins('move', s_('SBH'), 'b'), ins('move', s_('SBL'), 'b0'),
          ins('add', 'b', 'a'), ins('move', 'a1', s_('RBH')), ins('move', 'a0', s_('RBL')),
          ins('move', 'a', 'b'), ins('clr', 'a'), ins('move', s_('Q'), 'a1'), ins('sub', '#>$1', 'a'), ins('sub', 'b', 'a'),
          ins('move', 'a1', s_('RH')), ins('move', 'a0', s_('RL')), ins('bsr', '>tqherm'),
          ins('move', s_('Q'), 'b'), ins('move', s_('QSPL'), 'x0'), ins('cmp', 'x0', 'b'), ins('ble', 'tqtout'),
          ins('move', 'a', s_('O1'), c='(O1 as scratch: set again below)'),
          ins('sub', 'x0', 'b'), ins('move', 'b1', 'x0'), ins('move', s_('INVX'), 'y0'), ins('mpy', 'y0', 'x0', 'b'),
          ins('asl', '#$17', 'b', 'b'), ins('move', 'b1', s_('TMP2'), c='t = (Q - QSPL)/X'),
          ins('move', s_('RH'), 'a'), ins('move', s_('RL'), 'a0'), ins('move', s_('LLEN'), 'x0'), ins('sub', 'x0', 'a'),
          ins('bsr', '>tqlrd', c='the spliced-in read is linear (it fades in from 0)'), ins('move', s_('O1'), 'b'), ins('sub', 'b', 'a'), ins('move', 'a', 'x0'),
          ins('move', s_('TMP2'), 'y0'), ins('mac', 'y0', 'x0', 'b'), ins('move', 'b', 'a', c='out (1 - t) + t spliced'),
          'tqtout:', '; gap loss (two one-poles), head bump, de-emphasis, dropout']
    L += onepole(s_('GP1'), s_('GAPA')) + [ins('move', 'b', 'a'), ins('move', s_('GP2'), 'b'), ins('sub', 'b', 'a'),
                                            ins('move', 'a', 'x0'), ins('mac', 'x0', 'y0', 'b'), ins('move', 'b', s_('GP2'))] + [
          ins('move', 'b', 'x0')]
    L += [ins('move', 'x0', 'x1', c='head bump: Chamberlin SVF (lp += f bp; hp = x - lp - q bp; bp += f hp)'),
          ins('move', s_('BS2'), 'a'), ins('move', s_('BS1'), 'x0'), ins('move', q(B_F), 'y0'), ins('mac', 'x0', 'y0', 'a'),
          ins('move', 'a', s_('BS2')), ins('move', 'x1', 'b'), ins('sub', 'a', 'b'), ins('move', q(B_K), 'y1'),
          ins('mac', '-x0', 'y1', 'b'), ins('move', 'b', 'y1'), ins('move', 'x0', 'a'), ins('mac', 'y1', 'y0', 'a'),
          ins('move', 'a', s_('BS1')), ins('move', 'a', 'x0'), ins('move', 'x1', 'a'), ins('move', q(B_KB), 'y0'),
          ins('mac', 'y0', 'x0', 'a', c='x + (A^2 - 1) q bp'), ins('move', 'a', 'x1')]
    L += onepole(s_('DEM'), q(PRE_A)) + [
          ins('move', 'x1', 'a'), ins('move', q(0.375 * (1 - PRE_A)), 'y0'), ins('mac', '-y0', 'x0', 'a',
                                                                                c='y - 0.375 (y - dem\')'),
          ins('move', 'a', 'x0'), ins('move', s_('DROP'), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', s_('TMPS')),
          '; hiss: white > 11 kHz lowpass - its 900 Hz lowpass, x (0.0018 + 0.008 wear) x the fade x HISS',
          ins('move', g_('SEED'), 'x0'), ins('move', '#>$0343fd', 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('asr', 'a'), ins('move', 'a0', 'x0'), ins('move', 'x0', 'a'), ins('add', '#>$269ec3', 'a'),
          ins('move', 'a1', g_('SEED'), c='the LCG inline: a = white, -1..1'), ins('move', 'a', 'x1')]
    L += onepole(s_('HHP'), q(HISS_HA)) + [
          ins('move', 'x1', 'a'), ins('sub', 'b', 'a'), ins('move', 'a', 'x0'), ins('move', g_('HL'), 'y0'),
          ins('move', s_('TMPS'), 'a'), ins('mac', 'y0', 'x0', 'a'),
          ins('move', s_('CRK'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'x1'), ins('move', q(CRK_DECAY), 'y0'),
          ins('mpy', 'y0', 'x0', 'b'), ins('move', 'b', s_('CRK')),
          '; 8 Hz DC blocker, its state truncated toward zero (exact silence after a tail)',
          ins('move', s_('DCX'), 'x0'), ins('move', 'x1', s_('DCX')), ins('sub', 'x0', 'a'),
          ins('move', s_('DCY'), 'x0'), ins('move', q(DC_R), 'y0'), ins('mac', 'y0', 'x0', 'a')]
    L += tozero('a') + [ins('move', 'a', s_('DCY')), ins('move', 'a', s_('TMPS'), c='y: the wet (1/8)'),
          '; feedback (not while frozen): the wet\'s peak for the gain-riding, optional 320 Hz HPF (Chamberlin high-pass), '
          'fb = 2 (k g / 2) f truncated toward zero',
          ins('move', s_('MODE'), 'b'), ins('cmp', lit(2), 'b'), ins('beq', 'tqtint'),
          ins('abs', 'a'), ins('move', s_('ENV'), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'),
          ins('move', 'a', s_('ENV'), c='peak |y| since the last update'), ins('move', s_('TMPS'), 'a'),
          ins('move', s_('HPG'), 'b'), ins('tst', 'b'), ins('beq', 'tqthp'),
          ins('move', s_('DHP'), 'x0'), ins('add', 'x0', 'b'), ins('move', 'b', s_('HPG'))]
    L += [ins('move', s_('HS2'), 'a'), ins('move', s_('HS1'), 'x0'), ins('move', q(H_F), 'y0'), ins('mac', 'x0', 'y0', 'a'),
          ins('move', 'a', s_('HS2'), c='lp += f bp'), ins('move', s_('TMPS'), 'b'), ins('sub', 'a', 'b'),
          ins('move', q(H_K / 2), 'y1'), ins('mac', '-x0', 'y1', 'b'), ins('mac', '-x0', 'y1', 'b', c='hp = x - lp - q bp'),
          ins('move', 'b', 'y1'), ins('move', 'x0', 'a'), ins('mac', 'y1', 'y0', 'a'), ins('move', 'a', s_('HS1'), c='bp += f hp'),
          ins('move', 'b', 'a'), ins('move', s_('TMPS'), 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', s_('HPG'), 'y0'), ins('move', 'x0', 'a'), ins('mac', 'x1', 'y0', 'a', c='f = y + g (hp - y)'),
          'tqthp:',
          ins('move', s_('KFB'), 'b'), ins('move', s_('DKFB'), 'x0'), ins('add', 'x0', 'b'), ins('move', 'b', s_('KFB')),
          ins('move', 'a', 'x1', c='f'), ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x1', 'a'), ins('asl', 'a')] + tozero('a') + [
          ins('move', 'a', s_('FB')),
          'tqtint:', '; interpolate: O0 = A0(y) now, O1 = A1(y) next frame', ins('move', s_('TMPS'), 'x0')]
    L += allpass2(P0[0], P0[1], s_('I0A'), s_('I0B'), s_('I0D')) + [ins('move', 'a', s_('FR'))]
    L += [ins('move', s_('TMPS'), 'x0')] + allpass(P1[0], s_('I1A'), s_('I1B'))
    L += [ins('move', 'a', s_('O1')), ins('move', s_('FR'), 'x1')] + mixout(inw, 'x1') + [ins('rts')]
    return L


# ---------------------------------------------------------------- the whole effect
H = """; CYCLES_FORWARD_BRANCHES
; AERIA TAPE -- Chloaelis' tape delay (DSP.h TapeLine in DelayCell) in 24-bit fixed point. Generated by tape/gen.py.
; Two hard-panned lines (left -> left, right -> right). The tape runs at 22,050 Hz: polyphase IIR halfband
; down, one tape sample a side every second frame, the same halfband up. Every coefficient is the plugin's
; formula at that rate. NO magnetic saturation, NO tanh in the loop: |fb| is held by gain-riding.
;
;  knob 0 TIME  30 .. 700 ms (log), the left line
;  knob 1 SKEW  right time = left x 2^((v - 64)/64) (0.5 .. 1.99), held to 700 ms
;  knob 2 FDBK  regen 0 .. 0.6 at 115 (linear), 0.6 .. 1.15 over 116..127 (the plugin's range: it sings)
;  knob 3 WEAR  tape life: 0 = OFF (pristine, no ageing); else ruined after 600 s x 2^((64 - v)/16)
;  knob 4 WOW   wow + flutter depth x v/32 (32 = the plugin's: ~0.5 % when new, widening with wear)
;  knob 5 MIX   equal power (cos / sin); 0 = exact dry
;  FUNC (r6+$c..$e): HISS (level x v/64, slewed) / HPF (feedback 320 Hz) / FRZ (hold) / RFRZ (ColdFire only)
;                    TMOD (SLEW / FADE) / RFSH (0 -> 1: new tape)
;  r6+6 / r6+7: the ColdFire's random freeze for the left / right line (non-zero = hold)
"""


def build(yring, ywin, xring, xbase, out='tape.asm'):
    bld = Build(yring, ywin, xring, xbase, out)
    _lab[0] = 0
    L = [H + f'; Y: the left ring ({yring} words) at the base, then the damage maps (left +0, right +{DMG_STRIDE});'
             f' X: the right ring ({xring} words) at X:${xbase:05x}\n']
    # ============================================================ init
    L += ['init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
          ins('do', '#256', '>tqizero'), ins('move', 'a', 'x:(r4)+'), 'tqizero:', ins('nop')]
    L += [ins('move', 'x:>$213', 'r4'), ins('nop'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', g_('YB')),
          ins('move', 'a', g_('WY')), ins('move', lit(xbase), 'a'), ins('move', 'a', g_('WX')),
          ins('move', lit(max(bld.yclr, bld.xclr)), 'a'), ins('move', 'a', g_('CLR')),
          ins('move', '#>$1', 'a'), ins('move', 'a', g_('FIRST')), ins('move', '#>$5a17c3', 'a'), ins('move', 'a', g_('SEED'))]
    for nm, v in (('SBL', SIDE[0]), ('SBR', SIDE[1]), ('CBL', CBASE[0]), ('CBR', CBASE[1])):
        L += [ins('move', 'r7', 'a'), ins('add', lit(v), 'a'), ins('move', 'a', g_(nm))]
    for side in (0, 1):
        ring = bld.ring(side)
        f1, f2 = FLUT[side][0] / SR * TPU, FLUT[side][1] / SR * TPU
        assert [C[n] for n in ('FP1', 'FP2', 'FI1', 'FI2', 'FAMP')] == list(range(C['FP1'], C['FP1'] + 5))
        seq = ('RAMP', 'SIDEF', 'RBASE', 'RING', 'DMGB', 'NB', 'DMAXA', 'CLHI', 'FLKR')
        assert [C[n] for n in seq] == list(range(C['RAMP'], C['RAMP'] + 9))
        put = lambda src: [ins('move', src, 'a'), ins('move', 'a', 'x:(r4)+')]
        L += [ins('move', 'r7', 'a'), ins('add', lit(CBASE[side] + C['FP1']), 'a'), ins('move', 'a', 'r4')]   # (a move between)
        for v in (FLUT[side][2], FLUT[side][3], f1, f2, 0.5):
            L += put(q(v))
        L += [ins('move', lit(C['RAMP'] - C['FAMP'] - 1), 'n4'), ins('lua', '(r4)+n4', 'r4')]
        L += put(ONE) + put(lit(side))
        if side == 0:
            L += [ins('move', g_('YB'), 'a'), ins('move', 'a', 'x:(r4)+')] + put(lit(ring)) + [
                ins('move', g_('YB'), 'a'), ins('add', lit(yring), 'a'), ins('move', 'a', 'x:(r4)+')]
        else:
            L += put(lit(xbase)) + put(lit(ring)) + [
                ins('move', g_('YB'), 'a'), ins('add', lit(yring + DMG_STRIDE), 'a'), ins('move', 'a', 'x:(r4)+')]
        L += put(lit(bld.nb[side])) + put(q(min(bld.dmax(side), T_MAX * SR) / 16384)) + put(q((ring - 4) / 16384))
        L += put(q(0.13 * 16 * UPD / FS * bld.nb[side] / PLUGIN_NB * 64))
        L += [ins('move', ONE, 'a'), ins('move', 'a', f'x:(r7+${SIDE[side] + S["GA"]:02x})')]
    L += [ins('rts')]

    # ============================================================ per call
    L += ['proc:', ins('move', '#>$ffffff', 'm1'), ins('move', '#>$ffffff', 'm2'), ins('move', '#>$ffffff', 'm3'),
          ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm5'),
          ins('move', g_('CLR'), 'a'), ins('tst', 'a'), ins('beq', 'tqready'),
          '; ---- clearing: 128 words of each region a call, dry out ----',
          ins('sub', '#>$1', 'a'), ins('move', 'a', g_('CLR')),
          ins('cmp', lit(bld.yclr), 'a'), ins('bge', 'tqnocly'),
          ins('asl', '#$7', 'a', 'a'), ins('move', g_('YB'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'), ins('clr', 'a'),
          ins('do', '#128', '>tqclry'), ins('move', 'a', 'y:(r4)+'), 'tqclry:', ins('nop'),
          'tqnocly:', ins('move', g_('CLR'), 'a'), ins('cmp', lit(bld.xclr), 'a'), ins('bge', 'tqnoclx'),
          ins('asl', '#$7', 'a', 'a'), ins('add', lit(xbase), 'a'), ins('move', 'a', 'r4'), ins('clr', 'a'),
          ins('do', '#128', '>tqclrx'), ins('move', 'a', 'x:(r4)+'), 'tqclrx:', ins('nop'),
          'tqnoclx:', ins('rts'),
          'tqready:',
          '; ---- MIX: equal power, smoothed 1/16 a call; cos - 1 and sin ramp over the call ----',
          ins('move', 'x:(r6+$5)', 'x0'), ins('move', 'x0', 'a'), ins('move', q(1 / 127), 'y0'), ins('mac', 'y0', 'x0', 'a',
                                                                                              c='m = v/127')]
    L += clamp('a', '#>$0', ONE) + [ins('move', 'a', g_('T1')), ins('move', g_('MIXS'), 'b'), ins('sub', 'b', 'a'),
                                    ins('asr', '#$4', 'a', 'a'), ins('add', 'a', 'b'), ins('move', 'b', g_('MIXS'))]
    L += snap_close(G['T1'], G['MIXS'], 'tqmsnap') + [
          ins('move', g_('MIXS'), 'a'), ins('move', g_('MIXP'), 'x0'), ins('move', 'a', g_('MIXP')), ins('cmp', 'x0', 'a'),
          ins('bne', 'tqmchg'), ins('clr', 'a'), ins('move', 'a', g_('DSG')), ins('move', 'a', g_('DCM1')), ins('bra', 'tqmdone'),
          'tqmchg:', ins('tst', 'a'), ins('bne', 'tqmnz'), ins('clr', 'b'), ins('move', 'b', g_('SGT')),
          ins('move', 'b', g_('CMT')), ins('bra', 'tqmset'),
          'tqmnz:', ins('bsr', '>tqhsin'), ins('asl', 'b'), ins('move', 'b', g_('SGT'), c='sin(m pi/2)'),
          ins('move', g_('MIXS'), 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a'), ins('bsr', '>tqhsin'),
          ins('asl', 'b'), ins('add', '#>$800000', 'b'), ins('move', 'b', g_('CMT'), c='cos(m pi/2) - 1'),
          'tqmset:', ins('move', g_('SGT'), 'a'), ins('move', g_('SG'), 'x0'), ins('sub', 'x0', 'a'), ins('asr', '#$3', 'a', 'a'),
          ins('move', 'a', g_('DSG')), ins('move', g_('CMT'), 'a'), ins('move', g_('CM1'), 'x0'), ins('sub', 'x0', 'a'),
          ins('asr', '#$3', 'a', 'a'), ins('move', 'a', g_('DCM1')), 'tqmdone:',
          '; ---- FDBK: k/2 = 1.05 regen / 2, smoothed 1/16 a call ----',
          ins('move', 'x:(r6+$2)', 'a'), ins('move', lit(FB_KNEE_V << 16), 'x0'), ins('cmp', 'x0', 'a'), ins('bgt', 'tqfbtop'),
          ins('move', 'a', 'x0'), ins('move', q(FB_GAIN / 2 * FB_KNEE * 128 / FB_KNEE_V), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('bra', 'tqfbset'),
          'tqfbtop:', ins('sub', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', q(FB_GAIN / 2 * (FB_TOP - FB_KNEE) * 128 / (127 - FB_KNEE_V) / 8), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('asl', '#$3', 'a', 'a'), ins('add', q(FB_GAIN / 2 * FB_KNEE), 'a'),
          'tqfbset:', ins('move', g_('REGS'), 'b'), ins('sub', 'b', 'a'), ins('asr', '#$4', 'a', 'a'), ins('add', 'a', 'b'),
          ins('move', 'b', g_('REGS')),
          '; ---- WOW: W/4 = v/128, smoothed 1/2048 a call (0.74 s: the head offset moves as wob x W, so a jump would swoop) ----',
          ins('move', 'x:(r6+$4)', 'a'), ins('move', g_('WOWS'), 'b'), ins('sub', 'b', 'a'), ins('asr', '#$b', 'a', 'a'),
          ins('add', 'a', 'b'), ins('move', 'b', g_('WOWS')),
          '; ---- FUNC: HISS / HPF (r6+$c), FRZ (r6+$d), TMOD / RFSH (r6+$e) ----',
          ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$007f00', 'a'), ins('move', 'a1', g_('HPON')),
          ins('move', 'x:(r6+$d)', 'a'), ins('and', '#>$7f0000', 'a'), ins('move', 'a1', g_('FRZK')),
          ins('move', 'x:(r6+$e)', 'a'), ins('and', '#>$7f0000', 'a'), ins('move', 'a1', g_('TMOD')),
          ins('move', 'x:(r6+$e)', 'a'), ins('and', '#>$007f00', 'a'), ins('move', g_('RFPV'), 'b'), ins('move', 'a1', g_('RFPV')),
          ins('tst', 'b'), ins('bne', 'tqnorf'), ins('tst', 'a'), ins('beq', 'tqnorf'),
          '; RFSH 0 -> 1: new tape (age 0, both damage maps cleared)',
          ins('clr', 'a'), ins('move', 'a', g_('AGEH')), ins('move', 'a', g_('AGEL')),
          ins('move', g_('YB'), 'b'), ins('add', lit(yring), 'b'), ins('move', 'b', 'r1'), ins('nop'),
          ins('do', f'#{2 * DMG_STRIDE}', '>tqrfy'), ins('move', 'a', 'y:(r1)+'), 'tqrfy:', ins('nop'),
          'tqnorf:',
          '; ---- WEAR (every 32 calls): the life gate glides to (v > 0); age += 2^((v - 64)/16) / 600 s a second ----',
          ins('move', g_('CNT'), 'a'), ins('and', lit(WEAR_EVERY - 1), 'a'), ins('cmp', '#>$3', 'a'), ins('bne', 'tqwsq'),
          ins('move', 'x:(r6+$3)', 'a'), ins('tst', 'a'), ins('beq', 'tqnoage'),
          ins('move', ONE, 'a'), ins('move', 'a', g_('T1')),
          ins('move', 'x:(r6+$3)', 'x0'), ins('move', '#>$7f0000', 'a'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', q(128 / 16 / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='(127 - v)/16/32'),
          ins('bsr', '>tqexp'), ins('move', 'b', 'x0'),
          ins('move', q(16 * WEAR_EVERY / FS / LIFE * 2 ** (63 / 16) * 2 ** 11), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('asr', '#$b', 'a', 'a'), ins('move', g_('AGEH'), 'b'), ins('move', g_('AGEL'), 'b0'), ins('add', 'a', 'b'),
          ins('move', ONE, 'x0'), ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b'), ins('move', 'b1', g_('AGEH')),
          ins('move', 'b0', g_('AGEL')), ins('bra', 'tqage'),
          'tqnoage:', ins('clr', 'a'), ins('move', 'a', g_('T1')),
          'tqage:', ins('move', g_('T1'), 'a'), ins('move', g_('LG'), 'b'), ins('sub', 'b', 'a'), ins('asr', '#$1', 'a', 'a'),
          ins('add', 'a', 'b'), ins('move', 'b', g_('LG'))]
    L += snap_close(G['T1'], G['LG'], 'tqlgsnap') + [ins('bra', 'tqwskip'),
          'tqwsq:', ins('cmp', '#>$7', 'a'), ins('bne', 'tqwskip'), '; sqrt(age) on another call',
          ins('move', g_('AGEH'), 'x0'), ins('move', g_('LG'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('EAGE')),
          ins('move', 'a', 'x1'), ins('bsr', '>tqsqrt'), ins('move', 'b', g_('WEAR')),
          'tqwskip:',
          '; ---- HISS: the fade (0 -> 1 in 1.6 s after loading, smoothstep) x the level (slewed at the same rate) ----',
          ins('move', g_('HG'), 'a'), ins('add', q(16 / FS / 1.6), 'a'), ins('cmp', ONE, 'a'), ins('blt', 'tqhisg'),
          ins('move', ONE, 'a'),
          'tqhisg:', ins('move', 'a', g_('HG')),
          ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$7f0000', 'a'), ins('move', g_('HLV'), 'b'), ins('move', 'b', 'x0'),
          ins('sub', 'x0', 'a'), ins('move', q(16 / FS / 1.6 / 2), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a'),
          ins('move', q(-16 / FS / 1.4 / 2), 'x1'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a'), ins('add', 'a', 'b'),
          ins('move', g_('FIRST'), 'x0'), ins('move', 'x:(r6+$c)', 'a'),
          ins('and', '#>$7f0000', 'a'), ins('move', 'a1', 'x1'), ins('move', 'x0', 'a'), ins('tst', 'a'), ins('tne', 'x1', 'b',),
          ins('move', 'b', g_('HLV'), c='level/2 (at its knob from the start: only the fade-in applies at load)'),
          ins('move', g_('HG'), 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x1'), ins('move', q(0.75), 'a'),
          ins('move', q(0.5), 'y0'), ins('mac', '-y0', 'x0', 'a'), ins('move', 'a', 'y0'), ins('mpy', 'x1', 'y0', 'a'),
          ins('asl', '#$2', 'a', 'a', c='smoothstep = h^2 (3 - 2h)'), ins('move', 'a', 'x0'), ins('move', g_('HLV'), 'y0'),
          ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', g_('HGL'), c='fade x level / 2'), ins('move', 'a', 'x1'),
          ins('move', g_('WEAR'), 'y0'), ins('move', q(0.0018 * 2 / SCALE * 8), 'a'), ins('move', q(0.008 * 2 / SCALE * 8), 'x0'),
          ins('mac', 'y0', 'x0', 'a'), ins('move', 'a', 'y0'), ins('mpy', 'y0', 'x1', 'a'), ins('asr', '#$3', 'a', 'a'),
          ins('move', 'a', g_('HL')),
          '; ---- a side\'s update in three parts over 8 calls: L heads, L wow, L damage, -, R heads, R wow, R damage, - ----',
          ins('move', g_('CNT'), 'a'), ins('and', '#>$000003', 'a'), ins('cmp', '#>$3', 'a'), ins('beq', 'tqsdn'),
          ins('move', g_('CNT'), 'a'), ins('and', '#>$000004', 'a'), ins('bne', 'tqsdr'),
          ins('move', g_('SBL'), 'r3'), ins('move', g_('CBL'), 'r2'), ins('move', g_('WY'), 'x0'), ins('bra', 'tqsgo'),
          'tqsdr:', ins('move', g_('SBR'), 'r3'), ins('move', g_('CBR'), 'r2'), ins('move', g_('WX'), 'x0'),
          'tqsgo:', ins('move', 'x0', c_('WPOS')), ins('move', g_('CNT'), 'a'), ins('and', '#>$000003', 'a'), ins('beq', 'tqsp0'),
          ins('cmp', '#>$2', 'a'), ins('beq', 'tqsp2'), ins('bsr', '>tqswob'), ins('bra', 'tqsdn'),
          'tqsp0:', ins('bsr', '>tqside'), ins('bra', 'tqsdn'),
          'tqsp2:', ins('bsr', '>tqsdmg'), ins('move', g_('CNT'), 'a'), ins('and', '#>$000007', 'a'), ins('cmp', '#>$6', 'a'),
          ins('bne', 'tqsdn'), ins('clr', 'a'), ins('move', 'a', g_('FIRST')),
          'tqsdn:', ins('move', g_('CNT'), 'a'), ins('add', '#>$1', 'a'), ins('move', 'a1', g_('CNT')),
          '; ---- the frames ----', ins('move', g_('WY'), 'r4'), ins('move', g_('WX'), 'r5'),
          ins('move', lit(yring - 1), 'm4'), ins('move', lit(xring - 1), 'm5'), ins('move', lit(yring - 1), 'm1'),
          ins('do', 'n7', '>tqfend'),
          ins('move', g_('PH'), 'a'), ins('tst', 'a'), ins('bne', 'tqphb'),
          '; phase A: keep x[2n-1]; out = the last tick\'s O1',
          ins('move', ONE, 'a'), ins('move', 'a', g_('PH'))]
    for side in (0, 1):
        inw = 'x:(r0)' if side == 0 else 'x:(r0+n0)'
        sb = SIDE[side]
        if side == 1:
            L += [ins('move', '#>$1', 'n0')]
        L += [ins('move', inw, 'a'), ins('asr', '#$3', 'a', 'a'), ins('move', 'a', f'x:(r7+${sb + S["XA"]:02x})'),
              ins('move', f'x:(r7+${sb + S["O1"]:02x})', 'x1')] + mixout(inw, 'x1')
    L += [ins('bra', 'tqphd'), 'tqphb:', ins('clr', 'a'), ins('move', 'a', g_('PH')),
          ins('move', g_('DCM1'), 'b'), ins('tst', 'b'), ins('beq', 'tqmxs', c='both steps are 0 together (settled)'),
          ins('move', g_('CM1'), 'a'), ins('add', 'b', 'a'), ins('move', 'a', g_('CM1')),
          ins('move', g_('SG'), 'a'), ins('move', g_('DSG'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', g_('SG')), 'tqmxs:']
    for side in (0, 1):
        L += [ins('move', g_(('SBL', 'SBR')[side]), 'r3'), ins('move', g_(('CBL', 'CBR')[side]), 'r2'),
              ins('move', lit(side), 'n0'), ins('bsr', '>tqtick')]
    L += ['tqphd:', ins('move', '(r0)+'), ins('move', '(r0)+'), 'tqfend:', ins('nop'),
          '; exact ends of the gain ramps',
          ins('move', g_('SGT'), 'a'), ins('move', 'a', g_('SG')), ins('move', g_('CMT'), 'a'), ins('move', 'a', g_('CM1')),
          ins('move', 'r4', g_('WY')), ins('move', 'r5', g_('WX')),
          ins('move', '#>$ffffff', 'm1'), ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm5'), ins('rts'),
          '; ---- subroutines (after their callers) ----']
    L += side_sub() + tick_sub() + flake_sub() + rel_sub() + exp_sub() + div_sub() + tsin_sub() + hsin_sub() + herm_subs() + lin_subs()
    L += dis_subs() + sqrt_sub() + dmg_sub() + rand_sub()
    src = finish(L)
    (HERE / out).write_text(src)
    return bld


if __name__ == '__main__':
    todo = ['tape_test.asm'] if '--test' in sys.argv else ['tape.asm', 'tape_test.asm'] if '--all' in sys.argv else ['tape.asm']
    for nm in todo:
        b = build(out=nm, **BUILDS[nm])
        print('wrote', nm, 'LAT %.3f tape samples' % LAT, 'dmax', b.dmax(0), b.dmax(1))
