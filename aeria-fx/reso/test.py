"""RESO gate: tools/run.sh python3 /aeria/reso/test.py"""
import math
import os
import sys
sys.path.insert(0, '/aeria/tools')
import aeria as A

rep = A.Report('RESO')
mod, names, d, mems, init, proc, cost = A.standard(
    rep, '/aeria/reso/reso.asm', 'AERIA RESO', '/aeria/reso/manifest.py',
    {'MODE': 2, 'DECY': 127, 'DTUN': 127, 'TONE': 127, 'MIX': 127})
P0 = 44100 / (440 * 2 ** ((30 - 69) / 12))
blocks = 3000
n = blocks * 16
for mode, mname in enumerate(('WAVE', 'CPLX', 'DTND')):
    for idx in (12, 24, 48):
        p = list(d); p[6] = mode; p[0] = idx; p[1] = 64; p[2] = 90; p[3] = 127; p[4] = 0; p[5] = 127
        x = A.impulse(n, at=300 * 16, amp=0.9)
        L, R, _ = A.render(f'reso_{mode}_{idx}', mems, init, proc, p, x, blocks)
        seg = L[300 * 16 + 2000: 300 * 16 + 2000 + 16384]
        want = 44100 / (P0 * 2 ** (-idx / 12))
        cands = [want * 2 ** (c / 1200) for c in range(-100, 101, 2)]
        best = max(cands, key=lambda f: A.goertzel(seg, f))
        c = 1200 * math.log2(best / want)
        tol = 15 if mode != 2 else 100   # DTND's allpasses lower the pitch by design
        rep.check(f'{mname} PTCH {idx} ({want:.1f} Hz): impulse rings at the note',
                  (abs(c) <= tol if mode != 2 else c <= 5) and A.peak(L + R) <= 1.0 and A.rms(seg) > 1e-4,
                  f'peak bin {best:.1f} Hz ({c:+.0f} c), ring rms {A.db(A.rms(seg)):.1f} dBFS, peak {A.peak(L+R):.3f}')
    # decay: DECY 0 vs 127, energy 1 s after the impulse
    lv = []
    for dk in (0, 64, 127):
        p = list(d); p[6] = mode; p[0] = 24; p[2] = dk; p[3] = 127; p[4] = 0; p[5] = 127
        L, R, _ = A.render(f'reso_dec_{mode}_{dk}', mems, init, proc, p, A.impulse(n, at=300 * 16, amp=0.9), blocks)
        lv.append(A.rms(L[300 * 16 + 22050: 300 * 16 + 22050 + 4410]))
    rep.check(f'{mname}: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127)',
              lv[0] <= lv[1] <= lv[2] and lv[2] > 10 * max(lv[0], 1e-9),
              ' / '.join(f'{A.db(v):.1f}' for v in lv) + ' dBFS')
    # loud sustained input at longest decay: bounded, no DC, decays after input stops
    p = list(d); p[6] = mode; p[0] = 24; p[2] = 127; p[3] = 127; p[4] = 127; p[5] = 127
    want = 44100 / (P0 * 2 ** (-24 / 12))
    x = A.sine(want, 0.95, 441 * 100)
    L, R, _ = A.render(f'reso_hot_{mode}', mems, init, proc, p, x, 6000)
    tail = L[-4410:] + R[-4410:]
    rep.check(f'{mname}: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC',
              A.peak(L + R) <= 1.0 and abs(A.dc(L[600 * 16:])) < 1e-3,
              f'peak {A.peak(L+R):.3f}, dc {A.dc(L[600*16:]):+.5f}, tail rms {A.db(A.rms(tail)):.1f} dBFS')
# ---- stereo width (STEREO.md): R detuned by DTUN in WAVE and CPLX; DTND: R one allpass earlier in the loop ----
def wet(mode, dtun, ptch=24, decy=80):
    def run(xx, bl):
        q = list(d); q[6] = mode; q[0] = ptch; q[2] = decy; q[4] = dtun; q[5] = 127
        return A.render(f'reso_w{mode}_{dtun}', mems, init, proc, q, xx, bl)[:2]
    return run


f24 = 44100 / (P0 * 2 ** (-24 / 12))
# The sine is AT the note: each side is then the forced response of its resonator, the same frequency on both sides, and
# the correlation is the cosine of the phase between them. CPLX (one pole pair a side, driven into its clip) locks both
# sides to the drive at the default DTUN; it is the ringing after a hit that is wide there (the burst row). DTND
# places each PARTIAL left or right (level, not time): one steady sine is one partial, so it reads as a pan (the two
# rms figures), not as a low correlation -- its sine rows are gated on the mono sum only. No step bound here: a
# ringing string is full of harmonics.
for mode, mname in enumerate(('WAVE', 'CPLX', 'DTND')):
    for dt in (40, 127):
        A.stereo_report(rep, f'{mname} DTUN {dt}{" (default)" if dt == 40 else ""}, PTCH 24, MIX 127, sine at the note'
                        + (' (one partial: a pan)' if mode == 2 else ''),
                        wet(mode, dt), True, secs=4.0, fsine=f24, corr_max=((0.9, 0.9), (0.9, 0.99), (0.9, 1.0))[mode])
A.stereo_report(rep, 'WAVE DTUN 0', wet(0, 0), False, secs=3.0, fsine=f24)
A.stereo_report(rep, 'CPLX DTUN 0', wet(1, 0), False, secs=3.0, fsine=f24)
# DTND: left + right = 2 x the old mono output (0.6 v through the same soft clip) while neither side clips: compare with
# the pre-change build's output, and show where DTUN 0 stands (1-sample allpasses: mono but for the top octave)
import shutil
shutil.copy('/aeria/reso/reso.asm.prewide', A.BUILD / 'reso_prewide.asm')
om, _, oi, op = A.assemble(A.BUILD / 'reso_prewide.asm', 'AERIA RESOLD')
bl = int(3 * 44100 / 16)
for dt in (0, 40, 127):
    q = list(d); q[6] = 2; q[0] = 24; q[2] = 80; q[4] = dt; q[5] = 127
    xx = A.burst(bl * 16, amp=0.2)
    L, R, _ = A.render('reso_dt_new', mems, init, proc, q, xx, bl)
    Lo, Ro, _ = A.render('reso_dt_old', om, oi, op, q, xx, bl)
    # DC removed from both (quality pass: the old build's DC mean had a dead band of up to 4,095 LSB, a DC
    # offset of about -30 dB re this quiet output; the new 48-bit mean has none)
    mn = sum(L[13230:] + R[13230:]) / 2 / len(L[13230:]); mo = sum(Lo[13230:]) / len(Lo[13230:])
    err = A.rms([(u + v) / 2 - mn - (w - mo) for u, v, w in zip(L[13230:], R[13230:], Lo[13230:])]) / max(A.rms([w - mo for w in Lo[13230:]]), 1e-12)
    c = A.corr(L[13230:], R[13230:])
    rep.check(f'DTND DTUN {dt}: (L + R) / 2 is the old mono output (quiet burst: the clip is still linear; DC removed), within -35 dB '
              f'(-40 before the quality pass: the loop now stores toward zero, a 1-LSB change a write that the long ring integrates)',
              A.db(err) < -35 and Lo == Ro, f'difference {A.db(err):.1f} dB re the old output; L/R corr {c:+.3f}, '
              f'L {A.db(A.rms(L[13230:])):.1f} R {A.db(A.rms(R[13230:])):.1f} old {A.db(A.rms(Lo[13230:])):.1f} dBFS')
# ---- quality pass (12 Nov 2026) ----------------------------------------------------------------------
# tails end in exact silence (toward-zero storage of the recursive states, 48-bit CPLX and DTND DC mean)
blt = int(8 * 44100 / 16)
xb = A.noise(4410, 0.5, 6) + [0] * (blt * 16 - 4410)
res = []
for mode, decy, tone, ptch in ((0, 80, 90, 24), (0, 60, 110, 40), (1, 80, 90, 24), (1, 60, 110, 40),
                               (2, 80, 90, 24), (2, 60, 110, 40), (2, 127, 0, 0)):
    q = list(d); q[6] = mode; q[0] = ptch; q[2] = decy; q[3] = tone; q[5] = 127
    L, R, _ = A.render(f'reso_q_tail_{mode}_{decy}_{tone}_{ptch}', mems, init, proc, q, xb, blt)
    nz = [i for i in range(len(L)) if L[i] or R[i]]
    res.append((('WAVE', 'CPLX', 'DTND')[mode], decy, tone, ptch, max(abs(v) for v in L[-22050:] + R[-22050:]),
                (nz[-1] if nz else 0) / 44100))
rep.check('noise burst then silence: the last 0.5 s of 8 s is exactly 0 in every mode (before this pass WAVE held '
          '-60 LSB and DTND up to +1,091 LSB of DC, CPLX sang on at up to -62 dBFS)',
          all(r_[4] == 0 for r_ in res), '; '.join(f'{m_} DECY {a_} TONE {b_} PTCH {c_}: silent from {e_:.2f} s'
                                                   for m_, a_, b_, c_, _, e_ in res))
# CPLX no longer holds a limit cycle: DECY 100 (4.6 s to -40 dB), F#6, falls on at its rate
q = list(d); q[6] = 1; q[0] = 60; q[2] = 100; q[3] = 64; q[5] = 127
blt = int(11 * 44100 / 16)
L, R, _ = A.render('reso_q_cplx', mems, init, proc, q, A.noise(4410, 0.5, 6) + [0] * (blt * 16 - 4410), blt)
lv = [A.db(A.rms(L[int(t_ * 44100):int(t_ * 44100) + 4410]) or 1e-30) for t_ in (2, 6, 10)]
rep.check('CPLX DECY 100 PTCH 60 after a burst: the ring keeps falling at about 40 dB / 4.6 s (2 -> 6 -> 10 s: '
          'at least 25 dB a step; before: stuck at -62 dBFS from 6 s on)', lv[0] - lv[1] > 25 and lv[1] - lv[2] > 25,
          ' -> '.join(f'{v:.0f}' for v in lv) + ' dBFS')
# DTND: the two sides at the same level on noise (the side term balanced by SK; it leaned 1.5-2.5 dB right)
bn = int(5 * 44100 / 16)
xn = A.noise(bn * 16, 0.1, 8)
bal = []
for ptch in (0, 24, 48):
    for dt in (20, 64, 127):
        q = list(d); q[6] = 2; q[0] = ptch; q[2] = 90; q[3] = 110; q[4] = dt; q[5] = 127
        L, R, _ = A.render(f'reso_q_bal_{ptch}_{dt}', mems, init, proc, q, xn, bn)
        bal.append((ptch, dt, A.db(A.rms(L[44100:])) - A.db(A.rms(R[44100:]))))
mb = sum(b_ for _, _, b_ in bal) / len(bal)
rep.check(f'DTND on 0.1 FS noise, PTCH 0/24/48 x DTUN 20/64/127: no lean (mean L - R within +-0.6 dB: {mb:+.2f}; '
          'before this pass -1.6..-1.9 dB, every case right-heavy) and every case within +-3 dB (resonant noise: '
          'a few partials, so one case moves by 1-2 dB with the noise realisation)',
          abs(mb) <= 0.6 and all(abs(b_) <= 3.0 for _, _, b_ in bal), ', '.join(f'{p_}/{t_}: {b_:+.1f}' for p_, t_, b_ in bal) + ' dB (L - R)')
import subprocess
subprocess.run([sys.executable, '/aeria/reso/gen.py'], env=dict(os.environ, SK='0', OUT=str(A.BUILD / 'reso_sk0.asm')), check=True, capture_output=True)
km, _, ki, kp = A.assemble(A.BUILD / 'reso_sk0.asm', 'AERIA RESOSK0')
worst = -1e9
for dt in (0, 40, 127):
    q = list(d); q[6] = 2; q[0] = 24; q[2] = 80; q[4] = dt; q[5] = 127
    xx = A.burst(bl * 16, amp=0.2)
    L, R, _ = A.render('reso_sk_new', mems, init, proc, q, xx, bl)
    L0, R0, _ = A.render('reso_sk_0', km, ki, kp, q, xx, bl)
    err = A.rms([(u + v) - (u0 + v0) for u, v, u0, v0 in zip(L, R, L0, R0)]) / max(A.rms([u0 + v0 for u0, v0 in zip(L0, R0)]), 1e-12)
    worst = max(worst, A.db(err))
rep.check('DTND: the side balance term (SK) leaves the mono sum alone: L + R equals that of the SK=0 build of this source '
          '(quiet burst, DTUN 0/40/127) within -60 dB', worst < -60, f'worst {worst:.1f} dB')
rep.note('cost', f'static {cost[0]} cycles/sample (worst mode); dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f}')
rep.save('/aeria/reso/test_results.md')
