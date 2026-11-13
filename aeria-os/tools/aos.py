#!/usr/bin/env python3
"""AERIA OS's USB link (usb.c, interface 3): vendor requests on EP0.

    aos.py info                     what is running, and USB counters
    aos.py mem ADDR LEN [FILE]      read memory (hex to stdout, or raw to FILE)
    aos.py poke ADDR HEX            write bytes
    aos.py key CODE [0|1]           a key press (1) / release (0); both when omitted
    aos.py enc N DELTA              an encoder turn
    aos.py screen                   the screen as text
    aos.py boot MAIN.raw            send a new AERIA OS build and start it (RAM only; a power cycle runs the flashed one)
    aos.py wait [SECONDS]           wait until AERIA answers
Runs with the Modwerk venv's python (pyusb + libusb)."""
import struct, sys, time
import usb.core

VID, PID = 0x1209, 0x0001
STAGE = 0x45800000


def dev(timeout=0):
    t = time.time()
    while True:
        d = usb.core.find(idVendor=VID, idProduct=PID)
        if d is not None:
            return d
        if time.time() - t >= timeout:
            sys.exit('AERIA OS is not on USB')
        time.sleep(0.3)


def rd(d, addr, n):
    out = bytearray()
    while n:
        k = min(n, 4096)
        out += bytes(d.ctrl_transfer(0xC0, 0x01, addr >> 16, addr & 0xFFFF, k, timeout=3000))
        addr += k; n -= k
    return bytes(out)


def wr(d, addr, data):
    for o in range(0, len(data), 4096):
        chunk = data[o:o + 4096]
        a = addr + o
        d.ctrl_transfer(0x40, 0x02, a >> 16, a & 0xFFFF, chunk, timeout=3000)


# the running OS's own variables (SDRAM, cached): read and write them THROUGH the cache; rd / wr go round it
def rdc(d, addr, n):
    out = bytearray()
    while n:
        k = min(n, 4096)
        out += bytes(d.ctrl_transfer(0xC0, 0x09, addr >> 16, addr & 0xFFFF, k, timeout=3000))
        addr += k; n -= k
    return bytes(out)


def wrc(d, addr, data):
    for o in range(0, len(data), 4096):
        chunk = data[o:o + 4096]
        a = addr + o
        d.ctrl_transfer(0x40, 0x12, a >> 16, a & 0xFFFF, chunk, timeout=3000)


def info(d):
    b = bytes(d.ctrl_transfer(0xC0, 0x06, 0, 0, 96, timeout=2000))
    name, rest = b.split(b'\0', 1)
    keys = ['resets', 'setups', 'iso_sent', 'iso_short', 'dsp_words', 'tap_frames', 'rec_mask', 'rec_headers']
    vals = struct.unpack('>%dI' % (len(rest) // 4), rest[:len(rest) // 4 * 4])
    return name.decode(), dict(zip(keys, vals))


def main(a):
    if not a:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == 'wait':
        d = dev(float(a[1]) if len(a) > 1 else 60)
        print(*info(d)); return
    d = dev()
    if cmd == 'info':
        print(*info(d))
    elif cmd == 'mem':
        data = rd(d, int(a[1], 0), int(a[2], 0))
        if len(a) > 3: open(a[3], 'wb').write(data)
        else: print(data.hex())
    elif cmd == 'poke':
        wr(d, int(a[1], 0), bytes.fromhex(a[2]))
    elif cmd == 'key':
        code = int(a[1], 0)
        for p in ([int(a[2])] if len(a) > 2 else [1, 0]):
            d.ctrl_transfer(0x40, 0x03, code, p, None, timeout=2000)
            time.sleep(0.05)
    elif cmd == 'enc':
        d.ctrl_transfer(0x40, 0x04, int(a[1]), int(a[2]) & 0xFF, None, timeout=2000)
    elif cmd == 'screen':
        f = bytes(d.ctrl_transfer(0xC0, 0x05, 0, 0, 1024, timeout=2000))
        # AERIA's frame: 128 columns of two big-endian words; line y is bit (y & 31) of word 2x + (y < 32)
        w = struct.unpack('>256I', f)
        for y in range(64):
            print(''.join('#' if (w[2 * x + (1 if y < 32 else 0)] >> (y & 31)) & 1 else ' ' for x in range(128)).rstrip())
    elif cmd == 'boot':
        img = open(a[1], 'rb').read()
        assert img[:4] == b'\x4f\xef\xff\xe4', 'not an AERIA OS image'
        t = time.time()
        wr(d, STAGE, img)
        assert rd(d, STAGE, 4096) == img[:4096] and rd(d, STAGE + len(img) - 4096, 4096) == img[-4096:], 'staged copy differs'
        print(f'staged {len(img)} bytes in {time.time() - t:.1f} s; starting it')
        try:
            d.ctrl_transfer(0x40, 0x07, 0, 0, struct.pack('>II', STAGE, len(img)), timeout=2000)
        except usb.core.USBError:
            pass   # it may leave the bus before the status stage reaches us
        time.sleep(2)
        print(*info(dev(30)))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
