"""DSP core 0's own load meter (engine.asm: X:$33A01 this frame, $33A02 the worst, $33A03 frames over half, $33A04
overruns; 128 = a whole frame's time) and the ColdFire's sequencer block time, in the default chain and with the
sequencer busy. Instruction counts are the firmware's own; the simulator's DSP has no wait states, so these are a lower
bound -- compared with what was measured on the unit (12 Nov 2026: default chain 36-45 %, worst 55 %)."""
from common import *
def load(s, ms=3000):
    s.dsp(0, 'X', 0x33a01, 1); s.poke(0x100FBEE0, bytes(12))
    # zero the worst and the counters through the DSP's shared window is not possible from here: read before and after
    w0 = s.dsp(0, 'X', 0x33a02, 3); s.run(ms); w1 = s.dsp(0, 'X', 0x33a01, 4)
    blk = struct.unpack('>3I', s.peek(0x100FBEE0, 12))
    return dict(now=w1[0] * 100 // 128, worst=w1[1] * 100 // 128, half=w1[2] - w0[1], over=w1[3] - w0[2], cf_block_us=(blk[2] / max(blk[1], 1)) / 132.0)
s = boot(seq=False, wait=6000)
s.feed([tone(500, 30.0, 0.5), tone(700, 30.0, 0.5), None, None])
r = load(s); print('default chain:', r)
check('default chain: no overruns, worst frame under 90 %', r['over'] == 0 and r['worst'] < 90, r)
s.combo(K['FUNC'], K['MIDI']); s.run(1500)
clear_seq(s)
for t in range(8):
    trk(s, t, MACH=[MACH['SUB'], MACH['FM'], MACH['NOIS'], MACH['PLUK']][t % 4], DEC=60, VOL=90, HP=20, LP=100)
    for n in range(16): step(s, t, n, ratch=2 if n % 4 == 3 else 0)
s.tap(K['PLAY'], after=0)
r = load(s, 4000); print('8 tracks every step, filters on:', r)
check('8 tracks on every step: no overruns', r['over'] == 0, r)
check('the ColdFire makes a 32-sample block in under 400 us (it has 726)', r['cf_block_us'] < 400, '%.0f us' % r['cf_block_us'])
s.audio(); s.run(1000); o = s.audio()
check('... and it sounds on main and the headphones', rms_db(o[:, MAIN_L]) > -40 and rms_db(o[:, 5]) > -40, '%.1f / %.1f' % (rms_db(o[:, MAIN_L]), rms_db(o[:, 5])))
s.tap(K['STOP'])
done(s)
