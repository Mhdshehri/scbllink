# The private-match invitation, against SCBLLink's server (test/srv_udp): the host opens its
# anteroom and its private room, invites; the guest's search must give the private room (113=0)
# with the guest in it, plus the anteroom, and the guest's AddParticipants must bring it the
# 7003 join gate. Without an invitation the private room stays hidden. By Mhdshehri.
import sys, os, struct, socket
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'replay.py')).read().rsplit('main()',1)[0])
AP,SP=int(sys.argv[1]),int(sys.argv[2])
def req(proto,method,body,cid=[100]):
    cid[0]+=1; m=bytes([0x80|proto])+struct.pack('<II',cid[0],method)+body; return struct.pack('<I',len(m))+m
def s16(t): b=t.encode()+b'\0'; return struct.pack('<H',len(b))+b
def body(a):
    x=rmc.R(a); x.u('I'); x.u('B'); assert x.u('B')==1, 'error %08x'%x.u('I'); x.u('I'); x.u('I'); return a[x.o:]
class Game:
    def __init__(s,name,me):
        c=Client(AP,me); any_=s16(name)+s16('k')+s16('p')
        g=body(c.call(req(10,2,s16(name)+s16('UbiAuthenticationLoginCustomData')+struct.pack('<II',len(any_)+4,len(any_))+any_)))
        y=rmc.R(g); y.u('I'); s.pid=y.u('I'); y.take(y.u('I'))
        g=body(c.call(req(10,3,struct.pack('<II',s.pid,4096)))); y=rmc.R(g); y.u('I'); t=open_ticket(y.take(y.u('I')),s.pid)
        s.sec=Client(SP,me,t); s.notes=[]
    def call(s,proto,method,b):
        m=req(proto,method,b); s.sec.s.settimeout(3)
        # like Client.call, but server-to-game calls are kept (and acknowledged)
        s.sec.raw(2,6,s.sec.srvsig,s.sec.seq,b'\0'+rc4(SK,b'\0'+m)); s.sec.seq+=1
        while True:
            r=s.sec.recv()
            if r['type']!=2 or r['flags']&1: continue
            s.sec.raw(2,1,s.sec.srvsig,r['seq'],bytes([r['frag']]))
            d=rc4(SK,r['payload']); d=zlib.decompress(d[1:]) if d[0] else d[1:]
            if d[4]&0x80: s.notes.append(d); continue
            return body(d)
    def drain(s):
        s.sec.s.settimeout(0.3)
        try:
            while True:
                r=s.sec.recv()
                if r['type']==2 and not r['flags']&1:
                    s.sec.raw(2,1,s.sec.srvsig,r['seq'],bytes([r['frag']]))
                    d=rc4(SK,r['payload']); d=zlib.decompress(d[1:]) if d[0] else d[1:]
                    if d[4]&0x80: s.notes.append(d)
        except socket.timeout: pass
def attrs(l): return struct.pack('<I',len(l))+b''.join(struct.pack('<Ii',a,v) for a,v in l)
def urls(ip): return struct.pack('<I',2)+s16(f'prudp:/address={ip};port=13000;type=2')+s16(f'prudp:/address={ip};port=13000')
def search(g,host):
    b=g.call(42,24,struct.pack('<III',1,1,host)); x=rmc.R(b); out=[]
    for _ in range(x.u('I')):
        t=x.u('I'); sid=x.u('I'); hp=x.u('I'); u=[rmc.string(x) for _ in range(x.u('I'))]
        at={x.u('I'):x.u('i') for _ in range(x.u('I'))}; parts=[x.u('I') for _ in range(x.u('I'))]
        out.append((sid,hp,u,at,parts))
    return out
def gate(n):
    x=rmc.R(n); x.u('I'); p=x.u('B')&0x7f; x.u('I'); meth=x.u('I'); return p,meth,x.u('I'),x.u('I'),x.u('I'),x.u('I')
ok=True
def check(what,cond):
    global ok; print(('ok   ' if cond else 'FAIL ')+what); ok&=bool(cond)
host=Game('KaZ',0x3f); guest=Game('Sky',0x3d)
host.call(42,21,urls('10.237.51.1')); guest.call(42,21,urls('10.237.51.2'))
b=host.call(42,1,struct.pack('<I',1)+attrs([(113,1),(3,8),(4,0)])); ante=struct.unpack('<II',b)[1]
host.call(42,8,struct.pack('<II',1,ante)+struct.pack('<II',1,host.pid)+struct.pack('<I',0))
b=host.call(42,1,struct.pack('<I',1)+attrs([(113,0),(3,0),(4,8)])); room=struct.unpack('<II',b)[1]
host.call(42,8,struct.pack('<II',1,room)+struct.pack('<I',0)+struct.pack('<II',1,host.pid))
host.drain(); check('the host gets the join gate for itself', any(gate(n)==(14,1,host.pid,7003,host.pid,room) for n in host.notes)); host.notes=[]
res=search(guest,host.pid)
check('uninvited: only the anteroom', [r[0] for r in res]==[ante])
check('host URLs given', res and res[0][2][0].startswith('prudp:/address=10.237.51.1'))
try: guest.call(42,22,struct.pack('<II',1,room)); refused=False
except AssertionError: refused=True
check('uninvited JoinSession of the private room refused', refused)
import time; socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(b'INVITE KaZ Sky',('127.0.0.1',SP+100)); time.sleep(0.2)
res=search(guest,host.pid)
ids=[r[0] for r in res]
check('invited: private room and anteroom', sorted(ids)==sorted([room,ante]))
pr=[r for r in res if r[0]==room]
check('the private room is kind 0, with the guest in it', pr and pr[0][3].get(113)==0 and guest.pid in pr[0][4])
guest.call(42,8,struct.pack('<II',1,room)+struct.pack('<I',0)+struct.pack('<II',1,guest.pid))
guest.drain()
g=[gate(n) for n in guest.notes]
check('the guest gets the 7003 join gate for itself and the room', any(t==(14,1,guest.pid,7003,guest.pid,room) for t in g))
host.drain(); check('the host gets none', not host.notes)
# NAT echo from the peer socket
e=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); e.settimeout(2)
p=bytes.fromhex('7171050000000000000001053300000000dc4a8d7b80000102d3'); e.sendto(p,('127.0.0.1',SP))
a,_=e.recvfrom(200); check('NAT echo answered', a[:15]==p[10:25] and a[15:].startswith(b'udp:/address=127.0.0.1;port='))
# The order the game really uses (seen with two games): the invitation comes while
# the host has only its anteroom; the guest joins that; then the host makes the match room and
# adds itself and the guest, and each gets the join gate. A stranger can't be added.
bob=Game('Bob',0x3b); ann=Game('Ann',0x39)
b=bob.call(42,1,struct.pack('<I',1)+attrs([(113,1),(3,8),(4,0)])); bante=struct.unpack('<II',b)[1]
bob.call(42,8,struct.pack('<II',1,bante)+struct.pack('<II',1,bob.pid)+struct.pack('<I',0))
socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(b'INVITE Bob Ann',('127.0.0.1',SP+100)); time.sleep(0.2)
res=search(ann,bob.pid)
check('invited before a match room: the anteroom, with the guest in it', [r[0] for r in res]==[bante] and ann.pid in res[0][4])
ann.call(42,22,struct.pack('<II',1,bante))
b=bob.call(42,1,struct.pack('<I',1)+attrs([(113,0),(3,0),(4,8)])); broom=struct.unpack('<II',b)[1]
bob.call(42,8,struct.pack('<II',1,broom)+struct.pack('<I',0)+struct.pack('<II',1,bob.pid))
bob.call(42,8,struct.pack('<II',1,broom)+struct.pack('<I',0)+struct.pack('<II',1,ann.pid))
ann.drain(); bob.drain()
check('the guest gets the join gate when the host adds it', any(gate(n)==(14,1,bob.pid,7003,ann.pid,broom) for n in ann.notes))
check('the host gets its own', any(gate(n)==(14,1,bob.pid,7003,bob.pid,broom) for n in bob.notes))
try: bob.call(42,8,struct.pack('<II',1,broom)+struct.pack('<I',0)+struct.pack('<II',1,guest.pid)); refused=False
except AssertionError: refused=True
check('a stranger can\'t be added to the match room', refused)
# NAT probe: Ann (in Bob's anteroom) asks Bob to probe her; Bob is told 3.2 with her address.
# A stranger asking the same is ignored.
def cid_of(g): b=g.call(11,1,struct.pack('<I',0)); return struct.unpack('<II',b[:8])[1]
bcid=cid_of(bob); cid_of(ann); cid_of(guest)
bob.notes=[]
ann.call(3,3,struct.pack('<I',1)+s16(f'udp:/address=10.237.51.1;port=13000;RVCID={bcid}')+s16('udp:/address=10.237.51.20;port=13000;RVCID=7'))
bob.drain()
def probe(n):
    x=rmc.R(n); x.u('I'); p=x.u('B')&0x7f; x.u('I'); m=x.u('I'); return p,m,rmc.string(x)
check('the host is told to probe the guest', any(probe(n)==(3,2,'udp:/address=10.237.51.20;port=13000;RVCID=7') for n in bob.notes))
bob.notes=[]
other=Game('Eve',0x37)
other.call(3,3,struct.pack('<I',1)+s16(f'udp:/address=10.237.51.1;port=13000;RVCID={bcid}')+s16('udp:/address=6.6.6.6;port=1'))
bob.drain(); check('a stranger can\'t make the host probe', not bob.notes)
print('all ok' if ok else 'FAILED'); sys.exit(0 if ok else 1)
