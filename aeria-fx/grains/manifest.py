"""AERIA GRAINS -- live granular processor (GrainShifter scaled down).

Describes the long build (grains.asm, gen.py RING=long): 35,280 words of Y from the base the
allocator entry gives (Y:$0000 on AERIA), a ring of 35,279 samples (800 ms) wrapped in code.
grains_test.asm (RING=test) is the same code on a 15,870-word ring, for the stock harness;
there SIZE tops out at 295 ms and POS at 356 ms.
"""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_ST = tuple(f"{s:+d}" if s else "0" for s in range(-24, 25))
_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-grains", key="AERIA GRAINS", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/grains/test.py under dsp_host; not flashed",
    doc="Live granular: 4 grains over the last 800 ms, pitch, reverse, spray, freeze.",
    menu=MenuEntry(fx2_id=0x09, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"GRNS", fullname=b"AERIA Grains", build_tag=False),
    params=(
        Param(b"SIZE", 70, active=True, formatter=Formatter.PLAIN,
              doc="grain length 10..750 ms (10 + 290 u^2 + 469 u^4 ms, u = knob/128; shortened at extreme speeds)"),
        Param(b"DENS", 80, active=True, formatter=Formatter.PLAIN,
              doc="1 (gaps) .. 4 overlapping grains"),
        Param(b"POS", 20, active=True, formatter=Formatter.PLAIN,
              doc="how far back grains start: 0 = now (1 ms) .. 127 = 793 ms"),
        Param(b"SPRY", 30, active=True, formatter=Formatter.PLAIN,
              doc="random start offset (up to +-399 ms at 127); pan is SPRD (page 2)"),
        Param(b"PTCH", 24, count=49, active=True, formatter=Formatter.WIDE_STEPPED,
              labels=_ST, doc="grain pitch -24..+24 semitones (24 = unison)"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("THAW", "HOLD"), doc="HOLD stops recording: grains loop the frozen 800 ms"),
        _BLANK,
        Param(b"REV", 0, active=True, formatter=Formatter.PLAIN,
              doc="probability a grain plays backwards"),
        _BLANK,
        Param(b"SPRD", 100, active=True, formatter=Formatter.PLAIN,
              doc="stereo spread: each grain panned at random (64 = the whole field, 127 = mostly hard left/right) and its start moved up to +-5 ms with the pan; 0 = mono"),
        _BLANK,
    ),
    dsp=DspSection(asm="grains/grains.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=35280),
    harness=Harness(layout_char="7", is_server=False),
    dear={'SIZE': 0, 'DENS': 127, 'SPRY': 127, 'PTCH': 48, 'REV': 64, 'MIX': 127, 'SPRD': 127},
)
