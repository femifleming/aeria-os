"""COMP gate: tools/run.sh python3 /aeria/comp/test.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

FS, SR = A.FS, 44100.0
THR, RATI, BELW, ATK, REL, MIX, MAKE = range(7)


# ---- the plugin (Compander.h) in float, with this effect's knob laws --------
def laws(p):
    return dict(thr=-60.0 * p[THR] / 127, ratio=20.0 ** (p[RATI] / 127),
                bel=0.0 if p[BELW] == 64 else ((p[BELW] - 64) / 63 if p[BELW] > 64 else -(64 - p[BELW]) / 64),
                atk=0.05 * 4000.0 ** (p[ATK] / 127), rel=5.0 * 400.0 ** (p[REL] / 127),
                make=24.0 * p[MAKE] / 127)


def curve(x, k):
    """Compander::computeGain's gain computer: detector level x (dB) -> gain dB (no makeup)."""
    x = max(x, -120.0)
    thr, ratio, bel = k['thr'], k['ratio'], k['bel']
    if x > thr:
        return (thr + (x - thr) / ratio) - x
    if bel > 0:
        return max(-80.0, (x - thr) * 7.0 * bel)
    if bel < 0:
        ru = 1 + 9 * -bel
        fade = min(1.0, max(0.0, (x + 100) / 20))
        return min(30.0, (thr - x) * (1 - 1 / ru)) * fade
    return 0.0


def model_gain(pk, k):
    """Per-sample gain in dB (with makeup) of the float plugin for a peak sequence."""
    a = 1 - math.exp(-1 / (max(0.05, k['atk']) * 0.001 * SR))
    r = 1 - math.exp(-1 / (max(1.0, k['rel']) * 0.001 * SR))
    env, out = 0.0, []
    for v in pk:
        env += (a if v > env else r) * (v - env)
        out.append(curve(20 * math.log10(max(env, 1e-6)), k) + k['make'])
    return out


def params(d, **kw):
    p = list(d)
    for n, v in kw.items():
        p[names.index(n)] = v
    return p


def amp(db):
    return 10 ** (db / 20)


rep = A.Report('COMP')
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/comp/comp.asm', 'AERIA COMP', '/aeria/comp/manifest.py',
    {'THR': 64, 'RATI': 127, 'BELW': 0, 'ATK': 0, 'REL': 0, 'MAKE': 127, 'MIX': 64})
words = A.assemble('/aeria/comp/comp.asm', 'AERIA COMP')[1]
F0 = 1000.0
_n = [0]


def run(p, x, stereo=False):
    _n[0] += 1
    n = len(x) // (2 if stereo else 1)
    blocks = (n + 15) // 16
    return A.render(f'cp_{_n[0]}', mems, init, proc, p, x, blocks, stereo_in=stereo)[:2]


def sine_gain(p, level_db, secs=0.5):
    """Steady gain (dB) on a 1 kHz sine: rms out / rms in over the last 100 ms."""
    n = int(secs * SR) // 16 * 16
    x = A.sine(F0, amp(level_db), n)
    L, R = run(p, x)
    return A.db(A.rms(L[-4410:]) / A.rms(x[-4410:])), L, R, x


# ---- 1. static curve on steady sines (fast attack, slow release: env = peak) ----
for rv, tag in ((59, '4:1'), (127, '20:1')):
    p = params(d, THR=51, RATI=rv, BELW=64, ATK=0, REL=127, MIX=127, MAKE=0)
    k = laws(p)
    rows, worst = [], 0.0
    for lv in (-6, -12, -24, -36, -48):
        g, L, R, x = sine_gain(p, lv)
        want = curve(lv, k)
        worst = max(worst, abs(g - want))
        rows.append(f'{lv}: out {lv + g:+.2f} dBFS (ideal {lv + want:+.2f})')
    rep.check(f'static curve, 1 kHz sines, THR {k["thr"]:.2f} dB, ratio {k["ratio"]:.2f}:1 (RATI {rv}), '
              f'BELW off: within 1 dB of the ideal curve', worst < 1.0,
              '; '.join(rows) + f'; worst {worst:.3f} dB')

# ---- 2. below the threshold ----
p = params(d, THR=51, RATI=59, BELW=73, ATK=0, REL=127, MIX=127, MAKE=0)
k = laws(p)
g, *_ = sine_gain(p, -48)
want = curve(-48, k)
rep.check(f'BELW right (73: bel {k["bel"]:.3f}, 1:{1 + 7 * k["bel"]:.1f}): -48 dB sine, THR {k["thr"]:.2f} '
          f'-> lower by the expected amount within 2 dB', abs(g - want) < 2.0 and g < -3,
          f'gain {g:+.2f} dB, expected {want:+.2f} dB')
p = params(d, THR=0, RATI=0, BELW=127, ATK=0, REL=127, MIX=127, MAKE=0)
k = laws(p)
rows, ok = [], True
for lv in (-6, -20, -40):
    g, *_ = sine_gain(p, lv)
    want = curve(lv, k)
    ok &= abs(g - want) < 2.0
    rows.append(f'{lv} dB: gain {g:+.2f} (expected {want:+.2f})')
rep.check('BELW right full (1:8), THR 0: -6 dB -> -42 dB gain, -20 and -40 dB sit on the -80 dB floor, '
          'within 2 dB', ok, '; '.join(rows))
p = params(d, THR=51, RATI=59, BELW=0, ATK=0, REL=127, MIX=127, MAKE=0)
k = laws(p)
g, L, R, x = sine_gain(p, -48)
want = curve(-48, k)
rep.check(f'BELW left (0: 10:1 upward): -48 dB sine, THR {k["thr"]:.2f} -> higher by the expected amount '
          f'within 2 dB, not above the threshold, not above +30 dB',
          abs(g - want) < 2.0 and g > 3 and -48 + g <= k['thr'] and g <= 30.05
          and A.db(A.peak(L[-4410:])) <= k['thr'],
          f'gain {g:+.2f} dB, expected {want:+.2f} dB; out {-48 + g:+.2f} dBFS, peak '
          f'{A.db(A.peak(L[-4410:])):+.2f} dBFS')
p = params(d, THR=0, RATI=0, BELW=0, ATK=0, REL=127, MIX=127, MAKE=0)
k = laws(p)
rows, ok = [], True
for lv in (-3, -20, -40, -70, -90, -99):
    g, L, R, x = sine_gain(p, lv)
    want = curve(lv, k)
    ok &= abs(g - want) < 2.0 and g <= 30.05 and lv + g <= k['thr'] + 0.05
    rows.append(f'{lv} dB: gain {g:+.2f} (expected {want:+.2f})')
rep.check('BELW left full, THR 0, levels -3 .. -99 dB: cap +30 dB, fade below -80 dB, never above the '
          'threshold, within 2 dB', ok, '; '.join(rows))

# ---- 3. silence stays silence (upward full, makeup full) ----
p = params(d, THR=0, RATI=0, BELW=0, ATK=0, REL=0, MIX=127, MAKE=127)
L, R = run(p, [0] * 16000)
rep.check('true silence in, BELW fully left, MAKE 127 -> exact silence out', not any(L) and not any(R),
          f'max |out| {max(abs(v) for v in L + R)} LSB')
# a signal, then silence: output must reach exact zero as soon as the input does
x = A.sine(F0, 0.25, 8000) + [0] * 16000
L, R = run(p, x)
rep.check('signal then silence, same settings: output exactly 0 from the first silent sample',
          not any(L[8000:]) and A.peak(L[:8000]) > 0.2, f'tail max {max(abs(v) for v in L[8000:])} LSB')

# ---- 4. gain-curve accuracy on DC staircases against the float curve ----
LEVELS = [-0.5, -1, -2, -3, -4.5, -6, -8, -10, -12, -15, -18, -21, -24, -27, -30, -33, -36, -40, -44, -48,
          -52, -56, -60, -66, -72, -78, -84, -90, -95, -99]
HOLD = 4416
worst_all, lines = 0.0, []
for tag, kw in (('THR 51 RATI 59 off', dict(THR=51, RATI=59, BELW=64)),
                ('THR 51 RATI 127 off', dict(THR=51, RATI=127, BELW=64)),
                ('THR 20 RATI 30 exp 80', dict(THR=20, RATI=30, BELW=80)),
                ('THR 0 exp 127', dict(THR=0, RATI=0, BELW=127)),
                ('THR 51 RATI 59 up 0', dict(THR=51, RATI=59, BELW=0)),
                ('THR 0 up 0 MAKE 0', dict(THR=0, RATI=0, BELW=0)),
                ('THR 30 up 40 MAKE 64', dict(THR=30, RATI=90, BELW=40, MAKE=64)),
                ('THR 127 RATI 127 MAKE 127', dict(THR=127, RATI=127, BELW=64, MAKE=127))):
    base = dict(ATK=0, REL=0, MIX=127, MAKE=0)
    base.update(kw)
    p = params(d, **base)
    k = laws(p)
    x = []
    for lv in LEVELS:
        x += [round(amp(lv) * FS)] * HOLD
    L, R = run(p, x)
    worst, at, used = 0.0, None, 0
    for i, lv in enumerate(LEVELS):
        xin = x[i * HOLD]
        out = L[(i + 1) * HOLD - 1]
        want = curve(A.db(xin / FS), k) + k['make']
        ideal = xin * amp(want)
        if ideal < 2000 or ideal > 0.98 * FS:     # 1 LSB of truncation < 0.005 dB; not clipped
            continue
        used += 1
        e = A.db(out / FS) - A.db(ideal / FS)
        if abs(e) > abs(worst):
            worst, at = e, lv
    worst_all = max(worst_all, abs(worst))
    lines.append(f'{tag}: {worst:+.4f} dB at {at} dB ({used} levels)')
rep.check('gain-curve accuracy: DC staircase -0.5 .. -99 dBFS, 8 settings, steady gain against the float '
          'plugin curve (outputs between 2000 LSB and 0.98 FS)', worst_all < 0.05,
          f'worst {worst_all:.4f} dB; ' + '; '.join(lines))


# ---- 5. attack / release on a DC step -40 -> -6 -> -40 dBFS ----
def step_times(p, t_on=0.4, t_off=2.6):
    k = laws(p)
    lo, hi = round(amp(-40) * FS), round(amp(-6) * FS)
    n0, n1, n2 = 2208, int(t_on * SR) // 16 * 16, int(t_off * SR) // 16 * 16
    x = [lo] * n0 + [hi] * n1 + [lo] * n2
    L, R = run(p, x)
    g = [A.db(max(o, 1) / i) for o, i in zip(L, x)]
    m = model_gain([v / FS for v in x], k)

    def times(gg):
        full = gg[n0 + n1 - 1]                       # settled reduction (negative dB)
        ta = next(i for i in range(n0, n0 + n1) if gg[i] <= 0.9 * full) - n0
        tr = next((i for i in range(n0 + n1, len(gg)) if gg[i] >= 0.1 * full), len(gg)) - (n0 + n1)
        return full, ta, tr
    return k, times(g), times(m)


rows, ok = [], True
for av in (64, 100):
    p = params(d, THR=51, RATI=127, BELW=64, ATK=av, REL=40, MIX=127, MAKE=0)
    k, (fd, ta, _), (fm, tam, _) = step_times(p, t_on=1.0, t_off=0.5)
    ok &= abs(ta - tam) <= 0.3 * tam and abs(fd - fm) < 0.2
    rows.append(f'ATK {av} ({k["atk"]:.2f} ms): 90% of {fd:.2f} dB in {ta / SR * 1000:.2f} ms '
                f'(float plugin {tam / SR * 1000:.2f} ms)')
rep.check('attack: DC step -40 -> -6 dBFS, time to 90% of the gain reduction (dB) against the float plugin, '
          'within 30%', ok, '; '.join(rows))
rows, ok = [], True
for rv in (40, 90):
    p = params(d, THR=51, RATI=127, BELW=64, ATK=30, REL=rv, MIX=127, MAKE=0)
    k, (fd, _, tr), (fm, _, trm) = step_times(p, t_on=0.2, t_off=2.5)
    ok &= abs(tr - trm) <= 0.3 * trm
    rows.append(f'REL {rv} ({k["rel"]:.1f} ms): back to 10% of {fd:.2f} dB in {tr / SR * 1000:.1f} ms '
                f'(float plugin {trm / SR * 1000:.1f} ms)')
rep.check('release: DC step -6 -> -40 dBFS, time until 10% of the gain reduction is left against the float '
          'plugin, within 30%', ok, '; '.join(rows))
rows, ok = [], True
for av, rv in ((0, 0), (127, 127)):
    p = params(d, THR=51, RATI=127, BELW=64, ATK=av, REL=rv, MIX=127, MAKE=0)
    k, (fd, ta, tr), (fm, tam, trm) = step_times(p, t_on=3.0 if av else 0.2, t_off=12.0 if rv else 0.3)
    ok &= abs(ta - tam) <= max(1, 0.3 * tam) and abs(tr - trm) <= 0.3 * trm
    rows.append(f'ATK {av} / REL {rv}: attack {ta / SR * 1000:.3f} ms (plugin {tam / SR * 1000:.3f}), '
                f'release {tr / SR * 1000:.1f} ms (plugin {trm / SR * 1000:.1f})')
rep.check('attack/release at the knob ends (0.05 ms / 5 ms and 200 ms / 2 s): within 30% (or 1 sample) of the '
          'float plugin', ok, '; '.join(rows))

# ---- 6. makeup, saturation ----
rows, ok = [], True
for mv in (32, 64, 127):
    p = params(d, THR=0, RATI=0, BELW=64, ATK=40, REL=60, MIX=127, MAKE=mv)
    g, L, R, x = sine_gain(p, -30)
    want = 24 * mv / 127
    ok &= abs(g - want) < 1.0 and L == R
    rows.append(f'MAKE {mv}: {g:+.3f} dB (expected {want:+.3f})')
rep.check('MAKE on a -30 dB sine (THR 0, 1:1): expected gain within 1 dB', ok, '; '.join(rows))
p = params(d, THR=0, RATI=0, BELW=64, ATK=40, REL=60, MIX=127, MAKE=127)
x = A.sine(F0, 1.0, 16000)
L, R = run(p, x)
flips = sum(1 for o, i in zip(L, x) if (o > 0) != (i > 0) and abs(i) > 4)
S0 = 4000                                   # past the MIX smoother's ramp-in from init
clipped = sum(1 for o in L[S0:] if abs(o) >= FS)
should = sum(1 for i in x[S0:] if abs(i) * amp(24) >= FS)
exp_ok = all(abs(o) >= FS or abs(i) * amp(24) < FS * 1.001 for o, i in zip(L[S0:], x[S0:]))
rep.check('full-scale sine, MAKE 127 (+24 dB): saturates at the rails, no wrap (no sign flip against the '
          'input; every sample that should clip sits at full scale)',
          flips == 0 and exp_ok and abs(clipped - should) <= 8 and max(L) == FS and min(L) == -FS - 1,
          f'{flips} sign flips, {clipped} samples at the rails ({should} expected), max {max(L)}, min {min(L)}')
# worst case for the 56-bit path: gain parked at +30 +24 dB, slow attack, then full scale
p = params(d, THR=0, RATI=0, BELW=0, ATK=127, REL=127, MIX=127, MAKE=127)
NP = 88208
x = A.sine(F0, amp(-60), NP) + A.sine(F0, 1.0, 8000, ) + [FS, -FS - 1] * 200
L, R = run(p, x)
flips = sum(1 for o, i in zip(L[NP:], x[NP:]) if (o > 0) != (i > 0) and abs(i) > 64)
gq = A.db(A.rms(L[NP - 4410:NP]) / A.rms(x[NP - 4410:NP]))
g0 = max(abs(o) for o in L[NP:NP + 40])
rep.check('-60 dB sine lifted +30 dB (upward) +24 dB (makeup), then a full-scale burst and +-FS alternation '
          'with a 200 ms attack (gain near +54 dB at the onset): no sign flip, output at the rails',
          flips == 0 and max(L[NP:]) >= FS - 1 and min(L[NP:]) == -FS - 1 and abs(gq - 54) < 0.5
          and g0 >= FS - 1,
          f'{flips} sign flips; gain before the burst {gq:+.2f} dB; burst max {max(L[NP:])}, min {min(L[NP:])}')

# ---- 7. stereo link ----
p = params(d, THR=51, RATI=59, BELW=64, ATK=0, REL=127, MIX=127, MAKE=0)
k = laws(p)
n = 16000
xl, xr = A.sine(F0, amp(-6), n), A.sine(F0 * 1.37, amp(-40), n)
xs = [v for pair in zip(xl, xr) for v in pair]
L, R = run(p, xs, stereo=True)
gl = A.db(A.rms(L[-4410:]) / A.rms(xl[-4410:]))
gr = A.db(A.rms(R[-4410:]) / A.rms(xr[-4410:]))
want = curve(-6, k)
rep.check('stereo link: loud left (-6 dB), quiet right (-40 dB, alone it would get 0 dB) -> the same gain',
          abs(gl - gr) < 0.1 and abs(gl - want) < 1.0, f'L {gl:+.3f} dB, R {gr:+.3f} dB, curve {want:+.3f} dB')
xs = [v for pair in zip(xr, xl) for v in pair]
L, R = run(p, xs, stereo=True)
gl2 = A.db(A.rms(L[-4410:]) / A.rms(xr[-4410:]))
gr2 = A.db(A.rms(R[-4410:]) / A.rms(xl[-4410:]))
rep.check('stereo link, channels swapped: same gain again', abs(gl2 - gr2) < 0.1 and abs(gr2 - gl) < 0.05,
          f'L {gl2:+.3f} dB, R {gr2:+.3f} dB')

# ---- 8. unity and MIX ----
p = params(d, THR=0, RATI=0, BELW=64, MIX=127, MAKE=0)
x = A.noise(8000, 0.9, 5)
L, R = run(p, x)
e = max(abs(a - b) for a, b in zip(L, x))
rep.check('THR 0, 1:1, BELW off, MAKE 0, MIX 127: unity within 2 LSB', e <= 2 and L == R, f'max |err| {e} LSB')
p = params(d, THR=51, RATI=127, BELW=64, ATK=0, REL=127, MIX=64, MAKE=0)
k = laws(p)
g, L, R, x = sine_gain(p, -6)
want = A.db(0.5 + 0.5 * amp(curve(-6, k)))
rep.check('MIX 64: half dry + half wet on a -6 dB sine', abs(g - want) < 0.3, f'{g:+.3f} dB (expected {want:+.3f})')

rep.note('size', f'{len(words)} words of P; r7 state 23 words (00..16) + 24-word table (20..37)')


# ---- 9. knob jumps (quality pass, 12 Nov 2026): THR, RATI, BELW, MAKE glide (tau 2.9 ms), no step in the gain
def click_metric(y, at, span=700):
    # worst |2nd difference| / local peak, normalised so a clean 1 kHz sine reads 1.0
    k = (2 * math.pi * 1000 / SR) ** 2
    w = 0.0
    for i in range(at, at + span):
        pk = max(abs(v) for v in y[i - 22:i + 24]) or 1
        w = max(w, abs(y[i + 1] - 2 * y[i] + y[i - 1]) / pk / k)
    return w


for kn, lvl in (('THR', 0.01), ('RATI', 0.25), ('BELW', 0.01), ('MAKE', 0.01)):
    x = A.sine(F0, lvl, 700 * 16)
    ev = [(200 + 150 * i, params(d, **{kn: v})) for i, v in enumerate((0, 127, 0, 127))]
    _n[0] += 1
    L, R = A.render(f'cp_{_n[0]}', mems, init, proc, params(d, **{kn: 127}), x, 700, events=ev)[:2]
    pre = max(click_metric(L, (200 + 150 * i) * 16 - 700) for i in range(4))
    post = max(click_metric(L, (200 + 150 * i) * 16) for i in range(4))
    rep.check(f'knob jump {kn} 0<->127 on a {A.db(lvl):.0f} dB 1 kHz sine: no click (2nd-difference/peak after '
              f'the jump within 1.2x of before)', post <= 1.2 * pre + 0.05, f'before {pre:.2f}, after {post:.2f} '
              f'(was up to 46 before the glide)')
# ---- stereo (STEREO.md): one detector (max |L|, |R|), one gain: equal inputs give equal outputs
def wrun(xx, bl):
    return A.render('cp_w', mems, init, proc, params(d, THR=64, RATI=127, MAKE=40, MIX=127), xx, bl)[:2]


A.stereo_report(rep, 'THR -30 dB, 20:1, MAKE +7.6 dB, MIX 127', wrun, False, secs=2.0)
rep.note('cost', f'static {cost[0]} cycles/sample; dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f} '
                 f'instr/sample')
rep.save('/aeria/comp/test_results.md')
