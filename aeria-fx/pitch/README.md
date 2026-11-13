# AERIA PITCH

Two-head delay-line pitch shifter: a port of `fp::PitchShifter` (Cloudfin / Pinkie Pie's Balloon Shop `PitchShifter.h`).

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed; nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator) running
this source over the stock MAIN OS payloads (`tools/aeria.py`).

FX2 id 0x1b in `manifest.py` is PROVISIONAL: choose a free id at integration.

## Knobs

| slot | name | count | default | what |
|---|---|---|---|---|
| 0  | SHFT | 49 | 36 | transpose in semitones, -24..+24 (24 = unison) |
| 1  | FINE | 128 | 64 | fine tune, +-1 semitone, 64 = none |
| 2  | GRN | 128 | 77 | window 10..160 ms (squared taper, 77 = 65 ms); glides ~1.5 s |
| 3  | FDBK | 128 | 0 | shifted signal back into the line, 0..0.95; repeats climb/fall |
| 4  | TONE | 128 | 100 | wet + feedback lowpass, 210 Hz .. open at 127 |
| 5  | MIX | 128 | 64 | dry/wet; 0 = exact dry, 127 = wet |

## Design

- Two read heads half a window apart sweep the window at (1 - ratio) samples per sample.
  Gains: smoothstep(tri(phase)) for head 1 and 1 - that for head 2 (sum exactly 1; the cheap twin
  of the source's sin^2 law). The head gain is folded into the linear-interpolation weights, so a
  head costs two multiply-accumulates per channel.
- Ratio 2^((SHFT + FINE)/12): per block, Taylor 2^(x/8) and three squarings (error < 0.2 cent);
  head speed checked from the DSP's own state for all 49 SHFT values (worst 1.6 cents).
- Feedback (source: DC 8 Hz, LP 9 kHz, HP 60 Hz, softClip): wet -> TONE one-pole lowpass ->
  DC block (10 Hz) -> x - x^3/3 (unity slope, bounded at 2/3) -> FDBK (0..0.95) -> line.
- Buffer: the FX2 allocator buffer as one interleaved [L,R] ring of 15,870 words (m = $3dfd),
  never touching +$3DFE.. (Sidechain key window). Longest read 14,124 words back.
- Smoothing per sample: head speed (48-bit, 23 ms), window (48-bit, ~1.5 s), feedback, tone,
  mix (asr 8, mix lands exactly on its target outside the loop). 128 dry calls clear the ring.

## Cost

- Static (tools/build/cycle_count.py, per-sample loop, worst mode): **204 cycles/sample**
- dsp_host (meter minus a null effect, dearest settings, 0.5 FS noise): mean **195.4**, worst block 195.4 instructions/sample (instructions, no memory-contention stalls)
- Budget target: <= 300 per effect (8 effects share ~3,100 on one core)

## Test (`../tools/run.sh python3 /aeria/pitch/test.py`)

Result: all checks pass.

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

Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement in a real remix, `make check`.

## Known issues / differences from the plugin

- Linear interpolation, not Hermite (Hermite would add ~40 cycles; aliasing not measured).
- GRN glides (~1.5 s time constant) instead of Cloudfin's two-voice crossfade, so moving GRN bends
  the pitch slightly while it moves (up to ~1 semitone during a full-range jump).
- A pure sine shows the usual delay-line-shifter sidebands at the crossfade rate; the spectral
  peak sits within one crossfade-rate sideband of the target (tested), the head speed is exact.

## Files

- `pitch.asm` the DSP source
- `manifest.py` octabam module manifest (schema-checked by the test)
- `test.py` the gate; `test_results.md` its last result
