| result | check | detail |
|---|---|---|
| PASS | manifest loads (names <= 4 letters, defaults inside their counts, knob 0 = AMT default 0) | AMT 0 DPTH 64 GUST 64 LEAN 64 |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep, no parallel move, no backward branch, r7 displacements 0..63 |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean; <= 2,048 words | 536 words |
| PASS | cost at the dearest settings, 0.5 FS noise, 2.2 s (dsp_host meter minus a null effect): worst block <= 120 instructions a sample | mean 113.9, worst block 118.4 instructions a sample (AMT 127 DPTH 127 GUST 127 LEAN 127) |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no write outside the buffer window in Y) | peak 0.708 FS, stray 0 |
| PASS | AMT 0 from the start: output = input bit for bit (full-range ramp, other knobs at their dearest), and the routine costs under 2 instructions a sample | max |err| 0 LSB; rest cost 0.38 a sample |
| PASS | dirty state (garbage r7 block + garbage Y), silence in, AMT 0 and AMT 127: tail <= 0 LSB | worst tail 0 LSB after 1.1 s |
| PASS | every knob random every 3 calls (AMT too: rest, wake, run), split calls 0 and 7, guard: bounded, no stray write | peak 0.857 FS, stray 0 |
| PASS | AMT 127, plugin settings, 1 kHz sine, 60 s: pitch drifts both ways, never past the full-gust limit (20.7 cents + 1), the same on both sides | -14.0 .. +17.5 cents (rms 8.3); L and R within 0.01 cents of each other |
| PASS | the same render: level swells with the gusts (plugin: gain 0.96 .. 1.175, x the lean) and leans both ways, inside the plugin's -35 % / +12 % | rms of the two sides 0.941 .. 1.102 x the input; L re R -2.70 .. +1.43 dB (the plugin's extremes -4.73 / +4.73 dB) |
| PASS | DPTH 0 / 64 / 127 (GUST 110, a gust every 0.48 s): no drift at 0; the largest drift follows the knob and stays under its limit | DPTH 0: |drift| up to 0.00 cents (limit 0.0); DPTH 64: |drift| up to 17.96 cents (limit 20.7); DPTH 127: |drift| up to 34.81 cents (limit 40.7) |
| PASS | GUST 0 / 64 / 127, LEAN 0: more gusts, more swell (calm = x 0.9625, a full gust = x 1.175) | GUST 0 (one every 56.00 s): mean level x 0.958, 0.956 .. 0.962; GUST 64 (one every 3.50 s): mean level x 1.012, 0.961 .. 1.117; GUST 127 (one every 0.23 s): mean level x 1.110, 1.030 .. 1.171 |
| PASS | 9 kHz against 500 Hz, two renders of the same weather (DPTH 127), level every 10 ms: the high tone keeps its level within 0.5 dB (a linear read would dip -1.9 dB every time the fraction passes a half) | 9 kHz re 500 Hz: -0.468 .. -0.028 dB |
| PASS | AMT 127 -> 0 on a 441 Hz sine (the knob jumps; the effect glides it): no click, the output comes back to the input bit for bit, the routine goes to rest; silence in after that and AMT up again: exactly 0 out (the ring was cleared) | bit-exact from 271 ms after the knob, at rest (cost 1.44 a sample) from 271 ms; largest second difference on the way 1.018 x a sine's; after the second start 0 LSB |
| PASS | silence in, AMT 127: exactly 0 out (no feedback anywhere) |  |
| PASS | LEAN jumped 0 <-> 127 every 0.2 s (AMT 127, GUST 127, DPTH 0, 200 Hz sine): the lean glides (1/16 a call, 5.8 ms), no click -- largest second difference under 4 x a clean sine's (stepped per call before the quality pass: 30.8 x) | L 1.82 R 2.99 x a clean sine's |
| PASS | 60 s: 0.9 FS noise for 40 s, then silence -- bounded, no stray write, last second under -200 dBFS | peak 1.0000 FS, 71499 samples at the output limit, last second -240.0 dBFS rms, max |sample| in it 0 LSB, cost mean 113.9 / worst block 118.4; dearest settings: swell x 1.175 and lean x 1.24 on 0.9 FS clip at the output, as the plugin would |
| PASS | 60 s: 1.0 FS noise for 40 s, then silence -- bounded, no stray write, last second under -200 dBFS | peak 1.0000 FS, 43424 samples at the output limit, last second -240.0 dBFS rms, max |sample| in it 0 LSB, cost mean 113.8 / worst block 118.6; plugin settings, full-scale noise |
| info | size | 536 words of P; r7 state 37 words; Y ring 4096 words (base a multiple of 4096) |
| info | cost | dsp_host mean 113.9, worst block 118.4 instructions a sample at the dearest settings; at rest (AMT 0) 7 instructions a call |
