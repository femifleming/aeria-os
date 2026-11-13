"""audio quality, digital and through the cables (main L -> in B, phones -> in C): a test sample is put into slot 6.1
and played by track 1; the unit is recorded over USB (A B C D main L R). audioq.py"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, place, c1dbg
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
SYM={l.split()[-1]:int(l.split()[0],16) for l in open(S+'/sym.txt')}
FS=44100; MEM=0x46400000; TAG='0'
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
db=lambda v: 20*np.log10(max(float(v),1e-12))
# the test signal: 2 s of 997 Hz at -6 dB, 2 s sweep 20 Hz..20 kHz at -12 dB, 1 s silence, 1 s 997 Hz at -40 dB
t=np.arange(2*FS)/FS; sine=0.5*np.sin(2*np.pi*997*t)
k=np.log(20000/20)/2.0; sweep=0.25*np.sin(2*np.pi*20*(np.exp(k*t)-1)/k); fade=np.minimum(1,np.minimum(np.arange(2*FS),np.arange(2*FS)[::-1])/400.0)
low=0.01*np.sin(2*np.pi*997*np.arange(FS)/FS)
ref=np.concatenate([sine*fade,sweep*fade,np.zeros(FS),low*fade[:FS]*np.minimum(1,np.arange(FS)[::-1]/400.0)])
pcm=np.round(ref*8388607).astype('>i4'); st=np.repeat(pcm,2).tobytes()
def cap(secs,act=None):
    p=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/q%s.wav'%(TAG if act else '0'),'trim','0',str(secs)])
    if act: time.sleep(1.6); act()
    p.wait()
    w=wave.open(S+'/q%s.wav'%(TAG if act else '0')); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    return (b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
def thdn(y,f0=997):
    n=len(y); t=np.arange(n)/FS; best=None
    for f in np.linspace(f0-0.5,f0+0.5,41):
        A=np.stack([np.sin(2*np.pi*f*t),np.cos(2*np.pi*f*t),np.ones(n)],1); h=np.linalg.lstsq(A,y,rcond=None)[0]; e=y-A@h; p=(e**2).mean()
        if best is None or p<best[0]: best=(p,f,h,e)
    p,f,h,e=best; amp=np.hypot(h[0],h[1]); E=np.abs(np.fft.rfft(e*np.hanning(n)))**2; fr=np.fft.rfftfreq(n,1/FS)
    band=(fr>=20)&(fr<=20000); tot=E[band].sum(); kf=f*n/FS
    harm=sum(E[int(round(kf*hh))-4:int(round(kf*hh))+5].sum() for hh in range(2,10) if kf*hh+5<len(E))
    sig=(amp**2/2)*n*n*0.375/2*2/ n * n /n if False else None
    scale=(np.hanning(n)**2).sum()*n/2          # power of a unit-variance signal in this spectrum
    return f,db(amp),10*np.log10(max(tot/scale,1e-30)/(amp**2/2)),10*np.log10(max(harm/scale,1e-30)/(amp**2/2))
def resp(y,r):
    n=len(r); Y=np.fft.rfft(y[:n]*np.hanning(n)); R=np.fft.rfft(r*np.hanning(n)); fr=np.fft.rfftfreq(n,1/FS); out=[]
    H=np.abs(Y)**2; G=np.abs(R)**2
    for f in (20,30,40,50,70,100,200,1000,5000,10000,15000,18000,20000):
        m=(fr>f/1.06)&(fr<f*1.06); out.append((f,10*np.log10(H[m].sum()/G[m].sum())))
    z=dict(out)[1000]; return [(f,v-z) for f,v in out]
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
PB=place.SYM['params']; old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]
smp=SYM['st_smp']+14*80; smp_old=aos.rdc(d,smp,14)
try:
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))     # the inputs are not mixed in: no loop through the cables
    t0=time.time(); aos.wr(d,MEM,st); aos.wrc(d,smp,struct.pack('>IIIBB',MEM,len(ref),0x10000,0,0)); print('test sample written in %.0f s'%(time.time()-t0))
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    for tr in range(8): aos.wr(d,B+0x30+tr*4 if False else B+10,bytes([0]))
    for p in range(1): aos.wr(d,B+64+256+32-32+0,bytes([64]))                 # pattern 1, track 1: 64 steps (8 s)
    tp(0,19,0); tp(0,0,80); tp(0,1,64); tp(0,3,0); tp(0,4,127); tp(0,6,0); tp(0,7,127); tp(0,8,64)
    aos.wr(d,B+0x400,bytes([1,127,0,0,0,0,0,0,0,0,0,0]))
    print('track 1 length byte',aos.rd(d,B+64+256,8).hex())
    # silence first: every input's and output's own noise
    x=cap(3); print('\nNOISE, nothing playing (dB of full scale, 20 Hz-20 kHz not weighted):')
    for c,nm in enumerate(('in A (nothing plugged)','in B (main L by cable)','in C (phones by cable)','in D (nothing plugged)','main L (digital)','main R (digital)')):
        y=x[FS//2:,c]; print('  %-26s rms %6.1f dB  peak %6.1f dB  dc %+.5f'%(nm,db(np.sqrt(((y-y.mean())**2).mean())),db(np.abs(y-y.mean()).max()),y.mean()))
    for pan,tag in ((64,'pan in the middle'),(0,'pan hard left')):
        TAG=str(pan); tp(0,8,pan); time.sleep(7.5 if pan!=64 else 0); c1dbg.dspw(d,0x33a06,0); c1dbg.dspw(d,0x33a04,0); [c1dbg.dspw(d,int(sys.argv[i+1],16),int(sys.argv[i+2],16)) for i,v in enumerate(sys.argv) if v=='poke']; x=cap(9.5,lambda: op(1,1)); op(1,0); r=c1dbg.reports(d,0.4); time.sleep(0.5)
        e=np.abs(x[:,4])+np.abs(x[:,5]); i0=int(np.argmax(e>0.02))-12; print('\nPLAYING, %s (starts %.2f s into the take; dry %d, overruns %d)'%(tag,i0/FS,int(r.get(6,'0'),16),int(r.get(4,'0'),16)))
        for c,nm in ((4,'main L digital'),(5,'main R digital'),(1,'main L -> cable -> in B'),(2,'phones -> cable -> in C')):
            y=x[i0:,c]; a=y[int(0.4*FS):int(1.6*FS)]
            if np.abs(a).max()<1e-4: print('  %-24s silent (peak %.1f dB)'%(nm,db(np.abs(a).max()))); continue
            f,lv,tn,th=thdn(a); sil=y[int(4.25*FS):int(4.85*FS)]; lo=y[int(5.2*FS):int(5.8*FS)]; fl,ll,tl,_=thdn(lo)
            print('  %-24s 997 Hz reads %.2f Hz at %6.2f dB | THD+N %6.1f dB (harmonics alone %6.1f) | gap noise %6.1f dB | the -40 dB tone: %6.2f dB, THD+N %5.1f'%(nm,f,lv,tn,th,db(np.sqrt(((sil-sil.mean())**2).mean())),ll,tl))
            if pan==64:
                dly=0
                if c in (1,2):
                    cc=np.correlate(y[int(2.0*FS):int(2.0*FS)+30000],x[i0+int(2.0*FS):i0+int(2.0*FS)+30000-600,4],'valid'); dly=int(np.argmax(np.abs(cc))); 
                rr=resp(y[2*FS+dly:],ref[2*FS:4*FS]); print('      response against 1 kHz: '+'  '.join('%s %+.1f'%(('%d'%f if f<1000 else '%dk'%(f//1000)),v) for f,v in rr)+('   (cable delay %d samples)'%dly if c in (1,2) else ''))
finally:
    op(1,0); time.sleep(0.3); aos.wrc(d,smp,smp_old)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    aos.wr(d,B,keep); sq.seqmode(was); print('put back (sample bank 6 in RAM was written over: a restart reloads it)')
