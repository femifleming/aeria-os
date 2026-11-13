// AERIA OS: run our core-0 DSP program in the vendored dsp56300 emulator with BOTH ESAI ports live, and report
// what each TX slot carries -- no hardware, no ears. RX slot k gets a tone at 500*(k+1) Hz so the input side is
// checked too. The ColdFire's command (x:$30: 0-7 / 9 on the ESAI, $10-$17 / $19 on ESAI_1, 8 silence) is
// written straight into X memory, phase by phase.
//   esai_test prog.bin org_hex [cmd ...]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

using namespace dsp56k;

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: esai_test prog.bin org [cmd...]\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    const TWord org = std::strtoul(argv[2], nullptr, 16);
    std::vector<TWord> cmds;
    for (int i = 3; i < argc; ++i) cmds.push_back(std::strtoul(argv[i], nullptr, 16));
    if (cmds.empty()) cmds = {9, 0x19, 0, 2, 0x10, 0x12};

    DefaultMemoryValidator validator;
    Memory mem(validator, 0x080000, 0x800000, 0x200000);
    Peripherals56367 py;
    Peripherals56362 px(&py);
    DSP dsp(mem, &px, &py);

    for (size_t i = 0, a = org; i + 2 < b.size(); i += 3, ++a)
        mem.set(MemArea_P, static_cast<TWord>(a), b[i] | b[i + 1] << 8 | b[i + 2] << 16);

    static uint64_t rxn = 0;
    static double peakX[32], peakY[32];
    static uint64_t txX = 0, txY = 0;
    px.getEsai().setReadRxCallback([](uint64_t&, Audio::RxFrame& fr) {
        fr.resize(Audio::MaxSlotsPerFrame);
        for (uint32_t s = 0; s < Audio::MaxSlotsPerFrame; ++s) for (auto& w : fr[s]) w = 0;
        for (uint32_t s = 0; s < 4; ++s)
            fr[s][0] = static_cast<TWord>(static_cast<int32_t>(std::lrint(0.25 * 8388607.0 * std::sin(2 * M_PI * 500.0 * (s + 1) * rxn / 44100.0)))) & 0xffffff;
        ++rxn;
    });
    px.getEsai().setWriteTxCallback([](uint64_t&, const Audio::TxFrame& fr) {
        ++txX;
        for (uint32_t s = 0; s < fr.size() && s < 32; ++s) {
            double v = std::fabs(dsp2sample<float>(fr[s][0]));
            if (v > peakX[s]) peakX[s] = v;
        }
    });
    py.getEsai().setReadRxCallback([](uint64_t&, Audio::RxFrame& fr) {
        fr.resize(Audio::MaxSlotsPerFrame);
        for (uint32_t s = 0; s < Audio::MaxSlotsPerFrame; ++s) for (auto& w : fr[s]) w = 0;
    });
    py.getEsai().setWriteTxCallback([](uint64_t&, const Audio::TxFrame& fr) {
        ++txY;
        for (uint32_t s = 0; s < fr.size() && s < 32; ++s) {
            double v = std::fabs(dsp2sample<float>(fr[s][0]));
            if (v > peakY[s]) peakY[s] = v;
        }
    });
    px.getEsaiClock().setCyclesPerSample(4532 / 8);

    dsp.setPC(org);
    auto run = [&](uint64_t n) { const uint64_t end = dsp.getInstructionCounter() + n; while (dsp.getInstructionCounter() < end) dsp.exec(); };
    run(2000000);   // set-up and a few thousand frames
    std::printf("after start: PC %06x, TX frames X %llu Y %llu, x:$30=%06x\n", dsp.getPC().toWord(),
                (unsigned long long)txX, (unsigned long long)txY, mem.get(MemArea_X, 0x30));
    for (TWord c : cmds) {
        mem.set(MemArea_X, 0x30, c);
        for (auto& p : peakX) p = 0;
        for (auto& p : peakY) p = 0;
        const uint64_t x0 = txX, y0 = txY;
        run(4532ull * 2000);   // ~2000 samples
        std::printf("cmd %02x: frames X %llu Y %llu | ESAI peaks:", c, (unsigned long long)(txX - x0), (unsigned long long)(txY - y0));
        for (int s = 0; s < 8; ++s) std::printf(" %5.3f", peakX[s]);
        std::printf(" | ESAI_1:");
        for (int s = 0; s < 8; ++s) std::printf(" %5.3f", peakY[s]);
        std::printf(" | RX peaks x:$38-3b %06x %06x %06x %06x\n", mem.get(MemArea_X, 0x38), mem.get(MemArea_X, 0x39), mem.get(MemArea_X, 0x3a), mem.get(MemArea_X, 0x3b));
    }
    return 0;
}
