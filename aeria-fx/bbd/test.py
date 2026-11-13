"""BBD gate: tools/run.sh python3 /aeria/bbd/test.py   (writes bbd/test_results.md)

Builds (both generated here from gen.py, so neither can be stale):
  bbd_test.asm  rings at X:$10000 / $11000 (dsp_host's X); every render below runs on it
  bbd.asm       the hardware build: assembled and compared line for line (only the base fetch in init differs)
The plugin to compare with is ref.py (Chloaelis' BBDLine / DelayCell, tanh stages made linear).
"""
import math
import random
import re
import struct
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/bbd')
import aeria as A
import gen as G
import ref as RF

FSR = 44100
FS = A.FS
Path('/aeria/bbd/bbd.asm').write_text(G.build())
Path('/aeria/bbd/bbd_test.asm').write_text(G.build(G.TEST_XBASE, True))
rep = A.Report('BBD')
NAMES = ['TIME', 'SKEW', 'FDBK', 'TONE', 'MOD', 'MIX', 'HPF', 'FRZ', 'RFRZ', 'TMOD', 'RATE', '']
MAN = '/aeria/bbd/manifest.py'
ASM = '/aeria/bbd/bbd_test.asm'
KEY = 'AERIA BBD'
COST_LIMIT = 800


# ------------------------------------------------------------------ the standard gate (aeria.standard, with the cost
# limit this effect needs and the static count as information: its unrolled ticks are forward branches)
def standard(dear, blocks=1500):
    mod = A.registry._load_one(Path(MAN))
    names = [p.name.decode() if p.name else '' for p in mod.params]
    defaults = [p.default or 0 for p in mod.params]
    rep.check('manifest loads (schema: names <= 4, defaults inside their counts)',
              all(len(n) <= 4 for n in names) and all(0 <= (p.default or 0) < (p.count or 128) for p in mod.params),
              ' '.join(n for n in names if n))
    bad, dup = A.label_prefix_check(ASM)
    rep.check('no label is a prefix of another, no duplicates', not bad and not dup, bad or dup or '')
    rep.check('no rep instruction', A.no_rep(ASM))
    mems, words, init, proc = A.assemble(ASM, KEY)
    rep.check('assembles; disassembler round-trip + mpysu audit clean; <= 2,048 words (2,044 with the entry stub)',
              len(words) <= 2044, f'{len(words)} words')
    try:
        st, ok = A.static_cycles(ASM, Path(ASM).stem)
        rep.note('static cycle count (cycle_count.py: every unrolled clock edge taken, both heads)', f"{st['cycles']} cycles/sample")
    except SystemExit as e:
        rep.note('static cycle count', f'not countable by cycle_count.py: {str(e)[:200]}')
    nul = A.null_cost()
    dp = list(defaults)
    for k, v in dear.items():
        dp[names.index(k)] = v
    n = blocks * 16
    L, R, m = A.render('bb_dear', mems, init, proc, dp, A.noise(n, 0.5), blocks, guard=True)
    cost = m['max_per_frame'] - nul['max_per_frame']
    mean = m['mean_per_frame'] - nul['mean_per_frame']
    rep.check(f'cost at the dearest settings, 0.5 FS noise (dsp_host meter minus a null effect): worst block <= {COST_LIMIT}',
              cost <= COST_LIMIT, f'mean {mean:.1f}, worst block {cost:.1f} instructions a sample ({dear})')
    rep.check('dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes)',
              A.peak(L + R) <= 1.0 + 1e-6 and m['stray'] == 0 and m['clobber'] == 0,
              f"peak {A.peak(L + R):.3f} FS, stray {m['stray']}")
    dry = list(dp)
    dry[names.index('MIX')] = 0
    x = [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]
    L, R, _ = A.render('bb_dry', mems, init, proc, dry, x, blocks)
    rep.check('MIX 0 is exact dry from the first sample (full-range ramp, dearest other knobs)', L == x and R == x,
              f'max |err| {max(abs(a - b) for a, b in zip(L + R, x + x))} LSB')
    worst = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = A.with_fill(mems, fill, f'bbfill{fill:06x}')
        for kn in (defaults, dp):
            Lf, Rf, _ = A.render('bb_dirty', fm, init, proc, kn, None, 300, dirty=0x1234 + fill)
            worst = max(worst, max(abs(v) for v in Lf[-300:] + Rf[-300:]))
    rep.check('dirty state (garbage r7 block + Y) on silence -> silent tail', worst == 0, f'worst tail {worst} LSB')
    rnd = random.Random(7)
    counts = [p.count or 128 for p in mod.params]
    ev = []
    for b in range(0, blocks, 3):
        v = list(defaults)
        for j, p in enumerate(mod.params):
            if p.active:
                v[j] = rnd.randrange(counts[j])
        ev.append((b, v))
    worstp, wc = 0, 0
    for split in (0, 7):
        La, Ra, ma = A.render(f'bb_auto{split}', mems, init, proc, defaults, A.noise(n, 0.7, 3), blocks, events=ev,
                              split=split, guard=True)
        worstp = max(worstp, A.peak(La + Ra))
        wc = max(wc, ma['max_per_frame'] - nul['max_per_frame'])
        assert ma['stray'] == 0 and ma['clobber'] == 0
    rep.check('every knob random every 3 blocks (FADE transitions, freezes, HPF ...), splits 0 and 7, guard: bounded',
              worstp <= 1.0 + 1e-6, f'peak {worstp:.3f} FS; worst block {wc:.1f} instructions a sample')
    return mod, names, defaults, mems, init, proc, (cost, mean, wc)


DEAR = {'TIME': 0, 'SKEW': 0, 'FDBK': 127, 'TONE': 127, 'MOD': 127, 'MIX': 127, 'HPF': 1, 'TMOD': 0, 'RATE': 127}
mod, names, d, mems, init, proc, cost = standard(DEAR)
assert names == NAMES, names


def P(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def run(tag, p, x, blocks, mm=None, **kw):
    L, R, m = A.render('bb_' + tag, mm or mems, init, proc, p, x, blocks, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def pk(y):
    return max(abs(v) for v in y) / FS if y else 0.0


# ---- 0. the hardware build is the test build but for the base fetch ---------------------------------------
hw = [l for l in Path('/aeria/bbd/bbd.asm').read_text().splitlines() if not l.startswith(';')]
tb = [l for l in Path(ASM).read_text().splitlines() if not l.startswith(';')]
import difflib
dl = [l for l in difflib.unified_diff(hw, tb, lineterm='', n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---')]
hwm, hww, _, _ = A.assemble('/aeria/bbd/bbd.asm', 'AERIA BBDHW')
rep.check('hardware build (bbd.asm) assembles; differs from the test build only in init\'s base fetch',
          all(('$213' in l or '(r4),a' in l or 'nop' in l or '$010000' in l) for l in dl) and len(dl) <= 4,
          f'{len(hww)} words; diff: ' + ' | '.join(l.strip() for l in dl))

# ---- 1. delay times, both sides ------------------------------------------------------------------------------
rows, ok = [], True
burst = [round(0.5 * FS * math.sin(2 * math.pi * 700 * i / FSR) * math.sin(math.pi * i / 441)) for i in range(441)]
for tv, sk in ((0, 40), (40, 127), (83, 84), (127, 64), (110, 0)):
    tl, tr = G.time_of(tv, sk, False), G.time_of(tv, sk, True)
    st = 2000 * 16
    blocks = st // 16 + int(max(tl, tr) * FSR) // 16 + 400
    x = [0] * (blocks * 16)
    x[st:st + len(burst)] = burst
    L, R, _ = run('dly', P(TIME=tv, SKEW=sk, FDBK=0, MIX=127), x, blocks)
    got = []
    for y, want in ((L, tl), (R, tr)):
        w0 = st + int(want * FSR) - 600
        seg = y[w0:w0 + 1800]
        best = max(range(len(seg) - len(burst)), key=lambda k: sum(a * b for a, b in zip(seg[k:k + len(burst):3], burst[::3])))
        lag = w0 + best - st
        got.append(lag)
        ok &= abs(lag - want * FSR) <= 0.003 * want * FSR + 12
    rows.append(f'TIME {tv} SKEW {sk}: L {got[0]} (want {tl * FSR:.0f}), R {got[1]} (want {tr * FSR:.0f}) samples')
rep.check('delay times (a 10 ms tone burst, FDBK 0, both sides): within 0.3 % + 12 samples of 47 ms x 21.28^(TIME/127) '
          '(x 2^((SKEW-64)/64) right); the excess is the filters\' group delay', ok, '; '.join(rows))

# ---- 2. frequency response against the plugin, and bandwidth vs TIME -------------------------------------------
FR = [200, 1000, 2000, 3000, 4500, 6000, 8000]
rows, ok, bws = [], True, []
for tv in (0, 64, 127):
    T = G.time_of(tv)
    seg = int(T * FSR) + 8192 + 4096
    x = []
    for f in FR:
        x += A.sine(f, 0.25, seg)
    blocks = len(x) // 16 + 1
    L, R, _ = run('fr', P(TIME=tv, SKEW=64, FDBK=0, MIX=127, TONE=100), x, blocks)
    y = RF.run([v / FS for v in x], T, noise=False)
    ds, rs, errs = [], [], []
    for k, f in enumerate(FR):
        a0 = k * seg + seg - 8192
        gd = A.db(A.goertzel(L[a0:a0 + 8192], f) / 0.25)
        gr = A.db(A.goertzel([v * FS for v in y[a0:a0 + 8192]], f) / 0.25)
        ds.append(gd)
        rs.append(gr)
        if gr > -30:
            errs.append(abs(gd - gr))
    e = max(errs)
    ok &= e < 2.5
    bws.append(ds[FR.index(4500)])
    rows.append(f'TIME {tv} ({T * 1000:.0f} ms, clock {4096 / T / 1000:.1f} kHz): DSP / plugin dB ' +
                ' '.join(f'{f}:{a:+.1f}/{b:+.1f}' for f, a, b in zip(FR, ds, rs)) + f'; worst |diff| {e:.2f} dB')
rep.check('frequency response (-12 dBFS sines, steady) against the plugin\'s BBDLine (ref.py, linear): within 2.5 dB '
          'wherever the plugin is above -30 dB; the response at 4.5 kHz falls as TIME grows (the clock, the S&H and the '
          'charge-transfer pole follow it)', ok and bws[0] > bws[1] + 2 > bws[2] + 4,
          f'4.5 kHz: {bws[0]:+.1f} / {bws[1]:+.1f} / {bws[2]:+.1f} dB at TIME 0 / 64 / 127; ' + '; '.join(rows))

# ---- 3. aliasing and TONE ------------------------------------------------------------------------------------------
rows = []
T = G.time_of(127)
n = int(T * FSR) + 16384
x = A.sine(3000, 0.25, n)
L, R, _ = run('alias', P(TIME=127, SKEW=64, FDBK=0, MIX=127), x, n // 16)
y = RF.run([v / FS for v in x], T, noise=False)
fa = 4096 / T - 3000
ad = A.db(A.goertzel(L[-8192:], fa) / 0.25)
ar = A.db(A.goertzel([v * FS for v in y[-8192:]], fa) / 0.25)
rep.check(f'aliasing at TIME 127 (clock {4096 / T:.0f} Hz): a 3 kHz sine folds to {fa:.0f} Hz at the plugin\'s level '
          '(within 4 dB)', abs(ad - ar) < 4 and ad > -40, f'alias DSP {ad:+.1f} dB, plugin {ar:+.1f} dB re the input')
for tn in (40, 70, 100):
    seg = int(G.time_of(30) * FSR) + 8192 + 2048
    x = A.sine(4000, 0.25, seg)
    L, R, _ = run('tone', P(TIME=30, SKEW=64, FDBK=0, MIX=127, TONE=tn), x, seg // 16)
    y = RF.run([v / FS for v in x], G.time_of(30), noise=False, tone_scale=G.tone_scale(tn))
    rows.append((tn, A.db(A.goertzel(L[-8192:], 4000) / 0.25), A.db(A.goertzel([v * FS for v in y[-8192:]], 4000) / 0.25)))
rep.check('TONE moves both 4-pole filters (4 kHz sine, TIME 30): DSP within 2.5 dB of the plugin with its filters '
          'at the same cutoffs', all(abs(a - b) < 2.5 for _, a, b in rows if b > -40) and rows[0][1] < rows[1][1] < rows[2][1],
          '; '.join(f'TONE {t} ({6800 * G.tone_scale(t):.0f} Hz): {a:+.1f} / {b:+.1f} dB' for t, a, b in rows))

# ---- 4. regeneration: FDBK 127 bounded for 10 s, and every lower setting dies to exact silence -----------------
blocks = 10 * FSR // 16
for tv in (0, 60, 127):
    L, R, m = run('reg', P(TIME=tv, FDBK=127, MIX=127), A.noise(2 * FSR, 0.9, 3), blocks)
    secs = [max(pk(L[k * FSR:(k + 1) * FSR]), pk(R[k * FSR:(k + 1) * FSR])) for k in range(10)]
    rail = sum(1 for v in L + R if abs(v) >= FS - 1)
    rep.check(f'FDBK 127 (loop gain 1.21), TIME {tv}: 0.9 FS noise 2 s then 8 s silence -- bounded by the rider at '
              f'{G.CEIL} FS every second, nothing at the rail; it sings on (self-oscillation, as the plugin)',
              max(secs) <= G.CEIL + 0.002 and rail == 0 and min(secs[3:]) > 0.3,
              'peak per second ' + ' '.join(f'{v:.3f}' for v in secs) + f', rail samples {rail}')
rows, ok = [], True
for kn in (dict(FDBK=85, TIME=40), dict(FDBK=70, TIME=100, HPF=1), dict(FDBK=60, TIME=0, TONE=20), dict(FDBK=0, TIME=83)):
    blocks = 20 * FSR // 16
    L, R, _ = run('dec', P(MIX=127, **kn), A.noise(FSR, 0.7, 4), blocks)
    last = max((k for k, (u, v) in enumerate(zip(L, R)) if u or v), default=0)
    ok &= last < len(L) - FSR
    rows.append(f'{kn}: last non-zero sample {(last - FSR) / FSR:.2f} s after the input ended')
rep.check('silence after the input ends -> exactly 0 out (no limit cycle, no stuck LSB, the artefacts gated off '
          '1.1 s after the line goes quiet)', ok, '; '.join(rows))
L, R, _ = run('sil', P(**DEAR), None, 3 * FSR // 16)
rep.check('silence in, silence out (dearest settings, 3 s): every sample 0', not any(L) and not any(R))

# ---- 5. freeze ----------------------------------------------------------------------------------------------------
T = G.time_of(64)
per = int(T * FSR)
blocks = 8 * FSR // 16
ev = [(int(1.2 * FSR) // 16, P(TIME=64, SKEW=64, FRZ=1, MIX=127))]
L, R, _ = run('frz', P(TIME=64, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), blocks, events=ev)
a0 = int(1.2 * FSR) + per
passes = [A.rms(L[a0 + k * per:a0 + (k + 1) * per]) for k in range((len(L) - a0) // per)]
spread = A.db(max(passes) / min(passes))
rep.check('FRZ: the frozen ring recirculates (6.6 s, 30 laps): level per lap within 1 dB (windows a lap long) (each bucket leaks '
          '0.99985 a lap: -0.0013 dB, the plugin\'s); input and feedback cut', spread < 1.0 and passes[0] > 1e-3,
          f'{len(passes)} laps, rms {A.db(passes[0]):.1f} .. {A.db(passes[-1]):.1f} dBFS, spread {spread:.2f} dB')
ev = [(int(1.2 * FSR) // 16, P(TIME=64, SKEW=64, MIX=127))]
L, R, _ = run('rfrz', P(TIME=64, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), blocks, events=None,
              extra=('-pword', '0:6=0'))
L2, R2, _ = run('rfrz2', P(TIME=64, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), int(1.2 * FSR) // 16,
                extra=('-pword', '0:6=0'))
# word +6 = 1 from the start: left frozen from the first sample (nothing goes in), right normal
L3, R3, _ = run('rfrz3', P(TIME=64, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), blocks, extra=('-pword', '0:6=1'))
rep.check('the ColdFire\'s freeze word (knob block +6): bit 0 freezes the left only (the left line never takes the '
          'input, the right echoes it)', pk(L3[2000:]) < 1e-4 and pk(R3[per:]) > 0.05,
          f'left peak {pk(L3[2000:]):.2e}, right peak {pk(R3[per:]):.3f} FS')


# ---- 6. SLEW bends, FADE does not ----------------------------------------------------------------------------------
def freqs(y, a, b, win=2205):
    lp1, lp2 = RF.Biquad().lowpass(1500, 0.54), RF.Biquad().lowpass(1500, 1.31)
    y = [lp2.process(lp1.process(v)) for v in y]
    out = []
    for s in range(a, b - win, win):
        seg = y[s:s + win]
        zc = [k + seg[k] / (seg[k] - seg[k + 1]) for k in range(len(seg) - 1) if seg[k] <= 0 < seg[k + 1]]
        f = (len(zc) - 1) / (zc[-1] - zc[0]) * FSR if len(zc) > 2 else 0.0
        out.append((f, max(abs(v) for v in seg) / FS))
    return out


rows, okf = [], True
for tm, t0, t1 in ((1, 64, 90), (1, 90, 50), (1, 20, 127), (1, 127, 0), (0, 64, 90), (0, 90, 50)):
    blocks = 6 * FSR // 16
    ev = [(2 * FSR // 16, P(TIME=t1, SKEW=64, TMOD=tm, MIX=127, FDBK=0))]
    L, R, m = run('fade', P(TIME=t0, SKEW=64, TMOD=tm, MIX=127, FDBK=0), A.sine(1000, 0.3, blocks * 16), blocks, events=ev)
    fr = freqs(L, int(1.8 * FSR), len(L))
    dev = max(abs(f - 1000) for f, _ in fr) / 10
    amp0, amp1 = fr[0][1], fr[-1][1]
    amps = [a for _, a in fr]
    if tm:
        good = dev < 1.0 and max(amps) < 1.1 * max(amp0, amp1) + 0.02 and min(amps) > 0.4 * min(amp0, amp1)
        okf &= good
    else:
        okf &= dev > 20
    rows.append(f'{"FADE" if tm else "SLEW"} TIME {t0} -> {t1} ({G.time_of(t0) * 1000:.0f} -> {G.time_of(t1) * 1000:.0f} ms):'
                f' largest frequency deviation {dev:.2f} %, level {min(amps):.3f} .. {max(amps):.3f} FS (steady before / after {amp0:.3f} / {amp1:.3f}),'
                f' worst block {m["max_per_frame"] - A.null_cost()["max_per_frame"]:.0f}')
rep.check('time changes under a steady 1 kHz sine (FDBK 0; the output low-passed at 1.5 kHz first, as at 1 s the 4 kHz clock folds the tone to 3.1 kHz): FADE keeps the pitch (every 50 ms window within 1 %) and '
          'the level (within 0.4 x the lower .. 1.1 x the higher of the steady levels before and after; the crossfade cos^2/sin^2); SLEW bends it (the clock glides, > 20 %)', okf,
          '; '.join(rows))
# after a FADE the heard time is the new one
rows, ok = [], True
for t0, t1 in ((64, 100), (100, 30)):
    tn = G.time_of(t1)
    st = 6 * FSR
    blocks = st // 16 + int(tn * FSR) // 16 + 400
    x = [0] * (blocks * 16)
    x[st:st + len(burst)] = burst
    ev = [(FSR // 16, P(TIME=t1, SKEW=64, TMOD=1, MIX=127, FDBK=0))]
    L, R, _ = run('fadet', P(TIME=t0, SKEW=64, TMOD=1, MIX=127, FDBK=0), x, blocks, events=ev)
    w0 = st + int(tn * FSR) - 600
    seg = L[w0:w0 + 1800]
    best = max(range(len(seg) - len(burst)), key=lambda k: sum(a * b for a, b in zip(seg[k:k + len(burst):3], burst[::3])))
    lag = w0 + best - st
    ok &= abs(lag - tn * FSR) <= 0.003 * tn * FSR + 12
    rows.append(f'{t0} -> {t1}: echo at {lag} (want {tn * FSR:.0f})')
rep.check('after a FADE change the echo comes at the new time (heads, clock and pending switch all settled)', ok,
          '; '.join(rows))

# ---- 7. clicks / zipper on the other knobs -------------------------------------------------------------------------
rows, ok = [], True


def d2max(y):
    return max(abs(a - 2 * b + c) for a, b, c in zip(y[2:], y[1:], y))


for knob, lo, hi in (('TONE', 30, 127), ('FDBK', 0, 100), ('MIX', 0, 127), ('MOD', 0, 127)):
    blocks = 4 * FSR // 16
    ev = [(400 + 2 * k, P(**{knob: lo + (hi - lo) * k // 200}, TIME=40)) for k in range(201)]
    ev += [(1600, P(**{knob: lo}, TIME=40))]
    x = A.sine(440, 0.3, blocks * 16)
    L, R, _ = run('zip', P(**{knob: lo}, TIME=40), x, blocks, events=ev)
    Lc, Rc, _ = run('zipc', P(**{knob: hi}, TIME=40), x, blocks)
    ctl = max(d2max(Lc[300 * 16:]), d2max(L[300 * 16:400 * 16]))
    r = d2max(L[300 * 16:]) / max(ctl, 1)
    rows.append(f'{knob} {lo}..{hi} swept then dropped: largest 2nd difference {r:.2f} x the same render with the knob still')
    ok &= r < 1.5 or knob == 'MOD'
rep.check('knob moves on a 440 Hz sine (TIME 40; swept over 0.73 s, then a jump back): no click -- the largest second '
          'difference stays within 1.5 x that of the knob held (the BBD\'s noise and S&H steps set the floor); MOD bends '
          'pitch by design (reported)', ok, '; '.join(rows))

# ---- 8. HPF in the loop ----------------------------------------------------------------------------------------------
lev = []
for hp in (0, 1):
    T = G.time_of(30)
    n = int(T * FSR)
    blocks = (6 * n) // 16
    x = A.sine(80, 0.4, n // 2)
    L, R, _ = run('hpf', P(TIME=30, SKEW=64, FDBK=110, MIX=127, HPF=hp), x, blocks)
    lev.append(A.rms(L[4 * n:5 * n]))
rep.check('HPF: an 80 Hz tone\'s fourth repeat (FDBK 110) is far weaker with the 320 Hz feedback high-pass on',
          A.db(lev[1] / max(lev[0], 1e-9)) < -12, f'OFF {A.db(lev[0]):.1f} dBFS, ON {A.db(lev[1]):.1f} dBFS')

# ---- 9. the X rings: bounds and garbage --------------------------------------------------------------------------
XB = G.TEST_XBASE


def with_x(ms, tag, chunks):
    out = []
    for c, mm in zip('AB', ms):
        blob = Path(mm).read_bytes()
        body, term = blob[:-9], blob[-9:]
        for ad, words in chunks:
            body += struct.pack('<BII', 1, ad, len(words)) + struct.pack('<' + 'I' * len(words), *words)
        p_ = A.BUILD / f'{tag}_{c}.mem'
        p_.write_bytes(body + term)
        out.append(p_)
    return out


garb = [(0x123456 + 0x9e3779 * i) & 0xffffff for i in range(0x2000)]
xm = with_x(A.with_fill(mems, 0xa5a5a5, 'bbxf0'), 'bbxf1', [(XB, garb)])
L, R, _ = run('xdirty', P(**DEAR), None, 600, mm=xm, dirty=0x4321)
rep.check('garbage in the X rings + dirty r7 block + dirty Y, silence in, dearest knobs: silent (the rings are '
          'cleared over 16 dry calls first)', max(abs(v) for v in L + R) == 0, f'worst {max(abs(v) for v in L + R)} LSB')
ads = list(range(XB - 64, XB)) + list(range(XB + 0x2000, XB + 0x2040))
probe = [XB, XB + 0xfff, XB + 0x1000, XB + 0x1fff]
sm = with_x(mems, 'bbsent', [(XB - 64, [0x5e5e5e] * 64), (XB + 0x2000, [0x5e5e5e] * 64)])
rnd = random.Random(11)
blocks = 3000
ev = [(k, [rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), 127,
           rnd.randrange(2), rnd.randrange(2), 0, rnd.randrange(2), rnd.randrange(128), 0]) for k in range(0, blocks, 40)]
bad = used = 0
for split in (0, 7):
    _, _, m = run(f'sent{split}', P(), A.noise(blocks * 16, 0.7, 8), blocks, mm=sm, events=ev, split=split,
                  extra=['-peekx', ','.join(f'{v:x}' for v in ads + probe)])
    got = {int(k, 16): int(v, 16) for k, v in re.findall(r'X:0x([0-9a-f]+) = 0x([0-9a-f]+)', m['log'])}
    bad += sum(1 for v in ads if got.get(v) != 0x5e5e5e)
    used += sum(1 for v in probe if got.get(v, 0) != 0)
rep.check('X ring bounds: 64 sentinel words below X:$10000 and above X:$11fff unchanged after automated runs '
          '(splits 0 and 7); the first / last words of both rings were written', bad == 0 and used == 8,
          f'{bad} sentinel words changed of {2 * len(ads)}; ring probes written {used} of 8')

# ---- 10. cost while FADE transitions run, width ------------------------------------------------------------------
nul = A.null_cost()
blocks = 6000
ev = [(k, P(TIME=(0, 127, 20, 110)[(k // 600) % 4], SKEW=0, TMOD=1, FDBK=127, TONE=127, HPF=1, MIX=127))
      for k in range(0, blocks, 600)]
_, _, m = run('costf', P(TIME=0, SKEW=0, TMOD=1, FDBK=127, TONE=127, HPF=1, MIX=127), A.noise(blocks * 16, 0.5), blocks,
              events=ev)
cf = m['max_per_frame'] - nul['max_per_frame']
rep.note('cost', f'dearest settings: mean {cost[1]:.1f}, worst block {cost[0]:.1f}; FADE transitions running (TIME 0 <-> '
              f'127, both sides): worst block {cf:.1f}; random automation worst block {cost[2]:.1f} instructions a sample')


def wrun(xx, bl):
    return run('w', P(MIX=127), xx, bl)[:2]


A.stereo_report(rep, 'defaults (TIME 83, SKEW 84 = x1.24, FDBK 44), MIX 127', wrun, True, secs=4.0, info=True)
rep.note('size', f'{len(A.assemble(ASM, KEY)[1])} words of P (+4 stub); r7 block {0x40 + 2 * G.SIDE_WORDS} words '
                 f'(common 64, sides 2 x {G.SIDE_WORDS}); X rings 2 x 4,096 words')
rep.save('/aeria/bbd/test_results.md')
