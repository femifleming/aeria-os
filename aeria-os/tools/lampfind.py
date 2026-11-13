"""find the unmapped lamps with someone watching the panel: lampfind.py ID [LEVEL]  (even ids 0..134; level 0..15),
or lampfind.py sweep  (each unmapped lamp in turn, two seconds each, its id printed)"""
import sys,time; sys.path.insert(0,'/Users/femishonugafleming/octa/aeria-os/tools'); import aos
d=aos.dev()
KNOWN={0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,40,42,44,46,48,50,52,54,32,34,38,66,68,86,80,82,70,64,88,90,84,102,120,122}
def lamp(i,lv): d.ctrl_transfer(0x40,0x16,8,(lv&15)<<8|(i&255),None,timeout=2000); time.sleep(0.03)
if sys.argv[1]=='sweep':
    for i in range(0,136,2):
        if i in KNOWN: continue
        lamp(i,15); print('lamp id',i,'(row %d bit %d)'%(i>>3,i&7),flush=True); time.sleep(2); lamp(i,0)
elif sys.argv[1]=='set':
    lv=int(sys.argv[2])
    for i in sys.argv[3:]: lamp(int(i),lv)
elif sys.argv[1]=='all':
    for i in range(0,136):
        if (i&~1) not in KNOWN: lamp(i,int(sys.argv[2]))
else: lamp(int(sys.argv[1]),int(sys.argv[2]) if len(sys.argv)>2 else 15)
