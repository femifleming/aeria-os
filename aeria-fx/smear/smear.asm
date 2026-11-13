; CYCLES_FORWARD_BRANCHES
; AERIA SMEAR -- a time-domain stand-in for Balloon Shop's spectral smear
; and Snow freeze: two input allpass diffusers into a Dattorro-style
; figure-8 tank (two modulated allpasses, two plain allpasses, four delays,
; in-loop damping), read by three taps per side.
;
;  SMER  tank decay: RT60 0.3 .. 20 s (log), applied at four points per loop
;  DIFF  input diffusion 0..0.75 / 0.625 and tank allpass 0..0.7 / 0.5
;  TONE  in-loop damping lowpass (0 dark .. 127 none)
;  MOD   modulation depth of the two tank allpasses, 0..12 samples, 0.7 Hz
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
; One ring of 14509 words (m = $3dfd, ring 15,870), every line a segment of
; it (write age .. read age), below the Sidechain window.
;   IA1  age     0 ..   241
;   IA2  age   242 ..   415
;   MA   age   416 ..   878
;   DL1  age   879 ..  3776
;   AP2  age  3777 ..  4948
;   DL2  age  4949 ..  7368
;   MB   age  7369 ..  7984
;   DL3  age  7985 .. 10726
;   AP4  age 10727 .. 12454
;   DL4  age 12455 .. 14508

; r7: 00 base 01 ptr 02 clear  04/05 g t/s 06/07 damp t/s 08/09 input t/s
;   0a/0b diff t/s 0c/0d depth t/s 0e/0f mix t/s 10 lfo phase 11/12 lp A/B
;   13 A end 14 B end 15 diffused in 16 temp 18/19 A frac,int 1a/1b B frac,int
;   1c out L temp

init:
        move    #>$ffffff,m4
        move    r7,r4
        clr     a
        do      #48,>smizero
        move    a,x:(r4)+
smizero:
        nop     
        move    x:>$213,r4
        move    x:(r4),a
        move    a,x:(r7+$00)
        move    a,x:(r7+$01)
        move    #>$80,a
        move    a,x:(r7+$02)
        rts     
proc:
        move    #>$ffffff,m4
; FRZ: HOLD when the select is 1
        move    x:(r6+$4),a
        tst     a
        bne     smhold
        move    x:(r6),x0
        move    #>$183c4f,y0
        mpy     y0,x0,a
        bsr     smexp
        move    b,x0
        move    #>$0a730b,y0
        mpy     y0,x0,a
        bsr     smexp
        move    b,x:(r7+$04)
        move    x:(r6+$2),a
        move    #>$7f0000,x0
        cmp     x0,a
        beq     smdopen
        move    a,x0
        mpy     x0,x0,a
        move    a,x0
        move    #>$79999a,y0
        mpy     y0,x0,a
        add     #>$066666,a
        bra     smdset
smdopen:
        move    #>$7fffff,a
smdset:
        move    a,x:(r7+$06)
        move    #>$7fffff,a
        move    a,x:(r7+$08)
        bra     smcommon
smhold:
        move    #>$7fffff,a
        move    a,x:(r7+$04)
        move    a,x:(r7+$06)
        clr     a
        move    a,x:(r7+$08)
smcommon:
        move    x:(r6+$1),a
        move    a,x:(r7+$0a)
        move    x:(r6+$3),x0
        move    #>$180000,y0
        mpy     y0,x0,a
        move    x:(r6+$4),b
        tst     b
        beq     smdepth
        clr     a  ; HOLD parks the modulation: a lossless loop
smdepth:
        move    a,x:(r7+$0c)  ; depth: samples / 64
        move    x:(r6+$5),a
        move    #>$7f0000,x0
        cmp     x0,a
        bne     smmset
        move    #>$7fffff,a
smmset:
        move    a,x:(r7+$0e)
        move    x:(r7+$0e),a
        move    x:(r7+$0f),b
        sub     b,a
        abs     a
        cmp     #>$000100,a
        bge     smmsnap
        move    x:(r7+$0e),a
        move    a,x:(r7+$0f)
smmsnap:
        move    x:(r7+$04),a
        move    x:(r7+$05),b
        sub     b,a
        abs     a
        cmp     #>$001000,a
        bge     smgsnap
        move    x:(r7+$04),a
        move    a,x:(r7+$05)
smgsnap:
        move    x:(r7+$06),a
        move    x:(r7+$07),b
        sub     b,a
        abs     a
        cmp     #>$001000,a
        bge     smdsnap
        move    x:(r7+$06),a
        move    a,x:(r7+$07)
smdsnap:
        move    x:(r7+$0c),a
        move    x:(r7+$0d),b
        sub     b,a
        abs     a
        cmp     #>$000100,a
        bge     smosnap
        move    x:(r7+$0c),a
        move    a,x:(r7+$0d)
smosnap:
        move    x:(r7+$02),a
        tst     a
        beq     smready
        sub     #>$1,a
        move    a,x:(r7+$02)
        move    x:(r7+$04),b
        move    b,x:(r7+$05)
        move    x:(r7+$06),b
        move    b,x:(r7+$07)
        move    x:(r7+$08),b
        move    b,x:(r7+$09)
        move    x:(r7+$0a),b
        move    b,x:(r7+$0b)
        move    x:(r7+$0c),b
        move    b,x:(r7+$0d)
        cmp     #>$00007c,a
        bge     smnoclr
        asl     #$7,a,a
        move    x:(r7+$00),x0
        add     x0,a
        move    a,r4
        move    #>$3dfd,m4
        clr     a
        do      #128,>smclrlp
        move    a,y:(r4)+
smclrlp:
        nop     
        move    #>$ffffff,m4
smnoclr:
        rts     
smready:
        move    #>$3dfd,m4
        move    #>$3dfd,m5
        move    #>$1,n0
        move    x:(r7+$01),r5
        do      n7,>smend
        move    x:(r7+$04),a
        move    x:(r7+$05),b
        sub     b,a
        asr     #$b,a,a
        add     a,b
        move    b,x:(r7+$05)
        move    x:(r7+$06),a
        move    x:(r7+$07),b
        sub     b,a
        asr     #$b,a,a
        add     a,b
        move    b,x:(r7+$07)
        move    x:(r7+$08),a
        move    x:(r7+$09),b
        sub     b,a
        asr     #$b,a,a
        add     a,b
        move    b,x:(r7+$09)
        move    x:(r7+$0a),a
        move    x:(r7+$0b),b
        sub     b,a
        asr     #$8,a,a
        add     a,b
        move    b,x:(r7+$0b)
        move    x:(r7+$0c),a
        move    x:(r7+$0d),b
        sub     b,a
        asr     #$8,a,a
        add     a,b
        move    b,x:(r7+$0d)
        move    x:(r7+$0e),a
        move    x:(r7+$0f),b
        sub     b,a
        asr     #$7,a,a
        add     a,b
        move    b,x:(r7+$0f)
; ---- LFO and the two modulated read offsets (opposite phase) ----
        move    x:(r7+$10),a
        add     #>$00010a,a
        move    a1,x:(r7+$10)
        move    a1,a
        move    a,x0
        abs     a
        move    #>$7fffff,b
        sub     a,b
        move    b,y0
        mpy     y0,x0,a
        asl     #$2,a,a
        move    a,x0  ; parabolic sine
        move    x:(r7+$0d),y0
        mpy     y0,x0,a  ; mod / 64 samples, signed
        move    a,x:(r7+$16)
        move    a1,b
        asr     #$11,a,a
        move    a1,a
        and     #>$01ffff,b
        asl     #$6,b,b
        move    b1,x:(r7+$18)
        neg     a
        sub     #>$000361,a
        move    a1,x:(r7+$19)  ; -(end + D + int)
        move    x:(r7+$16),a
        neg     a
        move    a1,b
        asr     #$11,a,a
        move    a1,a
        and     #>$01ffff,b
        asl     #$6,b,b
        move    b1,x:(r7+$1a)
        neg     a
        sub     #>$001f23,a
        move    a1,x:(r7+$1b)  ; -(end + D + int)
; ---- mono in, input gain (freeze fades it), four diffusers ----
        move    x:(r0),a
        move    x:(r0+n0),x0
        add     x0,a
        asr     #$3,a,a  ; mono / 4: tank headroom
        move    a,x0
        move    x:(r7+$09),y1
        mpy     x0,y1,a
        move    x:(r7+$0b),x0
        move    #>$600000,y1
        mpy     x0,y1,b
        move    b,y1
        move    #>$ffff0f,n5
        move    y:(r5+n5),x0  ; IA1 end
        mac     -x0,y1,a
        move    #>$000000,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    x:(r7+$0b),x0
        move    #>$500000,y1
        mpy     x0,y1,b
        move    b,y1
        move    #>$fffe61,n5
        move    y:(r5+n5),x0  ; IA2 end
        mac     -x0,y1,a
        move    #>$ffff0e,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    a,x:(r7+$15)
; ---- tank ends (previous values) ----
        move    #>$ffe338,n5
        move    y:(r5+n5),x0
        move    x0,x:(r7+$13)
        move    #>$ffc754,n5
        move    y:(r5+n5),x0
        move    x0,x:(r7+$14)
; ---- tank half A ----
        move    x:(r7+$14),x0
        move    x:(r7+$05),y1
        mpy     x0,y1,a
        move    x:(r7+$15),x0
        add     x0,a
        move    x:(r7+$0b),x0
        move    #>$59999a,y1
        mpy     x0,y1,b
        move    b,y1
        move    x:(r7+$19),n5
        lua     (r5+n5),r4
        move    x:(r7+$18),y0
        move    y:(r4)-,x0
        move    y:(r4),b
        sub     x0,b
        move    b,x1
        mpy     x1,y0,b
        add     x0,b
        move    b,x0  ; MA interpolated end
        mac     -x0,y1,a
        move    #>$fffe60,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    #>$fffc91,n5
        move    a,y:(r5+n5)
        move    #>$fff140,n5
        move    y:(r5+n5),a
        move    x:(r7+$11),x0
        sub     x0,a
        move    a,x1
        move    x:(r7+$07),y1
        mpy     y1,x1,a
        add     x0,a
        move    a,x:(r7+$11)
        move    a,x0
        move    x:(r7+$05),y1
        mpy     x0,y1,a  ; x decay
        move    x:(r7+$0b),x0
        move    #>$400000,y1
        mpy     x0,y1,b
        move    b,y1
        move    #>$ffecac,n5
        move    y:(r5+n5),x0  ; AP2 end
        mac     -x0,y1,a
        move    #>$fff13f,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    #>$ffecab,n5
        move    a,y:(r5+n5)
; ---- tank half B ----
        move    x:(r7+$13),x0
        move    x:(r7+$05),y1
        mpy     x0,y1,a
        move    x:(r7+$15),x0
        add     x0,a
        move    x:(r7+$0b),x0
        move    #>$59999a,y1
        mpy     x0,y1,b
        move    b,y1
        move    x:(r7+$1b),n5
        lua     (r5+n5),r4
        move    x:(r7+$1a),y0
        move    y:(r4)-,x0
        move    y:(r4),b
        sub     x0,b
        move    b,x1
        mpy     x1,y0,b
        add     x0,b
        move    b,x0  ; MB interpolated end
        mac     -x0,y1,a
        move    #>$ffe337,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    #>$ffe0cf,n5
        move    a,y:(r5+n5)
        move    #>$ffd61a,n5
        move    y:(r5+n5),a
        move    x:(r7+$12),x0
        sub     x0,a
        move    a,x1
        move    x:(r7+$07),y1
        mpy     y1,x1,a
        add     x0,a
        move    a,x:(r7+$12)
        move    a,x0
        move    x:(r7+$05),y1
        mpy     x0,y1,a  ; x decay
        move    x:(r7+$0b),x0
        move    #>$400000,y1
        mpy     x0,y1,b
        move    b,y1
        move    #>$ffcf5a,n5
        move    y:(r5+n5),x0  ; AP4 end
        mac     -x0,y1,a
        move    #>$ffd619,n5
        move    a,y:(r5+n5)
        move    a,x1
        move    x0,a
        mac     y1,x1,a
        move    #>$ffcf59,n5
        move    a,y:(r5+n5)
; ---- output taps ----
        clr     a
        move    #>$ffdfbd,n5
        move    y:(r5+n5),x0
        add     x0,a
        move    #>$ffda62,n5
        move    y:(r5+n5),x0
        add     x0,a
        move    #>$fff92c,n5
        move    y:(r5+n5),x0
        sub     x0,a
        asr     a
        move    a,x:(r7+$1c)
        clr     a
        move    #>$fffade,n5
        move    y:(r5+n5),x0
        add     x0,a
        move    #>$fff536,n5
        move    y:(r5+n5),x0
        add     x0,a
        move    #>$ffdd10,n5
        move    y:(r5+n5),x0
        sub     x0,a
        asr     a
        move    a,x:(r7+$16)
        move    x:(r7+$1c),a
        move    x:(r7+$0f),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    x:(r7+$16),a
        move    x:(r7+$0f),y1
        move    x:(r0),b
        sub     b,a
        asr     a
        move    a,x0
        mpy     x0,y1,a
        asl     a
        add     b,a
        move    a,x:(r0)+
        move    (r5)+
smend:
        nop     
        move    r5,x:(r7+$01)
        move    #>$ffffff,m4
        move    #>$ffffff,m5
        rts     
; ---- subroutines (after their callers) ----
smexp:
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
        do      x1,>smxpend
        asr     b
smxpend:
        asl     b
        rts     
