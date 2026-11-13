"""AERIA HEAD gate. Run: aeria-fx/head/run.sh python3 -B /h/test_head.py"""
import math, re, sys
sys.path.insert(0, '/h')
import rig as R

rep = R.Report('HEAD')
text = R.pathlib.Path('/h/head.asm').read_text()
probs = R.source_checks(text)
rep.check('source: no label a prefix of another, no rep, displacements within -64..63', not probs, probs or '')
blob, words, hd = R.assemble(text, R.ORG, 'head')
dis = R.disassemble(blob, R.ORG)
bad = R.audit(dis)
rep.check('assembles; disassembled, no mpysu/macsu/mpyuu', not bad, f'{len(words)} words, {len(dis)} instructions' + (f' BAD {bad}' if bad else ''))
mults = sorted({' '.join(l.split()[1:3]) for l in dis if re.match(r'^[0-9a-f]{6}:\s+(mpy|mac)', l)})
rep.note('every multiply as disassembled', '; '.join(mults))
_, swords, ks = R.assemble(R.KEYSTUB.read_text(), R.STUB, 'keystub')
_, lwords, lp = R.assemble(R.LPLOC, R.LOOP, 'lploc')
_, nwords, nl = R.assemble('nlini:\n        rts\nnlpro:\n        rts\n', 0x2600, 'null')
CODE = ((R.ORG, words), (R.STUB, swords), (R.LOOP, lwords), (0x2600, nwords))
# rig5: HEAD on T5 (core 0); SMPL = T1, STAR = T2 on the other core (ksfor: every generation of the T1-4
# window); FEEDBACK = T8 on this core's bus (lploc at Y:$9c0, after HEAD: one block late).
HEAD, KS, LP = (hd['init'], hd['proc']), (ks['ksini'], ks['ksfor']), (lp['llini'], lp['llloc'])
OWN, OTHER = 0x33dfe, 0x3bdfe                 # HEAD on T5: its own core's seed mark (T5-8), the other's (T1-4)
D = [0, 118, 0, 0, 64, 0]                     # FDBK FTON SMPL STAR IN WARM defaults


def kn(fdbk=0, fton=118, smpl=0, star=0, inl=64, warm=0):
    return [fdbk, fton, smpl, star, inl, warm, 0, 0, 0, 0, 0, 0]


def mem_for(own=5, bus=R.LIVE, tag='m'):
    m = R.WORK / f'{tag}.mem'
    R.write_mem(m, *CODE, own=own, bus=bus)
    return m


def loop(mem, t1, t2, x, k, g=32, blocks=None, extra=(), tag='loop'):
    """T1 and T2 publishers (T1-4 window), HEAD, then the chain stub on HEAD's buffer (gain g/32 back as T8).
    Returns HEAD's output (stereo interleaved) and stdout."""
    blocks = blocks or len(x) // 32
    outs, so = R.host(mem, [KS, KS, HEAD, LP], [t1, t2, x, None],
                      [[0] * 12, [1] + [0] * 11, k if isinstance(k, str) else k, [g] + [0] * 11],
                      blocks, extra=extra, audioidx=[0, 1, 2, 2], tag=tag)
    return outs[3], so


def head_strays(so, k=2):
    m = re.search(rf'instance {k}: \d+ non-zero output samples, (\d+) stray write regions, (\d+) CLOBBERING', so)
    return (int(m.group(1)), int(m.group(2))) if m else None


n = 16 * 600
mem = mem_for()
t1 = R.stereo(R.noise(n, 0.7, 11), R.sine(220, 0.6, n))
t2 = R.stereo(R.sine(1234, 0.8, n), R.noise(n, 0.5, 12))
x = R.stereo(R.noise(n, 0.9, 13), R.sine(97, 0.95, n))

# ---- 1. exact passthrough -------------------------------------------------------
ok, worst = True, 0
for k in (kn(), kn(fton=0, warm=127), kn(fton=127, warm=64), kn(fton=30, warm=1)):
    for split in (None, 1, 7):
        extra = ['-split', f'0,0,{split},{split}'] if split else []
        out, so = loop(mem, t1, t2, x, k, g=127, extra=extra + ['-guard', '-dirty', '0x5a'], tag='pt')
        worst = max(worst, max(abs(a - b) for a, b in zip(out, x)))
        s = head_strays(so)
        ok &= out == x and s == (0, 0)
rep.check('FDBK 0 SMPL 0 STAR 0 IN 64: output == input bit for bit, live bus with loud T1/T2 and a x4 chain, '
          'FTON/WARM at 0/127/64/1, splits none/1/7, dirty Y, guard (no write outside HEAD)', ok, f'max |err| {worst} LSB')
for name, bus in (('no seed marks', {}), ('a wrong mark on the other core', {OTHER: [0x10001, 0], OWN: [0x10000, 0]}),
                  ('a wrong mark on this core', {OWN: [0x10001, 0], OTHER: [0x10000, 0]}),
                  ('a counter of 4 on this core', {OTHER: [0x10000, 0], OWN: [0x10000, 4]}),
                  ('a counter of 4 on the other core', {OTHER: [0x10000, 4], OWN: [0x10000, 0]}),
                  ('only this core seeded', {OWN: [0x10000, 0]}), ('only the other core seeded', {OTHER: [0x10000, 0]})):
    out, so = loop(mem_for(bus=bus, tag='nb'), t1, t2, x, kn(fdbk=127, smpl=127, star=127, warm=90), g=127, tag='nb')
    rep.check(f'key bus not live ({name}), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit',
              out == x, f'max |err| {max(abs(a - b) for a, b in zip(out, x))} LSB')

# ---- 2. IN --------------------------------------------------------------------------
out, _ = loop(mem, t1, t2, x, kn(inl=0), tag='in0')
rep.check('IN 0: exact silence from the second block (the first ramps down)', not any(out[32:]),
          f'nonzero after block 1: {sum(1 for v in out[32:] if v)}')
xs = R.stereo(R.sine(500, 0.25, n))
out, _ = loop(mem, t1, t2, xs, kn(inl=127), tag='in127')
ratio = R.rms(out[64:]) / R.rms(xs[64:])
rep.check('IN 127: gain 1 + 63/64 = 1.984 (0.25 FS sine)', abs(ratio - (1 + 63 / 64)) < 1e-3, f'{ratio:.5f}')
out, _ = loop(mem, t1, t2, xs, kn(inl=32), tag='in32')
ratio = R.rms(out[64:]) / R.rms(xs[64:])
rep.check('IN 32: gain 0.5', abs(ratio - 0.5) < 1e-3, f'{ratio:.5f}')

# ---- 3. sends (T1/T2 from the other core's window; the fixture fills every generation with this block) ----
zero = [0] * (2 * n)
out, _ = loop(mem, t1, t2, zero, kn(smpl=127), tag='s1')
err = max(abs(o - a * (127 / 128 * 129 / 128)) for o, a in zip(out[32:], t1[32:])) / R.Q
rep.check('SMPL 127, own input silent: output = T1 x 0.99994 (the window slot read; the base 32-sample generation lag is not modelled)', err < 4e-7, f'max err {err:.2e} FS')
out, _ = loop(mem, t1, t2, zero, kn(star=127), tag='s2')
err = max(abs(o - a * (127 / 128 * 129 / 128)) for o, a in zip(out[32:], t2[32:])) / R.Q
rep.check('STAR 127: output = T2 x 0.99994', err < 4e-7, f'max err {err:.2e} FS')
out, _ = loop(mem, t1, t2, x, kn(smpl=64, star=32, inl=96), tag='s3')
want = [xi * 1.5 + a * (64 / 128 * 129 / 128) + b * (32 / 128 * 129 / 128) for xi, a, b in zip(x, t1, t2)]
err = max(abs(o - min(R.FULL, max(-R.Q, w))) for o, w in zip(out[32:], want[32:])) / R.Q
rep.check('IN 96 + SMPL 64 + STAR 32: the sum of the three (limited at full scale)', err < 4e-7, f'max err {err:.2e} FS')

# ---- 4. key addressing on both cores (DC per generation: which slot is read) -------
ok, det = True, []
for own in (5, 8, 1):
    for cnt in range(4):
        # the reading core's counter = cnt, the publishing core's = cnt ^ 1 (HEAD must use its OWN core's)
        bus = {OWN: [0x10000, cnt], OTHER: [0x10000, cnt ^ 1]} if own > 4 else {OTHER: [0x10000, cnt], OWN: [0x10000, cnt ^ 1]}
        for g in range(4):      # T1 / T2 in the T1-4 core's window, generation g: DC (g+1)/16 (T2 negative)
            bus[0x3be00 + (0 * 4 + g) * 0x20] = [(g + 1) << 19] * 32
            bus[0x3be00 + (1 * 4 + g) * 0x20] = [-((g + 1) << 19) & 0xffffff] * 32
            # decoys: the same tracks' slots in the T5-8 window (T5/T6 there), which HEAD must not read
            bus[0x33e00 + (0 * 4 + g) * 0x20] = [5 << 19] * 32
            bus[0x33e00 + (1 * 4 + g) * 0x20] = [-(7 << 19) & 0xffffff] * 32
        bus[0x800] = [3 << 20] * 32                # T1 on a local bus: 3/8
        bus[0x840] = [-(1 << 20) & 0xffffff] * 32  # T2 local: -1/8
        bus[0x900] = [1 << 22] * 32                # T5/T6 local decoys: 1/2, -3/4
        bus[0x940] = [-(3 << 21) & 0xffffff] * 32
        m = R.WORK / 'addr.mem'
        R.write_mem(m, *CODE, own=own, bus=bus)
        outs, so = R.host(m, [HEAD], [None], [kn(smpl=127)], 8, tag='addr')
        o1 = outs[0][-1] / R.Q
        outs, so = R.host(m, [HEAD], [None], [kn(star=127)], 8, tag='addr')
        o2 = outs[0][-1] / R.Q
        if own == 1:             # same core as T1/T2: the local bus
            w1, w2 = 3 / 8, -1 / 8
        else:                    # T5-8 core counter = cnt; read gen (cnt+2)&3 of the T1-4 window
            gsel = (cnt + 2) & 3
            w1, w2 = (gsel + 1) / 16, -(gsel + 1) / 16
        k = 127 / 128 * 129 / 128
        good = abs(o1 - w1 * k) < 1e-5 and abs(o2 - w2 * k) < 1e-5
        ok &= good
        det.append(f'T{own} c{cnt}: {o1:+.4f}/{o2:+.4f}')
rep.check('key addressing: HEAD on T5 (and T8) reads T1/T2 from the T1-4 window, generation (core-0 counter + 2) & 3, '
          'for counters 0-3 (the other core\'s counter and the T5-8 window ignored); on T1 from its own bus', ok, '; '.join(det))
# the FEEDBACK key: (a) a tone only in T8's slot (Y:$9c0) comes out; (b) loud noise in every OTHER bus slot
# and every slot of both windows, T8's silent, gives exactly nothing (FDBK 127, SMPL/STAR 0)
tone = [v & 0xffffff for v in R.stereo(R.sine(2756.25, 0.25, 16))]   # 16 frames: one period a block
others = {0x800 + t * 0x40: [v & 0xffffff for v in R.noise(64, 0.5, 70 + t)] for t in range(8) if t != 7}
others.update({0x33e00 + g * 0x20: [v & 0xffffff for v in R.noise(32, 0.5, 80 + g)] for g in range(16)})
others.update({0x3be00 + g * 0x20: [v & 0xffffff for v in R.noise(32, 0.5, 100 + g)] for g in range(16)})
others[0x9e0] = [v & 0xffffff for v in R.noise(32, 0.5, 99)]      # T8's slot's upper half (+$20..$3f)
res = []
for name, bus in (('tone in T8 only', {**R.LIVE, 0x9c0: tone}), ('noise everywhere but T8', {**R.LIVE, **others})):
    m = R.WORK / 'fbaddr.mem'
    R.write_mem(m, *CODE, own=5, bus=bus)
    outs, so = R.host(m, [HEAD], [None], [kn(fdbk=127, fton=127)], 400, tag='fbaddr')
    res.append(R.peak(outs[0][-2048:]))
rep.check('FEEDBACK key: HEAD on T5 feeds back T8\'s slot of its own bus and nothing else '
          '(tone only in T8 -> heard; noise in every other bus slot and both windows -> exactly 0)',
          res[0] > 0.05 and res[1] == 0, f'T8 tone -> peak {res[0]:.3f}; others -> peak {res[1]:.6f}')

# ---- 5. feedback ---------------------------------------------------------------------
fs = 44100
burst = R.stereo(R.noise(fs, 0.9, 21), R.noise(fs, 0.9, 22)) + [0] * (2 * 5 * fs)
nb = len(burst) // 32
for warm in (0, 127):
    for fton in (127, 64):
        out, so = loop(mem, zero, zero, burst, kn(fdbk=127, fton=fton, warm=warm), g=32, blocks=nb, tag='fb1')
        sil = out[2 * fs:]
        last = out[-2 * fs:]
        nz = sum(1 for v in last if v)
        tail = next((i for i in range(len(sil) - 1, -1, -1) if sil[i]), -1)
        rep.check(f'FDBK 127 FTON {fton} WARM {warm}, unity chain: 1 s 0.9 FS noise then 5 s silence: bounded, '
                  'last second exactly 0', R.peak(out) <= 1.0 and nz == 0,
                  f'peak {R.peak(out):.3f} (silence part {R.peak(sil):.3f}); last nonzero sample {tail / 2 / fs:.2f} s '
                  f'into the silence; nonzero in last 1 s: {nz}')
for warm in (0, 127):
    out, so = loop(mem, zero, zero, burst, kn(fdbk=127, fton=127, warm=warm), g=127, blocks=nb, tag='fb2')
    sil = out[2 * fs:]
    rep.check(f'FDBK 127 WARM {warm} with a x3.97 chain (loop gain 3.8): bounded during silence by the leveller and clip '
              f'(|out| <= 0.95 * 2/3)', R.peak(sil) <= 0.95 * 2 / 3 + 1e-4,
              f'peak in silence {R.peak(sil):.3f}, last-second rms {R.db(R.rms(out[-2 * fs:])):.1f} dBFS '
              f'(it sustains: the chain has gain)')
# the leveller and WARM's divides, read from HEAD's state every block (-track): g = min(1, 0.3/env), 1/d = 1/(1+3w)
tf = R.WORK / 'track.txt'
out, so = loop(mem, zero, zero, burst[:2 * 2 * fs], kn(fdbk=127, fton=127, warm=90), g=127, blocks=2 * fs // 16,
               extra=['-track', 'c,f,21', '-trackinst', '2', '-trackout', str(tf)], tag='lev')
rows = [list(map(int, l.split())) for l in tf.read_text().splitlines()]
w = (90 << 16) / R.Q
gerr = max(abs(gw / R.Q - min(1.0, 0.3 / (ew / R.Q))) for _, ew, gw, _ in rows if ew > 0)
derr = max(abs(iw / R.Q - 1 / (1 + 3 * w)) for _, _, _, iw in rows)
above = sum(1 for _, ew, _, _ in rows if ew / R.Q > 0.3)
rep.check('leveller gain g = min(1, 0.3/env) and WARM 90 1/d = 1/(1+3w) as computed on the DSP (24-step divides)',
          gerr < 1e-5 and derr < 1e-5 and above > 100, f'max g error {gerr:.1e}, 1/d error {derr:.1e}; blocks with env > 0.3: {above}')
sil5 = [0] * (2 * 3 * fs)
out, so = loop(mem, zero, zero, sil5, kn(fdbk=127, fton=127, warm=127), g=127, blocks=len(sil5) // 32, tag='fb3')
rep.check('FDBK 127, x3.97 chain, silence from the start: every output sample exactly 0', not any(out),
          f'nonzero {sum(1 for v in out if v)}')
loud = R.stereo(R.noise(3 * fs, 0.99, 31), R.sine(60, 0.99, 3 * fs))
out, so = loop(mem, zero, zero, loud, kn(fdbk=127, fton=127, warm=127, inl=127), g=127, blocks=len(loud) // 32, tag='fb4')
rails = sum(1 for v in out if abs(v) >= R.FULL) / len(out)
rep.check('FDBK 127 IN 127 WARM 127, x3.97 chain, 3 s full-scale noise/60 Hz: finite and bounded (output limited at '
          'full scale by the store)', R.peak(out) <= 1.0, f'rms {R.db(R.rms(out)):.1f} dBFS, {rails * 100:.1f}% of samples at the rail')
# it does feed back: a short burst, FDBK 96, unity chain: repeats, decaying
short = R.stereo(R.sine(1000, 0.5, 256)) + [0] * (2 * fs)
out, so = loop(mem, zero, zero, short, kn(fdbk=96, fton=127), g=32, blocks=len(short) // 32, tag='fb5')
e = [R.rms(out[2 * (256 + 4410 * i): 2 * (256 + 4410 * (i + 1))]) for i in range(5)]
rep.check('FDBK 96, unity chain, 256-sample 1 kHz burst: the loop regenerates (energy after the burst) and decays '
          '(16-sample loop here, gain 0.72 a pass: gone by 100 ms)',
          e[0] > 1e-3 and e[1] < e[0] / 1000, 'rms per 100 ms after the burst: ' + ', '.join(f'{R.db(v):.1f}' for v in e) + ' dBFS')
out0, _ = loop(mem, zero, zero, short, kn(fdbk=0, fton=127), g=32, blocks=len(short) // 32, tag='fb6')
rep.check('FDBK 0 with the same burst: nothing after it', not any(out0[2 * 256:]), '')

# ---- 6. smoothing: knob jumps make per-sample ramps, not steps -------------------------
dcx = [R.Q // 4, -R.Q // 4] * n
dct = [R.Q // 4] * (2 * n)
ev = R.WORK / 'jumps.csv'
rows = []
for b in range(0, 600, 40):
    v = kn(fdbk=0, smpl=127 * ((b // 40) % 2), star=127 * ((b // 80) % 2), inl=(127, 0, 64)[(b // 40) % 3])
    rows.append(','.join(map(str, [b] + v)))
ev.write_text('\n'.join(rows) + '\n')
out, so = loop(mem, dct, dct, dcx, str(ev), tag='jump')
dl = max(abs(out[i + 2] - out[i]) for i in range(0, len(out) - 2, 2)) / R.Q
lim = 0.25 * (1 + 63 / 64) / 16 + 2 * 0.25 / 16
rep.check('IN 0<->127 and SMPL/STAR 0<->127 jumps (together) on 0.25 DC: the largest change between samples is the three '
          f'16-sample ramps at once (<= {lim:.4f}), never the jump itself (up to ~1.0)',
          dl <= lim + 1e-5 and R.peak(out) > 0.7, f'{dl:.4f} FS (output peak {R.peak(out):.3f}: the jumps happened)')
for slot in (0, 1, 5):
    rows = []
    for b in range(0, 600, 20):
        v = kn(fdbk=100, fton=127, warm=0)
        v[slot] = (127, 0)[(b // 20) % 2] if slot != 1 else (127, 10)[(b // 20) % 2]
        rows.append(','.join(map(str, [b] + v)))
    ev.write_text('\n'.join(rows) + '\n')
    out, so = loop(mem, zero, zero, R.stereo(R.sine(300, 0.4, n)), str(ev), g=32, tag='jump2')
    rep.check(f'{["FDBK", "FTON", "", "", "", "WARM"][slot]} jumping every 20 blocks under a looped 300 Hz tone: bounded',
              0.39 < R.peak(out) <= 1.0, f'peak {R.peak(out):.3f}')

# ---- 7. random automation, splits, guard -----------------------------------------------
import random
rnd = random.Random(5)
rows = [','.join(map(str, [b] + [rnd.randrange(128) for _ in range(6)] + [0] * 6)) for b in range(0, 3000, 3)]
ev.write_text('\n'.join(rows) + '\n')
nn = 16 * 3000
xr = R.stereo(R.noise(nn, 0.8, 41), R.noise(nn, 0.8, 42))
for split in (0, 5):
    extra = ['-guard', '-dirty', '0x77'] + (['-split', f'0,0,{split},{split}'] if split else [])
    out, so = loop(mem, R.stereo(R.noise(nn, 0.5, 43)), R.stereo(R.noise(nn, 0.5, 44)), xr, str(ev), g=127,
                   extra=extra, tag='auto')
    rep.check(f'random knobs every 3 blocks, x3.97 chain, split {split}, guard: bounded, no write outside HEAD',
              R.peak(out) <= 1.0 and head_strays(so) == (0, 0), f'peak {R.peak(out):.3f}, strays {head_strays(so)}')

# ---- 8. cost ------------------------------------------------------------------------------
cbus = dict(R.LIVE)
cbus[0x9c0] = [v & 0xffffff for v in R.noise(0x20, 0.5, 51)]
for gsl in range(16):
    cbus[0x3be00 + gsl * 0x20] = [v & 0xffffff for v in R.noise(32, 0.9, 60 + gsl)]
cm = R.WORK / 'cost.mem'
R.write_mem(cm, *CODE, own=5, bus=cbus)
nc = 16 * 2000
xc = R.stereo(R.noise(nc, 0.5, 52))
_, so0 = R.host(cm, [(nl['nlini'], nl['nlpro'])], [xc], [[0] * 12], 2000, tag='cnull')
null = R.meter(so0)[0]
res = {}
for name, k, extra in (('live, dearest (WARM>0 and env>0.3: both divides)', kn(fdbk=127, fton=127, smpl=127, star=127, inl=100, warm=90), []),
                       ('live, defaults', kn(), []),
                       ('live, dearest, trig split at frame 1 every block', kn(fdbk=127, fton=127, smpl=127, star=127, inl=100, warm=90), ['-split', '1']),
                       ('live, dearest, split at frame 8', kn(fdbk=127, fton=127, smpl=127, star=127, inl=100, warm=90), ['-split', '8'])):
    _, so = R.host(cm, [HEAD], [xc], [k], 2000, extra=extra, tag='cost')
    w, mn = R.meter(so)
    res[name] = (w - null, mn - null)
    rep.note(f'cost: {name}', f'worst block {w - null:.1f}, mean {mn - null:.1f} instructions/sample (core meter minus an rts-only effect)')
nm = R.WORK / 'costnl.mem'
R.write_mem(nm, *CODE, own=5, bus={})
_, so = R.host(nm, [HEAD], [xc], [kn(inl=100)], 2000, tag='costnl')
rep.note('cost: bus not live (IN-only loop)', f'worst block {R.meter(so)[0] - null:.1f} instructions/sample')
worst = max(v[0] for v in res.values())
rep.check('worst cost <= 200 emulator instructions/sample', worst <= 200, f'{worst:.1f}; charged on the unit at 7/4: {worst * 7 / 4:.0f}')
rep.save('/h/test_results.md')
