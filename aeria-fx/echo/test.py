"""ECHO gate: tools/run.sh python3 /aeria/echo/test.py

Builds under test (all generated here from gen.py, so none can be stale):
  echo_test.asm   Y ring 15,870 (m = $3dfd, the window aeria.py's -guard polices), X ring 16,384 at
                  X:$10000 -- every A.standard gate and every effect check runs on this one
  echo_fullx.asm  Y ring 16,384, X ring 16,384 at X:$10000: the full build with ONLY the XRING
                  constant moved -- the tap/ring checks are repeated on it, guard window $4000
  echo.asm        the full build (XRING = $4000): assembled, compared word for word with
                  echo_fullx.asm, never rendered (X:$4000.. holds the harness's own state)
"""
import cmath
import math
import struct
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/echo')
import aeria as A
import gen as G

FSR = 44100
for name, kw in G.BUILDS.items():
    G.build(out=name, **kw)
rep = A.Report('ECHO')
DEAR = {'TIME': 127, 'FDBK': 127, 'TONE': 127, 'RATO': 2, 'WDTH': 64, 'MIX': 127}
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/echo/echo_test.asm', 'AERIA ECHO', '/aeria/echo/manifest.py', DEAR)
TB = dict(mems=mems, init=init, proc=proc, dmax=G.dmax(15870, 16384), guard=True, tag='t')
fmems, fwords, finit, fproc = A.assemble('/aeria/echo/echo_fullx.asm', 'AERIA ECHOFX')
FB_ = dict(mems=fmems, init=finit, proc=fproc, dmax=G.dmax(16384, 16384), guard=0x4000, tag='f')
ST = 300 * 16          # first sample used by a test: the 128 clearing calls are long over


# ---- the formulas the knobs are specified by ------------------------------
def t_knob(v):
    return G.T_MAX * FSR * (G.T_MAX / G.T_MIN) ** (-(127 - v) / 127)


def taps(v, rset, dmax):
    r = G.SETS[rset][1]
    t = min(t_knob(v), dmax / max(r))
    return [t * x for x in r]


def fb_knob(v):
    return G.FB_MAX * v / 127


def tone_hz(v):
    return G.F_MAX * (G.F_MAX / G.F_MIN) ** (-(127 - v) / 127)


def coef(hz):
    return 1 - math.exp(-2 * math.pi * hz / FSR)


def P(time=80, fdbk=0, tone=127, rato=0, wdth=127, mix=127):
    return [time, fdbk, tone, rato, wdth, mix, 0, 0, 0, 0, 0, 0]


def run(tag, b, p, inp, blocks, **kw):
    kw.setdefault('guard', b['guard'])
    L, R, m = A.render(f"ec_{b['tag']}_{tag}", b['mems'], b['init'], b['proc'], p, inp, blocks, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, f'{tag}: guard {m["stray"]} stray'
    return L, R, m


def pk(x, at, w=40):
    lo = max(0, at - w)
    seg = x[lo: at + w + 1]
    i = max(range(len(seg)), key=lambda k: abs(seg[k]))
    return lo + i, seg[i] / A.FS


def energy(x, at, w):
    return sum(v * v for v in x[max(0, at - w): at + w + 1]) / A.FS ** 2


# ---- 0. the three builds are one source ------------------------------------
full = Path('/aeria/echo/echo.asm').read_text().splitlines()
fx = Path('/aeria/echo/echo_fullx.asm').read_text().splitlines()
tb = Path('/aeria/echo/echo_test.asm').read_text().splitlines()
code = lambda ls: [l for l in ls if not l.startswith(';')]
df = [(a, b) for a, b in zip(code(full), code(fx)) if a != b]
dt = [(a, b) for a, b in zip(code(full), code(tb)) if a != b]
fullmems, fullwords, _, _ = A.assemble('/aeria/echo/echo.asm', 'AERIA ECHOFULL')
dw = [i for i, (a, b) in enumerate(zip(fullwords, fwords)) if a != b]
rep.check('full build assembles (round-trip, mpysu audit); differs from echo_fullx only in the XRING literal',
          len(fullwords) == len(fwords) and len(df) == 2 and len(dw) == 2
          and all('$004000' in a and '$010000' in b for a, b in df),
          f'{len(fullwords)} words; {len(df)} source lines / {len(dw)} code words differ (init x ptr, clear base)')
rep.note('test build vs full build', f'{len(dt)} source lines differ: '
         + '; '.join(sorted({f"{a.split(';')[0].strip()} -> {b.split(';')[0].strip()}" for a, b in dt}))[:900])

# ---- 0b. the harness leaves X:$10000..$13fff alone --------------------------
probe = A.BUILD / 'ecxprobe.asm'
probe.write_text(f'''init:
        move    #>$ffffff,m4
        move    #>${G.TEST_XRING:06x},r4
        move    #>$5a5a5a,a
        move    #>$4000,x0
        do      x0,>ecpfill
        move    a,x:(r4)+
ecpfill:
        nop
        rts
proc:
        move    #>$ffffff,m4
        move    #>${G.TEST_XRING:06x},r4
        move    #>$5a5a5a,x0
        move    #>$4000,x1
        clr     b
        do      x1,>ecpscan
        move    x:(r4)+,a
        cmp     x0,a
        beq     ecpsame
        add     #>$1,b
ecpsame:
        nop
        nop
ecpscan:
        nop
        add     #>$1,b
        move    b1,x:(r0)
        rts
''')
pm, _, pi, pp = A.assemble(probe, 'AERIA ECXP')
Lp, Rp, _ = A.render('ec_xprobe', pm, pi, pp, [64] * 12, None, 200, split=7)
rep.check('harness X memory: a pattern at X:$10000..$13fff survives 200 dispatched blocks (split 7) unchanged',
          Lp[0] == 1 and set(Lp) == {0, 1},
          f'proc writes 1 + mismatching words into each call\'s first frame: values seen {sorted(set(Lp))}')

# ---- 1. impulse: six echoes at T x ratio ------------------------------------
for b in (TB, FB_):
    blocks = 800
    n = blocks * 16
    amp = 0.5
    L, R, _ = run('imp', b, P(time=80), A.impulse(n, at=ST, amp=amp), blocks)
    want = taps(80, 0, b['dmax'])
    rows, ok = [], True
    for k, w in enumerate(want):
        ch, other = (L, R) if k % 2 == 0 else (R, L)
        at, lv = pk(ch, ST + round(w))
        leak = max(abs(v) for v in other[ST + round(w) - 3: ST + round(w) + 4])
        rows.append((k, at - ST, w, lv, leak))
        ok &= abs(at - ST - w) <= 1.0 and leak == 0
    for side in (0, 1):
        l0, l1, l2 = (abs(rows[side + 2 * j][3]) for j in range(3))
        ok &= abs(A.db(l1 / l0) - A.db(0.8)) < 1 and abs(A.db(l2 / l0) - A.db(0.64)) < 1
    # absolute level: impulse amp x 0.62 x wet lowpass peak (its coefficient)
    lvl = amp * 0.62 * coef(min(20000, 1.5 * tone_hz(127)))
    ok &= abs(A.db(abs(rows[0][3]) / lvl)) < 1
    early = max(abs(v) for v in L[ST: ST + round(want[0]) - 2] + R[ST: ST + round(want[1]) - 2])
    ok &= early == 0
    rep.check(f"impulse, JUST, FDBK 0 [{'test build' if b is TB else 'full-size X build'}]: taps at T x ratio "
              '+-1 sample, levels 1 : 0.8 : 0.64 +-1 dB, L taps on L only, R on R only', ok,
              f'T {want[0]:.1f} smp; ' + ' '.join(f'{"LR"[k % 2]}{k // 2} {a} (want {w:.1f}) {A.db(abs(lv)):.2f} dB'
                                                  for k, a, w, lv, _ in rows)
              + f'; tap0 vs 0.62 x in x wet coef {A.db(abs(rows[0][3]) / lvl):+.2f} dB; before first tap {early} LSB')

# ---- 2. feedback: train period and decay ------------------------------------
b = TB
blocks = 1800
n = blocks * 16
fdbk = 64
L, R, _ = run('fb', b, P(time=80, fdbk=fdbk), A.impulse(n, at=ST, amp=0.1), blocks)
want = taps(80, 0, b['dmax'])


def model_energies(passes, at, aw, g, w=60):
    """Energy of pass n of an impulse: (tone lp -> dc blocker -> x g)^n, then the wet lowpass."""
    r = math.exp(-2 * math.pi * 10 / FSR)
    x = [0.0] * (2 * w + 1)
    x[w] = 1.0
    out = []
    for _ in range(passes):
        z = 0.0
        y = []
        for v in x:
            z += aw * (v - z)
            y.append(z)
        out.append(sum(v * v for v in y))
        z = x1 = y1 = 0.0
        nx = []
        for v in x:
            z += at * (v - z)
            yy = z - x1 + r * y1
            x1, y1 = z, yy
            nx.append(g * yy)
        x = nx
    return out


me = model_energies(4, coef(tone_hz(127)), coef(min(20000, 1.5 * tone_hz(127))), fb_knob(fdbk))
rows, ok = [], True
for side, ch in ((0, L), (1, R)):
    d0, dl = round(want[side]), want[side + 4]
    e, pos = [], []
    for k in range(4):
        at, _ = pk(ch, ST + d0 + round(k * dl), 8)
        pos.append(at)
        e.append(energy(ch, ST + d0 + round(k * dl), 60))
    for k in range(3):
        got, exp = 10 * math.log10(e[k + 1] / e[k]), 10 * math.log10(me[k + 1] / me[k])
        per = pos[k + 1] - pos[k]
        rows.append(f'{"LR"[side]} rep {k + 1}: period {per} (want {dl:.1f}), {got:.2f} dB (model {exp:.2f})')
        ok &= abs(per - dl) <= 1.0 and abs(got - exp) < 1.5
rep.check(f'FDBK {fdbk} (g = {fb_knob(fdbk):.3f}), TONE 127: the train repeats at the longest tap, each repeat '
          'down by g x filters (float model) +-1.5 dB', ok, '; '.join(rows))

# FDBK max, loud noise, 10 s: bounded, then decays
blocks = int(10 * FSR / 16)
n = blocks * 16
stop = int(6 * FSR)
x = A.noise(stop, 0.9, 5) + [0] * (n - stop)
L, R, m = run('fbmax', b, P(time=40, fdbk=127, wdth=127), x, blocks)
Ln, Rn, _ = run('fbmaxneg', b, P(time=40, fdbk=127, wdth=127), [-v for v in x], blocks)
sat = sum(1 for v in L + R if abs(v) >= A.FS - 1)
r_on = A.rms(L[stop - 22050: stop] + R[stop - 22050: stop])
r_off = A.rms(L[-22050:] + R[-22050:])
tl = taps(40, 0, b['dmax'])[5]
# a bias is what survives a polarity flip of the input; the rest of the mean is the noise's own
# low-frequency content (the comb peaks every 1 / 44 ms are 26 dB up at g = 0.95)
biasL = (A.dc(L[FSR:stop]) + A.dc(Ln[FSR:stop])) / 2
biasR = (A.dc(R[FSR:stop]) + A.dc(Rn[FSR:stop])) / 2
rep.check('FDBK 127 (g = 0.95), 0.9 FS noise for 6 s then silence, 10 s: wet bounded <= 1.0 FS, no DC bias '
          '(mean common to the input and its inverse), decays',
          A.peak(L + R) <= 1.0 and abs(biasL) < 2e-4 and abs(biasR) < 2e-4 and A.db(r_off) < A.db(r_on) - 30,
          f'peak {A.peak(L + R):.4f} FS, samples at the output limit {sat} of {2 * n} ({100 * sat / 2 / n:.3f} %), '
          f'bias L {biasL:+.2e} R {biasR:+.2e} FS (raw means L {A.dc(L[FSR:stop]):+.5f} / inverted {A.dc(Ln[FSR:stop]):+.5f}), '
          f'rms last 0.5 s of input {A.db(r_on):.1f} dBFS, last 0.5 s of the render {A.db(r_off):.1f} dBFS '
          f'(longest tap {tl / FSR * 1000:.1f} ms)')
x = A.noise(stop, 0.25, 6) + [0] * (n - stop)
L, R, m = run('fbmaxq', b, P(time=40, fdbk=127), x, blocks)
rep.check('FDBK 127, 0.25 FS noise: wet below the limiter (soft clipper, not the output limit, holds the loop)',
          A.peak(L + R) < 1.0, f'peak {A.peak(L + R):.3f} FS, rms {A.db(A.rms(L[FSR:stop])):.1f} dBFS')

# ---- 3. TONE darkens the repeats ---------------------------------------------
blocks = 1800
n = blocks * 16
res = {}
for tone in (30, 127):
    L, R, _ = run(f'tone{tone}', b, P(time=80, fdbk=110, tone=tone), A.impulse(n, at=ST, amp=0.5), blocks)
    at = ST + round(want[0] + 3 * want[4])
    seg = L[at - 300: at + 1200]
    hf = sum((p - q_) ** 2 for p, q_ in zip(seg[1:], seg)) / A.FS ** 2      # first difference: high band
    res[tone] = (hf, sum(v * v for v in seg) / A.FS ** 2)
rep.check('TONE 30 (789 Hz) vs 127 (18 kHz), FDBK 110: 3rd repeat much darker (first-difference energy)',
          10 * math.log10(res[30][0] / res[127][0]) < -30 and res[30][1] > 0,
          f'high band {10 * math.log10(res[30][0] / res[127][0]):.1f} dB, all bands '
          f'{10 * math.log10(res[30][1] / res[127][1]):.1f} dB (TONE 30 relative to 127)')


# ---- 4. seamless taps ----------------------------------------------------------
def inst_freq(x, f0, per, lo, hi, hop, span):
    """Mean frequency over `span` samples from the phase of a boxcar-demodulated
    x (boxcar = 8 whole periods, so the 2 f0 image cancels exactly)."""
    w = per * 8
    z = []
    for c in range(lo, hi, hop):
        acc = 0j
        for i in range(c - w // 2, c + w // 2):
            acc += x[i] * cmath.exp(-2j * math.pi * f0 * i / FSR)
        z.append(acc / w / A.FS * 2)
    k = span // hop
    worst, amin = 0.0, min(abs(v) for v in z)
    for i in range(len(z) - k):
        dphi = cmath.phase(z[i + k] / z[i]) if abs(z[i]) > 0 and abs(z[i + k]) > 0 else math.pi
        worst = max(worst, abs(dphi) / (2 * math.pi) * FSR / (k * hop))
    return worst, amin, max(abs(v) for v in z)


def clicks(x, lo, hi):
    seg = [v / A.FS for v in x[lo:hi]]
    d1 = max(abs(a - c) for a, c in zip(seg[1:], seg))
    d2 = max(abs(a - 2 * c + e) for a, c, e in zip(seg[2:], seg[1:], seg))
    return d1, d2, max(abs(v) for v in seg)


T0, T1 = 70, 90
old, new = taps(T0, 0, b['dmax']), taps(T1, 0, b['dmax'])


def clearance(f0):
    """How far the straight path old sum -> new sum stays from zero, per side (a
    crossfade between sums in antiphase must pass through a null: not a tap fault)."""
    worst = 9.0
    for side in (0, 1):
        po = sum(w * cmath.exp(-2j * math.pi * f0 * round(old[side + 2 * j]) / FSR) for j, w in enumerate(G.W))
        pn = sum(w * cmath.exp(-2j * math.pi * f0 * round(new[side + 2 * j]) / FSR) for j, w in enumerate(G.W))
        m = min(abs(po * (1 - t / 50) + pn * t / 50) for t in range(51))
        worst = min(worst, m / max(abs(po), abs(pn)))
    return worst


per = max(range(12, 19), key=lambda p_: clearance(FSR / p_))
f0 = FSR / per
blocks = int(1.6 * FSR / 16)
n = blocks * 16
chg = int(0.8 * FSR / 16)
ev = [(0, P(time=T0)), (chg, P(time=T1))]
L, R, _ = run('seam', b, P(time=T0), A.sine(f0, 0.4, n), blocks, events=ev)
lo, hi = chg * 16 - 2205, chg * 16 + 11025
rows, ok = [], True
slope = 2 * math.pi * f0 / FSR
for nm, ch in (('L', L), ('R', R)):
    dev, amin, amax = inst_freq(ch, f0, per, lo, hi, per * 4, 882)
    d1, d2, pkv = clicks(ch, lo, hi)
    ok &= dev < 0.01 * f0 and d1 <= 1.25 * slope * pkv and d2 <= 1.5 * slope * slope * pkv
    rows.append(f'{nm}: worst 20 ms mean deviation {dev:.2f} Hz ({100 * dev / f0:.3f} %), amplitude {amin:.3f}..{amax:.3f}, '
                f'max step {d1 / (slope * pkv):.3f} x and max 2nd difference {d2 / (slope * slope * pkv):.3f} x a sine\'s')
rep.check(f'TIME {T0} -> {T1} ({old[0]:.0f} -> {new[0]:.0f} samples) under a steady {f0:.0f} Hz sine, FDBK 0: frequency '
          'within 1 % from 50 ms before to 250 ms after, no click (a glide over the fade would be ~24 % flat)',
          ok, '; '.join(rows))
# the same on a ringing tail: a burst with 50 ms raised-cosine edges, then TIME moves 100 ms after it ends
burst = int(0.6 * FSR)
edge = int(0.05 * FSR)
x = A.sine(f0, 0.4, n)
for i in range(n):
    t = i - ST
    g = 0.0 if t < 0 or t >= burst else min(1.0, 0.5 - 0.5 * math.cos(math.pi * min(t, burst - t) / edge)
                                            if min(t, burst - t) < edge else 1.0)
    x[i] = round(x[i] * g)
chg = (ST + burst + int(0.1 * FSR)) // 16
rows, ok = [], True
for tag, ev in (('moved', [(0, P(time=T0, fdbk=115)), (chg, P(time=T1, fdbk=115))]), ('control', None)):
    L, R, _ = run('tail' + tag, b, P(time=T0, fdbk=115), x, blocks, events=ev)
    lo, hi = chg * 16 - 2205, chg * 16 + 11025
    for nm, ch in (('L', L), ('R', R)):
        dev, amin, amax = inst_freq(ch, f0, per, lo, hi, per * 4, 882)
        d1, d2, pkv = clicks(ch, lo, hi)
        if tag == 'moved':
            ok &= dev < 0.01 * f0 and d1 <= 1.25 * slope * pkv and d2 <= 1.5 * slope * slope * pkv
        rows.append(f'{tag} {nm}: {dev:.2f} Hz ({100 * dev / f0:.3f} %), amp {amin:.3f}..{amax:.3f}, step '
                    f'{d1 / (slope * pkv):.3f} x, 2nd diff {d2 / (slope * slope * pkv):.3f} x')
rep.check(f'TIME {T0} -> {T1} while a {f0:.0f} Hz tail rings (FDBK 115, input ended 100 ms earlier): frequency within '
          '1 %, no click (control = same render, TIME not moved)', ok, '; '.join(rows))
# fade law and length: UNIS, DC switched on; TIME 60 -> 127 while the old head already reads the DC
# and the new one (177 ms later) still reads silence: wet = (1 - sin^2(pi x / 2)) x level, x = t / 80 ms
blocks = int(1.2 * FSR / 16)
n = blocks * 16
dcv = 0.25
x = [0] * ST + [round(dcv * A.FS)] * (n - ST)
chg = (ST + round(taps(60, 5, b['dmax'])[0]) + 3000) // 16
L, R, _ = run('law', b, P(time=60, rato=5), x, blocks, events=[(0, P(time=60, rato=5)), (chg, P(time=127, rato=5))])
level = L[chg * 16 - 1] / A.FS
flen = G.FADE * FSR
err = max(abs(L[chg * 16 + i] / A.FS / level - math.cos(math.pi / 2 * min(1.0, (i + 1) / flen)) ** 2)
          for i in range(int(flen) + 400))
rep.check('fade law: old head on DC, new head on silence -> wet follows cos^2 over 80 ms (3528 samples) within 0.5 %',
          err < 5e-3 and abs(level / (dcv * sum(G.W)) - 1) < 0.01 and L[chg * 16 + int(flen) + 300] == 0,
          f'max error {err:.2e} of the level; level before {level:.4f} (0.62 x 2.44 x in = {dcv * sum(G.W):.4f})')
# gains sum to one: every head on the same DC through a fade -> wet does not move
x = [round(dcv * A.FS)] * n
chg = int(0.8 * FSR / 16)
L, R, _ = run('sum1', b, P(time=60), x, blocks, events=[(0, P(time=60)), (chg, P(time=100))])
seg = [v / A.FS for v in L[chg * 16 - 100: chg * 16 + 5000] + R[chg * 16 - 100: chg * 16 + 5000]]
rep.check('fade gains sum to 1: all heads on the same DC through a TIME move -> wet flat',
          max(seg) - min(seg) < 2e-6, f'spread {max(seg) - min(seg):.2e} FS at {seg[0]:.4f}')

# ---- 5. every RATO set at TIME 127 stays inside the ring ---------------------
for b in (TB, FB_):
    blocks = 1500
    n = blocks * 16
    rows, ok = [], True
    for rs, (nm, ratios) in enumerate(G.SETS):
        L, R, _ = run(f'ring{rs}', b, P(time=127, rato=rs), A.impulse(n, at=ST, amp=0.5), blocks)
        want = taps(127, rs, b['dmax'])
        firstL = next(i for i, v in enumerate(L[ST:]) if v) if any(L[ST:]) else -1
        firstR = next(i for i, v in enumerate(R[ST:]) if v) if any(R[ST:]) else -1
        good = abs(firstL - want[0]) <= 1 and abs(firstR - want[1]) <= 1
        for k, w in enumerate(want):
            ch = L if k % 2 == 0 else R
            mult = sum(wt for j, wt in enumerate(G.W) if round(want[k % 2 + 2 * j]) == round(w))
            at, lv = pk(ch, ST + round(w), 3)
            good &= abs(at - ST - w) <= 1 and abs(A.db(abs(lv) / (0.5 * mult * coef(20000)))) < 1
        ok &= good and max(want) <= b['dmax'] + 0.5
        rows.append(f'{nm}: T {want[0]:.0f}, longest {max(want):.0f}, first L {firstL} R {firstR}'
                    + ('' if good else ' BAD'))
    rep.check(f"every RATO set at TIME 127 [{'test build, ring 15,870: T held to 15,862 / largest ratio' if b is TB else 'full-size X build, rings 16,384'}]"
              ': nothing before the first tap, all six taps at T x ratio +-1 with their levels (no wrap)', ok, '; '.join(rows))

# ---- 6. DC, silence ------------------------------------------------------------
b = TB
blocks = int(3 * FSR / 16)
n = blocks * 16
L, R, _ = run('dc', b, P(time=40, fdbk=127), [round(0.25 * A.FS)] * n, blocks)
wantdc = 0.25 * sum(G.W)
rep.check('DC 0.25 FS in, FDBK 127: wet DC is the three direct taps only (0.62 x 2.44 x in), the loop adds none',
          abs(A.dc(L[-4410:]) / wantdc - 1) < 0.01 and abs(A.dc(R[-4410:]) / wantdc - 1) < 0.01,
          f'L {A.dc(L[-4410:]):.5f} R {A.dc(R[-4410:]):.5f} (want {wantdc:.5f})')
rows, ok = [], True
for tag, p, secs, src in (('FDBK 64 TIME 30', P(time=30, fdbk=64), 3, A.noise(8000, 0.9, 9)),
                          ('FDBK 127 TIME 0', P(time=0, fdbk=127), 9, A.noise(8000, 0.9, 9)),
                          ('FDBK 127 TIME 0 TONE 20 UNIS', P(time=0, fdbk=127, tone=20, rato=5), 9, A.impulse(8000, 100, 0.99)),
                          ('FDBK 100 TIME 50 WDTH 40 MIX 80', P(time=50, fdbk=100, wdth=40, mix=80), 12, A.noise(8000, 0.9, 4))):
    blocks = int(secs * FSR / 16)
    n = blocks * 16
    x = [0] * ST + src + [0] * (n - ST - len(src))
    L, R, _ = run('sil' + tag.replace(' ', ''), b, p, x, blocks)
    last = max((i for i, (u, v) in enumerate(zip(L, R)) if u or v), default=0)
    ok &= last < n - 4410
    rows.append(f'{tag}: last non-zero sample {(last - ST - len(src)) / FSR:.2f} s after the input ended ({secs} s render)')
rep.check('silence in -> exactly 0 out once the tail has decayed (no limit cycle, no stuck LSB)', ok, '; '.join(rows))


# garbage in the X ring (the standard dirty gate fills only Y and the r7 block)
def with_xfill(ms, tag):
    out = []
    for c, mm in zip('AB', ms):
        blob = Path(mm).read_bytes()
        body, term = blob[:-9], blob[-9:]
        w = [(0x123456 + 0x9e3779 * i) & 0xffffff for i in range(0x4000)]
        runx = struct.pack('<BII', 1, G.TEST_XRING, 0x4000) + struct.pack('<' + 'I' * 0x4000, *w)
        p_ = A.BUILD / f'{tag}_{c}.mem'
        p_.write_bytes(body + runx + term)
        out.append(p_)
    return out


xm = dict(TB, mems=with_xfill(A.with_fill(mems, 0xa5a5a5, 'ecxf0'), 'ecxf1'))
L, R, _ = run('xdirty', xm, P(time=127, fdbk=127, rato=2), None, 400, dirty=0x4321)
rep.check('dirty X ring (X:$10000..$13fff garbage) + dirty r7 block + dirty Y, silence in, FDBK 127, MIX 127: '
          'silent once cleared', max(abs(v) for v in L[128 * 16:] + R[128 * 16:]) == 0,
          f'worst after the 128 clearing calls {max(abs(v) for v in L[128 * 16:] + R[128 * 16:])} LSB')

# X bounds: aeria.py's guard shadows Y and P only, so sentinels either side of the X ring are
# loaded with the image and read back (-peekx) after a run with every knob automated
import random
import re


def with_sentinels(ms, tag):
    out = []
    for c, mm in zip('AB', ms):
        blob = Path(mm).read_bytes()
        body, term = blob[:-9], blob[-9:]
        for ad in (G.TEST_XRING - 64, G.TEST_XRING + 0x4000):
            body += struct.pack('<BII', 1, ad, 64) + struct.pack('<I', 0x5e5e5e) * 64
        p_ = A.BUILD / f'{tag}_{c}.mem'
        p_.write_bytes(body + term)
        out.append(p_)
    return out


rnd = random.Random(11)
blocks = 1500
ev = [(k, [rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), rnd.randrange(6), rnd.randrange(128), 127]
       + [0] * 6) for k in range(0, blocks, 40)]
ads = list(range(G.TEST_XRING - 64, G.TEST_XRING)) + list(range(G.TEST_XRING + 0x4000, G.TEST_XRING + 0x4040))
ring = [G.TEST_XRING, G.TEST_XRING + 0x1fff, G.TEST_XRING + 0x3fff]
bad, used = 0, 0
for bb, tg in ((TB, 'xst'), (FB_, 'xsf')):
    sm = dict(bb, mems=with_sentinels(bb['mems'], 'ec' + tg))
    for split in (0, 7):
        _, _, m = run(f'sent{split}', sm, P(time=127, fdbk=127), A.noise(blocks * 16, 0.7, 8), blocks, events=ev,
                      split=split, extra=['-peekx', ','.join(f'{v:x}' for v in ads + ring)])
        got = {int(k, 16): int(v, 16) for k, v in re.findall(r'X:0x([0-9a-f]+) = 0x([0-9a-f]+)', m['log'])}
        bad += sum(1 for v in ads if got.get(v) != 0x5e5e5e)
        used += sum(1 for v in ring if got.get(v, 0) != 0)
rep.check('X ring bounds: 64 sentinel words below X:$10000 and 64 above X:$13fff unchanged after automated runs '
          '(both harness builds, splits 0 and 7); first / middle / last ring words were written',
          bad == 0 and used == 12, f'{bad} sentinel words changed of {4 * len(ads)}; ring probes written {used} of 12')

# ---- 7. WDTH ---------------------------------------------------------------------
blocks = 1500
n = blocks * 16
L, R, _ = run('w0', b, P(time=60, fdbk=90, wdth=0), A.noise(n, 0.5, 2), blocks)
dif = max(abs(u - v) for u, v in zip(L[ST:], R[ST:])) / A.FS
rep.check('WDTH 0: wet is mono (L = R); WDTH 127 is the hard-panned case tested above',
          dif < 1e-5 and A.rms(L[ST:]) > 0.01, f'max |L - R| {dif:.2e} FS, rms {A.db(A.rms(L[ST:])):.1f} dBFS')

rep.note('cost', f'static {cost[0]} cycles/sample (worst of the plain / crossfading loops); dsp_host mean {cost[2]:.1f}, '
         f'worst block {cost[1]:.1f} instr/sample')
# cost while crossfading: TIME moved every 250 blocks keeps a fade running ~88 % of the time
blocks = 1500
ev = [(k, P(time=60 + 40 * ((k // 250) % 2), fdbk=127, rato=2, wdth=64)) for k in range(0, blocks, 250)]
_, _, m = run('costfade', b, P(time=60, fdbk=127, rato=2, wdth=64), A.noise(blocks * 16, 0.5), blocks, events=ev)
nul = A.null_cost()
rep.note('cost while crossfading', f"worst block {m['max_per_frame'] - nul['max_per_frame']:.1f} instr/sample "
         f"(mean over a run fading most of the time {m['mean_per_frame'] - nul['mean_per_frame']:.1f})")
# ---- stereo (STEREO.md): unchanged by the width pass, measured with the same check as the others (full-size X build)
def wrun(wd):
    def run_(xx, bl):
        return run(f'w{wd}', FB_, P(time=80, fdbk=60, tone=100, rato=0, wdth=wd, mix=127), xx, bl)[:2]
    return run_


A.stereo_report(rep, 'defaults (TIME 80, FDBK 60, JUST, WDTH 127), MIX 127', wrun(127), True, secs=4.0, info=True)
A.stereo_report(rep, 'WDTH 64', wrun(64), True, secs=4.0, info=True)
A.stereo_report(rep, 'WDTH 0', wrun(0), False, secs=4.0, info=True)
rep.note('size', f'{len(fullwords)} code words; r7 state {G.STATE_WORDS} words of 256')
rep.save('/aeria/echo/test_results.md')
