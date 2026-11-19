"""the level lamps above the REC keys follow the real inputs: A 62, B 60, C 56, D 58 (0..15)"""
from common import *
s = boot(seq=False)
LAMP = dict(A=62, B=60, C=56, D=58)
def lamps(): return [s.led(LAMP[c]) for c in 'ABCD']
s.run(500)
check('no signal: all four lamps dark', lamps() == [0, 0, 0, 0], lamps())
seen = {}
for i, c in enumerate('ABCD'):
    ch = [None] * 4; ch[i] = tone(440 * (i + 1), 0.9, 0.5)
    s.feed(ch); s.run(700)
    seen[c] = lamps()
    check('a tone on input %s (-6 dBFS) lights its lamp and no other' % c, seen[c][i] >= 12 and sum(1 for v in seen[c] if v) == 1, seen[c])
    s.run(900)
check('the lamps go dark again when the signal stops', lamps() == [0, 0, 0, 0], lamps())
lv = []
for db in (-3, -12, -24, -36, -48):
    s.feed([tone(1000, 0.9, 10 ** (db / 20.0)), None, None, None]); s.run(700); lv.append(s.led(62)); s.run(700)
check('louder input, brighter lamp (-3, -12, -24, -36, -48 dBFS): about 3.5 dB a step', all(a > b for a, b in zip(lv, lv[1:])) and lv[0] == 14 and lv[-1] >= 1, lv)
s.feed([tone(1000, 0.9, 0.999), None, None, None]); s.run(700)
check('full scale shows the top level', s.led(62) == 15, s.led(62)); s.run(700)
s.combo(K['FUNC'], K['MIDI']); s.run(1200)
s.feed([None, tone(1000, 0.9, 0.5), None, None]); s.run(700)
check('the lamps work in the sequencer face too', s.led(60) >= 12 and s.led(62) == 0, lamps())
done(s)
