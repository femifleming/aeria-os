#!/bin/sh
# builds c1trace natively against octabam's dsp56kEmu (macOS arm64)
cd "$(dirname "$0")"
O=$HOME/octa/octabam/out/emu; V=$HOME/octa/octabam/vendor/dsp56300/source
exec c++ -O2 -g -std=gnu++17 -w -arch arm64 -DASMJIT_STATIC -DDSP56300_DEBUGGER=0 -I$V -I$V/asmjit/src c1trace.cpp -o ${OUT:-c1trace} \
  $O/dsp56300/dsp56kEmu/libdsp56kEmu.a $O/dsp56300/dsp56kBase/libdsp56kBase.a $O/dsp56300/asmjit/libasmjit.a -lpthread
