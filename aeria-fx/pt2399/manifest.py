"""AERIA PT2399 -- Chloaelis' PT2399 delay: a CVSD chip with 44 kbit of RAM; grit and bandwidth follow the clock."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-pt2399", key="AERIA PT2399", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/pt2399/test.py under dsp_host; not flashed",
    doc="PT2399 (Chloaelis): two hard-panned CVSD delay chips, 1 bit a clock into 44,040 bits of RAM; slope-overload "
        "grit, clock-following bandwidth, S&H aliasing, clock whine; slew or fade time changes, freeze.",
    menu=MenuEntry(fx2_id=0x0e, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"PT99", fullname=b"AERIA PT2399", build_tag=False),
    params=(
        Param(b"TIME", 80, active=True, formatter=Formatter.PLAIN,
              doc="left time 50 ms x 20^(v/127) (50 ms .. 1 s); 80 = 0.33 s (the plugin's); under 0.333 s the clock "
                  "stays at 132 kbit/s and the read tap shortens"),
        Param(b"SKEW", 84, active=True, formatter=Formatter.PLAIN,
              doc="right time = left x 2^((v - 64)/64); 84 = x1.24 (the plugin's 0.41/0.33)"),
        Param(b"FDBK", 44, active=True, formatter=Formatter.PLAIN,
              doc="regen 1.15 v/127 (44 = the plugin's 0.4); loop gain 1.05 regen; past ~105 it sings, held at 0.95 FS"),
        Param(b"TONE", 100, active=True, formatter=Formatter.PLAIN,
              doc="the chip's bandwidth (clamp(0.048 clock, 2.4 k, 11.5 k)) x 2^((v - 100)/24); 100 = the plugin's"),
        Param(b"GRIT", 64, active=True, formatter=Formatter.PLAIN,
              doc="the CVSD's largest step x 2^-((v - 64)/32): 64 = the plugin's; up: slope overload sooner (grittier); "
                  "down: bigger steps (cleaner edges, more granular hiss)"),
        Param(b"MIX", 44, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet, linear; 0 = exact dry (44 = the plugin's 0.35)"),
        Param(b"HPF", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "ON"),
              doc="320 Hz high-pass (Q 0.6) in the feedback path"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "HOLD"),
              doc="freeze both sides: the RAM stops being written and loops bit for bit; input and feedback cut"),
        Param(b"RFRZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) random freeze chance v/127, the plugin's slow random binary oscillator per side; "
                  "the ColdFire writes knob-block word +6: bit 0 left held, bit 1 right held"),
        Param(b"TMOD", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("SLEW", "FADE"),
              doc="time changes: SLEW the clock glides (pitch bends); FADE a 25 + 25 ms dip around a jump of the head "
                  "(no bend); a slower clock waits for the RAM to refill first"),
        Param(b"MOD", 0, active=True, formatter=Formatter.PLAIN,
              doc="LFO>Time depth: the time knob + v x triangle (upward, in knob space, as the plugin)"),
        Param(b"RATE", 31, active=True, formatter=Formatter.PLAIN,
              doc="LFO rate 0.02 x 1000^(v/127) Hz (31 = 0.11 Hz); the right runs x1.18"),
    ),
    dsp=DspSection(asm="pt2399/pt2399.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # RAM: 1,835 X words a side (left at the base, right at +$800), the 24 masks at +$740, the halfbands at +$760, small
    # tables at +$f40, the big table +$1000..+$1fff (half-rate build, 13 Nov 2026); base a multiple of $2000
    claims=Claims(dsp_ranges=(DspRange("x", 0x2000, 8192, "AERIA PT2399 RAM + tables (core 1 X:$2000-$3fff)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'TIME': 0, 'SKEW': 0, 'FDBK': 127, 'TONE': 127, 'GRIT': 127, 'MIX': 127, 'HPF': 1, 'TMOD': 0,
          'MOD': 127, 'RATE': 127},
)
