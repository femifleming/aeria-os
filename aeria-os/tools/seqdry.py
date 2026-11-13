"""the sequencer under use: frames the DSP found the stream dry, and the longest the pump was away. seqdry.py"""
import sys,time,struct; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg
d=sq.d
was=sq.seqmode(1)
sq.key(0x28)
def dry(tag,act):
    c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a04,0); c1dbg.dspw(d,0x33a02,0); aos.wr(d,0x100FBEEC,bytes(4)); act(); time.sleep(0.3)
    r=c1dbg.reports(d,0.5); g=struct.unpack('>I',aos.rd(d,0x100FBEEC,4))[0]
    print('%-16s dry frames %d  overruns %d  worst load %d%%  longest pump gap %.2f ms'%(tag,int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(2,'0'),16)*100//128,g/132000))
dry('playing 5 s',lambda: time.sleep(5))
dry('keys+knobs',lambda: [(sq.enc(1,3),sq.key(0x11),sq.key(0x10)) for _ in range(12)])
if 'all' in sys.argv:
    dry('project save',lambda: (sq.key(0x1c),sq.key(2),sq.key(0x2d,1),sq.key(0x31),sq.key(0x2d,0),time.sleep(0.3),sq.key(0x31),time.sleep(1)))
    dry('project load',lambda: (sq.key(2),sq.key(0x31),time.sleep(2)))
    dry('kit browser',lambda: (sq.key(0x22),sq.key(0x2d,1),sq.key(0x31),sq.key(0x2d,0),time.sleep(1.5),sq.key(0x32),sq.key(0x32),sq.key(0x32),sq.key(0x32),time.sleep(0.5)))
sq.key(0x27)
sq.seqmode(was)
