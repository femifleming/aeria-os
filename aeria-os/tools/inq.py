"""where each sample of a recorded input comes from: white noise out of main L, by cable into in B. inq.py"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
SYM={l.split()[-1]:int(l.split()[0],16) for l in open(S+'/sym.txt')}; FS=44100; MEM=0x46400000
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
rng=np.random.default_rng(7); ref=0.25*rng.standard_normal(6*FS).clip(-3,3)
st=np.repeat(np.round(ref*8388607).astype('>i4'),2).tobytes()
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
PB=place.SYM['params']; old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]
smp=SYM['st_smp']+14*80; smp_old=aos.rdc(d,smp,14)
try:
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    aos.wr(d,MEM,st); aos.wrc(d,smp,struct.pack('>IIIBB',MEM,len(ref),0x10000,0,0))
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8)); aos.wr(d,B+64+256,bytes([64]))
    tp(0,19,0); tp(0,0,80); tp(0,1,64); tp(0,3,0); tp(0,4,127); tp(0,6,0); tp(0,7,127); tp(0,8,64); aos.wr(d,B+0x400,bytes([1,127,0,0,0,0,0,0,0,0,0,0]))
    p=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/inq.wav','trim','0','6']); time.sleep(1.6); op(1,1); p.wait(); op(1,0)
finally:
    time.sleep(0.3); aos.wrc(d,smp,smp_old)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    aos.wr(d,B,keep); sq.seqmode(was)
w=wave.open(S+'/inq.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
i0=int(np.argmax(np.abs(x[:,4])>0.02))+FS; i0-=i0%16; M=5000
for c,nm in ((1,'in B'),(2,'in C'),(5,'main R (digital, for comparison)')):
    print(nm+': for each place in a block of 16, the delay from main L that fits and how much of the sample it explains')
    out=[]
    for ph in range(16):
        idx=i0+ph+16*np.arange(M); y=x[idx,c]; r=[(abs(np.dot(x[idx-l,4],y)),l) for l in range(-20,140)]; v,l=max(r)
        out.append('%d:%+d (%.0f%%)'%(ph,l,100*v*v/(np.dot(x[idx-l,4],x[idx-l,4])*np.dot(y,y))))
    print('   '+'  '.join(out))
