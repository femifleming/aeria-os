"""what the sockets carry over time: a steady tone from track 1, panned left, right, middle; the levels second by second. phones.py [secs]"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
secs=int(sys.argv[1]) if len(sys.argv)>1 else 12
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
PB=place.SYM['params']; old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]
db=lambda v: 20*np.log10(max(float(v),1e-6))
try:
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    tp(0,19,1); tp(0,1,100); tp(0,4,127); tp(0,26,int(sys.argv[2]) if len(sys.argv)>2 else 0)
    for n in range(0,16,2): aos.wr(d,B+0x400+12*n,bytes([1,110,0,0,0,0,0,0,0,0,0,0]))
    op(1,1)
    for pan,nm in ((0,'LEFT'),(127,'RIGHT'),(64,'MIDDLE')):
        tp(0,8,pan); time.sleep(0.5)
        subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/ph.wav','trim','0',str(secs)])
        w=wave.open(S+'/ph.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
        x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
        print('track panned',nm)
        for c,cn in ((4,'main L digital'),(5,'main R digital'),(0,'in A'),(1,'in B'),(2,'in C'),(3,'in D')):
            r=[db(np.sqrt((x[i:i+22050,c]**2).mean())) for i in range(4410,len(x)-22050,22050)]
            print('   %-15s dB each half second: %s'%(cn,' '.join('%4.0f'%v for v in r)))
finally:
    op(1,0); time.sleep(0.3)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    aos.wr(d,B,keep); sq.seqmode(was); print('put back')
