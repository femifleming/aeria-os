| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | VOWL SHFT RES LEVL MIX |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1485 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 556 cycles/sample |
| FAIL | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 647.2, peak block 648.0 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.472 FS, dc -0.00053, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.852 FS |
| PASS | vowel 'a' (VOWL 0): noise in, wet peaks within 6 % of F1 650 / F2 1080 Hz; F1 is the strongest peak of the spectrum, L == R | 647 / 1083 Hz (-0.4 % / +0.3 %); F2 peak -6.3 dB under F1 (table -6 dB); the knob itself sits at 650 / 1080 Hz |
| PASS | vowel 'e' (VOWL 32): noise in, wet peaks within 6 % of F1 400 / F2 1700 Hz; F1 is the strongest peak of the spectrum, L == R | 399 / 1701 Hz (-0.2 % / +0.1 %); F2 peak -14.9 dB under F1 (table -14 dB); the knob itself sits at 399 / 1701 Hz |
| PASS | vowel 'i' (VOWL 64): noise in, wet peaks within 6 % of F1 290 / F2 1870 Hz; F1 is the strongest peak of the spectrum, L == R | 291 / 1847 Hz (+0.3 % / -1.2 %); F2 peak -13.7 dB under F1 (table -15 dB); the knob itself sits at 291 / 1845 Hz |
| PASS | vowel 'o' (VOWL 95): noise in, wet peaks within 6 % of F1 400 / F2 800 Hz; F1 is the strongest peak of the spectrum, L == R | 399 / 807 Hz (-0.3 % / +0.9 %); F2 peak -10.2 dB under F1 (table -10 dB); the knob itself sits at 399 / 805 Hz |
| PASS | vowel 'u' (VOWL 127): noise in, wet peaks within 6 % of F1 350 / F2 600 Hz; F1 is the strongest peak of the spectrum, L == R | 350 / 604 Hz (-0.1 % / +0.7 %); F2 peak -17.9 dB under F1 (table -20 dB); the knob itself sits at 350 / 600 Hz |
| PASS | wet against a float model of FormantFilter.h (0.2 FS noise, second half of 1.8 s): error under -30 dB of the wet, VOWL/SHFT/RES | 0/64/67: -66.6; 32/64/67: -70.1; 64/64/67: -72.1; 95/64/67: -68.4; 127/64/67: -68.1; 48/96/100: -69.1; 80/20/127: -46.1; 16/127/0: -87.1; 110/0/40: -71.2 dB |
| PASS | SHFT 96 (+12 semitones): first formant of a moves x2 within 6 % | 647 Hz -> 1302 Hz (x2.012) |
| PASS | SHFT 32 (-12 semitones): first formant of a moves x0.5 within 6 % | 647 Hz -> 325 Hz (x0.501) |
| PASS | SHFT 127 (+24 semitones): first formant of a moves x4 within 6 % | 647 Hz -> 2600 Hz (x4.017) |
| PASS | SHFT 0 (-24 semitones): first formant of a moves x0.25 within 6 % | 647 Hz -> 162 Hz (x0.250) |
| PASS | RES 110 (q 4.5) gives a narrower first-formant peak than RES 30 (q 0.28): -3 dB width and peak-to-mean ratio of the wet spectrum | -3 dB width 237 Hz -> 27 Hz (plugin bandwidth 80/q: 284 -> 18 Hz); peak/mean 25 -> 371 |
| PASS | level match: wet RMS within 3 dB of dry RMS, white noise 0.25 FS | 0.5 FS, each vowel, RES default | a -0.02 e +0.03 i -0.11 o +0.03 u -0.03 | a -0.03 e +0.02 i -0.12 o +0.03 u -0.03 | dB |
| info | level match at RES 0 | RES 127 (0.25 FS noise, wet - dry RMS) | a +0.02 e +0.02 i +0.02 o +0.02 u +0.02 | a +0.18 e -0.15 i -3.33 o -1.00 u -3.12 | dB; the float plugin model gives i -3.33 dB at RES 127 |
| PASS | LEVL OFF: the wet is the unmatched filter sum (well under the dry on noise), bounded | wet - dry -19.6 dB |
| PASS | RES 127, every vowel: 0.5 FS noise and a full-scale 40 Hz..16 kHz sine sweep stay <= 1.0 FS (also SHFT 0 and 127 on the sweep) | a/noise 1.000 (0.03 % at the rail); a/sweep 1.000 (3.42 % at the rail); e/noise 1.000 (0.01 % at the rail); e/sweep 1.000 (3.68 % at the rail); i/noise 0.849 (0.00 % at the rail); i/sweep 1.000 (3.54 % at the rail); o/noise 0.959 (0.00 % at the rail); o/sweep 1.000 (3.51 % at the rail); u/noise 0.912 (0.00 % at the rail); u/sweep 1.000 (3.36 % at the rail) |
| PASS | VOWL swept 0..127 over 4 s on a 100 Hz 0.25 FS sine: largest sample-to-sample jump of the wet < 0.01 FS (LEVL ON), < 0.002 FS (LEVL OFF) | LEVL 1 RES 67: 0.0045; LEVL 1 RES 127: 0.0008; LEVL 0 RES 67: 0.0002; LEVL 0 RES 127: 0.0000 FS (the dry sine itself steps 0.0036) |
| PASS | all three knobs jumped end to end in one block (100 Hz sine): no step > 0.01 FS in the wet | largest step 0.0036 FS |
| PASS | noise on the left only: the right channel stays exactly silent (independent filters, shared gain) | L rms 0.137, R max 0 |
| PASS | 1 kHz 0.5 FS sine, RES 127: no DC in the wet (rounded state updates) | dc -5.91e-07 FS, rms 0.222 (dry 0.354) |
| PASS | 0.5 FS noise burst then silence, RES 127: 2.6 s later the wet is under -90 dBFS (no limit cycle lifted by the 32x gain cap) | peak of the last second -240.0 dBFS (0 LSB) |
| info | cost, dsp_host instructions/sample minus null | defaults split 0: mean 645.8 / worst block 646.4; defaults split 7: mean 736.6 / worst block 737.1; dear split 0: mean 647.2 / worst block 648.0; dear split 7: mean 739.5 / worst block 740.1; SHFT 127 RES 0 VOWL 127 split 0: mean 644.9 / worst block 645.6; SHFT 127 RES 0 VOWL 127 split 7: mean 734.9 / worst block 735.5 |
| info | cost | static 556 cycles/sample (sample loop only; the per-call coefficient work is outside it); dsp_host mean 647.2, worst block 648.0 at the dearest settings |
