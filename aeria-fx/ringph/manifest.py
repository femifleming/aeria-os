"""AERIA RING -- StoreFX ring modulator (envelope follow) / six-stage phaser, MODE select."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, ModeView, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-ring", key="AERIA RING", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/ringph/test.py under dsp_host; not flashed",
    doc="Ring modulator with envelope-following carrier, or a six-stage feedback phaser.",
    menu=MenuEntry(fx2_id=0x06, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"RING", fullname=b"AERIA Ring", build_tag=False),
    params=(
        Param(b"FREQ", 70, active=True, formatter=Formatter.PLAIN,
              doc="RING carrier 20 Hz..5 kHz (log) / PHSR LFO rate 0.05..10 Hz (log)"),
        Param(b"FOLW", 38, active=True, formatter=Formatter.PLAIN,
              doc="RING: carrier rises with input level (x1..x7) / PHSR: feedback 0..0.85"),
        Param(b"SPRD", 127, active=True, formatter=Formatter.PLAIN,
              doc="stereo spread: RING right carrier 0..90 degrees ahead (127 = quadrature) / PHSR right LFO 0..180 degrees (127 = opposite sweeps)"),
        Param(b"BIAS", 0, active=True, formatter=Formatter.PLAIN,
              doc="RING: carrier toward 1 (0 ring mod .. 127 nearly dry AM) / PHSR: sweep depth"),
        Param(b"CNTR", 64, active=True, formatter=Formatter.PLAIN,
              doc="PHSR sweep centre on the 200 Hz..5 kHz log axis; unused in RING"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"MODE", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("RING", "PHSR"), doc="ring modulator / phaser"),
        _BLANK, _BLANK, _BLANK, _BLANK, _BLANK,
    ),
    mode_slot=6,
    mode_views=(
        ModeView(mode=0, names={4: b"---"}, defaults={0: 70, 1: 38, 2: 127, 3: 0, 5: 127}),
        ModeView(mode=1, names={0: b"RATE", 1: b"FDBK", 3: b"DPTH"},
                 defaults={0: 40, 1: 64, 2: 127, 3: 127, 4: 64, 5: 127}),
    ),
    dsp=DspSection(asm="ringph/ringph.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'MODE': 1, 'FREQ': 127, 'FOLW': 127, 'MIX': 64},
)
