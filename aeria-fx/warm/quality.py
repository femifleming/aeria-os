"""WARM quality probe (read-only): stereo balance, DC. tools/run.sh python3 /aeria/warm/quality.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

FSR = 44100
mems, words, init, proc = A.assemble('/aeria/warm/warm.asm', 'AERIA WARM')
NAMES = ['HEAT', 'TONE', 'BIAS', 'SHAP', 'LEVL', 'MIX']
D = [40, 110, 0, 0, 52, 127] + [0] * 6


def P(**kw):
    p = list(D)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


blocks = 4 * FSR // 16
n = blocks * 16
x = A.noise(n, 0.3, 4)
for tag, p in (('defaults', P()), ('HEAT 127 BIAS 127 LEVL 40', P(HEAT=127, BIAS=127, LEVL=40)),
               ('TAPE HEAT 96 BIAS 64', P(SHAP=1, HEAT=96, BIAS=64))):
    L, R, m = A.render('wmq', mems, init, proc, p, x, blocks, guard=True)
    s0 = FSR
    l, r = L[s0:], R[s0:]
    print(f'{tag}: L re R {A.db(A.rms(l) / A.rms(r)):+.3f} dB; max |L - R| {max(abs(a - b) for a, b in zip(l, r))} LSB; '
          f'DC L {sum(l) / len(l) / A.FS * 1e6:+.1f} ppm; peak {A.peak(l + r):.3f}')
