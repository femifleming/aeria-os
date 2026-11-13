"""mute scenes, QUAD + Y, resampling: seqnew2.py (battery RAM is kept and put back)"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
SYM={l.split()[-1]:int(l.split()[0],16) for l in open(S+'/sym.txt')}
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def step(t,n): aos.wr(d,B+0x400+12*(t*64+n),bytes([1,100,0,0,0,0,0,0,0,0,0,0]))
def mutes(): return aos.rd(d,B+10,1)[0]
def cap(secs=3):
    PB=place.SYM['params']; old=[]
    for k in (6,7): old.append(aos.rdc(d,PB+(6*12+k)*16+10,4)); aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a04,0); time.sleep(0.3)
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/n.wav','trim','0',str(secs)])
    r=c1dbg.reports(d,0.4)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    w=wave.open(S+'/n.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
    return x[8820:],int(r.get(6,'0'),16),int(r.get(4,'0'),16)
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
try:
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    for t in range(8):
        tp(t,19,1); tp(t,1,52+5*t); tp(t,4,60)
        for n in range(t%4,16,4): step(t,n)
    F=0x2d; A=0x19
    # mute scenes
    sq.key(F,1); sq.key(0x10); sq.key(0x12); sq.key(F,0); m1=mutes()
    sq.key(A,1); sq.key(F,1); sq.key(0); sq.key(F,0); sq.key(A,0)
    sq.key(F,1); sq.key(0x10); sq.key(0x12); sq.key(0x15); sq.key(F,0); m2=mutes()
    sq.key(A,1); sq.key(F,1); sq.key(1); sq.key(F,0); sq.key(A,0)
    sq.key(A,1); sq.key(0); sq.key(A,0); r1=mutes()
    sq.key(A,1); sq.key(1); r2=mutes(); sq.key(5); r3=mutes(); sq.key(A,0)
    print('mute scenes: kept %02x and %02x; scene 1 gives %02x, scene 2 %02x, an empty one leaves %02x  %s'%(m1,m2,r1,r2,r3,'OK' if (r1,r2,r3)==(m1,m2,m2) and m1!=m2 else 'WRONG'))
    aos.wr(d,B+10,bytes([0]))
    # QUAD + Y: main L is patched into input B (channel 1), the phones (cue) into input C (channel 2)
    aos.wr(d,B+10,bytes([0xFE])); tp(0,8,64); op(1,1)
    for name,out,y in (('MAIN',0,64),('CUE',1,64),('QUAD front',3,1),('QUAD middle',3,64),('QUAD back',3,127)):
        tp(0,26,out); tp(0,44,y); time.sleep(0.3); x,dry,ov=cap(2)
        print('%-12s main out %.3f  heard at in B %.3f (main)  in C %.3f (cue)  dry %d overruns %d'%(name,np.abs(x[:,4]).max(),np.abs(x[:,1]).max(),np.abs(x[:,2]).max(),dry,ov))
    tp(0,26,0); aos.wr(d,B+10,bytes([0]))
    # resampling: FUNC + BANK, one pattern from its next start
    rs=SYM['rs']; sm=SYM['st_smp']+14*80
    sq.key(F,1); sq.key(0x2f); sq.key(F,0); st=[aos.rdc(d,rs,1)[0]]
    t0=time.time()
    while time.time()-t0<8:
        s=aos.rdc(d,rs,1)[0]
        if s!=st[-1]: st.append(s)
        if s==0 and len(st)>1: break
        time.sleep(0.05)
    dd,frames,stp=struct.unpack('>III',aos.rdc(d,sm,12)); status=aos.rdc(d,sm+12,1)[0]
    print('resample: states %s, sample 6.1 holds %d frames (%.2f s; a pattern at 120 BPM is 2.00 s), status %d'%(st,frames,frames/44100,status))
    raw=aos.rdc(d,dd,4*2*4096) if frames>4096 else b''
    if raw:
        v=np.array(struct.unpack('>%di'%(len(raw)//4),raw),dtype=np.float64); print('   its first 4096 frames peak at %.3f of full scale (24 bit)'%(np.abs(v).max()/8388608))
    # play it back on track 1 alone
    aos.wr(d,B+10,bytes([0xFE])); tp(0,19,0); tp(0,0,80); tp(0,1,64); tp(0,4,127); aos.wr(d,B+0x400,bytes(12*64)); step(0,0); time.sleep(0.5)
    x,dry,ov=cap(3); print('sample 6.1 played on track 1: main out peak %.3f rms %.3f  dry %d overruns %d'%(np.abs(x[:,4]).max(),np.sqrt((x[:,4]**2).mean()),dry,ov))
finally:
    op(1,0); time.sleep(0.3); aos.wr(d,B,keep); sq.seqmode(was); print('put back')
