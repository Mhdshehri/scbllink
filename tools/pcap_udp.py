# reads scbllink pcaps (linktype 228); yields (t, proto, src, sport, dst, dport, payload)
import struct
def packets(path):
    d=open(path,'rb').read(); o=24; t0=None
    while o+16<=len(d):
        ts,us,incl,_=struct.unpack_from('<IIII',d,o); o+=16
        p=d[o:o+incl]; o+=incl
        t=ts+us/1e6; t0=t0 or t
        ihl=(p[0]&15)*4; pr=p[9]
        sp,dp=struct.unpack_from('>HH',p,ihl)
        pay=p[ihl+8:] if pr==17 else p[ihl+((p[ihl+12]>>4)*4):]
        yield t-t0,pr,'.'.join(map(str,p[12:16])),sp,'.'.join(map(str,p[16:20])),dp,pay
