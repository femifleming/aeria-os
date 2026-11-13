# AERIA HEAD test results (rig5 map, 9 Nov 2026)

Map: HEAD on T5 FX1 (THRU, core 0); T6-T8 NEIGHBOR (core 0); FEEDBACK key = T8 (own bus, Y:$9c0, one block late); SMPL key = T1, STAR key = T2 (T1-4 window, two generations behind core 0's counter). Fixtures: ksfor publishes T1/T2 in every generation of the T1-4 window; the chain stub writes HEAD output x G as T8's slot on core 0's bus. Run: `./run.sh python3 -B /h/test_head.py`. The rig4 results are kept in `.build/test_results.rig4.md`.

| result | check | detail |
|---|---|---|
| PASS | source: no label a prefix of another, no rep, displacements within -64..63 |  |
| PASS | assembles; disassembled, no mpysu/macsu/mpyuu | 384 words, 322 instructions |
| info | every multiply as disassembled | mac -x0,y1,a; mac -x0,y1,b; mac -x1,y0,b; mac -y0,x0,a; mac x1,y0,a; mac x1,y0,b; mac y0,x0,a; mac y1,y0,a; mac y1,y0,b; mpy x0,x0,a; mpy x0,x0,b; mpy x1,y0,a; mpy x1,y0,b; mpy y0,x0,a; mpy y1,y0,a; mpy y1,y0,b |
| PASS | FDBK 0 SMPL 0 STAR 0 IN 64: output == input bit for bit, live bus with loud T1/T2 and a x4 chain, FTON/WARM at 0/127/64/1, splits none/1/7, dirty Y, guard (no write outside HEAD) | max |err| 0 LSB |
| PASS | key bus not live (no seed marks), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (a wrong mark on the other core), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (a wrong mark on this core), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (a counter of 4 on this core), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (a counter of 4 on the other core), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (only this core seeded), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | key bus not live (only the other core seeded), FDBK/SMPL/STAR 127: sends and feedback silent, output == input bit for bit | max |err| 0 LSB |
| PASS | IN 0: exact silence from the second block (the first ramps down) | nonzero after block 1: 0 |
| PASS | IN 127: gain 1 + 63/64 = 1.984 (0.25 FS sine) | 1.98438 |
| PASS | IN 32: gain 0.5 | 0.50000 |
| PASS | SMPL 127, own input silent: output = T1 x 0.99994 (the window slot read; the base 32-sample generation lag is not modelled) | max err 1.19e-07 FS |
| PASS | STAR 127: output = T2 x 0.99994 | max err 1.19e-07 FS |
| PASS | IN 96 + SMPL 64 + STAR 32: the sum of the three (limited at full scale) | max err 1.19e-07 FS |
| PASS | key addressing: HEAD on T5 (and T8) reads T1/T2 from the T1-4 window, generation (core-0 counter + 2) & 3, for counters 0-3 (the other core's counter and the T5-8 window ignored); on T1 from its own bus | T5 c0: +0.1875/-0.1875; T5 c1: +0.2500/-0.2500; T5 c2: +0.0625/-0.0625; T5 c3: +0.1250/-0.1250; T8 c0: +0.1875/-0.1875; T8 c1: +0.2500/-0.2500; T8 c2: +0.0625/-0.0625; T8 c3: +0.1250/-0.1250; T1 c0: +0.3750/-0.1250; T1 c1: +0.3750/-0.1250; T1 c2: +0.3750/-0.1250; T1 c3: +0.3750/-0.1250 |
| PASS | FEEDBACK key: HEAD on T5 feeds back T8's slot of its own bus and nothing else (tone only in T8 -> heard; noise in every other bus slot and both windows -> exactly 0) | T8 tone -> peak 0.234; others -> peak 0.000000 |
| PASS | FDBK 127 FTON 127 WARM 0, unity chain: 1 s 0.9 FS noise then 5 s silence: bounded, last second exactly 0 | peak 1.000 (silence part 0.243); last nonzero sample 3.30 s into the silence; nonzero in last 1 s: 0 |
| PASS | FDBK 127 FTON 64 WARM 0, unity chain: 1 s 0.9 FS noise then 5 s silence: bounded, last second exactly 0 | peak 1.000 (silence part 0.221); last nonzero sample 3.15 s into the silence; nonzero in last 1 s: 0 |
| PASS | FDBK 127 FTON 127 WARM 127, unity chain: 1 s 0.9 FS noise then 5 s silence: bounded, last second exactly 0 | peak 1.000 (silence part 0.159); last nonzero sample 3.31 s into the silence; nonzero in last 1 s: 0 |
| PASS | FDBK 127 FTON 64 WARM 127, unity chain: 1 s 0.9 FS noise then 5 s silence: bounded, last second exactly 0 | peak 1.000 (silence part 0.159); last nonzero sample 3.15 s into the silence; nonzero in last 1 s: 0 |
| PASS | FDBK 127 WARM 0 with a x3.97 chain (loop gain 3.8): bounded during silence by the leveller and clip (|out| <= 0.95 * 2/3) | peak in silence 0.276, last-second rms -13.1 dBFS (it sustains: the chain has gain) |
| PASS | FDBK 127 WARM 127 with a x3.97 chain (loop gain 3.8): bounded during silence by the leveller and clip (|out| <= 0.95 * 2/3) | peak in silence 0.159, last-second rms -16.2 dBFS (it sustains: the chain has gain) |
| PASS | leveller gain g = min(1, 0.3/env) and WARM 90 1/d = 1/(1+3w) as computed on the DSP (24-step divides) | max g error 2.4e-07, 1/d error 9.3e-08; blocks with env > 0.3: 5507 |
| PASS | FDBK 127, x3.97 chain, silence from the start: every output sample exactly 0 | nonzero 0 |
| PASS | FDBK 127 IN 127 WARM 127, x3.97 chain, 3 s full-scale noise/60 Hz: finite and bounded (output limited at full scale by the store) | rms -1.4 dBFS, 58.4% of samples at the rail |
| PASS | FDBK 96, unity chain, 256-sample 1 kHz burst: the loop regenerates (energy after the burst) and decays (16-sample loop here, gain 0.72 a pass: gone by 100 ms) | rms per 100 ms after the burst: -38.0, -121.6, -121.6, -121.6, -121.6 dBFS |
| PASS | FDBK 0 with the same burst: nothing after it |  |
| PASS | IN 0<->127 and SMPL/STAR 0<->127 jumps (together) on 0.25 DC: the largest change between samples is the three 16-sample ramps at once (<= 0.0623), never the jump itself (up to ~1.0) | 0.0623 FS (output peak 0.996: the jumps happened) |
| PASS | FDBK jumping every 20 blocks under a looped 300 Hz tone: bounded | peak 1.000 |
| PASS | FTON jumping every 20 blocks under a looped 300 Hz tone: bounded | peak 0.888 |
| PASS | WARM jumping every 20 blocks under a looped 300 Hz tone: bounded | peak 0.793 |
| PASS | random knobs every 3 blocks, x3.97 chain, split 0, guard: bounded, no write outside HEAD | peak 1.000, strays (0, 0) |
| PASS | random knobs every 3 blocks, x3.97 chain, split 5, guard: bounded, no write outside HEAD | peak 1.000, strays (0, 0) |
| info | cost: live, dearest (WARM>0 and env>0.3: both divides) | worst block 150.9, mean 150.9 instructions/sample (core meter minus an rts-only effect) |
| info | cost: live, defaults | worst block 149.2, mean 149.2 instructions/sample (core meter minus an rts-only effect) |
| info | cost: live, dearest, trig split at frame 1 every block | worst block 169.9, mean 169.9 instructions/sample (core meter minus an rts-only effect) |
| info | cost: live, dearest, split at frame 8 | worst block 169.9, mean 169.9 instructions/sample (core meter minus an rts-only effect) |
| info | cost: bus not live (IN-only loop) | worst block 17.4 instructions/sample |
| PASS | worst cost <= 200 emulator instructions/sample | 169.9; charged on the unit at 7/4: 297 |
