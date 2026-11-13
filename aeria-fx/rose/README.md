# AERIA ROSE

A resonator tuned by church-window geometry and cymatics. Eight two-pole resonators ring at
ROOT × a set of ratios. The set comes from a geometry family (SHAP) at a symmetry order (FOLD,
3 = trefoil .. 12 = twelve-fold rose), voiced as the geometry gives it (TUNE OPEN) or folded into a
two-octave chord (TUNE CLSE). It has no delay buffer, so it fits FX1 or FX2.

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed. Nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator)
running this source over the stock MAIN OS payloads (`tools/aeria.py`, with the P table placed
first as the package places it).

FX id 0x1b in `manifest.py` is PROVISIONAL. It is everb's preferred id (ids 1-31 are all taken by
stock, the built modules or the other AERIA packages). Choose a free id at integration. On a clash
the base moves the later install to the lowest free id, so look the effect up by name.

No research was attached to the request, so the mapping below was designed from first principles.
Each choice is stated so you can swap any family.

## Knobs

| slot | name | count | default | what |
|---|---|---|---|---|
| 0 | ROOT | 61 | 24 (C3) | root = ratio 1, C1 (32.7 Hz) .. C6 (1047 Hz), semitones (WIDE dial, note names) |
| 1 | SHAP | 6 | 4 (ROSE) | geometry family: HARM PRIM CIRC SQRE ROSE GOTH |
| 2 | FOLD | 10 | 5 (n = 8) | symmetry order n = 3 .. 12 |
| 3 | DECY | 128 | 80 | ring time to -60 dB, 50 ms .. 12 s (log; T60 = 0.05 s × 240^(v/127)) |
| 4 | BRIT | 128 | 64 | decay tilt. 0: high modes die fast (stone, decay rate ×2 per octave above ROOT). 64: even. 127: high modes ring on (glass, ×0.5 per octave). Clamped to 50 ms .. 13 s per mode |
| 5 | MIX | 128 | 64 | dry/wet; 0 = exact dry |
| 6 (p2) | SPRD | 128 | 90 | stereo: 0 mono (L = R exactly), 127 even modes left, odd modes right |
| 7 | — | | | blank |
| 8 (p2) | ROTA | 128 | 0 | light moving round the panes: 0 off; depth 0.9·min(1, 8x), rate 3 Hz·x⁴ (x = v/128: 16 ≈ static tilt, 64 ≈ 0.19 Hz, 96 ≈ 0.95 Hz, 127 ≈ 2.9 Hz) |
| 9 | — | | | blank |
| 10 (p2) | TUNE | 2 | 0 | OPEN: the ratios as the geometry gives them. CLSE: every ratio octave-reduced into [1, 4), a close-voiced chord |
| 11 | — | | | blank |

The page-2 controls sit on even slots, the proven knob fields (`docs/remixer/MODULES.md`), as GRAIN
does.

## The mapping: geometry to frequency

Each SHAP × FOLD gives eight ratios over ROOT, sorted ascending. Pane k (mode k) is the k-th ratio,
and every set starts at 1 = ROOT. At run time any mode above 4 kHz is folded down by octaves
(root-dependent), so high sets turn into chords instead of whistles. The ratio lists are computed in
`gen.py` (`candidates()`), not hand-typed, except GOTH's chords. `ui_notes.md` has the complete
table for both TUNEs, with each mode's geometric tag.

- **HARM (harmonic rose).** A rose curve r = cos(nθ) is exactly two counter-rotating phasors at
  n−1 and n+1 times the base rate. An n-fold symmetric figure therefore only couples harmonics
  h ≡ ±1 (mod n). The set is the root plus the harmonics j·n ± 1. n = 4 gives the odd harmonics
  (clarinet-like). n = 3 gives the harmonics with no multiples of 3. High n gives sparse, high,
  bell-like partials.
- **PRIM (prime partials).** The root plus seven consecutive prime harmonics. The window starts at
  the (n−2)-th prime: n = 3 gives 2 3 5 7 11 13 17, and n = 12 gives 29 .. 53. FOLD climbs the primes.
- **CIRC (circular Chladni plate).** Modes of a thin circular plate, f ∝ j(m,s)², where j(m,s) is
  the s-th zero of the Bessel function J_m. A mode with m nodal diameters has m-fold symmetry, so for
  FOLD n the modes with m ∈ {0, n, 2n, 3n} are used: exactly the patterns with n-fold symmetry. The
  set is the eight lowest of those, over the (0,1) mode. These are inharmonic, gong and bell spectra.
  The Bessel zeros are computed in gen.py (integral form, bisection).
- **SQRE (square Chladni plate).** Modes (a, b) of a square plate, f ∝ a² + b², over the plate
  fundamental (1,1) = ROOT. FOLD climbs the ladder of distinct a² + b² values: n = 3 starts at (2,1),
  and each step up the ladder adds more nodal lines, so the Chladni figures get more complex. A square
  has no n-fold symmetry for most n, so here FOLD means figure complexity, not symmetry.
- **ROSE (rose-window tracery).** An n-petal rose window is laid out on a regular 2n-gon (petal tips
  plus cusps). Its chord lengths c_k = sin(kπ/2n) / sin(π/2n) (side = 1) are the window's own
  proportions. For example, the pentagon family gives φ = 1.618 and φ² = 2.618, and the octagon family
  gives 1 + √2 relatives. The set is the eight smallest products of chords (the chords form a
  multiplicative ring, so a chord of chords is another proportion in the same window). As n grows,
  c_k → k: the more petals, the closer the window comes to the harmonic series.
- **GOTH (just-intonation chord stacks).** One chord per FOLD: 3 organum (3-limit), 4 major, 5 minor,
  6 major 7th, 7 harmonic 7th (7-limit), 8 quartal, 9 major 9th, 10 minor 9th, 11 undecimal otonal,
  12 13-limit otonal (harmonics 8..15). For these, FOLD names a prime limit or chord, not a symmetry.
- **TUNE CLSE.** Every candidate of the family is octave-reduced into [1, 4) and duplicates within 1%
  are dropped, until there are eight. A set that runs short (the JI stacks and SQRE) continues with
  products of its own tones (combination tones). The CLSE SQRE sets are nearly the same for every
  FOLD, because the plate ladder reduces to a few octave classes.

### Ratio lists (TUNE OPEN)

**HARM** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 2, 4, 5, 7, 8, 10, 11 |
| 4 | 1, 3, 5, 7, 9, 11, 13, 15 |
| 5 | 1, 4, 6, 9, 11, 14, 16, 19 |
| 6 | 1, 5, 7, 11, 13, 17, 19, 23 |
| 7 | 1, 6, 8, 13, 15, 20, 22, 27 |
| 8 | 1, 7, 9, 15, 17, 23, 25, 31 |
| 9 | 1, 8, 10, 17, 19, 26, 28, 35 |
| 10 | 1, 9, 11, 19, 21, 29, 31, 39 |
| 11 | 1, 10, 12, 21, 23, 32, 34, 43 |
| 12 | 1, 11, 13, 23, 25, 35, 37, 47 |

**PRIM** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 2, 3, 5, 7, 11, 13, 17 |
| 4 | 1, 3, 5, 7, 11, 13, 17, 19 |
| 5 | 1, 5, 7, 11, 13, 17, 19, 23 |
| 6 | 1, 7, 11, 13, 17, 19, 23, 29 |
| 7 | 1, 11, 13, 17, 19, 23, 29, 31 |
| 8 | 1, 13, 17, 19, 23, 29, 31, 37 |
| 9 | 1, 17, 19, 23, 29, 31, 37, 41 |
| 10 | 1, 19, 23, 29, 31, 37, 41, 43 |
| 11 | 1, 23, 29, 31, 37, 41, 43, 47 |
| 12 | 1, 29, 31, 37, 41, 43, 47, 53 |

**CIRC** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 5.269, 7.039, 12.949, 16.475, 17.071, 24.042, 29.291 |
| 4 | 1, 5.269, 9.957, 12.949, 21.170, 24.042, 25.843, 35.719 |
| 5 | 1, 5.269, 12.949, 13.304, 24.042, 26.325, 36.233, 38.548 |
| 6 | 1, 5.269, 12.949, 17.071, 24.042, 31.932, 38.548, 48.214 |
| 7 | 1, 5.269, 12.949, 21.253, 24.042, 37.984, 38.548, 56.468 |
| 8 | 1, 5.269, 12.949, 24.042, 25.843, 38.548, 44.476, 56.468 |
| 9 | 1, 5.269, 12.949, 24.042, 30.837, 38.548, 51.401, 56.468 |
| 10 | 1, 5.269, 12.949, 24.042, 36.233, 38.548, 56.468, 58.755 |
| 11 | 1, 5.269, 12.949, 24.042, 38.548, 42.026, 56.468, 66.535 |
| 12 | 1, 5.269, 12.949, 24.042, 38.548, 48.214, 56.468, 74.737 |

**SQRE** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 2.5, 4, 5, 6.5, 8.5, 9, 10 |
| 4 | 1, 4, 5, 6.5, 8.5, 9, 10, 12.5 |
| 5 | 1, 5, 6.5, 8.5, 9, 10, 12.5, 13 |
| 6 | 1, 6.5, 8.5, 9, 10, 12.5, 13, 14.5 |
| 7 | 1, 8.5, 9, 10, 12.5, 13, 14.5, 16 |
| 8 | 1, 9, 10, 12.5, 13, 14.5, 16, 17 |
| 9 | 1, 10, 12.5, 13, 14.5, 16, 17, 18.5 |
| 10 | 1, 12.5, 13, 14.5, 16, 17, 18.5, 20 |
| 11 | 1, 13, 14.5, 16, 17, 18.5, 20, 20.5 |
| 12 | 1, 14.5, 16, 17, 18.5, 20, 20.5, 22.5 |

**ROSE** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 1.732, 2, 3, 3.464, 4, 5.196, 6 |
| 4 | 1, 1.848, 2.414, 2.613, 3.414, 4.461, 4.828, 5.828 |
| 5 | 1, 1.902, 2.618, 3.078, 3.236, 3.618, 4.980, 5.854 |
| 6 | 1, 1.932, 2.732, 3.346, 3.732, 3.864, 5.278, 6.464 |
| 7 | 1, 1.950, 2.802, 3.514, 3.802, 4.049, 4.381, 4.494 |
| 8 | 1, 1.962, 2.848, 3.625, 3.848, 4.262, 4.736, 5.027 |
| 9 | 1, 1.970, 2.879, 3.702, 3.879, 4.411, 4.987, 5.411 |
| 10 | 1, 1.975, 2.902, 3.757, 3.902, 4.520, 5.172, 5.696 |
| 11 | 1, 1.980, 2.919, 3.799, 3.919, 4.601, 5.310, 5.779 |
| 12 | 1, 1.983, 2.932, 3.831, 3.932, 4.664, 5.417, 5.814 |

**GOTH** (OPEN)

| n | ratios over ROOT (panes 0..7) |
|---|---|
| 3 | 1, 1.5, 2, 3, 4, 4.5, 6, 8 |
| 4 | 1, 1.25, 1.5, 2, 2.5, 3, 4, 5 |
| 5 | 1, 1.2, 1.5, 2, 2.4, 3, 4, 4.8 |
| 6 | 1, 1.25, 1.5, 1.875, 2, 2.5, 3, 3.75 |
| 7 | 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5 |
| 8 | 1, 1.333, 1.778, 2, 2.667, 3.556, 4, 5.333 |
| 9 | 1, 1.25, 1.5, 1.875, 2.25, 3, 3.75, 4.5 |
| 10 | 1, 1.2, 1.5, 1.8, 2.25, 2.4, 3, 3.6 |
| 11 | 1, 1.125, 1.25, 1.375, 1.5, 1.75, 2, 2.75 |
| 12 | 1, 1.125, 1.25, 1.375, 1.5, 1.625, 1.75, 1.875 |

## Design

- **Resonators.** Coupled form (a complex one-pole): re' = u + r cos w·re − r sin w·im,
  im' = r sin w·re + r cos w·im. The decay r is independent of frequency, and the low-frequency
  precision is good (sin w is stored directly). |r| ≤ 1 − 1.75e-5. sin and cos are Taylor series to
  w⁷ and w⁸ for w ≤ 2π·4 kHz/fs, so the rotation never exceeds unit magnitude. State saturates on
  store (move limiting), so a loud tone held at a mode is bounded, then soft clipped.
- **Coefficients.** The 16-call round robin runs only on the main (a ≠ 0) call; the trig-split
  sub-call runs only the sample loop. Even block 2k: mode k's frequency (2^-y from the P table's
  log2 ratio, octave fold above 4 kHz, sin/cos) and the global input gain g = c·√(1 − r0²) from DECY,
  with c = 1/8 for internal headroom and ×8 at the output. Odd block 2k+1: mode k's decay
  d = 2^−(Y0 − β·octaves), clamped, r = 2^−d by a cubic, its targets r cos w and r sin w, and its ROTA
  pane gain. Each call, half the modes glide toward their targets by 1/16 of the distance, about 12 ms.
  So SHAP, FOLD, TUNE, ROOT, DECY and BRIT changes retune the bank smoothly. They are never switched,
  and they never click (see the switch checks).
- **Sample.** mono = (L + R)/2 → 7 Hz DC block → u = g·mono → 8 modes → A = Σ even G_k·im_k,
  B = Σ odd G_k·im_k → L = A + B − s·B, R = A + B − s·A (s = SPRD, smoothed per block) → ×8 → soft clip
  s − s³/3 of the limited value (|y| ≤ 2/3) → dry/wet (per-sample smoothed, snapped). MIX 0 is
  bit-exact dry.
- **State.** 128 words of the r7 block ($00..$7F: globals at $00..$15, modes at $40 + 8k). init
  clears all 128. There is no Y buffer, no buffer claim, and so it can go on FX1 or FX2.
- **P table.** 960 words: 2 TUNE × 6 SHAP × 10 FOLD × 8 log2(ratio)/32. It is the manifest's ptable,
  placed before the code, and the source names its base with the package literal.

## Cost

- Static (cycle_count.py, the sample loop): **178 cycles/sample**
- dsp_host, dearest settings, 0.5 FS noise (meter minus a null effect): mean **195.5**, worst block
  197.4 instructions/sample at split 0. **Worst over SHAP 0..5 × trig splits 0/1/7: 199.2.**
- Declared in the package: 200 (executed). The base charges 7/4 of it, about 350. Hardware has
  measured 1.42-1.74× the claimed figures for other effects.
- The sample loop is 8 × 11 (resonators) + 24 (pane gains and buses) + 7 (stereo) + 2 × 19 (clip
  and mix) + input, DC block and mix glide. The per-block work averages about 17/sample.

## Test (`../tools/run.sh python3 /aeria/rose/test.py`)

Result: all checks pass (`test_results.md`). Besides the standard gate (manifest, labels, no `rep`,
assembly with the round-trip and mpysu audit, static cycles, cost, bounded with the guard clean,
MIX 0 exact dry, dirty state, random automation with splits 0 and 7), the gate checks:

- Every SHAP at FOLD 3/5/8/12 (OPEN), and every SHAP at FOLD 7 (CLSE): the impulse response has a
  peak within ±15 cents of every expected mode (ROOT × ratio, folded above 4 kHz). All 30 sets land
  at +0 cents. Each mode is ≥ 12 dB over the loudest between-mode probe; the measured margins are
  54.6-94.4 dB.
- SPRD 127: each mode is 69-86 dB louder on its own side. SPRD 0: L == R exactly.
- DECY lengthens the ring, and BRIT tilts it (top mode vs root: −31.6 / −0.1 / +6.5 dB at
  BRIT 0/64/127).
- A 0.95 FS sine at a mode for 1.6 s at DECY 127 and BRIT 127 (HARM, CIRC, ROSE): bounded (peak
  ≤ 0.83), no DC, and the ring decays after the sine stops.
- SHAP, FOLD, ROOT, TUNE, DECY and ROTA switched under quiet noise: the second difference after each
  switch stays within 2× of the settled stretches (no click).
- ROTA 127 swings a mode's level by 11.2 dB under a steady sine (0.1 dB at ROTA 0).

Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement, `make check`.

## Known issues

- Modes above 4 kHz fold down by octaves at run time. Two modes can land within a few cents of each
  other (they then beat or reinforce). The CLSE sets avoid this; OPEN does not.
- PRIM and HARM at high FOLD and CIRC are dense after folding. They sound like chords or bells by
  design, not like the clean series.
- BRIT is a decay tilt only. All modes get the same input gain, so dark settings are also quieter.
- The ROTA phase is not published to the ColdFire. A UI animation can match the rate, not the phase.
- The input gain follows DECY only (sqrt normalisation of the global decay). Long DECY on loud
  sustained input saturates the resonator state. It stays bounded and soft clipped, like RESO CPLX.
- Per-mode coefficient updates come every 16 blocks (5.8 ms), smoothed over about 12 ms. A fast LFO
  on ROOT will sound stepped-glided, not continuous.

## Files

- `gen.py` writes `rose.asm`, `rose_table.json` (the P table and every ratio set) and
  `ui_notes.md`. Edit gen.py, not the outputs.
- `manifest.py` is the octabam module manifest. Its ptable is read from rose_table.json.
- `test.py` is the gate; `test_results.md` is its last result. `.cost.json` holds the cost figures
  the packager declares.
- `package.sh`, `run_pkg.sh` and `pkg/` (recipe.py, build.py, verify.py) are head/bs_recipes.py,
  bs_package.py and bs_verify.py for this one row, with the P table added. They write
  `~/octa/build/fx-packages/aeria-rose.mwrm`.
- `ui_notes.md` is for the AERIA C UI: the slot order, the pane geometry and the exact ratio table
  per TUNE × SHAP × FOLD.
