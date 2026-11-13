; core 1 memory probe v2 (an "effect": init at +0, proc at +2). Non-destructive: each probed word is saved and put back,
; and it stops at the first failure. Every $400th word of Y from $800 and of X from $2400, up to $2fc00.
; $3F001 first bad Y address (0 = none), $3F002 why (1 does not hold, 2 mirrors the base, 3 is the shared window),
; $3F003 / $3F004 the same for X, $3F005 = $abcde when done.
        jclr    #0,x:<$3f,mtinit
        jclr    #0,x:<$3f,mtproc
mtinit:
        move    #>$400,n1

        clr     a
        move    a,x:>$3f006
        move    #>$5a5a5a,x1
        move    x1,y:>$400
        move    #>$800,r1
        do      #190,mylp
        move    x:>$3f006,a
        tst     a
        bne     mysk
        move    y:(r1),y0
        move    r1,x0
        move    x0,y:(r1)
        move    #>$1,y1
        move    y:(r1),a
        cmp     x0,a
        bne     myfl
        move    #>$2,y1
        move    y:>$400,a
        move    #>$5a5a5a,x1
        cmp     x1,a
        bne     myfl
        move    r1,a
        move    #>$00ffff,x1
        and     x1,a
        move    #>$030000,x1
        or      x1,a
        move    a1,r2
        move    #>$3,y1
        move    y:(r2),a
        cmp     x0,a
        bne     myok
myfl:
        move    y1,x:>$3f002
        move    r1,x:>$3f001
        move    y1,x:>$3f006
myok:
        move    y0,y:(r1)
mysk:
        move    (r1)+n1
        nop
mylp:
        nop

        clr     a
        move    a,x:>$3f006
        move    #>$5a5a5a,x1
        move    x1,x:>$2000
        move    #>$2400,r1
        do      #183,mxlp
        move    x:>$3f006,a
        tst     a
        bne     mxsk
        move    x:(r1),y0
        move    r1,x0
        move    x0,x:(r1)
        move    #>$1,y1
        move    x:(r1),a
        cmp     x0,a
        bne     mxfl
        move    #>$2,y1
        move    x:>$2000,a
        move    #>$5a5a5a,x1
        cmp     x1,a
        bne     mxfl
        move    r1,a
        move    #>$00ffff,x1
        and     x1,a
        move    #>$030000,x1
        or      x1,a
        move    a1,r2
        move    #>$3,y1
        move    x:(r2),a
        cmp     x0,a
        bne     mxok
mxfl:
        move    y1,x:>$3f004
        move    r1,x:>$3f003
        move    y1,x:>$3f006
mxok:
        move    y0,x:(r1)
mxsk:
        move    (r1)+n1
        nop
mxlp:
        nop

        move    #>$abcde,x0
        move    x0,x:>$3f005
        rts
mtproc:
        rts
