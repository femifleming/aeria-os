"""AERIA ECHO -- stereo just-intonation multi-tap delay (Cloudfin StereoDelay, fixed point)."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-echo", key="AERIA ECHO", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/echo/test.py under dsp_host; not flashed",
    doc="Echo: two hard-panned 3-tap lines at just-intonation ratios of one time; seamless taps.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"ECHO", fullname=b"AERIA Echo", build_tag=False),
    params=(
        Param(b"TIME", 80, active=True, formatter=Formatter.PLAIN,
              doc="base time T 10..220 ms (log); taps at T x ratio, longest held to 371 ms"),
        Param(b"FDBK", 60, active=True, formatter=Formatter.PLAIN,
              doc="regeneration 0..0.95 from each side's longest tap"),
        Param(b"TONE", 100, active=True, formatter=Formatter.PLAIN,
              doc="feedback lowpass 300 Hz..18 kHz (log); wet lowpass at 1.5x"),
        Param(b"RATO", 0, count=6, active=True, formatter=Formatter.WIDE_STEPPED,
              labels=("JUST", "MINR", "PYTH", "SEPT", "HARM", "UNIS"),
              doc="tap ratios L0 R0 L1 R1 L2 R2: 5-limit / minor / 3-limit / 7-limit / 8..13 / unison"),
        Param(b"WDTH", 127, active=True, formatter=Formatter.PLAIN,
              doc="wet width: 127 hard-panned sides, 0 mono"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        *(Param() for _ in range(6)),
    ),
    dsp=DspSection(asm="echo/echo.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # Left line: the whole 16,384-word FX2 instance buffer. Right line: X:$4000..$7fff.
    claims=Claims(stock_instance_buffer=True,
                  dsp_ranges=(DspRange("x", 0x4000, 16384, "AERIA ECHO right line (XRING)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'TIME': 127, 'FDBK': 127, 'TONE': 127, 'RATO': 2, 'WDTH': 64, 'MIX': 127},
)
