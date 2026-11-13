"""AERIA TAPE -- Chloaelis' tape delay (DSP.h TapeLine + DelayCell): wow, flutter, hiss, tape life; no saturation."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-tape", key="AERIA TAPE", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/tape/test.py under dsp_host; not flashed",
    doc="Tape delay (Chloaelis): two hard-panned tape lines with wow and flutter, pre/de-emphasis, a 95 Hz head "
        "bump, hiss that fades in, and a tape that wears (gap loss, oxide flakes, a held loop that disintegrates) "
        "until refreshed. No saturation: the regeneration is held by gain-riding. Uses ECHO's memory (core 1).",
    menu=MenuEntry(fx2_id=0x1e, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"TAPE", fullname=b"AERIA Tape", build_tag=False),
    params=(
        Param(b"TIME", 96, active=True, formatter=Formatter.PLAIN,
              doc="left time 30 ms x (700/30)^(v/127) (log, 30..700 ms; 96 = 323 ms, the plugin's 0.33 s)"),
        Param(b"SKEW", 84, active=True, formatter=Formatter.PLAIN,
              doc="right time = left x 2^((v - 64)/64) (0.5 .. 1.99, 64 = same; 84 = x1.24, the plugin's 0.41/0.33), "
                  "held to 700 ms"),
        Param(b"FDBK", 77, active=True, formatter=Formatter.PLAIN,
              doc="regen 0.6 v/115 up to 115, then 0.6 .. 1.15 over 116..127 (the plugin's over-unity top: it sings, "
                  "held by gain-riding); 77 = 0.40, the plugin's default"),
        Param(b"WEAR", 64, active=True, formatter=Formatter.PLAIN,
              doc="tape life: 0 = OFF (pristine, no ageing, the damage hidden); else the tape ages from new to ruined "
                  "in 600 s x 2^((64 - v)/16) (64 = the plugin's 10 min, 127 = 39 s, 1 = 2.6 h)"),
        Param(b"WOW", 32, active=True, formatter=Formatter.PLAIN,
              doc="wow + flutter depth x v/32 (32 = the plugin's: ~0.5 % new, wider as the tape wears; 127 = x 3.97)"),
        Param(b"MIX", 44, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet, equal power (the plugin's cos/sin); 0 = exact dry; 44 = 0.35, the plugin's default"),
        Param(b"HISS", 64, active=True, formatter=Formatter.PLAIN,
              doc="hiss (and crackle) level x v/64 (64 = the plugin's); fades in over 1.6 s after loading and slews "
                  "at that rate, never steps"),
        Param(b"HPF", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "ON"),
              doc="the plugin's 320 Hz high-pass in the regeneration"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "HOLD"),
              doc="hold both lines' last dA as a loop (the tape stops recording); it disintegrates as the tape wears"),
        Param(b"RFRZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) random freeze chance v/127: the plugin's slow random binary oscillator per side, "
                  "written to knob-block words +6 (left) / +7 (right), non-zero = hold"),
        Param(b"TMOD", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("SLEW", "FADE"),
              doc="time change: SLEW the tape speed glides (pitch bends, the capstan's 1.6 Hz servo); FADE a second "
                  "head at the new time, 70 ms crossfade"),
        Param(b"RFSH", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("KEEP", "NEW"),
              doc="0 -> 1 puts on new tape (age 0, the damage cleared)"),
    ),
    dsp=DspSection(asm="tape/tape.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(),
    harness=Harness(layout_char="7", is_server=False),
    dear={'TIME': 127, 'SKEW': 127, 'FDBK': 127, 'WEAR': 127, 'WOW': 127, 'MIX': 127},
)
