| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | SMER DIFF TONE MOD FRZ MIX |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 516 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 298 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 269.4, peak block 269.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.499 FS, dc -0.00038, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | SMER lengthens the smear (rms 1 s after an impulse, SMER 0/64/127) | -132.5 / -87.3 / -66.5 dBFS |
| PASS | wide: L/R correlation of the wet on noise < 0.5 | corr +0.016, peak 0.499 |
| PASS | HOLD sustains: rms at 4 s and 7.5 s within 3 dB of 1.5 s (input stopped at 1.2 s) | -28.6 / -28.7 / -28.8 dBFS, peak 0.499 |
| PASS | THAW at 8 s: decays again (SMER 40: RT60 ~1.2 s) -> 2.4 s later at least 30 dB down | -132.4 dBFS |
| PASS | HOLD from the start: new input does not enter the tank | wet peak 2.38e-07 |
| info | width, defaults, MIX 127, mono noise burst in | corr -0.005, L+R +3.01 dB re L / +2.97 dB re R, peak 0.194, rms L -38.8 R -38.7 dBFS |
| info | width, defaults, MIX 127, mono 440 Hz sine in | corr -0.978, L+R -6.26 dB re L / -0.88 dB re R, peak 0.500, rms L -19.1 R -24.5 dBFS |
| info | cost | static 298 cycles/sample; dsp_host mean 269.4, worst block 269.4 |
