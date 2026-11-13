"""FOG gate: tools/run.sh python3 /aeria/fog/test.py   (writes fog/test_results.md)

fog_test.asm (rings at base + $000/$400/$1000, for dsp_host's buffer at Y:$4000) is rendered;
fog.asm (rings at base + $000/$200/$600, for Y:$8a00) is assembled and compared with it."""
import cmath
import math
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/fog')
import aeria as A
import gen as G
import wxtest as T

FSR = 44100
for b in G.BUILDS:
    G.build(b)
rep = A.Report('FOG')
DEAR = {'AMT': 127, 'BLUR': 127, 'DARK': 127}
mod, names, d, mems, init, proc, cost = T.standard(
    rep, '/aeria/fog/fog_test.asm', 'AERIA FOG', '/aeria/fog/manifest.py', DEAR, guard=0x2000, cost_limit=130,
    tail_lsb=64, build_note='test layout')
ON = T.P(names, d, AMT=127)
GUARD = 0x2000

# ---- 0. the two builds are one source ----------------------------------------------------------------
full = [l for l in Path('/aeria/fog/fog.asm').read_text().splitlines() if not l.startswith(';')]
test = [l for l in Path('/aeria/fog/fog_test.asm').read_text().splitlines() if not l.startswith(';')]
df = sorted({f"{a.split(';')[0].strip()} -> {b.split(';')[0].strip()}" for a, b in zip(full, test) if a != b})
fw = A.assemble('/aeria/fog/fog.asm', 'AERIA FOGFULL')[1]
tw = A.assemble('/aeria/fog/fog_test.asm', 'AERIA FOG')[1]
dw = sum(1 for a, b in zip(fw, tw) if a != b)
rep.check('fog.asm (the hardware layout) assembles (round-trip, mpysu audit); it differs from the rendered test build '
          'only in ring offsets and the clearing count', len(fw) == len(tw) and len(full) == len(test) and dw <= 6,
          f'{len(fw)} words; {dw} code words differ: ' + '; '.join(df))
lay = []
for out in G.BUILDS:
    base = 0x8a00 if out == 'fog.asm' else 0x4000
    ok = True
    for r in 'CBA':
        a0 = base + G.OFFSETS[out][r]
        ok &= a0 % G.RING_ALIGN[r] == 0 and a0 + G.RING_WORDS[r] <= base + G.TOTAL[out]
    lay.append(f'{out} at Y:${base:04x}: ' + ', '.join(
        f'{r} ${base + G.OFFSETS[out][r]:04x}..${base + G.OFFSETS[out][r] + G.RING_WORDS[r] - 1:04x}' for r in 'CBA')
        + f' (claims ${base:04x}..${base + G.TOTAL[out] - 1:04x})')
    assert ok, out
rep.check('ring layout: each ring starts on a multiple of its reserved size and ends inside the claim', True, '; '.join(lay))


def run(tag, p, x, blocks, **kw):
    L, R, m = A.render('fg_' + tag, mems, init, proc, p, x, blocks, guard=GUARD, **kw)
    assert m['stray'] == 0 and m['clobber'] == 0, tag
    return L, R, m


def blockcost(tag):
    rows = [[int(v) for v in l.split()] for l in (A.BUILD / f'fg_{tag}.meter').read_text().splitlines()]
    nul = A.null_cost()['max_per_frame']
    return [r[1] / 16 - nul for r in rows]


# ---- the plugin in float (one channel), from MoreWeather.h ---------------------------------------------
def model(x, g=G.G_PLUGIN, hz=G.HZ_FOG, w=1.0):
    lines = [[0.0] * n_ for n_ in G.LEN]
    pos = [0] * 6
    a = G.lp_coef(hz)
    lp = 0.0
    out = []
    for v0 in x:
        v = v0
        for k in range(6):
            dl = lines[k][pos[k]]
            ww = v + g * dl
            lines[k][pos[k]] = ww
            pos[k] = (pos[k] + 1) % G.LEN[k]
            v = dl - g * ww
        lp += a * (v - lp)
        out.append(v0 + w * (lp - v0))
    return out


WARM = 16000           # samples: the clearing calls and the glide of the knob (23 ms) are long over
NIR = 16384


def impulse_response(tag, p, amp=0.5):
    blocks = (WARM + NIR) // 16 + 1
    x = A.impulse(blocks * 16, at=WARM, amp=amp)
    L, R, _ = run(tag, p, x, blocks)
    return [v / (amp * A.FS) for v in L[WARM:WARM + NIR]], [v / (amp * A.FS) for v in R[WARM:WARM + NIR]]


def mag(h, f):
    return abs(sum(v * cmath.exp(-2j * math.pi * f * i / FSR) for i, v in enumerate(h) if v))


def minus3(h, ref=200.0):
    """Frequency where |H| is 3.01 dB under |H(ref)| (bisection on the impulse response)."""
    m0 = mag(h, ref)
    lo, hi = ref, 21000.0
    if mag(h, hi) > m0 * 0.7079:
        return None
    for _ in range(22):
        mid = math.sqrt(lo * hi)
        if mag(h, mid) > m0 * 0.7079:
            lo = mid
        else:
            hi = mid
    return math.sqrt(lo * hi)


def spread(h):
    """ms by which 10 / 50 / 90 % of the energy has arrived."""
    e = [v * v for v in h]
    tot = sum(e)
    acc, out, marks = 0.0, [], [0.1, 0.5, 0.9]
    for i, v in enumerate(e):
        acc += v
        while marks and acc >= marks[0] * tot:
            out.append(i / FSR * 1000)
            marks.pop(0)
    return out


# ---- 1. full fog: the impulse response is the plugin's ----------------------------------------------------
hL, hR = impulse_response('ir', ON)
hm = model([1.0] + [0.0] * (NIR - 1))
err = max(abs(a - b) for a, b in zip(hL, hm))
t10, t50, t90 = spread(hL)
m10, m50, m90 = spread(hm)
late = math.sqrt(sum(v * v for v in hL[int(0.4 * FSR):]) / max(1e-30, sum(v * v for v in hL)))
rep.check('AMT 127, plugin settings: the impulse response is the float plugin\'s (six allpasses g = 0.6 at '
          f'{", ".join(str(v) for v in G.LEN)} frames, then the lowpass), both sides alike',
          err < 2e-4 and hL == hR,
          f'max |difference| {err:.2e} of the impulse; L == R {hL == hR}')
rep.check('attack blur: where the energy of one click lands (plugin: about a tenth of a second of smear, no tail)',
          abs(t50 - m50) < 1 and abs(t90 - m90) < 2 and late < 0.03,
          f'10 % by {t10:.1f} ms, half by {t50:.1f} ms, 90 % by {t90:.1f} ms (float plugin {m10:.1f} / {m50:.1f} / {m90:.1f}); '
          f'{100 * late * late:.3f} % of the energy later than 400 ms; first sample {hL[0]:+.4f} (g^6 x lowpass = '
          f'{G.G_PLUGIN ** 6 * G.lp_coef(G.HZ_FOG):+.4f})')
f3 = minus3(hL)
f3m = minus3(hm)
flat = [20 * math.log10(mag(hL, f) / mag(hL, 200.0)) for f in (50, 100, 400, 800)]
rep.check('the highs sink: -3 dB point of the full fog (the allpasses are flat; a one-pole at 4.5 kHz)',
          abs(f3 - f3m) < 30 and 4000 < f3 < 5200 and max(abs(v) for v in flat) < 0.3,
          f'-3 dB at {f3:.0f} Hz (float plugin {f3m:.0f} Hz); 10 kHz {20 * math.log10(mag(hL, 10000) / mag(hL, 200)):+.1f} dB, '
          f'16 kHz {20 * math.log10(mag(hL, 16000) / mag(hL, 200)):+.1f} dB; 50..800 Hz within {max(abs(v) for v in flat):.2f} dB')

# ---- 2. BLUR and DARK -----------------------------------------------------------------------------------------
rows, ok = [], True
for bl in (0, 50, 127):
    h, _ = impulse_response(f'bl{bl}', T.P(names, d, AMT=127, BLUR=bl))
    hm_ = model([1.0] + [0.0] * (NIR - 1), g=G.blur_g(bl))
    e = max(abs(a - b) for a, b in zip(h, hm_))
    s10, s50, s90 = spread(h)
    ok &= e < 3e-4
    rows.append(f'BLUR {bl} (g {G.blur_g(bl):.3f}): half the energy by {s50:.1f} ms, 90 % by {s90:.1f} ms, '
                f'max |difference| from the float model {e:.1e}')
rep.check('BLUR 0 / 50 / 127: the float model with g = 0.6 v/100 (0 = the six lines as one 61.5 ms delay)', ok, '; '.join(rows))
rows, ok = [], True
for dk in (0, 32, 64, 96, 127):
    h, _ = impulse_response(f'dk{dk}', T.P(names, d, AMT=127, DARK=dk))
    f = minus3(h)
    want = minus3(model([1.0] + [0.0] * (NIR - 1), hz=G.dark_hz(dk)))
    ok &= (f is None and want is None) or (f is not None and want is not None and abs(f / want - 1) < 0.02)
    rows.append(f'DARK {dk} ({G.dark_hz(dk):.0f} Hz): -3 dB at ' + ('none below 21 kHz' if f is None else f'{f:.0f} Hz')
                + ' (model ' + ('none' if want is None else f'{want:.0f}') + ')')
rep.check('DARK 0 .. 127: the lowpass at full fog is 18 kHz x 2^-(v/32) within 2 %', ok, '; '.join(rows))
# half way: amt = 64/127 -> mix = smoothstep(0.756), lowpass 18 kHz x 2^-(amt x 2)
amt = 64 / 127
xs = min(1.0, 1.5 * amt)
wmix = xs * xs * (3 - 2 * xs)
h, _ = impulse_response('half', T.P(names, d, AMT=64))
hm_ = model([1.0] + [0.0] * (NIR - 1), hz=G.dark_hz(64, amt), w=wmix)
e = max(abs(a - b) for a, b in zip(h, hm_))
rep.check('AMT 64: mix = smoothstep(1.5 amt) and the lowpass half way down (float model of the plugin at amt = 0.504)',
          e < 1e-3, f'mix {wmix:.4f}, lowpass {G.dark_hz(64, amt):.0f} Hz; max |difference| {e:.1e} (dry tap {h[0]:.4f}, model {hm_[0]:.4f})')

# ---- 3. AMT 127 -> 0 -> 127 ------------------------------------------------------------------------------------
secs = 6
blocks = secs * FSR // 16
n = blocks * 16
off = 2 * FSR // 16
on2 = 4 * FSR // 16
fq = 441.0
x = A.sine(fq, 0.5, 3 * FSR) + [0] * (n - 3 * FSR)
ev = [(0, ON)] + T.host_fade(names, d, off, 127, 0) + [(on2, ON)]
L, R, _ = run('fade', ON, x, blocks, events=ev)
bc = blockcost('fade')
same = next((i for i in range(off * 16, n) if all(L[j] == x[j] and R[j] == x[j] for j in range(i, min(i + 4000, on2 * 16)))), None)
rest = next((b for b in range(off, on2) if bc[b] < 2), None)
tail = max(abs(v) for v in L[on2 * 16:] + R[on2 * 16:])
sl = 2 * math.pi * fq / FSR
seg = [v / A.FS for v in L[off * 16 - 2000: off * 16 + 30000]]
d2 = max(abs(a - 2 * b + c) for a, b, c in zip(seg[2:], seg[1:], seg)) / (sl * sl * max(abs(v) for v in seg))
rep.check('AMT 127 -> 0 as the host fades it (every knob step, 0.35 s) on a 441 Hz sine: no click, the output comes back '
          'to the input bit for bit, the routine goes to rest; silence in and AMT up again 2 s later: nothing of the '
          'old sound (the lines were cleared)',
          same is not None and rest is not None and tail <= 64 and d2 < 1.15,
          f'bit-exact from {(same - off * 16) / FSR * 1000:.0f} ms after the fade began, at rest (cost {bc[rest]:.2f} a sample) from '
          f'{(rest - off) * 16 / FSR * 1000:.0f} ms; largest second difference on the way {d2:.3f} x a sine\'s; '
          f'after the second start at most {tail} LSB')

# ---- 4. silence while the fog is on ------------------------------------------------------------------------------
blocks = 4 * FSR // 16
n = blocks * 16
x = A.noise(FSR, 0.9, 3) + [0] * (n - FSR)
L, R, _ = run('quiet', ON, x, blocks)
res = max(abs(v) for v in L[-FSR:] + R[-FSR:])
nz = [i for i in range(n) if L[i] or R[i]]
zero = (nz[-1] + 1 - FSR) / FSR if nz else 0.0
mid = max(abs(v) for v in L[2 * FSR: 2 * FSR + FSR // 2] + R[2 * FSR: 2 * FSR + FSR // 2])
rep.check('0.9 FS noise for 1 s, then silence, fog still on: the lines store w = v + g x delayed floored and can hold '
          '-1 or -2 LSB of the /8 scale for good (DC of a few tens of LSB, under -100 dBFS); after 1.5 s of input that '
          'is exactly 0 each all-0 call is skipped, so the tail ends in exact silence (quality pass, 12 Nov: it used to '
          'keep that DC for good)', res == 0 and mid <= 64 and zero <= 1.51,
          f'1 .. 1.5 s after the input: at most {mid} LSB ({A.db(max(mid, 1) / A.FS):.0f} dBFS); exactly 0 from '
          f'{zero:.2f} s after it; last second at most {res} LSB')

# ---- 4b. quality pass (12 Nov): BLUR glides (1/8 every second call) and the allpass gain ramps a sample ---------
import random as _r
fz = 200.0
blocks = 4 * FSR // 16
n = blocks * 16
rnd = _r.Random(3)
rows = []
hfs = {}
for tag in ('still', 'sweep', 'jumps'):
    ev = []
    b, k = FSR // 16, 0
    while b < blocks and tag != 'still':
        if tag == 'sweep':
            ev.append((b, T.P(names, d, AMT=127, DARK=0, BLUR=127 - abs(127 - k % 254))))
            b += 11
        else:
            ev.append((b, T.P(names, d, AMT=127, DARK=0, BLUR=rnd.choice((0, 127)) if k % 2 == 0 else rnd.randrange(128))))
            b += int(0.1 * FSR / 16)
        k += 1
    L, R, _ = run('blur' + tag, T.P(names, d, AMT=127, DARK=0, BLUR=0), A.sine(fz, 0.5, n), blocks, events=ev)
    hf = -999.0
    for ch in (L[FSR:], R[FSR:]):
        d3 = [a - 3 * b_ + 3 * c - e for a, b_, c, e in zip(ch[3:], ch[2:], ch[1:], ch)]
        hf = max(hf, 10 * math.log10(sum(v * v for v in d3) / sum(v * v for v in ch)))
    hfs[tag] = hf
    rows.append(f'{tag}: {hf:.1f} dB')
rep.check('BLUR moved on a 200 Hz 0.5 FS sine (AMT 127, DARK 0): swept 0..127..0 a knob step every 11 calls, and random '
          'jumps every 0.1 s -- third-difference energy (where clicks show) re the signal; still: a clean sine\'s -92 dB. '
          'Before the quality pass: sweep -46.4, jumps -25.5 dB', hfs['sweep'] < -75 and hfs['jumps'] < -50,
          '; '.join(rows))

# ---- 5. a minute of noise and silence -----------------------------------------------------------------------------
T.minute(rep, 'fg', mems, init, proc, T.P(names, d, **DEAR), guard=GUARD, quiet_db=-100)
T.minute(rep, 'fgp', mems, init, proc, ON, guard=GUARD, amp=1.0, quiet_db=-100, note='plugin settings, full-scale noise')

rep.note('size', f'{len(tw)} words of P; r7 state {len(G.ST.at)} words; Y {G.TOTAL["fog.asm"]} words '
                 f'(rings {G.RING_WORDS["C"]} + {G.RING_WORDS["B"]} + {G.RING_WORDS["A"]} used)')
rep.note('cost', f'dsp_host mean {cost[0]:.1f}, worst block {cost[1]:.1f} instructions a sample at the dearest settings; '
                 'at rest (AMT 0) 7 instructions a call')
rep.save('/aeria/fog/test_results.md')
