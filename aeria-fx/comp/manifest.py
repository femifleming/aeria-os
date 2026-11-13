"""AERIA COMP -- stereo-linked compander (Cloudfin's Compander): compression above the threshold,
downward expansion or upward compression below it, makeup."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-comp", key="AERIA COMP", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/comp/test.py under dsp_host; not flashed",
    doc="Compander: compression above the threshold, expansion or upward compression below it.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"COMP", fullname=b"AERIA Comp", build_tag=False),
    params=(
        Param(b"THR", 38, active=True, formatter=Formatter.PLAIN,
              doc="threshold 0 .. -60 dBFS (linear in dB): -60 v/127"),
        Param(b"RATI", 29, active=True, formatter=Formatter.PLAIN,
              doc="ratio above the threshold 1:1 .. 20:1 (log): 20^(v/127)"),
        Param(b"BELW", 64, active=True, formatter=Formatter.BIPOLAR,
              doc="below the threshold: 64 off; right = downward expansion to 1:8 (floor -80 dB); "
                  "left = upward compression to 10:1 (cap +30 dB, never above the threshold)"),
        Param(b"ATK", 81, active=True, formatter=Formatter.PLAIN,
              doc="attack 0.05 .. 200 ms (log): 0.05 ms * 4000^(v/127)"),
        Param(b"REL", 83, active=True, formatter=Formatter.PLAIN,
              doc="release 5 .. 2000 ms (log): 5 ms * 400^(v/127)"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"MAKE", 0, active=True, formatter=Formatter.PLAIN,
              doc="makeup gain 0 .. +24 dB: 24 v/127"),
        _BLANK, _BLANK, _BLANK, _BLANK, _BLANK,
    ),
    dsp=DspSection(asm="comp/comp.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'THR': 64, 'RATI': 127, 'BELW': 0, 'ATK': 0, 'REL': 0, 'MAKE': 127, 'MIX': 64},
)
