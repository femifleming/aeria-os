"""Anti-alias low-pass for the pitch-up paths of PITCH and GRAINS (13 Nov 2026, "stop aliasing").

Pure python (runs in the build image, no numpy). A read head that runs `ratio` x as fast as the write folds
everything above fs / (2 ratio) back down; the line / ring is therefore fed through an order-3 elliptic-type
low-pass whose corner tracks the shift:

    w = B x + 2 A1H w1 + A2 w2        (biquad poles; w rounded)
    u = w + 2 CH w1 + K w2            (a pair of zeros on the unit circle)
    v = u + P v1                      (real pole; v rounded)
    y = v + K v1                      (zero at Nyquist)          y = H x, H = 1 at DC

K = 1 while shifting up, 0 at or below unison (B = 1, the rest 0: y = x).

The family: ONE analog prototype (pole pair w0 / Q, zero pair wz, real pole sg), normalised to the stop edge, put
through the bilinear transform with the stop edge at f_s = min(28,100 / ratio, 18,500) Hz: everything at or above
f_s lands under 16 kHz after the shift (fs - 16 kHz = 28.1 kHz), and the same analog shape at every ratio means the
same attenuation (>= 37 dB for what lands under 16 kHz, loss <= 1 dB up to 16 kHz out at +12, 14 kHz out at +24).
Under +7.24 semitones the edge stays at 18.5 kHz (nothing lands under 16 kHz any more; the filter passes ~14.5 kHz).

On the DSP A1H, A2, CH and P are degree-4 polynomials in t = (s/32 - UC) / UW (s = semitones up, clamped to
[SMIN, SMAX]): Horner, only in a block where s changed. POLY[k] holds the polynomial coefficients / 2 (Q23). B is then
divided out so that the DC gain is exactly 1: B = (1 - 2 A1H - A2)(1 - P) / (4 (1 + CH)).
"""
import math

FS = 44100.0
PROTO = (0.46057287057136087, 2.387494288141222, 1.138221417196655, 0.24154081751381296)   # w0, Q, wz, sg
LAND = 16000.0              # aliases landing under this are the ones removed
FSTOP_MAX = 18500.0
SMIN = 12 * math.log2((FS - LAND) / FSTOP_MAX)      # 7.24 semitones: below it the filter is held
SMAX = 25.5
UC = (SMIN + SMAX) / 2 / 32
UW = (SMAX - SMIN) / 2 / 32
DEG = 4
NAMES = ('B', 'A1H', 'A2', 'CH', 'P')


def stop_edge(ratio):
    return min((FS - LAND) / ratio, FSTOP_MAX)


def sections(s):
    """semitones up (> 0) -> ((b0, b1, b2), (a0, a1, a2)), ((b0, b1), (a0, a1)): the digital filter, unity at DC"""
    s = min(max(s, SMIN), SMAX)
    w0, Q, wz, sg = PROTO
    T = 1 / math.tan(math.pi * stop_edge(2 ** (s / 12)) / FS)

    def bil2(c2, c1, c0):
        return (c2 * T * T + c1 * T + c0, 2 * (c0 - c2 * T * T), c2 * T * T - c1 * T + c0)
    num = bil2(1, 0, wz * wz)
    den = bil2(1, w0 / Q, w0 * w0)
    a = tuple(v / den[0] for v in den)
    nn = tuple(v / num[0] for v in num)
    g = sum(a) / sum(nn)
    p = (T - sg) / (T + sg)
    return (tuple(g * v for v in nn), a), (((1 - p) / 2, (1 - p) / 2), (1.0, -p))


def words(s):
    """semitones up -> (B, A1H, A2, CH, P) as fractions (B for unity gain on x)"""
    (b, a), ((f0, _), (_, fa1)) = sections(s)
    c = b[1] / b[0]
    p = -fa1
    return ((1 + a[1] + a[2]) / (2 + c) * (1 - p) / 2, -a[1] / 2, -a[2], c / 2, p)


def tvar(s):
    return (s / 32 - UC) / UW


def _fit():
    pts = [SMIN + (SMAX - SMIN) * i / 399 for i in range(400)]
    ts = [tvar(s) for s in pts]
    ws = [words(s) for s in pts]
    out = []
    for k in range(1, 5):
        M = [[sum(t ** (i + j) for t in ts) for j in range(DEG + 1)] for i in range(DEG + 1)]
        v = [sum(t ** i * w[k] for t, w in zip(ts, ws)) for i in range(DEG + 1)]
        n = DEG + 1
        for i in range(n):
            piv = max(range(i, n), key=lambda r: abs(M[r][i]))
            M[i], M[piv] = M[piv], M[i]
            v[i], v[piv] = v[piv], v[i]
            for r in range(n):
                if r != i:
                    f = M[r][i] / M[i][i]
                    M[r] = [x - f * y for x, y in zip(M[r], M[i])]
                    v[r] -= f * v[i]
        out.append([v[i] / M[i][i] for i in range(n)])
    return out


COEF = _fit()                       # COEF[k][i]: word k + 1 (A1H A2 CH P) = sum_i COEF[k][i] t^i


def q23(x):
    v = round(x * (1 << 23))
    return max(-(1 << 23), min((1 << 23) - 1, v))


POLY = [[q23(c / 2) for c in row] for row in COEF]      # the DSP's constants: coefficient / 2


def dsp_words(s):
    """what the DSP computes (Horner in Q23 on t, coefficients / 2, x 2 at the end), as Q23 ints;
    s <= 0: the bypass (B = 1 - 2^-23)"""
    if s <= 0:
        return ((1 << 23) - 1, 0, 0, 0, 0, 0)
    s = min(max(s, SMIN), SMAX)
    t = q23(tvar(s)) / (1 << 23)
    out = []
    for row in POLY:
        acc = row[DEG] / (1 << 23)
        for i in range(DEG - 1, -1, -1):
            acc = acc * t + row[i] / (1 << 23)
        out.append(q23(2 * acc))
    a1h, a2, ch, p = (v / (1 << 23) for v in out)
    b = q23((1 - 2 * a1h - a2) / 8 * (1 - p) / ((1 + ch) / 2))
    return (b,) + tuple(out) + ((1 << 23) - 1,)


def response_db(wd, f):
    """|H| in dB of the DSP's words at f Hz"""
    B, A1H, A2, CH, P, K = (v / (1 << 23) for v in wd)
    z = complex(math.cos(2 * math.pi * f / FS), -math.sin(2 * math.pi * f / FS))
    h = B * (1 + 2 * CH * z + K * z * z) / (1 - 2 * A1H * z - A2 * z * z) * (1 + K * z) / (1 - P * z)
    return 20 * math.log10(abs(h) + 1e-30)


if __name__ == '__main__':
    print(f'SMIN {SMIN:.3f} SMAX {SMAX} UC {UC:.6f} UW {UW:.6f} 1/UW {1 / UW:.6f}')
    for k, row in enumerate(POLY):
        print(NAMES[k + 1], ' '.join(f'${v & 0xffffff:06x}' for v in row), [round(c, 5) for c in COEF[k]])
    worst = [-99, 0, 0]
    for i in range(1, 260):
        s = i / 10
        wd = dsp_words(s)
        r = 2 ** (s / 12)
        al = max(response_db(wd, f) for f in range(int(stop_edge(r)), 22050, 25))
        pb = min(response_db(wd, f) for f in range(20, int(min(16000 if r <= 2 else 16000 - 2000 * math.log2(r / 2), 21000) / r), 25))
        worst = [max(worst[0], al), min(worst[1], pb), max(worst[2], abs(response_db(wd, 1)))]
    print('0.1 .. 25.9 semitones: stopband (lands < 16 kHz) worst %.1f dB, passband worst %.2f dB, DC error %.3f dB' % tuple(worst))
    for s in (1, 7, 12, 19, 24, 25.25):
        wd = dsp_words(s)
        r = 2 ** (s / 12)
        print(s, ' '.join(f'{v / (1 << 23):+.5f}' for v in wd), 'DC', round(response_db(wd, 1), 3),
              'at fs/(2r)', round(response_db(wd, FS / 2 / r), 1), 'at stop', round(response_db(wd, stop_edge(r)), 1))
