; AERIA OS DSP: core 1's effect engine (our own code), loaded at P:$33800 in the shared window.
; Core 1 has no audio port: core 0's chain hands it blocks through the shared window (core 0's proxy, engine.asm
; pproc). Up to eight slots, entries at X:$33A10 + 16k (written by the ColdFire through core 0):
;   +0 active  +1 proc  +2 init  +3 r6 (knob block, shared)  +4 r7 (state, core 1's own X)  +5 Y buffer base
;   +6 init pending  +7 requests (core 0 counts)  +8 done (core 1 copies +7 when the block is made)
;   +9 the input buffer  +10 the output buffer (shared, 32 words each)  +11 core 1's own word that holds +5
; Each new request: the input to X:$900, the effect run with stock's FX ABI (r0 the block, n7 16 frames, a 1,
; r6, r7, X:$213 -> the Y base word), X:$900 to the output. X:$33A00 counts core 1's passes (alive).
; Start-up: stock's per-core writes, hello ($c0ffee) to the ColdFire; HF0 (host flag) hands back as a boot ROM stand-in.
start:
        move    #>$300,sr
        movep   #>$3fff,y:<<$ffffe4
        bset    #$e,y:<<$fffffd
        bset    #$e,y:<<$fffffe
        clr     a
        move    a,x:<$3f
; the shared entries start as whatever the RAM held: a cold boot ran garbage "slots" (10 Nov 2026). All cleared.
        move    #>$33a00,r4
        do      #$90,c1zr
        move    a,x:(r4)+
c1zr:
        nop
        move    a,x:>$2ca
c1hel:
        jclr    #1,x:<<$ffffc3,c1hel
        movep   #>$c0ffee,x:<<$ffffc7
c1loop:
        jset    #3,x:<<$ffffc3,handback
; when the host port is free, a report word: (n << 20) | the low 20 bits of X:$33A00 + n, n cycling 0..7
; ($33A00 the alive count; $33A01..7 debug words any core-1 effect may write)
        jclr    #1,x:<<$ffffc3,c1nrp
        move    x:>$2ca,a
        move    #>$1,x0
        add     x0,a
        move    #>$7,x0
        and     x0,a
        move    a1,x:>$2ca
        move    a1,r3
        move    #>$33a00,r1
        move    a1,n1
        nop
        move    x:(r1+n1),b
        move    #>$fffff,x0
        and     x0,b
        move    r3,a
        lsl     #20,a
        move    a1,x0
        or      x0,b
        movep   b1,x:<<$ffffc7
c1nrp:
        move    x:>$33a00,a
        move    #>$1,x0
        add     x0,a
        move    a1,x:>$33a00
        move    #>$33a10,r4
        move    r4,x:>$2c8
        do      #8,c1sl
        move    x:>$2c8,r4
        nop
        move    x:(r4),a
        tst     a
        beq     c1nx
        move    x:(r4+$7),a
        move    x:(r4+$8),x0
        cmp     x0,a
        beq     c1nx
        move    a1,x:>$2c9
; late? core 0 asks once a frame; more than one request waiting means core 1 missed a frame's block (its effect's
; sound repeats a block: a click). X:$33A05 counts them (debug word 5): the measure of core 1's real limit.
        sub     x0,a
        move    #>$ffff,x0
        and     x0,a
        move    #>$1,x0
        cmp     x0,a
        ble     c1ont
        move    x:>$33a05,a
        add     x0,a
        move    a1,x:>$33a05
c1ont:
        move    x:(r4+$b),r5
        move    x:(r4+$5),x0
        move    x0,x:(r5)
        move    r5,x:>$213
        move    x:(r4+$6),a
        tst     a
        beq     c1run
        clr     a
        move    a,x:(r4+$6)
        move    x:(r4+$3),r6
        move    x:(r4+$4),r7
        move    x:(r4+$2),r2
        bsr     >c1call
        move    x:>$2c8,r4
        nop
c1run:
        move    x:(r4+$9),r1
        move    #>$900,r0
        do      #32,c1ci
        move    x:(r1)+,x0
        move    x0,x:(r0)+
c1ci:
        nop
        move    x:>$2c8,r4
        nop
        move    x:(r4+$3),r6
        move    x:(r4+$4),r7
        move    x:(r4+$1),r2
        move    #>$900,r0
        move    #>16,n7
        move    #>$1,a
        bsr     >c1call
        move    x:>$2c8,r4
        nop
        move    x:(r4+$a),r1
        move    #>$900,r0
        do      #32,c1co
        move    x:(r0)+,x0
        move    x0,x:(r1)+
c1co:
        nop
        move    x:>$2c9,x0
        move    x0,x:(r4+$8)
c1nx:
        move    x:>$2c8,a
        move    #>$10,x0
        add     x0,a
        move    a1,x:>$2c8
        nop
c1sl:
        nop
        jclr    #0,x:<$3f,c1loop
c1call:
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
handback:
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
