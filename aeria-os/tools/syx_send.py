#!/usr/bin/env python3
"""Send an OS .syx to the Octatrack's Startup Menu (UPGRADE FIRMWARE, over USB MIDI), one message at a time,
paced like a slow C6 transfer.   .midivenv/bin/python syx_send.py FILE.syx [PORT_SUBSTRING] [MS_BETWEEN]"""
import sys, time
import rtmidi
path = sys.argv[1]; want = sys.argv[2] if len(sys.argv) > 2 else 'Octatrack'; gap = float(sys.argv[3]) / 1000 if len(sys.argv) > 3 else 0.03
data = open(path, 'rb').read()
msgs, i = [], 0
while True:
    s = data.find(b'\xf0', i)
    if s < 0: break
    e = data.find(b'\xf7', s)
    msgs.append(data[s:e + 1]); i = e + 1
out = rtmidi.MidiOut(); ports = out.get_ports()
idx = [k for k, p in enumerate(ports) if want.lower() in p.lower()]
if not idx: sys.exit(f'no MIDI port matching {want!r}: {ports}')
out.open_port(idx[0])
print(f'{len(msgs)} messages, {len(data)} bytes -> {ports[idx[0]]}')
t = time.time()
for n, m in enumerate(msgs):
    out.send_message(list(m)); time.sleep(gap)
    if n % 100 == 0: print(f'  {n}/{len(msgs)}  {time.time() - t:.0f} s', flush=True)
print('sent; the unit writes it and restarts')
