#!/usr/bin/env python3
"""AERIA OS -> an OS file the Octatrack's own upgrade path takes (Elektron's container, as Modwerk writes its bases).

    python3 -B mk_flash.py --stock ~/octa/build/to-card/OCTATRACK_OS1.40C_OFFICIAL.bin \
        --image ~/octa/aeria-os/build/ram/MAIN.raw --out ~/octa/aeria-os/build/flash

The MAIN section is AERIA OS from 0x40000400 and zeros after it -- none of the stock OS -- except the bootloader
copy the OS image carries (0x400de1e0-0x400e21e0, version word 0x400dea48), which stays stock's byte for byte:
nothing in AERIA OS re-flashes the bootloader, and keeping the copy identical means nothing ever could change it.
The output holds those stock bytes, so it stays private (this folder is in no Git checkout). Every other section of
the file is stock's (the container's own checks), and formats.verify re-reads the result."""
import argparse, hashlib, json, sys
from pathlib import Path

MODWERK = Path.home() / 'octa/modwerk'
UPSTREAM = Path.home() / '.cache/modwerk-upstream/elekloader'
LOAD = 0x40000400
BOOT_COPY = (0x400de1e0, 0x400e21e0)

ap = argparse.ArgumentParser()
ap.add_argument('--stock', required=True); ap.add_argument('--image', required=True); ap.add_argument('--out', required=True)
ap.add_argument('--version', default='AERIA')
a = ap.parse_args()
sys.path.append(str(MODWERK / 'sdk'))
import firmware_codec  # noqa: E402
firmware_codec.use_elekloader(UPSTREAM)
from elekloader import formats  # noqa: E402

stock, dev, rel = formats.load(a.stock)
stock_main = formats.main_image(stock, dev)
ours = Path(a.image).read_bytes()
lo, hi = BOOT_COPY[0] - LOAD, BOOT_COPY[1] - LOAD
print(f'{dev.name}: stock MAIN {len(stock_main)} bytes; AERIA OS {len(ours)} bytes')
code_end = len(ours.rstrip(b'\0')) if len(ours) < lo else lo
# the RAM image carries only the version word inside the copy: the code must end before the copy begins
body = ours[:lo]
assert not any(ours[lo:hi][:0xDE648 - lo]) and not any(ours[0xDE64A:hi] if len(ours) > 0xDE64A else b''), 'image reaches the bootloader copy'
main = bytearray(len(stock_main))
main[:len(body)] = body
main[lo:hi] = stock_main[lo:hi]
assert bytes(main[lo:hi]) == bytes(stock_main[lo:hi])
outputs = formats.write(stock, formats.pack_main(bytes(main), dev), dev, a.version)
facts = formats.verify(outputs, stock, bytes(main), dev, a.version)
out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
for ext, data in outputs.items():
    (out / f'AERIA_OS.{ext}').write_bytes(data)
# decode again from disk: the MAIN must be ours, the copy stock's
back = formats.main_image(formats.parse(str(out / 'AERIA_OS.bin'), dev), dev)
assert back == bytes(main), 'decoded MAIN differs'
assert back[lo:hi] == stock_main[lo:hi], 'bootloader copy differs'
stock_left = sum(1 for i in range(len(back)) if not lo <= i < hi and back[i] and back[i] == stock_main[i])
report = dict(device=dev.name, main_len=len(back), aeria_bytes=len(body.rstrip(b'\0')), boot_copy='stock, byte for byte',
              main_sha256=hashlib.sha256(back).hexdigest(), same_as_stock_outside_copy=stock_left,
              files={k: hashlib.sha256(v).hexdigest() for k, v in outputs.items()}, verify=facts)
(out / 'report.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
print(json.dumps({k: v for k, v in report.items() if k != 'verify'}, indent=1))
