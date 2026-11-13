// AERIA OS engine in the dsp56300 emulator: tones into A (441 Hz) and B (882 Hz), every TX slot recorded, then
// main L is checked against 0.5*A at the best latency: worst error, and whether the latency holds (no dropouts).
//   engine_test engine.bin 33000
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include <string>
using namespace dsp56k;
class AllowAll final : public IMemoryValidator { public: bool memValidateAccess(EMemArea, TWord, bool) const override { return true; } };
static std::vector<double> tx[8];
static uint64_t rxn = 0;
static bool streaming = false, recording = false;
static int routeLater = -1; static TWord routeBlk = 0;
static std::vector<double> recA, recB, recC, recD, recL, recR; static int hdr = 0, mtr6 = 0;
static uint64_t sw = 0;   // stereo frames written
static double S(uint64_t n) { return 0.25 * std::sin(2 * M_PI * 1000.0 * n / 44100.0); }
static double A(uint64_t n) { return 0.25 * std::sin(2 * M_PI * 441.0 * n / 44100.0); }
static double B(uint64_t n) { return 0.25 * std::sin(2 * M_PI * 882.0 * n / 44100.0); }
static TWord w24(double v) { return static_cast<TWord>(static_cast<int32_t>(std::lrint(v * 8388607.0))) & 0xffffff; }
int main(int argc, char** argv)
{
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    const TWord org = std::strtoul(argv[2], nullptr, 16);
    AllowAll validator;
    Memory mem(validator, 0x080000, 0x800000, 0x200000);
    Peripherals56367 py;
    Peripherals56362 px(&py);
    DSP dsp(mem, &px, &py);
    for (size_t i = 0, a = org; i + 2 < b.size(); i += 3, ++a)
        mem.set(MemArea_P, static_cast<TWord>(a), b[i] | b[i + 1] << 8 | b[i + 2] << 16);
    px.getEsai().setReadRxCallback([](uint64_t&, Audio::RxFrame& fr) {
        fr.resize(Audio::MaxSlotsPerFrame);
        for (uint32_t s = 0; s < Audio::MaxSlotsPerFrame; ++s) for (auto& w : fr[s]) w = 0;
        fr[2][0] = w24(A(rxn)); fr[3][0] = w24(B(rxn));   // ring words C D A B = slots 0..3
        ++rxn;
    });
    px.getEsai().setWriteTxCallback([](uint64_t&, const Audio::TxFrame& fr) {
        for (uint32_t s = 0; s < 8; ++s) { int32_t v = s < fr.size() ? (int32_t)(fr[s][0] << 8) >> 8 : 0; tx[s].push_back(v / 8388608.0); }
    });
    py.getEsai().setReadRxCallback([](uint64_t&, Audio::RxFrame& fr) { fr.resize(Audio::MaxSlotsPerFrame); for (uint32_t s = 0; s < Audio::MaxSlotsPerFrame; ++s) for (auto& w : fr[s]) w = 0; });
    py.getEsai().setWriteTxCallback([](uint64_t&, const Audio::TxFrame&) {});
    px.getEsaiClock().setCyclesPerSample(4532 / 8);
    dsp.setPC(org);
    { const uint64_t warm = 400000; while (dsp.getInstructionCounter() < warm) dsp.exec(); }   // the engine's start-up clears its tables first
    // optional chain: engine_test engine.bin 33000 fx1.bin:org:ybase fx2.bin:org:ybase ...  (slot k = argv[3+k])
    for (int a = 3, slot = 0; a < argc; ++a, ++slot) {
        std::string spec = argv[a];
        if (spec.rfind("route", 0) == 0) {   // routeS=lf,rf,lb,rb : slot S out of the chain to the quad buses; cue then carries no chain
            int sl = spec[5] - '0'; int g[4] = {0, 0, 0, 0}; std::string k = spec.substr(7);
            for (int q = 0; q < 4; ++q) { g[q] = std::atoi(k.c_str()); size_t cm = k.find(','); if (cm == std::string::npos) break; k = k.substr(cm + 1); }
            TWord blk = 0x220 + 9 * sl; mem.set(MemArea_X, blk, 1);
            for (int q = 0; q < 4; ++q) { mem.set(MemArea_X, blk + 1 + q, (TWord)(g[q] * 0x7fffff / 100)); mem.set(MemArea_X, blk + 5 + q, 0); }
            routeLater = sl; routeBlk = blk;
            mem.set(MemArea_X, 0x2ea, 0); std::printf("slot %d routed: %d %d %d %d (percent), cue chain off\n", sl, g[0], g[1], g[2], g[3]); --slot; continue; }
        if (spec.rfind("wx", 0) == 0) {   // wxLP:HP cutoffs in Hz (0 = off): the weather biquads
            double lp = std::atof(spec.c_str() + 2), hp = std::atof(spec.substr(spec.find(':') + 1).c_str());
            auto put = [&](TWord base, double f, bool hpf) { double w = 2 * M_PI * f / 44100.0, c = std::cos(w), sn = std::sin(w), al = sn / (2 * 0.7071), a0 = 1 + al;
                double b0 = hpf ? (1 + c) / 2 : (1 - c) / 2, b1 = hpf ? -(1 + c) : 1 - c, b2 = b0, v[5] = {b0 / a0, b1 / a0, b2 / a0, 2 * c / a0, -(1 - al) / a0};
                for (int q = 0; q < 5; ++q) mem.set(MemArea_X, base + q, (TWord)((int32_t)std::lrint(v[q] / 2 * 8388607.0)) & 0xffffff); };
            if (lp > 0) { put(0x200, lp, false); mem.set(MemArea_X, 0x20a, 1); }
            if (hp > 0) { put(0x205, hp, true); mem.set(MemArea_X, 0x20b, 1); }
            std::printf("weather: LP %.0f Hz, HP %.0f Hz\n", lp, hp); --slot; continue; }
        if (spec == "rec") { recording = true; mem.set(MemArea_X, 0x2f4, 15); std::printf("recording A B, C D, out and the tap\n"); --slot; continue; }
        if (spec == "stream") { streaming = true; mem.set(MemArea_X, 0x2e0, 0); mem.set(MemArea_X, 0x2e1, 0); mem.set(MemArea_X, 0x2f2, 0x400000); std::printf("stream on: a 1 kHz tone at 0.25 through the ring, inputs off\n"); --slot; continue; }
        if (spec == "fb") { mem.set(MemArea_X, 0x2d8, 0x780000); mem.set(MemArea_X, 0x2d9, 0x400000); mem.set(MemArea_X, 0x2da, 0x200000); std::printf("feedback on: k 0.94, tone 0.5, d 1\\n"); --slot; continue; }
        size_t c1 = spec.find(':'), c2 = spec.find(':', c1 + 1), c3 = spec.find(':', c2 + 1);
        TWord forg = std::strtoul(spec.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 16), yb = std::strtoul(spec.substr(c2 + 1, c3 == std::string::npos ? std::string::npos : c3 - c2 - 1).c_str(), nullptr, 16);
        int kv[6] = {64, 64, 64, 64, 64, 100};
        if (c3 != std::string::npos) { std::string k = spec.substr(c3 + 1); for (int q = 0; q < 6; ++q) { kv[q] = std::atoi(k.c_str()); size_t cm = k.find(','); if (cm == std::string::npos) break; k = k.substr(cm + 1); } }
        std::ifstream g(spec.substr(0, c1), std::ios::binary);
        std::vector<unsigned char> fb((std::istreambuf_iterator<char>(g)), {});
        for (size_t i = 0, ad = forg; i + 2 < fb.size(); i += 3, ++ad) mem.set(MemArea_P, (TWord)ad, fb[i] | fb[i + 1] << 8 | fb[i + 2] << 16);
        TWord base = 0x280 + 8 * slot, r6 = 0x1800 + 0x20 * slot, r7 = 0x1000 + 0x100 * slot, aw = 0x1900 + slot;
        for (int k = 0; k < 6; ++k) mem.set(MemArea_X, r6 + k, kv[k] << 16);
        mem.set(MemArea_X, aw, yb);
        mem.set(MemArea_X, base + 1, forg + 2); mem.set(MemArea_X, base + 2, forg); mem.set(MemArea_X, base + 3, r6);
        mem.set(MemArea_X, base + 4, r7); mem.set(MemArea_X, base + 5, aw); mem.set(MemArea_X, base + 6, 1); mem.set(MemArea_X, base, 1);
        if (const char* wv = std::getenv("WINDOW")) {   // the grain window table at r7+$80 (65 points)
            for (int i = 0; i <= 64; ++i) { double t = i / 64.0, w = std::string(wv) == "tri" ? 1 - std::fabs(2 * t - 1) : std::string(wv) == "hann" ? 0.5 - 0.5 * std::cos(2 * M_PI * t) : (i == 0 || i == 64 ? 0 : 1.0);
                mem.set(MemArea_X, r7 + 0x80 + i, (TWord)(std::min(w, 0.9999999) * 8388607.0)); } }
        std::printf("slot %d: %s at P:%05x, Y %05x\n", slot, spec.substr(0, c1).c_str(), forg, yb);
    }
    if (routeLater >= 0) mem.set(MemArea_X, 0x280 + 8 * routeLater + 7, routeBlk);   // the slot's link to its routing block
    const uint64_t end = 4532ull * 22050;   // ~0.5 s
    while (dsp.getInstructionCounter() < end) {
        for (int k = 0; k < 64; ++k) dsp.exec();
        while (px.getHDI08().hasTX()) {   // what DMA4 put on the host port, as the ColdFire reads it
            TWord w = px.getHDI08().readTX();
            static int ch = 0, mask = 0;
            if (!(w & 0x800000)) { if ((w >> 20) == 7) { mask = w & 15; ch = 0; ++hdr; } else if ((w >> 20) == 6) ++mtr6; continue; }
            int32_t v = (int32_t)((w & 0x7fffff) << 9) >> 8; double x = v / 8388608.0;
            std::vector<double>* order[6]; int n = 0;
            if (mask & 1) { order[n++] = &recA; order[n++] = &recB; }
            if (mask & 2) { order[n++] = &recC; order[n++] = &recD; }
            if (mask & 4) { order[n++] = &recL; order[n++] = &recR; }
            static std::vector<double> tapv[6]; if (mask & 8) for (int k = 0; k < 6; ++k) order[n++] = &tapv[k];
            if (tapv[0].size() == 20000) { double e = 0; for (size_t i = 10000; i < 20000; ++i) e = std::fmax(e, std::fabs(tapv[0][i] - recA[i])); std::printf("tap A vs balloon A: %.7f, tap main L peak", e); double p = 0; for (size_t i = 10000; i < 20000; ++i) p = std::fmax(p, std::fabs(tapv[4][i])); std::printf(" %.3f\n", p); }
            if (n) { order[ch % n]->push_back(x); ++ch; }
        }
        if (!streaming) continue;
        TWord wp = mem.get(MemArea_X, 0x2f0), rp = mem.get(MemArea_X, 0x2f1);
        while (((wp - rp) & 0x3fff) < 4096) {   // keep it a quarter full, as the ColdFire will
            mem.set(MemArea_X, wp, w24(S(sw))); wp = 0x4000 | ((wp + 1) & 0x3fff);
            mem.set(MemArea_X, wp, w24(-S(sw))); wp = 0x4000 | ((wp + 1) & 0x3fff);
            ++sw;
        }
        mem.set(MemArea_X, 0x2f0, wp);
    }
    if (recording) {
        auto fit = [](const std::vector<double>& r, double (*f)(uint64_t)) { int best = 0; double be = 1e9;
            for (int lat = -2000; lat <= 30000; ++lat) { double e = 0; for (size_t i = r.size() / 2; i < r.size(); i += 5) { long k = (long)i + lat; if (k < 0) { e = 1e9; break; } e = std::fmax(e, std::fabs(r[i] - f(k))); } if (e < be) { be = e; best = lat; } }
            std::printf("  offset %d, worst error %.7f\n", best, be); };
        std::printf("DCR4 %06x DSR4 %06x DCO4 %06x DSTR %06x HSR %06x HPCR %06x\n", px.read(0xffffdc, Nop), px.read(0xffffdf, Nop), px.read(0xffffdd, Nop), px.read(0xfffff4, Nop), px.read(0xffffc3, Nop), px.read(0xffffc4, Nop)); std::printf("ring w %06x r %06x 2f4 %06x hasTX %d\n", mem.get(MemArea_X,0x2f5), mem.get(MemArea_X,0x2f6), mem.get(MemArea_X,0x2f4), (int)px.getHDI08().hasTX()); std::printf("rec: headers %d, meter-6 words %d, A %zu B %zu C %zu D %zu L %zu R %zu\n", hdr, mtr6, recA.size(), recB.size(), recC.size(), recD.size(), recL.size(), recR.size());
        std::printf("A:"); fit(recA, A); std::printf("B:"); fit(recB, B);
        double outerr = 0; for (size_t i = recL.size() / 2; i < recL.size() && i < recA.size(); ++i) outerr = std::fmax(outerr, std::fabs(recL[i] - recA[i])); std::printf("out L vs A same frame: %.7f\n", outerr);
    }
    if (streaming) {
        const size_t n = tx[2].size(); int best = 0; double be = 1e9;
        for (int lat = -9000; lat <= 9000; ++lat) {
            double e = 0; for (size_t i = n / 2; i < n; i += 7) { long k = (long)i - lat; if (k < 0) { e = 1e9; break; } e = std::fmax(e, std::fabs(tx[2][i] - S(k))); }
            if (e < be) { be = e; best = lat; }
        }
        double er = 0; for (size_t i = n / 2; i < n; ++i) er = std::fmax(er, std::fabs(tx[3][i] + S((long)i - best)));
        std::printf("dbg 2f0 %06x 2f1 %06x 2f2 %06x 4000 %06x 4001 %06x 900 %06x 901 %06x 2e8 %06x\n", mem.get(MemArea_X,0x2f0), mem.get(MemArea_X,0x2f1), mem.get(MemArea_X,0x2f2), mem.get(MemArea_X,0x4000), mem.get(MemArea_X,0x4001), mem.get(MemArea_X,0x900), mem.get(MemArea_X,0x901), mem.get(MemArea_X,0x2e8));
        std::printf("stream: main L vs tone, latency %d, worst error %.6f; main R vs -tone %.6f; frames taken $2f3 %06x, written %llu\n", best, be, er, mem.get(MemArea_X, 0x2f3), (unsigned long long)sw);
    }
    std::printf("PC %06x, TX frames %zu, RX frames %llu, DSR2 %06x DDR3 %06x, frame counter x:$32 %06x\n", dsp.getPC().toWord(), tx[0].size(),
                (unsigned long long)rxn, px.read(0xffffe7, Nop), px.read(0xffffe2, Nop), mem.get(MemArea_X, 0x32));
    for (int s = 0; s < 8; ++s) { double p = 0; for (size_t i = tx[s].size() / 2; i < tx[s].size(); ++i) p = std::fmax(p, std::fabs(tx[s][i])); std::printf(" slot %d peak %.3f\n", s, p); }
    { const size_t n = tx[2].size(); double e2 = 0, e0 = 0; for (size_t i = n / 2; i < n; ++i) { long k = (long)i - 32; e2 = std::fmax(e2, std::fabs(tx[2][i] - A(k))); }
      std::printf("main L vs dry A (lag 32): worst %.6f\n", e2); (void)e0; }
    if (const char* ft = std::getenv("FTARGET")) {
        double f0 = std::atof(ft); const size_t n2 = tx[2].size(); size_t a0 = n2 / 2, len = n2 - a0;
        double tot = 0, re = 0, im = 0, re2 = 0, im2 = 0;
        for (size_t i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / len), v = tx[2][a0 + i] * w; tot += v * v;
            re += v * std::cos(2 * M_PI * f0 * i / 44100.0); im += v * std::sin(2 * M_PI * f0 * i / 44100.0);
            re2 += v * std::cos(2 * M_PI * 441.0 * i / 44100.0); im2 += v * std::sin(2 * M_PI * 441.0 * i / 44100.0); }
        double P = tot / (0.375 * len) + 1e-30;                       // the signal's power, the Hann window undone
        double pt = 2 * (re * re + im * im) / (0.25 * len * len) / P, pd = 2 * (re2 * re2 + im2 * im2) / (0.25 * len * len) / P;
        std::printf("purity: %.1f%% of the output is %.1f Hz, %.1f%% is the dry 441 Hz, the rest (%.1f%%) noise/artefacts\n", 100 * pt, f0, 100 * pd, 100 * (1 - pt - (f0 == 441.0 ? 0 : pd)));
    }
    if (std::getenv("FSCAN")) {
        const size_t n2 = tx[2].size(); size_t a0 = n2 / 2, len = n2 - a0; std::vector<std::pair<double,double>> pk;
        double tot = 0; for (size_t i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / len); tot += tx[2][a0+i]*tx[2][a0+i]*w*w; }
        for (double f = 40; f < 6000; f += 5) { double re = 0, im = 0; for (size_t i = 0; i < len; i += 2) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / len), v = tx[2][a0 + i] * w; re += v * std::cos(2 * M_PI * f * i / 44100.0); im += v * std::sin(2 * M_PI * f * i / 44100.0); } pk.push_back({4 * (re*re+im*im) / (0.25*len*len/4) / (tot / (0.375*len) + 1e-30), f}); }
        std::sort(pk.begin(), pk.end(), [](auto& a, auto& b){ return a.first > b.first; });
        std::printf("scan top:"); for (int k = 0; k < 4; ++k) std::printf(" %.0fHz(%.2f)", pk[k].second, pk[k].first);
        double ftop = pk[0].second, near = 0, all = 0; for (auto& q : pk) { all += q.first; if (std::fabs(q.second - ftop) <= 15) near += q.first; }
        std::printf("  | %.1f%% within 15 Hz of %.0f Hz\n", 100 * near / all, ftop);
    }
    // latency fit on slot 2 (main L) against 0.5*A over the second half, rx index = tx index + offset
    const size_t n = tx[2].size(); int best = 0; double be = 1e9;
    for (int lat = -64; lat <= 256; ++lat) {
        double e = 0; for (size_t i = n / 2; i < n; ++i) { long k = (long)i - lat; if (k < 0) continue; e = std::fmax(e, std::fabs(tx[2][i] - 0.5 * A(k))); }
        if (e < be) { be = e; best = lat; }
    }
    std::printf("main L vs 0.5*A: best latency %d samples, worst error %.6f over the second half\n", best, be);
    // per-frame check: does the error stay small everywhere (dropouts / wrong half)?
    size_t bad = 0; for (size_t i = n / 4; i < n; ++i) { long k = (long)i - best; if (k >= 0 && std::fabs(tx[2][i] - 0.5 * A(k)) > 0.001) ++bad; }
    std::printf("samples off by more than 0.001 from 1/4 on: %zu of %zu\n", bad, n - n / 4);
    return 0;
}
