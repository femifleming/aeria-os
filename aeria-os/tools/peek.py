import sys; sys.exit("DO NOT RUN: peek.py is stale (hard-coded METER_MAX address and an old host-port protocol). On 12 Nov 2026 it wrote into the running image and stopped DSP core 0 until a power cycle. Rewrite it from sym.txt before using it.")
"""peek DSP X memory through the engine's bring-up peek (meter 5): peek.py addr [addr...]"""
import sys, struct, time
sys.path.insert(0, '/Users/femishonugafleming/octa/aeria-os/tools')
import aos
METER_MAX = 0x40042924
def setpeek(d, a):
    for v in (0x800000 | 0x2fe, a):
        d.ctrl_transfer(0x40, 0x10, 0, 1, struct.pack('>I', v), timeout=2000); time.sleep(0.01)
def peek(d, a):
    setpeek(d, a); time.sleep(0.05)
    aos.wr(d, METER_MAX + 20, b'\0\0\0\0'); time.sleep(0.06)
    return struct.unpack('>I', aos.rd(d, METER_MAX + 20, 4))[0]
if __name__ == '__main__':
    d = aos.dev()
    st = lambda c: bytes(d.ctrl_transfer(0xC0, 0x10, c, 0, 16, timeout=2000))
    print('core0', st(0)[:3].hex(), 'core1', st(1)[:7].hex())
    for a in sys.argv[1:]:
        a = int(a, 16); print('X:%05x = %05x %05x' % (a, peek(d, a), peek(d, a)))
    setpeek(d, 0)
