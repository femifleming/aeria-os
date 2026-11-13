# AERIA DIST

StoreFX disto, clipper and degrade as one effect, MODE on page-2 slot 6 (DIST / CLIP / DGRD). No buffer.

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed; nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator) running
this source over the stock MAIN OS payloads (`tools/aeria.py`).

FX2 id 0x1f in `manifest.py` is PROVISIONAL: choose a free id at integration.

## Knobs

| slot | name | count | default | what |
|---|---|---|---|---|
| 0  | DRIV | 128 | 80 | DIST drive (d 0.4..0.0001) / CLIP threshold 0.25..0.001 / DGRD bits 24..2 |
| 1  | TONE | 128 | 70 | DIST slope lowpass / CLIP 2-pole lowpass 100 Hz..15 kHz / DGRD post lowpass |
| 2  | RATE | 128 | 127 | DGRD sample-and-hold rate, 1/100 .. 1 x 44.1 kHz (log); unused otherwise |
| 4  | LEVL | 128 | 64 | output level 0..2x (64 = 1x) |
| 5  | MIX | 128 | 127 | dry/wet; 0 = exact dry |
| 6 (p2) | MODE | 3 | 0 | drive / clipper / degrade |

Per-mode names/defaults (ModeView): mode 0: names {2: '---'} defaults {0: 80, 1: 70, 4: 64, 5: 127}; mode 1: names {0: 'THRS', 2: '---'} defaults {0: 70, 1: 90, 4: 80, 5: 127}; mode 2: names {0: 'BITS'} defaults {0: 90, 1: 127, 2: 60, 4: 64, 5: 127}

## Design

- DIST: s = sign(x) G|x| / (G|x| + 1), G = 1/d, d = 0.4 - 0.3999 DRIV. Same value as StoreFX's
  atan2(x, d) * 2/pi at G|x| = 1 and the same limits; computed as U / (U + 2^-14) with U kept in 48
  bits and one unrolled 24-step fractional divide per channel. Then the "slope" one-pole lowpass.
- CLIP: clamp(x, +-t) * 0.33/t, t 0.25 .. 0.001 (log), then two one-pole lowpasses 100 Hz .. 15 kHz
  (StoreFX uses a Butterworth SVF; two one-poles are cheaper and need no tan).
- DGRD: bits 24..2 (rounded quantiser: add half an LSB, mask), sample-and-hold at 1 .. 1/100 of
  44.1 kHz (log), TONE post lowpass.
- LEVL 0..2x. All continuous controls smoothed per sample; MODE change resets state and snaps.

## Cost

- Static (tools/build/cycle_count.py, per-sample loop, worst mode): **148 cycles/sample**
- dsp_host (meter minus a null effect, dearest settings, 0.5 FS noise): mean **150.9**, worst block 152.4 instructions/sample (instructions, no memory-contention stalls)
- Budget target: <= 300 per effect (8 effects share ~3,100 on one core)

## Test (`../tools/run.sh python3 /aeria/disto/test.py`)

Result: all checks pass.

| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | DRIV TONE RATE LEVL MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 647 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 148 cycles/sample (worst of 3 mode loops (148/112/100)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 150.9, peak block 152.4 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.742 FS, dc -0.00213, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 1.000 FS |
| PASS | DIST: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.615 harm 7.92e-02 pk 0.556 dc -0.00000; DRIV 64: fund 0.822 harm 1.56e-01 pk 0.714 dc -0.00000; DRIV 127: fund 1.260 harm 5.32e-01 pk 0.994 dc -0.00000 |
| PASS | CLIP: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.402 harm 9.34e-02 pk 0.330 dc -0.00000; DRIV 64: fund 0.420 harm 1.83e-01 pk 0.330 dc -0.00000; DRIV 127: fund 0.420 harm 1.83e-01 pk 0.330 dc -0.00000 |
| PASS | DGRD: 0.5 FS 441 Hz sine, DRIV 0/64/127 -> harmonics grow, bounded, no DC, L==R | DRIV 0: fund 0.500 harm 6.72e-08 pk 0.500 dc -0.00000; DRIV 64: fund 0.500 harm 2.74e-05 pk 0.500 dc -0.00000; DRIV 127: fund 0.548 harm 1.47e-01 pk 0.500 dc -0.00000 |
| PASS | DIST transfer: DRIV 0, x = 0.4 FS -> G|x|/(G|x|+1) = 0.5 | out 0.50049 vs 0.50000 |
| PASS | DGRD RATE 0: sample-and-hold at ~441 Hz (changes ~once per 100 samples) | 50 level changes in 4410 samples |
| PASS | DGRD DRIV 0 RATE 127 TONE 127: 24-bit, full rate -> output equals input | max |err| 2 LSB |
| PASS | stereo: L-only input leaves R silent | R peak 0 LSB, L peak 0.994 |
| info | cost | static 148 cycles/sample (worst mode); dsp_host mean 150.9, worst block 152.4 |

Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement in a real remix, `make check`.

## Known issues / differences from the plugin

- u/(1+u) is atan-like, not atan (slightly softer knee; same endpoints).
- No oversampling or ADAA: DIST/CLIP alias at high drive like the float original; DGRD aliases on
  purpose. `fx:audit` aliasing not run.
- Page-1 slot 3 is unused (blank).

## Files

- `disto.asm` the DSP source (generated by `gen.py`; edit gen.py)
- `manifest.py` octabam module manifest (schema-checked by the test)
- `test.py` the gate; `test_results.md` its last result
