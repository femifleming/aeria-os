"""AERIA SMEAR -- diffusion smear tank with freeze (time-domain stand-in for spectral smear / Snow)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-smear", key="AERIA SMEAR", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/smear/test.py under dsp_host; not flashed",
    doc="Smear: dense modulated diffusion tank; FRZ holds it ringing (lossless loop).",
    menu=MenuEntry(fx2_id=0x07, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"SMER", fullname=b"AERIA Smear", build_tag=False),
    params=(
        Param(b"SMER", 70, active=True, formatter=Formatter.PLAIN,
              doc="smear time: tank RT60 0.3..20 s (log)"),
        Param(b"DIFF", 100, active=True, formatter=Formatter.PLAIN,
              doc="diffusion: input 0..0.75, tank allpasses 0..0.7"),
        Param(b"TONE", 90, active=True, formatter=Formatter.PLAIN,
              doc="in-tank damping, 0 dark .. 127 none"),
        Param(b"MOD", 40, active=True, formatter=Formatter.PLAIN,
              doc="tank allpass modulation 0..12 samples at 0.7 Hz"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("THAW", "HOLD"), doc="HOLD: input out, tank lossless -- rings until THAW"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="smear/smear.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=15870),
    harness=Harness(layout_char="7", is_server=False),
    dear={'SMER': 127, 'DIFF': 127, 'MOD': 127, 'FRZ': 1, 'MIX': 127},
)
