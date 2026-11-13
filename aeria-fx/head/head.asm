; ---------------------------------------------------------------------------
; AERIA HEAD -- the input mixer and MAIN FEEDBACK at the head of AERIA's
; effect chain (FX1 of T5, the THRU track carrying inputs A B C D).
;
;   out = in * (1 + d(IN))                     IN 64 = unity, 0 = silence, 127 = x1.98
;       + SMPL * T1(key bus)  + STAR * T2(key bus)
;       + FDBK' * clip(drive * g * F(T8(key bus))) / drive
;
; TRACK MAP (rig5, 9 Nov 2026, the chain on core 0): HEAD on T5 FX1 (THRU,
; core 0); T6-T8 NEIGHBOR; T1 SAMPLE (FLEX) and T2 STARS (STATIC), TAP on
; FX1, core 1. The three key tracks are the three `move #>n,a` lines marked
; KEY below (n = track - 1; dsp_asm has no equ): SMPL = 0 (T1), STAR = 1
; (T2), FEEDBACK = 7 (T8). Any track 0..7 works from any track: same core
; reads the local bus, the other core the window.
;
; F is Cloudfin's global-loop conditioning (Engine.h 121-197): a 15 Hz DC
; blocker, a 70 Hz high-pass (here two one-pole sections at 70 Hz), a
; one-pole low-pass on FTON, then a level-riding gain g (peak envelope,
; 4 ms attack, 350 ms release, threshold 0.3: g = min(1, 0.3/env)), then
; the soft clip u - u^3/3 (u limited to +/-1, so |clip| <= 2/3) with WARM's
; drive d = 1 + 3w in front and 1/d after: small signals see unity gain at
; every WARM, so WARM adds saturation without raising the loop gain.
; FDBK' = FDBK * 0.95 * 128/127 (0 .. 0.95). Below an envelope of 2^-15
; (about -90 dBFS) FDBK' goes to 0, so an idle loop ends in digital
; silence instead of a fixed-point limit cycle.
;
; THE KEY BUS (the base's KEYBUS; ring-sc/ringsc.asm is the reference
; reader): every track's 32 words of audio as it enters FX1, per block, at
;   this core's bus    Y:$800 + track*$40 + (r0 & $1f)
;   the shared window  Y:other_base + ((track&3)*4 + ((own_counter+2)&3))*$20 + (r0 & $1f)
; with base $33e00 (T5-8 core) / $3be00 (T1-4 core), each core's seed mark
; at base-2 (= $10000 exactly when live) and counter at base-1 (0..3).
; With the map above HEAD on T5 reads T1 and T2 from the T1-4 core's window
; (two generations behind core 0's counter: 32 samples) and T8 from its own
; core's bus: T8's FX1 input is T7's output, the end of the chain the bus can
; see; T8 is dispatched after T5, so that is the previous block (16 samples).
; Which bus each read uses follows from X:$420 (the track being dispatched).
; Both cores' seed marks must be valid or the block runs the IN-only loop (sends and feedback silent, their
; ramps and filter states cleared), so a card image or a not-yet-seeded bus
; never feeds memory nothing writes. HEAD writes nothing in the bus or the
; window.
;
; Every knob is ramped per sample: a target per call, a step that reaches it
; in 16 samples, the value landing exactly on the target (ring-sc's ramp).
; FDBK 0, SMPL 0, STAR 0 and IN 64 leave every added term at exactly 0, so
; the output is the input bit for bit (live bus or not).
;
; Parameters (page 1 arrive as value<<16, LFO fraction in bits 8-15):
;   x:(r6+0) FDBK  x:(r6+1) FTON  x:(r6+2) SMPL
;   x:(r6+3) STAR  x:(r6+4) IN    x:(r6+5) WARM
;
; r7 block (34 words):
;   $00/$01 d(IN) value/step         $02/$03 SMPL gain    $04/$05 STAR gain
;   $06/$07 FDBK' (with 1/d)         $08/$09 g*d/4        $0a/$0b LP coefficient
;   $0c envelope (block rate)        $0d peak of the last call
;   $0e this core's half (0 or 4)    $0f g (scratch)
;   $10-$14 DC stage  [k, xpL, xpR, ypL, ypR]
;   $15-$19 HP stage 1, $1a-$1e HP stage 2 (same layout)
;   $1f/$20 LP state L/R             $21 1/d (scratch)
;
; Multiplies use only y0,x0 / x1,y0 / x0,x0 / y1,y0 / x0,y1 (signed
; encodings; the harness disassembles and refuses mpysu/macsu). No rep.
; Labels are `hd` + two digits, none a prefix of another. Every displaced
; move is within -64..63.
; ---------------------------------------------------------------------------
init:
        move    r7,r5                   ; r1 is the dispatcher's across init
        move    #>$ffffff,m5
        clr     a
        do      #<$22,>hd01
        move    a,x:(r5)+
hd01:
        rts

proc:
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    #>$ffffff,m5
; ---- IN: d = (v - 64)/64, 0 at 64, -1 at 0 ---------------------------------
        move    x:(r6+$4),a             ; knob word: positive, a0 and a2 clear
        sub     #>$400000,a
        asl     a
        move    r7,r4
        bsr     hd70
; ---- is the key bus live on both cores? ------------------------------------
        move    x:>$420,b
        and     #<4,b
        move    b1,x:(r7+$e)            ; this core's half: 4 (T5-8) or 0 (T1-4)
        move    b1,a
        asl     #$d,a,a
        move    a1,x0                   ; $8000 on the T5-8 core, else 0
        move    #>$3bdfe,a
        sub     x0,a
        move    a1,r5                   ; this core's seed mark
        move    y:(r5)+,b
        cmp     #>$10000,b
        bne     hd20
        move    y:(r5),b
        and     #>$fffffc,b             ; Z from b1 alone
        bne     hd20
        move    #>$33dfe,a
        add     x0,a
        move    a1,r5                   ; the other core's seed mark
        move    y:(r5)+,b
        cmp     #>$10000,b
        bne     hd20
        move    y:(r5),b
        and     #>$fffffc,b
        bne     hd20

; ==== LIVE ==================================================================
; ---- the three keys' addresses for this call --------------------------------
        move    r0,b
        and     #<$1f,b
        move    b1,x1                   ; this call's offset in the block
        move    #>0,a                   ; KEY SMPL: T1 (track - 1)
        bsr     hd80
        move    a1,r1
        move    #>1,a                   ; KEY STAR: T2
        bsr     hd80
        move    a1,r2
        move    #>7,a                   ; KEY FEEDBACK: T8
        bsr     hd80
        move    a1,r3
; ---- filter constants (rewritten every call) --------------------------------
        move    #>$0045f4,x0            ; 1 - exp(-2 pi 15/44100)
        move    x0,x:(r7+$10)
        move    #>$01452e,x0            ; 1 - exp(-2 pi 70/44100)
        move    x0,x:(r7+$15)
        move    x0,x:(r7+$1a)
; ---- envelope at block rate from the last call's peak -----------------------
        move    x:(r7+$d),a
        move    x:(r7+$c),x0
        sub     x0,a
        move    a,x1                    ; peak - env
        move    #>$0b1959,y0            ; attack: 1 - exp(-16/(0.004 fs))
        bgt     hd30
        move    #>$0021f3,y0            ; release: 1 - exp(-16/(0.350 fs))
hd30:
        tfr     x0,a
        mac     x1,y0,a
        move    a,x:(r7+$c)
        clr     b
        move    b,x:(r7+$d)             ; the peak restarts
; ---- g = min(1, 0.3/env) ------------------------------------------------------
        move    x:(r7+$c),x0
        move    #>$266666,a             ; threshold 0.3
        cmp     x0,a
        move    #>$7fffff,x1
        bge     hd31                    ; env <= 0.3: g = 1
        bsr     hd60                    ; x1 = 0.3/env
hd31:
        move    x1,x:(r7+$f)
; ---- WARM: dq = d/4 = 0.25 + 0.75w; 1/d = 0.25/dq ---------------------------
        move    x:(r6+$5),x1
        move    #>$600000,y0
        mpy     x1,y0,a
        add     #>$200000,a
        move    a,x0                    ; dq
        move    #>$200000,a
        cmp     x0,a
        move    #>$7fffff,x1
        bge     hd32                    ; WARM 0: 1/d = 1
        bsr     hd60                    ; x1 = 0.25/dq
hd32:
        move    x1,x:(r7+$21)
        move    x:(r7+$f),y0
        mpy     y0,x0,a                 ; g * d/4
        lua     (r7+$8),r4
        bsr     hd70
; ---- FDBK' = FDBK * 0.9575 / d, 0 below the gate ----------------------------
        move    x:(r6+$0),x1
        move    #>$7a8eb7,y0            ; 0.95 * 128/127
        mpy     x1,y0,a
        move    a,x1
        move    x:(r7+$21),y0
        mpy     x1,y0,a
        move    x:(r7+$c),b
        cmp     #>$000100,b             ; envelope below 2^-15: no feedback
        bge     hd33
        clr     a
hd33:
        lua     (r7+$6),r4
        bsr     hd70
; ---- SMPL and STAR: v/128 * 129/128 (127 = full within 2^-14) ---------------
        move    x:(r6+$2),a
        asr     #7,a,b
        add     b,a
        lua     (r7+$2),r4
        bsr     hd70
        move    x:(r6+$3),a
        asr     #7,a,b
        add     b,a
        lua     (r7+$4),r4
        bsr     hd70
; ---- FTON: c = q^2, q = 0.9 v/128 + 0.1 (about 70 Hz at 0, open at 127) -----
        move    x:(r6+$1),x1
        move    #>$733333,y0
        mpy     x1,y0,a
        add     #>$0ccccd,a
        move    a,x0
        mpy     x0,x0,a
        lua     (r7+$a),r4
        bsr     hd70

; ---- the sample loop -----------------------------------------------------------
        do      n7,>hd49
; ramps
        move    x:(r7+$0),a
        move    x:(r7+$1),x0
        add     x0,a
        move    a,x:(r7+$0)
        move    x:(r7+$2),a
        move    x:(r7+$3),x0
        add     x0,a
        move    a,x:(r7+$2)
        move    x:(r7+$4),a
        move    x:(r7+$5),x0
        add     x0,a
        move    a,x:(r7+$4)
        move    x:(r7+$6),a
        move    x:(r7+$7),x0
        add     x0,a
        move    a,x:(r7+$6)
        move    x:(r7+$8),a
        move    x:(r7+$9),x0
        add     x0,a
        move    a,x:(r7+$8)
        move    x:(r7+$a),a
        move    x:(r7+$b),x0
        add     x0,a
        move    a,x:(r7+$a)
; the end of the chain (the FEEDBACK key's FX1 input), through DC, HP, HP
        move    y:(r3)+,a
        move    y:(r3)+,b
        lua     (r7+$10),r4
        move    x:(r4)+,y0              ; k
        move    x:(r4)+,x0              ; xpL
        move    x:(r4)-,x1              ; xpR
        sub     x0,a    a,x:(r4)+       ; x - xp; xp = x
        sub     x1,b    b,x:(r4)+
        move    x:(r4)+,x0              ; ypL
        add     x0,a    x:(r4)-,x1      ; + yp; x1 = ypR
        add     x1,b
        mac     -y0,x0,a                ; - k yp
        mac     -x1,y0,b
        move    a,x:(r4)+
        move    b,x:(r4)+
        move    x:(r4)+,y0
        move    x:(r4)+,x0
        move    x:(r4)-,x1
        sub     x0,a    a,x:(r4)+
        sub     x1,b    b,x:(r4)+
        move    x:(r4)+,x0
        add     x0,a    x:(r4)-,x1
        add     x1,b
        mac     -y0,x0,a
        mac     -x1,y0,b
        move    a,x:(r4)+
        move    b,x:(r4)+
        move    x:(r4)+,y0
        move    x:(r4)+,x0
        move    x:(r4)-,x1
        sub     x0,a    a,x:(r4)+
        sub     x1,b    b,x:(r4)+
        move    x:(r4)+,x0
        add     x0,a    x:(r4)-,x1
        add     x1,b
        mac     -y0,x0,a
        mac     -x1,y0,b
        move    a,x:(r4)+
        move    b,x:(r4)+
; FTON low-pass: y += c (x - y)
        move    x:(r7+$a),y0
        move    x:(r4)+,x0
        move    x:(r4)-,x1
        sub     x0,a
        sub     x1,b
        move    a,y1
        tfr     x0,a
        mac     y1,y0,a
        move    b,y1
        tfr     x1,b
        mac     y1,y0,b
        move    a,x:(r4)+
        move    b,x:(r4)+
; the peak for the envelope
        move    a,x0                    ; fL
        move    b,x1                    ; fR
        abs     a
        abs     b
        move    b,y1
        cmp     y1,a
        tlt     y1,a
        move    x:(r7+$d),y1
        cmp     y1,a
        tlt     y1,a
        move    a,x:(r7+$d)
; u = 4 (g d/4) f, limited to +/-1; clip = u - u^3/3
        move    x:(r7+$8),y0
        mpy     y0,x0,a
        mpy     x1,y0,b
        asl     #2,a,a
        asl     #2,b,b
        move    a,x0                    ; uL
        move    b,x1                    ; uR
        move    #>$2aaaab,y0            ; 1/3
        mpy     x0,x0,a
        move    a,y1
        mpy     y1,y0,a
        move    a,y1
        tfr     x0,a
        mac     -x0,y1,a                ; clip L
        move    x1,x0
        mpy     x0,x0,b
        move    b,y1
        mpy     y1,y0,b
        move    b,y1
        tfr     x0,b
        mac     -x0,y1,b                ; clip R
; the mix
        move    a,x0
        move    b,x1
        move    x:(r7+$6),y0            ; FDBK' (1/d in it)
        mpy     y0,x0,a
        mpy     x1,y0,b
        move    x:(r0)+,x0              ; in L
        move    x:(r0)-,x1              ; in R
        add     x0,a
        add     x1,b
        move    x:(r7+$0),y0            ; d(IN)
        mac     y0,x0,a
        mac     x1,y0,b
        move    y:(r1)+,x0              ; SMPL key
        move    y:(r1)+,x1
        move    x:(r7+$2),y0
        mac     y0,x0,a
        mac     x1,y0,b
        move    y:(r2)+,x0              ; STAR key
        move    y:(r2)+,x1
        move    x:(r7+$4),y0
        mac     y0,x0,a
        mac     x1,y0,b
        move    a,x:(r0)+
        move    b,x:(r0)+
hd49:
        rts

; ==== NOT LIVE: IN only; sends, feedback and their state cleared ============
hd20:
        lua     (r7+$2),r4
        clr     a
        do      #<$1f,>hd21
        move    a,x:(r4)+
hd21:
        do      n7,>hd29
        move    x:(r7+$0),a
        move    x:(r7+$1),x0
        add     x0,a
        move    a,x:(r7+$0)
        move    a,y0
        move    x:(r0)+,x0
        move    x:(r0)-,x1
        tfr     x0,a
        tfr     x1,b
        mac     y0,x0,a
        mac     x1,y0,b
        move    a,x:(r0)+
        move    b,x:(r0)+
hd29:
        rts

; ---- x1 = a / x0 for 0 < a < x0 (fractions): 24 div steps, quotient in a0 --
hd60:
        andi    #$fe,ccr
        do      #<24,>hd61
        div     x0,a
hd61:
        move    a0,x1
        rts

; ---- ramp: a = target, r4 -> [value, step]: the step that lands in 16 -------
; Within 16 LSB the step is 0 and the value is set to the target.
hd70:
        move    a,x1                    ; target, limited
        move    x1,a                    ; clean: a0 = 0
        move    x:(r4),x0
        sub     x0,a
        asr     #4,a,a
        move    a,x0
        move    x0,a                    ; drop a0 before the test
        tst     a
        bne     hd71
        move    x1,x:(r4)
hd71:
        move    a,x:(r4+1)
        rts

; ---- a = key track 0..7 -> a1 = its Y address for this call ------------------
; x1 = the call's offset in the block (kept). Uses b, x0, y0, y1, r5.
hd80:
        move    a1,y1                   ; key
        and     #<4,a                   ; its core's half
        move    x:(r7+$e),x0            ; ours
        cmp     x0,a
        bne     hd81
        move    y1,a                    ; same core: Y:$800 + key*$40 + offset
        asl     #6,a,a
        add     x1,a
        add     #>$800,a
        rts
hd81:
        move    x0,a                    ; other core: its window, two behind our counter
        asl     #$d,a,a
        move    a1,y0                   ; $8000 on the T5-8 core, else 0
        move    y1,a
        and     #<3,a
        asl     #2,a,a
        move    a1,x0                   ; the key's index on its core, times 4
        move    #>$3bdff,b
        sub     y0,b
        move    b1,r5
        move    y:(r5),b                ; this core's generation counter
        add     #<2,b
        and     #<3,b
        move    b1,a
        add     x0,a
        asl     #5,a,a
        add     x1,a
        add     #>$33e00,a
        add     y0,a
        rts
