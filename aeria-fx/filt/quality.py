"""FILT probe (read-only): output level across RES (LP, CUT 64 = 650 Hz). tools/run.sh python3 /aeria/filt/quality.py"""
import math
import random
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

FSR = 44100
mems, words, init, proc = A.assemble('/aeria/filt/filt.asm', 'AERIA FILT')
# CUT RES MODE SKEW LRNZ MIX
base = [64, 0, 0, 64, 0, 127] + [0] * 6
blocks = 3 * FSR // 16
n = blocks * 16
rnd = random.Random(5)
# pink-ish noise: sum of octave-spaced one-pole-filtered white (Voss-like), 0.25 FS rms
w = [rnd.gauss(0, 1) for _ in range(n)]
st = [0.0] * 7
pink = []
for v in w:
    acc = 0.0
    for k in range(7):
        a = 2 ** -k
        st[k] += a * (v - st[k])
        acc += st[k] * math.sqrt(2 ** k) * 0.3
    pink.append(acc)
r = math.sqrt(sum(v * v for v in pink) / n)
pink = [round(v / r * 0.25 * A.FS) for v in pink]
sig = {'sine 100 Hz 0.5 FS': A.sine(100.0, 0.5, n), 'sine at the cutoff (650 Hz) 0.5 FS': A.sine(650.3, 0.5, n),
       'pink noise 0.25 FS rms': pink}
for res in (0, 64, 96, 112, 127):
    row = []
    for nm, x in sig.items():
        p = list(base)
        p[1] = res
        L, R, m = A.render('flq', mems, init, proc, p, x, blocks, guard=True)
        s0 = FSR
        row.append(f'{nm}: rms {A.db(A.rms(L[s0:]) / A.rms(x[s0:])):+.1f} dB, peak {A.db(A.peak(L[s0:]) / A.peak(x[s0:])):+.1f} dB')
    print(f'RES {res}: ' + '; '.join(row))
