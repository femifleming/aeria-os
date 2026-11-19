# AERIA OS: project specification

The single source of truth for what AERIA OS does, how it is built, how it is tested and what is known to be wrong.
Written 19 Nov 2026 under the owner's repair brief (18 Nov 2026: *no new features; make what exists work; the
original Octatrack is the behavioural reference; test in a simulator; deliver a build for the AERIA Flasher*).

Companion documents (all in `~/octa/docs/`):

| File | What it holds |
|---|---|
| `history/REQUESTS_CONSOLIDATED.md` | Every request, bug report and decision from 9–18 Nov, by date and by subsystem, with status (716 lines). **The feature inventory lives there**; this file does not repeat it. |
| `history/user_messages_raw_2026-11-09_to_16.txt` | The owner's own words, extracted from every session log (with the 17–18 Nov messages appended). |
| `REFERENCE_FINDINGS.md` | What the stock 1.40C firmware, Octabam, Modwerk, Octamax and Octamad establish about outputs, headphones, the codec, the knob and the lamps (with file:line / address evidence). |
| `../aeria-os/HARDWARE.md`, `../aeria-os/AUDIO.md` | The 9 Nov hardware and audio maps written from Octabam's documentation (still the register-level reference). |

Status words used below: **VERIFIED (sim)** = passes an automated test on the real firmware in the simulator;
**VERIFIED (unit, date)** = measured on the owner's unit by an earlier session; **HARDWARE ONLY** = cannot be shown
without the instrument; **KNOWN LIMIT** = understood, accepted, documented.

---

## 1. What AERIA OS is

A complete replacement operating system for the Elektron Octatrack **MKII**: its own ColdFire image at `0x40000400`,
its own programs for both DSP cores, no Elektron code at run time. The untouched bootstrap / Startup Menu stays in
NOR below `0x4000`, so a bad build is always recoverable (hold FUNC at power-on, or the MIDI/card OS upgrade).

Two faces, switched with **FUNC + MIDI**:

- **The sky** (multi-effect): inputs A–D through a chain of up to seven AERIA effects (Balloon Shop / Cloudfin
  family), weather that moves the knobs, LOOP / STARS / balloons as opt-in sources, master freeze delay, scenes on the
  crossfader, MIDI modules (GUMBALLS, PITCH, MARBLES, PONG), screensavers.
- **The sequencer**: 8 audio tracks (machines SMPL SUB FM NOIS PLUK MIDI THRU, each its own DSP channel with HP/LP
  filters, an effect, routing) plus 8 MIDI tracks on the same clock; 8 patterns, chains, song mode.

## 2. Hardware (target: MKII)

| Part | Fact | Source |
|---|---|---|
| Main CPU | Freescale MCF54455 ColdFire V4e, 264 MHz core / 132 MHz bus; data cache on (CACR `0xA50CE100`, ACR0 `0x4007E020`) | HARDWARE.md §1, Octabam CHIP.md |
| DSP | Freescale DSP56721, two DSP56300 cores, ~200 MHz each (4,532 instructions a sample); host port HI08 at `0x20000000`, core select `0xFC0A400C`; shared window `$30000–$3FFFF` (one memory, P/X/Y) | Octabam DSP.md / CHIP.md |
| DSP clock | Cores start on the crystal; core 1's first stage writes the shared PLL `x:$ffff7d = $2960b0`; bit 18 of `y:$fffff9`/`$fffffa` needed for a cold ESAI | memory: 10 Nov cold-boot recipe |
| Codec | Cirrus CS42432 (4 ADC, 6 DAC, TDM 8 × 32-bit slots), SPI on DSPI chip select 0; bootstrap writes `07 := FF` (all DACs muted) at every boot; the OS must write `07 := 00` | owner read the chip (10 Nov); REFERENCE_FINDINGS §4 |
| Outputs | Core 0's ESAI TX0, 8 slots. Ring words: **0/1 CUE, 2/3 MAIN, 4/5 HEADPHONES (MKII: right in word 4, left in word 5), 6/7 unused**. Headphones have their own DAC pair. | stock payload A P:`$30a–$359`; REFERENCE_FINDINGS §2 |
| Headphone knob | **Analog.** Stock's panel parser knows only key rows, encoders, the crossfader and identity reports; no code anywhere reads a headphone pot or writes a codec volume. The knob acts after the DAC. | REFERENCE_FINDINGS §3 (inferred: no schematic) |
| Inputs | RX0 ring words C, D, A, B (4 a sample) | stock payload A; AUDIO.md |
| Front panel | UART1 `0xFC064000`, ~317 kbaud; MKII loader handshake `60 02 70 00`; reports `0x2r` key rows, `0x3n` encoders, `0x40` crossfader; LCD blocks `0x1p`; LED rows `0x2r`, levels `0x3n id` | HARDWARE.md §3 |
| Level lamps | A 62/63, B 60/61, C 56/57, D 58/59 (even/odd = the two dies); stock also drives 128–131 on the MKII | REFERENCE_FINDINGS §5 |
| MIDI | DIN on UART0 `0xFC060000`, 31,250 baud. THRU is hardware. USB-MIDI not wanted (owner, 10 Nov). | midi.c |
| Card | CompactFlash on the ATA controller `0x90000000`, PIO, polled; FAT16/32 with long names (read and write) | card.c |
| USB | ChipIdea device: UAC2 audio (6 or 18 ch), mass storage (disk mode), AERIA's link (memory, keys, screen, RAM boot, flash) | usb.c |
| Battery RAM | 1 MB at `0x10000000`: projects (8 × 0x18000 from `0x10000000`), sequence `0x100C0000`, MIDI tracks `0x100CC400`, UI state/scenes `0x100E0000…`, loaded-sample list `0x100FBA00`, bring-up logs `0x100F9000` | seq.h, persist.c, sampler.c |
| NOR | OS image from `0x4000`; bootloader below never touched | nor.c, flasher |

MKI vs MKII: AERIA targets the MKII only (panel handshake, phones pair order, lamp ids). Nothing has been tested on a MKI.

## 3. Software architecture

```
ColdFire (one polled main loop, no interrupts)                 DSP core 0 (engine.asm, P:$200)            DSP core 1 (core1.asm, P:$33800)
 aeria_main (src/main.c)                                         frame = 16 samples, DMA2 TX ring X:$8000     up to 9 effect "entries" in the
  ├ panel: poll_panel / lcd_flush / LEDs (UART1)                 DMA3 RX ring X:$8100, DMA0 host ring X:$8400  shared window, run on blocks
  ├ dsp_poll  <- meters, stream count, recorder, stems (HI08)     hostparse: address/value writes + stream     core 0 posts (proxy slots)
  ├ stream_pump -> samples (sampler.c voices / seq.c tracks)     input matrix -> chain of 8 slots -> buses    returns one frame later
  ├ usb_poll (UAC2, MSC, link)  midi_poll (UART0)                 limiter (look-ahead) -> main / cue / phones
  ├ UI tick 61 Hz: module_tick (cloudfin/aeria12.c)              meters every 32 frames -> ColdFire
  ├ UI draw 30 Hz: module_draw -> 128x64 frame
  └ aeria_ui_sound / fx_either: knob values, routing -> DSP
```

- **Source**: `aeria-os/src/` (OS: main.c includes sampler.c, seq.c, nor.c, midi.c, usb.c, card.c), `aeria-os/dsp/`
  (engine.asm, core1.asm, stage1/stage1b.asm), `aeria-os/fx/gen_fx.py` (assembles the effects in `aeria-fx/` into
  `src/fx_table.inc`), `cloudfin/parts/*.c` (the UI: concatenated by `cloudfin/assemble.sh` into `cloudfin/aeria12.c`,
  compiled with `-DPREVIEW -DAERIAOS` through `src/aeria_ui.c`).
- **Effects**: 20 in the table (PITCH DIST GRAINS RING RESO SMEAR ROSE COMP VERB ECHO VOWL WIND FOG TWISTER HAIL
  AURORA FILT WARM PT2399 TAPE). Banned from every list by the owner: DIST (no distortion, 12 Nov), WIND, HAIL,
  TWISTER (13 Nov weather trim). `shelf.c fx_banned()`; the engine side refuses DIST in both faces.
- **The stream**: the ColdFire renders every sampler voice / sequencer track and sends it to the DSP through the host
  port (DMA0 ring, "burst" format in the sequencer: header `0xBFFF0n` + n words). Cushion ~768 samples (17 ms).
  MIDI out is queued with a due time so notes leave when their sound is heard.

## 4. Subsystems: reference, AERIA, what was wrong, what changed, how it is tested

### 4.1 Audio output and headphones (top priority)

- **Original Octatrack**: main L/R; cue L/R; headphones = a DSP crossfade of main and cue (MIX page PHONES MIX);
  headphone level is the analog knob.
- **Owner's requirement**: main out stereo L/R; headphones a copy of main by default, level from the headphone knob,
  main unaffected.
- **AERIA before**: phones = limited main + the back (cue) bus in the sky; in the sequencer each track's PHNS
  setting, defaulting to main + cue. Bench tests passed (15 Nov, RAM boot). Owner after flashing: *"there is still no
  sound out of the headphone output"* (17 Nov); earlier *"stereo, then right only, then left only, then cut out"* (13 Nov).
- **Causes found (19 Nov)**:
  1. **Start-up order (root-cause hypothesis, HARDWARE ONLY to confirm).** On a power-on, core 0's engine started the
     ESAI and lined its DMA rings up with the converter's frame *before* core 1's first stage set the shared PLL
     (both cores run from the crystal until then), and the PLL lock then stalled it while the converter ran on. The
     eight output words could sit in the eight converter slots shifted; a shift of two puts the phones copy (= main)
     on the main jacks and the silent unused words on the headphone jacks: exactly "main works, no headphones". Every
     bench test was a RAM boot with the PLL long locked, which is why none saw it. **Fix**: the engine waits for host
     flag 1 (`engine.asm gowait`); `main.c` raises it 20 ms after core 1 (and the PLL) is set. Behaviour is otherwise
     unchanged.
  2. **MKII phones pair order.** AERIA wrote left to word 4; stock (MKII branch of its output loop) writes right to
     word 4, left to word 5 — and the 12 Nov cable test agreed. Fixed in `engine.asm`.
- **Tests**: `sim/tests/test_audio_routing.py` — input A/B to main L/R at unity, headphones = main per side, L/R
  separation ≥ 30 dB on both pairs, unused words silent, silence in = silence out, the ring never slips, the same in
  the sequencer face. **VERIFIED (sim)**. The cold-start clock itself is not modelled: **HARDWARE ONLY**.
- **Headphone volume**: HARDWARE ONLY (analog). Nothing in software can or should change it.

### 4.2 Inputs and level (VU) lamps

- **Original**: the lamps above the REC keys show input level (green → yellow → red by driving both dies).
- **AERIA before**: lamps 62/60/56/58 driven from the engine's peaks, one die, 16 brightness steps (3.5 dB a step) —
  but the ladder's top threshold was twice the meter's full scale, so the two brightest steps never lit.
- **Fix**: threshold `0xE0000 → 0x72000` (`main.c vu_level`): -3 dBFS lights 14, full scale 15, -48 dBFS 1.
- **Tests**: `test_vu.py` — each input lights only its own lamp, dark when silent, monotonic over -3..-48 dBFS, top
  step at full scale, both faces. **VERIFIED (sim)**. Lamp colour (one die = one colour, not stock's green/yellow/red):
  KNOWN LIMIT, not a regression.

### 4.3 Sequencer (top priority)

Feature inventory: `history/REQUESTS_CONSOLIDATED.md` §2.7. Fixed 19 Nov:

| Bug | Cause | Fix |
|---|---|---|
| A step's VOL lock was heard on the *next* note | the track level is recomputed by the 61 Hz UI tick, after the 17 ms-ahead block was already rendered | level set when the note fires (`seq.c`) |
| MIDI tracks ignored the track's VEL | a step without velocity fell back to 100 | falls back to VEL (`seq.c mt_fire`) |
| MIDI notes wandered up to ~10 ms against their sound | the DIN queue was only serviced between screen blocks' worth of work (`lcd_flush` up to 5.7 ms) | serviced between every screen block too (`main.c stream_side`) |
| Kits loaded in on-card order (Finder's copy order), not name order | the card listing was raw FAT order | listings sorted (folders first, then A–Z) as on the original; deals made before carry no `*` marker and come back in their old order |

Tests (all **VERIFIED (sim)**): `test_seq_timing.py` (500.00 ms beats at 120 BPM, no drift over 4 bars, TEMPO + LEVEL,
headphones carry every hit, STOP silent), `test_seq_steps.py` (micro-timing ±12/48 = ±31.25 ms, ratchet ×4 evenly,
conditions 1:2 / 2:2, probability 50 %, VOL lock, swing 66 = +40 ms on off-steps, track length 3 = 375 ms, mute,
PATTERN + trig changes at the bar), `test_seq_midi.py` (8 MIDI tracks: channels, notes, velocity, micro-timing,
ratchet, lock, conditions, probability, polymetre, note-offs, place against the audio track), `test_kit.py` (kit from
the card in name order on tracks 1–8, survives a power cycle), `test_persist.py`.

KNOWN LIMITS:
- DIN carries ~1 message/ms; when several tracks' notes fall due together the last leaves up to ~5 ms later (worst seen
  6.4 ms, the first notes after PLAY).
- Filter (HP/LP) locks reach the DSP at the UI tick, so they can switch up to ~17 ms before the locked note is heard.

### 4.4 Menus and navigation

- **Rules (owner + 15 Nov)**: a page stays up until **NO**; NO goes back one level (second knob set → first → the sky
  alone / the track view); a page key again = its second knob set; no accidental state changes.
- **Fixed 19 Nov**:
  - NO on MASTER was *tap tempo*: leaving the page moved the BPM. NO now goes back; TEMPO taps.
  - Closing OUT (CUE) or the GRAIN CLOUD (REC) brought back the last page even after NO had put it away. They now return
    to exactly what was on screen when they opened.
  - The STARS / BALLOON views stayed on screen after NO. They now show only while their page is up.
- **Tests**: `test_nav.py` — every sky page key opens its page and NO returns to the sky; every sequencer page, its
  second set and NO; MASTER / PROJECTS / mixer from the sequencer; NO on MASTER changes nothing; 300 random presses
  leave the OS running with audio on main and headphones. Host tests `cloudfin/preview/nav_test.sh`, `br_test.sh`,
  `trim_test.sh` (ASan/UBSan). **VERIFIED (sim + host)**.

### 4.5 Effects (multi-effect face)

- DIST was still the default chain's slot 2 on a fresh start or INIT, and an older saved state could still hold it:
  now treated as an empty slot in the multi-effect face too (it already was in the sequencer).
- Effect DSP code is unchanged by this repair.

### 4.6 CPU and stability

Measured in the simulator (core 0's own meter, X:`$33A01–$33A04`; the simulator has no DSP wait states, so these read
low — the unit measured 36–45 % for the default chain on 12 Nov):

| Load | Sim: worst frame | Overruns | Unit (earlier) |
|---|---|---|---|
| default chain, live input | 29 % | 0 | 36–45 %, worst 55 % (12 Nov) |
| 8 tracks every step, filters, ratchets | 31 % | 0 | 95 % with 8 effects too (12 Nov), 0 overruns |
| ColdFire: one 32-sample sequencer block | 220 µs of 726 µs | — | pump 35 % + voices 22 % (12 Nov) |

Main loop: longest pass 9 ms (screen render ≤ 2.5 ms, `lcd_flush` ≤ 5.7 ms, LEDs ≤ 1.2 ms); the stream cushion is 17 ms,
so a pass never starves the audio. The probe that measured it stays in `main.c` (`-DPROBE_DRAW`, off in every build).

### 4.7 Persistence

Projects (8, battery RAM) save and load the sequence and play it; the state survives a power cycle (battery RAM kept,
DSP memory garbage): `test_persist.py` **VERIFIED (sim)**.

## 5. Build, package, flash

```sh
cd ~/octa/cloudfin && ./assemble.sh                       # the UI parts -> aeria12.c
cd ~/octa/aeria-os/src && FLASH=1 ./build.sh ../build/repair   # DSP (dsp_asm, checked) + ColdFire (m68k-elf-gcc 16.2, Docker) -> build/repair/ram/MAIN.raw
cd ~/octa/aeria-os && tools/release.sh "notes"            # flash image + AERIA_OS.bin (Elektron container, stock bootloader copy byte-for-byte) -> releases/<build name>/
```

- `build.sh` refuses a DSP listing with unassembled lines or a `mpysu`-encoded multiply (the 15 Nov phones bug).
- `flash/mk_flash.py` verifies the container (decodes it again; bootloader copy identical to stock's).
- The AERIA Flasher (`aeria-os/flasher/AERIA Flasher.app`, engine `flasher.py`) lists `releases/`, backs up battery
  RAM, RAM-boots the release and checks its name and screen **before** writing NOR, then verifies the write.
- Release marked `tested_on_unit: no` until the owner has run it.

## 6. Testing

`aeria-os/sim/` — the simulator: AERIA's real `MAIN.raw` and both DSP programs on Octabam's ColdFire and DSP56300
emulators (Octabam's checkout is used as a library, unmodified). Models the panel (keys, encoders, crossfader, LCD,
lamps), the codec's register writes, DIN MIDI, the CF card (a FAT16 image, `mkcard.sh`), battery RAM, DTIM3 time.
`aeriasim.py` drives it (keys, knobs, screen text, lamps, audio in on A–D, all eight output words out, memory).

```sh
aeria-os/sim/build.sh && aeria-os/sim/mkcard.sh    # once
aeria-os/sim/run_all.sh [MAIN.raw]                 # every test (15-25 min)
```

What the simulator does **not** show: the DSP's cold-start clock, the analog stage (DAC → jacks, headphone amp and
knob), real panel timing, DSP wait states (loads read low), USB with a real host.

### Results for the delivered build (AERIA OS Nov 19 2026 17:50:56)

Release `aeria-os/releases/Nov_19_2026_17-50-56/` (`AERIA_OS.bin` for the Flasher, `MAIN.raw`), byte-identical to the
tested image apart from the build-date string. `run_all.sh` on 19 Nov:

| Suite | Result | What it covers |
|---|---|---|
| test_audio_routing | PASS | main L/R, headphones = main (MKII order), separation, silence, ring alignment, both faces |
| test_vu | PASS | A–D lamps, level ladder, both faces |
| test_seq_timing | PASS | grid, drift, tempo change, headphones, STOP |
| test_seq_steps | PASS | micro-timing, ratchet, conditions, probability, VOL lock, swing, length, mute, pattern change |
| test_seq_midi | PASS | 8 MIDI tracks: channels, notes, VEL, locks, ratchets, conditions, polymetre, timing vs audio |
| test_kit | PASS | kit from the card in name order, survives a power cycle |
| test_persist | PASS | project save/load, power cycle |
| test_nav | PASS | every page key, NO, 300 random presses |
| test_cpu | PASS | no overruns, block time |
| host: nav_test, br_test, trim_test | PASS | UI logic under ASan/UBSan |

## 7. Must be checked on the unit (owner)

1. Power on cold: sound on **main L/R** and on the **headphones** (same mix), headphone knob changes only the phones.
2. Plug a source into A/B: the level lamps above REC1 follow it.
3. Sequencer: PLAY plays, a kit loads in file-name order.
4. If the headphones are still silent: listen to the **CUE** outputs. Main's sound there means the output slots are
   still shifted at power-on (report that; the next step is a start-up self-check of the slot alignment).

## 8. Repair log (19 Nov 2026)

See §4 for each fix. Files changed: `dsp/engine.asm` (start gate, phones order), `src/main.c` (HF1, lamp ladder, MIDI
servicing, DIST in the multi-effect face, probe), `src/seq.c` (VOL lock timing, MIDI VEL), `src/sampler.c` (sorted
listings), `cloudfin/parts/master.c` (NO), `cloudfin/parts/ui12.c` (overlays, page views). Pre-repair source:
`~/octa/backups/pre-repair-2026-11-19.tgz`.
