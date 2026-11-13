// core 1's engine alone in the emulator: ROSE in slot 5, a tone posted as requests, the output and counters read back
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
using namespace dsp56k;
class AllowAll final : public IMemoryValidator { public: bool memValidateAccess(EMemArea, TWord, bool) const override { return true; } };
static void load(Memory& m, const char* f, TWord org) { std::ifstream i(f, std::ios::binary); std::vector<unsigned char> b((std::istreambuf_iterator<char>(i)), {}); for (size_t k = 0, a = org; k + 2 < b.size(); k += 3, ++a) m.set(MemArea_P, (TWord)a, b[k] | b[k+1] << 8 | b[k+2] << 16); }
int main() {
    AllowAll v; Memory mem(v, 0x080000, 0x800000, 0x200000); Peripherals56367 py; Peripherals56362 px(&py); DSP dsp(mem, &px, &py);
    load(mem, "core1.bin", 0x33800); load(mem, "fx_rose.bin", 0x37000);
    dsp.setPC(0x33800);
    for (int i = 0; i < 20000; ++i) { dsp.exec(); if (px.getHDI08().hasTX()) px.getHDI08().readTX(); }
    std::printf("after start: PC %06x alive %06x\n", dsp.getPC().toWord(), mem.get(MemArea_X, 0x3f000));
    const TWord e = 0x3f010 + 16 * 5, kb = 0x3f200 + 32 * 5, in = 0x3f400 + 64 * 5, out = 0x3f420 + 64 * 5;
    int kn[6] = {24, 4, 5, 80, 64, 64}; for (int k = 0; k < 6; ++k) mem.set(MemArea_X, kb + k, kn[k] << 16);
    mem.set(MemArea_X, kb + 0xc, 0x5a0000);
    TWord ent[12] = {0, 0x37002, 0x37000, kb, 0x1000 + 0x100 * 5, 0, 1, 0, 0, in, out, 0x1905};
    for (int k = 1; k < 12; ++k) mem.set(MemArea_X, e + k, ent[k]); mem.set(MemArea_X, e, 1);
    double pk = 0; int frames = 0;
    for (int f = 0; f < 400; ++f) {
        for (int s = 0; s < 32; ++s) mem.set(MemArea_X, in + s, (TWord)((int)std::lrint(0.25 * std::sin(2 * M_PI * 441.0 * (f * 16 + s / 2) / 44100.0) * 8388607.0)) & 0xffffff);
        mem.set(MemArea_X, e + 7, (TWord)(f + 1));
        for (int i = 0; i < 30000 && mem.get(MemArea_X, e + 8) != (TWord)(f + 1); ++i) { dsp.exec(); if (px.getHDI08().hasTX()) px.getHDI08().readTX(); }
        if (mem.get(MemArea_X, e + 8) == (TWord)(f + 1)) frames++;
        if (f > 200) for (int s = 0; s < 32; ++s) { int32_t x = (int32_t)(mem.get(MemArea_X, out + s) << 8) >> 8; pk = std::fmax(pk, std::fabs(x / 8388608.0)); }
    }
    std::printf("frames done %d of 400, output peak %.3f, PC %06x, alive %06x\n", frames, pk, dsp.getPC().toWord(), mem.get(MemArea_X, 0x3f000));
}
