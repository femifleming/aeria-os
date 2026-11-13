"""AERIA SMEAR -- STFT spectral smear / freeze / gate (N = 1024, hop 256, true stereo). Replaces the
diffusion-tank SMEAR of aeria-fx/smear."""
from remix.schema import (Category, Proof, BusRole, Claims, DspSection, Formatter, Harness,
                          Kind, MenuEntry, Module, Param, YBase)

MODULE = Module(
    name="aeria-smear", key="AERIA SMEAR", kind=Kind.DSP_EFFECT,
    category=Category.TRACK, author="femi", author_url="",
    proof=Proof.RENDER, proof_note="aeria-fx/spectral/test.py under dsp_host; not flashed",
    doc="Smear: spectral sustain, freeze and gate on a 1024-point FFT; the wash is resynthesised with random phases.",
    menu=MenuEntry(fx2_id=0x07, donor_desc=0x400d58b8,      # PROVISIONAL id (the old SMEAR's)
                   abbr=b"SMER", fullname=b"AERIA Smear", build_tag=False),
    params=(
        Param(b"SMR", 80, active=True, formatter=Formatter.PLAIN,
              doc="sustain: every bin's magnitude is held and decays; 0 = off, 1 = 0.1 s .. 127 = 20 s to -60 dB (log)"),
        Param(b"FRZ", 0, active=True, formatter=Formatter.PLAIN,
              doc="freeze: live out, held spectrum in (random phases, a new set every hop); 127 = the input is ignored"),
        Param(b"GATE", 0, active=True, formatter=Formatter.PLAIN,
              doc="spectral gate: bins under the threshold are removed; 0 = off, 1 = -80 dBFS .. 127 = -20 dBFS"),
        Param(b"WIDE", 100, active=True, formatter=Formatter.PLAIN,
              doc="share of the bins whose wash has its own phase on each side; 0 = the same phases (mono-compatible)"),
        Param(b"TILT", 64, active=True, formatter=Formatter.PLAIN,
              doc="tilt of the wet around 1 kHz: 0 = -6 dB/oct (a pole at 100 Hz), 64 = flat, 127 = +6 dB/oct (a zero at 100 Hz)"),
        Param(b"MIX", 127, active=True, formatter=Formatter.PLAIN,
              doc="dry/wet; 0 = exact dry (the dry is not delayed, the wet is 1,280 samples late)"),
        *(Param() for _ in range(6)),
    ),
    # 9,216 words of Y at the base in X:$213 (a multiple of $800); P code only, no P table
    dsp=DspSection(asm="spectral/spectral.asm", priority=15,
                   bus_role=BusRole.NONE, ybase=YBase.NEVER,
                   r7_latch_slot=None, gate_label=None),
    claims=Claims(stock_instance_buffer=True, buffer_words=9216),
    harness=Harness(layout_char="7", is_server=False),
    dear={'SMR': 127, 'FRZ': 64, 'GATE': 100, 'WIDE': 127, 'TILT': 127, 'MIX': 64},
)
