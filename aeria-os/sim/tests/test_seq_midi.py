"""the eight MIDI tracks and the step features they share with the audio tracks, read off the DIN output byte by byte:
the grid, channels, notes, velocity, gate, micro-timing, ratchets, probability, conditions, a lock, track length, mutes"""
from common import *
s = boot(seq=True)
clear_seq(s)
head(s, nin=1, sync=0)                                  # incoming notes do not play tracks; no clock out
trk(s, 0, MACH=MACH['NOIS'], DEC=15, VOL=110)
for n in (0, 4, 8, 12): step(s, 0, n)                    # audio track 1: four on the floor
mtrk(s, 0, CH=1, NOTE=60, VEL=100, GATE=4)              # M1: every beat, C3 on channel 1
for n in (0, 4, 8, 12): mstep(s, 0, n)
mtrk(s, 1, CH=5, NOTE=64, VEL=90, GATE=2)               # M2 on channel 5: the step features
mstep(s, 1, 2, vel=33)                                  # a step's own velocity
mstep(s, 1, 6, micro=12)                                # a quarter of a step late (31.25 ms)
mstep(s, 1, 10, ratch=3)                                # three hits in the step
mstep(s, 1, 14, lock=(MP['NOTE'], 72))                  # a locked note
mtrk(s, 2, CH=9, NOTE=48)                               # M3 on channel 9: conditions
mstep(s, 2, 0, cond=1)                                  # 1:2 -- every other bar, the first
mstep(s, 2, 4, cond=2)                                  # 2:2 -- the second
mstep(s, 2, 8, prob=100)                                # always
mtrk(s, 3, CH=10, NOTE=50)                              # M4: three steps long against sixteen
s.poke(MT + 8 + 3, [3]); mstep(s, 3, 0)
s.run(300); s.midi_out(); s.audio()
s.tap(K['PLAY']); s.run(8400); s.tap(K['STOP']); s.run(300)
ev = notes(s.midi_out()); o = s.audio()
on = lambda ch, note=None: [e for e in ev if e[1] == (0x90 | (ch - 1)) and e[3] > 0 and (note is None or e[2] == note)]
m1 = on(1, 60); t0 = m1[0][0] if m1 else 0.0
g = np.diff([e[0] for e in m1]) * 1000.0
check('M1: a note on channel 1 every beat (8.5 s of play: eighteen)', len(m1) == 18, len(m1))
check('M1: 500 ms apart (within 4 ms: up to five messages fall due together and DIN sends one every 0.96 ms)', len(g) > 0 and np.max(np.abs(g - 500.0)) < 4.0, ['%.2f' % x for x in g[:5]])
offs = [e for e in ev if (e[1] & 0xF0) == 0x80 or ((e[1] & 0xF0) == 0x90 and e[3] == 0)]
check('every note is ended (as many note-offs as note-ons)', len(offs) == len([e for e in ev if (e[1] & 0xF0) == 0x90 and e[3] > 0]), (len(offs), len(ev)))
m2 = on(5); bar = 2.0
rel = sorted(((e[0] - t0) % bar * 1000.0, e[2], e[3]) for e in m2 if e[0] - t0 < bar - 0.01)
check('M2 plays on channel 5', len(m2) > 0, len(m2))
near = lambda ms: [r for r in rel if abs(r[0] - ms) < 6.0]   # DIN: ~1 ms a message, several due at once queue
check('M2 step 3: on the grid (250 ms) with its own velocity 33', bool(near(250.0)) and near(250.0)[0][2] == 33, near(250.0))
check('M2 step 7: micro-timing +12/48 lands 31.25 ms late (781.25 ms)', bool(near(781.25)) and abs(near(781.25)[0][0] - 781.25) < 4.0, [r for r in rel if 700 < r[0] < 900])
rt = [r for r in rel if 1245 <= r[0] < 1375]
check('M2 step 11: a ratchet of three, evenly in the step (1250, 1291.7, 1333.3 ms)', len(rt) == 3 and all(abs(r[0] - w) < 4.0 for r, w in zip(rt, (1250.0, 1291.67, 1333.33))), ['%.1f' % r[0] for r in rt])
check('M2 step 15: the locked note 72 instead of 64', bool(near(1750.0)) and near(1750.0)[0][1] == 72, near(1750.0))
check('M2: the other steps keep the track\'s note 64 and velocity 90', all(r[1] == 64 for r in rel if abs(r[0] - 1750.0) > 4) and near(781.25) and near(781.25)[0][2] == 90, rel[:3])
m3 = on(9); bars = lambda sel: sorted(set(int((e[0] - t0 + 0.05) // bar) for e in m3 if sel((e[0] - t0 + 0.05) % bar * 1000.0 - 50.0)))
played_bars = lambda: None
check('M3 condition 1:2 plays in bars 1, 3, 5 only', bars(lambda ms: abs(ms) < 15) == [0, 2, 4], bars(lambda ms: abs(ms) < 15))
check('M3 condition 2:2 plays in bars 2 and 4 only', bars(lambda ms: abs(ms - 500) < 15) == [1, 3], bars(lambda ms: abs(ms - 500) < 15))
check('M3 probability 100 % plays every bar', bars(lambda ms: abs(ms - 1000) < 15) == [0, 1, 2, 3], bars(lambda ms: abs(ms - 1000) < 15))
m4 = on(10); lat4 = [((e[0] - t0) * 1000.0) - 375.0 * round((e[0] - t0) * 1000.0 / 375.0) for e in m4]
check('M4: a track three steps long plays every 375 ms against the others\' sixteen (each note within 7 ms of its place; the first after PLAY is the latest)', len(m4) >= 20 and max(abs(x) for x in lat4) < 7.0 and abs(round((m4[-1][0] - t0) * 1000.0 / 375.0) - (len(m4) - 1)) == 0, (len(m4), ['%.1f' % x for x in lat4[:8]]))
# the audio track against the MIDI track: the note leaves when the sound is heard (the engine's output is ~2 ms behind
# the converter's input side; what matters is that the offset holds still)
ao = onsets(o[:, MAIN_L])
if len(ao) >= 16 and len(m1) >= 16:
    off = [(m1[i][0] - m1[0][0]) * 1000.0 - (ao[i] - ao[0]) / FS * 1000.0 for i in range(16)]
    check('MIDI notes keep their place against the audio track\'s hits (within 4 ms over four bars)', max(abs(x) for x in off) < 4.0, ['%.1f' % x for x in off[::3]])
check('the audio track played every beat beside the MIDI tracks', len(ao) == 18, len(ao))
done(s)
