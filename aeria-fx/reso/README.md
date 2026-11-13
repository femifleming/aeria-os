# AERIA RESO

StoreFX waveguide (Karplus), complex resonator and detuned (allpass) resonator as one effect, MODE on page-2 slot 6 (WAVE / CPLX / DTND).

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed; nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator) running
this source over the stock MAIN OS payloads (`tools/aeria.py`).

FX2 id 0x1e in `manifest.py` is PROVISIONAL: choose a free id at integration.

## Knobs

| slot | name | count | default | what |
|---|---|---|---|---|
| 0  | PTCH | 73 | 24 | resonant pitch in semitones, F#1 (46 Hz) .. F#7 (2.96 kHz) |
| 1  | FINE | 128 | 64 | fine tune +-0.5 semitone (64 = none; clamped at F#1) |
| 2  | DECY | 128 | 80 | fall time to -40 dB, 20 ms .. 20 s (log) |
| 3  | TONE | 128 | 90 | WAVE: loop damping (0 dark); CPLX: input lowpass; DTND: loop lowpass |
| 4  | DTUN | 128 | 40 | WAVE/CPLX: right channel up 0..25 cents; DTND: allpass dispersion |
| 5  | MIX | 128 | 64 | dry/wet; 0 = exact dry |
| 6 (p2) | MODE | 3 | 0 | waveguide / complex resonator / detuned |

Per-mode names/defaults (ModeView): mode 0: names {} defaults {0: 24, 2: 80, 3: 90, 4: 40, 5: 64}; mode 1: names {} defaults {0: 36, 2: 70, 3: 100, 4: 30, 5: 64}; mode 2: names {} defaults {0: 24, 2: 90, 3: 110, 4: 64, 5: 64}

## Design

- WAVE: stereo Karplus-Strong on a [L,R] pair ring; linear-interpolated read at P - h with a
  two-point damping FIR (h = 0..0.5 from TONE, its group delay compensated); per-pass gain from a
  -40 dB fall time; input gain 1.5 sqrt(1 - g^2) (StoreFX); soft clip out. R detuned by DTUN.
- CPLX: stereo complex one-pole resonator (rotation by r e^{jw}), TONE lowpass on the input,
  input scale 1.5 sqrt(1 - r^2)/4, output 4 im through a soft clip. R detuned by DTUN.
- DTND: mono waveguide with three Schroeder allpasses (g 0.3) in the loop at
  P x 0.5 x DTUN x (1, .9981, .9957) + 1 sample (StoreFX), TONE lowpass in the loop, a 1.7 Hz DC
  blocker in the loop (the loop's DC gain is 1/(1 - g)).
- PTCH F#1 (46.25 Hz) .. F#7 in semitones, FINE +-0.5 st; DECY 20 ms .. 20 s to -40 dB (log).
  Periods / coefficients per block through one 2^-y routine (half-angle exponential squared,
  then shifts), sin/cos by Taylor; all smoothed per sample (periods in 48 bits).
- Buffer: four 2,048-word segments (m = $7ff) = 8,192 words. MODE change re-arms a 68-call clear.

## Cost

- Static (tools/build/cycle_count.py, per-sample loop, worst mode): **215 cycles/sample**
- dsp_host (meter minus a null effect, dearest settings, 0.5 FS noise): mean **214.9**, worst block 214.9 instructions/sample (instructions, no memory-contention stalls)
- Budget target: <= 300 per effect (8 effects share ~3,100 on one core)

## Test (`../tools/run.sh python3 /aeria/reso/test.py`)

Result: all checks pass.

| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | PTCH FINE DECY TONE DTUN MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 996 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 215 cycles/sample (worst of 3 mode loops (140/122/215)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 214.9, peak block 214.9 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.499 FS, dc +0.00001, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | WAVE PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -49.1 dBFS, peak 0.199 |
| PASS | WAVE PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 185.0 Hz (+0 c), ring rms -51.1 dBFS, peak 0.116 |
| PASS | WAVE PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 740.0 Hz (+0 c), ring rms -54.2 dBFS, peak 0.055 |
| PASS | WAVE: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -240.0 / -78.7 / -60.6 dBFS |
| PASS | WAVE: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc -0.00007, tail rms -5.8 dBFS |
| PASS | CPLX PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 185.0 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 740.0 Hz (+0 c), ring rms -44.5 dBFS, peak 0.012 |
| PASS | CPLX: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -95.8 / -69.4 / -51.0 dBFS |
| PASS | CPLX: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00077, tail rms -4.0 dBFS |
| PASS | DTND PTCH 12 (92.5 Hz): impulse rings at the note | peak bin 92.5 Hz (+0 c), ring rms -53.6 dBFS, peak 0.092 |
| PASS | DTND PTCH 24 (185.0 Hz): impulse rings at the note | peak bin 183.9 Hz (-10 c), ring rms -55.7 dBFS, peak 0.057 |
| PASS | DTND PTCH 48 (740.0 Hz): impulse rings at the note | peak bin 720.6 Hz (-46 c), ring rms -59.6 dBFS, peak 0.030 |
| PASS | DTND: DECY lengthens the ring (rms 0.5 s after impulse at DECY 0/64/127) | -101.7 / -80.5 / -64.8 dBFS |
| PASS | DTND: 0.95 FS sine at the note for 1 s, DECY 127: bounded, no DC | peak 0.950, dc +0.00016, tail rms -34.2 dBFS |
| info | cost | static 215 cycles/sample (worst mode); dsp_host mean 214.9, worst block 214.9 |

Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement in a real remix, `make check`.

## Known issues / differences from the plugin

- StoreFX snaps pitch to the scale; here PTCH is chromatic (no scale).
- DTND's allpasses lower the pitch by design (as in StoreFX): -46 cents at PTCH 48 with DTUN 0.
- CPLX fed a loud tone at its own pitch saturates its state (bounded, then soft clipped).
- PTCH floor is F#1 (the pair ring holds 1,024 frames); FINE below F#1 is clamped.

## Files

- `reso.asm` the DSP source (generated by `gen.py`; edit gen.py)
- `manifest.py` octabam module manifest (schema-checked by the test)
- `test.py` the gate; `test_results.md` its last result
