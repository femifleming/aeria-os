"""AERIA HAIL -- the Balloon Shop's hailstones: tiny slices of what is playing, hit hard and bouncing."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-hail", key="AERIA HAIL", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/hail/test.py under dsp_host; not flashed",
    doc="Hail: slices of the sound hit hard and bounce, quicker and quieter, like a ball dropped on a table.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"HAIL", fullname=b"AERIA Hail", build_tag=False),
    params=(
        Param(b"AMT", 0, active=True, formatter=Formatter.PLAIN,
              doc="the weather's fade 0..1 (v/127): how often a stone is born (x RATE); 0 and none in the air = at rest"),
        Param(b"RATE", 64, active=True, formatter=Formatter.PLAIN,
              doc="stones a second at full amount: 2.2 x 2^((v - 64)/16) (0.14 .. 34)"),
        Param(b"SIZE", 64, active=True, formatter=Formatter.PLAIN,
              doc="slice length x 2^((v - 64)/64): 64 = 5..30 ms; held to what a stone's 1,365 words take at its speed"),
        Param(b"BNCE", 64, active=True, formatter=Formatter.PLAIN,
              doc="gap ratio 0.62..0.78 shifted by (v - 64)/64 x 0.25, held to 0.2..0.95 (64 = the plugin)"),
        Param(b"WDTH", 114, active=True, formatter=Formatter.PLAIN,
              doc="pan spread: random -1..1 x v/127 (114 = the plugin's 0.9)"),
        Param(),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="hail/hail.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # X: 8,190 words from the base handed over in the Y-base word (an X address here); no alignment
    claims=Claims(dsp_ranges=(DspRange("x", 0x2000, 8190, "AERIA HAIL stones (proposed: core 1 X:$2000)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'AMT': 127, 'RATE': 127, 'SIZE': 127, 'BNCE': 0, 'WDTH': 127},
)
