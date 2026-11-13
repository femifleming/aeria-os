| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | PTCH FINE DECY TONE DTUN MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 996 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 215 cycles/sample (worst of 3 mode loops (140/122/215)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 214.9, peak block 214.9 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.499 FS, dc +0.00001, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | WAVE PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -49.1 dBFS, peak 0.199 |
| PASS | WAVE PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 185.0 Hz (+0 c), ring rms -51.1 dBFS, peak 0.116 |
| PASS | WAVE PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 740.0 Hz (+0 c), ring rms -54.2 dBFS, peak 0.055 |
| PASS | WAVE: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -240.0 / -78.7 / -60.6 dBFS |
| PASS | WAVE: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc -0.00007, tail rms -5.8 dBFS |
| PASS | CPLX PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 185.0 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 740.0 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -95.8 / -69.4 / -51.0 dBFS |
| PASS | CPLX: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00077, tail rms -4.0 dBFS |
| PASS | DTND PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -53.6 dBFS, peak 0.092 |
| PASS | DTND PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 183.9 Hz (-10 c), ring rms -55.7 dBFS, peak 0.057 |
| PASS | DTND PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 720.6 Hz (-46 c), ring rms -59.6 dBFS, peak 0.030 |
| PASS | DTND: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -101.7 / -80.5 / -64.8 dBFS |
| PASS | DTND: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00016, tail rms -34.2 dBFS |
| info | cost | static 215 cycles/sample (worst mode); dsp_host mean 214.9, worst block 214.9 |
