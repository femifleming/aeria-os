"""Shared gate of the five weather effects (the same file in each folder; wind/ is the master).

tools/aeria.py's standard() wants a MIX knob; these effects have AMT (knob 0) instead, and
rest (return at once) at AMT 0. This is that gate with AMT in MIX's place, plus what the
weather brief asks of every one of them: bypass exact and nearly free at AMT 0, bounded over
60 s of noise and silence, cost as mean and worst block."""
import math
import random
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
import aeria as A  # noqa: E402

FSR = 44100
FS = A.FS


def P(names, defaults, **kw):
    p = list(defaults)
    for k, v in kw.items():
        p[names.index(k)] = v
    return p


def ramp(n):
    return [round(-FS + i * 2 * FS / (n - 1)) for i in range(n)]


def stereo(l, r):
    return [v for pair in zip(l, r) for v in pair]


def standard(rep, asm, key, manifest, dear, guard=True, cost_limit=200, tail_lsb=0, tail_blocks=3000,
             build_note='', minute=True, minute_peak=1.0, x_effect=False):
    """-> (mod, names, defaults, mems, init, proc, (mean, worst))."""
    mod = A.registry._load_one(Path(manifest))
    names = [p.name.decode() if p.name else '' for p in mod.params]
    defaults = [p.default or 0 for p in mod.params]
    ok = (all(len(nm) <= 4 for nm in names) and names[0] == 'AMT' and defaults[0] == 0
          and all(0 <= (p.default or 0) < (p.count or 128) for p in mod.params))
    rep.check('manifest loads (names <= 4 letters, defaults inside their counts, knob 0 = AMT default 0)', ok,
              ' '.join(f'{nm} {d}' for nm, d in zip(names, defaults) if nm))
    bad, dup = A.label_prefix_check(asm)
    rep.check('no label is a prefix of another, no duplicates', not bad and not dup, bad or dup or '')
    src = Path(asm).read_text()
    back = backward_branches(src)
    rep.check('no rep, no parallel move, no backward branch, r7 displacements 0..63',
              A.no_rep(asm) and not back, back or '')
    mems, words, init, proc = A.assemble(asm, key)
    rep.check('assembles; disassembler round-trip + mpysu audit clean; <= 2,048 words', len(words) <= 2048,
              f'{len(words)} words' + (f' ({build_note})' if build_note else ''))
    g = guard
    nul = A.null_cost()
    dp = list(defaults)
    for k, v in dear.items():
        dp[names.index(k)] = v
    tag = key.replace(' ', '_')
    # ---- cost at the dearest settings
    blocks = 6000
    n = blocks * 16
    L, R, m = A.render(tag + '_dear', mems, init, proc, dp, A.noise(n, 0.5), blocks, guard=g)
    worst = m['max_per_frame'] - nul['max_per_frame']
    mean = m['mean_per_frame'] - nul['mean_per_frame']
    rep.check(f'cost at the dearest settings, 0.5 FS noise, {n / FSR:.1f} s (dsp_host meter minus a null effect): '
              f'worst block <= {cost_limit} instructions a sample', worst <= cost_limit,
              f'mean {mean:.1f}, worst block {worst:.1f} instructions a sample '
              f'({" ".join(f"{k} {v}" for k, v in dear.items())})')
    rep.check('dearest settings on 0.5 FS noise: bounded, guard clean (no write outside the buffer window in Y)',
              A.peak(L + R) <= 1.0 + 1e-6 and m['stray'] == 0 and m['clobber'] == 0,
              f"peak {A.peak(L + R):.3f} FS, stray {m['stray']}")
    # ---- AMT 0: exact dry from the first sample, and nearly free
    dry = list(dp)
    dry[0] = 0
    x = ramp(n)
    L, R, m0 = A.render(tag + '_dry', mems, init, proc, dry, x, blocks, guard=g)
    rest = m0['max_per_frame'] - nul['max_per_frame']
    rep.check('AMT 0 from the start: output = input bit for bit (full-range ramp, other knobs at their dearest), '
              'and the routine costs under 2 instructions a sample', L == x and R == x and rest < 2,
              f'max |err| {max(abs(a - b) for a, b in zip(L + R, x + x))} LSB; rest cost {rest:.2f} a sample')
    # ---- dirty state
    worstt = 0
    for fill in (0x7fffff, 0x800000, 0x555555, 0xa5a5a5):
        fm = A.with_fill(mems, fill, f'{tag}_fill{fill:06x}')
        for kn in (defaults, dp):
            Lf, Rf, _ = A.render(tag + '_dirty', fm, init, proc, kn, None, tail_blocks, dirty=0x1234 + fill)
            worstt = max(worstt, max(abs(v) for v in Lf[-300:] + Rf[-300:]))
    rep.check(f'dirty state (garbage r7 block + garbage Y), silence in, AMT 0 and AMT 127: tail <= {tail_lsb} LSB',
              worstt <= tail_lsb, f'worst tail {worstt} LSB after {tail_blocks * 16 / FSR:.1f} s')
    # ---- automation
    rnd = random.Random(7)
    counts = [p.count or 128 for p in mod.params]
    blocks = 3000
    n = blocks * 16
    ev = []
    for b in range(0, blocks, 3):
        v = list(defaults)
        for j, p in enumerate(mod.params):
            if p.active:
                v[j] = rnd.randrange(counts[j])
        ev.append((b, v))
    worstp = 0
    strays = 0
    for split in (0, 7):
        La, Ra, ma = A.render(tag + f'_auto{split}', mems, init, proc, defaults, A.noise(n, 0.7, 3), blocks,
                              events=ev, split=split, guard=g)
        worstp = max(worstp, A.peak(La + Ra))
        strays += ma['stray'] + ma['clobber']
    rep.check('every knob random every 3 calls (AMT too: rest, wake, run), split calls 0 and 7, guard: bounded, '
              'no stray write', worstp <= 1.0 + 1e-6 and strays == 0, f'peak {worstp:.3f} FS, stray {strays}')
    return mod, names, defaults, mems, init, proc, (mean, worst)


def backward_branches(src):
    """Branches whose target label is above them (dsp_asm mis-sizes them), parallel moves."""
    lines = src.splitlines()
    at = {}
    for i, l in enumerate(lines):
        m = __import__('re').match(r'^([A-Za-z_][A-Za-z0-9_]*):', l)
        if m:
            at[m[1]] = i
    bad = []
    import re
    for i, l in enumerate(lines):
        code = l.split(';')[0]
        m = re.match(r'\s+(b[a-z]{2}|bra|bsr|jmp|jsr)\s+>?([A-Za-z_][A-Za-z0-9_]*)\s*$', code)
        if m and m[2] in at and at[m[2]] < i:
            bad.append(f'{m[1]} {m[2]} (line {i + 1})')
        if re.match(r'\s+\w+\s+\S+\s+\S+:\S+,\S+\s*$', code) or re.match(r'\s+\w+\s+\S+\s+\S+,\S+:\S+\s*$', code):
            bad.append(f'parallel move (line {i + 1})')
        m = re.search(r'\(r7\+\$([0-9a-f]+)\)', code)
        if m and int(m[1], 16) > 63:
            bad.append(f'displacement (line {i + 1})')
    return bad


def minute(rep, tag, mems, init, proc, p, guard=True, secs=60, amp=0.9, limit=1.0, quiet_db=None, note=''):
    """Noise for 2/3 of the time, then silence: bounded, and (quiet_db) the end is quiet."""
    blocks = int(secs * FSR / 16)
    n = blocks * 16
    stop = int(n * 2 / 3)
    x = A.noise(stop, amp, 9) + [0] * (n - stop)
    L, R, m = A.render(tag + '_minute', mems, init, proc, p, x, blocks, guard=guard)
    pk = A.peak(L + R)
    endr = A.rms(L[-FSR:] + R[-FSR:])
    sat = sum(1 for v in L + R if abs(v) >= FS - 1)
    ok = pk <= limit + 1e-6 and m['stray'] == 0 and m['clobber'] == 0
    if quiet_db is not None:
        ok = ok and A.db(endr) <= quiet_db
    rep.check(f'{secs} s: {amp} FS noise for {stop / FSR:.0f} s, then silence -- bounded, no stray write'
              + (f', last second under {quiet_db} dBFS' if quiet_db is not None else ''), ok,
              f'peak {pk:.4f} FS, {sat} samples at the output limit, last second {A.db(endr):.1f} dBFS rms, '
              f'max |sample| in it {max(abs(v) for v in L[-FSR:] + R[-FSR:])} LSB, '
              f'cost mean {m["mean_per_frame"] - A.null_cost()["mean_per_frame"]:.1f} / worst block '
              f'{m["max_per_frame"] - A.null_cost()["max_per_frame"]:.1f}' + (f'; {note}' if note else ''))
    return L, R, m


# ---- measurement helpers --------------------------------------------------------------------------
def demod(x, f0, hop, win):
    """Complex amplitude of x at f0 in windows of `win` samples every `hop` (Hann)."""
    import cmath
    out = []
    w = [0.5 - 0.5 * math.cos(2 * math.pi * i / win) for i in range(win)]
    c = [cmath.exp(-2j * math.pi * f0 * i / FSR) for i in range(win)]
    for s in range(0, len(x) - win, hop):
        acc = 0j
        ph = cmath.exp(-2j * math.pi * f0 * s / FSR)
        for i in range(win):
            acc += x[s + i] * w[i] * c[i]
        out.append(acc * ph * 2 / (sum(w) * FS))
    return out


def cents_track(x, f0, hop=256, win=1024):
    """Instantaneous frequency deviation (cents) of a near-f0 tone, one value a hop."""
    import cmath
    z = demod(x, f0, hop, win)
    out = []
    for a, b in zip(z, z[1:]):
        if abs(a) < 1e-6 or abs(b) < 1e-6:
            out.append(0.0)
            continue
        df = cmath.phase(b / a) / (2 * math.pi) * FSR / hop
        out.append(1200 * math.log2(max(1e-9, (f0 + df) / f0)))
    return out, [abs(v) for v in z]


def host_fade(names, defaults, at, v0, v1, secs=0.35, **kw):
    """The host's fade of AMT as events: every step of the 7-bit knob from v0 to v1, spread over `secs`
    (blocks of 16 frames) starting at block `at`."""
    steps = abs(v1 - v0)
    ev = []
    for k in range(1, steps + 1):
        v = v0 + (k if v1 > v0 else -k)
        ev.append((at + round(k * secs * FSR / 16 / steps), P(names, defaults, AMT=v, **kw)))
    return ev
