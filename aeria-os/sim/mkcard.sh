#!/bin/sh
# A small CompactFlash image for the simulator: FAT16 with an MBR, /AUDIO/KIT1 (eight drum-like one-shots made here),
# /AUDIO/LOOP.WAV (two seconds, a 220 Hz tone that says "left" and "right" by level) and an empty /AERIA.
set -e
cd "$(dirname "$0")"
rm -rf card.img card.dmg cardsrc; mkdir -p cardsrc/AUDIO/KIT1 cardsrc/AERIA
python3 - <<'PY'
import math, struct, wave, random
def wav(path, frames, ch=1):
    w = wave.open(path, 'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(44100)
    w.writeframes(b''.join(struct.pack('<h', max(-32767, min(32767, int(v * 32767)))) for f in frames for v in (f if ch > 1 else [f]))); w.close()
sr = 44100; rnd = random.Random(1)
def env(n, d): return [math.exp(-i / (d * sr)) for i in range(n)]
kick = [0.9 * e * math.sin(2 * math.pi * (50 * i / sr + 3.0 * (1 - math.exp(-i / (0.02 * sr))))) for i, e in enumerate(env(sr // 4, 0.08))]
snare = [0.7 * e * (0.5 * math.sin(2 * math.pi * 190 * i / sr) + 0.5 * rnd.uniform(-1, 1)) for i, e in enumerate(env(sr // 5, 0.05))]
hat = [0.5 * e * rnd.uniform(-1, 1) for e in env(sr // 12, 0.015)]
tone = lambda f, n=sr // 4: [0.6 * e * math.sin(2 * math.pi * f * i / sr) for i, e in enumerate(env(n, 0.1))]
for i, (name, s) in enumerate([('KICK', kick), ('SNARE', snare), ('HAT', hat), ('T220', tone(220)), ('T330', tone(330)), ('T440', tone(440)), ('T550', tone(550)), ('T660', tone(660))]):
    wav('cardsrc/AUDIO/KIT1/%d%s.WAV' % (i + 1, name), s)
wav('cardsrc/AUDIO/LOOP.WAV', [(0.5 * math.sin(2 * math.pi * 220 * i / sr), 0.25 * math.sin(2 * math.pi * 330 * i / sr)) for i in range(2 * sr)], 2)
PY
hdiutil create -quiet -size 48m -fs "MS-DOS FAT16" -volname AERIASIM -layout MBRSPUD card.dmg
MP=$(hdiutil attach -nobrowse card.dmg | awk -F'\t' '/Volumes/ {print $NF}')
cp -R cardsrc/AUDIO cardsrc/AERIA "$MP"/
find "$MP" -name '._*' -delete 2>/dev/null || true
hdiutil detach -quiet "$MP"
mv card.dmg card.img; rm -rf cardsrc
ls -la card.img
