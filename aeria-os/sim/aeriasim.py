"""AERIA OS in the simulator (sim/aeria_sim): the driver the tests use.

    from aeriasim import Sim, K
    s = Sim()                      # the build in build/flashimg (what the last release was made from), the test card
    s.run(4000)                    # milliseconds of the unit's time
    s.tap(K['PLAY']); s.enc(0, 3); s.show()
    out = s.audio()                # numpy int32 [frames, 8]: cue L R, main L R, phones (TX words 4, 5), -, -
    s.u8('seq_on'), s.led(62)      # the OS's own variables by name; a lamp's level 0..15

Everything here is the firmware's own behaviour in a software model of the unit. It says nothing about the analog
stage after the converters (the headphone amplifier and its knob, the output jacks)."""
import os, struct, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, 'build', 'flashimg')
K = dict(TRIG=0x00, TRACK=0x10, TEMPO=0x18, SCENEA=0x19, SCENEB=0x1a, PAGE=0x1b, PROJ=0x1c, PART=0x1d, AED=0x1e, ARR=0x1f,
         DOWN=0x20, RIGHT=0x21, SRC=0x22, AMP=0x23, LFO=0x24, FX1=0x25, FX2=0x26, STOP=0x27, PLAY=0x28, REC=0x29, CUE=0x2a,
         RECAB=0x2b, RECCD=0x2c, FUNC=0x2d, PATTERN=0x2e, BANK=0x2f, MIX=0x30, YES=0x31, NO=0x32, UP=0x33, LEFT=0x34,
         MIDI=0x35, REC3=0x36, PUSH=0x38, LEVELPUSH=0x3e)
FS = 44100
_G = [0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79a7, 0x79a4, 0x396b, 0x5bed, 0x7497, 0x126a, 0x5bad, 0x4927, 0x5fed, 0x6b6d, 0x2b6a, 0x6ba4,
      0x2b73, 0x6bad, 0x388e, 0x7492, 0x5b6f, 0x5b6a, 0x5bfd, 0x5aad, 0x5a92, 0x72a7, 0x7b6f, 0x2c97, 0x62a7, 0x628e, 0x5bc9, 0x798e,
      0x39ef, 0x7292, 0x7bef, 0x7bce, 0x05d0, 0x01c0, 0x0410, 0x0002, 0x2922, 0x224a, 0x0007, 0x2400, 0x2aab, 0x12a4, 0x5f7d, 0x52a5]
_GLYPH = {g: c for g, c in zip(_G, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-:.()_'&/#%")}   # AERIA's 3x5 font (cloudfin/parts/head12.c)
# TX ring words of core 0 (engine.asm's output loop writes them in this order)
CUE_L, CUE_R, MAIN_L, MAIN_R, PH_A, PH_B = 0, 1, 2, 3, 4, 5


def symbols(elf):
    """the ELF's symbols (name -> address), cached beside it"""
    cache = elf + '.sym'
    if not os.path.exists(cache) or os.path.getmtime(cache) < os.path.getmtime(elf):
        env = dict(os.environ, PATH='/Applications/Docker.app/Contents/Resources/bin:' + os.environ['PATH'])
        out = subprocess.run(['docker', 'run', '--rm', '--network', 'none', '-v', os.path.dirname(elf) + ':/o', 'modwerk-source-tools',
                              'm68k-elf-nm', '/o/' + os.path.basename(elf)], capture_output=True, text=True, env=env, check=True).stdout
        open(cache, 'w').write(out)
    sym = {}
    for l in open(cache):
        p = l.split()
        if len(p) == 3: sym.setdefault(p[2], int(p[0], 16))
    return sym


class Sim:
    def __init__(self, image=None, elf=None, bram=None, card='default', dirty=None, quiet=True):
        image = image or os.path.join(BUILD, 'ram', 'MAIN.raw')
        elf = elf or os.path.join(os.path.dirname(os.path.dirname(image)), 'aeria-os.elf')
        if card == 'default': card = os.path.join(HERE, 'card.img')
        a = [os.path.join(HERE, 'aeria_sim'), '--image', image]
        if bram: a += ['--bram', bram]
        if card: a += ['--card', card]
        if dirty: a += ['--dirty', str(dirty)]
        self.p = subprocess.Popen(a, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL if quiet else None, text=True, bufsize=1)
        self.tmp = tempfile.mkdtemp(prefix='aeriasim')
        l = self.p.stdout.readline()
        assert l.startswith('ready'), l
        self.sym = symbols(elf) if os.path.exists(elf) else {}
        self.ms = 0.0
        self.stopped = None

    # ---- the protocol ----
    def cmd(self, line):
        self.p.stdin.write(line + '\n'); self.p.stdin.flush()
        r = self.p.stdout.readline().rstrip('\n')
        if r.startswith('err') or not r: raise RuntimeError('%s -> %r' % (line, r))
        return r

    def close(self):
        try: self.cmd('quit')
        except Exception: pass
        try: self.p.wait(timeout=5)
        except Exception: self.p.kill()
    def __enter__(self): return self
    def __exit__(self, *a): self.close()

    def run(self, ms):
        r = self.cmd('run %g' % ms)
        kv = dict(x.split('=', 1) for x in r.split()[1:] if '=' in x)
        self.ms = float(kv['ms'])
        if 'stop' in r:
            self.stopped = r[r.index('stop=') + 5:]
            raise RuntimeError('the machine stopped: ' + self.stopped)
        return self.ms

    # ---- the panel ----
    def key(self, code, down): self.cmd('key %x %d' % (code, 1 if down else 0))
    def tap(self, code, hold=60, after=120):
        self.key(code, 1); self.run(hold); self.key(code, 0); self.run(after)
    def combo(self, hold, code, after=120):
        self.key(hold, 1); self.run(60); self.tap(code, after=60); self.key(hold, 0); self.run(after)
    def enc(self, n, delta, after=80):
        """a knob: 0..5 = A..F, 6 = LEVEL (the panel reports signed detents)"""
        while delta:
            d = max(-20, min(20, delta)); delta -= d
            self.cmd('enc %d %d' % (n, d & 0xFF)); self.run(after)
    def xfade(self, v, after=80): self.cmd('xfade %d' % v); self.run(after)
    def midi_in(self, data): self.cmd('midi ' + bytes(data).hex())
    def midi_out(self):
        """[(bus ticks at 132 MHz, byte)] sent on DIN since the last call"""
        r = self.cmd('midiout').split()[1:]
        return [(int(a), int(b)) for a, b in (x.split(':') for x in r)]

    def lcd(self):
        """the screen the panel holds: 64 rows of 128 0/1"""
        b = bytes.fromhex(self.cmd('lcd').split()[1])
        rows = np.zeros((64, 128), dtype=np.uint8)
        for p in range(8):
            for x in range(128):
                v = b[p * 128 + x]
                for bit in range(8):
                    if v >> bit & 1: rows[(7 - p) * 8 + bit, x] = 1
        return rows
    def show(self, file=sys.stdout):
        r = self.lcd()
        for y in range(0, 64, 2):
            print(''.join({(0, 0): ' ', (1, 0): '▀', (0, 1): '▄', (1, 1): '█'}[(int(a), int(b))] for a, b in zip(r[y], r[y + 1])).rstrip(), file=file)
    def lit(self): return int(self.lcd().sum())
    def words(self, rows=None):
        """the 3x5 text on the screen: [(y, x, string)], normal or inverted, strings of 4-pixel-pitch glyphs"""
        r = self.lcd() if rows is None else rows
        out = []
        for inv in (0, 1):
            g = (1 - r) if inv else r
            pad = np.zeros((66, 130), dtype=np.uint8); pad[1:65, 1:129] = g
            for y in range(60):
                hits = {}
                for x in range(126):
                    c = pad[y + 1:y + 6, x + 1:x + 4]
                    if pad[y + 1:y + 6, x].any() or pad[y + 1:y + 6, x + 4].any(): continue
                    code = 0
                    for rr in range(5):
                        for cc in range(3): code = code << 1 | int(c[rr, cc])
                    ch = _GLYPH.get(code)
                    if ch: hits[x] = ch
                x = 0
                while x < 126:
                    if x not in hits: x += 1; continue
                    t = hits[x]; x0 = x; x += 4
                    while True:
                        if x in hits: t += hits[x]; x += 4
                        elif x + 4 in hits and not pad[y + 1:y + 6, x + 1:x + 4].any(): t += ' ' + hits[x + 4]; x += 8
                        else: break
                    alnum = sum(ch.isalnum() for ch in t)
                    blank_above = not pad[y, x0 + 1:x].any(); blank_below = not pad[y + 6, x0 + 1:x].any()
                    if alnum >= 2 or (alnum == 1 and blank_above and blank_below):
                        if inv and not (blank_above or blank_below) and alnum < 2: continue
                        out.append((y, x0, ('~' if inv else '') + t))
        # a string found at y is found again, in pieces, a row up or down inside tall glyph runs: keep the longest per place
        out.sort()
        keep = []
        for y, x, t in out:
            if any(abs(y - y2) <= 4 and x2 <= x < x2 + 4 * len(t2) and (y, x) != (y2, x2) and len(t2.strip('~')) > len(t.strip('~')) for y2, x2, t2 in out): continue
            keep.append((y, x, t))
        return keep
    def lines(self):
        """the screen's text, a string a row"""
        rows = {}
        for y, x, t in self.words(): rows.setdefault(y, []).append((x, t))
        return [' | '.join(t for _, t in sorted(v)) for _, v in sorted(rows.items())]
    def read(self): return '\n'.join(self.lines())
    def has(self, text): return text in self.read()

    def leds(self):
        b = bytes.fromhex(self.cmd('leds').split()[1])
        return b[:256], b[256:]
    def led(self, lamp_id):
        """a lamp's level as the panel shows it: 0 when its row bit is off"""
        lv, rows = self.leds()
        on = rows[lamp_id >> 3] >> (lamp_id & 7) & 1 if lamp_id < 136 else 0
        return lv[lamp_id] if on else 0

    # ---- memory ----
    def peek(self, addr, n): return bytes.fromhex(self.cmd('peek %x %d' % (addr, n)).split()[1]) if n else b''
    def poke(self, addr, data): self.cmd('poke %x %s' % (addr, bytes(data).hex()))
    def a(self, name, off=0): return self.sym[name] + off
    def u8(self, name, off=0): return self.peek(self.a(name, off), 1)[0]
    def u16(self, name, off=0): return struct.unpack('>H', self.peek(self.a(name, off), 2))[0]
    def u32(self, name, off=0): return struct.unpack('>I', self.peek(self.a(name, off), 4))[0]
    def i32(self, name, off=0): return struct.unpack('>i', self.peek(self.a(name, off), 4))[0]
    def text(self, name, n=24): return self.peek(self.a(name), n).split(b'\0')[0].decode('latin1')
    def dsp(self, core, space, addr, n=1):
        """DSP memory words (24-bit, unsigned)"""
        return [int(x, 16) for x in self.cmd('dsp %d %s %x %d' % (core, space, addr, n)).split()[1:]]
    def dsps(self, core, space, addr, n=1):
        return [v - (1 << 24) if v & 0x800000 else v for v in self.dsp(core, space, addr, n)]
    def status(self): return dict(x.split('=', 1) for x in self.cmd('status').split()[1:] if '=' in x)
    def rotation(self):
        """(rot, changes, frame of the last change): which way the output ring sits in the converter's eight slots.
        A change after start-up means the ring slipped: every output then comes out of another converter channel."""
        r = self.cmd('rot').split()
        return int(r[1]), int(r[2].split('=')[1]), int(r[3].split('=')[1])
    def codec(self): return [(int(a, 16), int(b, 16)) for a, b in (x.split('=') for x in self.cmd('codec').split()[1:])]

    # ---- audio ----
    def frames(self): return int(self.cmd('rxframes').split()[1])
    def feed(self, chans, at=None):
        """audio into the inputs from receive frame `at` (default: now + a little): chans = float arrays for A B C D
        (-1..1, None = silence), all the same length. Returns the frame it starts at."""
        n = max(len(c) for c in chans if c is not None)
        buf = np.zeros((n, 4), dtype='<i4')
        for i, c in enumerate(chans):
            if c is not None: buf[:len(c), i] = np.clip(np.asarray(c) * 8388607.0, -8388607, 8388607).astype('<i4')
        if at is None: at = self.frames() + 64
        f = os.path.join(self.tmp, 'in.raw'); buf.tofile(f)
        self.cmd('input %s %d' % (f, at))
        return at
    def audio(self):
        """what core 0 sent to the converters since the last call: int32 [frames, 8] (24-bit samples)"""
        f = os.path.join(self.tmp, 'out.raw')
        self.cmd('audio ' + f)
        d = np.fromfile(f, dtype='<i4')
        return d.reshape(-1, 8)


def tone(freq, seconds, level=0.25): return level * np.sin(2 * np.pi * freq * np.arange(int(seconds * FS)) / FS)
def rms_db(x):
    """dBFS of a 24-bit signal's RMS (a full-scale sine reads -3.0)"""
    x = np.asarray(x, dtype=np.float64) / 8388608.0
    r = np.sqrt(np.mean(x * x)) if len(x) else 0.0
    return -200.0 if r <= 0 else 20 * np.log10(r)
def peak_db(x):
    p = np.max(np.abs(np.asarray(x, dtype=np.float64))) / 8388608.0 if len(x) else 0.0
    return -200.0 if p <= 0 else 20 * np.log10(p)
def level_at(x, freq):
    """dBFS of one frequency's component (amplitude of the sine, by correlation)"""
    x = np.asarray(x, dtype=np.float64) / 8388608.0
    n = np.arange(len(x)); w = 2 * np.pi * freq / FS
    a = 2 * np.hypot(np.dot(x, np.sin(w * n)), np.dot(x, np.cos(w * n))) / max(1, len(x))
    return -200.0 if a <= 0 else 20 * np.log10(a)
