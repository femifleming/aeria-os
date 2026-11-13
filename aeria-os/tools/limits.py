"""measure what each effect costs on the unit: core 0's frame load (the engine's meter) and core 1's spare passes."""
import sys,struct,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, place as P, c1dbg
d=P.d
names=['-','PITCH','DIST','GRAINS','RING','RESO','SMEAR','ROSE','COMP','VERB','ECHO','VOWL','WIND','FOG','TWISTER','HAIL','AURORA','FILT','WARM','BBD','PT2399','TAPE']
st=lambda s: 4 if s==0 else 5+(s-1)//2; sf=lambda s: 1 if s==0 else (s-1)&1
def live(s): return aos.rdc(d,P.SYM['preview_live_fx'],16)[sf(s)*8+st(s)]
def put(s,row):
    for attempt in range(3):
        if live(s)==row: return True
        P.key(0x22); time.sleep(0.1); P.key(s); time.sleep(0.15)
        if P.i32(P.SYM['sel_slot'])!=s: P.key(0x22); P.key(s); time.sleep(0.15)
        P.key(0x2f if live(s) else 0x31); time.sleep(0.2)
        for _ in range(30):
            cur=P.i32(P.SYM['shelf_row'])
            if cur==row: break
            P.enc(0,1 if row>cur else -1)
        P.key(0x31); time.sleep(0.8)
    return live(s)==row
def c0():
    c1dbg.dspw(d,0x33a02,0); time.sleep(1.0); r=c1dbg.reports(d,0.4); a=int(r.get(4,'0'),16); time.sleep(1.0); r2=c1dbg.reports(d,0.4)
    return int(r2.get(1,'0'),16)*100/128, int(r2.get(2,'0'),16)*100/128, (int(r2.get(4,'0'),16)-a)&0xfffff
def c1():
    a=int(c1dbg.reports(d,0.3)[0],16); t=time.time(); time.sleep(1.0); b=int(c1dbg.reports(d,0.3)[0],16); return ((b-a)&0xfffff)/(time.time()-t)
if __name__=='__main__':
    for s in range(7): put(s,0)
    time.sleep(1); b0=c0(); p0=c1(); print('empty chain: core 0 %.0f%% (worst %.0f%%), core 1 idle passes/s %.0f'%(b0[0],b0[1],p0))
    for row in range(1,12):
        ok=put(0,row); time.sleep(1.2); l=c0(); p=c1()
        on1 = p < p0*0.97
        print('%-7s %s core 0 %5.1f%% (worst %5.1f%%, overruns %d)  core 1 busy %5.1f%%   -> runs on core %d, costs about %.1f%% of it'%(names[row],'' if ok else '(NOT PLACED)',l[0],l[1],l[2],100*(1-p/p0),1 if on1 else 0,100*(1-p/p0) if on1 else l[0]-b0[0]))
        put(0,0); time.sleep(0.6)
