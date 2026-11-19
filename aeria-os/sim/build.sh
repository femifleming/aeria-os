#!/bin/sh
# aeria_sim: AERIA OS's simulator, linked against Octabam's emulator libraries (built once by `cmake` in
# ~/octa/octabam/out/emu; nothing in that checkout is changed by this).
set -e
cd "$(dirname "$0")"
OB=${OCTABAM:-$HOME/octa/octabam}
E=$OB/out/emu
[ -f "$E/libot_machine.a" ] || { echo "Octabam's emulator is not built: (cd $OB && cmake -B out/emu -S tools/emu/ot_emu -DCMAKE_OSX_ARCHITECTURES=arm64 && cmake --build out/emu -j8)"; exit 1; }
# Octabam's dsp.cpp with one probe added (a copy made here; the checkout is not touched): the slot the output ring's
# word 0 leaves in, frame by frame, which its own capture hides by turning every frame back
mkdir -p .build
python3 - "$OB/tools/emu/ot_emu/dsp.cpp" .build/dsp_aeria.cpp <<'PY'
import sys
s = open(sys.argv[1]).read()
old = "\t\t\t\tconst uint32_t rot = (dsr2 - 9) & 7;\n"
new = old + "\t\t\t\tif(i == 0) { extern uint32_t g_aeriaRot; extern uint64_t g_aeriaRotChanges, g_aeriaRotFrame; if(rot != g_aeriaRot) { ++g_aeriaRotChanges; g_aeriaRotFrame = c.txFrames.load(std::memory_order_relaxed); } g_aeriaRot = rot; }\n"
assert s.count(old) == 1
open(sys.argv[2], 'w').write(s.replace(old, new))
PY
c++ -O3 -DNDEBUG -std=gnu++17 -flto -arch arm64 -DASMJIT_STATIC -DDSP56300_DEBUGGER=0 -w \
  -I"$OB/vendor" -I"$OB/tools/emu/ot_emu" -I"$OB/vendor/dsp56300/source" -I"$OB/vendor/dsp56300/source/asmjit/src" \
  aeria_sim.cpp .build/dsp_aeria.cpp -o aeria_sim \
  "$E/libot_machine.a" "$E/mc68k/lib68kEmu.a" "$E/dsp56300/dsp56kEmu/libdsp56kEmu.a" "$E/dsp56300/dsp56kBase/libdsp56kBase.a" "$E/dsp56300/asmjit/libasmjit.a" -lpthread
echo built: "$(pwd)/aeria_sim"
