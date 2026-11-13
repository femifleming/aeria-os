#!/usr/bin/env python3
"""AERIA OS effects: assemble every AERIA effect (aeria-fx/*/manifest.py + its .asm) for AERIA OS's own DSP
engine, and write the ColdFire's table of them.

Each effect is assembled at its own P address in the shared window behind a 4-word entry stub
(`jclr #0,x:<$3f,init` at +0, `... proc` at +2: x:$3f is the engine's always-zero word), so the engine's chain
calls org+0 once (init) and org+2 every frame (proc), with stock's FX ABI (r0 block, n7 frames, r6 knobs,
r7 state, X:$213 -> a word holding the Y buffer base).

Writes: ../dsp/fx_<name>.bin/.lst, ../src/fx_table.inc (C: names, knobs, counts, default knob words, org, Y base).
Run: python3 gen_fx.py   (assembles in the modwerk-source-tools image)"""
import os, sys, types, subprocess, re

HERE = os.path.dirname(os.path.abspath(__file__))
FX = os.path.expanduser('~/octa/aeria-fx')
DSP = os.path.join(HERE, '..', 'dsp')
DOCKER = '/Applications/Docker.app/Contents/Resources/bin/docker'

# the effects, their P address and their Y buffer (aligned for the effect's modulo ring): see MEMORY below
EFFECTS = [  # folder, org, ybase (None = no buffer), core (0, 1, or 2 = either: no buffer, or one both cores reach), cost
    # cost: instructions a sample at the dearest settings, as each effect's own gate measured it (dsp_host)
    ('pitch',  0x34000, 0x38000, 2, 336),   # stereo micro-detune (WIDE): the right side has its own pair of heads (was 204)
    ('disto',  0x34800, None, 2, 152),
    ('grains', 0x35000, 0x00000, 0, 330),   # 35,280 words (800 ms), wrapped in code: core 0's Y:$0000-$89cf
    ('ringph', 0x35800, None, 2, 236),
    ('reso',   0x36000, 0x0A000, 0, 243),   # DTND stereo out (was 215)
    ('spectral', 0x36800, 0x3C000, 2, 1165),  # the FFT SMEAR (12 Nov 2026; 1,024-point STFT, mean 1,065 / worst call 1,137): Y:$3C000-$3E3FF, $3E400-$3FFFF free
    ('rose',   0x37000, None, 2, 205),
    # code in the shared window below the engine
    ('comp',   0x30000, None, 2, 223),
    ('verb',   0x30800, 0x00000, 1, 1145),
    # 12 Nov 2026: ECHO is now Chloaelis' dual delay (aeria-fx/echo2), in the old ECHO's row, slot and memory (saved rows keep
    # their meaning: the name stays AERIA ECHO). 1,378 words with the stub; worst block 310.2
    ('echo2',  0x31000, 0x08000, 1, 311),   # its right line: core 1's X:$4000-$7fff (fixed in the code)
    ('vowel',  0x31800, None, 2, 657),      # WIDE: a coefficient set a side, tuned on alternate calls (was 648)
    # the weather's own effects (Balloon Shop 1.2's MoreWeather; 12 Nov 2026): two to a 2K slot where the boot stages
    # sat (P:$32000-$32fff: only used at start-up) and the last effect slot. Their buffers: core 0's Y:$8a00-$9fff
    # (WIND or FOG, never both), core 1's X:$2000-$3fff (HAIL or AURORA, never both) and X:$8000-$8fff (TWISTER)
    ('wind',    0x32000, 0x09000, 0, 119),
    ('fog',     0x32400, 0x08a00, 0, 126),
    ('twister', 0x32800, 0x08000, 1, 190),
    ('hail',    0x32C00, 0x02000, 1, 134),
    ('aurora',  0x37800, 0x02000, 1, 216),
    # 12 Nov 2026: Chloaelis' multimode filter (TPT SVF, LP > BP > HP > NOTCH, a cutoff a side: SKEW and the ColdFire's
    # Lorenz words r6+6 / r6+7). No buffer: either core. Code in the last 1K of P:$37800's slot, after AURORA (926 words)
    ('filt',    0x37C00, None, 2, 129),     # aeria-fx/filt: mean 126.3, worst call 128.1 (the first call)
    # 12 Nov 2026: WARM (a soft clipper, no buffer, either core) in the free half of PITCH's slot; Chloaelis' three delays
    # (core 1 only, a full 2K each) in the shared window above the FFT SMEAR's memory (Y:$3c000-$3e3ff), P:$3e800-$3ffff
    ('warm',    0x34400, None, 2, 150),     # aeria-fx/warm: 811 words with the stub; worst block 149.8
    ('bbd',     0x3F000, 0x02000, 1, 780),  # core 1's X:$2000-$3fff (the base word used as an X address): never with HAIL or AURORA
    ('pt2399',  0x3F800, 0x02000, 1, 970),  # core 1's X:$2000-$2fff (as an X address; moved from X:$8000 13 Nov 2026: that region is unproven on core 1): never with BBD, HAIL, AURORA
    ('tape',    0x3E800, 0x08000, 1, 388),  # ECHO's memory exactly (Y:$8000-$bf7f, X:$4000-$7eff): never with ECHO
]
MEMORY = """core 0: P:$33000 engine, P:$34000-$37fff effects (2K words each), Y:$0000-$89cf GRAINS (800 ms), Y:$a000-$bfff RESO
segments, shared window Y:$38000-$3bdfd PITCH ring, Y:$3c000-$3fdfd SMEAR ring; P:$33a00-$33fff core 1's slot tables
core 1 (its own X and Y, each core: Y $0-$bfff, X $0-$8fff, measured 10 Nov 2026): P:$30000-$32fff effects (shared),
Y:$0000-$7fff VERB ring, Y:$8000-$bfff ECHO left, X:$4000-$7fff ECHO right
P:$37800 AURORA (926 words), P:$37c00-$37fff FILT (830 words with the stub; no buffer)
P:$34400-$347ff WARM (811 words, after PITCH's 802); shared window P:$3e400-$3e7ff free, P:$3e800 TAPE (2,045), P:$3f000 BBD
(1,785), P:$3f800 PT2399 (1,671): above the FFT SMEAR's Y:$3c000-$3e3ff, in the same 8K block as its last 1K
core 1 X:$2000-$3fff HAIL or AURORA or BBD; X:$8000-$8fff TWISTER or PT2399; Y:$8000-$bfff + X:$4000-$7fff ECHO or TAPE"""

# a stand-in for octabam's remix.schema: just keep what the manifests pass
class _Obj:
    def __init__(self, *a, **k): self.a = a; self.k = k
    def __getattr__(self, n): return self.k.get(n)
class _Enum:
    def __getattr__(self, n): return n
schema = types.ModuleType('remix.schema')
for n in ('Proof', 'BusRole', 'Claims', 'DspRange', 'DspSection', 'Formatter', 'Harness', 'Kind', 'MenuEntry', 'ModeView', 'Module', 'YBase', 'Category'):
    setattr(schema, n, _Obj)
for n in ('Category', 'Proof', 'BusRole', 'Formatter', 'Kind', 'YBase'):
    setattr(schema, n, _Enum())
class Param:
    def __init__(self, name=b"", default=0, count=128, active=False, **k): self.name = name; self.default = default; self.count = count; self.active = active; self.k = k
schema.Param = Param
remix = types.ModuleType('remix'); remix.schema = schema
sys.modules['remix'] = remix; sys.modules['remix.schema'] = schema

def manifest(folder):
    mp = os.path.join(FX, folder, 'manifest.py')
    ns = {'__file__': mp, '__name__': 'manifest_' + folder}
    src = open(mp).read()
    exec(compile(src, folder + '/manifest.py', 'exec'), ns)
    return ns['MODULE']

# a pinned source where the effect's own folder is ahead of this OS's memory map: GRAINS' 800 ms ring (35,280 words)
# needs core 0's Y:$0000 (PITCH, SMEAR and RESO moved first); until then the 360 ms one at Y:$38000
ASM_PIN = {}
# ---- what every knob's value reads as on the screen: its real unit, from the effect's own law (the UI's slot_text
# copies the string; the 3x5 font has A-Z 0-9 + - : . ( ) _ ' & / # %). Mode-dependent knobs read as their first mode.
import math
def _n(x, unit=''):          # three significant figures at most, no trailing zeros
    if x >= 100: t = '%d' % round(x)
    elif x >= 10: t = ('%.1f' % x).rstrip('0').rstrip('.')
    else: t = ('%.2f' % x).rstrip('0').rstrip('.')
    return t + unit
def _ms(ms): return _n(ms / 1000, 'S') if ms >= 1000 else _n(ms, 'MS')
def _hz(f): return _n(f / 1000, 'KHZ') if f >= 1000 else _n(f, 'HZ')
def _pct(v, top=100): return '%d%%' % round(v * top / 127)
def _sg(x, unit=''): return ('+' if x > 0 else '-' if x < 0 else '') + _n(abs(x), unit)
def _pole(c): return 'OPEN' if c >= 0.9999 else _hz(-math.log(1 - c) * 44100 / (2 * math.pi))
_NOTE = ('C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B')
def _note(m): return '%s%d' % (_NOTE[m % 12], m // 12 - 1)
def _belw(v):
    if v == 64: return 'OFF'
    if v > 64: return 'EXP 1:' + _n(1 + 7 * (v - 64) / 63)
    return 'UP ' + _n(1 + 9 * (64 - v) / 64) + ':1'
def _vtone(v):
    if v < 64: return 'LP ' + _hz(20000 * 10 ** (1.82 * (v - 64) / 64))
    return 'HP ' + _hz(500 * 10 ** ((v - 64) / 63))
def _vowl(v):
    p = v * 4 / 127; i = min(int(p), 3); f = p - i
    return 'AEIOU'[i] if f < 0.02 else 'AEIOU'[i + 1] if f > 0.98 else '%s-%s %d%%' % ('AEIOU'[i], 'AEIOU'[i + 1], round(100 * f))
MIXL = lambda v: _pct(v)
def _fmode(v):               # FILT's MODE: morph = 3 v/126 (the font has no '>': a blend reads LP/BP 40%)
    m = min(3.0, 3 * v / 126); i = min(int(m), 2); f = m - i
    nm = ('LP', 'BP', 'HP', 'NT'); full = ('LP', 'BP', 'HP', 'NOTCH')
    if f < 0.005: return full[i]
    if f > 0.995: return full[i + 1]
    return '%s/%s %d%%' % (nm[i], nm[i + 1], round(100 * f))
def _skew(v): return 'SAME' if v == 64 else 'X' + _n(2 ** ((v - 64) / 64))   # the delays' right time = left x 2^((v-64)/64)
def _offpct(v): return 'OFF' if v == 0 else _pct(v)
def _life(s): return _n(s, 'S') if s < 60 else _n(s / 60, 'MIN') if s < 3600 else _n(s / 3600, 'H')
import importlib.util as _ilu
_sp = _ilu.spec_from_file_location('spectral_gen', os.path.join(FX, 'spectral', 'gen.py')); SPG = _ilu.module_from_spec(_sp)
try: _sp.loader.exec_module(SPG)
except SystemExit: pass
LAW = {
 ('wind', 'AMT'): lambda v: _pct(v), ('wind', 'DPTH'): lambda v: _n(1200 * math.log2(1 + 0.012 * v / 64), 'C'),
 ('wind', 'GUST'): lambda v: _ms(3500 * 2 ** ((64 - v) / 16)), ('wind', 'LEAN'): lambda v: '%d%%' % round(v * 100 / 64),
 ('fog', 'AMT'): lambda v: _pct(v), ('fog', 'BLUR'): lambda v: 'G ' + _n(0.6 * v / 100),
 ('fog', 'DARK'): lambda v: 'OPEN' if v == 0 else _hz(18000 * 2 ** (-v / 32)),
 ('twister', 'AMT'): lambda v: 'GO' if v >= 64 else 'ARM', ('twister', 'RISE'): lambda v: _sg(v),
 ('twister', 'SPIN'): lambda v: _hz(0.3 + 5 * v / 64), ('twister', 'TIME'): lambda v: 'RND' if v == 0 else _ms(2000 * 2 ** (v / 32)),
 ('hail', 'AMT'): lambda v: _pct(v), ('hail', 'RATE'): lambda v: _n(2.2 * 2 ** ((v - 64) / 16)) + '/S',
 ('hail', 'SIZE'): lambda v: 'X' + _n(2 ** ((v - 64) / 64)), ('hail', 'BNCE'): lambda v: _n(min(0.95, max(0.2, 0.70 + (v - 64) / 64 * 0.25))),
 ('hail', 'WDTH'): lambda v: _pct(v),
 ('aurora', 'AMT'): lambda v: _pct(v), ('aurora', 'TIME'): lambda v: _ms(9000 * 2 ** ((v - 64) / 32)),
 ('aurora', 'TONE'): lambda v: _hz(min(11650, 6000 * 2 ** ((v - 64) / 24))), ('aurora', 'SHFT'): lambda v: ('UNIS', '+12', '+19', '+24')[v],
 ('pitch', 'SHFT'): lambda v: _sg(v - 24), ('pitch', 'FINE'): lambda v: _sg(round((v - 64) * 100 / 64), 'C'),
 ('pitch', 'GRN'): lambda v: _ms(10 + 150 * (v / 127) ** 2), ('pitch', 'FDBK'): lambda v: _pct(v, 95),
 ('pitch', 'TONE'): lambda v: 'OPEN' if v == 127 else _pole(0.03 + 0.97 * (v / 127) ** 2),
 ('pitch', 'WIDE'): lambda v: _n(1200 * math.log2(1 + 0.029302 * v / 127), 'C'),
 ('disto', 'DRIV'): lambda v: _sg(20 * math.log10(0.4 / (0.4 - 0.3999 * v / 127)), 'DB') if v else '0DB',
 ('disto', 'TONE'): lambda v: 'OPEN' if v == 127 else _pole(0.01 + 0.99 * v / 127),
 ('disto', 'RATE'): lambda v: _hz(44100 * 100 ** (v / 127 - 1)),
 ('disto', 'LEVL'): lambda v: '-INF' if v == 0 else _sg(round(20 * math.log10(v / 64), 1), 'DB'),
 ('grains', 'SIZE'): lambda v: _ms(10 + 290 * (v / 128) ** 2 + 469 * (v / 128) ** 4), ('grains', 'DENS'): lambda v: 'X' + _n(1 + 3 * v / 127),
 ('grains', 'POS'): lambda v: _ms((48 + 35181 * v / 128) / 44.1), ('grains', 'SPRY'): lambda v: _pct(v), ('grains', 'PTCH'): lambda v: _sg(v - 24),
 ('ringph', 'FREQ'): lambda v: _hz(20 * 250 ** (v / 127)), ('ringph', 'FOLW'): lambda v: 'X' + _n(1 + 6 * v / 127),
 ('ringph', 'SPRD'): lambda v: '%dDEG' % round(90 * v / 127), ('ringph', 'BIAS'): lambda v: _pct(v), ('ringph', 'CNTR'): lambda v: _hz(200 * 25 ** (v / 127)),
 ('reso', 'PTCH'): lambda v: _note(30 + v), ('reso', 'FINE'): lambda v: _sg(round((v - 64) * 50 / 64), 'C'),
 ('reso', 'DECY'): lambda v: _ms(20 * 1000 ** (v / 127)), ('reso', 'TONE'): lambda v: _pct(v), ('reso', 'DTUN'): lambda v: '+' + _n(25 * v / 127, 'C'),
 ('spectral', 'SMR'): lambda v: 'OFF' if v == 0 else _ms(1000 * SPG.smr_t60(v)), ('spectral', 'FRZ'): lambda v: 'HOLD' if v == 127 else _pct(v),
 ('spectral', 'GATE'): lambda v: 'OFF' if v == 0 else _n(SPG.gate_db(v), 'DB'), ('spectral', 'WIDE'): lambda v: _pct(v),
 ('spectral', 'TILT'): lambda v: 'FLAT' if v == 64 else _sg(round((SPG.tilt_db(v, 2000) - SPG.tilt_db(v, 500)) / 2, 1), 'DB'),
 ('rose', 'ROOT'): lambda v: _note(24 + v), ('rose', 'SHAP'): lambda v: ('HARM', 'PRIM', 'CIRC', 'SQRE', 'ROSE', 'GOTH')[v],
 ('rose', 'FOLD'): lambda v: '%d-FOLD' % (v + 3), ('rose', 'DECY'): lambda v: _ms(50 * 240 ** (v / 127)),
 ('rose', 'BRIT'): lambda v: 'EVEN' if v == 64 else ('STONE ' if v < 64 else 'GLASS ') + '%d' % abs(v - 64),
 ('comp', 'THR'): lambda v: _sg(round(-60 * v / 127, 1), 'DB') if v else '0DB', ('comp', 'RATI'): lambda v: _n(20 ** (v / 127)) + ':1',
 ('comp', 'BELW'): _belw, ('comp', 'ATK'): lambda v: _ms(0.05 * 4000 ** (v / 127)), ('comp', 'REL'): lambda v: _ms(5 * 400 ** (v / 127)),
 ('verb', 'DECY'): lambda v: _ms(300 * 100 ** (v / 127)), ('verb', 'TONE'): _vtone, ('verb', 'MOD'): lambda v: _n(24.7 * v / 127 / 44.1, 'MS'),
 ('echo', 'TIME'): lambda v: _ms(220 * 22 ** -(1 - v / 127)), ('echo', 'FDBK'): lambda v: _pct(v, 95), ('echo', 'TONE'): lambda v: _hz(300 * 60 ** (v / 127)),
 ('echo', 'RATO'): lambda v: ('JUST', 'MINR', 'PYTH', 'SEPT', 'HARM', 'UNIS')[v], ('echo', 'WDTH'): lambda v: _pct(v),
 ('vowel', 'VOWL'): _vowl, ('vowel', 'SHFT'): lambda v: _sg(round((v - 64) * 0.375 if v < 127 else 24, 1)),
 # the FUNC layer (page 2)
 ('disto', 'MODE'): lambda v: ('DIST', 'CLIP', 'DGRD')[v], ('ringph', 'MODE'): lambda v: ('RING', 'PHSR')[v],
 ('reso', 'MODE'): lambda v: ('WAVE', 'CPLX', 'DTND')[v], ('grains', 'FRZ'): lambda v: ('THAW', 'HOLD')[v], ('grains', 'REV'): lambda v: _pct(v),
 ('rose', 'SPRD'): lambda v: _pct(v), ('rose', 'ROTA'): lambda v: 'OFF' if v == 0 else '%d' % v, ('rose', 'TUNE'): lambda v: ('OPEN', 'CLSE')[v],
 ('comp', 'MAKE'): lambda v: '+' + _n(24 * v / 127, 'DB'),
 ('vowel', 'RES'): lambda v: 'X' + _n(0.1 * 80 ** (v / 127)), ('vowel', 'LEVL'): lambda v: ('OFF', 'ON')[v],
 ('grains', 'SPRD'): lambda v: _pct(v), ('vowel', 'WIDE'): lambda v: _pct(v),
 # FILT (12 Nov 2026): cutoff, resonance, morph, the right side's offset, the Lorenz depth; FUNC: the ColdFire's Lorenz
 ('filt', 'CUT'): lambda v: _hz(20 * 1000 ** (v / 127)), ('filt', 'RES'): lambda v: _pct(v), ('filt', 'MODE'): _fmode,
 ('filt', 'SKEW'): lambda v: 'SAME' if v == 64 else _sg((v - 64) / 16, 'OCT'),
 ('filt', 'LRNZ'): lambda v: 'OFF' if v == 0 else _pct(v),
 ('filt', 'LZSP'): lambda v: _pct(v), ('filt', 'CHAS'): lambda v: 'RHO ' + _n(24.5 + 155.5 * (v / 127) ** 2),
 ('filt', 'SHAP'): lambda v: 'BETA ' + _n(0.5 + 3.5 * v / 127),
 # ECHO, 2nd version (12 Nov 2026, Chloaelis' dual delay): FDBK is the loop gain (over unity past 115, held by the rider)
 ('echo2', 'TIME'): lambda v: _ms(30 * (37 / 3) ** (v / 127)),
 ('echo2', 'FDBK'): lambda v: '%d%%' % round(100 * (0.99 * v / 115 if v <= 115 else 0.99 + 0.2175 * (v - 115) / 12)),
 ('echo2', 'TONE'): lambda v: 'OPEN' if v == 127 else _hz(300 * 60 ** (v / 127)), ('echo2', 'SKEW'): _skew,
 ('echo2', 'SPRD'): lambda v: _pct(v), ('echo2', 'HPF'): lambda v: 'OFF' if v == 0 else _hz(320 * 2 ** ((v - 90) / 22)),
 ('echo2', 'FRZ'): lambda v: ('OFF', 'HOLD')[v], ('echo2', 'RFRZ'): lambda v: _pct(v), ('echo2', 'TMOD'): lambda v: ('SLEW', 'FADE')[v],
 ('echo2', 'GLID'): lambda v: _hz(1.6 * 2 ** ((v - 64) / 24)),
 # WARM (12 Nov 2026): drive into a soft clipper, a lowpass after, a little bias, an output level
 ('warm', 'HEAT'): lambda v: _sg(24 * v / 127, 'DB'), ('warm', 'TONE'): lambda v: 'OPEN' if v == 127 else _hz(1000 * 18 ** (v / 127)),
 ('warm', 'BIAS'): lambda v: _pct(v), ('warm', 'SHAP'): lambda v: ('SOFT', 'TAPE')[v],
 ('warm', 'LEVL'): lambda v: 'OFF' if v == 0 else _sg(round(20 * math.log10(v / 64), 1), 'DB'),
 # BBD (12 Nov 2026): FDBK is the regen (the loop gain is 1.05 x it)
 ('bbd', 'TIME'): lambda v: _ms(47 * (1000 / 47) ** (v / 127)), ('bbd', 'SKEW'): _skew, ('bbd', 'FDBK'): lambda v: _pct(v, 115),
 ('bbd', 'TONE'): lambda v: _hz(6800 * 2 ** ((v - 100) / 24)), ('bbd', 'MOD'): _offpct,
 ('bbd', 'HPF'): lambda v: ('OFF', 'ON')[v], ('bbd', 'FRZ'): lambda v: ('OFF', 'HOLD')[v], ('bbd', 'RFRZ'): _offpct,
 ('bbd', 'TMOD'): lambda v: ('SLEW', 'FADE')[v], ('bbd', 'RATE'): lambda v: _hz(0.02 * 1000 ** (v / 127)),
 # PT2399 (12 Nov 2026): TONE scales the chip's clock-following bandwidth; GRIT is the CVSD step (64 = the plugin's)
 ('pt2399', 'TIME'): lambda v: _ms(50 * 20 ** (v / 127)), ('pt2399', 'SKEW'): _skew, ('pt2399', 'FDBK'): lambda v: _pct(v, 115),
 ('pt2399', 'TONE'): lambda v: 'X' + _n(2 ** ((v - 100) / 24)), ('pt2399', 'GRIT'): lambda v: _sg(v - 64),
 ('pt2399', 'HPF'): lambda v: ('OFF', 'ON')[v], ('pt2399', 'FRZ'): lambda v: ('OFF', 'HOLD')[v], ('pt2399', 'RFRZ'): _offpct,
 ('pt2399', 'TMOD'): lambda v: ('SLEW', 'FADE')[v], ('pt2399', 'MOD'): _offpct, ('pt2399', 'RATE'): lambda v: _hz(0.02 * 1000 ** (v / 127)),
 # TAPE (12 Nov 2026): FDBK is the regen (0.6 at 115, the plugin's 1.15 at 127); WEAR is the tape's life (new to ruined)
 ('tape', 'TIME'): lambda v: _ms(30 * (70 / 3) ** (v / 127)), ('tape', 'SKEW'): _skew,
 ('tape', 'FDBK'): lambda v: '%d%%' % round(100 * (0.6 * v / 115 if v <= 115 else 0.6 + 0.55 * (v - 115) / 12)),
 ('tape', 'WEAR'): lambda v: 'OFF' if v == 0 else _life(600 * 2 ** ((64 - v) / 16)),
 ('tape', 'WOW'): lambda v: 'OFF' if v == 0 else 'X' + _n(v / 32), ('tape', 'HISS'): lambda v: 'OFF' if v == 0 else 'X' + _n(v / 64),
 ('tape', 'HPF'): lambda v: ('OFF', 'ON')[v], ('tape', 'FRZ'): lambda v: ('OFF', 'HOLD')[v], ('tape', 'RFRZ'): _offpct,
 ('tape', 'TMOD'): lambda v: ('SLEW', 'FADE')[v], ('tape', 'RFSH'): lambda v: ('KEEP', 'NEW')[v],
}
ROSE_TABLE = 0x37400   # P: after ROSE's code (P:$37000, < $400 words), 960 words
def assemble(folder, org, asmfile):
    src = open(os.path.join(FX, asmfile)).read()
    if folder == 'rose':
        assert src.count('#>$fab1e0') == 1
        src = src.replace('#>$fab1e0', '#>$%05x' % ROSE_TABLE)
    stub = "        jclr    #0,x:<$3f,init\n        jclr    #0,x:<$3f,proc\n"
    name = 'fx_' + folder
    open(os.path.join(DSP, name + '.asm'), 'w').write("; generated by aeria-os/fx/gen_fx.py from aeria-fx/%s\n" % asmfile + stub + src)
    r = subprocess.run([DOCKER, 'run', '--rm', '--network', 'none', '--user', '%d:%d' % (os.getuid(), os.getgid()), '-v', DSP + ':/d', '-w', '/d',
                        'modwerk-source-tools', 'sh', '-c',
                        '/opt/toolchain/vendor/dsp56300/build/source/dsp_host/dsp_asm -in %s.asm -org %x -out %s.bin -list > %s.lst 2>&1; cat %s.lst | grep -v "^0" ' % (name, org, name, name, name)],
                       capture_output=True, text=True)
    out = r.stdout.strip()
    m = re.search(r'assembled (\d+) words', out)
    bad = [l for l in out.splitlines() if not l.startswith('assembled') and not l.startswith('wrote')]
    if bad or not m: sys.exit('%s: %s' % (folder, '\n'.join(bad) or out))
    n = int(m.group(1))
    if n > 0x800: sys.exit('%s: %d words > 2K' % (folder, n))
    if folder == 'rose':
        import json
        assert org + n <= ROSE_TABLE, 'ROSE code reaches its table'
        tab = json.load(open(os.path.join(FX, 'rose', 'rose_table.json')))['table']
        b = bytearray(open(os.path.join(DSP, name + '.bin'), 'rb').read())
        b += bytes(3 * (ROSE_TABLE - org - n))
        for v in tab: b += bytes([v & 0xff, v >> 8 & 0xff, v >> 16 & 0xff])
        open(os.path.join(DSP, name + '.bin'), 'wb').write(b)
        n = ROSE_TABLE - org + len(tab)
    lst = open(os.path.join(DSP, name + '.lst')).read()
    if re.search(r'mpysu|macsu', lst): print('  note: %s has mpysu/macsu (audited in aeria-fx)' % folder)
    return n

rows = []
for folder, org, ybase, core, cost in EFFECTS:
    mod = manifest(folder)
    words = assemble(folder, org, ASM_PIN.get(folder, mod.dsp.asm))
    ps = mod.params
    p1 = []
    for k in range(6):
        p = ps[k]
        p1.append((p.name.decode() if p.active else '', p.count if p.active else 0, p.default if p.active else 0))
    # page 2: knobs 6..11 packed in r6+$c..$e (knob 6+2w at bits 22..16, 7+2w at bits 14..8)
    p2 = []
    for w in range(3):
        a, b = ps[6 + 2 * w], ps[7 + 2 * w]
        p2.append(((a.default & 0x7f) << 16) | ((b.default & 0x7f) << 8))
    p1b = []
    for k in range(6, 12):
        p = ps[k] if k < len(ps) else None
        on = p is not None and getattr(p, 'active', False)
        p1b.append((p.name.decode() if on else '', p.count if on else 0, p.default if on else 0))
    fm = []
    for k in p1 + p1b:
        law = LAW.get((folder, k[0])) or (MIXL if k[0] == 'MIX' else None)
        if k[0] and not law: sys.exit('no display law for %s %s' % (folder, k[0]))
        txt = [law(v) for v in range(k[1])] if law else None
        if txt:
            bad = [t for t in txt if len(t) > 10 or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-:.()_'&/#% " for c in t)]
            if bad: sys.exit('%s %s: cannot show %r' % (folder, k[0], bad[:3]))
        fm.append(txt)
    rows.append((mod.key.decode() if isinstance(mod.key, bytes) else mod.key, org, ybase, p1, p2, words, fm, p1b))
    print('%-12s P:$%05x %4d words  Y:%s' % (rows[-1][0], org, words, ('$%05x' % ybase) if ybase is not None else '-'))

with open(os.path.join(HERE, '..', 'src', 'fx_table.inc'), 'w') as f:
    f.write('/* generated by aeria-os/fx/gen_fx.py from the aeria-fx manifests: AERIA OS\'s effects.\n * %s */\n' % MEMORY.replace('\n', '\n * '))
    f.write('#define FX_COUNT %d\n' % len(rows))
    f.write('/* where each effect may run (0 core 0, 1 core 1, 2 either) and what it costs, instructions a sample */\n')
    f.write('static const uint8_t fx_core[FX_COUNT] = {%s};\n' % ', '.join(str(e[3]) for e in EFFECTS))
    f.write('static const uint16_t fx_cost[FX_COUNT] = {%s};\n' % ', '.join(str(e[4]) for e in EFFECTS))
    f.write('typedef struct { const char *name; uint32_t org, ybase; uint8_t has_y; const char *knob[6]; uint8_t count[6], def[6]; uint32_t page2[3]; const char *const *text[6];\n                 const char *knob2[6]; uint8_t count2[6], def2[6]; const char *const *text2[6]; } FxInfo;   /* knob2..: the FUNC layer (page 2) */\n')
    for i, (name, org, yb, p1, p2, _, fm, p1b) in enumerate(rows):
        for k, txt in enumerate(fm):
            if txt: f.write('static const char *const fxv_%d_%d[%d] = {%s};\n' % (i, k, len(txt), ', '.join('"%s"' % t for t in txt)))
    f.write('static const FxInfo fx_info[FX_COUNT] = {\n')
    for i, (name, org, yb, p1, p2, _, fm, p1b) in enumerate(rows):
        f.write('    {"%s", 0x%05xu, 0x%05xu, %d, {%s}, {%s}, {%s}, {%s}, {%s},\n     {%s}, {%s}, {%s}, {%s}},\n' % (name, org, yb or 0, yb is not None,
                ', '.join('"%s"' % k[0] for k in p1), ', '.join(str(k[1]) for k in p1), ', '.join(str(k[2]) for k in p1),
                ', '.join('0x%06xu' % w for w in p2), ', '.join(('fxv_%d_%d' % (i, k)) if fm[k] else '0' for k in range(6)),
                ', '.join('"%s"' % k[0] for k in p1b), ', '.join(str(k[1]) for k in p1b), ', '.join(str(k[2]) for k in p1b),
                ', '.join(('fxv_%d_%d' % (i, k)) if fm[k] else '0' for k in range(6, 12))))
    f.write('};\n')
    for name, org, *_ in rows:
        n = 'fx_' + [e[0] for e in EFFECTS if e[1] == org][0]
        b = open(os.path.join(DSP, n + '.bin'), 'rb').read()
        w = [b[i] | b[i + 1] << 8 | b[i + 2] << 16 for i in range(0, len(b), 3)]
        f.write('static const uint32_t %s_code[%d] = {%s};\n' % (n, len(w), ','.join('0x%06xu' % x for x in w)))
    f.write('static const uint32_t *const fx_code[FX_COUNT] = {%s};\n' % ', '.join('fx_%s_code' % e[0] for e in EFFECTS))
    f.write('static const uint16_t fx_words[FX_COUNT] = {%s};\n' % ', '.join(str(r[5]) for r in rows))
print('wrote src/fx_table.inc')
