"""how many sample voices the main processor can feed: fake looping voices are switched on and the stream's lead
over the DSP (frames queued ahead, wanted: about 2048) and the main loop's speed are watched. voices.py"""
import sys,struct,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, place as P
d=P.d; V=P.SYM['voice']; FS=P.SYM['flex_smp']
ptr,frames,step=struct.unpack('>III',aos.rdc(d,FS,12)); print('loop sample: %d frames'%frames)
def setv(n):
    for i in range(16):
        on = i < n
        b=struct.pack('>IIIIIIiiiiIBB',FS,(i*977)%max(frames-8,1),0,0x10000+i*997,0,frames,1<<24,0,-1,0,0,0,1 if on else 0)
        aos.wrc(d,V+46*i,b)
def watch(tag,t=3.0):
    lead=[]; l0=struct.unpack('>I',aos.rd(d,0x100F9018,4))[0]; t0=time.time()
    while time.time()-t0<t:
        w=aos.rdc(d,P.SYM['stream_taken'],4); taken,written=struct.unpack('>HH',w); lead.append((written-taken)&0xffff if ((written-taken)&0xffff)<0x8000 else ((written-taken)&0xffff)-0x10000); time.sleep(0.05)
    l1=struct.unpack('>I',aos.rd(d,0x100F9018,4))[0]
    print('%-34s frames queued ahead: least %5d, typical %5d   main loop %5.0f passes/s'%(tag,min(lead),sorted(lead)[len(lead)//2],(l1-l0)/(time.time()-t0)))
if __name__=='__main__':
    for n in (0,4,8,12,16):
        setv(n); time.sleep(0.5); watch('%2d voices'%n)
    import subprocess; p=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24','-n','trim','0','30']); time.sleep(1.5)
    for n in (8,12,16):
        setv(n); time.sleep(0.5); watch('%2d voices + USB recording'%n)
    setv(0); p.wait()
