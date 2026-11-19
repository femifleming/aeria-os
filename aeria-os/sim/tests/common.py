"""what the simulator tests share: booting, the two faces, the sequence's bytes in battery RAM (src/seq.h), onsets"""
import os, sys, struct, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from aeriasim import *
import numpy as np

IMAGE = os.environ.get('AERIA_IMAGE')            # default: build/flashimg (aeriasim.BUILD)
B = 0x100C0000                                   # the sequence: head, then steps
MT = 0x100CC400                                  # the MIDI tracks
OFF = dict(mode=5, sync=6, pcch=7, swing=8, pattern=9, mutes=10, chain_n=13, chain=14, mlen=22, sel=26, solo=27, nin=46, trk=64, len=320,
           fx_row=352, trk2=464, trk3=637)
TP = dict(SMPL=0, PTCH=1, STRT=2, ATK=3, DEC=4, VOL=5, HP=6, LP=7, PAN=8, DLY=9, MACH=19, SHUF=22, OUT=26, MOUT=27, COUT=28, HOUT=29)
MACH = dict(SMPL=0, SUB=1, FM=2, NOIS=3, PLUK=4, MIDI=5, THRU=6)
MP = dict(CH=0, NOTE=1, VEL=2, GATE=3, CHRD=4, PROG=5, OCT=6, CC1=7, CC1V=8, CC2=9, CC2V=10, FREE=11)
_fails = []

def boot(seq=None, wait=3500, **kw):
    """a unit switched on (uninitialised DSP memory unless told otherwise); seq = True / False puts it in that face"""
    kw.setdefault('dirty', 20261119)
    kw.setdefault('image', IMAGE); kw.setdefault('elf', os.environ.get('AERIA_ELF'))
    s = Sim(**kw)
    s.run(wait)
    if seq is not None and bool(s.u8('seq_on')) != bool(seq):
        s.combo(K['FUNC'], K['MIDI']); s.run(1200)
        assert bool(s.u8('seq_on')) == bool(seq), 'the face did not change'
    return s

def check(name, ok, detail=''):
    print('%s  %s%s' % ('ok  ' if ok else 'FAIL', name, ('   [' + str(detail) + ']') if detail != '' else ''), flush=True)
    if not ok: _fails.append(name)
    return ok

def done(s=None):
    if s is not None:
        st = s.status()
        check('both DSP cores still run', st['dsp0fault'] == '0' and st['dsp1fault'] == '0', st)
        s.close()
    print('RESULT %s: %s' % (os.path.basename(sys.argv[0]), 'PASS' if not _fails else 'FAIL (%d): %s' % (len(_fails), ', '.join(_fails))))
    sys.exit(1 if _fails else 0)

# ---- the sequence ----
def trk(s, t, **p):
    for k, v in p.items(): s.poke(B + OFF['trk'] + 32 * t + TP[k], [v])
def step(s, t, n, on=1, vel=0, micro=0, prob=0, cond=0, ratch=0, locks=(), pat=0, flags=None):
    lp = [0, 0, 0]; lv = [0, 0, 0]
    for i, (p, v) in enumerate(locks): lp[i] = p + 1; lv[i] = v
    f = (1 if on else 0) if flags is None else flags
    s.poke(B + 0x400 + 12 * ((pat * 8 + t) * 64 + n), [f, vel, micro & 0xFF, prob, cond, ratch] + lp + lv)
def clear_seq(s, pats=1):
    s.poke(B + 0x400, bytes(12 * 64 * 8 * pats)); s.poke(MT + 0x400, bytes(8 * 64 * 8 * pats))
def mtrk(s, t, **p):
    for k, v in p.items(): s.poke(MT + 40 + 16 * t + MP[k], [v])
def mstep(s, t, n, on=1, vel=0, micro=0, prob=0, cond=0, ratch=0, lock=None, pat=0):
    lp, lv = (lock[0] + 1, lock[1]) if lock else (0, 0)
    s.poke(MT + 0x400 + 8 * ((pat * 8 + t) * 64 + n), [1 if on else 0, vel, micro & 0xFF, prob, cond, ratch, lp, lv])
def head(s, **p):
    for k, v in p.items(): s.poke(B + OFF[k], [v] if isinstance(v, int) else v)

# ---- reading results ----
def onsets(x, thresh=0.02, gap=0.03):
    """sample indexes where |x| first passes thresh (of full scale) after `gap` seconds of quiet"""
    a = np.abs(np.asarray(x, dtype=np.float64)) / 8388608.0
    hot = np.flatnonzero(a > thresh)
    if not len(hot): return []
    out = [int(hot[0])]; g = int(gap * FS)
    for i, j in zip(hot[:-1], hot[1:]):
        if j - i > g: out.append(int(j))
    return out
def notes(mo):
    """DIN bytes [(ticks, byte)] -> [(seconds, status, d1, d2)] (running status and real-time bytes handled)"""
    out = []; st = 0; d = []
    for t, b in mo:
        if b >= 0xF8: out.append((t / 132e6, b, 0, 0)); continue
        if b & 0x80: st = b; d = []; continue
        d.append(b)
        need = 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
        if len(d) >= need: out.append((t / 132e6, st, d[0], d[1] if need > 1 else 0)); d = []
    return out
