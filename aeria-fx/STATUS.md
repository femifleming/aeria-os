# AERIA FX status

Private DSP56300 effects for AERIA (Octatrack MKII, Modwerk base, OS 1.40C). Nothing here
has been flashed or run on hardware; every result is from `dsp_host` (the vendored dsp56300
emulator, built from the `modwerk-source-tools` image) running each effect over the stock MAIN OS
payloads. Last full run: all six gates green (`tools/run_all.sh`).

Stereo width pass, 12 Nov 2026: see `STEREO.md` (PITCH WIDE, GRAINS SPRD, RING SPRD default, RESO DTND stereo, VOWL
WIDE; costs there supersede the table below: PITCH 280 static / 260 measured, RESO 226 / 225).

(This file covers the six effect ports below. `head/` and `tap/` in this folder are someone
else's work and are not described here.)

| effect | folder | assembles | renders | static cycles/sample (cycle_count.py) | dsp_host instr/sample mean / worst block | gate | known issues |
|---|---|---|---|---|---|---|---|
| PITCH | `pitch/` | yes | yes | 204 | 195 / 195 | all pass (23) | linear interp (not Hermite); GRN glides ~1.5 s and bends pitch slightly while moving |
| RESO (WAVE/CPLX/DTND) | `reso/` | yes | yes | 215 (modes 140/122/215) | 215 / 215 | all pass | chromatic, no scale snap; DTND allpasses flatten pitch (by design); CPLX state saturates on a loud tone at its pitch (bounded); PTCH floor F#1 |
| DIST (DIST/CLIP/DGRD) | `disto/` | yes | yes | 148 (148/112/100) | 151 / 152 | all pass | DIST is u/(1+u) (atan-like); CLIP uses 2 one-poles not an SVF; no oversampling, aliasing not measured; page-1 slot 3 blank |
| RING (RING/PHSR) | `ringph/` | yes | yes | 208 (155/208) | 223 / 225 | all pass | phaser coefficient -exp(-2 pi f/fs) shifts notches slightly above ~2 kHz; ring output 1x not StoreFX's 1.4x |
| SMEAR (smear + freeze) | `smear/` | yes | yes | 298 | 269 / 269 | all pass | NOT spectral: Dattorro-style diffusion tank; HOLD = static lossless tank (flat within 0.2 dB over 6 s), a wash with tank colouration, not a per-bin freeze |
| GRAINS (live) | `grains/` | yes | yes | 515 static = every grain restarting at once (impossible); real worst block 253 (235 base + 70 per restart, max 4 per block) | 235 / 238 | all pass | 360 ms memory (not 4 s); triangle window only; overlapping grains can partially cancel on steady pure tones |
| ROSE (8-mode resonator, rose window / Chladni / primes / JI) | `rose/` | yes | yes | 178 (sample loop) | 195.5 / 199.2 (worst over SHAP x splits 0/1/7) | all pass (55) | no buffer; modes above 4 kHz fold by octaves; BRIT is decay tilt only; ROTA phase not published to the UI; P table 960 words; packaged as aeria-rose.mwrm |

Worst-case sum of the six (static, GRAINS at its real worst block): ~1,326 cycles/sample of the
~3,100 shared on the tracks 5-8 core.

## What every gate checks (`tools/aeria.py: standard()`), per effect

- manifest loads through octabam's schema (names <= 4 chars, defaults inside counts, ModeView)
- no label is a prefix of another (dsp_asm resolves labels by prefix), no `rep`
- assembles through `build_bus.assemble`: disassembler round-trip + mpysu audit (no unaudited
  mpy/mac operand order), placed at P:0x2000 under FX id 0x17 in the stock payload dumps
- static cycles via `tools/build/cycle_count.py` (worst mode loop)
- dsp_host at the dearest settings on 0.5 FS noise: meter minus a null effect, bounded, `-guard`
  (window 0x3dfe) clean: no write outside the instance's buffer
- MIX 0 is bit-exact dry on a full-range ramp with every other knob at its dearest
- dirty state: instance block X:0x6200.. filled with 4 garbage words and Y with -dirty garbage,
  silence in -> last 300 samples exactly 0
- random automation of every active knob every 3 blocks, split calls 0 and 7, guard: bounded
- plus effect-specific numeric checks (pitch accuracy, ring frequency/decay, sidebands, notches,
  freeze hold/thaw, stereo independence) -- see each `<fx>/test_results.md` and README

## Not done / not tested

- Hardware: nothing flashed. `make check`, `verify_knob_clicks.py`, `npm run fx:audit`
  (aliasing/idle/mono), parameter locks, LFO modulation on the unit, both-core placement in a real
  remix, USB package build: all not run.
- FX2 ids in the manifests (0x1b 0x1e 0x1f 0x06 0x07 0x09) are PROVISIONAL placeholders.
- Instruction forms: every ALU/move form used has a precedent in shipped octabam modules (checked
  for the a0 moves and Tcc sources); (Rn+Nn) Y reads/writes and `lua` as in Mini Verb. Not
  hardware-proven for these sources.

## Tooling

- `tools/run.sh <cmd>`: Docker `modwerk-source-tools` with this folder at /aeria, the private SDK
  copy `.sdk` (from `modwerk/sdk/octabam`, vendor -> /opt/toolchain/vendor) at /sdk.
- `tools/run_all.sh`: every gate; `tools/readme.py`: regenerates the READMEs from manifest + results.
- `tools/aeria.py`: assemble/overlay/render harness; `tools/asmgen.py`: macro helpers used by the
  `gen.py` of reso/disto/ringph/smear/grains (their .asm files are generated -- edit gen.py).
- `tools/asm.sh <fx>/<file>.asm`: raw dsp_asm diagnostics.
- Found while porting (worth knowing): `asr #n` leaves the shifted-out fraction in a0, and a
  following `asl`/`neg` pulls it back into a1 (a read one sample/channel off) -- clear with
  `move a1,a`; dsp_asm encodes no backward `bsr` (subroutines go after their callers).
