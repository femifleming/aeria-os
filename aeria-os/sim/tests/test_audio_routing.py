"""the default sound path, a unit just switched on: inputs to main, the headphones a copy of main, the cue pair, the codec"""
from common import *
s = boot(seq=False)
check('the codec is unmuted (register 07 = 00)', (7, 0) in s.codec(), s.codec())
r0 = s.rotation()
s.audio(); s.feed([tone(1000, 2.2), tone(1500, 2.2), None, None]); s.run(2000)
o = s.audio()[-FS:]
L1, R1 = level_at(o[:, MAIN_L], 1000), level_at(o[:, MAIN_R], 1500)
check('input A reaches main left at its own level (-12 dBFS in)', abs(L1 + 12.0) < 0.5, '%.2f dB' % L1)
check('input B reaches main right', abs(R1 + 12.0) < 0.5, '%.2f dB' % R1)
check('headphones left (ring word 5 on the MKII) = main left', abs(level_at(o[:, 5], 1000) - L1) < 0.3, '%.2f dB' % level_at(o[:, 5], 1000))
check('headphones right (ring word 4) = main right', abs(level_at(o[:, 4], 1500) - R1) < 0.3, '%.2f dB' % level_at(o[:, 4], 1500))
check('headphones keep left and right apart', level_at(o[:, 5], 1500) < L1 - 30 and level_at(o[:, 4], 1000) < R1 - 30, '%.1f / %.1f dB' % (level_at(o[:, 5], 1500), level_at(o[:, 4], 1000)))
check('main keeps left and right apart (30 dB or more)', level_at(o[:, MAIN_L], 1500) < L1 - 30 and level_at(o[:, MAIN_R], 1000) < R1 - 30, '%.1f / %.1f dB' % (level_at(o[:, MAIN_L], 1500), level_at(o[:, MAIN_R], 1000)))
check('the two unused ring words stay silent', not o[:, 6].any() and not o[:, 7].any())
check('no clipping, no stray level (peak within 0.5 dB of the tone)', abs(peak_db(o[:, MAIN_L]) + 12.0) < 0.5 and abs(peak_db(o[:, 5]) + 12.0) < 0.5, '%.2f / %.2f' % (peak_db(o[:, MAIN_L]), peak_db(o[:, 5])))
# silence in, silence out
s.run(600); s.audio(); s.run(700); q = s.audio()
check('no input: main and headphones silent (under -90 dBFS)', max(peak_db(q[:, i]) for i in (2, 3, 4, 5)) < -90, ['%.0f' % peak_db(q[:, i]) for i in (2, 3, 4, 5)])
check('the output ring never slipped against the converter frame', s.rotation()[:2] == r0[:2], (r0, s.rotation()))
# the same in the sequencer's face
s.combo(K['FUNC'], K['MIDI']); s.run(1200)
check('FUNC + MIDI changes to the sequencer', s.u8('seq_on') == 1)
s.audio(); s.feed([tone(1000, 2.0), tone(1500, 2.0), None, None]); s.run(1800)
o = s.audio()[-FS:]
check('sequencer face: input A to main left and headphones left', abs(level_at(o[:, MAIN_L], 1000) + 12) < 0.5 and abs(level_at(o[:, 5], 1000) + 12) < 0.5, '%.2f / %.2f' % (level_at(o[:, MAIN_L], 1000), level_at(o[:, 5], 1000)))
check('sequencer face: input B to main right and headphones right', abs(level_at(o[:, MAIN_R], 1500) + 12) < 0.5 and abs(level_at(o[:, 4], 1500) + 12) < 0.5, '%.2f / %.2f' % (level_at(o[:, MAIN_R], 1500), level_at(o[:, 4], 1500)))
check('the ring did not slip at the change of face', s.rotation()[:2] == r0[:2], s.rotation())
done(s)
