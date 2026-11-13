"""RING gate: tools/run.sh python3 /aeria/ringph/test.py"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

rep = A.Report('RING')
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/ringph/ringph.asm', 'AERIA RING', '/aeria/ringph/manifest.py',
    {'MODE': 1, 'FREQ': 127, 'FOLW': 127, 'SPRD': 64, 'BIAS': 127, 'MIX': 64})
blocks = 700
n = blocks * 16
# RING: 1 kHz in, carrier at FREQ -> sidebands at fc +- 1 kHz, nothing at 1 kHz
fin = 1000.0
for fk in (64, 100):
    fc = 20 * 250 ** (fk / 127)
    p = list(d); p[6] = 0; p[0] = fk; p[1] = 0; p[2] = 0; p[3] = 0; p[5] = 127
    L, R, _ = A.render(f'rp_ring_{fk}', mems, init, proc, p, A.sine(fin, 0.5, n), blocks)
    seg = L[-11025:]
    lo, hi, mid = A.goertzel(seg, abs(fc - fin)), A.goertzel(seg, fc + fin), A.goertzel(seg, fin)
    rep.check(f'RING FREQ {fk} (carrier {fc:.0f} Hz): 1 kHz in -> sidebands {abs(fc-fin):.0f} and {fc+fin:.0f} Hz '
              f'each ~0.25, carrier-free input bin', abs(lo - 0.25) < 0.02 and abs(hi - 0.25) < 0.02 and mid < 0.01
              and L == R, f'{lo:.3f} / {hi:.3f}, 1 kHz {mid:.4f}')
# FOLW: louder input raises the carrier
fk = 50
fc = 20 * 250 ** (fk / 127)
res = []
for folw in (0, 127):
    p = list(d); p[6] = 0; p[0] = fk; p[1] = folw; p[3] = 0; p[5] = 127
    L, R, _ = A.render(f'rp_folw_{folw}', mems, init, proc, p, A.sine(fin, 0.5, n), blocks)
    seg = L[-11025:]
    fcs = [fc * 2 ** (c / 12) for c in range(0, 40)]
    best = max(fcs, key=lambda f: A.goertzel(seg, f + fin))
    res.append(best)
rep.check('RING FOLW 127 with a 0.5 FS input raises the carrier (StoreFX: up to x7)', res[1] > 3 * res[0],
          f'carrier {res[0]:.0f} Hz -> {res[1]:.0f} Hz')
# SPRD: the right carrier leads by 0..90 degrees. 127 = quadrature: for a sine in, each sideband is +-90 degrees between
# the sides (correlation 0), and L+R is the same ring 3 dB up. Checked against the carrier phase itself: with a DC input
# the output IS the carrier, so L and R must be sin and cos.
p = list(d); p[6] = 0; p[0] = 64; p[1] = 0; p[2] = 127; p[3] = 0; p[5] = 127
L, R, _ = A.render('rp_sprd', mems, init, proc, p, A.sine(fin, 0.5, n), blocks)
c = A.corr(L[-8000:], R[-8000:])
m = A.mono_db(L[-8000:], R[-8000:])
rep.check('RING SPRD 127: right carrier in quadrature (L/R correlation ~0, mono sum +3 dB: no cancellation)',
          abs(c) < 0.05 and abs(m - 3.01) < 0.3, f'corr {c:+.3f}, L+R {m:+.2f} dB re L')
fc = 20 * 250 ** (64 / 127)
res = []
for sp in (0, 64, 127):
    p[2] = sp
    L, R, _ = A.render(f'rp_sprd_dc{sp}', mems, init, proc, p, [round(0.5 * A.FS)] * n, blocks)
    seg = slice(n - 8820, n)
    # phase of each side's carrier at fc by quadrature projection; lead of R over L in degrees
    def ph(xx):
        re = sum(v * math.cos(2 * math.pi * fc * i / 44100) for i, v in enumerate(xx))
        im = sum(v * math.sin(2 * math.pi * fc * i / 44100) for i, v in enumerate(xx))
        return math.degrees(math.atan2(re, im))
    lead = (ph(R[seg]) - ph(L[seg]) + 180) % 360 - 180
    res.append((sp, lead, 90 * sp / 128))
rep.check('RING SPRD 0 / 64 / 127: right carrier leads by 0 / 45 / 89.3 degrees (+-3)',
          all(abs(l - w) <= 3 for _, l, w in res), '; '.join(f'SPRD {s_}: {l:+.1f} (want {w:.1f})' for s_, l, w in res))
# PHSR: static filter (DPTH 0, FDBK 0) measured from a small impulse response
p = list(d); p[6] = 1; p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 64; p[5] = 127
L, R, _ = A.render('rp_ph_static', mems, init, proc, p, A.impulse(n, at=400 * 16, amp=0.1), blocks)
ir = L[400 * 16: 400 * 16 + 4096]
grid = [200 * 25 ** (i / 120) for i in range(121)]
resp = [A.goertzel(ir, f) * len(ir) / 2 / 0.1 for f in grid]
lo = min(resp)
rep.check('PHSR DPTH 0 FDBK 0: static response has notches (min at least 30 dB under the peak)',
          lo < max(resp) / 31.6, f'min {A.db(lo):.1f} dB at {grid[resp.index(lo)]:.0f} Hz, max {A.db(max(resp)):.1f} dB')
p[1] = 127
x = A.noise(n, 0.5)
L, R, _ = A.render('rp_ph_fb', mems, init, proc, p, x, blocks)
rep.check('PHSR FDBK 127 on 0.5 FS noise: bounded, DC gain bounded (0.7(x+v): 1.4 at DC; |dc out| <= 1.4 |dc in| + 1e-3)',
          A.peak(L + R) <= 1.0 and abs(A.dc(L)) <= 1.4 * abs(A.dc(x)) + 1e-3,
          f'peak {A.peak(L+R):.3f}, dc out {A.dc(L):+.5f}, dc in {A.dc(x):+.5f}')
grid = [200 * 25 ** (i / 60) for i in range(61)]
p[0] = 127; p[3] = 127
L, R, _ = A.render('rp_ph_sweep', mems, init, proc, p, x, blocks)
a1 = [A.goertzel(L[2000:6096], f) for f in grid]
a2 = [A.goertzel(L[6100:10196], f) for f in grid]
diff = max(abs(A.db(u) - A.db(v)) for u, v in zip(a1, a2))
rep.check('PHSR RATE 127 DPTH 127: the spectrum moves between two 93 ms windows', diff > 10 and A.peak(L + R) <= 1.0,
          f'max band change {diff:.1f} dB')
# ---- stereo width (STEREO.md) ---------------------------------------------------------------
def wet(mode, sprd, kw={}):
    def run(xx, bl):
        q = list(d); q[6] = mode; q[2] = sprd; q[5] = 127
        for k, v in kw.items():
            q[k] = v
        return A.render(f'rp_w{mode}_{sprd}', mems, init, proc, q, xx, bl)[:2]
    return run


A.stereo_report(rep, 'RING defaults (SPRD 127), MIX 127', wet(0, 127), True, fmax=5000)
A.stereo_report(rep, 'RING SPRD 0, MIX 127', wet(0, 0), False, fmax=5000)
# PHSR as the mode view sets it (RATE 40, FDBK 64, DPTH 127, CNTR 64)
PH = {0: 40, 1: 64, 3: 127, 4: 64}
A.stereo_report(rep, 'PHSR (RATE 40 FDBK 64 DPTH 127) SPRD 127, MIX 127', wet(1, 127, PH), True, secs=6.0, corr_max=0.95, fmax=440 * 1.5)
A.stereo_report(rep, 'PHSR SPRD 0, MIX 127', wet(1, 0, PH), False, fmax=440 * 1.5)
# ---- quality pass (12 Nov 2026) --------------------------------------------------------------------
# FREQ 127 is the 5 kHz the display reads (the law was v/128: 4,789 Hz)
p = list(d); p[6] = 0; p[0] = 127; p[1] = 0; p[2] = 0; p[3] = 0; p[5] = 127
L, R, _ = A.render('rp_q_top', mems, init, proc, p, A.sine(fin, 0.5, n), blocks)
seg = L[-11025:]
cands = [4900 + 2 * k for k in range(101)]
best = max(cands, key=lambda f: A.goertzel(seg, f + fin))
rep.check('RING FREQ 127: carrier 5000 Hz (+-10), as the display law 20 x 250^(v/127) reads', abs(best - 5000) <= 10, f'carrier {best} Hz')
# PHSR tails end in exact silence (rounded stages, 48-bit feedback mean, silence flush after 0.74 s)
blt = int(4 * 44100 / 16)
xb = A.noise(22050, 0.7, 6) + [0] * (blt * 16 - 22050)
res = []
for fb_, c_, dp_ in ((0, 64, 127), (64, 0, 0), (127, 0, 0), (127, 64, 127), (127, 20, 60), (64, 127, 0)):
    p = list(d); p[6] = 1; p[0] = 40; p[1] = fb_; p[2] = 127; p[3] = dp_; p[4] = c_; p[5] = 127
    L, R, _ = A.render(f'rp_q_tail_{fb_}_{c_}_{dp_}', mems, init, proc, p, xb, blt)
    nz = [i for i in range(len(L)) if L[i] or R[i]]
    res.append((fb_, c_, dp_, max(abs(v) for v in L[-44100:] + R[-44100:]), (nz[-1] if nz else 0) / 44100))
rep.check('PHSR: noise burst then silence, FDBK 0/64/127 x CNTR x DPTH: the last second is exactly 0 '
          '(before this pass the allpass truncation and the feedback mean held up to 555 LSB of DC for ever)',
          all(r_[3] == 0 for r_ in res), '; '.join(f'FDBK {a_} CNTR {b_} DPTH {c_}: silent from {e_:.2f} s' for a_, b_, c_, _, e_ in res))
# CNTR jumps 0 <-> 127 (PHSR, DPTH 0, a 300 Hz sine): the centre glides per block, no ringing burst
bz = int(3 * 44100 / 16)
pz = list(d); pz[6] = 1; pz[0] = 40; pz[1] = 64; pz[2] = 127; pz[3] = 0; pz[4] = 64; pz[5] = 127
evz = []
for i, blk in enumerate(range(0, bz, 256)):
    q_ = list(pz); q_[4] = 0 if i % 2 else 127; evz.append((blk, q_))
L, R, _ = A.render('rp_q_cntr', mems, init, proc, pz, A.sine(300.0, 0.5, bz * 16), bz, events=evz)
d2 = max(abs(L[i + 1] - 2 * L[i] + L[i - 1]) for i in range(22050, len(L) - 1)) / A.FS
rep.check('PHSR CNTR jumping 0 <-> 127 every 93 ms (DPTH 0, 300 Hz sine): largest second difference < 0.03 FS '
          '(CNTR glides ~3 ms a block; before it stepped and the notches rang: 0.22 FS)', d2 < 0.03, f'{d2:.4f} FS')
rep.note('cost', f'static {cost[0]} cycles/sample (worst mode); dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f}')
rep.save('/aeria/ringph/test_results.md')
