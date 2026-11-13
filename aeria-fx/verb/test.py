"""VERB gate: tools/run.sh python3 /aeria/verb/test.py

The standard gate runs on the TEST-size build (verb_test.asm: every delay
length x 0.5, 15,870-word ring at the stock FX2 base). The FULL-size build
(verb.asm: 32,768-word ring, m = $7fff) is rendered too, on allocator slot 5
(Y:0x30000, 32K-aligned, in the shared window) with -guard 32768 and
-guard-shared. Both are compared against a floating-point model of the
plugin (FDNReverb.h) written out below.
"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/verb')
import aeria as A
import gen

fs = 44100
FS = A.FS
GT = gen.build('test')
GF = gen.build('full')
FULLX = ['-alloc', '5', '-guard-shared']       # full build: base Y:0x30000


# ---- floating-point model of the plugin (same structure as FDNReverb.h) ----
class Biquad:
    def __init__(self, kind, f, qf):
        w = 2 * math.pi * min(f, fs * 0.49) / fs
        c, al = math.cos(w), math.sin(w) / (2 * qf)
        if kind == 'lp':
            b = ((1 - c) / 2, 1 - c, (1 - c) / 2)
        else:
            b = ((1 + c) / 2, -(1 + c), (1 + c) / 2)
        a0 = 1 + al
        self.b0, self.b1, self.b2 = (v / a0 for v in b)
        self.a1, self.a2 = -2 * c / a0, (1 - al) / a0
        self.z1 = self.z2 = 0.0

    def run(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        return y


def knob_t60(k):
    return 0.3 * 100.0 ** (k / 127.0)


def knob_tone(k):
    return (k - 64) / 64.0 if k < 64 else (k - 64) / 63.0


def reference(G, decy, tone, modk, xl, xr):
    """Wet L, R of the plugin's algorithm for float inputs xl, xr (it starts
    with the first sample given). The LFO rates are the 24-bit-quantised ones
    the DSP uses (lfo_hz), everything else is the plugin's arithmetic."""
    t60, t = knob_t60(decy), knob_tone(tone)
    hpf = 500.0 * 10 ** t
    lpf = min(20000.0 * 10 ** (1.82 * t) if t < 0 else 20000.0, fs * 0.45)
    filt = [[Biquad('hp', hpf, 0.54119610), Biquad('hp', hpf, 1.30656296),
             Biquad('lp', lpf, 0.54119610), Biquad('lp', lpf, 1.30656296)] for _ in range(2)]
    apl = G['apl']
    apb = [[[0.0] * apl[c][a] for a in range(4)] for c in range(2)]
    api = [[0] * 4 for _ in range(2)]
    ln = G['ln']
    size = 1 << 14
    mask = size - 1
    buf = [[0.0] * size for _ in range(8)]
    w = 0
    gain = [10 ** (-3.0 * ln[i] / (t60 * fs)) for i in range(8)]
    rate = [2 * math.pi * f / fs for f in G['lfo_hz']]
    ph = [0.77 * i for i in range(8)]
    depth = modk / 127.0 * gen.DEPTH_MAX
    a_d = gen.DAMP_A
    z = [0.0] * 8
    outl, outr = [], []
    nrm = 0.35355339
    sin, floor = math.sin, math.floor
    for n in range(len(xl)):
        ins_ = [xl[n], xr[n]]
        for c in range(2):
            s = ins_[c]
            for f in filt[c]:
                s = f.run(s)
            for a in range(4):
                b = apb[c][a]
                i = api[c][a]
                dly = b[i]
                v = s + 0.62 * dly
                b[i] = v
                api[c][a] = (i + 1) % apl[c][a]
                s = dly - 0.62 * v
            ins_[c] = s
        y = [0.0] * 8
        for i in range(8):
            ph[i] += rate[i]
            d = ln[i] + depth * sin(ph[i])
            p = (w - 1) - d
            fl = floor(p)
            ii = int(fl)
            tt = p - fl
            bb = buf[i]
            xm1, x0, x1, x2 = bb[(ii - 1) & mask], bb[ii & mask], bb[(ii + 1) & mask], bb[(ii + 2) & mask]
            c1 = 0.5 * (x1 - xm1)
            c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2
            c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1)
            r = ((c3 * tt + c2) * tt + c1) * tt + x0
            z[i] += a_d * (r - z[i])
            y[i] = z[i] * gain[i]
        outl.append(0.55 * (y[0] - y[1] + y[2] + y[3] - y[4] + y[5] - y[6] + y[7]))
        outr.append(0.55 * (y[0] + y[1] - y[2] + y[3] + y[4] - y[5] - y[6] + y[7]))  # + y[7]: orthogonal to the left (12 Nov quality pass; the plugin has - y[7])
        h = 1
        while h < 8:
            for i in range(0, 8, h << 1):
                for j in range(i, i + h):
                    a_, b_ = y[j], y[j + h]
                    y[j], y[j + h] = a_ + b_, a_ - b_
            h <<= 1
        for i in range(8):
            buf[i][w] = y[i] * nrm + ins_[i & 1] * 0.5
        w = (w + 1) & mask
    return outl, outr


def knobs(decy=64, tone=64, modk=64, mixk=127):
    return [decy, tone, modk, 0, 0, mixk, 0, 0, 0, 0, 0, 0]


def errdb(got, ref, lo, hi):
    num = sum((g / FS - r) ** 2 for g, r in zip(got[lo:hi], ref[lo:hi]))
    den = sum(r * r for r in ref[lo:hi])
    return 10 * math.log10(max(num, 1e-30) / max(den, 1e-30))


def model_match(rep, tag, G, mems, init, proc, extra, cases, seconds=0.5):
    """Impulse on one side only -> both outputs against the float model."""
    start = G['nclr'] * 16                 # the first sample the effect processes
    at = start + 480
    blocks = (at + int(seconds * fs)) // 16 + 1
    n = blocks * 16
    for decy, tone, modk, side in cases:
        x = [0] * (2 * n)
        x[2 * at + side] = round(0.5 * FS)
        L, R, m = A.render(f'vb_{tag}_ref', mems, init, proc, knobs(decy, tone, modk), x, blocks,
                           stereo_in=True, extra=extra)
        xs = [0.0] * (n - start)
        xi = list(xs)
        xi[at - start] = 0.5
        rl, rr = reference(G, decy, tone, modk, xi if side == 0 else xs, xi if side == 1 else xs)
        el = errdb(L[start:], rl, at - start + 1, n - start)
        er = errdb(R[start:], rr, at - start + 1, n - start)
        lvl = 10 * math.log10(sum(v * v for v in rl[at - start:]) / (n - at))
        rep.check(f'{tag}: matches the float model of FDNReverb.h, impulse on {"LR"[side]}, DECY {decy} TONE {tone} '
                  f'MOD {modk} ({seconds} s; residual vs model, L / R)', el < -30 and er < -30,
                  f'{el:.1f} / {er:.1f} dB (model wet rms {lvl:.1f} dBFS)')


def edc_rt60(x, t0, lo=-5.0, hi=-25.0):
    """RT60 from the Schroeder integral of x[t0:], fitted between lo and hi dB."""
    e = [v * v for v in x[t0:]]
    tot = 0.0
    edc = [0.0] * len(e)
    for i in range(len(e) - 1, -1, -1):
        tot += e[i]
        edc[i] = tot
    if tot <= 0:
        return 0.0
    tl = th = None
    for i, v in enumerate(edc):
        d = 10 * math.log10(max(v / tot, 1e-30))
        if tl is None and d <= lo:
            tl = i
        if d <= hi:
            th = i
            break
    if tl is None or th is None:
        return float('nan')
    return (th - tl) / fs * 60.0 / (lo - hi)


def lowpass(x, f):
    """4th-order Butterworth low-pass (float), for band-limited decay fits."""
    f1_, f2_ = Biquad('lp', f, 0.54119610), Biquad('lp', f, 1.30656296)
    return [f2_.run(f1_.run(v)) for v in x]


def band_db(x, f0, f1, nf=12):
    """Mean power (dB) of x over a log-spaced set of Goertzel probes f0..f1."""
    n_ = len(x)
    x = [v * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n_ - 1))) for i, v in enumerate(x)]   # Hann
    p = 0.0
    for k in range(nf):
        f = f0 * (f1 / f0) ** (k / (nf - 1))
        p += A.goertzel(x, f) ** 2
    return 10 * math.log10(max(p / nf, 1e-30))


def suite(rep, tag, G, mems, init, proc, extra, guard):
    start = G['nclr'] * 16
    # --- decay: monotonic in DECY, RT60 close to the intended T60 ---
    st = start + 480
    blocks = int(1.6 * fs / 16)
    n = blocks * 16
    lv = []
    for k in (0, 64, 127):
        L, R, _ = A.render(f'vb_{tag}_dec', mems, init, proc, knobs(k, 64, 64), A.impulse(n, at=st, amp=0.9),
                           blocks, extra=extra)
        lv.append(A.rms(L[st + fs: st + fs + 4410] + R[st + fs: st + fs + 4410]))
    rep.check(f'{tag}: DECY lengthens the decay (wet rms 1.0 s after an impulse, DECY 0 / 64 / 127)',
              lv[0] < lv[1] < lv[2], ' / '.join(f'{A.db(v):.1f}' for v in lv) + ' dBFS')
    for k, secs in ((33, 2.2), (64, 5.0)):
        blocks = int(secs * fs / 16)
        n = blocks * 16
        L, R, _ = A.render(f'vb_{tag}_rt', mems, init, proc, knobs(k, 64, 64), A.impulse(n, at=st, amp=0.9),
                           blocks, extra=extra)
        rt_all = edc_rt60(L, st + 2205)
        rt = edc_rt60(lowpass(L, 1500.0), st + 2205)
        want = knob_t60(k)
        rep.check(f'{tag}: RT60 at DECY {k} within 25% of T60 = {want:.2f} s (Schroeder fit -5..-25 dB of the wet '
                  'impulse response below 1.5 kHz, where the 7.5 kHz damping costs little)',
                  abs(rt / want - 1) < 0.25, f'{rt:.2f} s ({(rt / want - 1) * 100:+.1f}%); broadband {rt_all:.2f} s')
        if k == 33:
            xs = [0.0] * (n - start)
            xs[st - start] = 0.9
            rl, _ = reference(G, k, 64, 64, xs, xs)
            rm = edc_rt60(rl, st - start + 2205)
            rep.check(f'{tag}: broadband RT60 at DECY {k} within 10% of the float model\'s (same fit)',
                      abs(rt_all / rm - 1) < 0.10, f'{rt_all:.3f} s vs model {rm:.3f} s ({(rt_all / rm - 1) * 100:+.1f}%)')
    # --- stereo decorrelation, bounded at the dearest settings ---
    blocks = int(3 * fs / 16)
    n = blocks * 16
    L, R, m = A.render(f'vb_{tag}_noise', mems, init, proc, knobs(64, 64, 64), A.noise(n, 0.25), blocks,
                       extra=extra, guard=guard)
    a, b = L[-40000:], R[-40000:]
    c = sum(x * y for x, y in zip(a, b)) / math.sqrt(sum(x * x for x in a) * sum(y * y for y in b))
    rep.check(f'{tag}: wide -- L/R correlation of the wet on mono noise, |corr| < 0.5', abs(c) < 0.5,
              f'corr {c:+.3f}, wet rms {A.db(A.rms(a)):.1f} dBFS, peak {A.peak(L + R):.3f}')
    msum = A.db(A.rms([x + y for x, y in zip(a, b)]) / A.rms(a))
    bal = A.db(A.rms(a) / A.rms(b))
    rep.check(f'{tag}: mono-compatible -- orthogonal output taps (quality pass 12 Nov 2026; the plugin\'s gave corr about '
              f'-0.2, L+R +2.3 dB): |corr| < 0.1, L+R within 0.6 dB of +3 dB, L/R balance within 0.5 dB',
              abs(c) < 0.1 and abs(msum - 3.01) < 0.6 and abs(bal) < 0.5,
              f'corr {c:+.3f}, L+R {msum:+.2f} dB re L, L/R {bal:+.2f} dB')
    worst, stray = 0.0, 0
    for tone in (0, 64, 127):
        L, R, m = A.render(f'vb_{tag}_dear', mems, init, proc, knobs(127, tone, 127), A.noise(n, 1.0, 5), blocks,
                           extra=extra, guard=guard)
        worst = max(worst, A.peak(L + R))
        stray += m['stray'] + m['clobber']
    rep.check(f'{tag}: bounded at the dearest settings (DECY 127, MOD 127, MIX 127, TONE 0/64/127, full-scale '
              f'noise), guard clean', worst <= 1.0 + 1e-6 and stray == 0, f'peak {worst:.3f} FS, stray {stray}')
    # --- TONE: darker on the left, thinner on the right ---
    blocks = int(2 * fs / 16)
    n = blocks * 16
    bands = {}
    for tone in (0, 64, 127):
        L, R, _ = A.render(f'vb_{tag}_tone', mems, init, proc, knobs(50, tone, 64), A.noise(n, 0.25, 9), blocks,
                           extra=extra)
        seg = L[-32768:]
        bands[tone] = (band_db(seg, 80, 300), band_db(seg, 700, 2000), band_db(seg, 5000, 12000))
    lo0, mid0, hi0 = bands[0]
    lo6, mid6, hi6 = bands[64]
    lo1, mid1, hi1 = bands[127]
    rep.check(f'{tag}: TONE 0 is darker than centre (5-12 kHz vs 80-300 Hz tilt at least 30 dB lower; '
              'lows up at least 10 dB)', (hi0 - lo0) < (hi6 - lo6) - 30 and lo0 > lo6 + 10,
              f'TONE 0 lo/mid/hi {lo0:.1f}/{mid0:.1f}/{hi0:.1f} dB, TONE 64 {lo6:.1f}/{mid6:.1f}/{hi6:.1f} dB')
    rep.check(f'{tag}: TONE 127 is thinner than centre (0.7-2 kHz at least 15 dB down, 80-300 Hz at least '
              '20 dB down, 5-12 kHz within 6 dB)', mid1 < mid6 - 15 and lo1 < lo6 - 20 and abs(hi1 - hi6) < 6,
              f'TONE 127 lo/mid/hi {lo1:.1f}/{mid1:.1f}/{hi1:.1f} dB')
    # --- DC: none builds up, even from an input that carries DC ---
    blocks = int(3 * fs / 16)
    n = blocks * 16
    off = round(0.3 * FS)
    worstdc = 0.0
    for tone in (0, 64):
        x = [v + off for v in A.noise(n, 0.3, 11)]
        L, R, _ = A.render(f'vb_{tag}_dc', mems, init, proc, knobs(127, tone, 64), x, blocks, extra=extra)
        worstdc = max(worstdc, abs(A.dc(L[-44100:])), abs(A.dc(R[-44100:])))
    rep.check(f'{tag}: no DC build-up (noise + 0.3 FS DC in, DECY 127, TONE 0 and 64: |mean| of the last second '
              'of wet < 1e-3 FS)', worstdc < 1e-3, f'{worstdc:.2e} FS')
    # --- silence after the tail: exact zeros ---
    res = []
    for k, tone, secs in ((0, 64, 4.0), (40, 0, 9.0)):
        blocks = int(secs * fs / 16)
        n = blocks * 16
        nb = int(0.5 * fs)
        x = A.noise(nb, 0.9, 13) + [0] * (n - nb)
        L, R, _ = A.render(f'vb_{tag}_tail', mems, init, proc, knobs(k, tone, 127), x, blocks, extra=extra)
        last = max(abs(v) for v in L[-22050:] + R[-22050:])
        nz = max((i for i in range(n) if L[i] or R[i]), default=0)
        res.append((k, tone, last, nz / fs))
    rep.check(f'{tag}: silence in -> exact silence out after the tail (0.5 s burst, then zeros; last 0.5 s of '
              'the render all 0)', all(r[2] == 0 for r in res),
              '; '.join(f'DECY {k} TONE {t}: last non-zero sample at {z:.2f} s, tail max {l} LSB'
                        for k, t, l, z in res))


rep = A.Report('VERB')
rep.note('builds', f"verb_test.asm ring {GT['ring']} (m ${GT['m']:04x}, {GT['used']} used); "
                   f"verb.asm ring {GF['ring']} (m ${GF['m']:04x}, {GF['used']} used)")
DEAR = {'DECY': 127, 'TONE': 64, 'MOD': 127, 'MIX': 127}
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/verb/verb_test.asm', 'AERIA VERB', '/aeria/verb/manifest.py', DEAR)
rep.note('cost (test size)', f'static {cost[0]} cycles/sample; dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f} '
                             'instr/sample (the standard gate\'s mean < 320 line is expected to FAIL for this effect)')
CASES = ((64, 64, 0, 0), (64, 64, 127, 1), (100, 0, 64, 0), (30, 127, 64, 1), (64, 30, 64, 0), (64, 100, 64, 1))
model_match(rep, 'test size', GT, mems, init, proc, (), CASES)
# LFO: the tracked sine state of lines 0 and 7 against 0.5 sin(2 pi f t + 0.77 i)
blocks = int(4 * fs / 16)
trk = '/aeria/.build/vb_lfo.track'
A.render('vb_lfo', mems, init, proc, knobs(), None, blocks, extra=['-track', '59,9f', '-trackout', trk])
rows = [l.split() for l in open(trk).read().splitlines() if l.strip() and not l.startswith('#')]
worst = 0.0
for r_ in rows:
    b = int(r_[0])
    vals = [int(v) for v in r_[-2:]]
    ns = (b + 1 - GT['nclr']) * 16
    if ns <= 0:
        continue
    for i, v in zip((0, 7), vals):
        v = (v - (1 << 24) if v & 0x800000 else v) / (1 << 23)
        worst = max(worst, abs(v - 0.5 * math.sin(2 * math.pi * GT['lfo_hz'][i] * ns / fs + 0.77 * i)))
rep.check('LFOs: line 0 (0.07 Hz) and line 7 (0.441 Hz) sine states track 0.5 sin(2 pi f t + 0.77 i) over 4 s',
          len(rows) > 1000 and worst < 0.002, f'max |err| {worst:.5f} of 0.5 ({len(rows)} blocks)')
suite(rep, 'test size', GT, mems, init, proc, (), True)

# ---------------- full-size build ----------------
fmems, fwords, finit, fproc = A.assemble('/aeria/verb/verb.asm', 'AERIA VERB FULL')
bad, dup = A.label_prefix_check('/aeria/verb/verb.asm')
rep.check('FULL: verb.asm assembles (round-trip + mpysu audit), labels clean, no rep',
          not bad and not dup and A.no_rep('/aeria/verb/verb.asm'), f'{len(fwords)} words')
nul = A.null_cost()
dp = knobs(127, 64, 127)
n = 1500 * 16
L, R, m = A.render('vb_full_dear', fmems, finit, fproc, dp, A.noise(n, 0.5), 1500, guard=GF['ring'], extra=FULLX)
base = [l for l in m['log'].splitlines() if 'base = Y:' in l]
rep.check('FULL: runs on allocator slot 5 (base Y:0x30000), dearest settings on 0.5 FS noise: bounded, '
          '-guard 32768 + -guard-shared clean', 'base = Y:0x30000' in ''.join(base) and A.peak(L + R) <= 1.0 + 1e-6
          and m['stray'] == 0 and m['clobber'] == 0,
          f"peak {A.peak(L + R):.3f} FS, stray {m['stray']}; cost mean "
          f"{m['mean_per_frame'] - nul['mean_per_frame']:.1f}, worst block "
          f"{m['max_per_frame'] - nul['max_per_frame']:.1f} instr/sample")
dry = list(dp)
dry[5] = 0
x = [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]
L, R, _ = A.render('vb_full_dry', fmems, finit, fproc, dry, x, 1500, extra=FULLX)
rep.check('FULL: MIX 0 is exact dry (full-range ramp)', L == x and R == x,
          f'max |err| {max(abs(a - b) for a, b in zip(L, x))} LSB')
# dirty: garbage r7 block and garbage poked over the ring (one word in every 61), silence in
worst = 0
for fill in (0x7fffff, 0xa5a5a5):
    fm = A.with_fill(fmems, fill, f'vbfull{fill:06x}')
    pk = ','.join(f'{0x30000 + a:x}={(0x123457 * (a + 1) + fill) & 0xffffff:x}' for a in range(0, 32768, 61))
    Lf, Rf, _ = A.render('vb_full_dirty', fm, finit, fproc, dp, None, 400, extra=FULLX + ['-pokey', pk])
    worst = max(worst, max(abs(v) for v in Lf[-600:] + Rf[-600:]))
rep.check('FULL: dirty state (garbage r7 block, garbage poked through the ring) on silence -> silent tail',
          worst == 0, f'worst tail {worst} LSB')
import random
rnd = random.Random(7)
ev = []
for b in range(0, 1500, 3):
    v = list(d)
    for j, p in enumerate(mod.params):
        if p.active:
            v[j] = rnd.randrange(128)
    ev.append((b, v))
wp, st_ = 0.0, 0
for split in (0, 7):
    La, Ra, ma = A.render(f'vb_full_auto{split}', fmems, finit, fproc, d, A.noise(n, 0.7, 3), 1500, events=ev,
                          split=split, guard=GF['ring'], extra=FULLX)
    wp = max(wp, A.peak(La + Ra))
    st_ += ma['stray'] + ma['clobber']
rep.check('FULL: random knob automation every 3 blocks, splits 0 and 7, guards: bounded, clean',
          wp <= 1.0 + 1e-6 and st_ == 0, f'peak {wp:.3f} FS, stray {st_}')
model_match(rep, 'FULL', GF, fmems, finit, fproc, FULLX, ((64, 64, 0, 0), (64, 64, 127, 1), (100, 0, 64, 0),
                                                          (30, 127, 64, 1)))
suite(rep, 'FULL', GF, fmems, finit, fproc, FULLX, GF['ring'])
# ---- stereo (STEREO.md): unchanged by the width pass, measured with the same check as the others (full-size build)
def wrun(xx, bl):
    return A.render('vb_full_w', fmems, finit, fproc, knobs(64, 64, 64), xx, bl, extra=FULLX)[:2]


A.stereo_report(rep, 'FULL: defaults (DECY 64, TONE 64, MOD 64), MIX 127', wrun, True, secs=4.0, info=True)
rep.note('not tested', 'hardware; core 1 placement (renders ran on core 0); the real 32K buffer base and its '
                       'allocator word; -dirty garbage fill does not reach Y:0x30000 (pokes used instead); '
                       'aliasing / fx:audit; parameter locks and LFO-driven knobs on the unit')
rep.save('/aeria/verb/test_results.md')
