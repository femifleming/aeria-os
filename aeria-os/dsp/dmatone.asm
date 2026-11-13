; AERIA OS DSP 0.4 -- the output fed the way stock feeds it (core 0). Our own code. Loaded at P:$33000.
; 0.3 wrote TX0 by polling TDE and every output stayed silent while the inputs worked. Stock never writes
; TX0 by hand once running: DMA2 streams a 256-word ring at X:$8000 into TX0 (payload A P:$3004b, values
; read for understanding: DOR2 $ffff01, DSR2 $8000, DDR2 $ffffa0, DCO2 $ff, DCR2 $cc6220). This does the same,
; with DCR2's interrupt enable off (we poll, SR masks everything).
; The ring holds a square tone on every slot: 32 samples x 8 slots, +$180000 for 16 samples, -$180000 for 16
; (1378 Hz, about -14.5 dBFS). The ColdFire's command word (host port) picks which ring words carry it:
;   0-7 = that word of each sample only, 9 = all, 8 = silence (the ring refilled in the main loop).
; Inputs: RX0 polled as in 0.3; a heartbeat 4 times a second (counted in RX words: 4 a sample) carries one
;   RX slot's peak: (slot << 22) | (peak >> 2).
; Start-up: stock's order (P:$30014, P:$30067, P:$3001d): the lines, ESAI_1, the ESAI, TFS, 7 primes, TDE, DMA2.
; Hand-back on HF0 as before, DMA2 off first.
; dsp_asm rules (TRAPS): no rep, no backward bra, no brclr, no long jmp/jcc, no mpy; jclr/jset absolute.
; X: $30 the command, $32 RX slot counter, $38-$3b RX peaks, $3c the beat's slot, $3d last filled command, $3f = 0.

start:
        move    #>$300,sr
        move    #>$ffff90,r0
        move    #>16,x0
        bsr     >dxr
        move    #>$ffffb3,r0
        move    #>13,x0
        bsr     >dxr
        move    #>$ffffc2,r0
        move    #>4,x0
        bsr     >dxr
        move    #>$ffffc8,r0
        move    #>56,x0
        bsr     >dxr
        move    #>$ffff90,r0
        move    #>16,x0
        bsr     >dyr
        move    #>$ffffc0,r0
        move    #>16,x0
        bsr     >dyr
        move    #>$ffffe0,r0
        move    #>32,x0
        bsr     >dyr
        move    #>$ffff80,r0
        move    #>16,x0
        bsr     >dxr
        move    #>$ffffa0,r0
        move    #>19,x0
        bsr     >dxr
        move    #>$ffff80,r0
        move    #>16,x0
        bsr     >dyr
        move    #>$ffffa0,r0
        move    #>32,x0
        bsr     >dyr
        move    #>$ffffd0,r0
        move    #>16,x0
        bsr     >dyr
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
        move    a,x:<$3e
        move    #>$9,x0
        move    x0,x:<$30
        move    #>$63,x0
        move    x0,x:<$3d
        bsr     >frng
        movep   #>$aa0000,x:<<$ffff98
        movep   #>$40,x:<<$ffffb4
        clr     a
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
primefs:
        jclr    #13,x:<<$ffffb3,primefs
        move    #>$7,x0
        move    x0,r6
primetx:
        jclr    #15,x:<<$ffffb3,primetx
        movep   a,x:<<$ffffa0
        move    r6,b
        move    #>$1,x1
        sub     x1,b
        move    b1,r6
        bne     primemore
        bra     primedone
primemore:
        jclr    #0,x:<$3f,primetx
primedone:
        jclr    #15,x:<<$ffffb3,primedone
        movep   #>$ffff01,x:<<$fffff1
        movep   #>$8000,x:<<$ffffe7
        movep   #>$ffffa0,x:<<$ffffe6
        movep   #>$ff,x:<<$ffffe5
        movep   #>$8c6220,x:<<$ffffe4
        clr     a
        move    a,x:<$32
        move    a,x:<$38
        move    a,x:<$39
        move    a,x:<$3a
        move    a,x:<$3b
        move    a,x:<$3c
        move    #>$0,r5
        move    #>$ac44,y1

mainloop:
        jset    #3,x:<<$ffffc3,handback
        jclr    #0,x:<<$ffffc3,nocmd
        movep   x:<<$ffffc6,x0
        move    x0,x:<$30
        bset    #0,x:<$3e
nocmd:
        jclr    #0,x:<$3e,nofl
        bsr     >frng
nofl:
        jclr    #8,x:<<$ffffb3,mainloop
        jclr    #6,x:<<$ffffb3,rxcount
        clr     a
        move    a,x:<$32
rxcount:
        movep   x:<<$ffffa8,a
        abs     a
        move    x:<$32,r2
        move    #>$38,n2
        nop
        move    x:(r2+n2),x1
        cmp     x1,a
        ble     rxnext
        move    a1,x:(r2+n2)
rxnext:
        move    (r2)+
        move    r2,a
        move    #>$3,x1
        and     x1,a
        move    a1,x:<$32
        move    (r5)+
        move    r5,a
        cmp     y1,a
        blt     nobeat
        move    #>$0,r5
        jclr    #1,x:<<$ffffc3,nobeat
        move    x:<$3c,r2
        move    #>$38,n2
        nop
        move    x:(r2+n2),a
        lsr     #2,a
        move    a1,b
        move    r2,a
        lsl     #22,a
        move    a1,x0
        or      x0,b
        movep   b1,x:<<$ffffc7
        clr     a
        move    a,x:(r2+n2)
        move    (r2)+
        move    r2,a
        move    #>$3,x1
        and     x1,a
        move    a1,x:<$3c
nobeat:
        jclr    #0,x:<$3f,mainloop

; frng: the ring X:$8000-$80ff from the command in x:$30 (remembered in x:$3d). r0 walks the ring, r1 counts
; the sample (0-31), r4 counts the word within the sample (0-7). One `do` loop, no backward branches.
frng:
        bclr    #0,x:<$3e
        move    x:<$30,x0
        move    x0,x:<$3d
        move    #>$8000,r0
        move    #>$0,r1
        move    #>$0,r4
        do      #256,fend
        move    r1,a
        move    #>$10,x1
        cmp     x1,a
        move    #>$180000,b
        blt     fpos
        move    #>$e80000,b
fpos:
        move    x:<$3d,a
        move    #>$9,x1
        cmp     x1,a
        beq     fput
        move    r4,x1
        cmp     x1,a
        beq     fput
        clr     b
fput:
        move    b1,x:(r0)+
        move    (r4)+
        move    r4,a
        move    #>$8,x1
        cmp     x1,a
        blt     fnxt
        move    #>$0,r4
        move    (r1)+
fnxt:
        nop
fend:
        rts

; dxr / dyr: send x0 registers from X (Y) starting at r0 to the ColdFire, one word each, waiting for HTDE.
dxr:
        do      x0,dxe
dxw:
        jclr    #1,x:<<$ffffc3,dxw
        move    x:(r0)+,a
        movep   a1,x:<<$ffffc7
        do      #2000,dxd
        nop
dxd:
        nop
dxe:
        rts
dyr:
        do      x0,dye
dyw:
        jclr    #1,x:<<$ffffc3,dyw
        move    y:(r0)+,a
        movep   a1,x:<<$ffffc7
        do      #2000,dyd
        nop
dyd:
        nop
dye:
        rts

handback:
        movep   #>$0,x:<<$ffffe4
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
