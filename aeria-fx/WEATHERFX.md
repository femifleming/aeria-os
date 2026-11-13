# Weather effects for AERIA OS: WIND, FOG, TWISTER, HAIL, AURORA

Ports of the Balloon Shop's new weather (`PinkiePiesBalloonShop/Source/dsp/MoreWeather.h`) as ordinary
chain effects. Each folder holds `gen.py` (constants recomputed for 44.1 kHz; writes the `.asm`),
the `.asm` (and a `_test.asm` where the dsp_host harness needs another buffer address), `manifest.py`,
`test.py`, `test_results.md`, and copies of the two shared helpers `wxlib.py` (source macros) and
`wxtest.py` (the gate; `wind/` holds the masters). Run a gate with
`tools/run.sh python3 /aeria/<fx>/test.py`.

Nothing here has run on hardware. Every number is from dsp_host (the emulator in `modwerk-source-tools`).
The Rainbow in MoreWeather.h was not asked for and is not ported.

## What every one of them does

- Knob 0 is **AMT** (default 0). At AMT 0 and settled the routine is at **rest**: it returns after 4..7
  instructions a call and the output is the input bit for bit. Gate: full-range ramp, exact from the first
  sample.
- Coming up from rest each one clears its own buffer over a few dry calls (256 words a call), so it needs
  no zeroed RAM. `init` zeroes the r7 block and takes the buffer base from the word X:$213 points to.
- The slow logic of the plugin (gusts, new delay targets, stones, curtains, the twister's progress) runs
  inside the effect once a call (16 frames, 0.363 ms) on a 24-bit LCG (s = 214013 s + 2531011, top 23 bits).
- All modulo registers used are set back to $ffffff on return; FOG also borrows r6 and puts it back.

## Memory

### Is core 1's X free? Not all of it.

Read from `aeria-os/dsp/core1.asm` and `aeria-os/src/main.c` (`fx_configure`): core 1's engine uses, in its
own X,

| X address | what |
|---|---|
| $3f | the always-zero word |
| $213 | pointer to the base word |
| $2c8..$2ca | the slot walk's scratch |
| $900..$91f | the block the effect works on |
| $1000..$17ff | the eight slots' r7 state blocks ($1000 + $100 s) |
| $1900..$1907 | the eight base words (entry + 11) |

ECHO's right line is X:$4000..$7fff; VERB keeps nothing in X outside its r7 block. So **X:$0000..$1fff is
not free as a whole**. What the sources leave untouched: X:$1908..$3fff and X:$8000..$8fff. That is
13.8K words, not 20K; and only $2000..$3fff (8,192, on an 8K boundary) and $8000..$8fff (4,096, on a 4K
boundary) are usable by a modulo ring of that size. Whether $8000..$8fff exists rests on the note in
gen_fx.py ("each core: X $0-$8fff, measured 10 Nov 2026"); I did not measure it.

### Proposed map

| effect | core | space | base (the `ybase` field) | words | alignment the code needs |
|---|---|---|---|---|---|
| WIND | 0 | Y | $9000 | 4,096 ($9000..$9fff) | base a multiple of 4,096 |
| FOG | 0 | Y | $8a00 | 5,632 ($8a00..$9fff) | base = $a00 past a multiple of $1000 (rings at +0, +$200, +$600) |
| TWISTER | 1 | **X** | $8000 | 4,096 ($8000..$8fff) | base a multiple of 4,096 |
| AURORA | 1 | **X** | $2000 | 8,192 ($2000..$3fff) | base a multiple of 8,192 |
| HAIL | 1 | **X** | $2000 | 8,190 ($2000..$3ffd) | none |

- WIND and FOG share Y:$9000..$9fff: never both (the plugin's own rule).
- **AURORA and HAIL share X:$2000..$3fff: never both.** TWISTER has its own region and can run beside either.
- X:$1a00..$1fff (1,536 words) stays spare.
- The three core-1 effects read the base from the same word as everyone else and use it as an X address, so
  the host moves them by changing the tuple, not the code. They only work where that X range is theirs:
  **core 1 only**. WIND and FOG keep their buffer in Y:$8a00.., which only core 0 has free: **core 0 only**.
- FOG's three rings: C $8a00..$8bd2 (467 used of 512), B $8c00..$8fc8 (969 of 1,024), A $9000..$9f9a
  (3,995 of 4,096). Any other base must keep each ring's start on a multiple of its reserved size; the
  offsets are the `OFFSETS` table at the top of `fog/gen.py`.

Sizes are constants at the top of each `gen.py` (`RING`, `WINDOW`, `LEN`, `STONES`, `SLOT`, ...).

## The EFFECTS tuples for gen_fx.py (org TBD)

```python
    # folder,   org,  ybase,   core, cost (worst call, instructions a sample, dsp_host)
    ('wind',    TBD, 0x09000, 0, 119),    # Y ring 4,096; never with fog
    ('fog',     TBD, 0x08a00, 0, 120),    # Y $8a00-$9fff; never with wind
    ('twister', TBD, 0x08000, 1, 189),    # core 1's X:$8000-$8fff (the base word is used as an X address)
    ('hail',    TBD, 0x02000, 1, 134),    # core 1's X:$2000-$3ffd; never with aurora
    ('aurora',  TBD, 0x02000, 1, 192),    # core 1's X:$2000-$3fff; never with hail
```

Code sizes: wind 528, fog 423, twister 588, hail 546, aurora 772 words (limit 2,048). `mod.dsp.asm` in each
manifest names the hardware build (`<fx>/<fx>.asm`). gen_fx.py will stop until `LAW` has an entry for
every knob below.

| effect | cost at the dearest settings: mean / worst call | at rest |
|---|---|---|
| WIND | 113.5 / 118.2 | 7 a call |
| FOG | 117.5 / 119.9 | 7 a call |
| TWISTER | 188.8 / 188.9 inside an event | 6 a call |
| HAIL | 43.9 / 133.9 over a minute at 34 stones a second (six sounding and one being born); plugin settings: about 9 / 65 | 4 a call |
| AURORA | 185.2 / 192.0 | 7 a call |

## Knobs (for the display law)

`v` is the knob 0..count-1. Suggested `LAW` lines use gen_fx.py's own helpers.

### WIND
| knob | default | meaning | suggested display |
|---|---|---|---|
| AMT | 0 | fade 0..1 = v/127: mix = smoothstep(1.5 amt), lean x amt | `_pct(v)` |
| DPTH | 64 | largest pitch drift in a full gust: 1200 log2(1 + 0.012 v/64) cents (64 = 20.6 c, 127 = 40.7 c) | `_n(1200 * math.log2(1 + 0.012 * v / 64), 'C')` |
| GUST | 64 | mean time between gusts 3.5 s x 2^((64 - v)/16) (56 s .. 0.23 s) | `_ms(3500 * 2 ** ((64 - v) / 16))` |
| LEAN | 64 | lean depth, x v/64 of the plugin's -35 % / +12 % | `'%d%%' % round(v * 100 / 64)` |

### FOG
| knob | default | meaning | suggested display |
|---|---|---|---|
| AMT | 0 | fade: mix = smoothstep(1.5 amt); lowpass 18 kHz x 2^-(amt x DARK/32) | `_pct(v)` |
| BLUR | 100 | allpass gain g = 0.6 v/100 (100 = the plugin's 0.6) | `'G ' + _n(0.6 * v / 100)` |
| DARK | 64 | lowpass at full fog 18 kHz x 2^-(v/32) (64 = 4.5 kHz, 127 = 1.15 kHz) | `'OPEN' if v == 0 else _hz(18000 * 2 ** (-v / 32))` |

### TWISTER
| knob | default | meaning | suggested display |
|---|---|---|---|
| AMT | 0 | the trigger (see below) | `'GO' if v >= 64 else 'ARM'` |
| RISE | 7 (count 13) | semitones at the top, 0..12 | `_sg(v)` |
| SPIN | 64 | spin at the top: 0.3 + 5 v/64 Hz | `_hz(0.3 + 5 * v / 64)` |
| TIME | 0 | 0 = RND 10..20 s (the plugin); else 2 s x 2^(v/32) (2.04 .. 31.3 s) | `'RND' if v == 0 else _ms(2000 * 2 ** (v / 32))` |

### HAIL
| knob | default | meaning | suggested display |
|---|---|---|---|
| AMT | 0 | fade: births x amt | `_pct(v)` |
| RATE | 64 | stones a second at full amount: 2.2 x 2^((v - 64)/16) (0.14 .. 34) | `_n(2.2 * 2 ** ((v - 64) / 16)) + '/S'` |
| SIZE | 64 | slice length x 2^((v - 64)/64) of 5..30 ms | `'X' + _n(2 ** ((v - 64) / 64))` |
| BNCE | 64 | gap ratio 0.62..0.78 shifted by (v - 64)/64 x 0.25, held to 0.2..0.95 | `_n(min(0.95, max(0.2, 0.70 + (v - 64) / 64 * 0.25)))` |
| WDTH | 114 | pan spread v/127 (114 = the plugin's 0.9) | `_pct(v)` |

### AURORA
| knob | default | meaning | suggested display |
|---|---|---|---|
| AMT | 0 | fade into the reverb (glided 1.2 s inside, as the plugin) | `_pct(v)` |
| TIME | 64 | decay T60 = 9 s x 2^((v - 64)/32) (2.25 .. 35 s) | `_ms(9000 * 2 ** ((v - 64) / 32))` |
| TONE | 64 | loop damping as the plugin's lowpass: 6 kHz x 2^((v - 64)/24) (0.94 .. 11.6 kHz) | `_hz(min(11650, 6000 * 2 ** ((v - 64) / 24)))` |
| SHFT | 1 (count 4) | UNIS / +12 (the plugin) / +19 / +24 | `('UNIS', '+12', '+19', '+24')[v]` |

## Rules for the host

- **Order.** The plugin runs twister, wind, fog on the sound, then adds hail and aurora made from
  (L + R)/2 of the result. HAIL and AURORA add to what reaches them and pass the dry through untouched.
- **WIND**: on with the wind; AMT = the wind's fade. Never with FOG (whichever came on last stays).
- **FOG**: on with the fog; AMT = its fade.
- **HAIL**: on with the hail; AMT = its fade. Stones are born inside the effect. After AMT reaches 0
  the stones in the air bounce out (a stone lasts about 0.5 s, 1.5 s at most at the plugin's settings), then it rests.
  Taking it out of the slot earlier cuts them.
- **AURORA**: on with the aurora; AMT = its fade. After AMT 0 the amount glides out (1.2 s) and the tail
  rings on (T60), then it rests when the curtains have been under 512 LSB for half a second: about 11 s
  at the plugin's settings. Taking it out of the slot earlier cuts the tail.
- **AURORA and HAIL are never loaded together** (one X region). In the plugin's auto weather they are
  never neighbours (aurora leads to clear or cloudy, hail comes only out of a storm).
- **TWISTER**: it can sit in a slot for good; at rest it costs 6 instructions a call.
  - Start: write knob 0 >= 64 (127). The effect starts ONE event when it sees the knob go from under 64 to
    64 or more, and does not look at the knob again until the event is over.
  - Re-arm: it must see the knob under 64 on at least one call (0.36 ms) while no event runs. Holding 127
    after the event does not start another. Write 0 any time after the start.
  - A freshly loaded TWISTER is armed: loading it with knob 0 already at 127 starts an event on its first call.
  - Length: TIME 0 draws 10..20 s inside the effect; otherwise TIME sets it. The event starts 16 calls
    (5.8 ms) after the trigger (the ring is cleared first).
  - The plugin starts one from its button, and by itself about every 150 s while it is windy AND cloudy
    (chance a second 1/150, not while one runs): that draw is the host's.
  - Progress is not written back to the knobs. It is the state word UH (r7 + 5, Q23 0..1) and the event's
    amount AEN (r7 + $1c) if the UI wants to peek.

## Against the plugin

Kept: signal flow, constants (recomputed for 44.1 kHz and a 16-frame tick), smoothstep envelopes, lean and
spin laws, equal-power pans, the Hermite read in WIND and TWISTER.

| effect | what differs |
|---|---|
| all | The plugin's own 0.35 s fade of the switch is the host's AMT; inside, AMT is only glided 23 ms (WIND, FOG) against knob steps. 24-bit LCG in place of xorshift64*. The pictures' outputs (heading, height, azimuth, hit counter) are not computed. |
| WIND | Tick every 16 frames at 44.1 kHz (plugin: 32 at 48 kHz), coefficients recomputed. Ring 2,048 frames (the delay never passes 30 ms = 1,323). Gains ramped a frame from once-a-call values. |
| FOG | Lowpass coefficient recomputed every 4th call, and only when the amount or DARK moved. The lines run at 1/8 level. **Left over on digital silence while the fog is on: DC up to 38 LSB (-107 dBFS)** -- a line stores floor(v + g x delayed), which can hold -1 or -2 for good; truncating toward zero would cost 36 instructions a sample. Impulse response against the float plugin: 9e-6. |
| TWISTER | **Window 46.1 ms (2,032 frames), not 60 ms**: 60 ms needs 5,324 words on an 8K boundary, which would take AURORA/HAIL's region. u^1.3 is a cubic (within 4 cents at RISE 7). RND length is 10 s x 2^r (same 10..20 s, log-even, no divide). Envelopes, pitch, spin once a call with ramped gains (plugin: a sample). sin by a quintic. |
| HAIL | **6 stones (plugin 8)**; at 2.2 a second all six were never busy in a minute. **A stone records its slice at birth, forward** (the plugin cuts it from the last 0.26 s of a 2 s ring, which does not fit), so its first hit comes one slice (5..31 ms) after birth. The envelope is recorded into the slice. Slices are multiples of 16 frames; hits start on calls (gaps keep their fraction). Rate 1.5 slices are held to 20.3 ms and rate 2 to 15.2 ms (1,365 words a stone). Half-frame reads are the plugin's Hermite exactly. Single stones against a float model: 5e-7. |
| AURORA | **Lines x 0.3855** (1039 1361 1651 1931 frames for the plugin's 2703 3523 4282 5014), gains set for the same T60; the loop lowpass sits 1.545 x higher so the highs keep about the same decay time (880 Hz at TIME 64: 7.9 s measured, 7.8 s for the plugin's loop). Window 2,204 frames (2,205). The 500 Hz biquad is a state-variable highpass. Each curtain looked at every 4th call (1.45 ms; plugin 1.33 ms). The Hann weight is ramped linearly over a call. Lines at 1/4 level, saturating. Products in the loop are rounded (mpyr/macr/rnd), not floored. Rest threshold 512 LSB (-84 dBFS) in place of 1e-5. Taps move whole frames: no interpolation, exact. |

## Not verified

- Hardware: nothing flashed or run on the unit.
- The X buffers were rendered at X:$10000 on dsp_host's core 0 (`*_test.asm`); the hardware builds differ
  in the three lines of `init` that fetch the base (compared word for word in each gate). X:$2000 and
  X:$8000 on core 1 were not exercised, and that X:$8000..$8fff exists is taken from gen_fx.py's note.
- Core 1's real time limit with TWISTER (189) and AURORA (192) beside VERB or ECHO: not measured.
- Static cycle counts (`cycle_count.py`) were not run; costs are dsp_host instruction counts.
- Instruction forms these sources use that no other effect in `aeria-fx` or `aeria-os/dsp` uses yet (all
  plain 56000 forms, assembled, round-tripped and run in the emulator, but new to the chip under our
  code): `lua (Rn)-Nn,Rm` (WIND, TWISTER, HAIL), `move y:(Rn)-Nn,x0` (FOG), `mpyr` / `macr` (AURORA).
- The display laws above are suggestions; gen_fx.py was not edited or run.
