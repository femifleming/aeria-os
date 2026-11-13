"""TWISTER quality probe: tools/run.sh python3 /aeria/twister/quality.py <test asm>"""
import math
import random
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/twister')
import aeria as A
import wxtest as T

FSR = 44100
mems, words, init, proc = A.assemble(sys.argv[1], 'AERIA TWISTER')
names = ['AMT', 'RISE', 'SPIN', 'TIME'] + [''] * 8
d = [0, 7, 64, 0] + [0] * 8
print(f'{len(words)} words')


def d2max(ch, s0, s1, f):
    seg = ch[s0:s1]
    return max(abs(a - 2 * b + c) for a, b, c in zip(seg[2:], seg[1:], seg)) / (0.5 * A.FS * (2 * math.pi * f / FSR) ** 2)


secs = 9
blocks = secs * FSR // 16
n = blocks * 16
f = 200.0
x = A.sine(f, 0.5, n)
base = T.P(names, d, AMT=127, TIME=64)          # an 8 s event
L0, R0, _ = A.render('twq_still', mems, init, proc, base, x, blocks, guard=True)
res = {}
for tag, kn in (('RISE', ('RISE',)), ('SPIN', ('SPIN',)), ('both', ('RISE', 'SPIN'))):
    rnd = random.Random(5)
    ev = []
    for b in range(FSR // 16, 7 * FSR // 16, int(0.15 * FSR / 16)):
        kw = dict(AMT=127, TIME=64)
        if 'RISE' in kn:
            kw['RISE'] = rnd.choice((0, 12))
        if 'SPIN' in kn:
            kw['SPIN'] = rnd.choice((0, 127))
        ev.append((b, T.P(names, d, **kw)))
    L1, R1, _ = A.render('twq_jump', mems, init, proc, base, x, blocks, events=ev, guard=True)
    res[tag] = (L1, R1)
for tag, (Lx, Rx) in [('knobs still', (L0, R0))] + [(f'{k} jumping every 0.15 s', v) for k, v in res.items()]:
    print(f'{tag}: largest second difference 1..7 s: L {d2max(Lx, FSR, 7 * FSR, f):.2f} R {d2max(Rx, FSR, 7 * FSR, f):.2f} '
          f'x a clean 200 Hz sine\'s')
