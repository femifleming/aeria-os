"""Shared helpers of the five weather effects' generators (WIND FOG TWISTER HAIL AURORA).

The same file sits in each of the five folders (wind/wxlib.py is the master; the others are
copies), so every folder builds alone. It only makes source text:

  render(template, state, consts)   {NAME} -> x:(r7+$nn) for state words, {=NAME} -> literal
  rand_sub / exp2_sub / sin_sub / div_sub: the block-rate subroutines (always AFTER their callers)

dsp_asm rules kept here: mpy/mac only in the eight signed operand orders, r7 displacements
0..63, no rep, no parallel moves, no backward branches, subroutines after their callers.
"""
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from asmgen import OK_PAIRS, e2_sub  # noqa: E402

FS = 44100.0
BLOCK = 16
DT = BLOCK / FS                       # one control tick


def q(x):
    """Q23 literal of a fraction."""
    v = round(x * (1 << 23))
    v = max(-(1 << 23), min((1 << 23) - 1, v))
    return f'#>${v & 0xffffff:06x}'


def n(v):
    """Integer literal."""
    return f'#>${int(v) & 0xffffff:06x}'


def pole(tau, dt=DT):
    """One-pole coefficient 1 - e^(-dt/tau)."""
    return 1 - math.exp(-dt / tau)


class State:
    """r7 state words by name (displacements 0..63 only)."""

    def __init__(self, names):
        self.at = {}
        for i, nm in enumerate(names.split()):
            self.at[nm] = i
        assert len(self.at) <= 64, f'{len(self.at)} state words: over the 64 a displaced move reaches'

    def __getitem__(self, nm):
        return self.at[nm]

    def doc(self):
        return ' '.join(f'{v:02x} {k}' for k, v in self.at.items())


def render(template, state, consts):
    """{NAME} -> x:(r7+$nn); {=NAME} -> the literal consts[NAME] (already formatted)."""
    def sub(m):
        key = m.group(1)
        if key.startswith('='):
            return str(consts[key[1:]])
        return f'x:(r7+${state[key]:02x})'
    src = re.sub(r'\{(=?[A-Za-z0-9_]+)\}', sub, template)
    out = []
    for line in src.splitlines():
        m = re.match(r'\s+(mpy|mac|mpyr|macr)\s+-?(\w\w),(\w\w),', line)
        if m and (m[2], m[3]) not in OK_PAIRS:
            raise ValueError('mpy operand order dsp_asm mis-encodes: ' + line)
        if re.match(r'\s+rep\b', line) or re.match(r'\s+tfr\s+[ab],[ab]', line):
            raise ValueError('forbidden form: ' + line)
        m = re.search(r'\(r7\+\$([0-9a-f]+)\)', line)
        if m and int(m[1], 16) > 63:
            raise ValueError('r7 displacement over 63: ' + line)
        out.append(line.rstrip())
    return '\n'.join(out) + '\n'


def rand_sub(label, slot):
    """a = x0 = uniform [0,1) in Q23 (clean). 24-bit LCG s = 214013 s + 2531011 (mod 2^24, full
    period), top 23 bits. Clobbers a b x0 y0. `slot` is the state word's x:(r7+..) text."""
    return f'''{label}:
        move    {slot},x0
        move    #>$0343fd,y0
        mpy     y0,x0,a
        asr     a                       ; a1:a0 = the whole product, a0 its low 24 bits
        move    a0,x0
        move    x0,a
        add     #>$269ec3,a
        move    a1,{slot}
        move    a1,b
        lsr     b
        move    b1,x0
        move    x0,a
        rts
'''


def exp2_sub(label, endlabel):
    """b = 2^-y for a = y/32 (Q23, y in [0,32)); clobbers a b x0 x1 y0 (asmgen.e2_sub)."""
    return '\n'.join(e2_sub(label, endlabel)) + '\n'



# 0.5 sin(pi u / 2) on u in [-1, 1]: odd quintic, least squares fit (gen-time), |err| < 2e-5
def _sinfit():
    import itertools
    xs = [i / 400 for i in range(401)]
    # normal equations for a1 x + a3 x^3 + a5 x^5 ~ 0.5 sin(pi x / 2)
    rows = [[x, x ** 3, x ** 5] for x in xs]
    ys = [0.5 * math.sin(math.pi * x / 2) for x in xs]
    A = [[sum(r[i] * r[j] for r in rows) for j in range(3)] for i in range(3)]
    b = [sum(r[i] * y for r, y in zip(rows, ys)) for i in range(3)]
    for i in range(3):
        p = A[i][i]
        A[i] = [v / p for v in A[i]]
        b[i] /= p
        for j in range(3):
            if j != i:
                f = A[j][i]
                A[j] = [u - f * v for u, v in zip(A[j], A[i])]
                b[j] -= f * b[i]
    del itertools
    return b


SIN_C = _sinfit()


def sin_lines(x='x0', acc='a'):
    """acc = 0.5 sin(pi u / 2), u in x0 (Q23, [-1,1]). Clobbers x1 y0 and acc. Inline lines."""
    assert x == 'x0'
    return f'''        mpy     x0,x0,{acc}
        move    {acc},x1                    ; u^2
        move    {q(SIN_C[2])},y0
        mpy     x1,y0,{acc}
        add     {q(SIN_C[1])},{acc}
        move    {acc},y0
        mpy     x1,y0,{acc}
        add     {q(SIN_C[0])},{acc}
        move    {acc},y0
        mpy     y0,x0,{acc}                 ; 0.5 sin(pi u / 2)
'''


def sin_sub(label):
    """Subroutine form of sin_lines: a = 0.5 sin(pi x0 / 2); clobbers a x1 y0."""
    return f'{label}:\n' + sin_lines() + '        rts\n'


def sin_model(u):
    return SIN_C[0] * u + SIN_C[1] * u ** 3 + SIN_C[2] * u ** 5


def div_sub(label, endlabel):
    """a0 = a1 / x0 as a Q23 fraction for 0 <= a < x0 (both positive); then b = that fraction
    (clean). 24 div steps, the form COMP uses. Clobbers a b."""
    return f'''{label}:
        move    a1,a                    ; a clean 48-bit dividend
        andi    #$fe,ccr
        do      #24,>{endlabel}
        div     x0,a
{endlabel}:
        nop
        move    a0,b
        rts
'''


SMOOTHSTEP = '''        mpy     x0,x0,a
        move    a,x1                    ; x^2
        move    #>$600000,a
        move    #>$400000,y0
        mac     -y0,x0,a                ; 0.75 - x/2
        move    a,y0
        mpy     x1,y0,a
        asl     #$2,a,a                 ; x^2 (3 - 2x); the move that takes it limits at 1
'''


def write(path, text, quiet=False):
    Path(path).write_text(text)
    if not quiet:
        print(f'wrote {path} ({len(text.splitlines())} lines)')
