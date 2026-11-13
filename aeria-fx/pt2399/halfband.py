"""The half-rate wrapper's filters (AERIA BBD; the same file sits in pt2399/): a polyphase IIR halfband pair.

Decimator 44.1 -> 22.05 kHz and interpolator 22.05 -> 44.1 kHz, each the classic two-path allpass halfband
(H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2, elliptic-style coefficients as in de Soras' HIIR): N_COEF first-order allpasses
in z^-2, split between the two paths (A0 takes the even-indexed coefficients). At the half rate each allpass is
    y[n] = c (x[n] - y[n-1]) + x[n-1]
one multiply-pair. N_COEF 5, TBW 0.04: flat (< 0.01 dB) to 10.5 kHz, -3 dB at 11.025 kHz (every two-path halfband),
>= 62 dB down from 12.05 kHz to 22.05 kHz -- for the decimator what would fold into 0..10 kHz, for the interpolator
the images of everything under 10 kHz. Not linear phase: about 2.3 samples (44.1 kHz) of group delay each at low
frequencies, rising to ~7 at 9 kHz.

Decimation: the pair (x[2n], x[2n+1]): A1 takes x[2n] when it arrives (the even sample), A0 takes x[2n+1], and
d[n] = (A0 + A1) / 2. Interpolation: w[n] -> A0(w) is the output at the odd sample (the one the core ran on), A1(w)
the output at the next even sample. So each full-rate sample runs a path per channel per filter, the work spread evenly.

Emitted code (DSP56300, X memory only): a path's state is a chain of K + 1 words s[0..K] (s[k] = the last input of
section k = the last output of section k - 1), its coefficients K words, both walked with post-increment so every
coefficient and state access rides in a multiply's parallel move: 4 instructions a section (5 for the last).
"""
import math

N_COEF = 5
TBW = 0.04


def _coefs(n, tr):
    k = math.tan((1 - tr * 2) * math.pi / 4) ** 2
    kk = (1 - k * k) ** 0.25
    e = 0.5 * (1 - kk) / (1 + kk)
    e2 = e * e
    e4 = e2 * e2
    q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))
    order = 2 * n + 1
    out = []
    for c in range(1, n + 1):
        num, i, j = 0.0, 0, 1
        while True:
            t = q ** (i * (i + 1)) * math.sin((i * 2 + 1) * c * math.pi / order) * j
            num += t
            j, i = -j, i + 1
            if abs(t) < 1e-100:
                break
        den, i, j = 0.0, 1, -1
        while True:
            t = q ** (i * i) * math.cos(i * 2 * c * math.pi / order) * j
            den += t
            j, i = -j, i + 1
            if abs(t) < 1e-100:
                break
        ww = num * q ** 0.25 / (den + 0.5)
        w2 = ww * ww
        x = math.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2)
        out.append((1 - x) / (1 + x))
    return out


C = _coefs(N_COEF, TBW)
A0 = C[0::2]          # the newer sample's path (decimator) / the first output of a pair (interpolator)
A1 = C[1::2]
K0, K1 = len(A0), len(A1)
assert all(0 < c < 1 for c in C)


def response(f, fs=44100.0):
    """|H| of the halfband (decimator or interpolator, both the same H) at f Hz."""
    import cmath
    z = cmath.exp(2j * math.pi * f / fs)
    z2 = z * z
    p0 = p1 = 1
    for c in A0:
        p0 *= (c + 1 / z2) / (1 + c / z2)
    for c in A1:
        p1 *= (c + 1 / z2) / (1 + c / z2)
    return abs(0.5 * (p0 + p1 / z))


class _Path:
    def __init__(self, cs):
        self.cs, self.s = cs, [0.0] * (len(cs) + 1)

    def __call__(self, v):
        for k, c in enumerate(self.cs):
            y = c * (v - self.s[k + 1]) + self.s[k]
            self.s[k] = v
            v = y
        self.s[-1] = v
        return v


def decimate(x):
    """float model: x at 44.1 kHz (even length) -> 22.05 kHz."""
    p0, p1 = _Path(A0), _Path(A1)
    return [0.5 * (p0(x[2 * n + 1]) + p1(x[2 * n])) for n in range(len(x) // 2)]


def interpolate(w):
    """float model: w at 22.05 kHz -> 44.1 kHz (A0 first, then A1, per input sample)."""
    p0, p1 = _Path(A0), _Path(A1)
    out = []
    for v in w:
        out += [p0(v), p1(v)]
    return out


def path(A, K, acc='b', pre=False, st='r3', cf='r5'):
    """One path. Pre: x0 = its input, st -> s[0], cf -> its first coefficient (pre=True: those two already loaded,
    y0 = c[0] with cf past it, acc = s[0]). Post: x0 = its output (also stored in s[K]), st and cf past this path's
    words. Clobbers a b x1 y0. (st, cf: address registers with linear addressing; the BBD uses r3 / r5.)"""
    accs = (acc, 'a' if acc == 'b' else 'b')
    if not pre:
        A('move', f'x:({cf})+', 'y0')
        A('move', f'x:({st})', accs[0])
    for k in range(K):
        ac, nx = accs[k % 2], accs[(k + 1) % 2]
        A.p(f'mac     y0,x0,{ac}', f'x0,x:({st})+')
        A('move', f'x:({st})', 'x1')
        if k < K - 1:
            A.p(f'mac     -x1,y0,{ac}', f'x:({cf})+,y0')
            A.p(f'tfr     x1,{nx}', f'{ac},x0')
        else:
            A('mac', '-x1', 'y0', ac)
            A('move', ac, 'x0')
            A('move', 'x0', f'x:({st})+')
