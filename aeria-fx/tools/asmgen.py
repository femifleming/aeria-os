"""Tiny macro helpers shared by the AERIA effect generators (gen.py per effect).

Every helper returns DSP56300 source lines. Conventions (see AGENTS.md traps):
 - mpy/mac operand pairs are written ONLY in the orders dsp_asm encodes
   signed: x0,x0 y0,y0 x1,x0 y1,y0 x0,y1 y0,x0 x1,y0 y1,x1.
 - no rep, no tfr a,b, no Tcc from an accumulator, displacements -64..63.
"""
import math

OK_PAIRS = {('x0', 'x0'), ('y0', 'y0'), ('x1', 'x0'), ('y1', 'y0'), ('x0', 'y1'),
            ('y0', 'x0'), ('x1', 'y0'), ('y1', 'x1')}


def q(x):
    """Q23 hex literal of a fraction (two's complement)."""
    v = round(x * (1 << 23))
    v = max(-(1 << 23), min((1 << 23) - 1, v))
    return f'#>${v & 0xffffff:06x}'


def ins(op, *args, c=''):
    s = f'        {op:<8}{",".join(args)}'
    return s + (f'  ; {c}' if c else '')


def check_mul(line):
    import re
    m = re.match(r'\s+(mpy|mac|mpyr|macr)\s+-?(\w\w),(\w\w),', line)
    if m and (m[2], m[3]) not in OK_PAIRS:
        raise ValueError('mpy operand order would mis-encode: ' + line)
    return line


def smooth(tgt, cur, shift, lo=None):
    """cur += (tgt - cur) >> shift. With lo: 48-bit state (cur hi, lo word)."""
    out = [ins('move', f'x:(r7+${tgt:02x})', 'a'), ins('move', f'x:(r7+${cur:02x})', 'b')]
    if lo is not None:
        out.append(ins('move', f'x:(r7+${lo:02x})', 'b0'))
    out += [ins('sub', 'b', 'a'), ins('asr', f'#${shift:x}', 'a', 'a'), ins('add', 'a', 'b')]
    if lo is not None:
        out += [ins('move', 'b1', f'x:(r7+${cur:02x})'), ins('move', 'b0', f'x:(r7+${lo:02x})')]
    else:
        out += [ins('move', 'b', f'x:(r7+${cur:02x})')]
    return out


def softclip(src='a', dst='a'):
    """dst = s - s^3/3 of the LIMITED src (|out| <= 2/3, unity slope at 0).
    Clobbers x0 x1 y0 y1 and dst."""
    return [ins('move', f'{src}', 'x0'), ins('mpy', 'x0', 'x0', dst), ins('move', dst, 'x1'),
            ins('mpy', 'x1', 'x0', dst), ins('move', dst, 'y1'), ins('move', 'x0', dst),
            ins('move', q(1 / 3), 'y0'), ins('mac', '-y1', 'y0', dst)]


def mix(wet='a', mixslot=0x11, last=True):
    """x:(r0)+ <- dry + m*(wet - dry), with 1 bit headroom on the difference.
    wet in a. Clobbers a b x0 y1."""
    assert wet == 'a'
    return [ins('move', f'x:(r7+${mixslot:02x})', 'y1'), ins('move', 'x:(r0)', 'b'),
            ins('sub', 'b', 'a'), ins('asr', 'a'), ins('move', 'a', 'x0'),
            ins('mpy', 'x0', 'y1', 'a'), ins('asl', 'a'), ins('add', 'b', 'a'),
            ins('move', 'a', 'x:(r0)+')]


def finish(lines):
    out = []
    for l in lines:
        for s in (l if isinstance(l, list) else [l]):
            out.append(check_mul(s))
    return '\n'.join(out) + '\n'


def e2_sub(label, endlabel):
    """b = 2^-y for a = y/32 (Q23, y in [0,32)); clobbers a b x0 x1 y0.
    2^-f = (e^(-f ln2 / 2))^2, the half-angle exponential by 4th-order Taylor
    (|err| < 1e-4 relative, 0.15 cent), then n+1 arithmetic right shifts and
    one left shift (exact: the shifted-out bits stay in b0)."""
    L = label
    return [
        f'{L}:',
        ins('move', 'a1', 'b'),
        ins('asr', '#$12', 'a', 'a', c='a1 = n = floor(y)'),
        ins('add', '#>$1', 'a'),
        ins('move', 'a1', 'x1', c='n + 1 (never 0: a do count of 0 is not a skip)'),
        ins('and', '#>$03ffff', 'b'),
        ins('asl', '#$5', 'b', 'b', c='f in Q23, b2 stays 0'),
        ins('move', 'b1', 'x0'),
        ins('move', q(math.log(2) / 2), 'y0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('move', 'b', 'y0', c='g = f ln2 / 2'),
        ins('move', q(1 / 48), 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('sub', q(1 / 12), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('add', q(0.25), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('sub', q(0.5), 'b'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'y0', 'x0', 'b'),
        ins('add', q(0.5), 'b', c='e^-g / 2'),
        ins('move', 'b', 'x0'),
        ins('mpy', 'x0', 'x0', 'b'),
        ins('asl', '#$2', 'b', 'b', c='2^-f in (0.5, 1]'),
        ins('do', 'x1', f'>{endlabel}'),
        ins('asr', 'b'),
        f'{endlabel}:',
        ins('asl', 'b'),
        ins('rts'),
    ]


def snap_close(t, s, label, thr=0x100):
    """Outside the sample loop: when |target - smoothed| < thr LSB, land on
    the target (the floor in asr stalls an upward glide up to 2^k - 1 LSB
    short; a mix that never reaches full wet leaks dry)."""
    return [ins('move', f'x:(r7+${t:02x})', 'a'), ins('move', f'x:(r7+${s:02x})', 'b'),
            ins('sub', 'b', 'a'), ins('abs', 'a'), ins('cmp', f'#>${thr:06x}', 'a'),
            ins('bge', label), ins('move', f'x:(r7+${t:02x})', 'a'),
            ins('move', 'a', f'x:(r7+${s:02x})'), f'{label}:']
