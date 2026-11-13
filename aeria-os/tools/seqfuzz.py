"""a soak: random keys and knobs in the sequencer while it plays, watching for dry frames, overruns, a dead DSP or a
fault. The sequence is saved first and put back. seqfuzz.py [seconds]"""
import sys,time,struct,random; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg
d=sq.d; B=0x100C0000; secs=float(sys.argv[1]) if len(sys.argv)>1 else 120
keep=aos.rd(d,B,0x400+0x6000); random.seed(7)
was=sq.seqmode(1)
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000)
# a kit of machines that need no samples, a busy pattern
for t in range(8):
    aos.wr(d,B+64+32*t+19,bytes([1+t%4])); aos.wr(d,B+64+32*t+1,bytes([50+5*t])); aos.wr(d,B+64+32*t+4,bytes([40+5*t]))
    for n in range(t%3,16,2+t%3): aos.wr(d,B+0x400+12*(t*64+n),bytes([1,100,0,0,0,0,0,0,0,0,0,0]))
sq.key(0x28)
keys=list(range(16))+list(range(0x10,0x18))+[0x22,0x23,0x24,0x25,0x26,0x1b,0x1f,0x35,0x20,0x21,0x33,0x34,0x31,0x32,0x29,0x2f,0x28,0x27,0x28,0x18]
for a in (0x33a06,0x33a04,0x33a05,0x33a02): c1dbg.dspw(d,a,0)
t0=time.time(); n=0; held=[]
i0=aos.info(d)
while time.time()-t0<secs:
    r=random.random()
    if r<0.45: sq.enc(random.randrange(7),random.choice([-9,-3,-1,1,3,9]))
    elif r<0.55 and len(held)<3: k=random.randrange(16); sq.key(k,1); held.append(k)        # hold a step (locks)
    elif r<0.65 and held: sq.key(held.pop(),0)
    elif r<0.70: sq.key(0x2d,1); sq.key(random.choice(list(range(0x10,0x18))+[0x29,0x27])); sq.key(0x2d,0)   # FUNC + track (mute), copy, paste
    elif r<0.74: sq.key(0x2e,1); sq.key(random.randrange(4)); sq.key(0x2e,0)                   # a pattern
    else: sq.key(random.choice(keys))
    n+=1
for k in held: sq.key(k,0)
time.sleep(0.5); r=c1dbg.reports(d,0.5); i1=aos.info(d)
print('%d inputs in %.0f s: dry frames %d, overruns %d, core 1 late %d, worst load %d%%, still answering: %s'%(n,time.time()-t0,int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(5,'0'),16),int(r.get(2,'0'),16)*100//128,i1[0]==i0[0]))
sq.key(0x27); time.sleep(0.3)
for k in (0x29,0x2f):
    pass
aos.wr(d,B,keep); sq.seqmode(was)
print('sequence put back')
