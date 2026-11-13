#!/usr/bin/env python3
"""Generates grains.asm (AERIA GRAINS: live granular on the FX2 ring, 4 grains)."""
import math
import os
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, snap_close  # noqa: E402

# ---- build parameters (environment) ----------------------------------------
#   RING=long (default)  35,280 ring words (800 ms)  -> grains.asm
#   RING=test            15,870 ring words (360 ms)  -> grains_test.asm
#   AGE=48 (default)     the current engine, the same code for both rings:
#                        - the ring is wrapped IN CODE (head index compare, one
#                          mirror word), no AGU modulo: a DSP56300 modifier
#                          register only does linear modulo up to $7fff (32,768
#                          words; $8000.. is reserved / multiple-wrap-around),
#                          so m = $89cf cannot address a 35,280-word ring. Any
#                          base works (no alignment rule).
#                        - grain ages and rates are 48 bits (hi word age/65536
#                          in Q23 = 128 LSB per sample, + a low word)
#   AGE=24               the original engine (24-bit ages in 1/16384 units,
#                        AGU modulo m = N - 1); RING=test only. RING=test AGE=24
#                        WINFIX=0 regenerates grains.asm.handpatched byte for byte.
#   WINFIX=1 (default)   window-table fraction = the 17 fraction bits of the phase.
#                        WINFIX=0 (AGE=24 only) keeps the hand patch's
#                        asl #6 / lsr #1, which leaves the index LSB in the
#                        fraction: every odd table segment reads one whole
#                        table step ahead (see README).
#   SNAP=1 (default)     a grain's start age is held to whole samples (and its low word
#                        cleared): at an integer rate (unison, unison reverse) every grain
#                        reads the ring exactly instead of a random fixed fraction (whose
#                        linear interpolation cost -2.3 dB at 10-15 kHz, -1.8 dB on white
#                        noise). SNAP=0 is the build before (the gate's wrap-seam checks use
#                        it: they need a fractional age to read the mirror word).
#   DFULL=1 (default)    DENS t = (3/4) DENS 128/127, so DENS 127 is exactly four full grains
#                        (the display law 1 + 3 v/127). Before, grain 3 sat at 0.977 there: a
#                        +-0.58 % level ripple at the grain rate. DFULL=0: the law before (AGE=24 too).
#   OUT=path             write somewhere else
RING = os.environ.get('RING', 'long')
AGE = int(os.environ.get('AGE', '48'))
WINFIX = int(os.environ.get('WINFIX', '1'))
SNAP = int(os.environ.get('SNAP', '1'))     # 1 (default): grain starts on whole samples (quality pass, 12 Nov 2026); 0: as before
#   AA=1 (default)       the anti-alias low-pass on the recording (13 Nov 2026, "stop aliasing"; aa_design.py):
#                        pitched-up grains read the ring 2^(PTCH/12) x as fast, so what sat above fs / (2 ratio)
#                        folded back down. The ring now records y/2 = H (L + R)/4, H an order-3 elliptic-type
#                        low-pass tracking PTCH (none at or under unison: (L + R)/4 exactly); the output gain is
#                        x 16 instead of x 8. The mix and norm glides become per-sample ramps set once a block
#                        (the same rates, 3.4 instructions a sample cheaper). AA=0: the build before. AGE=48 only.
AA = int(os.environ.get('AA', '1'))
assert RING in ('long', 'test') and AGE in (24, 48)
N = {'long': 35280, 'test': 15870}[RING]   # ring words
P48 = AGE == 48
assert P48 or RING == 'test'
assert WINFIX or not P48
DFULL = int(os.environ.get('DFULL', '1')) and P48
AA = AA and P48
if AA:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import aa_design as AD  # noqa: E402
# the low-pass's words at r1 = r7 + $40 (r7 displacements stop at 63; r7 + $40..$4c is zeroed by init)
AW = dict(B=0, A1H=1, A2=2, CH=3, P=4, K=5, W1=6, W2=7, V1=8, SC=9, S32=10, MR=11, NR=12)


def ax(name):
    return f'x:(r1+${AW[name]:x})'


def h24(v):
    return f'#>${v & 0xffffff:06x}'


def aa_update():
    """subroutine grfset: a = s32 (PTCH semitones / 32) in and out. Only when it changed: at or under unison
    B = 1/4, the rest 0; above, A1H A2 CH P = degree-4 polynomials in t (aa_design.POLY), K = 1,
    B = (1 - 2 A1H - A2)(1 - P) / (16 (1 + CH)): y/2 at unity DC gain."""
    out = ['; ---- the anti-alias low-pass coefficients for PTCH (only when it changed; aa_design.py) ----',
           'grfset:', ins('move', 'a', ax('S32')), ins('tst', 'a'), ins('bgt', 'grfup'),
           ins('move', '#>$800000', 'x0'), ins('move', ax('SC'), 'b'), ins('cmp', 'x0', 'b'), ins('beq', 'grfout'),
           ins('move', 'x0', ax('SC')), ins('move', '#>$200000', 'x0'), ins('move', 'x0', ax('B')), ins('clr', 'b')]
    out += [ins('move', 'b', ax(nm)) for nm in ('A1H', 'A2', 'CH', 'P', 'K')]
    out += ['grfout:', ins('move', ax('S32'), 'a'), ins('rts'),
            'grfup:', ins('move', h24(AD.q23(AD.SMIN / 32)), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'),
            ins('move', h24(AD.q23(AD.SMAX / 32)), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
            ins('move', ax('SC'), 'x0'), ins('cmp', 'x0', 'a'), ins('bne', 'grfnew'),
            ins('move', ax('S32'), 'a'), ins('rts'),
            'grfnew:', ins('move', 'a', ax('SC')), ins('sub', h24(AD.q23(AD.UC)), 'a'), ins('move', 'a', 'x0'),
            ins('move', h24(AD.q23(1 / AD.UW / 4)), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$2', 'a', 'a'),
            ins('move', 'a', 'y1', c='t')]
    for k, nm in enumerate(('A1H', 'A2', 'CH', 'P')):
        row = AD.POLY[k]
        out += [ins('move', h24(row[AD.DEG]), 'a')]
        for i in range(AD.DEG - 1, -1, -1):
            out += [ins('move', 'a', 'x0'), ins('mpy', 'x0', 'y1', 'a'), ins('add', h24(row[i]), 'a')]
        out += [ins('asl', 'a'), ins('move', 'a', ax(nm))]
    out += [ins('move', '#>$7fffff', 'x0'), ins('move', 'x0', ax('K')),
            ins('move', q(1 / 32), 'a'), ins('move', ax('A1H'), 'x0'), ins('move', q(1 / 16), 'y0'), ins('mac', '-y0', 'x0', 'a'),
            ins('move', ax('A2'), 'x0'), ins('move', q(1 / 32), 'y0'), ins('mac', '-y0', 'x0', 'a'),
            ins('move', 'a', 'x1', c='(1 - 2 A1H - A2) / 32'), ins('move', '#>$7fffff', 'a'), ins('move', ax('P'), 'x0'),
            ins('sub', 'x0', 'a'), ins('move', 'a', 'y0', c='1 - P'), ins('mpy', 'x1', 'y0', 'a'), ins('move', 'a1', 'a'),
            ins('move', ax('CH'), 'b'), ins('asr', 'b'), ins('add', '#>$400000', 'b'), ins('move', 'b', 'x0', c='(1 + CH) / 2')]
    out += div24() + [ins('move', 'a0', ax('B')), ins('move', ax('S32'), 'a'), ins('rts')]
    return out


def aa_record():
    """(L + R) through the low-pass; a = y/2 (rounded), for the ring"""
    return ['; record through the anti-alias low-pass (aa_design.py): w = B (L + R) + 2 A1H w1 + A2 w2;',
            '; u = w + 2 CH w1 + K w2; v = u + P v1; y/2 = v + K v1 (at or under unison: (L + R)/4 exactly)',
            ins('move', ax('B'), 'y0'), ins('move', 'x:(r0)', 'x0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('move', 'x:(r0+n0)', 'x0'), ins('mac', 'y0', 'x0', 'a'),
            ins('move', ax('W1'), 'x1'), ins('move', ax('A1H'), 'y0'), ins('mac', 'x1', 'y0', 'a'), ins('mac', 'x1', 'y0', 'a'),
            ins('move', ax('W2'), 'x0'), ins('move', ax('A2'), 'y0'), ins('macr', 'y0', 'x0', 'a', c='w'),
            ins('move', 'x1', ax('W2')), ins('move', 'a', ax('W1')),
            ins('move', ax('CH'), 'y0'), ins('mac', 'x1', 'y0', 'a'), ins('mac', 'x1', 'y0', 'a'),
            ins('move', ax('K'), 'y1'), ins('mac', 'x0', 'y1', 'a', c='u'),
            ins('move', ax('V1'), 'x1'), ins('move', ax('P'), 'y0'), ins('macr', 'x1', 'y0', 'a', c='v'),
            ins('move', 'a', ax('V1')), ins('macr', 'y1', 'x1', 'a', c='y/2')]


def aa_flush():
    """a rounding limit cycle (a few LSB) cleared; looked at every 8th block of a live head (index bits 4-6)"""
    return ['; anti-alias low-pass: a rounding limit cycle (a few LSB) is cleared (looked for every 8th block)',
            ins('move', 'x:(r7+$01)', 'a'), ins('and', '#>$000070', 'a'), ins('bne', 'grfkeep'),
            ins('move', ax('W1'), 'a'), ins('abs', 'a'), ins('move', ax('W2'), 'b'), ins('abs', 'b'), ins('add', 'b', 'a'),
            ins('move', ax('V1'), 'b'), ins('abs', 'b'), ins('add', 'b', 'a'), ins('cmp', '#>$000030', 'a'),
            ins('bgt', 'grfkeep'), ins('clr', 'a'), ins('move', 'a', ax('W1')), ins('move', 'a', ax('W2')),
            ins('move', 'a', ax('V1')), 'grfkeep:',
            '; the mix and norm glides: per-sample ramps for this block (the old per-sample one-poles\' rates)',
            ins('move', 'x:(r7+$04)', 'a'), ins('move', 'x:(r7+$05)', 'b'), ins('sub', 'b', 'a'), ins('asr', '#$7', 'a', 'a'),
            ins('move', 'a', ax('MR')),
            ins('move', 'x:(r7+$0e)', 'a'), ins('move', 'x:(r7+$0f)', 'b'), ins('sub', 'b', 'a'), ins('asr', '#$8', 'a', 'a'),
            ins('move', 'a', ax('NR'))]   # DENS 127 = four full grains (1 + 3 v/127, the display law); 0: 3 v/128 as before
NL = N - 1 if P48 else N   # ring length in samples (AGE=48: one of the N words is the mirror)
M = 48               # margin at either end of the ring, samples
R = NL - 2 * M - 2   # usable age span
SH = 16 if P48 else 14
S = float(1 << SH)   # "A-units": ages held as age/S in Q23, 2^(23-SH) LSB per sample
assert N < S, 'ages must stay below 1.0 in A-units'
MS = 44.1
LMIN = 10 * MS                                 # shortest grain, samples
L2 = 290 * MS                                  # squared-taper span (the 10..300 ms law)
U127 = 127 / 128
LTOP = {'long': 750.0, 'test': 0.0}[RING] * MS  # grain length at SIZE 127 (0: squared taper only)
L4 = (LTOP - LMIN - L2 * U127 ** 2) / U127 ** 4 if LTOP else 0.0   # quartic term, long ring only
LMAX = LMIN + L2 + L4                          # the law at u = 1 (never reached: u <= 127/128)
assert LMAX < S and LMIN > 1
CH = 512 if P48 else 128                       # ring words cleared per proc call after init
NCH = -(-N // CH)                              # chunks (the last one wraps: modulo addressing)
NCLR = NCH + 4                                 # dry calls after init
MOD = f'#>${N - 1:04x}'
GR = 4
JIT = 5.0 * MS      # AGE=48: SPRD 127 also moves a grain's start by up to +-5 ms, with its pan
ONE = '#>$7fffff'
NMS = round(NL / MS)


def A_(samples):
    return q(samples / S)


H24 = f"""; CYCLES_FORWARD_BRANCHES
; AERIA GRAINS -- a live granular processor (Balloon Shop GrainShifter,
; scaled down to one DSP56300 budget).
;
; The input, mono (L+R)/2, is recorded into the FX2 buffer as a {N}-word
; ring (360 ms; m = $3dfd, below the Sidechain window). Four grains: each a
; linearly interpolated read head with its own age (how far back), speed,
; triangle window, pan and gain, a quarter period apart; two staggered
; triangles sum to exactly 1.
;
;  SIZE  grain length 10 .. 300 ms (squared taper), shortened per block so
;        no grain can run across the record head or the ring's end at the
;        current speed (|age rate| * len <= {R} samples)
;  DENS  grains 1 (gaps) .. 4 (dense); level normalised by 2/max(2, sum)
;  POS   where grains start: 0 = just now .. 127 = 350 ms back
;  SPRY  random start offset (up to +- half the ring) and random pan
;  PTCH  -24 .. +24 semitones (speed 2^(st/12))
;  MIX   0 = exact dry
;  FRZ   (page 2 slot 6) HOLD stops recording; grains play the frozen ring
;  REV   (page 2 slot 8) probability that a grain plays backwards
;
; Each grain latches speed/direction, length, start, pan and gain when it
; restarts (its window is 0 there), so knob moves never click a grain.
; Random: 24-bit LCG x*1664525 + $6ef35f (mod 2^24), drawn at restarts.
; A grain's age moves by adv - speed per sample (adv 1 live, 0 frozen); a
; grain already playing when FRZ flips keeps its latched rate to its end.
;
; r7: 00 base 01 ptr 02 clear 03 rnd 04/05 mix t/s 06 dage fwd 07 dage rev
;   08 inc (1/len) 09 len 0a centre 0b spread 0c rev threshold 0d adv
;   0e norm t 0f norm s 10..13 grain gains 14 pan reach 15 tmp
;   16 sum L 17 sum R 18 lo fwd 19 hi fwd 1a lo rev 1b hi rev 1c speed/8
;   1d/1e chosen lo/hi at a restart
;   20+8k grain k: +0 phase (-1..0) +1 inc +2 age +3 dage +4 gL +5 gR
;   80..c0 the grain window, 65 points (AERIA's cloud editor)
"""

H48 = f"""; CYCLES_FORWARD_BRANCHES
; AERIA GRAINS -- a live granular processor (Balloon Shop GrainShifter,
; scaled down to one DSP56300 budget).   [gen.py RING={RING} AGE=48]
;
; The input, mono (L+R)/2, is recorded into {N} words of Y: a ring of
; {NL} samples ({NMS} ms) at base+1 .. base+{NL}, plus y[base], a mirror of
; the top word, so a linear-interpolated pair never straddles the wrap.
; base = the y word at x:(x:$213), read at init. The wrap is done in code
; (head index 0..{NL - 1} in r7+$01; a read index below 0 gets + {NL}): the
; AGU's modulo stops at 32,768 words. m4/m5 stay linear, no base alignment.
; Four grains: each a linearly interpolated read head with its own age (how
; far back), speed, window (the 65-point table at r7+$80, written by the
; host), pan and gain, a quarter period apart.
;
;  SIZE  grain length {LMIN / MS:.0f} .. {(LMIN + L2 * U127 ** 2 + L4 * U127 ** 4) / MS:.0f} ms: 10 + 290 u^2{f' + {L4 / MS:.1f} u^4' if L4 else ''} ms, u = knob/128;
;        shortened per block so no grain can run across the record head or
;        the ring's end at the current speed (|age rate| * len <= {R} samples)
;  DENS  grains 1 (gaps) .. 4 (dense); level normalised by 2/max(2, sum)
;  POS   where grains start: {M} + {R} knob/128 samples back
;        (0 = {M / MS:.1f} ms .. 127 = {(M + R * U127) / MS:.0f} ms)
;  SPRY  random start offset (up to +- {R // 2} samples, half the ring)
;  PTCH  -24 .. +24 semitones (speed 2^(st/12))
;  MIX   0 = exact dry
;  FRZ   (page 2 slot 6) HOLD stops recording; grains play the frozen ring
;  REV   (page 2 slot 8) probability that a grain plays backwards
;  SPRD  (page 2 slot 10) stereo spread: each grain is placed left/right at random,
;        pan s = SPRD x rnd held to +-1/2 (gL = g (1/2 + s), gR = g (1/2 - s): the two
;        gains always sum to g, so the mono sum is the same at any SPRD). Up to 64 the
;        pans fill the field evenly; above, more and more grains sit hard left or hard
;        right. The same rnd also moves the grain's start by SPRD x {JIT / MS:.0f} ms (left = older),
;        so the sides differ in time as well as level even with no SPRY at unison.
;        0 = every grain in the middle, starts untouched (L = R). (Pan used to ride on SPRY.)
;
; Each grain latches speed/direction, length, start, pan and gain when it
; restarts (its window is 0 there), so knob moves never click a grain.
; Random: 24-bit LCG x*1664525 + $6ef35f (mod 2^24), drawn at restarts.
; A grain's age moves by adv - speed per sample (adv 1 live, 0 frozen); a
; grain already playing when FRZ flips keeps its latched rate to its end.
;
; Ages are 48-bit: the high word in A-units (age/65536 in Q23, 128 LSB per
; sample), the low word 24 more fraction bits, so the rate adv - speed keeps
; the full precision of speed (no pitch quantisation) with a ring this long.
; Start positions, lengths and bounds are 24-bit A-unit words (all < 1.0:
; ({M} + {R} + {R // 2}) / 65536 = {(M + R + R // 2) / 65536:.3f}); displacement bounds use rate/8 in Q23.
;
; r7: 00 base 01 head index 02 clear 03 rnd 04/05 mix t/s 06 dage fwd hi 07 dage rev hi
;   08 inc (1/len, rounded up) 09 len 0a centre 0b spread 0c rev threshold 0d adv (0/1)
;   0e norm t 0f norm s 10..13 grain gains 14 pan reach (SPRD) 15 block tmp (speed/8, DENS), then r7+$c0 for the loop
;   16 sum L 17 sum R (block: dage/8 fwd, rev) 18 lo fwd 19 hi fwd 1a lo rev 1b hi rev
;   1c dage fwd lo 1d/1e chosen lo/hi at a restart (block: 1d adv in A-units) 1f dage rev lo
;   20+8k grain k: +0 phase (-1..0) +1 inc +2 age hi +3 dage hi +4 gL +5 gR
;                  +6 age lo +7 dage lo
;   80..c0 the grain window, 65 points, Q23 0..$7fffff (AERIA's cloud editor)
"""
H = H48 if P48 else H24
HSH = f'#${23 - SH:x}'      # A-units -> whole samples


def div24():
    """a (dividend) / x0 -> quotient in a0; both positive, a < x0."""
    return [ins('andi', '#$fe', 'ccr')] + [ins('div', 'x0', 'a')] * 24


def lcg():
    """x:(r7+$03) <- LCG step; x0 <- the new value (as a signed fraction)."""
    return [ins('move', 'x:(r7+$03)', 'x0'), ins('move', '#>$19660d', 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('asr', 'a', c='a0 = low 24 bits of x*k'), ins('move', 'a0', 'x0'), ins('move', 'x0', 'a'),
            ins('add', '#>$6ef35f', 'a'), ins('move', 'a1', 'x:(r7+$03)'), ins('move', 'a1', 'x0')]


def clamp01(slot):
    """a (accumulator) clamped to [0, 1) and stored."""
    return [ins('move', '#>$0', 'x0'), ins('tst', 'a'), ins('tlt', 'x0', 'a'), ins('move', ONE, 'x0'),
            ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('move', 'a', f'x:(r7+${slot:02x})')]


def window():
    """a = phase (-1..0) -> b = w. The 65-point table at r7+$80 (the host writes it):
    w = t[i] + (t[i+1] - t[i]) f, 64 (phase + 1) = i + f. Clobbers a b x0 x1 y0 r4."""
    if not WINFIX:      # the hand patch, verbatim: b1 after asl #6 still has the index LSB on top
        return ['; window from the cloud (AERIA\'s grain editor): w = t[i] + (t[i+1] - t[i]) f, 64 (phase + 1) = i + f, the 65-point',
                '; table at r7+$80 (the ColdFire writes it; a triangle unless the cloud changes it)',
                ins('move', 'a', 'b'), ins('asr', '#$11', 'a', 'a'), ins('move', '#>$c0', 'x0'), ins('add', 'x0', 'a'),
                ins('move', 'r7', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'r4'),
                ins('asl', '#$6', 'b', 'b'), ins('move', 'b1', 'x1'), ins('move', 'x1', 'b'),
                ins('lsr', '#$1', 'b'), ins('move', 'b1', 'y0'),
                ins('move', 'x:(r4)+', 'x0'), ins('move', 'x:(r4)', 'b'), ins('sub', 'x0', 'b'),
                ins('move', 'b', 'x1'), ins('mpy', 'x1', 'y0', 'b'), ins('asl', 'b'), ins('add', 'x0', 'b')]
    return ['; window: w = t[i] + (t[i+1] - t[i]) f, 64 (phase + 1) = i + f, t = the 65-point table at r7+$80',
            '; (the host writes it). i = phase >> 17 = -64..-1 from r7+$c0; f = the low 17 bits, as Q23.',
            ins('move', 'a', 'b'), ins('asr', '#$11', 'a', 'a')] + ([
            ins('move', 'x:(r7+$15)', 'x0', c='r7 + $c0')] if P48 else [ins('add', '#>$c0', 'a'), ins('move', 'r7', 'x0')]) + [
            ins('add', 'x0', 'a'), ins('move', 'a1', 'r4'),
            ins('and', '#>$01ffff', 'b'), ins('asl', '#$6', 'b', 'b'), ins('move', 'b1', 'y0', c='f'),
            ins('move', 'x:(r4)+', 'x0'), ins('move', 'x:(r4)', 'b'), ins('sub', 'x0', 'b'),
            ins('move', 'b', 'x1'), ins('mpy', 'x1', 'y0', 'b'), ins('add', 'x0', 'b')]


L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#80' if AA else '#64', '>grizero'), ins('move', 'a', 'x:(r4)+'), 'grizero:', ins('nop'),
     ins('move', 'x:>$213', 'r4'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', 'x:(r7+$00)'),
     *([] if P48 else [ins('move', 'a', 'x:(r7+$01)')]), ins('move', f'#>${NCLR:02x}', 'a'), ins('move', 'a', 'x:(r7+$02)'),
     ins('move', '#>$2b5d1f', 'a'), ins('move', 'a', 'x:(r7+$03)', c='LCG seed')]
for k in range(GR):
    L += [ins('move', q(-1 + k / GR), 'a'), ins('move', 'a', f'x:(r7+${0x20 + 8 * k:02x})', c='staggered'),
          ins('move', A_(M + 1), 'a'), ins('move', 'a', f'x:(r7+${0x22 + 8 * k:02x})')]
L += [ins('rts')]

# ================================================================ per block
L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$1', 'n0')] + ([
      ins('move', '#>$ffffff', 'm1'), ins('move', 'r7', 'b'), ins('add', '#>$40', 'b'),
      ins('move', 'b', 'r1', c='r1 = r7 + $40: the anti-alias low-pass\'s words')] if AA else []) + [
      '; speed/8 = 2^(st/12)/8 by the PITCH chain (Taylor 2^(x/8), three squarings)',
      ins('move', 'x:(r6+$4)', 'a'), ins('asl', '#$2', 'a', 'a'), ins('move', q(0.75), 'x0'),
      ins('sub', 'x0', 'a')] + ([ins('bsr', '>grfset', c='the anti-alias low-pass for PTCH (a = PTCH / 32 kept)')] if AA else []) + [
      ins('move', 'a', 'x0'),
      ins('move', q(math.log(2) / 3), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'y0'),
      ins('move', q(1 / 192), 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(1 / 48), 'a'),
      ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(1 / 16), 'a'),
      ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(1 / 8), 'a'),
      ins('move', 'a', 'x0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(1 / 8), 'a')]
for _ in range(3):
    L += [ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('asl', '#$3', 'a', 'a')]
SPD = 0x15 if P48 else 0x1c
L += [ins('move', 'a', f'x:(r7+${SPD:02x})'),
      '; adv (FRZ: page-2 slot 6)',
      ins('move', 'x:(r6+$c)', 'a'), ins('and', '#>$ff0000', 'a'), ins('move', A_(1), 'b'),
      ins('tst', 'a'), ins('beq', 'grlive'), ins('clr', 'b'), 'grlive:'] + ([
      ins('move', 'b', 'x:(r7+$1d)', c='adv in A-units: 128 or 0'),
      ins('asr', HSH, 'b', 'b'), ins('move', 'b', 'x:(r7+$0d)', c='adv in samples: 1 or 0')] if P48 else [
      ins('move', 'b', 'x:(r7+$0d)', c=f'adv in A-units: {1 << (23 - SH)} or 0')])
if P48:
    L += ['; dage fwd = adv - speed, rev = adv + speed: 48 bits, hi word in A-units (the bits asr',
          '; shifts out stay in a0 and are kept as the low word)',
          ins('move', 'x:(r7+$15)', 'a'), ins('asr', f'#${SH - 3:x}', 'a', 'a', c='a1:a0 = speed/65536'),
          ins('move', 'x:(r7+$1d)', 'b'),
          ins('sub', 'a', 'b'), ins('move', 'b1', 'x:(r7+$06)'), ins('move', 'b0', 'x:(r7+$1c)'),
          ins('move', 'x:(r7+$1d)', 'b'), ins('add', 'a', 'b'),
          ins('move', 'b1', 'x:(r7+$07)'), ins('move', 'b0', 'x:(r7+$1f)'),
          '; the same rates /8 in Q23 (samples per sample / 8), for the displacement bounds',
          ins('move', 'x:(r7+$1d)', 'a'), ins('asl', f'#${SH - 3:x}', 'a', 'a', c='adv/8: 1/8 or 0'),
          ins('move', 'x:(r7+$15)', 'x0'), ins('move', 'a', 'b'),
          ins('sub', 'x0', 'a'), ins('move', 'a', 'x:(r7+$16)'),
          ins('add', 'x0', 'b'), ins('move', 'b', 'x:(r7+$17)')]
    DF, DR = 0x16, 0x17
else:
    L += ['; dage fwd = adv - speed, rev = adv + speed (A-units)',
          ins('move', 'x:(r7+$1c)', 'a'), ins('asr', '#$b', 'a', 'a', c='speed/16384'), ins('move', 'a', 'x1'),
          ins('sub', 'x1', 'b'), ins('move', 'b', 'x:(r7+$06)'),
          ins('move', 'x:(r7+$0d)', 'b'), ins('add', 'x1', 'b'), ins('move', 'b', 'x:(r7+$07)')]
    DF, DR = 0x06, 0x07
# disp (A-units) = rate word * len word << DSHL
DSHL = '#$3' if P48 else '#$e'
L += ['; len from SIZE (A-units)',
      ins('move', 'x:(r6)', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x0'),
      ins('move', q(L2 / S), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(LMIN / S), 'a')]
if L4:
    L += [ins('mpy', 'x0', 'x0', 'b', c='u^4'), ins('move', 'b', 'x0'),
          ins('move', q(L4 / S), 'y0'), ins('mac', 'y0', 'x0', 'a')]
L += [ins('move', 'a', 'x:(r7+$09)'),
      '; dmax = max(|dage fwd|, REV ? dage rev : 0); if dmax len > R: len = R / dmax',
      ins('move', f'x:(r7+${DF:02x})', 'a'), ins('abs', 'a'),
      ins('move', 'x:(r6+$d)', 'b'), ins('and', '#>$ff0000', 'b'), ins('tst', 'b'), ins('beq', 'grnorev'),
      ins('move', f'x:(r7+${DR:02x})', 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'),
      'grnorev:', ins('move', 'a', 'y1', c='dmax (samples per sample / 8)' if P48 else 'dmax (A-units per sample)'),
      ins('move', 'x:(r7+$09)', 'x0'), ins('mpy', 'x0', 'y1', 'a'), ins('asl', DSHL, 'a', 'a'),
      ins('move', A_(R), 'x0'), ins('cmp', 'x0', 'a'), ins('ble', 'grlenok'),
      ins('move', 'y1', 'x0'), ins('move', q(R / S / 8) if P48 else q(R / S / S), 'a')] + div24() + [
      ins('move', 'a0', 'x:(r7+$09)'),
      'grlenok:',
      '; inc = 1/len = (1/S) / len_A' if P48 else '; inc = 1/len = (1/16384) / len_A',
      ins('move', 'x:(r7+$09)', 'x0'), ins('move', q(1 / S), 'a')] + div24()
if P48:
    L += ['; rounded UP, so a grain never lasts longer than len (the bounds assume dage len)',
          ins('move', 'a0', 'x0'), ins('move', 'x0', 'a'), ins('add', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$08)')]
else:
    L += [ins('move', 'a0', 'x:(r7+$08)')]
L += ['; centre (POS), spread and pan (SPRY), reverse threshold 2 REV - 1',
      ins('move', 'x:(r6+$2)', 'x0'), ins('move', q(R / S), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('add', A_(M), 'a'), ins('move', 'a', 'x:(r7+$0a)'),
      ins('move', 'x:(r6+$3)', 'x0'), ins('move', q(R / 2 / S), 'y0'), ins('mpy', 'y0', 'x0', 'a')] + ([
      '; SPRD (page 2 slot 10): pan reach, and its +-5 ms on the spread',
      ins('move', 'x:(r6+$e)', 'b'), ins('and', '#>$ff0000', 'b'), ins('move', 'b', 'x0'),
      ins('move', 'x0', 'x:(r7+$14)'), ins('move', q(JIT / S), 'y0'), ins('mac', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$0b)')] if P48 else [
      ins('move', 'a', 'x:(r7+$0b)'), ins('move', 'x:(r6+$3)', 'a'), ins('asr', 'a'),
      ins('move', 'a', 'x:(r7+$14)')]) + [
      ins('move', 'x:(r6+$d)', 'a'), ins('and', '#>$ff0000', 'a'), ins('asl', 'a'), ins('move', q(-1.0), 'x0'),
      ins('add', 'x0', 'a'), ins('move', 'a', 'x:(r7+$0c)'),
      '; start bounds per direction: lo = M + max(0, -disp), hi = M + R - max(0, disp), disp = dage len']
for d, (dslot, lo, hi) in enumerate(((DF, 0x18, 0x19), (DR, 0x1a, 0x1b))):
    L += [ins('move', f'x:(r7+${dslot:02x})', 'x0'), ins('move', 'x:(r7+$09)', 'y1'),
          ins('mpy', 'x0', 'y1', 'a'), ins('asl', DSHL, 'a', 'a'), ins('move', 'a', 'x1', c='disp'),
          ins('move', A_(M), 'b'), ins('tst', 'a'), ins('bge', f'grlo{d}'), ins('sub', 'x1', 'b'),
          f'grlo{d}:', ins('move', 'b', f'x:(r7+${lo:02x})'),
          ins('move', A_(M + R), 'b'), ins('tst', 'a'), ins('ble', f'grhi{d}'), ins('sub', 'x1', 'b'),
          f'grhi{d}:', ins('move', 'b', f'x:(r7+${hi:02x})')]
L += ['; DENS: t = 3 DENS / 4' + (' x 128/127 (127 = four full grains)' if DFULL else '') + '; g0 = 1, g2 = c(4t), g1 = c(4t - 1), g3 = c(4t - 2)',
      ins('move', ONE, 'a'), ins('move', 'a', 'x:(r7+$10)'),
      ins('move', 'x:(r6+$1)', 'x0'), ins('move', q(3 / 4 * 128 / 127) if DFULL else q(3 / 4), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$15)')]
for k, off in ((2, 0.0), (1, 0.25), (3, 0.5)):
    L += [ins('move', 'x:(r7+$15)', 'a'), ins('sub', q(off), 'a'), ins('asl', '#$2', 'a', 'a')] + clamp01(0x10 + k)
L += ['; norm = 2 / max(2, sum): (1/8) / (max(2, sum)/4), x4 in the loop',
      ins('move', q(0.25), 'a')]
for k in (1, 2, 3):
    L += [ins('move', f'x:(r7+${0x10 + k:02x})', 'x0'), ins('move', q(0.25), 'y0'), ins('mac', 'y0', 'x0', 'a')]
L += [ins('move', q(0.5), 'x0'), ins('cmp', 'x0', 'a'), ins('tlt', 'x0', 'a'), ins('move', 'a', 'x0'),
      ins('move', q(1 / 8), 'a')] + div24() + [ins('move', 'a0', 'x:(r7+$0e)'),
      '; MIX (page 1 slot 5)',
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'), ins('bne', 'grmset'),
      ins('move', ONE, 'a'), 'grmset:', ins('move', 'a', 'x:(r7+$04)')] + snap_close(0x04, 0x05, 'grmsnap')
L += [f'; bounded clearing ({NCLR} dry calls)' if not P48 else
      f'; bounded clearing ({NCLR} dry calls; {NCH} of them clear {CH} words each from min({CH} k, {N - CH}): all {N} words, none beyond)',
      ins('move', 'x:(r7+$02)', 'a'), ins('tst', 'a'), ins('beq', 'grready'),
      ins('sub', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$02)'),
      ins('move', 'x:(r7+$0e)', 'b'), ins('move', 'b', 'x:(r7+$0f)'),
      ins('move', 'x:(r7+$08)', 'b')] + [ins('move', 'b', f'x:(r7+${0x21 + 8 * k:02x})') for k in range(GR)] + [
      ins('cmp', f'#>${NCH:06x}', 'a'), ins('bge', 'grnoclr'), ins('asl', f'#${CH.bit_length() - 1:x}', 'a', 'a')]
if P48:
    L += [ins('move', f'#>${N - CH:06x}', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a', c='the top chunk overlaps, never overruns'),
          ins('move', 'x:(r7+$00)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'),
          ins('clr', 'a'), ins('do', f'#{CH}', '>grclrlp'),
          ins('move', 'a', 'y:(r4)+'), 'grclrlp:', ins('nop'),
          'grnoclr:', ins('rts'),
          'grready:'] + (aa_flush() if AA else []) + [ins('move', '#>$ffffff', 'm5'),
          ins('move', '#>$c0', 'a'), ins('move', 'r7', 'x0'), ins('add', 'x0', 'a'),
          ins('move', 'a', 'x:(r7+$15)', c='the window table\'s end, r7 + $c0, for the loop'),
          ins('move', 'x:(r7+$00)', 'a'), ins('add', '#>$1', 'a'), ins('move', 'a', 'r5', c='r5 = base + 1 = ring sample 0'),
          ins('do', 'n7', '>grend')]
else:
    L += [ins('move', 'x:(r7+$00)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'),
          ins('move', MOD, 'm4'), ins('clr', 'a'), ins('do', f'#{CH}', '>grclrlp'),
          ins('move', 'a', 'y:(r4)+'), 'grclrlp:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
          'grnoclr:', ins('rts'),
          'grready:', ins('move', MOD, 'm4'), ins('move', MOD, 'm5'),
          ins('move', '#>$ffffff', 'n4'), ins('move', 'x:(r7+$01)', 'r5'), ins('do', 'n7', '>grend')]

# ================================================================ per sample
if AA:
    L += [ins('move', 'x:(r7+$05)', 'a'), ins('move', ax('MR'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'x:(r7+$05)', c='mix += its ramp'),
          ins('move', 'x:(r7+$0f)', 'a'), ins('move', ax('NR'), 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'x:(r7+$0f)', c='norm += its ramp')]
else:
    L += smooth(0x04, 0x05, 7) + smooth(0x0e, 0x0f, 8)
L += ['; record at the head (age 0); frozen, the head does not advance, so this',
      '; slot (also the oldest, which no grain reads) is all that is written',
      ] + (aa_record() if AA else [ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'a'), ins('asr', 'a')]) + ([
      ins('move', 'x:(r7+$01)', 'b'), ins('move', 'b1', 'n5'), ins('lua', '(r5+n5)', 'r4'),
      ins('move', 'a', 'y:(r4)')] if P48 else [ins('move', 'a', 'y:(r5)')]) + [ins('clr', 'a'), ins('move', 'a', 'x:(r7+$16)'), ins('move', 'a', 'x:(r7+$17)')]
for k in range(GR):
    B = 0x20 + 8 * k
    run = f'grg{k}run'
    L += [f'; ======== grain {k} ========',
          ins('move', f'x:(r7+${B:02x})', 'a'), ins('move', f'x:(r7+${B + 1:02x})', 'x0'), ins('add', 'x0', 'a'),
          ins('tst', 'a'), ins('blt', run), f'grg{k}new:',
          '; ---- restart ----',
          ins('move', '#>$800000', 'x0'), ins('add', 'x0', 'a'),
          ins('move', 'a', f'x:(r7+${B if P48 else 0x15:02x})')] + lcg() + [
          ins('move', 'x:(r7+$0c)', 'b'), ins('cmp', 'x0', 'b', c='threshold - rnd > 0: reverse'),
          ins('move', 'x:(r7+$06)', 'a'), ins('move', 'x:(r7+$07)', 'y1'), ins('tgt', 'y1', 'a'),
          ins('move', 'a', f'x:(r7+${B + 3:02x})')] + ([
          ins('move', 'x:(r7+$1c)', 'a'), ins('move', 'x:(r7+$1f)', 'y1'), ins('tgt', 'y1', 'a'),
          ins('move', 'a', f'x:(r7+${B + 7:02x})', c='dage lo')] if P48 else []) + [
          ins('move', 'x:(r7+$18)', 'a'), ins('move', 'x:(r7+$1a)', 'y1'), ins('tgt', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$1d)', c='lo'),
          ins('move', 'x:(r7+$19)', 'a'), ins('move', 'x:(r7+$1b)', 'y1'), ins('tgt', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$1e)', c='hi')] + lcg() + [
          ins('move', 'x:(r7+$0b)', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'x:(r7+$0a)', 'x1'),
          ins('add', 'x1', 'a', c='start = centre + spread rnd'),
          ins('move', 'x:(r7+$1d)', 'x1'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a'),
          ins('move', 'x:(r7+$1e)', 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a')] + ([
          ins('and', '#>$ffff80', 'a', c='whole samples (quality pass): at an integer rate (unison) the grain reads'),
          ins('clr', 'b', c='the ring exactly, no interpolation lowpass (was -2.3 dB at 10-15 kHz)'),
          ins('move', 'b', f'x:(r7+${B + 6:02x})', c='age lo')] if P48 and SNAP else []) + [
          ins('move', 'a', f'x:(r7+${B + 2:02x})'),
          '; pan s = reach rnd' + (' held to +-1/2' if P48 else '') + '; gL = g (1/2 + s), gR = g (1/2 - s)',
          ins('move', 'x:(r7+$14)', 'y0'), ins('mpy', 'y0', 'x0', 'a')] + ([
          ins('move', q(0.5), 'x1'), ins('cmp', 'x1', 'a'), ins('tgt', 'x1', 'a'),
          ins('move', q(-0.5), 'x1'), ins('cmp', 'x1', 'a'), ins('tlt', 'x1', 'a')] if P48 else []) + [
          ins('move', 'a', 'y1'),
          ins('move', f'x:(r7+${0x10 + k:02x})', 'x1'),
          ins('move', q(0.5), 'a'), ins('add', 'y1', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
          ins('move', 'a', f'x:(r7+${B + 4:02x})'),
          ins('move', q(0.5), 'a'), ins('sub', 'y1', 'a'), ins('move', 'a', 'x0'), ins('mpy', 'x1', 'x0', 'a'),
          ins('move', 'a', f'x:(r7+${B + 5:02x})'),
          ins('move', 'x:(r7+$08)', 'a'), ins('move', 'a', f'x:(r7+${B + 1:02x})'),
          ins('move', f'x:(r7+${B if P48 else 0x15:02x})', 'a'),
          f'{run}:', ins('move', 'a', f'x:(r7+${B:02x})')] + window() + [ins('move', 'b', 'y1')]
    if P48:
        L += ['; age += dage (48 bits); linear read at age (age >= M > 0)',
              ins('move', f'x:(r7+${B + 2:02x})', 'a'), ins('move', f'x:(r7+${B + 6:02x})', 'a0'),
              ins('move', f'x:(r7+${B + 3:02x})', 'b'), ins('move', f'x:(r7+${B + 7:02x})', 'b0'),
              ins('add', 'b', 'a'), ins('move', 'a1', f'x:(r7+${B + 2:02x})'), ins('move', 'a0', f'x:(r7+${B + 6:02x})'),
              ins('asr', HSH, 'a', 'a', c='a1 = whole samples, a0 = fraction (24 bits, unsigned)'),
              ins('move', 'a0', 'x0'), ins('move', 'x0', 'b'), ins('lsr', '#$1', 'b'),
              ins('move', 'b1', 'y0', c='fraction, Q23'),
              '; ring index of the newer sample = head - whole, + ring length if that is below 0',
              ins('move', 'x:(r7+$01)', 'b'), ins('move', 'a1', 'x0'), ins('sub', 'x0', 'b'),
              ins('bge', f'grg{k}ok'), ins('add', f'#>${NL:06x}', 'b'), f'grg{k}ok:',
              ins('move', 'b1', 'n5'), ins('lua', '(r5+n5)', 'r4'),
              ins('move', 'y:(r4)-', 'x0'), ins('move', 'y:(r4)', 'a', c='the older one (y[base] mirrors the top)')]
    else:
        L += ['; age += dage; linear read at age (age >= M > 0)',
              ins('move', f'x:(r7+${B + 2:02x})', 'a'), ins('move', f'x:(r7+${B + 3:02x})', 'x0'),
              ins('add', 'x0', 'a'), ins('move', 'a', f'x:(r7+${B + 2:02x})'),
              ins('move', 'a1', 'b'), ins('asr', '#$9', 'a', 'a'), ins('move', 'a1', 'a', c='whole samples'),
              ins('and', '#>$0001ff', 'b'), ins('asl', '#$e', 'b', 'b'), ins('move', 'b1', 'y0', c='fraction'),
              ins('neg', 'a'), ins('move', 'a1', 'n5'), ins('lua', '(r5+n5)', 'r4'),
              ins('move', 'y:(r4)+n4', 'x0'), ins('move', 'y:(r4)', 'a')]
    L += [ins('sub', 'x0', 'a'),
          ins('move', 'a', 'x1'), ins('mpy', 'x1', 'y0', 'a'), ins('add', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('mpy', 'x0', 'y1', 'a', c='s w'), ins('move', 'a', 'x0'),
          ins('move', 'x:(r7+$16)', 'a'), ins('move', f'x:(r7+${B + 4:02x})', 'y1'), ins('mac', 'x0', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$16)'),
          ins('move', 'x:(r7+$17)', 'a'), ins('move', f'x:(r7+${B + 5:02x})', 'y1'), ins('mac', 'x0', 'y1', 'a'),
          ins('move', 'a', 'x:(r7+$17)')]
if P48:
    L += ['; advance the head index by adv (0 when frozen); at the wrap, copy the top word to its mirror y[base]',
          ins('move', 'x:(r7+$01)', 'a'), ins('move', 'x:(r7+$0d)', 'x0'), ins('add', 'x0', 'a'),
          ins('cmp', f'#>${NL:06x}', 'a'), ins('blt', 'grheadok'),
          ins('move', f'#>${NL - 1:06x}', 'n5'), ins('lua', '(r5+n5)', 'r4'), ins('move', '#>$ffffff', 'n5'),
          ins('move', 'y:(r4)', 'x0'), ins('lua', '(r5+n5)', 'r4'), ins('clr', 'a'), ins('move', 'x0', 'y:(r4)'),
          'grheadok:', ins('move', 'a', 'x:(r7+$01)')]
L += ['; ---- out = sum * norm * 8 (norm word = 1/(2 max(2,sum)), pans carry 1/2) ----']
for sl in (0x16, 0x17):
    L += [ins('move', f'x:(r7+${sl:02x})', 'x0'), ins('move', 'x:(r7+$0f)', 'y1'), ins('mpy', 'x0', 'y1', 'a'),
          ins('asl', '#$4' if AA else '#$3', 'a', 'a', c='x 16: the ring holds y/2' if AA else '')] + mix(mixslot=0x05)
if P48:
    L += ['grend:', ins('nop'), ins('rts')] + (aa_update() if AA else [])
else:
    L += ['; advance the head by adv (0 when frozen)',
          ins('move', 'x:(r7+$0d)', 'a'), ins('asr', HSH, 'a', 'a'), ins('move', 'a1', 'n5'), ins('nop'),
          ins('move', '(r5)+n5')]
    L += ['grend:', ins('nop'), ins('move', 'r5', 'x:(r7+$01)'), ins('move', '#>$ffffff', 'm4'),
          ins('move', '#>$ffffff', 'm5'), ins('rts')]
OUT = os.environ.get('OUT') or str(Path(__file__).with_name('grains.asm' if RING == 'long' else 'grains_test.asm'))
if not P48 and not os.environ.get('OUT'):
    sys.exit('AGE=24 is the legacy reference build: give OUT=path')
Path(OUT).write_text(finish(L))
print(f'wrote {OUT}: RING={RING} words={N} ring={NL} AGE={AGE} WINFIX={WINFIX} M={M} R={R} '
      f'SIZE 127 = {(LMIN + L2 * U127 ** 2 + L4 * U127 ** 4) / MS:.1f} ms, POS 127 = {(M + R * U127) / MS:.1f} ms, '
      f'clear {NCH} x {CH} words in {NCLR} calls')
