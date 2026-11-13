# AERIA OS (beta)

**AERIA by sadnoise** — an operating system of its own for the Elektron Octatrack MKII.
It replaces the Octatrack's OS (Elektron's bootloader and Startup Menu stay untouched, so the stock OS can always be put back from the card).

> **Beta.** This runs on real hardware, but it is unfinished, unofficial, and not affiliated with or endorsed by Elektron. Installing it removes the Octatrack OS from the unit until you reinstall it. Use at your own risk.

## What it is

Two faces on one unit:

- **The sky** — a weather-driven multi-effect on inputs A B C D: a chain of up to seven effects whose knobs the weather moves (CLEAR, CLOUDY, RAIN, STORM, SNOW, HEAT, FOG, WIND, HAIL, AURORA, TWISTER). A storm throws every free knob and lightning lends effects. Sources LOOP, STARS and BALLOONS (three recorders sung back as long grain clouds), scenes and a crossfader with four modes (scenes, DJ filter, effects wet, MOD depth), a master freeze delay with random freeze, a quad panner, MIDI modules (GUMBALLS, PITCH, MARBLES, PONG), screensavers and games.
- **The sequencer** — an 8-track groovebox: machines SMPL, SUB, FM, NOIS, PLUK (Karplus-Strong) and MIDI (polyphonic, with chords); parameter locks, ratchets with curves, conditions, per-track rate, direction, density, warp; a curved clock (after norns' *clockabout*) and a cellular automaton per track (after norns' *less-concepts*); mute scenes and scenes for track knobs; resampling; MIDI sync, transport, program change and notes in; 18-channel stems over USB.
- **MOD** (both faces) — three LFOs, a Lorenz attractor and a flock of boids, routed to any knob.

Effects (hand-written DSP56300 assembly): PITCH, GRAINS, RING, RESO, SMEAR (FFT), ROSE, COMP, VERB, ECHO (hard-panned L/R), VOWL, FILT (morphing filter driven by the Lorenz attractor), WARM, the weather's WIND, FOG, TWISTER, HAIL, AURORA — BBD, PT2399 and TAPE. Many are ports of the author's plugins *Cloudfin*, *Pinkie Pie's Balloon Shop* and *Chloaelis*.

## Manuals

The full manual (165 pages) and an 18-page quick start guide, in English (dark), are attached to the [beta release](../../releases). Their sources are in `cloudfin/preview/manual_src/` (`python3 build_all.py`).

## In this repository

| Folder | What |
|---|---|
| `aeria-os/src` | the OS for the ColdFire main processor (C): sampler, sequencer, USB audio/MIDI/link, card, panel |
| `aeria-os/dsp` | the DSP engine (DSP56300 assembly) for the two DSP cores |
| `aeria-os/fx` | `gen_fx.py`: assembles the effects into the OS's table |
| `aeria-os/flash` | `mk_flash.py`: wraps a build as an OS file the Startup Menu can install |
| `aeria-os/flasher` | **AERIA Flasher** (macOS app + engine): tries a build from memory, then flashes it, with saved-state backup |
| `aeria-os/tools` | test and measurement tools used on the unit over USB |
| `aeria-fx` | every effect: generator, assembly, tests and results |
| `cloudfin/parts` | the user interface (screens, pages, weather, sequencer UI, MOD, screensavers) |
| `cloudfin/preview` | screen previews, the manual's screenshot harness and the manual sources |
| `HARDWARE.md`, `AUDIO.md` | notes on the hardware as measured |

## What is *not* here

- **No Elektron code or files.** The Octatrack OS and its files are Elektron's and are not included. The release's `MAIN.raw` is AERIA OS's own image only.
- An installable OS file (`AERIA_OS.bin`) is not provided: it has to carry a copy of the unit's bootloader region, which is Elektron's. Build it yourself with `aeria-os/flash/mk_flash.py` from your own copy of the official OS file.
- The build uses tools from the Modwerk SDK and its Docker image (`modwerk-source-tools`), which are not included.

## Building (as the author does)

Paths in the scripts assume this layout under `~/octa/`. Roughly:

```sh
cd cloudfin && ./assemble.sh                     # UI parts -> aeria12.c
cd ../aeria-os/src && FLASH=1 ./build.sh ../build/flashimg
python3 ../flash/mk_flash.py --stock <official OS .bin> --image ../build/flashimg/ram/MAIN.raw --out ../build/flash
```

AERIA Flasher (`aeria-os/flasher`) expects the same layout, the Modwerk venv and a running AERIA OS on the unit; it is set up for the author's machine.

## History

- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — every release build and how the project got there.
- [`docs/LOG.md`](docs/LOG.md) — the full log: every request made while building AERIA OS, word for word, and what was done about it.

## Beta notes

- Latest (v0.9.2): less aliasing — PITCH, GRAINS and AURORA filter before shifting up, RING's carrier stays below Nyquist; the MOD page shows its routing in columns.
- Before that: a quality pass on fifteen effects, and BBD, PT2399 and TAPE are in (a DSP crash found in testing is fixed).
- Never press TRIG 2 (EMPTY RESET) in the Startup Menu unless you mean to wipe the unit.
- AERIA OS and the Elektron OS keep their saved data in the same battery-backed memory: back up before switching between them.

Made by sadnoise (femifleming), with Claude Code.
