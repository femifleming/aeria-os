| result | check | detail |
|---|---|---|
| PASS | gen.py RING=test AGE=24 WINFIX=0 regenerates grains.asm.handpatched byte for byte |  |
| PASS | grains.asm is what gen.py RING=long writes now |  |
| PASS | grains_test.asm is what gen.py RING=test writes now |  |
| info | ==== test build | /aeria/grains/grains_test.asm, 15870 ring words (15869 samples + mirror), base Y:$4000 (stock FX2 buffer), guard window $3dfe, triangle window table at X:$6280 |
| PASS | manifest loads (schema: names<=4, defaults in count) | SIZE DENS POS SPRY PTCH MIX FRZ REV |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1032 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 606 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 297.4, peak block 303.6 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.746 FS, dc -0.00084, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 1.000 FS |
| PASS | test: the instance got the intended ring base | base = Y:0x04000 |
| PASS | test: PTCH +0, DENS 0 (one grain): 440 Hz -> 440.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.241, 440 0.0000, peak +0 cents, out peak 0.500 |
| PASS | test: PTCH +12, DENS 0 (one grain): 440 Hz -> 880.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.232, 440 0.0034, peak +0 cents, out peak 0.500 |
| PASS | test: PTCH -12, DENS 0 (one grain): 440 Hz -> 220.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.236, 440 0.0058, peak +0 cents, out peak 0.500 |
| PASS | test: PTCH +7, DENS 0 (one grain): 440 Hz -> 659.3 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.238, 440 0.0055, peak +0 cents, out peak 0.500 |
| PASS | test: PTCH +24, DENS 0 (one grain): 440 Hz -> 1760.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.228, 440 0.0013, peak +0 cents, out peak 0.500 |
| PASS | test: PTCH -24, DENS 0 (one grain): 440 Hz -> 110.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.240, 440 0.0058, peak +0 cents, out peak 0.500 |
| PASS | test: DENS 127, unison, SPRY 0: output level within 1.5 dB of the input | -0.00 dB |
| PASS | test: REV 127, DENS 0 (one grain, always backwards): a 440 Hz sine stays 440 Hz (46 ms windows > 0.15, spectral peak within 20 cents) | 440 0.227, peak +0 cents |
| PASS | test: FRZ HOLD at 0.4 s, input -> 1 kHz at 0.5 s: 2.5 s later the output still holds 440 Hz, no 1 kHz | 440: 0.270, 1000: 0.0022 |
| PASS | test: THAW (same input): output follows the input to 1 kHz | 1000: 0.196, 440: 0.0026 |
| PASS | test: SPRY 127: random pan decorrelates L/R (corr < 0.9) | corr +0.639 |
| PASS | test: SIZE 127 grain length (DC in, one grain, unison: spacing of the window zeros) = 13031 samples (295.5 ms) within 0.5 % | spacings [13026.0, 13026.0] samples = [295.4, 295.4] ms |
| PASS | test: the output window follows the triangle table (max deviation from 1 - |2t - 1| over one grain < 0.5 % of the peak) | max deviation 0.002 %, window peak 0.5000 FS |
| PASS | test: SIZE 0 = 10 ms (441 samples) and SIZE 1 - SIZE 0 = 290/128^2 ms (0.78 samples), as before | SIZE 0: 2^23/inc = 441.00 samples, len word 441.000; SIZE 1: len word 441.773 |
| PASS | test: POS 127 reads 15695.8 samples (355.9 ms) back (impulse in, DENS 127, unison, SPRY 0: output peak within 1 sample, nothing elsewhere) | peak at +15696 samples (355.9 ms), 0.3968 FS (+1: 0.0000), largest other sample 0 LSB |
| PASS | test: POS 0 reads 48 samples back | peak at +48 samples |
| PASS | test: wrap seam: unison, DENS 127, POS 1, noise: output = input interpolated at the grain age on EVERY sample over 3.0 turns of the ring (max |err| < 0.4 % FS) | age 171.2109 samples (fraction 0.2109), max |err| 23541 LSB = 0.2806 % FS |
| PASS | test: wrap seam after FRZ HOLD with the head on the top ring word (index 15868), 2000 blocks, then THAW: output = interpolated input again on every sample over the next 1.8 turns (max |err| < 0.4 % FS) | head index while held [15868], frozen at block 4994, max |err| 23629 LSB = 0.2817 % FS |
| PASS | test: grain ages stay in [M, M + R] = [48, 15819] samples (never across the head or past the ring: SIZE 127 x PTCH -24..+24 x REV/POS/SPRY/FRZ corners + random automation, split 0 and 7; every grain after every block, 38 runs of 2776 blocks) | min 48.59, max 15818.78 samples (ring 15870) |
| PASS | test: no read returns uncleared memory: Y garbage under the whole ring, silence in, fixed far settings and random automation, splits 0 and 7: EVERY output sample is 0 (2776 blocks) | worst 0 LSB |
| info | test: cost | static 606 cycles/sample (every grain restarting in the same sample); dsp_host mean 297.4, worst block 303.6 instr/sample |
| info | ==== long build | /aeria/grains/grains.asm, 35280 ring words (35279 samples + mirror), base Y:$0000 (allocator entry X:$256 poked), guard window $89d0, triangle window table at X:$6280 |
| PASS | manifest loads (schema: names<=4, defaults in count) | SIZE DENS POS SPRY PTCH MIX FRZ REV |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1037 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 606 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 297.7, peak block 303.8 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.763 FS, dc -0.00119, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.698 FS |
| PASS | long: the instance got the intended ring base | base = Y:0x00000 |
| PASS | long: PTCH +0, DENS 0 (one grain): 440 Hz -> 440.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.246, 440 0.0000, peak +0 cents, out peak 0.500 |
| PASS | long: PTCH +12, DENS 0 (one grain): 440 Hz -> 880.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.247, 440 0.0035, peak +0 cents, out peak 0.500 |
| PASS | long: PTCH -12, DENS 0 (one grain): 440 Hz -> 220.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.243, 440 0.0058, peak +0 cents, out peak 0.500 |
| PASS | long: PTCH +7, DENS 0 (one grain): 440 Hz -> 659.3 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.244, 440 0.0056, peak +0 cents, out peak 0.500 |
| PASS | long: PTCH +24, DENS 0 (one grain): 440 Hz -> 1760.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.180, 440 0.0010, peak +0 cents, out peak 0.500 |
| PASS | long: PTCH -24, DENS 0 (one grain): 440 Hz -> 110.0 Hz (46 ms windows: target > 0.15, 440 Hz < target/10; median short-window spectral peak within 20 cents) | target 0.246, 440 0.0059, peak +0 cents, out peak 0.500 |
| PASS | long: DENS 127, unison, SPRY 0: output level within 1.5 dB of the input | -0.00 dB |
| PASS | long: REV 127, DENS 0 (one grain, always backwards): a 440 Hz sine stays 440 Hz (46 ms windows > 0.15, spectral peak within 20 cents) | 440 0.202, peak +0 cents |
| PASS | long: FRZ HOLD at 0.4 s, input -> 1 kHz at 0.5 s: 2.5 s later the output still holds 440 Hz, no 1 kHz | 440: 0.206, 1000: 0.0020 |
| PASS | long: THAW (same input): output follows the input to 1 kHz | 1000: 0.271, 440: 0.0019 |
| PASS | long: SPRY 127: random pan decorrelates L/R (corr < 0.9) | corr +0.581 |
| PASS | long: SIZE 127 grain length (DC in, one grain, unison: spacing of the window zeros) = 33075 samples (750.0 ms) within 0.5 % | spacings [33026.0, 33026.0] samples = [748.9, 748.9] ms |
| PASS | long: the output window follows the triangle table (max deviation from 1 - |2t - 1| over one grain < 0.5 % of the peak) | max deviation 0.000 %, window peak 0.5000 FS |
| PASS | long: SIZE 0 = 10 ms (441 samples) and SIZE 1 - SIZE 0 = 290/128^2 ms (0.78 samples), as before | SIZE 0: 2^23/inc = 441.00 samples, len word 441.000; SIZE 1: len word 441.773 |
| PASS | long: POS 127 reads 34954.1 samples (792.6 ms) back (impulse in, DENS 127, unison, SPRY 0: output peak within 1 sample, nothing elsewhere) | peak at +34954 samples (792.6 ms), 0.4237 FS (+1: 0.0739), largest other sample 0 LSB |
| PASS | long: POS 0 reads 48 samples back | peak at +48 samples |
| PASS | long: wrap seam: unison, DENS 127, POS 1, noise: output = input interpolated at the grain age on EVERY sample over 2.7 turns of the ring (max |err| < 0.4 % FS) | age 322.8516 samples (fraction 0.8516), max |err| 23985 LSB = 0.2859 % FS |
| PASS | long: wrap seam after FRZ HOLD with the head on the top ring word (index 35278), 2000 blocks, then THAW: output = interpolated input again on every sample over the next 2.1 turns (max |err| < 0.4 % FS) | head index while held [35278], frozen at block 33147, max |err| 24360 LSB = 0.2904 % FS |
| PASS | long: grain ages stay in [M, M + R] = [48, 35229] samples (never across the head or past the ring: SIZE 127 x PTCH -24..+24 x REV/POS/SPRY/FRZ corners + random automation, split 0 and 7; every grain after every block, 38 runs of 5809 blocks) | min 49.00, max 35227.88 samples (ring 35280) |
| PASS | long: no read returns uncleared memory: Y garbage under the whole ring, silence in, fixed far settings and random automation, splits 0 and 7: EVERY output sample is 0 (5809 blocks) | worst 0 LSB |
| info | long: cost | static 606 cycles/sample (every grain restarting in the same sample); dsp_host mean 297.7, worst block 303.8 instr/sample |
| info | vs pre-table build, unison, DENS 127, POS 0, SPRY 0, 440 Hz 0.5 FS | hand-patched: max |diff| 130298 LSB, rms -47.6 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -143.1 dB; grains_test.asm (aligned 1488 samples): max 3154 LSB, rms -77.1 dB, level +0.000 dB |
| PASS | triangle table reproduces the pre-table build (unison, DENS 127, POS 0, SPRY 0, 440 Hz 0.5 FS): grains_test.asm within -60 dB rms | -77.1 dB (max 3154 LSB) |
| info | vs pre-table build, unison, DENS 127, POS 0, SPRY 0, noise 0.5 FS | hand-patched: max |diff| 129330 LSB, rms -47.5 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -142.8 dB; grains_test.asm (aligned 1488 samples): max 3096 LSB, rms -77.1 dB, level +0.000 dB |
| PASS | triangle table reproduces the pre-table build (unison, DENS 127, POS 0, SPRY 0, noise 0.5 FS): grains_test.asm within -60 dB rms | -77.1 dB (max 3096 LSB) |
| info | vs pre-table build, unison, DENS 127, POS 20, SPRY 0, 440 Hz 0.5 FS | hand-patched: max |diff| 130236 LSB, rms -47.6 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -143.1 dB; grains_test.asm (aligned 1488 samples): max 41314 LSB, rms -40.2 dB, level +0.001 dB |
| info | vs pre-table build, unison, DENS 127, POS 20, SPRY 0, noise 0.5 FS | hand-patched: max |diff| 128020 LSB, rms -47.6 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -141.4 dB; grains_test.asm (aligned 1488 samples): max 1306100 LSB, rms -10.3 dB, level +0.934 dB |
| info | vs pre-table build, defaults but MIX 127 (SPRY 30), 440 Hz 0.5 FS | hand-patched: max |diff| 248876 LSB, rms -28.9 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -139.9 dB; grains_test.asm (aligned 1488 samples): max 154146 LSB, rms -36.5 dB, level -0.004 dB |
| info | vs pre-table build, defaults but MIX 127 (SPRY 30), noise 0.5 FS | hand-patched: max |diff| 259700 LSB, rms -28.4 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -138.8 dB; grains_test.asm (aligned 1488 samples): max 2189988 LSB, rms -10.0 dB, level -0.413 dB |
| info | vs pre-table build, PTCH +7, DENS 127, SPRY 0, 440 Hz 0.5 FS | hand-patched: max |diff| 146894 LSB, rms -20.9 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -132.2 dB; grains_test.asm (aligned 1488 samples): max 667566 LSB, rms -11.3 dB, level +0.011 dB |
| info | vs pre-table build, PTCH +7, DENS 127, SPRY 0, noise 0.5 FS | hand-patched: max |diff| 218482 LSB, rms -28.5 dB re output; hand-patched engine with the corrected window fraction (AGE=24 WINFIX=1): max 2 LSB, rms -137.6 dB; grains_test.asm (aligned 1488 samples): max 5900265 LSB, rms +2.9 dB, level -0.012 dB |
| info | hand-patched build window, DC in, one grain | max deviation from the triangle 4.181 % of the peak; grain spacings [13046.0, 13046.0] |
| info | cost of the pre-table build, same settings, for reference | mean 234.9, worst block 238.3 instr/sample |
| info | cost of the hand-patched build, same settings, for reference | mean 290.9, worst block 294.3 instr/sample |
