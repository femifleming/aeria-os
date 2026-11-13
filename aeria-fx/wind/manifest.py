"""AERIA WIND -- the Balloon Shop's wind: the sound read through a drifting delay, in gusts."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-wind", key="AERIA WIND", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/wind/test.py under dsp_host; not flashed",
    doc="Wind: gusts bend the pitch a few cents through a drifting delay, swell and lean the sound.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"WIND", fullname=b"AERIA Wind", build_tag=False),
    params=(
        Param(b"AMT", 0, active=True, formatter=Formatter.PLAIN,
              doc="the weather's fade 0..1 (v/127): mix = smoothstep(1.5 amt); 0 = exact dry, at rest"),
        Param(b"DPTH", 64, active=True, formatter=Formatter.PLAIN,
              doc="largest pitch drift in a full gust: 1200 log2(1 + 0.012 v/64) cents (64 = 20.6 c)"),
        Param(b"GUST", 64, active=True, formatter=Formatter.PLAIN,
              doc="a gust every 3.5 s x 2^((64 - v)/16) on average (56 s .. 0.23 s)"),
        Param(b"LEAN", 64, active=True, formatter=Formatter.PLAIN,
              doc="how far a gust leans the sound: v/64 x (-35 % one side, +12 % the other)"),
        Param(),
        Param(),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="wind/wind.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # one stereo ring of 4,096 words in Y; its base must be a multiple of 4,096
    claims=Claims(stock_instance_buffer=True, buffer_words=4096),
    harness=Harness(layout_char="7", is_server=False),
    dear={'AMT': 127, 'DPTH': 127, 'GUST': 127, 'LEAN': 127},
)
