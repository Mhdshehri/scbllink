# PRUDP v0 reader for SCBLLink captures. Facts from the game's traffic only. By Mhdshehri.
import struct
def rc4(key,data):
    S=list(range(256)); j=0
    for i in range(256):
        j=(j+S[i]+key[i%len(key)])&255; S[i],S[j]=S[j],S[i]
    i=j=0; out=bytearray()
    for b in data:
        i=(i+1)&255; j=(j+S[i])&255; S[i],S[j]=S[j],S[i]
        out.append(b^S[(S[i]+S[j])&255])
    return bytes(out)
TYPES={0:'SYN',1:'CONNECT',2:'DATA',3:'DISCONNECT',4:'PING'}
def parse(p, to_server):
    src,dst,tf,sess=p[0],p[1],p[2],p[3]
    typ,flags=tf&7,tf>>3
    sig,seq=struct.unpack_from('<IH',p,4)
    o=10; r=dict(src=src,dst=dst,type=typ,flags=flags,sess=sess,sig=sig,seq=seq,csum=p[-1])
    if typ in (0,1) and not flags&1 or typ in(0,1):
        r['conn']=struct.unpack_from('<I',p,o)[0]; o+=4
    if typ==2: r['frag']=p[o]; o+=1
    if flags&8:
        n=struct.unpack_from('<H',p,o)[0]; o+=2; r['payload']=p[o:o+n]
    else: r['payload']=p[o:-1]
    return r
