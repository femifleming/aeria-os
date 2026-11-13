"""the stems over USB: eight tracks, each its own pitch, recorded as 18 channels and checked. stemtest.py [heavy]"""
import sys,time,struct,subprocess; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq, c1dbg, place
import numpy as np, wave
d=sq.d; B=0x100C0000; C='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad/ca/fmt'; S='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
SD=[int(l.split()[0],16) for l in open(S+'/sym.txt') if l.split()[-1]=='stem_dbg'][0]
keep=aos.rd(d,B,0x400+12*64*8); PB=place.SYM['params']
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.05)
def trk(t,i,v): aos.wr(d,B+64+32*t+i,bytes([v]))
old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]; was=0
try:
    for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    was=sq.seqmode(1); op(4); op(3,1200)
    heavy='heavy' in sys.argv
    for t in range(8):
        trk(t,19,1); trk(t,1,52+t*5); trk(t,4,45 if not heavy else 100); trk(t,8,0 if t%2==0 else 127)
        if heavy: trk(t,6,30); trk(t,7,100)
        for n in range(0 if heavy else t,16,1 if heavy else 4): aos.wr(d,B+0x400+12*(t*64+n),bytes([1,100,0,0,0,0,0,0,0,0,0,0]))
    fxn=int(sys.argv[sys.argv.index('fx')+1]) if 'fx' in sys.argv else 0
    rows=[1,2,4,7,8,12,3,13][:fxn]; cores=[1,1,1,1,1,1,0,0]   # within the budgets the UI keeps: core 1 1183 of 1300, core 0 424 of 560
    for t,r in enumerate(rows): aos.wr(d,B+64+256+32+8+t,bytes([cores[t]])); aos.wr(d,B+64+256+32+16+12*t,bytes([64]*6+[0]*6)); aos.wr(d,B+64+256+32+t,bytes([r]))
    op(1,1)
    if 'pat' in sys.argv: op(6,2)
    print(subprocess.run([C,'18'],capture_output=True,text=True).stdout.strip().splitlines()[-1]); time.sleep(1.5)
    for a in (0x33a06,0x33a04,0x33a07,0x33a02): c1dbg.dspw(d,a,0)
    if 'pat' in sys.argv: op(6,2)
    aos.wrc(d,SD,struct.pack('>6I',0,0,0,99999,0,99)); i0=aos.info(d)[1]
    print('pattern flag before the recording',struct.unpack('>i',aos.rdc(d,SD+24,4))[0],'stems pushed',struct.unpack('>I',aos.rdc(d,SD+28,4))[0])
    if 'ffmpeg' in sys.argv: subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','avfoundation','-i',':AERIA audio','-t','6','-c:a','pcm_s24le',S+'/stems.wav'])
    else: subprocess.run(['sox','-t','coreaudio','AERIA audio','-b','24',S+'/stems.wav','trim','0','6'])
    print('pattern flag after the recording',struct.unpack('>i',aos.rdc(d,SD+24,4))[0],'stems pushed',struct.unpack('>I',aos.rdc(d,SD+28,4))[0],'seq mode',aos.rd(d,B+5,1)[0])
    r=c1dbg.reports(d,0.5); i1=aos.info(d)[1]; q1=struct.unpack('>6I',aos.rdc(d,SD,24))
    print('ring full %d, packet queue found empty %d, empty packets %d, ring held %d..%d frames, queue never under %d packets'%q1)
    print('while recording 18 channels: dry',int(r.get(6,'0'),16),'overruns',int(r.get(4,'0'),16),'stem frames skipped',int(r.get(7,'0'),16),'load %d%%'%(int(r.get(2,'0'),16)*100//128),{k:i1[k]-i0[k] for k in ('iso_sent','iso_short')})
    w=wave.open(S+'/stems.wav'); n=w.getnframes(); c=w.getnchannels(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,c,3)
    x=(b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))/8388608.0
    x=x[22050:]; out=[]
    for ch in range(c):
        y=x[:,ch]; f=np.abs(np.fft.rfft(y[:65536]*np.hanning(65536))); out.append('%s %.2f/%.0fHz/%d'%(('mL','mR')[ch] if ch<2 else 'T%d%s'%((ch-2)//2+1,'LR'[ch%2]),np.abs(y).max(),f.argmax()*44100/65536,int((np.abs(np.diff(y))>0.05).sum())))
    print('peak/strongest/jumps:',' | '.join(out[0:2]+out[2::2]))
finally:
    op(6,0); op(1,0)
    for t in range(8): aos.wr(d,B+64+256+32+t,bytes([0]))
    print(subprocess.run([C,'6'],capture_output=True,text=True).stdout.strip().splitlines()[-1]); time.sleep(0.5); aos.wr(d,B,keep); sq.seqmode(was)
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    print('put back')
