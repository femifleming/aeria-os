#!/usr/bin/env python3
"""Decode an AERIA DUMP capture (aeria-fx/head/dump.asm) back into the DSP's peripheral registers.

    dump_decode.py capture.wav [--ch N]      (N = the 1-based USB channel carrying the DUMP track)

Finds the sync (128 samples of 0, then 128 of REF), then reads each register's 8 symbols of 16 samples:
REF, six nibbles as (nibble+1)/16 of REF, 0. The middle 8 samples of a symbol are averaged; any gain cancels.
"""
import argparse, sys
import numpy as np

sys.path.insert(0, __import__('os').path.expanduser('~/octa/tools'))
import listen

ADDR = ([('x', 0xffff90 + i) for i in range(16)] + [('x', 0xffffb3 + i) for i in range(13)] +
        [('x', 0xffffc2 + i) for i in range(4)] + [('x', 0xffffc8 + i) for i in range(56)] +
        [('y', 0xffff90 + i) for i in range(16)] + [('y', 0xffffc0 + i) for i in range(16)] +
        [('y', 0xffffe0 + i) for i in range(32)])
assert len(ADDR) == 153


def decode(x):
    """x: one channel, float. Returns {(space, addr): value} from the first complete cycle."""
    a = np.abs(x)
    peak = np.percentile(a, 99.5)
    if peak <= 0:
        raise SystemExit('the channel is silent')
    quiet = a < peak * 0.01
    # the sync: >= 120 quiet samples, then >= 120 samples near one level (REF)
    run = 0
    for i in range(len(x) - 300):
        run = run + 1 if quiet[i] else 0
        if run >= 120 and not quiet[i + 1]:
            ref_block = x[i + 1:i + 129]
            ref = np.median(ref_block)
            if abs(ref) > peak * 0.3 and np.std(ref_block) < abs(ref) * 0.02:
                start = i + 1 + 128
                break
    else:
        raise SystemExit('no sync found')
    regs = {}
    for k, key in enumerate(ADDR):
        base = start + k * 128
        if base + 128 > len(x):
            raise SystemExit('the capture ends inside the cycle: record longer')
        sym = [np.mean(x[base + 16 * s + 4: base + 16 * s + 12]) for s in range(8)]
        r = sym[0]
        if abs(r) < peak * 0.3:
            raise SystemExit(f'lost the reference at register {k}')
        v = 0
        for s in range(1, 7):
            n = int(round(sym[s] / r * 16)) - 1
            v = (v << 4) | max(0, min(15, n))
        regs[key] = v
    return regs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--ch', type=int, default=15)
    a = ap.parse_args()
    d, sr = listen.load(a.wav)
    regs = decode(d[:, a.ch - 1])
    for (sp, ad), v in regs.items():
        print(f'{sp}:${ad:06x} = ${v:06x}')


if __name__ == '__main__':
    main()
