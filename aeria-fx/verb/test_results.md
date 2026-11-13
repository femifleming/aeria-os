| result | check | detail |
|---|---|---|
| info | builds | verb_test.asm ring 15870 (m $3dfd, 14355 used); verb.asm ring 32768 (m $7fff, 28469 used) |
| PASS | manifest loads (schema: names<=4, defaults in count) | DECY TONE MOD MIX |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 928 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 1217 cycles/sample (52w x2, 21w x4 x2 nested, 13w x4 x2 nested, 86w x8) |
| FAIL | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 1141.2, peak block 1145.0 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 1.000 FS, dc +0.00002, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.828 FS |
| info | cost (test size) | static 1217 cycles/sample; dsp_host mean 1141.2, worst block 1145.0 instr/sample (the standard gate's mean < 320 line is expected to FAIL for this effect) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 64 MOD 0 (0.5 s; residual vs model, L / R) | -55.0 / -54.2 dB (model wet rms -55.6 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 64 MOD 127 (0.5 s; residual vs model, L / R) | -54.8 / -54.5 dB (model wet rms -55.0 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 100 TONE 0 MOD 64 (0.5 s; residual vs model, L / R) | -34.8 / -35.6 dB (model wet rms -65.3 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 30 TONE 127 MOD 64 (0.5 s; residual vs model, L / R) | -53.6 / -53.4 dB (model wet rms -62.5 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 30 MOD 64 (0.5 s; residual vs model, L / R) | -50.2 / -49.3 dB (model wet rms -59.1 dBFS) |
| PASS | test size: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 100 MOD 64 (0.5 s; residual vs model, L / R) | -53.8 / -53.8 dB (model wet rms -56.6 dBFS) |
| PASS | LFOs: line 0 (0.07 Hz) and line 7 (0.441 Hz) sine states track 0.5 sin(2 pi f t + 0.77 i) over 4 s | max |err| 0.00001 of 0.5 (11025 blocks) |
| PASS | test size: DECY lengthens the decay (wet rms 1.0 s after an impulse, DECY 0 / 64 / 127) | -240.0 / -69.7 / -51.4 dBFS |
| PASS | test size: RT60 at DECY 33 within 25% of T60 = 0.99 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 0.94 s (-5.1%); broadband 0.80 s |
| PASS | test size: broadband RT60 at DECY 33 within 10% of the float model's (same fit) | 0.802 s vs model 0.808 s (-0.7%) |
| PASS | test size: RT60 at DECY 64 within 25% of T60 = 3.05 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 2.77 s (-9.3%); broadband 2.40 s |
| PASS | test size: wide -- L/R correlation of the wet on mono noise, |corr| < 0.5 | corr +0.013, wet rms -19.4 dBFS, peak 0.495 |
| PASS | test size: mono-compatible -- orthogonal output taps (quality pass 12 Nov 2026; the plugin's gave corr about -0.2, L+R +2.3 dB): |corr| < 0.1, L+R within 0.6 dB of +3 dB, L/R balance within 0.5 dB | corr +0.013, L+R +3.26 dB re L, L/R -0.39 dB |
| PASS | test size: bounded at the dearest settings (DECY 127, MOD 127, MIX 127, TONE 0/64/127, full-scale noise), guard clean | peak 1.000 FS, stray 0 |
| PASS | test size: TONE 0 is darker than centre (5-12 kHz vs 80-300 Hz tilt at least 30 dB lower; lows up at least 10 dB) | TONE 0 lo/mid/hi -57.9/-100.3/-161.9 dB, TONE 64 -85.3/-59.1/-62.1 dB |
| PASS | test size: TONE 127 is thinner than centre (0.7-2 kHz at least 15 dB down, 80-300 Hz at least 20 dB down, 5-12 kHz within 6 dB) | TONE 127 lo/mid/hi -160.2/-103.4/-63.2 dB |
| PASS | test size: no DC build-up (noise + 0.3 FS DC in, DECY 127, TONE 0 and 64: |mean| of the last second of wet < 1e-3 FS) | 2.40e-04 FS |
| PASS | test size: silence in -> exact silence out after the tail (0.5 s burst, then zeros; last 0.5 s of the render all 0) | DECY 0 TONE 64: last non-zero sample at 1.03 s, tail max 0 LSB; DECY 40 TONE 0: last non-zero sample at 2.43 s, tail max 0 LSB |
| PASS | FULL: verb.asm assembles (round-trip + mpysu audit), labels clean, no rep | 928 words |
| PASS | FULL: runs on allocator slot 5 (base Y:0x30000), dearest settings on 0.5 FS noise: bounded, -guard 32768 + -guard-shared clean | peak 0.904 FS, stray 0; cost mean 1140.5, worst block 1144.5 instr/sample |
| PASS | FULL: MIX 0 is exact dry (full-range ramp) | max |err| 0 LSB |
| PASS | FULL: dirty state (garbage r7 block, garbage poked through the ring) on silence -> silent tail | worst tail 0 LSB |
| PASS | FULL: random knob automation every 3 blocks, splits 0 and 7, guards: bounded, clean | peak 0.700 FS, stray 0 |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on L, DECY 64 TONE 64 MOD 0 (0.5 s; residual vs model, L / R) | -58.7 / -57.8 dB (model wet rms -57.4 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on R, DECY 64 TONE 64 MOD 127 (0.5 s; residual vs model, L / R) | -57.6 / -57.7 dB (model wet rms -57.2 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on L, DECY 100 TONE 0 MOD 64 (0.5 s; residual vs model, L / R) | -34.7 / -37.9 dB (model wet rms -68.2 dBFS) |
| PASS | FULL: matches the float model of FDNReverb.h, impulse on R, DECY 30 TONE 127 MOD 64 (0.5 s; residual vs model, L / R) | -54.4 / -54.5 dB (model wet rms -65.4 dBFS) |
| PASS | FULL: DECY lengthens the decay (wet rms 1.0 s after an impulse, DECY 0 / 64 / 127) | -240.0 / -70.5 / -52.6 dBFS |
| PASS | FULL: RT60 at DECY 33 within 25% of T60 = 0.99 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 0.99 s (-0.4%); broadband 0.83 s |
| PASS | FULL: broadband RT60 at DECY 33 within 10% of the float model's (same fit) | 0.828 s vs model 0.829 s (-0.1%) |
| PASS | FULL: RT60 at DECY 64 within 25% of T60 = 3.05 s (Schroeder fit -5..-25 dB of the wet impulse response below 1.5 kHz, where the 7.5 kHz damping costs little) | 2.93 s (-4.1%); broadband 2.46 s |
| PASS | FULL: wide -- L/R correlation of the wet on mono noise, |corr| < 0.5 | corr +0.005, wet rms -21.3 dBFS, peak 0.391 |
| PASS | FULL: mono-compatible -- orthogonal output taps (quality pass 12 Nov 2026; the plugin's gave corr about -0.2, L+R +2.3 dB): |corr| < 0.1, L+R within 0.6 dB of +3 dB, L/R balance within 0.5 dB | corr +0.005, L+R +3.14 dB re L, L/R -0.22 dB |
| PASS | FULL: bounded at the dearest settings (DECY 127, MOD 127, MIX 127, TONE 0/64/127, full-scale noise), guard clean | peak 1.000 FS, stray 0 |
| PASS | FULL: TONE 0 is darker than centre (5-12 kHz vs 80-300 Hz tilt at least 30 dB lower; lows up at least 10 dB) | TONE 0 lo/mid/hi -61.4/-101.7/-167.9 dB, TONE 64 -92.2/-61.0/-65.7 dB |
| PASS | FULL: TONE 127 is thinner than centre (0.7-2 kHz at least 15 dB down, 80-300 Hz at least 20 dB down, 5-12 kHz within 6 dB) | TONE 127 lo/mid/hi -164.2/-98.9/-66.5 dB |
| PASS | FULL: no DC build-up (noise + 0.3 FS DC in, DECY 127, TONE 0 and 64: |mean| of the last second of wet < 1e-3 FS) | 1.90e-04 FS |
| PASS | FULL: silence in -> exact silence out after the tail (0.5 s burst, then zeros; last 0.5 s of the render all 0) | DECY 0 TONE 64: last non-zero sample at 1.06 s, tail max 0 LSB; DECY 40 TONE 0: last non-zero sample at 2.50 s, tail max 0 LSB |
| info | width, FULL: defaults (DECY 64, TONE 64, MOD 64), MIX 127, mono noise burst in | corr +0.045, L+R +3.40 dB re L / +3.01 dB re R, peak 0.554, rms L -29.5 R -29.1 dBFS |
| info | width, FULL: defaults (DECY 64, TONE 64, MOD 64), MIX 127, mono 440 Hz sine in | corr -0.444, L+R -0.59 dB re L / +2.40 dB re R, peak 0.568, rms L -12.2 R -15.2 dBFS |
| info | not tested | hardware; core 1 placement (renders ran on core 0); the real 32K buffer base and its allocator word; -dirty garbage fill does not reach Y:0x30000 (pokes used instead); aliasing / fx:audit; parameter locks and LFO-driven knobs on the unit |
