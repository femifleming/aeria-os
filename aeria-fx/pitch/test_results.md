| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | SHFT FINE GRN FDBK TONE MIX WIDE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 567 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 298 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 278.6, peak block 278.6 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.948 FS, dc -0.01223, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | head speed (from DSP state: 1 - inc*W) matches 2^(SHFT/12) for all 49 SHFT values | worst error 1.57 cents |
| PASS | SHFT +12: 440 Hz sine -> 880.0 Hz: spectral peak within one crossfade-rate sideband (16.4 Hz), level > 0.3, nothing left at 440 | peak 875.75 Hz amp 0.471; 440 Hz residue 0.0002 |
| PASS | SHFT +0: 440 Hz sine -> 440.0 Hz: spectral peak within one crossfade-rate sideband (1.0 Hz), level > 0.3, nothing left at 440 | peak 440.00 Hz amp 0.500; 440 Hz residue 0.4998 |
| PASS | SHFT +7: 440 Hz sine -> 659.3 Hz: spectral peak within one crossfade-rate sideband (8.7 Hz), level > 0.3, nothing left at 440 | peak 657.01 Hz amp 0.451; 440 Hz residue 0.0003 |
| PASS | SHFT -12: 440 Hz sine -> 220.0 Hz: spectral peak within one crossfade-rate sideband (8.7 Hz), level > 0.3, nothing left at 440 | peak 222.25 Hz amp 0.471; 440 Hz residue 0.0005 |
| PASS | SHFT -24: 440 Hz sine -> 110.0 Hz: spectral peak within one crossfade-rate sideband (12.5 Hz), level > 0.3, nothing left at 440 | peak 113.25 Hz amp 0.452; 440 Hz residue 0.0003 |
| PASS | SHFT +24: 440 Hz sine -> 1760.0 Hz: spectral peak within one crossfade-rate sideband (47.2 Hz), level > 0.3, nothing left at 440 | peak 1746.75 Hz amp 0.461; 440 Hz residue 0.0000 |
| PASS | FINE 127: +0.98 st within 10 cents | 465.7 Hz vs 465.7 |
| PASS | FDBK 127 TONE 127, 0.9 FS noise burst then 5.6 s silence: bounded, decaying, no DC | peak 0.994, last 0.5 s rms -240.0 dBFS, dc +0.000000 |
| PASS | FDBK 127 TONE 0: bounded, decaying | peak 0.899, tail rms -240.0 dBFS |
| PASS | stereo: L-only input (FDBK 127) leaves R silent | R peak 0 LSB, L peak 1.000 |
| PASS | WIDE 40 at SHFT +0: left +8.0 c, right -8.0 c around the WIDE 0 pitch (+-2.5 c) | left +8.0 c, right -8.0 c, split 16.0 c (want 15.9); WIDE 0 peak +0.0 c from nominal |
| PASS | WIDE 127 at SHFT +0: left +25.0 c, right -25.0 c around the WIDE 0 pitch (+-2.5 c) | left +25.0 c, right -25.0 c, split 50.0 c (want 50.0); WIDE 0 peak +0.0 c from nominal |
| PASS | WIDE 40 at SHFT +12: left +8.0 c, right -8.0 c around the WIDE 0 pitch (+-2.5 c) | left +8.0 c, right -8.0 c, split 16.0 c (want 15.9); WIDE 0 peak -8.5 c from nominal |
| PASS | WIDE 127 at SHFT +12: left +25.0 c, right -25.0 c around the WIDE 0 pitch (+-2.5 c) | left +25.0 c, right -25.5 c, split 50.5 c (want 50.0); WIDE 0 peak -8.5 c from nominal |
| PASS | WIDE 40 at SHFT -7: left +8.0 c, right -8.0 c around the WIDE 0 pitch (+-2.5 c) | left +7.5 c, right -8.0 c, split 15.5 c (want 15.9); WIDE 0 peak +9.0 c from nominal |
| PASS | WIDE 127 at SHFT -7: left +25.0 c, right -25.0 c around the WIDE 0 pitch (+-2.5 c) | left +24.5 c, right -25.5 c, split 50.0 c (want 50.0); WIDE 0 peak +9.0 c from nominal |
| PASS | width, defaults (SHFT +12, WIDE 40), MIX 127, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr -0.001, L+R +2.99 dB re L / +3.02 dB re R, peak 0.685, rms L -26.5 R -26.5 dBFS |
| PASS | width, defaults (SHFT +12, WIDE 40), MIX 127, mono 440 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr -0.009, L+R +2.97 dB re L / +2.97 dB re R, peak 0.500, rms L -9.5 R -9.5 dBFS, largest step 0.0629 FS (a 1056 Hz partial at this peak: 0.0789) |
| PASS | width, unison (SHFT 0), WIDE 40, MIX 127, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.008, L+R +3.07 dB re L / +3.02 dB re R, peak 0.689, rms L -25.3 R -25.2 dBFS |
| PASS | width, unison (SHFT 0), WIDE 40, MIX 127, mono 440 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr -0.012, L+R +2.96 dB re L / +2.96 dB re R, peak 0.500, rms L -9.4 R -9.4 dBFS, largest step 0.0314 FS (a 528 Hz partial at this peak: 0.0394) |
| PASS | width, WIDE 127, MIX 127, mono noise burst in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr +0.016, L+R +3.06 dB re L / +3.10 dB re R, peak 0.654, rms L -26.5 R -26.5 dBFS |
| PASS | width, WIDE 127, MIX 127, mono 440 Hz sine in: wide (corr <= 0.90), mono sum within 3 dB of either side, bounded | corr -0.001, L+R +3.01 dB re L / +3.01 dB re R, peak 0.500, rms L -9.5 R -9.5 dBFS, largest step 0.0636 FS (a 1056 Hz partial at this peak: 0.0789) |
| PASS | width, WIDE 0, MIX 127, mono noise burst in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.690, rms L -27.1 R -27.1 dBFS, max |L - R| 0 LSB |
| PASS | width, WIDE 0, MIX 127, mono 440 Hz sine in: mono (L = R exactly), bounded | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.500, rms L -9.5 R -9.5 dBFS, max |L - R| 0 LSB, largest step 0.0627 FS (a 1056 Hz partial at this peak: 0.0789) |
| PASS | WIDE 127 -> 0 at 1.5 s: right walks back to left without a click; the last second is L = R exactly | max |L - R| in the last second 0 LSB; corr while wide -0.008; largest right step after the move 0.0627 FS |
| PASS | noise burst then 11.7 s silence, FDBK 127/64/0 x TONE x SHFT: the last 2 s are exactly 0 (48-bit tone LP and DC block, rounded clip input: no stuck DC, no limit cycle; before this pass FDBK 127 held -576 LSB of DC forever) | FDBK 127 TONE 127 SHFT +12: silent from 1.62 s; FDBK 127 TONE 0 SHFT +12: silent from 1.08 s; FDBK 127 TONE 64 SHFT +0: silent from 3.17 s; FDBK 64 TONE 100 SHFT -24: silent from 0.90 s; FDBK 127 TONE 127 SHFT +24: silent from 2.05 s; FDBK 0 TONE 100 SHFT +12: silent from 0.35 s |
| PASS | FDBK jumping 0 <-> 127 every 93 ms on a 300 Hz sine (unison): largest second difference < 0.004 FS (the feedback gain ramps per sample; per-block steps gave 0.0165) | L 0.00178 R 0.00178 FS; no knob moves 0.00091 FS |
| info | cost | static 298 cycles/sample; dsp_host mean 278.6, worst block 278.6 instr/sample |
