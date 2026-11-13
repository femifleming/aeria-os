"""record the sequencer dry: the inputs off while it records (the loopback cables would feed the outputs back in),
then as they were. seqcap.py out.wav seconds"""
import sys,struct,time,subprocess; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos, place as P
d=P.d; PB=P.SYM['params']
old=[aos.rdc(d,PB+(6*12+k)*16+10,4) for k in (6,7)]
for k in (6,7): aos.wrc(d,PB+(6*12+k)*16+10,struct.pack('>hh',0,0))
time.sleep(0.5)
subprocess.run(['sox','-q','-t','coreaudio','AERIA audio','-c','6','-b','24',sys.argv[1],'trim','0',sys.argv[2]])
for i,k in enumerate((6,7)): aos.wrc(d,PB+(6*12+k)*16+10,old[i])
print('inputs were',[struct.unpack('>hh',o) for o in old])
