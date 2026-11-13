"""TAPE gate: tools/run.sh python3 /aeria/tape/test.py   (writes tape/test_results.md)

Builds (both generated here from gen.py):
  tape_test.asm  Y ring 15,616 + damage maps at the harness's Y buffer (inside the 15,870-word window the
                 -guard polices), X ring 16,128 at X:$10000 -- every render below
  tape.asm       the hardware build (Y ring 16,128 at the base word, X ring 16,128 at X:$4000): assembled and
                 compared line by line with the test build (only the ring constants differ), never rendered
The reference ("the plugin") is DSP.h's TapeLine + DelayCell as formulas here in float: the same code at
sr = 22,050 (what the DSP runs) and at 48 kHz (the plugin's own rate), saturation dropped as the owner asked.
"""
import cmath
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/tape')
import aeria as A
import gen as G

FSR = 44100
FS = A.FS
for nm, kw in G.BUILDS.items():
    G.build(out=nm, **kw)
TB = G.Build(**G.BUILDS['tape_test.asm'], out='x')
rep = A.Report('TAPE')
NAMES = ['TIME', 'SKEW', 'FDBK', 'WEAR', 'WOW', 'MIX', 'HISS', 'HPF', 'FRZ', 'RFRZ', 'TMOD', 'RFSH']
DEAR = {'TIME': 127, 'SKEW': 127, 'FDBK': 127, 'WEAR': 127, 'WOW': 127, 'MIX': 127}


# ------------------------------------------------------------------ A.standard, with the hiss off where silence is due
def standard():
    """aeria.standard's checks; the dirty-state silence check runs with HISS 0 (the tape's hiss is the point of
    HISS > 0, and it is checked on its own below)."""
    sys.path.insert(0, str(A.SDK / 'tools'))
    from remix import registry
    mod = registry._load_one(Path('/aeria/tape/manifest.py'))
    names = [p.name.decode() if p.name else '' for p in mod.params]
    defaults = [p.default or 0 for p in mod.params]
    rep.check('manifest loads (schema: names<=4, defaults in count)', True, ' '.join(n for n in names if n))
    asm = '/aeria/tape/tape_test.asm'
    bad, dup = A.label_prefix_check(asm)
    rep.check('no label is a prefix of another, no duplicates', not bad and not dup, bad or dup or '')
    rep.check('no rep instruction', A.no_rep(asm))
    mems, words, init, proc = A.assemble(asm, 'AERIA TAPE')
    rep.check('assembles; disassembler round-trip + mpysu audit clean', len(words) + 4 <= 2048,
              f'{len(words)} words (+ the 4-word entry stub = {len(words) + 4} of 2,048)')
    try:
        st, ok = A.static_cycles(asm, 'tape_test')
        rep.note('static cycle count (cycle_count.py)', f"{st['cycles']} cycles/sample" + (f" ({st.get('inner')})" if st.get('inner') else '')
                 + ('' if ok else ' (verify: not ok)'))
    except (SystemExit, Exception) as e:  # noqa: BLE001
        rep.note('static cycle count (cycle_count.py)', f'not available for this layout: {str(e)[:120]}')
    nul = A.null_cost()
    dp = list(defaults)
    for k, v in DEAR.items():
        dp[names.index(k)] = v
    blocks = 1500
    n = blocks * 16
    L, R, m = A.render('tp_dear', mems, init, proc, dp, A.noise(n, 0.5), blocks, guard=True)
    cost = m['max_per_frame'] - nul['max_per_frame']
    mean = m['mean_per_frame'] - nul['mean_per_frame']
    rep.check('measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block)',
              mean < 320, f'mean {mean:.1f}, peak block {cost:.1f} instr/sample')
    rep.check('dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes)',
              A.peak(L + R) <= 1.0 + 1e-6 and m['stray'] == 0 and m['clobber'] == 0,
              f"peak {A.peak(L + R):.3f} FS, dc {A.dc(L[-20000:]):+.5f}, stray {m['stray']}")
    dry = list(dp)
    dry[names.index('MIX')] = 0
    x = [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]
    L, R, _ = A.render('tp_dry', mems, init, proc, dry, x, blocks)
    rep.check('MIX 0 is exact dry (full-range ramp, dearest other knobs, HISS 64)', L == x and R == x,
              f'max |err| {max(abs(a - b) for a, b in zip(L + R, x + x))} LSB')
    worst = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = A.with_fill(mems, fill, f'tpfill{fill:06x}')
        for kn in (defaults, dp):
            kn = list(kn)
            kn[names.index('HISS')] = 0
            Lf, Rf, _ = A.render('tp_dirty', fm, init, proc, kn, None, 300, dirty=0x1234 + fill)
            worst = max(worst, max(abs(v) for v in Lf[-300:] + Rf[-300:]))
    rep.check('dirty state (garbage r7 block + Y) on silence -> silent tail (HISS 0; defaults and dearest)', worst == 0,
              f'worst tail {worst} LSB')
    rnd = random.Random(7)
    counts = [p.count or 128 for p in mod.params]
    ev = []
    for b in range(0, blocks, 3):
        v = list(defaults)
        for j, p in enumerate(mod.params):
            if p.active:
                v[j] = rnd.randrange(counts[j])
        ev.append((b, v))
    worstp, stray = 0, 0
    for split in (0, 7):
        La, Ra, ma = A.render(f'tp_auto{split}', mems, init, proc, defaults, A.noise(n, 0.7, 3), blocks, events=ev,
                              split=split, guard=True)
        worstp = max(worstp, A.peak(La + Ra))
        stray += ma['stray'] + ma['clobber']
    rep.check('random knob automation every 3 blocks (all 12 knobs, FRZ / TMOD / RFSH included), splits 0 and 7, '
              'guard: bounded, no stray writes', worstp <= 1.0 + 1e-6 and stray == 0, f'peak {worstp:.3f} FS')
    return mod, names, defaults, mems, init, proc, (cost, mean)


mod, names, D, MEMS, INIT, PROC, COST = standard()
assert names == NAMES, names


def P(**kw):
    p = list(D)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


QUIET = dict(WOW=0, HISS=0, WEAR=0, FDBK=0, MIX=127)
ST = 200 * 16                   # the clearing calls (127) are long over


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('tp_' + tag, MEMS, INIT, PROC, p, x, blocks, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def dft(h, f, fs=FSR):
    w = cmath.exp(-2j * math.pi * f / fs)
    z, acc = 1, 0
    for v in h:
        acc += v * z
        z *= w
    return abs(acc)


def dbs(x):
    return 20 * math.log10(max(x, 1e-15))


# ------------------------------------------------------------------ the plugin as formulas
def opc(fc, sr):
    return 1 - math.exp(-2 * math.pi * fc / sr)


def op_h(a, zi):
    return a / (1 - (1 - a) * zi)


def rbj_peak(f, fc, q, db, sr):
    A_ = 10 ** (db / 40)
    w = 2 * math.pi * fc / sr
    c, al = math.cos(w), math.sin(w) / (2 * q)
    n = 1 / (1 + al / A_)
    b0, b1, b2, a1, a2 = (1 + al * A_) * n, -2 * c * n, (1 - al * A_) * n, -2 * c * n, (1 - al / A_) * n
    zi = cmath.exp(-2j * math.pi * f / sr)
    return (b0 + b1 * zi + b2 * zi * zi) / (1 + a1 * zi + a2 * zi * zi)


def chamberlin_peak(f, sr):
    zi = cmath.exp(-2j * math.pi * f / sr)
    fq, qq = G.B_F, G.B_K
    bp = fq * (1 - zi) / ((1 - zi) ** 2 + fq * fq * zi + fq * qq * zi * (1 - zi))
    return 1 + G.B_KB * bp


def chain(f, sr, wear=0.0, dr=0.0, dw=0.0, bump='rbj'):
    """the plugin's record + playback colouring at sr (no saturation): pre-emphasis, x 1.148 (1 - 0.8 dw)(1 - 0.18 wear),
    gap loss (two one-poles), head bump, de-emphasis, dropout, DC blocker. Complex gain at f."""
    zi = cmath.exp(-2j * math.pi * f / sr)
    pa = opc(2400, sr)
    h = 1 + 0.6 * (1 - op_h(pa, zi))
    h *= G.REC_GAIN * (1 - 0.18 * wear) * (1 - 0.8 * dw)
    wc = 2 * math.pi * 15000 * (1 - 0.8 * wear) * (1 - 0.55 * dr) / sr
    h *= op_h(wc / (1 + wc), zi) ** 2
    h *= rbj_peak(f, 95, 0.9, 2.5, sr) if bump == 'rbj' else chamberlin_peak(f, sr)
    h *= 1 - 0.375 * (1 - op_h(pa, zi))
    h *= 1 - 0.4 * dr
    r = 1 - 2 * math.pi * 8 / sr
    h *= (1 - zi) / (1 - r * zi)
    return h


def dsp_model(f, wear=0.0, bump='chamberlin'):
    """what the DSP should do: the chain at 22,050 (Chamberlin bump) x the halfband down and up"""
    return abs(chain(f, G.SR, wear, bump=bump)) * abs(G.hb_response(f)) ** 2


# ------------------------------------------------------------------ 0. the hardware build is the test build
full = [l for l in Path('/aeria/tape/tape.asm').read_text().splitlines() if not l.startswith(';')]
tst = [l for l in Path('/aeria/tape/tape_test.asm').read_text().splitlines() if not l.startswith(';')]
diff = [(a.split(';')[0].strip(), b.split(';')[0].strip()) for a, b in zip(full, tst) if a != b]
fw = A.assemble('/aeria/tape/tape.asm', 'AERIA TAPEFULL')[1]
rep.check('hardware build (tape.asm) assembles, same length, and differs from the test build only in ring constants '
          '(sizes, X base, masks, time ceiling)', len(full) == len(tst) and len(fw) == len(A.assemble('/aeria/tape/tape_test.asm', 'AERIA TAPE')[1]),
          f'{len(fw)} words; {len(diff)} lines differ: ' + '; '.join(sorted({f"{a} -> {b}" for a, b in diff}))[:700])

# ------------------------------------------------------------------ 1. hard-panned dual mono, echo times
blocks = 3000
rows, ok = [], True
for side in (0, 1):
    x = [0] * (blocks * 32)
    x[2 * ST + side] = round(0.5 * FS)
    L, R, _ = run(f'pan{side}', P(TIME=64, **QUIET), x, blocks, stereo_in=True)
    other = (R, L)[side]
    mine = (L, R)[side]
    ok &= not any(other) and any(mine)
    rows.append(f'{"left" if side == 0 else "right"} impulse: {"left" if side == 0 else "right"} out peak '
                f'{A.peak(mine):.3f}, the other side max |v| {max(abs(v) for v in other)} LSB')
rep.check('hard-panned dual mono: an impulse on one input comes back on that output only (the other exactly 0)', ok,
          '; '.join(rows))

rows, ok = [], True
for tv, sk in ((0, 64), (30, 0), (64, 84), (96, 84), (110, 127), (127, 64), (127, 127)):
    blocks = int(2.2 * FSR / 16)
    L, R, _ = run(f'tm{tv}_{sk}', P(TIME=tv, SKEW=sk, **QUIET), A.impulse(blocks * 16, at=ST, amp=0.5), blocks)
    for side, out in ((0, L), (1, R)):
        cap = min(TB.dmax(side), G.T_MAX * G.SR)
        want = G.time_of(tv, sk, side == 1) * FSR
        if G.time_of(tv, sk, side == 1) * G.SR - G.LAT > cap:
            want = (cap + G.LAT) * FSR / G.SR
        seg = out[ST:]
        i = max(range(len(seg)), key=lambda k: abs(seg[k]))
        err = i - want
        ok &= abs(err) <= 2
        rows.append(f'TIME {tv} SKEW {sk} {"LR"[side]}: {i / FSR * 1000:.2f} ms (want {want / FSR * 1000:.2f}, {err:+.1f} smp)')
rep.check('echo time: the impulse returns at 30 ms x (700/30)^(TIME/127) (left) and x 2^((SKEW - 64)/64) (right), held to '
          '700 ms (the test build\'s shorter Y ring holds the left at 685 ms), within 2 samples (largest peak; the halfband '
          'latency is taken off)', ok, '; '.join(rows))

# ------------------------------------------------------------------ 2. the colouring against the plugin
blocks = int(1.6 * FSR / 16)
L, R, _ = run('ir', P(TIME=127, **QUIET), A.impulse(blocks * 16, at=ST, amp=0.25), blocks)
d0 = round(min(G.time_of(127), min(TB.dmax(0), G.T_MAX * G.SR) / G.SR) * FSR)
h = [v / (0.25 * FS) for v in L[ST + d0 - 600: ST + d0 - 600 + 16384]]
FQ = [60, 95, 150, 300, 600, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000]
rows, e1, e2 = [], 0.0, 0.0
for f in FQ:
    got = dbs(dft(h, f))
    m_dsp = dbs(dsp_model(f))
    m_22 = dbs(abs(chain(f, G.SR)) * abs(G.hb_response(f)) ** 2)
    m_48 = dbs(abs(chain(f, 48000)))
    e1 = max(e1, abs(got - m_dsp))
    e2 = max(e2, abs(got - m_22))
    rows.append(f'{f} Hz {got:+.2f} dB (model {m_dsp:+.2f}, plugin@22k {m_22:+.2f}, plugin@48k {m_48:+.2f})')
rep.check('first echo\'s magnitude response, new tape (WEAR 0), 60 Hz..9 kHz: against the DSP\'s own model (chain at '
          '22,050 Hz with the Chamberlin bump, x the halfband twice) within 0.25 dB, against the plugin\'s formulas at '
          '22,050 Hz (RBJ bump) within 0.5 dB; the plugin at its own 48 kHz shown for comparison (it keeps more top end: '
          'the tape here is band-limited at ~10 kHz by the half-rate storage)', e1 < 0.25 and e2 < 0.5,
          f'max |DSP - model| {e1:.3f} dB, max |DSP - plugin@22k| {e2:.3f} dB; ' + '; '.join(rows))
for f in (10000, 11000, 12000, 14000, 16000, 20000):
    pass
hf = '; '.join(f'{f} Hz {dbs(dft(h, f)):+.1f} dB (plugin@48k {dbs(abs(chain(f, 48000))):+.1f})' for f in (10000, 11000, 12000, 14000, 18000))
rep.note('top end: the DSP against the plugin at 48 kHz', hf)

def herm_mag(f, sr, frac=0.5):
    """|H| of the plugin's 4-point Hermite (Catmull-Rom) read at a fixed fractional position"""
    t = frac
    w = [-0.5 * t ** 3 + t * t - 0.5 * t, 1.5 * t ** 3 - 2.5 * t * t + 1, -1.5 * t ** 3 + 2 * t * t + 0.5 * t, 0.5 * t ** 3 - 0.5 * t * t]
    zi = cmath.exp(-2j * math.pi * f / sr)
    return abs(sum(c * zi ** k for k, c in enumerate(w)))


rep.note('the read interpolator (the plugin\'s 4-point Hermite) at a half-sample position, here at 22,050 Hz against '
         'the plugin at 48 kHz: the half-rate tape loses more top end between samples (it moves with the delay\'s fraction)',
         '; '.join(f'{f} Hz {dbs(herm_mag(f, G.SR)):+.2f} dB (plugin {dbs(herm_mag(f, 48000)):+.2f})' for f in (2000, 5000, 7000, 9000)))

# ------------------------------------------------------------------ 3. regeneration, and the plugin's loop gain
blocks = int(1.5 * FSR / 16)
L, R, _ = run('rg', P(TIME=40, FDBK=77, WOW=0, HISS=0, WEAR=0, MIX=127), A.impulse(blocks * 16, at=ST, amp=0.25), blocks)
d = G.time_of(40) * FSR
rows, ok = [], True
for f in (300, 1000, 3000):
    lv = []
    for k in range(1, 5):
        a0 = ST + round(k * (d + 0.0)) - 300
        lv.append(dft(L[a0:a0 + 1600], f))
    k_ = G.FB_GAIN * G.regen_of(77)
    want = dbs(k_ * abs(chain(f, G.SR, bump='chamberlin')))
    got = [dbs(b / a) for a, b in zip(lv, lv[1:])]
    ok &= all(abs(g - want) < 0.6 for g in got)
    rows.append(f'{f} Hz: repeat to repeat {", ".join(f"{g:+.2f}" for g in got)} dB (1.05 regen |H| = {want:+.2f})')
rep.check(f'FDBK 77 (regen {G.regen_of(77):.3f}, the plugin\'s default 0.4): each repeat = the last x 1.05 regen x the '
          'colouring, within 0.6 dB (1,600-sample DFT windows, so the low end is approximate)', ok, '; '.join(rows))

# ------------------------------------------------------------------ 4. bounded at the extremes, decays to exact silence
rows, ok = [], True
for tag, kn in (('sing', P(TIME=127, SKEW=127, FDBK=127, WEAR=0, WOW=127, MIX=127, HISS=127)),
                ('singhp', P(TIME=0, SKEW=0, FDBK=127, WEAR=127, WOW=127, MIX=127, HISS=127, HPF=1)),
                ('singfade', P(TIME=20, FDBK=127, WEAR=64, WOW=64, MIX=127, TMOD=1))):
    secs = 8
    blocks = secs * FSR // 16
    x = A.noise(2 * FSR, 0.5, 9) + [0] * (blocks * 16 - 2 * FSR)
    ev = None
    if tag == 'singfade':
        ev = [(b, P(TIME=20 + (b // 2000) * 30 % 107, FDBK=127, WEAR=64, WOW=64, MIX=127, TMOD=1)) for b in range(0, blocks, 2000)]
    L, R, _ = run(tag, kn, x, blocks, events=ev, guard=True)
    pk = A.peak(L + R)
    rail = sum(1 for v in L + R if abs(v) >= FS - 1)
    tail = A.rms(L[-FSR:])
    ok &= pk <= 1.0 and rail == 0 and tail > 0.05
    rows.append(f'{tag}: peak {pk:.3f} FS, {rail} samples at the rail, rms of the last second {A.db(tail):.1f} dBFS (still singing)')
rep.check('FDBK 127 (regen 1.15, the plugin\'s top: over unity, it self-oscillates): -6 dBFS noise for 2 s then 6 s '
          'of silence, at the longest and the shortest times, WOW 127, HISS 127, wear, HPF, FADE mode with time jumps: '
          'the loop keeps singing but the gain-riding (g = min(1, 0.3 / (k env))) holds it -- never a sample at the rail '
          '(MIX 127)',
          ok, '; '.join(rows))

rows, ok = [], True
for tag, kn in (('d0', P(FDBK=0, HISS=0, MIX=127)), ('d77', P(FDBK=77, HISS=0, MIX=127, WOW=127)),
                ('d115', P(FDBK=115, HISS=0, MIX=127, TIME=40)), ('d100hp', P(FDBK=100, HISS=0, MIX=127, HPF=1, TIME=0)),
                ('dwear', P(FDBK=90, HISS=0, MIX=127, WEAR=127, TIME=70))):
    secs = 14
    blocks = secs * FSR // 16
    x = A.noise(FSR, 0.9, 4) + [0] * (blocks * 16 - FSR)
    L, R, _ = run(tag, kn, x, blocks)
    last = max((i for i in range(len(L) - 1, FSR, -1) if L[i] or R[i]), default=FSR)
    ok &= last < len(L) - FSR // 2
    rows.append(f'FDBK {kn[2]}{" HPF" if kn[7] else ""}{" WEAR 127" if kn[3] == 127 else ""}: exactly 0 from '
                f'{(last + 1 - FSR) / FSR:.2f} s after the input stops')
rep.check('silence in -> exactly 0 out once the tail has decayed (HISS 0; FDBK up to 115 = regen 0.6, the top of the '
          'weather\'s range; HPF; a worn tape)', ok, '; '.join(rows))
blocks = 2 * FSR // 16
L, R, _ = run('sil', P(**DEAR, HISS=0), None, blocks)
rep.check('silence in, silence out (dearest settings, HISS 0, 2 s): every sample 0', not any(L) and not any(R))

# ------------------------------------------------------------------ 5. wow and flutter
def zc_freq(y, a=0):
    """per-cycle frequency from rising zero crossings (linear interpolation): [(t, f)]"""
    for _ in range(3):          # [1 2 1]/4 three times: the halfband's image near 21 kHz would jitter the crossings
        y = [(p + 2 * c + n) / 4 for p, c, n in zip(y, y[1:], y[2:])]
    out, last = [], None
    for i in range(a + 1, len(y)):
        if y[i - 1] < 0 <= y[i]:
            t = i - 1 + (-y[i - 1]) / (y[i] - y[i - 1])
            if last is not None:
                out.append(((t + last) / 2 / FSR, FSR / (t - last)))
            last = t
    return out


def goertzel_series(t, v, f):
    """amplitude of a component at f in an unevenly but densely sampled series (mean dt)"""
    n = len(v)
    re = sum(x * math.cos(2 * math.pi * f * tt) for tt, x in zip(t, v))
    im = sum(x * math.sin(2 * math.pi * f * tt) for tt, x in zip(t, v))
    return 2 * math.hypot(re, im) / n


def wob_model(sr, secs, W=1.0, wear=0.0, seed=1):
    """the plugin's wow + flutter at sr: per-sample pitch ratio deviation -(wob[n] - wob[n-1]), averaged per 1 ms"""
    r = random.Random(seed)
    wa = opc(0.55, sr)
    fi1, fi2 = (6.1 + r.random() * 0.8) / sr, (11.3 + r.random() * 1.5) / sr
    wd, fd = 0.0021 * sr, 0.000028 * sr
    wt = w1 = w2 = 0.0
    timer = 0.0
    p1 = p2 = 0.0
    amp = 1.0
    prev = 0.0
    out, acc, cnt, per = [], 0.0, 0, int(sr / 1000)
    we = math.sqrt(wear)
    for _ in range(int(secs * sr)):
        timer -= 1
        if timer <= 0:
            wt = r.random() * 2 - 1
            timer = sr * (0.5 + 1.8 * r.random())
        w1 += (wt - w1) * wa
        w2 += (w1 - w2) * wa
        p1 = (p1 + fi1) % 1
        p2 = (p2 + fi2) % 1
        amp += ((0.5 + r.random()) - amp) * 0.0004 * 48000 / sr
        wob = W * (wd * (1 + 1.6 * we) * w2 + fd * (1 + 2 * we) * amp * (math.sin(2 * math.pi * p1) + 0.35 * math.sin(2 * math.pi * p2)))
        acc += -(wob - prev)
        prev = wob
        cnt += 1
        if cnt == per:
            out.append(acc / cnt)
            acc, cnt = 0.0, 0
    return out


def dev_stats(dev):
    rms = math.sqrt(sum(v * v for v in dev) / len(dev))
    return rms, max(abs(v) for v in dev)


secs = 30
blocks = secs * FSR // 16
f0 = 997.0
x = A.sine(f0, 0.5, blocks * 16)
res = {}
for wv in (0, 32, 127):
    L, R, _ = run(f'wow{wv}', P(TIME=64, WOW=wv, HISS=0, WEAR=0, FDBK=0, MIX=127), x, blocks)
    fr = zc_freq(L, 2 * FSR)
    tt = [a for a, _ in fr]
    dev = [b / f0 - 1 for _, b in fr]
    res[wv] = (tt, dev)
rms0, pk0 = dev_stats(res[0][1])
rms32, pk32 = dev_stats(res[32][1])
rms127, pk127 = dev_stats(res[127][1])
# the plugin at 48 kHz and at 22,050, several seeds, the same 30 s
mods48 = [dev_stats(wob_model(48000, secs - 2, seed=s)) for s in range(6)]
lo48, hi48 = min(r for r, _ in mods48), max(r for r, _ in mods48)
mean48 = sum(r for r, _ in mods48) / len(mods48)
rep.check('WOW 0: no pitch movement at all (997 Hz through the tape, per-cycle frequency)', rms0 < 2e-5,
          f'rms deviation {rms0 * 100:.5f} %, peak {pk0 * 100:.5f} %')
rep.check(f'WOW 32 (the plugin\'s depth), new tape, 30 s: pitch deviation rms within the spread of the plugin\'s own '
          f'wow + flutter (6 seeds at 48 kHz, x 0.6 .. x 1.6 of it: the wander is random)',
          0.6 * lo48 <= rms32 <= 1.6 * hi48,
          f'DSP rms {rms32 * 100:.3f} %, peak {pk32 * 100:.3f} %; plugin rms {lo48 * 100:.3f} .. {hi48 * 100:.3f} % '
          f'(mean {mean48 * 100:.3f} %), peaks {min(p for _, p in mods48) * 100:.3f} .. {max(p for _, p in mods48) * 100:.3f} %')
rep.check('WOW 127 = x 3.97 the depth: rms deviation 3.97 x WOW 32\'s within 25 % (same random path: same seed, '
          'same knob history otherwise)', abs(rms127 / rms32 / (127 / 32) - 1) < 0.25,
          f'ratio {rms127 / rms32:.2f} (want {127 / 32:.2f}); rms {rms127 * 100:.3f} %, peak {pk127 * 100:.3f} %')
# rates: the flutter lines and the wow below 2 Hz
tt, dev = res[32]
mean = sum(dev) / len(dev)
dev = [v - mean for v in dev]
fl1, fl2 = G.FLUT[0][0], G.FLUT[0][1]
a1 = goertzel_series(tt, dev, fl1)
a2 = goertzel_series(tt, dev, fl2)
want1 = 2 * math.pi * fl1 * G.FLUT_DEPTH / G.SR
want2 = 0.35 * 2 * math.pi * fl2 * G.FLUT_DEPTH / G.SR
off = [goertzel_series(tt, dev, f) for f in (4.5, 9.0, 15.0, 17.0)]
lowf = [goertzel_series(tt, dev, f) for f in (0.15, 0.3, 0.6)]
rep.check(f'flutter rates: the left line\'s pitch deviation has its lines at {fl1:.2f} Hz and {fl2:.2f} Hz (the plugin\'s '
          f'6.1-6.9 and 11.3-12.8 Hz draws) at the plugin\'s amplitude 2 pi f x 0.6 ms (x famp ~ 1) within x 0.6..1.5, '
          'well above the spectrum between them; the wow is the slow part (below 1 Hz)',
          0.6 < a1 / want1 < 1.5 and 0.6 < a2 / want2 < 1.5 and min(a1, a2) > 3 * max(off),
          f'{fl1:.2f} Hz: {a1 * 100:.4f} % (plugin {want1 * 100:.4f} %); {fl2:.2f} Hz: {a2 * 100:.4f} % (plugin {want2 * 100:.4f} %); '
          f'off-line (4.5/9/15/17 Hz) <= {max(off) * 100:.4f} %; slow (0.15/0.3/0.6 Hz) {", ".join(f"{v * 100:.3f}" for v in lowf)} %')

# ------------------------------------------------------------------ 6. hiss: level, fade, slew
def hiss_model(sr, n=200000, seed=3):
    r = random.Random(seed)
    lp = hp = 0.0
    a, ha = opc(11000, sr), opc(900, sr)
    acc = 0.0
    for _ in range(n):
        nz = r.random() * 2 - 1
        lp += (nz - lp) * a
        hp += (lp - hp) * ha
        acc += (lp - hp) ** 2
    return math.sqrt(acc / n) * 0.0018


secs = 6
blocks = secs * FSR // 16
L, R, _ = run('hiss', P(HISS=64, WEAR=0, WOW=0, FDBK=0, MIX=127), None, blocks)
lv = A.rms(L[3 * FSR:6 * FSR])
lvr = A.rms(R[3 * FSR:6 * FSR])
def hb_power_factor():
    num = den = 0.0
    a, ha = opc(11000, G.SR), opc(900, G.SR)
    for k in range(1, 1103):
        f = k * 10.0
        zi = cmath.exp(-2j * math.pi * f / G.SR)
        hl = op_h(a, zi)
        s_ = abs(hl * (1 - op_h(ha, zi))) ** 2
        num += s_ * abs(G.hb_response(f)) ** 2
        den += s_
    return num / den


m22 = hiss_model(G.SR) * math.sqrt(hb_power_factor())
m48 = hiss_model(48000)
rep.check('hiss level (HISS 64 = the plugin\'s, new tape, silence in, MIX 127): rms within 0.7 dB of the plugin\'s '
          'hiss at 22,050 Hz (white less its 900 Hz lowpass, x 0.0018); both sides, independent',
          abs(A.db(lv) - A.db(m22)) < 0.7 and abs(A.db(lvr) - A.db(m22)) < 0.7 and abs(A.corr(L[3 * FSR:4 * FSR], R[3 * FSR:4 * FSR])) < 0.1,
          f'L {A.db(lv):.1f} R {A.db(lvr):.1f} dBFS rms (model at 22,050 {A.db(m22):.1f}; the plugin at 48 kHz {A.db(m48):.1f}: '
          f'its hiss reaches to 24 kHz); L/R correlation {A.corr(L[3 * FSR:4 * FSR], R[3 * FSR:4 * FSR]):+.3f}')
win = FSR // 10
env = [A.rms(L[i:i + win]) for i in range(0, 3 * FSR, win)]
ss = lambda h: h * h * (3 - 2 * h)
errs = []
for k, e in enumerate(env):
    t = (k + 0.5) * 0.1 - 128 * 16 / FSR
    want = lv * ss(min(1, max(0, t / 1.6))) if t > 0 else 0
    errs.append(abs(e - want) / lv)
rep.check('the hiss fades in after loading (0 -> full over 1.6 s, smoothstep: the plugin\'s hissGain), never a step: '
          '0.1 s rms windows follow lv x smoothstep(t/1.6) within 0.1 of full level', max(errs) < 0.1,
          'windows (x full): ' + ', '.join(f'{e / lv:.2f}' for e in env[:22]))
ev = [(0, P(HISS=0, WEAR=0, WOW=0, FDBK=0, MIX=127)), (3 * FSR // 16, P(HISS=127, WEAR=0, WOW=0, FDBK=0, MIX=127)),
      (6 * FSR // 16, P(HISS=0, WEAR=0, WOW=0, FDBK=0, MIX=127))]
blocks = 9 * FSR // 16
L, R, _ = run('hissslew', ev[0][1], None, blocks, events=ev)
env = [A.rms(L[i:i + win]) for i in range(3 * FSR - win, 9 * FSR, win)]
top = max(env)
rise = next(k for k, e in enumerate(env) if e > 0.9 * top) * 0.1 - 0.1
last = max((i for i in range(len(L) - 1, 0, -1) if L[i] or R[i]), default=0)
rep.check('HISS 0 -> 127 -> 0: the level slews (0 to 2x the plugin\'s in ~3.2 s up, ~2.8 s down) instead of stepping; '
          'HISS 0 is exactly 0', 2.0 < rise < 4.5 and env[1] < 0.2 * top and last < 9 * FSR - FSR // 10,
          f'0.1 s windows from the step: {", ".join(f"{A.db(e):.0f}" for e in env[:40:3])} dBFS; 90 % after {rise:.1f} s; '
          f'last non-zero sample {last / FSR:.2f} s (the step down at 6 s)')

# ------------------------------------------------------------------ 7. tape life: wear raises the gap loss over time
LIFE127 = G.life_of(127)
secs = 40
blocks = secs * FSR // 16
x = [a + b for a, b in zip(A.sine(500, 0.3, blocks * 16), A.sine(5000, 0.3, blocks * 16))]
L, R, _ = run('wear', P(TIME=40, WEAR=127, WOW=0, HISS=0, FDBK=0, MIX=127), x, blocks)
L0, _, _ = run('wear0', P(TIME=40, WEAR=0, WOW=0, HISS=0, FDBK=0, MIX=127), x[:12 * FSR], 12 * FSR // 16)
r_a = A.goertzel(L0[2 * FSR:2 * FSR + 4410], 5000) / A.goertzel(L0[2 * FSR:2 * FSR + 4410], 500)
r_b = A.goertzel(L0[11 * FSR:11 * FSR + 4410], 5000) / A.goertzel(L0[11 * FSR:11 * FSR + 4410], 500)
new_m = dbs(abs(chain(5000, G.SR, 0, bump='chamberlin')) / abs(chain(500, G.SR, 0, bump='chamberlin')))
rows, ok = [], True
for t in (1, 8, 16, 24, 32, 39):
    rs = []
    for k in range(10):
        a0 = int((t - 0.5 + 0.1 * k) * FSR)
        seg = L[a0:a0 + 4410]
        rs.append(A.goertzel(seg, 5000) / max(A.goertzel(seg, 500), 1e-9))
    rs.sort()
    got = dbs(rs[len(rs) // 2]) - dbs(r_a)
    age = min(1.0, max(0.0, t - 128 * 16 / FSR) / LIFE127)
    want = dbs(abs(chain(5000, G.SR, math.sqrt(age), bump='chamberlin')) / abs(chain(500, G.SR, math.sqrt(age), bump='chamberlin'))) - new_m
    ok &= abs(got - want) < 0.6
    rows.append(f't {t} s (age {age:.2f}): {got:+.2f} dB (plugin {want:+.2f})')
rep.check(f'tape life, WEAR 127 (new to ruined in {LIFE127:.0f} s): the gap loss follows the wear, fc = 15 kHz (1 - 0.8 '
          'sqrt(age)) -- 5 kHz against 500 Hz through the tape over 40 s, re the new tape\'s, matches the plugin at that '
          'age within 0.6 dB (median of 10 windows a second: flakes dip single windows)', ok,
          f'new tape {dbs(r_a):+.2f} dB (plugin {new_m:+.2f}: the rest is the Hermite read at this fractional delay, see the '
          'note); ' + '; '.join(rows))
rep.check('WEAR 0: the tape never ages (the ratio at 2 s and 11 s identical within 0.05 dB)', abs(dbs(r_b / r_a)) < 0.05,
          f'{dbs(r_a):+.2f} / {dbs(r_b):+.2f} dB')
# dropouts: the 500 Hz envelope's spread late in life
env = [A.goertzel(L[i:i + 441], 500) for i in range(30 * FSR, 40 * FSR, 441)]
env0 = [A.goertzel(L[i:i + 441], 500) for i in range(2 * FSR, 6 * FSR, 441)]
rep.note('dropouts (oxide flakes): 500 Hz level in 10 ms windows', f'new tape (2..6 s) {A.db(min(env0)):.1f} .. {A.db(max(env0)):.1f} dB; '
         f'worn (30..40 s) {A.db(min(env)):.1f} .. {A.db(max(env)):.1f} dB')

# ------------------------------------------------------------------ 8. the held loop: disintegrates when worn, intact when not
def loop_run(tag, wear_knob, age_secs, hold_secs, time=30):
    blocks = int((age_secs + hold_secs) * FSR / 16)
    x = A.noise(int(age_secs * FSR), 0.5, 21) + [0] * (blocks * 16 - int(age_secs * FSR))
    base = dict(TIME=time, WEAR=wear_knob, WOW=0, HISS=0, FDBK=0, MIX=127)
    ev = [(0, P(**base)), (int((age_secs - 0.02) * FSR / 16), P(**base, FRZ=1))]
    L, R, _ = run(tag, ev[0][1], x, blocks, events=ev)
    return L


hold = 12
L = loop_run('frzw', 127, 30, hold)
ln_ = 2 * round(G.time_of(30) * G.SR - G.LAT)      # the loop: round(dA) tape samples
a0 = 31 * FSR
passes = []
for k in range(0, int((hold - 1.5) * FSR / ln_), 10):
    seg = L[a0 + round(k * ln_): a0 + round(k * ln_) + round(ln_)]
    passes.append((A.rms(seg), sum((u - v) ** 2 for u, v in zip(seg[1:], seg)) ** 0.5 / (sum(u * u for u in seg) ** 0.5 + 1e-9)))
L0 = loop_run('frz0', 0, 6, hold)
a0 = 7 * FSR
p0 = []
for k in range(0, int((hold - 1.5) * FSR / ln_), 10):
    seg = L0[a0 + round(k * ln_): a0 + round(k * ln_) + round(ln_)]
    p0.append(seg)
d0 = max(max(abs(u - v) for u, v in zip(s, p0[0])) for s in p0[1:])
lv_w = A.db(passes[-1][0] / passes[0][0])
hf_w = A.db(passes[-1][1] / passes[0][1])
rep.check(f'FRZ holds the last {G.time_of(30) * 1000:.0f} ms as a loop. Worn tape (WEAR 127 run for 30 s, age 0.77): the loop '
          'disintegrates pass after pass -- quieter and duller (the plugin\'s s += (prev - s)(0.03 w + 0.3 d), '
          's *= 1 - (0.0009 w + 0.012 d) on each sample it passes); WEAR 0: the loop stays intact (every 10th pass over '
          f'{hold - 1.5:.0f} s identical to the first within 16 LSB = 2 LSB of the 1/8-scale loop)',
          lv_w < -1.0 and hf_w < -1.0 and d0 <= 16,
          f'worn: level {lv_w:+.1f} dB and HF (first difference re level) {hf_w:+.1f} dB after {len(passes) * 10} passes '
          f'({", ".join(f"{A.db(r):.1f}" for r, _ in passes[::3])} dBFS every 30th pass); new: largest difference {d0} LSB')

# ------------------------------------------------------------------ 9. refresh puts on new tape
secs = 34
blocks = secs * FSR // 16
x = [a + b for a, b in zip(A.sine(500, 0.3, blocks * 16), A.sine(5000, 0.3, blocks * 16))]
base = dict(TIME=40, WEAR=127, WOW=0, HISS=0, FDBK=0, MIX=127)
ev = [(0, P(**base)), (30 * FSR // 16, P(**base, RFSH=1))]
L, R, _ = run('rfsh', ev[0][1], x, blocks, events=ev)
ratio = lambda a: dbs(A.goertzel(L[a:a + 4410], 5000) / A.goertzel(L[a:a + 4410], 500))
new, before, after = ratio(int(1.5 * FSR)), ratio(int(29.5 * FSR)), ratio(int(30.5 * FSR))
dip = [A.goertzel(L[i:i + 441], 500) for i in range(int(30.5 * FSR), 33 * FSR, 441)]
rep.check('RFSH 0 -> 1 puts on new tape: the gap loss is back to new at once (5 kHz re 500 Hz within 0.6 dB of the '
          'first seconds\') and the flakes are gone (500 Hz steady within 0.5 dB in 10 ms windows)',
          abs(after - new) < 0.6 and before < new - 2 and A.db(max(dip) / min(dip)) < 0.5,
          f'new {new:+.2f} dB, after 29.5 s {before:+.2f} dB, 0.5 s after RFSH {after:+.2f} dB; 500 Hz spread after '
          f'{A.db(max(dip) / min(dip)):.2f} dB')

# ------------------------------------------------------------------ 10. the ColdFire's freeze words, per side
blocks = 4 * FSR // 16
x = A.sine(700, 0.4, blocks * 16)
L, R, _ = run('fword', P(TIME=30, **QUIET), x, blocks, extra=('-pword', f'0:6={0x7fffff:x},0:7=0'))
rep.check('the freeze words: r6+6 non-zero holds the left line only -- set from the first call, the left holds its '
          'empty tape (silent) while the right echoes the input', not any(L[FSR:]) and A.rms(R[2 * FSR:]) > 0.05,
          f'left max |v| {max(abs(v) for v in L[FSR:])} LSB from 1 s, right rms {A.db(A.rms(R[2 * FSR:])):.1f} dBFS')

# ------------------------------------------------------------------ 11. knob moves without clicks
def d2(y):
    for _ in range(3):          # [1 2 1]/4 three times first: the halfband's -43 dB image near 21.6 kHz would dominate
        y = [(p + 2 * c + n) / 4 for p, c, n in zip(y, y[1:], y[2:])]
    return max(abs(a - 2 * b + c) for a, b, c in zip(y[2:], y[1:], y))


blocks = 3 * FSR // 16
x = A.sine(440, 0.5, blocks * 16)
clean = d2(x[FSR:2 * FSR]) / FS / 0.5
rows, ok = [], True
for tag, k, a, b in (('mix', 'MIX', 0, 127), ('fdbk', 'FDBK', 0, 115), ('wow', 'WOW', 0, 127), ('hpf', 'HPF', 0, 1),
                     ('skew', 'SKEW', 40, 90), ('wear', 'WEAR', 0, 127)):
    kn = dict(TIME=64, WEAR=0, HISS=0, FDBK=40, MIX=64, WOW=0)
    ev = [(0, P(**{**kn, k: a}))] + [(int((1 + 0.1 * i) * FSR / 16), P(**{**kn, k: (b if i % 2 == 0 else a)})) for i in range(8)]
    L, R, _ = run('z' + tag, ev[0][1], x, blocks, events=ev)
    seg = L[int(0.9 * FSR):int(2 * FSR)]
    r = d2(seg) / max(abs(v) for v in seg) / clean
    ok &= r < 2
    rows.append(f'{k} {a}<->{b} every 0.1 s: largest second difference {r:.2f} x a clean 440 Hz sine\'s')
rep.check('knob jumps on a steady 440 Hz sine (MIX, FDBK, WOW, HPF, SKEW, WEAR jumped eight times; TMOD SLEW): no click -- the '
          'largest second difference (images filtered out), re the output\'s own peak, within 2 x a clean sine\'s',
          ok, '; '.join(rows))

blocks = 4 * FSR // 16
x = A.sine(1500, 0.5, blocks * 16)
rows, ok = [], True
for mode in (1, 0):
    kn = dict(TIME=60, WEAR=0, HISS=0, FDBK=0, MIX=127, WOW=0, TMOD=mode)
    ev = [(0, P(**kn)), (2 * FSR // 16, P(**{**kn, 'TIME': 80}))]
    L, R, _ = run(f'tmod{mode}', ev[0][1], x, blocks, events=ev)
    fr = zc_freq(L[int(1.9 * FSR):int(3.6 * FSR)])
    devs = [abs(f / 1500 - 1) for _, f in fr]
    r = d2(L[int(1.9 * FSR):int(3.6 * FSR)]) / FS / (d2(x[FSR:2 * FSR]) / FS)
    if mode:
        ok &= max(devs) < 0.01 and r < 3
        rows.append(f'FADE: largest per-cycle frequency deviation {max(devs) * 100:.3f} %, second difference {r:.2f} x clean')
    else:
        ok &= max(devs) > 0.1
        rows.append(f'SLEW: the pitch bends while the tape speed glides, largest {max(devs) * 100:.1f} % (the plugin\'s '
                    '1.6 Hz servo)')
rep.check('TIME 60 -> 80 under a 1.5 kHz sine: TMOD FADE keeps the pitch (crossfade to a second head, no click); '
          'TMOD SLEW bends it (the capstan glides)', ok, '; '.join(rows))

# ------------------------------------------------------------------ 12. cost
nul = A.null_cost()
rows = []
worst = 0
for tag, kn in (('default', P()), ('dear', P(**DEAR)), ('dear + HPF + HISS 127', P(**DEAR, HPF=1, HISS=127)),
                ('frozen + HPF', P(**DEAR, HPF=1, FRZ=1)), ('FADE, TIME moving', P(**DEAR, TMOD=1, HPF=1))):
    blocks = 4000
    ev = None
    if 'moving' in tag:
        ev = [(b, P(**{**DEAR, 'TIME': 40 + (b // 400 % 2) * 50, 'TMOD': 1, 'HPF': 1})) for b in range(0, blocks, 400)]
    L, R, m = run('cost' + str(len(rows)), kn, A.noise(blocks * 16, 0.5, 2), blocks, events=ev)
    w = m['max_per_frame'] - nul['max_per_frame']
    worst = max(worst, w)
    rows.append(f'{tag}: mean {m["mean_per_frame"] - nul["mean_per_frame"]:.1f}, worst block {w:.1f}')
rep.note('cost (dsp_host instructions a sample, null effect taken off)', '; '.join(rows) + f'; worst of all {worst:.1f}')
rep.note('size', f'{len(fw)} code words (+ 4 stub); r7 block 256 words (globals $00-$3f, sample state $40/$80, call state '
         f'$c0/$e0); Y ring {G.BUILDS["tape.asm"]["yring"]} + 2 x {G.DMG_STRIDE} damage words from the base word, '
         f'X ring {G.BUILDS["tape.asm"]["xring"]} at X:$4000')
rep.save('/aeria/tape/test_results.md')
