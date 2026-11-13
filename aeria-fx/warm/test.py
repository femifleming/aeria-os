"""WARM gate: tools/run.sh python3 /aeria/warm/test.py   (writes warm/test_results.md)"""
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/warm')
import aeria as A
import gen as G

FSR = 44100
Path('/aeria/warm/warm.asm').write_text(G.build())
rep = A.Report('WARM')
DEAR = {'HEAT': 127, 'TONE': 100, 'BIAS': 127, 'SHAP': 1, 'LEVL': 127, 'MIX': 64}
mod, names, d, mems, init, proc, cost = A.standard(rep, '/aeria/warm/warm.asm', 'AERIA WARM', '/aeria/warm/manifest.py', DEAR)
NAMES = ['HEAT', 'TONE', 'BIAS', 'SHAP', 'LEVL', 'MIX', '', '', '', '', '', '']
assert names == NAMES, names
ST = 256 * 16


def P(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('wm_' + tag, mems, init, proc, p, x, blocks, guard=True, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0
    return L, R, m


def model(x, heat, tone, bias_v, shape, levl):
    """float: curve, 10 Hz DC blocker, tone one-pole, level (the knobs settled)"""
    cdc, ct = G.coef(G.DC_HZ), (None if tone >= 127 else G.coef(G.tone_hz(tone)))
    lp = tl = 0.0
    out = []
    for v in x:
        y = G.static(v / A.FS, heat, bias_v, shape) / 2
        lp += cdc * (y - lp)
        z = y - lp
        if ct is not None:
            tl += ct * (z - tl)
            z = tl
        out.append(2 * z * levl / 64)
    return out


# ---- 1. the transfer: the DSP against the float model, sample by sample ------------------------------------------
rows, ok = [], True
for heat, bias_v, shape, tone in ((0, 0, 0, 127), (64, 0, 0, 127), (127, 0, 0, 127), (127, 0, 1, 127), (80, 127, 0, 127),
                                  (127, 60, 1, 60), (30, 127, 1, 100)):
    blocks = int(1.5 * FSR / 16)
    n = blocks * 16
    x = [round(0.9 * A.FS * math.sin(2 * math.pi * 220 * i / FSR) * min(1, i / 4000)) for i in range(n)]
    p = P(HEAT=heat, BIAS=bias_v, SHAP=shape, TONE=tone, LEVL=40, MIX=127)
    L, R, _ = run(f'tr{heat}_{bias_v}_{shape}', p, x, blocks)
    M = model(x, heat, tone, bias_v, shape, 40)
    pk64 = max(abs(v) for v in model(x, heat, tone, bias_v, shape, 64)[ST:])
    err = max(abs(a - b * A.FS) for a, b in zip(L[FSR // 2:], M[FSR // 2:])) / A.FS
    ok &= err < 1e-3 and L == R
    rows.append(f'HEAT {heat} BIAS {bias_v} {G.SHAPES[shape][0]} TONE {tone}: max |DSP - model| {A.db(err):.0f} dBFS, peak {A.peak(L[ST:]):.3f}')
rep.check('transfer: 0.9 FS 220 Hz sine (faded in), the DSP against the float curve + 10 Hz DC blocker + tone one-pole, sample '
          'by sample from 0.5 s (the glides settled; LEVL 40, -4.1 dB, so nothing reaches the output limit): within -60 dBFS, both sides alike', ok, '; '.join(rows))
tab = []
for shape in (0, 1):
    for heat in (0, 64, 127):
        tab.append(f'{G.SHAPES[shape][0]} HEAT {heat} (+{24 * heat / 127:.1f} dB): ' + ' '.join(
            f'{xv:.2f}->{G.static(xv, heat, 0, shape):.3f}' for xv in (0.05, 0.1, 0.25, 0.5, 0.75, 1.0)))
rep.note('the static curve (BIAS 0, LEVL 64: input FS -> output FS)', '; '.join(tab))

# ---- 2. THD at several drives (1 kHz, 0.5 FS), against the model --------------------------------------------------
rows, ok = [], True
f0 = 1000.0
blocks = int(1.0 * FSR / 16)
n = blocks * 16
x = A.sine(f0, 0.5, n)
seg = slice(n - 44100 // 2, n)


def harmonics(y):
    y = y[seg] if isinstance(y[0], int) else y
    return [A.goertzel(y, f0 * k) if isinstance(y[0], int) else 0 for k in range(1, 10)]


for shape in (0, 1):
    for heat, bias_v in ((0, 0), (32, 0), (64, 0), (96, 0), (127, 0), (64, 64), (127, 127)):
        L, R, _ = run(f'thd{shape}_{heat}_{bias_v}', P(HEAT=heat, BIAS=bias_v, SHAP=shape, TONE=127, LEVL=64, MIX=127), x, blocks)
        M = [round(v * A.FS) for v in model(x, heat, 127, bias_v, shape, 64)]
        hd = [A.goertzel(L[seg], f0 * k) for k in range(1, 10)]
        hm = [A.goertzel(M[seg], f0 * k) for k in range(1, 10)]
        thd = math.sqrt(sum(h * h for h in hd[1:])) / hd[0]
        thm = math.sqrt(sum(h * h for h in hm[1:])) / hm[0]
        ok &= abs(A.db(thd) - A.db(thm)) < 0.5 or thd < 1e-4
        rows.append(f'{G.SHAPES[shape][0]} HEAT {heat} BIAS {bias_v}: THD {100 * thd:.2f} % (model {100 * thm:.2f} %), '
                    f'H2 {A.db(hd[1] / hd[0]):.0f} H3 {A.db(hd[2] / hd[0]):.0f} H5 {A.db(hd[4] / hd[0]):.0f} dBc, '
                    f'fundamental {A.db(hd[0]):.1f} dBFS')
rep.check('THD of a 0.5 FS 1 kHz sine (harmonics 2..9) at HEAT 0/32/64/96/127, BIAS 0/64/127, both curves: within 0.5 dB '
          'of the float model', ok, '; '.join(rows))

# ---- 3. aliasing: 5 kHz sine, full drive (no oversampling) ---------------------------------------------------------
rows = []
f0 = 5000.0
for shape in (0, 1):
    for amp in (0.5, 0.1):
        L, R, _ = run(f'alias{shape}{amp}', P(HEAT=127, SHAP=shape, TONE=127, LEVL=64, MIX=127), A.sine(f0, amp, n), blocks)
        y = L[seg]
        fund = A.goertzel(y, f0)
        harm, alias = [], []
        for k in range(2, 40):
            f = (k * f0) % FSR
            f = FSR - f if f > FSR / 2 else f
            lv = A.goertzel(y, f) / fund
            (harm if k * f0 < FSR / 2 else alias).append((k, f, lv))
        ka, fa, la = max(alias, key=lambda t: t[2])
        tot = math.sqrt(sum(l * l for _, _, l in alias))
        rows.append(f'{G.SHAPES[shape][0]}, {amp} FS: harmonics below Nyquist up to {A.db(max(l for *_, l in harm)):.0f} dBc; '
                    f'aliases total {A.db(tot):.0f} dBc, the largest {A.db(la):.0f} dBc (k = {ka} folded to {fa:.0f} Hz)')
rep.note('aliasing, 5 kHz sine at HEAT 127 (+24 dB), TONE open, no oversampling (Goertzel on the folded harmonic '
         'frequencies k x 5 kHz, k = 2..39)', '; '.join(rows))

# ---- 4. MIX 0 exact, silence, zipper ---------------------------------------------------------------------------------
blocks = int(1.0 * FSR / 16)
n = blocks * 16
rnd = random.Random(3)
xl = [round(-A.FS + i * 2 * A.FS / (n - 1)) for i in range(n)]
xr = [rnd.randrange(-A.FS, A.FS) for _ in range(n)]
xs = []
for a, b in zip(xl, xr):
    xs += [a, b]
ev = [(k * 20, P(MIX=0, HEAT=rnd.randrange(128), BIAS=rnd.randrange(128), SHAP=rnd.randrange(2), TONE=rnd.randrange(128),
                 LEVL=127)) for k in range(1, blocks // 20)]
L, R, _ = run('dry', P(MIX=0, HEAT=127, BIAS=127, LEVL=127), xs, blocks, events=ev, stereo_in=True)
e = max(max(abs(a - b) for a, b in zip(L, xl)), max(abs(a - b) for a, b in zip(R, xr)))
rep.check('MIX 0 is bit-exact dry (stereo ramp / random, every other knob moving)', e == 0, f'max |err| {e} LSB')
rows, ok = [], True
for p in (P(**DEAR), P(HEAT=127, BIAS=127, SHAP=0, TONE=127, MIX=127, LEVL=127)):
    L, R, _ = run('sil', p, None, blocks)
    ok &= not any(L) and not any(R)
x = A.noise(FSR // 2, 0.8, 4) + [0] * (n * 4 - FSR // 2)
L, R, _ = run('tail', P(HEAT=127, BIAS=127, SHAP=1, TONE=20, MIX=127, LEVL=127), x, blocks * 4)
last = max((i for i in range(len(L)) if L[i] or R[i]), default=0)
ok &= last < len(L) - FSR
rows.append(f'after 0.5 s of noise at HEAT 127 BIAS 127 TONE 20: exactly 0 from {(last - FSR // 2) / FSR:.2f} s after it')
rep.check('silence in, silence out (BIAS set: f(b) is taken off exactly); after an input stops the DC blocker and the '
          'lowpass reach exactly 0', ok, '; '.join(rows))


def hf(y, a=ST):
    y = y[a:]
    e3 = sum((y[i] - 3 * y[i - 1] + 3 * y[i - 2] - y[i - 3]) ** 2 for i in range(3, len(y)))
    return 10 * math.log10(max(e3, 1e-9) / (sum(v * v for v in y) or 1))


def glide_model(x, p0, events):
    """the left side in float with the generator's glides: HEAT (as g/s) and LEVL a sample at a time (2^-9), MIX
    (2^-7), BIAS and the TONE coefficient once a call (2^-5), f(b) once a call; knob moves land at their call"""
    ev = dict(events)
    p = list(p0)

    def tg(p):
        sh = p[3]
        s = G.SHAPES[sh][1]
        tc = 1.0 if p[1] >= 127 else G.coef(G.tone_hz(p[1]))
        m = 1.0 if p[5] >= 127 else p[5] / 128
        return G.gain(p[0]) / s, tc, G.bias(p[2]), sh, p[4] / 64, m, p[1] >= 127
    gt, tct, bt, sh, lt, mt, op = tg(p)
    gc, tcc, bc, lc, mc = gt, tct, bt, lt, mt
    cdc = G.coef(G.DC_HZ)
    lp = tl = 0.0
    out = []
    for i, v in enumerate(x):
        if i % 16 == 0:
            if i // 16 in ev:
                p = ev[i // 16]
            gt, tct, bt, sh, lt, mt, op = tg(p)
            tcc += (tct - tcc) / 2 ** G.CGLIDE
            bc += (bt - bc) / 2 ** G.CGLIDE
            fb = G.curve(bc, sh)
        gc += (gt - gc) / 2 ** G.GLIDE
        lc += (lt - lc) / 2 ** G.GLIDE
        mc += (mt - mc) / 128
        xv = v / A.FS
        y = (G.curve(gc * xv + bc, sh) - fb) / 2
        lp += cdc * (y - lp)
        z = y - lp
        if op:
            tl = z
        else:
            tl += tcc * (z - tl)
            z = tl
        w = 2 * z * lc
        out.append(A.FS * (xv + mc * (w - xv)))
    return out


blocks = int(3 * FSR / 16)
n = blocks * 16
x = A.sine(220, 0.3, n)
rows, ok = [], True
for knob in ('HEAT', 'BIAS', 'LEVL', 'TONE', 'MIX'):
    base = dict(HEAT=64, TONE=100)
    ev = [(k * 45, P(**dict(base, **{knob: rnd.randrange(65 if knob == 'LEVL' else 128)}))) for k in range(1, blocks // 45)]
    L, _, _ = run('zip', P(**base), x, blocks, events=ev)
    M = glide_model(x, P(**base), ev)
    h, hm = hf(L, FSR // 2), hf(M, FSR // 2)
    diff = [a - b for a, b in zip(L, M)]
    hd = hf(diff, FSR // 2) + 10 * math.log10(sum(v * v for v in diff[FSR // 2:]) / sum(v * v for v in L[FSR // 2:]))
    ok &= h < hm + 3
    rows.append(f'{knob}: HF {h:.1f} dB re the signal (the float model gliding the same way, sample by sample: {hm:.1f}; '
                f'HF of the difference {hd:.1f} dB)')
L0, _, _ = run('zip0', P(HEAT=64, TONE=100), x, blocks)
rep.check('knob jumps at random every 45 calls on a 0.3 FS 220 Hz sine (HEAT 64 TONE 100): the HF (third-difference energy, '
          'where clicks show) of the DSP within 3 dB of a float model gliding the same way sample by sample (11.6 ms '
          'glides; LEVL jumps kept to 0..64, 0 dB at most, so the output limit stays out of it; knobs still: ' + f'{hf(L0, FSR // 2):.1f} dB)', ok, '; '.join(rows))

# ---- 5. cost -----------------------------------------------------------------------------------------------------------
nul = A.null_cost()
rows, worst = [], cost[1]
for shape in (0, 1):
    for tone in (100, 127):
        _, _, m = run('cost', P(**dict(DEAR, SHAP=shape, TONE=tone)), A.noise(1500 * 16, 0.5), 1500)
        c = m['max_per_frame'] - nul['max_per_frame']
        worst = max(worst, c)
        rows.append(f'{G.SHAPES[shape][0]} TONE {tone}: worst block {c:.1f}')
rep.note('cost (dsp_host instructions a sample)', '; '.join(rows) + f'; worst {worst:.1f}; static count {cost[0]}')
rep.note('size', f'{len(A.assemble("/aeria/warm/warm.asm", "AERIA WARM")[1])} words of P; r7 block {G.USED} words; no buffer')
rep.save('/aeria/warm/test_results.md')
