"""fidelity test: put known noise into the intro sample ahead of its play position, record the unit over USB while
it plays, and report how closely each output matches it. noisetest.py [dry]"""
import sys,struct,time,subprocess,math; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, place as P
d=P.d; V=P.SYM['voice']; S='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
def voices():
    b=aos.rdc(d,V,0x2e0); return [(i,b[46*i+44],struct.unpack('>I',b[46*i+4:46*i+8])[0],struct.unpack('>I',b[46*i:46*i+4])[0]) for i in range(16) if b[46*i+45]]
t_end=time.time()+25; got=None
while time.time()<t_end:
    v=[x for x in voices() if x[1]==2]
    if v: got=v[0]; break
    time.sleep(0.2)
if not got: sys.exit('no intro voice')
i,_,p0,sp=got; ptr,frames,step=struct.unpack('>III',aos.rdc(d,sp,14)[:12])
A,N=330000,150000
nzb=open(S+'/noise.raw','rb').read()
t0=time.time(); aos.wr(d,ptr+A*8,nzb); print('noise written in %.1f s'%(time.time()-t0))
if 'dry' in sys.argv:
    PB=P.SYM['params']; old_in=aos.rdc(d,PB+(6*12+6)*16+10,4)+aos.rdc(d,PB+(6*12+7)*16+10,4)
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))      # MIX INAB, INCD off: the loopback cables stay out of it
    d.ctrl_transfer(0x40,0x03,0x2d,1,None,timeout=2000); time.sleep(0.08); P.key(0x31); d.ctrl_transfer(0x40,0x03,0x2d,0,None,timeout=2000); time.sleep(0.3)   # FUNC+YES: past the intro screen
    for s_ in range(0):
        P.key(0x22); P.key(s_)
        for _ in range(14): P.enc(5,-10)
    part=aos.rdc(d,P.SYM['preview_part_fx'],96); st=lambda s: 4 if s==0 else 5+(s-1)//2; sf=lambda s: 1 if s==0 else (s-1)&1
    print('MIX knobs now',[part[st(s)*12+sf(s)*6+5] for s in range(5)])
pos=lambda: dict((a,c) for a,b_,c,_ in voices()).get(i,-1)
while 0<=pos()<A-30000: time.sleep(0.05)
print('pos at capture start',pos())
subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/cap6.wav','trim','0','3.6'])
print('pos after',pos())
if 'dry' in sys.argv:
    aos.wrc(d,PB+(6*12+6)*16+10,old_in[:4]); aos.wrc(d,PB+(6*12+7)*16+10,old_in[4:])
