"""HAIL gate: tools/run.sh python3 /aeria/hail/test.py   (writes hail/test_results.md)

hail_test.asm (memory at X:$10000) is rendered; hail.asm (base from the word X:$213 points to) is
assembled and compared with it. Every stone's state words are read back a call at a time
(dsp_host -track), so births, hits, gaps, ratios, speeds and pans are counted, not guessed."""
import math
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/hail')
import aeria as A
import gen as G
import wxtest as T

FSR = 44100
for b in G.BUILDS:
    G.build(b)
rep = A.Report('HAIL')
DEAR = {'AMT': 127, 'RATE': 127, 'SIZE': 127, 'BNCE': 0, 'WDTH': 127}
mod, names, d, mems, init, proc, cost = T.standard(
    rep, '/aeria/hail/hail_test.asm', 'AERIA HAIL', '/aeria/hail/manifest.py', DEAR, cost_limit=200,
    build_note='test build, memory at X:$10000')
NUL = A.null_cost()['max_per_frame']
ON = T.P(names, d, AMT=127)

full = [l for l in Path('/aeria/hail/hail.asm').read_text().splitlines() if not l.startswith(';')]
test = [l for l in Path('/aeria/hail/hail_test.asm').read_text().splitlines() if not l.startswith(';')]
fw = A.assemble('/aeria/hail/hail.asm', 'AERIA HAILF')[1]
tw = A.assemble('/aeria/hail/hail_test.asm', 'AERIA HAIL')[1]


def strip(ls, drop):
    out = list(ls)
    for dl in drop:
        out.remove(dl)
    return out


rep.check('hail.asm (base from the word X:$213 points to) assembles (round-trip, mpysu audit); it differs from the '
          'rendered test build only in how init gets the base',
          strip(full, ['        move    x:>$213,r4', '        nop', '        move    x:(r4),a'])
          == strip(test, [f'        move    #>${G.TEST_XBASE:06x},a']) and len(fw) == len(tw) + 2,
          f'{len(fw)} words (test build {len(tw)})')

FLD = ('MODE', 'CNT', 'RATE2', 'LB', 'GAP', 'RATIO', 'HITS', 'PL', 'PR', 'HG')
OFFS = [G.SBASE + G.STRUCT * k + G.FIELDS.index(f) for k in range(G.STONES) for f in FLD]


def sgn(v):
    return v - (1 << 24) if v & 0x800000 else v


def run(tag, p, x, blocks, **kw):
    trk = f'/aeria/.build/hl_{tag}.track'
    L, R, m = A.render('hl_' + tag, mems, init, proc, p, x, blocks, guard=True,
                       extra=['-track', ','.join(f'{o:x}' for o in OFFS), '-trackout', trk], **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    rows = [[int(v) for v in l.split()] for l in open(trk).read().splitlines() if l.strip() and not l.startswith('#')]
    st = [[dict(zip(FLD, r[1 + k * len(FLD):1 + (k + 1) * len(FLD)])) for k in range(G.STONES)] for r in rows]
    return L, R, m, st


def blockcost(tag):
    rows = [[int(v) for v in l.split()] for l in (A.BUILD / f'hl_{tag}.meter').read_text().splitlines()]
    return [r[1] / 16 - NUL for r in rows]


def lives(st):
    """Every stone's life from the tracked words: dict(born, k, rate, lb, ratio, pan, hits=[blocks], gap0, end)."""
    out = []
    for k in range(G.STONES):
        cur = None
        prev = dict(MODE=0, CNT=0)
        for b, row in enumerate(st):
            s = row[k]
            if s['MODE'] == 1 and prev['MODE'] != 1:
                pl, pr = s['PL'] / 2 ** 23, s['PR'] / 2 ** 23
                cur = dict(born=b, k=k, rate=s['RATE2'] / 2, lb=s['LB'], ratio=s['RATIO'] / 2 ** 23,
                           pan=4 * math.atan2(pr, pl) / math.pi - 1, gap0=s['GAP'], nhits=s['HITS'], hits=[], end=None,
                           pl=pl, pr=pr)
                out.append(cur)
            if cur is not None and s['MODE'] == 2 and s['CNT'] > 0 and (prev['MODE'] != 2 or prev['CNT'] == 0):
                cur['hits'].append(b)
            if cur is not None and s['MODE'] == 0 and prev['MODE'] == 2:
                cur['end'] = b
                cur = None
            prev = s
    return sorted(out, key=lambda z: z['born'])


# ---- 1. a minute of hail at the plugin's settings ---------------------------------------------------------------
secs = 60
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(660.0, 0.3, n)
L, R, m, st = run('minute1', ON, x, blocks)
lv = lives(st)
done = [z for z in lv if z['end'] is not None]
per_s = len(lv) / secs
nh = [len(z['hits']) for z in done]
gap0 = [z['gap0'] / FSR * 1000 for z in lv]
slices = [z['lb'] * 16 / FSR * 1000 for z in lv]
rates = sorted({z['rate'] for z in lv})
share = {r: sum(1 for z in lv if z['rate'] == r) / len(lv) for r in rates}
air = [sum(1 for s in row if s['MODE']) for row in st]
rep.check('AMT 127, plugin settings, 60 s: stones are born about 2.2 a second (a call\'s chance 2.2 x 16/fs; none while '
          f'all {G.STONES} are in the air), each hits 4 .. 8 times', 1.6 < per_s < 2.8 and min(nh) >= 4 and max(nh) <= 8,
          f'{len(lv)} stones = {per_s:.2f} a second; hits a stone {min(nh)} .. {max(nh)} (mean {sum(nh) / len(nh):.1f}); '
          f'in the air at once: mean {sum(air) / len(air):.2f}, most {max(air)}, all {G.STONES} busy '
          f'{100 * sum(1 for v in air if v == G.STONES) / len(air):.2f} % of the time')
rep.check('their slices: 5 .. 30 ms (rate 1.5 held to 20.3 ms, rate 2 to 15.2 ms: a stone has 1,365 words), read at '
          '0.5 / 1 / 1.5 / 2 x, one in five each but rate 1 twice',
          min(slices) >= 4.9 and max(slices) <= 30.2 and rates == [0.5, 1.0, 1.5, 2.0] and 0.28 < share[1.0] < 0.52
          and all(0.1 < share[r] < 0.32 for r in (0.5, 1.5, 2.0)),
          f'slice {min(slices):.1f} .. {max(slices):.1f} ms; speeds ' + ', '.join(f'{r} x: {100 * share[r]:.0f} %' for r in rates))
# gaps: hit to hit, against first gap x ratio^k (a hit starts on a call = 16 frames; a gap is never shorter than the slice)
errs, ratios_ok, tot = [], 0, 0
ratio_meas = []
for z in done:
    g = z['gap0']
    tnext = 0.0
    for k in range(len(z['hits']) - 1):
        got = (z['hits'][k + 1] - z['hits'][k]) * 16
        want = max(g, z['lb'] * 16)
        errs.append(abs(got - want))
        if k >= 1 and g > z['lb'] * 16 + 32:
            ratio_meas.append(((z['hits'][k + 1] - z['hits'][k]) / (z['hits'][k] - z['hits'][k - 1]), z['ratio'], got))
        g = int(g * z['ratio'])
rr = [z['ratio'] for z in lv]
rerr = max(abs(a - b) * c / FSR * 1000 for a, b, c in ratio_meas)
rep.check('bounces: the first gap 90 .. 220 ms, every next gap x the stone\'s ratio (0.62 .. 0.78), quicker and quicker; '
          'hits land on calls, so a gap is within 16 frames of first gap x ratio^k (never shorter than the slice)',
          min(gap0) >= 89.9 and max(gap0) <= 220.1 and min(rr) >= 0.6199 and max(rr) <= 0.7801 and max(errs) <= 17,
          f'first gaps {min(gap0):.0f} .. {max(gap0):.0f} ms; ratios {min(rr):.3f} .. {max(rr):.3f}; {len(errs)} gaps, '
          f'worst {max(errs)} frames from the law; gap ratio as heard against the stone\'s ratio: within '
          f'{rerr:.2f} ms of gap on {len(ratio_meas)} pairs')
pans = [z['pan'] for z in lv]
lr = sum(1 for p in pans if p < 0) / len(pans)
rep.check('where they fall: pan = random x 0.9 (WDTH 114), equal power', -0.91 < min(pans) < -0.6 and 0.6 < max(pans) < 0.91
          and 0.35 < lr < 0.65 and all(abs(z['pl'] ** 2 + z['pr'] ** 2 - 0.5) < 2e-3 for z in lv),
          f'pan {min(pans):+.2f} .. {max(pans):+.2f}, mean {sum(pans) / len(pans):+.2f}, {100 * lr:.0f} % left of centre; '
          f'gl^2 + gr^2 = 2 within {max(abs(2 * (z["pl"] ** 2 + z["pr"] ** 2) - 1) for z in lv) * 2:.4f}')


# ---- 2. one stone's hits against the plugin's arithmetic ----------------------------------------------------------
def stone_model(xm, z, hit):
    """Float: the slice recorded from call z['born'], and hit number `hit` as (L, R) added to the output."""
    r2 = int(z['rate'] * 2)
    lb = z['lb']
    words = lb * 8 * r2
    rec_calls = (lb * r2 + 1) // 2 + 1
    uinc = math.floor((1 << 23) / words) / (1 << 23)
    atki = 2 * G.ATTACK / r2
    buf = []
    u, atk = (2 ** 23 - 1) / 2 ** 23, 0.0
    s0 = z['born'] * 16
    for j in range(rec_calls * 16):
        u = max(0.0, u - uinc)
        atk = min(1.0, atk + atki)
        buf.append(xm[s0 + j] * atk * u * u)
    hg = G.HIT0 * G.HIT_FALL ** hit
    outl, outr = [], []
    for i in range(lb * 16):
        p = 1 + i * r2 / 2
        k = int(p)
        if p == k:
            v = buf[k]
        else:
            v = (9 * (buf[k] + buf[k + 1]) - (buf[k - 1] + buf[k + 2])) / 16
        outl.append(2 * z['pl'] * hg * v)
        outr.append(2 * z['pr'] * hg * v)
    return outl, outr, rec_calls


xm = [v / A.FS for v in x]
wl = [(a - b) / A.FS for a, b in zip(L, x)]
wr = [(a - b) / A.FS for a, b in zip(R, x)]
rows, ok, seen = [], True, set()
for z in done:
    if z['rate'] in seen:
        continue
    # alone in the air for its first two hits?
    b0, b1 = z['born'], z['hits'][1] + z['lb']
    if any(o is not z and o['born'] < b1 and (o['end'] or 10 ** 9) > b0 for o in lv):
        continue
    seen.add(z['rate'])
    for h in (0, 1):
        ml, mr, rc = stone_model(xm, z, h)
        s0 = z['hits'][h] * 16
        e = max(max(abs(a - wl[s0 + i]) for i, a in enumerate(ml)), max(abs(a - wr[s0 + i]) for i, a in enumerate(mr)))
        pk = max(max(abs(v) for v in ml), max(abs(v) for v in mr))
        ok &= e < 2e-5 and (h or z['hits'][0] - z['born'] == rc)
        rows.append(f'rate {z["rate"]} x, {z["lb"] * 16 / FSR * 1000:.1f} ms, pan {z["pan"]:+.2f}, hit {h + 1}: peak {pk:.4f}, '
                    f'max |difference| {e:.1e}')
rep.check('stones that were alone in the air, first and second hit, against a float model of the plugin\'s stone '
          '(slice x min(1, attack) x (1 - t)^2 recorded at birth, read at its speed with Hermite on the half frames, '
          'x 0.9 x 0.72^hit, equal-power pan): the added sound is the model\'s; the first hit follows the recording at once',
          ok and len(seen) >= 3, '; '.join(rows) + f' ({len(seen)} of the 4 speeds found alone)')

# ---- 3. the knobs ---------------------------------------------------------------------------------------------------
secs = 30
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(660.0, 0.3, n)
rows, ok = [], True
for rv in (32, 64, 96):
    _, _, _, st_ = run(f'rate{rv}', T.P(names, d, AMT=127, RATE=rv), x, blocks)
    born = len(lives(st_))
    want = G.per_second(rv) * secs
    ok &= (0.6 * want - 3 < born < 1.4 * want + 3) if rv < 96 else born > 0.5 * want
    rows.append(f'RATE {rv}: {born / secs:.2f} a second (law {G.per_second(rv):.2f})')
_, _, _, st_ = run('half', T.P(names, d, AMT=64), x, blocks)
born = len(lives(st_))
ok &= 0.5 * G.per_second(64, 64 / 127) * secs - 3 < born < 1.5 * G.per_second(64, 64 / 127) * secs + 3
rows.append(f'AMT 64, RATE 64: {born / secs:.2f} a second (law {G.per_second(64, 64 / 127):.2f})')
rep.check('RATE and AMT, 30 s each: births follow 2.2 x 2^((v - 64)/16) x amt (at 8.8 a second the 6 stones are often '
          'all in the air: fewer are born)', ok, '; '.join(rows))
rows, ok = [], True
for sv in (0, 127):
    _, _, _, st_ = run(f'size{sv}', T.P(names, d, AMT=127, SIZE=sv), x, blocks)
    z_ = lives(st_)
    sl = [z['lb'] * 16 / FSR * 1000 for z in z_ if z['rate'] == 1.0]
    k = G.size_scale(sv)
    ok &= min(sl) >= 5 * k - 0.8 and max(sl) <= min(30 * k, G.LB_CAP[2] * 16 / FSR * 1000) + 0.8
    rows.append(f'SIZE {sv} (x {k:.2f}): rate-1 slices {min(sl):.1f} .. {max(sl):.1f} ms')
for bv in (0, 127):
    _, _, _, st_ = run(f'bnce{bv}', T.P(names, d, AMT=127, BNCE=bv), x, blocks)
    z_ = lives(st_)
    lo, hi = G.ratio_range(bv)
    rr_ = [z['ratio'] for z in z_]
    life = [(z['end'] - z['born']) * 16 / FSR for z in z_ if z['end']]
    ok &= min(rr_) >= lo - 1e-3 and max(rr_) <= hi + 1e-3
    rows.append(f'BNCE {bv}: ratios {min(rr_):.3f} .. {max(rr_):.3f} (law {lo:.2f} .. {hi:.2f}), a stone lasts '
                f'{sum(life) / len(life):.2f} s')
_, _, _, st_ = run('wdth0', T.P(names, d, AMT=127, WDTH=0), x, blocks)
p0 = [z['pan'] for z in lives(st_)]
_, _, _, st_ = run('wdth127', T.P(names, d, AMT=127, WDTH=127), x, blocks)
p1 = [z['pan'] for z in lives(st_)]
ok &= max(abs(v) for v in p0) < 1e-3 and max(abs(v) for v in p1) > 0.9
rows.append(f'WDTH 0: pans within {max(abs(v) for v in p0):.4f} of centre; WDTH 127: {min(p1):+.2f} .. {max(p1):+.2f}')
rep.check('SIZE, BNCE, WDTH at their ends', ok, '; '.join(rows))

# ---- 4. AMT to 0: no more births, the ones in the air finish, then rest --------------------------------------------------
secs = 12
blocks = secs * FSR // 16
n = blocks * 16
off = 6 * FSR // 16
x = A.noise(n, 0.3, 21)
ev = [(0, T.P(names, d, AMT=127, RATE=115)), (off, T.P(names, d, AMT=0, RATE=115))]
L, R, m, st = run('off', T.P(names, d, AMT=127, RATE=115), x, blocks, events=ev)
bc = blockcost('off')
lv = lives(st)
late = [z for z in lv if z['born'] >= off]
flying = [z for z in lv if z['born'] < off and (z['end'] or 10 ** 9) > off]
rest = next((b for b in range(off, blocks) if bc[b] < 2), None)
exact = rest is not None and all(L[i] == x[i] and R[i] == x[i] for i in range(rest * 16, n))
rep.check('AMT 127 -> 0 at 6 s (RATE 115): none born after it, the stones in the air bounce out, then the routine rests '
          'and output = input bit for bit', not late and exact and flying and all(z['end'] for z in flying),
          f'{len(flying)} in the air at the knob, the last ended {(max([z["end"] for z in flying] + [off]) - off) * 16 / FSR:.2f} s later; '
          f'rest from {(rest - off) * 16 / FSR:.2f} s, rest cost {max(bc[rest + 1:]):.2f} a sample')
L, R, m, st = run('sil', T.P(names, d, **DEAR), None, 6 * FSR // 16)
rep.check('silence in, dearest settings: exactly 0 out (stones of silence)', max(abs(v) for v in L + R) == 0,
          f'{len(lives(st))} stones born')

# ---- 5. a minute of noise and silence at the dearest settings ---------------------------------------------------------------
blocks = 60 * FSR // 16
n = blocks * 16
stop = 40 * FSR
x = A.noise(stop, 0.9, 9) + [0] * (n - stop)
L, R, m, st = run('minute2', T.P(names, d, **DEAR), x, blocks)
bc = blockcost('minute2')
air = [sum(1 for s in row if s['MODE']) for row in st]
snd = [sum(1 for s in row if s['MODE'] == 2 and s['CNT'] > 0) for row in st]
sat = sum(1 for v in L + R if abs(v) >= A.FS - 1)
tail = max(abs(v) for v in L[stop + 3 * FSR:] + R[stop + 3 * FSR:])
rep.check('60 s at the dearest settings (RATE 127 = 34 a second, SIZE 127, BNCE 0): 0.9 FS noise for 40 s, then silence '
          '-- bounded, no stray write, silent 3 s after the input (the slices in the air were recorded from sound)',
          A.peak(L + R) <= 1.0 + 1e-6 and tail == 0,
          f'peak {A.peak(L + R):.4f} FS ({sat} samples at the output limit: stones are added to 0.9 FS), '
          f'{len(lives(st))} stones; in the air mean {sum(air) / len(air):.2f} of {G.STONES}, sounding at once mean '
          f'{sum(snd) / len(snd):.2f}, most {max(snd)}; cost mean {sum(bc) / len(bc):.1f}, worst call {max(bc):.1f} a sample')
rep.note('size', f'{len(fw)} words of P; r7 state {G.SBASE + G.STONES * G.STRUCT} words (8 + {G.STONES} stones x {G.STRUCT}); '
                 f'X {G.TOTAL} words ({G.STONES} x {G.SLOT})')
rep.note('cost', f'dsp_host, dearest settings: mean {cost[0]:.1f}, worst call {cost[1]:.1f} instructions a sample over 2.2 s; over '
                 f'the minute above mean {sum(bc) / len(bc):.1f}, worst call {max(bc):.1f} (six stones sounding and one being '
                 'born); at rest 4 instructions a call')
rep.save('/aeria/hail/test_results.md')
