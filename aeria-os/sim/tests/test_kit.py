"""the sample machine with a kit from the card (KIT browser: FUNC+YES on SRC), each track its own WAV in folder order,
heard at its own pitch on main and the headphones"""
from common import *
s = boot(seq=True)
s.tap(K['SRC'], after=300); s.combo(K['FUNC'], K['YES'], after=600)
for _ in range(4):                                                  # the cursor onto AUDIO (folders A to Z: AERIA first)
    if any(l.replace('.', '') == '~AUDIO' for l in s.lines()): break
    s.tap(K['DOWN'], after=200)
s.tap(K['YES'], after=800)                                          # into AUDIO
listing = s.read()
for _ in range(6):                                                  # the cursor onto KIT1 (the highlighted line)
    if any(l.replace('.', '') == '~KIT1' for l in s.lines()): break
    s.tap(K['DOWN'], after=200)
s.tap(K['YES'], after=800)                                          # into KIT1: FUNC+YES loads the folder the browser is in
s.combo(K['FUNC'], K['YES'], after=600); s.run(5000)
after = s.read()
check('the browser lists the card\'s folder, folders first', 'KIT1' in listing and 'LOOP' in listing and listing.index('KIT1') < listing.index('LOOP'), listing.split('\n')[:4])
check('FUNC+YES in KIT1 loads the kit', 'KIT LOADED' in after or 'SMPL' in after, after.split('\n')[:2])
s.tap(K['TRACK'] + 0, after=300)
def play_kit(s):
    clear_seq(s)
    for t in range(8): trk(s, t, MACH=MACH['SMPL'], SMPL=96 + t, PTCH=64, DEC=127, VOL=110, ATK=0)
    for t in range(8): step(s, t, 2 * t)
    s.run(300); s.audio(); s.key(K['PLAY'], 1); s.run(60); s.key(K['PLAY'], 0); s.run(2100); o = s.audio(); s.tap(K['STOP'])
    return o
TONES = (220, 330, 440, 550, 660)
def which(o, t):
    """the step's loudest frequency (Hz), or None when it is silent"""
    L = o[:, MAIN_L].astype(np.float64); a = int((2 * t * 0.125 + 0.025) * FS); x = L[a:a + 4096]
    if peak_db(x) < -40: return None
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))); return float((np.argmax(sp[5:]) + 5) * FS / len(x))
WANT = [(40, 90), (150, 250), (3000, 20000)] + [(f * 0.95, f * 1.05) for f in TONES]   # kick, snare, hat, then the tones
fits = lambda got: all(g is not None and lo <= g <= hi for g, (lo, hi) in zip(got, WANT))
o = play_kit(s)
got = [which(o, t) for t in range(8)]
check('the kit loads in name order: 1KICK 2SNARE 3HAT 4T220 .. 8T660 on tracks 1-8', fits(got), ['%.0f' % g if g else None for g in got])
check('the headphones carry the kit too', abs(rms_db(o[:, 5]) - rms_db(o[:, MAIN_L])) < 0.5, '%.1f / %.1f' % (rms_db(o[:, 5]), rms_db(o[:, MAIN_L])))
import tempfile, os
f = os.path.join(tempfile.mkdtemp(), 'bram.bin'); s.run(2500); s.cmd('bramsave ' + f); s.close()
s = boot(bram=f, dirty=5, wait=9000)
got2 = [which(play_kit(s), t) for t in range(8)]
check('after a power cycle the kit comes back from the card in the same order', fits(got2), ['%.0f' % g if g else None for g in got2])
done(s)
