# AERIA TAP

FX1 insert for the AERIA source tracks (T1 SAMPLE, T2 STARS). It writes exact digital silence over the
track's block, so the source is heard only through the chain: AERIA HEAD on T3 reads T1 and T2 from the
key bus (its SMPL and STAR knobs).

- Source: `tap.asm`, 9 DSP words: `clr a`, then `do n7` with two `move a,x:(r0)+`. It has no state and no
  knobs. init writes nothing, so r1 is left alone.
- Package: `~/octa/build/fx-packages/tap.mwrm`, 446 bytes, ABI 6, sha256 `4c12e721…64503de`. FX1 only.
  Preferred id 0x1d, a free non-stock id (stock ids are `remix/schema.py` STOCK_FX2_IDS; the ten built
  packages use 6, 9, 10, 11, 14, 15, 23, 26, 27 and 30; 0x1d is Euclid's, which is not built). The base
  moves it to the lowest free handle if 0x1d is taken (`core-beta/source/dsp.c:174-178, 280-288`). Menu row:
  "TAP" / "AERIA TAP". The display name is "AERIA TAP". Declared cost: 3 executed cycles per sample, state 1.
  Built by `../head/package.py`.

## Why the bus still carries T1/T2 when TAP silences them

The base's key bus tap is KEYBUS site 0, `dsp_fx1_dispatch`. It is installed at P:$004a7 on payload A and
P:$0029c on payload B. `sdk/machines/octatrack/dsp-sites.json:12-15` says it runs "the dispatcher, for every
track on every block, **before FX1**". The tap code is `sctap` (`~/octa/build/core-beta/source/dsp.1333.asm`).
It copies the block at r0 to `Y:$800 + track*$40` and to the cross-core window, before the dispatcher's FX1
init/proc call at P:$4c8 (`sdk/octabam/AGENTS.md`, the r1 trap). `docs/OCTATRACK_PACKAGE_DSP_HOOKS.md:233`
lists the site as "FX1 dispatcher (per track, before FX1)", and lines 329 and 357 say "it copies every track's
FX1 input". So the bus holds T1/T2's audio from before TAP, and TAP only changes what the track sends to its
FX2 and the mix.

Notes:
- The bus is live only while a Part of the bank names a reader effect (AERIA HEAD). Without HEAD, T1 and T2
  are silent everywhere.
- Leave FX2 on T1/T2 as NONE. It would only process silence.
- The bus copy is taken at the FX1 input, so the T1/T2 level reaching HEAD comes from the AMP page and
  HEAD's SMPL/STAR, not the track LEVEL.

## Measured (dsp_host, `test_tap.py`, results in `test_results.md`)

| check | result |
|---|---|
| assembles; the disassembly matches; no mpy/mac at all | pass |
| on T1, T2 and T5, with 0.9 FS noise L and a full-scale sine R, trig splits none/1/7/15, a dirty Y and `-guard` | every output sample is 0; no stray writes |
| cost: core meter less an rts-only effect | 2.19 instructions/sample (2.44 with a trig split every block); charged on the unit at 7/4 of the declared 3, so 5 |

Run: `../head/run.sh python3 -B /t/test_tap.py`

Not verified: nothing has run on the unit. The page has no knobs (enable mask 0). That is a first for a
module page, though stock NONE has none either. If the chooser or page misbehaves, add one inert knob in
`package.py`.
