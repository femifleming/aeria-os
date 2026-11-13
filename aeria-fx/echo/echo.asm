; CYCLES_FORWARD_BRANCHES
; AERIA ECHO -- stereo just-intonation multi-tap delay (Cloudfin StereoDelay,
; fixed point). Two hard-panned lines, both fed the mono sum, three taps each;
; wet = 0.62 (y0 + 0.8 y1 + 0.64 y2) through a wet lowpass at 1.5 x TONE; each
; side regenerates from its longest tap through the tone lowpass, a 10 Hz DC
; blocker and a soft clipper times FDBK.
;
;  TIME  base time T, 10 .. 220 ms (log); T x the set's largest ratio is
;        held to 16376 samples (the rings), keeping the ratios intact
;  FDBK  regeneration 0 .. 0.95
;  TONE  feedback lowpass 300 Hz .. 18 kHz (log); wet lowpass at 1.5 x, <= 20 kHz
;  RATO  tap-ratio set [L0 R0 L1 R1 L2 R2]: JUST MINR PYTH SEPT HARM UNIS
;  WDTH  127 = hard-panned (the plugin), 0 = both sides summed to mono
;  MIX   0 = exact dry
;
; Seamless taps: when any tap's target moves by more than max(0.75 sample,
; 0.2 %) all six open a second read head at the new time and crossfade
; (sin^2, 80 ms); a later change waits for the fade to end. Tap times are
; whole samples (static between fades, so nothing is interpolated).
;
; LEFT line: Y ring of 16384 words (m = $3fff) at the instance buffer base.
; RIGHT line: X ring of 16384 words (m = $3fff) at the absolute base X:$4000
; (generator constant XRING). Lines hold (mono + regen) / 2.
;
; r7: 00 y base 01 y ptr 02 clear 03 x ptr 04/05 mix t/s 06/07 cross t/s
;   08 fdbk 09 tone coef 0a wet coef 0b fade x 0c fading 0d tmp
;   0e/0f fade gains old/new 10.. targets (A-units) 16.. current (A-units)
;   1c.. current -samples 22.. next -samples 28.. next (A-units)
;   2e..33 left, 34..39 right: wet lp hi/lo, tone lp hi/lo, dc hi/lo
;   40..6f ratio table: per set T ceiling, six ratio/2, pad

init:
        move    #>$ffffff,m4
        move    r7,r4
        clr     a
        do      #64,>ecizero
        move    a,x:(r4)+
ecizero:
        nop     
; JUST
        move    #>$4cc333,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$480000,a
        move    a,x:(r4)+
        move    #>$500000,a
        move    a,x:(r4)+
        move    #>$555555,a
        move    a,x:(r4)+
        move    #>$600000,a
        move    a,x:(r4)+
        move    #>$6aaaab,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
; MINR
        move    #>$4ff600,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$480000,a
        move    a,x:(r4)+
        move    #>$4ccccd,a
        move    a,x:(r4)+
        move    #>$555555,a
        move    a,x:(r4)+
        move    #>$600000,a
        move    a,x:(r4)+
        move    #>$666666,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
; PYTH
        move    #>$4bd097,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$480000,a
        move    a,x:(r4)+
        move    #>$510000,a
        move    a,x:(r4)+
        move    #>$555555,a
        move    a,x:(r4)+
        move    #>$600000,a
        move    a,x:(r4)+
        move    #>$6c0000,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
; SEPT
        move    #>$554aaa,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$492492,a
        move    a,x:(r4)+
        move    #>$4aaaab,a
        move    a,x:(r4)+
        move    #>$540000,a
        move    a,x:(r4)+
        move    #>$59999a,a
        move    a,x:(r4)+
        move    #>$600000,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
; HARM
        move    #>$4ebb13,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$480000,a
        move    a,x:(r4)+
        move    #>$500000,a
        move    a,x:(r4)+
        move    #>$580000,a
        move    a,x:(r4)+
        move    #>$600000,a
        move    a,x:(r4)+
        move    #>$680000,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
; UNIS
        move    #>$7ff000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        move    #>$400000,a
        move    a,x:(r4)+
        clr     a
        move    a,x:(r4)+
        move    x:>$213,r4
        move    x:(r4),a
        move    a,x:(r7+$00)
        move    a,x:(r7+$01)
        move    #>$004000,a
        move    a,x:(r7+$03)
        move    #>$80,a
        move    a,x:(r7+$02)
        rts     
proc:
        move    #>$ffffff,m4
        move    #>$1,n0
; FDBK
        move    x:(r6+$1),x0
        move    #>$7a8eb7,y0
        mpy     y0,x0,a
        move    a,x:(r7+$08)
; TONE: f = 18 k * 60^-(1 - knob); coef = 1 - e^-w; wet at 1.5 f <= 20 k
        move    #>$7f0000,a
        move    x:(r6+$2),x0
        move    #>$0,x1
        sub     x0,a
        tlt     x1,a
        move    a,x0
        move    #>$17d048,y0
        mpy     y0,x0,a
        bsr     ecexp
        move    b,x0
        move    x0,x:(r7+$0d)
        move    #>$0eccaf,y0
        mpy     y0,x0,a
        bsr     ecexp
        move    b,x0
        move    #>$7fffff,a
        sub     x0,a
        move    a,x:(r7+$09)
        move    x:(r7+$0d),x0
        move    #>$163306,y0
        mpy     y0,x0,a
        move    #>$1071a6,x0
        cmp     x0,a
        tgt     x0,a
        bsr     ecexp
        move    b,x0
        move    #>$7fffff,a
        sub     x0,a
        move    a,x:(r7+$0a)
; RATO: r4 -> the set's row (index held to the table)
        move    x:(r6+$3),x0
        move    #>$7f0000,a
        and     x0,a
        asr     #$d,a,a
        move    a1,x0
        move    x0,a
        move    #>$000028,x0
        cmp     x0,a
        tgt     x0,a
        add     #>$000040,a
        move    r7,x0
        add     x0,a
        move    a1,r4
; TIME: T = 220 ms * 22^-(1 - knob), held to the set's ceiling; six targets
        move    #>$7f0000,a
        move    x:(r6),x0
        move    #>$0,x1
        sub     x0,a
        tlt     x1,a
        move    a,x0
        move    #>$11fa6a,y0
        mpy     y0,x0,a
        bsr     ecexp
        move    b,x0
        move    #>$4bcc00,y0
        mpy     y0,x0,a
        move    x:(r4)+,x0
        cmp     x0,a
        tgt     x0,a
        move    a,y0
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$10)
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$11)
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$12)
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$13)
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$14)
        move    x:(r4)+,x0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$15)
; MIX, WDTH (cross = 1 - width)
        move    x:(r6+$5),a
        move    #>$7f0000,x0
        cmp     x0,a
        blt     ecmset
        move    #>$7fffff,a
ecmset:
        move    a,x:(r7+$04)
        move    #>$7f0000,a
        move    x:(r6+$4),x0
        move    #>$0,x1
        sub     x0,a
        tlt     x1,a
        move    a,x0
        move    #>$408102,y0
        mpy     y0,x0,a
        asl     a
        move    a,x:(r7+$06)
        move    x:(r7+$04),a
        move    x:(r7+$05),b
        sub     b,a
        abs     a
        cmp     #>$000100,a
        bge     ecmsnap
        move    x:(r7+$04),a
        move    a,x:(r7+$05)
ecmsnap:
        move    x:(r7+$06),a
        move    x:(r7+$07),b
        sub     b,a
        abs     a
        cmp     #>$000100,a
        bge     ecwsnap
        move    x:(r7+$06),a
        move    a,x:(r7+$07)
ecwsnap:
; bounded clearing: 128 dry calls, 128 words of each ring a call; taps snap
        move    x:(r7+$02),a
        tst     a
        beq     ecready
        sub     #>$1,a
        move    a,x:(r7+$02)
        move    x:(r7+$10),a
        move    a,x:(r7+$16)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$1c)
        move    x:(r7+$11),a
        move    a,x:(r7+$17)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$1d)
        move    x:(r7+$12),a
        move    a,x:(r7+$18)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$1e)
        move    x:(r7+$13),a
        move    a,x:(r7+$19)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$1f)
        move    x:(r7+$14),a
        move    a,x:(r7+$1a)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$20)
        move    x:(r7+$15),a
        move    a,x:(r7+$1b)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$21)
        clr     a
        move    a,x:(r7+$0c)
        move    a,x:(r7+$0b)
        move    x:(r7+$06),a
        move    a,x:(r7+$07)
        move    x:(r7+$02),a
        cmp     #>$000080,a
        bge     ecnoclry
        asl     #$7,a,a
        move    x:(r7+$00),x0
        add     x0,a
        move    a,r4
        move    #>$003fff,m4
        clr     a
        do      #128,>ecclry
        move    a,y:(r4)+
ecclry:
        nop     
        move    #>$ffffff,m4
ecnoclry:
        move    x:(r7+$02),a
        cmp     #>$000080,a
        bge     ecnoclrx
        asl     #$7,a,a
        add     #>$004000,a
        move    a,r4
        move    #>$003fff,m4
        clr     a
        do      #128,>ecclrx
        move    a,x:(r4)+
ecclrx:
        nop     
        move    #>$ffffff,m4
ecnoclrx:
        rts     
ecready:
; ---- seamless-tap state machine (block rate) ----
        move    x:(r7+$0c),a
        tst     a
        beq     eccheck
        move    x:(r7+$0b),a
        cmp     #>$7fffff,a
        blt     ecrun
; fade complete: the new heads become the current ones
        move    x:(r7+$28),a
        move    a,x:(r7+$16)
        move    x:(r7+$22),a
        move    a,x:(r7+$1c)
        move    x:(r7+$29),a
        move    a,x:(r7+$17)
        move    x:(r7+$23),a
        move    a,x:(r7+$1d)
        move    x:(r7+$2a),a
        move    a,x:(r7+$18)
        move    x:(r7+$24),a
        move    a,x:(r7+$1e)
        move    x:(r7+$2b),a
        move    a,x:(r7+$19)
        move    x:(r7+$25),a
        move    a,x:(r7+$1f)
        move    x:(r7+$2c),a
        move    a,x:(r7+$1a)
        move    x:(r7+$26),a
        move    a,x:(r7+$20)
        move    x:(r7+$2d),a
        move    a,x:(r7+$1b)
        move    x:(r7+$27),a
        move    a,x:(r7+$21)
        clr     a
        move    a,x:(r7+$0c)
eccheck:
; any |target - current| > max(0.75 sample, 0.2 %) ?
        move    x:(r7+$10),a
        move    x:(r7+$16),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        move    x:(r7+$11),a
        move    x:(r7+$17),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        move    x:(r7+$12),a
        move    x:(r7+$18),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        move    x:(r7+$13),a
        move    x:(r7+$19),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        move    x:(r7+$14),a
        move    x:(r7+$1a),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        move    x:(r7+$15),a
        move    x:(r7+$1b),x0
        sub     x0,a
        abs     a
        move    #>$004189,y0
        mpy     y0,x0,b
        move    #>$180,x1
        cmp     x1,b
        tlt     x1,b
        move    b,x1
        cmp     x1,a
        bgt     ecstart
        bra     ecrun
ecstart:
        move    x:(r7+$10),a
        move    a,x:(r7+$28)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$22)
        move    x:(r7+$11),a
        move    a,x:(r7+$29)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$23)
        move    x:(r7+$12),a
        move    a,x:(r7+$2a)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$24)
        move    x:(r7+$13),a
        move    a,x:(r7+$2b)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$25)
        move    x:(r7+$14),a
        move    a,x:(r7+$2c)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$26)
        move    x:(r7+$15),a
        move    a,x:(r7+$2d)
        add     #>$100,a
        asr     #$9,a,a
        move    a1,a
        neg     a
        move    a1,x:(r7+$27)
        clr     a
        move    a,x:(r7+$0b)
        move    #>$1,a
        move    a,x:(r7+$0c)
ecrun:
        move    #>$003fff,m5
        move    #>$003fff,m4
        move    x:(r7+$01),r5
        move    x:(r7+$03),r4
        move    x:(r7+$0c),a
        tst     a
        bne     ecfade
        do      n7,>ecplend
        move    x:(r7+$04),a
        move    x:(r7+$05),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$05)
        move    x:(r7+$06),a
        move    x:(r7+$07),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$07)
; ======== left (Y ring) ========
        move    x:(r7+$1c),n5
        move    y:(r5+n5),x1
        move    x:(r7+$1e),n5
        move    y:(r5+n5),x0
        move    x:(r7+$20),n5
        move    y:(r5+n5),y0
; wet = 2 (w0 t0 + w1 t1 + w2 t2), limited, then the wet lowpass (48-bit state)
        move    #>$4f5c29,y1
        mpy     y1,x1,a
        move    #>$3f7cee,y1
        mac     x0,y1,a
        move    #>$32ca58,y1
        mac     y1,y0,a
        asl     a
        move    a,x0
        move    x:(r7+$2e),b
        move    x:(r7+$2f),b0
        move    x:(r7+$0a),y1
        mac     x0,y1,b
        move    x:(r7+$2e),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$2e)
        move    b0,x:(r7+$2f)
; regen: tone lowpass on the longest tap, dc blocker on its 48-bit output
        move    x:(r7+$30),b
        move    x:(r7+$31),b0
        move    x:(r7+$32),a
        move    x:(r7+$33),a0
        sub     b,a
        move    x:(r7+$09),y1
        mac     y1,y0,b
        move    x:(r7+$30),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$30)
        move    b0,x:(r7+$31)
        add     b,a
        move    x:(r7+$32),x0
        move    #>$002ea7,y1
        mac     -x0,y1,a
        move    a0,x:(r7+$33)
        move    a,x:(r7+$32)
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecp0za
        add     y,a
ecp0za:
        move    a,x0
        move    x:(r7+$08),y1
        mpy     x0,y1,a
        asl     a  ; full scale again
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        mpy     x1,x0,a
        move    a,y1
        move    x0,a
        move    #>$2aaaab,y0
        mac     -y1,y0,a
; push (mono + regen) / 2, truncated toward zero
        move    x:(r0),b
        move    x:(r0+n0),x0
        add     x0,b
        asr     b
        add     b,a
        asr     a
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecp0zb
        add     y,a
ecp0zb:
        move    a,y:(r5)
; ======== right (X ring) ========
        move    x:(r7+$1d),n4
        move    x:(r4+n4),x1
        move    x:(r7+$1f),n4
        move    x:(r4+n4),x0
        move    x:(r7+$21),n4
        move    x:(r4+n4),y0
; wet = 2 (w0 t0 + w1 t1 + w2 t2), limited, then the wet lowpass (48-bit state)
        move    #>$4f5c29,y1
        mpy     y1,x1,a
        move    #>$3f7cee,y1
        mac     x0,y1,a
        move    #>$32ca58,y1
        mac     y1,y0,a
        asl     a
        move    a,x0
        move    x:(r7+$34),b
        move    x:(r7+$35),b0
        move    x:(r7+$0a),y1
        mac     x0,y1,b
        move    x:(r7+$34),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$34)
        move    b0,x:(r7+$35)
; regen: tone lowpass on the longest tap, dc blocker on its 48-bit output
        move    x:(r7+$36),b
        move    x:(r7+$37),b0
        move    x:(r7+$38),a
        move    x:(r7+$39),a0
        sub     b,a
        move    x:(r7+$09),y1
        mac     y1,y0,b
        move    x:(r7+$36),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$36)
        move    b0,x:(r7+$37)
        add     b,a
        move    x:(r7+$38),x0
        move    #>$002ea7,y1
        mac     -x0,y1,a
        move    a0,x:(r7+$39)
        move    a,x:(r7+$38)
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecp1za
        add     y,a
ecp1za:
        move    a,x0
        move    x:(r7+$08),y1
        mpy     x0,y1,a
        asl     a  ; full scale again
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        mpy     x1,x0,a
        move    a,y1
        move    x0,a
        move    #>$2aaaab,y0
        mac     -y1,y0,a
; push (mono + regen) / 2, truncated toward zero
        move    x:(r0),b
        move    x:(r0+n0),x0
        add     x0,b
        asr     b
        add     b,a
        asr     a
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecp1zb
        add     y,a
ecp1zb:
        move    a,x:(r4)
; ---- width, mix, advance ----
        move    x:(r7+$34),a
        move    x:(r7+$2e),x0
        sub     x0,a
        asr     a
        move    a,x1
        move    x:(r7+$07),y0
        mpy     x1,y0,a
        move    a,x1  ; cross (R - L) / 2
        move    x0,a
        add     x1,a
        move    x:(r7+$05),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    x:(r7+$34),a
        sub     x1,a
        move    x:(r7+$05),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    (r5)+
        move    (r4)+
ecplend:
        nop     
        bra     ecexit
ecfade:
        do      n7,>ecfdend
        move    x:(r7+$04),a
        move    x:(r7+$05),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$05)
        move    x:(r7+$06),a
        move    x:(r7+$07),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$07)
; fade gain: x += 1/len (saturating); new = sin^2(pi x / 2), old = 1 - new
        move    x:(r7+$0b),a
        add     #>$00094a,a
        move    a,x:(r7+$0b)
        move    x:(r7+$0b),a
        sub     #>$400000,a
        asl     a
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        move    #>$04a728,y0
        mpy     x1,y0,a
        sub     #>$292988,a
        move    a,y0
        mpy     x1,y0,a
        add     #>$648260,a
        move    a,y0
        mpy     y0,x0,a
        add     #>$400000,a
        move    a,x0
        move    x0,x:(r7+$0f)
        move    #>$7fffff,a
        sub     x0,a
        move    a,x:(r7+$0e)
; ======== left (Y ring) ========
        move    x:(r7+$0e),y1
        move    x:(r7+$0f),y0
        move    x:(r7+$1c),n5
        move    y:(r5+n5),x0
        mpy     x0,y1,a
        move    x:(r7+$22),n5
        move    y:(r5+n5),x0
        mac     y0,x0,a
        move    a,x1
        move    x:(r7+$1e),n5
        move    y:(r5+n5),x0
        mpy     x0,y1,a
        move    x:(r7+$24),n5
        move    y:(r5+n5),x0
        mac     y0,x0,a
        move    a,b
        move    x:(r7+$20),n5
        move    y:(r5+n5),x0
        mpy     x0,y1,a
        move    x:(r7+$26),n5
        move    y:(r5+n5),x0
        mac     y0,x0,a
        move    b,x0
        move    a,y0
; wet = 2 (w0 t0 + w1 t1 + w2 t2), limited, then the wet lowpass (48-bit state)
        move    #>$4f5c29,y1
        mpy     y1,x1,a
        move    #>$3f7cee,y1
        mac     x0,y1,a
        move    #>$32ca58,y1
        mac     y1,y0,a
        asl     a
        move    a,x0
        move    x:(r7+$2e),b
        move    x:(r7+$2f),b0
        move    x:(r7+$0a),y1
        mac     x0,y1,b
        move    x:(r7+$2e),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$2e)
        move    b0,x:(r7+$2f)
; regen: tone lowpass on the longest tap, dc blocker on its 48-bit output
        move    x:(r7+$30),b
        move    x:(r7+$31),b0
        move    x:(r7+$32),a
        move    x:(r7+$33),a0
        sub     b,a
        move    x:(r7+$09),y1
        mac     y1,y0,b
        move    x:(r7+$30),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$30)
        move    b0,x:(r7+$31)
        add     b,a
        move    x:(r7+$32),x0
        move    #>$002ea7,y1
        mac     -x0,y1,a
        move    a0,x:(r7+$33)
        move    a,x:(r7+$32)
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecf0za
        add     y,a
ecf0za:
        move    a,x0
        move    x:(r7+$08),y1
        mpy     x0,y1,a
        asl     a  ; full scale again
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        mpy     x1,x0,a
        move    a,y1
        move    x0,a
        move    #>$2aaaab,y0
        mac     -y1,y0,a
; push (mono + regen) / 2, truncated toward zero
        move    x:(r0),b
        move    x:(r0+n0),x0
        add     x0,b
        asr     b
        add     b,a
        asr     a
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecf0zb
        add     y,a
ecf0zb:
        move    a,y:(r5)
; ======== right (X ring) ========
        move    x:(r7+$0e),y1
        move    x:(r7+$0f),y0
        move    x:(r7+$1d),n4
        move    x:(r4+n4),x0
        mpy     x0,y1,a
        move    x:(r7+$23),n4
        move    x:(r4+n4),x0
        mac     y0,x0,a
        move    a,x1
        move    x:(r7+$1f),n4
        move    x:(r4+n4),x0
        mpy     x0,y1,a
        move    x:(r7+$25),n4
        move    x:(r4+n4),x0
        mac     y0,x0,a
        move    a,b
        move    x:(r7+$21),n4
        move    x:(r4+n4),x0
        mpy     x0,y1,a
        move    x:(r7+$27),n4
        move    x:(r4+n4),x0
        mac     y0,x0,a
        move    b,x0
        move    a,y0
; wet = 2 (w0 t0 + w1 t1 + w2 t2), limited, then the wet lowpass (48-bit state)
        move    #>$4f5c29,y1
        mpy     y1,x1,a
        move    #>$3f7cee,y1
        mac     x0,y1,a
        move    #>$32ca58,y1
        mac     y1,y0,a
        asl     a
        move    a,x0
        move    x:(r7+$34),b
        move    x:(r7+$35),b0
        move    x:(r7+$0a),y1
        mac     x0,y1,b
        move    x:(r7+$34),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$34)
        move    b0,x:(r7+$35)
; regen: tone lowpass on the longest tap, dc blocker on its 48-bit output
        move    x:(r7+$36),b
        move    x:(r7+$37),b0
        move    x:(r7+$38),a
        move    x:(r7+$39),a0
        sub     b,a
        move    x:(r7+$09),y1
        mac     y1,y0,b
        move    x:(r7+$36),x0
        mac     -x0,y1,b
        move    b1,x:(r7+$36)
        move    b0,x:(r7+$37)
        add     b,a
        move    x:(r7+$38),x0
        move    #>$002ea7,y1
        mac     -x0,y1,a
        move    a0,x:(r7+$39)
        move    a,x:(r7+$38)
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecf1za
        add     y,a
ecf1za:
        move    a,x0
        move    x:(r7+$08),y1
        mpy     x0,y1,a
        asl     a  ; full scale again
        move    a,x0
        mpy     x0,x0,a
        move    a,x1
        mpy     x1,x0,a
        move    a,y1
        move    x0,a
        move    #>$2aaaab,y0
        mac     -y1,y0,a
; push (mono + regen) / 2, truncated toward zero
        move    x:(r0),b
        move    x:(r0+n0),x0
        add     x0,b
        asr     b
        add     b,a
        asr     a
        move    #>$0,y1
        move    #>$ffffff,y0
        tst     a
        bge     ecf1zb
        add     y,a
ecf1zb:
        move    a,x:(r4)
; ---- width, mix, advance ----
        move    x:(r7+$34),a
        move    x:(r7+$2e),x0
        sub     x0,a
        asr     a
        move    a,x1
        move    x:(r7+$07),y0
        mpy     x1,y0,a
        move    a,x1  ; cross (R - L) / 2
        move    x0,a
        add     x1,a
        move    x:(r7+$05),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    x:(r7+$34),a
        sub     x1,a
        move    x:(r7+$05),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    (r5)+
        move    (r4)+
ecfdend:
        nop     
ecexit:
        move    r5,x:(r7+$01)
        move    r4,x:(r7+$03)
        move    #>$ffffff,m4
        move    #>$ffffff,m5
        rts     
; ---- subroutine (after its callers) ----
ecexp:
        move    a1,b
        asr     #$12,a,a  ; a1 = n = floor(y)
        add     #>$1,a
        move    a1,x1  ; n + 1 (never 0)
        and     #>$03ffff,b
        asl     #$5,b,b  ; f in Q23
        move    b1,x0
        move    #>$2c5c86,y0
        mpy     y0,x0,b
        move    b,y0  ; g = f ln2 / 2
        move    #>$008889,x0
        mpy     y0,x0,b
        neg     b
        add     #>$02aaab,b
        move    b,x0
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
        do      x1,>ecxpend
        asr     b
ecxpend:
        asl     b
        rts     
