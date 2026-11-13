"""ROSE gate: tools/run.sh python3 /aeria/rose/test.py"""
import json
import math
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
import aeria as A

T = json.loads(Path('/aeria/rose/rose_table.json').read_text())
TABLE = T['table']
LITERAL = '$fab1e0'


def assemble(asm, key):
    """aeria.assemble with the P table first, as dsp_packages places it (the literal = its base)."""
    text = Path(asm).read_text().replace(LITERAL, f'${A.ORG:x}')
    words, init, proc = A.build_bus.assemble(text, A.ORG + len(TABLE), key)
    words = list(TABLE) + list(words)

    def chunk(space, address, data):
        return A.struct.pack('<BII', space, address, len(data)) + A.struct.pack('<' + 'I' * len(data), *data)
    mems = []
    for c, mem in zip('AB', A.stock_mems()):
        blob = mem.read_bytes()
        blob = blob[:-9] + chunk(0, A.ORG, words)
        blob += chunk(1, 0x215 + A.FXID, [init]) + chunk(1, 0x235 + A.FXID, [proc]) + mem.read_bytes()[-9:]
        p = A.BUILD / f'{key.replace(" ", "_")}_{c}.mem'
        p.write_bytes(blob)
        mems.append(p)
    return mems, words, init, proc


_null_asm = A.assemble
A.assemble = lambda asm, key: _null_asm(asm, key) if 'NULL' in key else assemble(asm, key)

rep = A.Report('ROSE')
NAMES = ['ROOT', 'SHAP', 'FOLD', 'DECY', 'BRIT', 'MIX', 'SPRD', '', 'ROTA', '', 'TUNE', '']
DEAR = {'SHAP': 2, 'FOLD': 9, 'DECY': 127, 'BRIT': 127, 'ROTA': 127, 'SPRD': 127, 'MIX': 127, 'TUNE': 0}
mod, names, d, mems, init, proc, cost = A.standard(rep, '/aeria/rose/rose.asm', 'AERIA ROSE',
                                                   '/aeria/rose/manifest.py', DEAR)
assert names == NAMES, names
FS = 44100
FMAX = T['fmax']


def params(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


# ---- worst cost over trig splits (as the packager declares it) ----
null = A.null_cost()['max_per_frame']
worst = 0.0
rows = []
for sh in range(6):
    for split in (0, 1, 7):
        p = params(**dict(DEAR, SHAP=sh))
        _, _, m = A.render(f'rose_c{sh}_{split}', mems, init, proc, p, A.noise(1200 * 16, 0.5), 1200, split=split)
        c = m['max_per_frame'] - null
        worst = max(worst, c)
        rows.append(c)
rep.check('worst block over SHAP 0..5 x trig splits 0/1/7 (dsp_host meter minus null) <= 210 (was 199.2 before the 12 Nov quality pass)',
          worst <= 210, f'worst {worst:.1f} instr/sample (split 0 steady mean {cost[2]:.1f})')
WORST = worst


def freqs(tune, shape, fold, root_idx):
    """expected mode frequencies after the runtime octave fold above FMAX"""
    root = T['root_c1'] * 2 ** (root_idx / 12)
    out = []
    for r in T['sets'][f'{tune}/{shape}/{fold}']['ratios']:
        f = root * r
        while f > FMAX:
            f /= 2
        out.append(f)
    return out


def hann(x):
    n = len(x)
    return [v * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i, v in enumerate(x)]


def level(seg, f):
    return A.goertzel(seg, f)


def find_peak(seg, f, span):
    """best frequency within +-span cents (2-cent steps, then 0.5) -> (cents, level)"""
    best = max(range(-span, span + 1, 2), key=lambda c: level(seg, f * 2 ** (c / 1200)))
    best = max([best + h / 2 for h in range(-4, 5)], key=lambda c: level(seg, f * 2 ** (c / 1200)))
    return best, level(seg, f * 2 ** (best / 1200))


# ---- each SHAP produces its ratio set (impulse response, mono SPRD 0) ----
AT = 300 * 16
N = 1 << 16
cases = [('OPEN', s, n) for s in range(6) for n in (3, 5, 8, 12)] + [('CLSE', s, 7) for s in range(6)]
for tune, s, n in cases:
    shape = T['shapes'][s]
    root_idx = 24      # C3, 130.8 Hz
    p = params(ROOT=root_idx, SHAP=s, FOLD=n - 3, DECY=105, BRIT=64, MIX=127, SPRD=0, ROTA=0,
               TUNE=T['tunes'].index(tune))
    blocks = (AT + 2048 + N) // 16 + 8
    L, R, _ = A.render(f'rose_ir_{tune}_{s}_{n}', mems, init, proc, p, A.impulse(blocks * 16, at=AT, amp=0.9), blocks)
    seg = hann(L[AT + 2048: AT + 2048 + N])
    fs_ = sorted(freqs(tune, shape, n, root_idx))
    uniq = []
    for f in fs_:
        if not uniq or 1200 * math.log2(f / uniq[-1]) > 30:
            uniq.append(f)
    errs, lv = [], []
    for i, f in enumerate(uniq):
        nb = [abs(1200 * math.log2(f / g)) for j, g in enumerate(uniq) if j != i]
        span = int(min([40] + [x / 2 - 2 for x in nb]))
        c, l = find_peak(seg, f, max(span, 4))
        # modes closer than 30 cents were merged above: the peak may sit on the merged partner
        fp = f * 2 ** (c / 1200)
        c = min((1200 * math.log2(fp / g) for g in fs_), key=abs)
        errs.append(c)
        lv.append(l)
    # floor: midpoints between adjacent modes (gaps >= 1 semitone) and a few off-set probes
    probes = [math.sqrt(a * b) for a, b in zip(uniq, uniq[1:]) if 1200 * math.log2(b / a) > 150]
    floor = max([level(seg, f) for f in probes] or [1e-12])
    ok = all(abs(c) <= 15 for c in errs) and min(lv) > 4 * floor and A.peak(L + R) <= 1.0
    rep.check(f'{shape} FOLD {n} {tune}: impulse rings at its {len(uniq)} mode frequencies (C3 root)', ok,
              'cents ' + ' '.join(f'{c:+.0f}' for c in errs) +
              f'; weakest mode / loudest between-mode probe {A.db(min(lv) / floor):+.1f} dB' +
              f"; Hz {' '.join(f'{f:.0f}' for f in uniq)}")

# ---- stereo: SPRD 127 puts even modes left, odd modes right ----
p = params(ROOT=24, SHAP=4, FOLD=0, DECY=105, BRIT=64, MIX=127, SPRD=127, ROTA=0, TUNE=0)
blocks = (AT + 2048 + N) // 16 + 8
L, R, _ = A.render('rose_st', mems, init, proc, p, A.impulse(blocks * 16, at=AT, amp=0.9), blocks)
sl, sr = hann(L[AT + 2048: AT + 2048 + N]), hann(R[AT + 2048: AT + 2048 + N])
ratios = T['sets']['OPEN/ROSE/3']['ratios']
sep = []
for k, r in enumerate(ratios):
    f = T['root_c1'] * 4 * r
    a, b = level(sl, f), level(sr, f)
    sep.append(A.db(a / b) if k % 2 == 0 else A.db(b / a))
rep.check('SPRD 127: even modes in L, odd modes in R (ROSE FOLD 3)', min(sep) > 60,
          'own-side margin dB ' + ' '.join(f'{v:.0f}' for v in sep))
p[NAMES.index('SPRD')] = 0
L, R, _ = A.render('rose_mono', mems, init, proc, p, A.impulse(blocks * 16, at=AT, amp=0.9), blocks)
rep.check('SPRD 0: L == R', L == R, f'max |L-R| {max(abs(a - b) for a, b in zip(L, R))} LSB')

# ---- DECY lengthens the ring; BRIT tilts it ----
lv = []
for dk in (0, 64, 127):
    p = params(ROOT=24, SHAP=4, FOLD=5, DECY=dk, BRIT=64, MIX=127, SPRD=0, ROTA=0)
    L, R, _ = A.render(f'rose_dec_{dk}', mems, init, proc, p, A.impulse(4000 * 16, at=AT, amp=0.9), 4000)
    lv.append(A.rms(L[AT + 22050: AT + 22050 + 4410]))
rep.check('DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127)',
          lv[0] < lv[1] < lv[2] and lv[2] > 30 * max(lv[0], 1e-9), ' / '.join(f'{A.db(v):.1f}' for v in lv) + ' dBFS')
hi = []
for bk in (0, 64, 127):
    p = params(ROOT=12, SHAP=0, FOLD=1, DECY=100, BRIT=bk, MIX=127, SPRD=0, ROTA=0)
    L, R, _ = A.render(f'rose_brit_{bk}', mems, init, proc, p, A.impulse(4000 * 16, at=AT, amp=0.9), 4000)
    seg = hann(L[AT + 22050: AT + 22050 + 8192])
    fl = freqs('OPEN', 'HARM', 4, 12)
    hi.append(A.db(level(seg, fl[-1]) / max(level(seg, fl[0]), 1e-12)))
rep.check('BRIT tilts decay: top mode vs root 0.5 s after impulse rises with BRIT (0/64/127)',
          hi[0] < hi[1] < hi[2], ' / '.join(f'{v:+.1f}' for v in hi) + ' dB')

# ---- hot sine at a mode, longest decay, brightest: bounded, no DC, rings down without growing ----
for s in (0, 2, 4):
    p = params(ROOT=24, SHAP=s, FOLD=0, DECY=127, BRIT=127, MIX=127, SPRD=64, ROTA=0)
    f = freqs('OPEN', T['shapes'][s], 3, 24)[1]
    x = A.sine(f, 0.95, 441 * 160)
    L, R, _ = A.render(f'rose_hot_{s}', mems, init, proc, p, x, 9000)
    on, after1, after2 = L[-60000:-50000], L[-40000:-30000], L[-10000:]
    rep.check(f'{T["shapes"][s]}: 0.95 FS sine at mode 1 ({f:.0f} Hz) for 1.6 s, DECY 127 BRIT 127: '
              'bounded, no DC, decays after', A.peak(L + R) <= 1.0 and abs(A.dc(L[600 * 16:])) < 2e-3
              and A.rms(after2) < A.rms(after1) <= A.rms(on) * 1.01,
              f'peak {A.peak(L + R):.3f}, dc {A.dc(L[600 * 16:]):+.5f}, rms on/after/later '
              f'{A.db(A.rms(on)):.1f}/{A.db(A.rms(after1)):.1f}/{A.db(A.rms(after2)):.1f} dBFS')


# ---- smooth changes: SHAP / FOLD / ROOT / TUNE / DECY switches under a sustained input ----
def d2(x):
    return [abs(x[i] - 2 * x[i - 1] + x[i - 2]) for i in range(2, len(x))]


for knob, vals in (('SHAP', (0, 5, 2, 3, 1, 4)), ('FOLD', (0, 9, 3, 6)), ('ROOT', (0, 60, 24, 36)),
                   ('TUNE', (0, 1, 0, 1)), ('DECY', (0, 127, 20, 100)), ('ROTA', (0, 127, 0, 90))):
    base = params(ROOT=24, SHAP=4, FOLD=4, DECY=90, BRIT=64, MIX=127, SPRD=80, ROTA=0)
    ev, k = [], NAMES.index(knob)
    for i, v in enumerate(vals):
        p = list(base); p[k] = v
        ev.append((400 + 300 * i, p))
    blocks = 400 + 300 * len(vals)
    x = A.noise(blocks * 16, 0.05, 11)            # quiet noise: a click stands out
    L, R, _ = A.render(f'rose_sw_{knob}', mems, init, proc, base, x, blocks, events=ev)
    dd = d2(L)
    sw = max(max(dd[(b * 16) - 2: (b + 20) * 16]) for b, _ in ev)
    calm = max(max(dd[(b + 100) * 16: (b + 290) * 16]) for b, _ in ev)
    rep.check(f'{knob} switches under noise: no click (max 2nd difference, 20 blocks after each switch vs '
              'settled stretches)', sw <= 2.0 * calm and A.peak(L + R) <= 1.0,
              f'switch {A.db(sw / A.FS):.1f} dBFS vs settled {A.db(calm / A.FS):.1f} dBFS')

# ---- ROTA moves the light: a mode's level is modulated over time ----
p = params(ROOT=24, SHAP=4, FOLD=5, DECY=60, BRIT=64, MIX=127, SPRD=0, ROTA=127)
blocks = 8000
f = freqs('OPEN', 'ROSE', 8, 24)[2]
L, R, _ = A.render('rose_rota', mems, init, proc, p, A.sine(f, 0.1, blocks * 16), blocks)
env = [level(L[i: i + 2048], f) for i in range(20000, len(L) - 2048, 1024)]
p[NAMES.index('ROTA')] = 0
L0, _, _ = A.render('rose_rota0', mems, init, proc, p, A.sine(f, 0.1, blocks * 16), blocks)
env0 = [level(L0[i: i + 2048], f) for i in range(20000, len(L0) - 2048, 1024)]
sw = A.db(max(env) / min(env))
sw0 = A.db(max(env0) / min(env0))
rep.check('ROTA 127 sweeps a mode\'s level (light moving across the panes) vs ROTA 0',
          sw > sw0 + 10, f'mode 2 level swing {sw:.1f} dB (ROTA 0: {sw0:.1f} dB)')

# ---- tails end in exact silence (quality pass 12 Nov 2026): the recursion rounds (macr) and state words
# below 256 LSB move 4 LSB toward 0 (one mode per call); before, truncation left a stuck DC of -65..-84 dBFS
for decy, secs in ((30, 2.0), (60, 4.0), (90, 10.0)):
    p = params(ROOT=24, SHAP=0, FOLD=0, DECY=decy, BRIT=64, MIX=127, SPRD=90, ROTA=0)
    blocks = int(secs * FS / 16)
    nb = int(0.3 * FS)
    x = A.sine(130.813, 0.3, nb) + [0] * (blocks * 16 - nb)
    L, R, _ = A.render(f'rose_tail{decy}', mems, init, proc, p, x, blocks)
    lastnz = max([i for i in range(len(L)) if L[i] or R[i]] or [0])
    t60 = 0.05 * 240 ** (decy / 127)
    rep.check(f'DECY {decy} (T60 {t60:.2f} s): sine at ROOT 0.3 s then silence -> exact silence (no stuck DC, no limit cycle)',
              lastnz < len(L) - FS // 2, f'last nonzero sample at {lastnz / FS:.2f} s of {len(L) / FS:.1f} s')

# ---- knob jumps (quality pass): SPRD steps a sample (was 1/8 a call), pane gains 1/32 a call (was 1/16)
def click_metric(y, at, span=700):
    k = (2 * math.pi * 1000 / FS) ** 2
    w = 0.0
    for i in range(at, at + span):
        pk = max(abs(v) for v in y[i - 22:i + 24]) or 1
        w = max(w, abs(y[i + 1] - 2 * y[i] + y[i - 1]) / pk / k)
    return w


for kn, lim in (('SPRD', 1.3), ('ROTA', 3.5)):
    x = A.sine(1000, 0.1, 900 * 16)
    ev = []
    for i, v in enumerate((0, 127, 0, 127)):
        q_ = params(MIX=127); q_[NAMES.index(kn)] = v; ev.append((200 + 150 * i, q_))
    L, R, _ = A.render(f'rose_j{kn}', mems, init, proc, params(MIX=127), x, 900, events=ev)
    pre = max(click_metric(c, (200 + 150 * i) * 16 - 700) for i in range(4) for c in (L, R))
    post = max(click_metric(c, (200 + 150 * i) * 16) for i in range(4) for c in (L, R))
    rep.check(f'knob jump {kn} 0<->127 on a 1 kHz sine: 2nd-difference/peak after the jump (clean sine = 1) <= {lim}',
              post <= lim, f'before {pre:.2f}, after {post:.2f} (before the pass: SPRD 5.8, ROTA 5.2)')

# ---- stereo (STEREO.md): unchanged by the width pass, measured with the same check as the others
def wrun(sprd):
    def run(xx, bl):
        return A.render(f'rose_w{sprd}', mems, init, proc, params(MIX=127, SPRD=sprd), xx, bl)[:2]
    return run


A.stereo_report(rep, 'defaults (ROSE 8-fold, SPRD 90), MIX 127, sine at ROOT (C3)', wrun(90), True, secs=4.0, fsine=130.813, info=True)
A.stereo_report(rep, 'SPRD 127', wrun(127), True, secs=4.0, fsine=130.813, info=True)
A.stereo_report(rep, 'SPRD 0', wrun(0), False, secs=3.0, fsine=130.813, info=True)
rep.note('cost', f'static {cost[0]} cycles/sample (sample loop); dsp_host mean {cost[2]:.1f}, '
         f'worst block {cost[1]:.1f} (split 0), worst over splits 0/1/7 and all SHAP {WORST:.1f}')
rep.save('/aeria/rose/test_results.md')
Path('/aeria/rose/.cost.json').write_text(json.dumps(dict(static=cost[0], mean=cost[2], worst=WORST)))
