"""AERIA TWISTER -- the Balloon Shop's whirlwind: one event, the pitch climbs and falls while it spins."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-twister", key="AERIA TWISTER", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/twister/test.py under dsp_host; not flashed",
    doc="Twister: a whirlwind for 10 to 20 s; the pitch climbs a fifth and falls back while the sound spins.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"TWST", fullname=b"AERIA Twist", build_tag=False),
    params=(
        Param(b"AMT", 0, active=True, formatter=Formatter.PLAIN,
              doc="the trigger: crossing 64 upward starts one event; ignored until it has ended; under 64 re-arms"),
        Param(b"RISE", 7, count=13, active=True, formatter=Formatter.PLAIN,
              doc="semitones at the top, 0..12 (7 = the plugin's fifth)"),
        Param(b"SPIN", 64, active=True, formatter=Formatter.PLAIN,
              doc="spin added at the top: 5 Hz x v/64 on the 0.3 Hz it starts with (64 = 5.3 Hz at the top)"),
        Param(b"TIME", 0, active=True, formatter=Formatter.PLAIN,
              doc="0 = RND 10..20 s (the plugin); 1..127 = 2 s x 2^(v/32): 2.04 .. 31.3 s"),
        Param(),
        Param(),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="twister/twister.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # X ring: 4,096 words at the base handed over in the Y-base word (an X address here), a multiple of 4,096
    claims=Claims(dsp_ranges=(DspRange("x", 0x8000, 4096, "AERIA TWISTER ring (proposed: core 1 X:$8000)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'AMT': 127, 'RISE': 12, 'SPIN': 127, 'TIME': 127},
)
