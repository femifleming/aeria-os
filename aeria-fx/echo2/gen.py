#!/usr/bin/env python3
"""Generates echo2.asm (AERIA ECHO, second version: Chloaelis' dual hard-panned delay, DSP.h DelayCell).

    python3 gen.py              full build  -> echo2.asm   (Y ring 16,384 at the buffer base, X ring 16,384 at X:$4000)
    python3 gen.py --test       harness builds -> echo2_test.asm (Y 15,870, X at X:$10000), echo2_fullx.asm (Y 16,384, X at X:$10000)

Dual mono: left in -> left line (Y ring) -> left out; right in -> right line (X ring) -> right out. Each line is
read by a Catmull-Rom (Hermite) head at a fractional delay and regenerates through TONE (one-pole lowpass), two
one-pole high-passes (HPF; off = a 10 Hz DC blocker) and a gain rider (k = min(regen, ceiling / envelope), no
clipping), truncated toward zero. TMOD SLEW: the delay glides (two one-poles, GLID Hz, Chloaelis' tape servo law)
so the echoes bend in pitch. TMOD FADE: a second integer head at the new time, crossfaded (sin^2) over 60 ms.
FRZ / the random-freeze bit: the line stops taking input and re-records its own output (the loop holds exactly).
The three builds differ only in the constants YN, XN, XRING.
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, snap_close  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = 44100
S = 16384.0                      # A-units: a delay held as samples/16384 in Q23 (512 LSB a sample)
T_MIN, T_MAX = 0.030, 0.370      # TIME: the left delay, seconds (log)
F_MIN, F_MAX = 300.0, 18000.0    # TONE: feedback lowpass (127 = open)
HP_REF, HP_REFV, HP_OCT = 320.0, 90, 22   # HPF knob v: 320 Hz x 2^((v - 90)/22) (0 = off)
DC_HZ = 10.0                     # with HPF off the first high-pass is a 10 Hz DC blocker
GL_REF, GL_OCT = 1.6, 24         # GLID: 1.6 Hz x 2^((v - 64)/24) (Chloaelis' tape servo: two one-poles at 1.6 Hz)
FADE = 0.060
C_SIG = 0.58                     # the rider's ceiling on the regenerated signal (signal units, -4.7 dBFS); x 1.2075 (the
                                 # most a pass can grow inside one call) = 0.70
REL_S = 0.30                     # its envelope's release, seconds
MARGIN = 8
FB_KNEE = 115                    # FDBK: loop gain 0.99 v/115 up to 115, then up to 1.2075 at 127
G_KNEE, G_TOP = 0.99, 1.2075     # 1.2075 = Chloaelis' 1.05 x regen 1.15


def hp_onepole_factor():
    """Two one-pole high-passes at f1 = K f match the -3 dB point of the plugin's RBJ high-pass (Q 0.6) at f."""
    Q = 0.6
    x = (1 / Q ** 2 - 2)        # |H|^2 = w^4 / ((1 - w^2)^2 + w^2/Q^2) = 1/2  ->  w^4 - 2 (1 - ...)...
    # solve numerically
    lo, hi = 0.5, 4.0
    for _ in range(80):
        w = (lo + hi) / 2
        h2 = w ** 4 / ((1 - w * w) ** 2 + (w / Q) ** 2)
        lo, hi = (w, hi) if h2 < 0.5 else (lo, w)
    wb = (lo + hi) / 2          # biquad -3 dB at wb x f0
    wc = math.sqrt(1 / (math.sqrt(2) - 1))   # cascade of two one-poles: -3 dB at wc x f1  (w^2/(1+w^2) = 1/sqrt 2)
    del x
    return wb / wc


HPK = hp_onepole_factor()
HP_MAX = HP_REF * 2 ** ((127 - HP_REFV) / HP_OCT)     # the HPF knob's top, Hz (before x HPK)
GL_MAX = GL_REF * 2 ** ((127 - 64) / GL_OCT)


def coef(hz, n=1):
    return 1 - math.exp(-2 * math.pi * hz * n / FS)


def fb_gain(v):
    """FDBK knob -> small-signal loop gain (Chloaelis: 1.05 x regen)."""
    if v <= FB_KNEE:
        return G_KNEE * v / FB_KNEE
    return G_KNEE + (G_TOP - G_KNEE) * (v - FB_KNEE) / (127 - FB_KNEE)


def t_left(v):
    return T_MAX * (T_MAX / T_MIN) ** (-(127 - v) / 127)


def skew_ratio(v):
    return 2 ** ((v - 64) / 64)


def tone_hz(v):
    return None if v >= 127 else F_MAX * (F_MAX / F_MIN) ** (-(127 - v) / 127)


def hpf_hz(v):
    return None if v == 0 else HP_REF * 2 ** ((v - HP_REFV) / HP_OCT)


def glide_hz(v):
    return GL_REF * 2 ** ((v - 64) / GL_OCT)


# ---- r7 map. Per-sample words below $40 (short displacements); block-only words at r3 = r7 + $40 ----
MIXT, MIXC, WIDT, WIDC, FX, GOLD, GNEW, TCOEF, H1C, H2C, FROZ, U, T = range(13)
SIDE = (0x10, 0x28)
(DH, DL, IH, IL, NBN, TLH, TLL, H1H, H1L, H2H, H2L, PK, KC, KD, SN1, SU, ST, YS) = range(18)
# r3 block
YBASE, YPTR, CLR, XPTR, FADING, RG, GLC, MODE, TMP = range(9)
TGT = (0x09, 0x0a)
BSIDE = (0x10, 0x18)
S1H, S1L, S2H, S2L, NBA, ENV, HLD = range(7)
STATE_WORDS = 0x60
ONE = '#>$7fffff'


def sl(n):
    assert n < 0x40
    return f'x:(r7+${n:02x})'


def sd(c, k):
    return sl(SIDE[c] + k)


def bx(n):
    return f'x:(r3+${n:02x})'


def bd(c, k):
    return bx(BSIDE[c] + k)


def lit(v):
    return f'#>${v & 0xffffff:06x}'


def exp_sub(label, endlabel):
    """b = 2^-y for a = y/32 (Q23, y in [0,32)); clobbers a b x0 x1 y0 (the old ECHO's, relative error < 1e-5)."""
    return [
        f'{label}:',
        ins('move', 'a1', 'b'), ins('asr', '#$12', 'a', 'a'), ins('add', '#>$1', 'a'), ins('move', 'a1', 'x1'),
        ins('and', '#>$03ffff', 'b'), ins('asl', '#$5', 'b', 'b'), ins('move', 'b1', 'x0'),
        ins('move', q(math.log(2) / 2), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('move', 'b', 'y0'),
        ins('move', q(1 / 240), 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('neg', 'b'), ins('add', q(1 / 48), 'b'),
        ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(1 / 12), 'b'),
        ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.25), 'b'),
        ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('sub', q(0.5), 'b'),
        ins('move', 'b', 'x0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', q(0.5), 'b'),
        ins('move', 'b', 'x0'), ins('mpy', 'x0', 'x0', 'b'), ins('asl', '#$2', 'b', 'b'),
        ins('do', 'x1', f'>{endlabel}'), ins('asr', 'b'), f'{endlabel}:', ins('asl', 'b'), ins('rts')]


def word_down(const):
    """a = a knob word (v/128 in bits 22..16) -> a = max(0, 127/128 - word) * const."""
    return [ins('move', 'a', 'x0'), ins('move', '#>$7f0000', 'a'), ins('move', '#>$0', 'x1'), ins('sub', 'x0', 'a'),
            ins('tlt', 'x1', 'a'), ins('move', 'a', 'x0'), ins('move', q(const), 'y0'), ins('mpy', 'y0', 'x0', 'a')]


def coef_from(k1):
    """b = f/fmax (from exp) -> a = 1 - e^(-2 pi f n/fs), k1 = 2 pi fmax n / fs / ln2 / 32."""
    assert k1 < 1
    return [ins('move', 'b', 'x0'), ins('move', q(k1), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('bsr', '>e2exp'),
            ins('move', 'b', 'x0'), ins('move', ONE, 'a'), ins('sub', 'x0', 'a')]


def tozero(label):
    return [ins('move', '#>$0', 'y1'), ins('move', '#>$ffffff', 'y0'), ins('tst', 'a'),
            ins('bge', label), ins('add', 'y', 'a'), f'{label}:']


def extract(c, nreg=None):
    """a = D (48 bit, A-units) -> U = u (Q23), T = 2 u (1 - u); N - 1 (N = whole samples) to SN1 or to nreg."""
    return [ins('move', 'a1', 'b'), ins('move', 'a0', 'b0'), ins('and', '#>$0001ff', 'b'), ins('asl', '#$e', 'b', 'b'),
            ins('move', 'b1', 'x0'), ins('move', 'x0', sl(U)),
            ins('asr', '#$9', 'a', 'a'), ins('sub', '#>$1', 'a'), ins('move', 'a1', nreg or sd(c, SN1)),
            ins('move', ONE, 'b'), ins('sub', 'x0', 'b'), ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x0', 'b'),
            ins('asl', 'b'), ins('move', 'b', sl(T))]


def hermite(mem, rd, uslot, tslot):
    """Catmull-Rom at delay N + u from four reads (delays N-1, N, N+1, N+2, going back):
    y = p0 + u (p1 - p0) + (u (1-u)/2) [(1-u) (2 p0 - p1 - pm1) + u (2 p1 - p0 - p2)]   -> b (exactly p0 at u = 0)."""
    return [ins('move', f'{mem}:({rd})-', 'x0'), ins('move', f'{mem}:({rd})-', 'x1'),
            ins('move', f'{mem}:({rd})-', 'y0'), ins('move', f'{mem}:({rd})-', 'y1'),
            ins('move', 'x1', 'a'), ins('asl', 'a'), ins('sub', 'y0', 'a'), ins('sub', 'x0', 'a'), ins('asr', '#$2', 'a', 'a'),
            ins('move', 'y0', 'b'), ins('asl', 'b'), ins('sub', 'x1', 'b'), ins('sub', 'y1', 'b'), ins('asr', '#$2', 'b', 'b'),
            ins('move', 'a', 'x0', c='A/4'), ins('move', 'b', 'y1', c='B/4'),
            ins('move', 'y0', 'a'), ins('sub', 'x1', 'a'), ins('asr', 'a'), ins('move', 'a', 'y0', c='(p1 - p0)/2'),
            ins('move', 'x1', 'b'), ins('move', uslot, 'x1'),
            ins('mac', 'x1', 'y0', 'b'), ins('mac', 'x1', 'y0', 'b', c='p0 + u (p1 - p0)'),
            ins('move', 'x0', 'a'), ins('mac', '-x1', 'x0', 'a'), ins('mac', 'y1', 'x1', 'a', c='((1-u) A + u B)/4'),
            ins('move', 'a', 'x0'), ins('move', tslot, 'y0'), ins('mac', 'y0', 'x0', 'b', c='+ 2u(1-u) x that')]


def onepole_hp(st_hi, st_lo, cslot):
    """x0 = in -> a = in - lp (lp += c (in - lp), 48-bit state; the output uses lp's high word). Clobbers a x1 y1."""
    return [ins('move', st_hi, 'a'), ins('move', st_lo, 'a0'), ins('move', cslot, 'y1'), ins('mac', 'x0', 'y1', 'a'),
            ins('move', st_hi, 'x1'), ins('mac', '-y1', 'x1', 'a'), ins('move', 'a1', st_hi), ins('move', 'a0', st_lo),
            ins('move', 'a1', 'x1'), ins('move', 'x0', 'a'), ins('sub', 'x1', 'a')]


def build(yn=16384, xn=16384, xring=0x4000, out='echo2.asm'):
    YM, XM = yn - 1, xn - 1
    DM = min(yn, xn) - MARGIN
    dmax_a = DM * 512
    ycalls, xcalls = math.ceil(yn / 128), math.ceil(xn / 128)
    assert ycalls <= 128 and xcalls <= 128 and xring % 0x4000 == 0
    inc = round((1 << 23) / (FADE * FS))
    c_time = (128 / 127) * math.log2(T_MAX / T_MIN) / 32
    c_tone = (128 / 127) * math.log2(F_MAX / F_MIN) / 32
    k_tone = 2 * math.pi * F_MAX / FS / math.log(2) / 32
    c_hp = (128 / HP_OCT) / 32
    k_hp = 2 * math.pi * HP_MAX * HPK / FS / math.log(2) / 32
    c_gl = (128 / GL_OCT) / 32
    k_gl = 2 * math.pi * GL_MAX * 16 / FS / math.log(2) / 32
    rmax2 = 2 ** (63 / 64) / 2
    n_ = 1.5706268 - 0.6432292 + 0.0727102
    c1, c3, c5 = 1.5706268 / n_ / 2, 0.6432292 / n_ / 2, 0.0727102 / n_ / 2
    ch = C_SIG / 2 / 2           # (ceiling in line units) / 2
    rel = math.exp(-16 / (REL_S * FS))
    a1 = G_KNEE / FB_KNEE * 128 / 2
    a2 = (G_TOP - G_KNEE) / (127 - FB_KNEE) * 128 / 2
    MEM = (('y', 'r5', 'n5', 'r1'), ('x', 'r4', 'n4', 'r2'))

    H = f'''; CYCLES_FORWARD_BRANCHES
; AERIA ECHO (2nd version) -- Chloaelis' dual delay (DSP.h DelayCell), hard panned, dual mono. Generated by echo2/gen.py.
; left in -> left line (Y ring, {yn} words at the buffer base) -> left out; right in -> right line (X ring, {xn}
; words at X:${xring:04x}) -> right out. Lines hold (in + regen)/2. Delays {DM} samples at most ({DM / FS * 1000:.1f} ms).
;  knob 0 TIME  left delay {T_MIN * 1000:.0f} .. {T_MAX * 1000:.0f} ms (log)
;  knob 1 FDBK  loop gain 0.99 v/115 (v <= 115), then up to {G_TOP} at 127 (self-oscillation, held by the rider)
;  knob 2 TONE  feedback lowpass {F_MIN:.0f} Hz x 60^(v/127); 127 = open
;  knob 3 SKEW  right delay = left x 2^((v - 64)/64), held to the ring
;  knob 4 SPRD  127 = hard panned (the plugin), 0 = both sides summed
;  knob 5 MIX   0 = exact dry
;  FUNC r6+$c hi HPF  0 off (10 Hz DC blocker), else two one-pole high-passes at {HPK:.4f} x {HP_REF:.0f} Hz x 2^((v-{HP_REFV})/{HP_OCT})
;       r6+$c lo FRZ  0 / 1 hold;  r6+$d bit 0: the ColdFire's random freeze (set = hold)
;       r6+$d hi RFRZ (ColdFire only)   r6+$d lo TMOD 0 SLEW / 1 FADE   r6+$e hi GLID {GL_REF} Hz x 2^((v-64)/{GL_OCT})
; Rider: env = peak of the filtered read (block peak, release {REL_S} s); k = min(regen, {C_SIG / 2:.2f} / env) (line units),
; ramped over each call. Regen is truncated toward zero (decays to exact silence below unity).
'''
    L = [H]
    # ================================================================ init
    L += ['init:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm3'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
          ins('do', f'#{STATE_WORDS}', '>e2izero'), ins('move', 'a', 'x:(r4)+'), 'e2izero:', ins('nop'),
          ins('move', 'r7', 'x0'), ins('move', '#>$40', 'a'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r3'),
          ins('move', 'x:>$213', 'r4'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', bx(YBASE)),
          ins('move', 'a', bx(YPTR)), ins('move', lit(xring), 'a'), ins('move', 'a', bx(XPTR)),
          ins('move', '#>$80', 'a'), ins('move', 'a', bx(CLR)), ins('rts')]

    # ================================================================ per call: knobs
    L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$ffffff', 'm3'), ins('move', '#>$1', 'n0'),
          ins('move', 'r7', 'x0'), ins('move', '#>$40', 'a'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r3'),
          '; FDBK -> RG = k/2 at most',
          ins('move', 'x:(r6+$1)', 'x0'), ins('move', q(a1), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', 'x0', 'b'), ins('sub', q(FB_KNEE / 128), 'b'), ins('move', '#>$0', 'x1'), ins('tlt', 'x1', 'b'),
          ins('move', 'b', 'x0'), ins('move', q(a2), 'y0'), ins('mac', 'y0', 'x0', 'a'), ins('move', 'a', bx(RG)),
          '; TONE (127 = open)',
          ins('move', 'x:(r6+$2)', 'a')] + word_down(c_tone) + [ins('bsr', '>e2exp')] + coef_from(k_tone) + [
          ins('move', 'x:(r6+$2)', 'b'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'b'), ins('blt', 'e2tset'),
          ins('move', ONE, 'a'), 'e2tset:', ins('move', 'a', sl(TCOEF)),
          '; HPF (FUNC r6+$c bits 22..16): 0 = a 10 Hz DC blocker alone',
          ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$7f0000', 'a'), ins('tst', 'a'), ins('bne', 'e2hpon'),
          ins('move', q(coef(DC_HZ)), 'x0'), ins('move', 'x0', sl(H1C)), ins('clr', 'a'), ins('move', 'a', sl(H2C))]
    for c in (0, 1):
        L += [ins('move', 'a', sd(c, H2H)), ins('move', 'a', sd(c, H2L))]
    L += [ins('bra', 'e2hpdone'), 'e2hpon:'] + word_down(c_hp) + [ins('bsr', '>e2exp')] + coef_from(k_hp) + [
          ins('move', 'a', sl(H1C)), ins('move', 'a', sl(H2C)), 'e2hpdone:',
          '; FRZ (r6+$c bits 14..8) or the random-freeze bit (r6+$d bit 0)',
          ins('move', 'x:(r6+$d)', 'b'), ins('and', '#>$000001', 'b'), ins('move', 'b1', 'x0'),
          ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$007f00', 'a'), ins('or', 'x0', 'a'), ins('move', 'a1', sl(FROZ)),
          '; TMOD (r6+$d bits 14..8): 0 SLEW, else FADE',
          ins('move', 'x:(r6+$d)', 'a'), ins('and', '#>$007f00', 'a'), ins('move', 'a1', bx(MODE)),
          '; GLID (r6+$e bits 22..16): the glide one-poles\' coefficient a call',
          ins('move', 'x:(r6+$e)', 'a'), ins('and', '#>$7f0000', 'a')] + word_down(c_gl) + [ins('bsr', '>e2exp')] + \
        coef_from(k_gl) + [ins('move', 'a', bx(GLC)),
          '; TIME -> left target, SKEW -> right target (A-units, held to the rings)',
          ins('move', 'x:(r6)', 'a')] + word_down(c_time) + [ins('bsr', '>e2exp'),
          ins('move', 'b', 'x0'), ins('move', q(T_MAX * FS / S), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
          ins('move', lit(dmax_a), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
          ins('add', '#>$100', 'a'), ins('and', '#>$fffe00', 'a', c='whole samples'), ins('move', 'a', bx(TGT[0])),
          ins('move', 'x:(r6+$3)', 'a')] + word_down(c_gl * GL_OCT / 64) + [ins('bsr', '>e2exp'),
          ins('move', 'b', 'x0'), ins('move', q(rmax2), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'),
          ins('move', bx(TGT[0]), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', 'a'),
          ins('move', lit(dmax_a), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
          ins('add', '#>$100', 'a'), ins('and', '#>$fffe00', 'a'), ins('move', 'a', bx(TGT[1])),
          '; MIX, SPRD (cross = 1 - width)',
          ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'), ins('blt', 'e2mset'),
          ins('move', ONE, 'a'), 'e2mset:', ins('move', 'a', sl(MIXT)),
          ins('move', 'x:(r6+$4)', 'a')] + word_down(64 / 127) + [ins('asl', 'a'), ins('move', 'a', sl(WIDT))]
    L += ['; SPRD glides once a call (2^-3)'] + smooth(WIDT, WIDC, 3)
    L += snap_close(MIXT, MIXC, 'e2msnap') + snap_close(WIDT, WIDC, 'e2wsnap')

    # ---------------------------------------------------------------- clearing (128 dry calls)
    L += ['; bounded clearing: 128 dry calls, 128 words of each ring a call; the heads snap to their targets',
          ins('move', bx(CLR), 'a'), ins('tst', 'a'), ins('beq', 'e2ready'), ins('sub', '#>$1', 'a'), ins('move', 'a', bx(CLR))]
    for c in (0, 1):
        L += [ins('move', bx(TGT[c]), 'a'), ins('add', '#>$100', 'a'), ins('and', '#>$fffe00', 'a'),
              ins('move', 'a', sd(c, DH)), ins('move', 'a', bd(c, S1H)), ins('move', 'a', bd(c, S2H)), ins('clr', 'a'),
              ins('move', 'a', sd(c, DL)), ins('move', 'a', bd(c, S1L)), ins('move', 'a', bd(c, S2L)),
              ins('move', 'a', sd(c, IH)), ins('move', 'a', sd(c, IL)), ins('move', 'a', bd(c, ENV)), ins('move', 'a', bd(c, HLD)),
              ins('move', 'a', sd(c, PK)), ins('move', 'a', sd(c, KD)), ins('move', bx(RG), 'a'), ins('move', 'a', sd(c, KC))]
    L += [ins('clr', 'a'), ins('move', 'a', bx(FADING)), ins('move', 'a', sl(FX)),
          ins('move', sl(WIDT), 'a'), ins('move', 'a', sl(WIDC)),
          ins('move', bx(CLR), 'a'), ins('cmp', lit(ycalls), 'a'), ins('bge', 'e2noclry'),
          ins('asl', '#$7', 'a', 'a'), ins('move', bx(YBASE), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'),
          ins('move', lit(YM), 'm4'), ins('clr', 'a'), ins('do', '#128', '>e2clry'),
          ins('move', 'a', 'y:(r4)+'), 'e2clry:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
          'e2noclry:',
          ins('move', bx(CLR), 'a'), ins('cmp', lit(xcalls), 'a'), ins('bge', 'e2noclrx'),
          ins('asl', '#$7', 'a', 'a'), ins('add', lit(xring), 'a'), ins('move', 'a', 'r4'),
          ins('move', lit(XM), 'm4'), ins('clr', 'a'), ins('do', '#128', '>e2clrx'),
          ins('move', 'a', 'x:(r4)+'), 'e2clrx:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
          'e2noclrx:', ins('rts')]

    # ---------------------------------------------------------------- the rider (both sides, a call)
    L += ['e2ready:']
    for c in (0, 1):
        L += [f'; rider {"LR"[c]}: env = the block peak when higher (held one loop period), else released;',
              f';   k/2 = min(RG, ({C_SIG / 2:.2f}/2)/env); ramped over the call',
              ins('move', sd(c, PK), 'a'), ins('move', bd(c, ENV), 'x0'), ins('cmp', 'x0', 'a'), ins('blt', f'e2nopk{c}'),
              ins('move', 'a', bd(c, ENV)), ins('move', sd(c, DH), 'b'), ins('asr', '#$d', 'b', 'b'), ins('add', '#>$2', 'b'),
              ins('move', 'b1', bd(c, HLD), c='hold = the delay in calls + 2'), ins('bra', f'e2envd{c}'),
              f'e2nopk{c}:', ins('move', bd(c, HLD), 'b'), ins('tst', 'b'), ins('ble', f'e2rel{c}'),
              ins('sub', '#>$1', 'b'), ins('move', 'b', bd(c, HLD)), ins('bra', f'e2envd{c}'),
              f'e2rel{c}:', ins('move', bd(c, ENV), 'x0'), ins('move', q(rel), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
              ins('move', sd(c, PK), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'), ins('move', 'a', bd(c, ENV)),
              f'e2envd{c}:', ins('move', bd(c, ENV), 'a'), ins('clr', 'b'), ins('move', 'b', sd(c, PK)),
              ins('move', bx(RG), 'b'), ins('move', q(ch), 'x0'), ins('cmp', 'x0', 'a'), ins('ble', f'e2kset{c}'),
              ins('move', 'a', 'x0'), ins('move', q(ch), 'a'), ins('bsr', '>e2div'),
              ins('move', bx(RG), 'x0'), ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b'),
              f'e2kset{c}:', ins('move', sd(c, KC), 'x0'), ins('sub', 'x0', 'b'), ins('asr', '#$4', 'b', 'b'),
              ins('move', 'b', sd(c, KD))]

    # ---------------------------------------------------------------- the heads (a call)
    L += ['; ---- heads ----', ins('move', bx(FADING), 'a'), ins('tst', 'a'), ins('beq', 'e2nofade'),
          ins('move', sl(FX), 'a'), ins('cmp', ONE, 'a'), ins('blt', 'e2fstat'),
          '; fade complete: the new head becomes the current one']
    for c in (0, 1):
        L += [ins('move', bd(c, NBA), 'a'), ins('move', 'a', sd(c, DH)), ins('move', 'a', bd(c, S1H)),
              ins('move', 'a', bd(c, S2H)), ins('clr', 'a'), ins('move', 'a', sd(c, DL)), ins('move', 'a', bd(c, S1L)),
              ins('move', 'a', bd(c, S2L))]
    L += [ins('clr', 'a'), ins('move', 'a', bx(FADING)),
          'e2nofade:', ins('move', sl(FROZ), 'a'), ins('tst', 'a'), ins('bne', 'e2still'),
          ins('move', bx(MODE), 'a'), ins('tst', 'a'), ins('beq', 'e2slew'),
          '; FADE mode, idle: the glides sit on the head; a target more than 1 % + 4 samples away starts a fade']
    for c in (0, 1):
        L += [ins('move', sd(c, DH), 'a'), ins('move', 'a', bd(c, S1H)), ins('move', 'a', bd(c, S2H)),
              ins('move', sd(c, DL), 'a'), ins('move', 'a', bd(c, S1L)), ins('move', 'a', bd(c, S2L))]
    for c in (0, 1):
        L += [ins('move', bx(TGT[c]), 'a'), ins('move', sd(c, DH), 'x0'), ins('sub', 'x0', 'a'), ins('abs', 'a'),
              ins('move', q(0.01), 'y0'), ins('mpy', 'y0', 'x0', 'b'), ins('add', '#>$000800', 'b'),
              ins('cmp', 'b', 'a'), ins('bgt', 'e2fstart')]
    L += [ins('bra', 'e2still'), 'e2fstart:']
    for c in (0, 1):
        L += [ins('move', bx(TGT[c]), 'a'), ins('add', '#>$100', 'a'), ins('and', '#>$fffe00', 'a'),
              ins('move', 'a', bd(c, NBA)), ins('asr', '#$9', 'a', 'a'), ins('neg', 'a'), ins('move', 'a1', sd(c, NBN))]
    L += [ins('clr', 'a'), ins('move', 'a', sl(FX)), ins('move', '#>$1', 'a'), ins('move', 'a', bx(FADING)),
          ins('bra', 'e2fstat'),
          'e2slew:', '; SLEW: two one-poles (48 bit) toward the target; the head ramps to the second over the call']
    for c in (0, 1):
        L += [ins('move', bx(GLC), 'y0'),
              ins('move', bx(TGT[c]), 'a'), ins('move', bd(c, S1H), 'b'), ins('move', bd(c, S1L), 'b0'), ins('sub', 'b', 'a'),
              ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', 'a', 'b'),
              ins('move', 'b1', bd(c, S1H)), ins('move', 'b0', bd(c, S1L)),
              ins('move', bd(c, S2H), 'a'), ins('move', bd(c, S2L), 'a0'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
              ins('mpy', 'y0', 'x0', 'b'), ins('add', 'b', 'a'), ins('move', 'a1', bd(c, S2H)), ins('move', 'a0', bd(c, S2L)),
              '; within 2/512 sample of the target: land on it',
              ins('move', bx(TGT[c]), 'b'), ins('sub', 'a', 'b'), ins('abs', 'b'), ins('cmp', '#>$000002', 'b'),
              ins('bge', f'e2gs{c}'), ins('move', bx(TGT[c]), 'a'), ins('move', 'a', bd(c, S1H)), ins('move', 'a', bd(c, S2H)),
              ins('clr', 'a'), ins('move', 'a', bd(c, S1L)), ins('move', 'a', bd(c, S2L)), f'e2gs{c}:',
              '; inc = (S2 - D)/16; within 2/512 sample: the head lands on S2',
              ins('move', bd(c, S2H), 'a'), ins('move', bd(c, S2L), 'a0'), ins('move', sd(c, DH), 'b'),
              ins('move', sd(c, DL), 'b0'), ins('sub', 'b', 'a'), ins('move', 'a1', 'b'), ins('move', 'a0', 'b0'),
              ins('abs', 'b'), ins('cmp', '#>$000002', 'b'), ins('bge', f'e2gi{c}'),
              ins('move', bd(c, S2H), 'x0'), ins('move', 'x0', sd(c, DH)), ins('move', bd(c, S2L), 'x0'),
              ins('move', 'x0', sd(c, DL)), ins('clr', 'a'), f'e2gi{c}:',
              ins('asr', '#$4', 'a', 'a'), ins('move', 'a1', sd(c, IH)), ins('move', 'a0', sd(c, IL))]
    L += [ins('bra', 'e2mrun'), 'e2still:', '; frozen, or FADE idle: the head stands still']
    L += [ins('clr', 'a')] + [ins('move', 'a', sd(c, k)) for c in (0, 1) for k in (IH, IL)]
    L += [ins('bra', 'e2mrun'), 'e2fstat:', '; fading: the head stands still; its position once a call']
    for c in (0, 1):
        L += [ins('move', sd(c, DH), 'a'), ins('move', sd(c, DL), 'a0')] + extract(c) + [
              ins('move', sl(U), 'x0'), ins('move', 'x0', sd(c, SU)), ins('move', sl(T), 'x0'), ins('move', 'x0', sd(c, ST))]
    L += [ins('bsr', '>e2regs'), ins('bra', 'e2frun')]

    # ================================================================ per sample
    def side(c, fading, tag):
        mem, wr, n, rd = MEM[c]
        o = [f'; ======== {"left (Y ring)" if c == 0 else "right (X ring)"} ========']
        if fading:
            o += [ins('move', sd(c, SN1), n), ins('lua', f'({wr})-{n}', rd)] + hermite(mem, rd, sd(c, SU), sd(c, ST))
            o += ['; the new head (whole samples), sin^2 crossfade',
                  ins('move', sd(c, NBN), n), ins('move', f'{mem}:({wr}+{n})', 'x0'), ins('move', 'b', 'x1'),
                  ins('move', sl(GOLD), 'y0'), ins('mpy', 'x1', 'y0', 'b'), ins('move', sl(GNEW), 'y0'),
                  ins('mac', 'y0', 'x0', 'b')]
        else:
            o += [ins('move', sd(c, DH), 'a'), ins('move', sd(c, DL), 'a0'), ins('move', sd(c, IH), 'y1'),
                  ins('move', sd(c, IL), 'y0'), ins('add', 'y', 'a'), ins('move', 'a1', sd(c, DH)), ins('move', 'a0', sd(c, DL))]
            o += extract(c, n) + [ins('lua', f'({wr})-{n}', rd)] + hermite(mem, rd, sl(U), sl(T))
        o += [ins('move', 'b', 'x0'), ins('move', 'x0', sd(c, YS)), '; TONE lowpass, then the two high-passes']
        o += [ins('move', sd(c, TLH), 'a'), ins('move', sd(c, TLL), 'a0'), ins('move', sl(TCOEF), 'y1'),
              ins('mac', 'x0', 'y1', 'a'), ins('move', sd(c, TLH), 'x1'), ins('mac', '-y1', 'x1', 'a'),
              ins('move', 'a1', sd(c, TLH)), ins('move', 'a0', sd(c, TLL)), ins('move', 'a', 'x0')]
        o += onepole_hp(sd(c, H1H), sd(c, H1L), sl(H1C)) + [ins('move', 'a', 'x0')]
        o += onepole_hp(sd(c, H2H), sd(c, H2L), sl(H2C)) + [ins('move', 'a', 'x0')]
        o += ['; rider peak; regen = k f', ins('move', 'x0', 'a'), ins('abs', 'a'), ins('move', sd(c, PK), 'x1'),
              ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a'), ins('move', 'a', sd(c, PK)),
              ins('move', sd(c, KC), 'a'), ins('move', sd(c, KD), 'x1'), ins('add', 'x1', 'a'), ins('move', 'a', sd(c, KC)),
              ins('move', 'a', 'y1'), ins('mpy', 'x0', 'y1', 'a'), ins('asl', 'a'),
              ins('move', 'x:(r0)' if c == 0 else 'x:(r0+n0)', 'b'), ins('asr', 'b'), ins('add', 'b', 'a', c='(in + regen)/2')]
        o += tozero(f'e2{tag}z{c}') + [ins('move', sl(FROZ), 'b'), ins('move', sd(c, YS), 'x1'), ins('tst', 'b'),
                                       ins('tne', 'x1', 'a', c='frozen: the loop re-records itself'),
                                       ins('move', 'a', f'{mem}:({wr})')]
        return o

    def loop(fading, tag, endlabel):
        o = [ins('do', 'n7', f'>{endlabel}')]
        o += smooth(MIXT, MIXC, 7)
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
        o += ['; ---- spread, mix, advance (wet = 2 x the line) ----',
              ins('move', sd(1, YS), 'a'), ins('move', sd(0, YS), 'x0'), ins('sub', 'x0', 'a'), ins('asr', 'a'),
              ins('move', 'a', 'x1'), ins('move', sl(WIDC), 'y0'), ins('mpy', 'x1', 'y0', 'a'),
              ins('move', 'a', 'x1', c='cross/2'),
              ins('move', 'x0', 'a'), ins('add', 'x1', 'a'), ins('asl', 'a')] + mix(mixslot=MIXC) + [
              ins('move', sd(1, YS), 'a'), ins('sub', 'x1', 'a'), ins('asl', 'a')] + mix(mixslot=MIXC) + [
              ins('move', '(r5)+'), ins('move', '(r4)+')]
        o += [f'{endlabel}:', ins('nop')]
        return o

    L += ['e2mrun:', ins('bsr', '>e2regs')] + loop(False, 'p', 'e2plend') + [ins('bra', 'e2exit'), 'e2frun:'] + \
        loop(True, 'f', 'e2fdend')
    L += ['e2exit:', ins('move', 'r5', bx(YPTR)), ins('move', 'r4', bx(XPTR))]
    L += [ins('move', '#>$ffffff', m) for m in ('m1', 'm2', 'm4', 'm5')] + [ins('rts')]
    L += ['; ---- subroutines (after their callers) ----',
          'e2regs:', ins('move', lit(YM), 'm5'), ins('move', lit(YM), 'm1'), ins('move', lit(XM), 'm4'),
          ins('move', lit(XM), 'm2'), ins('move', bx(YPTR), 'r5'), ins('move', bx(XPTR), 'r4'), ins('rts'),
          'e2div:', '; a1 / x0 (0 <= a1 < x0) -> b, 24 div steps',
          ins('move', 'a1', 'a'), ins('andi', '#$fe', 'ccr'),
          ins('do', '#24', '>e2dvend'), ins('div', 'x0', 'a'), 'e2dvend:', ins('nop'), ins('move', 'a0', 'b'), ins('rts')]
    L += exp_sub('e2exp', 'e2xpend')
    (HERE / out).write_text(finish(L))
    return dict(out=out, yn=yn, xn=xn, xring=xring, dmax=DM)


TEST_XRING = 0x10000
BUILDS = {
    'echo2.asm': dict(yn=16384, xn=16384, xring=0x4000),
    'echo2_test.asm': dict(yn=15870, xn=16384, xring=TEST_XRING),
    'echo2_fullx.asm': dict(yn=16384, xn=16384, xring=TEST_XRING),
}

if __name__ == '__main__':
    todo = list(BUILDS) if '--all' in sys.argv else (['echo2_test.asm', 'echo2_fullx.asm'] if '--test' in sys.argv
                                                     else ['echo2.asm'])
    for name in todo:
        print('wrote', build(out=name, **BUILDS[name]))
