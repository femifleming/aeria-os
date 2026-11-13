"""AERIA ECHO (2nd version) -- Chloaelis' dual delay: a left line heard only left, a right line heard only right."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

_BLANK = Param(b"", 0)

MODULE = Module(
    name="aeria-echo", key="AERIA ECHO", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/echo2/test.py under dsp_host; not flashed",
    doc="Echo (Chloaelis' delay): dual mono, hard panned; a time a side, regen to self-oscillation held by a gain "
        "rider, feedback lowpass and high-pass, freeze, slew or crossfaded time changes.",
    menu=MenuEntry(fx2_id=0x0a, donor_desc=0x400d58b8,      # PROVISIONAL id (the old ECHO's)
                   abbr=b"ECHO", fullname=b"AERIA Echo", build_tag=False),
    params=(
        Param(b"TIME", 115, active=True, formatter=Formatter.PLAIN,
              doc="left delay 30..370 ms (log): 30 ms x (37/3)^(v/127); 115 = 292 ms"),
        Param(b"FDBK", 49, active=True, formatter=Formatter.PLAIN,
              doc="loop gain 0.99 v/115 up to 115 (49 = 0.42, the plugin's default regen 0.4 x 1.05); 116..127 "
                  "self-oscillate up to 1.2075 (the plugin's 115 %), held by the gain rider, never clipped"),
        Param(b"TONE", 127, active=True, formatter=Formatter.PLAIN,
              doc="feedback lowpass 300 Hz x 60^(v/127); 127 = open"),
        Param(b"SKEW", 84, active=True, formatter=Formatter.PLAIN,
              doc="right delay = left x 2^((v - 64)/64) (0.5 .. 1.98; 64 = same; 84 = x1.242, the plugin's 0.41/0.33); "
                  "held to 371 ms"),
        Param(b"SPRD", 127, active=True, formatter=Formatter.PLAIN,
              doc="127 = hard panned (the plugin); lower blends the two lines toward mono (0 = both sides L+R)"),
        Param(b"MIX", 64, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry"),
        Param(b"HPF", 0, active=True, formatter=Formatter.PLAIN,
              doc="feedback high-pass: 0 off (a 10 Hz DC blocker), else 12 dB/oct at 320 Hz x 2^((v - 90)/22) "
                  "(19 Hz..1 kHz; 90 = the plugin's 320 Hz)"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("OFF", "HOLD"), doc="HOLD: the lines stop taking input and loop what they hold"),
        Param(b"RFRZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) random-freeze chance v/127, the plugin's clock; the ColdFire sets bit 0 of the "
                  "knob word r6+$d while it holds"),
        Param(b"TMOD", 0, count=2, active=True, formatter=Formatter.STEPPED,
              labels=("SLEW", "FADE"), doc="time changes: SLEW glides (pitch bends), FADE crossfades two heads (60 ms)"),
        Param(b"GLID", 64, active=True, formatter=Formatter.PLAIN,
              doc="SLEW speed: two one-poles at 1.6 Hz x 2^((v - 64)/24) (0.26..9.9 Hz; 64 = the plugin's tape)"),
        _BLANK,
    ),
    dsp=DspSection(asm="echo2/echo2.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True,
                  dsp_ranges=(DspRange("x", 0x4000, 16384, "AERIA ECHO right line (XRING)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'TIME': 127, 'FDBK': 127, 'TONE': 100, 'SKEW': 127, 'SPRD': 64, 'MIX': 127, 'HPF': 90, 'GLID': 127},
)
