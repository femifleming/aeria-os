start:
        move    x:(r2)+,a
        jclr    #23,a1,start
        do      x0,lend
        move    x:(r2)+,a
        nop
lend:
        nop
        jpl     start
        jmi     start
        enddo
        tst     a
        bpl     fwd
        do      a1,l2
        nop
        nop
l2:
        nop
fwd:
        jset    #23,a1,start
        cmp     y1,b
        jeq     start
        jne     start
        move    r2,b
        rts
