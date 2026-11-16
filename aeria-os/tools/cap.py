"""record the unit's six USB channels (A B C D main-L main-R) with sox; RMS dB per channel per half second.
cap.py SECONDS [name]   (files go to ~/octa/aeria-os/build/cap/)"""
import sys,subprocess,wave,numpy as np,os
S=os.path.expanduser('~/octa/aeria-os/build/cap'); os.makedirs(S,exist_ok=True)
def rec(secs,name='cap'):
    f=S+'/'+name+'.wav'
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',f,'trim','0',str(secs)],check=True)
    return load(f)
def load(f):
    w=wave.open(f); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    return (b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
def db(v): return 20*np.log10(max(float(v),1e-7))
def report(x,step=22050):
    names=['A','B','C','D','L','R']
    for c in range(6):
        r=[db(np.sqrt((x[i:i+step,c]**2).mean())) for i in range(0,len(x)-step+1,step)]
        print('  %-2s %s'%(names[c],' '.join('%5.0f'%v for v in r)))
def xc(x,a,c,name='',span=600):
    """how channel c follows channel a: delay, gain, residual (dB)"""
    ya=x[:,a]-x[:,a].mean(); yc=x[:,c]-x[:,c].mean()
    s=ya[10000:60000]; t=yc[10000-span:60000+span]
    co=np.correlate(t,s,'valid'); k=int(np.argmax(np.abs(co))); dly=k-span
    tt=yc[10000+dly:60000+dly]; g=(s*tt).sum()/max((s*s).sum(),1e-12); r=tt-g*s
    res=db(np.sqrt((r*r).mean()/max((tt*tt).mean(),1e-12)))
    print('  %s: delay %+d, gain %.3f (%.1f dB), residual %.1f dB'%(name,dly,g,db(abs(g)),res)); return dly,g,res
if __name__=='__main__':
    secs=float(sys.argv[1]) if len(sys.argv)>1 else 3
    x=rec(secs,sys.argv[2] if len(sys.argv)>2 else 'cap'); print('frames',len(x)); report(x)
