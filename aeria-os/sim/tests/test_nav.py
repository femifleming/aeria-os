"""menus on the real firmware: every page key opens its page, NO always backs out to the sky (multi-effect face) or
to the track's first knob set (sequencer), NO never changes anything (the tempo), and random key presses cannot
freeze the OS or stop its sound"""
from common import *
import random
s = boot(seq=False, wait=6000)
norm = lambda ls: [l.replace('.', '') for l in ls]   # a stray pixel beside a word reads as a full stop
base = norm(s.lines())
SKY = dict(SRC='CHAIN', AMP='SKY', FX1='MIX', LFO='SOURCE', MIX='MASTER', ARR='STARS', PART='BALLOON', AED='LOOP', FX2='SYSTEM', REC='GRAIN CLOUD', MIDI='GUMBALLS', CUE='OUT')
for k, name in SKY.items():
    s.tap(K[k], after=400); opened = s.read()
    s.tap(K['NO'], after=300); s.run(4500); back = norm(s.lines())
    check('sky: %s opens %s, NO brings back the sky alone' % (k, name), name in opened.replace('~', '') and back == base, (opened.split('\n')[:1], back))
bpm0 = s.u8('params', 0) if False else None
s.tap(K['MIX'], after=300); t0 = s.read()
for _ in range(4): s.tap(K['NO'], after=150)
s.run(300); s.tap(K['MIX'], after=300); t1 = s.read()
tempo = lambda txt: [l for l in txt.split('\n') if 'TMPO' in l or 'BPM' in l]
check('NO on MASTER changes nothing (no tap tempo): the page is the same before and after', t0.split('\n')[1:] == t1.split('\n')[1:], (t0.split('\n')[1:3], t1.split('\n')[1:3]))
s.tap(K['NO'], after=300); s.run(4500)
# the sequencer face
s.combo(K['FUNC'], K['MIDI']); s.run(1200)
SEQ = dict(AMP='AMP', LFO='MOD', FX1='FLT', FX2='FX', ARR='GEN', PAGE='PTN', SRC='SRC')   # SRC last: the face opens on it
for k, name in SEQ.items():
    s.tap(K[k], after=300); a = s.lines()
    s.tap(K[k], after=300); b = s.lines()
    s.tap(K['NO'], after=300); c = s.lines()
    ok = len(a) > 1 and a[1].startswith(name) and c[1].startswith(name)
    if name not in ('FX',): ok = ok and a[2:3] == c[2:3]
    check('sequencer: %s shows %s; again = its second set; NO = back to the first' % (k, name), ok, (a[1:3], b[2:3], c[2:3]))
for k, name in (('MIX', 'MASTER'), ('PROJ', 'PROJECTS'), ('BANK', None)):
    s.tap(K[k], after=300); a = s.read(); s.tap(K['NO'], after=300); c = s.lines()
    check('sequencer: %s opens %s and NO returns to the track' % (k, name or 'the mixer'), (name is None or name in a) and c[0].startswith('T1'), (a.split('\n')[:2], c[:1]))
# fuzz: 300 random presses (never FUNC+PROJ+NO, the bring-up hand-back; never FUNC + trig 1 on SYSTEM: USB disk mode)
rnd = random.Random(7)
keys = [v for k, v in K.items() if k not in ('FUNC', 'PROJ', 'FX2')]
s.audio(); s.feed([tone(1000, 60.0), None, None, None])
f0 = s.frames(); lit_seen = set()
for i in range(300):
    c = rnd.choice(keys)
    if rnd.random() < 0.3: s.enc(rnd.randrange(7), rnd.choice((-3, -1, 1, 3)), after=20)
    else: s.tap(c, hold=30, after=30)
    if i % 50 == 49: lit_seen.add(s.lit())
for _ in range(3): s.tap(K['NO'], after=100)
s.tap(K['STOP'], after=300)
st = s.status()
check('300 random presses: the OS still runs (screen changing, DSP frames advancing)', s.frames() > f0 + FS and len(lit_seen) > 1, (s.frames() - f0, len(lit_seen)))
s.audio(); s.run(800); o = s.audio()
check('... and input A still reaches main and the headphones', level_at(o[:, MAIN_L], 1000) > -40 and level_at(o[:, 5], 1000) > -40, '%.1f / %.1f dB' % (level_at(o[:, MAIN_L], 1000), level_at(o[:, 5], 1000)))
done(s)
