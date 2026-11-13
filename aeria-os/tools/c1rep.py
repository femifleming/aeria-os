import sys,struct,time; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos
d=aos.dev(); b=aos.rd(d,0x100F9000,4*33); v=struct.unpack('>33I',b)
print('uart5=%x uart6=%x sent=%x timeouts=%x isr_after=%x beats=%x'%(v[12],v[13],v[25],v[26],v[27],v[29]))
st=lambda c: bytes(d.ctrl_transfer(0xC0,0x10,c,0,16,timeout=2000))
for rnd in range(2):
    out={}; e=time.time()+1.5
    while time.time()<e:
        r=st(1)
        if r[3]: x=r[4]<<16|r[5]<<8|r[6]; out.setdefault(x>>20,[]).append('%05x'%(x&0xfffff))
    print({k:v[-2:] for k,v in sorted(out.items())}, st(1)[:3].hex())
