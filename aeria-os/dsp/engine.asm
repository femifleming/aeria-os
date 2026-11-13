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
;   +6 init pending (the engine calls init once, then clears it)  +7 the slot's routing block (0: in the chain)
; A routing block (9 words, the ColdFire's): +0 routed out of the chain (to the buses) or not, +1..+4 target gains
;   L->main R->main L->cue R->cue, +5..+8 the gains in use, gliding 1/32 of the way each frame (no zipper).
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

; P:$33000 a branch over the proxy; P:$33002 its init, P:$33003 its proc (fixed: the ColdFire names them)
        bra     >start
pinit:
        rts
pproc:
        move    x:(r6+$11),r1
        move    r0,r2
        do      #32,pcin
        move    x:(r2)+,x0
        move    x0,x:(r1)+
pcin:
        nop
        move    x:(r6+$12),r1
        move    r0,r2
        do      #32,pcout
        move    x:(r1)+,x0
        move    x0,x:(r2)+
pcout:
        nop
        move    x:(r6+$10),r1
        nop
        move    x:(r1+$7),a
        move    #>$1,x0
        add     x0,a
        move    a1,x:(r1+$7)
        rts
; the proxy: a slot whose effect runs on core 1. Its knob block is in the shared window: +$10 core 1's slot entry,
; +$11 the input buffer, +$12 the output buffer. It posts this frame's block and takes back the last one core 1 made
; (one frame, 0.36 ms, later), then asks for the next.
start:
        move    #>$300,sr
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
        move    a,x:<$37
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
; the limiter's page X:$300-$3ff cleared (the host interrupt's saves are there too), its ceiling (half of -1 dBFS:
; it works on the main mix at half scale), gain 1, a history of "no reduction needed", its delay ring at $380
        move    #>$300,r0
        do      #$100,zelim
        move    a,x:(r0)+
zelim:
        nop
        move    #>$1c8312,x0
        move    x0,x:>$321
        move    #>$8800,r0
        do      #$40,zeeq
        move    a,x:(r0)+
zeeq:
        nop
        move    #>$7fffff,x0
        move    x0,x:>$322
        move    x0,x:>$325
        move    x0,x:>$326
        move    x0,x:>$327
        move    x0,x:>$328
        move    #>$380,x0
        move    x0,x:>$329
        move    #>$1,x0
        move    x0,x:>$320
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
        move    x0,x:>$2ea
        move    x0,x:>$2eb
        move    x0,x:>$2ec
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
waitfs:
        jclr    #13,x:<<$ffffb3,waitfs
        move    #>$7,r6
primeone:
        jclr    #15,x:<<$ffffb3,primeone
        movep   a,x:<<$ffffa0
        move    (r6)-
        move    r6,b
        tst     b
        beq     primeend
        jclr    #0,x:<$3f,primeone
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
rxfs:
        jclr    #6,x:<<$ffffb3,rxfs
        movep   #>$ffffa8,x:<<$ffffe3
        movep   #>$8100,x:<<$ffffe2
        movep   #>$23f,x:<<$ffffe1
        movep   #>$ac59c0,x:<<$ffffe0

; DMA0: every word the host sends, into the ring X:$8400-$87ff (as DMA3 takes the converter's: word transfers on
; the request, never stopping, the place wrapping by an offset register -- DOR0, -1023 after each 1024). The frame
; sorts them (hostparse). Polled, the port took a word only while the engine idled (4,000 frames a second of the
; samples' 44,100); by interrupt it cost 45 instructions a word; like this the transfer costs the engine nothing,
; which is what eight channels of samples need (12 Nov 2026).
        move    #>$8400,x0
        move    x0,x:>$337
        movep   #>$fffc01,x:<<$fffff3
        movep   #>$ffffc6,x:<<$ffffef
        movep   #>$8400,x:<<$ffffee
        movep   #>$3ff,x:<<$ffffed
        movep   #>$ac8040,x:<<$ffffec

; ---- the frame loop ----
mainloop:
        jset    #3,x:<<$ffffc3,handback
        movep   x:<<$ffffe7,a
        move    #>$80f0,x0
        cmp     x0,a
        beq     bankA
        move    #>$8070,x0
        cmp     x0,a
        beq     bankB
        jclr    #0,x:<$3f,mainloop
; (12 Nov 2026, late) the half filled is the one the converter is LEAVING, not the one it is about to enter: it
; comes round again 18 samples on, and the frame's output is written some way into the frame (the more work, the
; later). Filling the half about to play, every sample written after the converter had passed its place waited a
; whole turn (32 samples) while the rest played at once: at 35 % load three samples in sixteen came out late, a
; rasp on everything (the USB tap, taken before, never showed it). Now all sixteen wait the same turn.
bankA:
        move    #>$8080,r5
        bsr     >frame
        jclr    #0,x:<$3f,waitoff
bankB:
        move    #>$8000,r5
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
        jclr    #0,x:<$3f,waitoff

; ---- one frame: r5 = the TX half to fill ----
frame:
        move    r5,x:>$2c0
        movep   x:<<$ffffe7,a
        move    a1,x:>$33c
; where the input DMA is NOW (X:$33d), before the host's words are sorted: that takes a time that varies, and the
; block of input picked after it was now and then the one before again (sixteen samples heard twice, sixteen lost)
        bsr     >hostparse
; the load meter (12 Nov 2026): where the output DMA is when the frame's work starts; at its end, how far it moved
; -- 8 words a sample, so 128 = the whole frame's time, more = an overrun. X:$33A01 this frame's, $33A02 the most
; since the ColdFire last zeroed it (core 1's reports carry both out: debug words 1 and 2).
        movep   #>$cc6220,x:<<$ffffe4
; the newest complete input block: DDR3 (as the frame began) - $40, aligned to $40, wrapped into $8100..$833f
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
; the chain (main feedback was taken out 13 Nov 2026: the owner removed it from the OS on 12 Nov)
        move    #>$9a0,r0
        clr     a
        do      #64,qclr
        move    a,x:(r0)+
qclr:
        nop
        bsr     >chain
        bsr     >record
        bsr     >wthr
; out: main 2/3 = (chain + front bus) x master ($2e8 left, $2ec right: the balance); cue x $2eb (its level); cue 0/1 = (back bus + chain x $2ea) x master (the chain is copied
; to cue while nothing is routed back); phones 4/5 = main + back; slots 6/7 silent. X:$2f8-$2fd the six results.
; ---- the limiter (after Balloon Shop's PeakLimiter): a look-ahead brickwall at -1 dBFS on the main out. The main
; mix (front bus + chain, x master) is made at HALF scale first, so a sum that would have clipped still has its
; shape; the frame's loudest sample says what gain it needs (ceiling / peak, 1 when under); the gain is the least
; needed over this frame and the four before -- the audio is 64 samples (1.5 ms) late through a ring, so the gain
; is already down, by a ramp across one frame, when the peak comes out -- and it recovers in 150 ms.
; X:$320 on, $321 ceiling/2, $322 gain, $323 its step a sample, $324 the reduction held, $325-$328 what the last four
; frames needed, $329 the ring's place, $32a the frame's peak; $340-$35f this frame at half scale, $380-$3ff the ring.
        move    #>$900,r0
        move    #>$9a0,r1
        move    #>$340,r4
        move    #>$8840,r2
        do      #16,limpre
        move    x:(r0)+,x0
        move    x:(r1)+,a
        add     x0,a
        move    x:(r2)+,x0
        add     x0,a
        asr     #2,a,a
        move    a,x1
        move    x:>$2e8,y0
        mpy     x1,y0,a
        move    a,x:(r4)+
        move    x:(r0)+,x0
        move    x:(r1)+,a
        add     x0,a
        move    x:(r2)+,x0
        add     x0,a
        asr     #2,a,a
        move    a,x1
        move    x:>$2ec,y0
        mpy     x1,y0,a
        move    a,x:(r4)+
limpre:
        nop
; ---- bass mono (12 Nov 2026): what the two sides differ by under 50 Hz is taken out of both, so the low end sits in
; the middle. side = (L - R) / 2 through a one-pole lowpass (X:$8a40 its memory); L loses it, R gains it. Off
; (X:$8a41 not 0) while anything is placed among four outputs: there the pair is a front, not a left and right.
        move    x:>$8a41,a
        tst     a
        bne     bmoff
        move    #>$340,r0
        move    x:>$8a40,b
        move    #>$00e8ae,y0
        do      #16,bmlp
        move    x:(r0)+,a
        move    x:(r0)-,x0
        sub     x0,a
        asr     a
        sub     b,a
        move    a,x0
        mpy     y0,x0,a
        add     a,b
        move    x:(r0),a
        sub     b,a
        move    a,x:(r0)+
        move    x:(r0),a
        add     b,a
        move    a,x:(r0)+
bmlp:
        move    b,x:>$8a40
bmoff:
; ---- the master EQ (12 Nov 2026): four bands on the main mix, after the weather's filters and before the limiter,
; never moved by the weather. A band adds g times a filter of the signal to the signal: the low band a 2-pole
; lowpass (a shelf), the two middle ones a bandpass, the high band a high-pass. X:$8800 + 6 x band: the filter
; (b0 b1 b2 -a1 -a2, halved) and g / 4 (0 = the band is flat and skipped); state X:$8820 + 8 x band (left, right).
        move    #>$8800,r1
        move    #>$8820,r2
        do      #4,eqbands
        move    x:(r1+$5),a
        tst     a
        beq     eqflat
        move    #>$340,r0
        bsr     >eqone
        move    (r2)+
        move    (r2)+
        move    (r2)+
        move    (r2)+
        move    #>$341,r0
        bsr     >eqone
        move    (r2)-
        move    (r2)-
        move    (r2)-
        move    (r2)-
eqflat:
        move    #>$6,n1
        move    #>$8,n2
        move    (r1)+n1
        move    (r2)+n2
        nop
eqbands:
        nop
; the frame's loudest sample
        move    #>$340,r4
        clr     b
        do      #32,limpk
        move    x:(r4)+,a
        abs     a
        cmp     b,a
        ble     limpn
        move    a,b
limpn:
        nop
limpk:
        nop
        move    b1,x:>$32a
; what this frame needs: 1, or ceiling / peak
        move    x:>$32a,a
        move    x:>$321,x0
        move    #>$7fffff,x1
        move    x:>$320,b
        tst     b
        beq     limnd
        cmp     x0,a
        ble     limnd
        move    a1,y0
        move    x0,a
        andi    #$fe,ccr
        do      #24,limdv
        div     y0,a
limdv:
        move    a0,x1
limnd:
; the least over this frame and the last four; then the history moves on
        move    x1,b
        move    x:>$325,x0
        cmp     x0,b
        ble     lmna
        move    x0,b
lmna:
        move    x:>$326,x0
        cmp     x0,b
        ble     lmnb
        move    x0,b
lmnb:
        move    x:>$327,x0
        cmp     x0,b
        ble     lmnc
        move    x0,b
lmnc:
        move    x:>$328,x0
        cmp     x0,b
        ble     lmnd
        move    x0,b
lmnd:
        move    x:>$327,x0
        move    x0,x:>$328
        move    x:>$326,x0
        move    x0,x:>$327
        move    x:>$325,x0
        move    x0,x:>$326
        move    x1,x:>$325
; the reduction: down at once, back up slowly (150 ms: 0.00242 a frame)
        move    #>$7fffff,a
        sub     b,a
        move    x:>$324,x0
        cmp     x0,a
        bge     lmrs
        sub     x0,a
        move    a,x1
        move    #>$004f4c,y0
        mpy     x1,y0,a
        add     x0,a
        move    #>$100,x1
        cmp     x1,a
        bge     lmrs
        clr     a
lmrs:
        move    a,x:>$324
; the gain's step toward 1 - reduction, a sixteenth a sample
        move    #>$7fffff,b
        move    x:>$324,x0
        sub     x0,b
        move    x:>$322,x0
        sub     x0,b
        asr     #4,b,b
        move    b1,x:>$323
        move    x:>$329,r4
        move    #>$7f,m4
        move    #>$340,r3
        move    #>$360,r6
        move    x:>$2c0,r5
        move    #>$900,r0
        move    #>$9a0,r1
        move    #>$9c0,r2
        do      #16,outend
; (13 Nov 2026) each output once: cue = (chain x $2ea + back) x $2eb; main = the limiter's delayed block x its gain;
; phones = back x the balance + the limited main. (The main used to be made twice, the second time only to be taken
; out of the phones again: 107 instructions a sample, now 62.)
        move    x:(r0)+,x0
        move    x:>$2ea,y0
        mpy     y0,x0,b
        move    x:(r2)+,x1
        add     x1,b
        move    b,x0
        move    x:>$2eb,y0
        mpy     y0,x0,b
        move    b,x:>$2fa
        move    x:>$2e8,y0
        mpy     y0,x1,b
        move    b,x:>$2fc
        move    x:(r0)+,x0
        move    x:>$2ea,y0
        mpy     y0,x0,b
        move    x:(r2)+,x1
        add     x1,b
        move    b,x0
        move    x:>$2eb,y0
        mpy     y0,x0,b
        move    b,x:>$2fb
        move    x:>$2ec,y0
        mpy     y0,x1,b
        move    b,x:>$2fd
        move    x:>$322,a
        move    x:>$323,x0
        add     x0,a
        move    a,x:>$322
        move    a,y0
        move    x:(r4),x1
        move    x:(r3)+,x0
        move    x0,x:(r4)+
        mpy     x1,y0,a
        asl     #2,a,a
        move    a,x0
        move    x0,x:>$2f8
        move    x:>$2fc,b
        add     x0,b
        move    b,x:>$2fc
        move    x:(r4),x1
        move    x:(r3)+,x0
        move    x0,x:(r4)+
        mpy     x1,y0,a
        asl     #2,a,a
        move    a,x0
        move    x0,x:>$2f9
        move    x:>$2fd,b
        add     x0,b
        move    b,x:>$2fd
        move    x:>$2fa,x0
        move    x0,x:(r5)+
        move    x:>$2fb,x0
        move    x0,x:(r5)+
        move    x:>$2f8,x0
        move    x0,x:(r5)+
        move    x0,x:(r6)+
        move    x:>$2f9,x0
        move    x0,x:(r5)+
        move    x0,x:(r6)+
; the phones' pair: R first, then L (measured through a splitter, 12 Nov 2026: in the other order the phones' tip
; carried the right side)
        move    x:>$2fd,x0
        move    x0,x:(r5)+
        move    x:>$2fc,x0
        move    x0,x:(r5)+
        clr     a
        move    a,x:(r5)+
        move    a,x:(r5)+
        move    x:>$2f8,a
        abs     a
        move    x:<$3c,x1
        cmp     x1,a
        ble     opkl
        move    a1,x:<$3c
opkl:
        move    x:>$2f9,a
        abs     a
        move    x:<$3d,x1
        cmp     x1,a
        ble     opkr
        move    a1,x:<$3d
opkr:
        nop
outend:
        nop
        move    r4,x:>$329
        move    #>$ffffff,m4
        bsr     >meters
        bsr     >rflush
        movep   x:<<$ffffe7,a
        move    x:>$33c,x0
        sub     x0,a
        move    #>$ff,x0
        and     x0,a
        move    a1,x:>$33a01
        move    x:>$33a02,x0
        cmp     x0,a
        ble     ldmx
        move    a1,x:>$33a02
ldmx:
; and two counts: frames that took more than half their time ($33A03), and overruns ($33A04)
        move    #>$40,x0
        cmp     x0,a
        ble     ldhf
        move    x:>$33a03,b
        move    #>$1,x1
        add     x1,b
        move    b1,x:>$33a03
ldhf:
        move    #>$80,x0
        cmp     x0,a
        ble     ldov
        move    x:>$33a04,b
        move    #>$1,x1
        add     x1,b
        move    b1,x:>$33a04
ldov:
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
        move    #>$360,r6
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
        jset    #4,x:<$37,recmain
        move    x:>$2c3,a
        bsr     >rput
        move    x:>$2c4,a
        bsr     >rput
        move    x:>$2c1,a
        bsr     >rput
        move    x:>$2c2,a
        bsr     >rput
; main L R as they left for the converter (X:$360.., the output stage's own copy: after the weather's filters,
; the balance and the limiter; one frame, 0.36 ms, behind the inputs beside them)
recmain:
        move    x:(r6)+,a
        bsr     >rput
        move    x:(r6)+,a
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
; the master return starts each frame silent (X:$8840, 16 pairs)
        move    #>$8840,r4
        clr     a
        do      #32,stmzr
        move    a,x:(r4)+
stmzr:
        nop
        jset    #3,x:<$37,stmseq
        move    x:>$2f0,a
        move    x:>$2f1,x0
        sub     x0,a
        move    #>$3fff,x0
        and     x0,a
        move    a1,b
        move    #>$20,x0
        jclr    #2,x:<$37,stm2
        move    #>$40,x0
stm2:
        cmp     x0,b
        blt     stmdone
        move    x:>$2f1,r3
        move    #>$3fff,m3
        move    #>$900,r4
        move    #>$8840,r2
        move    x:>$2f2,y0
        do      #16,stmloop
        move    x:(r3)+,x0
        mpy     y0,x0,a
        asl     a
        move    x:(r4),x1
        add     x1,a
        move    a,x:(r4)+
        move    x:(r3)+,x0
        mpy     y0,x0,a
        asl     a
        move    x:(r4),x1
        add     x1,a
        move    a,x:(r4)+
        jclr    #2,x:<$37,stm4
; four words a frame: after the samples' left and right, the master return's (the freeze delay's wet sound, made
; by the ColdFire): added to the main mix after the chain, before the EQ and the limiter
        move    x:(r3)+,a
        asl     a
        move    a,x:(r2)+
        move    x:(r3)+,a
        asl     a
        move    a,x:(r2)+
stm4:
        nop
stmloop:
        nop
stmtail:
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
; the sequencer's stream (X:$37 bit 3; 12 Nov 2026): eight words a sample, one a track (mono, as sent), then the
; master return's two when bit 2 is set. The tracks go to X:$8880 (sample by sample, 8 words each); the chain gives
; each its own filter, slot and place among the outputs (trkload). When the ring runs dry the tracks are silent.
stmseq:
        move    x:>$2f0,a
        move    x:>$2f1,x0
        sub     x0,a
        move    #>$3fff,x0
        and     x0,a
        move    a1,b
        move    #>$80,x0
        jclr    #2,x:<$37,sqfmt
        move    #>$a0,x0
sqfmt:
        cmp     x0,b
        blt     sqdry
        move    x:>$2f1,r3
        move    #>$3fff,m3
        move    #>$8880,r4
        move    #>$8840,r2
        do      #16,sqloop
        do      #8,sqtrk
        move    x:(r3)+,x0
        move    x0,x:(r4)+
sqtrk:
        nop
        jclr    #2,x:<$37,sqnort
        move    x:(r3)+,a
        asl     a
        move    a,x:(r2)+
        move    x:(r3)+,a
        asl     a
        move    a,x:(r2)+
sqnort:
        nop
        nop
sqloop:
        nop
        jclr    #0,x:<$3f,stmtail
sqdry:
        move    x:>$33a06,a
        move    #>$1,x0
        add     x0,a
        move    a1,x:>$33a06
        move    #>$8880,r4
        clr     a
        do      #128,sqzr
        move    a,x:(r4)+
sqzr:
        nop
        jclr    #0,x:<$3f,stmdone

; ---- weather as sound (Balloon Shop's Weather.h): RAIN a 2-pole lowpass, HELIUM a 2-pole high-pass, on the chain's
; output after the feedback tap. The ColdFire eases the coefficients (rain in, thunder's dips). X:$200-$204 the LP's,
; $205-$209 the HP's: b0 b1 b2 -a1 -a2, each halved; $20a / $20b on. State X:$9e0-$9ef (x1 x2 y1 y2 per channel).
wthr:
        move    x:>$20a,a
        tst     a
        beq     wthp
        move    #>$900,r0
        move    #>$200,r1
        move    #>$9e0,r2
        bsr     >bqd
        move    #>$901,r0
        move    #>$9e4,r2
        bsr     >bqd
wthp:
        move    x:>$20b,a
        tst     a
        beq     wtdone
        move    #>$900,r0
        move    #>$205,r1
        move    #>$9e8,r2
        bsr     >bqd
        move    #>$901,r0
        move    #>$9ec,r2
        bsr     >bqd
wtdone:
        rts
; one EQ band over a channel of the limiter's block: r0 its first sample (stride 2), r1 the filter and g / 4, r2 its
; state. The same ten-instruction biquad as bqd; then the band's g times the filter's output is added to the sample.
eqone:
        move    r3,x:>$330
        move    r4,x:>$331
        move    n0,x:>$332
        move    x:(r1),x0
        move    x0,y:>$89e8
        move    x:(r1+$2),x0
        move    x0,y:>$89e9
        move    x:(r1+$1),x0
        move    x0,y:>$89ea
        move    x:(r1+$4),x0
        move    x0,y:>$89eb
        move    x:(r1+$3),x0
        move    x0,y:>$89ec
        move    x:(r1+$5),y1
        move    #>$89e8,r4
        move    #>$4,m4
        move    #>$1,m2
        move    #>$1,m3
        lua     (r2+$2),r3
        move    #>$2,n0
        do      #16,eqfin
        move    x:(r0),x0 y:(r4)+,y0
        mpy     y0,x0,a x:(r2)+,x1 y:(r4)+,y0
        mac     x1,y0,a x:(r2)-,x1 y:(r4)+,y0
        mac     x1,y0,a x0,x:(r2)+ y:(r4)+,y0
        move    x:(r3)+,x1
        mac     x1,y0,a x:(r3)-,x1 y:(r4)+,y0
        mac     x1,y0,a
        asl     a
        move    a,x1
        mpy     y1,x1,b a,x:(r3)+
        asl     #2,b,b
        add     x0,b
        move    b,x:(r0)+n0
eqfin:
        nop
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    x:>$330,r3
        move    x:>$331,r4
        move    x:>$332,n0
        rts

; ---- the chain: 8 slots at X:$280 ----
chain:
        move    #>$280,r4
        move    r4,x:>$2c8
        jclr    #3,x:<$37,chgo
; the sequencer: the inputs' mix is kept aside (it comes back as the chain's output); each slot works on its track
        move    #>$900,r0
        move    #>$8a20,r1
        do      #32,chkeep
        move    x:(r0)+,x0
        move    x0,x:(r1)+
chkeep:
        nop
        move    #>$8900,r0
        move    r0,x:>$8a00
        move    #>$8880,r0
        move    r0,x:>$8a01
; the stems (X:$37 bit 4): every track after its effect, as a stereo pair, to the ColdFire for USB: a header ($780000)
; and then 8 x 32 words, track by track. A frame is left out whole when the ColdFire is behind (X:$8a42 says whether).
        clr     a
        move    a,x:>$8a42
        jclr    #4,x:<$37,chgo
        move    x:>$2f5,a
        move    x:>$2f6,x0
        sub     x0,a
        move    #>$e00,x0
        cmp     x0,a
        bge     chstsk
        move    x:>$2f5,r3
        move    #>$780000,x0
        move    x0,x:(r3)+
        move    r3,x:>$2f5
        move    #>$1,a
        move    a,x:>$8a42
        jclr    #0,x:<$3f,chgo
chstsk:
        move    x:>$33a07,a
        move    #>$1,x0
        add     x0,a
        move    a1,x:>$33a07
chgo:
        do      #8,chend
        jclr    #3,x:<$37,chplain
        bsr     >trkload
        move    x:>$2c8,r4
        nop
        move    x:(r4),a
        tst     a
        beq     chroute
chplain:
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
        jset    #3,x:<$37,chnosv
        move    x:>$2c8,r4
        nop
        move    x:(r4+$7),a
        tst     a
        beq     chnosv
        move    a1,r5
        nop
        move    x:(r5),a
        tst     a
        beq     chnosv
        move    #>$900,r0
        move    #>$980,r1
        do      #32,chsvl
        move    x:(r0)+,x0
        move    x0,x:(r1)+
chsvl:
        nop
chnosv:
        move    x:>$2c8,r4
        nop
        move    x:(r4+$5),x0
        move    x0,x:>$213
        move    x:(r4+$3),r6
        move    x:(r4+$4),r7
        move    x:(r4+$1),r2
        move    #>$900,r0
        move    #>16,n7
        move    #>$1,a
        bsr     >callr2
chroute:
        jclr    #3,x:<$37,chrtn
        move    x:>$8a42,a
        tst     a
        beq     chnost
; a track with nothing in its slot is the same on both sides: its left alone goes (16 words), the ColdFire doubles it
        move    x:>$2f5,r3
        move    #>$900,r0
        move    #>$7fffff,y0
        move    #>$800000,y1
        move    #>$1,n0
        move    #>$20,x0
        move    x:>$2c8,r4
        nop
        move    x:(r4),a
        tst     a
        bne     chstw
        move    #>$2,n0
        move    #>$10,x0
chstw:
        do      x0,chstl
        move    x:(r0)+n0,a
        asr     a
        and     y0,a
        or      y1,a
        move    a1,x:(r3)+
chstl:
        move    r3,x:>$2f5
chnost:
        bsr     >trkmix
        jclr    #0,x:<$3f,chnext
chrtn:
        move    x:>$2c8,r4
        nop
        move    x:(r4+$7),a
        tst     a
        beq     chnext
        move    a1,r5
        nop
        move    x:(r5),a
        tst     a
        beq     chnext
        move    r5,r1
        move    #>$4,n1
        do      #4,chgld
        move    (r1)+
        move    x:(r1+n1),a
        move    x:(r1),b
        sub     a,b
        asr     #5,b,b
        add     b,a
        move    a,x:(r1+n1)
chgld:
        nop
        move    #>$900,r0
        move    #>$9a0,r1
        move    #>$9c0,r2
        do      #16,chqd
        move    x:(r0)+,x0
        move    x:(r5+$5),y0
        move    x:(r1),a
        mac     y0,x0,a
        move    a,x:(r1)+
        move    x:(r5+$7),y0
        move    x:(r2),a
        mac     y0,x0,a
        move    a,x:(r2)+
        move    x:(r0)+,x0
        move    x:(r5+$6),y0
        move    x:(r1),a
        mac     y0,x0,a
        move    a,x:(r1)+
        move    x:(r5+$8),y0
        move    x:(r2),a
        mac     y0,x0,a
        move    a,x:(r2)+
chqd:
        nop
        move    #>$980,r0
        move    #>$900,r1
        do      #32,chrst
        move    x:(r0)+,x0
        move    x0,x:(r1)+
chrst:
        nop
chnext:
        move    x:>$2c8,a
        move    #>$8,x0
        add     x0,a
        move    a1,x:>$2c8
        nop
chend:
        nop
        jclr    #3,x:<$37,chret
        move    #>$8a20,r0
        move    #>$900,r1
        do      #32,chback
        move    x:(r0)+,x0
        move    x0,x:(r1)+
chback:
        nop
chret:
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

; a track to the outputs: its routing block's four gains (glided), the main pair always, the cue pair when it has any
trkmix:
        move    x:>$2c8,r4
        nop
        move    x:(r4+$7),r5
        nop
        move    r5,r1
        move    #>$4,n1
        do      #4,tmgl
        move    (r1)+
        move    x:(r1+n1),a
        move    x:(r1),b
        sub     a,b
        asr     #5,b,b
        add     b,a
        move    a,x:(r1+n1)
tmgl:
        move    x:(r5+$5),y0
        move    x:(r5+$6),y1
        move    #>$900,r0
        move    #>$9a0,r1
        do      #16,tmmn
        move    x:(r0)+,x0
        move    x:(r1),a
        mac     y0,x0,a
        move    a,x:(r1)+
        move    x:(r0)+,x0
        move    x:(r1),a
        mac     x0,y1,a
        move    a,x:(r1)+
tmmn:
        move    x:(r5+$7),a
        move    x:(r5+$8),x0
        add     x0,a
        tst     a
        beq     tmend
        move    x:(r5+$7),y0
        move    x:(r5+$8),y1
        move    #>$900,r0
        move    #>$9c0,r1
        do      #16,tmcu
        move    x:(r0)+,x0
        move    x:(r1),a
        mac     y0,x0,a
        move    a,x:(r1)+
        move    x:(r0)+,x0
        move    x:(r1),a
        mac     x0,y1,a
        move    a,x:(r1)+
tmcu:
        nop
tmend:
        rts
; one track into the work buffer: its 16 samples (X:$8a01 its first word), its high-pass and low-pass (its block at
; X:$8a00: 0-4 the high-pass, 5-9 the low-pass, $a / $b whether each is on, $10 / $14 their memories), then left
; copied to right. The slot's routing block carries it to the outputs.
trkload:
        move    x:>$8a01,r1
        move    #>$8,n1
        move    #>$900,r0
        do      #16,tlcp
        move    x:(r1)+n1,x0
        move    x0,x:(r0)+
        move    (r0)+
tlcp:
        nop
        move    x:>$8a01,a
        move    #>$1,x0
        add     x0,a
        move    a1,x:>$8a01
        move    x:>$8a00,r1
        nop
        move    x:(r1+$a),a
        tst     a
        beq     tlnh
        move    #>$900,r0
        lua     (r1+$10),r2
        bsr     >bqd
tlnh:
        move    x:>$8a00,r1
        nop
        move    x:(r1+$b),a
        tst     a
        beq     tlnl
        lua     (r1+$14),r2
        lua     (r1+$5),r1
        move    #>$900,r0
        bsr     >bqd
tlnl:
        move    #>$900,r0
        do      #16,tldup
        move    x:(r0)+,x0
        move    x0,x:(r0)+
tldup:
        nop
        move    x:>$8a00,a
        move    #>$20,x0
        add     x0,a
        move    a1,x:>$8a00
        rts
; ---- the biquads, with parallel moves (13 Nov 2026): the coefficients copied to Y:$89e0 (b0 b2 b1 -a2 -a1, a ring
; of five), the input's and the output's last two values in X rings of two (state +0/+1 and +2/+3), so a sample is
; ten instructions (it was 29). A frame is sixteen samples, an even number: each ring is back where it began.
bqd:
        move    r3,x:>$330
        move    r4,x:>$331
        move    n0,x:>$332
        move    x:(r1),x0
        move    x0,y:>$89e0
        move    x:(r1+$2),x0
        move    x0,y:>$89e1
        move    x:(r1+$1),x0
        move    x0,y:>$89e2
        move    x:(r1+$4),x0
        move    x0,y:>$89e3
        move    x:(r1+$3),x0
        move    x0,y:>$89e4
        move    #>$89e0,r4
        move    #>$4,m4
        move    #>$1,m2
        move    #>$1,m3
        lua     (r2+$2),r3
        move    #>$2,n0
        do      #16,bqfin
        move    x:(r0),x0 y:(r4)+,y0
        mpy     y0,x0,a x:(r2)+,x1 y:(r4)+,y0
        mac     x1,y0,a x:(r2)-,x1 y:(r4)+,y0
        mac     x1,y0,a x0,x:(r2)+ y:(r4)+,y0
        move    x:(r3)+,x1
        mac     x1,y0,a x:(r3)-,x1 y:(r4)+,y0
        mac     x1,y0,a
        asl     a
        move    a,x:(r3)+
        move    a,x:(r0)+n0
bqfin:
        nop
        move    #>$ffffff,m2
        move    #>$ffffff,m3
        move    #>$ffffff,m4
        move    x:>$330,r3
        move    x:>$331,r4
        move    x:>$332,n0
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
; bring-up peek: X:$2fe non-zero = an X address whose low 20 bits go out as meter 5
        move    #>$5,x1
        cmp     x1,a
        bne     mtnp
        move    x:>$2fe,x1
        move    x1,r3
        move    b1,x0
        move    x1,b
        tst     b
        move    x0,b
        beq     mtnp
        move    x:(r3),b
        move    #>$fffff,x1
        and     x1,b
mtnp:
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

; ---- host words, sorted once a frame from DMA0's ring: an address word (bit 23 set; bit 22 = Y space; bits 21..0
; the address) and then its value; a word with bit 23 clear is a sample of the stream (left, right, ...: into the
; ring at $2f0). X:$337 how far the sorting has come, $33b where DMA0 is; at most 512 words a frame.
hostparse:
        movep   x:<<$ffffee,x0
        move    x0,x:>$33b
        move    #>$200,x0
        move    x0,x:>$338
        move    #>$3ff,m2
        move    x:>$337,r2
hplp:
        move    r2,a
        move    x:>$33b,x0
        cmp     x0,a
        beq     hpend
        move    x:>$338,b
        tst     b
        beq     hpend
        move    #>$1,x0
        sub     x0,b
        move    b1,x:>$338
        move    x:(r2)+,a
hpgot:
        move    a1,x:<$36
        jset    #0,x:<$30,hpval
        jclr    #23,x:<$36,hpstr
        move    a1,x:<$34
        move    #>$3fffff,x0
        and     x0,a
        move    a1,x:<$31
; an address $3fffnn is no address: nn words of the stream follow in one piece (the sequencer's: a sample of all its
; tracks at a time), copied in a loop of three instructions a word. If they have not all come yet, the header waits.
        move    x:<$31,b
        move    #>$3fff00,x0
        and     x0,b
        cmp     x0,b
        beq     hpburst
        bset    #0,x:<$30
        jclr    #0,x:<$3f,hplp
hpburst:
        move    x:<$31,b
        move    #>$ff,x0
        and     x0,b
        move    b1,x1
        move    x:>$33b,a
        move    r2,x0
        sub     x0,a
        move    #>$3ff,x0
        and     x0,a
        move    a1,b
        cmp     x1,b
        blt     hpwait
        move    x:>$2f0,r3
        move    #>$3fff,m3
        do      x1,hpbl
        move    x:(r2)+,a
        asl     a
        move    a1,x:(r3)+
hpbl:
        move    #>$ffffff,m3
        move    r3,x:>$2f0
        jclr    #0,x:<$3f,hplp
hpwait:
        move    (r2)-
        jclr    #0,x:<$3f,hpend
hpval:
        move    x:<$31,r3
        bclr    #0,x:<$30
        nop
        jset    #22,x:<$34,hpy
        move    a1,x:(r3)
        jclr    #0,x:<$3f,hplp
hpy:
        move    a1,y:(r3)
        jclr    #0,x:<$3f,hplp
; a word of the stream, and every one behind it: a tight loop (the sequencer sends 160 a frame, 480 when it catches
; up: one at a time through the general path they took a fifth of the frame -- measured 12 Nov 2026). It ends at
; the ring's end, at the frame's allowance, or at a word with bit 23 set (an address: back to the general path).
hpstr:
        move    x:>$2f0,r3
        move    #>$3fff,m3
        move    x:>$33b,y1
        move    x:>$338,b
        move    #>$1,x1
hpsl:
        asl     a
        move    a1,x:(r3)+
        move    r2,a
        cmp     y1,a
        beq     hpsx
        sub     x1,b
        beq     hpsx
        move    x:(r2)+,a
        jclr    #23,a1,hpsl
        move    #>$ffffff,m3
        move    r3,x:>$2f0
        move    b1,x:>$338
        jclr    #0,x:<$3f,hpgot
hpsx:
        move    #>$ffffff,m3
        move    r3,x:>$2f0
        move    b1,x:>$338
        jclr    #0,x:<$3f,hplp
hpend:
        move    r2,x:>$337
        move    #>$ffffff,m2
        rts

; ---- hand back: DMA and the ESAIs off, the host port as the boot ROM leaves it, then be a boot ROM ----
handback:
        ori     #$03,mr
        movep   #>$0,x:<<$ffffc2
        movep   #>$0,x:<<$ffffec
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
