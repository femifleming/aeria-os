"""Writes <fx>/README.md from the manifest, the notes below and <fx>/test_results.md.
Run inside the image: tools/run.sh python3 /aeria/tools/readme.py"""
import re
import sys
from pathlib import Path
sys.path.insert(0, '/aeria/tools')
import aeria as A  # noqa: E402  (sets up the SDK path)
from remix import registry  # noqa: E402

NOTES = {
'pitch': dict(
    title='AERIA PITCH',
    what='Two-head delay-line pitch shifter: a port of `fp::PitchShifter` (Cloudfin / Pinkie Pie\'s '
         'Balloon Shop `PitchShifter.h`).',
    design='''- Two read heads half a window apart sweep the window at (1 - ratio) samples per sample.
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
  mix (asr 8, mix lands exactly on its target outside the loop). 128 dry calls clear the ring.''',
    issues='''- Linear interpolation, not Hermite (Hermite would add ~40 cycles; aliasing not measured).
- GRN glides (~1.5 s time constant) instead of Cloudfin's two-voice crossfade, so moving GRN bends
  the pitch slightly while it moves (up to ~1 semitone during a full-range jump).
- A pure sine shows the usual delay-line-shifter sidebands at the crossfade rate; the spectral
  peak sits within one crossfade-rate sideband of the target (tested), the head speed is exact.''',
),
'reso': dict(
    title='AERIA RESO',
    what='StoreFX waveguide (Karplus), complex resonator and detuned (allpass) resonator as one effect, '
         'MODE on page-2 slot 6 (WAVE / CPLX / DTND).',
    design='''- WAVE: stereo Karplus-Strong on a [L,R] pair ring; linear-interpolated read at P - h with a
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
- Buffer: four 2,048-word segments (m = $7ff) = 8,192 words. MODE change re-arms a 68-call clear.''',
    issues='''- StoreFX snaps pitch to the scale; here PTCH is chromatic (no scale).
- DTND's allpasses lower the pitch by design (as in StoreFX): -46 cents at PTCH 48 with DTUN 0.
- CPLX fed a loud tone at its own pitch saturates its state (bounded, then soft clipped).
- PTCH floor is F#1 (the pair ring holds 1,024 frames); FINE below F#1 is clamped.''',
),
'disto': dict(
    title='AERIA DIST',
    what='StoreFX disto, clipper and degrade as one effect, MODE on page-2 slot 6 (DIST / CLIP / DGRD). '
         'No buffer.',
    design='''- DIST: s = sign(x) G|x| / (G|x| + 1), G = 1/d, d = 0.4 - 0.3999 DRIV. Same value as StoreFX's
  atan2(x, d) * 2/pi at G|x| = 1 and the same limits; computed as U / (U + 2^-14) with U kept in 48
  bits and one unrolled 24-step fractional divide per channel. Then the "slope" one-pole lowpass.
- CLIP: clamp(x, +-t) * 0.33/t, t 0.25 .. 0.001 (log), then two one-pole lowpasses 100 Hz .. 15 kHz
  (StoreFX uses a Butterworth SVF; two one-poles are cheaper and need no tan).
- DGRD: bits 24..2 (rounded quantiser: add half an LSB, mask), sample-and-hold at 1 .. 1/100 of
  44.1 kHz (log), TONE post lowpass.
- LEVL 0..2x. All continuous controls smoothed per sample; MODE change resets state and snaps.''',
    issues='''- u/(1+u) is atan-like, not atan (slightly softer knee; same endpoints).
- No oversampling or ADAA: DIST/CLIP alias at high drive like the float original; DGRD aliases on
  purpose. `fx:audit` aliasing not run.
- Page-1 slot 3 is unused (blank).''',
),
'ringph': dict(
    title='AERIA RING',
    what='StoreFX ring modulator (carrier follows the input envelope) and StoreFX phaser (six first-order '
         'allpasses, feedback, added to the dry) as one effect, MODE on page-2 slot 6 (RING / PHSR).',
    design='''- Sine: a wrapping 24-bit phase, s = 4p(1 - |p|), y = 0.775 s + 0.225 s|s| (< 0.1 % error).
- RING: carrier 20 Hz .. 5 kHz (log); FOLW multiplies the step by 1 + 6 FOLW min(1, 3 env)
  (env 3 ms / 120 ms, StoreFX's numbers); BIAS pulls the carrier toward 1 (AM); SPRD offsets the
  right carrier 0..180 degrees. Output = x * carrier (StoreFX has 1.4x; unity here to keep headroom).
- PHSR: LFO 0.05 .. 10 Hz; notch sweep 200 Hz .. 5 kHz on a log axis (u = CNTR + DPTH lfo / 2);
  coefficient a = -exp(-2 pi f / fs) (tan-free approximation of (tan - 1)/(tan + 1), exact to second
  order), computed per block for the block's end and glided per sample; FDBK 0..0.85 through
  x - x^3/3 and a 7 Hz DC blocker (StoreFX uses tanh and no DC block); out = 0.7 (x + v).''',
    issues='''- The phaser coefficient approximation moves the notches slightly above ~2 kHz relative to the tan
  form (e.g. -0.49 vs -0.46 at 5 kHz).
- RING's FOLW can push the carrier up to 35 kHz (as StoreFX): it aliases, by design of the source.''',
),
'smear': dict(
    title='AERIA SMEAR',
    what='A time-domain stand-in for Balloon Shop\'s spectral smear (SpectralGate) and Snow freeze: two '
         'input allpass diffusers into a Dattorro-style figure-8 tank with a freeze.',
    design='''- Two input allpasses (0.75 / 0.625 x DIFF), then a figure-8 tank: per half a modulated allpass
  (0.7 x DIFF, +-12 samples at 0.7 Hz), a delay, the damping lowpass (TONE), decay, a plain allpass
  (0.5 x DIFF), a delay, decay. Three taps per side, cross-taken, for width (L/R correlation ~0).
- SMER sets RT60 0.3 .. 20 s (log) from the loop length; decay applied at four points per loop.
- FRZ HOLD: input to the tank fades out, decay -> unity (1 - 2^-23), damping -> none, modulation -> 0,
  over ~50 ms; the tank is then a static lossless allpass/delay loop and rings unchanged (tested:
  level flat within 0.2 dB over 6 s) until THAW, when it decays at SMER again.
- One 15,870-word ring (m = $3dfd), every line a segment of it (14,509 words used).''',
    issues='''- NOT spectral. Snow holds per-bin magnitudes with a phase vocoder (16k FFT) and SpectralGate
  smears per-bin magnitudes in time; neither fits one DSP56300 budget next to seven other effects.
  This smears in time (dense diffuse echoes) and freezes a ringing tank: a frozen chord rings as a
  steady diffuse wash with the tank's own mode colouration, not as a static spectrum; transients
  are smeared, but there is no per-bin gate/threshold and no tilt.
- While held, the tank is static (modulation parked), so the hold can sound faintly metallic on
  narrow-band material; THAW restores modulation.''',
),
'grains': dict(
    title='AERIA GRAINS',
    what='A live granular processor (Balloon Shop GrainShifter scaled down): four grains over the last '
         '360 ms of input, recorded into the FX2 ring.',
    design='''- Mono record ((L+R)/2) into a 15,870-word ring (m = $3dfd); FRZ HOLD stops the head.
- Four grains a quarter period apart; each is a linear-interpolated read head with a triangle window
  (two staggered triangles sum to exactly 1: DENS 127 at unison reproduces the input level within
  0.01 dB). At a restart (window = 0) a grain latches direction (REV probability), length, start
  (POS + SPRY random, clamped so it can never cross the record head or the ring end at its speed),
  pan and gain, so knob moves never click a grain.
- Speed 2^(PTCH/12) by the PITCH chain; length 10..300 ms, shortened per block when |speed - 1|
  (or speed + 1 reversed) x length would exceed the ring.
- 24-bit LCG (x * 1664525 + $6ef35f mod 2^24) for the random draws, only at restarts.''',
    issues='''- 360 ms of memory (the source has 4 s tapes with two-tape freeze and anti-aliased octave bands);
  POS reaches 350 ms back. No random walkers for size/position, one envelope shape (triangle).
- Steady pure tones at shifted pitch can partially cancel between overlapping grains (independent
  grain phases); with SPRY > 0 the phases are random. Tested per grain (DENS 0).
- A grain already playing when FRZ flips keeps its latched rate to its end (a short glide).
- Static count 515 prices every grain restarting in the same sample, which cannot happen (a grain
  restarts at most once per 441 samples): base loop 235 cycles/sample + 70 per restart, so the true
  worst 16-frame block is 235 + 4 x 70 / 16 = 253 cycles/sample. dsp_host measured mean 235, worst block 238.''',
),
}


def knob_table(mod):
    rows = ['| slot | name | count | default | what |', '|---|---|---|---|---|']
    for i, p in enumerate(mod.params):
        if p.active and p.name:
            rows.append(f"| {i} {'(p2)' if i >= 6 else ''} | {p.name.decode()} | {p.count or 128} | "
                        f"{p.default} | {p.doc} |")
    if getattr(mod, 'mode_views', None):
        rows.append('')
        rows.append('Per-mode names/defaults (ModeView): ' + '; '.join(
            f"mode {v.mode}: names {dict((k, n.decode()) for k, n in (v.names or {}).items())} defaults {v.defaults}"
            for v in mod.mode_views))
    return '\n'.join(rows)


for fx, n in NOTES.items():
    folder = Path('/aeria') / fx
    mod = registry._load_one(folder / 'manifest.py')
    res = (folder / 'test_results.md').read_text() if (folder / 'test_results.md').exists() else '(not run)'
    cyc = re.search(r'static (\d+) cycles/sample.*?dsp_host mean ([0-9.]+), worst block ([0-9.]+)', res)
    fails = res.count('| FAIL |')
    cost = (f'- Static (tools/build/cycle_count.py, per-sample loop, worst mode): **{cyc[1]} cycles/sample**\n'
            f'- dsp_host (meter minus a null effect, dearest settings, 0.5 FS noise): mean **{cyc[2]}**, '
            f'worst block {cyc[3]} instructions/sample (instructions, no memory-contention stalls)\n'
            f'- Budget target: <= 300 per effect (8 effects share ~3,100 on one core)') if cyc else '(see results)'
    text = f"""# {n['title']}

{n['what']}

Private AERIA effect for the Octatrack MKII (Modwerk base, OS 1.40C). **Not flashed; nothing here
has run on hardware.** Every result below is from `dsp_host` (the vendored dsp56300 emulator) running
this source over the stock MAIN OS payloads (`tools/aeria.py`).

FX2 id {hex(mod.menu.fx2_id)} in `manifest.py` is PROVISIONAL: choose a free id at integration.

## Knobs

{knob_table(mod)}

## Design

{n['design']}

## Cost

{cost}

## Test (`../tools/run.sh python3 /aeria/{fx}/test.py`)

Result: {'all checks pass' if fails == 0 else f'{fails} FAILING check(s)'}.

{res}
Not tested: hardware, `npm run fx:audit` (aliasing/idle/mono), `verify_knob_clicks.py`, parameter
locks, LFO modulation on the unit, both-core placement in a real remix, `make check`.

## Known issues / differences from the plugin

{n['issues']}

## Files

- `{Path(mod.dsp.asm).name}` the DSP source{' (generated by `gen.py`; edit gen.py)' if (folder / 'gen.py').exists() else ''}
- `manifest.py` octabam module manifest (schema-checked by the test)
- `test.py` the gate; `test_results.md` its last result
"""
    (folder / 'README.md').write_text(text)
    print('wrote', folder / 'README.md', 'fails', fails)
