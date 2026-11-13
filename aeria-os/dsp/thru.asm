; AERIA OS DSP 0.1 -- first light (core 0). Our own code.
; Loaded by the boot ROM (or a parked core's ROM stand-in) at P:$33000, in the shared window, clear of
; where the next OS puts its bootstraps ($31000 / $32000).
;
; 1. The ESAI as the board needs it: the values and the order were read from stock's set-up for
;    understanding (AUDIO.md 3.1): Port H clocks from EXTAL, SAICR SYN, TX0 primed, TCCR/TCR, RCCR/RCR,
;    slot masks (8 out, 4 in), then Port C pins on last.
; 2. Every output slot = the mean of the 4 latest input words / 2 (each input /8, summed): so every input
;    reaches every jack whatever the slot map is.
; 2b. About once a second the DSP sends SAISR to the ColdFire (HOTX): proof it runs and the ESAI moves.
; 3. Host flag HF0 from the ColdFire = hand back: ESAI off, the host port as the boot ROM leaves it, then
;    this core IS a boot ROM (word count, load address, words, jump), as Octabam's REMIX SWITCH park
;    (MIT, Sam Banks) proved on a unit -- so the next OS loads as after a power-on.
; No rep, no backward bra, no brclr, no long jmp (dsp_asm traps): jclr/jset with absolute labels only.

start:
        move    #>$300,sr
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
        move    a,x:<$3f                ; a word kept at 0: `jclr #0,x:<$3f,L` is an always-taken long jump
        move    #>$0,r5                 ; TX words since the last heartbeat
        move    #>$56220,y1             ; 8 slots x 44100 = one heartbeat a second
        move    #>$100,r4
        move    #>$3,m4
        clr     b
        move    b,x:(r4)+
        move    b,x:(r4)+
        move    b,x:(r4)+
        move    b,x:(r4)+
mainloop:
        jset    #3,x:<<$ffffc3,handback
        jclr    #8,x:<<$ffffb3,txcheck
        movep   x:<<$ffffa8,x0
        move    x0,a
        asr     #3,a,a
        move    x:(r4),x1
        sub     x1,b
        move    a1,x1
        add     x1,b
        move    x1,x:(r4)+
txcheck:
        jclr    #15,x:<<$ffffb3,mainloop
        movep   b,x:<<$ffffa0
        move    (r5)+
        move    r5,a
        cmp     y1,a
        blt     nobeat
        move    #>$0,r5
        jclr    #1,x:<<$ffffc3,nobeat   ; HTDE: the host has taken the last one
        movep   x:<<$ffffb3,a           ; the heartbeat is SAISR as it stands (TUE 14, ROE 7: under/overruns)
        movep   a,x:<<$ffffc7           ; HOTX
nobeat:
        jclr    #0,x:<$3f,mainloop

handback:
        movep   #>$0,x:<<$ffffb5
        movep   #>$0,x:<<$ffffb7
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
