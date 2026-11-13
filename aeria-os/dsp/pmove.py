#!/usr/bin/env python3
"""dsp_asm has no `movem`: engine.asm writes `move x0,x:>$1c` / `>$1d` (447000 00001c/1d) where it means
`movem x0,p:$1c` / `$1d` (077084 ...). Patch those two (and only those) in the binary, in place."""
import sys
MOVE_X0_R0 = 0x445800   # dsp_asm's `move x0,x:(r0)+` (checked in stage1.lst)
p = sys.argv[1]; b = bytearray(open(p, 'rb').read())
w = [b[i] | b[i + 1] << 8 | b[i + 2] << 16 for i in range(0, len(b), 3)]
if len(sys.argv) > 2 and sys.argv[2] == 'stage1':
    # stage 1's word loop: `move x0,x:(r0)+` (the only one in it) -> `movem x0,p:(r0)+`
    hits = [i for i, v in enumerate(w) if v == MOVE_X0_R0]
    assert len(hits) == 1, hits
    w[hits[0]] = 0x075884
    for i, v in enumerate(w): b[3 * i:3 * i + 3] = bytes([v & 0xff, v >> 8 & 0xff, v >> 16])
    open(p, 'wb').write(b); print('pmove: stage1 patched'); sys.exit(0)
n = 0
for i in range(len(w) - 1):
    if w[i] == 0x447000 and w[i + 1] in (0x1c, 0x1d):
        w[i] = 0x077084; n += 1
assert n == 2, n
for i, v in enumerate(w): b[3 * i:3 * i + 3] = bytes([v & 0xff, v >> 8 & 0xff, v >> 16])
open(p, 'wb').write(b); print('pmove: patched', n)
