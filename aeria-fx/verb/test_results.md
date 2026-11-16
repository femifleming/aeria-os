| result | check | detail |
|---|---|---|
| info | builds | verb_test.asm ring 15870 (m $3dfd, 14259 used); verb.asm ring 32768 (m $7fff, 14259 used) |
| PASS | manifest loads (schema: names<=4, defaults in count) | DECY TONE MOD MIX |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1145 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 1324 cycles/sample (48w x2, 21w x4 x2 nested, 13w x4 x2 nested, 86w x8, fork worst-path (dispatch 6w, alts 52w/434w+884roll)) |
| FAIL | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 680.3, peak block 682.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.875 FS, dc -0.00003, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.795 FS |
| info | cost (test size) | static 1324 cycles/sample; dsp_host mean 680.3, worst block 682.4 instr/sample (the standard gate's mean < 320 line is expected to FAIL for this effect; the static figure is the odd sample, where the FDN runs: the even one costs about 60) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 64 MOD 0 (0.5 s; residual vs model, L / R) | -57.1 / -56.4 dB (model wet rms -58.5 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 64 MOD 127 (0.5 s; residual vs model, L / R) | -56.5 / -56.7 dB (model wet rms -57.8 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 100 TONE 0 MOD 64 (0.5 s; residual vs model, L / R) | -41.2 / -41.9 dB (model wet rms -68.2 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 30 TONE 127 MOD 64 (0.5 s; residual vs model, L / R) | -50.7 / -50.9 dB (model wet rms -68.2 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 30 MOD 64 (0.5 s; residual vs model, L / R) | -52.9 / -51.7 dB (model wet rms -62.1 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 100 MOD 64 (0.5 s; residual vs model, L / R) | -55.4 / -55.9 dB (model wet rms -59.4 dBFS) |
| PASS | LFOs: line 0 (0.07 Hz) and line 7 (0.441 Hz) sine states track 0.5 sin(2 pi f t + 0.77 i) over 4 s | max |err| 0.00002 of 0.5 (11025 blocks) |
| info | halfband (decimator = interpolator), polyphase IIR, 5 allpasses, coefficients A0 0.093604 0.557527 0.922692 | A1 0.315306 0.759827 | design: -0.00 / -0.00 / -0.00 / -0.01 / -3.01 dB at 5 / 9 / 10 / 10.5 / 11.025 kHz; at least 62.1 dB down from 12.05 to 22.05 kHz |
| PASS | half rate: interpolator images (DECY 30 TONE 64 MOD 0 MIX 127, -10 dBFS sines, the last 16,384 samples of 1 s): every image (22.05 kHz - f) at least 60 dB under the wet at f, both sides | 1000 Hz: wet L -4.6 dB re input, image at 21050 Hz -65.8 dB re the wet; 3000 Hz: wet L -12.2 dB re input, image at 19050 Hz -63.8 dB re the wet; 5000 Hz: wet L -24.2 dB re input, image at 17050 Hz -64.5 dB re the wet; 7000 Hz: wet L -13.2 dB re input, image at 15050 Hz -74.8 dB re the wet; 9000 Hz: wet L -19.0 dB re input, image at 13050 Hz -66.0 dB re the wet |
| PASS | half rate: decimator aliasing (same settings, -10 dBFS sines from 12.1 to 21 kHz): what folds to 22.05 kHz - f at least 60 dB under that frequency's own response | 12100 Hz in: -73.4 dB at 9950 Hz (a 9950 Hz input: -11.0 dB); 13000 Hz in: -76.5 dB at 9050 Hz (a 9050 Hz input: -9.3 dB); 15000 Hz in: -95.2 dB at 7050 Hz (a 7050 Hz input: -7.1 dB); 18000 Hz in: -92.9 dB at 4050 Hz (a 4050 Hz input: -6.7 dB); 21000 Hz in: -75.0 dB at 1050 Hz (a 1050 Hz input: -9.6 dB) |
| PASS | half rate: the wet is band-limited (white noise, DECY 50 TONE 64 MOD 64, MIX 127, 2 s): 13-21 kHz at least 60 dB under 1-5 kHz | 1-5 kHz -63.0, 9-10.5 kHz -6.5, 11.5-12.5 kHz -64.2, 13-21 kHz -66.5 dB re 1-5 kHz |
| PASS | half rate against the old full-rate build (verb.asm.prehalf, FULL, slot 5; white noise, DECY 50 TONE 64 MOD 64, MIX 127, 2 s): wet level below 8 kHz within 1.5 dB | +0.15 dB; bands 0.1-0.4/0.4-1.6/1.6-5/5-9/9-12/12-20 kHz, new - old: -0.2 / -0.1 / -0.3 / -1.3 / -7.9 / -59.7 dB |
| PASS | half rate against the old full-rate build: RT60 at DECY 33 below 1.5 kHz within 5%, broadband within 10% (same Schroeder fit) | below 1.5 kHz new 0.978 s, old 0.989 s; broadband new 0.867 s, old 0.828 s |
| PASS | FULL: verb.asm assembles (round-trip + mpysu audit), labels clean, no rep | 1145 words |
| PASS | FULL: runs on allocator slot 5 (base Y:0x30000), dearest settings on 0.5 FS noise: bounded, -guard 32768 + -guard-shared clean | peak 0.860 FS, stray 0; cost mean 680.1, worst block 682.4 instr/sample |
| PASS | FULL: MIX 0 is exact dry (full-range ramp) | max |err| 0 LSB |
| PASS | FULL: dirty state (garbage r7 block, garbage poked through the ring) on silence -> silent tail | worst tail 0 LSB |
| PASS | FULL: random knob automation every 3 blocks, splits 0 and 7, guards: bounded, clean | peak 0.793 FS, stray 0 |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 64 MOD 0 (0.5 s; residual vs model, L / R) | -57.1 / -56.4 dB (model wet rms -58.5 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 64 MOD 127 (0.5 s; residual vs model, L / R) | -56.5 / -56.7 dB (model wet rms -57.8 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on L, DECY 100 TONE 0 MOD 64 (0.5 s; residual vs model, L / R) | -41.2 / -41.9 dB (model wet rms -68.2 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on R, DECY 30 TONE 127 MOD 64 (0.5 s; residual vs model, L / R) | -50.7 / -50.9 dB (model wet rms -68.2 dBFS) |
| PASS | FULL: DECY lengthens the decay (wet rms 1.0 s after an impulse, DECY 0 / 64 / 127) | -240.0 / -69.9 / -52.0 dBFS |
| PASS | FULL: RT60 at DECY 33 within 25% of T60 = 0.99 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 0.98 s (-1.4%); broadband 0.87 s |
| PASS | FULL: broadband RT60 at DECY 33 within 10% of the float model's (same fit) | 0.867 s vs model 0.869 s (-0.3%) |
| PASS | FULL: RT60 at DECY 64 within 25% of T60 = 3.05 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 2.96 s (-3.2%); broadband 2.54 s |
| PASS | FULL: wide -- L/R correlation of the wet on mono noise, |corr| < 0.5 | corr +0.014, wet rms -22.1 dBFS, peak 0.371 |
| PASS | FULL: mono-compatible -- orthogonal output taps (quality pass 12 Nov 2026; the plugin's gave corr about -0.2, L+R +2.3 dB): |corr| < 0.1, L+R within 0.6 dB of +3 dB, L/R balance within 0.5 dB | corr +0.014, L+R +3.22 dB re L, L/R -0.31 dB |
| PASS | FULL: bounded at the dearest settings (DECY 127, MOD 127, MIX 127, TONE 0/64/127, full-scale noise), guard clean | peak 1.000 FS, stray 0 |
| PASS | FULL: TONE 0 is darker than centre (5-9 kHz (half rate: was 5-12) vs 80-300 Hz tilt at least 30 dB lower; lows up at least 10 dB) | TONE 0 lo/mid/hi -61.4/-101.6/-166.7 dB, TONE 64 -93.4/-61.0/-65.8 dB |
| PASS | FULL: TONE 127 is thinner than centre (0.7-2 kHz at least 15 dB down, 80-300 Hz at least 20 dB down, 5-9 kHz (was 5-12) within 6 dB) | TONE 127 lo/mid/hi -164.5/-105.3/-66.9 dB |
| PASS | FULL: no DC build-up (noise + 0.3 FS DC in, DECY 127, TONE 0 and 64: |mean| of the last second of wet < 1e-3 FS) | 1.69e-04 FS |
| PASS | FULL: silence in -> exact silence out after the tail (0.5 s burst, then zeros; last 0.5 s of the render all 0) | DECY 0 TONE 64: last non-zero sample at 1.06 s, tail max 0 LSB; DECY 40 TONE 0: last non-zero sample at 2.51 s, tail max 0 LSB |
| info | width, FULL: defaults (DECY 64, TONE 64, MOD 64), MIX 127, mono noise burst in | corr +0.054, L+R +3.44 dB re L / +3.05 dB re R, peak 0.516, rms L -28.9 R -28.5 dBFS |
| info | width, FULL: defaults (DECY 64, TONE 64, MOD 64), MIX 127, mono 440 Hz sine in | corr -0.597, L+R -1.67 dB re L / +0.37 dB re R, peak 0.545, rms L -12.9 R -14.9 dBFS |
| info | cost, FULL, the old full-rate build beside the new (dearest settings, 0.5 FS noise, slot 5) | old: mean 1140.5, worst block 1144.5, split 7 worst block 1198.0, P 928 words; new: split 7 worst block 736.1, P 1145 words |
| info | not tested | hardware; core 1 placement (renders ran on core 0); the real 32K buffer base and its allocator word; -dirty garbage fill does not reach Y:0x30000 (pokes used instead); fx:audit; parameter locks and LFO-driven knobs on the unit; the behaviour suite on the TEST build (it differs from FULL only in its ring) |
