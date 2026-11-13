"""AERIA PITCH -- two-head delay-line pitch shifter with feedback (Cloudfin / Balloon Shop port)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_ST = tuple(f"{s:+d}" if s else "0" for s in range(-24, 25))

MODULE = Module(
    name="aeria-pitch", key="AERIA PITCH", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/pitch/test.py under dsp_host; not flashed",
    doc="Two-head delay-line pitch shifter, -24..+24 st, stereo micro-detune, feedback through tone/DC/soft clip.",
    # fx2_id is PROVISIONAL: pick a free id at integration (0x1b/0x1e/0x1f unclaimed in octabam).
    menu=MenuEntry(fx2_id=0x1b, donor_desc=0x400d58b8,
                   abbr=b"PTCH", fullname=b"AERIA Pitch", build_tag=False),
    params=(
        Param(b"SHFT", 36, count=49, active=True, formatter=Formatter.WIDE_STEPPED,
              labels=_ST, doc="transpose in semitones, -24..+24 (24 = unison)"),
        Param(b"FINE", 64, active=True, formatter=Formatter.BIPOLAR,
              doc="fine tune, +-1 semitone, 64 = none"),
        Param(b"GRN", 77, active=True, formatter=Formatter.PLAIN,
              doc="window 10..160 ms (squared taper, 77 = 65 ms); glides ~1.5 s"),
        Param(b"FDBK", 0, active=True, formatter=Formatter.PLAIN,
              doc="shifted signal back into the line, 0..0.95; repeats climb/fall"),
        Param(b"TONE", 100, active=True, formatter=Formatter.PLAIN,
              doc="wet + feedback lowpass, 210 Hz .. open at 127"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry, 127 = wet"),
        Param(b"WIDE", 40, active=True, formatter=Formatter.PLAIN,
              doc="stereo micro-detune: left up, right down, 0..50 cents apart (40 = 16 c); 0 = both sides alike"),
        *(Param() for _ in range(5)),
    ),
    dsp=DspSection(asm="pitch/pitch.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=15870),
    harness=Harness(layout_char="7", is_server=False),
    dear={'SHFT': 0, 'GRN': 127, 'FDBK': 127, 'MIX': 127, 'WIDE': 127},
)
