# AERIA OS: the audio path at register level

Scope: what a from-scratch OS for the Octatrack MKII (no Elektron ColdFire or
DSP code) needs to know to boot the two DSP cores with its own program and
move audio from inputs A/B/C/D to MAIN, CUE and the headphones. Screen, keys,
card and USB are in another document.

Research only, 9 Nov 2026. Nothing here was run on hardware for this document.

Markers:
- ✅ read in a source that measured it, or read from the firmware
- 🟡 inferred (the reasoning is given), or taken from a chip manual from memory and not
  checked against the DSP56720/21 reference manual
- ❓ unknown

Citations use these short names:

| short | path |
|---|---|
| DSP | `~/octa/octabam/docs/firmware/DSP.md` |
| CHIP | `~/octa/octabam/docs/firmware/CHIP.md` |
| ARCH | `~/octa/octabam/docs/firmware/ARCHITECTURE.md` |
| BOOT | `~/octa/octabam/docs/firmware/BOOT.md` |
| KERN | `~/octa/octabam/docs/firmware/KERNEL.md` |
| STEM | `~/octa/octabam/docs/firmware/STEM_REC.md` |
| LAW | `~/octa/octabam/docs/firmware/LEVEL_LAW.md` |
| TRAPS | `~/octa/octabam/docs/contributing/TRAPS.md` |
| FM | `~/octa/octabam/docs/contributing/FAILURE_MODES.md` |
| PARK | `~/octa/octabam/modules/remix-switch/dsp_park.asm` |
| RSW | `~/octa/octabam/modules/remix-switch/README.md` |
| dsp.h / dsp.cpp / rtos.cpp / periph.h | `~/octa/octabam/tools/emu/ot_emu/…` |
| HOST | `~/octa/octabam/tools/harness/dsp_host/dsp_host.cpp` |
| PORT-HIST | `git -C ~/octa/octabam show 3ceba41:docs/history/COLDFIRE_PORT.md` (the port's milestone log; line numbers are in that blob) |
| MW-HWT | `~/octa/modwerk/docs/OCTATRACK_HARDWARE_TRAPS.md` |
| MW-EL | `~/octa/modwerk/docs/OCTATRACK_ELEKLOADER_MIGRATION.md` |
| MW-RX | `~/octa/modwerk/sdk/machines/octatrack/elekloader/dsp_receiver.asm` |
| AFX | `~/octa/aeria-fx/` |

Everything the sources know about the DSP was learnt by watching the stock
1.40C payloads run. Where this document says "stock does X", our OS is free to
do something else. The silicon facts (memory, clocks, the boot ROM, the ports)
carry over.

---

## 1. The chips, clocks and memory

| fact | value | |
|---|---|---|
| DSP part | Freescale Symphony **DSP56721** (`DSPB56721AG`): ONE chip with TWO DSP5636x (56300-family) cores | ✅ board photo, CHIP:63-64, 70 ("two separate DSP chips" is retracted) |
| core clock | 199.9 MHz. That is 4,532 instructions per sample at 44.1 kHz, measured with a timer probe on a unit | ✅ CHIP:64, 122 |
| who sets the core clock | not found. Neither payload writes PCTL. The PLL reset default would give 183.456 MHz, and the measured clock is not that, so something else sets it (the boot ROM, or the mode/PINIT pins) | ❓ CHIP:122. 🟡 If our program also leaves PCTL alone, it should get the same 199.9 MHz |
| external DSP memory | none; everything is on-chip | ✅ CHIP:65 |
| per-core memory (default map, OMR MS=0, which stock runs) | P 8K (`P:0-$1FFF`), X 36K, Y 48K | ✅ CHIP:251-265 and DSP:87-101. `P:$2000+` holds no written word on either core, measured on a unit |
| shared memory | 64K words at `$30000-$3FFFF`, reachable by both cores. P, X and Y alias there. Zero wait states as X/Y, 1 as P. Contention is per 8K block | ✅ CHIP:66, 232-245 (alias measured on hardware) |
| code in the shared window | executes; written at runtime and called from either core | ✅ DSP:98-101 |
| other map options | OMR MS/MSW1/MSW0 give P 16K/24K/32K/36K, trading X/Y. Untested on the unit | ✅ manual (CHIP:251-265), 🟡 unexercised |
| Y holes | `Y:$0C000-$2FFFF` reads silence, `Y:$40000+` freezes the core | ✅ CHIP:215-223 |
| high X | `X:$C000` and `X:$F000` answer single-word probes, but 1024-word blocks there fail: not usable | ✅ DSP:499-502 |
| cycles | 4,532 per core per sample. Stock's own frame work costs about 1,410 of them. Words are not cycles: a 1-word displaced move takes ~4 cycles, a 2-word one ~6 | ✅ CHIP:121-130, 190-199 |
| ColdFire | MCF54454 at 264 MHz, bus 132 MHz, FlexBus 66 MHz | ✅ CHIP:61-62, 84-92 |

**Which core does what.** These are stock's choices, not wiring:
- payload A (core 0) runs tracks 5-8, the ESAI audio port, the final
  mixdown and the output limiter;
- payload B (core 1) runs tracks 1-4;
- stock carries core 1's per-track audio to core 0 THROUGH THE COLDFIRE: the
  read-back block is forwarded as 512 words into core 0's `X:$4400`/`$2400`
  bank every frame;
- a 16-word core 1 → core 0 mailbox also sits in the shared window.

Sources ✅: DSP:32-33, 360-367, 458-475, 263-273; CHIP:303-307.

**The audio port in stock.** Only core 0's X-side ESAI moves audio, with DMA
in both directions. Under the port, core 1 reports 0 ESAI frames.
- ✅ dsp.h:198-202 and MW-HWT:400 ("payload A only").
- Conflict: DSP:397 says "both cores configure it at boot". Core 1 does
  `jsr` into payload A's `P:$30082/$3008a` from its entry stub (CHIP:275), so
  core 1 may run part of the setup code.
- 🟡 Whether core 1 has its own ESAI pins on the 56721, and which peripherals
  each core owns, is not in any source. ❓

For AERIA: **plan on core 0 owning all audio I/O.** Core 1 is optional extra
compute and talks to core 0 through the shared window (section 4.5).

---

## 2. Resetting and booting the DSPs

### 2.1 Reset

- ✅ **Nothing in the stock OS or bootstrap resets the DSP**, and no reset
  GPIO is known. A board reset (power-on) is the only known way to put the
  cores in their boot ROM.
  - RCR SOFTRST restarts the ColdFire and keeps SDRAM, but the DSPs keep
    running the old program. The next OS's upload then hangs, measured on a
    unit (ARCH:172-178; FM:277-280).
  - PARK:4-8: "the soft reset restarts the ColdFire but not the DSP … and
    nothing in the OS or the bootstrap resets it".
- 🟡 For AERIA this means two things:
  1. On a cold boot, when the Elektron bootstrap jumps to `0x40000400`, both
     cores are in the HI08 boot ROM, waiting. BOOT:42-45 says the stock
     upload at the OS entry assumes exactly this.
  2. For any warm restart (development RAM boots, an OS switch, a crash
     handler that reboots), we need a **park**, as octabam's REMIX SWITCH
     built:
     - a host command (`CVR = 0x80 | HV`; the vector is at `P:2·HV`) enters
       a handler that stops DMA0-3, stops both ESAI ports and clears HPCR
       bit 7;
     - the handler then emulates the boot ROM's protocol (2.3) in software;
     - the ColdFire drains each core's host-side receive register before it
       re-uploads.
     - This is proven on a unit (FM:280, PARK:40-127). Our own DSP program
       should carry an equivalent park handler from day one.
- ❓ Whether a ColdFire GPIO drives the DSP's RESET pin. The schematic is
  unknown. Finding one would remove the need for the park.

### 2.2 The ColdFire side of the host port (HI08)

The window `0x20000000-0x20000fff` is the **HI08 host-side register file of
the core selected by the GPIO byte at `0xFC0A400C`** (write 0 for core 0, 1
for core 1). Each register is one byte, at a 4-byte stride, in the low byte of
a 16-bit FlexBus access. ✅ dsp.h:3-22; ARCH:253-270; DSP:13-21.

| CF address | read | write | bits |
|---|---|---|---|
| `0x20000000` | ICR | ICR | RREQ 0, TREQ 1, HF0 3, HF1 4, INIT 7. `0x81` = INIT\|RREQ, an interface reset |
| `0x20000004` | CVR | CVR | HV 6:0, HC 7. The firmware polls bit 7 until the DSP takes the command |
| `0x20000008` | ISR | — | RXDF 0, TXDE 1, TRDY 2, HF2 3, HF3 4, HREQ 7 |
| `0x2000000C` | IVR | IVR | |
| `0x20000014` | RXH | TXH | bits 23:16 |
| `0x20000018` | RXM | TXM | bits 15:8 |
| `0x2000001C` | RXL | TXL | bits 7:0. The write to TXL **sends** the word; the read of RXL **takes** it |

Host-side facts:
- **16-bit and 32-bit writes to `+0x1C`.** A halfword write delivers its two
  bytes to TXM/TXL as one word. A longword write sends two words. Stock's
  per-frame blocks are 16-bit values, two per longword. ✅ ARCH:268-270.
- **The FlexBus chip select.** The window is CS2, `CSCR2 = 0x180` ✅
  (ARCH:253-256).
  - 🟡 The stock OS runs its DSP upload before main and with no CS2 set-up
    of its own before it (BOOT:28-38), so the Elektron bootstrap must have
    configured CS2 and the select GPIO by the time it jumps to us.
  - 🟡 Stock writes `0x0FFF0000` to `0xFC00801C` right after the upload
    (DSP:23). On the MCF5445x that address would be CSMR2. Its meaning is not
    decoded.
- **🟡 `0xFC0A400C` is a GPIO port output-data register.** By the MCF5445x
  map from memory it is PODR_TIMER. Not checked against the RM.
- **The DSP side of the same port** (✅ the addresses the payloads use,
  PARK:27-30):
  - HSR `x:$FFFFC3` (bit 0 HRDF, bit 1 HTDE);
  - HPCR `x:$FFFFC4` (bit 6 HEN);
  - HORX `x:$FFFFC6`.
  - 🟡 HCR `x:$FFFFC2` and HOTX `x:$FFFFC7`, from the 56300 HI08 map.
    Payloads write HOTX as `M_HOTX` (DSP:451).

### 2.3 The boot ROM upload protocol

Stock's sequence, `FUN_40001e50` at OS entry `0x4000050c` (✅ DSP:13-24):

```
GPIO 0xFC0A400C := 0              ; core 0
ICR := 0x81                       ; INIT|RREQ: interface reset
upload(bootstrapA, 50 words, P:0x31000) via the BOOT ROM
~10,000-iteration delay loop
upload payload A through bootstrap A's own record loader
GPIO := 1                         ; core 1
ICR := 0x81
upload(bootstrapB, 58 words, P:0x32000) via the BOOT ROM
upload payload B through bootstrap B
```

The boot ROM protocol, as the firmware's loader `0x40001d4c` drives it:
- ✅ dsp.h:15-21, ARCH:272-278, PARK:13-18, 98-127.
- Before the first word, the loader writes 0 to CVR (`+0x04`).
- For every word: spin until `ISR & 0x06` (TXDE|TRDY), then write TXH, TXM,
  TXL (high, mid, low). The TXL write sends.
- The words, in order:
  1. **count** (number of 24-bit program words);
  2. **load address** in P (stock uses the shared window: `P:$31000` for
     core 0, `P:$32000` for core 1);
  3. **count words** of program, written into P;
  4. then the ROM **jumps** to the load address. PARK:13-18 describes the
     emulated ROM's exit as `r0 = address + count`, `r1 = address`, CCR
     clear.
- ✅ The emulator holds a core until its ROM has seen the last word
  (dsp.h:42-45). 🟡 On the chip the ROM is assumed to do the same.
- ✅ Timing: both stock uploads together (~157 KB of payload) take ~60 ms on
  a unit (RSW:78).
- 🟡 For AERIA there is no need for a two-stage bootstrap. Ways to load:
  - Send our whole core program directly through the ROM.
  - Or, as stock does, send a small loader first. It is worth having anyway:
    it lets us load X and Y and use a record format.
  - Choosing the load address:
    - Loading into the shared window lets both cores' code coexist at fixed
      addresses. That is what stock did, at `$31000`/`$32000`.
    - Loading at `P:0` puts it in private internal P.
    - ❓ Whether the 56721 ROM accepts any load address. Stock only ever used
      the window.

Stock's second stage, NOT needed by AERIA, recorded so the echo behaviour is
understood (✅ DSP:41-52, ARCH:275-278):
- The payload is a stream of records `space (0 P / 1 X / 2 Y), address,
  count, words`. A space word above 3 terminates it.
- Words are 24-bit little-endian in the image.
- The ColdFire reads each record's space word back as an echo, waiting on
  RXDF.

Traps already paid for:
- ✅ **HPCR bit 7.** Stock's payload start sets HPCR bit 7, and after that
  the ColdFire reads 24-bit replies as the low byte repeated in every lane
  (`0x010101` where a boot reads `0x000001`). An upload that expects ROM-mode
  echoes then fails silently.
  - FM:279; PARK:55-66 (that bit 7 is "the lane mode" is 🟡).
  - 🟡 Rule: our program leaves HPCR as the ROM left it unless we measure
    otherwise, and our park restores it.
- ✅ Stale words in the host-side receive register after a warm restart
  break the next upload (FM:279). Drain RXDF before uploading.
- ✅ Stock's start also sets bit 14 of `y:$FFFFFD` and `y:$FFFFFE`, which a
  power-on leaves clear; the park clears them (PARK:120-126). ❓ What those
  registers are.
- ✅ The vendored emulator ran no peripheral while a long interrupt was open,
  so a park entered through a host command must `rti` into its loader rather
  than run inside the interrupt (PARK:67-80). This is an emulator artefact;
  the chip has no such state.

---

## 3. Audio I/O: ESAI, codecs, slots

### 3.1 What is known for certain

**The serial interface is the ESAI on core 0, X side**, in network (TDM) mode.
Stock's set-up (✅ DSP:397-415; PORT-HIST:1240-1258; dsp.h:198-202):

| register | stock value | decode |
|---|---|---|
| PDRH `x:$FFFF98` | `$AA0000` | Port H top byte: ETI1 ERI1 ETI0 ERI0. The ESAI clocks are generated **from the EXTAL crystal**, not from the core clock (DSP56720RM §8.2.2.4, as cited in PORT-HIST) |
| SAICR | `$40` | SYN = 1: the receiver runs on the transmitter's clocks |
| TCCR / RCCR | `$F40F00` | THCKD, TFSD, TCKD set (**the DSP drives the bit clock, frame sync and high-frequency clock: the DSP is clock MASTER**); TPSR = 1, TPM = 0, TFP = 0, **TDC = 7 → 8 slots** |
| TCR `x:$FFFFB5` | `$37D01` | network mode (TMOD = 1), TSWS = `$1F` (32-bit slots), TE0 (one transmit data line, TX0) |
| TSMA | `$FF` | 8 active transmit slots |
| RSMA | `$0F` | **4 active receive slots** (the ESAI-in ring holds 4 words per sample) ✅ PORT-HIST:1806, 1874 ("`RSMA = 0x0f` is the firmware's own, payload A `P:0x30032`") |
| RCR `x:$FFFFB7` | (value not recorded) | RX0 enabled ❓ exact bits |
| ESAI_1 (Y side) TCCR_1 | `$F00F00` | the same dividers, TSWS = `$1E`. Configured, but no DMA feeds it ✅ dsp.h:201-202. ESAI_1's TCR/RCR are at `y:$FFFF95`/`y:$FFFF97` (PARK:29, 45-46) |

Derived facts:
- ✅ **Frame:** 8 slots × 32 bits = 256 bit clocks per sample.
  `fs = EXTAL / 512` on the ÷2 reading of the clock chain. PORT-HIST:1258-1271
  marks the ÷2 reading 🟡 against the manual's prose.
  - 🟡 The crystal is then 22.5792 MHz. Nobody has photographed it or
    measured SCKT (PORT-HIST:1286-1290).
- ✅ **Sample rate 44.1 kHz** (ARCH:155-156; LAW:20-23).
  - The DSP frame is **16 samples = 362.8 µs**, measured on a unit with
    DTIM3 (ARCH:325-327, 423-425).
- ✅ **Word length 24 bits** in a 32-bit slot.
  - Stock's level-law captures are bit-exact 24-bit (LAW:20-23).
  - The emulator carries 24-bit words per slot (dsp.cpp:381-394).
- ✅ **The ESAI is live on core 0 on a unit.** CF METER read core 0's TUE = 0
  and ROE = 0 in every take (ARCH:423).
- ✅ **Stock's DMA rings on core 0:**
  - **DMA2: `X:$8000-$80FF` → TX0.** That is 256 words = 32 samples × 8
    slots, two 16-sample halves.
  - **DMA3: RX0 → `X:$8100…`.**
  - Both are armed at boot from `P:$3001D`.
  - Sources: dsp.h:198-202; MW-HWT:400; DSP:467.
  - ❓ The RX ring's extent and layout. MW-HWT:400 says `$8100-$833F`.
    PORT-HIST:1830-1832 has the input copy reading `r1 = $8280/$8282`,
    `n1 = 3`.
  - ❓ The DCR2/DCR3 values. Not recorded in any source. Only DCR0
    (host-port receive) is: `$8E82C0` (DSP:412-415).
- ✅ **Stock's frame clock is the TX ring itself.** Core 0's main loop polls
  DMA2's source pointer DSR2 (`x:$FFFFE7`, dsp.cpp:408) until it reads
  `$8070` (bank A) or `$80F0` (bank B), one slot before a half boundary.
  Then it does its frame (DSP:467). There is no timer.

### 3.2 Slot layout: what is mapped, what is not

**Outputs (TX0 ring, in "ring words", word k of each sample's 8):** these
were measured under the port with the stock program, by level and by which
mixer control moves which word. Not measured on a unit.

| ring word | stock writes | physical jack |
|---|---|---|
| 0 / 1 | the CUE bus L/R (the mixdown's first pass, cue gains) ✅ DSP:472 | 🟡 CUE OUT L/R |
| 2 / 3 | **MAIN L/R** ✅ DSP:472, dsp.h:212-214 (the fixture sample × 0.70 at level 64) | 🟡 MAIN OUT L/R (the natural reading; PORT-HIST:1808-1809, 1876-1878 say "hardware's to say") |
| 4 / 5 | `Y:$40+2j × words 0/1 + Y:$41+2j × words 2/3`: a per-sample blend of CUE and MAIN ✅ DSP:474. Reads 3.1-3.7 dB below MAIN with no cue send ✅ PORT-HIST:1876 | 🟡 **headphones**. The Octatrack's phones MIX blends CUE and MAIN, and an equal-power centre is −3 dB. dsp.h:213-216 labels this pair "Cue" instead: conflict, unresolved |
| 6 / 7 | never written by the mix ✅ PORT-HIST:1874-1876 | 🟡 unused (four inputs, six outputs: 6 of 8 slots) |

- ✅ The ESAI's slot counter and DMA2's ring index are **not in a fixed
  phase** in the emulator. A tone on ring word 2 came out on slot 1 in one
  run and slot 0 in another, and the rotation moved within a run as DMA2 was
  re-armed (PORT-HIST:1862-1871; dsp.cpp:409-417). So "slot k" and "ring
  word k" differ.
- 🟡 On hardware the frame sync pins slot 0. If our program does not use
  DMA, or arms DMA in step with the frame sync, ring word = slot.
- ❓ Which TX slot drives which DAC pin on a unit is not measured.

**Inputs (RX0, 4 slots):**
- ✅ The ColdFire's input pages (eDMA ch 7 → `0x80005660 + page·0x100`) hold
  C/D at `+0x00` and A/B at `+0x80` (STEM:7681-7739, DSP:367). That is the
  stock program's naming, and the stock recorder uses it.
- ✅ Under the port, a tone on RX0 slot 2 lands in the A/B pair. The receive
  ring's phase is fixed across runs (PORT-HIST:2215-2229).
- Conflict 🟡: the DIR AB / DIR CD mixer test under the same port called
  RX0 slots 0/1 "A/B" and 2/3 "C/D" (PORT-HIST:1840-1852). The two namings
  disagree by two slots. PORT-HIST:2224-2229 leaves it "unmeasured on the
  unit".
- ✅ The stock THRU track trails its input by 80 samples, five frames
  (STEM:7717-7722). The THRU path end to end is 155 samples under the port,
  128 of it the ColdFire's capture lag (PORT-HIST:1895-1903). A DSP-only
  passthrough needs none of that (section 5).

### 3.3 Codecs

| question | answer |
|---|---|
| ADC / DAC / codec part numbers | ❓ No source names them. CHIP section 1 lists only the CPU and DSP from the board photo |
| interface to the DSP | ✅ TDM on one ESAI data line each way, 8 × 32-bit TX slots and 4 RX slots, DSP is master (3.1). 🟡 So the converters are TDM slaves, or a CPLD or glue fans the TDM stream out to I²S converters |
| codec control bus (I²C/SPI) from the ColdFire | ❓ No ColdFire code that configures a codec has been identified. The DSPI (`0xFC05C000`) chip select 1 is the RTC (STEM:3047-3057). The I²C block at `0xFC058000` is untouched on the RTC path (STEM:3059-3062); nobody has searched for other users. The `sys` task does a "serial + SPI start-up" (KERN:74), not decoded |
| codec control from the DSP | ❓ Stock writes Port H (PDRH) and both ESAIs. Whether any DSP GPIO or serial host interface talks to a codec is not known |
| codec reset / mute / power pins | ❓ Not identified |
| the output path is DC-coupled | ✅ A bad DSP state can put "constant, up to full-scale DC on MAIN, CUE and the headphones" (`~/octa/modwerk/sdk/octabam/docs/HARDWARE_MATRIX.md:50-52`). **Test with outputs turned down / into a protected input** |
| analog input gain | ❓ Stock's GAIN AB/CD and DIR AB/CD are DSP-side gains carried in the ColdFire's level pages (STEM:7511-7556; PORT-HIST:1840-1858). No analog gain control is known |

🟡 The strongest available hint is **behavioural**:
- Stock writes nothing to the codecs that anyone has found.
- Stock's DSP set-up is just Port H + ESAI + DMA.
- So **the converters are probably hardware-strapped and start converting as
  soon as the DSP drives the clocks.**
- The first hardware experiment (section 5) answers this. If our DSP program
  drives the ESAI exactly as stock does and audio appears, no codec
  configuration exists or is needed.

---

## 4. ColdFire ↔ DSP data exchange

### 4.1 Stock's per-frame protocol (for reference; AERIA may simplify)

✅ DSP:352-395, 458-487; ARCH:240-270; KERN:89-117; rtos.cpp:62-70, 1627-1640; dsp.cpp:591-600.

1. **Core 0 detects a half boundary.** It sees DSR2 at `$8070`/`$80F0`
   (3.1), saves the bank pointers, and **writes a "bank word" to HOTX**
   (`P:$73`).
2. **That word is the ColdFire's frame interrupt.**
   - The interrupt is IRQ1, INTC0 source 1, vector `0x41`, level 5, handler
     `0x4000AAD0` (✅ KERN:91, 113).
   - ICR was set with RREQ. 🟡 The host-side RXDF then asserts HREQ, which
     is wired to the ColdFire's IRQ1. The emulator models the frame
     interrupt as exactly this edge (dsp.cpp:591-597, 2954-2958;
     rtos.cpp:1627-1640). The pin wiring itself is not seen.
3. **The ISR reads RXL** (the ping index, `0x4000AAFA`; dsp.h:18-21 and the
   override note in machine.cpp:70-91). That takes the word. Core 0 was
   waiting for HTDE at `P:$97`, and continues.
4. **The ISR writes CVR = `0x8C`** (host command vector `$0C` → `P:$18`) and
   spins on HC until the DSP takes it. Vectors `$08`/`$09` (`P:$10`/`$12`)
   mean "DMA a block in / out".
   - Before each block the ColdFire sends the DSP destination
     (`$6000 | X address`) and a halfword count.
   - The DSP's host-command handler programs DSP DMA0 from HORX into that
     address (`DCR0 = $8E82C0`).
   - ✅ DSP:354-358, 410-415; ARCH:261-265.
5. **The ColdFire's eDMA does the bulk moves:**
   - ch0: blocks to the DSP, DADDR = the port `0x2000001C`, 16-byte bursts;
   - ch1, ch6, ch7: read-backs (core 1's tracks, MAIN/CUE, the inputs).
   - The chain is ch1 → ch6 → ch7 by MAJORELINK, then a 7-state completion
     ISR on vectors `0x48/0x49/0x4F` (level 6).
   - ✅ DSP:354-367; KERN:93; periph.h:381-415.
6. Per frame, per core, the ColdFire sends:
   - 336 words of track records to `X:$080`;
   - 64 words of voice records to `X:$000`;
   - a 64-word level page to `X:$4800`/`$2800`;
   - and, on demand, 32-word sample-slot records.
   - It pulls 256 words of per-track post-FX audio back from each core.
   - ✅ DSP:360-367.
7. **Deadlines** (all measured on units):
   - The CPU share of the frame ISR is the limit. Long single frames pass;
     ~86-90 % of the CPU in the ISR stops the frames for good
     (MW-HWT:272-288).
   - Masking every interrupt for 300 µs stops core 0 for good: it misses its
     bank-word take past a ring half. Up to 200 µs is safe (MW-HWT:246-256).
   - Holding off the frame interrupt therefore kills stock's DSP program.

### 4.2 Host flags: a side channel that needs no reads

✅ The DSP can signal the ColdFire without the ColdFire taking any word.
- The DSP writes HF2/HF3 in its HCR; the ColdFire reads ISR bits 3/4.
- Modwerk's DSP loader answers every packet this way, because of the trap
  below (MW-EL:592-597; MW-RX:6-15).
- HF0/HF1 run the other way (ICR bits 3/4).

### 4.3 Traps in the exchange

- ✅ **Host read races are invisible in the emulator.** With no wait states
  on the host bus, a ColdFire burst can finish before the DSP has armed its
  DMA. Words then stay in HTX and a DSP waiting on HTDE waits forever. ot_emu
  makes a host read wait for the DSP's word, so it never shows (MW-EL:579-590;
  MW-RX:6-12).
- ✅ **No `rep` in code that may delay a host-command handler.** A `rep`
  cannot be interrupted. Late answers tear the read-back by a word and, at
  worst, scribble ColdFire RAM (TRAPS:256-268; MW-HWT:376-388).
- ✅ **DMA vs core contention per 8K X block.** Dense core work in the block a
  DMA channel uses (`$0000`, `$2000`, `$4000`, `$8000` on core 0) stalls the
  DMA. The symptoms: one-sided output, muted tracks leaking, a stuck
  sequencer. Keep hot DSP data out of the blocks your DMA rings live in. Y has
  no DMA. (TRAPS:264-268; MW-HWT:390-418.) The emulator models neither
  contention nor interrupt latency.
- ✅ **A host write at the frame interrupt's entry is lost every other frame**
  (TRAPS:893-905). This is a stock-payload artefact; it does not bind a new
  program.
- ✅ The emulator's DMA arm rule matters. The stock program arms its ESAI DMA
  channels long after the ESAI raised the request, and the port fires a
  request raised before arming (TRAPS:1483-1500; dsp.cpp, `setDmaTriggerOnArm`).

### 4.4 What AERIA actually needs from the exchange

🟡 For audio passthrough: **nothing after boot.** The ESAI is the DSP's own,
clocked from the crystal, and the DSP is the clock master. Once core 0 runs a
program that services RX0 → TX0, audio flows without the ColdFire.

For a real engine, a simpler protocol than stock's is possible:
- one parameter block per frame through HI08 + DSP DMA0;
- the ColdFire reads levels or meters through host flags or occasional
  host-command replies.
- The rule that falls out of 4.1/4.3: **the DSP must never block its audio
  path on the ColdFire.** Stock's core 0 waits at `P:$97` for HTDE every
  frame; that is the design that dies when the ColdFire is late. Decouple
  with rings and let the DSP run free.

### 4.5 Between the two cores

- ✅ The shared window `$30000-$3FFFF` is one memory, P/X/Y aliased
  (CHIP:232-245).
  - Stock uses `$30000-$30047` for per-frame staging and a 16-word
    core 1 → core 0 mailbox (`$37F00` in octabam, `$38000` in stock).
  - Payload B's boot zero loop clears `$37F00-$3FFFF` (CHIP:269-286;
    TRAPS:876-891). That is stock behaviour, irrelevant to our own program.
- ✅ The chip has an inter-core interrupt/data port, the ICC: each core can
  interrupt the other and pass write/poll data (CHIP:244-245, citing
  DSP56720RM §1.4.14).
  - 🟡 The register addresses as used by stock: core A writes `Y:$FFFFD7`
    and waits on bit 1 of `Y:$FFFFD6`; core B polls bit 1 of `Y:$FFFFD3` and
    reads `Y:$FFFFD4`. Inferred from use and modelled that way in the
    emulator (dsp.h:607-612; dsp.cpp:541-560).
  - Stock core 1 also reads core 0's bank word from `y:$FFFFD4` (DSP:376-377)
    and handshakes word by word with core 0 at the head of every frame
    (DSP:387-390).
- 🟡 For AERIA:
  - Core 1 can write its finished audio straight into a shared-window ring
    that core 0 mixes, without the ColdFire hop stock uses.
  - Keep the two cores in different 8K blocks of the window where possible
    (contention is per block, CHIP:242-243).
  - Use the ICC (or a flag word in the window) for the per-block handshake.

---

## 5. The smallest viable thru program

### 5.1 What it must do (core 0)

🟡 Derived from section 3. Untested.

1. **Be uploaded** through the boot ROM (2.3) to core 0. Core 1 can be left
   in its ROM; nothing in stock's audio path needs core 1's ESAI.
   - ❓ Whether a core 1 left in the ROM disturbs anything. Probably not.
2. **Set SR/OMR.** Stock's start is `move #>$300,sr` (IPL 3, every mode off,
   PARK:94-97). Leave OMR at the default map, and do not touch PCTL.
3. **Set Port H for EXTAL-derived ESAI clocks:** `PDRH = $AA0000`.
4. **Configure the ESAI as stock does:**
   - SAICR `$40`;
   - TCCR and RCCR `$F40F00`;
   - TCR `$37D01` (TE0, network, 32-bit slots);
   - TSMA `$FF`, RSMA `$0F`;
   - RCR with RE0 and the matching network/word-size bits (❓ stock's exact
     RCR value is not recorded);
   - ❓ TSMB/RSMB, probably 0;
   - ❓ the order of the writes (the manual usually wants the clocks and
     masks set before TE/RE).
5. **Move samples from RX0 to TX0.** Two routes:
   - **Polled / interrupt-driven, no DMA.** Wait for the receive flag, read
     RX0; wait for the transmit flag, write TX0. Track the slot with the
     frame-sync flags (RFS/TFS in SAISR).
     - 🟡 The SAISR bit numbers below are from the DSP56362 ESAI from memory
       (RFS 6, ROE 7, RDF 8, TFS 13, TUE 14, TDE 15). Check them against the
       DSP56720RM.
     - 🟡 The ESAI register addresses beyond TCR/RCR are also from the 56362
       map: TX0 `x:$FFFFA0`, RX0 `x:$FFFFA8`, SAISR `x:$FFFFB3`, SAICR
       `x:$FFFFB4`, TCCR `x:$FFFFB6`, RCCR `x:$FFFFB8`, TSMA/TSMB
       `x:$FFFFB9/BA`, RSMA/RSMB `x:$FFFFBB/BC`. TCR `$FFFFB5`, RCR
       `$FFFFB7` and PDRH `$FFFF98` are ✅ from PARK and PORT-HIST, which is
       consistent with that map.
   - **DMA rings as stock does.** A 256-word TX ring (32 samples × 8 slots)
     and an RX ring of 4 words per sample, with the core polling DSR2 for
     half boundaries. ❓ DCR2/DCR3 must be worked out from the manual; they
     are not in any source.
6. **The first-light mapping should not depend on the slot map.** Two
   unknowns would otherwise each silence the test:
   - which RX slot is input A (two-slot conflict, 3.2);
   - which TX slot is MAIN.

   So the first program writes, on **every** TX slot, the mean of the four
   latest RX words, at about −12 dB. Anything on any input then reaches
   every output.
7. **Second program: the slot identifier.**
   - TX slot k = a sine at 500·(k+1) Hz at −30 dBFS. Listen on MAIN, CUE and
     the phones, or record over a USB interface, to map TX slots to jacks.
   - Then route RX slot k alone to all outputs, for k = 0..3, to map the
     inputs.
   - This is exactly the emulator's `--audio-in tones` idea, applied on the
     unit (PORT-HIST:2229-2230).
8. **Include a park handler** at a host-command vector (2.1) so the next RAM
   boot can re-upload without a power cycle.

### 5.2 Sketch (OUR code, illustrative, not assembled, not run)

```asm
; core 0, loaded by the boot ROM at P:$0000 (or a window address)
start:  move    #>$300,sr               ; IPL 3, modes off (as stock's start)
        movep   #>$aa0000,x:<<$ffff98   ; PDRH: ESAI clocks from EXTAL
        movep   #>$40,x:<<$ffffb4       ; SAICR: SYN                  (56362 addr 🟡)
        movep   #>$f40f00,x:<<$ffffb6   ; TCCR: master, 8 slots       (56362 addr 🟡)
        movep   #>$f40f00,x:<<$ffffb8   ; RCCR                        (56362 addr 🟡)
        movep   #>$ff,x:<<$ffffb9       ; TSMA: 8 slots               (56362 addr 🟡)
        movep   #>$0f,x:<<$ffffbb       ; RSMA: 4 slots               (56362 addr 🟡)
        ;  RCR: RE0 + network + 32-bit slot -- value to be derived (❓)
        movep   #>$37d01,x:<<$ffffb5    ; TCR: TE0, network, 32-bit slot (last: enables TX)
        clr     a
loop:   ; wait RDF, read RX0 into a running mean (b); wait TDE, write b to TX0.
        ; Slot accounting is unnecessary for first light: every TX slot gets the mean.
        ; Use a DO-less polling loop; no rep (TRAPS:256-268).
        ...
```

Written with stock's register values (3.1) and nothing copied from the payload
code. Every 🟡 address must be checked against the DSP56720/21 reference manual
before it runs on a unit.

### 5.3 Known / unknown for passthrough

| item | status |
|---|---|
| boot ROM protocol and host register file | ✅ (2.2-2.3), proven by stock and by the park on a unit |
| DSPs are in the ROM when our OS starts after a power-on | ✅ BOOT:42-45 |
| ESAI configuration values that make audio on this board | ✅ stock's (3.1); our use of them 🟡 |
| ESAI register addresses beyond TCR/RCR/PDRH, SAISR bits | 🟡 56362 map, not verified for the 56721 |
| RCR value, DMA2/DMA3 DCR values, write order | ❓ |
| codec needs configuration / reset / unmute | ❓ (🟡 probably not) |
| RX slot → A/B/C/D | 🟡 two candidate maps |
| TX slot → MAIN / CUE / phones | 🟡 ring words 2/3 MAIN, 0/1 CUE, 4/5 phones |
| core clock with our program | 🟡 199.9 MHz if PCTL is untouched |

---

## 6. Hosting the aeria-fx effects in our own engine

### 6.1 The ABI they are written to

This is the Modwerk/stock FX-slot ABI ✅: DSP:146-170, 298-318, 522-553; HOST:9-16;
TRAPS:472-476, 706-722; FM:318-320; AFX/tap/tap.asm; AFX/rose/rose.asm:1-60.

| item | contract |
|---|---|
| entry points | `init` (called when the slot's id changes) and `proc` (called every block). `init < proc <= init + 256` |
| audio | `r0` = block base. Interleaved stereo `[L,R]` × n, **processed in place** in X (stock passes `r0 = 0`, so the block is `X:0`). Every effect reads and writes through `r0`, so the block can sit anywhere |
| block size | `n7` = frames in this call. The stock block is 16 samples. A block can be **split into two calls** at a trig: on the first call the `a` register is non-zero; on the second `a = 0` and `r0` is advanced by 2 × split (ROSE uses `a`, FM:318-320 the `r0` offset). Effects loop `do n7,…` per sample |
| parameters | `r6` → 6 page-1 words `x:(r6+0..5)`, each `value << 16` (0..127; LFO fraction in bits 8-15). Page 2 at `x:(r6+$C..$E)`, bits 16-23 and companions in 8-15 |
| state | `r7` → a per-instance X block. `$00-$83` usable; `$84+` is host-owned in stock (TRAPS:472-476). `init` zeroes what it uses |
| buffer | the Y buffer base, read in `init` only, from `x:(x:$213)` (stock's allocator pointer). FX1 slots are 3,072 words, FX2 slots 16,384 |
| registers | `init` must preserve `r1` (stock's FX1 dispatcher keeps the id there). Effects set their own `m` registers (e.g. `m = $FFFFFF`). Absolute Y scratch must sit at `Y:$800` or above |
| forbidden | `rep`, unaudited operand orders, backward `bra` (assembler limit), labels that prefix other labels (TRAPS:182-215) |

### 6.2 What each effect relies on beyond the block

Read from the sources, 9 Nov 2026:
- **PITCH, RESO, SMEAR, GRAINS:** the Y buffer through `x:$213` (one read
  each). PITCH uses one interleaved ring of 15,870 words (`AFX/pitch/pitch.asm:13-16`).
- **DIST, RING/PH:** no Y, only `r7` state.
- **ROSE:**
  - only `r7` state (128 words zeroed at init, `rose.asm:24-32`);
  - plus a **P-memory table** read with `p:(r2)`, whose base `$FAB1E0` is a
    placeholder the packager patches (`rose.asm:153-156`);
  - plus the `a`-flag split convention.
- **HEAD:**
  - Modwerk's **KEYBUS**: per-track FX1-input copies at
    `Y:$800 + track·$40`, and the other core's at `$33E00`/`$3BE00` in the
    shared window, with seed marks and generation counters;
  - `X:$420`, the track being dispatched, a stock variable;
  - `head.asm:21-40`. It is the one effect that is tied to the stock/Modwerk
    engine's layout.
- **TAP:** writes silence. Its role is to sit on FX1 under KEYBUS.
- **Costs** (`AFX/STATUS.md:10-21`; dsp_host instructions/sample): PITCH 195,
  RESO 215, DIST ≤152, RING 225, SMEAR 269, GRAINS ≤253, ROSE ≤199. The six
  together ~1,326 at worst, against 4,532 per core per sample (~3,100 left
  after stock's own work, a figure that does not apply to our engine).

### 6.3 What a self-written engine must provide

🟡 These are design consequences:
1. **A dispatcher** that, per track per block:
   - sets `r0` to a 16-frame interleaved X block;
   - sets `n7 = 16`, or two calls with `a ≠ 0` / `a = 0` and `r0` advanced
     at a split, if we keep sample-accurate parameter splits;
   - sets `r6` to a 15-word parameter block in the same `value<<16` format;
   - sets `r7` to a zeroed ≥ `$84`-word X state block;
   - calls `init` on (re)assignment and `proc` every block.

   Cheapest path: keep the ABI exactly, and every aeria-fx `.asm` assembles
   unchanged with `-org`.
2. **An allocator word at `X:$213`** pointing at a cell that holds the
   instance's Y base during `init`, or patch the four effects' one-line base
   read to our own convention.
3. **Y buffers** of 3,072 / 16,384 words. Internal Y is 48K per core plus the
   64K shared window.
4. **P space for code and tables.**
   - Internal P is only 8K per core on the default map.
   - The shared window (64K, executable) is the natural home for an effect
     library both cores can call.
   - OMR's alternative maps can trade X/Y for up to 36K internal P.
5. **For HEAD:** either re-implement a key bus (it is just "copy every
   track's FX1 input to `Y:$800 + t·$40`, mirror to the window"), or rewrite
   its reads.
6. **Keep stock's hard-won rules even in our own engine:** no `rep`; hot data
   outside DMA blocks; ramp knobs in 16 samples.
   - Our engine does not have stock's `x:$3F7F-$3FBF` frame scribble
     (TRAPS:909-939) or `X:$20-$FF` scratch, so most "absolute address" traps
     vanish.

---

## 7. Testing a custom DSP program in the emulator

### 7.1 `dsp_host`: one effect or one engine routine, no ColdFire

`~/octa/octabam/tools/harness/dsp_host/dsp_host.cpp`; the assembler `dsp_asm.cpp`
beside it.
- ✅ It boots the vendored dsp56kEmu cores and loads a `.mem` image. The
  format, from `tools/build/dsp_modmap.py --dumpmem`: per module `[u8 space][u32
  addr][u32 count]` then count × u32, terminated by space `0xFF`.
- It calls `init`/`proc` with the ABI above, with WAV in/out, `-meter`
  (instructions per block), `-guard` (writes outside the instance), `-dirty`,
  `-split`, `-core` with `-memB` for two cores sharing the window, and
  `-skew` as a cross-core race fuzz (HOST:9-90, 547-693).
- 🟡 For AERIA, write our own `.mem` (our engine's records) in that format
  rather than the stock dumps, which are Elektron bytes.
  - dsp_host "boots its own setup routine (found by opcode pattern)"
    (HOST:57-60), so a non-stock image may need a flag or a small patch
    there.
  - `-noctl` exists (HOST:567); what it skips is not read here.
- ✅ Traps:
  - Its default audio block `X:$80` collides with stock scratch: pass
    `-audio 0` for stock-like placement (TRAPS:564-575).
  - It keeps P private and X/Y shared only in the window, so it **cannot
    show P/X/Y aliasing** (DSP:295-296).
  - It does not run a frame routine or the ESAI.

### 7.2 `ot_emu`'s `DspPair`: the whole boot and the ESAI, with or without a ColdFire image

`~/octa/octabam/tools/emu/ot_emu/`. `test_dsp.cpp` is the template.

The model:
- ✅ `DspPair` models:
  - the HI08 host side at `0x20000000` and the select GPIO `0xFC0A400C`;
  - the **boot ROM** (vendored `DspBoot`: count, address, words, jump);
  - two cores whose `$30000-$3FFFF` is one memory;
  - the ICC mailbox;
  - core 0's X-side **ESAI with RX0 input and TX0 capture**.
  - Sources: dsp.h:1-45, 157-260; dsp.cpp:164-560.
- Its public API (dsp.h:126-260):
  - `read`/`write` on the host window;
  - `tickInstructions`/`tickSamples`;
  - `setAudioInput(interleaved, channels)` / `setAudioTones(true)`, which
    puts sine 500·(k+1) Hz on RX slot k;
  - `setAudioInputFromBoot(true)`, needed by a program that never sends
    stock's `0x8C`, because input otherwise starts at the first `0x8C`
    (dsp.cpp:381-387);
  - `setAudioCapture`/`audioOut(core)`;
  - `setAudioStream(Main|Cue|All)`;
  - `peek` and the various watches.
- 🟡 **A standalone C++ test needs no ColdFire image:**
  1. Construct `DspPair`.
  2. Drive the boot ROM through `write()` exactly as 2.3, with `select = 0`,
     ICR `0x81`, then the words.
  3. Enable input from boot.
  4. `tickSamples(N)`.
  5. Read `audioOut(0)`.

  This is the fastest loop for the thru program and the slot identifier.
- ✅ Full system: `ot_emu --image <raw binary loaded at 0x40000400> --dsp
  [--dsp-rt] --audio-in FILE|tones --audio-out PREFIX --dsp-peek … --dsp-pcwatch
  … --dsp-watch …` (main.cpp:1369-1460; `~/octa/octabam/tools/emu/README.md`).
  - Any raw image boots at `0x40000400` with SP `0x48000000`
    (machine.h:144-145).
  - 🟡 Many port features (the load runner, `--sequencer`, card staging)
    assume stock addresses. A from-scratch image should start with bare
    `--dsp --max N` runs.

Emulator caveats that bear on a custom program (✅ unless marked):
- **The frame interrupt is keyed on stock's PC.** The ColdFire's IRQ1 edge is
  raised when core 0 writes HOTX **from `P:$73`** (`g_bankIdPc`,
  dsp.cpp:51, 2954-2958). A program that signals the ColdFire elsewhere gets
  no vector `0x41` unless `--frame-timer` (a free-running 16-sample timer,
  main.cpp:1333, 1457) is used, or the constant is changed.
- **The ESAI timing is emulator-defined.** One slot fires per `ips/8`
  instructions regardless of PDRH, TCCR or the crystal (dsp.cpp:357-375,
  530-531; dsp.h:40-41). The clock-source configuration is not checked.
- The peripherals are the vendored **56362 (X side) / 56367 (Y side)**
  models, not the 56721's (dsp.cpp:183-186). 🟡 Addresses that differ on the
  56721 would not show.
- **P is 512K words in the emulator** (dsp.h:80), against 8K internal on the
  chip. A write to `P:$2000+` works in the emulator and vanishes on a unit
  (DSP:87-97).
- Not modelled:
  - memory/DMA contention, interrupt latency, wait states;
  - host-read races: the emulator makes a host read wait for the DSP
    (MW-EL:586-590);
  - aliasing in dsp_host.
- RAM is zeroed unless `--dsp-dirty` (the chip never zeroes) (dsp.h:240-243).
- The TX slot/ring rotation in the emulator is not hardware behaviour
  (3.2). Read TX by ring word or by tone frequency.

---

## Unknowns that block audio passthrough

In order of how likely each is to stop first light:

1. **Codec configuration and control pins.** Nobody has identified the ADC
   or DAC parts, or any I²C/SPI/GPIO traffic to them, or reset/mute lines.
   - 🟡 Stock appears to do nothing beyond clocking them from the DSP.
   - If they need set-up, we must find the bus and the part first: a board
     photo, or a logic trace of the codec pins at power-on.
2. **The ESAI register map and the bit-level details for the 56721.** We
   have stock's working values for PDRH, SAICR, TCCR/RCCR, TCR, TSMA and
   RSMA, but:
   - the RCR value, TSMB/RSMB, the DMA2/DMA3 DCRs and the set-up order are
     not recorded;
   - the addresses of TX0, RX0 and SAISR, and the SAISR bits, come from the
     56362 map (🟡).
   - Needs the DSP56720/21 reference manual, or a read of stock's set-up
     routine in the user's own dump (read for understanding, not copied).
3. **Which jack is which slot.** RX slot → A/B/C/D has two candidate maps
   (3.2), and TX ring words → MAIN/CUE/phones is inferred.
   - A "sum to all slots" first-light program removes this as a blocker.
   - The slot-identifier program then measures it on the unit.
4. **The DSP RESET line.** None is known, so every development iteration
   either power-cycles or needs a working park handler in the previous
   program. This is not a blocker for a cold boot.
5. **The core clock's origin.** It is 199.9 MHz under stock, by an unknown
   mechanism. This is not a blocker (the ESAI runs from EXTAL), but cycle
   budgets assume it.
6. **The board-level CS2/GPIO set-up for the HI08 window** is assumed done
   by the Elektron bootstrap before it jumps to `0x40000400` (🟡 from BOOT's
   entry order). If AERIA ever replaces the bootstrap, CSAR2/CSMR2/CSCR2 and
   the `0xFC0A400C` pin set-up must be found.
7. **Core 1's ESAI (or other peripherals).** Not needed for passthrough.
   Whether core 1 can reach audio pins at all is unknown.
