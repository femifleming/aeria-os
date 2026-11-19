"""the browser's sample preview on the unit: the card read and the time from the key to sound. preview_test.py"""
import sys; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); from ut import *
import numpy as np, subprocess
db=lambda v: 20*np.log10(max(float(v),1e-7))
def onset(y,ch,w=220):
    r=np.array([np.sqrt((y[i:i+w,ch]**2).mean()) for i in range(0,len(y)-w+1,w)])
    floor=np.median(r[:80]); hot=np.where(r>max(floor*10,2e-5))[0]
    return (hot[0]*w/44.1) if len(hot) else -1
def timing(tag,move):
    aos.wrc(d,SYM['hear_dbg'],bytes(16))
    f='/Users/you/octa/aeria-os/build/cap/prev_%s.wav'%tag
    pr=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',f,'trim','0','1.6'])
    time.sleep(0.6); move(); pr.wait(); y=cap.load(f)
    hd=[u32('hear_dbg',4*i) for i in range(3)]
    o=[onset(y,c) for c in (4,2)]
    print('  %-10s card read %5.1f ms (%6d frames, %3d chunks); sound %s ms after the key (main L / phones via C: %s)'%(tag,hd[0]/132000.0,hd[1],hd[2],'%.0f'%(min(x for x in o if x>=0)-600) if any(x>=0 for x in o) else 'not seen',['%.0f'%(x-600) if x>=0 else '-' for x in o]))
old=inputs_off()
try:
    seqmode(1); key(K['STOP']); key(K['SRC']); combo(K['FUNC'],K['YES']); time.sleep(0.6)
    for _ in range(3): key(K['YES']); time.sleep(1.0)
    print('browser: %d entries'%u32('br_count'))
    press=lambda c: (d.ctrl_transfer(0x40,0x03,c,1,None,timeout=2000), d.ctrl_transfer(0x40,0x03,c,0,None,timeout=2000))
    timing('RIGHT',lambda: press(K['RIGHT']))
    key(K['DOWN']); time.sleep(0.5); timing('RIGHT 2',lambda: press(K['RIGHT']))
    timing('DOWN auto',lambda: press(K['DOWN']))
    timing('DOWN auto2',lambda: press(K['DOWN']))
    timing('UP auto',lambda: press(K['UP']))
finally:
    for _ in range(4): key(K['NO'])
    inputs_back(old); print('browser closed: mode',u32('br_mode'))
