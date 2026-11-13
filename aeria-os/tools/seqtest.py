"""the sequencer by hand: a SUB kick on track 1 (four to the floor) and a higher SUB on track 2 (off-beats), 120 BPM.
seqtest.py [stop]"""
import sys,struct,time; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, c1dbg
d=aos.dev(); B=0x100C0000
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def trk(t,i,v): aos.wr(d,B+64+32*t+i,bytes([v]))
def step(p,t,n,flags=1,vel=100,micro=0,prob=0,cond=0,ratch=0,locks=()):
    lp=[0,0,0]; lv=[0,0,0]
    for k,(a,b) in enumerate(locks): lp[k]=a+1; lv[k]=b
    aos.wr(d,B+0x400+12*((p*8+t)*64+n),bytes([flags,vel,micro&0xff,prob,cond,ratch]+lp+lv))
if 'stop' in sys.argv: op(1,0); op(0,0); sys.exit()
if 'keep' not in sys.argv:
    op(4)
    trk(0,19,1); trk(0,4,70); trk(0,12,60); trk(0,13,30); trk(0,1,58)
    trk(1,19,1); trk(1,4,40); trk(1,1,82); trk(1,8,20)
    for n in (0,4,8,12): step(0,0,n)
    for n in (2,6,10,14): step(0,1,n)
op(0,1); op(3,1200); op(1,1)
time.sleep(1.5)
h=aos.rd(d,B,16); print('head',h.hex())
def load(tag=''):
    c1dbg.dspw(d,0x33a02,0); c1dbg.dspw(d,0x33a04,0); time.sleep(1.2); r=c1dbg.reports(d,0.5)
    print(tag,'core0 load %d%% worst %d%% overruns %s late1 %s'%(int(r.get(1,'0'),16)*100//128,int(r.get(2,'0'),16)*100//128,r.get(4),r.get(5)))
load('seq playing')
dg=struct.unpack('>3I',aos.rd(d,0x100FBEE0,12)); print('block render ticks last %d (%.0f us)'%(dg[0],dg[0]/132))
print(aos.info(d))
