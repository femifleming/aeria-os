#!/usr/bin/env python3
"""manual_png.py <pgm dir> <png dir> [contact.png]: the manual's screenshots, 128x64 PGM -> 768x384 PNG
(6x nearest neighbour, so the pixels stay square and crisp). Optionally a contact sheet for checking."""
import sys, glob, os
from PIL import Image, ImageDraw
src, dst = sys.argv[1], sys.argv[2]
names = sorted(glob.glob(os.path.join(src, '*.pgm')))
ims = []
for p in names:
    im = Image.open(p).convert('L').point(lambda v: 236 if v > 127 else 18)
    big = im.resize((768, 384), Image.NEAREST)
    big.save(os.path.join(dst, os.path.basename(p)[:-4] + '.png'), optimize=True)
    ims.append((os.path.basename(p)[:-4], im.resize((256, 128), Image.NEAREST)))
if len(sys.argv) > 3:
    per = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    for k in range(0, len(ims), per):
        part = ims[k:k + per]; cols = 4; rows = (len(part) + cols - 1) // cols
        sheet = Image.new('L', (cols * 266 + 10, rows * 150 + 10), 110); d = ImageDraw.Draw(sheet)
        for i, (n, im) in enumerate(part):
            x, y = 10 + (i % cols) * 266, 10 + (i // cols) * 150
            sheet.paste(im, (x, y)); d.text((x, y + 130), n, fill=255)
        sheet.save(sys.argv[3].replace('.png', '_%d.png' % (k // per)))
print(len(names), 'pngs')
