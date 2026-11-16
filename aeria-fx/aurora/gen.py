#!/usr/bin/env python3
"""Generates aurora.asm and aurora_test.asm (AERIA AURORA: the Balloon Shop's aurora,
MoreWeather.h `Aurora`, in fixed point).

    python3 aurora/gen.py          (host, no dependencies)   -- test.py imports this and rebuilds

aurora.asm       the hardware build: the ring's base is the word X:$213 points to, used as an X address
aurora_test.asm  the same source with the ring at X:$10000 (dsp_host hands out a Y base); one line differs

HALF RATE (13 Nov 2026; the full-rate build is gen.py.prehalf), as PT2399: a polyphase IIR halfband
(halfband.py, 5 allpasses, >= 62 dB from 12.05 kHz) decimates (L + R)/4 to 22.05 kHz, the whole aurora
(anti-alias low-pass, octave window, highpass, four lines, damping, curtains) runs a sample every second
frame, and an identical halfband interpolates the two curtain sums back to 44.1 kHz; only the halfbands and
the dry + wet sum run at 44.1 kHz. The ring (window + lines, the same times) now fits 4,096 words; the
halfbands' block sits at +$1000 of the same 8,192-word region. The wet ends at ~10.5 kHz (-3 dB at 11 kHz).
The anti-alias low-pass keeps its coefficient words, so at 22.05 kHz every one of its frequencies halves:
the shimmer's top (within 1 dB) is 8 kHz out at +12, 7.5 kHz at +19, 7 kHz at +24, and what would fold back
under 8 kHz is >= 37 dB down (the full-rate build: 16 / 15 / 14 kHz, folds under 16 kHz).
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import wxlib as W  # noqa: E402
from wxlib import q, n  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import halfband as HB  # noqa: E402
from asmgen import ins  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = W.FS                   # the knobs', the input's and the output's rate (a call: 16 frames)
FSH = FS / 2                # the core's rate (a call: 8 core frames)
CORE_FRAMES = 8

# ---- sizes: ONE ring of 4,096 X words holds the octave window and the four lines (core frames) -----------
REGION = 8192               # the X region (unchanged): the ring, then the halfbands' block at +$1000
RING = 4096
WINDOW = 1102               # core frames (49.98 ms, the full-rate build's 2,204 frames; the plugin's 0.05 s)
HALF = WINDOW // 2
TAP0 = 2
PITCH = WINDOW + 4          # words kept for the window at the ring's head
PLUGIN_MS = (61.3, 79.9, 97.1, 113.7)
PLUGIN_LEN = tuple(int(ms * 0.001 * FS) for ms in PLUGIN_MS)            # 2703 3523 4282 5014 at 44.1 kHz
LEN = (521, 677, 826, 965)          # core frames (23.6 30.7 37.5 43.8 ms): x 0.3851 in time, pairwise coprime
                                    # (the full-rate build: 1039 1361 1651 1931 frames, the same times)
SCALE = sum(LEN) / FSH / (sum(PLUGIN_LEN) / FS)
WR = (PITCH, PITCH + LEN[0], PITCH + LEN[0] + LEN[1], PITCH + LEN[0] + LEN[1] + LEN[2])
RD = tuple(w + l for w, l in zip(WR, LEN))
assert RD[3] <= RING - 1
# the halfbands' block at +HB_AT (r2 in the frames): the pair's parity, coefficients and states in the order the
# paths run. Even frame: the decimator's A1 path, then the interpolator's A1 paths (L, R) on the last core frame's
# curtains; odd frame: the decimator's A0 path (d = (A0 + A1)/2), the core, the interpolator's A0 paths (L, R).
HB_AT = 0x1000
HB_PAR = 0
HB_CE = 1                               # A1 coefficients x 3 (decimator, L, R)
HB_CO = HB_CE + 3 * HB.K1               # A0 coefficients x 3
HB_SE = HB_CO + 3 * HB.K0               # A1 states x 3 (K1 + 1 words each)
HB_SO = HB_SE + 3 * (HB.K1 + 1)         # A0 states x 3
HB_END = HB_SO + 3 * (HB.K0 + 1)
HB_P1 = HB_SE + HB.K1                   # the decimator's A1 output (its chain's last word)
assert HB_END <= 63 and RING <= HB_AT and HB_AT + HB_END <= REGION
CLR_WORDS = 256
CLR_BLOCKS = (HB_AT + CLR_WORDS) // CLR_WORDS     # the ring, then the 256 words holding the halfbands' block
TEST_XRING = 0x10000

# ---- the plugin's constants ------------------------------------------------------------------------------
T60 = 9.0                   # seconds (TIME 64)
T60_OCT = 32.0              # TIME: T60 = 9 s x 2^((v - 64)/32)
DAMP_HZ = 6000.0            # TONE 64 (the plugin's lowpass in its full-length loop)
PLUG_OCT = 24.0             # TONE: the plugin's lowpass 6 kHz x 2^((v - 64)/24), held to 11.65 kHz (the display's top)
PLUG_TOP = 11650.0


def _loop_hz(f):
    """the loop's one-pole at 22.05 kHz that loses, a turn of lines 0.3851 x as long, 0.3851 x what the plugin's
    one-pole at f (44.1 kHz) loses a turn of its own: (1 - a)/a^2 w^2 scaled (w doubles at half rate)."""
    ap = 1 - math.exp(-2 * math.pi * f / FS)
    t = SCALE * (1 - ap) / ap ** 2 / 4
    a = (-1 + math.sqrt(1 + 4 * t)) / (2 * t)
    return -math.log(1 - a) * FSH / (2 * math.pi)


def _tone_fit():
    """the loop's lowpass as F64 x 2^((v - 64)/TONE_OCT): least squares in log over the plugin's 2 .. 11.65 kHz
    (the full-rate build: 1.545 x the plugin's, 24 a doubling)."""
    xs = [(v - 64) / PLUG_OCT for v in range(23, 88)]
    ys = [math.log2(_loop_hz(DAMP_HZ * 2 ** x)) for x in xs]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    g = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    return 2 ** (my - g * mx), PLUG_OCT / g


LOOP_HZ64, TONE_OCT = _tone_fit()       # ~7.9 kHz at TONE 64, ~28 knob steps a doubling
DAMP_UP = LOOP_HZ64 / DAMP_HZ
TONE_TOP = LOOP_HZ64 * 2 ** (PLUG_OCT * math.log2(PLUG_TOP / DAMP_HZ) / TONE_OCT)   # where the plugin's is held
HP_HZ, HP_Q = 500.0, 0.707
SV_F = 2 * math.sin(math.pi * HP_HZ / FSH)
AMT_TAU = 1.2
CUR_TAU = 1.5
CUR_TICK = 4 * W.DT         # each curtain is looked at every 4th call (1.45 ms; the plugin: 64 samples)
PANS = tuple(p * 0.7071 for p in (-0.85, -0.3, 0.3, 0.85))
LEVEL = 0.45
LSHIFT = 2                  # the lines run at 1/4 of the plugin's level (a 9 s tail stacks up 5 x)
QUIET = 512                              # LSB (-84 dBFS; the plugin: 1e-5 = 84 LSB a curtain, under this port's
                                         # rounding floor)
QUIET_BLOCKS = round(0.5 / W.DT)
RIDE_CEIL = 0.4            # the lines' level (their damped reads, line scale) over which the injection is ridden down
RIDE_DOWN = 1 / 4           # a call while over (tau 1.3 ms)
RIDE_UP_SHIFT = 10          # back up 2^-10 a call (0.37 s) while under
AM_ZERO = round(1e-4 * (1 << 23))
STEPS = ('UNIS', '+12', '+19', '+24')    # SHFT: the read runs 1 x .. 4 x the write (whole frames: no interpolation)

# ---- anti-alias low-pass (13 Nov 2026, "stop aliasing"; the Hz below are at 44.1 kHz: at the core's 22.05 kHz the
# same words put every frequency at half, so +12 keeps 4 kHz in / 8 kHz out and folds under 8 kHz are >= 37 dB down): the window is read 2 / 3 / 4 x as fast as it is written,
# so what sits above fs / (2 x ratio) folds back down. (L + R) goes through an order-3 elliptic-type low-pass first
# (a biquad with a pair of zeros on the unit circle, then a real pole with a zero at Nyquist), designed per SHFT so
# that everything which would land below 16 kHz is >= 37 dB down while the shimmer keeps its top to within 1 dB:
# +12 up to 16 kHz out (8 kHz in), +19 to 15 kHz out, +24 to 14 kHz out. UNIS: no filter (bit for bit as before).
# AA[ratio] = (pole radius logit, pole Hz, zero Hz, real-pole logit): from a minimax search (scratch design script)
AA = {2: (1.52483816, 7814.82563, 14882.0525, 0.0412253595),
      3: (1.92847516, 4847.52177, 10261.7512, 0.742778404),
      4: (2.26082686, 3378.77955, 7821.42421, 1.19737751)}


def aa_coefs(ratio):
    """-> (B, A1H, A2, CH, P, K) as fractions: w = B d + 2 A1H w1 + A2 w2; u = w + 2 CH w1 + K w2;
    v = u + P v1; y = v + K v1 = H d (H: unity at DC), d the decimated (L + R)/4, so the window holds y =
    filtered (L + R)/2, halved. (Designed at 44.1 kHz; it runs at 22.05 kHz: every frequency halves.)"""
    if ratio == 1:
        return (1.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    lr, fp, fz, lp = AA[ratio]
    rr = 1 / (1 + math.exp(-lr))
    a1 = -2 * rr * math.cos(2 * math.pi * fp / FS)
    a2 = rr * rr
    c = -2 * math.cos(2 * math.pi * fz / FS)
    p = 1 / (1 + math.exp(-lp))
    g = (1 + a1 + a2) / (2 + c) * (1 - p) / 2
    return (g, -a1 / 2, -a2, c / 2, p, 1.0)


def aa_words(ratio):
    """the same as the DSP holds them (Q23 words, K = 1 as $7fffff)"""
    return tuple(min((1 << 23) - 1, max(-(1 << 23), round(v * (1 << 23)))) for v in aa_coefs(ratio))


def plugin_hz(v):
    """the plugin's lowpass for TONE v (the knob's meaning)."""
    return min(PLUG_TOP, DAMP_HZ * 2 ** ((v - 64) / PLUG_OCT))


def t60(v):
    return T60 * 2 ** ((v - 64) / T60_OCT)


def tone_hz(v):
    """The loop lowpass's own frequency at 22.05 kHz (a = 1 - e^-w, w = 2 pi f / 22.05 kHz; past 11 kHz it only
    names the coefficient)."""
    return min(TONE_TOP, LOOP_HZ64 * 2 ** ((v - 64) / TONE_OCT))


def line_gain(j, v=64):
    return 10 ** (-3 * LEN[j] / FSH / t60(v))


def pan_gains(p):
    a = (0.5 + 0.5 * p) * math.pi / 2
    return math.cos(a) * math.sqrt(2), math.sin(a) * math.sqrt(2)


ST = W.State('ST XB RP AMH AML RNG C STEP WW DW SVL SVB INJ WL WR LP0 LP1 LP2 LP3 G0 G1 G2 G3 '
             'CL0 CL1 CL2 CL3 CR0 CR1 CR2 CR3 CH0 CH1 CH2 CH3 CO0 CO1 CO2 CO3 CT0 CT1 CT2 CT3 '
             'CF0 CF1 CF2 CF3 QT BLK DAMP RT TLAST OLAST RIDE AR FB FA1 FA2 FC FP FK FW1 FW2 FV1')
AAW = ('FB', 'FA1', 'FA2', 'FC', 'FP', 'FK')


def per_curtain(j):
    """curtain j's stub: r4 = r7 + j (the curtain arrays are four words apart), its pan levels and decay constant
    in x1 y1 n4, then the shared body aucurc (one copy of it: the code is smaller, the arithmetic the same)"""
    gl, gr = pan_gains(PANS[j])
    kg = 3 * math.log2(10) * LEN[j] / FSH / (T60 * 2 ** (-64 / T60_OCT)) / 32     # y/32 at TIME 0
    return (f'aucur{j}:\n'
            f'        lua     (r7+${j:x}),r4           ; curtain {j}\n'
            f'        move    {q(gl / 2)},x1\n'
            f'        move    {q(gr / 2)},y1\n'
            f'        move    {q(kg)},n4\n'
            + ('' if j == 3 else '        bra     aucurc\n'))


def curtain_body():
    def at(nm):
        return f'x:(r4+${ST[nm + "0"]:02x})'
    return f'''aucurc:
; the curtain (r4 = r7 + its number; x1 y1 = its pan levels / 2, n4 its decay constant): its brightness wanders
; to a new place every 2 .. 7 s
        move    #>$ffffff,m4
        move    {at('CF')},a
        sub     #>$1,a
        move    a,{at('CF')}
        bgt     auheld
        bsr     aurand
        move    {{=CFSPAN}},y0
        mpy     y0,x0,a
        add     {{=CFLO}},a
        move    a,{at('CF')}
        bsr     aurand
        move    x0,{at('CT')}
auheld:
        move    {at('CT')},a
        move    {at('CH')},x0
        sub     x0,a
        move    a,x0
        move    {{=CURC}},y0
        mpy     y0,x0,a
        move    {at('CH')},b
        move    {at('CO')},b0
        add     a,b
        move    b1,{at('CH')}
        move    b0,{at('CO')}
; its level on each side, halved (the curtain sums go to the interpolator at half): 0.45 (0.2 + 0.8 brightness) x the pan / 2
        move    b1,x0
        move    {{=K36}},y0
        mpy     y0,x0,a
        add     {{=K09}},a
        move    a,x0
        mpy     x1,x0,a
        move    a,{at('CL')}
        mpy     x0,y1,a
        move    a,{at('CR')}
; its line's gain for the decay time: 2^-(K len / T60)
        move    {{RT}},x0
        move    n4,y0
        mpy     y0,x0,a
        bsr     auexp
        move    b,x0
        move    x0,{at('G')}
'''


def damp_line(j):
    return f'''        move    {n(RD[j])},n1
        move    {{LP{j}}},b
        move    x:(r1+n1),a
        sub     b,a
        move    a,x1
        macr    y1,x1,b
        move    b,{{LP{j}}}
'''


def out_line(j):
    return f'''        move    {n(RD[j])},n1
        move    {{CL{j}}},y0
        move    x:(r1+n1),x0
        mac     y0,x0,a
        move    {{CR{j}}},y0
        mac     y0,x0,b
'''


def write_line(j, acc, sign):
    return f'''        move    {acc},x1
        move    {{G{j}}},x0
        mpyr    x1,x0,a
        move    {{INJ}},x0
        {sign}     x0,a
        move    {n(WR[j])},n1
        move    a,x:(r1+n1)
'''


TEMPLATE = r'''; CYCLES_FORWARD_BRANCHES
; AERIA AURORA {=BUILDNOTE} -- generated by aurora/gen.py, do not edit.
; The Balloon Shop's aurora (MoreWeather.h, Aurora): the sound (L + R)/2 an octave up -- two
; Hann-windowed taps half a window apart, read twice as fast as written -- through a 500 Hz
; highpass into four delay lines that feed each other (Hadamard), highs damped as they go round,
; 9 s to die away; each line is a curtain with its own place between the speakers and a
; brightness that wanders. The curtains are ADDED to the dry sound.
;
;  knob 0 AMT   the weather's fade 0..127 = 0..1 into the reverb (glided here with the plugin's 1.2 s)
;  knob 1 TIME  decay: T60 = 9 s x 2^((v - 64)/32)     (64 = the plugin's 9 s; 2.25 .. 35 s)
;  knob 2 TONE  damping in the loop, as the plugin's lowpass: 6 kHz x 2^((v - 64)/24) (64 = the plugin). The loop's
;               own lowpass (22.05 kHz) is {=LOOPDOC}: the lines are 0.3851 x as long
;  knob 3 SHFT  0 UNIS, 1 +12 (the plugin's octave), 2 +19, 3 +24: the read runs 1 x .. 4 x the write
;               13 Nov 2026: (L + R) is low-passed first (order 3, elliptic-type, per SHFT; none at UNIS), so the
;               faster read does not fold the top of the sound back down (aliases landing under 8 kHz >= 37 dB down)
;
; HALF RATE (13 Nov 2026): a halfband (polyphase IIR, 5 allpasses) decimates (L + R)/4 to 22.05 kHz; the aurora
; runs a core frame every second frame; a halfband interpolates the curtains back up. Only the halfbands and the
; dry + wet sum run at 44.1 kHz. The wet ends at ~10.5 kHz. Halfband coefficients (A0 | A1): {=HBDOC}
;
; AMT 0, the glide at 0 and the curtains under 1e-5 for half a second: REST (returns at once,
; output = input bit for bit). Coming up from rest the ring is cleared over {=CLRB} dry calls.
; Each call one of the four curtains is looked at in turn (brightness, pan levels, line gain).
; The taps move by whole frames, so nothing is interpolated: exact.
; The lines run at 1/4 of the plugin's level; a rider on what goes into them keeps a long tail of loud sound out of their limit.
;
; X region: {=REGION} words, base a multiple of {=REGION}: {=BASENOTE}
; The ring is its first {=RING} words; the halfbands' block (parity, coefficients, states) is at +{=HBATDOC}.
; One pointer falls a word a core frame; from it: +0 the window ({=PITCH} words), then the lines
;   {=LINEDOC}
; (the plugin's {=PLEN} frames x {=SCALE}; gains set for the same decay time)
; r7: {=STATEDOC}

init:
        move    #>$ffffff,m4
        move    r7,r4
        clr     a
        do      #64,>auzero
        move    a,x:(r4)+
auzero:
        nop
{=GETBASE}        move    a,{XB}
        move    a,{RP}
        move    #>$00a020,a             ; the plugin's seed
        move    a,{RNG}
        rts
proc:
        move    {ST},a
        tst     a
        bne     auawake
        move    x:(r6),a
        tst     a
        bne     auwake
        rts                             ; REST: nothing touched
auwake:
        move    r7,r4
        move    #>$ffffff,m4
        move    #>$6,n4
        clr     a
        move    (r4)+n4
        do      #58,>auwz
        move    a,x:(r4)+               ; everything from C on: the plugin's reset
auwz:
        nop
        move    #>$400000,a
        move    a,{CH0}
        move    a,{CH1}
        move    a,{CH2}
        move    a,{CH3}
        move    a,{CT0}
        move    a,{CT1}
        move    a,{CT2}
        move    a,{CT3}
        move    a,{DAMP}
        move    #>$7f0000,a
        move    a,{G0}
        move    a,{G1}
        move    a,{G2}
        move    a,{G3}
        move    #>$ffffff,a
        move    a,{TLAST}
        move    a,{OLAST}
        move    #>$7fffff,a
        move    a,{RIDE}
        clr     a
        move    a,{AMH}
        move    a,{AML}
        move    {XB},a
        move    a,{RP}
        move    {=HALFN},a
        move    a,{C}
        move    #>$000003,a             ; the first running call looks at TIME and curtain 0
        move    a,{BLK}
        move    {=CLRBN},a
        move    a,{ST}
        rts
auawake:
        blt     aurun
; ---- clearing: call n ({=CLRB} .. 1) clears words (n - 1) * {=CLRW} ..
        sub     #>$1,a
        move    a,{ST}
        asl     #$8,a,a
        move    {XB},x0
        add     x0,a
        move    a1,r4
        move    #>$ffffff,m4
        clr     b
        do      {=CLRWN},>auclr
        move    b,x:(r4)+
auclr:
        nop
        move    {ST},a
        tst     a
        bne     aukeep
        move    #>$ffffff,a             ; cleared: RUN from the next call
        move    a,{ST}
; the halfbands' coefficients (their states and the parity are cleared): A1 x 3 at +{=HBCEDOC}, A0 x 3 after
        move    {XB},a
        add     {=HBCEN},a
        move    a1,r4
        move    #>$ffffff,m5
        lua     (r4+{=HBCODISP}),r5
        do      #3,>auhbc
{=HBCOEFS}auhbc:
        nop
aukeep:
        rts
aurun:
; ---- amount: knob / 127, glided with the plugin's 1.2 s (48 bits)
        move    x:(r6),x0
        move    #>$408102,y0
        mpy     y0,x0,a
        asl     a
        move    {AMH},x0
        sub     x0,a
        move    a,x0
        move    {=AMC},y0
        mpy     y0,x0,a
        move    {AMH},b
        move    {AML},b0
        add     a,b
        move    b1,{AMH}
        move    b0,{AML}
        move    x:(r6),a
        tst     a
        bne     auon
        move    {AMH},a
        cmp     {=AMZERO},a
        bge     auon
        clr     a                       ; the knob at 0 and the glide under 1e-4: the amount is 0
        move    a,{AMH}
        move    a,{AML}
        move    {QT},a
        cmp     {=QBLOCKS},a
        blt     auon
        clr     a                       ; and the curtains quiet for half a second: REST
        move    a,{ST}
        rts
auon:
; ---- what goes into the lines this call: the amount x the rider (1 unless the lines stack up near full scale)
        move    {AMH},x0
        move    {RIDE},y0
        mpy     y0,x0,a
        move    a,{AR}
; ---- SHFT: frames the taps move a frame; UNIS parks tap 1 at the window's top
        move    x:(r6+$3),a
        asr     #$10,a,a
        move    a1,a
        move    #>$3,x0
        cmp     x0,a
        tgt     x0,a
        move    a,{STEP}
        bsr     >auzaa                  ; the anti-alias low-pass for this SHFT
        move    {STEP},a
        tst     a
        bne     aumove
        move    {=HALFN},a
        move    a,{C}
aumove:
; ---- the window's weight at the END of this call, ramped to over its 16 frames
        move    {STEP},a
        asl     #$3,a,a                 ; 8 core frames a call
        move    a1,x0
        move    {C},a
        sub     x0,a
        bge     auwin
        add     {=WINN},a
        tst     a
        bge     auwin
        add     {=WINN},a
auwin:
        asl     #$c,a,a
        move    a,x0
        move    {=KPH},y0
        mpy     y0,x0,a
        asl     a                       ; phase = c / window
        sub     #>$400000,a
        abs     a
        asl     #$2,a,a
        neg     a
        add     #>$7fffff,a
        move    a,x0                    ; 1 - |4 phase - 2|
        bsr     ausin
        add     #>$400000,a             ; sin^2(pi phase)
        move    {WW},b
        sub     b,a
        asr     #$3,a,a
        move    a,{DW}
; ---- one job a call: curtain BLK & 3 (and, with curtain 0 / 1, the TIME / TONE knob if it moved)
        move    {BLK},a
        add     #>$1,a
        and     #>$000003,a
        move    a1,{BLK}
        tst     a
        bne     aunot0
        move    x:(r6+$1),a
        move    {TLAST},x0
        cmp     x0,a
        beq     aucur0
        move    a,{TLAST}
        asr     #$3,a,a                 ; (v/32)/32
        move    a1,a
        bsr     auexp
        move    b,x0
        move    x0,{RT}                 ; 2^-(v/32): 1/T60 against TIME 0's
        bra     aucur0
aunot0:
        cmp     #>$000001,a
        bne     aunot1
        move    x:(r6+$2),a
        move    {OLAST},x0
        cmp     x0,a
        beq     aucur1
        move    a,{OLAST}
        move    #>$7f0000,b
        sub     a,b
        move    b,x0
        move    {=KTONE},y0
        mpy     y0,x0,a                 ; ((127 - v)/24)/32
        bsr     auexp
        move    b,x0
        move    {=KWTOP},y0
        mpy     y0,x0,a                 ; w / (32 ln 2)
        move    {=KWCAP},x0
        cmp     x0,a
        tgt     x0,a
        bsr     auexp
        move    #>$7fffff,a
        sub     b,a
        move    a,{DAMP}                ; 1 - e^-w
        bra     aucur1
aunot1:
        cmp     #>$000002,a
        beq     aucur2
        bra     aucur3
{=CURTAINS}aucurd:
{=FRAMEHEAD}; the taps: c core frames (+ 2) back and half a window further round; c falls STEP a core frame
        move    {C},a
        move    {STEP},x0
        sub     x0,a
        bge     autapc
        add     {=WINN},a
autapc:
        move    a1,{C}
        add     {=TAP0N},a
        move    a1,n1
        add     {=HALFN},a
        move    x:(r1+n1),x0
        cmp     {=WRAPN},a
        blt     autaph
        sub     {=WINN},a
autaph:
        move    a1,n1
        move    {WW},b
        move    x:(r1+n1),x1
        move    {DW},y0
        add     y0,b
        move    b,{WW}
        move    b,y0
; y/4 = tap2/2 + w (tap1 - tap2)/2 (the window holds y/2)
        move    x0,a
        sub     x1,a
        asr     a
        move    a,x0
        mpy     y0,x0,a
        move    x1,b
        asr     b
        add     b,a
; 500 Hz highpass (state variable, on y/4): lp += f bp; hp = x - lp - q bp; bp += f hp
        move    {SVB},x0
        move    {=SVF},y0
        move    {SVL},b
        macr    y0,x0,b
        move    b,{SVL}
        sub     b,a
        move    {=SVQ2},y1
        mac     -x0,y1,a
        macr    -x0,y1,a
        move    a,x1
        move    {SVB},b
        macr    x1,y0,b
        move    b,{SVB}
; into the lines: +-(up x amount)/2, at the lines' 1/4 scale (hp is on y/4: one shift fewer than LSHIFT)
        move    {AR},y0
        mpy     x1,y0,a
        asr     {=ISHIFT},a,a
        rnd     a
        move    a,{INJ}
; the curtains: each line's output to its place
        clr     a
        clr     b
{=OUTS}        move    a,{WL}                  ; (the curtains' levels are halved: WL, WR = the sums / 2)
        move    b,{WR}
; the highs damped as they go round
        move    {DAMP},y1
{=DAMPS}; Hadamard: h0 h1 h2 h3 = (l0 +- l1 +- l2 +- l3)/2
        move    {LP0},a
        move    {LP1},x0
        add     x0,a
        asr     a
        rnd     a
        move    a,x1
        move    {LP0},b
        sub     x0,b
        asr     b
        rnd     b
        move    b,y1
        move    {LP2},a
        move    {LP3},x0
        add     x0,a
        asr     a
        rnd     a
        move    {LP2},b
        sub     x0,b
        asr     b
        rnd     b
        move    a,x0
        move    b,y0
        move    x1,a
        add     x0,a                    ; h0
        move    x1,b
        sub     x0,b                    ; h2
{=WRITE0}{=WRITE2}        move    y1,a
        add     y0,a                    ; h1
        move    y1,b
        sub     y0,b                    ; h3
{=WRITE1}{=WRITE3}        move    (r1)-
{=FRAMETAIL}auframes:
        nop
        move    r1,{RP}
        move    #>$ffffff,m1
; the rider: the largest of the four lines' damped reads (the last frame) over {=RCEIL} of the line scale rides the
; injection down {=RDOWN} a call; under it the rider comes back 2^-{=RUPS} a call. A long TIME on loud sound
; can no longer stack the lines into their limit (the plugin's float lines would only grow)
        move    {LP0},a
        abs     a
{=RIDEMAX}        cmp     {=RCEILN},a
        move    {RIDE},b
        ble     aurok
        move    b,x0
        move    {=RDOWNQ},y0
        mpy     y0,x0,a
        sub     a,b
        bra     aurset
aurok:
        move    #>$7fffff,a
        sub     b,a
        asr     #${=RUPSH},a,a
        add     a,b
aurset:
        move    b,{RIDE}
; quiet? (the last frame's curtains, both sides, against 512 LSB)
        move    {WL},a
        abs     a
        move    {WR},b
        abs     b
        add     b,a
        asl     {=QSHIFT},a,a
        cmp     {=QUIETN},a
        ble     auquiet
        clr     a
        move    a,{QT}
        rts
auquiet:
        move    {QT},a
        cmp     {=QBLOCKS},a
        bge     auqx
        add     #>$1,a
        move    a,{QT}
auqx:
        rts
; ---- subroutines (after their callers) ----
{=AASUB}{=RANDSUB}{=EXPSUB}{=SINSUB}'''


class Asm:
    """line maker for halfband.path (A(op, args..), A.p(alu, parallel move))"""
    def __init__(self):
        self.L = []

    def __call__(self, op, *args, c=''):
        self.L.append(ins(op, *args, c=c))

    def p(self, alu, move='', c=''):
        self.L.append(f'        {alu} {move}'.rstrip() + (f'  ; {c}' if c else ''))

    def text(self):
        return '\n'.join(self.L) + '\n'


def ap_path(A, K, tail, pre=True):
    """one halfband path of K first-order allpasses (halfband.path's sections: y = c (x - y[-1]) + x[-1], 4
    instructions a section, every coefficient and state access in a multiply's parallel move), r5 -> its
    coefficients, r3 -> its state chain s[0..K]. Pre: x0 = the input; pre=True: y0 = c[0] (r5 past it) and b = s[0].
    tail: 'deceven' (the decimator's A1: stores its output, preloads the interpolator's first path),
    'decodd' (the decimator's A0: x0 = d = (A0 + A1)/2), 'out' / 'outlast' (an interpolator path: x:(r0)+ <- dry +
    8 x its output; 'out' preloads the next path)."""
    accs = ('b', 'a')
    if not pre:
        A('move', 'x:(r5)+', 'y0')
        A('move', 'x:(r3)', 'b')
    for k in range(K):
        ac, nx = accs[k % 2], accs[(k + 1) % 2]
        A.p(f'mac     y0,x0,{ac}', 'x0,x:(r3)+')
        A('move', 'x:(r3)', 'x1')
        if k < K - 1:
            A.p(f'mac     -x1,y0,{ac}', 'x:(r5)+,y0')
            A.p(f'tfr     x1,{nx}', f'{ac},x0')
            continue
        if tail == 'deceven':
            assert ac == 'a'
            A.p(f'mac     -x1,y0,{ac}', 'x:(r5)+,y0', c="its output; the interpolator's first coefficient")
            A('move', ac, 'x:(r3)+', c='s[K]: the output (the odd frame reads it)')
            A('move', 'x:(r3)', 'b', c="the interpolator's first state")
        elif tail == 'decodd':
            A.p(f'mac     -x1,y0,{ac}', 'x:(r2+n2),x0', c="its output; x0 = the A1 path's (the even frame)")
            A('move', ac, 'x:(r3)+')
            A('add', 'x0', ac)
            A('asr', ac)
            A('move', ac, 'x0', c='d: the decimated (L + R)/4')
        else:
            A.p(f'mac     -x1,y0,{ac}', 'x:(r0),x0', c='its output; x0 = the dry')
            A('move', ac, 'x:(r3)+', c='s[K]')
            A('asl', f'#${LSHIFT + 1:x}', ac, ac, c="x 4 (the lines' scale) x 2 (the halved levels)")
            if tail == 'out':
                A.p(f'add     x0,{ac}', 'x:(r5)+,y0', c="the next path's first coefficient")
                A('move', ac, 'x:(r0)+', c='dry + wet')
                A('move', 'x:(r3)', 'b', c="the next path's first state")
            else:
                A('add', 'x0', ac)
                A('move', ac, 'x:(r0)+', c='dry + wet')


def dec_in(A, K, tail):
    """the decimator's path K on (L + R)/4 of this frame (r5 -> its coefficients, r3 -> its states)"""
    A('move', 'x:(r0+n0)', 'x0')
    A('move', 'x:(r0)', 'a')
    A.p('add     x0,a', 'x:(r5)+,y0', c='L + R; the first coefficient')
    A.p('asr     a', 'x:(r3),b', c='the first state')
    A('asr', 'a')
    A('move', 'a', 'x0', c='(L + R)/4')
    ap_path(A, K, tail)


def interp_out(A, K, pre):
    """the interpolator's paths K on WL then WR (each the curtain sum / 2): x:(r0)+ <- dry + 8 x its output"""
    A('move', '{WL}', 'x0')
    ap_path(A, K, 'out', pre)
    A('move', '{WR}', 'x0')
    ap_path(A, K, 'outlast')


AA_CORE = '''; anti-alias low-pass on d (per SHFT; UNIS: B = 1, the rest 0 = d): the window holds y/2
; w = B d + 2 A1H w1 + A2 w2;  u = w + 2 CH w1 + K w2;  v = u + P v1;  y/2 = v + K v1
        move    {FB},y0
        mpy     y0,x0,a
        move    {FW1},x1
        move    {FA1},y0
        mac     x1,y0,a
        mac     x1,y0,a
        move    {FW2},x0
        move    {FA2},y0
        macr    y0,x0,a
        move    x1,{FW2}
        move    a,{FW1}
        move    {FC},y0
        mac     x1,y0,a
        mac     x1,y0,a
        move    {FK},y1
        mac     x0,y1,a
        move    {FV1},x1
        move    {FP},y0
        macr    x1,y0,a
        move    a,{FV1}
        macr    y1,x1,a
        move    a,x:(r1)
'''


def frame_head():
    """the frame loop up to the core's taps (the core runs on odd frames on x0 = the decimated (L + R)/4)"""
    A = Asm()
    A.L.append('; ======== the frames (44.1 kHz): the halfbands around a 22.05 kHz core frame every odd frame ========')
    A('move', '{=RINGM}', 'm1')
    A('move', '{RP}', 'r1')
    A('move', '#>$1', 'n0')
    A('move', '{XB}', 'a')
    A('add', '{=HBATN}', 'a')
    A('move', 'a1', 'r2', c="the halfbands' block")
    for r in ('m2', 'm3', 'm5'):
        A('move', '#>$ffffff', r)
    A('move', f'#>${HB_P1:x}', 'n2', c="x:(r2+n2): the decimator's A1 output")
    A('lua', f'(r2+${HB_CO:02x})', 'r5', c='(an odd first frame: the pointers where an even frame leaves them)')
    A('lua', f'(r2+${HB_SO:02x})', 'r3')
    A('do', 'n7', '>auframes')
    A('bchg', '#0', 'x:(r2)', c="the pair's parity (C = it before the change): 0 = the first (even) frame, 1 = the second (the core runs)")
    A('bcs', 'auodd')
    A.L.append("; even: the decimator's A1 path takes this frame; out: the interpolator's A1 paths on the last core frame")
    A('lua', f'(r2+${HB_CE:02x})', 'r5')
    A('lua', f'(r2+${HB_SE:02x})', 'r3')
    dec_in(A, HB.K1, 'deceven')
    interp_out(A, HB.K1, pre=True)
    A('bra', 'aufnx')
    A.L.append('auodd:')
    A.L.append("; odd (r5, r3 where the even frame left them): the decimator's A0 path; d = (A0 + A1)/2; the core frame; "
               "out: the interpolator's A0 paths")
    dec_in(A, HB.K0, 'decodd')
    return A.text() + AA_CORE


def frame_tail():
    A = Asm()
    A.L.append("; out: the interpolator's A0 paths on this core frame's curtains")
    interp_out(A, HB.K0, pre=False)
    A.L.append('aufnx:')
    A('nop', c="(the loop's last instruction)")
    return A.text()


def hb_coefs():
    """the do #3 body: A1 to (r4)+, A0 to (r5)+"""
    out = ''
    for cs, r in ((HB.A1, 'r4'), (HB.A0, 'r5')):
        for c in cs:
            out += f'        move    {q(c)},x0\n        move    x0,x:({r})+\n'
    return out


def aa_sub():
    """the per-call coefficient set for SHFT (STEP 0..3): six words, subroutine auzaa (after its caller)"""
    out = '; the anti-alias low-pass coefficients for this SHFT: B A1H A2 CH P K (see gen.py aa_coefs)\nauzaa:\n'
    out += '        move    {STEP},a\n'
    labels = ('auzb12', 'auzb19', 'auzb24')
    for st in range(4):
        if st:
            out += f'{labels[st - 1]}:\n'
        if st < 3:
            out += f'        cmp     #>${st:06x},a\n        bne     {labels[st]}\n'
        for name, w in zip(AAW, aa_words(st + 1)):
            out += f'        move    #>${w & 0xffffff:06x},x0\n        move    x0,{{{name}}}\n'
        out += '        rts\n'
    return out


def build(out='aurora.asm', quiet=True):
    test = out != 'aurora.asm'
    getbase = (f'        move    {n(TEST_XRING)},a\n' if test else
               '        move    x:>$213,r4\n        nop\n        move    x:(r4),a\n')
    c = dict(
        BUILDNOTE='(TEST: ring at X:$10000)' if test else '',
        BASENOTE='X:$10000 (test)' if test else 'the word X:$213 points to (an X address on this effect)',
        GETBASE=getbase, RING=RING, REGION=REGION, HBATDOC=f'${HB_AT:x}', HBATN=n(HB_AT),
        HBCEDOC=f'${HB_AT + HB_CE:x}', HBCEN=n(HB_AT + HB_CE), HBCODISP=f'${HB_CO - HB_CE:02x}', HBCOEFS=hb_coefs(),
        HBDOC=' '.join(f'{c:.6f}' for c in HB.A0) + ' | ' + ' '.join(f'{c:.6f}' for c in HB.A1),
        FRAMEHEAD=frame_head(), FRAMETAIL=frame_tail(), QSHIFT=f'#${LSHIFT + 1:x}',
        LOOPDOC=f'{LOOP_HZ64:.0f} Hz x 2^((v - 64)/{TONE_OCT:.2f}), held to {TONE_TOP:.0f} Hz (fitted to lose a turn '
                f"0.3851 x what the plugin's loses, 2 .. 11.65 kHz)", PITCH=PITCH, CLRB=CLR_BLOCKS, CLRW=CLR_WORDS, STATEDOC=ST.doc(),
        LINEDOC='\n;   '.join(f'line {j}: written at +{WR[j]}, read at +{RD[j]} ({LEN[j]} core frames, {LEN[j] / FSH * 1000:.1f} ms)'
                              for j in range(4)),
        PLEN=' '.join(str(v) for v in PLUGIN_LEN), SCALE=f'{SCALE:.4f}',
        CLRBN=n(CLR_BLOCKS), CLRWN=n(CLR_WORDS), RINGM=n(RING - 1),
        AMC=q(W.pole(AMT_TAU)), AMZERO=n(AM_ZERO), QBLOCKS=n(QUIET_BLOCKS), QUIETN=n(QUIET),
        HALFN=n(HALF), WINN=n(WINDOW), TAP0N=n(TAP0), WRAPN=n(WINDOW + TAP0),
        KPH=q(2048 / WINDOW / 2),
        KTONE=q(128 / TONE_OCT / 32),
        KWTOP=q(2 * math.pi * LOOP_HZ64 * 2 ** (63 / TONE_OCT) / FSH / math.log(2) / 32),
        KWCAP=q(2 * math.pi * TONE_TOP / FSH / math.log(2) / 32),
        CFSPAN=n(round(5.0 / CUR_TICK)), CFLO=n(round(2.0 / CUR_TICK)), CURC=q(W.pole(CUR_TAU, CUR_TICK)),
        K36=q(LEVEL * 0.8), K09=q(LEVEL * 0.2),
        SVF=q(SV_F), SVQ2=q(1 / HP_Q / 2), LSHIFT=f'#${LSHIFT:x}', ISHIFT=f'#${LSHIFT - 1:x}', AASUB=aa_sub(),
        CURTAINS=''.join(per_curtain(j) for j in range(4)) + curtain_body(),
        RCEIL=RIDE_CEIL, RDOWN=f'1/{round(1 / RIDE_DOWN)}', RUPS=RIDE_UP_SHIFT, RUPSH=f'{RIDE_UP_SHIFT:x}',
        RCEILN=q(RIDE_CEIL), RDOWNQ=q(RIDE_DOWN),
        RIDEMAX=''.join(f'        move    {{LP{j}}},b\n        abs     b\n        move    b,x0\n'
                        f'        cmp     x0,a\n        tlt     x0,a\n' for j in (1, 2, 3)),
        OUTS=''.join(out_line(j) for j in range(4)), DAMPS=''.join(damp_line(j) for j in range(4)),
        WRITE0=write_line(0, 'a', 'add'), WRITE2=write_line(2, 'b', 'add'),
        WRITE1=write_line(1, 'a', 'sub'), WRITE3=write_line(3, 'b', 'sub'),
        RANDSUB=W.rand_sub('aurand', '{RNG}'), EXPSUB=W.exp2_sub('auexp', 'aupow'), SINSUB=W.sin_sub('ausin'),
    )
    src = W.render(W.render(W.render(TEMPLATE, ST, c), ST, c), ST, c)
    W.write(HERE / out, src, quiet)
    return src


BUILDS = ('aurora.asm', 'aurora_test.asm')

if __name__ == '__main__':
    for b in BUILDS:
        build(b, quiet=False)
