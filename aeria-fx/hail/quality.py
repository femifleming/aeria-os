"""HAIL quality probe: tools/run.sh python3 /aeria/hail/quality.py <test asm>  (clicks at hit edges)"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/hail')
import aeria as A
import wxtest as T

FSR = 44100
mems, words, init, proc = A.assemble(sys.argv[1], 'AERIA HAIL')
names = ['AMT', 'RATE', 'SIZE', 'BNCE', 'WDTH'] + [''] * 7
d = [0, 64, 64, 64, 114] + [0] * 7
print(f'{len(words)} words')
secs = 20
blocks = secs * FSR // 16
n = blocks * 16
f = 300.0
x = A.sine(f, 0.5, n)
L, R, m = A.render('hlq', mems, init, proc, T.P(names, d, AMT=127, RATE=96), x, blocks, guard=True)
sd = 0.5 * A.FS * (2 * math.pi * f / FSR) ** 2
wet = [a - b for a, b in zip(L, x)]
pk = max(abs(v) for v in wet)
d2 = max(abs(a - 2 * b + c) for a, b, c in zip(wet[2:], wet[1:], wet))
print(f'300 Hz 0.5 FS, RATE 96, 20 s: wet peak {pk / A.FS:.3f} FS; largest second difference of the wet {d2 / sd:.2f} x '
      f'a clean 0.5 FS 300 Hz sine\'s (rate-2 stones read the slice at 600 Hz: 4 x); DC of the wet {A.dc(wet) if hasattr(A, "dc") else 0}')
