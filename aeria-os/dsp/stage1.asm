; What a cold DSP needs before anything (stock's first stages do the same, read for understanding, 10 Nov 2026):
; bit 18 of y:$fffff9 and y:$fffffa on each core (without them a cold core 0's ESAI never frame-synced), and on
; core 1 x:$ffff7d = $2960b0 (the shared PLL: a cold DSP otherwise runs from its crystal).
; AERIA OS DSP stage 1. Our own code. Sent through the boot ROM to P:$31000 (where stock's first stage goes: a cold
; boot ROM took 19 words of the whole engine and stopped, 10 Nov 2026), it loads the real program the same way the
; ROM does -- count, address, words, through the host port, into the shared window (X writes reach P there) -- and
; jumps to it. x:$3f is zeroed for the backward-jump idiom.
        ori     #$03,mr
        bset    #$12,y:<<$fffff9
        bset    #$12,y:<<$fffffa
        clr     b
        move    b,x:<$3f
        move    #>$1,x1
s1cnt:
        jclr    #0,x:<<$ffffc3,s1cnt
        movep   x:<<$ffffc6,a
s1adr:
        jclr    #0,x:<<$ffffc3,s1adr
        movep   x:<<$ffffc6,r0
        move    r0,r1
s1wrd:
        jclr    #0,x:<<$ffffc3,s1wrd
        movep   x:<<$ffffc6,x:(r0)+
        sub     x1,a
        beq     s1go
        jclr    #0,x:<$3f,s1wrd
s1go:
        jmp     (r1)
