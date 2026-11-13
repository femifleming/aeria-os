"""AERIA BBD -- Chloaelis' bucket-brigade delay: 4,096 buckets a side, compander, clocked aliasing; no saturation."""
from remix.schema import (Category, Proof, BusRole, Claims, DspRange, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-bbd", key="AERIA BBD", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/bbd/test.py under dsp_host; not flashed",
    doc="Bucket brigade (Chloaelis): two hard-panned 4,096-stage lines clocked at 4096/time, NE570-style compander, "
        "fixed anti-alias and reconstruction filters, leak, clock feedthrough; slew or fade time changes, freeze.",
    menu=MenuEntry(fx2_id=0x0b, donor_desc=0x400d58b8,      # PROVISIONAL id
                   abbr=b"BBD", fullname=b"AERIA BBD", build_tag=False),
    params=(
        Param(b"TIME", 83, active=True, formatter=Formatter.PLAIN,
              doc="left time 47 ms x 21.28^(v/127) (47 ms .. 1 s); 83 = 0.346 s (the plugin's 0.33)"),
        Param(b"SKEW", 84, active=True, formatter=Formatter.PLAIN,
              doc="right time = left x 2^((v - 64)/64) (x0.5 .. x2, held to 47 ms .. 1 s); 84 = x1.24 (the plugin's 0.41/0.33)"),
        Param(b"FDBK", 44, active=True, formatter=Formatter.PLAIN,
              doc="regen 1.15 v/127 (44 = the plugin's 0.4); the loop gain is 1.05 regen, so past ~105 it sings, held "
                  "at 0.95 FS by the wet rider"),
        Param(b"TONE", 100, active=True, formatter=Formatter.PLAIN,
              doc="anti-alias / reconstruction cutoffs x 2^((v - 100)/24): 100 = the plugin's 6.8 / 6.2 kHz"),
        Param(b"MOD", 0, active=True, formatter=Formatter.PLAIN,
              doc="LFO>Time depth: the time knob + v x triangle (upward, in knob space, as the plugin); slews pitch"),
        Param(b"MIX", 44, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet, linear; 0 = exact dry (44 = the plugin's 0.35)"),
        Param(b"HPF", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "ON"),
              doc="320 Hz high-pass (Q 0.6) in the feedback path"),
        Param(b"FRZ", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("OFF", "HOLD"),
              doc="freeze both sides: the buckets recirculate (leaking 0.99985 a lap), input and feedback cut"),
        Param(b"RFRZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="(ColdFire only) random freeze chance v/127, the plugin's slow random binary oscillator per side; "
                  "the ColdFire writes knob-block word +6: bit 0 left held, bit 1 right held"),
        Param(b"TMOD", 0, count=2, active=True, formatter=Formatter.STEPPED, labels=("SLEW", "FADE"),
              doc="time changes: SLEW the clock glides (pitch bends); FADE a second head is crossfaded in (50 ms), "
                  "no bend; a longer time waits for the ring to refill first"),
        Param(b"RATE", 31, active=True, formatter=Formatter.PLAIN,
              doc="LFO rate 0.02 x 1000^(v/127) Hz (31 = 0.11 Hz, the plugin's left default); the right runs x1.18"),
        Param(),
    ),
    dsp=DspSection(asm="bbd/bbd.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    # Rings: 4,096 X words a side at the base handed over in the Y-base word (an X address here), a multiple of $2000
    claims=Claims(dsp_ranges=(DspRange("x", 0x2000, 8192, "AERIA BBD rings (core 1 X:$2000-$3fff)"),)),
    harness=Harness(layout_char="7", is_server=False),
    dear={'TIME': 0, 'SKEW': 0, 'FDBK': 127, 'TONE': 127, 'MOD': 127, 'MIX': 127, 'HPF': 1, 'TMOD': 0, 'RATE': 127},
)
