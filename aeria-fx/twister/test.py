"""TWISTER gate: tools/run.sh python3 /aeria/twister/test.py   (writes twister/test_results.md)

twister_test.asm (ring at X:$10000) is rendered; twister.asm (ring base from the word X:$213 points
to) is assembled and compared with it."""
import math
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/twister')
import aeria as A
import gen as G
import wxtest as T

FSR = 44100
for b in G.BUILDS:
    G.build(b)
rep = A.Report('TWISTER')
DEAR = {'AMT': 127, 'RISE': 12, 'SPIN': 127, 'TIME': 127}
mod, names, d, mems, init, proc, cost = T.standard(
    rep, '/aeria/twister/twister_test.asm', 'AERIA TWISTER', '/aeria/twister/manifest.py', DEAR, cost_limit=200,
    build_note='test build, ring at X:$10000')
NUL = A.null_cost()['max_per_frame']

full = [l for l in Path('/aeria/twister/twister.asm').read_text().splitlines() if not l.startswith(';')]
test = [l for l in Path('/aeria/twister/twister_test.asm').read_text().splitlines() if not l.startswith(';')]
fw = A.assemble('/aeria/twister/twister.asm', 'AERIA TWISTERF')[1]
tw = A.assemble('/aeria/twister/twister_test.asm', 'AERIA TWISTER')[1]
body = [l for l in full if l not in ('        move    x:>$213,r4', '        nop', '        move    x:(r4),a')]
tbody = [l for l in test if l != f'        move    #>${G.TEST_XRING:06x},a' and l != '        nop']
rep.check('twister.asm (ring base from the word X:$213 points to) assembles (round-trip, mpysu audit); it differs '
          'from the rendered test build only in how init gets the base', body == tbody and len(fw) == len(tw) + 2,
          f'{len(fw)} words (test build {len(tw)}: move #>$010000,a in place of move x:>$213,r4 / nop / move x:(r4),a)')


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('tw_' + tag, mems, init, proc, p, x, blocks, guard=True, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def blockcost(tag):
    rows = [[int(v) for v in l.split()] for l in (A.BUILD / f'tw_{tag}.meter').read_text().splitlines()]
    return [r[1] / 16 - NUL for r in rows]


def span(bc):
    on = [i for i, v in enumerate(bc) if v > 50]
    return (on[0], on[-1] + 1) if on else (0, 0)


# ---- 1. one event at the plugin's settings (TIME 0 = RND), 1 kHz sine: pitch, spin, level -----------------
f0 = 1000.0
secs = 23
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(f0, 0.4, n)
GO = T.P(names, d, AMT=127)
L, R, _ = run('event', GO, x, blocks)
bc = blockcost('event')
b0, b1 = span(bc)
dur = (b1 - b0) * 16 / FSR
exact = all(L[i] == x[i] and R[i] == x[i] for i in range(b1 * 16, n)) and all(L[i] == x[i] for i in range(0, b0 * 16))
rep.check('AMT 127, TIME 0 (RND): one event of 10 .. 20 s starts after the 16 clearing calls, runs by itself with the '
          'knob left at 127, ends, and does not start again; before and after it output = input bit for bit, at rest',
          10.0 <= dur <= 20.0 and exact and max(bc[b1 + 1:]) < 2 and b0 == 17,
          f'event {dur:.2f} s (calls {b0} .. {b1}); rest cost after it {max(bc[b1 + 1:]):.2f} a sample')
# pitch from the mono sum's zero crossings in 50 ms steps (the two Hann taps beat a little: median of 5)
M = [a + b for a, b in zip(L, R)]
step = 2205
semis, lvl, pan, tt = [], [], [], []
for s0 in range(b0 * 16, b1 * 16 - step, step):
    seg = M[s0:s0 + step]
    zc = [i + a / (a - b) for i, (a, b) in enumerate(zip(seg, seg[1:])) if a < 0 <= b]
    f = (len(zc) - 1) * FSR / (zc[-1] - zc[0]) if len(zc) > 2 else f0
    semis.append(12 * math.log2(f / f0))
    lvl.append(math.sqrt((A.rms(L[s0:s0 + step]) ** 2 + A.rms(R[s0:s0 + step]) ** 2) / 2) / (0.4 / math.sqrt(2)))
    pan.append(A.rms(L[s0:s0 + step]) - A.rms(R[s0:s0 + step]))
    tt.append((s0 - b0 * 16) / FSR / dur)
med = [sorted(semis[max(0, i - 2):i + 3])[len(semis[max(0, i - 2):i + 3]) // 2] for i in range(len(semis))]
want = [G.event(u + step / 2 / FSR / dur)[1] for u in tt]
# (where the amount is under 0.6 the dry sound, unshifted, leads the mono sum: compared where the wet leads)
wet = [G.event(u + step / 2 / FSR / dur)[0] > 0.6 for u in tt]
err = max(abs(a - b) for a, b, w_ in zip(med, want, wet) if w_)
top = max(med)
utop = tt[med.index(top)]
rep.check('pitch over the event (1 kHz sine, mono sum, 50 ms steps): climbs as 7 x u^1.3, turns in the last 15 % and is '
          'back at the end; within 0.35 semitone of the plugin\'s curve wherever the wet leads (amount over 0.6)',
          err < 0.35 and 5.0 < top < 6.2 and abs(med[-1]) < 0.3,
          f'top {top:+.2f} semitones at u = {utop:.2f} (plugin: {max(want):+.2f}); at u 0.25 / 0.5 / 0.75: '
          + ' / '.join(f'{med[min(range(len(tt)), key=lambda i: abs(tt[i] - u))]:+.2f}' for u in (0.25, 0.5, 0.75))
          + f' (plugin {G.event(0.25)[1]:+.2f} / {G.event(0.5)[1]:+.2f} / {G.event(0.75)[1]:+.2f}); largest difference {err:.2f}; '
          f'u^1.3 is a cubic here, off by at most {7 * G.POW_ERR * 100:.0f} cents')
# spin: zero crossings of (L level - R level) in 10 ms steps -> side-to-side rate
step2 = 441
pn = []
for s0 in range(b0 * 16, b1 * 16 - step2, step2):
    pn.append(A.rms(L[s0:s0 + step2]) - A.rms(R[s0:s0 + step2]))
cross = [i for i, (a, b) in enumerate(zip(pn, pn[1:])) if a < 0 <= b]
rates = [(0.5 * (c0 + c1) * step2 / FSR / dur, FSR / step2 / (c1 - c0)) for c0, c1 in zip(cross, cross[1:])]


def rate_at(u):
    near = [r for (uu, r) in rates if abs(uu - u) < 0.06]
    return sum(near) / len(near) if near else float('nan')


fast = max(r for _, r in rates)
rows = ' / '.join(f'{rate_at(u):.2f}' for u in (0.15, 0.4, 0.6, 0.8))
wantr = ' / '.join(f'{G.event(u)[2]:.2f}' for u in (0.15, 0.4, 0.6, 0.8))
okr = all(abs(rate_at(u) - G.event(u)[2]) < 0.25 + 0.1 * G.event(u)[2] for u in (0.4, 0.6, 0.8))
depth = max(abs(v) for v in pan) / (0.4 / math.sqrt(2))
rep.check('spin over the event: the sound goes from side to side at 0.3 + 5 u^2 Hz, faster and faster, and swells',
          okr and 3.2 < fast < 4.6 and max(lvl) < 1.45,
          f'side-to-side rate at u 0.15 / 0.4 / 0.6 / 0.8: {rows} Hz (plugin {wantr}); fastest {fast:.2f} Hz '
          f'(plugin: {max(G.event(i / 200)[2] for i in range(201)):.2f} at u = 0.87); L - R level swings {depth:.2f} of the input; '
          f'level of the two sides {min(lvl):.2f} .. {max(lvl):.2f} x the input')

# ---- 2. the trigger ------------------------------------------------------------------------------------------
secs = 12
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(f0, 0.4, n)
FIX = T.P(names, d, AMT=127, TIME=1)          # 2.04 s
LOW = T.P(names, d, AMT=0, TIME=1)
MID = T.P(names, d, AMT=63, TIME=1)
s_ = FSR // 16
ev = [(0, MID), (1 * s_, FIX), (1 * s_ + 40, LOW), (2 * s_, FIX), (2 * s_ + 400, MID), (6 * s_, T.P(names, d, AMT=64, TIME=1)),
      (9 * s_, LOW)]
L, R, _ = run('trig', MID, x, blocks, events=ev)
bc = blockcost('trig')
runs = []
i = 0
while i < len(bc):
    if bc[i] > 50:
        j = i
        while j < len(bc) and bc[j] > 50:
            j += 1
        runs.append((i * 16 / FSR, (j - i) * 16 / FSR))
        i = j
    else:
        i += 1
want_len = G.seconds(1)
rep.check('trigger (TIME 1 = 2.04 s): 63 does nothing; 127 at 1 s starts one; back to 0 and 127 again at 2 s while it '
          'runs: ignored; left under 64, then 64 at 6 s: the second one; held there: no third',
          len(runs) == 2 and abs(runs[0][0] - 1.0) < 0.02 and abs(runs[1][0] - 6.0) < 0.02
          and all(abs(r[1] - want_len) < 0.02 for r in runs),
          '; '.join(f'event at {a:.3f} s for {b:.3f} s' for a, b in runs) + f' (TIME 1 = {want_len:.3f} s)')
rows, ok = [], True
for tv in (1, 32, 64):
    bl = int((G.seconds(tv) + 1) * FSR / 16)
    run(f'time{tv}', T.P(names, d, AMT=127, TIME=tv), None, bl)
    c0, c1 = span(blockcost(f'time{tv}'))
    got = (c1 - c0) * 16 / FSR
    ok &= abs(got / G.seconds(tv) - 1) < 0.005
    rows.append(f'TIME {tv}: {got:.3f} s (2 s x 2^(v/32) = {G.seconds(tv):.3f})')
lens = []
for k in range(4):
    # RND draws from the effect's own generator: start k + 1 events in a row, time the last
    evs = []
    t = 0
    for j in range(k + 1):
        evs += [(t, T.P(names, d, AMT=127, RISE=0, SPIN=0)), (t + 4, T.P(names, d, AMT=0, RISE=0, SPIN=0))]
        t += int(21 * FSR / 16)
    if k == 3:
        run('rnd', T.P(names, d, AMT=0), None, t, events=evs)
        bcr = blockcost('rnd')
        i = 0
        while i < len(bcr):
            if bcr[i] > 50:
                j = i
                while j < len(bcr) and bcr[j] > 50:
                    j += 1
                lens.append((j - i) * 16 / FSR)
                i = j
            else:
                i += 1
rep.check('TIME: 1 / 32 / 64 give 2 s x 2^(v/32) within 0.5 %; TIME 0 (RND), four events in a row: each 10 .. 20 s, '
          'not the same', ok and len(lens) == 4 and all(10 <= v <= 20 for v in lens) and len({round(v, 1) for v in lens}) >= 3,
          '; '.join(rows) + '; RND: ' + ', '.join(f'{v:.2f} s' for v in lens))

# ---- 3. RISE and SPIN -----------------------------------------------------------------------------------------
secs = 5
blocks = secs * FSR // 16
n = blocks * 16
x = A.sine(f0, 0.4, n)
rows, ok = [], True
for rise in (0, 7, 12):
    L, R, _ = run(f'rise{rise}', T.P(names, d, AMT=127, RISE=rise, SPIN=0, TIME=32), x, blocks)
    c0, c1 = span(blockcost(f'rise{rise}'))
    M = [a + b for a, b in zip(L, R)]
    best = 0.0
    for s0 in range(c0 * 16, c1 * 16 - 2205, 2205):
        seg = M[s0:s0 + 2205]
        zc = [i + a / (a - b) for i, (a, b) in enumerate(zip(seg, seg[1:])) if a < 0 <= b]
        if len(zc) > 2:
            best = max(best, 12 * math.log2((len(zc) - 1) * FSR / (zc[-1] - zc[0]) / f0))
    wantt = max(G.event(i / 400, rise=rise)[1] for i in range(401))
    ok &= abs(best - wantt) < 0.4
    rows.append(f'RISE {rise}: top {best:+.2f} semitones (curve {wantt:+.2f})')
rep.check('RISE 0 / 7 / 12 (4 s events): the top of the climb follows the knob', ok, '; '.join(rows))
L, R, _ = run('spin0', T.P(names, d, AMT=127, RISE=0, SPIN=0, TIME=32), x, blocks)
c0, c1 = span(blockcost('spin0'))
pn = [A.rms(L[s0:s0 + 441]) - A.rms(R[s0:s0 + 441]) for s0 in range(c0 * 16, c1 * 16 - 441, 441)]
cr0 = sum(1 for a, b in zip(pn, pn[1:]) if a < 0 <= b)
L, R, _ = run('spin127', T.P(names, d, AMT=127, RISE=0, SPIN=127, TIME=32), x, blocks)
pn = [A.rms(L[s0:s0 + 441]) - A.rms(R[s0:s0 + 441]) for s0 in range(c0 * 16, c1 * 16 - 441, 441)]
cr1 = sum(1 for a, b in zip(pn, pn[1:]) if a < 0 <= b)


def turns(spin, T_=4.0):
    return sum(G.event((i + 0.5) / 4000, spin=spin)[2] for i in range(4000)) * T_ / 4000


rep.check('SPIN 0 / 127 (4 s events, RISE 0): turns in the event = the integral of 0.3 + 5 (v/64) u^2 out',
          abs(cr0 - turns(0)) <= 1.0 and abs(cr1 - turns(127)) <= 1.5,
          f'SPIN 0: {cr0} turns (curve {turns(0):.1f}); SPIN 127: {cr1} turns (curve {turns(127):.1f})')
# RISE 0, SPIN 0 on DC: the two Hann taps sum to the input (no pitch, no ripple), only the swell and the pan are left
xdc = [round(0.25 * A.FS)] * n
L, R, _ = run('dc', T.P(names, d, AMT=127, RISE=0, SPIN=0, TIME=32), xdc, blocks)
mid = (c0 + c1) // 2 * 16
segL = [(a + b) / (2 * 0.25 * A.FS) for a, b in zip(L[mid - 4000:mid + 4000], R[mid - 4000:mid + 4000])]
rip = max(segL) - min(segL)
rep.check('RISE 0 on DC, mid event: (L + R)/2 flat at 1.3 x 0.6 (the two windows sum to 1 -- a wrong window or tap spacing '
          'would ripple; the spin only moves it between the sides)', rip < 0.005 and abs(sum(segL) / len(segL) - 0.78) < 0.01,
          f'(L + R)/(2 in) {min(segL):.4f} .. {max(segL):.4f} over 180 ms (up 1.3 x (1 - 0.8/2) = 0.78)')

# ---- 3b. quality pass (12 Nov): RISE glides inside (1/16 a call), a jumped knob slides the pitch -------------------
import random as _r
fq = 200.0
blocks = 9 * FSR // 16
n = blocks * 16
rnd = _r.Random(5)
ev = [(b, T.P(names, d, AMT=127, TIME=64, RISE=rnd.choice((0, 12))))
      for b in range(FSR // 16, 7 * FSR // 16, int(0.15 * FSR / 16))]
L, R, _ = run('risej', T.P(names, d, AMT=127, TIME=64), A.sine(fq, 0.5, n), blocks, events=ev)
sd = 0.5 * A.FS * (2 * math.pi * fq / FSR) ** 2
rj = [max(abs(a - 2 * b + c) for a, b, c in zip(ch[FSR + 2:7 * FSR], ch[FSR + 1:7 * FSR], ch[FSR:7 * FSR])) / sd
      for ch in (L, R)]
rep.check('RISE jumped 0 <-> 12 at random every 0.15 s inside an 8 s event (200 Hz 0.5 FS sine): the pitch slides (RISE '
          'glided 1/16 a call, 5.8 ms), no click -- largest second difference under 5 x a clean 200 Hz sine\'s (an octave '
          'up is 4 x by itself; before the quality pass the steps gave 16.9 x)', max(rj) < 5,
          f'L {rj[0]:.2f} R {rj[1]:.2f} x')

# ---- 4. silence, stale ring, a minute ------------------------------------------------------------------------------
blocks = 9 * FSR // 16
n = blocks * 16
x = A.noise(2 * FSR, 0.7, 5) + [0] * (n - 2 * FSR)
ev = [(0, T.P(names, d, AMT=127, TIME=1)), (10, T.P(names, d, AMT=0, TIME=1)), (5 * s_, T.P(names, d, AMT=127, TIME=1))]
L, R, _ = run('stale', T.P(names, d, AMT=127, TIME=1), x, blocks, events=ev)
tail = max(abs(v) for v in L[3 * FSR:] + R[3 * FSR:])
rep.check('an event over noise, then silence and a second event 5 s in: exactly 0 out (no feedback; the ring is cleared '
          'at every start)', tail == 0, f'{tail} LSB')
# 60 s: events back to back at the dearest settings (TIME 64 = 8 s), noise then silence
blocks = 60 * FSR // 16
n = blocks * 16
stop = 40 * FSR
x = A.noise(stop, 0.9, 9) + [0] * (n - stop)
ev = []
for t in range(0, blocks, int(8.2 * FSR / 16)):
    ev += [(t, T.P(names, d, AMT=127, RISE=12, SPIN=127, TIME=64)), (t + 8, T.P(names, d, AMT=0, RISE=12, SPIN=127, TIME=64))]
L, R, m = run('minute', T.P(names, d, AMT=0), x, blocks, events=ev)
bc = blockcost('minute')
on = [v for v in bc if v > 50]
sat = sum(1 for v in L + R if abs(v) >= A.FS - 1)
rep.check('60 s: 0.9 FS noise for 40 s then silence, 8 s events back to back at RISE 12 SPIN 127: bounded, no stray '
          'write, silent once the input is', A.peak(L + R) <= 1.0 + 1e-6 and max(abs(v) for v in L[-FSR * 15:] + R[-FSR * 15:]) == 0,
          f'peak {A.peak(L + R):.4f} FS ({sat} samples at the output limit: the swell is x 1.3), last 15 s 0 LSB; '
          f'{len(on) * 16 / FSR:.1f} s inside events, cost there mean {sum(on) / len(on):.1f} / worst call {max(on):.1f}')
rep.note('size', f'{len(fw)} words of P; r7 state {len(G.ST.at)} words; X ring {G.RING} words (base a multiple of {G.RING}); '
                 f'window {G.WINDOW} frames = {G.WINDOW / FSR * 1000:.1f} ms (plugin 60 ms)')
rep.note('cost', f'inside an event: worst call {max(on):.1f}, mean {sum(on) / len(on):.1f} instructions a sample; '
                 f'at rest 6 instructions a call (standard gate: mean {cost[0]:.1f}, worst {cost[1]:.1f} over a render '
                 'that is mostly rest after its one event)')
rep.save('/aeria/twister/test_results.md')
