"""AERIA FOG -- the Balloon Shop's fog: six short allpasses blur every attack, the highs sink."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-fog", key="AERIA FOG", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/fog/test.py under dsp_host; not flashed",
    doc="Fog: six allpasses in a row smear every attack over a tenth of a second, the highs sink to 4.5 kHz.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"FOG", fullname=b"AERIA Fog", build_tag=False),
    params=(
        Param(b"AMT", 0, active=True, formatter=Formatter.PLAIN,
              doc="the weather's fade 0..1 (v/127): mix = smoothstep(1.5 amt), lowpass 18 kHz x 2^-(amt DARK/32); "
                  "0 = exact dry, at rest"),
        Param(b"BLUR", 100, active=True, formatter=Formatter.PLAIN,
              doc="allpass gain g = 0.6 v/100 (100 = the plugin's 0.6)"),
        Param(b"DARK", 64, active=True, formatter=Formatter.PLAIN,
              doc="lowpass at full fog: 18 kHz x 2^-(v/32) (64 = 4.5 kHz, 0 = open, 127 = 1.15 kHz)"),
        Param(),
        Param(),
        Param(),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="fog/fog.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # Y: 5,632 words from a base $a00 past a multiple of $1000 (rings at +0, +$200, +$600)
    claims=Claims(stock_instance_buffer=True, buffer_words=5632),
    harness=Harness(layout_char="7", is_server=False),
    dear={'AMT': 127, 'BLUR': 127, 'DARK': 127},
)
