"""GRAINS gate: tools/run.sh python3 /aeria/grains/test.py

Two builds of one generator (gen.py):
  long  grains.asm       35,280 ring words. The harness's FX2 buffer is 16,384 words at Y:$4000,
                         too small, so for this build the test points the allocator entry (X:$256)
                         at Y:$0000, as on the unit, and widens the guard window to the ring
                         ($89d0 words: any write at Y:$89d0..$bfff or into P is reported).
  test  grains_test.asm  15,870 ring words in the stock buffer, stock guard window ($3dfe).
The two are the same code (ring wrapped in code, 48-bit ages); only the ring constants differ.
Both read their grain window from the 65-point table at r7+$80, which the host writes; the test
writes a triangle there (X:$6280..$62c0) by appending a chunk to the memory image.
All of the hooks are in this file; tools/ is untouched.
"""
import math
import os
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
import aeria as A

G = Path('/aeria/grains')
SHARED = A.BUILD
A.BUILD = SHARED / 'grains'          # own scratch: other effects' gates run in .build at the same time
A.BUILD.mkdir(exist_ok=True)
for c in 'AB':
    if (SHARED / f'stock_{c}.mem').exists() and not (A.BUILD / f'stock_{c}.mem').exists():
        shutil.copy(SHARED / f'stock_{c}.mem', A.BUILD / f'stock_{c}.mem')
fs = 44100
R7 = 0x6200
M = 48


def table(kind):
    """the 65-point window as aeria-os/dsp/engine_test.cpp fills it"""
    out = []
    for i in range(65):
        t = i / 64
        w = {'tri': 1 - abs(2 * t - 1), 'hann': 0.5 - 0.5 * math.cos(2 * math.pi * t),
             'square': 0.0 if i in (0, 64) else 1.0}[kind]
        out.append(int(min(w, 0.9999999) * 8388607.0))
    return out


CUR = dict(window='tri', base=None, guard=0x3dfe)
_assemble, _render = A.assemble, A.render


def chunk(space, address, data):
    return struct.pack('<BII', space, address, len(data)) + struct.pack('<' + 'I' * len(data), *data)


def assemble(asm, key):
    """A.assemble + the window table in the instance block (+ the allocator entry for the long ring)."""
    mems, words, init, proc = _assemble(asm, key)
    for p in mems:
        blob = p.read_bytes()
        add = chunk(1, R7 + 0x80, table(CUR['window']))
        if CUR['base'] is not None:
            add += chunk(1, 0x256, [CUR['base']])
        p.write_bytes(blob[:-9] + add + blob[-9:])
    return mems, words, init, proc


def render(*a, **k):
    if k.get('guard') is True:
        k['guard'] = CUR['guard']
    return _render(*a, **k)


A.assemble, A.render = assemble, render


def gen(out, **env):
    e = dict(os.environ, OUT=str(out), **{k: str(v) for k, v in env.items()})
    subprocess.run([sys.executable, str(G / 'gen.py')], env=e, check=True, capture_output=True)
    return Path(out)


def zcp(sig, th=0.02 * A.FS):
    """pitch from zero crossings with hysteresis (window edges hover near 0)."""
    state, cnt = 0, 0
    for v in sig:
        if v > th and state <= 0:
            cnt += state < 0; state = 1
        elif v < -th and state >= 0:
            cnt += state > 0; state = -1
    return cnt * 44100 / 2 / len(sig)


def stpitch(sig, want, w=2048):
    """median over 46 ms windows of the strongest frequency within +-1 st of want, in cents."""
    out = []
    for i in range(0, len(sig) - w, w):
        part = sig[i:i + w]
        if A.rms(part) < 0.02:
            continue
        cs = range(-100, 101, 4)
        out.append(max(cs, key=lambda c: A.goertzel(part, want * 2 ** (c / 1200))))
    out.sort()
    return out[len(out) // 2] if out else 999


def stg(sig, f, w=2048):
    """mean short-time Goertzel magnitude (grain phases are independent, so a
    long window cancels; 46 ms windows see one or two grains)."""
    vals = [A.goertzel(sig[i:i + w], f) for i in range(0, len(sig) - w, w)]
    return sum(vals) / len(vals)


def track(tag, mems, init, proc, params, inp, blocks, slots, **kw):
    """render + the r7-relative X words in slots, sampled after every block"""
    tf = A.BUILD / f'{tag}.track'
    L, R, m = A.render(tag, mems, init, proc, params, inp, blocks,
                       extra=('-track', ','.join(f'{s:x}' for s in slots), '-trackout', str(tf)), **kw)
    rows = [[int(v) for v in l.split()][1:] for l in tf.read_text().splitlines()]
    return L, R, rows


def s24(v):
    return v - (1 << 24) if v & 0x800000 else v


def envelope_check(rep, name, mems, init, proc, d, want_len, blocks):
    """DC in, one grain (DENS 0), unison, POS 0: the output IS the window, once per grain."""
    p = list(d); p[0] = 127; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 24; p[5] = 127; p[8] = 0
    n = blocks * 16
    L, R, _ = A.render(f'gr_env_{name}', mems, init, proc, p, [A.FS // 2] * n, blocks)
    pk = max(L)
    zeros, i = [], n // 8
    while i < n:                       # centres of the runs where the window is < 1 %
        if L[i] < 0.01 * pk:
            j = i
            while j < n and L[j] < 0.01 * pk:
                j += 1
            if j < n:
                zeros.append((i + j - 1) / 2)
            i = j
        else:
            i += 1
    gaps = [b - a for a, b in zip(zeros, zeros[1:])]
    while gaps and gaps[0] > 1.1 * want_len:    # the first grain after init is silent (its gains latch at a restart)
        gaps, zeros = gaps[1:], zeros[1:]
    ok = len(gaps) >= 1 and all(abs(g - want_len) <= max(4, 0.005 * want_len) for g in gaps)
    dev = 0.0
    if gaps:
        a, b = zeros[0], zeros[1]
        for t in range(int(a) + 2, int(b) - 1):
            u = (t - a) / (b - a)
            dev = max(dev, abs(L[t] / pk - (1 - abs(2 * u - 1))))
    return ok, gaps, dev, pk / A.FS


def gate(rep, name, asm, key, N, base, guard):
    CUR.update(window='tri', base=base, guard=guard)
    R = (N - 1) - 2 * M - 2      # the ring holds N - 1 samples: one of the N words is the mirror
    long = name == 'long'
    rep.note(f'==== {name} build', f'{asm}, {N} ring words ({N - 1} samples + mirror), base '
             + ('Y:$0000 (allocator entry X:$256 poked)' if base is not None else 'Y:$4000 (stock FX2 buffer)')
             + f', guard window ${guard:x}, triangle window table at X:${R7 + 0x80:x}')
    mod, names, d, mems, init, proc, cost = A.standard(
        rep, asm, key, '/aeria/grains/manifest.py',
        {'SIZE': 0, 'DENS': 127, 'SPRY': 127, 'PTCH': 48, 'REV': 127, 'MIX': 127, 'SPRD': 127})
    SPRD = names.index('SPRD')
    dflt = list(d)
    d = list(d); d[SPRD] = 0      # the checks below read the LEFT output of single grains: every grain in the middle
    log = (A.BUILD / (key.replace(' ', '_') + '_dear.log')).read_text()
    want = f'base = Y:0x{base if base is not None else 0x4000:05x}'
    rep.check(f'{name}: the instance got the intended ring base', want in log, want)
    blocks = 1500
    n = blocks * 16
    f0 = 440.0
    x = A.sine(f0, 0.5, n)
    for idx, st in ((24, 0), (36, 12), (12, -12), (31, 7), (48, 24), (0, -24)):
        p = list(d); p[0] = 70; p[1] = 0; p[3] = 0; p[4] = idx; p[5] = 127; p[8] = 0
        L, Rr, _ = A.render(f'gr_{name}_{st}', mems, init, proc, p, x, blocks)
        seg = L[-16384:]
        wantf = f0 * 2 ** (st / 12)
        t, o = stg(seg, wantf), stg(seg, f0) if st else 0.0
        zc = stpitch(seg, wantf)
        rep.check(f'{name}: PTCH {st:+d}, DENS 0 (one grain): 440 Hz -> {wantf:.1f} Hz (46 ms windows: target > 0.15, '
                  f'440 Hz < target/10; median short-window spectral peak within 20 cents)',
                  t > 0.15 and o < t / 10 and abs(zc) <= 20 and A.peak(L + Rr) <= 1.0,
                  f'target {t:.3f}, 440 {o:.4f}, peak {zc:+d} cents, out peak {A.peak(L+Rr):.3f}')
    # DENS 127 + unison + no spray: grains reconstruct the input level (2 staggered triangles sum to 1)
    p = list(d); p[0] = 70; p[1] = 127; p[2] = 10; p[3] = 0; p[4] = 24; p[5] = 127; p[8] = 0
    L, Rr, _ = A.render(f'gr_{name}_level', mems, init, proc, p, x, blocks)
    lv = A.rms(L[-16384:]) / A.rms(x[-16384:])
    rep.check(f'{name}: DENS 127, unison, SPRY 0: output level within 1.5 dB of the input', abs(A.db(lv)) < 1.5,
              f'{A.db(lv):+.2f} dB')
    p[8] = 127; p[1] = 0
    L, Rr, _ = A.render(f'gr_{name}_rev', mems, init, proc, p, x, blocks)
    zc = stpitch(L[-16384:], 440.0)
    rep.check(f'{name}: REV 127, DENS 0 (one grain, always backwards): a 440 Hz sine stays 440 Hz (46 ms windows > 0.15, '
              f'spectral peak within 20 cents)',
              stg(L[-16384:], 440.0) > 0.15 and abs(zc) <= 20 and A.peak(L + Rr) <= 1.0,
              f'440 {stg(L[-16384:], 440.0):.3f}, peak {zc:+d} cents')
    # FREEZE: tone A for 0.5 s then tone B (3 s render, last 0.5 s analysed)
    fb = int(3 * fs / 16)
    fn = fb * 16
    xa = A.sine(440.0, 0.5, int(0.5 * fs)) + A.sine(1000.0, 0.5, fn - int(0.5 * fs))
    p = list(d); p[0] = 50; p[1] = 127; p[2] = 30 if not long else 13; p[3] = 20 if not long else 9; p[4] = 24
    p[5] = 127; p[8] = 0
    ph = list(p); ph[6] = 1
    L, Rr, _ = A.render(f'gr_{name}_freeze', mems, init, proc, p, xa, fb, events=[(0, p), (int(0.4 * fs / 16), ph)])
    seg = L[-22050:]
    rep.check(f'{name}: FRZ HOLD at 0.4 s, input -> 1 kHz at 0.5 s: 2.5 s later the output still holds 440 Hz, no 1 kHz',
              stg(seg, 440.0) > 0.1 and stg(seg, 1000.0) < 0.01,
              f'440: {stg(seg, 440.0):.3f}, 1000: {stg(seg, 1000.0):.4f}')
    L, Rr, _ = A.render(f'gr_{name}_thaw', mems, init, proc, p, xa, fb)
    seg = L[-22050:]
    rep.check(f'{name}: THAW (same input): output follows the input to 1 kHz',
              stg(seg, 1000.0) > 0.1 and stg(seg, 440.0) < 0.01,
              f'1000: {stg(seg, 1000.0):.3f}, 440: {stg(seg, 440.0):.4f}')
    # ---- stereo: SPRD (page 2) places each grain left/right at random, and moves its start with the pan ----
    # measured from 1 s: before that grains can still read the ring's first recorded sample (recording starts when the
    # clearing ends, mid-cycle of the test sine: a step that is in the INPUT as the ring saw it)
    def wet(tag, **kw):
        def run(xx, bl):
            q_ = list(dflt); q_[5] = 127
            for k_, v_ in kw.items():
                q_[names.index(k_)] = v_
            return A.render(f'gr_{name}_w{tag}', mems, init, proc, q_, xx, bl)[:2]
        return run
    A.stereo_report(rep, f'{name}: defaults (SPRY 30, SPRD 100), MIX 127', wet('d'), True, secs=6.0, fmax=660, skip=1.0)
    A.stereo_report(rep, f'{name}: SPRY 0, unison, DENS 127, SPRD 100 (no spray: the width is SPRD alone)',
                    wet('n', SPRY=0, DENS=127), True, secs=6.0, fmax=660, skip=1.0)
    A.stereo_report(rep, f'{name}: SPRY 127, SPRD 127', wet('x', SPRY=127, SPRD=127), True, secs=6.0, fmax=660, skip=1.0)
    A.stereo_report(rep, f'{name}: SPRD 0 (SPRY 127: spray alone no longer pans)', wet('m', SPRY=127, SPRD=0), False, secs=6.0, fmax=660, skip=1.0)
    # the pan law on the DSP's own grain gains (r7 20+8k+4/5): gL + gR = the grain's gain, at every restart
    q_ = list(dflt); q_[1] = 127; q_[5] = 127; q_[SPRD] = 127; q_[0] = 20
    _, _, rows = track(f'gr_{name}_pan', mems, init, proc, q_, A.noise(3000 * 16, 0.3), 3000,
                       [0x24, 0x25, 0x2c, 0x2d, 0x34, 0x35, 0x3c, 0x3d, 0x10, 0x11, 0x12, 0x13, 0x20, 0x28, 0x30, 0x38])
    worst, hard, grains, sl_ = 0, 0, 0, 0.0
    for prev, r_ in zip(rows[100:], rows[101:]):
        for k_ in range(4):
            if s24(r_[12 + k_]) < s24(prev[12 + k_]):          # the phase fell back: this grain restarted in this block
                gl, gr, g = r_[2 * k_], r_[2 * k_ + 1], r_[8 + k_]
                worst = max(worst, abs(gl + gr - g))
                grains += 1
                hard += gl == 0 or gr == 0
                sl_ += gl / max(g, 1)
    rep.check(f'{name}: SPRD 127: every grain has gL + gR = its gain (the mono sum does not depend on the pan); about half '
              f'sit hard left or right, and left and right get equal shares', worst <= 2 and 0.35 < hard / grains < 0.65
              and 0.4 < sl_ / grains < 0.6,
              f'max |gL + gR - g| {worst} LSB over {grains} grains; {100 * hard / grains:.0f} % hard-panned; mean left share {sl_ / grains:.2f}')

    # ---- ring-length checks ------------------------------------------------
    u = 127 / 128
    want_len = {'long': 750.0, 'test': 10 + 290 * u * u}[name] * 44.1
    eb = int((3.6 * want_len + 0.3 * fs) / 16) + 200
    ok, gaps, dev, pk = envelope_check(rep, name, mems, init, proc, d, want_len, eb)
    rep.check(f'{name}: SIZE 127 grain length (DC in, one grain, unison: spacing of the window zeros) = '
              f'{want_len:.0f} samples ({want_len / 44.1:.1f} ms) within 0.5 %',
              ok, f'spacings {[round(g, 1) for g in gaps]} samples = {[round(g / 44.1, 1) for g in gaps]} ms')
    rep.check(f'{name}: the output window follows the triangle table (max deviation from 1 - |2t - 1| over one grain '
              f'< 0.5 % of the peak)', gaps and dev < 0.005, f'max deviation {100 * dev:.3f} %, window peak {pk:.4f} FS')
    # SIZE 0: the short end
    p = list(d); p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 24; p[5] = 127; p[8] = 0
    _, _, rows = track(f'gr_{name}_inc0', mems, init, proc, p, None, 200, [0x08, 0x09])
    inc0, len0 = rows[-1]
    p[0] = 1
    _, _, rows = track(f'gr_{name}_inc1', mems, init, proc, p, None, 200, [0x08, 0x09])
    rep.check(f'{name}: SIZE 0 = 10 ms (441 samples) and SIZE 1 - SIZE 0 = 290/128^2 ms (0.78 samples), as before',
              abs((1 << 23) / inc0 - 441) < 1 and abs((rows[-1][1] - len0) / 128 - 290 * 44.1 / 16384) < 0.02,
              f'SIZE 0: 2^23/inc = {(1 << 23) / inc0:.2f} samples, len word {len0 / 128:.3f}; SIZE 1: len word {rows[-1][1] / 128:.3f}')
    # POS 127: an impulse comes back M + R 127/128 samples later
    at = 20000
    want_d = M + R * u
    ib = (at + int(want_d) + 3000) // 16
    p = list(d); p[0] = 70; p[1] = 127; p[2] = 127; p[3] = 0; p[4] = 24; p[5] = 127; p[8] = 0
    L, Rr, _ = A.render(f'gr_{name}_pos', mems, init, proc, p, A.impulse(ib * 16, at=at, amp=0.5), ib)
    k = max(range(len(L)), key=lambda i: abs(L[i]))
    rest = max(abs(v) for i, v in enumerate(L) if abs(i - k) > 1)
    rep.check(f'{name}: POS 127 reads {want_d:.1f} samples ({want_d / 44.1:.1f} ms) back (impulse in, DENS 127, unison, '
              f'SPRY 0: output peak within 1 sample, nothing elsewhere)',
              abs(k - at - want_d) <= 1.0 and rest == 0,
              f'peak at +{k - at} samples ({(k - at) / 44.1:.1f} ms), {L[k] / A.FS:.4f} FS (+1: {L[k + 1] / A.FS:.4f}), '
              f'largest other sample {rest} LSB')
    p[2] = 0
    L, Rr, _ = A.render(f'gr_{name}_pos0', mems, init, proc, p, A.impulse(1600 * 16, at=at, amp=0.5), 1600)
    k = max(range(len(L)), key=lambda i: abs(L[i]))
    rep.check(f'{name}: POS 0 reads {M} samples back', k - at == M, f'peak at +{k - at} samples')
    # the wrap seam: unison, DENS 127, POS 1 (a fractional age), noise in, several turns of the ring.
    # The output must be the input, linearly interpolated at that age, on every sample -- including
    # the ones whose pair straddles the wrap (the older sample then comes from the mirror word).
    # Tolerance: DENS 127 leaves grain 3 at gain 0.977 (t = 3 DENS/4, as before), a +-0.58 % level
    # ripple = 0.29 % FS on 0.5 FS noise; a wrong sample at the seam would be tens of % FS.
    NL = N - 1
    nclr = 73 if long else 35                       # dry calls after init (gen.py NCLR)
    p = list(d); p[0] = 70; p[1] = 127; p[2] = 1; p[3] = 0; p[4] = 24; p[5] = 127; p[8] = 0
    sb = int(2.6 * NL / 16) + 400
    xs = A.noise(sb * 16, 0.5, 21)
    # quality pass (12 Nov 2026): grain starts are whole samples (gen.py SNAP=1), so at unison the
    # output is the input exactly delayed; the seam checks below need a fractional age (to weigh the
    # mirror word), so they run on the SNAP=0 build of the same source
    L, Rr, rows = track(f'gr_{name}_snap', mems, init, proc, p, xs, sb, [0x22, 0x26, 0x01])
    age = (s24(rows[-1][0]) + rows[-1][1] / (1 << 24)) / 128
    w = int(age)
    t0 = nclr * 16 + 3 * 6200
    e = max(abs(L[t] - xs[t - w]) for t in range(t0, sb * 16))
    rep.check(f'{name}: unison, DENS 127, POS 1, noise (SNAP=1, the shipped build): the grain age is a whole number of samples '
              f'and the output is the input exactly delayed on every sample (max |err| < 64 LSB; DENS 127 is four full grains '
              f'now (DFULL): before this pass a +-0.58 % level ripple, 24,592 LSB here)',
              age == w and e < 64, f'age {age:.6f} samples, max |err| {e:.0f} LSB = {100 * e / A.FS:.4f} % FS')
    shipped = (mems, init, proc)
    ns = gen(A.BUILD / f'grains_{name}_nosnap.asm', RING='long' if long else 'test', SNAP=0)
    mems, _, init, proc = A.assemble(ns, key + 'N')
    L, Rr, rows = track(f'gr_{name}_seam', mems, init, proc, p, xs, sb, [0x22, 0x26, 0x01])
    age = (s24(rows[-1][0]) + rows[-1][1] / (1 << 24)) / 128
    w, f = int(age), age - int(age)

    def seam_err(L, x, lo_, hi_):
        return max(abs(L[t] - (x[t - w] * (1 - f) + x[t - w - 1] * f)) for t in range(lo_, hi_))
    t0 = nclr * 16 + 3 * 6200                      # past the first (silent, then fading-in) grains
    e = seam_err(L, xs, t0, sb * 16)
    turns = (sb - nclr) * 16 / NL
    rep.check(f'{name}: wrap seam (SNAP=0 build): unison, DENS 127, POS 1, noise: output = input interpolated at the grain age on '
              f'EVERY sample over {turns:.1f} turns of the ring (max |err| < 64 LSB)',
              0 < f < 1 and e < 64 and turns > 2,
              f'age {age:.4f} samples (fraction {f:.4f}), max |err| {e:.0f} LSB = {100 * e / A.FS:.4f} % FS')
    # the same with FRZ HOLD taken exactly while the head sits on the TOP ring word (its mirror goes
    # stale while frozen), held 2000 blocks, then THAW: after the grains that started frozen have
    # ended, the output is the interpolated input again, across the next wraps
    kf = (NL - 1) * pow(16, -1, NL) % NL            # 16 kf = NL - 1 (mod NL)
    bf = nclr + kf
    hold = 2000
    fbk = bf + hold + int(2.4 * NL / 16) + 600
    xf = A.noise(fbk * 16, 0.5, 22)
    ph = list(p); ph[6] = 1
    L, Rr, rows = track(f'gr_{name}_seamfrz', mems, init, proc, p, xf, fbk, [0x22, 0x26, 0x01],
                        events=[(0, p), (bf, ph), (bf + hold, p)])
    held = {r_[2] for r_ in rows[bf:bf + hold]}
    t1 = (bf + hold) * 16 + 3 * 6200
    e = seam_err(L, xf, t1, fbk * 16)
    rep.check(f'{name}: wrap seam (SNAP=0 build) after FRZ HOLD with the head on the top ring word (index {NL - 1}), 2000 blocks, then '
              f'THAW: output = interpolated input again on every sample over the next '
              f'{(fbk * 16 - t1) / NL:.1f} turns (max |err| < 64 LSB)',
              held == {NL - 1} and e < 64,
              f'head index while held {sorted(held)}, frozen at block {bf}, max |err| {e:.0f} LSB = {100 * e / A.FS:.4f} % FS')
    mems, init, proc = shipped
    # ages stay inside [M, M + R] (a read never crosses the record head or the ring end): the age
    # words of the four grains after every block, at the settings that use the whole span
    slots = [0x22, 0x2a, 0x32, 0x3a]
    lo, hi, cases = 1e9, -1e9, 0
    nclr = 80 if long else 40
    tb = int(2.5 * (R + 2000) / 16)
    rnd = random.Random(11)
    ev = []
    for b in range(0, tb, 5):
        v = list(d)
        for j, pm in enumerate(mod.params):
            if pm.active:
                v[j] = rnd.randrange(pm.count or 128)
        v[0] = rnd.choice((127, 127, 100, v[0])); v[5] = 127
        ev.append((b, v))
    sets = [dict(SIZE=127, PTCH=q_, REV=r_, POS=po, SPRY=sp, FRZ=fz)
            for q_ in (0, 11, 12, 23, 24, 25, 36, 37, 48) for r_, po, sp, fz in
            ((0, 127, 0, 0), (127, 0, 0, 0), (64, 64, 127, 0), (127, 127, 127, 1))]
    for sset in sets + ['auto0', 'auto7']:
        p = list(d); p[1] = 127; p[5] = 127
        kw = {}
        if isinstance(sset, dict):
            p[0] = sset['SIZE']; p[2] = sset['POS']; p[3] = sset['SPRY']; p[4] = sset['PTCH']; p[8] = sset['REV']
            if sset['FRZ']:
                pf = list(p); pf[6] = 1
                kw = dict(events=[(0, p), (tb // 3, pf)])
        else:
            kw = dict(events=ev, split=int(sset[4:]))
        _, _, rows = track(f'gr_{name}_age', mems, init, proc, p, None, tb, slots, **kw)
        ages = [s24(v) / 128 for r_ in rows[nclr:] for v in r_]
        lo, hi, cases = min(lo, min(ages)), max(hi, max(ages)), cases + 1
    rep.check(f'{name}: grain ages stay in [M, M + R] = [{M}, {M + R}] samples (never across the head or past the ring: '
              f'SIZE 127 x PTCH -24..+24 x REV/POS/SPRY/FRZ corners + random automation, split 0 and 7; '
              f'every grain after every block, {cases} runs of {tb} blocks)',
              lo >= M - 1 and hi <= M + R + 1, f'min {lo:.2f}, max {hi:.2f} samples (ring {N})')
    # stale memory: garbage everywhere the ring can be, silence in, random automation: every sample 0
    extra = ()
    if base == 0:   # -dirty fills Y:$1000..$bfff; the ring also covers Y:0..$fff
        g = random.Random(5)
        extra = ('-pokey', ','.join(f'{a_:x}={g.randrange(1, 1 << 24):x}' for a_ in range(0x1000)))
    worst = 0
    for split in (0, 7):
        for evs, pp in ((ev, d), (None, [127, 127, 127, 127, 24, 127, 0, 0, 64, 0, 0, 0]),
                        (None, [127, 127, 127, 127, 0, 127, 0, 0, 127, 0, 0, 0])):
            L, Rr, _ = A.render(f'gr_{name}_stale', mems, init, proc, pp, None, tb, events=evs, split=split,
                                dirty=0x51234, extra=extra)
            worst = max(worst, max(abs(v) for v in L + Rr))
    rep.check(f'{name}: no read returns uncleared memory: Y garbage under the whole ring, silence in, '
              f'fixed far settings and random automation, splits 0 and 7: EVERY output sample is 0 ({tb} blocks)',
              worst == 0, f'worst {worst} LSB')
    # ---- aliasing (13 Nov 2026, "stop aliasing"): the anti-alias low-pass on the recording -------------------------
    sys.path.insert(0, '/aeria/grains')
    import aa_design as AD
    trk = A.BUILD / f'gr_{name}_aa.track'
    worstw = 0
    for pt_ in (0, 12, 24, 25, 28, 31, 36, 40, 43, 48, 36):
        pp = list(d); pp[4] = pt_
        A.render(f'gr_{name}_aatrk', mems, init, proc, pp, None, 40, extra=['-track', '40,41,42,43,44,45', '-trackout', str(trk)])
        got = [int(v) for v in trk.read_text().split('\n')[-2].split()[1:]]
        want = list(AD.dsp_words(pt_ - 24))
        if pt_ <= 24:
            want[0] = 1 << 21                                  # (L + R)/4: the ring holds y/2
        else:
            want[0] = round(want[0] / 4)
        worstw = max(worstw, max(abs(((g - (w & 0xffffff) + (1 << 23)) % (1 << 24)) - (1 << 23)) for g, w in zip(got, want)))
    rep.check(f'{name}: anti-alias low-pass: the coefficient words the DSP computes per PTCH (degree-4 polynomials, B '
              'divided out for y/2 at unity DC gain) are aa_design.py\'s, within 64 LSB (at or under unison: B = 1/4, the '
              'rest 0)', worstw <= 64, f'worst difference {worstw} LSB over 11 settings')

    def bandnoise(lo, hi, nn, rms_=0.1, seed=7, k=48):
        import random
        rg = random.Random(seed)
        acc = [0.0] * nn
        for j in range(k):
            w = 2 * math.pi * (lo + (hi - lo) * (j + rg.random()) / k) / 44100
            rot = complex(math.cos(w), math.sin(w))
            ph = rg.random() * 2 * math.pi
            z = complex(math.cos(ph), math.sin(ph))
            for i in range(nn):
                acc[i] += z.imag
                z *= rot
        sc = rms_ * A.FS / math.sqrt(sum(v * v for v in acc) / nn)
        return [round(v * sc) for v in acc]
    rows, ok = [], True
    bl_ = int(2.5 * 44100 / 16)
    for st in (12, 19, 24):
        r = 2 ** (st / 12)
        pp = list(d); pp[0] = 70; pp[1] = 127; pp[2] = 20; pp[3] = 0; pp[4] = 24 + st; pp[5] = 127

        def wetdb(lo, hi, tag):
            L, Rr, _ = A.render(f'gr_{name}_al{st}{tag}', mems, init, proc, pp, bandnoise(lo, hi, bl_ * 16), bl_)
            return A.db(A.rms([a + b for a, b in zip(L[44100:], Rr[44100:])]) + 1e-12)
        ref = wetdb(2000 / r, 4000 / r, 'r')
        low = wetdb((44100 - 16000) / r, 21000, 'l') - ref
        high = wetdb(22050 / r + 100, (44100 - 16000) / r, 'h') - ref
        top = wetdb(12000 / r, 14000 / r, 't') - ref
        ok &= low < -30 and high < -15 and top > -10
        rows.append(f'+{st}: landing < 16 kHz {low:.1f} dB, 16-22 kHz {high:.1f} dB, wanted 12-14 kHz out {top:+.1f} dB')
    rep.check(f'{name}: aliasing: band noise above fs / (2 ratio) in (DENS 127, SPRY 0, MIX 127), output power re the '
              'output of the same power in the band that lands at 2-4 kHz: what folds back under 16 kHz is >= 30 dB down, '
              'what lands at 16-22 kHz >= 15 dB down (before the low-pass, the same measure: -0.2 .. -2.9 dB and -0.1 .. -0.8 dB: as loud as the wanted sound), the '
              'wanted top (12-14 kHz out) within 10 dB of 2-4 kHz', ok, '; '.join(rows))
    rep.note(f'{name}: cost', f'static {cost[0]} cycles/sample (every grain restarting in the same sample); '
                              f'dsp_host mean {cost[2]:.1f}, worst block {cost[1]:.1f} instr/sample')
    return mems, init, proc, d


rep = A.Report('GRAINS')

# ---- the generator reproduces the files --------------------------------------
leg = gen(A.BUILD / 'grains_legacy.asm', RING='test', AGE=24, WINFIX=0)
rep.check('gen.py RING=test AGE=24 WINFIX=0 regenerates grains.asm.handpatched byte for byte',
          leg.read_bytes() == (G / 'grains.asm.handpatched').read_bytes())
legfix = gen(A.BUILD / 'grains_legfix.asm', RING='test', AGE=24, WINFIX=1)
for nm, f in (('long', 'grains.asm'), ('test', 'grains_test.asm')):
    rep.check(f'{f} is what gen.py RING={nm} writes now', gen(A.BUILD / f'regen_{f}', RING=nm).read_bytes()
              == (G / f).read_bytes())

mt, it, pt, d = gate(rep, 'test', '/aeria/grains/grains_test.asm', 'AERIA GRAINST', 15870, None, 0x3dfe)
gate(rep, 'long', '/aeria/grains/grains.asm', 'AERIA GRAINS', 35280, 0, 0x89d0)

# ---- against the build before the window table (test ring, triangle table) -----
CUR.update(window='tri', base=None, guard=0x3dfe)
# the quality pass (SNAP, DFULL) changes the output on purpose: these comparisons use the same source built as before it
mt, _, it, pt = A.assemble(gen(A.BUILD / 'grains_test_before.asm', RING='test', SNAP=0, DFULL=0, AA=0), 'AERIA GRAINSB')
shutil.copy(G / 'grains.asm.pretable', A.BUILD / 'grains_pretable.asm')
shutil.copy(G / 'grains.asm.handpatched', A.BUILD / 'grains_handpatched.asm')
refs = {}
for k, f in (('pre', 'grains_pretable.asm'), ('hand', 'grains_handpatched.asm'), ('fix', 'grains_legfix.asm')):
    m_, _, i_, p_ = A.assemble(A.BUILD / f, 'AERIA GR' + k.upper())
    refs[k] = (m_, i_, p_)
blocks = 2000
n = blocks * 16
SH = (128 - 35) * 16          # the 48-bit build finishes clearing 93 blocks earlier (35 dry calls, not 128)
sigs = {'440 Hz 0.5 FS': A.sine(440.0, 0.5, n), 'noise 0.5 FS': A.noise(n, 0.5, 9)}
cases = {'unison, DENS 127, POS 0, SPRY 0': (70, 127, 0, 0, 24, 127, 0, 0, 0, 0, 0, 0),
         'unison, DENS 127, POS 20, SPRY 0': (70, 127, 20, 0, 24, 127, 0, 0, 0, 0, 0, 0),
         'defaults but MIX 127 (SPRY 30)': (70, 80, 20, 30, 24, 127, 0, 0, 0, 0, 0, 0),
         'PTCH +7, DENS 127, SPRY 0': (70, 127, 20, 0, 31, 127, 0, 0, 0, 0, 0, 0)}
for cn, p in cases.items():
    for sn, x in sigs.items():
        out = {k: A.render(f'gr_ref_{k}', m, i_, p_, list(p), x, blocks)[0] for k, (m, i_, p_) in refs.items()}
        xs = [0] * SH + x[:n - SH]
        pre_s = A.render('gr_ref_pre_s', *refs['pre'], list(p), xs, blocks)[0]
        new = A.render('gr_ref_new', mt, it, pt, list(p), x, blocks)[0]
        a0 = 300 * 16
        ref = out['pre'][a0:]
        rr = A.rms(ref)

        def cmpd(sig, refsig):
            dlt = [u - v for u, v in zip(sig, refsig)]
            return max(abs(v) for v in dlt), A.db(A.rms(dlt) / max(A.rms(refsig), 1e-12))
        mh, dh = cmpd(out['hand'][a0:], ref)
        mf, df = cmpd(out['fix'][a0:], ref)
        mn, dn = cmpd(new[a0 - SH:n - SH], pre_s[a0:])
        lv = A.db(A.rms(new[a0 - SH:n - SH]) / max(A.rms(pre_s[a0:]), 1e-12))
        rep.note(f'vs pre-table build, {cn}, {sn}',
                 f'hand-patched: max |diff| {mh} LSB, rms {dh:+.1f} dB re output; '
                 f'hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max {mf} LSB, rms {df:+.1f} dB; '
                 f'grains_test.asm (aligned {SH} samples): max {mn} LSB, rms {dn:+.1f} dB, '
                 f'level {lv:+.3f} dB')
        if 'POS 0' in cn:
            rep.check(f'triangle table reproduces the pre-table build ({cn}, {sn}): grains_test.asm (SNAP=0 DFULL=0) within -60 dB rms',
                      dn < -60, f'{dn:+.1f} dB (max {mn} LSB)')
# the window itself, hand-patched build: DC in, one grain
ok, gaps, dev, pk = envelope_check(rep, 'hand', *refs['hand'], d, (10 + 290 * (127 / 128) ** 2) * 44.1, 3600)
rep.note('hand-patched build window, DC in, one grain',
         f'max deviation from the triangle {100 * dev:.3f} % of the peak; grain spacings {[round(g, 1) for g in gaps]}')
CUR.update(guard=0x3dfe)
for k in ('pre', 'hand'):
    _, _, mm = A.render(f'gr_cost_{k}', *refs[k], [0, 127, 20, 127, 48, 127, 0, 0, 127, 0, 0, 0], A.noise(1500 * 16, 0.5), 1500,
                        guard=True)
    nul = A.null_cost()
    rep.note(f'cost of the {"pre-table" if k == "pre" else "hand-patched"} build, same settings, for reference',
             f"mean {mm['mean_per_frame'] - nul['mean_per_frame']:.1f}, worst block "
             f"{mm['max_per_frame'] - nul['max_per_frame']:.1f} instr/sample")
rep.save('/aeria/grains/test_results.md')
