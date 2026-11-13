"""the new machines and clocks, one track at a time, recorded: seqnew.py"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def step(t,n,flags=1,vel=100,micro=0,prob=0,cond=0,ratch=0,locks=()):
    lp=[0,0,0]; lv=[0,0,0]
    for k,(a,b) in enumerate(locks): lp[k]=a+1; lv[k]=b
    aos.wr(d,B+0x400+12*(t*64+n),bytes([flags&255,vel,micro&0xff,prob,cond,ratch]+lp+lv))
def clear(): aos.wr(d,B+0x400,bytes(12*64*8))
def cap(tag,secs=3):
    old=[]; PB=__import__('place').SYM['params']
    for k in (6,7): old.append(aos.rdc(d,PB+(6*12+k)*16+10,4)); aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a04,0); c1dbg.dspw(d,0x33a02,0); time.sleep(0.4)
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/n.wav','trim','0',str(secs)])
    r=c1dbg.reports(d,0.4)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    w=wave.open(S+'/n.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
    y=x[8820:,4]; k=32; e=np.abs(y); env=e[:len(e)//k*k].reshape(-1,k).max(1); th=max(env.max()*0.35,1e-4)
    on=[i for i in range(3,len(env)) if env[i]>th and env[i-3:i].max()<th*0.7]; iv=np.round(np.diff(np.array(on))*k/44.1).astype(int)
    f=np.abs(np.fft.rfft(y[:65536]*np.hanning(65536))); cen=(f*np.arange(len(f))).sum()/max(f.sum(),1e-9)*44100/65536
    print('%-34s peak %.3f  centroid %5.0f Hz  dry %d load %d%%  onsets %d  gaps ms %s'%(tag,np.abs(y).max(),cen,int(r.get(6,'0'),16),int(r.get(2,'0'),16)*100//128,len(on),list(iv[:14])))
keep=aos.rd(d,B,0x400+12*64*8)
try:
    op(0,1); op(4); op(3,1200); clear()
    T=0
    for n in (0,4,8,12): step(T,n)
    tp(T,19,1); tp(T,4,50); op(1,1); cap('SUB, four to the bar')
    tp(T,19,2); tp(T,36,16); tp(T,37,90); tp(T,38,30); cap('FM 1:1, index falling')
    tp(T,36,57); tp(T,38,127); tp(T,1,88); tp(T,4,25); cap('FM 3.56:1 bright short (a hat)')
    tp(T,19,3); tp(T,1,100); tp(T,4,20); cap('NOIS high')
    tp(T,19,4); tp(T,1,64); tp(T,4,110); tp(T,37,100); tp(T,38,100); cap('PLUK at the middle')
    tp(T,1,76); cap('PLUK an octave up')
    tp(T,19,1); tp(T,1,76); tp(T,4,30)
    clear(); step(T,0,ratch=8); cap('SUB ratchet 8 even')
    step(T,0,flags=1|(5<<4),ratch=8); cap('... curve +5 (coming to rest)')
    step(T,0,flags=1|((-5&15)<<4),ratch=8); cap('... curve -5 (letting go)')
    step(T,0,ratch=64); cap('... 64 hits (a pitch)')
    clear()
    for n in range(16): step(T,n)
    tp(T,39,6); cap('16 steps at RATE 4/3')
    tp(T,39,5); tp(T,40,100); cap('WARP +36 (slowing through the bar)',4)
    tp(T,40,64); tp(T,34,60); cap('DENS 47%')
    tp(T,34,127); tp(T,32,3); cap('DIR RND (all steps on: even)')
finally:
    op(1,0); time.sleep(0.3); aos.wr(d,B,keep); op(0,keep[5]); print('put back')
