#!/usr/bin/env python3
"""Load a program into a cold DSP core by hand over AERIA's link (usb.c 0x10/0x11), at the bootstrap's bus
timing: stage 1 through the boot ROM, then the program through stage 1; then print every word the core sends."""
import sys, struct, time
sys.path.insert(0, '/Users/femishonugafleming/octa/aeria-os/tools')
import aos
core = int(sys.argv[2]) if len(sys.argv) > 2 else 0
d = aos.dev(20)
def st(): return bytes(d.ctrl_transfer(0xC0, 0x10, core, 0, 16, timeout=2000))
def put(v):
    for _ in range(200):
        if st()[0] & 0x06: break
        time.sleep(0.005)
    else: raise SystemExit(f'core {core} stopped taking words')
    d.ctrl_transfer(0x40, 0x10, core, 1, struct.pack('>I', v), timeout=2000)
def words(path): b = open(path, 'rb').read(); return [b[i] | b[i+1] << 8 | b[i+2] << 16 for i in range(0, len(b), 3)]
d.ctrl_transfer(0x40, 0x11, 0xFC00, 0x8020, struct.pack('>I', 0x1180), timeout=2000)   # CSCR2: slow
d.ctrl_transfer(0x40, 0x10, core, 2, struct.pack('>I', 0x81), timeout=2000)          # ICR: interface reset
d.ctrl_transfer(0x40, 0x10, core, 3, struct.pack('>I', 0), timeout=2000)             # CVR 0
s1 = words('/Users/femishonugafleming/octa/aeria-os/dsp/stage1.bin' if core == 0 else '/Users/femishonugafleming/octa/aeria-os/dsp/stage1b.bin')
for v in [len(s1), 0x31000 if core == 0 else 0x32000] + s1: put(v)
print('stage 1 sent'); time.sleep(0.05)
prog = words(sys.argv[1])
org = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x33000
for i, v in enumerate([len(prog), org] + prog): put(v)
print(f'program sent ({len(prog)} words)')
t = time.time()
while time.time() - t < 4:
    r = st()
    if r[3]: print('  <- %s' % r[4:7].hex())
