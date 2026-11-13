"""AERIA FX private test harness (runs inside modwerk-source-tools).

Mounts expected:  /aeria  -> ~/octa/aeria-fx      /sdk -> ~/octa/aeria-fx/.sdk
Assembles one effect source with octabam's own build_bus.assemble (so the
disassembler round-trip and the mpysu audit both run), overlays it on the
stock MAIN OS payload dumps at P:0x2000 under FX id 0x17, and renders it
through dsp_host (built from the image's own vendored emulator).
"""
import array
import math
import os
import re
import subprocess
import sys
from pathlib import Path

SDK = Path('/sdk')
AERIA = Path('/aeria')
BUILD = AERIA / '.build'
HOST = BUILD / 'dsp_host'
FXID = 0x17
ORG = 0x2000
FRAMES = 16
FS = 8388607

os.chdir(SDK)
sys.path.insert(0, str(SDK / 'tools'))
import toolpath  # noqa: E402,F401
import send_probe  # noqa: E402
from remix import registry  # noqa: E402
from remix.schema import Remix  # noqa: E402
registry.remix = lambda _: Remix(name='aeria-test', doc='Private DSP test.',
                                 modules=('MINIVERB',), fallback='NONE')
import build_bus  # noqa: E402
import struct  # noqa: E402

STOCK = SDK / 'out/raw/section_3_MAIN_OS.bin'


def stock_mems():
    out = []
    for c in 'AB':
        p = BUILD / f'stock_{c}.mem'
        if not p.exists():
            send_probe.dump_mem(STOCK, p, c)
        out.append(p)
    return out


def assemble(asm, key):
    """-> (mems, words, init, proc). Refuses on round-trip / mpysu drift."""
    asm = Path(asm)
    words, init, proc = build_bus.assemble(asm.read_text(), ORG, key)

    def chunk(space, address, data):
        return struct.pack('<BII', space, address, len(data)) + struct.pack('<' + 'I' * len(data), *data)
    mems = []
    for c, mem in zip('AB', stock_mems()):
        blob = mem.read_bytes()
        assert blob[-9] == 255
        blob = blob[:-9] + chunk(0, ORG, words)
        blob += chunk(1, 0x215 + FXID, [init]) + chunk(1, 0x235 + FXID, [proc]) + mem.read_bytes()[-9:]
        p = BUILD / f'{key}_{c}.mem'
        p.write_bytes(blob)
        mems.append(p)
    return mems, words, init, proc


def static_cycles(asm, name):
    import cycle_count
    cycle_count._ASM[name] = Path(asm)
    m = cycle_count.measure(name)
    ok = cycle_count.verify(name, m)
    return m, ok


def raw(path, values):
    a = array.array('i', [int(v) for v in values])
    Path(path).write_bytes(a.tobytes())
    return Path(path)


def read_raw(path):
    a = array.array('i')
    a.frombytes(Path(path).read_bytes())
    return a


def render(tag, mems, init, proc, params, inp, blocks, events=None, split=0,
           core=0, guard=False, extra=(), stereo_in=False, dirty=None):
    """params: 12 ints. inp: list of mono samples (ints) or None for silence.
    events: [(block, [12 ints])]. Returns (L, R, meter dict)."""
    out = BUILD / f'{tag}.raw'
    meter = out.with_suffix('.meter')
    cmd = [str(HOST), '-mem', str(mems[0]), '-memB', str(mems[1]),
           '-init', f'{init:x}', '-proc', f'{proc:x}', '-inst', '1',
           '-core', str(core), '-alloc', '1', '-r7', '2', '-audioidx', '0',
           '-allocproc', 'end', '-frames', str(FRAMES), '-blocks', str(blocks),
           '-out', str(out), '-meter', str(meter), '-params', ','.join(map(str, params))]
    if inp is not None:
        n = blocks * FRAMES * (2 if stereo_in else 1)
        data = list(inp)[:n] + [0] * max(0, n - len(inp))
        cmd += ['-in', str(raw(BUILD / f'{tag}.in.raw', data))]
        if stereo_in:
            cmd += ['-stereo']
    else:
        cmd += ['-in', '-']
    if events:
        pf = BUILD / f'{tag}.csv'
        pf.write_text(''.join(','.join(map(str, [b, *v])) + '\n' for b, v in events))
        cmd += ['-paramfile', str(pf)]
    if split:
        cmd += ['-split', str(split)]
    if guard:
        cmd += ['-guard', str(guard if guard is not True else 0x3dfe)]
    if dirty is not None:
        cmd += ['-dirty', hex(dirty)]
    cmd += list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    log = r.stdout + r.stderr
    out.with_suffix('.log').write_text(' '.join(cmd) + '\n' + log)
    if r.returncode:
        raise RuntimeError(f'{tag}: dsp_host rc={r.returncode}\n{log[-2000:]}')
    mi = re.search(r'instance 0: ([0-9.]+) instructions/sample', log)
    mv = re.search(r'(\d+) stray write regions, (\d+) CLOBBERING', log)
    rows = [[int(v) for v in l.split()] for l in meter.read_text().splitlines()]
    col = [r_[1 + core] for r_ in rows]
    steady = col[200:] if len(col) > 400 else col
    m = dict(max_block=max(col), mean_block=sum(steady) / len(steady),
             max_per_frame=max(col) / FRAMES, mean_per_frame=sum(steady) / len(steady) / FRAMES,
             log=log, inst=float(mi[1]) if mi else None,
             stray=int(mv[1]) if mv else 0, clobber=int(mv[2]) if mv else 0)
    a = read_raw(out)
    return list(a[0::2]), list(a[1::2]), m


# ---- signal helpers -------------------------------------------------------
def sine(freq, amp, n, start=0):
    return [round(amp * FS * math.sin(2 * math.pi * freq * (i - start) / 44100)) if i >= start else 0
            for i in range(n)]


def impulse(n, at=256, amp=0.5):
    return [round(amp * FS) if i == at else 0 for i in range(n)]


def noise(n, amp=0.25, seed=1):
    import random
    rnd = random.Random(seed)
    return [round(amp * FS * (2 * rnd.random() - 1)) for _ in range(n)]


def peak(x):
    return max(abs(v) for v in x) / FS if x else 0.0


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x))) / FS


def dc(x):
    return sum(x) / max(1, len(x)) / FS


def db(v):
    return 20 * math.log10(max(v, 1e-12))


def dom_freq(x, lo=20, hi=20000, step=None):
    """Crude dominant-frequency estimate via zero crossings of the signal."""
    zc = sum(1 for a, b in zip(x, x[1:]) if (a < 0) != (b < 0))
    return zc * 44100 / 2 / len(x)


def goertzel(x, f):
    w = 2 * math.pi * f / 44100
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - c * s1 * s2
    return math.sqrt(max(p, 0)) * 2 / len(x) / FS


class Report:
    def __init__(self, name):
        self.name, self.rows, self.fail = name, [], 0

    def check(self, label, ok, detail=''):
        self.rows.append((label, bool(ok), detail))
        print(('PASS ' if ok else 'FAIL ') + label + (' -- ' + str(detail) if detail else ''), flush=True)
        if not ok:
            self.fail += 1

    def note(self, label, detail):
        self.rows.append((label, None, detail))
        print('INFO ' + label + ' -- ' + str(detail), flush=True)

    def markdown(self):
        lines = ['| result | check | detail |', '|---|---|---|']
        for label, ok, d in self.rows:
            lines.append(f"| {'PASS' if ok else ('FAIL' if ok is False else 'info')} | {label} | {d} |")
        return '\n'.join(lines)

    def save(self, path):
        Path(path).write_text(self.markdown() + '\n')
        print(f'{self.name}: {self.fail} failure(s)')


def with_fill(mems, fill, tag):
    """Copies of the mems with the instance block X:0x6200..0x62ff = fill."""
    out = []
    for c, m in zip('AB', mems):
        blob = Path(m).read_bytes()
        body, term = blob[:-9], blob[-9:]
        run = struct.pack('<BII', 1, 0x6200, 0x100) + struct.pack('<I', fill) * 0x100
        p = BUILD / f'{tag}_{c}.mem'
        p.write_bytes(body + run + term)
        out.append(p)
    return out


def label_prefix_check(asm):
    labels = [l.split(':')[0] for l in Path(asm).read_text().splitlines()
              if re.match(r'^[A-Za-z_][A-Za-z0-9_]*:', l)]
    bad = [(a, b) for a in labels for b in labels if a != b and b.startswith(a)]
    dup = {l for l in labels if labels.count(l) > 1}
    return bad, dup


def no_rep(asm):
    return not any(re.match(r'\s+rep\b', l) for l in Path(asm).read_text().splitlines())


_NULL = None


def null_cost(blocks=512):
    """Harness overhead (instructions per frame) of an effect that only rts."""
    global _NULL
    if _NULL is None:
        src = BUILD / 'null.asm'
        src.write_text('init:\n        rts\nproc:\n        rts\n')
        mems, _, i, p = assemble(src, 'AERIA NULL')
        _, _, m = render('null', mems, i, p, [0] * 12, None, 1500)
        _NULL = m
    return _NULL


def standard(rep, asm, key, manifest, dear, blocks=1500):
    """Checks every effect gets: manifest, labels, assembly, static cycles,
    measured cost, MIX 0 exact dry, dirty-state silence, automation + split
    bounded and guarded. Returns (mems, init, proc, defaults)."""
    sys.path.insert(0, str(SDK / 'tools'))
    mod = registry._load_one(Path(manifest))
    names = [p.name.decode() if p.name else '' for p in mod.params]
    defaults = [p.default or 0 for p in mod.params]
    rep.check('manifest loads (schema: names<=4, defaults in count)', True,
              ' '.join(n for n in names if n))
    bad, dup = label_prefix_check(asm)
    rep.check('no label is a prefix of another, no duplicates', not bad and not dup, bad or dup or '')
    rep.check('no rep instruction', no_rep(asm))
    mems, words, init, proc = assemble(asm, key)
    rep.check('assembles; disassembler round-trip + mpysu audit clean', True, f'{len(words)} words')
    try:
        st, ok = static_cycles(asm, Path(asm).stem)
        rep.check('static cycle count (cycle_count.py, worst mode loop)', ok, f"{st['cycles']} cycles/sample"
                  + (f" ({st.get('inner')})" if st.get('inner') else ''))
    except SystemExit as e:
        rep.check('static cycle count', False, str(e))
        st = {'cycles': None}
    nul = null_cost()
    dp = list(defaults)
    for k, v in dear.items():
        dp[names.index(k)] = v
    n = blocks * FRAMES
    L, R, m = render(key.replace(' ', '_') + '_dear', mems, init, proc, dp, noise(n, 0.5), blocks, guard=True)
    cost = m['max_per_frame'] - nul['max_per_frame']
    mean = m['mean_per_frame'] - nul['mean_per_frame']
    rep.check('measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: '
              'steady mean / worst block)', mean < 320, f'mean {mean:.1f}, peak block {cost:.1f} instr/sample')
    rep.check('dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes)',
              peak(L + R) <= 1.0 + 1e-6 and m['stray'] == 0 and m['clobber'] == 0,
              f"peak {peak(L+R):.3f} FS, dc {dc(L[-20000:]):+.5f}, stray {m['stray']}")
    # MIX 0 exact dry, from the first sample, every other knob at dear.
    dry = list(dp); dry[names.index('MIX')] = 0
    x = [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]  # full-range ramp
    x = [v + s for v, s in zip(x, sine(997, 0.0, n))]
    L, R, _ = render(key.replace(' ', '_') + '_dry', mems, init, proc, dry, x, blocks)
    rep.check('MIX 0 is exact dry (full-range ramp, dearest other knobs)', L == x and R == x,
              f'max |err| {max(abs(a-b) for a, b in zip(L, x))} LSB')
    # Dirty state: garbage r7 block + garbage Y, silence in, last 300 samples silent.
    worst = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = with_fill(mems, fill, f'fill{fill:06x}')
        for kn in (defaults, dp):
            Lf, Rf, _ = render(key.replace(' ', '_') + '_dirty', fm, init, proc, kn, None, 300, dirty=0x1234 + fill)
            worst = max(worst, max(abs(v) for v in Lf[-300:] + Rf[-300:]))
    rep.check('dirty state (garbage r7 block + Y) on silence -> silent tail', worst == 0,
              f'worst tail {worst} LSB')
    # Automation: every active knob random per block, split calls, guard on.
    import random
    rnd = random.Random(7)
    counts = [p.count or 128 for p in mod.params]
    ev = []
    for b in range(0, blocks, 3):
        v = list(defaults)
        for j, p in enumerate(mod.params):
            if p.active:
                v[j] = rnd.randrange(counts[j])
        ev.append((b, v))
    worstp = 0
    for split in (0, 7):
        La, Ra, ma = render(key.replace(' ', '_') + f'_auto{split}', mems, init, proc, defaults,
                            noise(n, 0.7, 3), blocks, events=ev, split=split, guard=True)
        worstp = max(worstp, peak(La + Ra))
        if ma['stray'] or ma['clobber']:
            rep.check(f'automation split {split}: guard', False, f"stray {ma['stray']}")
    rep.check('random knob automation every 3 blocks, splits 0 and 7, guard: bounded',
              worstp <= 1.0 + 1e-6, f'peak {worstp:.3f} FS')
    return mod, names, defaults, mems, init, proc, (st['cycles'], cost, mean)


# ---- stereo width / mono compatibility (the STEREO.md pass) -----------------
def corr(a, b):
    """Normalised L/R correlation (zero lag) of two equal-length sequences."""
    saa = sum(v * v for v in a)
    sbb = sum(v * v for v in b)
    if saa == 0 or sbb == 0:
        return 1.0 if saa == sbb else 0.0
    return sum(u * v for u, v in zip(a, b)) / math.sqrt(saa * sbb)


def mono_db(L, R):
    """rms(L + R) against rms(L), dB: +6.02 = identical sides, +3 = uncorrelated, -inf = inverted."""
    return db(rms([u + v for u, v in zip(L, R)]) / max(rms(L), 1e-12))


def burst(n, amp=0.7, at=13230, tau=0.15, seed=11):
    """A decaying white-noise burst starting at sample `at` (silence before)."""
    import random
    rnd = random.Random(seed)
    return [round(amp * FS * (2 * rnd.random() - 1) * math.exp(-(i - at) / (tau * 44100))) if i >= at else 0
            for i in range(n)]


def stereo_report(rep, name, run, wide, secs=3.0, fsine=440.0, corr_max=0.9, fmax=None, skip=0.3, info=False):
    """The width check every effect in STEREO.md gets. run(x, blocks) -> (L, R) renders the effect
    fully wet on a MONO input (the harness copies it to both sides, as a sequencer track does).
    wide=True : L/R correlation clearly below 1 (<= corr_max) on a decaying noise burst and on a
                steady sine, and the mono sum L+R no more than 3 dB under L (and under R).
    wide=False: L == R sample for sample (correlation 1.000, sum +6.02 dB).
    Both: bounded, and on the sine no click -- no sample step larger than a full-level sine at
    `fmax` Hz could make (fmax=None skips that part; give the highest partial the effect makes).
    corr_max may be a pair (burst, sine). info=True only notes the figures.
    Returns {signal: (corr, mono dB re L, mono dB re R)}."""
    blocks = int(secs * 44100 / FRAMES)
    n = blocks * FRAMES
    a = int(skip * 44100)
    out = {}
    cms = corr_max if isinstance(corr_max, (tuple, list)) else (corr_max, corr_max)
    for (sig, x), corr_max in zip((('noise burst', burst(n)), (f'{fsine:g} Hz sine', sine(fsine, 0.5, n))), cms):
        L, R = run(x, blocks)
        a0 = 13230 if sig == 'noise burst' else a       # the burst: from its onset; the sine: after `skip` seconds
        Ls, Rs = L[a0:], R[a0:]
        c = corr(Ls, Rs)
        ml, mr = mono_db(Ls, Rs), mono_db(Rs, Ls)
        pk = peak(L + R)
        live = rms(Ls) > 1e-4 and rms(Rs) > 1e-4
        ok = pk <= 1.0 + 1e-6 and live
        detail = (f'corr {c:+.3f}, L+R {ml:+.2f} dB re L / {mr:+.2f} dB re R, peak {pk:.3f}, '
                  f'rms L {db(rms(Ls)):.1f} R {db(rms(Rs)):.1f} dBFS')
        if wide:
            ok = ok and c <= corr_max and ml >= -3.0 and mr >= -3.0
        else:
            ok = ok and L == R
            detail += f', max |L - R| {max(abs(u - v) for u, v in zip(L, R))} LSB'
        if fmax and sig != 'noise burst':
            lim = 2 * math.sin(math.pi * min(fmax, 22050) / 44100) * max(peak(Ls), peak(Rs)) * 1.05 + 4e-6
            st = max(max(abs(u - v) for u, v in zip(Ls, Ls[1:])), max(abs(u - v) for u, v in zip(Rs, Rs[1:]))) / FS
            ok = ok and st <= lim
            detail += f', largest step {st:.4f} FS (a {fmax:g} Hz partial at this peak: {lim:.4f})'
        if info:        # an effect this pass left alone: the figures, for STEREO.md, not a gate
            rep.note(f'width, {name}, mono {sig} in', detail)
        else:
            rep.check(f'width, {name}, mono {sig} in: ' + ('wide (corr <= %.2f), mono sum within 3 dB of either side' % corr_max
                                                          if wide else 'mono (L = R exactly)') + ', bounded', ok, detail)
        out[sig] = (c, ml, mr)
    return out
