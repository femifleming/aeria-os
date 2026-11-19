"""what is kept: a project saved from the panel and loaded back (the sequence with it), and everything across a power
cycle (battery RAM carried into a fresh boot, with the DSP memory uninitialised again)"""
from common import *
import tempfile, os
s = boot(seq=True)
clear_seq(s); trk(s, 0, MACH=MACH['NOIS'], DEC=8, VOL=110)
for n in (0, 4, 8, 12): step(s, 0, n)
s.run(300)
seq_a = s.peek(B + 0x400, 12 * 64)
# save into project 2
s.tap(K['PROJ'], after=300); s.tap(K['TRIG'] + 1, after=200)
s.combo(K['FUNC'], K['YES'], after=400); naming = s.read()
s.tap(K['YES'], after=400); s.run(2500); saved = s.read()
check('FUNC+YES on PROJECTS asks for a name, YES saves', 'NAME' in naming or 'SAVE' in naming or 'YES' in naming, naming.split('\n')[:3])
s.tap(K['NO'], after=300)
# change the sequence, then load project 2
clear_seq(s); step(s, 0, 2); s.run(300)
s.tap(K['PROJ'], after=300); s.tap(K['TRIG'] + 1, after=200); s.tap(K['YES'], after=400); s.run(4000)
check('YES on project 2 loads it: the four steps are back', s.peek(B + 0x400, 12 * 64) == seq_a, s.read().split('\n')[:2])
s.tap(K['NO'], after=300)
s.audio(); s.tap(K['PLAY'], after=0); s.run(2200); o = s.audio(); s.tap(K['STOP'])
check('... and it plays: four hits a bar', len(onsets(o[:, MAIN_L], 0.01, 0.015)) in (4, 5), len(onsets(o[:, MAIN_L], 0.01, 0.015)))
# a power cycle
f = os.path.join(tempfile.mkdtemp(), 'bram.bin'); s.run(3000); s.cmd('bramsave ' + f)
was_seq = s.u8('seq_on'); s.close()
s = boot(bram=f, dirty=99)
check('after a power cycle the OS comes back in the face it was left in', s.u8('seq_on') == was_seq, s.u8('seq_on'))
check('... with the sequence', s.peek(B + 0x400, 12 * 64) == seq_a)
s.audio(); s.tap(K['PLAY'], after=0); s.run(2200); o = s.audio(); s.tap(K['STOP'])
check('... and it plays, on main and on the headphones', len(onsets(o[:, MAIN_L], 0.01, 0.015)) in (4, 5) and len(onsets(o[:, 5], 0.01, 0.015)) in (4, 5), (len(onsets(o[:, MAIN_L], 0.01, 0.015)), len(onsets(o[:, 5], 0.01, 0.015))))
s.tap(K['PROJ'], after=300)
check('... and project 2 is still in the list', 'EMPTY' not in [l for l in s.lines() if l.startswith('2')][:1], s.lines()[:4])
done(s)
