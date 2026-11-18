# Sends damaged versions of recorded calls to a test server (run it under a sanitizer). By Mhdshehri.
import sys, os, random, struct, zlib, socket
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv_saved=sys.argv; 
import importlib.util
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'replay.py')).read().rsplit('main()',1)[0]
exec(src)
path,ap,sp=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]); random.seed(int(sys.argv[4]) if len(sys.argv)>4 else 1)
def login(me):
    c=Client(ap,me); name=b'fz'
    any_=struct.pack('<H',3)+name+b'\0'+b'\x02\x00k\x00'+b'\x02\x00p\x00'
    m=b'\x8a'+struct.pack('<II',1,2)+struct.pack('<H',3)+name+b'\0'+b'\x21\x00UbiAuthenticationLoginCustomData\x00'+struct.pack('<II',len(any_)+4,len(any_))+any_
    a=c.call(struct.pack('<I',len(m))+m); y=rmc.R(a[14:]); assert y.u('I')==0x10001; pid=y.u('I'); t=open_ticket(y.take(y.u('I')),pid)
    return c,Client(sp,me,t)
auth,sec=login(0x3c)
reqs=recorded(path); n=0
def blast(c,m):
    body=b'\0'+m if random.random()<.7 else bytes([random.choice([1,2,9])])+(zlib.compress(m) if random.random()<.8 else os.urandom(20))
    if random.random()<.1: body=body[:random.randrange(1,len(body)+1)]
    c.raw(2,6,c.srvsig,c.seq,bytes([random.choice([0,0,0,1,5])])+rc4(SK,body)); c.seq+=1
    c.s.settimeout(0.002)
    try:
        while True:
            d,_=c.s.recvfrom(4096)
            r=parse(d,False)
            if r['type']==2 and not r['flags']&1: c.raw(2,1,c.srvsig,r['seq'],bytes([r['frag']]))
    except socket.timeout: pass
for rnd in range(40):
    for port,me,m,_ in reqs:
        b=bytearray(m[:3000]); k=random.choice([0,1,1,2,3])
        for _ in range(k):
            i=random.randrange(len(b)); b[i]=random.choice([0,0xff,0x7f,0x80,random.randrange(256)])
        if random.random()<.2 and len(b)>14: i=random.randrange(13,len(b)); b[i:i+4]=struct.pack('<I',random.choice([0xffffffff,0x7fffffff,100000,65536,4097]))
        if random.random()<.15: b=b[:random.randrange(5,len(b)+1)]
        if random.random()<.8: b[0:4]=struct.pack('<I',len(b)-4)
        blast(auth if port==21126 else sec, bytes(b)); n+=1
# raw rubbish with a good checksum, and without
for _ in range(3000):
    p=bytes([0x3c,0x31,random.randrange(256),random.randrange(256)])+os.urandom(random.randrange(0,60))
    for port in (ap,sp): sec.s.sendto(p+bytes([csum(p)]),('127.0.0.1',port)); sec.s.sendto(os.urandom(random.randrange(1,80)),('127.0.0.1',port))
print('sent',n,'damaged calls')
