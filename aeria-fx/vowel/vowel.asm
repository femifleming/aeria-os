; CYCLES_FORWARD_BRANCHES
; AERIA VOWL -- vowel formant filter (Cloudfin FormantFilter.h in 24-bit fixed point).
; Five parallel TPT state-variable bandpasses per channel; VOWL morphs a > e > i > o > u
; (log frequency, dB level, linear bandwidth), SHFT moves every formant -24..+24
; semitones (bandwidth x sqrt of the ratio), RES 0.1..8 narrows the bands, and an RMS
; level matcher (0.1 .. 32x, 250 ms power, 80 ms gain) holds the wet at the dry loudness.
;
; WIDE: every formant sits lower on the left and higher on the right, by the same fraction d of its
; frequency: d = WIDE x min(0.0595 (a semitone), 2 kh) -- kh = 1/(2Q), so the two sides' peaks are
; never more than 2 bandwidths apart, however narrow RES makes them. Around each formant the two
; sides then differ in phase by up to +-63 degrees (that is the width); a tone exactly between the two
; peaks still sums in mono to within 1.0 dB of either side (2 / sqrt(1 + x^2), x <= 2), and noise
; through a formant correlates 1 / (1 + x^2) between the sides. Each side has its own coefficient
; block; a call tunes ONE side (they take turns: each is retuned every second call, so the cost
; is that of the one shared set this effect had). The states, the level matcher (one gain for both
; sides: the image does not wander with level) and the knob glides are as before.
; WIDE 0: both blocks hold the same numbers (L = R on equal inputs once the knobs are at rest).
; Per call (block of n7 frames), all five formants of one side, as the plugin does every 16 samples:
;   knobs glide 2^-6 per call; v = VOWL * 4/127; row i0 = int(v), t = frac(v)
;   log2 f = lerp(row, row+1; t) + semis/12          (table word = log2(hz)/16)
;   h = w/2 = 0.5836 * 2^-(14 - log2 f);  sin h, cos h by Taylor (h <= 0.51)
;   sw = 2 sh ch, cw = 1 - 2 sh^2;  P = sw cw, S2 = sw^2
;   kh = k/2 = lerp(bw)/256 * 2^(7 - log2 q - log2 f0 - semis/24), clamped 1/240 .. 1
;   1/D, D = 1 + k P, by three Newton steps;  a2 = P/D, a3 = S2/D, c1 = a3 + k a2
;       (the plugin's g a1, g a2 and 1 - a1 with g = tan w: no coefficient exceeds 1)
;   amp = 2^-(lerp(|dB|) log2(10)/20)
;   gain target: 2^-(5 - (log2(pin+eps) - log2(pout+eps))/2), y clamped 0 .. 8.322
; Per sample, per channel, per formant, on x/4 with the states scaled by kh
; (s = kh ic, so every state stays near the input's size at any Q) and held in 48 bits.
; Each state product is taken in double precision: the multiplicand is split into its
; rounded high word and signed low word, the low-word products are shifted down 24 bits
; (a 24-bit multiplicand leaves a dead band of ~Q LSB that rings on after the input stops):
;   v3 = kh x/4 - s2;  s2 += 2 (a2 s1 + a3 v3);  t = a2 v3 - c1 s1;  v1 = s1 + t;  s1 += 2 t
;   wet/4 = 2 sum amp v1;  out = sat(wet/4 * gain/32 * 128);  dry/wet; MIX 0 is exact dry.
;
;  (the first call tunes the left side and copies it to the right, so neither starts empty)
; r7: 00 snap flag 01/02 mix t/s 03/04 vowl t/s 05/06 shft t/s 07/08 res t/s 09 t
;     0a ys 0b yq 0c lf/2 0d bw 0e sin h 0f 1-cos h 10 P 11 side 12 S2 1a first call
;     8e -WIDE (left) / +WIDE (right) for this call's side  8f h
;     13 gain target 14/15 gain (48 bit) 16/17 pin 18/19 pout (48 bit)
;     1b/1c x/4 L R 1d/1e wet/4 L R 1f log tmp
; r7+$20..$6a: vowel constants (row = vowel, 3 per formant), written by init
; r1 = r7+$70: LEFT formant f at +5f: c1 a2 a3 kh amp; +25+f: v1 of the current channel
; r5 = r7+$c2: RIGHT formant f at +5f: c1 a2 a3 kh amp
; r2 = r7+$90: channel c formant f at +25c+5f: s1 rounded, s1 hi lo, s2 hi lo

init:
        move    #>$ffffff,m4
        move    r7,r4
        clr     a
        do      #219,>vwzero
        move    a,x:(r4)+
vwzero:
        nop     
        move    #>$20,n4
        move    r7,r4
        nop     
        move    (r4)+n4
        move    #>$4ac11e,a
        move    a,x:(r4)+
        move    #>$280000,a
        move    a,x:(r4)+
        move    #>$000000,a
        move    a,x:(r4)+
        move    #>$509d51,a
        move    a,x:(r4)+
        move    #>$2d0000,a
        move    a,x:(r4)+
        move    #>$03fc7f,a
        move    a,x:(r4)+
        move    #>$5af966,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$04a694,a
        move    a,x:(r4)+
        move    #>$5c03c3,a
        move    a,x:(r4)+
        move    #>$410000,a
        move    a,x:(r4)+
        move    #>$0550a9,a
        move    a,x:(r4)+
        move    #>$5d546d,a
        move    a,x:(r4)+
        move    #>$460000,a
        move    a,x:(r4)+
        move    #>$0e9dd2,a
        move    a,x:(r4)+
        move    #>$45269e,a
        move    a,x:(r4)+
        move    #>$230000,a
        move    a,x:(r4)+
        move    #>$000000,a
        move    a,x:(r4)+
        move    #>$55d9be,a
        move    a,x:(r4)+
        move    #>$280000,a
        move    a,x:(r4)+
        move    #>$094d28,a
        move    a,x:(r4)+
        move    #>$5ac11e,a
        move    a,x:(r4)+
        move    #>$320000,a
        move    a,x:(r4)+
        move    #>$07f8fe,a
        move    a,x:(r4)+
        move    #>$5d269e,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$094d28,a
        move    a,x:(r4)+
        move    #>$5e722a,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$0d49a8,a
        move    a,x:(r4)+
        move    #>$417074,a
        move    a,x:(r4)+
        move    #>$140000,a
        move    a,x:(r4)+
        move    #>$000000,a
        move    a,x:(r4)+
        move    #>$56f359,a
        move    a,x:(r4)+
        move    #>$2d0000,a
        move    a,x:(r4)+
        move    #>$09f73e,a
        move    a,x:(r4)+
        move    #>$5b9c15,a
        move    a,x:(r4)+
        move    #>$320000,a
        move    a,x:(r4)+
        move    #>$0bf57d,a
        move    a,x:(r4)+
        move    #>$5d546d,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$0d49a8,a
        move    a,x:(r4)+
        move    #>$5e50f7,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$13ee7b,a
        move    a,x:(r4)+
        move    #>$45269e,a
        move    a,x:(r4)+
        move    #>$140000,a
        move    a,x:(r4)+
        move    #>$000000,a
        move    a,x:(r4)+
        move    #>$4d269e,a
        move    a,x:(r4)+
        move    #>$280000,a
        move    a,x:(r4)+
        move    #>$06a4d4,a
        move    a,x:(r4)+
        move    #>$5ac11e,a
        move    a,x:(r4)+
        move    #>$320000,a
        move    a,x:(r4)+
        move    #>$07f8fe,a
        move    a,x:(r4)+
        move    #>$5b9c15,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$07f8fe,a
        move    a,x:(r4)+
        move    #>$5c67ee,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$114627,a
        move    a,x:(r4)+
        move    #>$439c15,a
        move    a,x:(r4)+
        move    #>$140000,a
        move    a,x:(r4)+
        move    #>$000000,a
        move    a,x:(r4)+
        move    #>$49d49f,a
        move    a,x:(r4)+
        move    #>$1e0000,a
        move    a,x:(r4)+
        move    #>$0d49a8,a
        move    a,x:(r4)+
        move    #>$5b30a1,a
        move    a,x:(r4)+
        move    #>$320000,a
        move    a,x:(r4)+
        move    #>$0b4b68,a
        move    a,x:(r4)+
        move    #>$5c03c3,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$094d28,a
        move    a,x:(r4)+
        move    #>$5d8189,a
        move    a,x:(r4)+
        move    #>$3c0000,a
        move    a,x:(r4)+
        move    #>$114627,a
        move    a,x:(r4)+
        move    #>$1,a
        move    a,x:(r7+$00)
        move    #>$040000,a
        move    a,x:(r7+$13)
        move    a,x:(r7+$14)
        rts     
proc:
        move    #>$ffffff,m1
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    #>$ffffff,m5
        move    #>$70,n1
        move    #>$c2,n5
        move    r7,r1
        move    r7,r5
        move    (r1)+n1
        move    (r5)+n5
        move    x:(r6+$5),a
        move    #>$7f0000,x0
        cmp     x0,a
        bne     vwmset
        move    #>$7fffff,a
vwmset:
        move    a,x:(r7+$01)
        move    x:(r7+$01),a
        move    x:(r7+$02),b
        sub     b,a
        abs     a
        cmp     #>$000100,a
        bge     vwmsnap
        move    x:(r7+$01),a
        move    a,x:(r7+$02)
vwmsnap:
        move    x:(r6),a
        move    a,x:(r7+$03)
        move    x:(r6+$1),a
        cmp     x0,a  ; x0 = 127: SHFT top is +24 exactly
        bne     vwsset
        move    #>$7fffff,a
vwsset:
        move    a,x:(r7+$05)
        move    x:(r6+$2),a
        move    a,x:(r7+$07)
        move    x:(r7+$00),a
        tst     a
        beq     vwglide
        move    x:(r7+$03),b
        move    b,x:(r7+$04)
        move    x:(r7+$05),b
        move    b,x:(r7+$06)
        move    x:(r7+$07),b
        move    b,x:(r7+$08)
        clr     a
        move    a,x:(r7+$00)
        move    #>$1,a
        move    a,x:(r7+$11)  ; first call: the left side (the toggle below makes it 0)
        move    a,x:(r7+$1a)
vwglide:
        move    x:(r7+$03),a
        move    x:(r7+$04),b
        sub     b,a
        asr     #$6,a,a
        add     a,b
        move    b,x:(r7+$04)
        move    x:(r7+$05),a
        move    x:(r7+$06),b
        sub     b,a
        asr     #$6,a,a
        add     a,b
        move    b,x:(r7+$06)
        move    x:(r7+$07),a
        move    x:(r7+$08),b
        sub     b,a
        asr     #$6,a,a
        add     a,b
        move    b,x:(r7+$08)
; v/4 = knob * 128/127 (limited at 1): row = top 2 bits, t = the rest
        move    x:(r7+$04),x0
        move    x0,a
        move    #>$010204,y0
        mac     y0,x0,a
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a
        move    a,x0
        move    x0,a  ; 24 bits, a0 clear
        asr     #$15,a,a
        move    a1,a  ; i0 = 0..3
        move    a,b
        asl     #$4,b,b
        sub     a,b  ; 15 i0
        move    b1,n3
        move    r7,r3
        nop     
        move    (r3)+n3
        move    #>$20,n3
        move    x0,b
        and     #>$1fffff,b
        move    (r3)+n3  ; r3 = row i0
        asl     #$2,b,b
        move    b1,x:(r7+$09)  ; t
; this call's side: toggle; r2 = its coefficient block; ws = -WIDE (left) / +WIDE (right)
        move    #>$1,b
        move    x:(r7+$11),x0
        sub     x0,b
        move    b,x:(r7+$11)
        move    x:(r6+$4),a
        move    r5,r2
        tst     b
        bne     vwright
        neg     a
        move    r1,r2
vwright:
        move    a,x:(r7+$8e)
; ys = 14/32 - oct/32, yq = (log2 q - 3)/32 + oct/64   (oct = 4 (SHFT - 1/2))
        move    x:(r7+$06),a
        sub     #>$400000,a
        asr     #$3,a,a
        move    a,x0
        move    #>$380000,b
        sub     x0,b
        move    b,x:(r7+$0a)
        move    x:(r7+$06),a
        sub     #>$400000,a
        asr     #$4,a,a
        move    x:(r7+$08),x0
        move    #>$197ca1,y0
        mac     y0,x0,a
        sub     #>$1949a8,a
        move    a,x:(r7+$0b)
; ---- formant 1 ----
        lua     (r2+$0),r4
        move    x:(r3+$00),a
        move    x:(r3+$0f),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        asr     a
        move    a,x0
        move    x0,x:(r7+$0c)  ; log2(f0)/32
        move    x:(r7+$0a),a
        sub     x0,a
        bsr     vwexp
        move    b,x0
        move    #>$4ab2c9,y0
        mpy     y0,x0,a  ; h = w/2
        move    a,x:(r7+$8f)
        move    x:(r3+$01),a
        move    x:(r3+$10),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        move    a,x:(r7+$0d)
        move    x:(r7+$0b),a
        move    x:(r7+$0c),x0
        add     x0,a
        bsr     vwexp
        move    b,x0
        move    x:(r7+$0d),y0
        mpy     y0,x0,a
        asl     #$4,a,a  ; kh = k/2
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a  ; Q >= 0.5
        move    #>$008889,x0
        cmp     x0,a
        tlt     x0,a  ; Q <= 120
        move    a,x0
        move    x0,x:(r4+$3)
; this side's h: h (1 + ws min(cap, x kh)), then the coefficients
        asl     a
        move    #>$079c7d,x1
        cmp     x1,a
        tgt     x1,a
        move    a,x1
        move    x:(r7+$8e),y0
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$8f),y0
        mpy     x1,y0,a
        add     y0,a
        bsr     vwsico
        move    x:(r4+$3),x0
        bsr     vwdiv
        move    x:(r3+$02),a
        move    x:(r3+$11),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        bsr     vwexp
        move    b,x:(r4+$4)  ; amp (limits at 1)
; ---- formant 2 ----
        lua     (r2+$5),r4
        move    x:(r3+$03),a
        move    x:(r3+$12),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        asr     a
        move    a,x0
        move    x0,x:(r7+$0c)  ; log2(f0)/32
        move    x:(r7+$0a),a
        sub     x0,a
        bsr     vwexp
        move    b,x0
        move    #>$4ab2c9,y0
        mpy     y0,x0,a  ; h = w/2
        move    a,x:(r7+$8f)
        move    x:(r3+$04),a
        move    x:(r3+$13),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        move    a,x:(r7+$0d)
        move    x:(r7+$0b),a
        move    x:(r7+$0c),x0
        add     x0,a
        bsr     vwexp
        move    b,x0
        move    x:(r7+$0d),y0
        mpy     y0,x0,a
        asl     #$4,a,a  ; kh = k/2
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a  ; Q >= 0.5
        move    #>$008889,x0
        cmp     x0,a
        tlt     x0,a  ; Q <= 120
        move    a,x0
        move    x0,x:(r4+$3)
; this side's h: h (1 + ws min(cap, x kh)), then the coefficients
        asl     a
        move    #>$079c7d,x1
        cmp     x1,a
        tgt     x1,a
        move    a,x1
        move    x:(r7+$8e),y0
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$8f),y0
        mpy     x1,y0,a
        add     y0,a
        bsr     vwsico
        move    x:(r4+$3),x0
        bsr     vwdiv
        move    x:(r3+$05),a
        move    x:(r3+$14),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        bsr     vwexp
        move    b,x:(r4+$4)  ; amp (limits at 1)
; ---- formant 3 ----
        lua     (r2+$a),r4
        move    x:(r3+$06),a
        move    x:(r3+$15),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        asr     a
        move    a,x0
        move    x0,x:(r7+$0c)  ; log2(f0)/32
        move    x:(r7+$0a),a
        sub     x0,a
        bsr     vwexp
        move    b,x0
        move    #>$4ab2c9,y0
        mpy     y0,x0,a  ; h = w/2
        move    a,x:(r7+$8f)
        move    x:(r3+$07),a
        move    x:(r3+$16),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        move    a,x:(r7+$0d)
        move    x:(r7+$0b),a
        move    x:(r7+$0c),x0
        add     x0,a
        bsr     vwexp
        move    b,x0
        move    x:(r7+$0d),y0
        mpy     y0,x0,a
        asl     #$4,a,a  ; kh = k/2
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a  ; Q >= 0.5
        move    #>$008889,x0
        cmp     x0,a
        tlt     x0,a  ; Q <= 120
        move    a,x0
        move    x0,x:(r4+$3)
; this side's h: h (1 + ws min(cap, x kh)), then the coefficients
        asl     a
        move    #>$079c7d,x1
        cmp     x1,a
        tgt     x1,a
        move    a,x1
        move    x:(r7+$8e),y0
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$8f),y0
        mpy     x1,y0,a
        add     y0,a
        bsr     vwsico
        move    x:(r4+$3),x0
        bsr     vwdiv
        move    x:(r3+$08),a
        move    x:(r3+$17),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        bsr     vwexp
        move    b,x:(r4+$4)  ; amp (limits at 1)
; ---- formant 4 ----
        lua     (r2+$f),r4
        move    x:(r3+$09),a
        move    x:(r3+$18),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        asr     a
        move    a,x0
        move    x0,x:(r7+$0c)  ; log2(f0)/32
        move    x:(r7+$0a),a
        sub     x0,a
        bsr     vwexp
        move    b,x0
        move    #>$4ab2c9,y0
        mpy     y0,x0,a  ; h = w/2
        move    a,x:(r7+$8f)
        move    x:(r3+$0a),a
        move    x:(r3+$19),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        move    a,x:(r7+$0d)
        move    x:(r7+$0b),a
        move    x:(r7+$0c),x0
        add     x0,a
        bsr     vwexp
        move    b,x0
        move    x:(r7+$0d),y0
        mpy     y0,x0,a
        asl     #$4,a,a  ; kh = k/2
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a  ; Q >= 0.5
        move    #>$008889,x0
        cmp     x0,a
        tlt     x0,a  ; Q <= 120
        move    a,x0
        move    x0,x:(r4+$3)
; this side's h: h (1 + ws min(cap, x kh)), then the coefficients
        asl     a
        move    #>$079c7d,x1
        cmp     x1,a
        tgt     x1,a
        move    a,x1
        move    x:(r7+$8e),y0
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$8f),y0
        mpy     x1,y0,a
        add     y0,a
        bsr     vwsico
        move    x:(r4+$3),x0
        bsr     vwdiv
        move    x:(r3+$0b),a
        move    x:(r3+$1a),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        bsr     vwexp
        move    b,x:(r4+$4)  ; amp (limits at 1)
; ---- formant 5 ----
        lua     (r2+$14),r4
        move    x:(r3+$0c),a
        move    x:(r3+$1b),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        asr     a
        move    a,x0
        move    x0,x:(r7+$0c)  ; log2(f0)/32
        move    x:(r7+$0a),a
        sub     x0,a
        bsr     vwexp
        move    b,x0
        move    #>$4ab2c9,y0
        mpy     y0,x0,a  ; h = w/2
        move    a,x:(r7+$8f)
        move    x:(r3+$0d),a
        move    x:(r3+$1c),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        move    a,x:(r7+$0d)
        move    x:(r7+$0b),a
        move    x:(r7+$0c),x0
        add     x0,a
        bsr     vwexp
        move    b,x0
        move    x:(r7+$0d),y0
        mpy     y0,x0,a
        asl     #$4,a,a  ; kh = k/2
        move    #>$7fffff,x0
        cmp     x0,a
        tgt     x0,a  ; Q >= 0.5
        move    #>$008889,x0
        cmp     x0,a
        tlt     x0,a  ; Q <= 120
        move    a,x0
        move    x0,x:(r4+$3)
; this side's h: h (1 + ws min(cap, x kh)), then the coefficients
        asl     a
        move    #>$079c7d,x1
        cmp     x1,a
        tgt     x1,a
        move    a,x1
        move    x:(r7+$8e),y0
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$8f),y0
        mpy     x1,y0,a
        add     y0,a
        bsr     vwsico
        move    x:(r4+$3),x0
        bsr     vwdiv
        move    x:(r3+$0e),a
        move    x:(r3+$1d),b
        sub     a,b
        move    b,x0
        move    x:(r7+$09),y0
        mac     y0,x0,a
        bsr     vwexp
        move    b,x:(r4+$4)  ; amp (limits at 1)
; ---- first call: the right side starts as a copy of the left; r2 = the states ----
        move    x:(r7+$1a),a
        tst     a
        beq     vwnocopy
        clr     a
        move    a,x:(r7+$1a)
        move    r1,r3
        move    r5,r4
        do      #25,>vwcopy
        move    x:(r3)+,x0
        move    x0,x:(r4)+
vwcopy:
        nop     
vwnocopy:
        move    #>$90,n2
        move    r7,r2
        nop     
        move    (r2)+n2
; ---- level match target (LEVL 0: unity) ----
        move    #>$040000,a
        move    a,x:(r7+$13)
        move    x:(r6+$3),a
        tst     a
        beq     vwlvend
        move    x:(r7+$16),b
        move    x:(r7+$17),b0
        move    #>$42,x0
        mpy     x0,x0,a  ; eps
        add     b,a
        bsr     vwlog
        move    b,x:(r7+$1f)
        move    x:(r7+$18),b
        move    x:(r7+$19),b0
        move    #>$42,x0
        mpy     x0,x0,a  ; eps
        add     b,a
        bsr     vwlog
        move    x:(r7+$1f),x0
        sub     x0,b
        add     #>$140000,b
        move    #>$0,x0
        tst     b
        tlt     x0,b  ; gain <= 32
        move    #>$2149a8,x0
        cmp     x0,b
        tgt     x0,b  ; gain >= 0.1
        move    b,x0
        move    x0,a
        bsr     vwexp
        move    b,x:(r7+$13)  ; gain/32 (limits at 1)
vwlvend:
        do      n7,>vwend
        move    x:(r7+$01),a
        move    x:(r7+$02),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$02)
; gain glide, 80 ms, 48-bit state
        move    x:(r7+$13),a
        move    x:(r7+$14),b
        move    x:(r7+$15),b0
        sub     b,a
        move    a,x0
        move    #>$000949,y0
        mac     y0,x0,b
        move    b1,x:(r7+$14)
        move    b0,x:(r7+$15)
; ---- L ----
        move    x:(r0),a
        asr     #$2,a,a
        rnd     a
        move    a,x:(r7+$1b)  ; x/4
; formant 1
        move    x:(r7+$1b),x1
        move    x:(r1+$03),x0
        move    x:(r2+$03),b
        move    x:(r2+$04),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$02),x0
        move    x:(r1+$01),y0
        mpy     y0,x0,a
        move    x:(r1+$02),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$00),x0
        mac     x1,y0,a
        move    x:(r1+$01),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$03),b
        move    x:(r2+$04),b0
        add     b,a
        move    a,x:(r2+$03)
        move    a0,x:(r2+$04)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$02),x0
        move    x:(r1+$00),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$00),x0
        mac     -y0,x0,a
        move    x:(r1+$01),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$01),b
        move    x:(r2+$02),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$19)
        move    a,x:(r2+$01)
        move    a0,x:(r2+$02)
        rnd     a
        move    a,x:(r2+$00)  ; rounded copy: the multiplicand
; formant 2
        move    x:(r7+$1b),x1
        move    x:(r1+$08),x0
        move    x:(r2+$08),b
        move    x:(r2+$09),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$07),x0
        move    x:(r1+$06),y0
        mpy     y0,x0,a
        move    x:(r1+$07),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$05),x0
        mac     x1,y0,a
        move    x:(r1+$06),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$08),b
        move    x:(r2+$09),b0
        add     b,a
        move    a,x:(r2+$08)
        move    a0,x:(r2+$09)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$07),x0
        move    x:(r1+$05),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$05),x0
        mac     -y0,x0,a
        move    x:(r1+$06),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$06),b
        move    x:(r2+$07),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1a)
        move    a,x:(r2+$06)
        move    a0,x:(r2+$07)
        rnd     a
        move    a,x:(r2+$05)  ; rounded copy: the multiplicand
; formant 3
        move    x:(r7+$1b),x1
        move    x:(r1+$0d),x0
        move    x:(r2+$0d),b
        move    x:(r2+$0e),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$0c),x0
        move    x:(r1+$0b),y0
        mpy     y0,x0,a
        move    x:(r1+$0c),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$0a),x0
        mac     x1,y0,a
        move    x:(r1+$0b),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$0d),b
        move    x:(r2+$0e),b0
        add     b,a
        move    a,x:(r2+$0d)
        move    a0,x:(r2+$0e)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$0c),x0
        move    x:(r1+$0a),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$0a),x0
        mac     -y0,x0,a
        move    x:(r1+$0b),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$0b),b
        move    x:(r2+$0c),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1b)
        move    a,x:(r2+$0b)
        move    a0,x:(r2+$0c)
        rnd     a
        move    a,x:(r2+$0a)  ; rounded copy: the multiplicand
; formant 4
        move    x:(r7+$1b),x1
        move    x:(r1+$12),x0
        move    x:(r2+$12),b
        move    x:(r2+$13),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$11),x0
        move    x:(r1+$10),y0
        mpy     y0,x0,a
        move    x:(r1+$11),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$0f),x0
        mac     x1,y0,a
        move    x:(r1+$10),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$12),b
        move    x:(r2+$13),b0
        add     b,a
        move    a,x:(r2+$12)
        move    a0,x:(r2+$13)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$11),x0
        move    x:(r1+$0f),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$0f),x0
        mac     -y0,x0,a
        move    x:(r1+$10),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$10),b
        move    x:(r2+$11),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1c)
        move    a,x:(r2+$10)
        move    a0,x:(r2+$11)
        rnd     a
        move    a,x:(r2+$0f)  ; rounded copy: the multiplicand
; formant 5
        move    x:(r7+$1b),x1
        move    x:(r1+$17),x0
        move    x:(r2+$17),b
        move    x:(r2+$18),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$16),x0
        move    x:(r1+$15),y0
        mpy     y0,x0,a
        move    x:(r1+$16),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$14),x0
        mac     x1,y0,a
        move    x:(r1+$15),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$17),b
        move    x:(r2+$18),b0
        add     b,a
        move    a,x:(r2+$17)
        move    a0,x:(r2+$18)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$16),x0
        move    x:(r1+$14),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$14),x0
        mac     -y0,x0,a
        move    x:(r1+$15),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$15),b
        move    x:(r2+$16),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1d)
        move    a,x:(r2+$15)
        move    a0,x:(r2+$16)
        rnd     a
        move    a,x:(r2+$14)  ; rounded copy: the multiplicand
        move    x:(r1+$19),x0
        move    x:(r1+$04),y0
        mpy     y0,x0,a
        move    x:(r1+$1a),x0
        move    x:(r1+$09),y0
        mac     y0,x0,a
        move    x:(r1+$1b),x0
        move    x:(r1+$0e),y0
        mac     y0,x0,a
        move    x:(r1+$1c),x0
        move    x:(r1+$13),y0
        mac     y0,x0,a
        move    x:(r1+$1d),x0
        move    x:(r1+$18),y0
        mac     y0,x0,a
        asl     a
        rnd     a
        move    a,x1
        move    x1,x:(r7+$1d)  ; wet/4 before the gain
        move    x:(r7+$14),y0
        mpy     x1,y0,a
        asl     #$7,a,a
        move    a,x1  ; limits
        move    x1,a
        move    x:(r7+$02),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
; ---- R ----
        move    x:(r0),a
        asr     #$2,a,a
        rnd     a
        move    a,x:(r7+$1c)  ; x/4
; formant 1
        move    x:(r7+$1c),x1
        move    x:(r5+$03),x0
        move    x:(r2+$1c),b
        move    x:(r2+$1d),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$1b),x0
        move    x:(r5+$01),y0
        mpy     y0,x0,a
        move    x:(r5+$02),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$19),x0
        mac     x1,y0,a
        move    x:(r5+$01),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$1c),b
        move    x:(r2+$1d),b0
        add     b,a
        move    a,x:(r2+$1c)
        move    a0,x:(r2+$1d)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$1b),x0
        move    x:(r5+$00),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$19),x0
        mac     -y0,x0,a
        move    x:(r5+$01),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$1a),b
        move    x:(r2+$1b),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$19)
        move    a,x:(r2+$1a)
        move    a0,x:(r2+$1b)
        rnd     a
        move    a,x:(r2+$19)  ; rounded copy: the multiplicand
; formant 2
        move    x:(r7+$1c),x1
        move    x:(r5+$08),x0
        move    x:(r2+$21),b
        move    x:(r2+$22),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$20),x0
        move    x:(r5+$06),y0
        mpy     y0,x0,a
        move    x:(r5+$07),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$1e),x0
        mac     x1,y0,a
        move    x:(r5+$06),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$21),b
        move    x:(r2+$22),b0
        add     b,a
        move    a,x:(r2+$21)
        move    a0,x:(r2+$22)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$20),x0
        move    x:(r5+$05),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$1e),x0
        mac     -y0,x0,a
        move    x:(r5+$06),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$1f),b
        move    x:(r2+$20),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1a)
        move    a,x:(r2+$1f)
        move    a0,x:(r2+$20)
        rnd     a
        move    a,x:(r2+$1e)  ; rounded copy: the multiplicand
; formant 3
        move    x:(r7+$1c),x1
        move    x:(r5+$0d),x0
        move    x:(r2+$26),b
        move    x:(r2+$27),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$25),x0
        move    x:(r5+$0b),y0
        mpy     y0,x0,a
        move    x:(r5+$0c),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$23),x0
        mac     x1,y0,a
        move    x:(r5+$0b),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$26),b
        move    x:(r2+$27),b0
        add     b,a
        move    a,x:(r2+$26)
        move    a0,x:(r2+$27)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$25),x0
        move    x:(r5+$0a),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$23),x0
        mac     -y0,x0,a
        move    x:(r5+$0b),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$24),b
        move    x:(r2+$25),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1b)
        move    a,x:(r2+$24)
        move    a0,x:(r2+$25)
        rnd     a
        move    a,x:(r2+$23)  ; rounded copy: the multiplicand
; formant 4
        move    x:(r7+$1c),x1
        move    x:(r5+$12),x0
        move    x:(r2+$2b),b
        move    x:(r2+$2c),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$2a),x0
        move    x:(r5+$10),y0
        mpy     y0,x0,a
        move    x:(r5+$11),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$28),x0
        mac     x1,y0,a
        move    x:(r5+$10),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$2b),b
        move    x:(r2+$2c),b0
        add     b,a
        move    a,x:(r2+$2b)
        move    a0,x:(r2+$2c)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$2a),x0
        move    x:(r5+$0f),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$28),x0
        mac     -y0,x0,a
        move    x:(r5+$10),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$29),b
        move    x:(r2+$2a),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1c)
        move    a,x:(r2+$29)
        move    a0,x:(r2+$2a)
        rnd     a
        move    a,x:(r2+$28)  ; rounded copy: the multiplicand
; formant 5
        move    x:(r7+$1c),x1
        move    x:(r5+$17),x0
        move    x:(r2+$30),b
        move    x:(r2+$31),b0
        neg     b
        mac     x1,x0,b  ; v3 = kh x - s2 (48 bit)
        move    b0,y1  ; its low word, signed
        rnd     b
        move    b,x1
        move    x:(r2+$2f),x0
        move    x:(r5+$15),y0
        mpy     y0,x0,a
        move    x:(r5+$16),y0
        mac     y1,y0,a
        asr     #$18,a,a  ; low-word products
        move    x:(r2+$2d),x0
        mac     x1,y0,a
        move    x:(r5+$15),y0
        mac     y0,x0,a  ; a2 s1 + a3 v3
        asl     a
        move    x:(r2+$30),b
        move    x:(r2+$31),b0
        add     b,a
        move    a,x:(r2+$30)
        move    a0,x:(r2+$31)  ; s2 += 2 (a2 s1 + a3 v3)
        mpy     y1,y0,a
        move    x:(r2+$2f),x0
        move    x:(r5+$14),y0
        mac     -y0,x0,a
        asr     #$18,a,a
        move    x:(r2+$2d),x0
        mac     -y0,x0,a
        move    x:(r5+$15),y0
        mac     x1,y0,a  ; t = a2 v3 - c1 s1
        move    x:(r2+$2e),b
        move    x:(r2+$2f),b0
        add     a,b  ; v1 = s1 + t
        add     b,a  ; s1 += 2 t
        rnd     b
        move    b,x:(r1+$1d)
        move    a,x:(r2+$2e)
        move    a0,x:(r2+$2f)
        rnd     a
        move    a,x:(r2+$2d)  ; rounded copy: the multiplicand
        move    x:(r1+$19),x0
        move    x:(r5+$04),y0
        mpy     y0,x0,a
        move    x:(r1+$1a),x0
        move    x:(r5+$09),y0
        mac     y0,x0,a
        move    x:(r1+$1b),x0
        move    x:(r5+$0e),y0
        mac     y0,x0,a
        move    x:(r1+$1c),x0
        move    x:(r5+$13),y0
        mac     y0,x0,a
        move    x:(r1+$1d),x0
        move    x:(r5+$18),y0
        mac     y0,x0,a
        asl     a
        rnd     a
        move    a,x1
        move    x1,x:(r7+$1e)  ; wet/4 before the gain
        move    x:(r7+$14),y0
        mpy     x1,y0,a
        asl     #$7,a,a
        move    a,x1  ; limits
        move    x1,a
        move    x:(r7+$02),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
; power, 250 ms: p += (e - p)(2^-13 - 2^-15), 48 bit
        move    x:(r7+$1b),x0
        mpy     x0,x0,a
        move    x:(r7+$1c),y0
        mac     y0,y0,a
        move    x:(r7+$16),b
        move    x:(r7+$17),b0
        sub     b,a
        asr     #$d,a,a
        add     a,b
        asr     #$2,a,a
        sub     a,b
        move    b1,x:(r7+$16)
        move    b0,x:(r7+$17)
; power, 250 ms: p += (e - p)(2^-13 - 2^-15), 48 bit
        move    x:(r7+$1d),x0
        mpy     x0,x0,a
        move    x:(r7+$1e),y0
        mac     y0,y0,a
        move    x:(r7+$18),b
        move    x:(r7+$19),b0
        sub     b,a
        asr     #$d,a,a
        add     a,b
        asr     #$2,a,a
        sub     a,b
        move    b1,x:(r7+$18)
        move    b0,x:(r7+$19)
vwend:
        nop     
        rts     
; ---- subroutines ----
vwexp:
        move    a1,b
        asr     #$12,a,a  ; a1 = n = floor(y)
        add     #>$1,a
        move    a1,x1  ; n + 1 (never 0: a do count of 0 is not a skip)
        and     #>$03ffff,b
        asl     #$5,b,b  ; f in Q23, b2 stays 0
        move    b1,x0
        move    #>$2c5c86,y0
        mpy     y0,x0,b
        move    b,y0  ; g = f ln2 / 2
        move    #>$02aaab,x0
        mpy     y0,x0,b
        sub     #>$0aaaab,b
        move    b,x0
        mpy     y0,x0,b
        add     #>$200000,b
        move    b,x0
        mpy     y0,x0,b
        sub     #>$400000,b
        move    b,x0
        mpy     y0,x0,b
        add     #>$400000,b  ; e^-g / 2
        move    b,x0
        mpy     x0,x0,b
        asl     #$2,b,b  ; 2^-f in (0.5, 1]
        do      x1,>vwxpend
        asr     b
vwxpend:
        asl     b
        rts     
vwsico:
; a = h -> P = sin w cos w, S2 = sin^2 w  (w = 2h)
        move    a,x1
        move    a,x0
        mpy     x0,x0,a
        move    a,y0  ; h^2
        move    #>$000680,x0
        mpy     y0,x0,a
        move    #>$011111,b
        sub     a,b
        move    b,x0
        mpy     y0,x0,a
        move    #>$155555,b
        sub     a,b
        move    b,x0
        mpy     y0,x0,a
        move    a,x0
        mpy     x1,x0,a
        move    x1,b
        sub     a,b
        move    b,x:(r7+$0e)  ; sin h
        move    #>$0000d0,x0
        mpy     y0,x0,a
        move    #>$002d83,b
        sub     a,b
        move    b,x0
        mpy     y0,x0,a
        move    #>$055555,b
        sub     a,b
        move    b,x0
        mpy     y0,x0,a
        move    #>$400000,b
        sub     a,b
        move    b,x0
        mpy     y0,x0,a  ; 1 - cos h
        move    #>$7fffff,b
        sub     a,b
        move    b,x0  ; cos h
        move    x:(r7+$0e),y0
        mpy     y0,x0,a
        asl     a
        move    a,x1  ; sin w
        mpy     y0,y0,b
        asl     b
        move    #>$7fffff,a
        sub     b,a
        move    a,x0  ; cos w
        mpy     x1,x0,a
        move    a,x:(r7+$10)
        move    x1,y0
        mpy     y0,y0,a
        move    a,x:(r7+$12)
        rts     
vwdiv:
; x0 = kh -> (r4) c1, a2, a3: P, S2 over D = 1 + 2 kh P
        move    x:(r7+$10),y0
        move    #>$400000,a
        mac     y0,x0,a
        move    a,x1  ; d = D/2 in [0.5, 1)
        move    #>$7a8241,a
        add     #>$400000,a
        sub     x1,a
        move    a,y0  ; r ~ 1/D
        move    #>$400000,a
        mac     -x1,y0,a
        add     #>$400000,a
        move    a,x0  ; 1 - d r
        mpy     y0,x0,a
        asl     a
        move    a,y0  ; r = 2 r (1 - d r)
        move    #>$400000,a
        mac     -x1,y0,a
        add     #>$400000,a
        move    a,x0  ; 1 - d r
        mpy     y0,x0,a
        asl     a
        move    a,y0  ; r = 2 r (1 - d r)
        move    #>$400000,a
        mac     -x1,y0,a
        add     #>$400000,a
        move    a,x0  ; 1 - d r
        mpy     y0,x0,a
        asl     a
        move    a,y0  ; r = 2 r (1 - d r)
        move    x:(r7+$10),x0
        mpy     y0,x0,a
        move    a,x1
        move    x1,x:(r4+$1)  ; a2 = P/D
        move    x:(r7+$12),x0
        mpy     y0,x0,a
        move    a,x:(r4+$2)  ; a3 = S2/D
        move    x:(r4+$3),y0
        mpy     x1,y0,b
        asl     b
        add     a,b
        move    b,x:(r4)  ; c1 = 1 - a1 = a3 + k a2
        rts     
vwlog:
; a = p (48 bit, > 0) -> b = log2(p)/64 + const (Q23): p = m 2^-e, m in [0.5, 1)
        clr     b
        move    #>$000080,x0
        cmp     x0,a
        bge     vw001
        asl     #$10,a,a
        add     #>$10,b
vw001:
        move    #>$000080,x0
        cmp     x0,a
        bge     vw002
        asl     #$10,a,a
        add     #>$10,b
vw002:
        move    #>$008000,x0
        cmp     x0,a
        bge     vw003
        asl     #$8,a,a
        add     #>$8,b
vw003:
        move    #>$080000,x0
        cmp     x0,a
        bge     vw004
        asl     #$4,a,a
        add     #>$4,b
vw004:
        move    #>$200000,x0
        cmp     x0,a
        bge     vw005
        asl     #$2,a,a
        add     #>$2,b
vw005:
        move    #>$400000,x0
        cmp     x0,a
        bge     vw006
        asl     #$1,a,a
        add     #>$1,b
vw006:
        move    b1,x1  ; e
        sub     #>$400000,a
        asl     a
        move    a,y0  ; u = 2m - 1
        move    #>$02c098,x0
        mpy     y0,x0,b
        sub     #>$0bffd0,b
        move    b,x0
        mpy     y0,x0,b
        add     #>$1a34c3,b
        move    b,x0
        mpy     y0,x0,b
        sub     #>$2d3713,b
        move    b,x0
        mpy     y0,x0,b
        add     #>$5c4169,b
        move    b,x0
        mpy     y0,x0,b
        add     #>$000045,b  ; 0.5 log2(1 + u)
        asr     #$5,b,b
        move    x1,a
        asl     #$11,a,a
        sub     a,b  ; - e/64
        rts     
