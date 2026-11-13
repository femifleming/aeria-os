; AERIA OS DSP engine (core 0). Our own code. Loaded at P:$33000 by the boot ROM or a parked core's stand-in.
;
; Audio: the ESAI as stock's payload sets it up (values read for understanding: AUDIO.md 3.1), fed by DMA:
;   DMA2: X:$8000-$80ff -> TX0, 32 samples x 8 slot words; the loop fills the half DMA2 is not reading.
;   DMA3: RX0 -> X:$8100-$833f, 144 samples x 4 words (C, D, A, B), continuous.
;   A frame is 16 samples. The loop waits for DSR2 = $80f0 (fill $8000) or $8070 (fill $8080).
; Each frame: the newest complete 16-sample input block -> the stereo work buffer X:$900 (32 words L,R),
;   through the effect chain (stock's FX ABI: r0 block, n7 frames, r6 knob block, r7 state, X:$213 -> a word
;   holding the Y buffer base, a = 1), then out to every output pair (cue 0/1, main 2/3, phones 4/5).
; Inputs (words of x:$200-$203, 24-bit gains, $7fffff = 1.0, set by the ColdFire):
;   L = A*g(A->L) + C*g(C->L) + ...  -- a 4x2 matrix at X:$210 (row per input: to L, to R).
; Master gain X:$218. Chain: 8 slots at X:$280, 8 words each:
;   +0 enabled  +1 proc address  +2 init address  +3 r6 (knob block)  +4 r7 (state)  +5 X:$213 value
;   +6 init pending (the engine calls init once, then clears it)
; Host port, ColdFire -> DSP: a word with bit 23 set is an address (bit 22: Y space, bits 21..0); the next word is
;   written there (the shared window aliases P, so effect code is loaded this way too).
;   Host flag HF0: hand back (as every AERIA OS DSP program).
; DSP -> ColdFire: every 32 frames, if the host has taken the last: the peaks of A, B, C, D and out L, out R as
;   (index << 20) | (peak >> 4), one word per frame.
; dsp_asm rules: no rep, no backward bra/bsr/bcc, no long jmp: backward jumps are `jclr #0,x:<$3f,label`
;   (x:$3f = 0). Labels never a prefix of another.
;
; X short: $3f zero, $30 the addr latch flag, $31 the latched address, $32 frame counter, $33 meter index,
;   $38-$3d peaks (A B C D outL outR).

start:
        move    #>$300,sr
        move    omr,x0
crb01:
        jclr    #1,x:<<$ffffc3,crb01
        movep   x0,x:<<$ffffc7
        movep   x:<<$fffffd,x0
crb02:
        jclr    #1,x:<<$ffffc3,crb02
        movep   x0,x:<<$ffffc7
crb03:
        jclr    #1,x:<<$ffffc3,crb03
        movep   #>$ae0001,x:<<$ffffc7
        movep   #>$3fff,y:<<$ffffe4
        bset    #$e,y:<<$fffffd
        bset    #$e,y:<<$fffffe
        movep   #>$40,y:<<$ffff94
        move    #>$123456,x0
        movep   x0,y:<<$ffff80
        movep   #>$f00f00,y:<<$ffff96
        movep   #>$37901,y:<<$ffff95
        movep   #>$900f00,y:<<$ffff98
        movep   #>$17901,y:<<$ffff97
        movep   #>$ff,y:<<$ffff9b
        movep   #>$0,y:<<$ffff9c
        movep   #>$ff,y:<<$ffff99
        movep   #>$0,y:<<$ffff9a
        movep   #>$ffffff,y:<<$ffff9f
        movep   #>$ffffff,y:<<$ffff9e
        clr     a
        move    a,x:<$3f
        move    a,x:<$30
        move    a,x:<$31
        move    a,x:<$32
        move    a,x:<$33
        move    a,x:<$38
        move    a,x:<$39
        move    a,x:<$3a
        move    a,x:<$3b
        move    a,x:<$3c
        move    a,x:<$3d
; the rings and the tables cleared
        move    #>$8000,r0
        do      #$340,zerng
        move    a,x:(r0)+
zerng:
        nop
        move    #>$200,r0
        do      #$100,zetab
        move    a,x:(r0)+
zetab:
        nop
; the sample stream: a ring at X:$4000-$7fff (stereo words L R), write pointer $2f0, read pointer $2f1
        move    #>$4000,x0
        move    x0,x:>$2f0
        move    x0,x:>$2f1
; to the ColdFire: two 4096-word buffers X:$2000 / X:$3000 (15 frames each: the ColdFire may be away a while), filled a frame at a time (write $2f5, this buffer's start
; $2f6) and sent by DMA4 into HTX while the core works on; sources $2f4 (bit 0 A B, 1 C D, 2 out, 3 USB's tap)
        move    #>$2000,x0
        move    x0,x:>$2f5
        move    x0,x:>$2f6
; the matrix counts in halves (the sum is doubled, so a gain reaches 2.0).
; default routing: A -> L and R at 0.5 x 2 = 1.0 only (B, C, D are metered, not mixed: on the bring-up rig they are the
; outputs looped back, and mixing them would feed back). The ColdFire sets the real routing. Master 1.0.
        move    #>$400000,x0
        move    x0,x:>$2e0
        move    x0,x:>$2e1
        move    #>$7fffff,x0
        move    x0,x:>$2e8
; the ESAI, in stock's order
        movep   #>$aa0000,x:<<$ffff98
        movep   #>$40,x:<<$ffffb4
        movep   a,x:<<$ffffa0
        movep   #>$f40f00,x:<<$ffffb6
        movep   #>$37d01,x:<<$ffffb5
        movep   #>$f40f00,x:<<$ffffb8
        movep   #>$17d01,x:<<$ffffb7
        movep   #>$f,x:<<$ffffbb
        movep   #>$0,x:<<$ffffbc
        movep   #>$ff,x:<<$ffffb9
        movep   #>$0,x:<<$ffffba
        movep   #>$ffffff,x:<<$ffffbf
        movep   #>$ffffff,x:<<$ffffbe
        clr     a
crb04:
        jclr    #1,x:<<$ffffc3,crb04
        movep   #>$ae0002,x:<<$ffffc7
waitfs:
        jclr    #13,x:<<$ffffb3,waitfs
        move    #>$7,r6
crb05:
        jclr    #1,x:<<$ffffc3,crb05
        movep   #>$ae0003,x:<<$ffffc7
primeone:
        jclr    #15,x:<<$ffffb3,primeone
        movep   a,x:<<$ffffa0
        move    (r6)-
        move    r6,b
        tst     b
        beq     primeend
        jclr    #0,x:<$3f,primeone
crb06:
        jclr    #1,x:<<$ffffc3,crb06
        movep   #>$ae0004,x:<<$ffffc7
primeend:
        jclr    #15,x:<<$ffffb3,primeend
; DMA2: the TX ring, stock's values. On the chip DMA2 stops at the end of each 256-word pass and stock restarts
; it from the DMA2 interrupt (vector P:$1c) -- the emulator never stops it, so only the unit showed it (the engine
; froze after two frames, 10 Nov 2026). Here the vector is a FAST interrupt, its two words the re-arm itself:
; `movep #>$cc6220,x:<<$ffffe4`. Host interrupts off (HCR 0), DMA2 at level 0 (IPRC as stock), SR unmasked.
        movep   #>$0,x:<<$ffffc2
        move    #>$08f4a4,x0
        move    x0,x:>$1c        ; PATCHED by pmove.py to `movem x0,p:$1c` (077084): dsp_asm has no movem
        move    #>$cc6220,x0
        move    x0,x:>$1d        ; PATCHED likewise
        bset    #$10,x:<<$ffffff
        bclr    #$11,x:<<$ffffff
        movep   #>$ffff01,x:<<$fffff1
        movep   #>$8000,x:<<$ffffe7
        movep   #>$ffffa0,x:<<$ffffe6
        movep   #>$ff,x:<<$ffffe5
        movep   #>$cc6220,x:<<$ffffe4
        andi    #$fc,mr
; DMA3: the RX ring, from a receive frame sync (stock's values)
        movep   #>$fffdc1,x:<<$fffff0
crb07:
        jclr    #1,x:<<$ffffc3,crb07
        movep   #>$ae0005,x:<<$ffffc7
rxfs:
        jclr    #6,x:<<$ffffb3,rxfs
        movep   #>$ffffa8,x:<<$ffffe3
        movep   #>$8100,x:<<$ffffe2
        movep   #>$23f,x:<<$ffffe1
        movep   #>$ac59c0,x:<<$ffffe0

; ---- the frame loop ----
crb08:
        jclr    #1,x:<<$ffffc3,crb08
        movep   #>$ae0006,x:<<$ffffc7
mainloop:
        jset    #3,x:<<$ffffc3,handback
        bsr     >hostin
        movep   x:<<$ffffe7,a
        move    #>$80f0,x0
        cmp     x0,a
        beq     bankA
        move    #>$8070,x0
        cmp     x0,a
        beq     bankB
        jclr    #0,x:<$3f,mainloop
bankA:
        move    #>$8000,r5
        bsr     >frame
        jclr    #0,x:<$3f,waitoff
bankB:
        move    #>$8080,r5
        bsr     >frame
; wait until DSR2 has moved on, so one position is one frame
waitoff:
        movep   x:<<$ffffe7,a
        move    #>$80f0,x0
        cmp     x0,a
        beq     waitmore
        move    #>$8070,x0
        cmp     x0,a
        beq     waitmore
        jclr    #0,x:<$3f,mainloop
waitmore:
        bsr     >hostin
        jclr    #0,x:<$3f,waitoff

; ---- one frame: r5 = the TX half to fill ----
frame:
        move    r5,x:>$2c0
        movep   #>$cc6220,x:<<$ffffe4
; the newest complete input block: DDR3 - $40, aligned to $40, wrapped into $8100..$833f
        movep   x:<<$ffffe2,a
        move    #>$40,x0
        sub     x0,a
        move    #>$ffffc0,x0
        and     x0,a
        move    #>$8100,x0
        cmp     x0,a
        bge     inok
        move    #>$240,x0
        add     x0,a
inok:
        move    a1,r1
        move    #>$900,r0
; per sample: C D A B in, L R out (the matrix rows: A $2e0/$2e1, B $2e2/$2e3, C $2e4/$2e5, D $2e6/$2e7)
        do      #16,mixend
        move    x:(r1)+,x0
        move    x:(r1)+,x1
        move    x:(r1)+,y0
        move    x:(r1)+,y1
        move    x0,x:>$2c1
        move    x1,x:>$2c2
        move    y0,x:>$2c3
        move    y1,x:>$2c4
        bsr     >peaks
        move    x:>$2c3,x0
        move    x:>$2e0,y0
        mpy     y0,x0,a
        move    x:>$2c4,x0
        move    x:>$2e2,y0
        mac     y0,x0,a
        move    x:>$2c1,x0
        move    x:>$2e4,y0
        mac     y0,x0,a
        move    x:>$2c2,x0
        move    x:>$2e6,y0
        mac     y0,x0,a
        asl     a
        move    a,x:(r0)+
        move    x:>$2c3,x0
        move    x:>$2e1,y0
        mpy     y0,x0,a
        move    x:>$2c4,x0
        move    x:>$2e3,y0
        mac     y0,x0,a
        move    x:>$2c1,x0
        move    x:>$2e5,y0
        mac     y0,x0,a
        move    x:>$2c2,x0
        move    x:>$2e7,y0
        mac     y0,x0,a
        asl     a
        move    a,x:(r0)+
mixend:
        nop
        move    r1,x:>$2c5
        bsr     >stream
; main feedback (HEAD's), then the chain; the chain's output is kept for the next frame's feedback
        bsr     >fback
        bsr     >chain
        bsr     >record
        move    #>$900,r4
        move    #>$940,r5
        do      #32,fbcopy
        move    x:(r4)+,x0
        move    x0,x:(r5)+
fbcopy:
        nop
; out: every pair, through the master gain; slots 6/7 silent
        move    x:>$2c0,r5
        move    #>$900,r0
        do      #16,outend
        move    x:(r0)+,x0
        move    x:>$2e8,y0
        mpy     y0,x0,a
        move    x:(r0)+,x0
        mpy     y0,x0,b
        move    a,x:(r5)+
        move    b,x:(r5)+
        move    a,x:(r5)+
        move    b,x:(r5)+
        move    a,x:(r5)+
        move    b,x:(r5)+
        move    a,x:>$2c6
        move    b,x:>$2c7
        clr     a
        move    a,x:(r5)+
        move    a,x:(r5)+
        move    x:>$2c6,a
        abs     a
        move    x:<$3c,x1
        cmp     x1,a
        ble     opkl
        move    a1,x:<$3c
opkl:
        move    x:>$2c7,a
        abs     a
        move    x:<$3d,x1
        cmp     x1,a
        ble     opkr
        move    a1,x:<$3d
opkr:
        nop
outend:
        nop
        bsr     >meters
        bsr     >rflush
        rts

; ---- the recorder (bit 3 of $2f4: USB's tap -- A B C D as they come in, then main L R after the master gain)
; ---- each frame, if $2f4 names sources and the ring has room, a header (7 << 20 | sources) and then
; per sample the chosen pairs (A B, C D, out L R), each word (sample >> 1) with bit 23 set; rflush passes it to
; the ColdFire a word at a time whenever the host port is free
record:
        move    x:>$2f4,a
        tst     a
        beq     recdone
        move    a1,x:<$35
        move    x:>$2f5,a
        move    x:>$2f6,x0
        sub     x0,a
        move    #>$f00,x0
        cmp     x0,a
        bge     recdone
        move    x:>$2c5,a
        move    #>$40,x0
        sub     x0,a
        move    a1,r1
        move    #>$900,r4
        move    x:>$2f5,r3
        move    x:<$35,a
        move    #>$700000,x0
        or      x0,a
        move    a1,x:(r3)+
        do      #16,recloop
        move    x:(r1)+,x0
        move    x0,x:>$2c1
        move    x:(r1)+,x0
        move    x0,x:>$2c2
        move    x:(r1)+,x0
        move    x0,x:>$2c3
        move    x:(r1)+,x0
        move    x0,x:>$2c4
        jclr    #0,x:<$35,recn1
        move    x:>$2c3,a
        bsr     >rput
        move    x:>$2c4,a
        bsr     >rput
recn1:
        jclr    #1,x:<$35,recn2
        move    x:>$2c1,a
        bsr     >rput
        move    x:>$2c2,a
        bsr     >rput
recn2:
        jclr    #2,x:<$35,recskip
        move    x:(r4)+,a
        bsr     >rput
        move    x:(r4)+,a
        bsr     >rput
        jclr    #0,x:<$3f,recn3
recskip:
        move    (r4)+
        move    (r4)+
recn3:
        jclr    #3,x:<$35,recn4
        move    x:>$2c3,a
        bsr     >rput
        move    x:>$2c4,a
        bsr     >rput
        move    x:>$2c1,a
        bsr     >rput
        move    x:>$2c2,a
        bsr     >rput
        move    (r4)-
        move    (r4)-
        move    x:(r4)+,x0
        move    x:>$2e8,y0
        mpy     y0,x0,a
        bsr     >rput
        move    x:(r4)+,x0
        move    x:>$2e8,y0
        mpy     y0,x0,a
        bsr     >rput
recn4:
        nop
recloop:
        nop
        move    r3,x:>$2f5
recdone:
        rts
rput:
        asr     a
        move    #>$7fffff,x1
        and     x1,a
        move    #>$800000,x1
        or      x1,a
        move    a1,x:(r3)+
        rts
; ---- the frame's words to the ColdFire: when DMA4 is idle, it takes this buffer (a word each time HTX empties:
; DRS 17, word mode, DE cleared at the end; source X post-increment, destination fixed) and the other buffer fills
rflush:
        jset    #23,x:<<$ffffdc,rfdone
        move    x:>$2f5,a
        move    x:>$2f6,x0
        sub     x0,a
        tst     a
        beq     rfdone
        move    #>$1,x1
        sub     x1,a
        move    a1,x1
        movep   x0,x:<<$ffffdf
        movep   #>$ffffc7,x:<<$ffffde
        movep   x1,x:<<$ffffdd
        movep   #>$888a50,x:<<$ffffdc
        move    x0,a
        move    #>$1000,x1
        eor     x1,a
        move    a1,x:>$2f6
        move    a1,x:>$2f5
rfdone:
        rts

; ---- the sample stream: 16 stereo frames from the ring into the work buffer at gain $2f2 (x2), when 32 words are
; there; $2f3 counts the frames taken (the ColdFire paces its writing by it: meter index 6)
stream:
        move    x:>$2f0,a
        move    x:>$2f1,x0
        sub     x0,a
        move    #>$3fff,x0
        and     x0,a
        move    a1,b
        move    #>$20,x0
        cmp     x0,b
        blt     stmdone
        move    x:>$2f1,r3
        move    #>$3fff,m3
        move    #>$900,r4
        move    x:>$2f2,y0
        do      #32,stmloop
        move    x:(r3)+,x0
        mpy     y0,x0,a
        asl     a
        move    x:(r4),x1
        add     x1,a
        move    a,x:(r4)+
stmloop:
        nop
        move    #>$ffffff,m3
        move    r3,x:>$2f1
        move    x:>$2f3,a
        move    #>$10,x0
        add     x0,a
        move    #>$ffff,x0
        and     x0,a
        move    a1,x:>$2f3
stmdone:
        move    x:>$2f3,a
        lsl     #4,a
        move    a1,x:<$3e
        rts

; ---- main feedback (AERIA HEAD's, after Cloudfin's global loop): the previous frame's chain output ($940, L R)
; high-passed at 70 Hz, low-passed at the tone, ridden by g = min(1, 0.3/envelope), soft-clipped with WARM's drive
; (clip(d u)/d, u - u^3/3), times FDBK -- added into the work buffer. FDBK 0 adds nothing and clears the state.
; ColdFire sets: $2d8 k = FDBK*0.95/d, $2d9 the tone coefficient, $2da d/4. State: $2d0-$2d5 (xp, yp, lp per
; channel, L at even, R at odd), $2d6 envelope, $2d7 g, $2dc peak scratch.
fback:
        move    x:>$2d8,a
        tst     a
        beq     fbclear
        clr     a
        move    a,x:>$2dc
        move    #>$940,r4
        do      #32,fbpkend
        move    x:(r4)+,a
        abs     a
        move    x:>$2dc,x1
        cmp     x1,a
        ble     fbpkn
        move    a1,x:>$2dc
fbpkn:
        nop
fbpkend:
        nop
        move    x:>$2d6,x0
        move    #>$7fdde0,y0
        mpy     y0,x0,a
        move    x:>$2dc,x1
        cmp     x1,a
        bge     fbenv
        move    x1,a
fbenv:
        move    a,x:>$2d6
        move    #>$7fffff,b
        move    #>$266666,x1
        cmp     x1,a
        ble     fbgok
        move    a,x0
        move    #>$266666,a
        andi    #$fe,ccr
        do      #24,fbdiv
        div     x0,a
fbdiv:
        nop
        move    a0,x0
        move    x0,b
fbgok:
        move    b,x:>$2d7
        move    #>$940,r4
        move    #>$900,r5
        do      #16,fbloop
        move    x:(r4)+,x0
        move    #>$2d0,r6
        bsr     >fbch
        move    x:(r4)+,x0
        move    #>$2d1,r6
        bsr     >fbch
        nop
fbloop:
        nop
        rts
fbclear:
        clr     a
        move    #>$2d0,r4
        do      #8,fbclr
        move    a,x:(r4)+
fbclr:
        nop
        rts
; one sample of one channel: x0 in, r6 the channel's state (xp +0, yp +2, lp +4), adds into x:(r5)+
fbch:
        move    x:(r6),x1
        move    x0,x:(r6)
        move    x0,a
        sub     x1,a
        move    x:(r6+2),x1
        add     x1,a
        move    a,x0
        move    #>$7eb852,y0
        mpy     y0,x0,a
        move    a,x:(r6+2)
        move    x:(r6+4),x1
        sub     x1,a
        move    a,x0
        move    x:>$2d9,y0
        mpy     y0,x0,a
        add     x1,a
        move    a,x:(r6+4)
        move    a,x0
        move    x:>$2d7,y0
        mpy     y0,x0,a
        move    a,x0
        move    x:>$2da,y0
        mpy     y0,x0,a
        asl     #2,a,a
        move    a,x1
        move    x1,x0
        move    x1,y0
        mpy     y0,x0,a
        move    a,y0
        move    x1,x0
        mpy     y0,x0,a
        move    a,x0
        move    #>$2aaaab,y0
        mpy     y0,x0,a
        move    a,y1
        move    x1,b
        sub     y1,b
        move    b,x0
        move    x:>$2d8,y0
        mpy     y0,x0,a
        move    x:(r5),b
        add     a,b
        move    b,x:(r5)+
        rts

; ---- the chain: 8 slots at X:$280 ----
chain:
        move    #>$280,r4
        move    r4,x:>$2c8
        do      #8,chend
        move    x:>$2c8,r4
        nop
        move    x:(r4),a
        tst     a
        beq     chnext
        move    x:(r4+$6),a
        tst     a
        beq     chproc
        clr     a
        move    a,x:(r4+$6)
        move    x:(r4+$5),x0
        move    x0,x:>$213
        move    x:(r4+$3),r6
        move    x:(r4+$4),r7
        move    x:(r4+$2),r2
        bsr     >callr2
        move    x:>$2c8,r4
        nop
chproc:
        move    x:(r4+$5),x0
        move    x0,x:>$213
        move    x:(r4+$3),r6
        move    x:(r4+$4),r7
        move    x:(r4+$1),r2
        move    #>$900,r0
        move    #>16,n7
        move    #>$1,a
        bsr     >callr2
chnext:
        move    x:>$2c8,a
        move    #>$8,x0
        add     x0,a
        move    a1,x:>$2c8
        nop
chend:
        nop
        rts
; the effect is called with the registers it expects; it may use any of them, and its own do loops
callr2:
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    #>$ffffff,m5
        move    #>$ffffff,m6
        move    #>$ffffff,m7
        jsr     (r2)
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    #>$ffffff,m5
        move    #>$ffffff,m6
        move    #>$ffffff,m7
        rts

; ---- input peaks: x:$221.. C D A B -> $3a $3b $38 $39 ----
peaks:
        move    x:>$2c1,a
        abs     a
        move    x:<$3a,x1
        cmp     x1,a
        ble     pkc
        move    a1,x:<$3a
pkc:
        move    x:>$2c2,a
        abs     a
        move    x:<$3b,x1
        cmp     x1,a
        ble     pkd
        move    a1,x:<$3b
pkd:
        move    x:>$2c3,a
        abs     a
        move    x:<$38,x1
        cmp     x1,a
        ble     pka
        move    a1,x:<$38
pka:
        move    x:>$2c4,a
        abs     a
        move    x:<$39,x1
        cmp     x1,a
        ble     pkb
        move    a1,x:<$39
pkb:
        rts

; ---- meters: one peak to the host per frame, while the frame counter's low 5 bits are 0..5 ----
meters:
        move    x:<$32,a
        move    #>$1,x1
        add     x1,a
        move    a1,x:<$32
        move    #>$1f,x1
        and     x1,a
        move    #>$7,x1
        cmp     x1,a
        bge     mtdone
        move    a1,r2
        move    #>$38,n2
        nop
        move    x:(r2+n2),b
        lsr     #4,b
        move    a1,x0
        move    b1,x1
        move    x0,a
        lsl     #20,a
        move    a1,b
        move    x1,x0
        or      x0,b
        move    x:>$2f5,a
        move    x:>$2f6,x0
        sub     x0,a
        move    #>$ff0,x0
        cmp     x0,a
        bge     mtfull
        move    x:>$2f5,r3
        nop
        move    b1,x:(r3)+
        move    r3,x:>$2f5
mtfull:
        clr     a
        move    a,x:(r2+n2)
mtdone:
        rts

; ---- host words: an address word (bit 23 set; bit 22 = Y space; bits 21..0 the address), then the value ----
hostin:
        jclr    #0,x:<<$ffffc3,hidone
        movep   x:<<$ffffc6,a
        move    x:<$30,b
        tst     b
        bne     hival
        tst     a
        bge     histr
        move    a1,x:<$34
        move    #>$3fffff,x0
        and     x0,a
        move    a1,x:<$31
        move    #>$1,b
        move    b1,x:<$30
        jclr    #0,x:<$3f,hidone
hival:
        move    x:<$31,r3
        clr     b
        move    b1,x:<$30
        jset    #22,x:<$34,hiy
        move    a1,x:(r3)
        jclr    #0,x:<$3f,hidone
hiy:
        move    a1,y:(r3)
        jclr    #0,x:<$3f,hidone
histr:
        asl     a
        move    x:>$2f0,r3
        move    #>$3fff,m3
        nop
        move    a1,x:(r3)+
        move    #>$ffffff,m3
        move    r3,x:>$2f0
hidone:
        rts

; ---- hand back: DMA and the ESAIs off, the host port as the boot ROM leaves it, then be a boot ROM ----
handback:
        ori     #$03,mr
        movep   #>$0,x:<<$ffffe4
        movep   #>$0,x:<<$ffffe0
        movep   #>$0,x:<<$ffffb5
        movep   #>$0,x:<<$ffffb7
        movep   #>$0,y:<<$ffff95
        movep   #>$0,y:<<$ffff97
        bclr    #6,x:<<$ffffc4
        bclr    #7,x:<<$ffffc4
        bset    #6,x:<<$ffffc4
        bclr    #$e,y:<<$fffffd
        bclr    #$e,y:<<$fffffe
        move    #>$1,x1
romcount:
        jclr    #0,x:<<$ffffc3,romcount
        movep   x:<<$ffffc6,x0
        move    x0,a
romaddr:
        jclr    #0,x:<<$ffffc3,romaddr
        movep   x:<<$ffffc6,r0
        move    r0,r1
romword:
        jclr    #0,x:<<$ffffc3,romword
        movep   x:<<$ffffc6,x:(r0)+
        sub     x1,a
        beq     romdone
        jclr    #0,x:<$3f,romword
romdone:
        jmp     (r1)
