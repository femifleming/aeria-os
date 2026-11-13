#!/usr/bin/env python3
"""Generates filt.asm (AERIA FILT: Chloaelis' multimode filter, DSP.h MultiModeFilter, in 24-bit fixed point).

    python3 filt/gen.py          (host, no dependencies)   -- test.py imports this and rebuilds

A TPT (Zavalishin) state-variable filter whose MODE morphs LP > BP > HP > NOTCH, cutoff 20 Hz .. 20 kHz,
resonance k = 2 .. 0.04, each side with its own cutoff (SKEW, and the Lorenz attractor the ColdFire runs).
The plugin's tanh input stage and its integrator limiter are NOT ported: nothing here saturates. The
resonance is made safe linearly instead: a gain c (<= 1) holds the morphed response's resonant peak at
unity (no setting makes a tone louder than it came in), and k never goes under 0.04.

No buffer, no P table: everything lives in the r7 block (64 words).
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, finish, e2_sub, snap_close  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = 44100.0
F_LO, F_HI = 20.0, 20000.0
OCT = math.log2(F_HI / F_LO)            # 9.966 octaves of CUT
KCUT = OCT * 128 / 127 / 16             # o/16 = CUT word x KCUT  (word = v/128)
OMAX = OCT / 16                         # o/16 at 20 kHz
SKEW_STEP = 1 / 16                      # SKEW: (v - 64)/16 octaves on the right side (-4 .. +3.94)
LZ_OCT = 3.5                            # the plugin: cutoff x 2^(depth x 3.5 x lorenz)
KLZ = LZ_OCT * 128 / 127 / 16           # o/16 += LRNZ word x KLZ x lorenz word
H20K = math.pi * F_HI / (2 * FS)        # h = theta/2 at 20 kHz (theta = pi fc / fs)
KH_MIN = 0.02                           # k >= 0.04 (the plugin's k at res 1)
KRES = 0.98 * 128 / 127                 # kh = k/2 = 1 - 0.98 res, res = v/127
KMODE = 3 * 128 / 126 / 4               # (morph/4) = MODE word x KMODE: morph = 3 v/126 (42 BP, 84 HP, 126 NOTCH)
LPK = 0.16                              # LP's resonant peak <= 1/k + 0.16 (k < 1.19), 1 above
OCTA = math.sqrt(2) - 1                 # |(a, b)| <= max + 0.4142 min (overestimates by <= 8.2 %)
XSHIFT = 2                              # the filter runs on kh x / 4
OSHIFT = 4                              # the output sum is formed / 16
PHASES = 3                              # a call does one job in turn: left side, the gains, right side
GLIDE = 3                               # each job glides its knobs 2^-3 (every third call: tau 8.7 ms); cutoffs twice
RAMP = 48                               # the gains and kh/4 ramp sample by sample over the 48 samples to the next job
LZ_WORDS = (6, 7)                       # knob-block words the ColdFire writes: left / right Lorenz, signed Q23


# ------------------------------------------------------------------ the float reference
def cut_oct(v):
    """CUT 0..127 -> octaves above 20 Hz."""
    return OCT * min(1.0, v / 127)


def hz_of_oct(o):
    return F_LO * 2 ** min(OCT, max(0.0, o))


def side_hz(cut, skew=64, lrnz=0, lz=0.0, right=False):
    o = cut_oct(cut) + LZ_OCT * (lrnz / 127) * lz
    if right:
        o += (skew - 64) * SKEW_STEP
    return hz_of_oct(o)


def kh_of(res):
    return max(KH_MIN, 1 - 0.98 * min(1.0, res / 127))


def morph_of(mode):
    return min(3.0, 3 * mode / 126)


def weights(m):
    """(lp, bp, hp, notch) weights of the plugin's morph."""
    i = min(int(m), 2)
    t = m - i
    w = [0.0] * 4
    w[i] += 1 - t
    w[i + 1] += t
    return w


def comp(kh, m):
    """c <= 1: holds the morphed response's resonant peak at (about) unity. At the cutoff the resonant part of
    the morph (weight wr = |LP - HP|) is wr/k in quadrature with the rest (weight 1 - wr: BP 1, notch 0); LP's
    own peak is <= Glp = max(1, 1/k + 0.16). Peak estimate G = |(1 - wr, wr Glp)| by max + 0.4142 min;
    c = 1/G when G > 1."""
    w = weights(m)
    wr = abs(w[0] - w[2])
    glp = max(1.0, 1 / (2 * kh) + LPK)
    a, b = 1 - wr, wr * glp
    g = max(a, b) + OCTA * min(a, b)
    return 1.0 if g <= 1 else 1 / g


class Model:
    """DSP.h MultiModeFilter without the tanh stage and the integrator limiter, one side, float; times comp()."""

    def __init__(self):
        self.ic1 = self.ic2 = 0.0

    def run(self, x, hz, res, mode):
        fc = min(max(hz, 12.0), FS * 0.46)
        g = math.tan(math.pi * fc / FS)
        kh = kh_of(res)
        k = 2 * kh
        m = morph_of(mode)
        w = weights(m)
        c = comp(kh, m)
        a1 = 1 / (1 + g * (g + k))
        a2, a3 = g * a1, g * g * a1
        out = []
        for v in x:
            v3 = v - self.ic2
            v1 = a1 * self.ic1 + a2 * v3
            v2 = self.ic2 + a2 * self.ic1 + a3 * v3
            self.ic1 = 2 * v1 - self.ic1
            self.ic2 = 2 * v2 - self.ic2
            lp, bp, hp = v2, k * v1, v - k * v1 - v2
            out.append(c * (w[0] * lp + w[1] * bp + w[2] * hp + w[3] * (lp + hp)))
        return out


def response(hz, res, mode, f):
    """|H(f)| of the reference (bilinear SVF, morphed, times comp)."""
    fc = min(max(hz, 12.0), FS * 0.46)
    g = math.tan(math.pi * fc / FS)
    kh = kh_of(res)
    k = 2 * kh
    m = morph_of(mode)
    w = weights(m)
    z = complex(math.cos(2 * math.pi * f / FS), math.sin(2 * math.pi * f / FS))
    s = (z - 1) / (z + 1) / g                  # bilinear, prewarped at fc
    d = s * s + k * s + 1
    lp, bp, hp = 1 / d, k * s / d, s * s / d
    return abs(comp(kh, m) * (w[0] * lp + w[1] * bp + w[2] * hp + w[3] * (lp + hp)))


# ------------------------------------------------------------------ r7 block
SLOTS = """FIRST PH OL1 OL2 OR1 OR2 KH1 KHS MD1 MDS MXS MXT KAC KBC KCC KHC DKA DKB DKC DKH KAT KBT KCT KHT
SN PP S2T SW CW TB TD T1 RA RB RC WR EK CC GLC GRC DGL DGR R6S""".split()
AT = {nm: i for i, nm in enumerate(SLOTS)}
COEF = {'L': 0x2c, 'R': 0x2f}           # c1 a2 g/8 (the g/8 a job writes: the target of the g ramp)
STATE = {'L': 0x32, 'R': 0x37}          # s1 hi lo rounded, s2 hi lo
assert len(SLOTS) <= COEF['L'] and STATE['R'] + 5 <= 64
USED = 64
RAMPS = (('KAC', 'DKA', 'KAT', 'r1'), ('KBC', 'DKB', 'KBT', 'r2'), ('KCC', 'DKC', 'KCT', 'r3'), ('KHC', 'DKH', 'KHT', 'r5'))
GRAMPS = (('GLC', 'DGL', None, 'r4', 'L'), ('GRC', 'DGR', None, 'r6', 'R'))   # g/8 ramps, a side's own
RREG = {c: r for c, _, _, r in RAMPS}
GREG = {sd: r for _, _, _, r, sd in GRAMPS}


def r7(nm):
    return f'x:(r7+${AT[nm]:02x})'


def cf(side, k):
    return f'x:(r7+${COEF[side] + k:02x})'


def st(side, k):
    return f'x:(r7+${STATE[side] + k:02x})'


H = f"""; CYCLES_FORWARD_BRANCHES
; AERIA FILT -- Chloaelis' multimode filter (DSP.h MultiModeFilter) in 24-bit fixed point. Generated by filt/gen.py.
; TPT state-variable filter, MODE morphs LP > BP > HP > NOTCH continuously, cutoff 20 Hz .. 20 kHz (log),
; k = 2 .. 0.04. NO tanh input stage, NO integrator limiter: nothing saturates. Instead a linear gain c <= 1
; holds the morphed response's resonant peak at unity: at the cutoff the resonant part (weight wr = |LP - HP|)
; is wr Glp, Glp = max(1, 1/k + {LPK}), in quadrature with the rest (1 - wr); G = max + {OCTA:.4f} min; c = 1/G (G > 1).
;
;  knob 0 CUT   cutoff 20 Hz x 1000^(v/127)                      (both sides)
;  knob 1 RES   k = 2 - 1.96 v/127 (kh = k/2 >= {KH_MIN})
;  knob 2 MODE  morph = 3 v/126: 0 LP, 42 BP, 84 HP, 126..127 NOTCH, linear in between (the plugin's morph)
;  knob 3 SKEW  right side's cutoff (v - 64)/16 octaves from the left's
;  knob 4 LRNZ  Lorenz depth: each side's cutoff x 2^(3.5 (v/127) lz), lz = the signed word r6+$6 (left) / r6+$7
;               (right) the ColdFire writes (Q23, -1..1: the attractor's x and y as the plugin scales them)
;  knob 5 MIX   dry/wet; 0 = exact dry
;  (knob words r6+$c..$e, the FUNC layer LZSP CHAS SHAP, are the ColdFire's: not read here)
; A call does ONE job in turn (left side, gains, right side): each runs every third call (0.73 ms).
;   left/right: the cutoff target glides twice 2^-{GLIDE} (two one-poles, 8.7 ms each), then that side's
;     coefficients:  o = octaves above 20 Hz, clamped 0..{OCT:.4f};  h = {H20K:.5f} x 2^-(o_max - o) = theta/2
;     s, c = sin, cos theta (Taylor on h, double angle);  D = 1 + k s c;  a2 = s c / D;  a3 = s^2 / D
;     c1 = a3 + k a2 (= 1 - a1);  g/8 = (s/8)/c (= tan(theta)/8, by division)
;   gains: RES and MODE glide twice 2^-{GLIDE}, MIX once; the morph weights, c, and the gains KA KB KC and kh/4; each of
;     these four then ramps sample by sample over the next {RAMP} samples (address registers r1 r2 r3 r5)
; The first call snaps every glide, tunes the left side and copies it to the right; until the first
; gains job the output is the dry (the gains start at 0) and then ramps to the wet over {RAMP} samples.
; Per sample and side, on x' = kh x/4 (every state stays near the input's size at any k) in 48 bits;
; products of a state take its rounded high word and its signed low word (no dead band):
;   w = s2 - x';  t = -a2 w - c1 s1;  v1 = s1 + t;  s1 += 2t;  u = g v1;  v2 = s2 + u;  s2 += 2u
;   out = x + 16 (KA x + KB v1 + KC v2)   (KA = mix (c ra - 1)/16, KB = mix c rb/2, KC = mix c rc/(4 kh))
;   with the morph as  wet = c (ra x + rb k v1 + rc v2): ra = hp + nt, rb = bp - hp - nt, rc = lp - hp weights
; r7: {' '.join(f'{v:02x} {k}' for k, v in AT.items())}
;     ${COEF['L']:02x}/${COEF['R']:02x}: c1 a2 g/8 left/right   ${STATE['L']:02x}/${STATE['R']:02x}: s1 hi lo rnd, s2 hi lo
"""


def clamp(acc, lo, hi):
    """acc into [lo, hi] (Q23 literals); clobbers x0."""
    return [ins('move', hi, 'x0'), ins('cmp', 'x0', acc), ins('tgt', 'x0', acc),
            ins('move', lo, 'x0'), ins('cmp', 'x0', acc), ins('tlt', 'x0', acc)]


def glide(slot, shift=GLIDE):
    """slot += (a - slot) >> shift (a = the target); the new value is left in b."""
    return [ins('move', r7(slot), 'b'), ins('sub', 'b', 'a'), ins('asr', f'#${shift:x}', 'a', 'a'),
            ins('add', 'a', 'b'), ins('move', 'b', r7(slot))]


def glide2(s1, s2, shift=GLIDE):
    """two one-poles: s1 toward a, s2 toward s1; the new s2 is left in a."""
    return glide(s1, shift) + [ins('move', r7(s2), 'a'), ins('sub', 'a', 'b'), ins('asr', f'#${shift:x}', 'b', 'b'),
                               ins('add', 'b', 'a'), ins('move', 'a', r7(s2))]


def tgt_side(right):
    """a = that side's cutoff target, o/16 clamped: CUT KCUT + LRNZ KLZ lz (+ skew on the right)."""
    L = [ins('move', 'x:(r6)', 'x0'), ins('move', q(KCUT), 'y0'), ins('mpy', 'y0', 'x0', 'b'),
         ins('move', 'x:(r6+$4)', 'x0'), ins('move', q(KLZ), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
         ins('move', 'a', 'x0'), ins('move', f'x:(r6+${LZ_WORDS[1 if right else 0]:x})', 'y0'),
         ins('mpy', 'y0', 'x0', 'a'), ins('add', 'b', 'a', c='base + depth x lorenz')]
    if right:
        L += [ins('move', 'x:(r6+$3)', 'b'), ins('sub', q(0.5), 'b'), ins('asr', 'b'), ins('add', 'b', 'a', c='skew')]
    return L + clamp('a', '#>$000000', q(OMAX))


def tgt_kh():
    return [ins('move', 'x:(r6+$1)', 'x0'), ins('move', q(KRES), 'y0'), ins('move', '#>$7fffff', 'a'),
            ins('mac', '-y0', 'x0', 'a')] + clamp('a', q(KH_MIN), '#>$7fffff')


def tgt_mix():
    return [ins('move', 'x:(r6+$5)', 'x0'), ins('move', 'x0', 'a'), ins('move', q(1 / 127), 'y0'),
            ins('mac', 'y0', 'x0', 'a'), ins('move', '#>$7fffff', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a')]


def channel(side):
    """One sample of one side: x:(r0) in, x:(r0)+ out. Uses a b x0 x1 y0 y1; r1 r2 r3 = KA KB KC, r5 = kh/4."""
    S1H, S1L, S1R, S2H, S2L = (st(side, k) for k in range(5))
    C1, A2, G8 = (cf(side, k) for k in range(3))
    return [f'; ---- {"left" if side == "L" else "right"} ----',
            ins('move', S2H, 'b'), ins('move', S2L, 'b0'),
            ins('move', 'x:(r0)', 'x0'), ins('move', RREG['KHC'], 'y0'),
            ins('mac', '-y0', 'x0', 'b', c='w = s2 - kh x/4  (= -v3, 48 bit)'),
            ins('move', 'b0', 'y1', c='its low word, signed'), ins('rnd', 'b'), ins('move', 'b', 'x1'),
            ins('move', A2, 'y0'), ins('mpy', '-y1', 'y0', 'a'),
            ins('move', S1L, 'x0'), ins('move', C1, 'y0'), ins('mac', '-y0', 'x0', 'a'),
            ins('asr', '#$18', 'a', 'a', c='the low-word products'),
            ins('move', S1R, 'x0'), ins('mac', '-y0', 'x0', 'a'),
            ins('move', A2, 'y0'), ins('mac', '-x1', 'y0', 'a', c='t = a2 v3 - c1 s1'),
            ins('move', S1H, 'b'), ins('move', S1L, 'b0'),
            ins('add', 'a', 'b', c='v1 = s1 + t'), ins('add', 'b', 'a', c='s1 + 2t'),
            ins('move', 'a', S1H), ins('move', 'a0', S1L), ins('rnd', 'a'), ins('move', 'a', S1R),
            ins('move', 'b0', 'y1'), ins('rnd', 'b'), ins('move', 'b', 'x1', c='v1: x1 rounded, y1 low'),
            ins('move', GREG[side], 'y0'), ins('mpy', 'y1', 'y0', 'a'), ins('asr', '#$18', 'a', 'a'),
            ins('mac', 'x1', 'y0', 'a'), ins('asl', '#$3', 'a', 'a', c='u = g v1'),
            ins('move', S2H, 'b'), ins('move', S2L, 'b0'),
            ins('add', 'a', 'b', c='v2 = s2 + u'), ins('add', 'b', 'a', c='s2 + 2u'),
            ins('move', 'a', S2H), ins('move', 'a0', S2L), ins('rnd', 'b'), ins('move', 'b', 'y1'),
            ins('move', RREG['KCC'], 'y0'), ins('mpy', 'y1', 'y0', 'a'),
            ins('move', RREG['KBC'], 'y0'), ins('mac', 'x1', 'y0', 'a'),
            ins('move', RREG['KAC'], 'y0'), ins('move', 'x:(r0)', 'x0'), ins('mac', 'y0', 'x0', 'a'),
            ins('asl', f'#${OSHIFT:x}', 'a', 'a'), ins('add', 'x0', 'a', c='+ the dry'),
            ins('move', 'a', 'x:(r0)+')]


def side_job(side, first=False):
    c, dl, _, _, _ = GRAMPS[0 if side == 'L' else 1]
    g8 = f'x:(r7+${COEF[side] + 2:02x})'
    L = [ins('move', g8, 'x0'), ins('move', 'x0', r7(c), c='g ramps from the last target ...'),
         ins('lua', f'(r7+${COEF[side]:02x})', 'r4'), ins('bsr', '>flside'),
         ins('move', g8, 'a', c='... to the new one')]
    if first:
        return L + [ins('move', 'a', r7(c))]
    return L + [ins('move', r7(c), 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x0'),
                ins('move', q(1 / RAMP), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', r7(dl))]


def build():
    L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
         ins('do', f'#{USED}', '>flzero'), ins('move', 'a', 'x:(r4)+'), 'flzero:', ins('nop'), ins('rts')]

    # ------------------------------------------------------------ dispatch
    L += ['proc:']
    L += [ins('move', '#>$ffffff', m) for m in ('m1', 'm2', 'm3', 'm4', 'm5')]
    L += [ins('move', r7('FIRST'), 'a'), ins('tst', 'a'), ins('bne', 'flwhich'),
          '; ---- first call: every glide at its target, the left side tuned and copied to the right ----']
    L += tgt_side(False) + [ins('move', 'a', r7('OL1')), ins('move', 'a', r7('OL2'))]
    L += tgt_side(True) + [ins('move', 'a', r7('OR1')), ins('move', 'a', r7('OR2'))]
    L += tgt_kh() + [ins('move', 'a', r7('KH1')), ins('move', 'a', r7('KHS')), ins('asr', f'#${XSHIFT:x}', 'a', 'a'),
                     ins('move', 'a', r7('KHC')), ins('move', 'a', r7('KHT'))]
    L += tgt_mix() + [ins('move', 'a', r7('MXS')), ins('move', 'a', r7('MXT')),
                      ins('move', 'x:(r6+$2)', 'x0'), ins('move', 'x0', r7('MD1')), ins('move', 'x0', r7('MDS')),
                      ins('move', r7('OL2'), 'a')] + side_job('L', first=True)
    L += [ins('move', f'x:(r7+${COEF["L"] + k:02x})', 'x0') if j == 0 else ins('move', 'x0', f'x:(r7+${COEF["R"] + k:02x})')
          for k in range(3) for j in range(2)]
    L += [ins('move', r7('GLC'), 'x0'), ins('move', 'x0', r7('GRC'))]
    L += [ins('move', '#>$1', 'a'), ins('move', 'a', r7('FIRST')), ins('move', 'a', r7('PH'), c='next: the gains'),
          ins('bra', 'flloop'),
          'flwhich:', '; ---- PH 0 left side, 1 gains, 2 right side ----',
          ins('move', r7('PH'), 'a'), ins('move', 'a', 'b'), ins('add', '#>$1', 'b'), ins('cmp', '#>$3', 'b'),
          ins('blt', 'flphok'), ins('clr', 'b'), 'flphok:', ins('move', 'b', r7('PH')),
          ins('tst', 'a'), ins('beq', 'flleft'), ins('cmp', '#>$1', 'a'), ins('beq', 'flgains'),
          '; ---- right side ----']
    L += tgt_side(True) + glide2('OR1', 'OR2') + side_job('R') + [ins('bra', 'flloop'), 'flleft:', '; ---- left side ----']
    L += tgt_side(False) + glide2('OL1', 'OL2') + side_job('L') + [ins('bra', 'flloop')]

    # ------------------------------------------------------------ the gains job
    L += ['flgains:', '; ---- RES, MODE, MIX ----']
    L += tgt_kh() + glide2('KH1', 'KHS')
    L += [ins('move', 'x:(r6+$2)', 'a')] + glide2('MD1', 'MDS')
    L += tgt_mix() + [ins('move', 'a', r7('MXT'))] + glide('MXS')
    L += snap_close(AT['MXT'], AT['MXS'], 'flmsnap')
    L += ['; morph/4 = MODE x 3/4 x 128/126 (<= 3/4): i = top bits, t = the rest',
          ins('move', r7('MDS'), 'x0'), ins('move', q(KMODE), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', '#>$600000', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('move', 'x0', 'a', c='24 bits, a0 clear'), ins('move', 'x0', 'b'),
          ins('asr', '#$15', 'a', 'a'), ins('move', 'a1', 'a', c='i = 0..3'),
          ins('and', '#>$1fffff', 'b'), ins('asl', '#$2', 'b', 'b', c='t'),
          ins('tst', 'a'), ins('beq', 'flmzero'),
          ins('cmp', '#>$1', 'a'), ins('beq', 'flmone'),
          ins('cmp', '#>$2', 'a'), ins('beq', 'flmtwo'),
          '; i = 3: NOTCH',
          ins('move', '#>$7fffff', 'x0'), ins('move', 'x0', r7('RA')), ins('move', '#>$800000', 'x0'),
          ins('move', 'x0', r7('RB')), ins('clr', 'b'), ins('move', 'b', r7('RC')), ins('move', 'b', r7('WR')),
          ins('bra', 'flpeak'),
          'flmzero:', '; LP > BP: ra 0, rb t, rc 1 - t',
          ins('clr', 'a'), ins('move', 'a', r7('RA')), ins('move', 'b', r7('RB')),
          ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'), ins('move', 'a', r7('RC')), ins('move', 'a', r7('WR')),
          ins('bra', 'flpeak'),
          'flmone:', '; BP > HP: ra t, rb 1 - 2t, rc -t',
          ins('move', 'b', r7('RA')), ins('move', 'b', r7('WR')), ins('move', 'b', 'x0'),
          ins('move', '#>$7fffff', 'a'), ins('sub', 'x0', 'a'), ins('sub', 'x0', 'a'), ins('move', 'a', r7('RB')),
          ins('neg', 'b'), ins('move', 'b', r7('RC')),
          ins('bra', 'flpeak'),
          'flmtwo:', '; HP > NOTCH: ra 1, rb -1, rc t - 1',
          ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'), ins('move', 'a', r7('WR')), ins('neg', 'a'),
          ins('move', 'a', r7('RC')), ins('move', '#>$7fffff', 'x0'), ins('move', 'x0', r7('RA')),
          ins('move', '#>$800000', 'x0'), ins('move', 'x0', r7('RB')),
          'flpeak:', '; E = 2^-7/kh = 1/(64 k);  Glp/32 = max(1/32, 2E + 0.16/32)',
          ins('move', r7('KHS'), 'x0'), ins('move', '#>$010000', 'a'), ins('bsr', '>fldiv'),
          ins('move', 'b', r7('EK')), ins('move', 'b', 'a'), ins('asl', 'a'), ins('add', q(LPK / 32), 'a'),
          ins('move', q(1 / 32), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'),
          '; B = wr Glp/32, A = (1 - wr)/32;  G/32 = max + 0.4142 min',
          ins('move', 'a', 'x0'), ins('move', r7('WR'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', '#>$7fffff', 'b'), ins('move', r7('WR'), 'y1'), ins('sub', 'y1', 'b'), ins('asr', '#$5', 'b', 'b'),
          ins('move', 'b', 'y1'), ins('move', 'b', 'a'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a', c='max'),
          ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b', c='min'),
          ins('move', 'b', 'y0'), ins('move', q(OCTA), 'x1'), ins('mpy', 'x1', 'y0', 'b'), ins('add', 'b', 'a'),
          '; c = 1 when G <= 1, else (1/32)/(G/32)',
          ins('move', 'a', 'x0'), ins('move', q(1 / 32), 'a'), ins('cmp', 'x0', 'a'), ins('bge', 'flcone'),
          ins('bsr', '>fldiv'), ins('bra', 'flchave'),
          'flcone:', ins('move', '#>$7fffff', 'b'),
          'flchave:', ins('move', 'b', r7('CC')), ins('move', 'b', 'x1'),
          '; KA = mix (c ra - 1)/16',
          ins('move', r7('RA'), 'y0'), ins('mpy', 'x1', 'y0', 'b'), ins('move', '#>$800000', 'a'), ins('add', 'b', 'a'),
          ins('move', 'a', 'x0'), ins('move', r7('MXS'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('asr', f'#${OSHIFT:x}', 'a', 'a'), ins('move', 'a', r7('T1')),
          '; KB = mix c rb / 2',
          ins('move', r7('RB'), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
          ins('move', 'a', 'x0'), ins('move', r7('MXS'), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('asr', 'b'),
          '; KC = mix c rc / (4 kh) = mix (c rc) E 32',
          ins('move', r7('RC'), 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('move', 'a', 'x0'),
          ins('move', r7('EK'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$5', 'a', 'a'),
          ins('move', 'a', 'x0'), ins('move', r7('MXS'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          '; the four ramps: from the last target to the new one over the next %d samples' % RAMP,
          ins('move', 'a', r7('TB')), ins('move', 'b', r7('TD')),
          ins('move', r7('KHS'), 'a'), ins('asr', f'#${XSHIFT:x}', 'a', 'a'), ins('move', 'a', r7('SN'))]
    for c, dl, t, src in zip(('KAC', 'KBC', 'KCC', 'KHC'), ('DKA', 'DKB', 'DKC', 'DKH'), ('KAT', 'KBT', 'KCT', 'KHT'),
                             ('T1', 'TD', 'TB', 'SN')):
        L += [ins('move', r7(t), 'x0'), ins('move', 'x0', r7(c), c='start exactly at the last target'),
              ins('move', r7(src), 'a'), ins('move', 'a', r7(t)), ins('sub', 'x0', 'a'), ins('move', 'a', 'x0'),
              ins('move', q(1 / RAMP), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', r7(dl))]

    # ------------------------------------------------------------ the sample loop
    L += ['flloop:', ins('move', 'r6', r7('R6S'), c='r6 is a ramp in the loop (put back after)')]
    for c, dl, _, r in RAMPS:
        L += [ins('move', r7(c), r), ins('move', r7(dl), 'n' + r[1])]
    for c, dl, _, r, _ in GRAMPS:
        L += [ins('move', r7(c), r), ins('move', r7(dl), 'n' + r[1])]
    L += [ins('move', '#>$ffffff', 'm6'), ins('do', 'n7', '>flend')]
    L += [ins('move', f'({r})+n{r[1]}') for _, _, _, r in RAMPS] + [ins('move', f'({r})+n{r[1]}') for _, _, _, r, _ in GRAMPS]
    L += channel('L') + channel('R')
    L += ['flend:', ins('nop')]
    L += [ins('move', r, r7(c)) for c, _, _, r in RAMPS] + [ins('move', r, r7(c)) for c, _, _, r, _ in GRAMPS]
    L += [ins('move', r7('R6S'), 'r6'), ins('rts')]

    # ------------------------------------------------------------ subroutines (after their callers)
    L += ['; ---- a = o/16, r4 = the side\'s c1 a2 g/8 ----', 'flside:',
          ins('move', 'a', 'x0'), ins('move', q(OMAX), 'a'), ins('sub', 'x0', 'a'), ins('asr', 'a', c='y/32, y = octaves under 20 kHz'),
          ins('bsr', '>flexp'),
          ins('move', 'b', 'x0'), ins('move', q(H20K), 'y0'), ins('mpy', 'y0', 'x0', 'a', c='h = theta/2'),
          ins('bsr', '>flsico'),
          ins('move', r7('KHS'), 'x0'), ins('bsr', '>flcoef'),
          ins('move', r7('SW'), 'a'), ins('asr', '#$3', 'a', 'a'), ins('move', r7('CW'), 'x0'),
          ins('bsr', '>fldiv'), ins('move', 'b', 'x:(r4+$2)', c='g/8 = (s/8)/c'),
          ins('rts')]
    L += e2_sub('flexp', 'flxpend')
    L += ['flsico:', '; a = h -> SW = sin 2h, CW = cos 2h, PP = SW CW, S2T = SW^2',
          ins('move', 'a', 'x1'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'y0', c='h^2'),
          ins('move', q(1 / 5040), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 120), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 6), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
          ins('move', 'x1', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', r7('SN'), c='sin h'),
          ins('move', q(1 / 40320), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 720), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(1 / 24), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', q(0.5), 'b'), ins('sub', 'a', 'b'),
          ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'a', c='1 - cos h'),
          ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0', c='cos h'),
          ins('move', r7('SN'), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'), ins('move', 'a', 'x1', c='sin 2h'),
          ins('move', 'x1', r7('SW')),
          ins('mpy', 'y0', 'y0', 'b'), ins('asl', 'b'), ins('move', '#>$7fffff', 'a'), ins('sub', 'b', 'a'),
          ins('move', 'a', 'x0', c='cos 2h'), ins('move', 'x0', r7('CW')),
          ins('mpy', 'x1', 'x0', 'a'), ins('move', 'a', r7('PP')),
          ins('move', 'x1', 'y0'), ins('mpy', 'y0', 'y0', 'a'), ins('move', 'a', r7('S2T')), ins('rts')]
    L += ['flcoef:', '; x0 = kh -> (r4) c1, a2: P, S2 over D = 1 + 2 kh P (r = 1/D by three Newton steps)',
          ins('move', r7('PP'), 'y0'), ins('move', q(0.5), 'a'), ins('mac', 'y0', 'x0', 'a'),
          ins('move', 'a', 'x1', c='d = D/2 in [0.5, 1)'),
          ins('move', q(0.9571), 'a'), ins('add', q(0.5), 'a'), ins('sub', 'x1', 'a'), ins('move', 'a', 'y0', c='r ~ 1/D')]
    for _ in range(3):
        L += [ins('move', q(0.5), 'a'), ins('mac', '-x1', 'y0', 'a'), ins('add', q(0.5), 'a'),
              ins('move', 'a', 'x0', c='1 - d r'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'),
              ins('move', 'a', 'y0', c='r = 2 r (1 - d r)')]
    L += [ins('move', r7('PP'), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', 'x1', 'x:(r4+$1)', c='a2 = P/D'),
          ins('move', r7('S2T'), 'x0'), ins('mpy', 'y0', 'x0', 'a', c='a3 = S2/D'),
          ins('move', r7('KHS'), 'y0'), ins('mpy', 'x1', 'y0', 'b'), ins('asl', 'b'), ins('add', 'a', 'b'),
          ins('move', 'b', 'x:(r4)', c='c1 = a3 + k a2'), ins('rts')]
    L += ['fldiv:', '; a1 / x0 (0 <= a1 < x0) -> b, 24 div steps',
          ins('move', 'a1', 'a'), ins('andi', '#$fe', 'ccr'),
          ins('do', '#24', '>fldvend'), ins('div', 'x0', 'a'), 'fldvend:', ins('nop'),
          ins('move', 'a0', 'b'), ins('rts')]
    return finish(L)


if __name__ == '__main__':
    src = build()
    (HERE / 'filt.asm').write_text(src)
    print(f'wrote filt.asm ({len(src.splitlines())} lines)')
