# AERIA RING

StoreFX ring modulator (carrier follows the input envelope) and StoreFX phaser (six first-order allpasses, feedback, added to the dry) as one effect, MODE on page-2 slot 6 (RING / PHSR).

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed; nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator) running
this source over the stock MAIN OS payloads (`tools/aeria.py`).

FX2 id 0x6 in `manifest.py` is PROVISIONAL: choose a free id at integration.

## Knobs

| slot | name | count | default | what |
|---|---|---|---|---|
| 0  | FREQ | 128 | 70 | RING carrier 20 Hz..5 kHz (log) / PHSR LFO rate 0.05..10 Hz (log) |
| 1  | FOLW | 128 | 38 | RING: carrier rises with input level (x1..x7) / PHSR: feedback 0..0.85 |
| 2  | SPRD | 128 | 0 | right channel carrier/LFO phase offset 0..180 degrees |
| 3  | BIAS | 128 | 0 | RING: carrier toward 1 (0 ring mod .. 127 nearly dry AM) / PHSR: sweep depth |
| 4  | CNTR | 128 | 64 | PHSR sweep centre on the 200 Hz..5 kHz log axis; unused in RING |
| 5  | MIX | 128 | 127 | dry/wet; 0 = exact dry |
| 6 (p2) | MODE | 2 | 0 | ring modulator / phaser |

Per-mode names/defaults (ModeView): mode 0: names {4: '---'} defaults {0: 70, 1: 38, 2: 0, 3: 0, 5: 127}; mode 1: names {0: 'RATE', 1: 'FDBK', 3: 'DPTH'} defaults {0: 40, 1: 64, 2: 32, 3: 127, 4: 64, 5: 127}

## Design

- Sine: a wrapping 24-bit phase, s = 4p(1 - |p|), y = 0.775 s + 0.225 s|s| (< 0.1 % error).
- RING: carrier 20 Hz .. 5 kHz (log); FOLW multiplies the step by 1 + 6 FOLW min(1, 3 env)
  (env 3 ms / 120 ms, StoreFX's numbers); BIAS pulls the carrier toward 1 (AM); SPRD offsets the
  right carrier 0..180 degrees. Output = x * carrier (StoreFX has 1.4x; unity here to keep headroom).
- PHSR: LFO 0.05 .. 10 Hz; notch sweep 200 Hz .. 5 kHz on a log axis (u = CNTR + DPTH lfo / 2);
  coefficient a = -exp(-2 pi f / fs) (tan-free approximation of (tan - 1)/(tan + 1), exact to second
  order), computed per block for the block's end and glided per sample; FDBK 0..0.85 through
  x - x^3/3 and a 7 Hz DC blocker (StoreFX uses tanh and no DC block); out = 0.7 (x + v).

## Cost

- Static (tools/build/cycle_count.py, per-sample loop, worst mode): **208 cycles/sample**
- dsp_host (meter minus a null effect, dearest settings, 0.5 FS noise): mean **222.8**, worst block 225.2 instructions/sample (instructions, no memory-contention stalls)
- Budget target: <= 300 per effect (8 effects share ~3,100 on one core)

## Test (`../tools/run.sh python3 /aeria/ringph/test.py`)

Result: all checks pass.

| result | check | detail |
|---|---|---|
| PASS | manifest loads (schema: names<=4, defaults in count) | FREQ FOLW SPRD BIAS CNTR MIX MODE |
| PASS | no label is a prefix of another, no duplicates |  |
| PASS | no rep instruction |  |
| PASS | assembles; disassembler round-trip + mpysu audit clean | 665 words |
| PASS | static cycle count (cycle_count.py, worst mode loop) | 208 cycles/sample (worst of 2 mode loops (155/208)) |
| PASS | measured cost at dearest settings, 0.5 FS noise (dsp_host meter minus a null effect: steady mean / worst block) | mean 222.8, peak block 225.2 instr/sample |
| PASS | dearest settings on 0.5 FS noise: bounded, guard clean (no stray writes) | peak 0.600 FS, dc -0.00114, stray 0 |
| PASS | MIX 0 is exact dry (full-range ramp, dearest other knobs) | max |err| 0 LSB |
| PASS | dirty state (garbage r7 block + Y) on silence -> silent tail | worst tail 0 LSB |
| PASS | random knob automation every 3 blocks, splits 0 and 7, guard: bounded | peak 0.700 FS |
| PASS | RING FREQ 64 (carrier 316 Hz): 1 kHz in -> sidebands 684 and 1316 Hz each ~0.25, carrier-free input bin | 0.250 / 0.249, 1 kHz 0.0018 |
| PASS | RING FREQ 100 (carrier 1494 Hz): 1 kHz in -> sidebands 494 and 2494 Hz each ~0.25, carrier-free input bin | 0.250 / 0.249, 1 kHz 0.0022 |
| PASS | RING FOLW 127 with a 0.5 FS input raises the carrier (StoreFX: up to x7) | carrier 173 Hz -> 1232 Hz |
| PASS | RING SPRD 127: right carrier ~180 degrees (L/R correlation near -1) | corr -1.000 |
| PASS | PHSR DPTH 0 FDBK 0: static response has notches (min at least 30 dB under the peak) | min -41.3 dB at 269 Hz, max 2.9 dB |
| PASS | PHSR FDBK 127 on 0.5 FS noise: bounded, DC gain bounded (0.7(x+v): 1.4 at DC; |dc out| <= 1.4 |dc in| + 1e-3) | peak 0.700, dc out -0.00174, dc in -0.00081 |
| PASS | PHSR RATE 127 DPTH 127: the spectrum moves between two 93 ms windows | max band change 30.7 dB |
| info | cost | static 208 cycles/sample (worst mode); dsp_host mean 222.8, worst block 225.2 |

Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement in a real remix, `make check`.

## Known issues / differences from the plugin

- The phaser coefficient approximation moves the notches slightly above ~2 kHz relative to the tan
  form (e.g. -0.49 vs -0.46 at 5 kHz).
- RING's FOLW can push the carrier up to 35 kHz (as StoreFX): it aliases, by design of the source.

## Files

- `ringph.asm` the DSP source (generated by `gen.py`; edit gen.py)
- `manifest.py` octabam module manifest (schema-checked by the test)
- `test.py` the gate; `test_results.md` its last result
