#!/usr/bin/env python3
"""Generates aurora.asm and aurora_test.asm (AERIA AURORA: the Balloon Shop's aurora,
MoreWeather.h `Aurora`, in fixed point).

    python3 aurora/gen.py          (host, no dependencies)   -- test.py imports this and rebuilds

aurora.asm       the hardware build: the ring's base is the word X:$213 points to, used as an X address
aurora_test.asm  the same source with the ring at X:$10000 (dsp_host hands out a Y base); one line differs
"""
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import wxlib as W  # noqa: E402
from wxlib import q, n  # noqa: E402

HERE = Path(__file__).resolve().parent
FS = W.FS

# ---- sizes: ONE ring of 8,192 X words holds the octave window and the four lines ---------------------
RING = 8192
WINDOW = 2204               # frames (49.98 ms; the plugin's 0.05 s, made even so the taps sit half a window apart)
HALF = WINDOW // 2
TAP0 = 2
PITCH = WINDOW + 4          # words kept for the window at the ring's head
PLUGIN_MS = (61.3, 79.9, 97.1, 113.7)
PLUGIN_LEN = tuple(int(ms * 0.001 * FS) for ms in PLUGIN_MS)            # 2703 3523 4282 5014 at 44.1 kHz
LEN = (1039, 1361, 1651, 1931)                                          # x 0.3855, pairwise coprime
SCALE = sum(LEN) / sum(PLUGIN_LEN)
WR = (PITCH, PITCH + LEN[0], PITCH + LEN[0] + LEN[1], PITCH + LEN[0] + LEN[1] + LEN[2])
RD = tuple(w + l for w, l in zip(WR, LEN))
assert RD[3] <= RING - 1
CLR_WORDS = 256
CLR_BLOCKS = RING // CLR_WORDS
TEST_XRING = 0x10000

# ---- the plugin's constants ------------------------------------------------------------------------------
T60 = 9.0                   # seconds (TIME 64)
T60_OCT = 32.0              # TIME: T60 = 9 s x 2^((v - 64)/32)
DAMP_HZ = 6000.0            # TONE 64 (the plugin's lowpass in its full-length loop)
TONE_OCT = 24.0             # TONE: 6 kHz x 2^((v - 64)/24), held to 18 kHz
TONE_TOP = 18000.0
DAMP_UP = 1.545             # the loop's own lowpass sits this much higher: lines 0.3855 x as long must lose
                            # 0.3855 x as much a turn ((1 - a)/a^2 scaled; exact at 6 kHz, within 8 % from 2 to 12 kHz)
HP_HZ, HP_Q = 500.0, 0.707
SV_F = 2 * math.sin(math.pi * HP_HZ / FS)
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

# ---- anti-alias low-pass (13 Nov 2026, "stop aliasing"): the window is read 2 / 3 / 4 x as fast as it is written,
# so what sits above fs / (2 x ratio) folds back down. (L + R) goes through an order-3 elliptic-type low-pass first
# (a biquad with a pair of zeros on the unit circle, then a real pole with a zero at Nyquist), designed per SHFT so
# that everything which would land below 16 kHz is >= 37 dB down while the shimmer keeps its top to within 1 dB:
# +12 up to 16 kHz out (8 kHz in), +19 to 15 kHz out, +24 to 14 kHz out. UNIS: no filter (bit for bit as before).
# AA[ratio] = (pole radius logit, pole Hz, zero Hz, real-pole logit): from a minimax search (scratch design script)
AA = {2: (1.52483816, 7814.82563, 14882.0525, 0.0412253595),
      3: (1.92847516, 4847.52177, 10261.7512, 0.742778404),
      4: (2.26082686, 3378.77955, 7821.42421, 1.19737751)}


def aa_coefs(ratio):
    """-> (B, A1H, A2, CH, P, K) as fractions: w = B (L + R) + 2 A1H w1 + A2 w2; u = w + 2 CH w1 + K w2;
    v = u + P v1; y = v + K v1 = H (L + R)/4 (H: unity at DC), so the window holds y = filtered (L + R)/2, halved."""
    if ratio == 1:
        return (0.25, 0.0, 0.0, 0.0, 0.0, 0.0)
    lr, fp, fz, lp = AA[ratio]
    rr = 1 / (1 + math.exp(-lr))
    a1 = -2 * rr * math.cos(2 * math.pi * fp / FS)
    a2 = rr * rr
    c = -2 * math.cos(2 * math.pi * fz / FS)
    p = 1 / (1 + math.exp(-lp))
    g = (1 + a1 + a2) / (2 + c) * (1 - p) / 2
    return (g / 4, -a1 / 2, -a2, c / 2, p, 1.0)


def aa_words(ratio):
    """the same as the DSP holds them (Q23 words, K = 1 as $7fffff)"""
    return tuple(min((1 << 23) - 1, max(-(1 << 23), round(v * (1 << 23)))) for v in aa_coefs(ratio))


def t60(v):
    return T60 * 2 ** ((v - 64) / T60_OCT)


def tone_hz(v):
    """The loop lowpass's own frequency (the plugin-equivalent one is this / DAMP_UP)."""
    return min(TONE_TOP, DAMP_UP * DAMP_HZ * 2 ** ((v - 64) / TONE_OCT))


def line_gain(j, v=64):
    return 10 ** (-3 * LEN[j] / FS / t60(v))


def pan_gains(p):
    a = (0.5 + 0.5 * p) * math.pi / 2
    return math.cos(a) * math.sqrt(2), math.sin(a) * math.sqrt(2)


ST = W.State('ST XB RP AMH AML RNG C STEP WW DW SVL SVB INJ WL WR LP0 LP1 LP2 LP3 G0 G1 G2 G3 '
             'CL0 CL1 CL2 CL3 CR0 CR1 CR2 CR3 CH0 CH1 CH2 CH3 CO0 CO1 CO2 CO3 CT0 CT1 CT2 CT3 '
             'CF0 CF1 CF2 CF3 QT BLK DAMP RT TLAST OLAST RIDE AR FB FA1 FA2 FC FP FK FW1 FW2 FV1')
AAW = ('FB', 'FA1', 'FA2', 'FC', 'FP', 'FK')


def per_curtain(j):
    gl, gr = pan_gains(PANS[j])
    kg = 3 * math.log2(10) * LEN[j] / FS / (T60 * 2 ** (-64 / T60_OCT)) / 32      # y/32 at TIME 0
    return f'''aucur{j}:
; curtain {j}: its brightness wanders to a new place every 2 .. 7 s
        move    {{CF{j}}},a
        sub     #>$1,a
        move    a,{{CF{j}}}
        bgt     auheld{j}
        bsr     aurand
        move    {{=CFSPAN}},y0
        mpy     y0,x0,a
        add     {{=CFLO}},a
        move    a,{{CF{j}}}
        bsr     aurand
        move    x0,{{CT{j}}}
auheld{j}:
        move    {{CT{j}}},a
        move    {{CH{j}}},x0
        sub     x0,a
        move    a,x0
        move    {{=CURC}},y0
        mpy     y0,x0,a
        move    {{CH{j}}},b
        move    {{CO{j}}},b0
        add     a,b
        move    b1,{{CH{j}}}
        move    b0,{{CO{j}}}
; its level on each side: 0.45 (0.2 + 0.8 brightness) x the pan
        move    b1,x0
        move    {{=K36}},y0
        mpy     y0,x0,a
        add     {{=K09}},a
        move    a,x0
        move    {q(gl / 2)},y0
        mpy     y0,x0,a
        asl     a
        move    a,{{CL{j}}}
        move    {q(gr / 2)},y0
        mpy     y0,x0,a
        asl     a
        move    a,{{CR{j}}}
; its line's gain for the decay time: 2^-(K len / T60)
        move    {{RT}},x0
        move    {q(kg)},y0
        mpy     y0,x0,a
        bsr     auexp
        move    b,x0
        move    x0,{{G{j}}}
        bra     aucurd
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
;               own lowpass sits 1.545 x higher (9.27 kHz at 64, held to 18 kHz): the lines are 0.3855 x as long
;  knob 3 SHFT  0 UNIS, 1 +12 (the plugin's octave), 2 +19, 3 +24: the read runs 1 x .. 4 x the write
;               13 Nov 2026: (L + R) is low-passed first (order 3, elliptic-type, per SHFT; none at UNIS), so the
;               faster read does not fold the top of the sound back down (aliases landing under 16 kHz >= 37 dB down)
;
; AMT 0, the glide at 0 and the curtains under 1e-5 for half a second: REST (returns at once,
; output = input bit for bit). Coming up from rest the ring is cleared over {=CLRB} dry calls.
; Each call one of the four curtains is looked at in turn (brightness, pan levels, line gain).
; The taps move by whole frames, so nothing is interpolated: exact.
; The lines run at 1/4 of the plugin's level; a rider on what goes into them keeps a long tail of loud sound out of their limit.
;
; X ring: {=RING} words, base a multiple of {=RING}: {=BASENOTE}
; One pointer falls a word a frame; from it: +0 the window ({=PITCH} words), then the lines
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
        asl     #$4,a,a
        move    a1,x0
        move    {C},a
        sub     x0,a
        bge     auwin
        add     {=WINN},a
        tst     a
        bge     auwin
        add     {=WINN},a
auwin:
        asl     #$b,a,a
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
        asr     #$4,a,a
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
; ======== the frames ========
        move    {=RINGM},m1
        move    {RP},r1
        move    #>$1,n0
        do      n7,>auframes
; anti-alias low-pass on L + R (per SHFT; UNIS: B = 1/4, the rest 0 = (L + R)/4 exactly): the window holds y/2
; w = B (L + R) + 2 A1H w1 + A2 w2;  u = w + 2 CH w1 + K w2;  v = u + P v1;  y/2 = v + K v1
        move    {FB},y0
        move    x:(r0),x0
        mpy     y0,x0,a
        move    x:(r0+n0),x0
        mac     y0,x0,a
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
; the taps: c frames (+ 2) back and half a window further round; c falls STEP a frame
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
{=OUTS}        move    a,{WL}
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
{=WRITE1}{=WRITE3}; out = dry + curtains
        move    {WL},a
        asl     {=LSHIFT},a,a
        move    x:(r0),x0
        add     x0,a
        move    a,x:(r0)+
        move    {WR},b
        asl     {=LSHIFT},b,b
        move    x:(r0),x0
        add     x0,b
        move    b,x:(r0)+
        move    (r1)-
auframes:
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
        asl     {=LSHIFT},a,a
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
        GETBASE=getbase, RING=RING, PITCH=PITCH, CLRB=CLR_BLOCKS, CLRW=CLR_WORDS, STATEDOC=ST.doc(),
        LINEDOC='\n;   '.join(f'line {j}: written at +{WR[j]}, read at +{RD[j]} ({LEN[j]} frames, {LEN[j] / FS * 1000:.1f} ms)'
                              for j in range(4)),
        PLEN=' '.join(str(v) for v in PLUGIN_LEN), SCALE=f'{SCALE:.4f}',
        CLRBN=n(CLR_BLOCKS), CLRWN=n(CLR_WORDS), RINGM=n(RING - 1),
        AMC=q(W.pole(AMT_TAU)), AMZERO=n(AM_ZERO), QBLOCKS=n(QUIET_BLOCKS), QUIETN=n(QUIET),
        HALFN=n(HALF), WINN=n(WINDOW), TAP0N=n(TAP0), WRAPN=n(WINDOW + TAP0),
        KPH=q(4096 / WINDOW / 2),
        KTONE=q(128 / TONE_OCT / 32),
        KWTOP=q(2 * math.pi * DAMP_UP * DAMP_HZ * 2 ** (63 / TONE_OCT) / FS / math.log(2) / 32),
        KWCAP=q(2 * math.pi * TONE_TOP / FS / math.log(2) / 32),
        CFSPAN=n(round(5.0 / CUR_TICK)), CFLO=n(round(2.0 / CUR_TICK)), CURC=q(W.pole(CUR_TAU, CUR_TICK)),
        K36=q(LEVEL * 0.8), K09=q(LEVEL * 0.2),
        SVF=q(SV_F), SVQ2=q(1 / HP_Q / 2), LSHIFT=f'#${LSHIFT:x}', ISHIFT=f'#${LSHIFT - 1:x}', AASUB=aa_sub(),
        CURTAINS=''.join(per_curtain(j) for j in range(4)),
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
