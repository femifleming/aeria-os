"""ECHO (2nd version) gate: tools/run.sh python3 /aeria/echo2/test.py   (writes echo2/test_results.md)

Builds (all generated here from gen.py):
  echo2_test.asm   Y ring 15,870 (the window aeria.py's -guard polices), X ring 16,384 at X:$10000: every gate
  echo2_fullx.asm  Y ring 16,384, X ring at X:$10000: the full build with only XRING moved (the longest delays)
  echo2.asm        the full build (XRING = $4000): assembled, compared word for word with echo2_fullx.asm
"""
import cmath
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/echo2')
import aeria as A
import gen as G

FSR = 44100
for name, kw in G.BUILDS.items():
    G.build(out=name, **kw)
rep = A.Report('ECHO2')
DEAR = {'TIME': 127, 'FDBK': 127, 'TONE': 100, 'SKEW': 127, 'SPRD': 64, 'MIX': 127, 'HPF': 90, 'GLID': 127}
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/echo2/echo2_test.asm', 'AERIA ECHO2', '/aeria/echo2/manifest.py', DEAR)
NAMES = ['TIME', 'FDBK', 'TONE', 'SKEW', 'SPRD', 'MIX', 'HPF', 'FRZ', 'RFRZ', 'TMOD', 'GLID', '']
assert names == NAMES, names
TB = dict(mems=mems, init=init, proc=proc, dmax=15870 - G.MARGIN, guard=True, tag='t')
fmems, fwords, finit, fproc = A.assemble('/aeria/echo2/echo2_fullx.asm', 'AERIA E2FX')
FB_ = dict(mems=fmems, init=finit, proc=fproc, dmax=16384 - G.MARGIN, guard=0x4000, tag='f')
ST = 300 * 16          # first sample a test uses: the 128 clearing calls are over


def P(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def run(tag, b, p, inp, blocks, **kw):
    kw.setdefault('guard', b['guard'])
    L, R, m = A.render(f"e2_{b['tag']}_{tag}", b['mems'], b['init'], b['proc'], p, inp, blocks, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, f'{tag}: guard {m["stray"]} stray'
    return L, R, m


def st(l, r):
    out = []
    for a, b_ in zip(l, r):
        out += [a, b_]
    return out


def delays(time, skew, dmax):
    tl = min(G.t_left(time) * FSR, dmax)
    return round(tl), round(min(tl * G.skew_ratio(skew), dmax))


def dft(h, f):
    w = cmath.exp(-2j * math.pi * f / FSR)
    z, acc = 1, 0
    for v in h:
        acc += v * z
        z *= w
    return acc


def d2(x):
    return max(abs(a - 2 * b_ + c) for a, b_, c in zip(x[2:], x[1:], x))


# ---- 0. the three builds are one source ---------------------------------------------------------------------
code = lambda f: [l for l in Path(f).read_text().splitlines() if not l.startswith(';')]
df = [(a, b_) for a, b_ in zip(code('/aeria/echo2/echo2.asm'), code('/aeria/echo2/echo2_fullx.asm')) if a != b_]
fullmems, fullwords, _, _ = A.assemble('/aeria/echo2/echo2.asm', 'AERIA E2FULL')
dw = [i for i, (a, b_) in enumerate(zip(fullwords, fwords)) if a != b_]
rep.check('full build assembles (round-trip, mpysu audit); differs from echo2_fullx only in the XRING literal',
          len(fullwords) == len(fwords) and len(df) == 2 and len(dw) == 2 and all('$004000' in a and '$010000' in b_ for a, b_ in df),
          f'{len(fullwords)} words; {len(df)} source lines / {len(dw)} code words differ')

# ---- 1. per-side time accuracy and hard panning ---------------------------------------------------------------
rows, ok = [], True
for b in (TB, FB_):
    for time, skew in ((115, 84), (0, 64), (64, 0), (127, 127), (90, 30)):
        want = delays(time, skew, b['dmax'])
        blocks = (ST + max(want) + 600) // 16
        n = blocks * 16
        for side in (0, 1):
            imp = A.impulse(n, at=ST, amp=0.5)
            zero = [0] * n
            x = st(imp, zero) if side == 0 else st(zero, imp)
            L, R, _ = run(f'imp{time}_{skew}_{side}', b, P(TIME=time, SKEW=skew, FDBK=0, MIX=127), x, blocks, stereo_in=True)
            me, other = (L, R) if side == 0 else (R, L)
            at = max(range(ST + 1, n), key=lambda i: abs(me[i]))
            lvl = me[at] / (0.5 * A.FS)
            early = max(abs(v) for v in me[ST + 1: at])
            leak = max(abs(v) for v in other)
            good = abs(at - ST - want[side]) <= 1 and abs(lvl - 1) < 1e-5 and early == 0 and leak == 0
            ok &= good
            rows.append(f'{b["tag"]} TIME {time} SKEW {skew} {"LR"[side]}: {at - ST} (want {want[side]}) level {lvl:.6f} '
                        f'other side max {leak} LSB')
rep.check('impulse into ONE side, FDBK 0, MIX 127: one echo at the knob\'s delay (TIME: 30 ms x (37/3)^(v/127); right = '
          'left x 2^((SKEW - 64)/64), held to the ring) +-1 sample, level 1 (wet = the input), nothing before it, the '
          'other output exactly 0 (both builds; t = test rings 15,862 max, f = full 16,376)', ok, '; '.join(rows))

# hard panning under every feature: feedback, HPF, tone, freeze, fade -- one side driven, the other exactly silent
b = TB
blocks = int(4 * FSR / 16)
n = blocks * 16
rows, ok = [], True
for lbl, kn, ev in (('FDBK 127 HPF 90 TONE 60', dict(FDBK=127, HPF=90, TONE=60), None),
                    ('FDBK 110, SLEW: TIME moved', dict(FDBK=110), [(1000, P(FDBK=110, TIME=40))]),
                    ('FDBK 110, FADE: TIME moved, then FRZ', dict(FDBK=110, TMOD=1),
                     [(1000, P(FDBK=110, TMOD=1, TIME=40)), (6000, P(FDBK=110, TMOD=1, TIME=40, FRZ=1))])):
    for side in (0, 1):
        nz = A.noise(n, 0.7, 3 + side)
        zero = [0] * n
        x = st(nz, zero) if side == 0 else st(zero, nz)
        L, R, _ = run(f'pan{side}', b, P(MIX=127, **kn), x, blocks, events=ev, stereo_in=True)
        me, other = (L, R) if side == 0 else (R, L)
        leak = max(abs(v) for v in other)
        ok &= leak == 0 and A.rms(me[ST:]) > 0.05
        rows.append(f'{lbl}, {"LR"[side]} driven: other side max {leak} LSB, driven rms {A.db(A.rms(me[ST:])):.1f} dBFS')
rep.check('hard panning: 0.7 FS noise into one side only, MIX 127, SPRD 127: the other output is exactly 0 throughout',
          ok, '; '.join(rows))

# ---- 2. regeneration: loop gain, TONE and HPF against the formulas --------------------------------------------
# impulse into the left: echo 1 at D is the read; echo 2 at 2D is k x TONE x HP1 x HP2 of it
b = TB


def hp_model(f, v):
    """the two one-poles (48-bit-free float model): HPF off = one 10 Hz pole"""
    z = cmath.exp(-1j * 2 * math.pi * f / FSR)

    def onep(fc):
        c = G.coef(fc)
        lp = c / (1 - (1 - c) * z)
        return 1 - lp
    if v == 0:
        return onep(G.DC_HZ)
    fc = G.hpf_hz(v) * G.HPK
    return onep(fc) ** 2


def tone_model(f, v):
    if v >= 127:
        c = 1 - 2 ** -23
    else:
        c = G.coef(G.tone_hz(v))
    z = cmath.exp(-1j * 2 * math.pi * f / FSR)
    return c / (1 - (1 - c) * z)


def rbj_hp(f, f0, Q=0.6):
    w0 = 2 * math.pi * f0 / FSR
    al = math.sin(w0) / (2 * Q)
    cw = math.cos(w0)
    b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
    a0, a1, a2 = 1 + al, -2 * cw, 1 - al
    z = cmath.exp(-1j * 2 * math.pi * f / FSR)
    return (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z)


FREQS = [30, 60, 100, 160, 250, 320, 400, 630, 1000, 2000, 4000, 8000, 12000]
rows, ok, worst_rbj = [], True, []
for fdbk, tone, hpf in ((49, 127, 0), (100, 127, 90), (100, 60, 0), (80, 90, 60), (115, 127, 120)):
    time = 40
    D = delays(time, 64, b['dmax'])[0]
    blocks = (ST + 2 * D + 2 * D) // 16 + 2
    n = blocks * 16
    x = st(A.impulse(n, at=ST, amp=0.5), [0] * n)
    L, R, _ = run(f'resp{fdbk}_{tone}_{hpf}', b, P(TIME=time, SKEW=64, FDBK=fdbk, TONE=tone, HPF=hpf, MIX=127), x, blocks,
                  stereo_in=True)
    e1 = [v / A.FS for v in L[ST + D - 4: ST + D + D - 4]]
    e2 = [v / A.FS for v in L[ST + 2 * D - 4: ST + 3 * D - 4]]
    g = G.fb_gain(fdbk)
    errs = []
    for f in FREQS:
        got = abs(dft(e2, f)) / abs(dft(e1, f))
        want = g * abs(tone_model(f, tone) * hp_model(f, hpf))
        errs.append((want, abs(A.db(got) - A.db(want))))
    top = max(w_ for w_, _ in errs)
    errs = [e_ for w_, e_ in errs if w_ > top * 10 ** (-30 / 20)]
    e = max(errs)
    ok &= e < 0.1
    rows.append(f'FDBK {fdbk} (g {g:.3f}) TONE {tone} HPF {hpf}: max |error| {e:.3f} dB over {len(errs)} frequencies')
    if hpf:
        f0 = G.hpf_hz(hpf)
        dev = [A.db(abs(hp_model(f, hpf))) - A.db(abs(rbj_hp(f, f0))) for f in FREQS]
        worst_rbj.append(f'HPF {hpf} ({f0:.0f} Hz): two one-poles vs the plugin\'s RBJ Q 0.6 ' +
                         ' '.join(f'{f}:{v:+.1f}' for f, v in zip(FREQS, dev) if f <= 4000) + ' dB')
rep.check('second echo / first echo spectrum (impulse, left) = FDBK\'s loop gain x TONE one-pole x the high-pass pair '
          '(float model of the generator\'s laws), within 0.1 dB wherever the model is within 30 dB of its peak', ok, '; '.join(rows))
rep.note('HPF against Chloaelis\' Biquad::highpass(f, Q 0.6): same -3 dB point, 12 dB/oct, a rounder knee', '; '.join(worst_rbj))

# ---- 3. FDBK 127 for 10 s: self-oscillation held by the rider, never clipped ------------------------------------
blocks = int(12 * FSR / 16)
n = blocks * 16
stop = 4 * FSR
rows, ok = [], True
for lbl, kn, amp in (('TIME 40 TONE 127', dict(TIME=40), 0.9), ('TIME 127 SKEW 30 TONE 90 HPF 90', dict(TIME=127, SKEW=30, TONE=90, HPF=90), 0.9),
                     ('TIME 0 (30 ms), quiet input', dict(TIME=0), 0.05), ('FADE, TIME moved every 0.5 s', dict(TIME=60, TMOD=1), 0.9)):
    ev = None
    if 'moved' in lbl:
        ev = [(k * int(0.5 * FSR / 16), P(FDBK=127, MIX=127, TMOD=1, TIME=(60 if k % 2 else 100))) for k in range(1, 24)]
    x = A.noise(stop, amp, 9) + [0] * (n - stop)
    L, R, _ = run('fbmax', b, P(FDBK=127, MIX=127, **kn), x, blocks, events=ev)
    pk_on = A.peak(L[ST:stop] + R[ST:stop])
    tail = stop + 16384 + 64          # the input's own last pass has gone by
    pk_off = A.peak(L[tail:] + R[tail:])
    r_end = A.rms(L[-FSR:] + R[-FSR:])
    rail = sum(1 for v in L + R if abs(v) >= A.FS - 1)
    ok &= pk_off <= 0.75 and r_end > 0.01 and (rail == 0 or amp > 0.5)
    rows.append(f'{lbl}, {amp} FS noise 4 s: steady rms {A.db(r_end):.1f} dBFS, crest {A.peak(L[-FSR:] + R[-FSR:]) / max(r_end, 1e-9):.1f}; peak with input {pk_on:.3f}, once the input\'s last pass has gone by {pk_off:.3f} FS, rms of the last second '
                f'{A.db(r_end):.1f} dBFS, samples at the rail {rail}')
rep.check('FDBK 127 (loop gain 1.2075): keeps singing after the input stops, the regenerated wet held under 0.75 FS '
          '(rider ceiling 0.58, x 1.2075 at most while a pass grows) for 8 s; with a 0.05 FS input nothing reaches the rail (a loud input plus a loud loop can sum '
          'past full scale at the OUTPUT: counted; the lines themselves never saturate)', ok, '; '.join(rows))

# decays to exact silence below unity
rows, ok = [], True
for kn, secs in ((dict(FDBK=110, TIME=0), 20), (dict(FDBK=115, TIME=0), 70), (dict(FDBK=115, TIME=0, HPF=127, TONE=40), 10),
                 (dict(FDBK=100, TIME=60, TONE=127), 20), (dict(FDBK=110, TIME=0, TMOD=1, HPF=60), 20)):
    blocks = int(secs * FSR / 16)
    n = blocks * 16
    x = A.noise(FSR, 0.7, 4) + [0] * (n - FSR)
    L, R, _ = run('decay', b, P(MIX=127, **kn), x, blocks)
    last = max((i for i in range(n) if L[i] or R[i]), default=0)
    ok &= last < n - FSR
    rows.append(f'{kn} (loop gain {G.fb_gain(kn["FDBK"]):.3f}): last non-zero sample {(last - FSR) / FSR:.2f} s after the input '
                f'({secs} s render)')
rep.check('silence after the input once the regeneration is below unity: exactly 0 (at 0.99 a pass and 30 ms, -140 dB takes '
          '~45 s)', ok, '; '.join(rows))

# ---- 4. FREEZE holds exactly ---------------------------------------------------------------------------------------
rows, ok = [], True
for lbl, kn in (('SLEW', dict()), ('FADE', dict(TMOD=1)), ('FDBK 127 HPF 100', dict(FDBK=127, HPF=100))):
    blocks = int(11 * FSR / 16)
    n = blocks * 16
    fz = 2000                     # block of the freeze
    x = A.noise(fz * 16, 0.7, 6) + [0] * (n - fz * 16)
    base = dict(TIME=100, SKEW=40, FDBK=80, MIX=127)
    base.update(kn)
    L, R, _ = run('frz', b, P(**base), x, blocks, events=[(fz, P(**dict(base, FRZ=1)))])
    DL_, DR_ = delays(base['TIME'], base['SKEW'], b['dmax'])
    a0 = fz * 16 + max(DL_, DR_) + 64
    errl = max(abs(L[i] - L[i - DL_]) for i in range(a0, n))
    errr = max(abs(R[i] - R[i - DR_]) for i in range(a0, n))
    ok &= errl == 0 and errr == 0 and A.rms(L[-FSR:]) > 0.01
    rows.append(f'{lbl}: L period {DL_}, R period {DR_}: max |out[n] - out[n - D]| L {errl} R {errr} LSB over '
                f'{(n - a0) / FSR:.1f} s, rms L {A.db(A.rms(L[-FSR:])):.1f} R {A.db(A.rms(R[-FSR:])):.1f} dBFS at the end')
# the random-freeze bit: r6+$d bit 0 set from the start: nothing is ever written, so no echo is heard
blocks = int(3 * FSR / 16)
n = blocks * 16
x = A.noise(FSR, 0.7, 7) + [0] * (n - FSR)
pw = (P()[NAMES.index('RFRZ')] << 16) | (P()[NAMES.index('TMOD')] << 8) | 1
L, R, _ = run('rfrz', b, P(TIME=20, FDBK=100, MIX=127), x, blocks, extra=('-pword', f'0:d={pw:x}'))
silent = max(abs(v) for v in L[FSR + 16:] + R[FSR + 16:])
L2, R2, _ = run('rfrz0', b, P(TIME=20, FDBK=100, MIX=127), x, blocks)
ok &= silent == 0 and A.rms(L2[FSR:]) > 0.01
rows.append(f'random-freeze bit (r6+$d bit 0) held from the start: after the input max {silent} LSB '
            f'(the same render without it: rms {A.db(A.rms(L2[FSR:])):.1f} dBFS)')
# thaw: the loop takes input again
rep.check('FRZ HOLD: the output repeats with the line\'s period exactly (bit for bit) for 9 s, no decay; the ColdFire\'s '
          'random-freeze bit freezes the same way', ok, '; '.join(rows))


# ---- 5. SLEW bends the pitch smoothly; FADE crossfades without a click -------------------------------------------
def inst_freq(x, a, bnd):
    """frequency per half period from interpolated zero crossings"""
    zc = []
    for i in range(a, bnd):
        if (x[i - 1] < 0) != (x[i] < 0) and x[i] != x[i - 1]:
            zc.append(i - 1 + x[i - 1] / (x[i - 1] - x[i]))
    return [(zc[k], FSR / 2 / (zc[k + 1] - zc[k])) for k in range(len(zc) - 1)]


def glide_model(d0, d1, glv, blocks):
    """the generator's law: two one-poles a call toward the target, the head ramping linearly within the call"""
    g = G.coef(G.glide_hz(glv), 16)
    s1 = s2 = d = d0
    out = []
    for _ in range(blocks):
        s1 += g * (d1 - s1)
        s2 += g * (s1 - s2)
        inc = (s2 - d) / 16
        for _ in range(16):
            d += inc
            out.append(d)
    return out


def plugin_slew(d0, d1, n):
    """Chloaelis' TapeLine: dS += (dT - dS) a; dA += (dS - dA) a, a sample at a time, a = onePoleCoef(1.6 Hz)"""
    a = G.coef(1.6)
    ds = da = d0
    out = []
    for _ in range(n):
        ds += (d1 - ds) * a
        da += (ds - da) * a
        out.append(da)
    return out


f0 = 1000.0
rows, ok = [], True
for glv, t0, t1 in ((64, 80, 100), (64, 100, 60), (127, 60, 110), (0, 90, 80)):
    blocks = int(4 * FSR / 16)
    n = blocks * 16
    mv = 1000
    x = A.sine(f0, 0.5, n)
    L, R, _ = run('slew', b, P(TIME=t0, FDBK=0, MIX=127, GLID=glv), x, blocks, events=[(mv, P(TIME=t1, FDBK=0, MIX=127, GLID=glv))])
    D0, D1 = delays(t0, 84, b['dmax'])[0], delays(t1, 84, b['dmax'])[0]
    mdl = glide_model(G.t_left(t0) * FSR, G.t_left(t1) * FSR, glv, blocks - mv)
    want = [f0 * (1 - (mdl[i + 1] - mdl[i])) for i in range(len(mdl) - 1)]
    fr = inst_freq(L, mv * 16 - 2000, n - 200)
    pts = [(f, abs(want[min(len(want) - 1, max(0, round(t) - mv * 16))])) for t, f in fr if t > mv * 16 + 200]
    rev = any(w_ < 0 for w_ in want)
    err = max(abs(f - w_) for f, w_ in pts if w_ > 300)
    lo, hi = min(f for _, f in fr), max(f for _, f in fr)
    cents = 1200 * math.log2(hi / lo)
    jump = max(abs(fr[k + 1][1] - fr[k][1]) for k in range(len(fr) - 1))
    sl_ = 2 * math.pi * hi / FSR
    dd = d2(L[mv * 16:]) / (0.5 * A.FS * sl_ * sl_)
    endf = sum(f for _, f in fr[-200:]) / 200
    ok &= (err < 0.01 * f0 + 2 or rev) and dd < 1.1 and abs(endf - f0) < 1
    pm = plugin_slew(G.t_left(t0) * FSR, G.t_left(t1) * FSR, len(mdl))
    pdev = max(abs(a_ - b2) for a_, b2 in zip(mdl, pm))
    rows.append(f'GLID {glv} ({G.glide_hz(glv):.2f} Hz) TIME {t0} -> {t1} ({D0} -> {D1}): 1 kHz bends {lo:.0f}..{hi:.0f} Hz '
                f'({cents:.0f} cents), tracks the law within {err:.1f} Hz' + (' (the head runs faster than 1 sample a sample for a while: playback reverses, the zero-crossing estimate cannot follow; not gated)' if rev else '') + f', largest step between half periods {jump:.1f} Hz, '
                f'2nd difference {dd:.3f} x a clean sine\'s at the top frequency, settles at {endf:.2f} Hz'
                + (f'; head vs the plugin\'s per-sample tape servo: max {pdev:.1f} samples apart' if glv == 64 else ''))
rep.check('SLEW: TIME moved under a 1 kHz sine (FDBK 0): the pitch bends by -dD/dt and comes back, following the two '
          'one-poles within 1 % (+2 Hz, where the law stays above 300 Hz), no click (2nd difference within 1.1 x a sine at the highest pitch reached)',
          ok, '; '.join(rows))

rows, ok = [], True
for t0, t1, f in ((70, 90, 2940.0), (110, 40, 2940.0), (64, 66, 440.0)):
    blocks = int(3 * FSR / 16)
    n = blocks * 16
    mv = 1000
    x = A.sine(f, 0.5, n)
    L, R, _ = run('fade', b, P(TIME=t0, FDBK=0, MIX=127, TMOD=1), x, blocks, events=[(mv, P(TIME=t1, FDBK=0, MIX=127, TMOD=1))])
    fr = inst_freq(L, mv * 16 - 2000, mv * 16 + int(0.3 * FSR))
    dev = max(abs(v - f) for _, v in fr)
    seg = L[mv * 16 - 500: mv * 16 + int(0.1 * FSR)]
    sl_ = 2 * math.pi * f / FSR
    dd = d2(seg) / (0.5 * A.FS * sl_ * sl_)
    env = [max(abs(v) for v in L[i:i + int(FSR / f) + 2]) for i in range(mv * 16, mv * 16 + int(0.07 * FSR), 32)]
    gate = abs(t1 - t0) <= 20
    ok &= (dev < 0.01 * f or not gate) and dd < 1.5
    rows.append(f'TIME {t0} -> {t1}, {f:.0f} Hz{"" if gate else " (heads 8,800 samples apart: two equal sines out of phase dip and slew phase mid-fade, as any crossfade; frequency not gated)"}: frequency within {dev:.2f} Hz, 2nd difference {dd:.3f} x a sine\'s, '
                f'level through the fade {A.db(min(env) / A.FS):.1f} .. {A.db(max(env) / A.FS):.1f} dBFS')
# DC through both heads: the gains sum to one
blocks = int(1.5 * FSR / 16)
n = blocks * 16
dcv = round(0.25 * A.FS)
L, R, _ = run('fadedc', b, P(TIME=70, FDBK=0, MIX=127, TMOD=1), [dcv] * n, blocks, events=[(1500, P(TIME=90, FDBK=0, MIX=127, TMOD=1))])
spread = (max(L[1500 * 16: 1500 * 16 + 4000]) - min(L[1500 * 16: 1500 * 16 + 4000])) / A.FS
ok &= spread < 2e-6
rows.append(f'DC through a fade: spread {spread:.2e} FS (the sin^2 gains sum to 1)')
rep.check('FADE: TIME moved under a steady sine: no pitch change (within 1 %, small moves), no click (2nd difference within 1.5 x); '
          'the crossfade gains sum to one', ok, '; '.join(rows))

# ---- 6. MIX 0 bit-exact with everything on; silence in, silence out -------------------------------------------------
blocks = int(1.5 * FSR / 16)
n = blocks * 16
rnd = random.Random(5)
xl = [round(-A.FS + i * 2 * A.FS / (n - 1)) for i in range(n)]
xr = [rnd.randrange(-A.FS, A.FS) for _ in range(n)]
ev = [(k * 40, P(MIX=0, FDBK=127, HPF=60, TONE=20, TIME=rnd.randrange(128), FRZ=k % 2, TMOD=(k // 3) % 2)) for k in range(1, blocks // 40)]
L, R, _ = run('dry', b, P(MIX=0, FDBK=127), st(xl, xr), blocks, events=ev, stereo_in=True)
e = max(max(abs(a_ - b2) for a_, b2 in zip(L, xl)), max(abs(a_ - b2) for a_, b2 in zip(R, xr)))
rep.check('MIX 0 is bit-exact dry (stereo: a full-range ramp left, random right; FDBK 127, HPF, TONE, FRZ and TMOD '
          'toggled, TIME moved)', e == 0, f'max |err| {e} LSB')
L, R, _ = run('sil', b, P(**{k: v for k, v in DEAR.items()}), None, blocks)
rep.check('silence in, silence out (dearest settings): every sample 0', not any(L) and not any(R))

# ---- 7. no zipper on knob moves -------------------------------------------------------------------------------------
blocks = int(4 * FSR / 16)
n = blocks * 16
x = A.sine(440, 0.5, n)


def hf(y):
    y = y[ST + 3:]
    e3 = sum((y[i] - 3 * y[i - 1] + 3 * y[i - 2] - y[i - 3]) ** 2 for i in range(3, len(y)))
    return 10 * math.log10(max(e3, 1e-9) / (sum(v * v for v in y) or 1))


rnd = random.Random(8)
rows, ok = [], True
for knob in ('FDBK', 'TONE', 'MIX', 'SPRD', 'HPF'):
    ev = [(k * 45, P(**dict(dict(TIME=90, FDBK=90), **{knob: rnd.randrange(128)}))) for k in range(1, blocks // 45)]
    if knob == 'FDBK':
        ev = [(k * 45, P(TIME=90, FDBK=rnd.randrange(110))) for k in range(1, blocks // 45)]
    L, R, _ = run('zip', b, P(TIME=90, FDBK=90), x, blocks, events=ev)
    L0, _, _ = run('zip0', b, P(TIME=90, FDBK=90), x, blocks)
    h, h0 = hf(L), hf(L0)
    ok &= h < h0 + 6
    rows.append(f'{knob} jumped every 0.73 ms x 45: HF {h:.1f} dB re the signal (knobs still {h0:.1f})')
rep.check('knob jumps (FDBK, TONE, MIX, SPRD, HPF at random every 45 calls) on a 440 Hz sine with FDBK 90: the '
          'third-difference energy (clicks) within 6 dB of the same render with the knobs still', ok, '; '.join(rows))

# ---- 8. cost in the three regimes ------------------------------------------------------------------------------------
nul = A.null_cost()
blocks = 3000
n = blocks * 16
x = A.noise(n, 0.5, 2)
rows = []
worst = cost[1]
for lbl, p, ev in (('SLEW, gliding all the time (TIME jumps every 40 calls, GLID 127)',
                    P(**DEAR), [(k * 40, P(**dict(DEAR, TIME=(20 if k % 2 else 120)))) for k in range(1, blocks // 40)]),
                   ('FADE, fading most of the time', P(**dict(DEAR, TMOD=1)),
                    [(k * 170, P(**dict(DEAR, TMOD=1, TIME=(20 if k % 2 else 120)))) for k in range(1, blocks // 170)]),
                   ('frozen', P(**dict(DEAR, FRZ=1)), None)):
    _, _, m = run('cost', b, p, x, blocks, events=ev)
    c = m['max_per_frame'] - nul['max_per_frame']
    worst = max(worst, c)
    rows.append(f'{lbl}: mean {m["mean_per_frame"] - nul["mean_per_frame"]:.1f}, worst block {c:.1f}')
rep.note('cost (dsp_host instructions a sample, meter minus a null effect)', f'dearest static: mean {cost[2]:.1f}, worst block '
         f'{cost[1]:.1f}; ' + '; '.join(rows) + f'; worst of all {worst:.1f}; static count {cost[0]}')
rep.note('size', f'{len(fullwords)} code words (2,048 limit); r7 block {G.STATE_WORDS} words; Y ring 16,384 (buffer base), '
         'X ring 16,384 at X:$4000 (the old ECHO\'s memory)')
rep.save('/aeria/echo2/test_results.md')
