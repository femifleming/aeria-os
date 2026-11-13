"""core 1 helpers: dspw(d, addr, val) writes DSP X memory through core 0's engine; reports(d) reads core 1's debug words."""
import sys,struct,time; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos
def st(d,c): return bytes(d.ctrl_transfer(0xC0,0x10,c,0,16,timeout=2000))
def dspw(d,addr,val,y=0):
    d.ctrl_transfer(0x40,0x10,0,4,struct.pack('>II',(0x80000000 if y else 0)|addr,val&0xffffff),timeout=2000)
def reports(d,t=1.0):
    out={}; e=time.time()+t
    while time.time()<e:
        r=st(d,1)
        if r[3]: x=r[4]<<16|r[5]<<8|r[6]; out[x>>20]=x&0xfffff
    return {k:'%05x'%v for k,v in sorted(out.items())}
def load(d,path,org):
    b=open(path,'rb').read()
    for i in range(0,len(b),3): dspw(d,org+i//3,b[i]|b[i+1]<<8|b[i+2]<<16)
def run_slot7(d,org):
    e=0x33A80
    for o,v in ((0,0),(1,org+2),(2,org),(3,0x33C00),(4,0x1700),(5,0),(6,1),(8,0),(9,0x33FC0),(10,0x33FE0),(11,0x1907),(7,1),(0,1)): dspw(d,e+o,v)
