| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | THR RATI BELW ATK REL MIX MAKE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 557 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 250 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 222.4, peak block 223.2 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.683 FS, dc -0.00129, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.924 FS |
| PASS | static curve, 1 kHz sines, THR -24.09 dB, ratio 4.02:1 (RATI 59), BELW off: within 1 dB of the ideal curve | -6: out -19.59 dBFS (ideal -19.60); -12: out -21.08 dBFS (ideal -21.09); -24: out -24.07 dBFS (ideal -24.07); -36: out -36.00 dBFS (ideal -36.00); -48: out -48.00 dBFS (ideal -48.00); worst 0.005 dB |
| PASS | static curve, 1 kHz sines, THR -24.09 dB, ratio 20.00:1 (RATI 127), BELW off: within 1 dB of the ideal curve | -6: out -23.18 dBFS (ideal -23.19); -12: out -23.48 dBFS (ideal -23.49); -24: out -24.08 dBFS (ideal -24.09); -36: out -36.00 dBFS (ideal -36.00); -48: out -48.00 dBFS (ideal -48.00); worst 0.007 dB |
| PASS | BELW right (73: bel 0.143, 1:2.0): -48 dB sine, THR -24.09 -> lower by the expected amount within 2 dB | gain -23.91 dB, expected -23.91 dB |
| PASS | BELW right full (1:8), THR 0: -6 dB -> -42 dB gain, -20 and -40 dB sit on the -80 dB floor, within 2 dB | -6 dB: gain -42.04 (expected -42.00); -20 dB: gain -80.01 (expected -80.00); -40 dB: gain -79.91 (expected -80.00) |
| PASS | BELW left (0: 10:1 upward): -48 dB sine, THR -24.09 -> higher by the expected amount within 2 dB, not above the threshold, not above +30 dB | gain +21.52 dB, expected +21.51 dB; out -26.48 dBFS, peak -26.48 dBFS |
| PASS | BELW left full, THR 0, levels -3 .. -99 dB: cap +30 dB, fade below -80 dB, never above the threshold, within 2 dB | -3 dB: gain +2.71 (expected +2.70); -20 dB: gain +18.01 (expected +18.00); -40 dB: gain +30.00 (expected +30.00); -70 dB: gain +30.00 (expected +30.00); -90 dB: gain +14.95 (expected +15.00); -99 dB: gain +1.37 (expected +1.50) |
| PASS | true silence in, BELW fully left, MAKE 127 -> exact silence out | max |out| 0 LSB |
| PASS | signal then silence, same settings: output exactly 0 from the first silent sample | tail max 0 LSB |
| PASS | gain-curve accuracy: DC staircase -0.5 .. -99 dBFS, 8 settings, steady gain against the float plugin curve (outputs between 2000 LSB and 0.98 FS) | worst 0.0055 dB; THR 51 RATI 59 off: -0.0041 dB at -72 dB (25 levels); THR 51 RATI 127 off: -0.0041 dB at -72 dB (25 levels); THR 20 RATI 30 exp 80: -0.0020 dB at -30 dB (15 levels); THR 0 exp 127: -0.0014 dB at -8 dB (7 levels); THR 51 RATI 59 up 0: -0.0014 dB at -84 dB (27 levels); THR 0 up 0 MAKE 0: -0.0014 dB at -84 dB (25 levels); THR 30 up 40 MAKE 64: +0.0008 dB at -30 dB (28 levels); THR 127 RATI 127 MAKE 127: -0.0055 dB at -95 dB (29 levels) |
| PASS | attack: DC step -40 -> -6 dBFS, time to 90% of the gain reduction (dB) against the float plugin, within 30% | ATK 64 (3.27 ms): 90% of -17.19 dB in 5.37 ms (float plugin 5.37 ms); ATK 100 (34.30 ms): 90% of -17.19 dB in 56.76 ms (float plugin 56.76 ms) |
| PASS | release: DC step -6 -> -40 dBFS, time until 10% of the gain reduction is left against the float plugin, within 30% | REL 40 (33.0 ms): back to 10% of -17.19 dB in 65.8 ms (float plugin 65.8 ms); REL 90 (349.1 ms): back to 10% of -17.19 dB in 698.5 ms (float plugin 696.1 ms) |
| PASS | attack/release at the knob ends (0.05 ms / 5 ms and 200 ms / 2 s): within 30% (or 1 sample) of the float plugin | ATK 0 / REL 0: attack 0.068 ms (plugin 0.068), release 10.0 ms (plugin 10.0); ATK 127 / REL 127: attack 333.628 ms (plugin 333.311), release 4035.3 ms (plugin 3988.2) |
| PASS | MAKE on a -30 dB sine (THR 0, 1:1): expected gain within 1 dB | MAKE 32: +6.048 dB (expected +6.047); MAKE 64: +12.095 dB (expected +12.094); MAKE 127: +24.000 dB (expected +24.000) |
| PASS | full-scale sine, MAKE 127 (+24 dB): saturates at the rails, no wrap (no sign flip against the input; every sample that should clip sits at full scale) | 0 sign flips, 11537 samples at the rails (11537 expected), max 8388607, min -8388608 |
| PASS | -60 dB sine lifted +30 dB (upward) +24 dB (makeup), then a full-scale burst and +-FS alternation with a 200 ms attack (gain near +54 dB at the onset): no sign flip, output at the rails | 0 sign flips; gain before the burst +54.00 dB; burst max 8388607, min -8388608 |
| PASS | stereo link: loud left (-6 dB), quiet right (-40 dB, alone it would get 0 dB) -> the same gain | L -13.591 dB, R -13.591 dB, curve -13.595 dB |
| PASS | stereo link, channels swapped: same gain again | L -13.591 dB, R -13.591 dB |
| PASS | THR 0, 1:1, BELW off, MAKE 0, MIX 127: unity within 2 LSB | max |err| 2 LSB |
| PASS | MIX 64: half dry + half wet on a -6 dB sine | -4.895 dB (expected -4.896) |
| info | size | 557 words of P; r7 state 23 words (00..16) + 24-word table (20..37) |
| PASS | knob jump THR 0<->127 on a -40 dB 1 kHz sine: no click (2nd-difference/peak after the jump within 1.2x of before) | before 1.00, after 1.00 (was up to 46 before the glide) |
| PASS | knob jump RATI 0<->127 on a -12 dB 1 kHz sine: no click (2nd-difference/peak after the jump within 1.2x of before) | before 1.14, after 1.00 (was up to 46 before the glide) |
| PASS | knob jump BELW 0<->127 on a -40 dB 1 kHz sine: no click (2nd-difference/peak after the jump within 1.2x of before) | before 7.43, after 5.47 (was up to 46 before the glide) |
| PASS | knob jump MAKE 0<->127 on a -40 dB 1 kHz sine: no click (2nd-difference/peak after the jump within 1.2x of before) | before 1.25, after 1.00 (was up to 46 before the glide) |
| PASS | width, THR -30 dB, 20:1, MAKE +7.6 dB, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 1.000, rms L -29.1 R -29.1 dBFS, max |L - R| 0 LSB |
| PASS | width, THR -30 dB, 20:1, MAKE +7.6 dB, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 1.000, rms L -23.8 R -23.8 dBFS, max |L - R| 0 LSB |
| info | cost | static 250 cycles/sample; dsp_host mean 222.4, worst block 223.2 instr/sample |
