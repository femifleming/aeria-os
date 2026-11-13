# AERIA FX: stereo width pass (12 Nov 2026)

Request: "i love wide stereo effects too, if there any effects that can utilize that, do it. if there
are effects that make sense to be mono then make them mono as well."

Why it matters most in the sequencer: there every track reaches its effect MONO (left copied to right)
and is then panned by the track's L/R gains, so an effect whose two outputs differ is the only width a
track has. In the multi-effect mode the input is real stereo and every effect here keeps the two sides'
audio paths separate (none of them sums a stereo input to mono on the way through, except where its
header says so: GRAINS, SMEAR, ROSE, RESO DTND and ECHO feed from L+R).

Nothing here has been on hardware. Every figure is from the emulator gate of the effect
(`<fx>/test.py`, dsp_host in the `modwerk-source-tools` image) and is in its `test_results.md`.

## How width is measured (`tools/aeria.py: stereo_report`)

The effect fully wet, a MONO input (identical on both sides), two signals: a decaying white-noise burst
(0.7 FS, 150 ms time constant) and a steady 0.5 FS sine.

- **corr**: zero-lag correlation of output L and R. 1.000 = the same signal twice (mono), 0 = unrelated.
- **L+R re L**: level of the mono sum against one side. +6.0 dB = identical sides, +3.0 dB = unrelated
  sides, below 0 = the sides are cancelling. The gate for a "wide" effect: correlation clearly under 1
  AND the sum no more than 3 dB under either side. For a "mono" one: L = R sample for sample.
- bounded (peak <= 1.0 FS), and on the sine no sample step larger than the highest partial the effect
  makes could produce (a click would be one).

## Audit

"cost" = the EFFECTS number in `aeria-os/fx/gen_fx.py` (instructions a sample at the dearest settings).

| effect | before this pass | now | what was done | new knob | cost before -> now | corr burst / sine | L+R re L, burst / sine |
|---|---|---|---|---|---|---|---|
| PITCH | dual mono: both sides read the same two heads | **wide** | the right side has its own pair of heads; left runs WIDE/2 sharp, right WIDE/2 flat (micro-detune) | WIDE, FUNC layer knob 1 (slot 6), default 40 = 15.9 c, range 0..50 c | 204 -> **280** (measured 195 -> 260) | -0.001 / -0.009 | +3.0 / +3.0 dB |
| DIST (DIST/CLIP/DGRD) | two independent identical paths, shared S&H clock | **mono by nature**, unchanged | nothing: equal inputs give equal outputs bit for bit; a stereo input keeps its image | - | 152 | 1.000 / 1.000 (L = R exactly) | +6.02 / +6.02 dB |
| GRAINS | random pan per grain, but only as much as SPRY (default 30: nearly centred) | **wide** | pan split off SPRY onto its own SPRD; pans reach hard L/R; the pan also moves the grain's start +-5 ms | SPRD, FUNC layer knob 5 (slot 10), default 100 | 304 -> 304 (measured 303.8 -> 304.4; static worst case 606 -> 638) | +0.04 / +0.35 (long build), +0.08 / +0.20 (test build) | +0.7 / +3.4 dB (long), +2.3 / +3.5 dB (test) |
| RING (RING) | SPRD existed, default 0 = mono; at 127 right = -left (mono sum: nothing) | **wide** | SPRD default 127; in RING it now spans 0..90 degrees, so the top is quadrature carriers | SPRD (existing, main knob 3), default 0 -> 127 | 225 -> 225 | +0.017 / +0.012 | +3.1 / +3.1 dB |
| RING (PHSR) | same knob: right LFO 0..180 degrees | **wide** when DPTH (the BIAS knob) is up | default only (127 = opposite sweeps) | as above | as above | +0.59 / +0.59 (RATE 40 FDBK 64 DPTH 127) | +5.9 / +5.4 dB |
| RESO (WAVE, CPLX) | already wide: right string detuned by DTUN (default 40 = 7.9 c) | wide, unchanged | nothing | - (DTUN is the width) | - | WAVE +0.05 / +0.71, CPLX +0.55 / +0.98 (sine AT the note) | WAVE +2.8 / +5.1, CPLX +4.8 / +6.0 dB |
| RESO (DTND) | mono: one string to both sides | **wide** (moderate, fixed) | partials placed left/right: left = v + 0.5 (u1 - u2), right = v - 0.5 (u1 - u2) | none free; grows with DTUN (allpass length) | 215 -> **226** (measured 215.0 -> 224.7) | +0.46 (DTUN 40), +0.54 (127) / a pan (see below) | +5.2 / +7.9 dB; L+R = 2 x the old output |
| SMEAR | already wide: opposite-phase modulation on the tank's two halves, different taps a side | wide, unchanged | nothing | - | 269 | -0.005 / -0.98 (440 Hz, see "Observations") | +3.0 / -6.3 dB |
| ROSE | already wide: even panes left, odd panes right, SPRD on the FUNC layer (default 90) | wide, unchanged | nothing | - | 199 | +0.55 / +0.94 at SPRD 90; -0.004 / -0.04 at SPRD 127 | +5.6 / +5.4 dB; SPRD 127: +3.8 / +0.3 dB |
| COMP | already linked: detector = max(abs L, abs R), one envelope, one gain | **mono-linked**, unchanged | nothing | - | 208 | 1.000 / 1.000 (L = R exactly) | +6.02 / +6.02 dB |
| VERB | already wide: lines alternate left/right, two output sign patterns | wide, unchanged (not touched: cost) | nothing | - | 1145 | -0.17 / -0.35 | +2.3 / -0.4 dB |
| ECHO | already wide: two hard-panned lines with different tap ratios, WDTH knob (default 127) | wide, unchanged | nothing | - | 259 | +0.03 / +0.34 | +3.2 / +1.8 dB |
| VOWL | dual mono: one coefficient set for both sides | **wide** (moderate) | every formant lower on the left, higher on the right; a coefficient set a side, tuned on alternate calls | WIDE, main knob 5 (slot 4, was blank), default 80 | 648 -> **657** (measured 648.0 -> 656.9) | +0.66 / +0.47 (sine on F1, 650 Hz) | +5.4 / +5.0 dB |

## The changed effects

### PITCH: stereo micro-detune (`pitch/pitch.asm`, hand-written; backup `pitch.asm.prewide`)

- The two channels no longer share their read heads. One phase p as before; a 48-bit offset phi creeps
  by dphi = ratio x eps / (2 W) a sample; the left pair of heads runs at p - phi, the right at p + phi.
  So left plays eps/2 sharp and right eps/2 flat of the set pitch, WIDE 0..127 = 0..50 cents apart
  (40 = 15.9 c: +-8 c, the classic "micro-shift"). It works at unison too (SHFT 0: a detune chorus).
- Checked: left/right spectral peaks +8.0 / -8.0 c at WIDE 40 and +25.0 / -25.0 c at WIDE 127
  (SHFT 0, +12, -7; within 0.5 c of the law).
- Mono: two copies a few cents apart beat slowly; they never cancel. Sum +3.0 dB over either side on
  noise and on a sine.
- WIDE 0: phi walks back to 0 the short way round (1/256 a block, ~0.1 s time constant, a brief glide
  of the right side, no click: tested) and then right = left bit for bit on equal inputs.
- The set pitch is the mean of the two sides; the old pitch-accuracy checks now run at WIDE 0 (changed
  expectation: with WIDE up the left peak sits above and the right below, by design).
- **Cost: the one real increase of this pass.** A second pair of heads is 52 instructions (gains, two
  addresses, weights) plus the phase arithmetic. To pay part of it back, three smoothers that ran every
  sample (head speed, feedback, tone coefficient) now run once per block with the same time constants
  (2^-6 and 2^-4 a block instead of 2^-10 and 2^-8 a sample); the window glide and the mix stay per
  sample. Net: 195.4 -> 260.2 instructions a sample measured (static count 204 -> 280, the number the
  table carries). If that is too dear for core 0's budget, `pitch.asm.prewide` + `manifest.py.prewide`
  are the old effect; cheaper widenings exist (a chorus-style bounded delay offset on the right with
  shared head gains would be about +12 %, a plain L/R split of the two heads about free) but neither
  sounds like a real detuned pair, and the split is not wide at unison.
- Other behaviour change from the block-rate smoothers: a full-range jump of FDBK now lands in 16
  steps of a block instead of a per-sample glide (same ~6 ms). Not audible in the emulator renders;
  not heard on hardware.

### GRAINS: SPRD (`grains/gen.py`, both builds; backups `gen.py.prewide`, `grains*.asm.prewide`)

- Pan used to be SPRY/2 x rnd: at the default SPRY 30 every grain sat within 12 % of the centre, and
  width could not be had without also spraying the start positions over +-94 ms.
- Now SPRD (FUNC layer, page-2 slot 10, r6+$e bits 22..16) is the pan reach, alone:
  s = SPRD x rnd held to +-1/2, gL = g (1/2 + s), gR = g (1/2 - s). Up to 64 the pans fill the field
  evenly; above it more and more grains sit hard left or hard right (127: 54 % of them, measured;
  left and right get equal shares). gL + gR = g for every grain (checked on the DSP's own gain words,
  +-1 LSB), so **the mono sum does not depend on SPRD at all**.
- The same random number also moves the grain's start by SPRD x 5 ms (left = older). Without this, at
  unison with SPRY 0 all four grains read the same instant and the two sides would only differ in
  level; with it they differ in time as well (corr +0.21 / +0.26 with SPRY 0, DENS 127, SPRD 100).
- SPRD 0: every grain in the middle, starts untouched: L = R exactly, and the builds reproduce the
  pre-table reference renders as before (those rows of the gate are unchanged).
- SPRY keeps its name and law but **no longer pans** (changed expectation; the old "SPRY 127
  decorrelates" check became SPRD checks). All single-grain checks in the gate run at SPRD 0.
- A short hit is replayed by one to four grains, each somewhere in the field, so one hit can land well
  to one side (the burst rows show up to 8 dB between the sides): that is what a wide granular does;
  turn SPRD down for a steadier image.
- Cost: six instructions at a grain restart only. Worst block 303.8 -> 304.4. The legacy AGE=24
  builds are untouched (`gen.py RING=test AGE=24 WINFIX=0` still regenerates `grains.asm.handpatched`
  byte for byte).

### RING / PHSR: SPRD default and range (`ringph/gen.py`)

- RING: the right carrier's lead is now 0..90 degrees (was 0..180). At 127 the carriers are sin and
  cos: for any input each sideband is +-90 degrees between the sides (corr 0) and the mono sum is the
  same ring 3 dB up. The old top (180 degrees) was right = -left: correlation -1.000 and a mono sum of
  nothing; that polarity flip is no longer reachable. Measured lead at SPRD 0 / 64 / 127: 0.0 / 44.8 /
  89.1 degrees. At a low FREQ this reads as a rotating tremolo.
- PHSR: unchanged law (right LFO 0..180 degrees); 127 = the two sides sweep opposite ways.
- Default 0 -> 127. SPRD 0 is still L = R exactly in both modes.
- Changed expectation in the gate: "SPRD 127: correlation near -1" became "quadrature: correlation ~0,
  mono sum +3 dB". The display law in gen_fx.py reads 0..90DEG (the first mode).
- No per-sample cost (the halving is done once per block).
- Note: in AERIA OS the knobs take their first-mode defaults, so BIAS (= DPTH in PHSR) starts at 0 and a
  phaser with no sweep depth has nothing to spread. Turn BIAS up in PHSR.

### RESO DTND: partials left and right (`reso/gen.py`)

- WAVE and CPLX were already stereo (right detuned by DTUN) and are untouched.
- DTND was one string to both sides. Now, with v the loop signal after the third allpass (the old
  output) and u1, u2 the same string after the first and second: left = v + 0.5 (u1 - u2),
  right = v - 0.5 (u1 - u2). u1 - u2 is what the second allpass changes, so each partial leans left or
  right by its own phase across that allpass, the lows (where a short allpass changes nothing) stay
  in the middle, and **left + right = 2 v: the mono sum is the old output** (difference -83 dB or
  better on a quiet burst at DTUN 0 / 40 / 127; each side then goes through its own soft clip).
- There is no free knob in RESO; the amount is fixed (0.5) and the spread follows DTUN through the
  allpass length. DTUN 0 leaves 1-sample allpasses: corr +0.98, mono but for the top octave (not bit
  exact: the one mode where "0" is not exactly L = R).
- A single steady sine is one partial, so it is PANNED rather than decorrelated (e.g. DTUN 127 at the
  note: left -24.6, right -40.4 dBFS, sum +0.15 dB re left): its sine rows are gated on the mono sum only.
- Cost: +21 instructions in the DTND loop (two tap stores, the side signal, a second soft clip), partly
  paid back by gliding the loop gain and the loop lowpass coefficient once per call instead of per
  sample (-14). 215.0 -> 224.7 measured, static 215 -> 226. WAVE (140) and CPLX (122) are unchanged, but
  the table carries one number per effect, the dearest mode.

### VOWL: formants split left / right (`vowel/gen.py`)

- WIDE (main page slot 4, which was blank; default 80): every formant sits a fraction d lower on the
  left and d higher on the right, d = WIDE x min(5.9 % (a semitone), 2 kh), kh = 1/(2Q). The second
  term keeps the two sides' peaks within two bandwidths of each other however narrow RES makes them:
  a first version without it (a fixed +-1 st) lost 3.6 dB in mono on a tone between the two peaks at
  RES 100, WIDE 127 (correlation -0.79); with the cap the worst case found is -0.7 / -1.1 dB
  (RES 127, WIDE 127, sine on the formant).
- How it is afforded: the coefficient work is this effect's cost outside the sample loop, so the two
  sides TAKE TURNS: each call tunes one side's five formants. Each side is retuned every second call
  (0.73 ms instead of 0.36 ms); the knob glides, states and the level matcher (one gain for both
  sides, so the image does not wander with level) are as before. The first call tunes the left and
  copies it to the right.
- It is a moderate widening by nature: two resonances at slightly different frequencies differ in phase
  only near the formants (noise: corr +0.66 at the default, +0.53 at WIDE 127, +0.14 at WIDE 127 with
  RES 100). A steady sine BETWEEN formants passes both sides almost alike (440 Hz: corr +1.000), so the
  gate's sine sits on a formant (650 Hz, F1 of "a").
- A pitched source: its harmonics fall nearer one side's peaks than the other's, so the sides differ
  in level with the note (110 Hz saw, default WIDE: within 1.6 dB on a e i o, 4.3 dB on "u"). Turn WIDE
  down if a bass line leans.
- WIDE 0: both blocks hold the same numbers: L = R exactly once the knobs are at rest (while a knob
  glides the two sides are one call apart). All the plugin-comparison checks of the gate run at WIDE 0
  and pass unchanged.
- Cost: 648.0 -> 656.9 (+1.4 %). The gate's standing "mean < 320" FAIL line for this effect is the old
  one (the standard gate's limit, not a regression).

## Left alone, and why

- **COMP**: "make mono what should be mono" is already true here. One detector on max(|L|, |R|), one
  envelope, one gain for both sides: the image cannot wander. Equal inputs: L = R bit for bit.
- **DIST**: the two sides are clipped / degraded independently but identically (same drive, tone, the
  same sample-and-hold clock in DGRD), so a mono track stays exactly mono and a stereo input keeps its
  image. Linking the clippers or summing the low end would cost instructions to change nothing a mono
  track can hear; for stereo inputs independent clipping is the normal choice. No change.
- **ECHO**: two hard-panned lines fed the mono sum, different tap ratios a side, WDTH (default 127)
  already there. This IS the ping-pong / L-R time offset. No change.
- **VERB**: lines alternate left/right, two output sign patterns; corr on mono noise -0.21 (the gate's own row), -0.17 on the burst here. Not
  touched (and it is the one effect with no cost headroom).
- **SMEAR**: opposite-phase modulation of the tank's two halves and different taps a side were already
  there; corr -0.005 on the burst.
- **ROSE**: even panes left, odd panes right, SPRD (FUNC layer, default 90) crossfades from mono;
  SPRD 127 is fully split. No change.

## Observations (not changed, worth knowing)

- **Steady pure tones through SMEAR, VERB, ECHO and ROSE SPRD 127** are not "wide but mono-safe" in the
  sense of the gate: a tank or a delay gives each frequency its own fixed phase (or level) between the
  sides, so one sine can come out near anti-phase (SMEAR, 440 Hz: corr -0.98, sum 6.3 dB under the left)
  or mostly on one side (ECHO 440 Hz: 7 dB between the sides; VERB 440 Hz: 6 dB, corr -0.35; ROSE: the root's pane is a left pane). On
  anything with a spectrum the same effects sum to +3 dB. This is how every decorrelating reverb or
  multi-tap behaves; it is reported, not fixed.
- CPLX driven by a loud sine exactly at its note locks both sides to the drive (corr +0.98 at the
  default DTUN); its width is in the ringing after a hit (corr +0.55).
- The READMEs of the changed effects (`<fx>/README.md`, generated by `tools/readme.py` from hand-kept
  notes) were not regenerated and do not mention WIDE / SPRD.

## Files

- Changed sources: `pitch/pitch.asm`, `grains/gen.py` (-> `grains.asm`, `grains_test.asm`),
  `ringph/gen.py` (-> `ringph.asm`), `reso/gen.py` (-> `reso.asm`), `vowel/gen.py` (-> `vowel.asm`), the
  manifests of pitch, grains, ringph, vowel; every one has a `.prewide` copy beside it.
- Gates: every `<fx>/test.py` got the width check (`tools/aeria.py: stereo_report`, added at the end of
  that file; `tools/aeria.py.prewide` is the file before). For the effects left alone the rows are
  "info" (figures only); COMP and DIST are gated as mono.
- `aeria-os/fx/gen_fx.py`: the cost of pitch (280), reso (226), vowel (657) in EFFECTS; display laws for
  pitch WIDE (cents), grains SPRD (%), vowel WIDE (%), and ringph SPRD now 0..90DEG.
