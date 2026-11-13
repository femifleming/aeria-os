#!/usr/bin/env python3
"""AERIA with its input tap: build.py's C path plus one stock-code site (ABI 4, private: it carries 6 stock bytes).

    python3 -B /src/build_aeria.py /tmp/aeria.c -o /out/aeria.mwrm [--repo /repo] [--stock MAIN.raw] [--hashed]

The site: 0x4000d99a, the frame interrupt's tail, 6 bytes. Stock there is 23c1 8000 4800 (move.l %d1,0x80004800,
SHA-256 9a7d1001...5f707b91, the guard TUNER declares for the same site, sdk/octabam/modules/tuner/manifest.py).
New bytes: 4ef9 <aeria_frame_stub> (jmp abs.l; the address word, offset 2, is a self-reference the loader relocates).
The stub (pitch.c) replays the store, calls aeria_frame() and jumps to 0x4000d9a0, the next stock instruction (on a
USB base, the base's own jmp to its audio shim). The interrupt restores d0-d7/a0-a6 from its stack at 0x4000d9a6.
Everything else is build.py's own build_c: same flags, codegen check, double link, hooks, module id (the output stem).
--stock checks the site's bytes in a MAIN image loaded at 0x40000400 (stock or a base's MAIN.raw).
--hashed packages ABI 7 instead (the stock bytes' SHA-256, not the bytes) for a base that admits hashed C packages.
"""
import argparse
import hashlib
import importlib.util
import struct
import sys
import tempfile
from pathlib import Path

SITE, STOCK = 0x4000d99a, bytes.fromhex('23c180004800')
STOCK_SHA = '9a7d100114ea50c9317af4f9de9210dd0eeae82f9ce03b255ac0d7bb5f707b91'
NEXT = 0x4000d9a0
STUB = 'aeria_frame_stub'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('sources', type=Path, nargs='+')
    ap.add_argument('-o', '--output', type=Path, required=True)
    ap.add_argument('--repo', type=Path, default=Path('/repo'))
    ap.add_argument('--cross', default='m68k-elf-')
    ap.add_argument('--stock', type=Path)
    ap.add_argument('--hashed', action='store_true')
    args = ap.parse_args()

    spec = importlib.util.spec_from_file_location('mw_build', args.repo / 'sdk/runtime/loader/build.py')
    b = importlib.util.module_from_spec(spec)
    sys.argv[0] = str(args.repo / 'sdk/runtime/loader/build.py')
    spec.loader.exec_module(b)

    if hashlib.sha256(STOCK).hexdigest() != STOCK_SHA:
        sys.exit('stock bytes do not match the declared guard')
    if args.stock:
        main_img = args.stock.read_bytes()
        at = SITE - 0x40000400
        if main_img[at:at + 6] != STOCK:
            sys.exit('%s: bytes at %#x are %s, not stock %s (a base or module owns them?)'
                     % (args.stock, SITE, main_img[at:at + 6].hex(), STOCK.hex()))

    spec2 = importlib.util.spec_from_file_location('runtime_c_codegen', b.HERE.parent / 'm68k_codegen.py')
    codegen = importlib.util.module_from_spec(spec2); spec2.loader.exec_module(codegen)
    import subprocess
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        objects = [tmp / ('%d.o' % i) for i in range(len(args.sources))]
        for source, obj in zip(args.sources, objects):
            subprocess.run([args.cross + 'gcc', *b.CFLAGS, *codegen.FLAGS, '-c', str(source), '-o', str(obj)], check=True)
        codegen.check_mcf54455(objects, args.cross + 'objdump')
        image, symbols, bss, offsets = b.link_units(objects, args.cross, 0, tmp)
    hooks = [symbols.get(name, b.NONE) for name in b.HANDLERS]
    if STUB not in symbols:
        sys.exit('no %s: is pitch.c in the sources (and not built with PREVIEW)?' % STUB)
    stub = symbols[STUB] - symbols['__image_start']
    # the stub as pitch.c spells it: the displaced store, jsr aeria_frame (relocated), jmp to the next stock instruction
    if (stub & 1 or image[stub:stub + 6] != STOCK or image[stub + 6:stub + 8] != b'\x4e\xb9' or stub + 8 not in offsets
            or image[stub + 12:stub + 18] != b'\x4e\xf9' + NEXT.to_bytes(4, 'big')):
        sys.exit('%s does not hold the expected stub: %s' % (STUB, image[stub:stub + 18].hex()))
    code = b'\x4e\xf9' + stub.to_bytes(4, 'big')
    b.unevented([(SITE, STOCK, code, [2])])  # not a base-owned event's bytes
    if args.hashed:
        sites = [(SITE, bytes.fromhex(STOCK_SHA), code, [2], 0, len(STOCK))]
        data = b.package(image, bss, hooks, offsets, sites, name=args.output.stem, hashed=True,
                         title=args.output.stem[:15], abi=7)
    else:
        data = b.package(image, bss, hooks, offsets, [(SITE, STOCK, code, [2])], name=args.output.stem)
    args.output.write_bytes(data)

    # read the package back: the site record is where README's ABI 4 layout puts it
    abi = struct.unpack_from('>H', data, 4)[0]
    n_img, n_bss, n_rel, n_hooks, n_sites, ident = struct.unpack_from('>IIIIII', data, 8)
    if abi == 4:
        at = 32 + 4 * n_hooks + n_img + 4 * n_rel
        addr, length, nrel = struct.unpack_from('>IHH', data, at)
        rec = data[at + 8:at + 8 + 2 * length + 2 * nrel]
        assert (n_sites, addr, length, nrel) == (1, SITE, 6, 1) and rec[:6] == STOCK and rec[6:12] == code \
            and struct.unpack_from('>H', rec, 12)[0] == 2 and len(data) == at + 8 + 2 * length + 2 * nrel, 'site record'
    print('%s: module %08x, ABI %d, %d bytes of code and data, %d of bss, %d relocations, %d hooks, %d stock-code site '
          '(%#x: %s -> jmp image+%#x)' % (args.output, ident, abi, n_img, n_bss, n_rel,
                                          sum(h != b.NONE for h in hooks), n_sites, SITE, STOCK.hex(), stub))


if __name__ == '__main__':
    main()
