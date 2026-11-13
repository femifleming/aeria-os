| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | ROOT SHAP FOLD DECY BRIT MIX SPRD ROTA TUNE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 1569 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 182 cycles/sample |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 201.5, peak block 203.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.446 FS, dc +0.00007, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.683 FS |
| PASS | worst block over SHAP 0..5 x trig splits 0/1/7 (dsp_host meter minus null) <= 210 (was 199.2 before the 12 Nov quality pass) | worst 205.2 instr/sample (split 0 steady mean 201.5) |
| PASS | HARM FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +75.3 dB; Hz 131 262 523 654 916 1047 1308 1439 |
| PASS | HARM FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +78.4 dB; Hz 131 523 785 1177 1439 1831 2093 2485 |
| PASS | HARM FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +77.8 dB; Hz 131 916 1177 1962 2028 2224 3009 3270 |
| PASS | HARM FOLD 12 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +78.4 dB; Hz 131 1439 1701 2289 2420 3009 3074 3270 |
| PASS | PRIM FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +76.6 dB; Hz 131 262 392 654 916 1439 1701 2224 |
| PASS | PRIM FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +78.9 dB; Hz 131 654 916 1439 1701 2224 2485 3009 |
| PASS | PRIM FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +80.7 dB; Hz 131 1701 2028 2224 2420 2485 3009 3794 |
| PASS | PRIM FOLD 12 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +83.8 dB; Hz 131 2028 2420 2682 2812 3074 3467 3794 |
| PASS | CIRC FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 -1 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +86.5 dB; Hz 131 689 921 1694 2155 2233 3145 3832 |
| PASS | CIRC FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 -1 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +88.3 dB; Hz 131 689 1694 1740 2370 2521 3145 3444 |
| PASS | CIRC FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 -1 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +83.2 dB; Hz 131 689 1694 2521 2909 3145 3381 3693 |
| PASS | CIRC FOLD 12 OPEN: impulse rings at its 7 mode frequencies (C3 root) | cents +0 -1 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +81.8 dB; Hz 131 689 1694 2444 2521 3145 3693 |
| PASS | SQRE FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +76.2 dB; Hz 131 327 523 654 850 1112 1177 1308 |
| PASS | SQRE FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +79.4 dB; Hz 131 654 850 1112 1177 1308 1635 1701 |
| PASS | SQRE FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +79.9 dB; Hz 131 1177 1308 1635 1701 1897 2093 2224 |
| PASS | SQRE FOLD 12 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +87.5 dB; Hz 131 1897 2093 2224 2420 2616 2682 2943 |
| PASS | ROSE FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +61.9 dB; Hz 131 227 262 392 453 523 680 785 |
| PASS | ROSE FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +64.6 dB; Hz 131 249 342 403 423 473 651 766 |
| PASS | ROSE FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +68.4 dB; Hz 131 257 373 474 503 558 619 658 |
| PASS | ROSE FOLD 12 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +70.1 dB; Hz 131 259 384 501 514 610 709 760 |
| PASS | GOTH FOLD 3 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +70.6 dB; Hz 131 196 262 392 523 589 785 1047 |
| PASS | GOTH FOLD 5 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +64.8 dB; Hz 131 157 196 262 314 392 523 628 |
| PASS | GOTH FOLD 8 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +62.5 dB; Hz 131 174 233 262 349 465 523 698 |
| PASS | GOTH FOLD 12 OPEN: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +59.1 dB; Hz 131 147 164 180 196 213 229 245 |
| PASS | HARM FOLD 7 CLSE: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +68.4 dB; Hz 131 262 327 360 392 425 441 491 |
| PASS | PRIM FOLD 7 CLSE: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +59.8 dB; Hz 131 278 311 360 376 425 474 507 |
| PASS | CIRC FOLD 7 CLSE: impulse rings at its 7 mode frequencies (C3 root) | cents +0 -0 -1 +0 +0 +0 +0; weakest mode / loudest between-mode probe +57.4 dB; Hz 131 311 345 393 423 462 473 |
| PASS | SQRE FOLD 7 CLSE: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +64.7 dB; Hz 131 262 278 294 327 409 425 474 |
| PASS | ROSE FOLD 7 CLSE: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 -1 +0; weakest mode / loudest between-mode probe +68.1 dB; Hz 131 255 265 287 294 367 460 497 |
| PASS | GOTH FOLD 7 CLSE: impulse rings at its 8 mode frequencies (C3 root) | cents +0 +0 +0 +0 +0 +0 +0 +0; weakest mode / loudest between-mode probe +64.4 dB; Hz 131 164 196 229 262 327 392 458 |
| PASS | SPRD 127: even modes in L, odd modes in R (ROSE FOLD 3) | own-side margin dB 70 83 73 75 67 82 70 89 |
| PASS | SPRD 0: L == R | max |L-R| 0 LSB |
| PASS | DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -240.0 / -84.0 / -50.1 dBFS |
| PASS | BRIT tilts decay: top mode vs root 0.5 s after impulse rises with BRIT (0/64/127) | -80.9 / -0.2 / +6.4 dB |
| PASS | HARM: 0.95 FS sine at mode 1 (262 Hz) for 1.6 s, DECY 127 BRIT 127: bounded, no DC, decays after | peak 0.692, dc -0.00021, rms on/after/later -4.6/-5.1/-6.7 dBFS |
| PASS | CIRC: 0.95 FS sine at mode 1 (689 Hz) for 1.6 s, DECY 127 BRIT 127: bounded, no DC, decays after | peak 0.834, dc +0.00003, rms on/after/later -4.6/-5.1/-6.8 dBFS |
| PASS | ROSE: 0.95 FS sine at mode 1 (227 Hz) for 1.6 s, DECY 127 BRIT 127: bounded, no DC, decays after | peak 0.667, dc +0.00067, rms on/after/later -4.6/-5.1/-6.7 dBFS |
| PASS | SHAP switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -47.7 dBFS vs settled -47.1 dBFS |
| PASS | FOLD switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -68.9 dBFS vs settled -68.1 dBFS |
| PASS | ROOT switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -47.2 dBFS vs settled -44.3 dBFS |
| PASS | TUNE switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -70.5 dBFS vs settled -68.9 dBFS |
| PASS | DECY switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -60.9 dBFS vs settled -58.9 dBFS |
| PASS | ROTA switches under noise: no click (max 2nd difference, 20 blocks after each switch vs settled stretches) | switch -63.1 dBFS vs settled -67.0 dBFS |
| PASS | ROTA 127 sweeps a mode's level (light moving across the panes) vs ROTA 0 | mode 2 level swing 10.9 dB (ROTA 0: 0.1 dB) |
| PASS | DECY 30 (T60 0.18 s): sine at ROOT 0.3 s then silence -> exact silence (no stuck DC, no limit cycle) | last nonzero sample at 0.60 s of 2.0 s |
| PASS | DECY 60 (T60 0.67 s): sine at ROOT 0.3 s then silence -> exact silence (no stuck DC, no limit cycle) | last nonzero sample at 1.38 s of 4.0 s |
| PASS | DECY 90 (T60 2.43 s): sine at ROOT 0.3 s then silence -> exact silence (no stuck DC, no limit cycle) | last nonzero sample at 3.88 s of 10.0 s |
| PASS | knob jump SPRD 0<->127 on a 1 kHz sine: 2nd-difference/peak after the jump (clean sine = 1) <= 1.3 | before 1.13, after 1.02 (before the pass: SPRD 5.8, ROTA 5.2) |
| PASS | knob jump ROTA 0<->127 on a 1 kHz sine: 2nd-difference/peak after the jump (clean sine = 1) <= 3.5 | before 1.48, after 2.94 (before the pass: SPRD 5.8, ROTA 5.2) |
| info | width, defaults (ROSE 8-fold, SPRD 90), MIX 127, sine at ROOT (C3), mono noise burst in | corr +0.548, L+R +5.55 dB re L / +4.33 dB re R, peak 0.429, rms L -29.7 R -28.5 dBFS |
| info | width, defaults (ROSE 8-fold, SPRD 90), MIX 127, sine at ROOT (C3), mono 130.813 Hz sine in | corr +0.943, L+R +5.37 dB re L / +6.45 dB re R, peak 0.667, rms L -3.9 R -5.0 dBFS |
| info | width, SPRD 127, mono noise burst in | corr -0.004, L+R +3.78 dB re L / +2.33 dB re R, peak 0.419, rms L -30.2 R -28.8 dBFS |
| info | width, SPRD 127, mono 130.813 Hz sine in | corr -0.043, L+R +0.26 dB re L / +10.84 dB re R, peak 0.667, rms L -3.9 R -14.5 dBFS |
| info | width, SPRD 0, mono noise burst in | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.511, rms L -25.2 R -25.2 dBFS, max |L - R| 0 LSB |
| info | width, SPRD 0, mono 130.813 Hz sine in | corr +1.000, L+R +6.02 dB re L / +6.02 dB re R, peak 0.667, rms L -3.9 R -3.9 dBFS, max |L - R| 0 LSB |
| info | cost | static 182 cycles/sample (sample loop); dsp_host mean 201.5, worst block 203.4 (split 0), worst over splits 0/1/7 and all SHAP 205.2 |
