#!/usr/bin/env python3
"""Write an AERIA OS card file (AERIA_OS.bin from flash/mk_flash.py) into the unit's NOR through AERIA OS's own
link (usb.c FLASH): decoded to the plain container here, checked to hold the flash build's MAIN with stock's
bootloader copy, staged, verified, written back to front by the unit, then read back whole.
    <modwerk venv python> aos_flash.py AERIA_OS.bin FLASH_BUILD_MAIN.raw"""
import sys, time, struct
sys.path.insert(0, '/Users/you/octa/aeria-os/tools'); sys.path.append('/Users/you/octa/modwerk/sdk')
import firmware_codec; firmware_codec.use_elekloader('/Users/you/.cache/modwerk-upstream/elekloader')
from elekloader import elek, formats
import aos, usb.core
raw = open(sys.argv[1], 'rb').read()
plain = elek.decode_bin(raw)[1]
stock, dev, rel = formats.load('/Users/you/octa/build/to-card/OCTATRACK_OS1.40C_OFFICIAL.bin')
main = formats.main_image(formats.parse(raw, dev), dev)
img = open(sys.argv[2], 'rb').read()
assert main[:0xDDDE0] == img[:0xDDDE0], 'the container does not hold this build'
assert main[0xDDDE0:0xE1DE0] == formats.main_image(stock, dev)[0xDDDE0:0xE1DE0], 'bootloader copy is not stock'
d = aos.dev()
cur = aos.rd(d, 0x4000, 8)
assert plain[:4] == b'ELEK' and cur[:8] == plain[:8], 'NOR holds a different build code'
aos.wr(d, aos.STAGE, plain)
assert aos.rd(d, aos.STAGE, len(plain)) == plain, 'staging differs'
print(f'staged {len(plain)} bytes; writing NOR')
try: d.ctrl_transfer(0x40, 0x0C, 0, 0, struct.pack('>III', aos.STAGE, len(plain), sum(plain) & 0xffffffff), timeout=3000)
except usb.core.USBError: pass
r = 1
for i in range(180):
    time.sleep(1)
    try:
        d = aos.dev(10); r, p = struct.unpack('>II', bytes(d.ctrl_transfer(0xC0, 0x0E, 0, 0, 8, timeout=2000)))
        if r != 1: break
    except usb.core.USBError: pass
ok = r == 2 and aos.rd(aos.dev(10), 0x4000, len(plain)) == plain
print('NOR written and verified' if ok else f'FAILED: result {r:#x}')
sys.exit(0 if ok else 1)
