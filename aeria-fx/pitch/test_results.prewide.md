| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | SHFT FINE GRN FDBK TONE MIX |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 395 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 204 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 195.4, peak block 195.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.979 FS, dc -0.01356, stray 0 |
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
| PASS | FDBK 127 TONE 127, 0.9 FS noise burst then 5.6 s silence: bounded, decaying, no DC | peak 0.899, last 0.5 s rms -91.1 dBFS, dc -0.000028 |
| PASS | FDBK 127 TONE 0: bounded, decaying | peak 0.899, tail rms -94.4 dBFS |
| PASS | stereo: L-only input (FDBK 127) leaves R silent | R peak 0 LSB, L peak 1.000 |
| info | cost | static 204 cycles/sample; dsp_host mean 195.4, worst block 195.4 instr/sample |
