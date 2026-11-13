| result | check | detail |
|---|---|---|
| PASS | manifest loads (names <= 4 letters, defaults inside their counts, knob 0 = AMT default 0) | AMT 0 RISE 7 SPIN 64 TIME 0 |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep, no parallel move, no backward branch, r7 displacements 0..63 |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean; <= 2,048 words | 594 words (test build, ring at X:$10000) |
| PASS | cost at the dearest settings, 0.5 FS noise, 2.2 s (dsp_host meter minus a null effect): worst block <= 200 instructions a sample | mean 189.2, worst block 189.3 instructions a sample (AMT 127 RISE 12 SPIN 127 TIME 127) |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no write outside the buffer window in Y) | peak 0.578 FS, stray 0 |
| PASS | AMT 0 from the start: output = input bit for bit (full-range ramp, other knobs at their dearest), and the routine costs under 2 instructions a sample | max |err| 0 LSB; rest cost 0.50 a sample |
| PASS | dirty state (garbage r7 block + garbage Y), silence in, AMT 0 and AMT 127: tail <= 0 LSB | worst tail 0 LSB after 1.1 s |
| PASS | every knob random every 3 calls (AMT too: rest, wake, run), split calls 0 and 7, guard: bounded, no stray write | peak 0.957 FS, stray 0 |
| PASS | twister.asm (ring base from the word X:$213 points to) assembles (round-trip, mpysu audit); it differs from the rendered test build only in how init gets the base | 596 words (test build 594: move #>$010000,a in place of move x:>$213,r4 / nop / move x:(r4),a) |
| PASS | AMT 127, TIME 0 (RND): one event of 10 .. 20 s starts after the 16 clearing calls, runs by itself with the knob left at 127, ends, and does not start again; before and after it output = input bit for bit, at rest | event 12.03 s (calls 17 .. 33166); rest cost after it 0.56 a sample |
| PASS | pitch over the event (1 kHz sine, mono sum, 50 ms steps): climbs as 7 x u^1.3, turns in the last 15 % and is back at the end; within 0.35 semitone of the plugin's curve wherever the wet leads (amount over 0.6) | top +5.67 semitones at u = 0.85 (plugin: +5.69); at u 0.25 / 0.5 / 0.75: +1.16 / +2.83 / +4.83 (plugin +1.15 / +2.84 / +4.82); largest difference 0.07; u^1.3 is a cubic here, off by at most 4 cents |
| PASS | spin over the event: the sound goes from side to side at 0.3 + 5 u^2 Hz, faster and faster, and swells | side-to-side rate at u 0.15 / 0.4 / 0.6 / 0.8: 0.52 / 1.18 / 2.21 / 3.47 Hz (plugin 0.41 / 1.10 / 2.10 / 3.50); fastest 3.85 Hz (plugin: 3.95 at u = 0.87); L - R level swings 1.04 of the input; level of the two sides 0.50 .. 1.00 x the input |
| PASS | trigger (TIME 1 = 2.04 s): 63 does nothing; 127 at 1 s starts one; back to 0 and 127 again at 2 s while it runs: ignored; left under 64, then 64 at 6 s: the second one; held there: no third | event at 1.006 s for 2.044 s; event at 6.006 s for 2.044 s (TIME 1 = 2.044 s) |
| PASS | TIME: 1 / 32 / 64 give 2 s x 2^(v/32) within 0.5 %; TIME 0 (RND), four events in a row: each 10 .. 20 s, not the same | TIME 1: 2.044 s (2 s x 2^(v/32) = 2.044); TIME 32: 4.000 s (2 s x 2^(v/32) = 4.000); TIME 64: 8.000 s (2 s x 2^(v/32) = 8.000); RND: 12.03 s, 11.41 s, 13.77 s, 19.40 s |
| PASS | RISE 0 / 7 / 12 (4 s events): the top of the climb follows the knob | RISE 0: top +0.00 semitones (curve +0.00); RISE 7: top +5.69 semitones (curve +5.69); RISE 12: top +9.75 semitones (curve +9.76) |
| PASS | SPIN 0 / 127 (4 s events, RISE 0): turns in the event = the integral of 0.3 + 5 (v/64) u^2 out | SPIN 0: 1 turns (curve 1.2); SPIN 127: 12 turns (curve 11.7) |
| PASS | RISE 0 on DC, mid event: (L + R)/2 flat at 1.3 x 0.6 (the two windows sum to 1 -- a wrong window or tap spacing would ripple; the spin only moves it between the sides) | (L + R)/(2 in) 0.7800 .. 0.7800 over 180 ms (up 1.3 x (1 - 0.8/2) = 0.78) |
| PASS | RISE jumped 0 <-> 12 at random every 0.15 s inside an 8 s event (200 Hz 0.5 FS sine): the pitch slides (RISE glided 1/16 a call, 5.8 ms), no click -- largest second difference under 5 x a clean 200 Hz sine's (an octave up is 4 x by itself; before the quality pass the steps gave 16.9 x) | L 3.75 R 3.95 x |
| PASS | an event over noise, then silence and a second event 5 s in: exactly 0 out (no feedback; the ring is cleared at every start) | 0 LSB |
| PASS | 60 s: 0.9 FS noise for 40 s then silence, 8 s events back to back at RISE 12 SPIN 127: bounded, no stray write, silent once the input is | peak 1.0000 FS (18778 samples at the output limit: the swell is x 1.3), last 15 s 0 LSB; 58.6 s inside events, cost there mean 189.2 / worst call 189.3 |
| info | size | 596 words of P; r7 state 34 words; X ring 4096 words (base a multiple of 4096); window 2032 frames = 46.1 ms (plugin 60 ms) |
| info | cost | inside an event: worst call 189.3, mean 189.2 instructions a sample; at rest 6 instructions a call (standard gate: mean 189.2, worst 189.3 over a render that is mostly rest after its one event) |
