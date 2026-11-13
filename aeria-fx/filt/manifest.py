"""AERIA FILT -- Chloaelis' multimode filter: TPT SVF morphing LP > BP > HP > NOTCH, a cutoff per side (Lorenz)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-filt", key="AERIA FILT", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/filt/test.py under dsp_host; not flashed",
    doc="Multimode filter (Chloaelis): LP > BP > HP > notch morph, resonance to k = 0.04 without saturation, "
        "a cutoff per side moved by SKEW and the ColdFire's Lorenz attractor.",
    menu=MenuEntry(fx2_id=0x0f, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"FILT", fullname=b"AERIA Filter", build_tag=False),
    params=(
        Param(b"CUT", 88, active=True, formatter=Formatter.PLAIN,
              doc="cutoff 20 Hz x 1000^(v/127) (log 20 Hz .. 20 kHz); both sides"),
        Param(b"RES", 19, active=True, formatter=Formatter.PLAIN,
              doc="resonance: k = 2 - 1.96 v/127 (the plugin's; 19 = its default 0.15); the resonant peak is held at unity"),
        Param(b"MODE", 0, active=True, formatter=Formatter.PLAIN,
              doc="morph 3 v/127: 0 LP, 42 BP, 85 HP, 127 NOTCH, linear in between"),
        Param(b"SKEW", 64, active=True, formatter=Formatter.PLAIN,
              doc="right cutoff = left x 2^((v - 64)/16) (-4 .. +3.94 octaves; 64 = same)"),
        Param(b"LRNZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="Lorenz depth: each side's cutoff x 2^(3.5 (v/127) lz), lz = the knob-block words +6 (left, the "
                  "attractor's x) and +7 (right, its y) the ColdFire writes, signed Q23; 0 = off"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"LZSP", 44, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) Lorenz speed v/127: 0.03 x 2^(9 v/127) attractor time units a second"),
        Param(b"CHAS", 38, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) Lorenz chaos: rho = 24.5 + 155.5 (v/127)^2"),
        Param(b"SHAP", 79, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) Lorenz shape: beta = 0.5 + 3.5 v/127 (79 = 8/3)"),
        _BLANK, _BLANK, _BLANK,
    ),
    dsp=DspSection(asm="filt/filt.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'CUT': 100, 'RES': 127, 'MODE': 64, 'SKEW': 90, 'LRNZ': 127, 'MIX': 64},
)
