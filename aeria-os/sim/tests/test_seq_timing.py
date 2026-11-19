"""the sequencer's clock and its sound: steps on the grid at the tempo, no drift, main and headphones together, stop is silent"""
from common import *
s = boot(seq=True)
clear_seq(s)
trk(s, 0, MACH=MACH['NOIS'], DEC=20, VOL=110)
for n in (0, 4, 8, 12): step(s, 0, n)
s.run(300); s.audio()
s.tap(K['PLAY']); s.run(8300)                     # four bars and a bit at 120 BPM
check('PLAY starts the sequencer', s.read().count('PLAY') >= 1, s.lines()[:1])
o = s.audio()
on = onsets(o[:, MAIN_L]); gaps = np.diff(on) / FS * 1000.0
check('sixteen hits in four bars (and the seventeenth)', len(on) in (16, 17), len(on))
check('every beat 500.0 ms after the last (120 BPM), within 0.5 ms', len(gaps) > 0 and np.max(np.abs(gaps - 500.0)) < 0.5, ['%.2f' % g for g in gaps[:6]])
check('no drift over four bars (under 1 ms)', len(on) >= 16 and abs((on[15] - on[0]) / FS * 1000.0 - 7500.0) < 1.0, (on[15] - on[0]) / FS * 1000.0 if len(on) >= 16 else None)
ph = onsets(o[:, 5])
check('the headphones carry every hit', len(ph) == len(on), (len(ph), len(on)))
check('headphones as loud as main', abs(rms_db(o[:, 5]) - rms_db(o[:, MAIN_L])) < 0.5, '%.1f / %.1f dB' % (rms_db(o[:, 5]), rms_db(o[:, MAIN_L])))
check('main left and right both sound', abs(rms_db(o[:, MAIN_L]) - rms_db(o[:, MAIN_R])) < 1.0, '%.1f / %.1f' % (rms_db(o[:, MAIN_L]), rms_db(o[:, MAIN_R])))
s.tap(K['STOP']); s.run(700); s.audio(); s.run(1200); q = s.audio()
check('STOP: silence (under -80 dBFS)', peak_db(q[:, MAIN_L]) < -80 and peak_db(q[:, 5]) < -80, '%.0f / %.0f' % (peak_db(q[:, MAIN_L]), peak_db(q[:, 5])))
# another tempo: TEMPO held + LEVEL
s.key(K['TEMPO'], 1); s.run(80); s.enc(6, 20); s.enc(6, 20); s.key(K['TEMPO'], 0); s.run(200)
bpm = s.read()
s.audio(); s.tap(K['PLAY']); s.run(4200); o = s.audio(); on = onsets(o[:, MAIN_L]); gaps = np.diff(on) / FS * 1000.0
shown = [int(w) for l in s.lines()[:1] for w in l.replace('|', ' ').split() if w.isdigit() and 60 <= int(w) <= 300]
check('TEMPO held + LEVEL changes the tempo shown', shown and shown[0] != 120, s.lines()[:1])
if shown and len(gaps):
    want = 60000.0 / shown[0]
    check('the beats follow the tempo shown (%d BPM: %.1f ms)' % (shown[0], want), np.max(np.abs(gaps - want)) < 0.6, ['%.2f' % g for g in gaps[:5]])
s.tap(K['STOP'])
done(s)
