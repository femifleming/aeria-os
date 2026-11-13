"""AURORA gate: tools/run.sh python3 /aeria/aurora/test.py   (writes aurora/test_results.md)

aurora_test.asm (ring at X:$10000) is rendered; aurora.asm (ring base from the word X:$213 points
to) is assembled and compared with it."""
import math
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/aurora')
import aeria as A
import gen as G
import wxlib as W
import wxtest as T

FSR = 44100
for b in G.BUILDS:
    G.build(b)
rep = A.Report('AURORA')
DEAR = {'AMT': 127, 'TIME': 127, 'TONE': 127, 'SHFT': 3}
mod, names, d, mems, init, proc, cost = T.standard(
    rep, '/aeria/aurora/aurora_test.asm', 'AERIA AURORA', '/aeria/aurora/manifest.py', DEAR, cost_limit=200,
    build_note='test build, ring at X:$10000')
NUL = A.null_cost()['max_per_frame']
ON = T.P(names, d, AMT=127)

full = [l for l in Path('/aeria/aurora/aurora.asm').read_text().splitlines() if not l.startswith(';')]
test = [l for l in Path('/aeria/aurora/aurora_test.asm').read_text().splitlines() if not l.startswith(';')]
fw = A.assemble('/aeria/aurora/aurora.asm', 'AERIA AURORAF')[1]
tw = A.assemble('/aeria/aurora/aurora_test.asm', 'AERIA AURORA')[1]
body = [l for l in full if l not in ('        move    x:>$213,r4', '        nop', '        move    x:(r4),a')]
tbody = [l for l in test if l != f'        move    #>${G.TEST_XRING:06x},a' and l != '        nop']
rep.check('aurora.asm (ring base from the word X:$213 points to) assembles (round-trip, mpysu audit); it differs '
          'from the rendered test build only in how init gets the base', body == tbody and len(fw) == len(tw) + 2,
          f'{len(fw)} words (test build {len(tw)})')


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('au_' + tag, mems, init, proc, p, x, blocks, guard=True, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def blockcost(tag):
    rows = [[int(v) for v in l.split()] for l in (A.BUILD / f'au_{tag}.meter').read_text().splitlines()]
    return [r[1] / 16 - NUL for r in rows]


# ---- the port in float: window, highpass, four lines (the curtains' levels are left out: they wander) -----
FIRST = 1 + G.CLR_BLOCKS            # the first running call (call 0 wakes, then the clearing calls)


def model(x, nblocks, time=64, tone=64, step=1, target=1.0):
    """-> the four lines' outputs o_j[n] (plugin level, i.e. x 4 of the DSP's lines), from call FIRST on."""
    Wn, H = G.WINDOW, G.HALF
    ring = [0.0] * (Wn + 8)
    lines = [[0.0] * l for l in G.LEN]
    pos = [0] * 4
    lp = [0.0] * 4
    g = [G.line_gain(j, time) for j in range(4)]
    damp = 1 - math.exp(-2 * math.pi * G.tone_hz(tone) / FSR)
    amc = W.pole(G.AMT_TAU)
    f, q2 = G.SV_F, 1 / G.HP_Q
    amt = 0.0
    c = H
    ww = 0.0
    svl = svb = 0.0
    wp = 0
    outs = [[], [], [], []]

    def win(cc):
        return math.sin(math.pi * cc / Wn) ** 2

    for b in range(nblocks):
        amt += amc * (target - amt)
        if step == 0:
            c = H
        dw = (win((c - 16 * step) % Wn) - ww) / 16
        for i in range(16):
            m = x[b * 16 + i]
            ring[wp] = m
            c = (c - step) % Wn
            t1 = ring[(wp - (c + 2)) % len(ring)]
            c2 = c + H
            if c2 >= Wn:
                c2 -= Wn
            t2 = ring[(wp - (c2 + 2)) % len(ring)]
            wp = (wp + 1) % len(ring)
            ww += dw
            up = t2 + ww * (t1 - t2)
            svl += f * svb
            hp = up - svl - q2 * svb
            svb += f * hp
            inj = 0.5 * hp * amt
            o = [lines[j][pos[j]] for j in range(4)]
            for j in range(4):
                outs[j].append(o[j])
                lp[j] += damp * (o[j] - lp[j])
            h = (0.5 * (lp[0] + lp[1] + lp[2] + lp[3]), 0.5 * (lp[0] - lp[1] + lp[2] - lp[3]),
                 0.5 * (lp[0] + lp[1] - lp[2] - lp[3]), 0.5 * (lp[0] - lp[1] - lp[2] + lp[3]))
            for j in range(4):
                lines[j][pos[j]] = g[j] * h[j] + (-inj if j & 1 else inj)
                pos[j] = (pos[j] + 1) % G.LEN[j]
    return outs


def fit(y, basis, lo, hi):
    """Least squares y ~ sum a_j basis_j over [lo, hi): (a, relative rms residual)."""
    k = len(basis)
    M = [[sum(basis[i][t] * basis[j][t] for t in range(lo, hi)) for j in range(k)] for i in range(k)]
    v = [sum(basis[i][t] * y[t] for t in range(lo, hi)) for i in range(k)]
    for i in range(k):
        p = M[i][i]
        M[i] = [u / p for u in M[i]]
        v[i] /= p
        for j in range(k):
            if j != i:
                fct = M[j][i]
                M[j] = [u - fct * w for u, w in zip(M[j], M[i])]
                v[j] -= fct * v[i]
    res = math.sqrt(sum((y[t] - sum(v[j] * basis[j][t] for j in range(k))) ** 2 for t in range(lo, hi)))
    ref = math.sqrt(sum(y[t] ** 2 for t in range(lo, hi)))
    return v, res / max(ref, 1e-30)


# ---- 1. the whole path against the float model -----------------------------------------------------------------
secs = 3
blocks = secs * FSR // 16
n = blocks * 16
xin = [round(0.6 * A.FS * (math.sin(2 * math.pi * 233.0 * i / FSR) * 0.5 + math.sin(2 * math.pi * 1171.0 * i / FSR) * 0.3)
       * (1.0 if (i // 9000) % 2 == 0 else 0.15)) for i in range(n)]
L, R, _ = run('model', ON, xin, blocks)
xm = [v / A.FS for v in xin[FIRST * 16:]]
om = model(xm, blocks - FIRST)
wl = [(a - b) / A.FS for a, b in zip(L[FIRST * 16:], xin[FIRST * 16:])]
wr = [(a - b) / A.FS for a, b in zip(R[FIRST * 16:], xin[FIRST * 16:])]
rows, ok = [], True
gains = [G.pan_gains(p) for p in G.PANS]
for t0 in (0.5, 1.5, 2.5):
    lo = int(t0 * FSR)
    hi = lo + 1200
    al, rl = fit(wl, om, lo, hi)
    ar, rr = fit(wr, om, lo, hi)
    br = [a / (G.LEVEL * gl) for a, (gl, gr) in zip(al, gains)]          # 0.2 + 0.8 brightness, from L
    br2 = [a / (G.LEVEL * gr) for a, (gl, gr) in zip(ar, gains)]         # the same, from R
    ok &= rl < 4e-3 and rr < 4e-3 and all(0.19 < v < 1.01 for v in br) and max(abs(u - v) for u, v in zip(br, br2)) < 0.03
    rows.append(f'at {t0} s: residual L {rl:.1e} R {rr:.1e}; curtain brightness (0.2 + 0.8 b) '
                + ' '.join(f'{v:.3f}' for v in br) + f' (from R within {max(abs(u - v) for u, v in zip(br, br2)):.4f})')
rep.check('two tones in steps, plugin settings, 3 s: the wet (out - in) on each side is the float model\'s four line '
          'outputs (octave window, 500 Hz highpass, 1.2 s amount glide, Hadamard lines, damping) times four levels '
          '-- 27 ms windows fitted for the levels (they drift: residual under 4e-3); the same four brightnesses come out of L and R through the pans (within 0.03)',
          ok, '; '.join(rows))

# ---- 2. an octave up, above 500 Hz -----------------------------------------------------------------------------------
secs = 4
blocks = secs * FSR // 16
n = blocks * 16
rows, ok = [], True
for fin in (110.0, 440.0, 1500.0):
    x = A.sine(fin, 0.3, n)
    L, R, _ = run(f'oct{int(fin)}', ON, x, blocks)
    w = [a + b - 2 * c for a, b, c in zip(L, R, x)][3 * FSR:3 * FSR + 22050]
    lv = {k: A.db(A.goertzel(w, fin * k) + 1e-12) for k in (1, 2, 3, 4)}
    ok &= lv[2] == max(lv.values()) and all(lv[k] < -78 for k in (1, 3, 4))
    rows.append((fin, lv))
# the highpass: the same input level, 110 Hz (220 Hz in the reverb) against 440 and 1500 Hz
f_, q_ = G.SV_F, 1 / G.HP_Q


def hp_db(fr):
    z = complex(math.cos(2 * math.pi * fr / FSR), math.sin(2 * math.pi * fr / FSR))
    # Chamberlin: lp[n] = lp[n-1] + f bp[n-1]; hp = x - lp - q bp[n-1]; bp = bp[n-1] + f hp
    # bp = f hp / (1 - z^-1); lp = f z^-1 bp / (1 - z^-1)
    i1 = 1 / (1 - 1 / z)
    hpx = 1 / (1 + q_ * f_ * i1 / z + f_ * f_ * i1 * i1 / z)
    return 20 * math.log10(abs(hpx))


def rbj_db(fr):
    w0 = 2 * math.pi * G.HP_HZ / FSR
    al = math.sin(w0) / (2 * G.HP_Q)
    b0 = (1 + math.cos(w0)) / 2
    z = complex(math.cos(2 * math.pi * fr / FSR), math.sin(2 * math.pi * fr / FSR))
    return 20 * math.log10(abs((b0 - 2 * b0 / z + b0 / z / z) / (1 + al - 2 * math.cos(w0) / z + (1 - al) / z / z)))


dev = max(abs(hp_db(fr) - rbj_db(fr)) for fr in (100, 200, 300, 500, 700, 1000, 2000, 5000, 10000, 15000))
rep.check('a -10 dBFS sine in: the curtains ring at twice its frequency (how loud depends on where it falls between the '
          'loop\'s modes); the input\'s own frequency and the other multiples stay under -78 dBFS; under 500 Hz the '
          'highpass thins it',
          ok and rows[0][1][2] < rows[1][1][2] - 6,
          '; '.join(f'{fin:.0f} Hz in: {2 * fin:.0f} Hz at {lv[2]:.1f} dB, {fin:.0f} Hz {lv[1]:.1f}, {3 * fin:.0f} Hz {lv[3]:.1f}, '
                    f'{4 * fin:.0f} Hz {lv[4]:.1f}' for fin, lv in rows)
          + f'; highpass (state variable) at 220 / 500 / 880 Hz: {hp_db(220):+.1f} / {hp_db(500):+.1f} / {hp_db(880):+.1f} dB, '
          f'within {dev:.2f} dB of the plugin\'s biquad from 100 Hz to 15 kHz')

# ---- 3. SHFT ------------------------------------------------------------------------------------------------------------
rows, ok = [], True
x = A.sine(440.0, 0.3, n)
for sh, mult in ((0, 1), (1, 2), (2, 3), (3, 4)):
    L, R, _ = run(f'sh{sh}', T.P(names, d, AMT=127, SHFT=sh), x, blocks)
    w = [a + b - 2 * c for a, b, c in zip(L, R, x)][3 * FSR:3 * FSR + 22050]
    lv = [A.db(A.goertzel(w, 440.0 * k) + 1e-12) for k in (1, 2, 3, 4)]
    ok &= lv.index(max(lv)) == mult - 1 and all(max(lv) - v > 30 for i, v in enumerate(lv) if i != mult - 1)
    rows.append(f'{G.STEPS[sh]}: ' + ' / '.join(f'{v:.0f}' for v in lv))
rep.check('SHFT 0..3 on a 440 Hz sine: the reverb rings at 440 / 880 / 1320 / 1760 Hz, the rest 30 dB under',
          ok, 'dB at 440 / 880 / 1320 / 1760 Hz -- ' + '; '.join(rows))


# ---- 4. decay --------------------------------------------------------------------------------------------------------------
def t60_theory(fr, time, lens, damp_hz):
    a = 1 - math.exp(-2 * math.pi * damp_hz / FSR)
    w = 2 * math.pi * fr / FSR
    hd = a / math.sqrt(1 - 2 * (1 - a) * math.cos(w) + (1 - a) ** 2)
    per_s = 60 / G.t60(time) + -20 * math.log10(hd) * FSR / (sum(lens) / len(lens))
    return 60 / per_s


def decay(tag, p, fin, hold, secs):
    blocks_ = int(secs * FSR / 16)
    n_ = blocks_ * 16
    stop = int(hold * FSR)
    x_ = A.sine(fin, 0.3, stop) + [0] * (n_ - stop)
    L_, R_, _ = run(tag, p, x_, blocks_)
    pts = []
    ring = [a + b for a, b in zip(L_, R_)]
    for s0 in range(stop + 2205, n_ - 4410, 2205):
        pts.append(((s0 - stop) / FSR, A.db(A.goertzel(ring[s0:s0 + 4410], 2 * fin) + 1e-12)))
    top = pts[0][1]
    use = [(t, v) for t, v in pts if top - 40 < v < top - 3 and v > -110]
    mt = sum(t for t, _ in use) / len(use)
    mv = sum(v for _, v in use) / len(use)
    slope = sum((t - mt) * (v - mv) for t, v in use) / sum((t - mt) ** 2 for t, _ in use)
    return -60 / slope, pts


rows, ok = [], True
for tv, secs_ in ((32, 9), (64, 14), (96, 22)):
    got, pts = decay(f'dec{tv}', T.P(names, d, AMT=127, TIME=tv), 440.0, 4.0, secs_)
    want = t60_theory(880.0, tv, G.LEN, G.tone_hz(64))
    plug = t60_theory(880.0, tv, G.PLUGIN_LEN, G.DAMP_HZ)
    ok &= abs(got / want - 1) < 0.15
    rows.append(f'TIME {tv} (T60 {G.t60(tv):.1f} s): 880 Hz dies away in {got:.2f} s (this loop\'s theory {want:.2f} s; the '
                f'plugin\'s full-length loop {plug:.2f} s)')
rep.check('decay: a 440 Hz sine stops, the 880 Hz ring falls 60 dB in the set time less what the damping takes at '
          '880 Hz (level at 880 Hz in 100 ms windows, slope fitted from -3 to -40 dB), within 15 %', ok, '; '.join(rows))
rows, ok = [], True
for tn in (20, 64, 110):
    got, _ = decay(f'tone{tn}', T.P(names, d, AMT=127, TONE=tn), 1500.0, 4.0, 12)
    want = t60_theory(3000.0, 64, G.LEN, G.tone_hz(tn))
    plug = t60_theory(3000.0, 64, G.PLUGIN_LEN, G.tone_hz(tn) / G.DAMP_UP)
    ok &= abs(got / want - 1) < 0.3
    rows.append(f'TONE {tn} (plugin lowpass {G.tone_hz(tn) / G.DAMP_UP:.0f} Hz, this loop\'s {G.tone_hz(tn):.0f} Hz): 3 kHz '
                f'dies away in {got:.2f} s (theory {want:.2f}; the plugin\'s loop with that lowpass {plug:.2f})')
rep.check('TONE: the highs die sooner with the knob down (1.5 kHz sine in, 3 kHz ringing), within 30 % of a rough theory (the set T60 and the mean lowpass loss a turn; single modes differ)',
          ok, '; '.join(rows))

# ---- 5. the curtains wander; AMT down: the tail rings out, then rest ------------------------------------------------------
secs = 40
blocks = secs * FSR // 16
n = blocks * 16
off = 20 * FSR // 16
x = A.noise(n, 0.25, 12)
ev = [(0, ON), (off, T.P(names, d, AMT=0))]
L, R, _ = run('long', ON, x, blocks, events=ev)
bc = blockcost('long')
wl = [a - b for a, b in zip(L, x)]
wr = [a - b for a, b in zip(R, x)]
bal = []
for s0 in range(4 * FSR, off * 16 - FSR, FSR // 2):
    bal.append(A.db(A.rms(wl[s0:s0 + FSR // 2]) / A.rms(wr[s0:s0 + FSR // 2])))
c = A.corr(wl[5 * FSR:15 * FSR], wr[5 * FSR:15 * FSR])
lvl = A.db(A.rms(wl[10 * FSR:19 * FSR] + wr[10 * FSR:19 * FSR]) / A.rms(x[10 * FSR:19 * FSR]))
rest = next((b for b in range(off, blocks) if bc[b] < 2), None)
exact = rest is not None and all(L[i] == x[i] and R[i] == x[i] for i in range(rest * 16, n))
before = max(abs(v) for v in wl[rest * 16 - 16:rest * 16] + wr[rest * 16 - 16:rest * 16]) if rest else -1
rep.check('0.25 FS noise, plugin settings: the curtains sit apart (the plugin pans: +-0.60 and +-0.21, equal power) and their balance wanders; '
          'AMT to 0 at 20 s: the amount glides out (1.2 s), the tail rings on with the dry sound untouched, and when it '
          'has been under 512 LSB for half a second the routine rests, bit-exact',
          c < 0.9 and max(bal) - min(bal) > 1.0 and exact and before <= 512,
          f'wet {lvl:+.1f} dB re the input; L/R correlation {c:+.2f}; L re R level over 0.5 s steps {min(bal):+.1f} .. '
          f'{max(bal):+.1f} dB; at rest {(rest - off) * 16 / FSR:.1f} s after the knob (wet then {before} LSB), '
          f'rest cost {max(bc[rest + 1:]):.2f} a sample')
blocks = 6 * FSR // 16
L, R, _ = run('sil', ON, None, blocks)
rep.check('silence in, AMT 127: exactly 0 out', max(abs(v) for v in L + R) == 0)

# ---- 6. a minute of noise and silence ------------------------------------------------------------------------------------------
T.minute(rep, 'au', mems, init, proc, ON, quiet_db=-80, note='plugin settings: T60 9 s, 20 s of tail')
T.minute(rep, 'aud', mems, init, proc, T.P(names, d, **DEAR), amp=1.0,
         note='dearest settings, full-scale noise: T60 35 s (the rider keeps the lines out of their limit: check 7)')

# ---- 7. quality pass (12 Nov): the rider on what goes into the lines -------------------------------------------------------
TRK = [G.ST[f'LP{j}'] for j in range(4)] + [G.ST['RIDE']]


def tracked(tag, p, x, blocks):
    tr = A.BUILD / f'au_{tag}.track'
    run(tag, p, x, blocks, extra=['-track', ','.join(f'{v:x}' for v in TRK), '-trackout', str(tr)])
    pk, rmin, sat = 0, 1.0, 0
    for l in tr.read_text().splitlines()[FIRST + 2:]:
        v = [int(u) for u in l.split()[1:]]
        v = [u - (1 << 24) if u & 0x800000 else u for u in v]
        pk = max(pk, max(abs(u) for u in v[:4]))
        sat += max(abs(u) for u in v[:4]) >= 0x7f0000
        rmin = min(rmin, v[4] / A.FS)
    return pk / A.FS, rmin, sat


rows = []
ok = True
for tag, p, amp, secs in (('rq25', ON, 0.25, 10), ('rq90', ON, 0.9, 20),
                          ('rqd', T.P(names, d, **DEAR), 1.0, 40), ('rqt', T.P(names, d, AMT=127, TIME=127, TONE=127), 1.0, 40)):
    blocks = int(secs * FSR / 16)
    pk, rmin, sat = tracked(tag, p, A.noise(blocks * 16, amp, 9), blocks)
    rows.append(f'{tag} ({amp} FS noise {secs} s): line peak {pk:.3f}, rider down to {rmin:.3f}, calls with a line at its '
                f'limit {sat}')
    ok = ok and sat == 0 and (tag != 'rq25' or rmin > 0.999)
rep.check('the rider: the lines (their damped reads, sampled a call) never reach their limit, even at T60 35 s on full-scale '
          'noise (before the quality pass, 1.0 FS noise for 40 s at TIME 127 TONE 127: line peak 0.998 / 0.999 and 5 / 4 calls at the limit); at the plugin\'s '
          'settings on 0.25 FS noise it never moves', ok, '; '.join(rows))
rep.note('size', f'{len(fw)} words of P; r7 state {len(G.ST.at)} words; X ring {G.RING} words (base a multiple of {G.RING}): '
                 f'window {G.WINDOW} frames, lines {" ".join(str(v) for v in G.LEN)} frames (the plugin at 44.1 kHz: '
                 f'{" ".join(str(v) for v in G.PLUGIN_LEN)}, x {G.SCALE:.4f})')
rep.note('cost', f'dsp_host mean {cost[0]:.1f}, worst block {cost[1]:.1f} instructions a sample at the dearest settings; '
                 'at rest (AMT 0, tail gone) 7 instructions a call')
rep.save('/aeria/aurora/test_results.md')
