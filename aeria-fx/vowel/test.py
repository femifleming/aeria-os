"""VOWL gate: tools/run.sh python3 /aeria/vowel/test.py"""
import cmath
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/vowel')
import aeria as A
import gen as G          # constants + the float reference of the plugin's updateCoefs; writes nothing

rep = A.Report('VOWL')
NAMES = ['VOWL', 'SHFT', 'RES', 'LEVL', 'WIDE', 'MIX', '', '', '', '', '', '']
DEAR = {'VOWL': 64, 'SHFT': 0, 'RES': 127, 'LEVL': 1, 'MIX': 64, 'WIDE': 127}
mod, names, d, mems, init, proc, cost = A.standard(rep, '/aeria/vowel/vowel.asm', 'AERIA VOWL',
                                                   '/aeria/vowel/manifest.py', DEAR)
assert names == NAMES, names
FS = 44100
VK = (0, 32, 64, 95, 127)          # the knob value nearest each vowel a e i o u
RAIL = 1.0 + 1e-6                  # -2^23 is the negative rail (as aeria.standard allows)


def model(x, vk, sk=64, rk=67):
    """FormantFilter.h in floats (mono in on both channels, knobs static): the wet."""
    sv = []
    for hz, qq, amp in G.formants(vk, sk, rk):
        g = math.tan(math.pi * min(max(hz, 10.0), FS * 0.45) / FS)
        k = 1 / max(0.05, qq)
        a1 = 1 / (1 + g * (g + k))
        sv.append([a1, g * a1, g * g * a1, k * amp, 0.0, 0.0])
    pc, gc = 1 - math.exp(-1 / (0.25 * FS)), 1 - math.exp(-1 / (0.08 * FS))
    pin = pout = 0.0
    gain, out = 1.0, []
    for v in x:
        v /= A.FS
        acc = 0.0
        for f in sv:
            v3 = v - f[5]
            v1 = f[0] * f[4] + f[1] * v3
            v2 = f[5] + f[1] * f[4] + f[2] * v3
            f[4] = 2 * v1 - f[4]
            f[5] = 2 * v2 - f[5]
            acc += f[3] * v1
        pin += pc * (2 * v * v - pin)
        pout += pc * (2 * acc * acc - pout)
        gain += gc * (min(32.0, max(0.1, math.sqrt((pin + 1e-9) / (pout + 1e-9)))) - gain)
        out.append(acc * gain * A.FS)
    return out


def params(**kw):
    p = list(d)
    p[NAMES.index('WIDE')] = 0     # the plugin comparisons below are at WIDE 0 (both sides on the set formants)
    for k, v in kw.items():
        p[NAMES.index(k)] = v
    return p


def fft(x):
    n = len(x)
    j = 0
    x = list(x)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    m = 2
    while m <= n:
        w = [cmath.exp(-2j * math.pi * k / m) for k in range(m // 2)]
        for s in range(0, n, m):
            for k in range(m // 2):
                a, b = x[s + k], x[s + k + m // 2] * w[k]
                x[s + k], x[s + k + m // 2] = a + b, a - b
        m *= 2
    return x


NF = 8192
WIN = [0.5 - 0.5 * math.cos(2 * math.pi * i / NF) for i in range(NF)]


def psd(x):
    """Welch power spectrum (Hann, 50 % overlap), bins 0..NF/2, 3-bin smoothed."""
    acc = [0.0] * (NF // 2 + 1)
    cnt = 0
    for s in range(0, len(x) - NF + 1, NF // 2):
        X = fft([v * w / A.FS for v, w in zip(x[s:s + NF], WIN)])
        for k in range(NF // 2 + 1):
            acc[k] += X[k].real ** 2 + X[k].imag ** 2
        cnt += 1
    acc = [a / cnt for a in acc]
    return [(acc[max(k - 1, 0)] + acc[k] + acc[min(k + 1, NF // 2)]) / 3 for k in range(NF // 2 + 1)]


def peak_in(p, lo, hi):
    """(frequency, power) of the strongest bin in [lo, hi] Hz, centroid over +-3 bins."""
    k0, k1 = int(lo * NF / FS), int(hi * NF / FS) + 1
    k = max(range(k0, k1), key=lambda i: p[i])
    ks = range(k - 3, k + 4)
    return sum(i * p[i] for i in ks) / sum(p[i] for i in ks) * FS / NF, p[k]


def width3(p, f):
    """-3 dB width (Hz) of the peak nearest f."""
    k = max(range(int(f * 0.9 * NF / FS), int(f * 1.1 * NF / FS) + 1), key=lambda i: p[i])
    a = b = k
    while a > 1 and p[a] > p[k] / 2:
        a -= 1
    while b < NF // 2 and p[b] > p[k] / 2:
        b += 1
    return (b - a) * FS / NF


BL = 11025                          # 4 s
N = BL * 16
NOISE = A.noise(N, 0.25)
TAIL = slice(N // 4, N)

# ---- formant peaks per vowel (RES 100 = q 3.15: bands narrow enough to separate F2 from F1's skirt)
for i, vk in enumerate(VK):
    L, R, _ = A.render(f'vw_v{i}', mems, init, proc, params(VOWL=vk, RES=100), NOISE, BL)
    p = psd(L[TAIL])
    f1, f2, f3 = G.FR[i][0], G.FR[i][1], G.FR[i][2]
    m1, p1 = peak_in(p, f1 / 1.5, math.sqrt(f1 * f2))
    m2, p2 = peak_in(p, math.sqrt(f1 * f2), math.sqrt(f2 * f3))
    top, _ = peak_in(p, 60, 12000)
    exact = G.formants(vk, 64, 100)
    rep.check(f"vowel '{G.VOWELS[i]}' (VOWL {vk}): noise in, wet peaks within 6 % of F1 {f1} / F2 {f2} Hz; "
              f"F1 is the strongest peak of the spectrum, L == R",
              abs(m1 / f1 - 1) < 0.06 and abs(m2 / f2 - 1) < 0.06 and abs(top / f1 - 1) < 0.06 and L == R,
              f'{m1:.0f} / {m2:.0f} Hz ({(m1 / f1 - 1) * 100:+.1f} % / {(m2 / f2 - 1) * 100:+.1f} %); F2 peak '
              f'{A.db(math.sqrt(p2 / p1)):.1f} dB under F1 (table {G.DB[i][1]} dB); the knob itself sits at '
              f'{exact[0][0]:.0f} / {exact[1][0]:.0f} Hz')

# ---- against the float plugin model, sample by sample
MB = 5000
MN = MB * 16
rows, ok = [], True
for i, (vk, sk, rk) in enumerate(((0, 64, 67), (32, 64, 67), (64, 64, 67), (95, 64, 67), (127, 64, 67),
                                  (48, 96, 100), (80, 20, 127), (16, 127, 0), (110, 0, 40))):
    x = A.noise(MN, 0.2, 20 + i)
    L, R, _ = A.render(f'vw_m{i}', mems, init, proc, params(VOWL=vk, SHFT=sk, RES=rk), x, MB)
    ref = model(x, vk, sk, rk)
    seg = slice(MN // 2, MN)
    e = A.db(A.rms([a - b for a, b in zip(L[seg], ref[seg])]) / A.rms(ref[seg]))
    ok &= e < -30
    rows.append(f'{vk}/{sk}/{rk}: {e:.1f}')
rep.check('wet against a float model of FormantFilter.h (0.2 FS noise, second half of 1.8 s): error under '
          '-30 dB of the wet, VOWL/SHFT/RES', ok, '; '.join(rows) + ' dB')

# ---- SHFT
base = None
for sk, semis in ((64, 0), (96, 12), (32, -12), (127, 24), (0, -24)):
    L, R, _ = A.render(f'vw_s{sk}', mems, init, proc, params(VOWL=0, SHFT=sk, RES=100), NOISE, BL)
    p = psd(L[TAIL])
    want = 650 * 2 ** (semis / 12)
    m1, _ = peak_in(p, want / 1.4, want * 1.25)
    if base is None:
        base = m1
        continue
    rep.check(f'SHFT {sk} ({semis:+d} semitones): first formant of a moves x{2 ** (semis / 12):g} within 6 %',
              abs(m1 / base / 2 ** (semis / 12) - 1) < 0.06, f'{base:.0f} Hz -> {m1:.0f} Hz (x{m1 / base:.3f})')

# ---- RES
res = {}
for rk in (30, 110):
    L, R, _ = A.render(f'vw_r{rk}', mems, init, proc, params(VOWL=0, RES=rk), NOISE, BL)
    p = psd(L[TAIL])
    res[rk] = (width3(p, 650), max(p[10:]) / (sum(p[10:]) / len(p[10:])))
rep.check('RES 110 (q 4.5) gives a narrower first-formant peak than RES 30 (q 0.28): -3 dB width and '
          'peak-to-mean ratio of the wet spectrum',
          res[110][0] < res[30][0] / 3 and res[110][1] > 3 * res[30][1],
          f'-3 dB width {res[30][0]:.0f} Hz -> {res[110][0]:.0f} Hz (plugin bandwidth 80/q: 284 -> 18 Hz); '
          f'peak/mean {res[30][1]:.0f} -> {res[110][1]:.0f}')

# ---- level match
rows, ok = [], True
for amp in (0.25, 0.5):
    x = A.noise(N, amp, 5)
    for i, vk in enumerate(VK):
        L, R, _ = A.render(f'vw_l{i}', mems, init, proc, params(VOWL=vk), x, BL)
        dlt = A.db(A.rms(L[TAIL])) - A.db(A.rms(x[TAIL]))
        ok &= abs(dlt) < 3 and A.peak(L + R) <= RAIL
        rows.append(f'{G.VOWELS[i]} {dlt:+.2f}')
    rows.append('|')
rep.check('level match: wet RMS within 3 dB of dry RMS, white noise 0.25 FS | 0.5 FS, each vowel, RES default',
          ok, ' '.join(rows) + ' dB')
rows = []
for rk in (0, 127):
    for i, vk in enumerate(VK):
        L, R, _ = A.render(f'vw_lr{i}', mems, init, proc, params(VOWL=vk, RES=rk), NOISE, BL)
        rows.append(f'{G.VOWELS[i]} {A.db(A.rms(L[TAIL])) - A.db(A.rms(NOISE[TAIL])):+.2f}')
    rows.append('|')
ref = model(NOISE, 64, 64, 127)
rep.note('level match at RES 0 | RES 127 (0.25 FS noise, wet - dry RMS)', ' '.join(rows) + ' dB; the float '
         f"plugin model gives i {A.db(A.rms(ref[TAIL])) - A.db(A.rms(NOISE[TAIL])):+.2f} dB at RES 127")
x = A.noise(N, 0.25, 9)
L, R, _ = A.render('vw_loff', mems, init, proc, params(VOWL=0, LEVL=0), x, BL)
raw = A.db(A.rms(L[TAIL])) - A.db(A.rms(x[TAIL]))
rep.check('LEVL OFF: the wet is the unmatched filter sum (well under the dry on noise), bounded',
          raw < -6 and A.peak(L + R) <= RAIL, f'wet - dry {raw:+.1f} dB')

# ---- bounded at RES max
SB = 8000
SN_ = SB * 16
sweep, ph = [], 0.0
for i in range(SN_):
    ph += 2 * math.pi * 40 * (16000 / 40) ** (i / SN_) / FS
    sweep.append(round(A.FS * math.sin(ph)))
worst, clip, rows = 0.0, 0, []
for i, vk in enumerate(VK):
    for tag, x, bl in (('noise', A.noise(SN_, 0.5, 11), SB), ('sweep', sweep, SB)):
        L, R, _ = A.render(f'vw_b{i}{tag}', mems, init, proc, params(VOWL=vk, RES=127), x, bl)
        pk = A.peak(L + R)
        worst = max(worst, pk)
        sat = sum(1 for v in L if abs(v) >= A.FS)
        rows.append(f'{G.VOWELS[i]}/{tag} {pk:.3f} ({100 * sat / len(L):.2f} % at the rail)')
for sk in (0, 127):
    L, R, _ = A.render(f'vw_bs{sk}', mems, init, proc, params(VOWL=64, SHFT=sk, RES=127), sweep, SB)
    worst = max(worst, A.peak(L + R))
rep.check('RES 127, every vowel: 0.5 FS noise and a full-scale 40 Hz..16 kHz sine sweep stay <= 1.0 FS '
          '(also SHFT 0 and 127 on the sweep)', worst <= RAIL, '; '.join(rows))

# ---- no clicks: VOWL swept end to end over 4 s, one knob step every 86 blocks
SIN = A.sine(100, 0.25, N)
dry_step = max(abs(a - b) for a, b in zip(SIN, SIN[1:])) / A.FS
ev = [(k * (BL // 128), params(VOWL=k)) for k in range(128)]
rows, ok = [], True
for lv, bound in ((1, 0.01), (0, 0.002)):
    for rk in (67, 127):
        evl = [(b, [*v[:2], rk, lv, *v[4:]]) for b, v in ev]
        L, R, _ = A.render(f'vw_c{lv}{rk}', mems, init, proc, evl[0][1], SIN, BL, events=evl)
        j = max(abs(a - b) for a, b in zip(L[2000:], L[2001:])) / A.FS
        ok &= j < bound
        rows.append(f'LEVL {lv} RES {rk}: {j:.4f}')
rep.check('VOWL swept 0..127 over 4 s on a 100 Hz 0.25 FS sine: largest sample-to-sample jump of the wet '
          '< 0.01 FS (LEVL ON), < 0.002 FS (LEVL OFF)', ok,
          '; '.join(rows) + f' FS (the dry sine itself steps {dry_step:.4f})')
# an abrupt knob jump (a -> u, SHFT -24 -> +24, RES min -> max in one block)
L, R, _ = A.render('vw_jump', mems, init, proc, params(VOWL=0, SHFT=0, RES=0), SIN, 4000,
                   events=[(2000, params(VOWL=127, SHFT=127, RES=127))])
j = max(abs(a - b) for a, b in zip(L[16000:], L[16001:])) / A.FS
rep.check('all three knobs jumped end to end in one block (100 Hz sine): no step > 0.01 FS in the wet',
          j < 0.01 and A.peak(L + R) <= RAIL, f'largest step {j:.4f} FS')

# ---- stereo independence, DC, silence after a burst
xs = []
for v in A.noise(2000 * 16, 0.25, 4):
    xs += [v, 0]
L, R, _ = A.render('vw_st', mems, init, proc, params(), xs, 2000, stereo_in=True)
rep.check('noise on the left only: the right channel stays exactly silent (independent filters, shared gain)',
          max(abs(v) for v in R) == 0 and A.rms(L[-8000:]) > 0.05, f'L rms {A.rms(L[-8000:]):.3f}, R max 0')
x = A.sine(1000, 0.5, 3000 * 16)
L, R, _ = A.render('vw_dc', mems, init, proc, params(RES=127), x, 3000)
rep.check('1 kHz 0.5 FS sine, RES 127: no DC in the wet (rounded state updates)', abs(A.dc(L[-22050:])) < 2e-5,
          f'dc {A.dc(L[-22050:]):+.2e} FS, rms {A.rms(L[-22050:]):.3f} (dry {A.rms(x[-22050:]):.3f})')
x = A.noise(1000 * 16, 0.5, 6) + [0] * (9000 * 16)
L, R, _ = A.render('vw_tail', mems, init, proc, params(RES=127), x, 10000)
tail = L[-44100:]
rep.check('0.5 FS noise burst then silence, RES 127: 2.6 s later the wet is under -90 dBFS '
          '(no limit cycle lifted by the 32x gain cap)', A.peak(tail) < 10 ** (-90 / 20),
          f'peak of the last second {A.db(A.peak(tail)):.1f} dBFS ({max(abs(v) for v in tail)} LSB)')

# ---- cost over settings
null = A.null_cost()
rows = []
for tag, kw in (('defaults', {}), ('dear', DEAR), ('SHFT 127 RES 0 VOWL 127', dict(VOWL=127, SHFT=127, RES=0))):
    for split in (0, 7):
        _, _, m = A.render('vw_cost', mems, init, proc, params(**kw), A.noise(1200 * 16, 0.5), 1200, split=split)
        rows.append(f"{tag} split {split}: mean {m['mean_per_frame'] - null['mean_per_frame']:.1f} / worst block "
                    f"{m['max_per_frame'] - null['max_per_frame']:.1f}")
rep.note('cost, dsp_host instructions/sample minus null (the plain-name rows are at WIDE 0)', '; '.join(rows))

# ---- stereo: WIDE splits the formants left (down) / right (up) --------------------------------
def fpeak(xx, lo, hi):
    pp = psd(xx)
    return peak_in(pp, lo, hi)


def side_shift(wk, hz, bw, rk):
    """(left, right) cents of a formant at WIDE wk, as gen.py tunes it: d = WIDE min(cap, x kh), kh = 1/(2Q)."""
    kh = min(1.0, max(1 / (2 * G.QMAX), bw / G.knob_res(rk) / hz / 2))
    dd = wk / 128 * min(G.WIDE_CAP, G.WIDE_X * kh)
    return 1200 * math.log2(1 - dd), 1200 * math.log2(1 + dd)


rows = []
okw = True
for rk, tol in ((80, 25), (115, 8)):
    for wk in (0, 64, 127):
        L, R, _ = A.render(f'vw_wide{wk}', mems, init, proc, params(VOWL=0, RES=rk, WIDE=wk), NOISE, BL)
        fl, fr = fpeak(L[-(1 << 16):], 500, 850)[0], fpeak(R[-(1 << 16):], 500, 850)[0]
        wl, wr = side_shift(wk, 650, 80, rk)
        got_l, got_r = 1200 * math.log2(fl / 650), 1200 * math.log2(fr / 650)
        okw &= abs(got_l - wl) <= tol and abs(got_r - wr) <= tol
        rows.append(f'RES {rk} WIDE {wk}: left {fl:.0f} Hz ({got_l:+.0f} c, want {wl:+.0f}), right {fr:.0f} Hz ({got_r:+.0f} c, want {wr:+.0f})')
rep.check('WIDE 0 / 64 / 127: the first formant of "a" (650 Hz) moves down on the left and up on the right by WIDE x '
          'min(a semitone, 2 half-bandwidths): RES 80 (the semitone cap, +-25 c: a broad peak) and RES 115 (the bandwidth '
          'cap, +-8 c)', okw, '; '.join(rows))


def wet(wk, **kw):
    def run(xx, bl):
        return A.render(f'vw_w{wk}', mems, init, proc, params(WIDE=wk, **kw), xx, bl)[:2]
    return run


# a steady sine only decorrelates where the two sides' resonances disagree in phase: on a formant (650 Hz = F1 of "a").
# Between formants both sides pass it almost alike (440 Hz: reported, not gated as wide).
A.stereo_report(rep, 'defaults (WIDE 80, RES 67), MIX 127', wet(80), True, fsine=650.0, corr_max=0.92, fmax=650 * 1.2)
A.stereo_report(rep, 'WIDE 127, RES 67', wet(127), True, fsine=650.0, corr_max=0.92, fmax=650 * 1.2)
A.stereo_report(rep, 'WIDE 127, RES 100 (narrow bands)', wet(127, RES=100), True, fsine=650.0, corr_max=0.92, fmax=650 * 1.2)
A.stereo_report(rep, 'WIDE 127, RES 127 (the narrowest bands: the worst case for a tone between the two sides\' peaks)',
                wet(127, RES=127), True, fsine=650.0, corr_max=0.92, fmax=650 * 1.2)
A.stereo_report(rep, 'WIDE 0', wet(0), False, fsine=650.0, fmax=650 * 1.2)
bl_ = int(3 * FS / 16)
L, R, _ = A.render('vw_w440', mems, init, proc, params(WIDE=80), A.sine(440, 0.5, bl_ * 16), bl_)
rep.note('width, defaults, 440 Hz sine (between formants)',
         f'corr {A.corr(L[13230:], R[13230:]):+.3f}, L+R {A.mono_db(L[13230:], R[13230:]):+.2f} dB re L / '
         f'{A.mono_db(R[13230:], L[13230:]):+.2f} dB re R')
saw = [round(0.4 * A.FS * (2 * ((i * 110 / 44100) % 1) - 1)) for i in range(bl_ * 16)]
rows = []
for vk in VK:
    L, R, _ = A.render('vw_wsaw', mems, init, proc, params(VOWL=vk, WIDE=80), saw, bl_)
    rows.append(f'{G.VOWELS[VK.index(vk)]}: corr {A.corr(L[13230:], R[13230:]):+.2f}, L {A.db(A.rms(L[13230:])):.1f} R {A.db(A.rms(R[13230:])):.1f} dBFS, '
                f'L+R {A.mono_db(L[13230:], R[13230:]):+.1f} dB re L')
rep.note('width, defaults, 110 Hz saw (a pitched source: harmonics fall nearer one side\'s peaks, so the sides differ in level)', '; '.join(rows))
rep.note('cost', f'static {cost[0]} cycles/sample (sample loop only; the per-call coefficient work is outside it); dsp_host mean {cost[2]:.1f}, worst block '
                 f'{cost[1]:.1f} at the dearest settings')
rep.save('/aeria/vowel/test_results.md')
