import sys,time; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import place as P, load as L, aos, c1dbg
d=P.d
def counts(t=2.0):
    a=c1dbg.reports(d,0.4); time.sleep(t); b=c1dbg.reports(d,0.4)
    f=lambda k: (int(b.get(k,'0'),16)-int(a.get(k,'0'),16))&0xfffff
    return 'frames over half: %d/s, overruns: %d/s'%(f(3)/(t+0.4),f(4)/(t+0.4))
def show(tag): L.load(tag); print('    ',counts())
if __name__=='__main__': show(' '.join(sys.argv[1:]))
