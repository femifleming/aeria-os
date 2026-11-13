| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | DRIV TONE RATE LEVL MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 647 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 148 cycles/sample (worst of 3 mode loops (148/112/100)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 150.9, peak block 152.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.742 FS, dc -0.00213, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 1.000 FS |
| PASS | DIST: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.615 harm 7.92e-02 pk 0.556 dc -0.00000; DRIV 64: fund 0.822 harm 1.56e-01 pk 0.714 dc -0.00000; DRIV 127: fund 1.260 harm 5.32e-01 pk 0.994 dc -0.00000 |
| PASS | CLIP: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.402 harm 9.34e-02 pk 0.330 dc -0.00000; DRIV 64: fund 0.420 harm 1.83e-01 pk 0.330 dc -0.00000; DRIV 127: fund 0.420 harm 1.83e-01 pk 0.330 dc -0.00000 |
| PASS | DGRD: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.500 harm 6.72e-08 pk 0.500 dc -0.00000; DRIV 64: fund 0.500 harm 2.74e-05 pk 0.500 dc -0.00000; DRIV 127: fund 0.548 harm 1.47e-01 pk 0.500 dc -0.00000 |
| PASS | DIST transfer: DRIV 0, x = 0.4 FS -> G|x|/(G|x|+1) = 0.5 | out 0.50049 vs 0.50000 |
| PASS | DGRD RATE 0: sample-and-hold at ~441 Hz (changes ~once per 100 samples) | 50 level changes in 4410 samples |
| PASS | DGRD DRIV 0 RATE 127 TONE 127: 24-bit, full rate -> output equals input | max |err| 2 LSB |
| PASS | stereo: L-only input leaves R silent | R peak 0 LSB, L peak 0.994 |
| PASS | width, DIST DRIV 100 RATE 90, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.850, rms L -16.1 R -16.1 dBFS, max |L - R| 0 LSB |
| PASS | width, DIST DRIV 100 RATE 90, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.851, rms L -2.6 R -2.6 dBFS, max |L - R| 0 LSB |
| PASS | width, CLIP DRIV 100 RATE 90, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.282, rms L -25.8 R -25.8 dBFS, max |L - R| 0 LSB |
| PASS | width, CLIP DRIV 100 RATE 90, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.467, rms L -11.0 R -11.0 dBFS, max |L - R| 0 LSB |
| PASS | width, DGRD DRIV 100 RATE 90, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.602, rms L -24.4 R -24.4 dBFS, max |L - R| 0 LSB |
| PASS | width, DGRD DRIV 100 RATE 90, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.498, rms L -9.2 R -9.2 dBFS, max |L - R| 0 LSB |
| info | cost | static 148 cycles/sample (worst mode); dsp_host mean 150.9, worst block 152.4 |
