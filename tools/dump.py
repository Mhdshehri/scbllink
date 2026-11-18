import sys, zlib, struct
sys.path.insert(0,'.')
from pcap_udp import packets
from prudp0 import *
def messages(path):
    buf={}; seen=set()
    for t,pr,s,sp,d,dp,pay in packets(path):
        if pr!=17 or not({sp,dp}&{21126,21127}): continue
        c2s = dp in(21126,21127); port=dp if c2s else sp
        r=parse(pay,c2s)
        if r['type']!=2 or not r['payload']: continue
        key=(c2s,port,r['sess'] if c2s else r['dst'])
        if (key,r['seq']) in seen: continue  # sent again (its acknowledgement was lost)
        seen.add((key,r['seq']))
        dec=rc4(b'CD&ML',r['payload'])
        # fragments: frag 0 = last/only
        buf.setdefault(key,b'')
        buf[key]+=inflate(dec)
        if r['frag']==0:
            m=buf.pop(key); yield t,c2s,port,r['seq'],m
def inflate(m):
    if m[0]==0: return m[1:]
    return zlib.decompress(m[1:])
if __name__=='__main__':
    for t,c2s,port,seq,m in messages(sys.argv[1]):
        b=m
        print(f"{t:7.2f} {'C>S' if c2s else 'S>C'} {port} #{seq} len={len(b)} {b[:70].hex()}")
