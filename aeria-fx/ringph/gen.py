#!/usr/bin/env python3
"""Generates ringph.asm (AERIA RING: ring modulator with envelope follow / six-stage phaser)."""
import math
import os
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, softclip, mix, finish, e2_sub, snap_close  # noqa: E402

H = '''; CYCLES_FORWARD_BRANCHES
; AERIA RING -- StoreFX ring modulator (carrier follows the input level)
; and StoreFX phaser (six first-order allpasses swept by a sine, fed back
; through a soft clip, added to the dry) as one effect, MODE page-2 slot 6.
;
;  RING  carrier sin(pi p), p a wrapping 24-bit phase; frequency 20 Hz ..
;        5 kHz (FREQ, log); FOLW: the step is multiplied by
;        1 + 6 FOLW min(1, 3 env) (env: |mono in|, 3 ms attack, 120 ms
;        release), held under 0.9 x Nyquist (19.8 kHz: past Nyquist it folded
;        back down; 13 Nov 2026); BIAS mixes the carrier toward 1 (0 = ring, 127 = nearly
;        dry AM); SPRD offsets the right carrier 0..90 degrees: at 127 the
;        two carriers are in quadrature (sin / cos), so the sidebands sit
;        +-90 degrees apart between the sides -- wide, and the mono sum is
;        the same ring 3 dB up, never a cancellation (it was 0..180: at the
;        top right = -left, a mono sum of nothing).
;  PHSR  LFO 0.05 .. 10 Hz (RATE, log); notch sweep log 200 Hz .. 5 kHz:
;        u = CNTR + DPTH lfo / 2 clamped to [0,1], f = 200 * 25^u; stage
;        coefficient a = -exp(-2 pi f / fs) (tan-free first-order allpass
;        approximation, exact to second order), computed per block for the
;        block's end and glided per sample (2^-3); FDBK 0 .. 0.85 through
;        x - x^3/3; out = 0.7 (x + v). SPRD offsets the right LFO 0..180
;        degrees (127: the two sides sweep opposite ways).
;  Sine: s = 4p(1-|p|), y = 0.775 s + 0.225 s|s| (|err| < 0.1 %).
;
; r7: 03 mode+1  04/05 inc t/s (RING carrier, PHSR LFO)  06/07 follow|fb
;     08/09 bias|depth  0a/0b spread  0c/0d mix  0e env  0f phase
;     10/11 aL t/s  12/13 aR t/s  14..19 L stage states  1a..1f R states
;     20 fbL  21 fbR  22 temp  23 cntr  24 snap flag  25/26 fb DC mean L/R  27/28 their low words
;     29 silent-block count (PHSR)  2a CNTR target (23 = CNTR glided per block)
; Quality pass (12 Nov 2026): allpass stages round (macr), the feedback DC mean is 48-bit, PHSR clears
; its states after 0.74 s of digital silence in (tails end in exact 0); FREQ/RATE/CNTR reach the top
; of their ranges at 127 (v/127, the display law; they were v/128).
'''


def sine():
    """a = p (sign-extended Q23 in a) -> a = sin(pi p). Clobbers b x0 x1 y0 y1."""
    return [ins('move', 'a', 'x0'), ins('abs', 'a'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'),
            ins('move', 'b', 'y0', c='1 - |p|'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$2', 'a', 'a'),
            ins('move', 'a', 'x0', c='s = 4 p (1 - |p|)'), ins('abs', 'a'), ins('move', 'a', 'y0'),
            ins('mpy', 'y0', 'x0', 'a', c='s |s|'), ins('move', 'a', 'x1'),
            ins('move', q(0.225), 'y0'), ins('mpy', 'x1', 'y0', 'a'), ins('move', q(0.775), 'y0'),
            ins('mac', 'y0', 'x0', 'a')]


def knob_exp(slot, span_log2, scale, run_label, top=None):
    """a <- scale * 2^-(span (1 - v/127)), v = the knob value 0..127 (knob word = v/128): 127 is the
    top of the range exactly, as the display law reads (quality pass: it was 1 - v/128, 4 % short at the top)."""
    return [ins('move', f'x:(r6+${slot:x})', 'a'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'),
            ins('move', 'b', 'x0'), ins('move', q(span_log2 / 32 * 128 / 127), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
            ins('sub', q(span_log2 / 32 / 127), 'a'), ins('move', '#>$0', 'x0'), ins('tst', 'a'), ins('tlt', 'x0', 'a'),
            ins('bsr', 'rpexp'), ins('move', 'b', 'x0'), ins('move', q(scale), 'y0'),
            ins('mpy', 'y0', 'x0', 'a')]


QR = int(os.environ.get('QR', '1'))     # allpass stages: rounded (macr) instead of truncated
QM = int(os.environ.get('QM', '1'))     # feedback DC mean: 48 bits
QF = int(os.environ.get('QF', '1'))
CS = int(os.environ.get('CS', '3'))     # PHSR: CNTR glide per block, 2^-CS     # PHSR: clear the states after 0.74 s of digital silence in
CLAMP = float(os.environ.get('CLAMP', '0.9'))   # RING: the carrier step held to this x Nyquist (0: no clamp, the build before)
RLP = int(os.environ.get('RLP', '1'))   # RING: the input low-pass that tracks the carrier (13 Nov 2026); 0: none (the build before)
OUTF = os.environ.get('OUT')
# ---- RING input low-pass (13 Nov 2026, "stop aliasing"): x c puts each input partial f at |f - fc| and f + fc;
# above Nyquist the upper one folds back down. A 2-pole Butterworth low-pass (zeros at Nyquist) on each side's input
# with its corner where f + fc meets Nyquist, fs/2 - fc (from the carrier step at the start of each block); off
# (flat) while that corner would sit above 19 kHz (carrier under 3 kHz). A1H(step), A2(step) are polynomials in
# t = (step - SC) / SW (fitted to the bilinear Butterworth); B = (1 - 2 A1H - A2)/8 (y/2, unity at DC).
#   w = B x + 2 A1H w1 + A2 w2 (rounded);  y/2 = w + 2 K w1 + K w2;  the ring product takes y/2 x 2.
RLO, RHI = 0.136, CLAMP or 0.9
RSC, RSW = (RLO + RHI) / 2, (RHI - RLO) / 2
RW = dict(B=0x2b, A1H=0x2c, A2=0x2d, K=0x2e, W1L=0x2f, W2L=0x30, W1R=0x31, W2R=0x32, XL=0x33, XR=0x34)


def _rlp_fit():
    def co(st):
        k = 1 / math.tan(math.pi * st / 2)
        n = 1 + math.sqrt(2) * k + k * k
        return -(2 * (k * k - 1) / n) / 2, -((1 - math.sqrt(2) * k + k * k) / n)
    pts = [RLO + (RHI - RLO) * i / 299 for i in range(300)]
    ts = [(v - RSC) / RSW for v in pts]
    out = []
    for kk, deg in ((0, 3), (1, 4)):
        ys = [co(v)[kk] for v in pts]
        M = [[sum(t ** (i + j) for t in ts) for j in range(deg + 1)] for i in range(deg + 1)]
        vv = [sum(t ** i * y for t, y in zip(ts, ys)) for i in range(deg + 1)]
        n = deg + 1
        for i in range(n):
            for r in range(n):
                if r != i:
                    f = M[r][i] / M[i][i]
                    M[r] = [a - f * b for a, b in zip(M[r], M[i])]
                    vv[r] -= f * vv[i]
        out.append([vv[i] / M[i][i] for i in range(n)])
    return out


RPOLY = _rlp_fit() if RLP else None


def rx(nm):
    return f'x:(r7+${RW[nm]:02x})'


def rlp_block():
    """per block: the carrier step from the smoothed state (as the frame loop forms it), held to the clamp; the
    low-pass words for it. Clobbers a b x0 x1 y0 y1."""
    out = ['; RING input low-pass for this block: step = inc (1 + 6 FOLW min(1, 3 env)) from the state, as below',
           ins('move', 'x:(r7+$0e)', 'x0'), ins('move', q(3 / 8), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
           ins('move', q(1 / 8), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('move', 'a', 'x0'),
           ins('move', 'x:(r7+$07)', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(6 / 8), 'y0'),
           ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$3', 'a', 'a'), ins('add', q(1 / 8), 'a'), ins('move', 'a', 'x0'),
           ins('move', 'x:(r7+$05)', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$3', 'a', 'a'),
           ins('move', q(RHI), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
           ins('move', q(RLO), 'x0'), ins('cmp', 'x0', 'a'), ins('bge', 'rplpon'),
           '; carrier under 3 kHz: no filter (B = 1/2, the rest 0: y/2 = x/2)',
           ins('move', '#>$400000', 'x0'), ins('move', 'x0', rx('B')), ins('clr', 'a'),
           ins('move', 'a', rx('A1H')), ins('move', 'a', rx('A2')), ins('move', 'a', rx('K')), ins('bra', 'rplpdone'),
           'rplpon:', ins('sub', q(RSC), 'a'), ins('move', 'a', 'x0'), ins('move', q(1 / RSW / 4), 'y0'),
           ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$2', 'a', 'a'), ins('move', 'a', 'y1', c='t')]
    for kk, nm in enumerate(('A1H', 'A2')):
        row = [v / 2 for v in RPOLY[kk]]
        out += [ins('move', q(row[-1]), 'a')]
        for c in reversed(row[:-1]):
            out += [ins('move', 'a', 'x0'), ins('mpy', 'x0', 'y1', 'a'), ins('add', q(c), 'a')]
        out += [ins('asl', 'a'), ins('move', 'a', rx(nm))]
    out += [ins('move', q(1 / 8), 'a'), ins('move', rx('A1H'), 'x0'), ins('move', q(1 / 4), 'y0'), ins('mac', '-y0', 'x0', 'a'),
            ins('move', rx('A2'), 'x0'), ins('move', q(1 / 8), 'y0'), ins('mac', '-y0', 'x0', 'a'),
            ins('move', 'a', rx('B'), c='(1 - 2 A1H - A2)/8'), ins('move', '#>$7fffff', 'x0'), ins('move', 'x0', rx('K')),
            'rplpdone:',
            '; a rounding limit cycle (a few LSB) left in the filter states is cleared',
            ins('move', rx('W1L'), 'a'), ins('abs', 'a'), ins('move', rx('W2L'), 'b'), ins('abs', 'b'), ins('add', 'b', 'a'),
            ins('move', rx('W1R'), 'b'), ins('abs', 'b'), ins('add', 'b', 'a'), ins('move', rx('W2R'), 'b'), ins('abs', 'b'),
            ins('add', 'b', 'a'), ins('cmp', '#>$000040', 'a'), ins('bgt', 'rplpkeep'), ins('clr', 'a')]
    out += [ins('move', 'a', rx(nm)) for nm in ('W1L', 'W2L', 'W1R', 'W2R')]
    return out + ['rplpkeep:']


def rlp_frame():
    out = []
    for side, src in (('L', 'x:(r0)'), ('R', 'x:(r0+n0)')):
        out += [f'; RING input low-pass, {side}: y/2 -> {rx("X" + side)}',
                ins('move', src, 'x0'), ins('move', rx('B'), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
                ins('move', rx('W1' + side), 'x1'), ins('move', rx('A1H'), 'y0'), ins('mac', 'x1', 'y0', 'a'), ins('mac', 'x1', 'y0', 'a'),
                ins('move', rx('W2' + side), 'x0'), ins('move', rx('A2'), 'y0'), ins('macr', 'y0', 'x0', 'a'),
                ins('move', 'x1', rx('W2' + side)), ins('move', 'a', rx('W1' + side)),
                ins('move', rx('K'), 'y0'), ins('mac', 'x1', 'y0', 'a'), ins('mac', 'x1', 'y0', 'a'), ins('mac', 'y0', 'x0', 'a'),
                ins('move', 'a', rx('X' + side))]
    return out
L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#64' if RLP else '#48', '>rpizero'), ins('move', 'a', 'x:(r4)+'), 'rpizero:', ins('nop'), ins('rts')]

L += ['proc:', ins('move', '#>$ffffff', 'm4'), ins('move', '#>$1', 'n0'),
      ins('move', 'x:(r6+$c)', 'a'), ins('asr', '#$10', 'a', 'a'), ins('move', '#>$1', 'x0'),
      ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'), ins('add', '#>$1', 'a'),
      ins('move', 'x:(r7+$03)', 'x0'), ins('cmp', 'x0', 'a'), ins('beq', 'rpsame'),
      ins('move', 'a', 'x:(r7+$03)'), ins('lua', '(r7+$e)', 'r4'), ins('clr', 'a'),
      ins('do', '#27', '>rpmzero'), ins('move', 'a', 'x:(r4)+'), 'rpmzero:', ins('nop'),
      ins('move', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$24)'),
      'rpsame:',
      ins('move', 'x:(r6+$1)', 'a'), ins('move', 'a', 'x:(r7+$06)'),
      ins('move', 'x:(r6+$3)', 'a'), ins('move', 'a', 'x:(r7+$08)'),
      ins('move', 'x:(r6+$2)', 'a'), ins('move', 'a', 'x:(r7+$0a)'),
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'rpmset'), ins('move', '#>$7fffff', 'a'), 'rpmset:', ins('move', 'a', 'x:(r7+$0c)')] \
    + snap_close(0x0c, 0x0d, 'rpmsnap') + [
      ins('move', 'x:(r7+$03)', 'a'), ins('move', '#>$1', 'x0'), ins('cmp', 'x0', 'a'),
      ins('beq', 'rpbring'), ins('bra', 'rpbphsr')]


def snap(pairs, run):
    out = [ins('move', 'x:(r7+$24)', 'a'), ins('tst', 'a'), ins('beq', run)]
    for t in pairs:
        out += [ins('move', f'x:(r7+${t:02x})', 'b'), ins('move', 'b', f'x:(r7+${t + 1:02x})')]
    return out + [ins('clr', 'a'), ins('move', 'a', 'x:(r7+$24)')]


# ======================= RING ===============================================
# inc = 2 f / fs, f = 20 * 250^FREQ  -> 0.22676 * 2^-(7.966 (1 - FREQ))
L += ['rpbring:'] + knob_exp(0, math.log2(250), 2 * 5000 / 44100, 'r') + [ins('move', 'a', 'x:(r7+$04)'),
      ins('move', 'x:(r7+$0a)', 'a'), ins('asr', 'a'), ins('move', 'a', 'x:(r7+$0a)', c='RING: SPRD 0..90 degrees')]
L += snap((0x04, 0x06, 0x08, 0x0a), 'rprrun') + ['rprrun:'] + (rlp_block() if RLP else []) + [ins('do', 'n7', '>rprend')]
L += smooth(0x04, 0x05, 7) + smooth(0x06, 0x07, 7) + smooth(0x08, 0x09, 7) + smooth(0x0a, 0x0b, 7) \
    + smooth(0x0c, 0x0d, 7)
L += (rlp_frame() if RLP else []) + ['; envelope of |mono|: attack 3 ms, release 120 ms',
      ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'a'), ins('asr', 'a'),
      ins('abs', 'a'), ins('move', 'x:(r7+$0e)', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
      ins('move', q(1 - math.exp(-1 / (0.120 * 44100))), 'y1'),
      ins('move', q(1 - math.exp(-1 / (0.003 * 44100))), 'y0'),
      ]
L += [ins('move', 'y1', 'b'), ins('tst', 'a'), ins('tgt', 'y0', 'b'), ins('move', 'b', 'y1'),
              ins('mpy', 'y1', 'x1', 'a'), ins('add', 'x0', 'a'), ins('move', 'a', 'x:(r7+$0e)'),
              '; step = inc (1 + 6 FOLW min(1, 3 env)), formed /8 then x8 in 56 bits',
              ins('move', 'a', 'x0'), ins('move', q(3 / 8), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
              ins('move', q(1 / 8), 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
              ins('move', 'a', 'x0', c='min(1, 3 env) / 8'), ins('move', 'x:(r7+$07)', 'y0'),
              ins('mpy', 'y0', 'x0', 'a'), ins('move', 'a', 'x0'), ins('move', q(6 / 8), 'y0'),
              ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$3', 'a', 'a', c='6 FOLW e / 8'), ins('add', q(1 / 8), 'a'), ins('move', 'a', 'x0'),
              ins('move', 'x:(r7+$05)', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$3', 'a', 'a')] + ([
              ins('move', q(CLAMP), 'x0', c='carrier held under %.2f x Nyquist (%.0f Hz): FREQ x FOLW past it used to fold' % (CLAMP, CLAMP * 22050)),
              ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a', c='back down (aliased carrier, 13 Nov 2026)')] if CLAMP else []) + [
              ins('move', 'x:(r7+$0f)', 'x0'), ins('add', 'x0', 'a'),
              ins('move', 'a1', 'x:(r7+$0f)', c='phase wraps mod 2 in a1')]
for ch in range(2):
    L += [f'; ---- RING {"LR"[ch]} ----', ins('move', 'x:(r7+$0f)', 'a')]
    if ch:
        L += [ins('move', 'x:(r7+$0b)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'a', c='wrap')]
    L += sine() + ['; carrier toward 1 by BIAS: c = s + b (1 - s)',
                   ins('move', 'a', 'x0'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'),
                   ins('move', 'b', 'x1'), ins('move', 'x:(r7+$09)', 'y0'), ins('mac', 'x1', 'y0', 'a'),
                   ins('move', 'a', 'x0')] + ([ins('move', rx('XR' if ch else 'XL'), 'y1'), ins('mpy', 'x0', 'y1', 'a'),
                   ins('asl', 'a', c='the low-passed input is y/2')] if RLP else [ins('move', 'x:(r0)', 'y1'), ins('mpy', 'x0', 'y1', 'a')]) \
        + mix(mixslot=0x0d)
L += ['rprend:', ins('nop'), ins('rts')]

# ======================= PHSR ===============================================
# LFO inc = 2 f / fs, f = 0.05 * 200^RATE -> 4.5351e-4 * 2^-(7.644 (1 - RATE)); per BLOCK x16
L += ['rpbphsr:'] + knob_exp(0, math.log2(200), 2 * 10 / 44100, 'p') + [ins('move', 'a', 'x:(r7+$04)'),
      ins('move', 'x:(r6+$4)', 'a'), ins('move', 'a', 'x0'), ins('move', q(1 / 127), 'y0'), ins('mac', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$2a)', c='CNTR v/127 (127 = 5 kHz, the display law; limited to $7fffff)'),
      ins('move', 'x:(r6+$1)', 'x0'), ins('move', q(0.85), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'a', 'x:(r7+$06)', c='fb 0..0.85')]
L += snap((0x04, 0x06, 0x08, 0x0a, 0x2a), 'rpprun') + ['rpprun:']
L += ['; CNTR glides per block (2^-%d: ~%.0f ms), so a jump sweeps the notches instead of ringing them' % (CS, 16 * 2 ** CS / 44.1)] \
    + smooth(0x2a, 0x23, CS)
# coefficient targets for the block's end, both channels
L += [ins('move', 'x:(r7+$05)', 'x0'), ins('move', q(16 / 128), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('asl', '#$7', 'a', 'a'), ins('move', 'x:(r7+$0f)', 'x0'), ins('add', 'x0', 'a'),
      ins('move', 'a1', 'x:(r7+$22)', c='phase at block end')]
for ch, at in enumerate((0x10, 0x12)):
    L += [ins('move', 'x:(r7+$22)', 'a')]
    if ch:
        L += [ins('move', 'x:(r7+$0b)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a1', 'a')]
    L += sine() + ['; u = CNTR + DPTH lfo / 2 in [0, 1)',
                   ins('move', 'a', 'x0'), ins('move', 'x:(r7+$09)', 'y0'), ins('mpy', 'y0', 'x0', 'a'),
                   ins('asr', 'a'), ins('move', 'x:(r7+$23)', 'x0'), ins('add', 'x0', 'a'),
                   ins('move', '#>$0', 'x0'), ins('tst', 'a'), ins('tlt', 'x0', 'a'),
                   ins('move', '#>$7fffff', 'x0'), ins('cmp', 'x0', 'a'), ins('tgt', 'x0', 'a'),
                   ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'), ins('move', 'b', 'x0'),
                   ins('move', q(math.log2(25) / 32), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
                   ins('bsr', 'rpexp'), ins('move', 'b', 'x0'),
                   ins('move', q(2 * math.pi * 5000 / 44100 / math.log(2) / 32), 'y0'),
                   ins('mpy', 'y0', 'x0', 'a', c='E/32, E = 2 pi f / (fs ln 2)'), ins('bsr', 'rpexp'),
                   ins('neg', 'b'), ins('move', 'b', f'x:(r7+${at:02x})')]
if QF:
    L += ['; silence flush (quality pass): after 2048 blocks (0.74 s) of digital silence in, the stage and feedback',
          '; states are cleared. Rounding leaves first-order allpasses near -1 a dead band of up to ~18 LSB, so',
          '; without this a few LSB of DC could sit in the output for ever.',
          ins('move', 'r0', 'r4'), ins('clr', 'a'), ins('do', 'n7', '>rpqscan'),
          ins('move', 'x:(r4)+', 'x0'), ins('or', 'x0', 'a'), ins('move', 'x:(r4)+', 'x0'), ins('or', 'x0', 'a'),
          'rpqscan:', ins('move', 'x:(r7+$29)', 'b'), ins('add', '#>$1', 'b'), ins('tst', 'a'), ins('beq', 'rpqquiet'),
          ins('clr', 'b'), 'rpqquiet:', ins('move', '#>$000800', 'x0'), ins('cmp', 'x0', 'b'), ins('tgt', 'x0', 'b'),
          ins('move', 'b', 'x:(r7+$29)'), ins('blt', 'rpqkeep'),
          ins('clr', 'a'), ins('lua', '(r7+$14)', 'r4'), ins('do', '#14', '>rpqz1'), ins('move', 'a', 'x:(r4)+'), 'rpqz1:',
          ins('lua', '(r7+$25)', 'r4'), ins('do', '#4', '>rpqz2'), ins('move', 'a', 'x:(r4)+'), 'rpqz2:',
          'rpqkeep:']
L += [ins('move', 'x:(r7+$24)', 'a'), ins('tst', 'a'), ins('beq', 'rpploop'),
      ins('move', 'x:(r7+$10)', 'a'), ins('move', 'a', 'x:(r7+$11)'),
      ins('move', 'x:(r7+$12)', 'a'), ins('move', 'a', 'x:(r7+$13)'),
      ins('clr', 'a'), ins('move', 'a', 'x:(r7+$24)'),
      'rpploop:', ins('do', 'n7', '>rppend')]
L += smooth(0x04, 0x05, 7) + smooth(0x06, 0x07, 7) + smooth(0x08, 0x09, 7) + smooth(0x0a, 0x0b, 7) \
    + smooth(0x0c, 0x0d, 7) + smooth(0x10, 0x11, 3) + smooth(0x12, 0x13, 3)
L += [ins('move', 'x:(r7+$0f)', 'a'), ins('move', 'x:(r7+$05)', 'x0'), ins('add', 'x0', 'a'),
      ins('move', 'a1', 'x:(r7+$0f)')]
for ch, (ac, z0, fb) in enumerate(((0x11, 0x14, 0x20), (0x13, 0x1a, 0x21))):
    L += [f'; ---- PHSR {"LR"[ch]} ----', ins('move', 'x:(r0)', 'a'), ins('move', f'x:(r7+${fb:02x})', 'x0'),
          ins('move', 'x:(r7+$07)', 'y0'), ins('mac', 'y0', 'x0', 'a'), ins('move', 'a', 'x0', c='v'),
          ins('move', f'x:(r7+${ac:02x})', 'y0')]
    for k in range(6):
        z = z0 + k
        MC = 'macr' if QR else 'mac'
        L += [ins('move', f'x:(r7+${z:02x})', 'b'), ins(MC, 'y0', 'x0', 'b', c=f'stage {k + 1}: y = a v + z'),
              ins('move', 'b', 'x1'), ins('move', 'x0', 'a'), ins(MC, '-x1', 'y0', 'a'),
              ins('move', 'a', f'x:(r7+${z:02x})', c='z = v - a y'), ins('move', 'x1', 'x0')]
    mean = 0x25 + ch
    L += [ins('move', 'x0', 'a'), ins('move', 'x0', 'x:(r7+$22)')] + softclip('a', 'a') + [
          '; DC-free feedback: subtract a leaky mean (2^-10, ~7 Hz)',
          ins('move', f'x:(r7+${mean:02x})', 'b')] + ([ins('move', f'x:(r7+${mean + 2:02x})', 'b0')] if QM else []) + [
          ins('sub', 'b', 'a'),
          ins('move', 'a', f'x:(r7+${fb:02x})'), ins('asr', '#$a', 'a', 'a'), ins('add', 'a', 'b')] + (
          [ins('move', 'b1', f'x:(r7+${mean:02x})'), ins('move', 'b0', f'x:(r7+${mean + 2:02x})')] if QM else
          [ins('move', 'b', f'x:(r7+${mean:02x})')]) + [
          ins('move', 'x:(r7+$22)', 'a'), ins('move', 'x:(r0)', 'x0'), ins('add', 'x0', 'a'),
          ins('move', 'a', 'x0'), ins('move', q(0.7), 'y0'), ins('mpy', 'y0', 'x0', 'a')] + mix(mixslot=0x0d)
L += ['rppend:', ins('nop'), ins('rts')]

L += ['; ---- subroutines (after their callers) ----'] + e2_sub('rpexp', 'rpxpend')
Path(OUTF or Path(__file__).with_name('ringph.asm')).write_text(finish(L))
print('wrote ringph.asm')
