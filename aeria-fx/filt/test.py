"""FILT gate: tools/run.sh python3 /aeria/filt/test.py   (writes filt/test_results.md)"""
import cmath
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/filt')
import aeria as A
import gen as G

FSR = 44100
FS = A.FS
Path('/aeria/filt/filt.asm').write_text(G.build())
rep = A.Report('FILT')
NAMES = ['CUT', 'RES', 'MODE', 'SKEW', 'LRNZ', 'MIX', 'LZSP', 'CHAS', 'SHAP', '', '', '']
DEAR = {'CUT': 100, 'RES': 127, 'MODE': 64, 'SKEW': 90, 'LRNZ': 127, 'MIX': 64}
mod, names, d, mems, init, proc, cost = A.standard(rep, '/aeria/filt/filt.asm', 'AERIA FILT',
                                                   '/aeria/filt/manifest.py', DEAR)
assert names == NAMES, names


def P(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def lzword(x):
    return f'{max(-(1 << 23), min((1 << 23) - 1, round(x * (1 << 23)))) & 0xffffff:x}'


def run(tag, p, x, blocks, lz=None, **kw):
    extra = ()
    if lz is not None:
        extra = ('-pword', f'0:6={lzword(lz[0])},0:7={lzword(lz[1])}')
    L, R, m = A.render('fl_' + tag, mems, init, proc, p, x, blocks, extra=extra, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def dft(h, f):
    w = cmath.exp(-2j * math.pi * f / FSR)
    z, acc = 1, 0
    for v in h:
        acc += v * z
        z *= w
    return abs(acc)


def ir(tag, p, n, lz=None, amp=0.5, at=256):
    blocks = (n + at) // 16 + 1
    L, R, _ = run(tag, p, A.impulse(blocks * 16, at=at, amp=amp), blocks, lz=lz)
    return [v / (amp * FS) for v in L[at:at + n]], [v / (amp * FS) for v in R[at:at + n]]


def dbs(x):
    return 20 * math.log10(max(x, 1e-12))


# ---- 1. magnitude response against the plugin's formulas, both sides ------------------------------------------
FREQS = [25, 40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 14000, 18000]
CASES = [  # (label, knobs, lorenz words)
    ('LP, RES 0, CUT 64', dict(CUT=64, RES=0, MODE=0), None),
    ('LP, RES 100, CUT 64', dict(CUT=64, RES=100, MODE=0), None),
    ('BP, RES 100, CUT 64', dict(CUT=64, RES=100, MODE=42), None),
    ('HP, RES 100, CUT 64', dict(CUT=64, RES=100, MODE=84), None),
    ('NOTCH, RES 100, CUT 64', dict(CUT=64, RES=100, MODE=126), None),
    ('LP>BP half, RES 127, CUT 100, SKEW 96 (+2 oct right)', dict(CUT=100, RES=127, MODE=21, SKEW=96), None),
    ('BP>HP 30 %, RES 120, CUT 30, SKEW 40 (-1.5 oct right)', dict(CUT=30, RES=120, MODE=55, SKEW=40), None),
    ('HP>NOTCH 60 %, RES 64, CUT 110', dict(CUT=110, RES=64, MODE=110), None),
    ('LP, RES 127, CUT 0 (20 Hz)', dict(CUT=0, RES=127, MODE=0), None),
    ('HP, RES 127, CUT 127 (20 kHz)', dict(CUT=127, RES=127, MODE=84), None),
    ('BP, RES 110, CUT 64, LRNZ 127, lorenz L +0.5 R -0.8', dict(CUT=64, RES=110, MODE=42, LRNZ=127), (0.5, -0.8)),
    ('LP, RES 90, CUT 70, LRNZ 64, lorenz L -1 R +1, SKEW 70', dict(CUT=70, RES=90, MODE=0, LRNZ=64, SKEW=70), (-1.0, 0.99999)),
]
rows, worst, worsta = [], 0.0, 0.0
for lbl, kn, lz in CASES:
    p = P(**kn)
    lzl, lzr = lz or (0.0, 0.0)
    fl = G.side_hz(kn['CUT'], kn.get('SKEW', 64), kn.get('LRNZ', 0), lzl)
    fr = G.side_hz(kn['CUT'], kn.get('SKEW', 64), kn.get('LRNZ', 0), lzr, right=True)
    n = 131072 if min(fl, fr) < 100 else 32768
    hL, hR = ir(f'r{len(rows)}', p, n, lz=lz)
    errs, aerr = [], 0.0
    for h, fc in ((hL, fl), (hR, fr)):
        fs_ = FREQS + [fc, fc * 2 ** (1 / 24), fc * 2 ** (-1 / 24)]
        pk = max(G.response(fc, p[1], p[2], f) for f in fs_)
        for f in fs_:
            ref = G.response(fc, p[1], p[2], f)
            got = dft(h, f)
            aerr = max(aerr, abs(got - ref) / pk)
            if ref > pk * 10 ** (-30 / 20):
                errs.append(abs(dbs(got) - dbs(ref)))
    e = max(errs)
    worst = max(worst, e)
    worsta = max(worsta, aerr)
    at_l = dbs(dft(hL, fl))
    at_r = dbs(dft(hR, fr))
    rows.append(f'{lbl}: L {fl:.0f} Hz, R {fr:.0f} Hz, at the cutoff L {at_l:+.2f} dB R {at_r:+.2f} dB '
                f'(c = {G.comp(G.kh_of(p[1]), G.morph_of(p[2])):.3f}), max |error| {e:.3f} dB where within 30 dB of the peak, '
                f'largest |H - Href| {A.db(aerr):.0f} dB re the peak')
rep.check('magnitude response (impulse response, DFT at 16 frequencies 25 Hz..18 kHz, each cutoff and +-1/24 octave, both sides) '
          'against the plugin\'s TPT SVF (bilinear, prewarped) x the peak gain c: within 0.2 dB wherever the reference is '
          'within 30 dB of its peak, and |H - Href| under -40 dB re the peak everywhere',
          worst < 0.2 and worsta < 10 ** (-40 / 20), f'worst {worst:.3f} dB, {A.db(worsta):.1f} dB re the peak; ' + '; '.join(rows))

# ---- 1b. pure-tone sweep (steady sines), both sides ------------------------------------------------------------------
rows, worst = [], 0.0
for lbl, kn in (('LP RES 100 CUT 64 SKEW 80', dict(CUT=64, RES=100, MODE=0, SKEW=80)),
                ('BP RES 127 CUT 90 SKEW 48', dict(CUT=90, RES=127, MODE=42, SKEW=48)),
                ('NOTCH RES 50 CUT 50', dict(CUT=50, RES=50, MODE=126))):
    p = P(**kn)
    fl, fr = G.side_hz(kn['CUT'], kn['SKEW'] if 'SKEW' in kn else 64), G.side_hz(kn['CUT'], kn.get('SKEW', 64), right=True)
    tones = sorted({round(f) for f in (fl / 2, fl / 1.25, fl, fl * 1.25, fl * 2, fr / 2, fr, fr * 2, 100, 3000)})
    seg = 8192
    settle = 16384
    x = []
    for f in tones:
        x += A.sine(f, 0.5, settle + seg)
    blocks = len(x) // 16 + 1
    L, R, _ = run(f'sw{len(rows)}', p, x, blocks)
    errs = []
    for i, f in enumerate(tones):
        a0 = i * (settle + seg) + settle
        for out, fc in ((L, fl), (R, fr)):
            got = A.goertzel(out[a0:a0 + seg], f) / 0.5
            ref = G.response(fc, p[1], p[2], f)
            if ref > 10 ** (-50 / 20):
                errs.append(abs(dbs(got) - dbs(ref)))
    worst = max(worst, max(errs))
    rows.append(f'{lbl}: {len(tones)} tones {tones[0]}..{tones[-1]} Hz, L {fl:.0f} R {fr:.0f} Hz, max |error| {max(errs):.3f} dB')
rep.check('pure-tone sweep (-6 dBFS sines, 0.37 s settle, Goertzel over 8192 samples), both sides, against the reference: '
          'within 0.1 dB (where above -50 dB)', worst < 0.1, '; '.join(rows))

# ---- 2. MODE morph is continuous ---------------------------------------------------------------------------------------
# (a) the model's gain at the cutoff and an octave either side, MODE 0..127 one step at a time: no jumps
jump, fine = 0.0, 0.0
for res in (0, 64, 127):
    for step, store in ((1, 0), (1 / 64, 1)):
        prev = None
        mode = 0.0
        while mode <= 127:
            g3 = [G.response(1000, res, mode, f) for f in (500, 1000, 2000)]
            if prev:
                j = max(abs(a - b) for a, b in zip(g3, prev))
                if store:
                    fine = max(fine, j)
                else:
                    jump = max(jump, j)
            prev = g3
            mode += step
# (b) the DSP, MODE swept 0 -> 127 one step every 4 calls on a 1 kHz sine at the cutoff (and back at once 127 -> 0)
fq = 1000.0
cut = round(127 * math.log2(fq / 20) / G.OCT)
fq = G.side_hz(cut)
sl = 2 * math.pi * fq / FSR


def d2ratio(y):
    pk = max(abs(v) for v in y) or 1
    return max(abs(a - 2 * b + c) for a, b, c in zip(y[2:], y[1:], y)) / (sl * sl * pk)


rows, okm = [], True
for res in (30, 100, 127):
    blocks = 3 * FSR // 16
    ev = [(0, P(CUT=cut, RES=res, MODE=0))] + [(200 + 4 * m, P(CUT=cut, RES=res, MODE=m)) for m in range(1, 128)]
    ev += [(1600, P(CUT=cut, RES=res, MODE=0))]
    L, R, _ = run(f'morph{res}', P(CUT=cut, RES=res, MODE=0), A.sine(fq, 0.5, blocks * 16), blocks, events=ev)
    r = d2ratio(L[100 * 16:1600 * 16])
    rj = d2ratio(L[1600 * 16 - 2:])
    env = [max(abs(v) for v in L[i:i + 64]) for i in range(3200, 1600 * 16, 64)]
    envj = max(abs(a - b) for a, b in zip(env, env[1:])) / FS
    okm &= r < 1.3 and rj < 2.0 and envj < 0.1
    rows.append(f'RES {res}: second difference {r:.3f} (sweep) / {rj:.3f} (jump) x a clean sine\'s, largest change of the 1.45 ms envelope '
                f'{envj:.4f} FS, level at LP/BP/HP/NOTCH {A.db(max(abs(v) for v in L[190 * 16:200 * 16]) / FS):.1f} / '
                f'{A.db(max(abs(v) for v in L[(200 + 168) * 16:(200 + 172) * 16]) / FS):.1f} / '
                f'{A.db(max(abs(v) for v in L[(200 + 336) * 16:(200 + 340) * 16]) / FS):.1f} / '
                f'{A.db(max(abs(v) for v in L[(200 + 504) * 16:(200 + 508) * 16]) / FS):.1f} dBFS')
rep.check('MODE morph is continuous: the reference\'s |H| at f/2, f, 2f, MODE swept in 1/64 knob steps, never moves '
          'more than 0.02 a step (RES 0/64/127; the plugin\'s linear morph is steep at high Q next to NOTCH); '
          'the DSP swept 0 -> 127 a step every 1.45 ms and dropped 127 -> 0 at once on a 1 kHz sine at the cutoff: '
          'no click (second difference within 1.3 x a clean sine\'s on the sweep, 2 x on the jump), the 1.45 ms envelope '
          'never moves more than 0.1 FS (at RES 127 the plugin\'s morph drops the Q 25 HP peak to the notch within ~4 MODE steps)',
          fine < 0.02 and okm, f'reference: largest change {fine:.4f} a 1/64 step ({jump:.3f} a whole step, '
          f'at RES 127 MODE 125 -> 126); ' + '; '.join(rows))

# ---- 3. RES max: never blows up, never clips ----------------------------------------------------------------------------
rows, ok3 = [], True
pk_noise6, pk_sine6, pk_noise0, sat0 = 0.0, 0.0, 0.0, 0
secs = 3
blocks = secs * FSR // 16
n = blocks * 16
for mode in (0, 42, 84, 126):
    for cut in range(0, 128, 9):
        p = P(CUT=cut, RES=127, MODE=mode)
        fc = G.side_hz(cut)
        for tag, x in (('n6', A.noise(n, 0.5, 5)), ('s6', A.sine(min(fc, 20000), 0.5, n)), ('n0', A.noise(n, 1.0, 6))):
            L, R, m = run(f'st{mode}_{cut}_{tag}', p, x, blocks)
            pk = A.peak(L + R)
            sat = sum(1 for v in L + R if abs(v) >= FS - 1)
            if tag == 'n6':
                pk_noise6 = max(pk_noise6, pk)
            elif tag == 's6':
                pk_sine6 = max(pk_sine6, pk)
            else:
                pk_noise0 = max(pk_noise0, pk)
                sat0 += sat
            if tag != 'n0':
                ok3 &= pk < 1.0 and sat == 0
            tail_rms = A.rms(L[-FSR // 2:])
            ok3 &= tail_rms < 1.0
# the decay after a resonant ring: silence in after the noise -> exact silence
dec = []
for cut in (0, 40, 64, 100, 127):
    blocks = 12 * FSR // 16
    x = A.noise(2 * FSR, 1.0, 8) + [0] * (blocks * 16 - 2 * FSR)
    L, R, _ = run(f'dec{cut}', P(CUT=cut, RES=127, MODE=0), x, blocks)
    zero_at = next((i for i in range(len(L) - 1, 2 * FSR, -1) if L[i] != 0 or R[i] != 0), 2 * FSR) + 1
    dec.append(f'CUT {cut}: exactly 0 from {(zero_at - 2 * FSR) / FSR * 1000:.0f} ms after the input stops')
    ok3 &= zero_at < len(L) - 2 * FSR
rep.check('RES 127 at CUT 0, 9, .. 126 and MODE LP / BP / HP / NOTCH, 3 s each: -6 dBFS noise and a -6 dBFS sine at '
          'the cutoff stay under full scale with no sample at the rail; full-scale noise bounded; after full-scale '
          'noise stops at RES 127 the output reaches exact silence',
          ok3, f'peak: -6 dBFS noise {pk_noise6:.3f} FS ({A.db(pk_noise6):.1f} dBFS), -6 dBFS sine at the cutoff '
               f'{pk_sine6:.3f} FS ({A.db(pk_sine6):.1f} dBFS); full-scale noise {pk_noise0:.3f} FS with {sat0} samples at '
               f'the rail in {4 * 15} renders of 3 s; ' + '; '.join(dec))

# ---- 4. zipper: CUT stepped every 723 samples (61 Hz) against an ideal filter gliding sample by sample -------------------
TICK = 723


def model_glide(x, targets, res, mode, tick=TICK):
    """The float reference with the DSP's cutoff glide (two one-poles, 1/8 every 48 samples) run SAMPLE BY SAMPLE:
    the cutoff and every coefficient move continuously, so whatever HF it has is the filter's own, not zipper."""
    al = 1 - (1 - 2 ** -G.GLIDE) ** (1 / 48)
    o1 = o2 = G.cut_oct(targets[0])
    kh = G.kh_of(res)
    k = 2 * kh
    m = G.morph_of(mode)
    w = G.weights(m)
    c = G.comp(kh, m)
    ic1 = ic2 = 0.0
    out = []
    for i, v in enumerate(x):
        t = G.cut_oct(targets[min(len(targets) - 1, (i // 16) * 16 // tick)])
        o1 += al * (t - o1)
        o2 += al * (o1 - o2)
        g = math.tan(math.pi * G.hz_of_oct(o2) / FSR)
        a1 = 1 / (1 + g * (g + k))
        a2 = g * a1
        a3 = g * a2
        v /= FS
        v3 = v - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2 * v1 - ic1
        ic2 = 2 * v2 - ic2
        lp, bp, hp = v2, k * v1, v - k * v1 - v2
        out.append(FS * c * (w[0] * lp + w[1] * bp + w[2] * hp + w[3] * (lp + hp)))
    return out


def hf(y, a=4096, b=-64):
    """energy of the third difference re the signal's, dB (gain (2 sin(pi f/fs))^3: 150 Hz -86 dB, 10 kHz +7 dB)"""
    y = y[a:b]
    e3 = sum((y[i] - 3 * y[i - 1] + 3 * y[i - 2] - y[i - 3]) ** 2 for i in range(3, len(y)))
    return 10 * math.log10(max(e3, 1e-9) / (sum(v * v for v in y) or 1))


rnd = random.Random(61)
steps = 120
jumps = [rnd.randrange(10, 118) for _ in range(steps)]
lorenz = [round(64 + 38 * math.sin(2 * math.pi * 0.7 * i / 61) + 14 * math.sin(2 * math.pi * 1.9 * i / 61 + 1)) for i in range(steps)]
n = steps * TICK
blocks = n // 16
rows, ok4 = [], True
for lbl, tg, kn, x in (('random jumps 10..117, LP RES 0, 150 Hz sine', jumps, dict(RES=0, MODE=0), A.sine(150, 0.5, n)),
                       ('random jumps, LP RES 64, 150 Hz sine', jumps, dict(RES=64, MODE=0), A.sine(150, 0.5, n)),
                       ('random jumps, BP RES 100, 150 Hz + 1.1 kHz', jumps, dict(RES=100, MODE=42),
                        [a + b for a, b in zip(A.sine(150, 0.25, n), A.sine(1100, 0.25, n))]),
                       ('random jumps, LP RES 127, 100 Hz sine', jumps, dict(RES=127, MODE=0), A.sine(100, 0.5, n)),
                       ('Lorenz-like path (two sines, 0.7 and 1.9 Hz, +-52 steps), HP RES 90, 300 Hz sine', lorenz,
                        dict(RES=90, MODE=84), A.sine(300, 0.5, n)),
                       ('Lorenz-like path, LP RES 30, noise', lorenz, dict(RES=30, MODE=0), A.noise(n, 0.5, 4))):
    ev = [(round(i * TICK / 16), P(CUT=t, **kn)) for i, t in enumerate(tg)]
    Ls, Rs, _ = run('zst', P(CUT=tg[0], **kn), x, blocks, events=ev)
    M = model_glide(x, tg, kn['RES'], kn['MODE'])
    hd, hm = hf(Ls), hf(M)
    diff = [a_ - b_ for a_, b_ in zip(Ls, M)]
    hdiff = hf(diff) + 10 * math.log10(sum(v * v for v in diff[4096:-64]) / sum(v * v for v in Ls[4096:-64]))
    ok4 &= hd < hm + 6 or hd < -90
    rows.append(f'{lbl}: HF {hd:.1f} dB re the signal (ideal filter {hm:.1f}), HF of the difference {hdiff:.1f} dB')
rep.check(f'zipper: CUT moved every {TICK} samples (61 Hz, {steps} moves) -- random jumps, and a smooth Lorenz-like path in '
          'knob steps -- against the float reference gliding the same way SAMPLE BY SAMPLE (no steps at all): the HF '
          '(third-difference energy, where clicks show) of the DSP is within 6 dB of the ideal filter\'s, or under -90 dB',
          ok4, '; '.join(rows))

# ---- 5. silence, DC, MIX -------------------------------------------------------------------------------------------------
blocks = 2 * FSR // 16
L, R, _ = run('sil', P(**DEAR), None, blocks)
rep.check('silence in, silence out (dearest settings, 2 s): every sample 0', not any(L) and not any(R))
rows, ok5 = [], True
dcv = round(0.25 * FS)
for mode in (0, 21, 42, 84, 105, 126):
    for res in (0, 127):
        blocks = 4 * FSR // 16
        p = P(CUT=40, RES=res, MODE=mode)
        L, R, _ = run(f'dc{mode}_{res}', p, [dcv] * (blocks * 16), blocks)
        got = sum(L[-4410:]) / 4410
        w = G.weights(G.morph_of(mode))
        exp = G.comp(G.kh_of(res), G.morph_of(mode)) * (w[0] + w[3]) * dcv
        err = abs(got - exp)
        ok5 &= err <= 64 and L[-1] == L[-2] and R[-4410:] == L[-4410:]
        rows.append(f'MODE {mode} RES {res}: {got / FS:+.6f} FS (want {exp / FS:+.6f}, |err| {err:.1f} LSB)')
rep.check('DC 0.25 FS for 4 s at CUT 40 (157 Hz): out = c (LP + NOTCH weights) x DC (LP and NOTCH pass it, BP and HP '
          'block it), steady, both sides alike, '
          'within 64 LSB', ok5, '; '.join(rows))
rows = []
for kn in (dict(MIX=0, CUT=0, RES=127, MODE=0), dict(MIX=0, CUT=127, RES=127, MODE=85, LRNZ=127)):
    x = [round(-FS + i * 2 * FS / (88192 - 1)) for i in range(88192)]
    L, R, _ = run('dry', P(**kn), x, 88192 // 16, lz=(0.7, -0.7))
    rows.append(max(abs(a - b) for a, b in zip(L + R, x + x)))
rep.check('MIX 0 is bit-exact dry (full-range ramp, RES 127, CUT 0 and 127, Lorenz words set)', max(rows) == 0,
          f'max |err| {max(rows)} LSB')

# ---- 6. the Lorenz words and SKEW move each side alone ---------------------------------------------------------------------
rows, ok6 = [], True
for lzl, lzr, lr, sk in ((0.5, 0.0, 127, 64), (0.0, -0.5, 127, 64), (1.0, -1.0, 64, 64), (0.0, 0.0, 0, 112), (0.9, 0.9, 0, 64)):
    p = P(CUT=64, RES=100, MODE=42, LRNZ=lr, SKEW=sk)
    hL, hR = ir(f'lz{len(rows)}', p, 32768, lz=(lzl, lzr))
    want = []
    for h, side in ((hL, False), (hR, True)):
        fc = G.side_hz(64, sk, lr, lzr if side else lzl, right=side)
        grid = [fc * 2 ** (i / 96) for i in range(-48, 49)]
        best = max(grid, key=lambda f: dft(h, f))
        want.append((fc, best))
        ok6 &= abs(math.log2(best / fc)) < 1.5 / 96
    rows.append(f'LRNZ {lr} SKEW {sk} lorenz L {lzl:+.1f} R {lzr:+.1f}: peaks L {want[0][1]:.0f} Hz (want {want[0][0]:.0f}), '
                f'R {want[1][1]:.0f} Hz (want {want[1][0]:.0f})')
rep.check('Lorenz words (r6+6 left, r6+7 right, signed Q23) and SKEW: each side\'s BP peak where 20 Hz x 1000^(CUT/127) x '
          '2^(3.5 LRNZ/127 lz) (x 2^((SKEW - 64)/16) on the right) puts it, within 1/64 octave; LRNZ 0 ignores the words',
          ok6, '; '.join(rows))

rep.note('size', f'{len(A.assemble("/aeria/filt/filt.asm", "AERIA FILT")[1])} words of P; r7 block {G.USED} words; no Y, no X buffer')
rep.note('cost', f'dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f} instructions a sample at the dearest settings')
rep.save('/aeria/filt/test_results.md')
