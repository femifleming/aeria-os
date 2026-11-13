#!/usr/bin/env python3
"""Generates spectral.asm (AERIA SMEAR: a real STFT spectral processor, N = 1024, hop 256).

Runs on the host (no dependencies): python3 spectral/gen.py
Importing this module (test.py does) only builds the constants and the source text; nothing is written.
"""
import math
import re
from pathlib import Path

FS = 44100.0
N, HOP = 1024, 256
LATENCY = N + HOP                  # the job of one hop is spread over the 16 calls of the next: 1,024 + 256

# ---------------------------------------------------------------- memory map (offsets from the Y base)
TW, RP, WK, IR, OR_, HH, END = 0x0000, 0x0400, 0x0800, 0x1000, 0x1800, 0x2000, 0x2400
MEM_WORDS = END

# ---------------------------------------------------------------- r7 slots
_names = ('BASE RPB WKB IRB ORB HHB WKE UADDR BUDG PHASE READY R0S R6S IRP ORP RNG '
          'P0 H2 TSN J TWP UIN UOUT SK SK2 JL TSW '
          'BP BQ BREM BS BH BDONE R5P N5 R6P N6 '
          'EDEC THR GL GL8 DITH FS WTHR TAUT TAUS YN MIXS CD CG CB MODE STL STR OJ OP OW OT OCNT XT').split()
S = {n: i for i, n in enumerate(_names)}
assert len(S) <= 0x40, len(S)
USED_X = len(S)

# ---------------------------------------------------------------- constants


def q(x):
    v = round(x * (1 << 23))
    v = max(-(1 << 23), min((1 << 23) - 1, v))
    return f'#>${v & 0xffffff:06x}'


UNIT = 64                          # butterflies a unit
R4G = 32                           # radix-4 groups a unit
BPAIR = 16                         # bin pairs a unit
OLP = 128                          # overlap-add points a unit
LCG = 0x343FD5                     # x -> x * LCG mod 2^24 (odd seed), top bits used
SEED = 0x2F6E2B
SMR_T0, SMR_T1 = 0.1, 20.0         # (nominal) decay times at the ends of the knob
E0 = 0.4                           # 1 - d at SMR 1 .. E0 / 200 at SMR 127
KSMR = math.log2(200) * 128 / 127 / 32
GATE_TOP_DB, GATE_RANGE_DB = -20.0, 60.0
KGATE = (GATE_RANGE_DB / (20 * math.log10(2))) / 126 * 128 / 32
SLOT_FS = 1 / 4                    # a full-scale bin-centred sine reads 1/4 in a slot
# bin magnitude: max(max + MB0 min, MA1 max + MB1 min) of |re|, |im| (two-segment alpha-max-beta-min,
# within -1.2 % .. +1.2 % of the true magnitude; it was max + 0.375 min, -2.8 % .. +6.8 %, whose ripple
# with the phase the hold kept as a wash on steady tones). Quality pass 12 Nov 2026.
MB0, MA1, MB1 = 5 / 32, 27 / 32, 71 / 128
EST = 1.0041                       # mean of the estimate over phase (was 1.03 for max + 0.375 min)
TMAX = 10 ** (GATE_TOP_DB / 20) * SLOT_FS * EST
HR = 8 / 3                         # the overlap ring holds wet / HR
F_PIVOT = 1000.0
KAPPA2 = 2 * (1 - math.cos(2 * math.pi * F_PIVOT / FS))
F_TILT_LO = 100.0                  # the tilt's zero / pole goes down to here
CMIN = 1 - math.exp(-2 * math.pi * F_TILT_LO / FS)


def smr_e(v):
    """1 - d (per hop) of SMR knob v."""
    return 1.0 if v == 0 else E0 * 200 ** (-v / 127)


def smr_t60(v):
    """seconds to -60 dB of a held bin at SMR v."""
    e = smr_e(v)
    return 0.0 if e >= 1 else -3 * HOP / FS / math.log10(1 - e)


def gate_db(v):
    """threshold in dBFS (peak of a sine) of GATE knob v; None = off."""
    return None if v == 0 else GATE_TOP_DB - GATE_RANGE_DB * (127 - v) / 126


def tilt_c(v):
    """(mode, c) of TILT knob v: mode +1 = one zero (treble up), -1 = one pole (bass up); the
    zero / pole sits at b = 1 - c."""
    t = max(-1.0, min(1.0, (v - 64) / 63))
    u = abs(t)
    c = (1 - u) ** 3 * (1 - CMIN) + CMIN
    return (1 if t >= 0 else -1), c


def tilt_db(v, f):
    """the tilt filter's gain in dB at f Hz for knob v (unity at 1 kHz)."""
    mode, c = tilt_c(v)
    b = 1 - c
    w = 2 * math.pi * f / FS
    mag = lambda ww: math.sqrt(1 - 2 * b * math.cos(ww) + b * b)
    g = mag(w) / mag(2 * math.pi * F_PIVOT / FS)
    return 20 * math.log10(g) * mode


# ---------------------------------------------------------------- code fragments
def body(text):
    return [l.rstrip() for l in text.strip('\n').splitlines()]


def ninstr(lines):
    return sum(1 for l in lines if l.strip() and not l.strip().startswith(';') and not re.match(r'^\w+:', l))


def R(text):
    """{NAME} -> x:(r7+$nn)"""
    return re.sub(r'\{(\w+)\}', lambda m: f'x:(r7+${S[m[1]]:02x})', text)


DIF = body('''
        move    y:(r0),a
        move    y:(r1)+,x0
        add     x0,a
        asr     a
        move    a,y:(r0)+
        sub     x0,a
        move    a,x0
        move    y:(r0),a
        move    y:(r1)-,x1
        add     x1,a
        asr     a
        move    a,y:(r0)+
        sub     x1,a
        move    a,x1
        mpy     y0,x0,a
        macr    y1,x1,a
        move    a,y:(r1)+
        mpy     x1,y0,a
        macr    -x0,y1,a
        move    a,y:(r1)+
''')
DIT = body('''
        move    y:(r1)+,x0
        move    y:(r1)-,x1
        mpy     y0,x0,b
        macr    -y1,x1,b
        move    y:(r0),a
        sub     b,a
        addl    a,b
        move    b,y:(r0)+
        move    a,y:(r1)+
        mpy     x1,y0,b
        macr    x0,y1,b
        move    y:(r0),a
        sub     b,a
        addl    a,b
        move    b,y:(r0)+
        move    a,y:(r1)+
''')
def strided(b):
    """the same butterfly with its last top / bottom stores stepping to the next group (n0, n1)."""
    out, top, bot = list(b), [i for i, l in enumerate(b) if l.endswith(',y:(r0)+')], [i for i, l in enumerate(b) if l.endswith(',y:(r1)+')]
    out[top[-1]] += 'n0'
    out[bot[-1]] += 'n1'
    return out


TWLOAD = body('''
        move    y:(r2)+,y0
        move    y:(r2)+n2,y1
''')
# forward last two passes (h = 2, 1) of one group of four points, halving twice; r0 w0/w4, r1 w2/w6,
# r2 w1/w5, r3 w3/w7, n = 4: each pointer reads low, reads high, writes low, writes high (-> next group)
FR4 = body('''
        move    y:(r0)+n0,a
        move    y:(r0)-n0,x0
        add     x0,a
        asr     a
        move    a,x1
        sub     x0,a
        move    a,y0
        move    y:(r1)+n1,a
        move    y:(r1)-n1,x0
        add     x0,a
        asr     a
        move    a,y1
        sub     x0,a
        move    a,x0
        move    x1,a
        add     y1,a
        asr     a
        move    a,y:(r0)+n0
        sub     y1,a
        move    a,y:(r1)+n1
        move    y:(r3)+n3,a
        move    y:(r3)-n3,x1
        add     x1,a
        asr     a
        move    a,y1
        sub     x1,a
        move    y0,b
        move    a,x1
        add     x1,b
        asr     b
        move    b,y:(r0)+n0
        sub     x1,b
        move    b,y:(r1)+n1
        move    y:(r2),a
        move    y:(r2)+n2,b
        move    y:(r2)-n2,x1
        add     x1,a
        asr     a
        sub     x1,b
        asr     b
        add     y1,a
        asr     a
        move    a,y:(r2)+n2
        sub     y1,a
        move    a,y:(r3)+n3
        add     x0,b
        asr     b
        move    b,y:(r3)+n3
        sub     x0,b
        move    b,y:(r2)+n2
''')
# inverse first two passes (h = 1, 2), unscaled, same pointers
IR4 = body('''
        move    y:(r0),a
        move    y:(r0)+n0,b
        move    y:(r1)+n1,x0
        add     x0,a
        sub     x0,b
        move    a,x1
        move    b,y0
        move    y:(r0),a
        move    y:(r0)-n0,b
        move    y:(r1)-n1,x0
        add     x0,a
        sub     x0,b
        move    b,x0
        move    x1,b
        sub     a,b
        addl    b,a
        move    a,y:(r0)+n0
        move    b,y:(r0)+n0
        move    y:(r2),a
        move    y:(r2)+n2,b
        move    y:(r3)+n3,x1
        add     x1,a
        sub     x1,b
        move    y:(r3)-n3,x1
        move    y:(r2)-n2,y1
        add     x0,b
        move    b,y:(r3)+n3
        sub     x0,b
        sub     x0,b
        move    b,y:(r3)+n3
        move    y1,b
        add     x1,b
        sub     b,a
        addl    a,b
        move    b,y:(r2)+n2
        move    a,y:(r2)+n2
        move    y1,a
        sub     x1,a
        move    y0,b
        sub     a,b
        move    b,y:(r1)+n1
        addl    b,a
        move    a,y:(r1)+n1
''')
# one side of a bin pair, from its two parts in a (re) and b (im), to its wash magnitude 2W in x0 and
# its live gain in y1; H at (r3)+
SIDE = body(R('''
        abs     a
        abs     b
        move    a,y0
        add     b,a
        cmp     y0,b
        tlt     y0,b
        sub     b,a
        move    a,y0
        move    b,x1
        move    MB0Q,y1
        mac     y1,y0,b
        move    MA1Q,y1
        mpy     y1,x1,a
        move    MB1Q,y1
        mac     y1,y0,a
        cmp     a,b
        tlt     a,b
        move    {THR},x0
        cmp     x0,b
        move    #0,x1
        tlt     x1,b
        move    {GL8},a
        tlt     x1,a
        move    a,y1
        move    y:(r3),x0
        move    x0,a
        move    {DITH},a0
        move    {EDEC},x1
        mac     -x1,x0,a
        move    b,y0
        cmp     y0,a
        tlt     y0,a
        move    a,y:(r3)+
        move    {GL},x1
        mac     -x1,y0,a
        asr     #2,a,a
        move    a,x0
''').replace('MB0Q', q(MB0)).replace('MA1Q', q(MA1)).replace('MB1Q', q(MB1)))
PAIR = body('''
        move    y:(r1)+,a
        move    y:(r2),x0
        sub     x0,a
        move    a,y:(r2)+
        add     x0,a
        add     x0,a
        move    y:(r1),b
        move    y:(r2),x1
        add     x1,b
        move    b,y:(r2)-
        sub     x1,b
        sub     x1,b
        move    b,y:(r1)-
        move    a,y:(r1)
''') + SIDE + body('''
        move    y:(r5)+,y0
        move    y:(r1),x1
        mpy     y1,x1,a
        macr    y0,x0,a
        move    a,y:(r1)+
        move    y:(r5)+n5,y0
        move    y:(r1),x1
        mpy     y1,x1,a
        macr    y0,x0,a
        move    a,y:(r1)-
        move    y:(r2)+,a
        move    y:(r2)-,b
''') + SIDE + body('''
        move    y:(r6)-,y0
        move    y:(r2),x1
        mpy     y1,x1,a
        macr    -y0,x0,a
        move    y:(r1),b
        sub     a,b
        addl    b,a
        move    a,y:(r1)+
        move    b,y:(r2)+
        move    y:(r6)+n6,y0
        move    y:(r2),x1
        mpy     y1,x1,a
        macr    y0,x0,a
        move    y:(r1),b
        sub     b,a
        addl    a,b
        move    b,y:(r1)+
        move    a,y:(r2)-n2
''')


def cin(sign):
    return body(f'''
        move    y:(r3)+n3,x1
        move    y1,a
        mac     {sign}y1,x1,a
        move    a,y0
        move    y:(r4)+,x0
        mpyr    y0,x0,a
        move    a,y:(r2)+
        move    y:(r4)+,x0
        mpyr    y0,x0,a
        move    a,y:(r2)+
''')


def ola(sign):
    return body(f'''
        move    y:(r3)+n3,y0
        move    y:(r2)+,x0
        move    y:(r5),a
        mac     {sign}y0,x0,a
        add     x0,a
        move    a,y:(r5)+
        move    y:(r2)+,x0
        move    y:(r5),a
        mac     {sign}y0,x0,a
        add     x0,a
        move    a,y:(r5)+
''')


LASTQ = body('''
        move    y:(r3)+n3,y0
        move    y:(r2)+,x0
        mpy     y0,x0,a
        add     x0,a
        move    a,y:(r5)+
        move    y:(r2)+,x0
        mpy     y0,x0,a
        add     x0,a
        move    a,y:(r5)+
''')
FIR = body(R('''
        move    x:(r0),x0
        move    x0,y:(r4)+n4
        move    {CD},y1
        mpy     x0,y1,b
        move    y:(r5)+n5,x0
        mpy     y0,x0,a
        move    {CB},y1
        mac     -y1,x1,a
        move    x0,x1
        asl     #5,a,a
        add     b,a
        move    a,x:(r0)+n0
'''))
IIR = body(R('''
        move    x:(r0),x0
        move    x0,y:(r4)+n4
        move    {CD},y1
        mpy     x0,y1,b
        move    y:(r5)+n5,x0
        mpy     y0,x0,a
        asl     #2,a,a
        move    {CB},y1
        mac     y1,x1,a
        move    a,x1
        add     b,a
        move    a,x:(r0)+n0
'''))

# ---------------------------------------------------------------- the schedule's weights (instructions)
W_FWD = UNIT * (ninstr(DIF) + 2) + 40
W_INV = UNIT * (ninstr(DIT) + 2) + 40
W_FWA = UNIT * ninstr(DIF) + 70      # twiddle-outer passes (h = 8 .. 64): the twiddle is loaded once a column
W_INA = UNIT * ninstr(DIT) + 70
PA = 4                             # of the 8 passes
W_FR4 = R4G * ninstr(FR4) + 25
W_IR4 = R4G * ninstr(IR4) + 25
W_PAIR = ninstr(PAIR)
W_BOV = 60
W_OLA = OLP * ninstr(ola('-')) + 35
W_MAND = 256 * ninstr(LASTQ) + N * ninstr(cin('-')) + 330
PASSES = 8                         # h = 512 .. 4 (h = 2, 1 are the radix-4 stage)
UNITS = ([('fwd', W_FWD)] * (3 * (N // 2) // UNIT) + [('fwa', W_FWA)] * (PA * (N // 2) // UNIT) + [('fwd', W_FWD)] * ((N // 2) // UNIT)
         + [('fr4', W_FR4)] * (N // 4 // R4G)
         + [('bin', W_BOV + n * W_PAIR) for n in (1, 2, 4, 8)] + [('bin', W_BOV + BPAIR * W_PAIR)] * ((511 - 15) // BPAIR)
         + [('ir4', W_IR4)] * (N // 4 // R4G) + [('inv', W_INV)] * ((N // 2) // UNIT) + [('ina', W_INA)] * (PA * (N // 2) // UNIT)
         + [('inv', W_INV)] * (3 * (N // 2) // UNIT)
         + [('ola', W_OLA)] * (768 // OLP))
BIAS = 700
TOTAL = W_MAND + sum(w for _, w in UNITS)
BF = -(-(TOTAL + BIAS) // HOP)     # budget granted a frame


def schedule(frames=16):
    """instructions of schedule work in each of the hop's calls (weights, not measurements)."""
    out, budget, i = [], -(W_MAND + BIAS), 0
    for c in range(HOP // frames):
        cost = W_MAND if c == 0 else 0
        budget += frames * BF
        while budget >= 0 and i < len(UNITS):
            cost += UNITS[i][1]
            budget -= UNITS[i][1]
            i += 1
        if budget >= 0:
            budget = -1
        out.append(cost)
    assert i == len(UNITS), 'the hop does not finish its job'
    return out


def tw_levels():
    out = []
    for b in range(8, -1, -1):
        ang = math.pi * (1 << b) / 512
        out.append((b, math.cos(ang), math.sin(ang), 1 << (8 - b), (1 << (b + 2)) - 1, 1 << (b + 1)))
    return out


def build():
    L = []
    A = L.append

    def T(text):
        for l in body(R(text)):
            L.append(l)

    def lin(*regs):
        for r in regs:
            A(f'        move    #>$ffffff,m{r}')

    hdr = f'''; AERIA SMEAR -- STFT spectral smear / freeze / gate (generated by spectral/gen.py: edit that).
;
; N = 1024, hop 256 (4x), Hann analysis x Hann synthesis (sum 1.5), true stereo through ONE complex
; FFT each way: z = L + jR; bins k and N-k give X_L = (Z[k] + conj Z[N-k]) / 2 and
; X_R = (Z[k] - conj Z[N-k]) / 2j; each side is processed on its own, Z'[k] = A + jB,
; Z'[N-k] = conj A + j conj B, and one inverse FFT returns L in the real part and R in the imaginary.
;
; No bit reversal anywhere: the forward transform is decimation in frequency (natural order in,
; bit-reversed out, halving every pass), the inverse is decimation in time (bit-reversed in, natural
; out, unscaled). In bit-reversed order the partner of position p inside the block [2^t, 2^(t+1)) is
; its mirror in that block, so the pairs are walked with one pointer up and one down, block by block
; (511 pairs; positions 0 and 1 = DC and Nyquist only take the live gain). Passes h = 2, 1 are one
; multiplier-free radix-4 stage each way; passes h = 8..64 run twiddle by twiddle (the twiddle is
; loaded once for its column of butterflies, the work buffer addressed modulo 2048), the others
; butterfly by butterfly (twiddle pointer modulo 1024).
;
; Per side and bin: m = max(max + 5/32 min, 27/32 max + 71/128 min) of |re|, |im| (within 1.2 %); gate: m < THR -> the bin is out (m = 0, live
; gain 0); H = max(m, floor(H - e H + r)) (e = 1 - decay, from SMR; r in [0, 1) LSB is new every hop:
; the decay is exponential on average down to the last LSB, reaches 0, and holds exactly at e = 0); out = (gl x bin + 2 (H - gl m) x random unit phasor) / 8.
; gl = 1 - FRZ. The wash is twice the magnitude it replaces: frames with fresh random phases add in
; power, 6 dB under the same frames in phase. Full FRZ: gl = 0, e = 0, THR = max: H is not touched.
; Random phasors: a table of 512 (random entries of the twiddle table, random sign), walked every hop
; from a new random start with a new random odd stride; the right side walks the same entries (WIDE 0)
; or its own (WIDE 127); in between the first WIDE/127 of the pairs (bit-reversed order: spread over
; the spectrum) have their own.
; TILT is a first-order filter on the wet output: one zero (treble up) or one pole (bass up) sliding
; from Nyquist down to {F_TILT_LO:g} Hz, gain held at 1 at {F_PIVOT:g} Hz (a Newton step a call on 1/sqrt).
;
; WORK IS SPREAD: one hop's job is cut into units of about 1.3-1.7 k instructions; every call is
; granted {BF} instructions a frame and runs units while its budget is not negative. Only the start of a
; job is fixed to one call (the first of the hop): the last quarter of the previous frame into the
; overlap ring (its slots were being played until now), and the windowed copy-in (the input ring is
; exactly 1,024 frames, so it is read before this call's input goes in). Then: 8 forward passes
; (64 units), radix-4 (8), bins (35), radix-4 (8), 8 inverse passes (64), overlap-add of the first
; three quarters (6), idle. Latency {LATENCY} samples (N + one hop of work). After init the first 3 calls
; clear the buffers and fill the tables (the shared window is not zero at boot); the audio passes dry.
; The unit to run next is an address in the r7 block (jsr (r1)); a unit returns its weight in x0.
; Calls of other than 16 frames are granted in proportion (the stock dispatcher splits blocks).
;
; Per call, after the work: MIX slews 1/128 a call (0 = the block is not touched); the tilt's
; coefficients follow the knob (1/32 of the way a call); then each side's 16 frames: input into the
; ring, wet from the overlap ring through the tilt, x 8/3, + dry x (1 - mix), saturating.
; Uses r0-r6, n0-n6, m0-m6 (left linear), up to 3 levels of the hardware stack below its caller's
; (jsr unit, bsr, do / nested do). An interrupt taken inside finds m0-m6 possibly modulo.
;
; Y memory, {MEM_WORDS} words from the base (X:$213 -> word -> base; must be a multiple of $800), Y only:
;   +$0000 twiddles: 512 x (cos, sin)(pi k / 512), also the Hann window (w = (1 - cos) / 2)
;   +$0400 random unit phasors, 512 x (re, im)          (modulo 1024)
;   +$0800 FFT work, 1024 x (re = L, im = R)
;   +$1000 input ring, 1024 x (L, R)                    (modulo 2048)
;   +$1800 overlap ring, 1024 x (L, R), wet x 3/8        (modulo 2048)
;   +$2000 H, 512 x (left, right) in pair order
; Scale: copy-in is x w / 2, the forward FFT / 1024, a slot is 2 X / 1024 (full-scale sine: 1/4);
; the bins leave at 1/8 of that (18 dB of room for the wash in the inverse FFT and the overlap ring).
;
; r7: {' '.join(f'{i:02x} {n}' for n, i in S.items())}
'''
    L.extend(hdr.rstrip('\n').splitlines())

    # ---------------------------------------------------------------- init
    T('''
init:
        move    #>$ffffff,m4
        move    r7,r4
        clr     a
        do      #>$100,>sqizz
        move    a,x:(r4)+
sqizz:
        move    x:>$213,r4
        nop
        move    x:(r4),a
        move    a1,{BASE}
        add     #>$400,a
        move    a1,{RPB}
        add     #>$400,a
        move    a1,{WKB}
        add     #>$800,a
        move    a1,{IRB}
        move    a1,{WKE}
        add     #>$800,a
        move    a1,{ORB}
        add     #>$800,a
        move    a1,{HHB}
        move    #>sqbta,r1
        move    r1,{UADDR}
''')
    A(f'        move    #>${SEED:06x},a')
    T('''
        move    a1,{RNG}
        move    #>$100000,a
        move    a1,{YN}
        rts
''')
    # ---------------------------------------------------------------- proc
    T('''
proc:
        move    r0,{R0S}
        move    r6,{R6S}
''')
    lin(0, 1, 2, 3, 4, 5, 6)
    T(f'''
        move    {{READY}},a
        jclr    #0,a1,>sqpnj
        move    {{PHASE}},a
        sub     #>{HOP},a
        jset    #23,a1,>sqpnj
        move    a1,{{PHASE}}
        bsr     >sqjob
sqpnj:
        move    n7,x0
        move    #>{BF},y0
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    {{BUDG}},a
        add     x0,a
        move    a1,{{BUDG}}
        jset    #23,a1,>sqwdn
sqwlp:
        move    {{UADDR}},r1
        nop
        jsr     (r1)
        move    {{BUDG}},a
        sub     x0,a
        move    a1,{{BUDG}}
        jclr    #23,a1,>sqwlp
sqwdn:
        move    {{R0S}},r0
        move    {{R6S}},r6
        move    {{READY}},a
        jclr    #0,a1,>sqpend
        move    x:(r6+$5),x0
        move    #>$408102,y0
        mpy     y0,x0,a
        asl     a
        move    a,x0
        move    {{MIXS}},b
        move    x0,a
        sub     b,a
        move    #>$010000,y0
        cmp     y0,a
        tgt     y0,a
        move    #>$ff0000,y0
        cmp     y0,a
        tlt     y0,a
        add     a,b
        move    b1,{{MIXS}}
        move    #>2047,m4
        move    #>2047,m5
        move    {{IRP}},r4
        move    {{ORP}},r5
        tst     b
        bne     >sqwet
        move    n7,a
        asl     a
        move    a1,r1
        move    a1,n5
        do      r1,>sqdry
        move    x:(r0)+,x0
        move    x0,y:(r4)+
sqdry:
        move    (r5)+n5
        bra     >sqpfin
sqwet:
''')
    # tilt: the smoothed knob, its zero / pole, the gain that holds 1 kHz
    T(f'''
        move    {{TAUT}},a
        move    {{TAUS}},b
        sub     b,a
        asr     #5,a,a
        add     a,b
        move    b,x0
        move    x0,{{TAUS}}
        move    x0,b
        abs     b
        move    #>$7fffff,a
        sub     b,a
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        mpy     x1,x0,a
        move    a,x0
        move    {q(1 - CMIN)},y0
        mpy     y0,x0,a
        add     {q(CMIN)},a
        move    a,x0
        move    #>$7fffff,b
        sub     x0,b
        move    b,y1
        mpy     x0,x0,a
        move    {q(KAPPA2)},y0
        mac     y1,y0,a
        move    a,x1
        move    x1,{{XT}}
        move    {{YN}},y0
        mpy     x1,y0,a
        move    a,x0
        mpy     y0,x0,a
        asl     #5,a,a
        move    #>$400000,b
        sub     a,b
        move    b,x0
        move    y0,a
        mac     y0,x0,a
        move    #>$100000,x0
        cmp     x0,a
        tlt     x0,a
        move    #>$720000,x0
        cmp     x0,a
        tgt     x0,a
        move    a,y0
        move    y0,{{YN}}
        move    {{MIXS}},x0
        move    #>$7fffff,a
        sub     x0,a
        move    a1,{{CD}}
        move    {q(2 / 3)},x1
        mpy     x1,x0,a
        move    a,x0
        move    {{TAUS}},a
        jset    #23,a1,>sqtii
        mpy     y0,x0,a
        move    a,x0
        move    x0,{{CG}}
        mpy     x0,y1,a
        move    a1,{{CB}}
        clr     a
        move    a1,{{MODE}}
        bra     >sqtdn
sqtii:
        move    {{XT}},x1
        mpy     x1,y0,a
        asl     #3,a,a
        move    a,x1
        mpy     x1,x0,a
        move    a1,{{CG}}
        move    y1,{{CB}}
        move    #>1,a
        move    a1,{{MODE}}
sqtdn:
        move    #2,n0
        move    #2,n4
        move    #2,n5
        move    {{CG}},y0
        move    {{STL}},x1
        bsr     >sqchn
        move    x1,{{STL}}
        move    {{R0S}},r0
        nop
        move    (r0)+
        move    {{IRP}},r4
        move    {{ORP}},r5
        move    (r4)+
        move    (r5)+
        move    {{STR}},x1
        bsr     >sqchn
        move    x1,{{STR}}
        move    (r4)-
        move    (r5)-
sqpfin:
        move    r4,{{IRP}}
        move    r5,{{ORP}}
        move    n7,x0
        move    {{PHASE}},a
        add     x0,a
        move    a1,{{PHASE}}
''')
    lin(4, 5)
    T('''
sqpend:
        rts
''')
    # one channel of the block: input ring in, tilted wet + dry out
    T('''
sqchn:
        move    {MODE},a
        jset    #0,a1,>sqchi
        do      n7,>sqchf
''')
    L.extend(FIR)
    T('''
sqchf:
        rts
sqchi:
        do      n7,>sqchj
''')
    L.extend(IIR)
    T('''
sqchj:
        rts
''')
    # ---------------------------------------------------------------- the start of a hop's job
    T('''
sqjob:
        bsr     >sqhop
        move    #>2047,m5
        move    {ORP},r5
        move    #>512,n5
        move    {WKB},r2
        move    (r5)-n5
        move    #>1536,n2
        move    {BASE},r3
        move    (r2)+n2
        move    #>512,n3
        nop
        move    (r3)+n3
        move    #2,n3
        do      #>$100,>sqjlq
''')
    L.extend(LASTQ)
    T('''
sqjlq:
        move    {ORP},r5
        nop
        move    (r5)+n5
        move    r5,{OJ}
        move    #>$ffffff,m5
        move    #>2047,m4
        move    {IRP},r4
        move    {WKB},r2
        move    {BASE},r3
        move    #>$200000,y1
        do      #>$200,>sqjca
''')
    L.extend(cin('-'))
    T('''
sqjca:
        move    {BASE},r3
        do      #>$200,>sqjcb
''')
    L.extend(cin(''))
    T(f'''
sqjcb:
        move    #>$ffffff,m4
        move    #>1024,x0
        move    #>2,x1
        move    x0,{{H2}}
        move    x1,{{TSW}}
        bsr     >sqpset
        move    #>sqfwu,r1
        move    r1,{{UADDR}}
        move    #>{-(W_MAND + BIAS) & 0xffffff},a
        move    a1,{{BUDG}}
        rts
''')
    # ---------------------------------------------------------------- the knobs, once a hop
    T(f'''
sqhop:
        move    x:(r6+$1),x0
        move    #>$408102,y0
        mpy     y0,x0,a
        asl     a
        move    a,x0
        move    {{FS}},b
        move    x0,a
        sub     b,a
        asr     a
        add     a,b
        move    b,y0
        move    y0,b
        move    x0,a
        sub     b,a
        abs     a
        cmp     #>$008000,a
        bge     >sqhpa
        move    x0,b
sqhpa:
        move    b1,{{FS}}
        move    b1,x0
        move    #>$7fffff,a
        sub     x0,a
        move    a1,{{GL}}
        asr     #3,a,a
        move    a1,{{GL8}}
        move    x:(r6+$0),x0
        move    #>$7fffff,b
        move    x0,a
        tst     a
        beq     >sqhpb
        move    {q(KSMR)},y0
        mpy     y0,x0,a
        bsr     >sqexp
        move    b,x0
        move    {q(E0)},y0
        mpy     y0,x0,b
sqhpb:
        move    b1,{{EDEC}}
        move    x:(r6+$2),x0
        clr     b
        move    x0,a
        tst     a
        beq     >sqhpc
        move    #>$7f0000,a
        sub     x0,a
        move    a,x0
        move    {q(KGATE)},y0
        mpy     y0,x0,a
        bsr     >sqexp
        move    b,x0
        move    {q(TMAX)},y0
        mpy     y0,x0,b
sqhpc:
        move    {{FS}},a
        cmp     #>$7ff000,a
        blt     >sqhpd
        move    #>$7fffff,b
        clr     a
        move    a1,{{GL}}
        move    a1,{{GL8}}
        move    a1,{{EDEC}}
sqhpd:
        move    b1,{{THR}}
        move    x:(r6+$3),x0
        move    #>516,y0
        mpy     y0,x0,a
        move    a1,{{WTHR}}
        move    x:(r6+$4),a
        sub     #>$400000,a
        asl     a
        move    a,x0
        move    {q(0.5 * 64 / 63)},y0
        mpy     y0,x0,a
        asl     a
        move    a,x0
        move    x0,{{TAUT}}
        move    {{RNG}},x0
        move    #>${LCG:06x},y0
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        lsr     #13,a
        and     #>$3fe,a
        move    {{RPB}},x1
        add     x1,a
        move    a1,{{R5P}}
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        lsr     #13,a
        and     #>$3fc,a
        add     #>1,a
        move    a1,{{N5}}
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        lsr     #13,a
        and     #>$3fe,a
        add     x1,a
        add     #>1,a
        move    a1,{{R6P}}
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        lsr     #13,a
        and     #>$3fc,a
        add     #>3,a
        move    a1,{{N6}}
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,{{DITH}}
        move    x0,{{RNG}}
        rts
''')
    # b = 2^-y for a = y / 32 (y in [0, 32)); clobbers a b x0 x1 y0  (tools/asmgen.py e2_sub)
    T(f'''
sqexp:
        move    a1,b
        asr     #$12,a,a
        add     #>$1,a
        move    a1,x1
        and     #>$03ffff,b
        asl     #$5,b,b
        move    b1,x0
        move    {q(math.log(2) / 2)},y0
        mpy     y0,x0,b
        move    b,y0
        move    {q(1 / 48)},x0
        mpy     y0,x0,b
        sub     {q(1 / 12)},b
        move    b,x0
        mpy     y0,x0,b
        add     {q(0.25)},b
        move    b,x0
        mpy     y0,x0,b
        sub     {q(0.5)},b
        move    b,x0
        mpy     y0,x0,b
        add     {q(0.5)},b
        move    b,x0
        mpy     x0,x0,b
        asl     #$2,b,b
        do      x1,>sqexz
        asr     b
sqexz:
        asl     b
        rts
''')
    def fftunit(name, bfly, weight, nxt):
        T(f'''
{name}u:
        move    #>1023,m2
        move    {{P0}},r0
        move    {{H2}},n0
        move    {{TWP}},r2
        lua     (r0)+n0,r1
        move    {{TSN}},n2
        move    {{SK}},n0
        move    {{UOUT}},r3
        move    {{UIN}},r4
        move    n0,n1
        do      r3,>{name}o
        do      r4,>{name}i
''')
        L.extend(TWLOAD)
        L.extend(bfly)
        T(f'''
{name}i:
        move    (r0)+n0
        move    (r1)+n1
{name}o:
        move    #>$ffffff,m2
        move    r2,{{TWP}}
        move    {{J}},a
        add     #>{UNIT},a
        move    {{JL}},x0
        cmp     x0,a
        bne     >{name}a
        clr     a
        move    {{SK2}},n0
        nop
        move    (r0)+n0
{name}a:
        move    a1,{{J}}
        move    r0,{{P0}}
        move    r0,a
        move    {{WKE}},x0
        cmp     x0,a
        bne     >{name}b
        bsr     >{nxt}
{name}b:
        move    #>{weight},x0
        rts
''')

    def r4unit(name, grp, weight, nxt):
        T(f'''
{name}u:
        move    {{P0}},r0
        move    #4,n0
        lua     (r0+$1),r2
        lua     (r0+$2),r1
        lua     (r0+$3),r3
        move    #4,n1
        move    #4,n2
        move    #4,n3
        do      #>{R4G},>{name}z
''')
        L.extend(grp)
        T(f'''
{name}z:
        move    r0,{{P0}}
        move    r0,a
        move    {{WKE}},x0
        cmp     x0,a
        bne     >{name}b
        bsr     >{nxt}
{name}b:
        move    #>{weight},x0
        rts
''')

    def fftunita(name, bfly, weight, nxt):
        T(f'''
{name}u:
        move    #>2047,m0
        move    #>2047,m1
        move    {{P0}},r0
        move    {{H2}},n0
        move    {{TWP}},r2
        lua     (r0)+n0,r1
        move    {{TSN}},n2
        move    {{SK}},n0
        move    {{UOUT}},r3
        move    {{UIN}},r4
        move    n0,n1
        do      r3,>{name}o
''')
        L.extend(TWLOAD)
        A(f'        do      r4,>{name}i')
        L.extend(strided(bfly))
        T(f'''
{name}i:
        move    (r0)+
        move    (r1)+
        move    (r0)+
        move    (r1)+
{name}o:
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    r2,{{TWP}}
        move    r0,{{P0}}
        move    {{J}},a
        add     #>{UNIT},a
        move    a1,{{J}}
        move    {{JL}},x0
        cmp     x0,a
        bne     >{name}b
        bsr     >{nxt}
{name}b:
        move    #>{weight},x0
        rts
''')

    # ---------------------------------------------------------------- forward
    fftunit('sqfw', DIF, W_FWD, 'sqfnx')
    fftunita('sqfa', DIF, W_FWA, 'sqfnx')
    T('''
sqfnx:
        move    {H2},a
        asr     a
        move    a1,x0
        move    x0,a
        cmp     #>8,a
        bge     >sqfny
        move    #>sqfru,r1
        move    r1,{UADDR}
        move    {WKB},a
        move    a1,{P0}
        rts
sqfny:
        move    x0,{H2}
        move    {TSW},a
        asl     a
        move    a1,x1
        move    x1,{TSW}
        bsr     >sqpset
        move    #>sqfwu,r1
        move    #>sqfau,r2
        tst     b
        beq     >sqfnz
        move    r2,r1
sqfnz:
        move    r1,{UADDR}
        rts
''')
    r4unit('sqfr', FR4, W_FR4, 'sqbni')
    # ---------------------------------------------------------------- bins
    T('''
sqbni:
        move    {WKB},r1
        move    {GL8},y0
        do      #4,>sqbnj
        move    y:(r1),x0
        mpy     y0,x0,a
        asl     a
        move    a,y:(r1)+
sqbnj:
        move    r1,{BP}
        move    (r1)+
        move    (r1)+
        move    r1,{BQ}
        move    #>4,a
        move    a1,{BS}
        move    #>1,a
        move    a1,{BREM}
        clr     a
        move    a1,{BDONE}
        move    {HHB},a
        move    a1,{BH}
        move    #>sqbin,r1
        move    r1,{UADDR}
        rts
sqbin:
        move    #>1023,m5
        move    #>1023,m6
        move    {BREM},a
        move    #>16,x0
        cmp     x0,a
        tgt     x0,a
        move    a1,r4
        move    {BP},r1
        move    {BQ},r2
        move    #3,n2
        move    {BH},r3
        move    {R5P},r5
        move    {N5},n5
        move    {BDONE},a
        move    {WTHR},x0
        cmp     x0,a
        bge     >sqbnm
        move    {R6P},r6
        move    {N6},n6
        bra     >sqbng
sqbnm:
        lua     (r5+$1),r6
        move    n5,a
        add     #>2,a
        move    a1,n6
sqbng:
        do      r4,>sqbnz
''')
    L.extend(PAIR)
    T(f'''
sqbnz:
        move    #>$ffffff,m5
        move    #>$ffffff,m6
        move    r1,{{BP}}
        move    r2,{{BQ}}
        move    r3,{{BH}}
        move    r5,{{R5P}}
        move    r6,{{R6P}}
        move    r4,x0
        move    {{BDONE}},a
        add     x0,a
        move    a1,{{BDONE}}
        move    {{BREM}},a
        sub     x0,a
        move    a1,{{BREM}}
        tst     a
        bne     >sqbnw
        move    {{BS}},a
        asl     a
        move    a1,x1
        move    x1,{{BS}}
        cmp     #>2048,a
        bne     >sqbnx
        move    #>sqiru,r1
        move    r1,{{UADDR}}
        move    {{WKB}},a
        move    a1,{{P0}}
        bra     >sqbnw
sqbnx:
        move    {{WKB}},a
        add     x1,a
        move    a1,{{BP}}
        add     x1,a
        sub     #>2,a
        move    a1,{{BQ}}
        move    x1,a
        asr     #2,a,a
        move    a1,{{BREM}}
sqbnw:
        move    #>{W_PAIR},y0
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        add     #>{W_BOV},a
        move    a1,x0
        rts
''')
    # ---------------------------------------------------------------- inverse
    r4unit('sqir', IR4, W_IR4, 'sqini')
    T('''
sqini:
        move    #>8,x0
        move    #>256,x1
        move    x0,{H2}
        move    x1,{TSW}
        bsr     >sqpset
        move    #>sqivu,r1
        move    r1,{UADDR}
        rts
''')
    fftunit('sqiv', DIT, W_INV, 'sqinx')
    fftunita('sqia', DIT, W_INA, 'sqinx')
    T('''
sqinx:
        move    {H2},a
        asl     a
        move    a1,x0
        move    x0,a
        cmp     #>1025,a
        blt     >sqiny
        move    {OJ},a
        move    a1,{OP}
        move    {WKB},a
        move    a1,{OW}
        move    {BASE},a
        move    a1,{OT}
        move    #>4,a
        move    a1,{OCNT}
        move    #>sqoau,r1
        move    r1,{UADDR}
        rts
sqiny:
        move    x0,{H2}
        move    {TSW},a
        asr     a
        move    a1,x1
        move    x1,{TSW}
        bsr     >sqpset
        move    #>sqivu,r1
        move    #>sqiau,r2
        tst     b
        beq     >sqinz
        move    r2,r1
sqinz:
        move    r1,{UADDR}
        rts
''')
    # ---------------------------------------------------------------- pass set-up: x0 = 2h (words), x1 = twiddle step (words)
    T('''
sqpset:
        move    x1,a
        sub     #>1,a
        move    a1,{TSN}
        move    x0,a
        asr     a
        move    a1,y0
        move    #>64,y1
        move    y0,a
        cmp     y1,a
        tgt     y1,a
        move    a1,{UIN}
        move    y0,a
        cmp     y1,a
        tlt     y1,a
        move    a1,{JL}
        clr     b
        move    x0,a
        cmp     #>129,a
        bge     >sqpsb
        cmp     #>16,a
        bge     >sqpsd
        move    x0,{SK}
        clr     a
        move    a1,{SK2}
        move    x1,a
        asr     #4,a,a
        move    a1,{UOUT}
        bra     >sqpsc
sqpsd:
        asl     a
        sub     #>1,a
        move    a1,{SK}
        move    x0,a
        asr     #4,a,a
        move    a1,{UOUT}
        move    x1,a
        asr     a
        move    a1,{UIN}
        move    #>512,a
        move    a1,{JL}
        move    #>1,b
        bra     >sqpsc
sqpsb:
        clr     a
        move    a1,{SK}
        move    x0,{SK2}
        move    #>1,a
        move    a1,{UOUT}
sqpsc:
        move    {WKB},a
        move    a1,{P0}
        clr     a
        move    a1,{J}
        move    {BASE},a
        move    a1,{TWP}
        rts
''')

    # ---------------------------------------------------------------- overlap-add, first three quarters

    def olaunit(name, sign, after):
        T(f'''
{name}u:
        move    #>2047,m5
        move    {{OP}},r5
        move    {{OW}},r2
        move    {{OT}},r3
        move    #2,n3
        do      #>{OLP},>{name}z
''')
        L.extend(ola(sign))
        T(f'''
{name}z:
        move    #>$ffffff,m5
        move    r5,{{OP}}
        move    r2,{{OW}}
        move    r3,{{OT}}
        move    {{OCNT}},a
        sub     #>1,a
        move    a1,{{OCNT}}
        tst     a
        bne     >{name}b
''')
        T(after)
        T(f'''
{name}b:
        move    #>{W_OLA},x0
        rts
''')

    olaunit('sqoa', '-', '''
        move    {BASE},a
        move    a1,{OT}
        move    #>2,a
        move    a1,{OCNT}
        move    #>sqobu,r1
        move    r1,{UADDR}
''')
    olaunit('sqob', '', '''
        move    #>sqidl,r1
        move    r1,{UADDR}
''')
    # ---------------------------------------------------------------- boot: clear, twiddles, random phasors
    T(f'''
sqbta:
        move    {{WKB}},r1
        move    #>{END - WK},x0
        clr     a
        do      x0,>sqbtz
        move    a,y:(r1)+
sqbtz:
        move    #>sqbtb,r1
        move    r1,{{UADDR}}
        bra     >sqidl
sqbtb:
        move    {{BASE}},r4
        move    #>$7fffff,a
        move    a,y:(r4)+
        clr     a
        move    a,y:(r4)-
''')
    for b, c, s, cnt, step, off in tw_levels():
        T(f'''
        move    {q(c)},y0
        move    {q(s)},y1
        move    #>{cnt},r3
        move    #>{step},n1
        move    #>{off},n2
        bsr     >sqtwl
''')
    T(f'''
        move    #>sqbtc,r1
        move    r1,{{UADDR}}
        bra     >sqidl
sqtwl:
        move    r4,r2
        move    r4,r1
        move    (r2)+n2
        move    n1,n2
        do      r3,>sqtwz
        move    y:(r1)+,x0
        move    y:(r1)+n1,x1
        mpy     y0,x0,a
        macr    -y1,x1,a
        move    a,y:(r2)+
        mpy     x1,y0,a
        macr    x0,y1,a
        move    a,y:(r2)+n2
sqtwz:
        rts
sqbtc:
        move    {{BASE}},r1
        move    {{RPB}},r3
        lua     (r1+$1),r2
        move    {{RNG}},x0
        move    #>${LCG:06x},y0
        do      #>$200,>sqbcz
        mpy     y0,x0,a
        asr     a
        move    a0,x0
        move    x0,a
        lsr     #13,a
        and     #>$3fe,a
        move    a1,n1
        move    a1,n2
        move    y:(r1+n1),a
        jclr    #23,x0,>sqbcp
        neg     a
sqbcp:
        move    a,y:(r3)+
        move    y:(r2+n2),a
        jclr    #23,x0,>sqbcq
        neg     a
sqbcq:
        move    a,y:(r3)+
        nop
        nop
sqbcz:
        move    x0,{{RNG}}
        move    #>1,a
        move    a1,{{READY}}
        move    #>{HOP},a
        move    a1,{{PHASE}}
        move    {{IRB}},a
        move    a1,{{IRP}}
        move    {{ORB}},a
        move    a1,{{ORP}}
        move    #>sqidl,r1
        move    r1,{{UADDR}}
sqidl:
        move    {{BUDG}},a
        add     #>1,a
        move    a1,x0
        rts
''')
    return L


def check(lines):
    labels = [m[1] for l in lines if (m := re.match(r'^(\w+):', l))]
    assert len(labels) == len(set(labels)), 'duplicate label'
    code = [l.split(';')[0] for l in lines if not l.startswith(';')]
    for a in labels:
        for b in labels:
            assert a == b or a not in b, f'label {a} is inside label {b}'
    for l in code:
        m = re.match(r'\s+(mpy|mac|mpyr|macr)\s+-?(\w\w),(\w\w),', l)
        if m:
            assert (m[2], m[3]) in {('x0', 'x0'), ('y0', 'y0'), ('x1', 'x0'), ('y1', 'y0'), ('x0', 'y1'),
                                    ('y0', 'x0'), ('x1', 'y0'), ('y1', 'x1')}, l
        assert not re.match(r'\s+rep\b', l), l
    return labels


SRC = build()
LABELS = check(SRC)
TEXT = '\n'.join(SRC) + '\n'

if __name__ == '__main__':
    out = Path(__file__).resolve().parent / 'spectral.asm'
    out.write_text(TEXT)
    sch = schedule()
    print(f'wrote {out}: {ninstr(SRC)} instructions, {USED_X} r7 words, {MEM_WORDS} Y words')
    print(f'weights: fwd {W_FWD} fr4 {W_FR4} pair {W_PAIR} ir4 {W_IR4} inv {W_INV} ola {W_OLA} mand {W_MAND}; '
          f'{len(UNITS)} units, total {TOTAL}, {BF} a frame')
    print('schedule a call:', sch, 'max/mean', max(sch) / (sum(sch) / len(sch)))
