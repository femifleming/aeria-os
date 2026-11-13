"""everything at once in the sequencer: eight tracks sounding on most steps, each sending MIDI chords, effects on
four, filters and delay sends on all, the master delay at its longest with full feedback, the clock sent, and the
18-channel stems recorded on the computer meanwhile. torture.py [seconds]"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, sq, c1dbg, place
import numpy as np, wave
d=sq.d; B=0x100C0000; PB=place.SYM['params']; secs=sys.argv[1] if len(sys.argv)>1 else '30'
C='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad/ca/fmt'; S='/private/tmp/claude-501/-Users-femishonugafleming-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
MD,MO,MLN=[int(x,16) for x in sys.argv[2:5]] if len(sys.argv)>4 else (0,0,0)
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.03)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def par(pg,i,v=None):
    a=PB+(pg*12+i)*16+10
    if v is None: return aos.rdc(d,a,4)
    aos.wrc(d,a,v if isinstance(v,bytes) else struct.pack('>hh',v,v))
def u32(a): return struct.unpack('>I',aos.rdc(d,a,4))[0] if a else 0
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
old={(pg,i):par(pg,i) for pg,i in ((6,6),(6,7),(10,0),(10,1),(10,4),(10,5))}
try:
    for k in (6,7): par(6,k,0)                                  # inputs off: the loopback cables stay out of it
    op(4); time.sleep(0.1); op(0,1); op(3,1300)
    aos.wr(d,B+6,bytes([1,1]))                                   # the clock sent; program changes on channel 1
    mach=[0,0,0,0,2,3,4,1]                                       # four samples from the kit, FM, NOIS, PLUK, SUB
    for t in range(8):
        tp(t,19,mach[t]); tp(t,0,96+t); tp(t,1,60+t*3); tp(t,4,70 if mach[t] else 127); tp(t,6,25); tp(t,7,105); tp(t,9,90); tp(t,10,20)
        tp(t,20,t+1); tp(t,21,48+t); tp(t,41,7); tp(t,42,6)     # MIDI: channel t+1, MAJ7 chords, gate 6/8
        for n in range(16):
            if (n+t)%2==0 or t>5: aos.wr(d,B+0x400+12*(t*64+n),bytes([1,100,0,0,0,3 if (n==7 and t==2) else 0,0,0,0,0,0,0]))
    for t,(r,c) in enumerate(((1,1),(2,1),(4,1),(3,0))):         # PITCH, DIST, RING on core 1, GRAINS on core 0
        aos.wr(d,B+64+256+32+8+t,bytes([c])); aos.wr(d,B+64+256+32+16+12*t,bytes([64,64,64,64,64,100,0,0,0,0,0,0])); aos.wr(d,B+64+256+32+t,bytes([r]))
    par(10,0,16); par(10,1,127); par(10,4,127); par(10,5,110)    # master delay: 16 sixteenths, feedback full, send full, wet
    time.sleep(0.5); op(1,1)
    print(subprocess.run([C,'18'],capture_output=True,text=True).stdout.strip().splitlines()[-1]); time.sleep(2)
    for a in (0x33a06,0x33a04,0x33a05,0x33a07,0x33a02): c1dbg.dspw(d,a,0)
    aos.wr(d,0x100FBEEC,bytes(4)); i0=aos.info(d)[1]; m0=(u32(MD),u32(MO),u32(MLN))
    subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-b','24',S+'/torture.wav','trim','0',secs])
    r=c1dbg.reports(d,0.5); i1=aos.info(d)[1]; m1=(u32(MD),u32(MO),u32(MLN)); g=struct.unpack('>I',aos.rd(d,0x100FBEEC,4))[0]
    print('DSP: dry frames %d, overruns %d, core 1 late %d, stem frames skipped %d, worst load %d%%; longest pump gap %.1f ms'%(int(r.get(6,'0'),16),int(r.get(4,'0'),16),int(r.get(5,'0'),16),int(r.get(7,'0'),16),int(r.get(2,'0'),16)*100//128,g/132000))
    print('MIDI: %d messages sent, %d dropped, %d bytes came back through the cable; USB: %d packets, %d empty'%(m1[1]-m0[1],m1[0]-m0[0],m1[2]-m0[2],i1['iso_sent']-i0['iso_sent'],i1['iso_short']-i0['iso_short']))
    w=wave.open(S+'/torture.wav'); n=w.getnframes(); c=w.getnchannels(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,c,3)
    x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
    x=x[44100:]; out=[]
    for ch in range(c):
        y=x[:,ch]; out.append('%s pk %.2f rms %.3f clip %d'%(('mainL','mainR')[ch] if ch<2 else 'T%d%s'%((ch-2)//2+1,'LR'[ch%2]),np.abs(y).max(),np.sqrt((y*y).mean()),int((np.abs(y)>0.985).sum())))
    print('recording %d ch, %.1f s:'%(c,len(x)/44100)); print('  '+'\n  '.join(out[:2]+out[2::2]))
    # the master: is it stuck, growing, or DC?
    for ch in (0,1):
        y=x[:,ch]; k=44100; lv=[np.sqrt((y[i:i+k]**2).mean()) for i in range(0,len(y)-k,k)]
        print('  main %s level second by second: %s  dc %.4f'%('LR'[ch],' '.join('%.2f'%v for v in lv[:30]),y.mean()))
finally:
    op(1,0); subprocess.run([C,'6'],capture_output=True)
    for (pg,i),v in old.items(): par(pg,i,v)
    for t in range(8): aos.wr(d,B+64+256+32+t,bytes([0]))
    time.sleep(0.5); aos.wr(d,B,keep); sq.seqmode(was); print('put back; face:', 'sequencer' if aos.rd(d,B+5,1)[0] else 'AERIA')
