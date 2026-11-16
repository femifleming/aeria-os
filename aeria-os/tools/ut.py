"""on-unit test helpers (15 Nov 2026): symbols, keys, screen, params, samples, DSP peek, capture.
import with: sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); from ut import *
Needs build/sym.txt from tools/sym.sh (the dev build's symbols)."""
import sys,time,struct,os; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos, c1dbg, cap
SYMF='/Users/you/octa/aeria-os/build/sym.txt'
SYM={l.split()[-1]:int(l.split()[0],16) for l in open(SYMF)}
d=aos.dev()
K=dict(TRIG=0x00,TRACK=0x10,SCENEA=0x19,SCENEB=0x1a,PAGE=0x1b,PROJ=0x1c,PART=0x1d,AED=0x1e,ARR=0x1f,DOWN=0x20,RIGHT=0x21,SRC=0x22,AMP=0x23,LFO=0x24,FX1=0x25,FX2=0x26,STOP=0x27,PLAY=0x28,REC=0x29,CUE=0x2a,RECAB=0x2b,RECCD=0x2c,FUNC=0x2d,PATTERN=0x2e,BANK=0x2f,MIX=0x30,YES=0x31,NO=0x32,UP=0x33,LEFT=0x34,MIDI=0x35,REC3=0x36,PUSH=0x38,LEVELPUSH=0x3e,TEMPO=0x18)
def key(c,p=None,gap=0.06):
    for q in ([p] if p is not None else [1,0]):
        for tries in range(6):
            try: d.ctrl_transfer(0x40,0x03,c,q,None,timeout=2000); break
            except Exception as e:
                if tries == 5: raise
                time.sleep(1.0)   # the OS is busy (a project being copied): the request is answered when it is back
        time.sleep(gap)
def combo(hold,c): key(hold,1); key(c); key(hold,0)
def enc(e,dl): d.ctrl_transfer(0x40,0x04,e,dl&0xFF,None,timeout=2000); time.sleep(0.06)
def screen():
    f=bytes(d.ctrl_transfer(0xC0,0x05,0,0,1024,timeout=2000)); w=struct.unpack('>256I',f)
    r=[''.join('#' if (w[2*x+(1 if y<32 else 0)]>>(y&31))&1 else ' ' for x in range(128)) for y in range(64)]
    for y in range(0,64,2): print(''.join({'  ':' ','# ':'▀',' #':'▄','##':'█'}[a+b] for a,b in zip(r[y],r[y+1])).rstrip())
def u8(name,off=0): return aos.rdc(d,SYM[name]+off,1)[0]
def i32(name,off=0): return struct.unpack('>i',aos.rdc(d,SYM[name]+off,4))[0]
def u32(name,off=0): return struct.unpack('>I',aos.rdc(d,SYM[name]+off,4))[0]
def u16(name,off=0): return struct.unpack('>H',aos.rdc(d,SYM[name]+off,2))[0]
def toast(): return aos.rdc(d,SYM['toast'],24).split(b'\0')[0].decode()
PB=SYM['params']
def par(pg,i,v=None):
    a=PB+(pg*12+i)*16+10
    if v is None: return struct.unpack('>hh',aos.rdc(d,a,4))
    aos.wrc(d,a,struct.pack('>hh',v,v))
def seq_on(): return u8('seq_on')==1
def seqmode(on):
    for _ in range(3):
        if seq_on()==bool(on): return
        combo(0x2d,0x35); time.sleep(1.0)
    assert seq_on()==bool(on),'face did not change'
def smp(i):
    b=aos.rdc(d,SYM['st_smp']+14*i,14); p,f,s=struct.unpack('>III',b[:12]); return (p,f,s,b[12])
def loads(t=1.0):
    r=c1dbg.reports(d,t); g=lambda k: int(r[k],16) if k in r else -1
    return 'core0 load %d%% worst %d%% over %d dry %d late1 %d'%(g(1)*100//128 if g(1)>=0 else -1,g(2)*100//128 if g(2)>=0 else -1,g(4),g(6),g(5))
def dry(): return int(c1dbg.reports(d,0.6).get(6,'0'),16)
def voices():
    b=aos.rdc(d,SYM['voice'],0x300); return [(i,b[48*i+44],b[48*i+45]) for i in range(16) if b[48*i+45]]
def info(): return aos.info(d)
def alive(tag=''):
    try: n1=info()[1]['dsp_words']; time.sleep(0.25); n2=info()[1]['dsp_words']; ok=n2>n1
    except Exception: ok=False
    if not ok: print('!! NOT ALIVE',tag); sys.exit(1)
    return True
def peek(addr,wait=0.25):
    """an X-memory word (its low 20 bits) through meter 5 (engine.asm bring-up peek at X:$2fe); the max seen over `wait`"""
    c1dbg.dspw(d,0x2fe,addr); time.sleep(0.05)
    aos.wrc(d,SYM['meter_max']+20,bytes(4)); time.sleep(wait)
    return struct.unpack('>I',aos.rdc(d,SYM['meter_max']+20,4))[0]
def peek_off(): c1dbg.dspw(d,0x2fe,0)
def inputs_off():
    """MIX INAB / INCD off for a measurement (the loopback cables would feed back); returns what to give back"""
    o=(par(6,6),par(6,7)); par(6,6,0); par(6,7,0); return o
def inputs_back(o): par(6,6,o[0][0]); par(6,7,o[1][0])
B=0x100C0000
def seq_backup(): return aos.rd(d,B,0x400+0x6000)
def seq_restore(k): aos.wr(d,B,k)
