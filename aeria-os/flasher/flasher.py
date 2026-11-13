#!/usr/bin/env python3
"""AERIA Flasher's engine: lists releases, checks the unit, flashes one safely. Prints one line per event:
   STATUS <text> | STEP <n>/<total> <text> | OK <text> | FAIL <text> | UNIT <build name> | RELEASE <dir>\t<name>\t<made>\t<tested>\t<notes>
   flasher.py list | flasher.py unit | flasher.py flash <release dir> | flasher.py restore-state <backup file>
The order of a flash: the unit is checked (AERIA OS answering), its saved state (battery RAM 0x100C0000-0x100EFFFF)
is backed up, the new build is started from RAM and must come back with its own name and a drawn screen, the saved
state is compared, and only then is the flash written (the flash tool checks the file holds this build and the
stock bootloader, writes, and reads it all back). Nothing here touches the bootloader or the Startup Menu."""
import sys, os, time, struct, glob, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
def out(*a): print(*a, flush=True)
def releases():
    rs = []
    for d in glob.glob(os.path.join(ROOT, 'releases', '*')):
        f = os.path.join(d, 'release.txt')
        if not os.path.exists(f) or not os.path.exists(os.path.join(d, 'AERIA_OS.bin')): continue
        kv = {}
        for l in open(f):
            if ':' in l: k, v = l.split(':', 1); kv[k.strip()] = v.strip()
        rs.append((os.path.getmtime(os.path.join(d, 'AERIA_OS.bin')), d, kv))
    return [(d, kv) for _, d, kv in sorted(rs, reverse=True)]
def unit(quiet=False):
    import aos, usb.core
    try:
        d = aos.dev(3)
        name, _ = aos.info(d)
        return d, name
    except BaseException as e:
        if not quiet: out('FAIL The Octatrack is not answering. Plug it in by USB, switch it on and let AERIA OS start (the sky on the screen), then try again.')
        return None, None
def screen_drawn(d):
    f = bytes(d.ctrl_transfer(0xC0, 0x05, 0, 0, 1024, timeout=2000))
    return sum(bin(b).count('1') for b in f) > 200
def main(a):
    cmd = a[0] if a else 'list'
    if cmd == 'list':
        for d, kv in releases(): out('RELEASE ' + '\t'.join([d, kv.get('name', ''), kv.get('made', ''), kv.get('tested_on_unit', ''), kv.get('notes', '')]))
        return 0
    if cmd == 'unit':
        d, name = unit()
        if d: out('UNIT ' + name)
        return 0 if d else 1
    if cmd == 'restore-state':
        import aos
        d, name = unit()
        if not d: return 1
        data = open(a[1], 'rb').read(); aos.wr(d, 0x100C0000, data)
        out('OK Saved state put back. Switch the Octatrack off and on to start with it.'); return 0
    if cmd != 'flash': out('FAIL unknown command'); return 1
    import aos
    rel = a[1]; kv = dict(releases()).get(rel) or {}
    want = kv.get('name', '')
    raw = os.path.join(rel, 'MAIN.raw'); binf = os.path.join(rel, 'AERIA_OS.bin')
    T = 6
    out(f'STEP 1/{T} Looking for the Octatrack')
    d, name = unit()
    if not d: return 1
    out(f'STATUS It is running {name}')
    out(f'STEP 2/{T} Backing up your saved state (patterns, projects, settings)')
    keep = aos.rd(d, 0x100C0000, 0x30000)
    bk = os.path.join(ROOT, 'backups', time.strftime('state_%Y-%m-%d_%H-%M-%S.bin'))
    os.makedirs(os.path.dirname(bk), exist_ok=True); open(bk, 'wb').write(keep)
    out(f'STATUS Backup: {bk}')
    out(f'STEP 3/{T} Trying the new firmware from memory first (nothing is written yet)')
    r = subprocess.run([sys.executable, '-B', os.path.join(ROOT, 'tools', 'aos.py'), 'boot', raw], capture_output=True, text=True)
    if r.returncode: out('FAIL Could not start it from memory: ' + (r.stderr.strip().splitlines() or ['?'])[-1] + '. Nothing was flashed; switch the Octatrack off and on.'); return 1
    time.sleep(4)
    d = None
    for i in range(20):
        try:
            import aos as A; d = A.dev(2); n2, _ = A.info(d); break
        except BaseException: time.sleep(1)
    if not d: out('FAIL The new firmware did not come back on USB. Nothing was flashed: switch the Octatrack off and on and it starts the old firmware.'); return 1
    if want and n2 != want: out(f'FAIL It came back as {n2}, not {want}. Nothing was flashed; switch off and on.'); return 1
    time.sleep(8)
    if not screen_drawn(d): out('FAIL The new firmware started but drew nothing on the screen. Nothing was flashed; switch off and on.'); return 1
    out(f'STATUS {n2} is running from memory and drawing the screen')
    out(f'STEP 4/{T} Checking your saved state is still there')
    now = aos.rd(d, 0x100C0000, 0x30000)
    diff = sum(1 for i in range(0, len(now), 64) if now[i:i + 64] != keep[i:i + 64])
    out(f'STATUS {"unchanged" if diff == 0 else f"{diff} small blocks changed (it saves as it runs; the backup has the old copy)"}')
    out(f'STEP 5/{T} Writing the flash (about a minute: do not unplug or switch off)')
    r = subprocess.run([sys.executable, '-B', os.path.join(ROOT, 'tools', 'aos_flash.py'), binf, raw], capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or [''])[-1]
    if r.returncode or 'verified' not in last:
        out('FAIL Writing the flash went wrong: ' + (last or (r.stderr.strip().splitlines() or ['?'])[-1]) + '. Do NOT switch off; tell Claude. The Startup Menu (hold FUNC at power-on) can always reinstall an OS from the card.'); return 1
    out(f'STEP 6/{T} Done')
    out(f'OK {want or n2} is in the flash and verified. It is running now; next time you switch on it starts by itself.')
    return 0
if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
