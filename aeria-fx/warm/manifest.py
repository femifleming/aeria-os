"""AERIA WARM -- a simple warm soft clipper (the owner's stand-in for Chloaelis' tube)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-warm", key="AERIA WARM", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/warm/test.py under dsp_host; not flashed",
    doc="Warm: a gentle soft clipper (cubic or a rounder quintic), up to +24 dB into it, a little bias for even "
        "harmonics, a lowpass after and an output level.",
    menu=MenuEntry(fx2_id=0x0b, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"WARM", fullname=b"AERIA Warm", build_tag=False),
    params=(
        Param(b"HEAT", 40, active=True, formatter=Formatter.PLAIN,
              doc="drive 0..+24 dB into the curve (24 v/127 dB); the small-signal gain is the drive"),
        Param(b"TONE", 110, active=True, formatter=Formatter.PLAIN,
              doc="one-pole lowpass after the curve: 1 kHz x 18^(v/127); 127 = open"),
        Param(b"BIAS", 0, active=True, formatter=Formatter.PLAIN,
              doc="asymmetry: 0.25 v/127 added before the curve (its own offset taken off after): even harmonics"),
        Param(b"SHAP", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("SOFT", "TAPE"), doc="SOFT 1.5u - 0.5u^3 / TAPE (15u - 10u^3 + 3u^5)/8, rounder at the knee"),
        Param(b"LEVL", 52, active=True, formatter=Formatter.PLAIN,
              doc="output level v/64 (64 = 0 dB, 127 = +5.9 dB, 0 = off)"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="warm/warm.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'HEAT': 127, 'TONE': 100, 'BIAS': 127, 'SHAP': 1, 'LEVL': 127, 'MIX': 64},
)
