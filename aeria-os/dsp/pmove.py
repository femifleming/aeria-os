#!/usr/bin/env python3
"""dsp_asm has no `movem`: engine.asm writes `move x0,x:>$1c` / `>$1d` (447000 00001c/1d) where it means
`movem x0,p:$1c` / `$1d` (077084 ...). Patch those two (and only those) in the binary, in place."""
import sys
p = sys.argv[1]; b = bytearray(open(p, 'rb').read())
w = [b[i] | b[i + 1] << 8 | b[i + 2] << 16 for i in range(0, len(b), 3)]
n = 0
for i in range(len(w) - 1):
    if w[i] == 0x447000 and w[i + 1] in (0x1c, 0x1d):
        w[i] = 0x077084; n += 1
assert n == 2, n
for i, v in enumerate(w): b[3 * i:3 * i + 3] = bytes([v & 0xff, v >> 8 & 0xff, v >> 16])
open(p, 'wb').write(b); print('pmove: patched', n)
