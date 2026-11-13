"""SMEAR gate: tools/run.sh python3 /aeria/smear/test.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

rep = A.Report('SMEAR')
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/smear/smear.asm', 'AERIA SMEAR', '/aeria/smear/manifest.py',
    {'SMER': 127, 'DIFF': 127, 'MOD': 127, 'FRZ': 0, 'MIX': 127})
fs = 44100
blocks = int(4 * fs / 16)
n = blocks * 16
st = 300 * 16
lv = []
for sm in (0, 64, 127):
    p = list(d); p[0] = sm; p[3] = 0; p[4] = 0; p[5] = 127
    L, R, _ = A.render(f'sm_dec_{sm}', mems, init, proc, p, A.impulse(n, at=st, amp=0.9), blocks)
    lv.append(A.rms(L[st + fs: st + fs + 4410]))
rep.check('SMER lengthens the smear (rms 1 s after an impulse, SMER 0/64/127)', lv[0] < lv[1] < lv[2],
          ' / '.join(f'{A.db(v):.1f}' for v in lv) + ' dBFS')
# stereo decorrelation
p = list(d); p[4] = 0; p[5] = 127
L, R, _ = A.render('sm_noise', mems, init, proc, p, A.noise(n, 0.5), blocks)
a, b = L[-20000:], R[-20000:]
c = sum(x * y for x, y in zip(a, b)) / math.sqrt(sum(x * x for x in a) * sum(y * y for y in b))
rep.check('wide: L/R correlation of the wet on noise < 0.5', abs(c) < 0.5 and A.peak(L + R) <= 1.0,
          f'corr {c:+.3f}, peak {A.peak(L+R):.3f}')
# freeze: 1 s of noise, HOLD at 1 s, input stops at 1.2 s; then THAW at 8 s
blocks = int(11 * fs / 16)
n = blocks * 16
x = A.noise(int(1.2 * fs), 0.5) + [0] * (n - int(1.2 * fs))
base = list(d); base[0] = 40; base[3] = 40; base[5] = 127
hold = list(base); hold[4] = 1
ev = [(0, base), (int(1.0 * fs / 16), hold), (int(8 * fs / 16), base)]
L, R, _ = A.render('sm_freeze', mems, init, proc, base, x, blocks, events=ev)
r = lambda t: A.rms(L[int(t * fs): int(t * fs) + 4410] + R[int(t * fs): int(t * fs) + 4410])
r15, r4, r7, r10 = r(1.5), r(4), r(7.5), r(10.4)
rep.check('HOLD sustains: rms at 4 s and 7.5 s within 3 dB of 1.5 s (input stopped at 1.2 s)',
          abs(A.db(r4) - A.db(r15)) < 3 and abs(A.db(r7) - A.db(r15)) < 3 and A.peak(L + R) <= 1.0,
          f'{A.db(r15):.1f} / {A.db(r4):.1f} / {A.db(r7):.1f} dBFS, peak {A.peak(L+R):.3f}')
rep.check('THAW at 8 s: decays again (SMER 40: RT60 ~1.2 s) -> 2.4 s later at least 30 dB down',
          A.db(r10) < A.db(r7) - 30, f'{A.db(r10):.1f} dBFS')
# freeze ignores new input: hold an empty tank, play a tone -> wet stays silent
x = A.sine(1000.0, 0.5, n)
L, R, _ = A.render('sm_freeze_in', mems, init, proc, hold, x, int(2 * fs / 16))
rep.check('HOLD from the start: new input does not enter the tank', A.peak(L[-20000:] + R[-20000:]) < 1e-3,
          f'wet peak {A.peak(L[-20000:] + R[-20000:]):.2e}')
# ---- stereo (STEREO.md): unchanged by the width pass, measured with the same check as the others
def wrun(xx, bl):
    q = list(d); q[5] = 127
    return A.render('sm_w', mems, init, proc, q, xx, bl)[:2]


A.stereo_report(rep, 'defaults, MIX 127', wrun, True, secs=4.0, info=True)
rep.note('cost', f'static {cost[0]} cycles/sample; dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f}')
rep.save('/aeria/smear/test_results.md')
