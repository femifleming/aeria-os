| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | FREQ FOLW SPRD BIAS CNTR MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 665 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 208 cycles/sample (worst of 2 mode loops (155/208)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 222.8, peak block 225.2 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.600 FS, dc -0.00114, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | RING FREQ 64 (carrier 316 Hz): 1 kHz in -> sidebands 684 and 1316 Hz each ~0.25, carrier-free input bin | 0.250 / 0.249, 1 kHz 0.0018 |
| PASS | RING FREQ 100 (carrier 1494 Hz): 1 kHz in -> sidebands 494 and 2494 Hz each ~0.25, carrier-free input bin | 0.250 / 0.249, 1 kHz 0.0022 |
| PASS | RING FOLW 127 with a 0.5 FS input raises the carrier (StoreFX: up to x7) | carrier 173 Hz -> 1232 Hz |
| PASS | RING SPRD 127: right carrier ~180 degrees (L/R correlation near -1) | corr -1.000 |
| PASS | PHSR DPTH 0 FDBK 0: static response has notches (min at least 30 dB under the peak) | min -41.3 dB at 269 Hz, max 2.9 dB |
| PASS | PHSR FDBK 127 on 0.5 FS noise: bounded, DC gain bounded (0.7(x+v): 1.4 at DC; |dc out| <= 1.4 |dc in| + 1e-3) | peak 0.700, dc out -0.00174, dc in -0.00081 |
| PASS | PHSR RATE 127 DPTH 127: the spectrum moves between two 93 ms windows | max band change 30.7 dB |
| info | cost | static 208 cycles/sample (worst mode); dsp_host mean 222.8, worst block 225.2 |
