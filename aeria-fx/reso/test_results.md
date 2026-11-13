| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | PTCH FINE DECY TONE DTUN MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1088 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 243 cycles/sample (worst of 3 mode loops (150/164/243)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 234.7, peak block 235.6 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.499 FS, dc -0.00013, stray 0 |
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
| PASS | CPLX: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -240.0 / -69.4 / -51.1 dBFS |
| PASS | CPLX: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00077, tail rms -4.0 dBFS |
| PASS | DTND PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -54.0 dBFS, peak 0.130 |
| PASS | DTND PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 183.9 Hz (-10 c), ring rms -55.9 dBFS, peak 0.084 |
| PASS | DTND PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 720.6 Hz (-46 c), ring rms -59.7 dBFS, peak 0.035 |
| PASS | DTND: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -240.0 / -83.0 / -65.1 dBFS |
| PASS | DTND: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00000, tail rms -30.5 dBFS |
| PASS | width, WAVE DTUN 40 (default), PTCH 24, MIX 127, sine at the note, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.048, L+R +2.78 dB re L / +3.69 dB re R, peak 0.478, rms L -28.9 R -29.8 dBFS |
| PASS | width, WAVE DTUN 40 (default), PTCH 24, MIX 127, sine at the note, mono 184.997 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.707, L+R +5.14 dB re L / +5.53 dB re R, peak 0.667, rms L -3.7 R -4.1 dBFS |
| PASS | width, WAVE DTUN 127, PTCH 24, MIX 127, sine at the note, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.003, L+R +2.83 dB re L / +3.22 dB re R, peak 0.478, rms L -28.9 R -29.2 dBFS |
| PASS | width, WAVE DTUN 127, PTCH 24, MIX 127, sine at the note, mono 184.997 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.328, L+R +3.75 dB re L / +4.78 dB re R, peak 0.667, rms L -3.7 R -4.8 dBFS |
| PASS | width, CPLX DTUN 40 (default), PTCH 24, MIX 127, sine at the note, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.550, L+R +4.76 dB re L / +5.07 dB re R, peak 0.476, rms L -23.3 R -23.6 dBFS |
| PASS | width, CPLX DTUN 40 (default), PTCH 24, MIX 127, sine at the note, mono 184.997 Hz sine in: wide (corr <= 0.99), mono sum within 3 dB of either side, bounded | corr +0.981, L+R +5.98 dB re L / +5.98 dB re R, peak 0.667, rms L -3.9 R -3.9 dBFS |
| PASS | width, CPLX DTUN 127, PTCH 24, MIX 127, sine at the note, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.114, L+R +4.18 dB re L / +2.87 dB re R, peak 0.476, rms L -23.3 R -22.0 dBFS |
| PASS | width, CPLX DTUN 127, PTCH 24, MIX 127, sine at the note, mono 184.997 Hz sine in: wide (corr <= 0.99), mono sum within 3 dB of either side, bounded | corr +0.844, L+R +5.67 dB re L / +5.66 dB re R, peak 0.667, rms L -3.9 R -3.9 dBFS |
| PASS | width, DTND DTUN 40 (default), PTCH 24, MIX 127, sine at the note (one partial: a pan), mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.247, L+R +3.48 dB re L / +4.51 dB re R, peak 0.182, rms L -37.7 R -38.7 dBFS |
| PASS | width, DTND DTUN 40 (default), PTCH 24, MIX 127, sine at the note (one partial: a pan), mono 184.997 Hz sine in: wide (corr <= 1.00), mono sum within 3 dB of either side, bounded | corr +0.994, L+R +7.66 dB re L / +4.62 dB re R, peak 0.500, rms L -30.1 R -27.1 dBFS |
| PASS | width, DTND DTUN 127, PTCH 24, MIX 127, sine at the note (one partial: a pan), mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.355, L+R +4.20 dB re L / +4.46 dB re R, peak 0.166, rms L -36.4 R -36.7 dBFS |
| PASS | width, DTND DTUN 127, PTCH 24, MIX 127, sine at the note (one partial: a pan), mono 184.997 Hz sine in: wide (corr <= 1.00), mono sum within 3 dB of either side, bounded | corr -0.847, L+R -1.81 dB re L / +10.85 dB re R, peak 0.500, rms L -22.7 R -35.3 dBFS |
| PASS | width, WAVE DTUN 0, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.478, rms L -27.5 R -27.5 dBFS, max |L - R| 0 LSB |
| PASS | width, WAVE DTUN 0, mono 184.997 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.667, rms L -3.8 R -3.8 dBFS, max |L - R| 0 LSB |
| PASS | width, CPLX DTUN 0, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.476, rms L -22.0 R -22.0 dBFS, max |L - R| 0 LSB |
| PASS | width, CPLX DTUN 0, mono 184.997 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.667, rms L -3.9 R -3.9 dBFS, max |L - R| 0 LSB |
| PASS | DTND DTUN 0: (L + R) / 2 is the old mono output (quiet burst: the clip is still linear; DC removed), within -35 dB (-40 before the quality pass: the loop now stores toward zero, a 1-LSB change a write that the long ring integrates) | difference -37.8 dB re the old output; L/R corr +0.982, L -47.9 R -47.5 old -47.7 dBFS |
| PASS | DTND DTUN 40: (L + R) / 2 is the old mono output (quiet burst: the clip is still linear; DC removed), within -35 dB (-40 before the quality pass: the loop now stores toward zero, a 1-LSB change a write that the long ring integrates) | difference -41.5 dB re the old output; L/R corr +0.247, L -47.2 R -48.2 old -49.7 dBFS |
| PASS | DTND DTUN 127: (L + R) / 2 is the old mono output (quiet burst: the clip is still linear; DC removed), within -35 dB (-40 before the quality pass: the loop now stores toward zero, a 1-LSB change a write that the long ring integrates) | difference -44.4 dB re the old output; L/R corr +0.356, L -45.9 R -46.2 old -47.7 dBFS |
| PASS | noise burst then silence: the last 0.5 s of 8 s is exactly 0 in every mode (before this pass WAVE held -60 LSB and DTND up to +1,091 LSB of DC, CPLX sang on at up to -62 dBFS) | WAVE DECY 80 TONE 90 PTCH 24: silent from 3.13 s; WAVE DECY 60 TONE 110 PTCH 40: silent from 1.23 s; CPLX DECY 80 TONE 90 PTCH 24: silent from 4.79 s; CPLX DECY 60 TONE 110 PTCH 40: silent from 1.77 s; DTND DECY 80 TONE 90 PTCH 24: silent from 3.58 s; DTND DECY 60 TONE 110 PTCH 40: silent from 1.58 s; DTND DECY 127 TONE 0 PTCH 0: silent from 5.88 s |
| PASS | CPLX DECY 100 PTCH 60 after a burst: the ring keeps falling at about 40 dB / 4.6 s (2 -> 6 -> 10 s: at least 25 dB a step; before: stuck at -62 dBFS from 6 s on) | -64 -> -96 -> -129 dBFS |
| PASS | DTND on 0.1 FS noise, PTCH 0/24/48 x DTUN 20/64/127: no lean (mean L - R within +-0.6 dB: -0.36; before this pass -1.6..-1.9 dB, every case right-heavy) and every case within +-3 dB (resonant noise: a few partials, so one case moves by 1-2 dB with the noise realisation) | 0/20: +0.5, 0/64: -0.2, 0/127: +0.4, 24/20: -0.7, 24/64: +0.7, 24/127: +0.3, 48/20: -1.0, 48/64: -0.8, 48/127: -2.4 dB (L - R) |
| PASS | DTND: the side balance term (SK) leaves the mono sum alone: L + R equals that of the SK=0 build of this source (quiet burst, DTUN 0/40/127) within -60 dB | worst -84.7 dB |
| info | cost | static 243 cycles/sample (worst mode); dsp_host mean 234.7, worst block 235.6 |
