"""WIND gate: tools/run.sh python3 /aeria/wind/test.py   (writes wind/test_results.md)"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/wind')
import aeria as A
import gen as G
import wxtest as T

FSR = 44100
G.build()
rep = A.Report('WIND')
DEAR = {'AMT': 127, 'DPTH': 127, 'GUST': 127, 'LEAN': 127}
mod, names, d, mems, init, proc, cost = T.standard(
    rep, '/aeria/wind/wind.asm', 'AERIA WIND', '/aeria/wind/manifest.py', DEAR, cost_limit=120)
ON = T.P(names, d, AMT=127)


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('wn_' + tag, mems, init, proc, p, x, blocks, guard=True, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def blockcost(tag):
    rows = [[int(v) for v in l.split()] for l in (A.BUILD / f'wn_{tag}.meter').read_text().splitlines()]
    nul = A.null_cost()['max_per_frame']
    return [r[1] / 16 - nul for r in rows]


# ---- 1. the plugin's settings on a steady sine, 60 s: drift, swell, lean ----------------------------
secs = 60
blocks = secs * FSR // 16
n = blocks * 16
f0 = 1000.0
x = A.sine(f0, 0.5, n)
L, R, m = run('sine', ON, x, blocks)
cl, al = T.cents_track(L, f0)
cr, ar = T.cents_track(R, f0)
s = 2 * FSR // 256                    # after the fade in
cl, cr, al, ar = cl[s:], cr[s:], al[s:-1], ar[s:-1]
lean = [20 * math.log10(a / b) for a, b in zip(al, ar)]
both = [math.sqrt((a * a + b * b) / 2) / 0.5 for a, b in zip(al, ar)]
same = max(abs(a - b) for a, b in zip(cl, cr))
lim = G.depth_cents(64)
rep.check(f'AMT 127, plugin settings, 1 kHz sine, {secs} s: pitch drifts both ways, never past the full-gust limit '
          f'({lim:.1f} cents + 1), the same on both sides', min(cl) < -8 and max(cl) > 8 and max(abs(v) for v in cl) < lim + 1
          and same < 0.5,
          f'{min(cl):+.1f} .. {max(cl):+.1f} cents (rms {math.sqrt(sum(v * v for v in cl) / len(cl)):.1f}); '
          f'L and R within {same:.2f} cents of each other')
gl0, gr0 = G.lean_gains(64, 1.0)
rep.check('the same render: level swells with the gusts (plugin: gain 0.96 .. 1.175, x the lean) and leans both ways, '
          'inside the plugin\'s -35 % / +12 %',
          0.85 < min(both) and max(both) < 1.2 and min(lean) < -1 and max(lean) > 1
          and min(lean) > 20 * math.log10(gl0 / gr0) - 0.1 and max(lean) < 20 * math.log10(gr0 / gl0) + 0.1,
          f'rms of the two sides {min(both):.3f} .. {max(both):.3f} x the input; L re R {min(lean):+.2f} .. {max(lean):+.2f} dB '
          f'(the plugin\'s extremes {20 * math.log10(gl0 / gr0):+.2f} / {20 * math.log10(gr0 / gl0):+.2f} dB)')

# ---- 2. DPTH and GUST ---------------------------------------------------------------------------------
secs = 20
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(f0, 0.5, n)
rows = []
res = {}
for dp in (0, 64, 127):
    L, R, _ = run(f'dp{dp}', T.P(names, d, AMT=127, DPTH=dp, GUST=110), x, blocks)
    c, _a = T.cents_track(L, f0)
    c = c[2 * FSR // 256:]
    res[dp] = max(abs(v) for v in c)
    rows.append(f'DPTH {dp}: |drift| up to {res[dp]:.2f} cents (limit {G.depth_cents(dp):.1f})')
rep.check('DPTH 0 / 64 / 127 (GUST 110, a gust every 0.48 s): no drift at 0; the largest drift follows the knob and '
          'stays under its limit', res[0] < 0.05 and 10 < res[64] < G.depth_cents(64) + 1
          and res[64] * 1.4 < res[127] < G.depth_cents(127) + 2, '; '.join(rows))
rows = []
res = {}
for gu in (0, 64, 127):
    L, R, _ = run(f'gu{gu}', T.P(names, d, AMT=127, GUST=gu, LEAN=0), x, blocks)
    _c, a = T.cents_track(L, f0)
    a = [v / 0.5 for v in a[2 * FSR // 256:-1]]
    res[gu] = sum(a) / len(a)
    rows.append(f'GUST {gu} (one every {G.gust_seconds(gu):.2f} s): mean level x {res[gu]:.3f}, {min(a):.3f} .. {max(a):.3f}')
rep.check('GUST 0 / 64 / 127, LEAN 0: more gusts, more swell (calm = x 0.9625, a full gust = x 1.175)',
          res[0] < res[64] < res[127] and res[0] < 0.99 and res[127] > 1.1, '; '.join(rows))

# ---- 3. the read is Hermite: a high tone's level does not flutter as the delay's fraction sweeps -------
secs = 12
blocks = secs * FSR // 16
n = blocks * 16
PH = T.P(names, d, AMT=127, DPTH=127, GUST=110, LEAN=0)
Ll, _, _ = run('hlo', PH, A.sine(500.0, 0.25, n), blocks)       # the control tick does not hear the input:
Lh, _, _ = run('hhi', PH, A.sine(9000.0, 0.25, n), blocks)      # both renders drift and swell alike
win = 441
rat = []
for s0 in range(2 * FSR, n - win, win):
    rat.append(20 * math.log10(A.rms(Lh[s0:s0 + win]) / A.rms(Ll[s0:s0 + win])))
lin = 20 * math.log10(math.cos(math.pi * 9000 / FSR))
rep.check('9 kHz against 500 Hz, two renders of the same weather (DPTH 127), level every 10 ms: the high tone keeps '
          f'its level within 0.5 dB (a linear read would dip {lin:.1f} dB every time the fraction passes a half)',
          max(rat) - min(rat) < 0.5 and min(rat) > -0.5,
          f'9 kHz re 500 Hz: {min(rat):+.3f} .. {max(rat):+.3f} dB')

# ---- 4. AMT 127 -> 0: back to the input bit for bit, then rest; up again: nothing stale ------------------
secs = 6
blocks = secs * FSR // 16
n = blocks * 16
off = 2 * FSR // 16
on2 = 4 * FSR // 16
fq = 441.0
x = A.sine(fq, 0.5, 3 * FSR) + [0] * (n - 3 * FSR)
ev = [(0, ON), (off, T.P(names, d, AMT=0)), (on2, ON)]
L, R, _ = run('fade', ON, x, blocks, events=ev)
bc = blockcost('fade')
same = next((i for i in range(off * 16, n) if all(L[j] == x[j] and R[j] == x[j] for j in range(i, min(i + 4000, on2 * 16)))), None)
rest = next((b for b in range(off, on2) if bc[b] < 2), None)
tail = max(abs(v) for v in L[3 * FSR + 2000:] + R[3 * FSR + 2000:])
# no click on the way out: the second difference of each side stays a sine's
sl = 2 * math.pi * fq / FSR
d2 = 0.0
for ch in (L, R):
    seg = [v / A.FS for v in ch[off * 16 - 2000: off * 16 + 6000]]
    pkv = max(abs(v) for v in seg)
    d2 = max(d2, max(abs(a - 2 * b + c) for a, b, c in zip(seg[2:], seg[1:], seg)) / (sl * sl * pkv))
rep.check('AMT 127 -> 0 on a 441 Hz sine (the knob jumps; the effect glides it): no click, the output comes back to the '
          'input bit for bit, the routine goes to rest; silence in after that and AMT up again: exactly 0 out '
          '(the ring was cleared)',
          same is not None and rest is not None and tail == 0 and d2 < 1.3,
          f'bit-exact from {(same - off * 16) / FSR * 1000:.0f} ms after the knob, at rest (cost {bc[rest]:.2f} a sample) from '
          f'{(rest - off) * 16 / FSR * 1000:.0f} ms; largest second difference on the way {d2:.3f} x a sine\'s; '
          f'after the second start {tail} LSB')
L, R, _ = run('sil', ON, None, 3000)
rep.check('silence in, AMT 127: exactly 0 out (no feedback anywhere)', max(abs(v) for v in L + R) == 0)

# ---- 4b. quality pass (12 Nov): LEAN is glided inside, a jumped knob does not step the gains -------------
secs = 6
blocks = secs * FSR // 16
n = blocks * 16
fz = 200.0
ev = [(b, T.P(names, d, AMT=127, DPTH=0, GUST=127, LEAN=127 if k % 2 == 0 else 0))
      for k, b in enumerate(range(FSR // 16, blocks, int(0.2 * FSR / 16)))]
L, R, _ = run('leanj', T.P(names, d, AMT=127, DPTH=0, GUST=127, LEAN=0), A.sine(fz, 0.5, n), blocks, events=ev)
sd = 0.5 * A.FS * (2 * math.pi * fz / FSR) ** 2
lj = [max(abs(a - 2 * b + c) for a, b, c in zip(ch[FSR + 2:], ch[FSR + 1:], ch[FSR:])) / sd for ch in (L, R)]
rep.check('LEAN jumped 0 <-> 127 every 0.2 s (AMT 127, GUST 127, DPTH 0, 200 Hz sine): the lean glides (1/16 a call, '
          '5.8 ms), no click -- largest second difference under 4 x a clean sine\'s (stepped per call before the '
          'quality pass: 30.8 x)', max(lj) < 4, f'L {lj[0]:.2f} R {lj[1]:.2f} x a clean sine\'s')

# ---- 5. a minute of noise and silence ---------------------------------------------------------------------
T.minute(rep, 'wn', mems, init, proc, T.P(names, d, **DEAR), quiet_db=-200,
         note='dearest settings: swell x 1.175 and lean x 1.24 on 0.9 FS clip at the output, as the plugin would')
T.minute(rep, 'wnp', mems, init, proc, ON, amp=1.0, quiet_db=-200, note='plugin settings, full-scale noise')

rep.note('size', f'{len(A.assemble("/aeria/wind/wind.asm", "AERIA WIND")[1])} words of P; r7 state {len(G.ST.at)} words; '
                 f'Y ring {G.RING} words (base a multiple of {G.RING})')
rep.note('cost', f'dsp_host mean {cost[0]:.1f}, worst block {cost[1]:.1f} instructions a sample at the dearest settings; '
                 'at rest (AMT 0) 7 instructions a call')
rep.save('/aeria/wind/test_results.md')
