"""the audio tracks' step features, read from the sound: micro-timing, ratchets, conditions, probability, a volume lock,
swing, a track's own length, mutes, pattern change at the bar. One track a test, the NOIS machine (short hits)."""
from common import *
s = boot(seq=True)
STEP = 125.0                                            # ms at 120 BPM
def play(setup, ms, pats=1):
    s.tap(K['STOP']); s.run(150); clear_seq(s, pats)
    for t in range(8): trk(s, t, MACH=MACH['NOIS'], DEC=8, VOL=110, ATK=0)
    head(s, swing=50, mutes=0, solo=0, pattern=0, chain_n=0)
    for pt in range(2):
        s.poke(B + OFF['len'] + 8 * pt, [16] * 8)
    s.poke(B + OFF['mlen'], [16, 16, 16, 16])
    setup(); s.run(150); s.audio()
    s.tap(K['PLAY'], after=0); s.run(ms); o = s.audio()
    s.tap(K['STOP']); s.run(200)
    return o[:, MAIN_L]
def ms_of(o):
    on = onsets(o, 0.01, 0.015)
    return [(x - on[0]) / FS * 1000.0 for x in on] if on else []
def near(xs, want, tol=0.6): return any(abs(x - want) < tol for x in xs)

o = play(lambda: (step(s, 0, 0), step(s, 0, 4, micro=12), step(s, 0, 8, micro=-12)), 2100)
t = ms_of(o)
check('micro-timing: +12/48 lands 31.25 ms late, -12/48 31.25 ms early', near(t, 500 + 31.25) and near(t, 1000 - 31.25), ['%.2f' % x for x in t[:4]])

o = play(lambda: (step(s, 0, 0), step(s, 0, 4, ratch=4)), 1000)
t = ms_of(o)
check('a ratchet of four: four hits evenly through the step (500, 531.25, 562.5, 593.75 ms)', all(near(t, 500 + k * 31.25, 0.8) for k in range(4)), ['%.2f' % x for x in t[:6]])

o = play(lambda: (step(s, 0, 0), step(s, 0, 8, cond=1), step(s, 0, 12, cond=2)), 8000)
t = ms_of(o)
bars_with = lambda pos: sorted(int(x // 2000) for x in t if abs(x % 2000 - pos) < 1.0)
check('condition 1:2 sounds in bars 1 and 3', bars_with(1000) == [0, 2], bars_with(1000))
check('condition 2:2 sounds in bars 2 and 4', bars_with(1500) == [1, 3], bars_with(1500))

def prob():
    step(s, 0, 0)
    for n in range(1, 16): step(s, 0, n, prob=50)
o = play(prob, 8000); t = ms_of(o)
hits = len([x for x in t if abs(x % 2000) > 1.0])
check('probability 50 %: about half of 60 chances sound (20..40)', 20 <= hits <= 40, hits)

o = play(lambda: (step(s, 0, 0), step(s, 0, 4, locks=[(TP['VOL'], 40)]), step(s, 0, 8)), 1400)
on = onsets(o, 0.01, 0.015); pk = [peak_db(o[x:x + 2000]) for x in on[:3]]
check('a volume lock makes that step quieter, the next is back to the track\'s level', len(pk) == 3 and pk[1] < pk[0] - 6 and abs(pk[2] - pk[0]) < 1.5, ['%.1f' % x for x in pk])

o = play(lambda: ([step(s, 0, n) for n in range(4)], head(s, swing=66)), 600)
t = ms_of(o); sw = (66 - 50) / 50.0 * STEP
check('swing 66: the off steps land %.1f ms late, the on steps on the grid' % sw, near(t, STEP + sw, 1.0) and near(t, 2 * STEP, 0.6) and near(t, 3 * STEP + sw, 1.0), ['%.2f' % x for x in t[:4]])

o = play(lambda: (s.poke(B + OFF['len'] + 0, [3]), step(s, 0, 0)), 2000)
t = ms_of(o)
check('a track three steps long repeats every 375 ms', len(t) >= 5 and all(abs(b - a - 375.0) < 0.6 for a, b in zip(t, t[1:5])), ['%.2f' % x for x in t[:6]])

o = play(lambda: ([step(s, 0, n) for n in (0, 4, 8, 12)], head(s, mutes=1)), 2100)
check('a muted track is silent', peak_db(o) < -80, '%.1f' % peak_db(o))

def two_pats():
    step(s, 0, 0, pat=0); step(s, 0, 8, pat=1)
play(two_pats, 300, pats=2)
s.key(K['PLAY'], 1); s.audio(); s.run(60); s.key(K['PLAY'], 0)          # the capture starts with the press
s.key(K['PATTERN'], 1); s.run(60); s.tap(K['TRIG'] + 1); s.key(K['PATTERN'], 0)
s.run(4300); o = s.audio()[:, MAIN_L]; s.tap(K['STOP'])
t = [x / FS * 1000.0 for x in onsets(o, 0.01, 0.015)]                   # from the press (plus the ~20 ms the stream and the engine take)
check('PATTERN + trig 2 changes to B at the end of the bar: A\'s step 1 at 0, B\'s step 9 at 3000 ms, nothing else', len(t) == 2 and abs(t[0] - 20) < 15 and abs(t[1] - 3020) < 15, ['%.0f' % x for x in t])
done(s)
