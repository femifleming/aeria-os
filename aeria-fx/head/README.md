# AERIA HEAD

## Track map (rig5, retargeted 9 Nov 2026)

The chain moved to core 0: HEAD on T5, NEIGHBOR chain T6-T8, sources on T1/T2.

| track | role | core | how HEAD reads it |
|---|---|---|---|
| T1 | SAMPLE source (FLEX loop), TAP on FX1 | 1 | SMPL key |
| T2 | STARS source (STATIC), TAP on FX1 | 1 | STAR key |
| T3, T4 | unused | 1 | — |
| T5 | THRU (inputs A B C D); HEAD on FX1 | 0 | its own input |
| T6, T7, T8 | NEIGHBOR chain | 0 | T8 is the FEEDBACK key: its FX1 input is T7's output |

- **SMPL and STAR** come from the other core's window (T1-4 core, `Y:$3be00`), two generations behind core 0's
  counter. On the unit that is 32 samples late.
- **FEEDBACK** comes from core 0's own bus (`Y:$9c0`). T8 is dispatched after T5, so HEAD gets the previous block
  (16 samples), plus the NEIGHBOR hops.
- **Changing the map:** the three key tracks are literal `move #>n,a` lines in `head.asm`, marked
  `KEY SMPL`, `KEY STAR` and `KEY FEEDBACK`, where n = track − 1 (now 0, 1, 7). `dsp_asm` has no `equ`. The
  addressing follows X:$420, so any key track works from any HEAD track (tested both directions: HEAD on T5/T8
  reading T1/T2 cross-core, HEAD on T1 reading its own bus).
- **Core 0 budget:** HEAD now sits on the core with the smaller allowance (3120). The tap's 51 cycles must still
  fit on core 0 beside HEAD and the T6-T8 FX2s (`../../build/research-keybus.md` §1.3), or the bus waits silently.

### Current build

- **Package:** `head.mwrm`, 2,182 bytes, ABI 9, requires keybus, sha256
  `0f2f8b0c4e1bd837ebf4d90b42ad4d6d2c687be4046558400c4d601dfbe5973b`. It differs from the rig4 build only in the
  three key immediates (3 bytes).
- **Previous (rig4) build** kept as `~/octa/build/fx-packages/head-rig4.mwrm`, sha256
  `fbc4dab818228ca09388ff94656fb48196e871f479bb2855bbb67bb6a655d8a6` (source `.build/head.rig4.asm`, results
  `.build/test_results.rig4.md`). It is HEAD on T1, SMPL T5, STAR T6, FEEDBACK T4.
- **Code:** 384 DSP words.
- **Declared cost:** 170 executed instructions per sample. The worst measured case is 169.9, with a trig
  split every block. Unsplit it is 150.9; with the bus not live it is 17.4.
- **Rig:** HEAD runs as T5 (X:$420 = 4). `ksfor` fills every generation of the T1-4 core's window with T1/T2.
  The chain stub (`LPLOC`) writes HEAD's output (× G) into T8's slot of the local bus after HEAD runs, so HEAD
  hears it one block later, as on the unit.
- **Tests:** all pass (`test_results.md`):
  - Exact passthrough at the defaults.
  - Seven "bus not live" cases (no marks, a wrong mark or a counter of 4 on either core, only one core seeded)
    fall back to passthrough.
  - Sends match the model within 1.2e-7 of full scale.
  - Addressing: HEAD on T5 and on T8 reads T1/T2 from the T1-4 window generation (core-0 counter + 2) & 3 for
    counters 0-3, ignoring the other core's counter and decoys in the T5-8 window and local bus. HEAD on T1 reads
    them from its own bus.
  - FEEDBACK key: a tone placed only in T8's slot is heard. Noise in every other bus slot and in both windows
    gives exactly 0.
  - Feedback stays bounded and decays to exact silence with a unity chain.
  - With a ×3.97 chain it stays bounded and keeps sounding at about −13 dBFS.
  - Knob smoothing, random automation and the guard are clean.
- **Stock-free scan:** clean.

### Superseded

The rig4 build (HEAD on T1, SMPL T5, STAR T6, FEEDBACK T4) is superseded by rig5 above. Everything below this
section describes the first build, where HEAD sat on T3, sent from T1/T2 and fed back from T8. Its sha256 `115854b0…`, its 169-cycle figure, and every mention of T1/T2/T3/T8 below are
superseded by the build above. That source is kept as `.build/head.v1.asm`.


FX1 effect for T3 (THRU, inputs A B C D). It is the input mixer and main feedback at the head of the AERIA
chain. T3 FX2 and T4..T8 (NEIGHBOR) FX1/FX2 follow it.

```
out = in·(1 + d(IN)) + SMPL·T1 + STAR·T2 + FDBK'·clip(drive·g·F(T8)) / drive
```

- **T1, T2** are read from the key bus: the tracks' audio as it enters FX1, taken before AERIA TAP silences
  them (see `../tap/README.md`). On T3 they are on the same core and dispatched first, so they arrive in the
  same sample, with no latency (tested).
- **T8** is T8's FX1 input, which is T7's output. That is the end of the chain the bus can see. On T3 it is
  read from the other core's window, two generations behind core 1's counter: 32 samples plus the NEIGHBOR
  hops (about 200 samples in total by research-fx.md's estimate, never measured). T8's own FX1/FX2 are not
  in the loop.
- **F** follows Cloudfin's global loop (`~/Cloudfin/Source/dsp/Engine.h` 151-163):
  - DC blocker at 15 Hz.
  - High-pass at 70 Hz, built as two one-pole sections. This is the one deviation: Cloudfin uses a 2nd-order
    Butterworth with Q 0.707; this has Q 0.5 and costs fewer cycles.
  - One-pole low-pass on FTON.
  - Level-riding gain `g = min(1, 0.3/env)`. env is a peak follower with 4 ms attack and 350 ms release.
    The peak is taken every sample; env and g are updated each block and ramped per sample.
  - Soft clip `u − u³/3`, with u limited to ±1, so the clip output never exceeds 2/3. It stands in for tanh.
- **WARM** sets the drive `d = 1 + 3w`, applied before the clip and divided out after it. Small signals
  keep unity gain at every WARM setting: WARM adds saturation, not loop gain.
- **FDBK'** runs 0 .. 0.95.
- **Gate:** when env is below 2^-15 (about -90 dBFS), FDBK' goes to 0. An idle loop therefore ends in exact
  digital silence instead of a fixed-point limit cycle.

## Knobs (page 1)

| knob | range | default |
|---|---|---|
| FDBK | main feedback 0 .. 0.95 | 0 |
| FTON | feedback LP `c = q²`, `q = 0.9v/128 + 0.1`: about 70 Hz at 0, about 2.5 kHz at 64, about 14 kHz at 118, open at 127 | 118 |
| SMPL | T1 send 0 .. 0.99994 | 0 |
| STAR | T2 send 0 .. 0.99994 | 0 |
| IN | own input, (v−64)/64 added: 0 = silence, 64 = unity (exact), 127 = ×1.98 | 64 |
| WARM | clip drive 1 .. 4 (loop gain unchanged) | 0 |

Every knob is ramped per sample. Each call computes a step that reaches the target in 16 samples, and the
value lands exactly on the target (ring-sc's ramp). The defaults give exact passthrough.

## Key bus liveness

HEAD reads the bus only when both cores' seed marks are valid: `Y:$3bdfe` / `Y:$33dfe` equal `$10000`, and
the counters are 0..3. Otherwise it runs an IN-only loop (17 instructions/sample): sends and feedback are
silent, and their ramps and filter states are cleared. When the bus comes up, the sends and feedback ramp in
from 0. If the bus drops while sound is running, the sends and feedback cut immediately. The bus only goes
down when no Part names a reader, i.e. when HEAD itself is gone. HEAD writes nothing in the bus or the window
(guard-checked). Addressing uses `X:$420`, so HEAD also works on other tracks; on T5-T8 the T1/T2 reads cross
over to the other core's window (tested on T7 for every counter value).

## Package

- `~/octa/build/fx-packages/head.mwrm`: 2,178 bytes, ABI 9, `requires keybus`, sha256 `115854b0…eb9ff3667`.
- Read claims: ring-sc's four, `y:0:r:0x800:0x200`, `y:1:r:0x800:0x200`, `y:0:r:0x33dfe:0x202`,
  `y:1:r:0x3bdfe:0x202`.
- FX1 only. 383 DSP words, 5 relocations, state 34 words.
- Declared cost: 169 executed cycles per sample. The base charges 7/4 of that, about 296 on core 1, plus the
  key bus tap at 51 on both cores.
- Preferred id 0x07, the only non-stock id below 32 that no module claims. The base reassigns it if 0x07 is
  taken.
- Menu row "HEAD" / "AERIA HEAD"; display name "AERIA HEAD".
- Stock-free scan (`scan_pkgs.py`, the method of `fx-packages/tools/scan.py`): longest shared run 0, 0
  check_stock_free hits. tap.mwrm is clean too.

Build: `./run.sh python3 -B /h/package.py`. It uses the repo's `dsp_packages.package` (two-origin relocation
and placement proofs), `build.py`'s `main`/`dsp_section`/header, and `abi9.py`, all read-only from `/work`.
The page recipe is generated here, because `descriptor-recipes.json` has no AERIA rows. It uses DARK REV as
the donor, as miniverb, ring-sc and character do, and writes every page-1 slot the way ring-sc writes its
plain knobs. The packing is checked byte for byte against `build.py page_section` on airwindows-chorus.

## Measured (dsp_host, `test_head.py`; full table in `test_results.md`, 0 failures)

The rig is a synthetic memory image like ring-sc's `verify.py`, with no firmware. Instances, in order:

1. ring-sc's `keystub` publishing T1 to the local bus.
2. `keystub` publishing T2.
3. HEAD on T3.
4. A chain stub on HEAD's own audio buffer. It runs after HEAD, leaves HEAD's output untouched, and publishes
   it × G as T8 in every generation of the core-0 window.

That makes a real closed loop with 16 samples of latency, but the chain is reduced to a gain.

| check | result |
|---|---|
| defaults (FDBK 0, SMPL 0, STAR 0, IN 64), live bus with loud T1/T2, ×4 chain, FTON/WARM varied, splits, dirty Y | bit-exact passthrough |
| bus not live (no mark, wrong mark, counter 4, only one core seeded), FDBK/SMPL/STAR 127 | bit-exact passthrough |
| IN 0 / 32 / 127 | silence / 0.50000 / 1.98438 |
| SMPL 127, STAR 127, and a three-way mix | output equals the model within 1.2e-7 FS, same sample |
| leveller g and 1/d, read from DSP state | error 2.4e-7 and 9.3e-8 |
| FDBK 127, unity chain, 1 s of noise then silence, FTON 127/64 × WARM 0/127 | bounded; the tail decays and is exactly 0 from about 3.2 s into the silence, when the -90 dB gate closes |
| FDBK 127, ×3.97 chain (loop gain 3.8), then silence | bounded by the leveller and clip (peak 0.28, ≤ 0.95·2/3); it sustains at about -13 dBFS rms |
| FDBK 127, ×3.97 chain, silence from the start | every sample exactly 0 |
| full-scale input, IN 127, FDBK 127, ×3.97 chain | bounded (the store limits at full scale) |
| knob jumps 0↔127 on DC | the largest step is the 16-sample ramp, 0.0623 FS, against about 1.0 unsmoothed |
| random automation every 3 blocks with splits and `-guard` | bounded; no write outside HEAD |
| cost, core meter less an rts-only effect | 150.0 per sample (live, dearest); 148.2 at defaults; **168.1 with a trig split every block** (the per-call setup runs twice); 17.4 when the bus is not live |

The assembler traps were all checked: every multiply was disassembled and is a signed form (no
mpysu/macsu), there is no `rep`, labels are `hd` + two digits, displacements are within -64..63, and init
leaves r1 alone.

Run: `./run.sh python3 -B /h/test_head.py`, then `./run.sh python3 -B /h/scan_pkgs.py`.

## Not verified

- Nothing has run on the unit.
- The rig shows neither the base's tap and counter running nor the two cores running at once. The loop
  latency here is 16 samples; on the unit it is about 200, never measured.
- If the real chain (T3 FX2 .. T7) has net gain above about 1/0.95, the loop does not die out at FDBK max.
  It sings at the leveller's level, about -13 dBFS, as Cloudfin's does. With a unity or lossy chain it
  decays to exact silence.
- The page recipe is hand-built (above). The knob layout on the unit is unchecked.
- The cost figures are dsp_host instruction counts, not chip cycles (research-fx.md: hardware measured
  1.42-1.74× claims).
