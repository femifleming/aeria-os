"""AURORA quality probe: tools/run.sh python3 /aeria/aurora/quality.py <test asm>"""
import math
import sys
sys.path.insert(0, '/aeria/tools')
sys.path.insert(0, '/aeria/aurora')
import aeria as A
import gen as G
import wxtest as T

FSR = 44100
mems, words, init, proc = A.assemble(sys.argv[1], 'AERIA AURORA')
names = ['AMT', 'TIME', 'TONE', 'SHFT'] + [''] * 8
d = [0, 64, 64, 1] + [0] * 8
LPS = [G.ST[f'LP{j}'] for j in range(4)]
print(f'{len(words)} words')


def lines(tag, p, x, blocks):
    tr = A.BUILD / f'auq_{tag}.track'
    L, R, m = A.render('auq_' + tag, mems, init, proc, p, x, blocks, guard=True,
                       extra=['-track', ','.join(f'{v:x}' for v in LPS), '-trackout', str(tr)])
    pk = 0
    sat = 0
    for l in tr.read_text().splitlines():
        for v in map(int, l.split()[1:]):
            v = v - (1 << 24) if v & 0x800000 else v
            pk = max(pk, abs(v))
            sat += abs(v) >= 0x7f0000
    return L, R, m, pk / A.FS, sat


for tag, p, amp, secs in (('plug25', T.P(names, d, AMT=127), 0.25, 20),
                          ('plug90', T.P(names, d, AMT=127), 0.9, 20),
                          ('dear100', T.P(names, d, AMT=127, TIME=127, TONE=127, SHFT=3), 1.0, 40),
                          ('t127s1', T.P(names, d, AMT=127, TIME=127, TONE=127, SHFT=1), 1.0, 40)):
    blocks = int(secs * FSR / 16)
    n = blocks * 16
    x = A.noise(n, amp, 9)
    L, R, m, pk, sat = lines(tag, p, x, blocks)
    wet = [a - b for a, b in zip(L[n // 2:], x[n // 2:])]
    print(f'{tag}: {amp} FS noise {secs} s: line (damped read) peak {pk:.3f} of the line scale, blocks with a line '
          f'within 1 % of full scale {sat}; wet rms (2nd half) {A.db(A.rms(wet)):.1f} dBFS; cost worst '
          f'{m["max_per_frame"] - A.null_cost()["max_per_frame"]:.1f}')

# aliasing of the shift (whole-frame taps, no anti-alias filter)
for shft, fin in () if len(sys.argv) > 2 else ((1, 15000.0), (2, 9000.0), (3, 7000.0), (3, 9000.0)):
    secs = 3
    blocks = int(secs * FSR / 16)
    n = blocks * 16
    x = A.sine(fin, 0.5, n)
    L, R, m = A.render(f'auq_al{shft}', mems, init, proc, T.P(names, d, AMT=127, SHFT=shft), x, blocks, guard=True)
    wet = [a - b for a, b in zip(L[FSR:], x[FSR:])]
    fo = (fin * (shft + 1 if shft < 2 else (3 if shft == 2 else 4))) % FSR
    fo = FSR - fo if fo > FSR / 2 else fo
    ref = A.sine(1000.0, 0.5, n)
    L2, R2, _ = A.render(f'auq_al{shft}r', mems, init, proc, T.P(names, d, AMT=127, SHFT=shft), ref, blocks,
                         guard=True)
    w2 = [a - b for a, b in zip(L2[FSR:], ref[FSR:])]
    print(f'SHFT {shft}: {fin:.0f} Hz in -> alias at {fo:.0f} Hz: wet rms {A.db(A.rms(wet)):.1f} dBFS '
          f'(a 1 kHz tone of the same level: wet {A.db(A.rms(w2)):.1f} dBFS)')
