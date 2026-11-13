"""PT2399 gate: tools/run.sh python3 /aeria/pt2399/test.py   (writes pt2399/test_results.md)

Builds (both generated here from gen.py, so neither can be stale):
  pt2399_test.asm  RAM at X:$10000 / $10800 (dsp_host's X); every render below runs on it
  pt2399.asm       the hardware build: assembled and compared line for line (only the base fetch in init differs)
The plugin to compare with is ref.py (Chloaelis' PT2399Line / DelayCell, tanh stages made linear).
"""
import math
import random
import re
import struct
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/pt2399')
import aeria as A
import gen as G
import ref as RF

FSR = 44100
FS = A.FS
Path('/aeria/pt2399/pt2399.asm').write_text(G.build())
Path('/aeria/pt2399/pt2399_test.asm').write_text(G.build(G.TEST_XBASE, True))
rep = A.Report('PT2399')
NAMES = ['TIME', 'SKEW', 'FDBK', 'TONE', 'GRIT', 'MIX', 'HPF', 'FRZ', 'RFRZ', 'TMOD', 'MOD', 'RATE']
MAN = '/aeria/pt2399/manifest.py'
ASM = '/aeria/pt2399/pt2399_test.asm'
KEY = 'AERIA PT2399'
COST_LIMIT = 1000


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
        rep.note('static cycle count', f"{st['cycles']} cycles/sample")
    except SystemExit as e:
        rep.note('static cycle count', f'not countable by cycle_count.py: {str(e)[:200]}')
    nul = A.null_cost()
    dp = list(defaults)
    for k, v in dear.items():
        dp[names.index(k)] = v
    n = blocks * 16
    L, R, m = A.render('pt_dear', mems, init, proc, dp, A.noise(n, 0.5), blocks, guard=True)
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
    L, R, _ = A.render('pt_dry', mems, init, proc, dry, x, blocks)
    rep.check('MIX 0 is exact dry from the first sample (full-range ramp, dearest other knobs)', L == x and R == x,
              f'max |err| {max(abs(a - b) for a, b in zip(L + R, x + x))} LSB')
    worst = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = A.with_fill(mems, fill, f'ptfill{fill:06x}')
        for kn in (defaults, dp):
            Lf, Rf, _ = A.render('pt_dirty', fm, init, proc, kn, None, 300, dirty=0x1234 + fill)
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
        La, Ra, ma = A.render(f'pt_auto{split}', mems, init, proc, defaults, A.noise(n, 0.7, 3), blocks, events=ev,
                              split=split, guard=True)
        worstp = max(worstp, A.peak(La + Ra))
        wc = max(wc, ma['max_per_frame'] - nul['max_per_frame'])
        assert ma['stray'] == 0 and ma['clobber'] == 0
    rep.check('every knob random every 3 blocks (FADE transitions, freezes, HPF ...), splits 0 and 7, guard: bounded',
              worstp <= 1.0 + 1e-6, f'peak {worstp:.3f} FS; worst block {wc:.1f} instructions a sample')
    return mod, names, defaults, mems, init, proc, (cost, mean, wc)


DEAR = {'TIME': 0, 'SKEW': 0, 'FDBK': 127, 'TONE': 127, 'GRIT': 127, 'MIX': 127, 'HPF': 1, 'TMOD': 0, 'MOD': 127, 'RATE': 127}
mod, names, d, mems, init, proc, cost = standard(DEAR)
assert names == NAMES, names


def P(**kw):
    p = list(d)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def run(tag, p, x, blocks, mm=None, **kw):
    L, R, m = A.render('pt_' + tag, mm or mems, init, proc, p, x, blocks, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def pk(y):
    return max(abs(v) for v in y) / FS if y else 0.0


# ---- 0. the hardware build is the test build but for the base fetch ---------------------------------------
hw = [l for l in Path('/aeria/pt2399/pt2399.asm').read_text().splitlines() if not l.startswith(';')]
tb = [l for l in Path(ASM).read_text().splitlines() if not l.startswith(';')]
import difflib
dl = [l for l in difflib.unified_diff(hw, tb, lineterm='', n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---')]
hwm, hww, _, _ = A.assemble('/aeria/pt2399/pt2399.asm', 'AERIA PTHW')
rep.check('hardware build (pt2399.asm) assembles; differs from the test build only in init\'s base fetch',
          all(('$213' in l or '(r4),a' in l or 'nop' in l or '$010000' in l) for l in dl) and len(dl) <= 4,
          f'{len(hww)} words; diff: ' + ' | '.join(l.strip() for l in dl))

# ---- 1. delay times (the uncapped clock and the shortened tap) -----------------------------------------------------
rows, ok = [], True
burst = [round(0.4 * FS * math.sin(2 * math.pi * 700 * i / FSR) * math.sin(math.pi * i / 441)) for i in range(441)]


def echo_lag(y, st, want):
    w0 = st + int(want * FSR) - 700
    seg = y[w0:w0 + 2000]
    best = max(range(len(seg) - len(burst)), key=lambda k: sum(a * b for a, b in zip(seg[k:k + len(burst):3], burst[::3])))
    return w0 + best - st


for tv, sk in ((0, 40), (30, 127), (80, 84), (127, 64), (110, 0)):
    tl, tr = G.time_of(tv, sk, False), G.time_of(tv, sk, True)
    st = 400 * 16
    blocks = st // 16 + int(max(tl, tr) * FSR) // 16 + 400
    x = [0] * (blocks * 16)
    x[st:st + len(burst)] = burst
    L, R, _ = run('dly', P(TIME=tv, SKEW=sk, FDBK=0, MIX=127), x, blocks)
    got = [echo_lag(L, st, tl), echo_lag(R, st, tr)]
    for g, want in zip(got, (tl, tr)):
        ok &= abs(g - want * FSR) <= 0.003 * want * FSR + 30
    rows.append(f'TIME {tv} SKEW {sk}: L {got[0]} (want {tl * FSR:.0f}), R {got[1]} (want {tr * FSR:.0f})')
rep.check('delay times (a 10 ms tone burst, FDBK 0, both sides): within 0.3 % + 30 samples of 50 ms x 20^(TIME/127) '
          '(x 2^((SKEW-64)/64) right), in both clock regions (under 333 ms the tap shortens)', ok, '; '.join(rows))

# ---- 2. frequency response vs the plugin, and the bandwidth following the clock ----------------------------------
FR = [300, 1000, 2000, 3000, 4500, 6000]
rows, ok, at45 = [], True, []
for tv in (80, 100, 127):
    T = G.time_of(tv)
    seg = int(T * FSR) + 8192 + 4096
    x = []
    for f in FR:
        x += A.sine(f, 0.1, seg)
    blocks = len(x) // 16 + 1
    L, R, _ = run('fr', P(TIME=tv, SKEW=64, FDBK=0, MIX=127), x, blocks)
    y = RF.run([v / FS for v in x], T, noise=False)
    ds, rs, errs = [], [], []
    for k, f in enumerate(FR):
        a0 = k * seg + seg - 8192
        gd = A.db(A.goertzel(L[a0:a0 + 8192], f) / 0.1)
        gr = A.db(A.goertzel([v * FS for v in y[a0:a0 + 8192]], f) / 0.1)
        ds.append(gd)
        rs.append(gr)
        if gr > -25:
            errs.append(abs(gd - gr))
    e = max(errs)
    ok &= e < 2.5
    at45.append(ds[FR.index(3000)])
    rows.append(f'TIME {tv} ({T * 1000:.0f} ms, {G.clock_of(T)[0] * FSR / 1000:.0f} kbit/s, bw {G.bw_of(G.clock_of(T)[0]):.0f} Hz): '
                'DSP / plugin dB ' + ' '.join(f'{f}:{a:+.1f}/{b:+.1f}' for f, a, b in zip(FR, ds, rs)) + f'; worst {e:.2f} dB')
rep.check('frequency response (-20 dBFS sines, steady) against the plugin\'s PT2399Line (ref.py, linear) where the '
          'clock is the plugin\'s (333 ms .. 1 s): within 2.5 dB wherever the plugin is above -25 dB; 3 kHz falls as '
          'TIME (and so the clock\'s bandwidth) falls', ok and at45[0] > at45[1] > at45[2],
          f'3 kHz: {" / ".join(f"{v:+.1f}" for v in at45)} dB; ' + '; '.join(rows))

# ---- 3. grit vs time: noise + distortion on a 1 kHz sine, DSP against the plugin ------------------------------------
def residual_db(y, f, amp):
    seg = y[-16384:]
    w = 2 * math.pi * f / FSR
    c = sum(v * math.cos(w * k) for k, v in enumerate(seg)) * 2 / len(seg)
    s_ = sum(v * math.sin(w * k) for k, v in enumerate(seg)) * 2 / len(seg)
    res = [v - c * math.cos(w * k) - s_ * math.sin(w * k) for k, v in enumerate(seg)]
    return A.db(A.rms(res) / (math.hypot(c, s_) / math.sqrt(2) / FS))


rows, gd_, gr_ = [], [], []
for tv in (30, 80, 100, 127):
    T = G.time_of(tv)
    n = int(T * FSR) + 20000
    x = A.sine(1000, 0.3, n)
    L, R, _ = run('grit', P(TIME=tv, SKEW=64, FDBK=0, MIX=127), x, n // 16)
    y = [v * FS for v in RF.run([v / FS for v in x], T, noise=False)]
    yc = [v * FS for v in RF.run([v / FS for v in x], T, noise=False, cap=3)]
    dd, r_, rc = residual_db(L, 1000, 0.3), residual_db(y, 1000, 0.3), residual_db(yc, 1000, 0.3)
    gd_.append(dd)
    gr_.append(r_)
    rows.append(f'TIME {tv} ({T * 1000:.0f} ms): DSP {dd:+.1f} dB, plugin {r_:+.1f} dB, plugin on the capped clock {rc:+.1f} dB')
rep.check('grit vs time: the residual (everything but the 1 kHz fundamental, -10 dBFS in, FDBK 0) re the fundamental '
          'grows as TIME grows, and where the clocks agree (TIME >= 80) the DSP is within 4 dB of the plugin',
          gd_[1] < gd_[2] < gd_[3] and all(abs(a - b) < 4 for a, b in zip(gd_[1:], gr_[1:])), '; '.join(rows))
rows, res = [], {}
for gv in (0, 64, 127):
    T = G.time_of(100)
    n = int(T * FSR) + 20000
    for tag, f, amp in (('loud 3 kHz', 3000, 0.4), ('quiet 300 Hz', 300, 0.03)):
        L, R, _ = run('gk', P(TIME=100, SKEW=64, FDBK=0, MIX=127, GRIT=gv), A.sine(f, amp, n), n // 16)
        res[(gv, tag)] = residual_db(L, f, amp)
rep.check('GRIT moves the largest step, and the plugin\'s 64 is the cleanest: on a loud 3 kHz sine 127 overloads the slope '
          '(smaller steps) and 0 adds granular error (bigger steps), both grittier than 64; a quiet sine is ruled by the '
          'smallest step, which GRIT leaves alone (reported)',
          res[(127, 'loud 3 kHz')] > res[(64, 'loud 3 kHz')] + 1 and res[(0, 'loud 3 kHz')] > res[(64, 'loud 3 kHz')] + 1,
          '; '.join(f'GRIT {g} {t}: {v:+.1f} dB' for (g, t), v in res.items()))

# ---- 4. regeneration -------------------------------------------------------------------------------------------------
blocks = 10 * FSR // 16
for tv in (0, 80, 127):
    L, R, m = run('reg', P(TIME=tv, FDBK=127, MIX=127), A.noise(2 * FSR, 0.9, 3), blocks)
    secs = [max(pk(L[k * FSR:(k + 1) * FSR]), pk(R[k * FSR:(k + 1) * FSR])) for k in range(10)]
    rail = sum(1 for v in L + R if abs(v) >= FS - 1)
    rep.check(f'FDBK 127 (loop gain 1.21), TIME {tv}: 0.9 FS noise 2 s then 8 s silence -- bounded by the rider at '
              f'{G.CEIL} FS every second, nothing at the rail; it sings on', max(secs) <= G.CEIL + 0.002 and rail == 0
              and min(secs[3:]) > 0.3, 'peak per second ' + ' '.join(f'{v:.3f}' for v in secs) + f', rail samples {rail}')
rows, ok = [], True
for kn in (dict(FDBK=85, TIME=30), dict(FDBK=70, TIME=100, HPF=1), dict(FDBK=60, TIME=0, TONE=20), dict(FDBK=0, TIME=80)):
    blocks = 20 * FSR // 16
    L, R, _ = run('dec', P(MIX=127, **kn), A.noise(FSR, 0.7, 4), blocks)
    last = max((k for k, (u, v) in enumerate(zip(L, R)) if u or v), default=0)
    ok &= last < len(L) - FSR
    rows.append(f'{kn}: last non-zero sample {(last - FSR) / FSR:.2f} s after the input ended')
rep.check('silence after the input ends -> exactly 0 out: the gate closes 1.1 s after the input is under -72 dBFS and '
          'the loop under -42 dBFS (with feedback the CVSD\'s granular noise regenerates at -50..-60 dBFS for good, as in '
          'the plugin)', ok, '; '.join(rows))
L, R, _ = run('sil', P(**DEAR), None, 3 * FSR // 16)
rep.check('silence in, silence out (dearest settings, 3 s): every sample 0', not any(L) and not any(R))

# ---- 5. freeze -------------------------------------------------------------------------------------------------------
T = G.time_of(100)
per = round(T * FSR)
blocks = 9 * FSR // 16
ev = [(int(1.2 * FSR) // 16, P(TIME=100, SKEW=64, FRZ=1, MIX=127))]
L, R, _ = run('frz', P(TIME=100, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), blocks, events=ev)
a0 = int(1.2 * FSR) + per
laps = [L[a0 + k * per:a0 + (k + 1) * per] for k in range((len(L) - a0) // per)]
r = [A.rms(v) for v in laps]
same = min(A.corr(laps[0], v) for v in laps[1:])
rep.check('FRZ: the RAM loops bit for bit (13 laps): every lap correlates > 0.95 with the first (the decoder\'s state '
          'and the S&H grouping carry over a lap, so not sample-exact), level per lap within 0.5 dB',
          A.db(max(r) / min(r)) < 0.5 and same > 0.95 and r[0] > 1e-3,
          f'{len(laps)} laps of {per} samples, rms {A.db(r[0]):.1f} dBFS, spread {A.db(max(r) / min(r)):.2f} dB, '
          f'lowest lap correlation {same:.4f}')
L3, R3, _ = run('rfrz', P(TIME=60, SKEW=64, MIX=127), A.noise(int(1.1 * FSR), 0.3, 5), 3 * FSR // 16, extra=('-pword', '0:6=1'))
rep.check('the ColdFire\'s freeze word (knob block +6): bit 0 freezes the left only (the left RAM keeps its idle bits, '
          'the right echoes the input)', pk(L3[int(G.time_of(60) * FSR) + 500:]) < 2e-3 and pk(R3[int(G.time_of(60) * FSR):]) > 0.05,
          f'left peak {pk(L3[int(G.time_of(60) * FSR) + 500:]):.2e}, right peak {pk(R3[int(G.time_of(60) * FSR):]):.3f} FS')


# ---- 6. SLEW bends, FADE does not --------------------------------------------------------------------------------------
def freqs(y, a, b, win=2205):
    lp1, lp2 = RF.Biquad().lowpass(1500, 0.54), RF.Biquad().lowpass(1500, 1.31)
    y = [lp2.process(lp1.process(v)) for v in y]
    out = []
    for s in range(a, b - win, win):
        seg = y[s:s + win]
        zc = [k + seg[k] / (seg[k] - seg[k + 1]) for k in range(len(seg) - 1) if seg[k] <= 0 < seg[k + 1]]
        f = (len(zc) - 1) / (zc[-1] - zc[0]) * FSR if len(zc) > 2 else 0.0
        sub = min(max(abs(v) for v in seg[k:k + 220]) for k in range(0, win - 220, 220)) / FS
        out.append((f, max(abs(v) for v in seg) / FS, sub))
    return out


rows, okf = [], True
for tm, t0, t1 in ((1, 80, 110), (1, 110, 80), (1, 20, 127), (1, 127, 0), (1, 10, 40), (0, 80, 110), (0, 110, 30)):
    blocks = 6 * FSR // 16
    ev = [(2 * FSR // 16, P(TIME=t1, SKEW=64, TMOD=tm, MIX=127, FDBK=0))]
    L, R, m = run('fade', P(TIME=t0, SKEW=64, TMOD=tm, MIX=127, FDBK=0), A.sine(500, 0.2, blocks * 16), blocks, events=ev)
    fq = freqs(L, int(1.8 * FSR), len(L))
    lev = sorted(a for _, a, _ in fq)[len(fq) // 2]
    fr = [(f, a) for f, a, sub in fq if sub > 0.5 * lev]       # windows clear of the FADE dip
    dev = max(abs(f - 500) for f, _ in fr) / 5
    if tm:
        okf &= dev < 1.5
    else:
        okf &= dev > 10
    rows.append(f'{"FADE" if tm else "SLEW"} TIME {t0} -> {t1} ({G.time_of(t0) * 1000:.0f} -> {G.time_of(t1) * 1000:.0f} ms):'
                f' largest deviation {dev:.2f} % (windows clear of the dip), level {min(a for _, a in fr):.3f} .. '
                f'{max(a for _, a in fr):.3f} FS, worst block {m["max_per_frame"] - A.null_cost()["max_per_frame"]:.0f}')
rep.check('time changes under a steady 500 Hz sine (FDBK 0, low-passed at 1.5 kHz for the count): FADE keeps the pitch '
          '(every 50 ms window clear of the dip -- no 5 ms stretch under half the steady level -- within 1.5 %); SLEW bends it',
          okf, '; '.join(rows))
rows, ok = [], True
for t0, t1 in ((80, 115), (115, 20)):
    tn = G.time_of(t1)
    st = 7 * FSR
    blocks = st // 16 + int(tn * FSR) // 16 + 400
    x = [0] * (blocks * 16)
    x[st:st + len(burst)] = burst
    ev = [(FSR // 16, P(TIME=t1, SKEW=64, TMOD=1, MIX=127, FDBK=0))]
    L, R, _ = run('fadet', P(TIME=t0, SKEW=64, TMOD=1, MIX=127, FDBK=0), x, blocks, events=ev)
    lag = echo_lag(L, st, tn)
    ok &= abs(lag - tn * FSR) <= 0.003 * tn * FSR + 30
    rows.append(f'{t0} -> {t1}: echo at {lag} (want {tn * FSR:.0f})')
rep.check('after a FADE change the echo comes at the new time', ok, '; '.join(rows))


# ---- 7. clicks on the other knobs ---------------------------------------------------------------------------------------
def d2max(y):
    return max(abs(a - 2 * b + c) for a, b, c in zip(y[2:], y[1:], y))


rows, ok = [], True
for knob, lo, hi in (('TONE', 30, 127), ('FDBK', 0, 100), ('MIX', 0, 127), ('GRIT', 0, 127)):
    blocks = 4 * FSR // 16
    ev = [(400 + 2 * k, P(**{knob: lo + (hi - lo) * k // 200}, TIME=90)) for k in range(201)]
    ev += [(1600, P(**{knob: lo}, TIME=90))]
    x = A.sine(440, 0.3, blocks * 16)
    L, R, _ = run('zip', P(**{knob: lo}, TIME=90), x, blocks, events=ev)
    Lc, Rc, _ = run('zipc', P(**{knob: hi}, TIME=90), x, blocks)
    Ll, Rl, _ = run('zipl', P(**{knob: lo}, TIME=90), x, blocks)
    ctl = max(d2max(Lc[300 * 16:]), d2max(Ll[300 * 16:]))
    rr = d2max(L[300 * 16:]) / max(ctl, 1)
    rows.append(f'{knob} {lo}..{hi}: largest 2nd difference {rr:.2f} x the knob held')
    ok &= rr < 1.5
rep.check('knob moves on a 440 Hz sine (TIME 90; swept over 0.73 s, then a jump back): no click (largest second '
          'difference within 1.5 x the larger of the renders with the knob held at either end)', ok, '; '.join(rows))

# ---- 8. HPF ----------------------------------------------------------------------------------------------------------------
lev = []
for hp in (0, 1):
    T = G.time_of(30)
    n = int(T * FSR)
    L, R, _ = run('hpf', P(TIME=30, SKEW=64, FDBK=110, MIX=127, HPF=hp), A.sine(80, 0.4, n // 2), (6 * n) // 16)
    lev.append(A.rms(L[4 * n:5 * n]))
rep.check('HPF: an 80 Hz tone\'s fourth repeat (FDBK 110) is far weaker with the 320 Hz feedback high-pass on',
          A.db(lev[1] / max(lev[0], 1e-9)) < -12, f'OFF {A.db(lev[0]):.1f} dBFS, ON {A.db(lev[1]):.1f} dBFS')

# ---- 9. the RAM in X: garbage, bounds --------------------------------------------------------------------------------------
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


garb = [(0x123456 + 0x9e3779 * i) & 0xffffff for i in range(0x1000)]
xm = with_x(A.with_fill(mems, 0xa5a5a5, 'ptxf0'), 'ptxf1', [(XB, garb)])
L, R, _ = run('xdirty', P(**DEAR), None, 600, mm=xm, dirty=0x4321)
rep.check('garbage in the X RAM (and the masks) + dirty r7 block + dirty Y, silence in, dearest knobs: silent (the RAM '
          'is set to the idle pattern over 8 dry calls, then the masks are written)', max(abs(v) for v in L + R) == 0,
          f'worst {max(abs(v) for v in L + R)} LSB')
gaps = (list(range(XB - 64, XB)) + list(range(XB + G.RAMW, XB + G.MASK_OFF)) + list(range(XB + G.MASK_OFF + 24, XB + 0x800))
        + list(range(XB + 0x800 + G.RAMW, XB + 0x1000)) + list(range(XB + 0x1000, XB + 0x1040)))
probe = [XB, XB + G.RAMW - 1, XB + 0x800, XB + 0x800 + G.RAMW - 1]
rnd = random.Random(11)
blocks = 3000
ev = [(k, [rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), rnd.randrange(128), 127,
           rnd.randrange(2), rnd.randrange(2), 0, rnd.randrange(2), rnd.randrange(128), rnd.randrange(128)])
      for k in range(0, blocks, 40)]
bad = used = 0
sm = with_x(mems, 'ptsent', [(XB - 64, [0x5e5e5e] * 64), (XB + 0x1000, [0x5e5e5e] * 64)])
for split in (0, 7):
    _, _, m = run(f'sent{split}', P(), A.noise(blocks * 16, 0.7, 8), blocks, mm=sm, events=ev, split=split,
                  extra=['-peekx', ','.join(f'{v:x}' for v in gaps + probe)])
    got = {int(k, 16): int(v, 16) for k, v in re.findall(r'X:0x([0-9a-f]+) = 0x([0-9a-f]+)', m['log'])}
    bad += sum(1 for v in gaps if got.get(v) not in (0x5e5e5e, 0xaaaaaa))
    used += sum(1 for v in probe if got.get(v, 0xaaaaaa) != 0xaaaaaa)
rep.check('RAM bounds: outside the two 1,835-word RAMs and the 24 masks nothing changes after automated runs (sentinels '
          'below and above, the gaps hold the clearing\'s idle words); the first / last words of both RAMs were written',
          bad == 0 and used >= 6, f'{bad} words outside changed (of {2 * len(gaps)}); RAM probes written {used} of 8')

# ---- 10. cost, width ---------------------------------------------------------------------------------------------------
nul = A.null_cost()
blocks = 6000
ev = [(k, P(TIME=(0, 127, 20, 110)[(k // 600) % 4], SKEW=0, TMOD=1, FDBK=127, TONE=127, HPF=1, MIX=127, GRIT=127))
      for k in range(0, blocks, 600)]
_, _, m = run('costf', P(TIME=0, SKEW=0, TMOD=1, FDBK=127, TONE=127, HPF=1, MIX=127, GRIT=127), A.noise(blocks * 16, 0.5),
              blocks, events=ev)
cf = m['max_per_frame'] - nul['max_per_frame']
ev = [(k, P(TIME=(0, 60)[(k // 300) % 2], SKEW=0, TMOD=0, FDBK=127, TONE=127, HPF=1, MIX=127, GRIT=127))
      for k in range(0, blocks, 300)]
_, _, m = run('costs', P(TIME=0, SKEW=0, FDBK=127, TONE=127, HPF=1, MIX=127, GRIT=127), A.noise(blocks * 16, 0.5),
              blocks, events=ev)
cs_ = m['max_per_frame'] - nul['max_per_frame']
rep.note('cost', f'dearest settings: mean {cost[1]:.1f}, worst block {cost[0]:.1f}; FADE transitions: worst block {cf:.1f}; '
              f'SLEW tap moves under 333 ms (readers at 4 bits): worst block {cs_:.1f}; random automation worst block '
              f'{cost[2]:.1f} instructions a sample')


def wrun(xx, bl):
    return run('w', P(MIX=127), xx, bl)[:2]


A.stereo_report(rep, 'defaults (TIME 80, SKEW 84 = x1.24, FDBK 44), MIX 127', wrun, True, secs=4.0, info=True)
rep.note('size', f'{len(A.assemble(ASM, KEY)[1])} words of P (+4 stub); r7 block 256 words; X RAM 2 x 1,835 words + 24 masks')
rep.save('/aeria/pt2399/test_results.md')
