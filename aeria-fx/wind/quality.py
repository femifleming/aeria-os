"""WIND quality probe: tools/run.sh python3 /aeria/wind/quality.py <asm>  (LEAN jumps on a 200 Hz sine)."""
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/wind')
import aeria as A
import wxtest as T
from pathlib import Path

FSR = 44100
asm = sys.argv[1]
mems, words, init, proc = A.assemble(asm, 'AERIA WIND')
names = ['AMT', 'DPTH', 'GUST', 'LEAN', '', ''] + [''] * 6
d = [0, 64, 64, 64, 0, 0] + [0] * 6


def lean_jumps(tag):
    secs = 6
    blocks = secs * FSR // 16
    n = blocks * 16
    f = 200.0
    x = A.sine(f, 0.5, n)
    ev = []
    for k, b in enumerate(range(int(1.0 * FSR / 16), blocks, int(0.2 * FSR / 16))):
        ev.append((b, T.P(names, d, AMT=127, DPTH=0, GUST=127, LEAN=127 if k % 2 == 0 else 0)))
    L, R, m = A.render(tag, mems, init, proc, T.P(names, d, AMT=127, DPTH=0, GUST=127, LEAN=0), x, blocks,
                       events=ev, guard=True)
    s0 = int(1.0 * FSR)
    sd = 0.5 * A.FS * (2 * math.pi * f / FSR) ** 2
    out = []
    for ch in (L, R):
        seg = ch[s0:]
        out.append(max(abs(a - 2 * b + c) for a, b, c in zip(seg[2:], seg[1:], seg)) / sd)
    return out, words


r, w = lean_jumps('wnq_lean')
print(f'{len(w)} words; LEAN 0<->127 every 0.2 s, 200 Hz sine, AMT 127 GUST 127 DPTH 0: largest second difference '
      f'L {r[0]:.2f} R {r[1]:.2f} x a clean sine\'s')
