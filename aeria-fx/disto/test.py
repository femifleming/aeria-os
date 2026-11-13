"""DIST gate: tools/run.sh python3 /aeria/disto/test.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

rep = A.Report('DIST')
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/disto/disto.asm', 'AERIA DIST', '/aeria/disto/manifest.py',
    {'MODE': 0, 'DRIV': 127, 'TONE': 64, 'MIX': 64})
blocks = 600
n = blocks * 16
f0 = 441.0
for mode, mname in enumerate(('DIST', 'CLIP', 'DGRD')):
    rows = []
    for drv in (0, 64, 127):
        p = list(d); p[6] = mode; p[0] = drv; p[1] = 127; p[2] = 127; p[4] = 64; p[5] = 127
        x = A.sine(f0, 0.5, n)
        L, R, _ = A.render(f'ds_{mode}_{drv}', mems, init, proc, p, x, blocks)
        seg = L[-8800:]
        h1 = A.goertzel(seg, f0)
        hs = math.sqrt(sum(A.goertzel(seg, f0 * k) ** 2 for k in range(2, 12)))
        rows.append((drv, h1, hs, A.peak(seg), A.dc(seg), L == R))
    detail = '; '.join(f'DRIV {r[0]}: fund {r[1]:.3f} harm {r[2]:.2e} pk {r[3]:.3f} dc {r[4]:+.5f}' for r in rows)
    ok = all(abs(r[4]) < 1e-3 and r[3] <= 1.0 and r[5] for r in rows)
    if mode == 0:
        ok &= rows[0][2] < rows[1][2] < rows[2][2]       # more drive, more harmonics
        g = 1 / 0.4 * 0.5
        ok &= abs(rows[0][1] - 0) < 1                     # sanity
    if mode == 1:
        ok &= rows[0][2] < rows[2][2]
    if mode == 2:
        ok &= rows[0][2] < 1e-5 and rows[0][2] < rows[1][2] < rows[2][2] and rows[2][2] > 1e-2
    rep.check(f'{mname}: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R', ok, detail)
# DIST transfer: one value check against s = G|x|/(G|x|+1)
p = list(d); p[6] = 0; p[0] = 0; p[1] = 127; p[4] = 64; p[5] = 127
x = [round(0.4 * A.FS)] * n
L, R, _ = A.render('ds_dc', mems, init, proc, p, x, blocks)
want = 0.4 / 0.4 / (1 + 0.4 / 0.4)
rep.check('DIST transfer: DRIV 0, x = 0.4 FS -> G|x|/(G|x|+1) = 0.5', abs(L[-1] / A.FS - want) < 2e-3,
          f'out {L[-1] / A.FS:.5f} vs {want:.5f}')
# DGRD rate: RATE 0 holds for ~100 samples
p = list(d); p[6] = 2; p[0] = 0; p[1] = 127; p[2] = 0; p[4] = 64; p[5] = 127
x = A.sine(97.0, 0.8, n)
L, R, _ = A.render('ds_rate', mems, init, proc, p, x, blocks)
seg = L[-4410:]
runs = sum(1 for a, b in zip(seg, seg[1:]) if abs(a - b) > 64)
rep.check('DGRD RATE 0: sample-and-hold at ~441 Hz (changes ~once per 100 samples)', 30 <= runs <= 60,
          f'{runs} level changes in 4410 samples')
p[2] = 127
L, R, _ = A.render('ds_rate127', mems, init, proc, p, x, blocks)
rep.check('DGRD DRIV 0 RATE 127 TONE 127: 24-bit, full rate -> output equals input',
          max(abs(a - b) for a, b in zip(L[-4410:], x[-4410:])) <= 2,
          f'max |err| {max(abs(a - b) for a, b in zip(L[-4410:], x[-4410:]))} LSB')
# stereo independence: L sine, R silence
p = list(d); p[6] = 0; p[0] = 127; p[5] = 127
xs = []
for v in A.sine(f0, 0.5, n):
    xs += [v, 0]
L, R, _ = A.render('ds_stereo', mems, init, proc, p, xs, blocks, stereo_in=True)
rep.check('stereo: L-only input leaves R silent', max(abs(v) for v in R) == 0 and A.peak(L) > 0.1,
          f'R peak {max(abs(v) for v in R)} LSB, L peak {A.peak(L):.3f}')
# ---- stereo (STEREO.md): no memory shared between the sides and none of it differs: equal inputs give equal outputs
for mode, mname in enumerate(('DIST', 'CLIP', 'DGRD')):
    def run(xx, bl, mode=mode):
        q = list(d); q[6] = mode; q[0] = 100; q[2] = 90; q[5] = 127
        return A.render(f'ds_w{mode}', mems, init, proc, q, xx, bl)[:2]
    A.stereo_report(rep, f'{mname} DRIV 100 RATE 90, MIX 127', run, False, secs=2.0)
rep.note('cost', f'static {cost[0]} cycles/sample (worst mode); dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f}')
rep.save('/aeria/disto/test_results.md')
