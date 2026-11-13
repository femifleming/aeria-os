"""stack effects by writing the chain directly (no keys) and report both cores: stress.py"""
import sys,time,struct; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, place as P, c1dbg
d=P.d; names=['-','PITCH','DIST','GRAINS','RING','RESO','SMEAR','ROSE','COMP','VERB','ECHO','VOWL','WIND','FOG','TWISTER','HAIL','AURORA','FILT','WARM','BBD','PT2399','TAPE']
st=lambda s: 4 if s==0 else 5+(s-1)//2; sf=lambda s: 1 if s==0 else (s-1)&1
def chain(rows):
    for s in range(7): aos.wrc(d,P.SYM['preview_live_fx']+sf(s)*8+st(s),bytes([rows[s] if s<len(rows) else 0]))
def status(tag,settle=6.5):
    time.sleep(settle); c1dbg.dspw(d,0x33a02,0); r=c1dbg.reports(d,0.4); t=time.time(); time.sleep(2.0); r2=c1dbg.reports(d,0.4); dt=time.time()-t
    f=lambda k:(int(r2.get(k,'0'),16)-int(r.get(k,'0'),16))&0xfffff
    cores=struct.unpack('7b',aos.rdc(d,P.SYM['slot_core'],7)); rows=aos.rdc(d,P.SYM['slot_row'],7)
    print('%-40s core0 %3d%% worst %3d%% overruns/s %4.0f | core1 spare passes/s %7.0f late/s %5.0f | %s'%(tag,int(r2.get(1,'0'),16)*100//128,int(r2.get(2,'0'),16)*100//128,f(4)/dt,f(0)/dt,f(5)/dt,' '.join('%s@%d'%(names[rows[i]],cores[i]) for i in range(7) if rows[i])))
    return f(4)/dt, f(5)/dt
if __name__=='__main__':
    chain([]); status('empty')
    for combo in sys.argv[1:]:
        rows=[names.index(x) for x in combo.split('+')]; chain(rows); status(combo)
    chain([1,2,3,4,6]); status('back to the default chain')
