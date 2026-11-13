// c1trace: core 1's real dispatcher (dsp/core1.bin) + effects in dsp56kEmu, every write traced (12 Nov 2026).
// Memory starts as random garbage (the unit's RAM is not cleared). A simulated ColdFire writes slot entries / knobs
// one word at a time, interleaved with core 1's execution (as through core 0's host port); a simulated core 0 posts a
// block (proxy: in-buffer, +7 request) every frame. Flags: any write by effect code outside its declared regions, any
// shared-window write that is not the dispatcher's own, any PC outside known code (runaway), SP depth, SR/OMR/LA/LC
// changed across `jsr (r2)`, a DO loop left active, call length, core 0 proxy words (kb+$10..$12) changed.
// usage: c1trace <scenario> [seed]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <regex>
#include <cstdarg>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
using namespace dsp56k;
class AllowAll final : public IMemoryValidator { public: bool memValidateAccess(EMemArea, TWord, bool) const override { return true; } };

static const char* DIR = "/Users/you/octa/aeria-os/dsp/";
struct Fx { const char* name; const char* bin; TWord org, ybase; TWord len; };
static std::vector<Fx> FX = {
    {"echo", "fx_echo.bin", 0x31000, 0x08000, 0}, {"filt", "fx_filt.bin", 0x37c00, 0, 0}, {"warm", "fx_warm.bin", 0x34400, 0, 0},
    {"bbd", "fx_bbd.bin", 0x3f000, 0x02000, 0}, {"pt2399", "fx_pt2399.bin", 0x3f800, 0x08000, 0}, {"tape", "fx_tape.bin", 0x3e800, 0x08000, 0},
};
static int fxi(const std::string& n) { for (size_t i = 0; i < FX.size(); ++i) if (n == FX[i].name) return (int)i; std::fprintf(stderr, "no fx %s\n", n.c_str()); std::exit(2); }

static const TWord SH_LO = 0x30000, SH_HI = 0x40000, JSR_PC = 0x338b4, RET_PC = 0x338b5;
static TWord shared[SH_HI - SH_LO];
static TWord ENTRY(int s) { return 0x33a10 + 16 * s; }
static TWord KB(int s) { return 0x33c00 + 32 * s; }
static TWord CIN(int s) { return 0x33e00 + 64 * s; }
static TWord COUT(int s) { return 0x33e20 + 64 * s; }

struct Ctx;
static Ctx* G;
struct Ctx {
    AllowAll v; Memory mem; Peripherals56367 py; Peripherals56362 px; DSP dsp;
    std::mt19937 rng;
    int slotFx[8]; bool c0active[8] = {};   // what the ColdFire last configured; core 0's proxy slot enabled
    std::deque<std::array<TWord, 3>> cf;    // ColdFire queue: area (0 X, 1 Y, 9 = core-0 proxy enable s/val), addr, val
    uint64_t nextCf = 0, nextFrame = 0, frameLen = 72000;
    long violations = 0, frames = 0, lateFrames = 0; int maxSP = 0; std::map<std::string, long> vcount;
    // call tracking
    bool inCall = false; TWord callR2 = 0, sr0 = 0, omr0 = 0, sp0 = 0, la0 = 0, lc0 = 0; uint64_t callStart = 0; int callSlot = -1;
    std::map<std::string, uint64_t> maxCall; std::map<std::string, int> maxDepth;
    TWord pcRing[64]; int pcPos = 0; bool dead = false; std::string why;
    Ctx(uint32_t seed) : mem(v, 0x40000, 0x40000, 0x40000), px(&py), dsp(mem, &px, &py), rng(seed) {}
    TWord rnd() { return rng() & 0xffffff; }
    int fxAtPC(TWord pc) const { for (size_t i = 0; i < FX.size(); ++i) if (pc >= FX[i].org && pc < FX[i].org + FX[i].len) return (int)i; return -1; }
};

static void loadBin(Ctx& c, Fx& f) {
    std::ifstream i(std::string(DIR) + f.bin, std::ios::binary); std::vector<unsigned char> b((std::istreambuf_iterator<char>(i)), {});
    TWord a = f.org; for (size_t k = 0; k + 2 < b.size(); k += 3, ++a) c.mem.set(MemArea_P, a, b[k] | b[k + 1] << 8 | b[k + 2] << 16);
    f.len = a - f.org;
}

static bool inR(TWord a, TWord lo, TWord n) { return a >= lo && a < lo + n; }
static bool effAllowed(int fx, int s, EMemArea ar, TWord a) {
    if (ar == MemArea_X && (inR(a, 0x1000 + 0x100 * s, 0x100) || inR(a, 0x900, 0x20))) return true;
    std::string n = FX[fx].name;
    if (n == "bbd") return ar == MemArea_X && inR(a, 0x2000, 0x2000);
    if (n == "pt2399") return ar == MemArea_X && inR(a, 0x8000, 0x1000);
    if (n == "tape") return (ar == MemArea_Y && inR(a, 0x8000, 0x3f80)) || (ar == MemArea_X && inR(a, 0x4000, 0x3f00));
    if (n == "echo") return (ar == MemArea_Y && inR(a, 0x8000, 0x4000)) || (ar == MemArea_X && inR(a, 0x4000, 0x4000));
    return false;   // filt, warm: no buffers
}
static bool dispAllowed(EMemArea ar, TWord a) {
    if (ar != MemArea_X) return false;
    return inR(a, 0x2c8, 3) || a == 0x213 || a == 0x3f || inR(a, 0x33a00, 0x90) || inR(a, 0x900, 0x20) || inR(a, 0x1900, 8)
        || (a >= 0x33e00 && a < 0x34000 && ((a - 0x33e00) & 0x3f) >= 0x20);
}
static const char* AN[] = {"P", "X", "Y"};
static void flag(Ctx& c, const std::string& kind, const char* fmt, ...) {
    c.violations++; long n = ++c.vcount[kind];
    if (n <= 6) { char buf[512]; va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
        std::printf("  !! [%s] frame %ld: %s\n", kind.c_str(), c.frames, buf); }
}

static void onWrite(EMemArea ar, TWord a, TWord val) {
    if (!G) return;
    Ctx& c = *G; TWord pc = c.dsp.getCurrentInstructionPC();
    if (pc >= 0x33800 && pc < 0x33a00) { if (!dispAllowed(ar, a)) flag(c, "dispatcher-write", "PC %05x writes %s:%05x = %06x", pc, AN[ar], a, val); return; }
    int f = c.fxAtPC(pc);
    if (f < 0) { flag(c, "unknown-pc-write", "PC %05x writes %s:%05x = %06x", pc, AN[ar], a, val); return; }
    int s = c.callSlot;
    if (!effAllowed(f, s, ar, a)) {
        const char* where = (a >= SH_LO && a < SH_HI) ? " SHARED WINDOW" : (ar == MemArea_X && inR(a, 0x1000, 0x800)) ? " (another slot's r7)" : "";
        flag(c, std::string(FX[f].name) + "-stray" + where, "%s slot %d PC %05x writes %s:%05x = %06x%s", FX[f].name, s, pc, AN[ar], a, val, where);
    }
}

static void cfw(Ctx& c, TWord a, TWord v, int area = 0) { c.cf.push_back({(TWord)area, a, v & 0xffffff}); }
// main.c fx_configure for a core-1 slot (row 0 / -1 = empty: only the clears)
static void configure(Ctx& c, int s, int fx, const int* knobs, const TWord* p2) {
    cfw(c, 0x280 + 8 * s, 0, 9);            // core 0 slot off
    cfw(c, ENTRY(s), 0);
    cfw(c, KB(s) + 6, 0); cfw(c, KB(s) + 7, 0);
    c.slotFx[s] = fx;
    if (fx < 0) return;
    const Fx& f = FX[fx];
    for (int k = 0; k < 6; k++) cfw(c, KB(s) + k, (TWord)knobs[k] << 16);
    for (int w = 0; w < 3; w++) cfw(c, KB(s) + 0xc + w, p2[w]);
    cfw(c, KB(s) + 0x10, ENTRY(s)); cfw(c, KB(s) + 0x11, CIN(s)); cfw(c, KB(s) + 0x12, COUT(s));
    for (int i = 0; i < 32; i++) cfw(c, COUT(s) + i, 0);
    TWord e = ENTRY(s);
    cfw(c, e + 1, f.org + 2); cfw(c, e + 2, f.org); cfw(c, e + 3, KB(s)); cfw(c, e + 4, 0x1000 + 0x100 * s); cfw(c, e + 5, f.ybase);
    cfw(c, e + 6, 1); cfw(c, e + 7, 0); cfw(c, e + 8, 0); cfw(c, e + 9, CIN(s)); cfw(c, e + 10, COUT(s)); cfw(c, e + 11, 0x1900 + s);
    cfw(c, e, 1);
    cfw(c, 0x280 + 8 * s, 1, 9);            // core 0 proxy slot on
}

// every X/Y operand of the instruction at PC (from the .lst), its effective address from the registers before it runs
struct Acc { int sp; int rn; int mode; int off; };   // mode 0 (rN)(+/-...) 1 (rN+nN) 2 (rN+off) 3 -(rN)
static std::map<TWord, std::vector<Acc>> ACC; static std::map<TWord, std::string> TXT;
static void loadLst(const char* lst) {
    std::ifstream i(std::string(DIR) + lst); std::string l;
    std::regex line(R"(^([0-9a-f]{6}):\s+(\S+)\s*(.*?)\s*;)"), op(R"(([xy]):(-?)\(r([0-7])((\+n[0-7])|([+-]\$[0-9a-f]+))?\))");
    while (std::getline(i, l)) {
        std::smatch m; if (!std::regex_search(l, m, line)) continue;
        TWord pc = (TWord)std::stoul(m[1], nullptr, 16); std::string o = m[2], a = m[3];
        TXT[pc] = o + " " + a;
        if (o == "lua" || o == "lra") continue;
        for (std::sregex_iterator it(a.begin(), a.end(), op), e; it != e; ++it) {
            Acc x{(*it)[1] == "x" ? 1 : 2, std::stoi((*it)[3]), 0, 0};
            if ((*it)[2] == "-") x.mode = 3; else if ((*it)[5].matched) x.mode = 1;
            else if ((*it)[6].matched) { std::string k = (*it)[6]; x.mode = 2; x.off = (int)std::stol(k.substr(2), nullptr, 16) * (k[0] == '-' ? -1 : 1); }
            ACC[pc].push_back(x);
        }
    }
}
static void checkAccess(Ctx& c, int f, TWord pc, const DspRegs& r) {
    auto it = ACC.find(pc); if (it == ACC.end()) return;
    for (auto& x : it->second) {
        TWord rv = r.r[x.rn].toWord(), ea = rv;
        if (x.mode == 1) ea = (rv + r.n[x.rn].toWord()) & 0xffffff; else if (x.mode == 2) ea = (rv + x.off) & 0xffffff; else if (x.mode == 3) ea = (rv - 1) & 0xffffff;
        bool bad; const char* why = "";
        TWord kb = KB(c.callSlot);
        if (ea >= SH_LO && ea < SH_HI) { bad = !(x.sp == 1 && ea >= kb && ea < kb + 0x20); why = "shared window"; }
        else if (ea >= 0xffff80) { bad = true; why = "peripheral"; }
        else if (ea >= SH_HI) { bad = true; why = "above $40000 (Y there froze a core)"; }
        else if (x.sp == 1) { bad = ea >= 0x9000; why = "X hole"; }
        else { bad = ea >= 0xc000; why = "Y hole"; }
        if (bad) flag(c, std::string(FX[f].name) + " access " + why, "slot %d PC %05x '%s' %s:%06x (r%d %06x n %06x m %06x)", c.callSlot, pc, TXT[pc].c_str(), x.sp == 1 ? "X" : "Y", ea, x.rn, rv, r.n[x.rn].toWord(), r.m[x.rn].toWord());
    }
}

static void step(Ctx& c) {
    // ColdFire: one host word every 300..2500 instructions
    uint64_t ic = c.dsp.getInstructionCounter();
    if (!c.cf.empty() && ic >= c.nextCf) {
        auto w = c.cf.front(); c.cf.pop_front();
        if (w[0] == 9) c.c0active[(w[1] - 0x280) / 8] = w[2] != 0;
        else { G = nullptr; c.mem.set(w[0] == 0 ? MemArea_X : MemArea_Y, w[1], w[2]); G = &c; }
        c.nextCf = ic + 300 + c.rng() % 2200;
    }
    // core 0: once a frame the proxy of every enabled core-1 slot posts its block
    if (ic >= c.nextFrame) {
        c.frames++; c.nextFrame = ic + c.frameLen;
        G = nullptr;
        for (int s = 0; s < 8; s++) if (c.c0active[s]) {
            TWord kb = KB(s);
            TWord e = c.mem.get(MemArea_X, kb + 0x10), in = c.mem.get(MemArea_X, kb + 0x11), out = c.mem.get(MemArea_X, kb + 0x12);
            if (e != ENTRY(s) || in != CIN(s) || out != COUT(s)) { G = &c; flag(c, "PROXY-WORDS-CORRUPT", "slot %d kb+$10..12 = %06x %06x %06x (core 0 would write 32 words there)", s, e, in, out); G = nullptr; }
            double ph = c.frames * 16;
            for (int i = 0; i < 32; i++) { double x = 0.5 * std::sin(ph * 0.0623 + i / 2 * 0.0623) + 0.3 * ((int)(c.rng() % 2001) - 1000) / 1000.0; c.mem.set(MemArea_X, CIN(s) + i, (TWord)((int)std::lrint(x * 4194303.0)) & 0xffffff); }
            TWord req = c.mem.get(MemArea_X, ENTRY(s) + 7), done = c.mem.get(MemArea_X, ENTRY(s) + 8);
            if (((req - done) & 0xffff) > 1) c.lateFrames++;
            c.mem.set(MemArea_X, ENTRY(s) + 7, (req + 1) & 0xffffff);
        }
        G = &c;
    }
    const auto& r = c.dsp.readRegs();
    TWord pc = c.dsp.getPC().toWord();
    if (pc == JSR_PC) {
        c.inCall = true; c.callR2 = r.r[2].toWord(); c.sr0 = c.dsp.getSR().toWord(); c.omr0 = r.omr.toWord(); c.sp0 = r.sp.toWord();
        c.la0 = r.la.toWord(); c.lc0 = r.lc.toWord(); c.callStart = ic;
        TWord r4 = c.mem.get(MemArea_X, 0x2c8); c.callSlot = (int)((r4 - 0x33a10) / 16);
    }
    if (pc == RET_PC && c.inCall) {
        c.inCall = false; int f = c.fxAtPC(c.callR2);
        std::string key = std::string(f >= 0 ? FX[f].name : "?") + (f >= 0 && c.callR2 == FX[f].org ? ".init" : ".proc");
        uint64_t n = ic - c.callStart; if (n > c.maxCall[key]) c.maxCall[key] = n;
        TWord sr = c.dsp.getSR().toWord(), omr = r.omr.toWord(), sp = r.sp.toWord();
        if ((sr & ~0xff) != (c.sr0 & ~0xff)) flag(c, key + " SR-changed", "SR %06x -> %06x", c.sr0, sr);
        if (omr != c.omr0) flag(c, key + " OMR-changed", "OMR %06x -> %06x", c.omr0, omr);
        if (sp != c.sp0) flag(c, key + " SP-changed", "SP %x -> %x", c.sp0, sp);
        if (r.la.toWord() != c.la0 || r.lc.toWord() != c.lc0) flag(c, key + " LA/LC-changed", "LA %06x LC %06x -> LA %06x LC %06x", c.la0, c.lc0, r.la.toWord(), r.lc.toWord());
        if (n > 30000) flag(c, key + " long-call", "%llu instructions", (unsigned long long)n);
    }
    if (c.inCall) {
        int f = c.fxAtPC(pc);
        if (f >= 0) checkAccess(c, f, pc, r);
        if (f >= 0) { std::string key = FX[f].name; int d = (int)(r.sp.toWord() & 0xf) - (int)(c.sp0 & 0xf); if (d > c.maxDepth[key]) c.maxDepth[key] = d; }
        if ((r.sp.toWord() & 0xf) > c.maxSP) c.maxSP = r.sp.toWord() & 0xf;
        if (ic - c.callStart > 2000000) { c.dead = true; c.why = "call never returns"; }
    }
    if (r.sp.toWord() & 0x30) { flag(c, "STACK-ERROR", "SP %06x at PC %05x", r.sp.toWord(), pc); }
    bool known = (pc >= 0x33800 && pc < 0x33a00) || c.fxAtPC(pc) >= 0;
    if (!known) { c.dead = true; char b[96]; std::snprintf(b, sizeof b, "PC %05x outside known code", pc); c.why = b; }
    c.pcRing[c.pcPos++ & 63] = pc;
    if (getenv("C1DBG")) { std::fprintf(stderr, "pc %05x\n", pc); }
    c.dsp.exec(); c.dsp.doLoopEnd();
    while (c.px.getHDI08().hasTX()) c.px.getHDI08().readTX();
}

static void runFrames(Ctx& c, long nframes) { long end = c.frames + nframes; while (c.frames < end && !c.dead) step(c); }
static void drainCF(Ctx& c) { while (!c.cf.empty() && !c.dead) step(c); }

static void knobs(Ctx& c, int s, int k, int v) { cfw(c, KB(s) + k, (TWord)v << 16); }
static void p2(Ctx& c, int s, int w, int hi, int lo) { cfw(c, KB(s) + 0xc + w, (TWord)hi << 16 | (TWord)lo << 8); }
// random knob abuse on slot s: main 0..127 incl. extremes, FUNC bytes 0..127 (the sequencer masks to 7 bits only),
// the freeze words, every so many frames
static void abuse(Ctx& c, int s, long frames, int every) {
    for (long f = 0; f < frames && !c.dead; f += every) {
        int what = c.rng() % 10;
        auto v = [&]() { int r = c.rng() % 6; return r == 0 ? 0 : r == 1 ? 127 : r == 2 ? 1 : (int)(c.rng() % 128); };
        if (what < 5) knobs(c, s, c.rng() % 6, v());
        else if (what < 8) p2(c, s, c.rng() % 3, v(), v());
        else if (what == 8) cfw(c, KB(s) + 6, c.rng() % 4);
        else cfw(c, KB(s) + 7, c.rng() % 2);
        runFrames(c, every);
    }
}

static void report(Ctx& c, const char* name) {
    std::printf("== %s: %ld frames, late %ld, violations %ld, max SP %d%s%s\n", name, c.frames, c.lateFrames, c.violations, c.maxSP,
                c.dead ? ", DEAD: " : "", c.why.c_str());
    for (auto& k : c.vcount) std::printf("   %-40s %ld\n", k.first.c_str(), k.second);
    for (auto& k : c.maxCall) std::printf("   longest %-14s %llu instr (%.0f a sample)\n", k.first.c_str(), (unsigned long long)k.second, k.second / 16.0);
    for (auto& k : c.maxDepth) std::printf("   deepest stack inside %-8s +%d below the dispatcher's\n", k.first.c_str(), k.second);
    if (c.dead) { std::printf("   last PCs:"); for (int i = 0; i < 64; i++) std::printf(" %05x", c.pcRing[(c.pcPos + i) & 63]); std::printf("\n"); }
}

int main(int argc, char** argv) {
    std::string sc = argc > 1 ? argv[1] : "solo:bbd";
    uint32_t seed = argc > 2 ? (uint32_t)std::atoi(argv[2]) : 1;
    long secs = argc > 3 ? std::atol(argv[3]) : 3;
    Ctx c(seed); G = &c;
    c.dsp.setHostStepped(true);
    c.mem.setSharedWindow(SH_LO, SH_HI, shared, nullptr);
    for (TWord a = 0; a < SH_LO; a++) { c.mem.set(MemArea_X, a, c.rnd()); c.mem.set(MemArea_Y, a, c.rnd()); c.mem.set(MemArea_P, a, c.rnd()); }
    for (auto& w : shared) w = c.rnd();
    G = nullptr;
    Fx core1 = {"core1", "core1.bin", 0x33800, 0, 0}; loadBin(c, core1);
    for (auto& f : FX) { loadBin(c, f); loadLst((std::string(f.bin).substr(0, std::string(f.bin).size() - 4) + ".lst").c_str()); }
    for (int s = 0; s < 8; s++) c.slotFx[s] = -1;
    { size_t n = 0; for (auto& a : ACC) n += a.second.size(); std::printf("operands parsed: %zu at %zu PCs\n", n, ACC.size()); }
    c.mem.setWriteHook(onWrite);
    G = &c;
    c.dsp.setPC(0x33800); c.dsp.writeReg(Reg_SP, TReg24(0)); c.dsp.writeReg(Reg_OMR, TReg24(0)); c.dsp.writeReg(Reg_SR, TReg24(0xc00300));
    { const auto& r = c.dsp.readRegs(); std::printf("m4 %06x sp %06x\n", r.m[4].toWord(), r.sp.toWord()); }
    for (int i = 0; i < 300; ++i) { step(c); }
    std::printf("after 300: pc %05x X:33a10 %06x X:33a90 %06x shared %06x\n", c.dsp.getPC().toWord(), c.mem.get(MemArea_X, 0x33a10), c.mem.get(MemArea_X, 0x33a90), shared[0x3a10]);
    for (int i = 0; i < 20000; ++i) step(c);   // start-up: clears $33a00..
    c.frames = 0; c.vcount.clear(); c.violations = 0;
    long F = secs * 44100 / 16;
    auto def = [&](int fx, int* k, TWord* w) {
        std::string n = FX[fx].name;
        int d[6] = {83, 84, 44, 100, 0, 44}; for (int i = 0; i < 6; i++) k[i] = d[i];
        w[0] = 0; w[1] = 0; w[2] = 0x1f0000; (void)n;
    };
    int k[6]; TWord w[3];
    auto load = [&](int s, const std::string& n) { int f = n == "-" ? -1 : fxi(n); def(f < 0 ? 0 : f, k, w); configure(c, s, f, k, w); };
    if (sc.rfind("solo:", 0) == 0) {           // one effect, slot from arg 4 (default 0): default knobs, then abuse
        int s = argc > 4 ? std::atoi(argv[4]) : 0;
        load(s, sc.substr(5)); drainCF(c); runFrames(c, 400);
        abuse(c, s, F, 20);
        abuse(c, s, F, 1);                     // every frame: TIME jumps, FRZ/TMOD/RFSH flips
    } else if (sc == "repro") {               // PT2399 s0 + TAPE s1 ~3 s, both cleared, BBD s0 + TAPE s1
        for (int round = 0; round < 2 && !c.dead; round++) {
            load(0, "pt2399"); load(1, "tape"); drainCF(c); abuse(c, 0, F / 2, 7); abuse(c, 1, F / 2, 9);
            load(0, "-"); load(1, "-"); drainCF(c); runFrames(c, 50);
            load(0, "bbd"); load(1, "tape"); drainCF(c); abuse(c, 0, F / 2, 7); abuse(c, 1, F / 2, 11);
            load(0, "-"); load(1, "-"); drainCF(c); runFrames(c, 50);
        }
    } else if (sc == "repro-fast") {          // replacements without clearing first, straight after each other
        for (int round = 0; round < 6 && !c.dead; round++) {
            load(0, "pt2399"); load(1, "tape"); runFrames(c, F / 3);
            load(0, "-"); load(1, "-"); runFrames(c, 3);
            load(0, "bbd"); load(1, "tape"); runFrames(c, F / 3);
        }
        drainCF(c); runFrames(c, 200);
    } else if (sc == "cycle") {               // track 0: ECHO FILT WARM BBD PT2399 TAPE, each replacing the last
        const char* seq[] = {"echo", "filt", "warm", "bbd", "pt2399", "tape"};
        for (int round = 0; round < 4 && !c.dead; round++) for (auto n : seq) { load(0, n); abuse(c, 0, F / 6, 13); }
    } else if (sc == "selftest") {            // the harness must catch this: BBD given a base in the shared window
        FX[fxi("bbd")].ybase = 0x32000; load(0, "bbd"); drainCF(c); runFrames(c, 30);
    } else { std::fprintf(stderr, "unknown scenario\n"); return 2; }
    drainCF(c);
    report(c, (sc + " seed " + std::to_string(seed)).c_str());
    return c.dead || c.violations ? 1 : 0;
}
