import sys,struct,time,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, place as P
d=P.d; V=P.SYM['voice']; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
def voices():
    b=aos.rdc(d,V,0x2e0); return [(i,b[46*i+44],struct.unpack('>I',b[46*i+4:46*i+8])[0],struct.unpack('>I',b[46*i:46*i+4])[0]) for i in range(16) if b[46*i+45]]
t_end=time.time()+25; got=None
while time.time()<t_end:
    v=[x for x in voices() if x[1]==2]
    if v: got=v[0]; break
    time.sleep(0.2)
if not got: sys.exit('no intro voice')
if len(sys.argv)>1:      # dry chain: every MIX down (PITCH GRAINS SMEAR at 64, DIST RING at 127)
    for s_ in range(5):
        P.key(0x22); P.key(s_)
        for _ in range(14): P.enc(5,-10)
i,_,p1,sp=got; p1=dict((a,c) for a,b_,c,_ in voices())[i]; t0=time.time(); time.sleep(1.0); v2=dict((a,c) for a,b_,c,_ in voices()); dt=time.time()-t0
print('stream rate alone %.0f frames/s'%((v2.get(i,p1)-p1)/dt))
smp=aos.rdc(d,sp,14); ptr,frames,step=struct.unpack('>III',smp[:12])
p=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/cap5.wav','trim','0','3'])
time.sleep(0.5); pos1=dict((a,c) for a,b_,c,_ in voices()).get(i,0); ta=time.time(); time.sleep(1.5); pos2=dict((a,c) for a,b_,c,_ in voices()).get(i,0); tb=time.time(); p.wait()
print('stream rate while USB audio records %.0f frames/s'%((pos2-pos1)/(tb-ta)))
a=max(0,pos1-60000); n=min(frames-a, 60000+int(4*44100)); raw=b''
for o in range(0,n*8,4096): raw+=aos.rd(d,ptr+a*8+o,min(4096,n*8-o))
open(S+'/intro.raw','wb').write(raw)
