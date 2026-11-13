#!/usr/bin/env python3
"""Writes the anti-alias low-pass (13 Nov 2026) into pitch.asm from pitch.asm.preaa (the hand-written source before it).

    python3 pitch/aa_patch.py        (host or build image, no dependencies)

Three insertions, all generated from aa_design.py's numbers: the per-block coefficient update (subroutine ptlpset, only
when SHFT + FINE changed), the limit-cycle flush (once a block), and the filter on each side's line input (input +
feedback) in the frame loop.
"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import aa_design as D  # noqa: E402

# r7 words (free after $2b)
S = dict(B=0x2c, A1H=0x2d, A2=0x2e, CH=0x2f, P=0x30, K=0x31, SC=0x32,
         W1L=0x33, W2L=0x34, V1L=0x35, W1R=0x36, W2R=0x37, V1R=0x38, MR=0x39, S32=0x3a)
FLUSH = 0x60                     # LSB: |w1| + |w2| + |v1| summed over both sides at or under this: cleared


def x(name):
    return f'x:(r7+${S[name]:02x})'


def h(v):
    return f'#>${v & 0xffffff:06x}'


def lines():
    """the line input of both sides through the low-pass, interleaved (A1H, A2, CH, P, K loaded once or twice for
    the pair). In: y1 = fb (the ramped feedback gain). a = left, b = right; both written to the ring."""
    return f'''; ---- write input + feedback through the anti-alias low-pass (both channels; see aa_design.py) ----
;   w = B (in + fb s) + 2 A1H w1 + A2 w2;  u = w + 2 CH w1 + K w2;  v = u + P v1;  y = v + K v1
        move    {x('B')},x0
        mpy     x0,y1,b
        move    b,y1                    ; B fb
        move    x:(r0),y0
        mpy     y0,x0,a
        move    x:(r7+$16),x1
        mac     y1,x1,a                 ; B (left in + fb s)
        move    x:(r0+n0),y0
        mpy     y0,x0,b
        move    x:(r7+$17),x1
        mac     y1,x1,b                 ; B (right in + fb s)
        move    {x('A1H')},y0
        move    {x('W1L')},x1
        mac     x1,y0,a
        mac     x1,y0,a
        move    {x('W1R')},x0
        mac     y0,x0,b
        mac     y0,x0,b
        move    {x('W2L')},y1
        move    {x('A2')},y0
        macr    y1,y0,a                 ; w left
        move    x1,{x('W2L')}
        move    a,{x('W1L')}
        move    {x('CH')},y0
        mac     x1,y0,a
        mac     x1,y0,a
        move    {x('K')},x1
        mac     y1,x1,a                 ; u left
        move    {x('W2R')},y1
        move    {x('A2')},y0
        macr    y1,y0,b                 ; w right
        move    x0,{x('W2R')}
        move    b,{x('W1R')}
        move    {x('CH')},y0
        mac     y0,x0,b
        mac     y0,x0,b
        mac     y1,x1,b                 ; u right
        move    {x('P')},y0
        move    {x('V1L')},y1
        macr    y1,y0,a                 ; v left
        move    a,{x('V1L')}
        macr    y1,x1,a                 ; y left
        move    {x('V1R')},y1
        macr    y1,y0,b                 ; v right
        move    b,{x('V1R')}
        macr    y1,x1,b                 ; y right
        move    a,y:(r5)+
        move    b,y:(r5)+
'''


def horner(k, name):
    row = D.POLY[k]
    out = f'        move    {h(row[D.DEG])},a\n'
    for i in range(D.DEG - 1, -1, -1):
        out += f'        move    a,x0\n        mpy     x0,y1,a\n        add     {h(row[i])},a\n'
    return out + f'        asl     a\n        move    a,{x(name)}\n'


def sub():
    smin, smax = D.q23(D.SMIN / 32), D.q23(D.SMAX / 32)
    kt = D.q23(1 / D.UW / 4)
    out = f'''; ---- anti-alias low-pass coefficients (13 Nov 2026): only when SHFT + FINE changed ----
; s32 = semitones / 32 as at the top of proc; <= 0: no filter (B = 1, the rest 0); else clamped to
; [{D.SMIN:.2f}, {D.SMAX}] / 32, t = (s32 - {D.UC:.6f}) / {D.UW:.6f}, A1H A2 CH P degree-{D.DEG} polynomials in t,
; then B = (1 - 2 A1H - A2)(1 - P) / (4 (1 + CH)): the DC gain is exactly 1
ptlpset:
        move    {x('S32')},a            ; s32, kept at the top of proc
        tst     a
        bgt     ptaaup
        move    #>$800000,x0
        move    {x('SC')},b
        cmp     x0,b
        beq     ptaaout
        move    x0,{x('SC')}
        move    #>$7fffff,x0
        move    x0,{x('B')}
        clr     b
        move    b,{x('A1H')}
        move    b,{x('A2')}
        move    b,{x('CH')}
        move    b,{x('P')}
        move    b,{x('K')}
ptaaout:
        rts
ptaaup:
        move    {h(smin)},x0
        cmp     x0,a
        tlt     x0,a
        move    {h(smax)},x0
        cmp     x0,a
        tgt     x0,a
        move    {x('SC')},x0
        cmp     x0,a
        bne     ptaanew
        rts
ptaanew:
        move    a,{x('SC')}
        sub     {h(D.q23(D.UC))},a
        move    a,x0
        move    {h(kt)},y0
        mpy     y0,x0,a
        asl     #$2,a,a
        move    a,y1                    ; t
'''
    for k, name in enumerate(('A1H', 'A2', 'CH', 'P')):
        out += horner(k, name)
    out += f'''        move    #>$7fffff,x0
        move    x0,{x('K')}
        move    #>$100000,a
        move    {x('A1H')},x0
        move    #>$200000,y0
        mac     -y0,x0,a
        move    {x('A2')},x0
        move    #>$100000,y0
        mac     -y0,x0,a
        move    a,x1                    ; (1 - 2 A1H - A2) / 8
        move    #>$7fffff,a
        move    {x('P')},x0
        sub     x0,a
        move    a,y0                    ; 1 - P
        mpy     x1,y0,a
        move    a1,a
        move    {x('CH')},b
        asr     b
        add     #>$400000,b
        move    b,x0                    ; (1 + CH) / 2
        andi    #$fe,ccr
        do      #24,>ptaadiv
        div     x0,a
ptaadiv:
        nop
        move    a0,{x('B')}
        rts
'''
    return out


def flush():
    out = ('; anti-alias low-pass: a rounding limit cycle (a few LSB) is cleared, looked for every 8th block (r5 moves\n'
           '; 32 words a block)\n        move    r5,a\n        and     #>$0000e0,a\n        bne     ptaakeep\n')
    first = True
    for nm in ('W1L', 'W2L', 'V1L', 'W1R', 'W2R', 'V1R'):
        if first:
            out += f'        move    {x(nm)},a\n        abs     a\n'
            first = False
        else:
            out += f'        move    {x(nm)},b\n        abs     b\n        add     b,a\n'
    out += f'        cmp     {h(FLUSH)},a\n        bgt     ptaakeep\n        clr     a\n'
    for nm in ('W1L', 'W2L', 'V1L', 'W1R', 'W2R', 'V1R'):
        out += f'        move    a,{x(nm)}\n'
    return out + 'ptaakeep:\n'


def build():
    src = (HERE / 'pitch.asm.preaa').read_text()

    def rep(old, new, cnt=1):
        nonlocal src
        assert src.count(old) == cnt, old
        src = src.replace(old, new)
    rep(''';  (12 Nov quality pass: tone LP and DC block keep 48-bit states so tails reach exact 0;''',
        ''';  2c..31 anti-alias low-pass B A1H A2 CH P K  32 its s32 (the last update)
;  33/34/35 left w1 w2 v1  36/37/38 right w1 w2 v1  39 mix ramp a sample  3a s32 (top of proc)
;  (13 Nov 2026, "stop aliasing": an up-shift folds what sits above fs / (2 ratio) back down. Each side's line
;   input (input + feedback) goes through an order-3 elliptic-type low-pass that tracks SHFT + FINE: what would
;   land under 16 kHz is >= 37 dB down, the top of the shifted sound is kept to within 1 dB up to 16 kHz out at
;   +12, 14 kHz at +24 (aa_design.py; this file is written by aa_patch.py from pitch.asm.preaa). At or under
;   unison the filter is off (B = 1 - 2^-23, the rest 0).)
;  (12 Nov quality pass: tone LP and DC block keep 48-bit states so tails reach exact 0;''')
    rep('''ptwdon:
''', '''ptwdon:
        bsr     >ptlpset                ; anti-alias low-pass coefficients (when SHFT + FINE changed)
''')
    rep('''        move    #>$3dfd,m1
        move    #>$3dfd,m2
''', flush() + '''        move    #>$3dfd,m1
        move    #>$3dfd,m2
''')
    rep('''        move    a,y1
        move    x:(r7+$16),x0
        move    x:(r0),a
        mac     x0,y1,a
        move    a,y:(r5)+
        move    x:(r7+$17),x0
        move    x:(r0+n0),a
        mac     x0,y1,a
        move    a,y:(r5)+
''', '''        move    a,y1
''' + lines())
    # s32 kept for the low-pass update
    rep('''        move    #>$640000,x0
        sub     x0,a
        move    a,x0
''', '''        move    #>$640000,x0
        sub     x0,a
        move    a,x:(r7+$3a)            ; s32 for the anti-alias low-pass update
        move    a,x0
''')
    # mix: a per-sample ramp set once a block (was a per-sample one-pole, asr 8): the same glide, 2 fewer a sample
    rep('''        move    x:(r7+$0e),a
        move    x:(r7+$0f),b
        sub     b,a
        asr     #$8,a,a
        add     b,a
        move    a,x:(r7+$0f)
''', '''        move    x:(r7+$0f),a
        move    x:(r7+$39),x0
        add     x0,a
        move    a,x:(r7+$0f)            ; mix += its ramp (set per block)
''')
    rep('''ptmglide:
''', '''ptmglide:
; the mix ramp for this block: 1/256 of the way a sample (the old per-sample glide's rate; 0 once landed)
        move    x:(r7+$0e),a
        move    x:(r7+$0f),b
        sub     b,a
        asr     #$8,a,a
        move    a,x:(r7+$39)
''')
    src = src.rstrip('\n') + '\n' + sub()
    (HERE / 'pitch.asm').write_text(src)
    print('wrote pitch.asm')


if __name__ == '__main__':
    build()
