"""core 0's spare time: turns of the engine's wait loop per frame (now, and the least since zeroed), read through
core 1's reports. load.py [tag]"""
import sys,struct,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, c1dbg, place as P
d=P.d
def load(tag=''):
    c1dbg.dspw(d,0x33a02,0); time.sleep(1.2); r=c1dbg.reports(d,0.5)
    n,m=int(r.get(1,'0'),16),int(r.get(2,'0'),16); print('%-40s core 0 load now %3d%%, worst frame in a second %3d%%'%(tag,n*100//128,m*100//128))
if __name__=='__main__': load(' '.join(sys.argv[1:]))
