#!/bin/sh
# AERIA OS build, in Modwerk's toolchain container (m68k-elf-gcc 16.2)
set -e
cd "$(dirname "$0")"
OUT=${1:-../build}
mkdir -p "$OUT"
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
# the DSP program first (Octabam's vendored dsp_asm), checked by disassembling it back
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$(cd ../dsp && pwd)":/d -w /d modwerk-source-tools sh -c '
  /opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in engine.asm -org 33000 -out slots.bin -list > slots.lst 2>&1 &&
  /opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in core1.asm -org 33800 -out core1.bin -list > core1.lst 2>&1 &&
  /opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in stage1.asm -org 31000 -out stage1.bin -list > stage1.lst 2>&1 &&
  /opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in stage1b.asm -org 32000 -out stage1b.bin -list > stage1b.lst 2>&1 &&
  ! grep -v "^0" slots.lst core1.lst stage1.lst | grep -v ":assembled\|:wrote" '
python3 ../dsp/pmove.py ../dsp/slots.bin
python3 ../dsp/mkinc.py ../dsp/slots.bin dsp_slots.inc 33000 slots
python3 ../dsp/mkinc.py ../dsp/core1.bin dsp_core1.inc 33800 core1
python3 ../dsp/mkinc.py ../dsp/stage1.bin dsp_stage1.inc 31000 stage1
python3 ../dsp/mkinc.py ../dsp/stage1b.bin dsp_stage1b.inc 32000 stage1b
docker run --rm --network none --user "$(id -u):$(id -g)" -e FLASH="${FLASH:-0}" -e XDEF="${XDEF:-}" -v "$(cd ../.. && pwd)":/octa -v "$(cd "$OUT" && pwd)":/o -w /octa/aeria-os/src modwerk-source-tools sh -c '
  F="-mcpu=54455 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -Werror -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-late-combine-instructions"; [ "$FLASH" = 1 ] && F="$F -DAERIA_FLASH"; [ -n "$XDEF" ] && F="$F $XDEF"
  m68k-elf-gcc $F -c start.S -o /o/start.o &&
  m68k-elf-gcc $F -c main.c -o /o/main.o && m68k-elf-gcc $F -Wno-unused -Wno-unused-parameter -Wno-error -I. -I../../cloudfin -c aeria_ui.c -o /o/ui.o &&
  m68k-elf-gcc $F -T link.ld /o/start.o /o/main.o /o/ui.o -o /o/aeria-os.elf &&
  m68k-elf-objcopy -O binary /o/aeria-os.elf /o/aeria-os.bin &&
  m68k-elf-size /o/aeria-os.elf && m68k-elf-objdump -d /o/aeria-os.elf | head -12'
python3 pack.py "$OUT/aeria-os.bin" "$OUT/ram" 1aba734bfcfa1aaf37ed87945bb2a5e993d6ba41619a7c7d0608a64e19da820a
