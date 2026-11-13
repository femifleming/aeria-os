"""ECHO2 quality probe (read-only): stereo balance, mono sum, tail. tools/run.sh python3 /aeria/echo2/quality.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

FSR = 44100
mems, words, init, proc = A.assemble('/aeria/echo2/echo2_test.asm', 'AERIA ECHO2')
NAMES = ['TIME', 'FDBK', 'TONE', 'SKEW', 'SPRD', 'MIX', 'HPF', 'FRZ', 'RFRZ', 'TMOD', 'GLID', '']
D = [115, 49, 127, 84, 127, 64, 0, 0, 0, 0, 64, 0]


def P(**kw):
    p = list(D)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


blocks = 8 * FSR // 16
n = blocks * 16
x = A.noise(n, 0.3, 4)
for tag, p in (('defaults', P()), ('SPRD 0', P(SPRD=0)), ('SPRD 64', P(SPRD=64)), ('SKEW 64 SPRD 127', P(SKEW=64)),
               ('MIX 127', P(MIX=127)), ('MIX 127 FDBK 110', P(MIX=127, FDBK=110))):
    L, R, m = A.render('e2q', mems, init, proc, p, x, blocks, guard=True)
    s0 = 2 * FSR
    l, r = L[s0:], R[s0:]
    wl = [a - b for a, b in zip(l, x[s0:])]
    wr = [a - b for a, b in zip(r, x[s0:])]
    print(f'{tag}: L re R {A.db(A.rms(l) / A.rms(r)):+.2f} dB; wet L re R {A.db(A.rms(wl) / max(A.rms(wr), 1e-9)):+.2f} dB; '
          f'corr {A.corr(l, r):+.3f}; mono sum {A.mono_db(l, r):+.2f} dB re L (+6.02 = identical, +3 = uncorrelated); '
          f'DC L {sum(l) / len(l) / A.FS * 1e6:+.1f} ppm')
