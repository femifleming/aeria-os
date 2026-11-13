"""MIDI notes in -> tracks: bytes injected as if from MIDI IN, the result recorded. seqnin.py"""
import sys,time,struct,subprocess,threading; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, sq, place
import numpy as np, wave
d=sq.d; B=0x100C0000; S='/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad'
def op(a,b=0): d.ctrl_transfer(0x40,0x16,a,b,None,timeout=2000); time.sleep(0.02)
def tp(t,i,v): aos.wr(d,(B+64+32*t+i) if i<32 else (B+64+256+32+16+96+16*t+(i-32)),bytes([v]))
def note(ch,n,v=100): op(7,0x90|ch); op(7,n); op(7,v)
def rec(secs,act):
    PB=place.SYM['params']; old=[]
    for k in (6,7): old.append(aos.rdc(d,PB+(6*12+k)*16+10,4)); aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
    p=subprocess.Popen(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',S+'/n.wav','trim','0',str(secs)]); time.sleep(1.0); act(); p.wait()
    for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
    w=wave.open(S+'/n.wav'); n=w.getnframes(); b=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(-1,6,3)
    return (b[:,:,0].astype(np.int32)|(b[:,:,1].astype(np.int32)<<8)|(b[:,:,2].astype(np.int8).astype(np.int32)<<16))[:,4]/8388608.0
def pitch(y):
    i=np.argmax(np.abs(y)>0.02)
    if np.abs(y).max()<0.02: return 0
    z=y[i+400:i+400+8192]; f=np.abs(np.fft.rfft(z*np.hanning(len(z)))); return f.argmax()*44100/len(z)
keep=aos.rd(d,B,0x400+0x6000); was=sq.seqmode(1)
try:
    op(4); op(3,1200); aos.wr(d,B+0x400,bytes(12*64*8))
    for t in range(8): tp(t,19,1); tp(t,1,76); tp(t,4,90)
    f0=pitch(rec(2.5,lambda: note(2,60))); f1=pitch(rec(2.5,lambda: note(2,72))); f2=pitch(rec(2.5,lambda: note(2,67)))
    print('track 3 by MIDI: note 60 %.0f Hz, note 72 %.0f Hz (x%.2f, 2.00 wanted), note 67 %.0f Hz (x%.2f, 1.50 wanted)'%(f0,f1,f1/max(f0,1),f2,f2/max(f0,1)))
    aos.wr(d,B+0x1e,bytes([1])) if False else None
    tp(1,19,5); y=rec(2,lambda: note(1,60)); print('a MIDI track is left alone: peak %.3f'%np.abs(y[20000:]).max())
    tp(1,19,1)
    # live: REC, PLAY, a note on track 5 -> a step with a pitch lock
    sq.key(0x29); op(1,1); time.sleep(0.7); note(4,64); time.sleep(1.5); op(1,0); sq.key(0x29)
    st=aos.rd(d,B+0x400+12*(4*64),12*16); on=[(n,st[12*n+1],st[12*n+2] if st[12*n+2]<128 else st[12*n+2]-256,list(st[12*n+6:12*n+12])) for n in range(16) if st[12*n]&1]
    print('live recording wrote (step, vel, micro, locks):',on,'(PTCH lock is 2, value 80 wanted)')
finally:
    op(1,0); time.sleep(0.3); aos.wr(d,B,keep); sq.seqmode(was); print('put back')
