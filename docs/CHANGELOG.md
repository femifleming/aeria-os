# AERIA OS — changelog

The full request-by-request log is in [LOG.md](LOG.md).

## How it got here (before the release builds; days approximate)

- **9 Nov 2026** — Started as Modwerk modules on top of the stock Octatrack OS (AERIA multi-effect with Cloudfin / Balloon Shop effects, weather, LOOP, STARS, balloon recorders, GUMBALLS MIDI, pitch tracker, screensavers, own boot art). Same day the owner asked for a whole new operating system: AERIA OS boots from the untouched Elektron bootloader, with its own panel driver, screen and DSP program (first audio from the inputs).
- **10 Nov** — Outputs brought to life (the codec's DAC mute register), DSP engine with an 8-slot effect chain on two cores, SD card (FAT16/32) and sampler, USB audio + link, cold boot from flash. Overnight: the backlog (routing, quad panner, CCs, the first PDF manual).
- **11 Nov** — Stream starvation (the "lofi" bug) fixed with an interrupt-driven host port; core 1 effect engine; grain engines; FFT SMEAR; weather effects WIND, FOG, HAIL, AURORA, TWISTER; MIDI over DIN in/out/thru.
- **12 Nov** — Sequencer mode (8 tracks: SMPL, SUB, FM, NOIS, PLUK, MIDI; locks, ratchets, conditions, mute scenes, resampling, 18-channel USB stems); scenes, master freeze delay, readable displays; output stage late-sample fix and phones L/R fix; level lamps, card lamp, ARR lamp; storm that throws every knob and lightning that lends effects; no distortion effects; AERIA Flasher app.
- **13 Nov** — Chloaelis ports (FILT with Lorenz, hard-panned ECHO, WARM, BBD, PT2399, TAPE), MOD matrix (3 LFOs, Lorenz, boids), crossfader modes, sequencer scenes, curved clock and cellular automaton (after norns scripts), quality pass on fifteen effects, the DSP crash with the delays found and fixed, anti-aliasing; English dark manual and quick guide; public beta on GitHub.

## Release builds

### AERIA OS Nov 13 2026 09:26:48

- Tested on the unit: yes
- BBD removed at the owner's request (its row stays empty so saved projects keep their numbers; an old BBD slot loads empty). PT2399 (half rate, cost 630) and TAPE stay. Includes the half-rate PT2399, the DSP-room engine and the placement fix. Checked on the unit: effects, old BBD rows load as empty, 8/8 features.

### AERIA OS Nov 13 2026 09:22:34

- Tested on the unit: yes
- BBD and PT2399 run at half rate (22.05 kHz inside, polyphase halfband filters, images -64 dB): BBD 780 -> 485, PT2399 970 -> 630. Their top end now stops near 10 kHz. Checked on the unit: every effect, 54 delay swaps incl. PT2399 -> BBD, 8/8 features.

### AERIA OS Nov 13 2026 08:21:44

- Tested on the unit: yes
- Effect placement tries both DSP cores (a big effect like SMEAR is no longer refused, or put where it overloads, because of the order effects were picked). Headphone output measured clean for 4+ minutes (sky with the owner's chain, sequencer, effects swapped every 2 s). Includes the 08:03 DSP-room build.

### AERIA OS Nov 13 2026 08:03:09

- Tested on the unit: yes
- More DSP room: the engine runs from core 0's private memory (no wait states): the first core's own load halved (sky worst case 700 -> 370; sequencer torture 89% -> 50%), master EQ and weather/track filters 3x cheaper, main feedback code removed. Room on core 0 for effects 990 -> 1280 in the sky, 560 -> 1060 in the sequencer. Card preview starts after half a second instead of loading the whole file. Checked on the unit: effects, 8/8 features, torture, full chains with EQ + weather + USB recording.

### AERIA OS Nov 13 2026 07:10:51

- Tested on the unit: yes
- Anti-aliasing: PITCH, GRAINS and AURORA filter before shifting up (folded tones 37-65 dB lower), RING's carrier stays below Nyquist and its input is band-limited. MOD page shows its routing columns. Checked on the unit: every effect loads and plays without overruns, 8/8 features, torture.

### AERIA OS Nov 13 2026 05:53:11

- Tested on the unit: yes
- BBD, PT2399 and TAPE are back: the DSP crash is fixed (PT2399 moved to core 1's X:$2000; it now shares memory with BBD, HAIL and AURORA). Quality pass on 15 effects. Checked on the unit: every effect loads and plays, the crash sequences survive 12 + 4 rounds and 30 random swaps, 8/8 features, torture.

### AERIA OS Nov 13 2026 05:35:53

- Tested on the unit: yes
- Quality pass on 15 effects (exact-silence tails for PITCH/RING/RESO/ROSE/FOG, no clicks on PITCH FDBK, RING CNTR, COMP, WIND LEAN, FOG BLUR, TWISTER RISE, ROSE SPRD; GRAINS flat at unison, DENS 127 = four grains; SMEAR cleaner sustain; VERB mono-safe; RESO DTND balanced; AURORA rider). PT2399 loop fix. BBD/PT2399/TAPE still held back. Checked on the unit: 8/8 features, regression, effects.

### AERIA OS Nov 13 2026 04:18:16

- Tested on the unit: yes
- FILT (Lorenz filter), new hard-panned ECHO, WARM; MOD page; crossfader modes incl. DJ filter (works in the sequencer too); sequencer scenes for track knobs; CLK curved clock (clockabout); cellular automaton RULE/SEED/CAMD (less-concepts); STOP = dry + clear sky; LOOP PLAY/STOP; bright/dim steps. BBD, PT2399 and TAPE held back (a DSP crash under investigation). Checked on the unit: 8/8 new-feature tests, regression, MIDI.

### AERIA OS Nov 12 2026 22:57:53

- Tested on the unit: yes
- MOD page (LFOs, Lorenz, flock), random freeze on the master delay, LRNZ panner mode, no main feedback; storm, no DIST, phones fix, lamps, screen fix (the build flashed on 12 Nov at 22:58)

GitHub pre-releases: v0.9.0-beta (04:18:16), v0.9.1-beta (05:53:11), v0.9.2-beta (07:10:51), v0.9.3-beta (09:26:48).
