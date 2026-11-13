"""each new effect on track 1 of the sequencer, on the hardware: audio out, DSP load, overruns, dry frames. fxhw.py"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq, c1dbg, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
FX=B+64+256+32
def fx(t,row,core,knobs):
    aos.wr(d,FX+8+t,bytes([core])); aos.wr(d,FX+16+12*t,bytes(knobs)); aos.wr(d,FX+t,bytes([row]))
def cap(secs=3):
    PB=place.SYM['params']; old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    for a in (0x33a06,0x33a04,0x33a02): c1dbg.dspw(d,a,0)
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/fx.wav','trim','0',str(secs)])
    r=c1dbg.reports(d,0.4)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    w=wave.open(S+'/fx.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
    return x[4410:],r
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
try:
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    tp(0,19,1); tp(0,1,70); tp(0,4,40)
    for n in (0,6,8,14): aos.wr(d,B+0x400+12*n,bytes([1,110,0,0,0,0,0,0,0,0,0,0]))
    op(1,1); time.sleep(0.5)
    x,r=cap(); base=np.sqrt((x[:,4]**2).mean()); print('%-8s main L rms %.3f peak %.3f  L/R corr %.2f  dry %s overruns %s load %d%%'%('none',base,np.abs(x[:,4]).max(),np.corrcoef(x[:,4],x[:,5])[0,1],int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(2,'0'),16)*100//128))
    tests=[('ECHO',10,1,[90,90,127,84,127,100]),('FILT',17,0,[60,90,40,64,100,127]),('FILT c1',17,1,[60,90,40,64,100,127]),('WARM',18,0,[100,110,40,0,52,127]),('PT2399',20,1,[80,84,90,100,100,90]),('TAPE',21,1,[96,84,90,100,60,90])]
    for name,row,core,kn in tests:
        fx(0,0,0,[0]*12); time.sleep(0.4)
        fx(0,row,core,kn+[0]*6); time.sleep(1.5)
        x,r=cap()
        L,R=x[:,4],x[:,5]
        print('%-8s main L rms %.3f peak %.3f  R rms %.3f  L/R corr %+.2f  dry %s overruns %s load %d%%  core1 late %s'%(name,np.sqrt((L**2).mean()),np.abs(L).max(),np.sqrt((R**2).mean()),np.corrcoef(L,R)[0,1],int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(2,'0'),16)*100//128,int(r.get(5,'0'),16)))
finally:
    op(1,0); time.sleep(0.3); fx(0,0,0,[0]*12); aos.wr(d,B,keep); sq.seqmode(was); print('put back')
