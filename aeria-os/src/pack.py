#!/usr/bin/env python3
"""Turn the linked AERIA OS into a RAM-bootable MAIN.raw (HARDWARE.md 7.1): flat at 0x40000400, first word
lea -28(%sp),%sp, padded to the gate's minimum length, carrying the bootstrap version word NOR has (0x0408)
at offset 0xDE648. proofs.json names the flashed base, so `device.mjs boot` expects the unit to come back on
it (AERIA OS hands back with a soft reset)."""
import json, struct, sys
from pathlib import Path
img, out, base = Path(sys.argv[1]).read_bytes(), Path(sys.argv[2]), sys.argv[3]
assert struct.unpack('>I', img[:4])[0] == 0x4FEFFFE4, 'entry must start with lea -28(%sp),%sp'
MIN, MAX, VER = 0xDE64A, 0x140000, 0xDE648
assert len(img) <= VER, 'image reaches the bootstrap version word'
img = img + bytes(MIN - len(img))
img = img[:VER] + struct.pack('>H', 0x0408) + img[VER + 2:]
assert MIN <= len(img) <= MAX
out.mkdir(parents=True, exist_ok=True)
(out / 'MAIN.raw').write_bytes(img)
(out / 'proofs.json').write_text(json.dumps({'configurationHash': base, 'note': 'AERIA OS: hands back to this flashed base'}) + '\n')
print(f'{out}/MAIN.raw: {len(img)} bytes')
