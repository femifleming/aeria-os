// aeria_sim -- AERIA OS in a simulated Octatrack MKII.
//
// Runs the REAL firmware: the ColdFire image (MAIN.raw, as flashed) on Octabam's ColdFire core, and whatever that
// image uploads to the two DSP56300 cores on Octabam's DSP pair (boot ROM, host port, ESAI, DMA, shared window).
// Nothing of AERIA OS is recompiled or stubbed. Around them this file models only what AERIA OS touches:
//   DTIM3 (its clock), UART1 (the front panel: keys, encoders, crossfader in; LCD blocks and LED levels out),
//   UART0 (DIN MIDI), DSPI (the CS42432 codec's registers on select 0, the clock chip on select 1), the GPIO page,
//   the ATA task file (a card image), battery RAM, the USB controller as "no cable".
// It is a SOFTWARE model: it shows what the firmware computes and sends (every word of the eight ESAI transmit
// slots, every codec register write, every LED level) -- not what the analog stage after the converters does.
//
// Octabam's emulator (~/octa/octabam, tools/emu/ot_emu) is used as a library, unmodified; its own front end is built
// around Elektron's scheduler and stops AERIA OS at its first timer wait, hence this small driver.
//
//   aeria_sim --image MAIN.raw [--bram FILE] [--card FILE] [--dirty SEED] [--cold]
// then a line protocol on stdin/stdout (sim/aeriasim.py speaks it):
//   run MS | key CODE 0|1 | enc N DELTA | xfade V | midi HEX | midiout | lcd | leds | tx | codec
//   peek ADDR LEN | poke ADDR HEX | dsp CORE X|Y|P ADDR LEN | input FILE | rxframes | audio FILE | status | bramsave FILE | quit
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "card.h"
#include "dsp.h"
#include "machine.h"

namespace ot
{
	uint32_t g_aeriaRot = 0xff;				// set by the probe in the DSP pair (build.sh)
	uint64_t g_aeriaRotChanges = 0, g_aeriaRotFrame = 0;
}
using ot::g_aeriaRot; using ot::g_aeriaRotChanges; using ot::g_aeriaRotFrame;

namespace
{
	std::vector<uint8_t> readFile(const std::string& _p)
	{
		std::ifstream f(_p, std::ios::binary);
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}
	std::string hex(const std::vector<uint8_t>& _b)
	{
		static const char* d = "0123456789abcdef";
		std::string s;
		s.reserve(_b.size() * 2);
		for(const auto b : _b) { s.push_back(d[b >> 4]); s.push_back(d[b & 15]); }
		return s;
	}
	std::vector<uint8_t> unhex(const std::string& _s)
	{
		std::vector<uint8_t> o;
		for(size_t i = 0; i + 1 < _s.size(); i += 2)
			o.push_back(static_cast<uint8_t>(std::strtoul(_s.substr(i, 2).c_str(), nullptr, 16)));
		return o;
	}

	// The clock: instructions -> bus-clock ticks. Octabam's calibration of the unit: 3990 ColdFire instructions and
	// 4532 DSP instructions per audio sample (44.1 kHz). DTIM3 counts the 132 MHz bus clock.
	// ColdFire instructions per sample of the CONVERTER: Octabam's ESAI model clocks a slot every floor(4532 / 8) = 566
	// DSP instructions, so its sample is 4,528 DSP instructions, not 4,532 -- 0.09 % faster than the ColdFire's timer
	// thought, which made every MIDI note drift against the audio by 0.4 ms a beat (19 Nov 2026). The ColdFire's time is
	// scaled to the converter's instead, as on the unit, where both crystals agree to some tens of ppm.
	constexpr double g_cfIps = 566.0 * 8.0 * ot::DspPair::g_cfIps / ot::DspPair::g_dspIps;	// per sample
	constexpr double g_ticksPerInstr = 132e6 / (g_cfIps * 44100.0);

	// Slow bus cycles. An instruction that reaches through the FlexBus (the DSP's host port, the card) takes far longer
	// than one that does not: on the unit a tight loop reading the host port manages about 800,000 words a second at
	// four accesses a word (measured 12 Nov 2026, the stems' fast path), i.e. roughly 0.28 us an access -- some fifty
	// instructions' worth. Without that the simulated ColdFire floods the engine's 1,024-word host ring during the
	// effect upload, which the unit's does not. g_extra counts the added time in instruction units; the clock and the
	// DSPs' schedule both include it.
	uint64_t g_extra = 0;
	constexpr uint64_t g_flexBusCost = 49;		// instruction units per FlexBus access (0.28 us)
	constexpr uint64_t g_ipsCost = 5;			// on-chip peripheral access (~30 ns)

	// The host port as AERIA OS uses it. AERIA writes each of TXH / TXM / TXL (and ICR, CVR) with one 16-bit access,
	// the value in the low byte, and reads them the same way -- and on the unit its 24-bit words arrive whole (every
	// upload and every address word since 9 Nov 2026 depends on it). Octabam's model of the port puts the HIGH byte
	// of a 16-bit access into the register before the one addressed (its reading of stock's block transfers), which
	// would zero TXH and TXM under AERIA's writes. So a 16-bit access is passed on as the low byte alone, to the
	// register addressed. This is the one place the simulator departs from Octabam's model, on the unit's evidence.
	class HostPort final : public ot::Coprocessor
	{
	public:
		explicit HostPort(ot::DspPair& _d) : m_d(_d) {}
		void slow(const uint32_t _a) { if(_a >= ot::DspPair::g_window && _a < ot::DspPair::g_windowEnd) { g_extra += g_flexBusCost; m_d.tickInstructions(g_flexBusCost); } }
		bool read(uint32_t _a, uint8_t _size, uint32_t& _out) override
		{
			slow(_a);
			if(_size == 2 && _a >= ot::DspPair::g_window && _a < ot::DspPair::g_windowEnd && !(_a & 1))
				return m_d.read(_a + 1, 1, _out);
			return m_d.read(_a, _size, _out);
		}
		bool write(uint32_t _a, uint8_t _size, uint32_t _v) override
		{
			slow(_a);
			if(_size == 2 && _a >= ot::DspPair::g_window && _a < ot::DspPair::g_windowEnd && !(_a & 1))
				return m_d.write(_a + 1, 1, _v & 0xff);
			return m_d.write(_a, _size, _v);
		}
		void tickInstructions(uint64_t _n) override { m_d.tickInstructions(_n); }
		double tickSamples(double _n) override { return m_d.tickSamples(_n); }
		void sync() override { m_d.sync(); }
		int selected() const override { return m_d.selected(); }
		bool hostRingEmpty(int _c) const override { return m_d.hostRingEmpty(_c); }
		void pushHalfwords(uint32_t _a, const std::vector<uint16_t>& _h) override { m_d.pushHalfwords(_a, _h); }
		size_t pullHalfwords(uint32_t _a, int _c, std::vector<uint16_t>& _o, size_t _n) override { return m_d.pullHalfwords(_a, _c, _o, _n); }
		std::string blockNote(int _c) override { return m_d.blockNote(_c); }
		uint32_t peekWord(int _c, char _s, uint32_t _a) const override { return m_d.peekWord(_c, _s, _a); }
	private:
		ot::DspPair& m_d;
	};

	struct Sim
	{
		ot::Machine& m;
		ot::DspPair& dsp;
		std::unique_ptr<ot::AtaCard> card;

		uint32_t dtcnBase = 0;			// DTIM3: a write to DTCN clears the count
		uint64_t vinstr() const { return m.instructions() + g_extra; }		// the machine's time, in instruction units
		void slow(const uint64_t _n) { g_extra += _n; dsp.tickInstructions(_n); }
		uint32_t ticks() const { return static_cast<uint32_t>(ticks64()); }
		uint64_t ticks64() const { return static_cast<uint64_t>(static_cast<double>(vinstr()) * g_ticksPerInstr); }

		// ---- a UART: bytes in when asked for, bytes out paced by the line's speed ----
		struct Uart
		{
			std::deque<uint8_t> rx;
			uint64_t txFreeAt = 0;		// bus ticks: when the transmitter takes the next byte
			uint64_t byteTicks = 4160;	// ten bits at the line's speed
			std::vector<uint8_t> tx;	// everything sent since the last take
			std::vector<uint64_t> txAt;
		};
		Uart panel, midi;

		// ---- the front panel (MKII) at the far end of UART1 ----
		uint8_t lcd[8][128] = {};
		uint8_t ledLevel[256] = {};
		uint8_t ledRow[17] = {};
		uint8_t keyRow[8] = {};
		int xfadeNow = 0;
		std::vector<uint8_t> pmsg;
		uint64_t lcdBlocks = 0, ledMsgs = 0, panelUnknown = 0;
		bool loader = false;
		static size_t plen(const uint8_t _op)
		{
			switch(_op >> 4)
			{
			case 0x1: return 10;						// LCD block: page, column, eight bytes
			case 0x2: case 0x3: case 0xa: return 2;		// LED row / LED level
			case 0x6: case 0x7: return 2;				// 60 xx, 70 xx, 74 xx
			case 0xb: return _op == 0xb5 ? 6 : 2;		// palette entry, backlight
			default: return 1;
			}
		}
		void panelByte(const uint8_t _b)
		{
			pmsg.push_back(_b);
			if(pmsg.size() < plen(pmsg[0]))
				return;
			const auto q = pmsg;
			pmsg.clear();
			const uint8_t op = q[0];
			if((op >> 4) == 1 && q.size() == 10)
			{
				const int page = op & 7, col = q[1] & 0x7f;
				for(int k = 0; k < 8 && col + k < 128; ++k)
					lcd[page][col + k] = q[2 + k];
				++lcdBlocks;
			}
			else if((op >> 4) == 2 && q.size() == 2) { ledRow[op & 15] = q[1]; ++ledMsgs; }
			else if(op == 0xa0) { ledRow[16] = q[1]; ++ledMsgs; }
			else if((op >> 4) == 3 && q.size() == 2) { ledLevel[q[1]] = op & 15; ++ledMsgs; }
			else if(op == 0x60) { loader = q[1] == 0x02; if(q[1] == 0x00) panelDump(); }
			else if(op == 0x70 && loader) { for(const uint8_t b : {0x70, 0x05, 0x08, 0x00, 0x00}) panel.rx.push_back(b); }
			else if(op == 0x74) { for(const uint8_t b : {0x70, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}) panel.rx.push_back(b); }
			else if(op == 0xb5 || op == 0xb7) {}
			else ++panelUnknown;
		}
		// what the panel sends when its application starts: the crossfader and every key row
		void panelDump()
		{
			panel.rx.push_back(0x40); panel.rx.push_back(static_cast<uint8_t>(xfadeNow));
			for(int r = 0; r < 8; ++r) { panel.rx.push_back(static_cast<uint8_t>(0x20 | r)); panel.rx.push_back(keyRow[r]); }
		}
		void key(const int _code, const bool _down)
		{
			const int r = (_code >> 3) & 7;
			if(_down) keyRow[r] |= static_cast<uint8_t>(1u << (_code & 7)); else keyRow[r] &= static_cast<uint8_t>(~(1u << (_code & 7)));
			panel.rx.push_back(static_cast<uint8_t>(0x20 | r)); panel.rx.push_back(keyRow[r]);
		}

		// ---- DSPI: the codec (select 0) and the clock chip (select 1) ----
		std::deque<uint8_t> spiRx;
		std::vector<uint8_t> spiFrame;	// the bytes of the transaction in progress
		uint32_t spiCs = 0;
		uint8_t codec[256] = {};
		std::vector<std::pair<uint8_t, uint8_t>> codecWrites;
		void spiPush(const uint32_t _v)
		{
			const uint32_t cs = (_v >> 16) & 0x3f;
			const uint8_t b = static_cast<uint8_t>(_v);
			if(spiFrame.empty()) spiCs = cs;
			uint8_t reply = 0;
			spiFrame.push_back(b);
			if(spiCs & 1)					// the codec: 9E reg val (write), 9F then clocks (read from the last MAP)
			{
				if(spiFrame[0] == 0x9f && spiFrame.size() >= 2) reply = codec[static_cast<uint8_t>(codecMap + spiFrame.size() - 2)];
				if(spiFrame[0] == 0x9e && spiFrame.size() == 2) codecMap = spiFrame[1] & 0x7f;
				if(spiFrame[0] == 0x9e && spiFrame.size() >= 3)
				{
					const uint8_t reg = static_cast<uint8_t>(codecMap + spiFrame.size() - 3);
					codec[reg] = b;
					codecWrites.emplace_back(reg, b);
				}
			}
			else if(spiCs & 2)				// the clock: <reg> 00 -> the register (BCD), 12:00:00
			{
				if(spiFrame.size() == 2) { const uint8_t r = spiFrame[0] & 0x7f; reply = r == 3 ? 0x12 : 0x00; }
			}
			spiRx.push_back(reply);
			if(!(_v & 0x80000000u)) spiFrame.clear();	// CONT clear: the select is released after this byte
		}
		uint8_t codecMap = 0;

		// ---- plain registers ----
		uint8_t gpio[256] = {};
		uint8_t reg3a = 0;
		std::unordered_map<uint32_t, uint32_t> misc;
		bool reset = false;
		std::unordered_map<uint32_t, uint32_t> unknownReads, unknownWrites;

		bool read(const uint32_t _a, const uint8_t _size, uint32_t& _out)
		{
			slow(_a >= 0x90000000 && _a < 0x90001000 ? g_flexBusCost : g_ipsCost);
			if(_a >= 0xfc07c000 && _a < 0xfc07c010)
			{
				if(_a == 0xfc07c00c) { _out = ticks() - dtcnBase; return true; }
				_out = misc[_a]; return true;
			}
			if(_a >= 0xfc064000 && _a < 0xfc064040) return uartRead(panel, _a - 0xfc064000, _out);
			if(_a >= 0xfc060000 && _a < 0xfc060040) return uartRead(midi, _a - 0xfc060000, _out);
			if(_a >= 0xfc05c000 && _a < 0xfc05c100)
			{
				const uint32_t off = _a - 0xfc05c000;
				if(off == 0x2c) { _out = (static_cast<uint32_t>(std::min<size_t>(spiRx.size(), 15)) << 4) | 0x02000000u; return true; }
				if(off == 0x38) { _out = spiRx.empty() ? 0 : spiRx.front(); if(!spiRx.empty()) spiRx.pop_front(); return true; }
				_out = misc[_a]; return true;
			}
			if(_a >= 0xfc0a4000 && _a < 0xfc0a4100 && _size == 1)
			{
				const uint32_t off = _a - 0xfc0a4000;
				if(off == 0x3a) { _out = static_cast<uint32_t>(reg3a | ((reg3a & 0x20) ? 0x40 : 0)); return true; }	// the MKII's loopback: bit 6 follows bit 5
				_out = gpio[off]; return true;
			}
			if(_a >= 0x90000000 && _a < 0x90001000)
			{
				const uint32_t off = _a - 0x90000000;
				if(off >= 0xa0 && off <= 0xdb)
				{
					if(card) { _out = card->read(off, _size); return true; }
					_out = 0; return true;				// no card: never ready
				}
				_out = misc[_a]; return true;
			}
			if(_a >= 0xfc0b0000 && _a < 0xfc0b1000) { _out = 0; return true; }		// USB: no cable (OTGSC B-session clear)
			if(_a >= 0xfc000000 && _a < 0xfc100000)
			{
				if(const auto it = misc.find(_a); it != misc.end()) { _out = it->second; return true; }
				++unknownReads[_a];
				_out = 0; return true;
			}
			return false;
		}
		bool uartRead(Uart& _u, const uint32_t _off, uint32_t& _out)
		{
			if(_off == 4) { _out = (_u.rx.empty() ? 0u : 1u) | (ticks64() >= _u.txFreeAt ? 0x0cu : 0u); return true; }
			if(_off == 0xc) { _out = _u.rx.empty() ? 0 : _u.rx.front(); if(!_u.rx.empty()) _u.rx.pop_front(); return true; }
			_out = 0; return true;
		}
		void uartWrite(Uart& _u, const uint32_t _off, const uint32_t _v, const bool _isPanel)
		{
			if(_off == 0x1c) { _u.byteTicks = static_cast<uint64_t>(_v ? _v : 1) * 320; return; }		// UBG2: 132 MHz / (32 x n), ten bits a byte
			if(_off != 0xc) return;
			const uint64_t t = ticks64();
			_u.txFreeAt = (t > _u.txFreeAt ? t : _u.txFreeAt) + _u.byteTicks;
			_u.tx.push_back(static_cast<uint8_t>(_v));
			_u.txAt.push_back(t);
			if(_isPanel) panelByte(static_cast<uint8_t>(_v));
		}
		void write(const uint32_t _a, const uint8_t _size, const uint32_t _v)
		{
			slow(_a >= 0x90000000 && _a < 0x90001000 ? g_flexBusCost : g_ipsCost);
			if(_a >= 0xfc07c000 && _a < 0xfc07c010) { if(_a == 0xfc07c00c) dtcnBase = ticks(); else misc[_a] = _v; return; }
			if(_a >= 0xfc064000 && _a < 0xfc064040) { uartWrite(panel, _a - 0xfc064000, _v, true); return; }
			if(_a >= 0xfc060000 && _a < 0xfc060040) { uartWrite(midi, _a - 0xfc060000, _v, false); return; }
			if(_a >= 0xfc05c000 && _a < 0xfc05c100)
			{
				const uint32_t off = _a - 0xfc05c000;
				if(off == 0x34) spiPush(_v); else if(off != 0x2c) misc[_a] = _v;
				return;
			}
			if(_a >= 0xfc0a4000 && _a < 0xfc0a4100 && _size == 1)
			{
				const uint32_t off = _a - 0xfc0a4000;
				if(off == 0x3a) reg3a |= static_cast<uint8_t>(_v);			// set
				else if(off == 0x52) reg3a &= static_cast<uint8_t>(_v);		// clear
				gpio[off] = static_cast<uint8_t>(_v);
				return;
			}
			if(_a == 0xfc0a0000 && (_v & 0x80)) { reset = true; return; }		// RCR: soft reset
			if(_a >= 0x90000000 && _a < 0x90001000)
			{
				const uint32_t off = _a - 0x90000000;
				if(off >= 0xa0 && off <= 0xdb) { if(card) card->write(off, _size, _v); return; }
				misc[_a] = _v; return;
			}
			if(_a >= 0xfc0b0000 && _a < 0xfc0b1000) return;
			if(_a >= 0xfc000000 && _a < 0xfc100000) { misc[_a] = _v; ++unknownWrites[_a]; return; }
		}
	};
}

int main(int _argc, char** _argv)
{
	std::string image, bram, cardPath;
	uint32_t dirty = 0;
	bool exact = false;
	for(int i = 1; i < _argc; ++i)
	{
		const std::string a = _argv[i];
		if(a == "--image" && i + 1 < _argc) image = _argv[++i];
		else if(a == "--bram" && i + 1 < _argc) bram = _argv[++i];
		else if(a == "--card" && i + 1 < _argc) cardPath = _argv[++i];
		else if(a == "--dirty" && i + 1 < _argc) dirty = static_cast<uint32_t>(std::strtoul(_argv[++i], nullptr, 0));
		else if(a == "--exact") exact = true;
		else { std::fprintf(stderr, "usage: aeria_sim --image MAIN.raw [--bram FILE] [--card FILE] [--dirty SEED] [--exact]\n"); return 2; }
	}
	const auto img = readFile(image);
	if(img.empty()) { std::fprintf(stderr, "cannot read %s\n", image.c_str()); return 1; }

	ot::Machine m(img);
	// AERIA OS's memory beyond what stock's map declares: the rest of the SDRAM (samples, the stack), the uncached
	// alias USB uses, and the battery RAM (1 MB at 0x10000000)
	m.mapRegion(0x42000000, 0x04000000);
	m.mapRegion(0x4f000000, 0x01000000);
	m.mapRegion(0x10000000, 0x000b0000);
	m.mapRegion(0x100c0000, 0x00040000);
	if(!bram.empty())
	{
		const auto b = readFile(bram);			// a dump of 0x10000000.. (1 MB), or of 0x100C0000.. (the 192 KB state backup)
		const uint32_t base = b.size() <= 0x40000 ? 0x100c0000 : 0x10000000;
		for(size_t i = 0; i < b.size() && i < 0x100000; ++i) m.write8(base + static_cast<uint32_t>(i), b[i]);
	}

	ot::DspPair dsp(ot::DspPair::g_dspIps / ot::DspPair::g_cfIps, ot::DspPair::g_dspIps, false);
	dsp.setIdleSkip(false);
	dsp.setAudioCapture(true);
	dsp.setAudioInputFromBoot(true);
	if(dirty) dsp.dirty(dirty);
	HostPort port(dsp);
	m.setCoprocessor(&port);

	Sim s{m, dsp};
	if(!cardPath.empty())
	{
		auto bytes = readFile(cardPath);
		if(bytes.empty()) { std::fprintf(stderr, "cannot read card %s\n", cardPath.c_str()); return 1; }
		s.card = std::make_unique<ot::AtaCard>(std::move(bytes));
	}
	m.setPeripheralHandlers(
		[&s](uint32_t a, uint8_t sz, uint32_t& out) { return s.read(a, sz, out); },
		[&s](uint32_t a, uint8_t sz, uint32_t v) { s.write(a, sz, v); });
	// the pair's work is booked a ColdFire instruction at a time and run in chunks of up to a sample's worth at every
	// host-port access (and every 256 ColdFire instructions): the same schedule, about three times as fast. --exact
	// runs it tick by tick.
	if(!exact) dsp.setLazy(ot::DspPair::g_lazyDefault);

	std::vector<int32_t> input;		// the four inputs, as slots C D A B, from the DSP's first receive frame
	size_t audioTaken = 0;
	std::string stop;
	const auto run = [&](const double _ms)
	{
		const uint64_t target = s.vinstr() + static_cast<uint64_t>(_ms * 44.1 * g_cfIps);
		while(s.vinstr() < target && stop.empty())
		{
			if(!m.stepFast()) { stop = "illegal: " + m.why(); break; }
			if(!(m.instructions() & 255)) dsp.sync();
			if(s.reset) { stop = "soft reset (the OS handed back)"; break; }
		}
		dsp.sync();
	};

	std::printf("ready image=%s bytes=%zu\n", image.c_str(), img.size());
	std::fflush(stdout);
	std::string line;
	while(std::getline(std::cin, line))
	{
		std::istringstream is(line);
		std::string cmd;
		is >> cmd;
		std::ostringstream out;
		if(cmd == "run")
		{
			double ms = 0; is >> ms; run(ms);
			out << "ok ms=" << static_cast<double>(s.vinstr()) / (44.1 * g_cfIps) << " pc=" << std::hex << m.pc() << std::dec << (stop.empty() ? "" : " stop=" + stop);
		}
		else if(cmd == "key") { int c = 0, d = 0; is >> std::hex >> c >> std::dec >> d; s.key(c, d != 0); out << "ok"; }
		else if(cmd == "enc") { int n = 0, d = 0; is >> n >> d; s.panel.rx.push_back(static_cast<uint8_t>(0x30 | (n & 15))); s.panel.rx.push_back(static_cast<uint8_t>(d)); out << "ok"; }
		else if(cmd == "xfade") { int v = 0; is >> v; s.xfadeNow = v & 0xff; s.panel.rx.push_back(0x40); s.panel.rx.push_back(static_cast<uint8_t>(v)); out << "ok"; }
		else if(cmd == "midi") { std::string h; is >> h; for(const auto b : unhex(h)) s.midi.rx.push_back(b); out << "ok"; }
		else if(cmd == "midiout")
		{
			out << "midiout";
			for(size_t i = 0; i < s.midi.tx.size(); ++i) out << ' ' << s.midi.txAt[i] << ':' << static_cast<unsigned>(s.midi.tx[i]);
			s.midi.tx.clear(); s.midi.txAt.clear();
		}
		else if(cmd == "lcd")
		{
			std::vector<uint8_t> b;
			for(int p = 0; p < 8; ++p) for(int x = 0; x < 128; ++x) b.push_back(s.lcd[p][x]);
			out << "lcd " << hex(b);
		}
		else if(cmd == "leds")
		{
			std::vector<uint8_t> b(s.ledLevel, s.ledLevel + 256);
			b.insert(b.end(), s.ledRow, s.ledRow + 17);
			out << "leds " << hex(b);
		}
		else if(cmd == "tx") { out << "tx n=" << s.panel.tx.size() << " lcdblocks=" << s.lcdBlocks << " ledmsgs=" << s.ledMsgs << " unknown=" << s.panelUnknown; s.panel.tx.clear(); s.panel.txAt.clear(); }
		else if(cmd == "codec")
		{
			out << "codec";
			for(const auto& w : s.codecWrites) out << ' ' << std::hex << static_cast<unsigned>(w.first) << '=' << static_cast<unsigned>(w.second) << std::dec;
		}
		else if(cmd == "peek")
		{
			uint32_t a = 0, n = 0; is >> std::hex >> a >> std::dec >> n;
			std::vector<uint8_t> b;
			for(uint32_t i = 0; i < n && i < 0x100000; ++i) b.push_back(m.read8(a + i));
			out << "peek " << hex(b);
		}
		else if(cmd == "poke")
		{
			uint32_t a = 0; std::string h; is >> std::hex >> a >> h;
			const auto b = unhex(h);
			for(size_t i = 0; i < b.size(); ++i) m.write8(a + static_cast<uint32_t>(i), b[i]);
			out << "ok";
		}
		else if(cmd == "dsp")
		{
			int core = 0; char sp = 'X'; uint32_t a = 0, n = 0; is >> core >> sp >> std::hex >> a >> std::dec >> n;
			dsp.sync();
			out << "dsp" << std::hex;
			for(uint32_t i = 0; i < n && i < 65536; ++i) out << ' ' << dsp.peekWord(core, sp, a + i);
		}
		else if(cmd == "input")
		{
			// a raw file of int32 little-endian frames, four a frame: inputs A B C D; placed from receive frame N
			std::string p; uint64_t at = 0; is >> p >> at;
			const auto b = readFile(p);
			const size_t frames = b.size() / 16;
			if(input.size() < (at + frames) * 4) input.resize((at + frames) * 4, 0);
			for(size_t f = 0; f < frames; ++f)
			{
				int32_t v[4]; std::memcpy(v, b.data() + f * 16, 16);
				int32_t* o = &input[(at + f) * 4];
				o[0] = v[2]; o[1] = v[3]; o[2] = v[0]; o[3] = v[1];		// slots: C D A B
			}
			dsp.sync();
			dsp.setAudioInput(input, 4);
			out << "ok frames=" << frames;
		}
		else if(cmd == "rxframes")
		{
			// the receive frames so far cannot be asked of the pair directly: the transmit side's capture runs with it
			dsp.sync();
			out << "rxframes " << dsp.audioOut(0).size() / ot::DspPair::g_audioSlots;
		}
		else if(cmd == "audio")
		{
			// everything core 0 sent since the last call: int32 little-endian, eight slot words a frame
			// (0/1 cue, 2/3 main, 4/5 phones, 6/7 unused), to FILE
			std::string p; is >> p;
			dsp.sync();
			const auto& cap = dsp.audioOut(0);
			std::ofstream f(p, std::ios::binary);
			if(cap.size() > audioTaken)
				f.write(reinterpret_cast<const char*>(cap.data() + audioTaken), static_cast<std::streamsize>((cap.size() - audioTaken) * sizeof(int32_t)));
			out << "audio frames=" << (cap.size() - audioTaken) / ot::DspPair::g_audioSlots << " total=" << cap.size() / ot::DspPair::g_audioSlots;
			audioTaken = cap.size();
		}
		else if(cmd == "status")
		{
			dsp.sync();
			out << "status ms=" << static_cast<double>(s.vinstr()) / (44.1 * g_cfIps) << " instr=" << m.instructions() << " pc=" << std::hex << m.pc()
				<< " dsp0pc=" << dsp.pc(0) << " dsp1pc=" << dsp.pc(1) << std::dec
				<< " dsp0boot=" << dsp.bootFinished(0) << " dsp1boot=" << dsp.bootFinished(1)
				<< " dsp0fault=" << dsp.faulted(0) << " dsp1fault=" << dsp.faulted(1)
				<< " hostin0=" << dsp.hostWordsIn(0) << " hostout0=" << dsp.hostWordsOut(0)
				<< " frames=" << dsp.audioOut(0).size() / ot::DspPair::g_audioSlots
				<< " unkR=" << s.unknownReads.size() << " unkW=" << s.unknownWrites.size()
				<< (stop.empty() ? "" : " stop=" + stop);
		}
		else if(cmd == "rot")
		{
			// the ESAI slot the output ring's word 0 leaves in (so ring word w is in slot (w - rot) & 7... as Octabam's
			// -9 rule has it: rot = (DSR2 - 9) & 7 when a frame completes), how often it changed, and the frame it last did
			dsp.sync();
			out << "rot " << g_aeriaRot << " changes=" << g_aeriaRotChanges << " lastframe=" << g_aeriaRotFrame;
		}
		else if(cmd == "unknown")
		{
			out << "unknown" << std::hex;
			for(const auto& [a, n] : s.unknownReads) out << " R" << a << "x" << std::dec << n << std::hex;
			for(const auto& [a, n] : s.unknownWrites) out << " W" << a << "x" << std::dec << n << std::hex;
		}
		else if(cmd == "report") { dsp.sync(); std::string r = dsp.report(); for(auto& c : r) if(c == '\n') c = '|'; out << "report " << r; }
		else if(cmd == "bramsave")
		{
			std::string p; is >> p;
			std::ofstream f(p, std::ios::binary);
			for(uint32_t i = 0; i < 0x100000; ++i) { const char c = static_cast<char>(m.read8(0x10000000 + i)); f.write(&c, 1); }
			out << "ok";
		}
		else if(cmd == "quit") { std::printf("ok\n"); std::fflush(stdout); return 0; }
		else out << "err unknown command";
		std::printf("%s\n", out.str().c_str());
		std::fflush(stdout);
	}
	return 0;
}
