; AERIA OS DSP 0.3 -- the slot map (core 0). Our own code. Loaded at P:$33000 like thru.asm.
; Outputs: a 441 Hz square tone (about -21 dB) on ONE TX slot, the one the ColdFire last sent over the
;   host port (a word 0-7; 8 = silence; 9 = every slot); every other slot silent. Slots are counted by our own TX word
;   counter (mod 8) and the tone flips every 400 TX words (50 frames): run 1 showed TFS is not seen at
;   TDE time, so nothing here depends on it. A run's slot numbers are a fixed rotation of the real ones.
; ESAI_1 (Y side) is set up as stock sets it up too, and serviced polled: the ColdFire's word picks the port
;   and slot -- 0-7 / 9 (all) on the ESAI, $10-$17 / $19 (all) on ESAI_1, 8 = silence everywhere.
; Heartbeat (4 a second): one RX slot's full peak a beat, rotating: (slot << 22) | (peak >> 2); each slot's window 1 s.
; Inputs: the peak of each RX slot over a second (RFS marks slot 0); once a second the four peaks go to
;   the ColdFire as one word, 6 bits each (slot 0 in bits 23..18 ... slot 3 in bits 5..0).
; ESAI_1 is primed with $123456 and then left alone, as stock does (P:$30069): it repeats that word forever,
;   which the board appears to need (9 Nov 2026: silent outputs whenever ESAI_1 carried anything else).
;   Only an explicit ESAI_1 test command ($10-$19) writes it.
; Start-up in stock's order (payload A P:$30000): the lines below, ESAI_1, then the ESAI, wait for TFS and
;   prime 7 TX words, then run. Start-up also does what stock's payload A does first (P:$30014): y:$ffffe4 = $3fff and bit 14 of y:$fffffd
;   and y:$fffffe (probably the board's lines to the converters: without them the loopback heard nothing on
;   either port, 9 Nov 2026). Hand-back clears the two bits again, as the park always has.
; Hand-back on host flag HF0, exactly as thru.asm.
; dsp_asm rules (TRAPS): no rep, no backward bra, no brclr, no long jmp/jcc, no mpy; jclr/jset absolute.
;
; X memory (short addresses reach $00-$3f only): $3f = 0 (the always-taken jump), $30 the tone's slot,
;           $31 TX slot counter, $32 RX slot counter, $34 tone half-period counter, $35 tone value, $38-$3b RX peaks.

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
        move    a,x:<$3f
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
        clr     a
        move    a,x:<$33
        move    a,x:<$3f
        move    a,x:<$31
        move    a,x:<$32
        move    a,x:<$34
        move    a,x:<$36
        move    a,x:<$37
        move    a,x:<$38
        move    a,x:<$39
        move    a,x:<$3a
        move    a,x:<$3b
        move    a,x:<$3c
        move    #>$9,x0
        move    x0,x:<$30
        move    #>$180000,x0
        move    x0,x:<$35
        move    #>$0,r5
        move    #>$15888,y1

mainloop:
        jset    #3,x:<<$ffffc3,handback
        jclr    #0,x:<<$ffffc3,nocmd
        movep   x:<<$ffffc6,x0
        move    x0,x:<$30
nocmd:
        move    x:<$30,a
        move    #>$10,x1
        cmp     x1,a
        blt     noesai1
        jclr    #15,y:<<$ffff93,noesai1
        move    x:<$33,a
        move    x:<$30,x1
        move    #>$10,y0
        cmp     y0,a
        clr     b
        move    x1,a
        move    #>$19,y0
        cmp     y0,a
        beq     e1tone
        move    x:<$33,a
        move    #>$10,y0
        add     y0,a
        cmp     x1,a
        bne     e1write
e1tone:
        move    x:<$35,b
e1write:
        movep   b,y:<<$ffff80
        move    x:<$33,a
        move    #>$1,y0
        add     y0,a
        move    #>$7,y0
        and     y0,a
        move    a1,x:<$33
noesai1:
        jclr    #8,x:<<$ffffb3,notrx
        jclr    #6,x:<<$ffffb3,rxcount
        clr     a
        move    a,x:<$32
        move    x:<$37,a
        move    #>$1,x1
        add     x1,a
        move    a1,x:<$37
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
notrx:
        jclr    #15,x:<<$ffffb3,mainloop
        jclr    #13,x:<<$ffffb3,notfs
        move    x:<$36,a
        move    #>$1,x1
        add     x1,a
        move    a1,x:<$36
notfs:
        move    x:<$34,a
        move    #>$1,x1
        add     x1,a
        move    #>$190,x1
        cmp     x1,a
        blt     tonekeep
        clr     a
        move    x:<$35,b
        neg     b
        move    b1,x:<$35
tonekeep:
        move    a1,x:<$34
txslot:
        move    x:<$30,b
        move    #>$9,x1
        cmp     x1,b
        beq     txall
        move    x:<$31,a
        move    x:<$30,x1
        cmp     x1,a
        beq     txtone
        clr     b
        bra     txwrite
txall:
        move    x:<$31,a
txtone:
        move    x:<$35,b
txwrite:
        movep   b,x:<<$ffffa0
        move    #>$1,x1
        add     x1,a
        move    #>$7,x1
        and     x1,a
        move    a1,x:<$31
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

handback:
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
