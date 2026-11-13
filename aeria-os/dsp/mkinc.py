#!/usr/bin/env python3
"""thru.bin (dsp_asm: 24-bit little-endian words) -> a C include for the ColdFire side."""
import sys
b = open(sys.argv[1], 'rb').read()
words = [b[i] | b[i + 1] << 8 | b[i + 2] << 16 for i in range(0, len(b), 3)]
org, name = int(sys.argv[3], 16), sys.argv[4]
with open(sys.argv[2], 'w') as f:
    f.write(f'/* generated from dsp/{name}.asm by dsp/mkinc.py: {len(words)} words at P:${org:05x} */\n')
    f.write(f'#define DSP_{name.upper()}_ORG 0x{org:05x}u\n')
    f.write(f'static const uint32_t dsp_{name}[{len(words)}] = {{' + ', '.join(f'0x{w:06x}u' for w in words) + '};\n')
