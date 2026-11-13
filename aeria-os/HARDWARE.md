# AERIA OS: Octatrack MKII hardware map (everything except the audio/DSP path)

This is the map for writing a new OS from scratch. It contains no Elektron code. Addresses
like `0x4001f4dc` point to routines in the stock 1.40C image. They are given only so you can
find where a fact came from. AERIA does not call them.

Markers: ✅ read from a source (doc, emulator model, Modwerk code) that states it as measured
or byte-exact · 🟡 inferred (by the source or by me; I say which) · ❓ unknown.

Source abbreviations:
- `OB/` = `~/octa/octabam/`
- `FW/` = `OB/docs/firmware/`
- `EMU/` = `OB/tools/emu/ot_emu/`
- `MW/` = `~/octa/modwerk/`
- `EL/` = `MW/sdk/machines/octatrack/elekloader/`

"RM" means the MCF54455 Reference Manual. It is **not** in the local sources, so any fact
marked "RM knowledge" is 🟡 and needs checking against the manual.

---

## 1. CPU, clocks, boot hand-off, memory map

### 1.1 The part and its clocks

| Fact | Value | Mark | Source |
|---|---|---|---|
| CPU | Freescale ColdFire **MCF54454/5 (V4e core)**, 32-bit, big-endian, VR266. The board photo reads `MCF54454`; another note says `MCF5445A`. | ✅ | `FW/CHIP.md:61`, `FW/ARCHITECTURE.md:26` |
| Toolchain | `-mcpu=54455`. The V4e adds `mvs/mvz/mov3q/byterev/ff1` and EMAC. | ✅ | `EL/boot.s:17`, `EMU/machine.h:31-34`, `FW/MIDI.md:5-8` |
| MMU | Present on V4e but **not used** by stock ("no MMU"). | ✅ | `OB/docs/contributing/PLACEMENT.md:236` |
| FPU | Assume none; build with `-msoft-float`. | 🟡 RM knowledge | – |
| Clock tree | crystal 24 MHz → VCO 528 → **CPU 264 MHz**, **internal bus 132 MHz** (clocks the UARTs, PIT, DSPI and DTIM), FlexBus 66 MHz | ✅ | `FW/CHIP.md:73-92` |
| PLL | `PCR` at `0xFC0C4000` = `0x16777731`, set by the bootstrap. Stock reads it back and expects (top byte × 12 MHz) = 264 MHz. AERIA need not touch it. | ✅ | `FW/CHIP.md:75-82`, `EMU/machine.cpp:59-62` |

### 1.2 What the bootstrap does before AERIA runs

The bootstrap lives in NOR `0x0000..0x3FFF` (never write it). Its reset path, in order:

1. PLL, then RAMBAR1.
2. GPIO, SDRAM controller (precharge, two refreshes, mode register), the panel link, and the
   Startup Menu if a key is held.
3. Caches on.
4. Depack NOR `0x4012` into **`0x40000400`**.
5. `ACR0 = ACR1 = 0`, `CACR = 0x0008C000`.
6. `move.l 0x8000050A,-(sp) ; jsr 0x40000400`.

✅ `FW/BOOT.md:8-24`, `FW/ARCHITECTURE.md:123-133`

Whatever sits at `0x40000400` when that `jsr` lands **is** the OS. Nothing on the path tests
or clears SDRAM outside the unpacked image, except TESTMODE. ✅ `FW/BOOT.md:21-24`

### 1.3 Machine state when AERIA's entry runs

| Item | State at entry | Mark | Source |
|---|---|---|---|
| PC | `0x40000400`, reached by `jsr`. The return address is at `0(sp)`. | ✅ | `FW/BOOT.md:19` |
| Argument at `4(sp)` | A flags long copied from SRAM `0x8000050A`. Bit 0 = Startup Menu EMPTY RESET chosen. Bit 1 = panel tag `0x26` byte was `2`. | ✅ | `FW/BOOT.md:129-131`, `FW/BOOT.md:137-139` |
| Raw Startup-Menu bytes | Still readable in SRAM: `0x80000200` (key-row byte / menu selection) and `0x80000508`. | ✅ | `FW/BOOT.md:113-150` |
| CACR | `0x0008C000`. My reading of the bits: instruction cache and branch cache on, **data cache off**. | ✅ value · 🟡 decode (RM knowledge) | `FW/BOOT.md:17-18`, `EL/boot.s:55` ("what the bootstrap leaves") |
| ACR0 / ACR1 | 0 | ✅ | `FW/BOOT.md:17` |
| SP | The bootstrap's own stack. The value is unknown. Stock's entry replaces it at once with `movea.l #0x48000000,%sp` (the top of cached SDRAM, through the uncached alias). | ❓ value · ✅ stock's choice | `FW/BOOT.md:31` |
| SR / interrupt mask | No interrupts are set up on this path; the bootstrap does its panel work polled. A ColdFire reset leaves SR = `0x27xx`. The emulator starts with `SR = 0x2700` (its own choice). | 🟡 | `FW/BOOT.md:87-88` ("no DSP, no cache set-up, no interrupts"), `EMU/machine.h:20-24` |
| VBR | Unknown. It is probably the bootstrap's table in NOR. **AERIA must set its own.** | ❓ | – |
| Watchdog | None observed. A unit spun forever at `SR 0x2700 ; bra .` until power-cycled. | ✅ (absence observed) | `FW/ARCHITECTURE.md:70-76` |
| DSP56721 | Cold boot: held in its HI08 boot ROM, waiting for an upload. After a Modwerk RAM boot: both cores parked by host command `$0F` (not reset). | ✅ | `FW/BOOT.md:41-45`, `EL/boot.s:206-243` |
| Panel UART1 | Already initialised and used, polled, by the bootstrap. | ✅ used · 🟡 config reusable | `FW/BOOT.md:113-121` |
| USB controller | The bootstrap sets the ULPI PHY select in PORTSC1. After a soft reset (RAM boot) the controller keeps its state, except that Modwerk clears USBCMD.RS first. | ✅ | `EMU/usb.h:8-10`, `MW/docs/OCTATRACK_HARDWARE_TRAPS.md:155-159` |

### 1.4 What AERIA must set up itself

All of this follows from the table above; each item is the same thing stock's entry and
kernel init do (`FW/KERNEL.md:54-59`).

1. **Stack.** Use `0x48000000` like stock, or SRAM.
2. **VBR and a 256-entry vector table.** Stock uses VBR = `0x40000000`: the 1 KB *below* the
   image, filled at run time by `movec`. A ColdFire VBR must be 1 MB-aligned (🟡 RM
   knowledge). `0x40000000` and `0x80000000` both qualify.
3. **Zero BSS.** Stock zero-fills its own range at `0x40000518`.
4. **Interrupt controllers** (§2.3).
5. **Caches, if wanted.** Stock's running values are `CACR = 0xA50CE100` and
   `ACR0 = 0x4007E020`: SDRAM `0x40000000..0x47FFFFFF` is copyback-cached and everything else
   is cache-inhibited precise (DDCM = 10). ✅ `FW/ARCHITECTURE.md:389-391`,
   `FW/STEM_REC.md:7399-7404`, `PLACEMENT.md:236`
6. **Ignore the bootstrap-reflash path.** Stock's entry compares NOR's bootstrap version
   (`0x3FFC`) with the copy it carries and **re-flashes the bootstrap** if its own is newer.
   AERIA must carry no such code and must never write NOR below `0x4000`.
   ✅ `FW/BOOT.md:34`, `FW/BOOT.md:46-49`

### 1.5 Memory map

| Range | What | Mark | Source |
|---|---|---|---|
| `0x00000000` | NOR flash, CS0. 8 MB decode. Spansion S29GL-N family (ID `0001/227E`). 16-bit port, 5 wait states (`CSMR0 0x007F0001`, `CSCR0 0x0C8015B0`). Layout in §7. | 🟡 (read from image, not measured) | `FW/ARCHITECTURE.md:78-112` |
| `0x10000000` | CS1, 1 MB decode: **battery-backed SRAM**. Stock keeps its settings, patterns and Parts here. EMPTY RESET clears `0x10000000..0x100FFF00`. | ✅ | `FW/ARCHITECTURE.md:89-90`, `FW/BOOT.md:137-139`, `FW/STORAGE.md:103-105`, `OB/docs/guide/BUILDING.md:318` |
| `0x20000000` | CS2: DSP HI08 host port. The bootstrap sets `CSCR2 0x1180` and stock sets `0x180`. **Out of scope here** (DSP agent). | ✅ | `FW/ARCHITECTURE.md:253-270`, `EMU/periph.h:479-497` |
| `0x40000000..0x47FFFFFF` | **SDRAM, 128 MB**, cached via ACR0 once enabled. The OS image starts at `+0x400`. SDCS0 = `0x4000001B` (256 MB decode). | ✅ | `PLACEMENT.md:235-236`, `FW/CHIP.md:67` |
| `0x48000000..0x4FFFFFFF` | The same SDRAM again, **uncached alias** (`+0x08000000`). | ✅ | `PLACEMENT.md:236`, `EL/boot.c:47` |
| `0x80000000` | **RAMBAR1 on-chip SRAM** (fast; about 2.9 ns per access against 36-83 ns for SDRAM). The bootstrap uses `0x80000200..0x8000050D`. The size is 32 KB per RM knowledge; stock's highest seen address is about `0x80006920`, and one doc searched to `0x8000FFFF`. | ✅ base · 🟡 size | `FW/BOOT.md:111-139`, `FW/KERNEL.md:23-25`, `FW/CHIP.md:146-157`, `FW/STEM_REC.md:7705` |
| `0x90000000` | **On-chip ATA controller** (§5). | ✅ | `FW/ARCHITECTURE.md:209-221`, `EMU/card.h` |
| `0xFC000000..` | On-chip peripherals (fixed MBAR space). | ✅ | `FW/ARCHITECTURE.md:537` |

### 1.6 Peripheral addresses used by stock

| Base | Block | Mark | Source |
|---|---|---|---|
| `0xFC044000` / `0xFC045000` | eDMA control (`+0x18` SERQ, `+0x19` CERQ, `+0x1C` CINT, `+0x1E` SSRT, `+0x1F` CDNE) / TCDs, 32 B per channel | ✅ | `EMU/periph.h:381-424`, `FW/STEM_REC.md:6400-6416` |
| `0xFC048000` | INTC0 (vectors 64 + source) | ✅ | `FW/KERNEL.md:84-87` |
| `0xFC04C000` | INTC1 (vectors 128 + source) | ✅ | same |
| `0xFC05C000` | DSPI (RTC on PCS2) | ✅ | `EMU/periph.h:306-345` |
| `0xFC060000` | UART0: MIDI IN/OUT | ✅ | `FW/MIDI.md:12`, `FW/MIDI.md:383-387` |
| `0xFC064000` | UART1: **front-panel link** | ✅ | `FW/PANEL.md:153`, `FW/KERNEL.md:96` |
| `0xFC068000` | UART2: "serial block", RX only. Purpose unknown; MKI crossfader is likely (§3.6). | ✅ exists · 🟡 purpose | `FW/KERNEL.md:97` |
| `0xFC070000 + 0x4000·n` | DTIM0..3 | ✅ | `FW/PANEL.md:385-397`, `EMU/periph.h:69-102` |
| `0xFC080000` / `0xFC084000` | PIT0 / PIT1 | ✅ | `FW/KERNEL.md:129-132` |
| `0xFC0A0000` | RCR. Writing `0x80` = **soft reset**: it restarts the ColdFire and the bootstrap, keeps SDRAM, and does **not** reset the DSP. | ✅ | `EL/boot.s:57`, `EL/boot.s:241-242`, `FW/ARCHITECTURE.md:172-186` |
| `0xFC0A4000..` | GPIO: `0xFC0A400C` DSP core select; `0xFC0A403A` / `0xFC0A4052` MKI/MKII probe (§3.7) | ✅ | `FW/ARCHITECTURE.md:254`, `FW/PANEL.md:247-251` |
| `0xFC0B0000` | USB OTG (Chipidea/ARC) device controller | ✅ | `EMU/usb.h:1-60` |
| `0xFC0BC000` | Unknown block. INTC1 source 49's handler counts and acks `0xFC0BC008`. | ❓ | `FW/KERNEL.md:104` |
| `0xFC0C4000` | PLL PCR | ✅ | `FW/CHIP.md:75` |
| FlexBus chip-select registers (`CSARn/CSMRn/CSCRn`) | The values are known (above); the register addresses are not in the sources. The bootstrap sets them, so AERIA need not. | ❓ addresses | – |

---

## 2. Timers, interrupt controller, and how stock gets its tick

### 2.1 DMA timers (DTIM): use these for AERIA's tick

Their input clock is pinned down: DTRR = 132,000,000 gives exactly 1 s.
✅ `EMU/periph.h:69-102`, `FW/PANEL.md:385-397`

**Registers** (block `0xFC070000 + 0x4000·n`, interrupt INTC0 source 32 + n, vector `0x60 + n`):

| Offset | Register |
|---|---|
| `+0` | DTMR (16 bit): RST bit 0, CLK bits 2:1 (01 = bus/1, 10 = bus/16, 11 = DTIN pin), FRR bit 3, ORRI bit 4, PS bits 15:8 |
| `+2` | DTXMR (8 bit) |
| `+3` | DTER (8 bit, write 1 to clear): CAP = 1, REF = 2 |
| `+4` | DTRR |
| `+8` | DTCR |
| `+0xC` | DTCN (any write clears it) |

**What stock programs:**

| Timer | Settings | Use |
|---|---|---|
| DTIM0 | DTMR `7` (DTIN0 pin) | MIDI clock timestamping. The pin's source is unknown (❓); the emulator infers 256·fs. |
| **DTIM1** | DTMR `0x1D` (bus/16, restart, ref interrupt), DTRR 68750 → **8.333 ms = 120 Hz** | Stock's UI/LED/key tick. The handler acks by writing `2` to DTER and posts a 60 Hz UI/sys message every second tick. |
| DTIM2 | DTMR `0x13`, DTRR 132,000,000 | 1 s soft-timer and MIDI-out pacing |
| DTIM3 | DTMR `0x0B` (bus/1, no interrupt) | Free-running **132 MHz timestamp**. DTCN3 = `0xFC07C00C`. | 

DTCN3 address: `MW/docs/OCTATRACK_HARDWARE_TRAPS.md:270`.

### 2.2 PIT (stock's scheduler time slice)

✅ `FW/KERNEL.md:27-34`, `FW/KERNEL.md:129-132`, `EMU/periph.h:26-36`

- **Registers:** PCSR `+0` (EN 1, RLD 2, PIF 4 (w1c), PIE 8, OVW 16, prescaler bits 11:8), PMR `+2`, PCNTR `+4`.
- **PIT0** `0xFC080000`, INTC1 source 43 (vector `0xAB`). Stock sets PCSR `0x0B36` at init and `0x0B3F` on every switch (prescaler 2¹¹), PMR 643.
- **Rate ambiguity (🟡):** that is 5 ms if the PIT counts 264 MHz, or **10 ms if it counts the 132 MHz bus**. The RM says bus; the emulators assume 264 MHz. This has not been measured on a unit (`FW/CHIP.md:94-100`). Use DTIM if the period matters.
- **PIT1** `0xFC084000`, INTC1 source 44: stock's storage delay timer (PCSR `0x0B3A`, PMR 2014).
- **Forced reschedule:** stock triggers one by setting INTFRCH bit 11 of INTC1 (`0xFC04C010 |= 0x800`), which forces source 43.

### 2.3 Interrupt controllers

✅ `FW/KERNEL.md:82-132`, `EMU/periph.h:591-653`

**Register layout** (both controllers):

| Offset | Register |
|---|---|
| `+0x00` / `+0x04` | IPRH / IPRL |
| `+0x08` / `+0x0C` | IMRH / IMRL. Bit 0 of IMRL = MASKALL. |
| `+0x10` / `+0x14` | INTFRCH / INTFRCL |
| `+0x1C` (byte) | SIMR |
| `+0x1D` (byte) | CIMR. Value = source number; `0x40` = all. |
| `+0x40 + n` (byte) | ICRn = the source's level |

**How to unmask a source:** stock never writes IMRH/IMRL. It sets ICRn and writes the source
number to CIMR. 🟡 A CIMR write must also clear IMRL's MASKALL bit.

**Rules the emulator relies on:**
- A *forced* request (INTFRC) ignores the mask, but a source whose ICR is 0 is never
  delivered. ✅ `EMU/periph.h:615-622`
- Vector = `vectorBase + source`.

**Sources AERIA cares about:**

| Controller.source | Vector | Device | Stock level |
|---|---|---|---|
| INTC0.26 | `0x5A` | UART0 (MIDI) | 6 |
| INTC0.27 | `0x5B` | UART1 (panel) | 6 |
| INTC0.28 | `0x5C` | UART2 | 4 |
| INTC0.32..35 | `0x60..0x63` | DTIM0..3 | – |
| INTC0.8+n | – | eDMA channel n | – |
| INTC0.1 | `0x41` | DSP frame | 5 (DSP agent) |
| INTC1.43 / 44 | `0xAB` / `0xAC` | PIT0 / PIT1 | 1 / 2 |
| INTC1.47 | `0xAF` | USB OTG | 4 |
| INTC1.54 | `0xB6` | ATA | 5 |

---

## 3. The front panel

The panel is a separate microcontroller. It connects to the CPU over **UART1 at
`0xFC064000`, 312,500 baud**, full duplex, raw bytes with no framing layer.
✅ `FW/PANEL.md:151-156`, `FW/PANEL.md:420-460`

### 3.1 UART registers

The emulator models only what the firmware uses. ✅ `EMU/periph.h:172-182`, `EMU/periph.cpp:248-288`

| Offset | Register |
|---|---|
| `+0x04` | Read: USR. Bit 0 RXRDY, bit 2 TXRDY, bit 3 TXEMP. |
| `+0x0C` | Read: RX byte. Write: TX byte. |
| `+0x14` | Write: UIMR. Bit 0 = TX-ready interrupt, bit 1 = RX interrupt (3 = both, 2 = RX only). |

- Mode, baud and command registers (`+0x00`, `+0x08`, `+0x18`/`+0x1C`): ❓ not modelled or
  documented. Per RM knowledge (🟡) they are UMR1/2, UCR, UBG1/UBG2.
- **Baud divider** = (264 MHz ÷ 2) ÷ (32 × baud), computed by stock's UART init. ✅ `FW/CHIP.md:79-82`
  - 31,250 baud → exactly 132.
  - 312,500 baud → 13.2, so the divider is presumably 13, which gives about 317 kbaud (+1.5%). 🟡 my arithmetic; not read from the image.
- **Practical route for a minimal OS (🟡):** the bootstrap has already brought UART1 up and used
  it polled. Reuse it as left and poll `+0x04` / `+0x0C`, exactly as Modwerk's reset code
  does (`EL/boot.s:316-324`: wait for TXEMP, write UTB). Only reprogram it once the mode and
  divider registers are confirmed.

### 3.2 CPU → panel: message formats

Each message's **first byte decides its length**. There is no length field and no checksum.
Stock sends each message atomically (under a mutex), so messages never interleave.
✅ `FW/PANEL.md:456-472`, decoder `OB/tools/panel/panel_link.py`

| First byte | Length | Meaning |
|---|---|---|
| `0x10..0x17` | 10 | **LCD block**: `0x10\|page`, `col`, then 8 bytes (§3.3) |
| `0x20..0x2F` | 2 | **LED bitmap row** `r = op & 0xF`, then one mask byte: bit n = LED id `r·8 + n` on |
| `0xA0..0xAF` | 2 | LED bitmap row `16 + (op & 0xF)`. There are 17 rows = 136 LED ids. |
| `0x30..0x3F` | 2 | **LED level**: `0x30 \| level` (0..15), then the LED id. Stock's init sends level 15 to ids `0x00..0x57`. |
| `0x40..0x4F` | 1 | One-byte command. Stock sends `0x43` at boot on an **MKI** (hello / report request). |
| `0x60 xx` | 2 | MKII panel-loader control: `60 02` = enter loader, `60 00` = leave loader |
| `0x70 00` | 2 | MKII: loader version query, valid inside `60 02` |
| `0x74 00` | 2 | MKII: ask for the `0x7n` identity report |
| `0xB5` | 6 | MKII **LED colour/palette entry**: 16-bit index, then three bytes r, g, b (§3.5) |
| `0xB7` | 2 | **LCD backlight** 0..255 |

Anything else is unknown. Resync is by first byte only.

### 3.3 The LCD: 128 × 64, 1 bit per pixel

✅ Decoded from the stock UART1 stream under the emulator, and drawn correctly for every stock
screen tested. `FW/PANEL.md:503-531`

- **Layout.** The screen is eight horizontal pages, each 8 pixels tall.
- **One message.** `0x10 | p`, `c`, `b0..b7`.
  - `p` = page 0..7. **Page 7 is the top of the screen**: page p covers screen rows
    `8·(7−p) .. 8·(7−p)+7`.
  - `c` = column start, one of **0, 8, 16, …, 120** only. Other values are treated as junk.
  - `b_k` = the vertical 8-pixel column at x = c + k. **Bit 0 = the top row of the band.**
- **Pixel formula:** `pixel(x, y) = (frame[(7 − y/8)·128 + x] >> (y & 7)) & 1`.
- **Full frame:** 8 pages × 16 blocks × 10 B = 1,280 B. At 312.5 kbaud (about 31 kB/s at 10
  bits per byte) that is about 41 ms, so at most about 24 full frames per second. Stock sends
  only blocks that changed since the last frame, and a quiet screen sends nothing.
  🟡 arithmetic; diffing ✅ `FW/PANEL.md:534-575`
- **No scroll or start-line command** exists in the table. Stock gets grey by dithering only;
  the display is effectively monochrome. ✅ `FW/PANEL.md:115-149`, `FW/PANEL.md:525-528`
- **The panel has a second, text-mode protocol** (its own font, "page/cell cursor"), which the
  bootstrap uses to show the 10-character ELEK name. It is not decoded. ❓
  `MW/docs/STARTUP_ANIMATION.md:283-330`

### 3.4 LEDs

✅ `OB/tools/panel/KEYMAP.md:103-147`, `OB/tools/panel/key_map.json` "leds", `EL/boot_anim.c:15-19`, `EL/boot_anim.c:309-342`

**How ids work.** LED id = `row·8 + bit`. Each physical lamp is **two ids, 2i (channel A) and
2i+1 (channel B)**. On an MKI these are two fixed colours. On an **MKII each id is RGB**,
coloured by `0xB5` palette entries. The MKII has **67 lamps**; the MKI has 44. Each id is lit
by its bit in the row bitmap and dimmed by its level (`0x3n id`).

**Measured map (row.bit of the even/A id):**

| Lamp | row.bit |
|---|---|
| Trig n (1..16) | `(n−1)/4 . 2·((n−1)%4)` |
| Track n (1..8) | `5 + (n−1)/4 . 2·((n−1)%4)` |
| STOP | 11.0 |
| PLAY | 11.3 |
| REC | 10.4 |
| Scene A / B | 4.0 / 4.2 |
| TEMPO | 4.6 (🟡) |
| Pages: PLAYBACK / AMP / LFO / FX1 / FX2 | 8.2 / 8.4 / 10.6 / 10.0 / 10.2 |
| MIDI | 8.6 |
| MIXER | 12.6 |
| Arranger | 8.0 |
| PROJECT / PART menu (MKII) | 15.0 / 15.2 |

Not found: a FUNC LED. The CF-activity LED is not on this link. Rows 7 and 16 are unexplained.

**Stock's state right after boot:** rows 4=05, 5=a9, 6=aa, 7=ff, 8=04, 9=01, 11=01, 16=0f.
Levels are 15 for most ids. ✅ `KEYMAP.md:120-122`

**Physical lamp positions** in 0.01 mm sit in a stock table, used by Modwerk's animation. That
is Elektron data, so it is not reproduced here. 🟡

### 3.5 MKII colours

The MKII's colours come from `0xB5` messages. Stock sends 204 of them at boot via its "LED
scheme" routine. Modwerk calls `palette(id, register 0..2, r, g, b)` and turns lamps white with
`palette(2i, 1, L, L, L)`. ✅ `FW/PANEL.md:317-321`, `EL/boot_anim.c:63-64`, `EL/boot_anim.c:323-327`

The exact 16-bit index formula ("2·a+b"), the r/g/b value range, and the panel's default
palette after power-on are **❓**. Until they are known, a lamp may show in whatever colour
the panel defaults to.

### 3.6 Panel → CPU: report formats

✅ `FW/PANEL.md:151-163`, `FW/PANEL.md:592-617`, `KEYMAP.md:44-101`

The first byte's high nibble is the class; the low nibble is the row or encoder.

| First byte | Payload | Meaning |
|---|---|---|
| `0x2r` (r = 0..7) | 1 byte | **Key row r**, bitmask, set = held. Key code = `r·8 + bit`. Stock XORs against the last mask to find edges. Row 7 (`0x27`) is the encoder push switches. |
| `0x3n` (n = 0..6) | 1 byte | **Encoder n**, signed detent delta. A..F = 0..5, **LEVEL = 6**. |
| `0x40` | 1 byte | **Crossfader**, raw ADC 0..255. Stock scales it by a NOR calibration record (§7.3) to 0..127. Rows `0x41..0x4F` are ignored. |
| `0x7n` | 9 bytes | MKII identity report, the answer to `74 00`. Byte 1 = UI version minor, byte 3 ≠ 0 = "UI tested", byte 4 == 22 sets a flag. |

**Key codes** (code = row·8 + bit). ✅ `FW/PANEL.md:220-234`, `FW/PANEL.md:299-308`, `key_map.json`

| Codes | Key |
|---|---|
| `0x00..0x0F` | TRIG 1..16 (rows 0x20, 0x21) |
| `0x10..0x17` | T1..T8 (row 0x22) |
| `0x18` | TEMPO |
| `0x19` / `0x1A` | SCENE A / B |
| `0x1B` | PAGE (MKI: SCALE) |
| `0x1C` / `0x1D` / `0x1E` / `0x1F` | PROJ / PART / AED / ARR (MKII only) |
| `0x20` / `0x21` | DOWN / RIGHT |
| `0x22..0x26` | SRC(PLAYBACK) / AMP / LFO / FX1 / FX2 |
| `0x27` | STOP |
| `0x28` | PLAY |
| `0x29` | REC |
| `0x2A` | CUE |
| `0x2B` / `0x2C` | REC AB / REC CD (🟡) |
| `0x2D` | FUNC |
| `0x2E` / `0x2F` | PATTERN / BANK |
| `0x30` | MIXER |
| `0x31` / `0x32` | YES / NO |
| `0x33` / `0x34` | UP / LEFT |
| `0x35` | MIDI |
| `0x36` | REC3 (MKII, 🟡) |
| `0x38..0x3D` | Encoder A..F push |
| `0x3E` | LEVEL push |

**Level knob.** The LEVEL control is the endless encoder 6 (`0x36 <delta>`) plus its push
(code `0x3E`). The analog MAIN volume pot does not appear in the protocol. ❓ (it is not in
any source)

**Physical layout** of the encoders: on the MIXER page, the top row left to right is A B C and
the bottom row is D E F. ✅ `KEYMAP.md:101`

**Crossfader on MKI.** On an MKI, stock installs a crossfader poll at 31,250 baud on a separate
UART (MKI only), with the same scaling handler. I infer that is UART2 `0xFC068000`. 🟡
`FW/PANEL.md:254-255`, `FW/KERNEL.md:97`

### 3.7 MKII start-up exchange and model detection

**What stock does at start-up:**

1. **Model probe.** Ten times: drive GPIO bit 5 high (write `0x20` to `0xFC0A403A`), read bit
   6, then drive bit 5 low (write `0xDF` to `0xFC0A4052`) and read bit 6 again. If bit 6
   follows bit 5, it is an MKII. Which register bit 6 is read from is my reading of the
   emulator's override. ✅ / 🟡 `FW/PANEL.md:247-258`, `EMU/main.cpp:1568-1587`
2. **MKII loader handshake** (interrupts off, polled):
   1. Send `60 02 70 00`.
   2. Read 5 bytes. A reply of `70 05 v …`, where v equals the panel-firmware version the OS
      carries (8 in 1.40C), means "current".
   3. Send `60 00` (leave the loader).
   4. Any other v makes stock **re-flash the panel MCU** (erase `80 42`, then `cmd addr32`
      writes). With nothing answering, stock spins forever in the read.

   ✅ `FW/PANEL.md:260-272`
3. **Report request.** Send `74 00` (MKII) or `0x43` (MKI). The panel answers `0x70` plus 9
   bytes. Then come the LED level init and the LCD blocks. ✅ `FW/PANEL.md:274-280`, `FW/PANEL.md:560-564`

**What the bootstrap does before that** (🟡, my reading of `FW/BOOT.md:113-128`):
- It sends `60 00` and then reads tagged pairs until `0x27`.
- Tag `0x25`'s byte, with FUNC = `0x20`, opens the menu. Menu items are selected by the TRIG
  bits of row `0x20`.
- So `60 00` seems to make the panel dump its key rows `0x2r` in order.
- Modwerk sends `60 02` before a soft reset to put the MKII panel "back to its start-up state".
  ✅ `EL/boot.s:220-228`

**AERIA safety rule:** never send the loader erase/write commands. A broken panel MCU would
also break the FUNC Startup-Menu recovery. 🟡

---

## 4. MIDI DIN

✅ `FW/MIDI.md:12`, `FW/MIDI.md:383-395`, `FW/MIDI.md:703-713`, `FW/KERNEL.md:95`

- **UART0 at `0xFC060000`** carries **MIDI IN** (RX interrupt INTC0.26, vector `0x5A`, level 6).
- It also carries **MIDI OUT**: stock's CC drainer feeds the UART0 transmitter from a ring
  through the same ISR.
- **31,250 baud**, divider 132 from the 132 MHz bus.
- Register usage is the same as UART1 (§3.1).
- MIDI THRU is presumably hardware. ❓
- Stock timestamps `0xF8` clock bytes from DTIM0's counter. ✅ `FW/MIDI.md:388-391`

---

## 5. CompactFlash (on-chip ATA controller at `0x90000000`)

### 5.1 Registers and commands

✅ `FW/ARCHITECTURE.md:209-221`, `EMU/card.h:11-31`, `EMU/card.cpp`, `FW/STEM_REC.md:2676-2690`, `FW/STEM_REC.md:6390-6440`

**Task-file registers** (byte offsets from `0x90000000`, 16-bit data):

| Offset | Register |
|---|---|
| `+0xA0` | DATA |
| `+0xA4` | FEATURES / ERROR |
| `+0xA8` | SECTOR COUNT |
| `+0xAC` / `+0xB0` / `+0xB4` | LBA 0..23 |
| `+0xB8` | DEVICE (`\|0xE0` = LBA; LBA 24..27 in the low nibble) |
| `+0xBC` | COMMAND / STATUS (BSY `0x80`, DRDY `0x40`, DSC `0x10`, DRQ `0x08`, ERR `0x01`) |
| `+0xD8` | ALT STATUS / device control |

**Controller registers:**
- `+0x18` = the ATA FIFO data port (eDMA target).
- `+0x24` = the ATA control register. Stock writes `0` there around a 25 µs delay in its card
  reset path, and writes it to put the interface into DMA mode.
- Timing and interrupt registers below `0xA0`: ❓. They are not in the sources. They are
  presumably programmed by stock's ATA init (`0x400160F8`), so **AERIA must program them
  itself**.

**Commands stock uses:** `0x20` READ SECTORS, `0x30` WRITE SECTORS, `0xC8` READ DMA, `0xCA`
WRITE DMA, `0xE0` STANDBY, and IDENTIFY. The driver picks PIO or DMA from IDENTIFY.

### 5.2 PIO

- One interrupt per sector: INTC1.54, vector `0xB6`, level 5.
- **INTRQ is cleared by reading STATUS** (not ALT STATUS).
- Stock halts the CPU if BSY is still set in the ISR.
- The emulator puts the first byte of each 16-bit DATA word in the high half (big-endian).
  Real byte-lane order is 🟡: check it against a known sector (MBR `55 AA`).
- **Count byte trap:** a count of 0 in the task file means 256 sectors (the ATA rule).
  Getting this wrong once wiped a card under emulation. ✅ `EMU/card.cpp:84-90`

### 5.3 DMA

| Direction | eDMA channel | Setup |
|---|---|---|
| Write | 15 | TCD at `0xFC0451E0`; destination `0x90000018`; 32 B per request × 16 requests = one 512 B sector; channel left enabled. One ATA interrupt ends the command. |
| Read | 14 | Completion interrupt INTC0.22 (vector `0x56`, level 3). Sectors done = (TCD14 DADDR `0xFC0451D0` − buffer) >> 9. |

Stock's DMA buffers are in the **uncached alias**.

🟡 Correction: the "ATA host control in MBAR `0xFC0451xx`" in `ARCHITECTURE.md` is in fact
eDMA TCD 14/15 (`0xFC045000 + 32·ch`).

### 5.4 Filesystem

FAT16/32. Stock's own driver is not reusable. 🟡 Use FatFs or similar on top of the sector
layer above.

---

## 6. USB

✅ `EMU/usb.h:1-60`, `OB/tools/emu/README.md:283-343`, `EL/boot.s:37`, `EL/boot.s:230-234`

**Controller.** MCF5445x **USB OTG = Chipidea/ARC device controller** at `0xFC0B0000`, with an
**external ULPI high-speed PHY** (selected by the bootstrap). Interrupt INTC1.47, vector
`0xAF`, level 4.

**Registers modelled** (offset from `0xFC0B0000`):

| Offset | Register |
|---|---|
| `0x140` | USBCMD (bit 0 RS = run / D+ pull-up) |
| `0x144` | USBSTS (UI 1, PCI 4, URI 0x40, SRI 0x80) |
| `0x148` | USBINTR |
| `0x154` | DEVICEADDR |
| `0x158` | EPLISTADDR |
| `0x184` | PORTSC1 |
| `0x1A4` | OTGSC (BSV 0x800, BSVIS 0x80000, BSVIE 0x8000000) |
| `0x1A8` | USBMODE |
| `0x1AC` | EPSETUPSR |
| `0x1B0` | EPPRIME |
| `0x1B4` | EPFLUSH |
| `0x1B8` | EPSR |
| `0x1BC` | EPCOMPLETE |
| `0x1C0..` | EPCTRL0..3 |

**Data structures.** Queue heads and transfer descriptors are in guest memory, big-endian:
- dTD: `+0` next, `+4` token, `+8..+0x18` buffer pages.
- dQH: `+8` next-dTD, `+0x28` the SETUP buffer.

**What stock does:**
- `USBMODE = 0x0E` (device, big-endian), EP list at an SDRAM address, `USBINTR = 0x57`.
- It brings the controller up only once OTGSC reports a B-session (cable present).
- It enumerates as VID `0x1935` PID `0x0002` (MSC).

**Before a soft reset**, clear USBCMD.RS and wait about 50 ms. Without that, about one RAM
boot in six came back without USB. ✅ `MW/docs/OCTATRACK_HARDWARE_TRAPS.md:155-159`

**Other sources.** Modwerk's elekloader has a full working device stack: `EL/ep0.c`, plus the
octemu bench protocol for testing. Read them for behaviour, but beware their stock-hook
dependencies. 🟡

**Not needed for a minimal OS.**

---

## 7. Image formats

### 7.1 RAM boot via Modwerk `npm run device -- boot DIR` (no flashing)

**Flow** (✅ `MW/scripts/device.mjs:677-706`, `EL/boot.c:88-126`, `EL/boot.s:67-180`):

1. The host sends `DIR/MAIN.raw` (and reads `DIR/proofs.json`'s `configurationHash`) to the
   **running Modwerk base** over USB.
2. The base stages the image in its `.bss` (inside the reserve `0x40A955E0..0x41495DE0`) and
   writes a mailbox there: `"MWBT"`, length, x33 hash, check = magic^len^hash.
3. About 0.5 s later it quiesces, parks both DSP cores, puts the MKII panel in `60 02` state,
   stops USB, and **soft-resets** (RCR = `0x80`).
4. The bootstrap reruns and unpacks the **flashed** base.
5. That base's **gate** (patched over the entry's `movea.l #0x48000000,%sp` at `0x40000412`)
   scans the reserve. If there is exactly one valid mailbox, it clears it (one shot) and
   checks the image.
6. It copies a stub out, then copies the image to `0x40000400` with **caches off**.
7. It sets `CACR = 0x0008C000` (as the bootstrap leaves it), pushes the bootstrap's argument,
   and `jsr 0x40000400`.

**What `MAIN.raw` must satisfy:**

| # | Check | Where checked |
|---|---|---|
| 1 | A flat binary **linked at `0x40000400`**: file offset 0 = `0x40000400`. | – |
| 2 | **First 32-bit word = `0x4FEFFFE4`**, the encoding of `lea -28(%sp),%sp`. | `device.mjs`, `boot.c`, `boot.s` gate. Start AERIA's entry with that harmless instruction. |
| 3 | **Length ≤ `0x140000` (1,310,720 B)** and **≥ `0xDE64A` (909,898 B)**. Pad with zeros. | – |
| 4 | **The big-endian u16 at file offset `0xDE648` (= `0x400DEA48`) equals NOR's bootstrap version at `0x3FFC`** (`0x0408` on units with 1.40C's bootstrap). | Otherwise the gate refuses with "BVER". This exists because a stock entry re-flashes the bootstrap when its copy is newer; AERIA has no such code but must still carry the word. |
| 5 | The x33 hash must match: the gate refuses ("HASH") if the stage changed. | Computed by the base, not by you. |

**Results and recovery:**
- On refusal, the gate records `SIZE` / `FIRS` / `BVER` / `HASH` in the mailbox status and
  boots the flashed base. It **never hangs** at that point.
- A power cycle always returns to the flashed base.
- `device.mjs` will report that the unit "did not come back", because AERIA does not answer
  Modwerk's USB HELLO. That is expected.
- `--chunk 4096` is needed on macOS (`HARDWARE_TRAPS.md:79-85`).
- Unplug other Elektron USB devices first (`OCTATRACK_DRIVING.md:58`).

### 7.2 Flashed OS container: ELEK in `.bin` (card OS UPGRADE) or `.syx` (Startup Menu MIDI UPGRADE)

✅ `MW/vendor/elekloader/kit/src/elek.ts` (whole file), `MW/sdk/firmware_codec/{aplib,elz}.py`, `FW/ARCHITECTURE.md:33-76`, `FW/ARCHITECTURE.md:104-121`

**The ELEK container** is what gets programmed to **NOR `0x4000`**:

| Offset | Size | Field |
|---|---|---|
| `0x00` | 4 | `"ELEK"` |
| `0x04` | 4 | ASCII build/version code. 🟡 I infer this is the 4-digit code OS UPGRADE compares; stock is `0178`-class. |
| `0x08` | 10 | ASCII **name**, right-justified, space-padded. The panel shows it at boot in its own font, which is **upper-case only**: lower-case codes select symbols (`MW/docs/STARTUP_ANIMATION.md:283-290`). |
| `0x12` | 4 | Section: u32 BE stream length n |
| `0x16` | 4 | u32 BE byte-sum of the stream. The depacker does not check it; OS UPGRADE may. |
| `0x1A` | n | Packed stream |
| after | – | Zero pad to an even length |

- The bootstrap depacks from NOR `0x4012`, skipping the 8-byte length/sum header, into
  `0x40000400`. **It checks nothing.** ✅ `MW/docs/OCTATRACK_HARDWARE_TRAPS.md:178-182`
- **The packed stream** (Elektron's LZ, "aPLib-like", `elz.py`): control bits come
  MSB-first from tag bytes. A 1 bit = literal byte. A 0 bit = match: an Elias-gamma offset
  (gamma 2 = reuse the last offset; otherwise `(g<<8)+byte − 767`, where a coded value of
  exactly 767 ends the stream) and a gamma length (offset > 3328 adds 1). Pack with
  `firmware_codec/aplib.py pack_section()` (Modwerk's GPL code) or reimplement from `elz.py`.
- **Size limits:**
  - The container fits the OS region `0x4000..0x1FFFFF` (2,080,768 B).
  - Stock OS UPGRADE **refuses a `.bin` over 1 MB + 12 B** (`-3`).
  - Keep clear of the 64 KB sector `0x1F0000` that holds the calibration words (§7.3). 🟡

**`.bin` = ELUP card file** (stock OS UPGRADE lists `/*.BIN` on the card root):

1. `"ELUP"`, u32 seed.
2. Ciphered words of `[u32 len] + container`, padded to 4.
3. A ciphered additive checksum word.

The cipher is XOR-feedback with constants `0x9E3B16A2` / `0x764E28CA` and mixers `0x360FA955`
/ `0xEF4A9AB6`. It uses rot16 or bswap depending on bit 23 of the previous ciphertext word.
See `encodeBin()` in `elek.ts`.

**OS UPGRADE's checks** (`0x4007F748`):

| Code | Meaning |
|---|---|
| `-1` | IO error |
| `-2` | Not a valid OS |
| `-3` | Length |
| `-4` | Checksum |
| `-5` | MK1 not allowed: version code < `"0156"` |
| `-6` | Downgrade: version < `"0178"` |

On an MKII the OS-string refusal is ≤ `"0155"`. Stock then programs NOR from `0x4000`,
verifies it, shows "PLEASE REBOOT!" and spins; the user power-cycles. 🟡 `FW/PANEL.md:256-258`
That path needs a running **stock** OS (or a stock-derived one).

**`.syx` (legacy Elektron SysEx transport, device id `0x05`):**
- One data message per 63 container bytes:
  `F0 00 20 3C 05 00 7E <chk hi nib> <chk lo nib> <6 nibbles: 0x4000 + offset> <8-in-7 data> F7`
- Then an empty `… 7E F7`.
- Then the end message `… 7F <6 nibbles length> F7`.
- See `encodeSyx()` / `packetChecksum()` in `elek.ts`.
- The bootstrap's MIDI UPGRADE checks the checksum and the version against the MK1/downgrade
  flags, never NOR (`HARDWARE_TRAPS.md:188-190`). **This is the route for flashing AERIA
  without any stock OS running**, and also the recovery route (§9).

Modwerk's own USB `flash` command writes only a container that depacks to the base already
running (`device.mjs:653-660`), so it **cannot** flash AERIA.

### 7.3 NOR layout outside the OS

🟡 `FW/ARCHITECTURE.md:104-121`, `FW/PANEL.md:162`, `FW/PANEL.md:603`

| Range | Contents |
|---|---|
| `0x0000..0x3FFF` | Bootstrap and Startup Menu, including MIDI UPGRADE. **Never write.** |
| `0x3FFC` | Bootstrap version word |
| `0x4000..0x1FFFFF` | The OS region. Stock's sector table: `0x4000`, `0x6000..0xE000` (8 KB each), then `0x10000..0x1F0000` (64 KB each). |
| `0x1FFFFA..0x1FFFFF` | Crossfader calibration (magic `0x1234`). The sources disagree on which word holds the magic: `0x1FFFFA` per ARCHITECTURE, `0x1FFFFE` per PANEL. A test-mode marker `0xDCBA` is also read at `0x1FFFFE`. |
| `0x200000..` | `"EFGH"` table, purpose unknown |

The part is S29GL-N: unlock at word `0x555`/`0x2AA`, sector erase `0x80`/`0x30`, word program
`0xA0`, polled.

---

## 8. Running a non-Elektron image in `ot_emu`

✅ `EMU/machine.cpp:20-98`, `EMU/machine.cpp:820-918`, `EMU/rtos.cpp:291-394`, `EMU/main.cpp:1565-1740`, `OB/tools/emu/README.md:41-60`, `OB/tools/emu/README.md:230-270`

**Build.** In `~/octa/octabam`: `make emu-cf` (needs `cmake` plus the project's `make setup`)
→ `out/emu/ot_emu`. **It is not built in this checkout** (`out/emu/ot_emu` is absent).
Running `./out/emu/ot_emu` with an unknown argument prints its usage block.

**Command line.** `ot_emu --image AERIA.raw [--max N] [--periph] [--mkii] [--card card.img]`

The emulator does **not** run the bootstrap, so `--image` takes the same flat `MAIN.raw` as §7.1:
- It copies the image to `0x40000400`.
- It sets `SR = 0x2700`, `SP = 0x48000000`, `PC = 0x40000400`.
- No argument is pushed: `4(sp)` reads 0.
- The PLL reads 264 MHz.

**How it runs, and why a custom OS does not work without a patch:**

1. **Phase 1 ("boot"):** the CPU runs until the first fetch of a `trap #0` opcode (`0x4E40`).
   In this phase:
   - Every peripheral read returns **all-ones**, apart from a few overrides. A polled
     UART1 RX therefore sees an endless stream of `0xFF` bytes.
   - Writes are only logged; `--periph` prints the first 60.
   - **No interrupts are delivered.**
   - A loop that stays inside 64 bytes of code for 4 × 500k instructions without memory
     progress stops the run with `FAULT: unrecognised spin`. AERIA's idle or poll loop would
     trigger this.
2. **Phase 2 (`Rtos`):** this has the real PIT/DTIM/INTC/UART/ATA/USB models, `--live`
   panel input, `--serial-out` (UART TX streams) and `--lcd`. But `Rtos::install()`
   **refuses** unless vectors 32 and 171 at VBR `0x40000000` both point to stock's scheduler
   `0x40000550`. It also hard-codes stock addresses: the main idle spin `0x4001FC9C`, the
   current-TCB pointer, and the card/project hooks.
3. **`--lcd FILE`** dumps the *stock* plane at `0x46C7E0EA`, a rotated layout, plus stock
   window tables. **`lcd_view.py`** shows that file.
   - Address-independent alternative: decode the UART1 TX stream with
     `python3 tools/panel/panel_link.py --decode tx.bin --png lcd.png`. The `tx.bin` comes
     from `--serial-out`, which writes `FILE.a` = UART1 and `FILE.b` = UART2. That option
     exists only in phase 2.
   - Workaround hack (🟡): have AERIA also mirror its frame into `0x46C7E0EA` in the
     plane layout (128 rows × 8 bytes, MSB left, pixel (x,y) = column 63−y of row x). Then
     `--lcd` + `lcd_view.py` would show it, but only once phase 2 is reached.

**What it takes in practice (my recommendation, 🟡).** Add a small `--bare` mode to `ot_emu`.
This is octabam's repo, so it is outside this read-only research:
- construct `Rtos` without the kernel-vector check;
- route peripherals to the models from instruction 0;
- deliver interrupts through the guest's own VBR;
- disable the stall fault;
- keep `--serial-out`, `--live` and `--mkii`.

Then panel input works through `--live FIFO` (lines `key <code> down|up`, `enc <n> <delta>`,
`pot <0..255>`), and output through `panel_link.py --decode`. The `--mkii` model answers
`60 02 70 00` with `70 05 v 00 00` and `74 00` with a 10-byte report, where v is read from the
image's long at `0x400D81A4`. **AERIA's image will not have that value**, so make the model
answer a fixed version in bare mode. ✅ `EMU/periph.h:237-304`, `EMU/main.cpp:1780-1791`

**Blind spots** (`MW/docs/OCTATRACK_HARDWARE_TRAPS.md:746-760`, `FW/CHIP.md:157-162`): the
emulator does not model caches, bus timing, reset settling or RAM retention. DMA completes
instantly. It never restarts from RAM.

---

## 9. Recovery if a new OS hangs

| Situation | How to get back | Source |
|---|---|---|
| AERIA was **RAM-booted** (§7.1) | **Power-cycle.** The flashed Modwerk base boots, because the mailbox was spent before the hand-over. | ✅ `EL/boot.s:5-7`, `EL/boot.s:67-73`, `device.mjs:690` |
| AERIA wants to hand back to the base without a power cycle | Clear USBCMD.RS, wait about 50 ms, write `0x80` to RCR `0xFC0A0000`. This works only if AERIA left the DSPs parked as it found them; a stalled DSP hangs the next boot at the logo. After a crash restart, power-cycle before the next RAM boot. | ✅ `HARDWARE_TRAPS.md:155-159`, `HARDWARE_TRAPS.md:323-330` |
| AERIA was **flashed** and hangs or shows garbage | Power off, **hold FUNC**, power on → **STARTUP MENU**. The bootstrap reads the panel *before* unpacking the OS, so no OS code runs. **TRIG 3 = MIDI UPGRADE**: send a good `.syx` (stock 1.40C or a Modwerk/octabam image) over **5-pin DIN MIDI, not USB**. Wait through PREPARING / UPDATING FLASH and do not power off. | ✅ `OB/docs/guide/BUILDING.md:305-319`, `FW/BOOT.md:111-131`, `elek.ts` devices.ts `recovery` |
| Other menu items | TRIG 1 TESTMODE (RAM test). TRIG 2 **EMPTY RESET**, which clears battery SRAM `0x10000000..` (not the card). TRIG 4 SEND UPGRADE. TRIG 5 EXIT boots normally. | ✅ `FW/BOOT.md:123-131`, `BUILDING.md:318` |
| Battery SRAM | **Don't scribble on CS1 (`0x10000000`) unless intended.** Stock keeps its settings and current bank there. If stock misbehaves after AERIA, EMPTY RESET fixes it. | ✅ `FW/STORAGE.md:103-105` |

**What can still brick the unit:**
- writing NOR `0x0000..0x3FFF` (the bootstrap);
- re-flashing the MKII panel MCU (it carries the keys the Startup Menu needs);
- possibly erasing the calibration sector at `0x1F0000` (crossfader only). 🟡

None of these is needed for AERIA.

---

## 10. Minimal OS checklist ("boots, draws, reads keys, lights LEDs")

1. Entry at `0x40000400` begins `lea -28(%sp),%sp` (§7.1). Set SR = `0x2700` and SP. Install
   VBR plus 256 vectors, with a default handler that writes a code to SRAM and spins.
2. Zero BSS. Optionally enable caches with stock's ACR0/CACR values.
3. Panel bring-up, polled on UART1 (`+4` TXEMP/RXRDY, `+0xC` data), **with timeouts**:
   1. Send `60 02 70 00`, read up to 5 bytes, then send `60 00`.
   2. Send `74 00`, read `0x70` plus 9 bytes.
   3. Then `3f id` for every id (level 15), `0x20..0x2F` / `0xA0` rows, `b7 ff` backlight, and
      128 LCD blocks (§3.2-3.4).
4. Poll RX for `0x2r` / `0x3n` / `0x40` reports. Light the TRIG LED for each held TRIG key
   (code n → LED row `n/4`, bit `2·(n%4)`).
5. Tick from DTIM1 (DTMR `0x1D`, DTRR 68750, ack DTER = 2, INTC0 source 33 via ICR33 + CIMR),
   or just poll DTCN3.

---

## 11. Unknowns that block a minimal OS (screen + keys + LEDs)

1. **Whether the MKII panel needs the `60 02 70 00` → `60 00` and `74 00` exchange before it
   accepts LCD/LED messages or sends key reports**, and what state the bootstrap leaves it in.
   The bootstrap sends `60 00`; the rest is 🟡. *Test:* a RAM-booted probe that skips each step
   and records what comes back in SRAM.
2. **UART1's mode and baud registers as the bootstrap leaves them.** Reusing them is assumed
   (🟡). The register layout above `+0x14` and the 312.5k divider (13?) are unverified.
   *Test:* dump `0xFC064000..0xFC06401F` from a running base with `device.mjs mem` (read-only;
   the owner runs it).
3. **MKII LED colours.** The `0xB5` index formula, the RGB range, and the panel's power-on
   default palette are unknown. Without a palette, lamps may be off or the wrong colour even
   with bits and levels set. The level/bit rules themselves are known.
4. **Whether a default LED level exists.** Stock always sends `3f id` at init. Do the same;
   low risk.
5. **SR, VBR and SP at entry.** AERIA overwrites them all, so this does not block.
6. **No emulator path for a non-Elektron image** (§8). Bring-up would go straight to hardware
   unless `ot_emu` gets a bare mode. This is the biggest *tooling* gap.
7. **MKI differences.** `0x43` hello; crossfader likely on UART2; no PROJ/PART/AED/ARR keys;
   fixed two-colour LEDs. Not needed for an MKII-only target.
8. **Not blocking but open:**
   - the panel's text-mode protocol;
   - the PIT clock (264 vs 132 MHz);
   - ATA timing registers;
   - FlexBus CS register addresses;
   - the `0xFC0BC000` block;
   - UART2's role;
   - the SRAM size (32 KB 🟡).
