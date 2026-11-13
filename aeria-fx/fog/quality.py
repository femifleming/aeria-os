"""FOG quality probe: tools/run.sh python3 /aeria/fog/quality.py <test-layout asm>"""
import math
import random
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/fog')
import aeria as A
import wxtest as T

FSR = 44100
mems, words, init, proc = A.assemble(sys.argv[1], 'AERIA FOG')
names = ['AMT', 'BLUR', 'DARK'] + [''] * 9
d = [0, 100, 64] + [0] * 9


def hf(x, f0):
    """energy of x after a 4th difference (clicks), dB re the sine's own 4th difference."""
    e = [a - 4 * b + 6 * c - 4 * dd + ee for a, b, c, dd, ee in zip(x[4:], x[3:], x[2:], x[1:], x)]
    return max(abs(v) for v in e)


def hfdb(ch):
    """third-difference energy re the signal, dB (where clicks show; a clean 200 Hz sine: -92 dB)"""
    d3 = [a - 3 * b + 3 * c - e for a, b, c, e in zip(ch[3:], ch[2:], ch[1:], ch)]
    return 10 * math.log10(sum(v * v for v in d3) / sum(v * v for v in ch))


def blur(tag, jumps):
    secs = 4
    blocks = secs * FSR // 16
    n = blocks * 16
    f = 200.0
    x = A.sine(f, 0.5, n)
    rnd = random.Random(3)
    ev = []
    b = FSR // 16
    k = 0
    while b < blocks:
        if jumps is None:
            break
        if jumps:
            v = rnd.choice((0, 127)) if k % 2 == 0 else rnd.randrange(128)
            ev.append((b, T.P(names, d, AMT=127, DARK=0, BLUR=v)))
            b += int(0.1 * FSR / 16)
        else:
            ev.append((b, T.P(names, d, AMT=127, DARK=0, BLUR=127 - abs(127 - k % 254))))
            b += 11
        k += 1
    L, R, m = A.render(tag, mems, init, proc, T.P(names, d, AMT=127, DARK=0, BLUR=0), x, blocks, events=ev,
                       guard=0x2000)
    s0 = FSR
    sd = 0.5 * A.FS * (2 * math.pi * f / FSR) ** 4 * 16 / 16
    # a clean sine's 4th difference: A w^4
    return (max(hf(ch[s0:], f) for ch in (L, R)) / (0.5 * A.FS * (2 * math.pi * f / FSR) ** 4),
            max(hfdb(ch[s0:]) for ch in (L, R)))


print(f'{len(words)} words')
for tag, j in (('still (BLUR 0)', None), ('BLUR random jumps every 0.1 s', True), ('BLUR swept 0..127 a step every 11 calls', False)):
    r = blur('fgq_b' + str(j), j)
    print(f'{tag}, 200 Hz 0.5 FS sine, DARK 0: largest 4th difference {r[0]:.0f} x a clean sine\'s; third-difference energy {r[1]:.1f} dB re the signal')
blocks = 4 * FSR // 16
n = blocks * 16
x = A.noise(FSR, 0.9, 3) + [0] * (n - FSR)
for tag, p in (('plugin', T.P(names, d, AMT=127)), ('dear', T.P(names, d, AMT=127, BLUR=127, DARK=127))):
    L, R, m = A.render('fgq_q' + tag, mems, init, proc, p, x, blocks, guard=0x2000)
    last = L[-FSR:] + R[-FSR:]
    nz = [i for i in range(n) if L[i] or R[i]]
    z = (nz[-1] + 1) if nz and nz[-1] < n - 1 else None
    print(f'{tag}: 0.9 FS noise 1 s then silence: last second max {max(abs(v) for v in last)} LSB; '
          f'exactly 0 from {"never" if z is None else f"{(z - FSR) / FSR:.2f} s after the input stops"}')
