"""AERIA ROSE -- modal resonator tuned by rose-window geometry and cymatics (8 modes, no delay buffer)."""
import json
from pathlib import Path

from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_NOTES = ('C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B')
_ROOT = tuple(f"{_NOTES[i % 12]}{1 + i // 12}" for i in range(61))        # C1 .. C6
_BLANK = Param(b"", 0)
_TABLE = tuple(json.loads((Path(__file__).parent / 'rose_table.json').read_text())['table'])

MODULE = Module(
    name="aeria-rose", key="AERIA ROSE", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/rose/test.py under dsp_host; not flashed",
    doc="Resonator: 8 modes tuned by rose-window geometry, Chladni plates, primes or JI chords.",
    menu=MenuEntry(fx2_id=0x1b, donor_desc=0x400d58b8,      # PROVISIONAL id (everb's preferred id)
                   abbr=b"ROSE", fullname=b"AERIA Rose", build_tag=False),
    params=(
        Param(b"ROOT", 24, count=61, active=True, formatter=Formatter.WIDE_STEPPED,
              labels=_ROOT, doc="root (ratio 1) in semitones, C1 (32.7 Hz) .. C6 (1047 Hz)"),
        Param(b"SHAP", 4, count=6, active=True, formatter=Formatter.STEPPED,
              labels=("HARM", "PRIM", "CIRC", "SQRE", "ROSE", "GOTH"),
              doc="geometry family: harmonics, primes, circular plate, square plate, rose window, JI chord"),
        Param(b"FOLD", 5, count=10, active=True, formatter=Formatter.STEPPED,
              labels=tuple(str(n) for n in range(3, 13)),
              doc="symmetry order n = 3 (trefoil) .. 12 (twelve-fold rose)"),
        Param(b"DECY", 80, active=True, formatter=Formatter.PLAIN,
              doc="ring time to -60 dB, 50 ms .. 12 s (log)"),
        Param(b"BRIT", 64, active=True, formatter=Formatter.PLAIN,
              doc="decay tilt: 0 high modes die fast (stone), 64 even, 127 high modes ring on (glass)"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"SPRD", 90, active=True, formatter=Formatter.PLAIN,
              doc="stereo: 0 mono, 127 even modes left / odd modes right"),
        _BLANK,
        Param(b"ROTA", 0, active=True, formatter=Formatter.PLAIN,
              doc="light moving round the panes: 0 off, then depth to 16 and rate up to 3 Hz"),
        _BLANK,
        Param(b"TUNE", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("OPEN", "CLSE"), doc="OPEN: ratios as the geometry gives; CLSE: folded into 2 octaves"),
        _BLANK,
    ),
    dsp=DspSection(asm="rose/rose.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None, ptable=_TABLE),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'SHAP': 2, 'FOLD': 9, 'DECY': 127, 'BRIT': 127, 'ROTA': 127, 'SPRD': 127, 'MIX': 127},
)
