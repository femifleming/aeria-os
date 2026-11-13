"""the stream's throughput against DSP load in the sequencer. seqpump.py (dev build; sp_dbg at 0x4006da94)"""
import sys,time,struct; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg
d=sq.d; B=0x100C0000; A=0x4006dd60
keep=aos.rd(d,B,0x400+12*64*8)
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def trk(t,i,v): aos.wr(d,B+64+32*t+i,bytes([v]))
def measure(tag,secs=3):
    c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a02,0); c1dbg.dspw(d,0x33a04,0); c1dbg.dspw(d,0x33a05,0); aos.wrc(d,A,bytes(24)); t0=time.time(); time.sleep(secs)
    v=struct.unpack('>6I',aos.rdc(d,A,24)); dt=time.time()-t0; r=c1dbg.reports(d,0.5)
    print('%-26s dry %4d over %4d late1 %3d load %3d%% | words %6.0f/s give-ups %4.0f/s pump %4.1f%% of the main processor'%(tag,int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(5,'0'),16),int(r.get(2,'0'),16)*100//128,v[1]/dt,v[2]/dt,v[3]/132e4/dt))
def fx(t,row,core=0):
    aos.wr(d,B+64+256+32+8+t,bytes([core])); aos.wr(d,B+64+256+32+16+12*t,bytes([64]*6+[0]*6)); aos.wr(d,B+64+256+32+t,bytes([row])); time.sleep(0.6)
try:
    op(0,1); op(4); op(3,1400)
    for t in range(8):
        trk(t,0,96+t); trk(t,1,71); trk(t,4,127); aos.wr(d,B+0x400+12*(t*64),bytes([1,90,0,0,0,0,0,0,0,0,0,0])*16)
    op(1,1); measure('8 tracks')
    for t in range(8): trk(t,6,30); trk(t,7,100)
    measure('+ filters on all')
    tot=0
    for t,r in enumerate((int(x) for x in (sys.argv[-1] if len(sys.argv)>1 else '1,2,3').split(','))):
        c=0
        if r<0: r=-r; c=1
        fx(t,r,c); measure('+ row %d on T%d core %d'%(r,t+1,c))
finally:
    op(1,0)
    for t in range(8): aos.wr(d,B+64+256+32+t,bytes([0]))
    time.sleep(0.5); aos.wr(d,B,keep); op(0,keep[5]); print('put back')
