"""AERIA AURORA -- the Balloon Shop's aurora: the sound an octave up, ringing in four long curtains."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-aurora", key="AERIA AURORA", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/aurora/test.py under dsp_host; not flashed",
    doc="Aurora: the sound an octave up in a 9 s reverb of four curtains that swell and fade, added to the dry.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"AURA", fullname=b"AERIA Aurora", build_tag=False),
    params=(
        Param(b"AMT", 0, active=True, formatter=Formatter.PLAIN,
              doc="the weather's fade 0..1 (v/127) into the reverb, glided 1.2 s; 0 and the tail gone = at rest"),
        Param(b"TIME", 64, active=True, formatter=Formatter.PLAIN,
              doc="decay T60 = 9 s x 2^((v - 64)/32): 2.25 .. 35 s"),
        Param(b"TONE", 64, active=True, formatter=Formatter.PLAIN,
              doc="damping in the loop, as the plugin's lowpass: 6 kHz x 2^((v - 64)/24) (0.94 .. 11.6 kHz)"),
        Param(b"SHFT", 1, count=4, active=True, formatter=Formatter.STEPPED,
              labels=("UNIS", "+12", "+19", "+24"),
              doc="what goes into the reverb: the sound as it is, an octave up (the plugin), +19, +24 semitones"),
        Param(),
        Param(),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="aurora/aurora.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # X ring: 8,192 words at the base handed over in the Y-base word (an X address here), a multiple of 8,192
    claims=Claims(dsp_ranges=(DspRange("x", 0x2000, 8192, "AERIA AURORA ring (proposed: core 1 X:$2000)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'AMT': 127, 'TIME': 127, 'TONE': 127, 'SHFT': 3},
)
