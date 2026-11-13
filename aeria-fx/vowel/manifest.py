"""AERIA VOWL -- vowel formant filter (five TPT bandpasses per channel, RMS level match)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-vowl", key="AERIA VOWL", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/vowel/test.py under dsp_host; not flashed",
    doc="Vowel formant filter: five resonant bands morphing a-e-i-o-u, level matched, formants split left/right.",
    menu=MenuEntry(fx2_id=0x0b, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"VOWL", fullname=b"AERIA Vowel", build_tag=False),
    params=(
        Param(b"VOWL", 0, active=True, formatter=Formatter.PLAIN,
              doc="vowel, continuous: 0 a, 32 e, 64 i, 95 o, 127 u (v = knob * 4/127)"),
        Param(b"SHFT", 64, active=True, formatter=Formatter.PLAIN,
              doc="all formants -24..+24 semitones, 0.375 per step, 64 = 0, 96 = +12, 127 = +24"),
        Param(b"RES", 67, active=True, formatter=Formatter.PLAIN,
              doc="band narrowing 0.1..8 (log: 0.1 * 80^(knob/127)); 67 = 1.0"),
        Param(b"LEVL", 1, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("OFF", "ON"), doc="RMS level match (wet held at the dry loudness, 0.1..32x)"),
        Param(b"WIDE", 80, active=True, formatter=Formatter.PLAIN,
              doc="stereo: every formant lower on the left, higher on the right, by up to a semitone and never more than two bandwidths apart; 0 = both sides alike"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        _BLANK, _BLANK, _BLANK, _BLANK, _BLANK, _BLANK,
    ),
    dsp=DspSection(asm="vowel/vowel.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'VOWL': 64, 'SHFT': 0, 'RES': 127, 'LEVL': 1, 'MIX': 64, 'WIDE': 127},
)
