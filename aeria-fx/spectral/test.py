"""SMEAR (spectral) gate: tools/run.sh python3 /aeria/spectral/test.py   [quick]

Everything runs in dsp_host (the dsp56300 emulator) on the assembled effect; nothing here is hardware.
`quick` skips the 3 x 8 s stability render. Every render is a few seconds (the whole gate is light).
"""
import array
import cmath
import collections
import math
import random
import re
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/spectral')
import aeria as A
import gen as G

QUICK = 'quick' in sys.argv
ONLY = [a[4:].split(',') for a in sys.argv if a.startswith('sec=')]


def sec_on(k):
    return not ONLY or k in ONLY[0]


ASM = '/aeria/spectral/spectral.asm'
KEY = 'AERIA SPECTRAL'            # build / file key (the manifest's key is the old effect's: AERIA SMEAR)
SR = 44100
FS = A.FS
D = G.LATENCY
NAMES = ['SMR', 'FRZ', 'GATE', 'WIDE', 'TILT', 'MIX']
rep = A.Report('SMEAR')
t_frz = SR // 16                   # the block where FRZ goes to 127 in the freeze tests (1 s)

assert Path(ASM).read_text() == G.TEXT, 'spectral.asm is not what gen.py generates: run python3 spectral/gen.py'


def P(**kw):
    """knob list: neutral (SMR 0 FRZ 0 GATE 0 WIDE 100 TILT 64 MIX 127) unless given."""
    p = dict(SMR=0, FRZ=0, GATE=0, WIDE=100, TILT=64, MIX=127)
    p.update(kw)
    return [p[k] for k in NAMES] + [0] * 6


def run(tag, params, xl, xr=None, events=None, split=0, guard=True, extra=(), dirty=None, mm=None, alloc=1):
    """-> (L, R, m): arrays of ints; m: mean / worst instructions a sample, the per-call counts."""
    n = len(xl) // 16 * 16
    blocks = n // 16
    xr = xl if xr is None else xr
    inter = array.array('i', bytes(8 * n))
    inter[0::2] = array.array('i', xl[:n])
    inter[1::2] = array.array('i', xr[:n])
    inf = A.BUILD / f'{tag}.in.raw'
    inf.write_bytes(inter.tobytes())
    out, meter = A.BUILD / f'{tag}.raw', A.BUILD / f'{tag}.meter'
    mm = mm or MEMS
    cmd = [str(A.HOST), '-mem', str(mm[0]), '-memB', str(mm[1]), '-init', f'{INIT:x}', '-proc', f'{PROC:x}',
           '-inst', '1', '-core', '0', '-alloc', str(alloc), '-r7', '2', '-audioidx', '0', '-allocproc', 'end',
           '-frames', '16', '-blocks', str(blocks), '-out', str(out), '-meter', str(meter),
           '-params', ','.join(map(str, params)), '-in', str(inf), '-stereo']
    if events:
        pf = A.BUILD / f'{tag}.csv'
        pf.write_text(''.join(','.join(map(str, [b, *v])) + '\n' for b, v in events))
        cmd += ['-paramfile', str(pf)]
    if split:
        cmd += ['-split', str(split)]
    if guard:
        cmd += ['-guard', str(G.MEM_WORDS)]
        if alloc != 1:
            cmd += ['-guard-shared']
    if dirty is not None:
        cmd += ['-dirty', hex(dirty)]
    cmd += list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    log = r.stdout + r.stderr
    if r.returncode:
        raise RuntimeError(f'{tag}: dsp_host rc={r.returncode}\n{log[-2000:]}')
    mv = re.search(r'(\d+) stray write regions, (\d+) CLOBBERING', log)
    col = [int(l.split()[1]) for l in meter.read_text().splitlines()]
    steady = col[64:] if len(col) > 128 else col
    a = array.array('i')
    a.frombytes(out.read_bytes())
    m = dict(mean=sum(steady) / len(steady) / 16, worst=max(steady) / 16, calls=steady, log=log,
             stray=int(mv[1]) if mv else 0, clobber=int(mv[2]) if mv else 0)
    return a[0::2], a[1::2], m


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x))) / FS


def db(v):
    return 20 * math.log10(max(v, 1e-12))


def errdb(o, x, d=D, a=8192):
    """rms(o[n] - x[n - d]) against rms(x), dB, from sample a."""
    s = e = 0
    for i in range(a, len(o)):
        v = x[i - d]
        s += v * v
        e += (o[i] - v) ** 2
    return 10 * math.log10(max(e, 1e-9) / max(s, 1e-9))


def maxstep(x, a=0, b=None):
    b = len(x) if b is None else b
    return max(abs(x[i] - x[i - 1]) for i in range(a + 1, b)) / FS


def sweep(n, f0, f1, amp):
    k = math.log(f1 / f0)
    T = n / SR
    return [round(amp * FS * math.sin(2 * math.pi * f0 * T / k * (math.exp(k * i / n) - 1))) for i in range(n)]


def chord(n, amp=0.15, fr=(220.0, 277.18, 329.63, 440.0, 554.37)):
    return [round(FS * amp * sum(math.sin(2 * math.pi * f * i / SR + k) for k, f in enumerate(fr))) for i in range(n)]


def fit_sine(x, f, a, b):
    """least-squares sine at f over x[a:b] -> (its amplitude, rms of the rest), FS units."""
    w = 2 * math.pi * f / SR
    S = [math.sin(w * t) for t in range(a, b)]
    C = [math.cos(w * t) for t in range(a, b)]
    y = [v / FS for v in x[a:b]]
    ss, cc, sc = sum(u * u for u in S), sum(u * u for u in C), sum(u * v for u, v in zip(S, C))
    ys, yc = sum(u * v for u, v in zip(y, S)), sum(u * v for u, v in zip(y, C))
    det = ss * cc - sc * sc
    ka, kb = (ys * cc - yc * sc) / det, (yc * ss - ys * sc) / det
    res = sum((v - ka * u - kb * c) ** 2 for v, u, c in zip(y, S, C)) / len(y)
    return math.hypot(ka, kb), math.sqrt(res)


def goertzel(x, f):
    return A.goertzel(x, f)


def fft(x):
    n = len(x)
    x = list(x)
    j = 0
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


def psd(x, nf=4096):
    """mean power spectrum (Hann, 50 % overlap), bins 0..nf/2, in FS^2 a bin."""
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / nf) for i in range(nf)]
    acc = [0.0] * (nf // 2 + 1)
    cnt = 0
    for s in range(0, len(x) - nf + 1, nf // 2):
        X = fft([v * w / FS for v, w in zip(x[s:s + nf], win)])
        for k in range(nf // 2 + 1):
            acc[k] += (X[k].real ** 2 + X[k].imag ** 2)
        cnt += 1
    return [a / cnt / (nf * nf * 0.375) * 2 for a in acc]


def am_lines(x, freqs, blk=32):
    """envelope (rms of blk samples) modulation depth at each frequency, relative to the mean level."""
    env = [math.sqrt(sum(v * v for v in x[i:i + blk]) / blk) for i in range(0, len(x) - blk, blk)]
    mean = sum(env) / len(env)
    fe = SR / blk
    out = []
    for f in freqs:
        c = s = 0.0
        w = 2 * math.pi * f / fe
        for i, v in enumerate(env):
            c += (v - mean) * math.cos(w * i)
            s += (v - mean) * math.sin(w * i)
        out.append(2 * math.hypot(c, s) / len(env) / mean)
    return out


# ======================================================================= 1. build, size, memory
sys.path.insert(0, str(A.SDK / 'tools'))
mod = A.registry._load_one(Path('/aeria/spectral/manifest.py'))
names = [p.name.decode() if p.name else '' for p in mod.params]
defaults = [p.default or 0 for p in mod.params]
rep.check('manifest loads (schema: names <= 4, defaults in count); name SMEAR (key AERIA SMEAR)',
          names[:6] == NAMES and mod.key == 'AERIA SMEAR', ' '.join(f'{n} {d}' for n, d in zip(names[:6], defaults)))
bad, dup = A.label_prefix_check(ASM)
rep.check('no label is a prefix of (or inside) another, no duplicates, no rep', not bad and not dup and A.no_rep(ASM),
          f'{len(G.LABELS)} labels')
MEMS, words, INIT, PROC = A.assemble(ASM, KEY)
rep.check('assembles; disassembler round-trip + mpysu audit clean; code <= 2,048 words with the 4-word entry stub',
          len(words) + 4 <= 2048, f'{len(words)} words (+4 stub = {len(words) + 4})')
src = [l.split(';')[0] for l in G.SRC if not l.startswith(';')]
xacc = sorted({m[0] for l in src for m in re.finditer(r'x:\([^)]*\)[+-]?(?:n\d)?', l)})
rep.check('every buffer access is Y: (the window is one memory in X, Y and P on the unit): X is used only at r0 (the '
          'block), r6 (knobs), r7 (state) and, in init, r4 (the clear of the r7 block, the word X:$213 points at)',
          all(re.match(r'x:\(r[0467]', a) for a in xacc) and not any(re.search(r'\bp:\(', l) for l in src),
          f'{len(xacc)} X forms; state: {G.USED_X} of the 256 words at r7')

n = 32000
xl, xr = A.noise(n, 0.25, 1), A.noise(n, 0.25, 2)
L, R, m = run('sp_mem', P(SMR=100, FRZ=40, GATE=20, TILT=90), xl, xr)
rep.check(f'memory: guard at {G.MEM_WORDS} words from the base ({G.MEM_WORDS:#x}) -- no write outside it or into code',
          m['stray'] == 0 and m['clobber'] == 0, f"{G.MEM_WORDS} words of Y used: base+$0000..${G.MEM_WORDS - 1:04x}; "
          f"at today's base $3C000 that leaves $3E400-$3FFFF ({0x4000 - G.MEM_WORDS} words)")
L5, R5, m5 = run('sp_mem5', P(SMR=100, FRZ=40, GATE=20, TILT=90), xl, xr, alloc=5)
rep.check('same render with the buffer in the shared window (allocator slot 5, Y:$30000, -guard-shared): identical output, guard clean',
          list(L5) == list(L) and list(R5) == list(R) and m5['stray'] == 0, f"stray {m5['stray']}")

# tables: dump after boot
_, _, mt = run('sp_tab', P(), [0] * 1024, extra=['-dumpy', f'4000,{0x4000 + G.WK:x},/aeria/.build/sp_tab.dump'], guard=False)
raw = Path('/aeria/.build/sp_tab.dump').read_bytes()
w24 = [(v - (1 << 24) if v & 0x800000 else v) / 2 ** 23 for v in (struct.unpack_from('<I', raw, 4 * i)[0] & 0xffffff
                                                                 for i in range(G.WK))]
twe = max(max(abs(w24[2 * k] - math.cos(math.pi * k / 512)), abs(w24[2 * k + 1] - math.sin(math.pi * k / 512)))
          for k in range(512))
rp = [(w24[G.RP + 2 * k], w24[G.RP + 2 * k + 1]) for k in range(G.NPH)]
wt = w24[G.WT:G.WT + G.N // 2]
wte = max(abs(wt[n] - G.win(n)) for n in range(G.N // 2))
cola = max(abs(wt[n] ** 2 + wt[G.N // 2 - 1 - n] ** 2 - 1) for n in range(G.N // 2))
mag = [math.hypot(*p) for p in rp]
quad = collections.Counter(int((math.atan2(p[1], p[0]) + math.pi) / (math.pi / 2)) % 4 for p in rp)
rep.check('twiddle table (filled at boot by angle addition from 9 constants): max error < 2e-6',
          twe < 2e-6, f'{twe:.2e} (cos and sin of pi k / 512, k = 0..511)')
rep.check('window table (Vorbis, filled at boot from the twiddles and a Taylor series): max error < 2e-6, and '
          'power-complementary (w(n)^2 + w(n + 512)^2 = 1 within 4e-6: the overlap-add is constant)',
          wte < 2e-6 and cola < 4e-6, f'error {wte:.2e}, |w(n)^2 + w(n + 512)^2 - 1| <= {cola:.2e} (512 entries, n = 0..511)')
rep.check(f'random phasor table: {G.NPH} entries of magnitude {G.PHS:.6f} (1 / sqrt 2: the wash is sqrt 2 x the magnitude '
          'it replaces), all four quadrants, mean vector small',
          0.9999 * G.PHS < min(mag) and max(mag) < 1.0001 * G.PHS and min(quad.values()) > G.NPH // 6
          and math.hypot(sum(p[0] for p in rp), sum(p[1] for p in rp)) / G.NPH < 0.18 * G.PHS,
          f'|u| {min(mag):.6f}..{max(mag):.6f}, quadrants {[quad[i] for i in range(4)]}, '
          f'|mean| {math.hypot(sum(p[0] for p in rp), sum(p[1] for p in rp)) / G.NPH / G.PHS:.3f} of it')

# ======================================================================= 2. pass-through
if sec_on('2'):
    n = 4 * SR // 16 * 16
    xl, xr = A.noise(n, 0.25, 11), A.noise(n, 0.25, 12)
    L, R, m = run('sp_pt', P(), xl, xr)
    best = min(range(D - 3, D + 4), key=lambda d: errdb(L, xl, d, 40000))
    el, er = errdb(L, xl), errdb(R, xr)
    rep.check(f'pass-through (SMR 0, FRZ 0, GATE 0, TILT 64, MIX 127), white noise 0.25 FS, different on the two sides: '
              f'output = input delayed by {D}, error under -60 dB', el < -60 and er < -60 and best == D,
              f'L {el:.1f} dB, R {er:.1f} dB re the input (best lag {best}); floor {db(rms(xl)) + el:.1f} dBFS: the 24-bit '
              f'rounding of the bins leaving at 1/8 scale and of the two transforms')
    n = 4 * SR // 16 * 16
    sl, sr_ = sweep(n, 40, 16000, 0.5), sweep(n, 16000, 40, 0.5)
    L, R, m = run('sp_sw', P(), sl, sr_)
    el, er = errdb(L, sl), errdb(R, sr_)
    rep.check('pass-through, 0.5 FS log sine sweeps (left 40 Hz -> 16 kHz, right 16 kHz -> 40 Hz, 4 s): error under -60 dB',
              el < -60 and er < -60, f'L {el:.1f} dB, R {er:.1f} dB')
    ql = [v // 100 for v in xl]
    L, R, m = run('sp_q', P(), ql, [v // 100 for v in xr])
    rep.note('pass-through, the same noise 40 dB down (-52 dBFS rms)', f'L {errdb(L, ql):.1f} dB re the input (floor {db(rms(ql)) + errdb(L, ql):.1f} dBFS)')
    zero = [0] * n
    L, R, m = run('sp_xl', P(), xl, zero)
    ctl = db(rms(R[8192:]) / rms(L[8192:]))
    L2, R2, m = run('sp_xr', P(), zero, xr)
    ctr = db(rms(L2[8192:]) / rms(R2[8192:]))
    rep.check('stereo through one complex FFT: noise on one side only, the other side under -70 dB of it',
              ctl < -70 and ctr < -70, f'L -> R {ctl:.1f} dB, R -> L {ctr:.1f} dB')
    L, R, m = run('sp_dcn', P(), [FS // 4] * 16000, [(FS // 4) * (1 if i & 1 else -1) for i in range(16000)])
    rep.check('DC (left) and Nyquist (right) pass (bins 0 and N/2 take the live gain only)',
              abs(L[-1] / (FS // 4) - 1) < 1e-3 and abs(abs(R[-1]) / (FS // 4) - 1) < 1e-3 and R[-1] * R[-2] < 0,
              f'{L[-1] / (FS // 4):.5f}, {abs(R[-1]) / (FS // 4):.5f} of the input')
    # the sine window (analysis x synthesis = sin^2, 50 % overlap) is constant overlap-add: a steady sine comes back at
    # its level with no ripple at the hop rate. Gain of each 64-sample block (least squares against the delayed
    # input), over 2 s; grouped by the block's position in the hop.
    n = (3 * SR + 64) // 16 * 16
    for f, amp in ((997.0, 0.5), (151.3, 0.7)):
        x = A.sine(f, amp, n)
        L, R, m = run('sp_cola', P(), x)
        gs = []
        for b0 in range(SR, n - 64, 64):
            num = sum(L[i] * x[i - D] for i in range(b0, b0 + 64))
            den = sum(x[i - D] ** 2 for i in range(b0, b0 + 64))
            gs.append((b0 % G.HOP // 64, num / den))
        by = [[g for k, g in gs if k == j] for j in range(G.HOP // 64)]
        pos = [sum(v) / len(v) for v in by]
        rip = db(max(g for _, g in gs)) - db(min(g for _, g in gs))
        lvl = db(sum(g for _, g in gs) / len(gs))
        rep.check(f'COLA: a steady {f:g} Hz sine ({amp} FS) at MIX 127, SMR 0, FRZ 0 comes back at its level with no ripple at '
                  f'the hop rate (gain against the delayed input of each 64-sample block over 2 s: level within 0.01 dB, '
                  f'spread under 0.005 dB; Hann x Hann at this 50 % overlap would swing 6 dB)', abs(lvl) < 0.01 and rip < 0.005,
                  f'level {lvl:+.5f} dB, block gains spread {rip:.5f} dB; mean by position in the hop '
                  + ' '.join(f'{db(v):+.5f}' for v in pos) + ' dB')

# ======================================================================= 3. freeze
if sec_on('3'):
    T = 7.5
    n = int(T * SR) // 16 * 16
    t_off, t_rel = int(1.5 * SR), int(6.5 * SR) // 16
    x = chord(n)
    xs = [v if i < t_off else 0 for i, v in enumerate(x)]
    ev = [(0, P()), (t_frz, P(FRZ=127)), (t_rel, P())]
    L, R, m = run('sp_fz', P(), xs, events=ev)
    lv = [db(rms(L[k * SR:(k + 1) * SR])) for k in range(2, 6)]
    live = db(rms(L[SR // 2:SR]))
    hop = SR / G.HOP
    near = [round(hop * r) for r in (0.62, 0.66, 0.7, 0.74, 0.78, 0.82, 1.18, 1.22, 1.26, 1.3, 1.34, 1.38)]
    am = am_lines(L[2 * SR:6 * SR], [hop] + near)
    am2 = am_lines(L[2 * SR:6 * SR], [2 * hop])
    nb = sorted(am[1:])[len(near) // 2]
    rep.check('freeze: a chord (5 sines, -12.5 dBFS), FRZ 127 at 1 s, input silenced at 1.5 s: the output goes on for 5 s '
              'at a steady level (1 s windows within 1 dB of each other, within 2 dB of the live level)',
              max(lv) - min(lv) < 1.0 and abs(sum(lv) / len(lv) - live) < 2.0,
              f'live {live:.2f} dBFS; frozen, each second from 2 s: ' + ' '.join(f'{v:.2f}' for v in lv)
              + f' (spread {max(lv) - min(lv):.2f} dB, first to last {lv[-1] - lv[0]:+.2f} dB)')
    rep.check(f'freeze: no pulsing at the hop rate -- the envelope (rms of 32 samples, 4 s) has no line at {hop:.1f} Hz above '
              f'its own random modulation at {near[0]}..{near[-1]} Hz (+6 dB allowed)', am[0] < 2 * nb,
              f'modulation depth at the hop rate {db(am[0]):.1f} dB (at twice the hop rate {db(am2[0]):.1f} dB); '
              f'median of 12 nearby frequencies {db(nb):.1f} dB, largest {db(max(am[1:])):.1f} dB')
    a0 = t_rel * 16
    st_frozen = maxstep(L, 2 * SR, a0)
    st_rel = maxstep(L, a0, n)
    rep.check('freeze released into silence (FRZ 127 -> 0 at 6.5 s, SMR 0): no step larger than the frozen wash makes itself, '
              'and the output ends at exactly 0', st_rel <= st_frozen * 1.05 and max(abs(v) for v in L[-2000:]) == 0,
              f'largest step {st_rel:.4f} FS after the release, {st_frozen:.4f} FS in the 4.5 s frozen before it; '
              f'-60 dB reached {next((i for i in range(a0, n - 64) if max(abs(v) for v in L[i:i + 64]) < 1e-3 * FS), n) - a0} samples after the knob')
    ev = [(0, P()), (t_frz, P(FRZ=127)), (3 * SR // 16, P())]
    n4 = 5 * SR // 16 * 16
    L, R, m = run('sp_fz2', P(), x[:n4], events=ev)
    st_a, st_b, st_live = maxstep(L, SR * 3 // 2, 3 * SR), maxstep(L, 3 * SR, 4 * SR), maxstep(L, 4 * SR, n4)
    e_back = errdb(L[:n4], x, D, 4 * SR)
    rep.check('freeze released with the input still playing (FRZ 127 at 1 s, 0 at 3 s): no step larger than the frozen '
              'wash or the live chord make, and the pass-through is back', st_b <= max(st_a, st_live) * 1.05 and e_back < -60,
              f'largest step {st_b:.4f} FS in the second after the release (frozen {st_a:.4f}, live {st_live:.4f}); '
              f'error from 4 s {e_back:.1f} dB')
    for name, sig in (('white noise 0.25 FS', A.noise(4 * SR, 0.25, 5)), ('a sine on bin 20 (861 Hz) 0.5 FS', A.sine(20 * SR / 1024, 0.5, 4 * SR)),
                      ('a 1 kHz sine 0.5 FS', A.sine(1000.0, 0.5, 4 * SR))):
        Lf, Rf, _ = run('sp_fzl', P(), sig, events=[(0, P()), (t_frz, P(FRZ=127))])
        rep.note(f'freeze level, {name}', f'live {db(rms(Lf[SR // 2:SR])):.2f} dBFS, frozen {db(rms(Lf[2 * SR:4 * SR])):.2f} dBFS '
                 f'({db(rms(Lf[2 * SR:4 * SR])) - db(rms(Lf[SR // 2:SR])):+.2f} dB), peak {A.peak(Lf[2 * SR:]):.3f} FS')

# ======================================================================= 4. smear
if sec_on('4'):
    rows, ok = [], True
    for v, secs in ((40, 4), (60, 5), (80, 6)):
        n = secs * SR // 16 * 16
        x = [0] * n
        for i in range(2000, SR, 4410):
            x[i] = FS // 2
        L, R, m = run('sp_sm', P(SMR=v), x)
        pts = []
        for k in range(10, secs * 8):
            r_ = rms(L[k * SR // 8:(k + 1) * SR // 8])
            if r_ > 10 ** (-85 / 20):
                pts.append((k / 8, db(r_)))
        mx = sum(t for t, _ in pts) / len(pts)
        my = sum(y for _, y in pts) / len(pts)
        slope = sum((t - mx) * (y - my) for t, y in pts) / sum((t - mx) ** 2 for t, _ in pts)
        t60, want = -60 / slope, G.smr_t60(v)
        ok &= abs(t60 / want - 1) < 0.1
        rows.append(f'SMR {v}: {t60:.2f} s (law {want:.2f} s, {pts[0][1]:.1f} dBFS at 1.25 s)')
    rep.check('smear: ten clicks (0.5 FS, 10 a second) then silence; the wash rings on and decays at the knob\'s time '
              '(-60 dB time from the slope of its level from 1.25 s down to -85 dBFS, within 10 % of the law)', ok, '; '.join(rows))
    # the long times: the held magnitudes H themselves (dumped after the same input rendered to two lengths, a whole
    # number of hops apart) decay by (1 - e) a hop; the sum over the bins gives e, so minutes are measured in seconds
    hd = '/aeria/.build/sp_hd.dump'
    n1 = (3 * SR // 2) // G.HOP * G.HOP
    hops = 2 * SR // G.HOP
    n2 = n1 + hops * G.HOP
    xb = A.noise(n2, 0.5, 51)
    xb = [v if i < SR // 2 else 0 for i, v in enumerate(xb)]
    rows, ok = [], True

    def hsum(v, nn):
        run('sp_hd', P(SMR=v), xb[:nn], extra=['-dumpy', f'{0x4000 + G.HH:x},{0x4000 + G.END:x},{hd}'])
        raw_ = Path(hd).read_bytes()
        return [struct.unpack_from('<I', raw_, 4 * i)[0] & 0xffffff for i in range(1024)]

    for v in (80, 100, 110, 120, 127):
        h1, h2 = hsum(v, n1), hsum(v, n2)
        sel = [i for i in range(1024) if h1[i] > 4096]
        ratio = sum(h2[i] for i in sel) / sum(h1[i] for i in sel)
        t60, want = -3 * G.HOP * hops / SR / math.log10(ratio), G.smr_t60(v)
        ok &= abs(t60 / want - 1) < 0.03 and len(sel) > 500
        rows.append(f'SMR {v}: {t60:.1f} s (law {want:.1f} s, {len(sel)} bins, H x {ratio:.6f} in {hops} hops)')
    rep.check(f'smear, long times: the held magnitudes after 0.5 s of noise (0.5 FS), dumped {hops} hops apart in the silence, '
              f'decay at the law within 3 % (log 1 - e = log2(200) v / 127 up to {G.SMR_KNEE}, then steeper to 2^-{G.Y_TOP:g} '
              f'at 127)', ok, '; '.join(rows))
    n = 3 * SR // 16 * 16
    x = [0] * n
    for i in range(2000, SR, 4410):
        x[i] = FS // 2
    L, R, m = run('sp_sm0', P(SMR=0), x)
    rep.check(f'SMR 0: no sustain (the clicks pass, nothing rings: silence {D:,} samples after the last one\'s frame ends)',
              max(abs(v) for v in L[SR + 4000:]) == 0, f'peak after 1.09 s: {max(abs(v) for v in L[SR + 4000:])} LSB')
    n = 5 * SR // 16 * 16
    xn = A.noise(n, 0.1, 41)
    rows = []
    for v in (40, 80, 127):
        L, R, m = run('sp_smn', P(SMR=v), xn)
        rows.append(f'SMR {v}: {db(rms(L[4 * SR:])) - db(rms(xn[4 * SR:])):+.1f} dB')
    rep.note('smear on steady white noise (0.1 FS): the hold keeps every bin\'s recent peak, so the wet is louder than the input',
             ', '.join(rows) + ' after 4 s (a steady sine is not: see the next line)')
    # a steady tone: faded in over 0.3 s (an abrupt start splashes over every bin, and at SMR 80 the hold keeps
    # that for its 5.6 s), measured from 2 s; the hold must not turn the estimate's ripple with the phase, or
    # the window's leakage (which wobbles with the phase of a steady tone), into a wash
    n = 7 * SR // 2 // 16 * 16
    rows, ok = [], True
    for f, lim in ((440.0, -60), (1000.0, -60), (3000.0, -60), (150.0, None)):
        x = [round(v * min(1.0, i / (0.3 * SR)) ** 2) for i, v in enumerate(A.sine(f, 0.25, n))]
        L, R, m = run('sp_sm1', P(SMR=80), x)
        tone, resid = fit_sine(L, f, 2 * SR, n)
        wash = db(resid / (0.25 / math.sqrt(2)))
        if lim is not None:
            ok &= abs(db(tone / 0.25)) < 0.1 and wash < lim
        rows.append(f'{f:g} Hz: tone {db(tone / 0.25):+.2f} dB, everything else {wash:.1f} dB re the tone' + ('' if lim else ' (info)'))
    rep.check('SMR 80 on a steady sine (0.25 FS, faded in): the tone passes within 0.1 dB and the wash it adds stays under -60 dB '
              'at 440 Hz, 1 kHz and 3 kHz (three-segment magnitude estimate within 0.5 %, Vorbis window; the 4x Hann SMEAR measured '
              '-72 / -77 / -56 dB here, and -50 dB at 150 Hz, where the tone\'s mirror image beats in its low bins)', ok, '; '.join(rows))

# ======================================================================= 5. gate
if sec_on('5'):
    n = 4 * SR // 16 * 16
    rnd = random.Random(3)
    tone = A.sine(1000.0, 0.25, n)
    nz = [round(FS * 0.001 * math.sqrt(3) * (2 * rnd.random() - 1)) for _ in range(n)]
    x = [a + b for a, b in zip(tone, nz)]
    res = {}
    for gv in (0, 1, 64, 127):
        L, R, m = run('sp_gt', P(GATE=gv), x)
        seg = L[SR:SR + 3 * 32768 // 1]
        p = psd(seg[:65536], 4096)
        hz = SR / 4096
        far = sum(p[k] for k in range(int(3000 / hz), int(9000 / hz)))
        k0 = round(1000 / hz)
        allb = sum(p[k] for k in range(2, 2048) if abs(k - k0) > 8)
        res[gv] = (10 * math.log10(max(far, 1e-30)), 10 * math.log10(max(allb, 1e-30)), db(goertzel(seg, 1000.0) / 0.25))
    rep.check('gate: a 1 kHz sine (-12 dBFS) + white noise (-60 dBFS rms); with GATE up the noise floor falls by more than '
              '30 dB (3-9 kHz band) and the tone stays within 0.5 dB (1.5 dB at GATE 127: its -9 dB side bin, 1.2 bins off at -21 dBFS, goes)',
              res[0][0] - res[64][0] > 30 and res[0][0] - res[127][0] > 30 and all(abs(res[g][2]) < 0.5 for g in (0, 1, 64))
              and abs(res[127][2]) < 1.5,
              '; '.join(f'GATE {g} ({"off" if g == 0 else "%.0f dBFS" % G.gate_db(g)}): 3-9 kHz noise {res[g][0]:.1f} dBFS '
                        f'({res[g][0] - res[0][0]:+.1f} dB), all but 1 kHz +-86 Hz {res[g][1]:.1f} dBFS ({res[g][1] - res[0][1]:+.1f} dB), '
                        f'tone {res[g][2]:+.2f} dB' for g in res))
    rows, ok = [], True
    for lvl in (-30, -50):
        amp = 10 ** (lvl / 20)
        s = A.sine(20 * SR / 1024, amp, 2 * SR // 16 * 16)
        cand = [gv for gv in range(1, 128) if lvl - 8 <= G.gate_db(gv) <= lvl + 8]
        gone = lambda gv: rms(run('sp_gth', P(GATE=gv), s)[0][SR:]) < 0.05 * amp
        first = None
        if gone(cand[-1]) and not gone(cand[0]):          # bisect: the gate removes the sine from some knob on
            lo, hi = cand[0], cand[-1]
            while hi - lo > 1:
                mid = (lo + hi) // 2
                lo, hi = (lo, mid) if gone(mid) else (mid, hi)
            first = hi
        ok &= first is not None and abs(G.gate_db(first) - lvl) < 3
        rows.append(f'a {lvl} dBFS sine (bin 20) is removed from GATE {first} = {G.gate_db(first):.1f} dBFS')
    rep.check('gate threshold law (log, 0.476 dB a step, 127 = -20 dBFS peak of a sine): the knob that first removes a sine '
              'is within 3 dB of its level', ok, '; '.join(rows))

# ======================================================================= 6. wide
if sec_on('6'):
    n = 5 * SR // 16 * 16
    x = A.noise(n, 0.25, 21)
    rows, ok, cs = [], True, {}
    for wv in (0, 32, 64, 96, 127):
        L, R, m = run('sp_wd', P(WIDE=wv), x, events=[(0, P(WIDE=wv)), (t_frz, P(WIDE=wv, FRZ=127))])
        a, b = L[2 * SR:], R[2 * SR:]
        c = A.corr(a, b)
        ms = A.mono_db(a, b)
        cs[wv] = c
        rows.append(f'WIDE {wv}: corr {c:+.3f}, L+R {ms:+.2f} dB re L' + (f', max |L-R| {max(abs(u - v) for u, v in zip(a, b))} LSB' if wv == 0 else ''))
        if wv == 0:
            ok &= c > 0.9999 and abs(ms - 6.02) < 0.05
        if wv == 127:
            ok &= abs(c) < 0.1 and abs(ms - 3.01) < 0.6
    ok &= cs[0] > cs[32] > cs[64] > cs[96] > cs[127]
    rep.check('WIDE on a frozen wash (mono noise in): 0 = the same phases on both sides (correlation 1, mono sum +6 dB), '
              '127 = independent (correlation about 0, mono sum +3 dB), monotonic between', ok, '; '.join(rows))
    L, R, m = run('sp_wd2', P(WIDE=127), x)
    dlr = rms([u - v for u, v in zip(L, R)])
    rep.check('WIDE does not touch the live path: mono in, WIDE 127, no wash (SMR 0, FRZ 0) -> L = R to the rounding floor '
              '(the two sides are the real and the imaginary part: not the same arithmetic)',
              db(dlr) < -90, f'L - R {db(dlr):.1f} dBFS rms, max {max(abs(u - v) for u, v in zip(L, R))} LSB')

# ======================================================================= 7. tilt
if sec_on('7'):
    bins = (3, 6, 12, 24, 48, 96, 192, 384)
    fr = [b * SR / 1024 for b in bins]
    n = 3 * SR // 16 * 16
    x = [round(FS * 0.03 * sum(math.sin(2 * math.pi * f * i / SR + 1.3 * k) for k, f in enumerate(fr))) for i in range(n)]
    gains = {}
    for tv in (0, 32, 64, 96, 127):
        L, R, m = run('sp_tl', P(TILT=tv), x)
        seg = L[SR:SR + 65536]
        gains[tv] = [db(goertzel(seg, f) / 0.03) for f in fr]
    rows, ok = [], True
    for tv in (0, 127):
        g = gains[tv]
        sl = (g[6] - g[2]) / 4                      # 517 Hz .. 8.27 kHz, 4 octaves
        sl_lo = (g[5] - g[1]) / 4                   # 258 Hz .. 4.13 kHz
        pred = [G.tilt_db(tv, f) for f in fr]
        ok &= abs(abs(sl_lo) - 6) < 1.0 and max(abs(a - b) for a, b in zip(g, pred)) < 0.5 and abs(g[4] - G.tilt_db(tv, fr[4])) < 0.3
        rows.append(f'TILT {tv}: ' + ' '.join(f'{f:.0f} Hz {v:+.1f}' for f, v in zip(fr, g)) + f' dB -> {sl_lo:+.2f} dB/oct over '
                    f'258 Hz-4.1 kHz, {sl:+.2f} dB/oct over 517 Hz-8.3 kHz, {(g[3] - g[1]) / 2:+.2f} dB/oct over 258 Hz-1 kHz')
    flat = max(abs(v) for v in gains[64])
    rep.check('tilt (a first-order filter on the wet: one zero / one pole down to 100 Hz, unity at 1 kHz): about +-6 dB an '
              'octave at the ends of the knob, within 0.5 dB of the filter\'s own curve at 8 tones; 64 is flat',
              ok and flat < 0.05, '; '.join(rows) + f'; TILT 64: within {flat:.3f} dB; TILT 32 / 96 at 258 Hz and 4.1 kHz: '
              f'{gains[32][1]:+.1f} / {gains[32][5]:+.1f} dB and {gains[96][1]:+.1f} / {gains[96][5]:+.1f} dB')

# ======================================================================= standard: MIX 0, dirty state, automation
dear = P(SMR=127, FRZ=64, GATE=100, WIDE=127, TILT=127, MIX=64)
if sec_on('standard'):
    n = 24000
    ramp = [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]
    dry = list(dear)
    dry[5] = 0
    L, R, m = run('sp_dry', dry, ramp, [-v for v in ramp])
    rep.check('MIX 0 is exact dry from the first sample (full-range ramps, every other knob at its dearest)',
              list(L) == ramp and list(R) == [-v for v in ramp], f'max |err| {max(abs(a - b) for a, b in zip(L, ramp))} LSB')
    worst = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = A.with_fill(MEMS, fill, f'spfill{fill:06x}')
        for kn in (defaults, dear):
            Lf, Rf, _ = run('sp_dirty', kn, [0] * (400 * 16), mm=fm, dirty=0x1234 + fill, guard=False)
            worst = max(worst, max(abs(v) for v in list(Lf) + list(Rf)))
    rep.check('dirty state (garbage r7 block and garbage Y: the boot clears and refills everything) on silence -> silence '
              'throughout', worst == 0, f'worst {worst} LSB')
    rnd = random.Random(7)
    BL = 6000
    ev = [(b, [rnd.randrange(128) for _ in range(6)] + [0] * 6) for b in range(0, BL, 3)]
    xa = A.noise(BL * 16, 0.7, 3)
    pk = 0
    for sp in (0, 7):
        La, Ra, ma = run(f'sp_auto{sp}', defaults, xa, A.noise(BL * 16, 0.7, 4), events=ev, split=sp)
        pk = max(pk, A.peak(La), A.peak(Ra))
        if ma['stray'] or ma['clobber']:
            rep.check(f'automation split {sp}: guard', False, f"stray {ma['stray']}")
    rep.check('every knob random every 3 blocks on 0.7 FS noise, whole blocks and blocks split 7 + 9, guard on: bounded, '
              'no stray write', pk <= 1.0 + 1e-6, f'peak {pk:.3f} FS')
    n = 2 * SR // 16 * 16
    xl, xr = A.noise(n, 0.25, 11), A.noise(n, 0.25, 12)
    L, R, m = run('sp_split', P(), xl, xr, split=7)
    rep.check('blocks split 7 + 9 (the stock dispatcher\'s trig split; AERIA\'s engine always calls 16): pass-through still '
              'under -60 dB', errdb(L, xl) < -60 and errdb(R, xr) < -60, f'L {errdb(L, xl):.1f} dB, R {errdb(R, xr):.1f} dB')

# ======================================================================= 8. stability, knob moves
if sec_on('8'):
    n = 3 * SR // 16 * 16
    s440 = A.sine(440.0, 0.25, n)
    base = dict(SMR=80, FRZ=0, GATE=0, WIDE=100, TILT=64, MIX=127)
    ref = {}
    rows, ok = [], True
    sine_step = 2 * math.sin(math.pi * 440 / SR) * 0.25
    for k in NAMES:
        st = {}
        for end in (0, 127):
            Lr, Rr, _ = run('sp_kref', P(**{**base, k: end}), s440)
            st[end] = max(maxstep(Lr, SR // 2, n), maxstep(Rr, SR // 2, n))
        evs = []
        nb = n // 16
        for b in range(nb):
            t = b / nb
            v = round(127 * (1 - abs(2 * t - 1)))               # 0 -> 127 -> 0 over 3 s
            evs.append((b, P(**{**base, k: v})))
        Ls, Rs, _ = run('sp_ksw', evs[0][1], s440, events=evs)
        sw = max(maxstep(Ls, SR // 2, n), maxstep(Rs, SR // 2, n))
        evj = [(b, P(**{**base, k: 127 if (b // 1378) & 1 else 0})) for b in range(0, nb, 1378)]    # a jump every 0.5 s
        Lj, Rj, _ = run('sp_kjp', evj[0][1], s440, events=evj)
        jp = max(maxstep(Lj, SR // 2, n), maxstep(Rj, SR // 2, n))
        lim = max(st.values()) * 1.15 + 0.2 * sine_step
        good = sw <= lim and jp <= lim
        ok &= good
        rows.append(f'{k}: sweep {sw:.4f}, jumps {jp:.4f} (static at 0: {st[0]:.4f}, at 127: {st[127]:.4f})' + ('' if good else ' <<'))
    rep.check('knob moves on a 440 Hz sine (0.25 FS; its own largest step is %.4f FS), SMR 80: each knob swept 0 -> 127 -> 0 '
              'over 3 s, then jumped 0 <-> 127 every 0.5 s: no step beyond 1.15 x the larger of the two static settings (3 s each) '
              '+ a fifth of the sine\'s' % sine_step, ok, '; '.join(rows) + ' FS')

    if not QUICK:
        seg = 8 * SR
        n = 3 * seg // 16 * 16
        rnd = random.Random(99)
        big = array.array('i', bytes(4 * n))
        for i in range(seg):
            big[i] = round(0.95 * FS * (2 * rnd.random() - 1))
        half = SR // 220
        for i in range(2 * seg, n):
            big[i] = FS if (i // half) & 1 else -FS
        params = P(SMR=127, FRZ=0, GATE=20, WIDE=127, TILT=64, MIX=127)
        L, R, m = run('sp_long', params, big, extra=['-dumpy', f'{0x4000 + G.HH:x},{0x4000 + G.END:x},/aeria/.build/sp_h.dump'])
        lv = [db(rms(L[k * SR:(k + 1) * SR])) for k in range(24)]
        hraw = Path('/aeria/.build/sp_h.dump').read_bytes()
        hh = [struct.unpack_from('<I', hraw, 4 * i)[0] & 0xffffff for i in range(1024)]
        sil = lv[10:16]
        dc = [abs(sum(L[k * SR:(k + 1) * SR]) / SR / FS) for k in (4, 12, 20)]
        rep.check('3 x 8 s at SMR 127, WIDE 127, MIX 127: 0.95 FS noise, silence, a full-scale 220 Hz square: the level follows '
                  'the input (no growth within a section), the silence holds the wash (T60 %.0f min at 127: within 1 dB, no '
                  'growth), H stays a magnitude (0 <= H < 1/2), guard clean' % (G.smr_t60(127) / 60),
                  max(lv[2:8]) - min(lv[2:8]) < 1.0 and max(sil) - min(sil) < 1.0 and sum(sil[3:]) <= sum(sil[:3]) + 0.6
                  and max(lv[18:]) - min(lv[18:]) < 1.0 and max(hh) < 0x400000 and m['stray'] == 0,
                  f'noise section {min(lv[2:8]):.2f}..{max(lv[2:8]):.2f} dBFS (input {db(0.95 / math.sqrt(3)):.2f}: the held wash '
                  f'adds to it and the sum clips), silence ' + ' '.join(f'{v:.2f}' for v in sil) + ' dBFS, square section '
                  f'{min(lv[18:]):.2f}..{max(lv[18:]):.2f} dBFS (input 0.00); largest H {max(hh) / 2 ** 23:.4f}; '
                  f'samples at the rail: noise {sum(1 for v in L[2 * SR:8 * SR] if abs(v) >= FS) / (6 * SR) * 100:.2f} %, square '
                  f'{sum(1 for v in L[18 * SR:] if abs(v) >= FS) / (6 * SR) * 100:.2f} %; dc {max(dc):.5f} FS')
        del L, R
        n = 3 * 4 * SR // 16 * 16
        seg = n // 3
        small = array.array('i', bytes(4 * n))
        for i in range(seg):
            small[i] = big[i]
        for i in range(2 * seg, n):
            small[i] = FS if (i // half) & 1 else -FS
        rnd = random.Random(8)
        ev = [(b, [rnd.randrange(128) for _ in range(6)] + [0] * 6) for b in range(0, n // 16, 5)]
        L, R, m = run('sp_long2', params, small, events=ev)
        lv = [db(rms(L[k * SR // 2:(k + 1) * SR // 2])) for k in range(24)]
        rep.check('3 x 4 s of the same three sections with every knob random every 5 blocks: bounded, guard clean, no section '
                  'louder than +12 dB over its input', max(lv[:8]) < db(0.95 / math.sqrt(3)) + 12 and max(lv[16:]) <= 0.01 and m['stray'] == 0,
                  f'loudest half second: noise {max(lv[:8]):.2f}, silence {max(lv[9:16]):.2f}, square {max(lv[16:]):.2f} dBFS')

# ======================================================================= 9. cost
if sec_on('9'):
    n = 3 * SR // 16 * 16
    xn = A.noise(n, 0.5, 31)
    COST_MEAN = 600                # half the 4x-overlap SMEAR's 1,150 (it measured 1,092 mean, 1,165 worst call)
    rows, ok, worst_all, mean_all = [], True, 0, 0
    for name, pr, sig in (('defaults (SMR 80, WIDE 100, MIX 127), 0.5 FS noise', defaults, xn),
                          ('dearest (SMR 127 FRZ 64 GATE 100 WIDE 127 TILT 127 MIX 64), noise', dear, xn),
                          ('frozen (FRZ 127), noise', P(FRZ=127, SMR=80), xn),
                          ('TILT 0 (the pole), GATE 127, noise', P(SMR=80, TILT=0, GATE=127), xn),
                          ('defaults, silence', defaults, [0] * n),
                          ('MIX 0 (dry: the transform still runs)', P(SMR=80, MIX=0), xn)):
        L, R, m = run('sp_cost', pr, sig)
        ok &= m['mean'] <= COST_MEAN and m['worst'] <= 1.15 * m['mean']
        worst_all, mean_all = max(worst_all, m['worst']), max(mean_all, m['mean'])
        rows.append(f"{name}: mean {m['mean']:.1f}, worst call {m['worst']:.1f} (+{(m['worst'] / m['mean'] - 1) * 100:.1f} %), "
                    f"least {min(m['calls']) / 16:.1f}")
        if pr is defaults and sig is xn:
            calls = m['calls']
    rep.check(f'cost, dsp_host instructions a sample over 3 s ({len(calls):,} calls of 16 frames after the first 64; a null effect '
              f'costs 0.06): mean <= {COST_MEAN:,} and the worst call within 15 % of the mean', ok, '; '.join(rows))
    hist = collections.Counter(c // 16 // 20 * 20 for c in calls)
    cph = G.HOP // 16
    slots = [(min(calls[k::cph]) / 16, max(calls[k::cph]) / 16) for k in range(cph)]
    rep.note('cost, distribution of the calls (defaults, noise): instructions a sample -> share of calls',
             ', '.join(f'{k}-{k + 19}: {v * 100 / len(calls):.1f} %' for k, v in sorted(hist.items())))
    rep.note(f'cost by position in the hop ({cph} calls, the pattern repeats exactly): instructions a sample',
             ' '.join(f'{a:.0f}' if b - a < 1 else f'{a:.0f}-{b:.0f}' for a, b in slots))
    sch = G.schedule()
    rep.note('the schedule', f'{len(G.UNITS)} units a hop + the fixed start ({G.W_MAND} instructions: knobs, set-up and the first '
             f'copy-in unit); budget {G.BF} instructions a frame; weights: copy-in {G.W_CIN}, last half of the previous frame {G.W_LQ}, '
             f'forward unit {G.W_FWD}/{G.W_FWA}, radix-4 {G.W_FR4}/{G.W_IR4}, {G.BPAIR} bin pairs {G.W_BOV + G.BPAIR * G.W_PAIR}, '
             f'inverse {G.W_INV}/{G.W_INA}, overlap {G.W_OLA}')
    L, R, m = run('sp_cost7', defaults, xn[:2 * SR // 16 * 16], split=7)
    rep.note('cost with blocks split 7 + 9 (not AERIA\'s engine)', f"mean {m['mean']:.1f}, worst block {m['worst']:.1f} instructions a sample")

if ONLY:
    print(f'(sections {ONLY[0]} only: test_results.md not written) {rep.fail} failure(s)')
else:
    rep.save('/aeria/spectral/test_results.md')
