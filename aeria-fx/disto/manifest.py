"""AERIA DIST -- StoreFX disto / clipper / degrade as one effect with a MODE select."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, ModeView, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-dist", key="AERIA DIST", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/disto/test.py under dsp_host; not flashed",
    doc="Distortion: atan-like drive, clipper with lowpass, bit/rate degrade.",
    menu=MenuEntry(fx2_id=0x1f, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"DIST", fullname=b"AERIA Dist", build_tag=False),
    params=(
        Param(b"DRIV", 80, active=True, formatter=Formatter.PLAIN,
              doc="DIST drive (d 0.4..0.0001) / CLIP threshold 0.25..0.001 / DGRD bits 24..2"),
        Param(b"TONE", 70, active=True, formatter=Formatter.PLAIN,
              doc="DIST slope lowpass / CLIP 2-pole lowpass 100 Hz..15 kHz / DGRD post lowpass"),
        Param(b"RATE", 127, active=True, formatter=Formatter.PLAIN,
              doc="DGRD sample-and-hold rate, 1/100 .. 1 x 44.1 kHz (log); unused otherwise"),
        _BLANK,
        Param(b"LEVL", 64, active=True, formatter=Formatter.PLAIN,
              doc="output level 0..2x (64 = 1x)"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"MODE", 0, count=3, active=True, formatter=Formatter.STEPPED,
              labels=("DIST", "CLIP", "DGRD"), doc="drive / clipper / degrade"),
        _BLANK, _BLANK, _BLANK, _BLANK, _BLANK,
    ),
    mode_slot=6,
    mode_views=(
        ModeView(mode=0, names={2: b"---"}, defaults={0: 80, 1: 70, 4: 64, 5: 127}),
        ModeView(mode=1, names={0: b"THRS", 2: b"---"}, defaults={0: 70, 1: 90, 4: 80, 5: 127}),
        ModeView(mode=2, names={0: b"BITS"}, defaults={0: 90, 1: 127, 2: 60, 4: 64, 5: 127}),
    ),
    dsp=DspSection(asm="disto/disto.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'MODE': 0, 'DRIV': 127, 'MIX': 64},
)
