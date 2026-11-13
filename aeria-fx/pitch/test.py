"""PITCH gate: run with tools/run.sh python3 /aeria/pitch/test.py"""
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

rep = A.Report('PITCH')
asm, man = '/aeria/pitch/pitch.asm', '/aeria/pitch/manifest.py'
mod, names, d, mems, init, proc, cost = A.standard(
    rep, asm, 'AERIA PITCH', man, {'SHFT': 0, 'GRN': 127, 'FDBK': 127, 'TONE': 127, 'MIX': 127, 'WIDE': 127})
WIDE = names.index('WIDE')

blocks = 5000
n = blocks * A.FRAMES
f0 = 440.0
x = A.sine(f0, 0.5, n)
import math
def sgn(v): return v - (1 << 24) if v & 0x800000 else v
worst_c = 0
for idx in range(49):
    p = list(d); p[0] = idx
    A.render('pitch_track', mems, init, proc, p, None, 300,
             extra=['-track', '5,8', '-trackout', '/aeria/.build/track.txt'])
    last = open('/aeria/.build/track.txt').read().split('\n')[-2].split()
    inc, wh = sgn(int(last[1])) / 2**23, int(last[2]) / 2**23 * 8192
    r_eff = 1 - inc * wh
    c = 1200 * math.log2(r_eff / 2 ** ((idx - 24) / 12))
    worst_c = max(worst_c, abs(c))
rep.check('head speed (from DSP state: 1 - inc*W) matches 2^(SHFT/12) for all 49 SHFT values',
          worst_c <= 2.0, f'worst error {worst_c:.2f} cents')
for idx, st in ((36, 12), (24, 0), (31, 7), (12, -12), (0, -24), (48, 24)):
    p = list(d); p[0] = idx; p[3] = 0; p[4] = 127; p[5] = 127; p[WIDE] = 0   # the set pitch: WIDE 0 (WIDE puts L above, R below)
    L, R, _ = A.render(f'pitch_{st}', mems, init, proc, p, x, blocks)
    seg = L[-65536:]
    want = f0 * 2 ** (st / 12)
    fc = abs(1 - 2 ** (st / 12)) * 44100 / 2866 + 1.0
    cands = [want + k * 0.25 for k in range(-200, 201)]
    best = max(cands, key=lambda f: A.goertzel(seg, f))
    lvl = A.goertzel(seg, best)
    rep.check(f'SHFT {st:+d}: 440 Hz sine -> {want:.1f} Hz: spectral peak within one crossfade-rate '
              f'sideband ({fc:.1f} Hz), level > 0.3, nothing left at 440',
              abs(best - want) <= fc and lvl > 0.3 and (st == 0 or A.goertzel(seg, 440.0) < 0.01),
              f'peak {best:.2f} Hz amp {lvl:.3f}; 440 Hz residue {A.goertzel(seg, 440.0):.4f}')
# FINE: +64 -> ~ +1 st (FINE 127 = +63/64 st)
p = list(d); p[0] = 24; p[1] = 127; p[3] = 0; p[4] = 127; p[5] = 127; p[WIDE] = 0
L, R, _ = A.render('pitch_fine', mems, init, proc, p, x, blocks)
want = f0 * 2 ** (63 / 64 / 12)
cands = [want * 2 ** (c / 1200) for c in range(-40, 41, 2)]
best = max(cands, key=lambda f: A.goertzel(L[-65536:], f))
rep.check('FINE 127: +0.98 st within 10 cents', abs(1200 * __import__('math').log2(best / want)) <= 10, f'{best:.1f} Hz vs {want:.1f}')
# Feedback at max: impulse, bounded, decays, no DC.
p = list(d); p[0] = 36; p[3] = 127; p[4] = 127; p[5] = 127
blocks2 = 6000
L, R, _ = A.render('pitch_fb', mems, init, proc, p, A.noise(300 * 16, 0.9), blocks2)
tail = L[-22050:] + R[-22050:]
rep.check('FDBK 127 TONE 127, 0.9 FS noise burst then 5.6 s silence: bounded, decaying, no DC',
          A.peak(L + R) <= 1.0 and A.rms(tail) < 0.01 and abs(A.dc(tail)) < 1e-3,
          f'peak {A.peak(L+R):.3f}, last 0.5 s rms {A.db(A.rms(tail)):.1f} dBFS, dc {A.dc(tail):+.6f}')
p[4] = 0
L, R, _ = A.render('pitch_fb_dark', mems, init, proc, p, A.noise(300 * 16, 0.9), blocks2)
tail = L[-22050:] + R[-22050:]
rep.check('FDBK 127 TONE 0: bounded, decaying', A.peak(L + R) <= 1.0 and A.rms(tail) < 0.01,
          f'peak {A.peak(L+R):.3f}, tail rms {A.db(A.rms(tail)):.1f} dBFS')
# stereo independence: L sine, R silence, FDBK max -> R stays silent
p = list(d); p[0] = 36; p[3] = 127; p[4] = 127; p[5] = 127
xs = []
for v in A.sine(440.0, 0.5, 1200 * 16):
    xs += [v, 0]
L, R, _ = A.render('pitch_stereo', mems, init, proc, p, xs, 1200, stereo_in=True)
rep.check('stereo: L-only input (FDBK 127) leaves R silent', max(abs(v) for v in R) == 0 and A.peak(L) > 0.1,
          f'R peak {max(abs(v) for v in R)} LSB, L peak {A.peak(L):.3f}')
# ---- stereo: WIDE = L/R micro-detune ----------------------------------------------------------
def peak_cents(seg, f_ref, lo=-60, hi=60):
    best = max(range(lo * 2, hi * 2 + 1), key=lambda c: A.goertzel(seg, f_ref * 2 ** (c / 2400)))
    return best / 2


for idx, st in ((24, 0), (36, 12), (17, -7)):
    want = f0 * 2 ** (st / 12)
    p = list(d); p[0] = idx; p[3] = 0; p[4] = 127; p[5] = 127; p[WIDE] = 0
    L, R, _ = A.render(f'pitch_wide_{st}_0', mems, init, proc, p, x, blocks)
    c0 = peak_cents(L[-65536:], want)      # where this method puts the set pitch (splice sidebands pull it a few cents)
    for wk in (40, 127):
        p[WIDE] = wk
        L, R, _ = A.render(f'pitch_wide_{st}_{wk}', mems, init, proc, p, x, blocks)
        half = 1200 * math.log2(1 + 0.029302 * wk / 127) / 2
        cl, cr = peak_cents(L[-65536:], want) - c0, peak_cents(R[-65536:], want) - c0
        rep.check(f'WIDE {wk} at SHFT {st:+d}: left {half:+.1f} c, right {-half:+.1f} c around the WIDE 0 pitch (+-2.5 c)',
                  abs(cl - half) <= 2.5 and abs(cr + half) <= 2.5,
                  f'left {cl:+.1f} c, right {cr:+.1f} c, split {cl - cr:.1f} c (want {2 * half:.1f}); WIDE 0 peak {c0:+.1f} c from nominal')


def wet(wk, shft=36):
    def run(xx, bl):
        p = list(d); p[0] = shft; p[3] = 0; p[4] = 127; p[5] = 127; p[WIDE] = wk
        return A.render(f'pitch_w{wk}_{shft}', mems, init, proc, p, xx, bl)[:2]
    return run


A.stereo_report(rep, 'defaults (SHFT +12, WIDE 40), MIX 127', wet(40), True, fmax=880 * 1.2)
A.stereo_report(rep, 'unison (SHFT 0), WIDE 40, MIX 127', wet(40, 24), True, fmax=440 * 1.2)
A.stereo_report(rep, 'WIDE 127, MIX 127', wet(127), True, fmax=880 * 1.2)
A.stereo_report(rep, 'WIDE 0, MIX 127', wet(0), False, fmax=880 * 1.2)
# WIDE back to 0 mid-flight: the right heads walk home, then L = R again
p = list(d); p[3] = 0; p[4] = 127; p[5] = 127; p[WIDE] = 127
p0 = list(p); p0[WIDE] = 0
bl = int(6 * 44100 / 16)
L, R, _ = A.render('pitch_wide_home', mems, init, proc, p, A.sine(f0, 0.5, bl * 16), bl, events=[(0, p), (int(1.5 * 44100 / 16), p0)])
tail = max(abs(a - b) for a, b in zip(L[-44100:], R[-44100:]))
mid = A.corr(L[44100:66150], R[44100:66150])
st_ = max(abs(a - b) for a, b in zip(R[66150:], R[66151:])) / A.FS
rep.check('WIDE 127 -> 0 at 1.5 s: right walks back to left without a click; the last second is L = R exactly',
          tail == 0 and st_ <= 2 * math.sin(math.pi * 880 * 1.3 / 44100) * A.peak(R) * 1.05,
          f'max |L - R| in the last second {tail} LSB; corr while wide {mid:+.3f}; largest right step after the move {st_:.4f} FS')
# ---- quality pass (12 Nov 2026): tails reach exact silence; FDBK jumps glide per sample ----------------------
blt = int(12 * 44100 / 16)
xb = A.noise(13230, 0.9, 5) + [0] * (blt * 16 - 13230)
worst = []
for fb, tone, sh in ((127, 127, 36), (127, 0, 36), (127, 64, 24), (64, 100, 0), (127, 127, 48), (0, 100, 36)):
    p = list(d); p[0] = sh; p[3] = fb; p[4] = tone; p[5] = 127
    L, R, _ = A.render(f'pitch_qtail_{fb}_{tone}_{sh}', mems, init, proc, p, xb, blt)
    nz = [i for i in range(len(L)) if L[i] or R[i]]
    worst.append((max(abs(v) for v in L[-88200:] + R[-88200:]), (nz[-1] if nz else 0) / 44100, fb, tone, sh))
rep.check('noise burst then 11.7 s silence, FDBK 127/64/0 x TONE x SHFT: the last 2 s are exactly 0 (48-bit tone LP and DC block, '
          'rounded clip input: no stuck DC, no limit cycle; before this pass FDBK 127 held -576 LSB of DC forever)',
          all(w[0] == 0 for w in worst), '; '.join(f'FDBK {w[2]} TONE {w[3]} SHFT {w[4]-24:+d}: silent from {w[1]:.2f} s' for w in worst))
bz = int(4 * 44100 / 16)
pz = list(d); pz[0] = 24; pz[3] = 0; pz[4] = 127; pz[5] = 127; pz[WIDE] = 0
evz = []
for i, blk in enumerate(range(0, bz, 256)):
    q = list(pz); q[3] = 0 if i % 2 else 127; evz.append((blk, q))
xz = A.sine(300.0, 0.5, bz * 16)
L, R, _ = A.render('pitch_qzip', mems, init, proc, pz, xz, bz, events=evz)
L0, _, _ = A.render('pitch_qzip0', mems, init, proc, pz, xz, bz)
def d2(v): return max(abs(v[i + 1] - 2 * v[i] + v[i - 1]) for i in range(22050, len(v) - 1)) / A.FS
rep.check('FDBK jumping 0 <-> 127 every 93 ms on a 300 Hz sine (unison): largest second difference < 0.004 FS '
          '(the feedback gain ramps per sample; per-block steps gave 0.0165)', d2(L) < 0.004 and d2(R) < 0.004,
          f'L {d2(L):.5f} R {d2(R):.5f} FS; no knob moves {d2(L0):.5f} FS')
rep.note('cost', f'static {cost[0]} cycles/sample; dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f} instr/sample')
rep.save('/aeria/pitch/test_results.md')
