; core 1 probe: count; write the count to shared X:$3F000; read it back; send (n<<20 | low 20 bits) words:
; n 0 the counter, n 1 what X:$3F000 reads back, n 2 X:$3F060's +7 (core 0's requests for slot 5)
        clr     a
        move    a,x:<$3f
        move    a,x:<$30
p1lp:
        move    x:<$30,a
        move    #>$1,x0
        add     x0,a
        move    a1,x:<$30
        move    a1,x:>$3f000
        jclr    #1,x:<<$ffffc3,p1nx
        move    x:<$31,b
        move    #>$1,x0
        add     x0,b
        move    #>$3,x0
        and     x0,b
        move    b1,x:<$31
        move    b1,r2
        move    x:<$30,a
        move    #>$1,x0
        cmp     x0,b
        bne     p1b
        move    x:>$3f000,a
p1b:
        move    #>$2,x0
        cmp     x0,b
        bne     p1c
        move    x:>$3f067,a
p1c:
        move    #>$fffff,x0
        and     x0,a
        move    r2,b
        lsl     #20,b
        move    b1,x0
        or      x0,a
        movep   a1,x:<<$ffffc7
p1nx:
        jclr    #0,x:<$3f,p1lp
