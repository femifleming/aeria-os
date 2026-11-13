| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | FREQ FOLW SPRD BIAS CNTR MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 727 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 212 cycles/sample (worst of 3 mode loops (155/4/212)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 232.4, peak block 235.3 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.600 FS, dc -0.00093, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | RING FREQ 64 (carrier 323 Hz): 1 kHz in -> sidebands 677 and 1323 Hz each ~0.25, carrier-free input bin | 0.249 / 0.249, 1 kHz 0.0004 |
| PASS | RING FREQ 100 (carrier 1546 Hz): 1 kHz in -> sidebands 546 and 2546 Hz each ~0.25, carrier-free input bin | 0.249 / 0.249, 1 kHz 0.0018 |
| PASS | RING FOLW 127 with a 0.5 FS input raises the carrier (StoreFX: up to x7) | carrier 176 Hz -> 1253 Hz |
| PASS | RING SPRD 127: right carrier in quadrature (L/R correlation ~0, mono sum +3 dB: no cancellation) | corr +0.013, L+R +3.08 dB re L |
| PASS | RING SPRD 0 / 64 / 127: right carrier leads by 0 / 45 / 89.3 degrees (+-3) | SPRD 0: +0.0 (want 0.0); SPRD 64: +45.1 (want 45.0); SPRD 127: +89.3 (want 89.3) |
| PASS | PHSR DPTH 0 FDBK 0: static response has notches (min at least 30 dB under the peak) | min -35.3 dB at 269 Hz, max 2.9 dB |
| PASS | PHSR FDBK 127 on 0.5 FS noise: bounded, DC gain bounded (0.7(x+v): 1.4 at DC; |dc out| <= 1.4 |dc in| + 1e-3) | peak 0.700, dc out -0.00175, dc in -0.00081 |
| PASS | PHSR RATE 127 DPTH 127: the spectrum moves between two 93 ms windows | max band change 19.6 dB |
| PASS | width, RING defaults (SPRD 127), MIX 127, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.022, L+R +3.13 dB re L / +3.09 dB re R, peak 0.688, rms L -26.5 R -26.4 dBFS |
| PASS | width, RING defaults (SPRD 127), MIX 127, mono 440 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.012, L+R +3.06 dB re L / +3.06 dB re R, peak 0.500, rms L -12.0 R -12.0 dBFS, largest step 0.0822 FS (a 5000 Hz partial at this peak: 0.3661) |
| PASS | width, RING SPRD 0, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.682, rms L -26.5 R -26.5 dBFS, max |L - R| 0 LSB |
| PASS | width, RING SPRD 0, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.500, rms L -12.0 R -12.0 dBFS, max |L - R| 0 LSB, largest step 0.0822 FS (a 5000 Hz partial at this peak: 0.3661) |
| PASS | width, PHSR (RATE 40 FDBK 64 DPTH 127) SPRD 127, MIX 127, mono noise burst in: wide (corr <= 0.95), mono sum within 3 dB of either side, bounded | corr +0.586, L+R +5.93 dB re L / +4.21 dB re R, peak 0.700, rms L -26.3 R -24.5 dBFS |
| PASS | width, PHSR (RATE 40 FDBK 64 DPTH 127) SPRD 127, MIX 127, mono 440 Hz sine in: wide (corr <= 0.95), mono sum within 3 dB of either side, bounded | corr +0.588, L+R +5.38 dB re L / +4.68 dB re R, peak 0.700, rms L -9.5 R -8.8 dBFS, largest step 0.0663 FS (a 660 Hz partial at this peak: 0.0691) |
| PASS | width, PHSR SPRD 0, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.700, rms L -23.0 R -23.0 dBFS, max |L - R| 0 LSB |
| PASS | width, PHSR SPRD 0, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.700, rms L -9.3 R -9.3 dBFS, max |L - R| 0 LSB, largest step 0.0656 FS (a 660 Hz partial at this peak: 0.0691) |
| PASS | RING FREQ 127: carrier 5000 Hz (+-10), as the display law 20 x 250^(v/127) reads | carrier 5000 Hz |
| PASS | PHSR: noise burst then silence, FDBK 0/64/127 x CNTR x DPTH: the last second is exactly 0 (before this pass the allpass truncation and the feedback mean held up to 555 LSB of DC for ever) | FDBK 0 CNTR 64 DPTH 127: silent from 1.24 s; FDBK 64 CNTR 0 DPTH 0: silent from 1.24 s; FDBK 127 CNTR 0 DPTH 0: silent from 1.24 s; FDBK 127 CNTR 64 DPTH 127: silent from 1.24 s; FDBK 127 CNTR 20 DPTH 60: silent from 1.24 s; FDBK 64 CNTR 127 DPTH 0: silent from 0.89 s |
| PASS | PHSR CNTR jumping 0 <-> 127 every 93 ms (DPTH 0, 300 Hz sine): largest second difference < 0.03 FS (CNTR glides ~3 ms a block; before it stepped and the notches rang: 0.22 FS) | 0.0113 FS |
| info | cost | static 212 cycles/sample (worst mode); dsp_host mean 232.4, worst block 235.3 |
