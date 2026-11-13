"""drive the sequencer's UI by hand: sq.py k CODE [0|1] | e ENC DELTA | s (screen) | w SECONDS ... (several in a row)"""
import sys,time,struct; sys.path.insert(0,'/Users/you/octa/aeria-os/tools'); import aos
d=aos.dev()
def key(c,p=None):
    for q in ([p] if p is not None else [1,0]): d.ctrl_transfer(0x40,0x03,c,q,None,timeout=2000); time.sleep(0.06)
def enc(e,dl): d.ctrl_transfer(0x40,0x04,e,dl&0xFF,None,timeout=2000); time.sleep(0.06)
def screen():
    f=bytes(d.ctrl_transfer(0xC0,0x05,0,0,1024,timeout=2000)); w=struct.unpack('>256I',f)
    rows=[''.join('#' if (w[2*x+(1 if y<32 else 0)]>>(y&31))&1 else ' ' for x in range(128)) for y in range(64)]
    # two lines of the screen per text row (half blocks) to keep it short
    for y in range(0,64,2): print(''.join({'  ':' ','# ':'▀',' #':'▄','##':'█'}[a+b] for a,b in zip(rows[y],rows[y+1])).rstrip())
def seqmode(on):
    """put the OS's face where the test needs it (FUNC + MIDI) and say where it was: read from the UI's own flag,
    not from the sequence's head (tests rewrite that)"""
    import aos
    a=None
    for l in open('/private/tmp/claude-501/-Users-you-octa/a6e8c4e0-16ca-4199-84ec-2e9cf68c9ec3/scratchpad/sym.txt'):
        f=l.split()
        if f[-1]=='seq_on': a=int(f[0],16)
    was=aos.rdc(d,a,1)[0]==1
    for _ in range(3):                      # (a first key may only wake the screen from a saver)
        if (aos.rdc(d,a,1)[0]==1)==bool(on): break
        key(0x2d,1); key(0x35); key(0x2d,0); time.sleep(1.2)
    if was!=bool(on):
        assert (aos.rdc(d,a,1)[0]==1)==bool(on), 'the face did not change'
    return was
if __name__=='__main__':
    a=sys.argv[1:]; i=0
    while i<len(a):
        c=a[i]
        if c=='k':
            if i+2<len(a) and a[i+2] in('0','1'): key(int(a[i+1],0),int(a[i+2])); i+=3
            else: key(int(a[i+1],0)); i+=2
        elif c=='e': enc(int(a[i+1]),int(a[i+2])); i+=3
        elif c=='s': screen(); i+=1
        elif c=='w': time.sleep(float(a[i+1])); i+=2
        else: sys.exit('?'+c)
