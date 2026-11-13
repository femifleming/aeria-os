#!/usr/bin/env python3
"""Generates smear.asm (AERIA SMEAR: diffusion smear tank with freeze)."""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import ins, q, smooth, mix, finish, e2_sub, snap_close, softclip  # noqa: E402

D = 12   # modulation reach of the two tank allpasses, samples
SEG = [('IA1', 241), ('IA2', 173),
       ('MA', 437), ('DL1', 2897), ('AP2', 1171), ('DL2', 2419),
       ('MB', 590), ('DL3', 2741), ('AP4', 1727), ('DL4', 2053)]
start, age = {}, 0
for name, ln in SEG:
    start[name] = age
    age += ln + 1 + (2 * D + 1 if name in ('MA', 'MB') else 0)
RING = age
assert RING <= 15870
LEN = dict(SEG)
HALF = LEN['MA'] + D + LEN['DL1'] + LEN['AP2'] + LEN['DL2']    # one half of the figure-8, samples


def wr(name):          # write age of a line
    return start[name]


def rd(name, extra=0):  # read age at the line's end
    return start[name] + LEN[name] + extra


def nimm(age_):
    return f'#>${(-age_) & 0xffffff:06x}'


H = f'''; CYCLES_FORWARD_BRANCHES
; AERIA SMEAR -- a time-domain stand-in for Balloon Shop's spectral smear
; and Snow freeze: two input allpass diffusers into a Dattorro-style
; figure-8 tank (two modulated allpasses, two plain allpasses, four delays,
; in-loop damping), read by three taps per side.
;
;  SMER  tank decay: RT60 0.3 .. 20 s (log), applied at four points per loop
;  DIFF  input diffusion 0..0.75 / 0.625 and tank allpass 0..0.7 / 0.5
;  TONE  in-loop damping lowpass (0 dark .. 127 none)
;  MOD   modulation depth of the two tank allpasses, 0..{D} samples, 0.7 Hz
;  FRZ   HOLD: input to the tank fades out, decay -> unity (1 - 2^-23),
;        damping -> none, modulation -> 0, over ~50 ms; THAW returns. The
;        frozen tank is then a static lossless allpass/delay loop: it rings
;        on, unchanged, until thawed.
;  MIX   0 = exact dry
;
; NOT an FFT: it smears in time (dense echoes), it does not hold per-bin
; magnitudes, so a frozen chord rings as a shimmering wash, not a static
; spectrum. See README.
;
; One ring of {RING} words (m = $3dfd, ring 15,870), every line a segment of
; it (write age .. read age), below the Sidechain window.
''' + ''.join(f';   {n:4s} age {start[n]:5d} .. {rd(n, 2 * D + 1 if n in ("MA", "MB") else 0):5d}\n' for n, _ in SEG) + '''
; r7: 00 base 01 ptr 02 clear  04/05 g t/s 06/07 damp t/s 08/09 input t/s
;   0a/0b diff t/s 0c/0d depth t/s 0e/0f mix t/s 10 lfo phase 11/12 lp A/B
;   13 A end 14 B end 15 diffused in 16 temp 18/19 A frac,int 1a/1b B frac,int
;   1c out L temp
'''


def ap(name, coef_reg='y1'):
    """Schroeder allpass on the ring: d = line end; w = a - g d -> line start;
    a = d + g w. Coefficient in y1. a in, a out."""
    assert coef_reg == 'y1'
    return [ins('move', nimm(rd(name)), 'n5'), ins('move', 'y:(r5+n5)', 'x0', c=f'{name} end'),
            ins('mac', '-x0', 'y1', 'a'), ins('move', nimm(wr(name)), 'n5'), ins('move', 'a', 'y:(r5+n5)'),
            ins('move', 'a', 'x1'), ins('move', 'x0', 'a'), ins('mac', 'y1', 'x1', 'a')]


def mod_ap(name, fr_slot, n_slot):
    """Modulated tank allpass (coefficient in y1); n_slot holds the full
    (negative) read offset, fr_slot the fraction toward the older sample."""
    return [ins('move', f'x:(r7+${n_slot:02x})', 'n5'), ins('lua', '(r5+n5)', 'r4'),
            ins('move', f'x:(r7+${fr_slot:02x})', 'y0'),
            ins('move', 'y:(r4)-', 'x0'), ins('move', 'y:(r4)', 'b'), ins('sub', 'x0', 'b'),
            ins('move', 'b', 'x1'), ins('mpy', 'x1', 'y0', 'b'), ins('add', 'x0', 'b'),
            ins('move', 'b', 'x0', c=f'{name} interpolated end'),
            ins('mac', '-x0', 'y1', 'a'),
            ins('move', nimm(wr(name)), 'n5'), ins('move', 'a', 'y:(r5+n5)'),
            ins('move', 'a', 'x1'), ins('move', 'x0', 'a'), ins('mac', 'y1', 'x1', 'a')]


def tap(name, frac, sign):
    age_ = start[name] + int(round(LEN[name] * frac))
    return [ins('move', nimm(age_), 'n5'), ins('move', 'y:(r5+n5)', 'x0'),
            ins('add' if sign > 0 else 'sub', 'x0', 'a')]


L = [H, 'init:', ins('move', '#>$ffffff', 'm4'), ins('move', 'r7', 'r4'), ins('clr', 'a'),
     ins('do', '#48', '>smizero'), ins('move', 'a', 'x:(r4)+'), 'smizero:', ins('nop'),
     ins('move', 'x:>$213', 'r4'), ins('move', 'x:(r4)', 'a'), ins('move', 'a', 'x:(r7+$00)'),
     ins('move', 'a', 'x:(r7+$01)'), ins('move', '#>$80', 'a'), ins('move', 'a', 'x:(r7+$02)'),
     ins('rts')]

L += ['proc:', ins('move', '#>$ffffff', 'm4'),
      '; FRZ: HOLD when the select is 1',
      ins('move', 'x:(r6+$4)', 'a'), ins('tst', 'a'), ins('bne', 'smhold'),
      # g per half loop = 2^-(E), E = 3 log2(10) HALF / (fs T), T = 0.3 * 66.7^SMER
      ins('move', 'x:(r6)', 'x0'), ins('move', q(math.log2(20 / 0.3) / 32), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'smexp'), ins('move', 'b', 'x0'),
      ins('move', q(3 * math.log2(10) * HALF / 2 / (44100 * 0.3) / 32), 'y0'),
      ins('mpy', 'y0', 'x0', 'a'), ins('bsr', 'smexp'), ins('move', 'b', 'x:(r7+$04)'),
      # damping coefficient 0.05 + 0.95 TONE^2, 1 at 127
      ins('move', 'x:(r6+$2)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('beq', 'smdopen'), ins('move', 'a', 'x0'), ins('mpy', 'x0', 'x0', 'a'), ins('move', 'a', 'x0'),
      ins('move', q(0.95), 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('add', q(0.05), 'a'),
      ins('bra', 'smdset'), 'smdopen:', ins('move', '#>$7fffff', 'a'),
      'smdset:', ins('move', 'a', 'x:(r7+$06)'),
      ins('move', '#>$7fffff', 'a'), ins('move', 'a', 'x:(r7+$08)'), ins('bra', 'smcommon'),
      'smhold:', ins('move', '#>$7fffff', 'a'), ins('move', 'a', 'x:(r7+$04)'),
      ins('move', 'a', 'x:(r7+$06)'), ins('clr', 'a'), ins('move', 'a', 'x:(r7+$08)'),
      'smcommon:',
      ins('move', 'x:(r6+$1)', 'a'), ins('move', 'a', 'x:(r7+$0a)'),
      ins('move', 'x:(r6+$3)', 'x0'), ins('move', q(D / 64), 'y0'), ins('mpy', 'y0', 'x0', 'a'),
      ins('move', 'x:(r6+$4)', 'b'), ins('tst', 'b'), ins('beq', 'smdepth'), ins('clr', 'a', c='HOLD parks the modulation: a lossless loop'),
      'smdepth:', ins('move', 'a', 'x:(r7+$0c)', c='depth: samples / 64'),
      ins('move', 'x:(r6+$5)', 'a'), ins('move', '#>$7f0000', 'x0'), ins('cmp', 'x0', 'a'),
      ins('bne', 'smmset'), ins('move', '#>$7fffff', 'a'), 'smmset:', ins('move', 'a', 'x:(r7+$0e)')] \
    + snap_close(0x0e, 0x0f, 'smmsnap') + snap_close(0x04, 0x05, 'smgsnap', 0x1000) + snap_close(0x06, 0x07, 'smdsnap', 0x1000) + snap_close(0x0c, 0x0d, 'smosnap', 0x100) + [
      ins('move', 'x:(r7+$02)', 'a'), ins('tst', 'a'), ins('beq', 'smready'),
      ins('sub', '#>$1', 'a'), ins('move', 'a', 'x:(r7+$02)')]
for t in (0x04, 0x06, 0x08, 0x0a, 0x0c):
    L += [ins('move', f'x:(r7+${t:02x})', 'b'), ins('move', 'b', f'x:(r7+${t + 1:02x})')]
L += [ins('cmp', '#>$00007c', 'a'), ins('bge', 'smnoclr'), ins('asl', '#$7', 'a', 'a'),
      ins('move', 'x:(r7+$00)', 'x0'), ins('add', 'x0', 'a'), ins('move', 'a', 'r4'),
      ins('move', '#>$3dfd', 'm4'), ins('clr', 'a'), ins('do', '#128', '>smclrlp'),
      ins('move', 'a', 'y:(r4)+'), 'smclrlp:', ins('nop'), ins('move', '#>$ffffff', 'm4'),
      'smnoclr:', ins('rts'),
      'smready:', ins('move', '#>$3dfd', 'm4'), ins('move', '#>$3dfd', 'm5'), ins('move', '#>$1', 'n0'),
      ins('move', 'x:(r7+$01)', 'r5'), ins('do', 'n7', '>smend')]
L += smooth(0x04, 0x05, 11) + smooth(0x06, 0x07, 11) + smooth(0x08, 0x09, 11) + smooth(0x0a, 0x0b, 8) \
    + smooth(0x0c, 0x0d, 8) + smooth(0x0e, 0x0f, 7)
# LFO 0.7 Hz: phase step 2 * 0.7 / 44100
L += ['; ---- LFO and the two modulated read offsets (opposite phase) ----',
      ins('move', 'x:(r7+$10)', 'a'), ins('add', q(2 * 0.7 / 44100), 'a'), ins('move', 'a1', 'x:(r7+$10)'),
      ins('move', 'a1', 'a'),
      ins('move', 'a', 'x0'), ins('abs', 'a'), ins('move', '#>$7fffff', 'b'), ins('sub', 'a', 'b'),
      ins('move', 'b', 'y0'), ins('mpy', 'y0', 'x0', 'a'), ins('asl', '#$2', 'a', 'a'),
      ins('move', 'a', 'x0', c='parabolic sine'), ins('move', 'x:(r7+$0d)', 'y0'),
      ins('mpy', 'y0', 'x0', 'a', c='mod / 64 samples, signed')]
for ch, (fr, nn) in enumerate(((0x18, 0x19), (0x1a, 0x1b))):
    if ch:
        L += [ins('move', 'x:(r7+$16)', 'a'), ins('neg', 'a')]
    else:
        L += [ins('move', 'a', 'x:(r7+$16)')]
    # mod/64 in Q23: int = a >> 17 (floor), frac = low 17 bits << 6
    L += [ins('move', 'a1', 'b'), ins('asr', '#$11', 'a', 'a'), ins('move', 'a1', 'a'),
          ins('and', '#>$01ffff', 'b'), ins('asl', '#$6', 'b', 'b'), ins('move', 'b1', f'x:(r7+${fr:02x})'),
          ins('neg', 'a'), ins('sub', nimm(-rd(('MA', 'MB')[ch], D)).replace('#>$', '#>$'), 'a'),
          ins('move', 'a1', f'x:(r7+${nn:02x})', c='-(end + D + int)')]
L += ['; ---- mono in, input gain (freeze fades it), four diffusers ----',
      ins('move', 'x:(r0)', 'a'), ins('move', 'x:(r0+n0)', 'x0'), ins('add', 'x0', 'a'), ins('asr', '#$3', 'a', 'a', c='mono / 4: tank headroom'),
      ins('move', 'a', 'x0'), ins('move', 'x:(r7+$09)', 'y1'), ins('mpy', 'x0', 'y1', 'a'),
      ins('move', 'x:(r7+$0b)', 'x0'), ins('move', q(0.75), 'y1'), ins('mpy', 'x0', 'y1', 'b'),
      ins('move', 'b', 'y1')]
L += ap('IA1', 'y1')
L += [ins('move', 'x:(r7+$0b)', 'x0'), ins('move', q(0.625), 'y1'), ins('mpy', 'x0', 'y1', 'b'),
      ins('move', 'b', 'y1')]
L += ap('IA2', 'y1') + [ins('move', 'a', 'x:(r7+$15)')]
L += ['; ---- tank ends (previous values) ----',
      ins('move', nimm(rd('DL2')), 'n5'), ins('move', 'y:(r5+n5)', 'x0'), ins('move', 'x0', 'x:(r7+$13)'),
      ins('move', nimm(rd('DL4')), 'n5'), ins('move', 'y:(r5+n5)', 'x0'), ins('move', 'x0', 'x:(r7+$14)')]
for half, (other, mname, fr, nn, dl1, apn, dl2, lp) in enumerate((
        (0x14, 'MA', 0x18, 0x19, 'DL1', 'AP2', 'DL2', 0x11),
        (0x13, 'MB', 0x1a, 0x1b, 'DL3', 'AP4', 'DL4', 0x12))):
    L += [f'; ---- tank half {"AB"[half]} ----',
          ins('move', f'x:(r7+${other:02x})', 'x0'), ins('move', 'x:(r7+$05)', 'y1'),
          ins('mpy', 'x0', 'y1', 'a'), ins('move', 'x:(r7+$15)', 'x0'), ins('add', 'x0', 'a'),
          ins('move', 'x:(r7+$0b)', 'x0'), ins('move', q(0.7), 'y1'), ins('mpy', 'x0', 'y1', 'b'),
          ins('move', 'b', 'y1')]
    L += mod_ap(mname, fr, nn)
    L += [ins('move', nimm(wr(dl1)), 'n5'), ins('move', 'a', 'y:(r5+n5)'),
          ins('move', nimm(rd(dl1)), 'n5'), ins('move', 'y:(r5+n5)', 'a'),
          ins('move', f'x:(r7+${lp:02x})', 'x0'), ins('sub', 'x0', 'a'), ins('move', 'a', 'x1'),
          ins('move', 'x:(r7+$07)', 'y1'), ins('mpy', 'y1', 'x1', 'a'), ins('add', 'x0', 'a'),
          ins('move', 'a', f'x:(r7+${lp:02x})'), ins('move', 'a', 'x0'), ins('move', 'x:(r7+$05)', 'y1'),
          ins('mpy', 'x0', 'y1', 'a', c='x decay'),
          ins('move', 'x:(r7+$0b)', 'x0'), ins('move', q(0.5), 'y1'), ins('mpy', 'x0', 'y1', 'b'),
          ins('move', 'b', 'y1')]
    L += ap(apn, 'y1') + [ins('move', nimm(wr(dl2)), 'n5'), ins('move', 'a', 'y:(r5+n5)')]
L += ['; ---- output taps ----', ins('clr', 'a')]
for name, fr, sg in (('DL3', 0.10, 1), ('DL3', 0.60, 1), ('DL1', 0.30, -1)):
    L += tap(name, fr, sg)
L += [ins('asr', 'a'), ins('move', 'a', 'x:(r7+$1c)'), ins('clr', 'a')]
for name, fr, sg in (('DL1', 0.15, 1), ('DL1', 0.65, 1), ('DL3', 0.35, -1)):
    L += tap(name, fr, sg)
L += [ins('asr', 'a'), ins('move', 'a', 'x:(r7+$16)'), ins('move', 'x:(r7+$1c)', 'a')] + mix(mixslot=0x0f) \
    + [ins('move', 'x:(r7+$16)', 'a')] + mix(mixslot=0x0f) + [ins('move', '(r5)+')]
L += ['smend:', ins('nop'), ins('move', 'r5', 'x:(r7+$01)'), ins('move', '#>$ffffff', 'm4'),
      ins('move', '#>$ffffff', 'm5'), ins('rts')]
L += ['; ---- subroutines (after their callers) ----'] + e2_sub('smexp', 'smxpend')
Path(__file__).with_name('smear.asm').write_text(finish(L))
print('wrote smear.asm', RING, 'words; half loop', HALF)
