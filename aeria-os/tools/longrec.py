"""long recordings in the multi-effect: all three balloons caught until they are full (15 s each), the unit watched
meanwhile; then popped again. longrec.py"""
import sys,time,struct; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg
d=sq.d; was=sq.seqmode(0)
def rep(tag):
    c1dbg.dspw(d,0x33a04,0); c1dbg.dspw(d,0x33a02,0); c1dbg.dspw(d,0x33a05,0); aos.wr(d,0x100FBEEC,bytes(4)); time.sleep(2); r=c1dbg.reports(d,0.4); g=struct.unpack('>I',aos.rd(d,0x100FBEEC,4))[0]
    print('%-34s overruns %d, core 1 late %d, worst load %d%%, longest pump gap %.1f ms, link %s'%(tag,int(r.get(4,'0'),16),int(r.get(5,'0'),16),int(r.get(2,'0'),16)*100//128,g/132000,aos.info(d)[0][-8:]))
rep('before')
for k in (0x2b,0x2c,0x36): sq.key(k)              # all three start catching
for i in range(9): rep('catching, %2d s'%(2*i+2))
for k in (0x2b,0x2c,0x36): sq.key(k)              # ... and float
rep('three balloons floating (singing)')
time.sleep(4); rep('... 6 s on')
for k in (0x2b,0x2c,0x36): sq.key(0x2d,1); sq.key(k); sq.key(0x2d,0)   # popped
time.sleep(1.5); rep('popped')
sq.seqmode(was)
