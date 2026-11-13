; ---------------------------------------------------------------------------
; AERIA DUMP -- a diagnostic insert: it plays this core's peripheral registers out as audio, so a USB
; capture of the track reads the live hardware state of a DSP that is making sound (AERIA OS bring-up:
; what stock leaves set that its code never visibly writes).
;
; Output (both channels the same, the input is discarded), one cycle of 256 + 153*128 samples, forever:
;   sync: 128 samples of 0, then 128 of REF ($100000)
;   per register k (0..152), 8 symbols of 16 samples each:
;     REF, then the value's six nibbles high to low as (nibble+1) << 16, then 0
;   decode a nibble as round(level / REF * 16) - 1: any gain on the track cancels.
; Registers (no reads with side effects: no RX/TX data, no host data, not the cores' mailbox y:$ffffd0-df):
;   k   0-15  x:$ffff90-$ffff9f      k  89-104  y:$ffff90-$ffff9f
;   k  16-28  x:$ffffb3-$ffffbf      k 105-120  y:$ffffc0-$ffffcf
;   k  29-32  x:$ffffc2-$ffffc5      k 121-152  y:$ffffe0-$ffffff
;   k  33-88  x:$ffffc8-$ffffff
; ABI: r0 = audio block (interleaved stereo, in place), n7 = frames, r7 = this instance's state:
;   x:(r7+0) = the position in the cycle. init leaves r1 alone.
; dsp_asm rules: no rep, no mpy, no backward bra; every label four characters, so none is another's prefix.
; ---------------------------------------------------------------------------
init:
        clr     a
        move    a,x:(r7+0)
        rts

proc:
        move    #>$ffffff,m0
        move    #>$ffffff,m1
        do      n7,>dmen
        move    x:(r7+0),a
        move    #>$100,x1
        cmp     x1,a
        bge     dmrg
        clr     b
        move    #>$80,x1
        cmp     x1,a
        blt     dmgo
        move    #>$100000,b
        bra     dmgo
dmrg:
        sub     x1,a
        move    a1,x0
        lsr     #7,a
        move    a1,y0
        move    #>$99,x1
        cmp     x1,a
        blt     dmok
        clr     a
        move    a,x:(r7+0)
        clr     b
        bra     dmwr
dmok:
        move    x0,a
        lsr     #4,a
        move    #>$7,x1
        and     x1,a
        move    a1,y1
        move    y0,a
        move    #>$10,x1
        cmp     x1,a
        bge     dmxb
        move    #>$ffff90,x1
        add     x1,a
        move    a1,r1
        bra     dmrx
dmxb:
        move    #>$1d,x1
        cmp     x1,a
        bge     dmxc
        move    #>$ffffa3,x1
        add     x1,a
        move    a1,r1
        bra     dmrx
dmxc:
        move    #>$21,x1
        cmp     x1,a
        bge     dmxd
        move    #>$ffffa5,x1
        add     x1,a
        move    a1,r1
        bra     dmrx
dmxd:
        move    #>$59,x1
        cmp     x1,a
        bge     dmya
        move    #>$ffffa7,x1
        add     x1,a
        move    a1,r1
        bra     dmrx
dmya:
        move    #>$69,x1
        cmp     x1,a
        bge     dmyb
        move    #>$ffff37,x1
        add     x1,a
        move    a1,r1
        bra     dmry
dmyb:
        move    #>$79,x1
        cmp     x1,a
        bge     dmyc
        move    #>$ffff57,x1
        add     x1,a
        move    a1,r1
        bra     dmry
dmyc:
        move    #>$ffff67,x1
        add     x1,a
        move    a1,r1
        bra     dmry
dmrx:
        nop
        move    x:(r1),b
        bra     dmrv
dmry:
        nop
        move    y:(r1),b
dmrv:
        move    y1,a
        tst     a
        bne     dmsa
        move    #>$100000,b
        bra     dmgo
dmsa:
        move    #>$7,x1
        cmp     x1,a
        bne     dmsb
        clr     b
        bra     dmgo
dmsb:
        move    #>$6,a
        move    y1,x1
        sub     x1,a
        tst     a
        beq     dmsz
        move    a1,x0
        do      x0,>dmsc
        lsr     #4,b
dmsc:
        nop
dmsz:
        move    b1,a
        move    #>$f,x1
        and     x1,a
        move    #>$1,x1
        add     x1,a
        lsl     #16,a
        move    a1,b
dmgo:
        move    x:(r7+0),a
        move    #>$1,x1
        add     x1,a
        move    a1,x:(r7+0)
dmwr:
        move    b1,x:(r0)+
        move    b1,x:(r0)+
dmen:
        rts
