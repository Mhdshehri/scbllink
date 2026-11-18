# Plays a recorded game's calls against a server (ours, on 127.0.0.1) and compares the answers
# with the recorded ones. usage: replay.py capture.pcap auth_port secure_port     By Mhdshehri.
import sys, os, socket, struct, zlib, hashlib, hmac
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcap_udp import packets
from prudp0 import rc4, parse
import rmc
AK=b'yl4NG7qZ'; SK=b'CD&ML'
def csum(d):
    l=len(d)-len(d)%4
    t=sum(struct.unpack_from('<I',d,i)[0] for i in range(0,l,4))&0xffffffff
    return (sum(t.to_bytes(4,'little'))+sum(AK)+sum(d[l:]))&255
def md5n(b,n):
    for _ in range(n): b=hashlib.md5(b).digest()
    return b
class Client:
    def __init__(s,port,me,ticket=None):
        s.s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.s.settimeout(3); s.addr=('127.0.0.1',port)
        s.me=me; s.sess=0x18; s.mysig=0x5aef33c8; s.seq=1; s.inbuf=b''
        s.raw(0,4,0,0,struct.pack('<I',0)); r=s.recv(); assert r['type']==0 and r['flags']==9, r
        s.srvsig=r['conn']
        pay=b''
        if ticket:
            key,pid,token=ticket; chal=0x11223344
            req=rc4(key,struct.pack('<III',pid,0,chal)+bytes(16))
            pay=rc4(SK,b'\0'+struct.pack('<I',len(token))+token+struct.pack('<I',len(req))+req)
        s.raw(1,6,s.srvsig,1,struct.pack('<I',s.mysig)+pay); r=s.recv(); assert r['type']==1, r
        if ticket:
            a=rc4(SK,r['payload']); assert a==b'\0'+struct.pack('<II',4,chal+1), a.hex()
        s.seq=2
    def raw(s,typ,flags,sig,seq,rest):
        p=bytes([s.me,0x31,typ|flags<<3,s.sess if typ else 0])+struct.pack('<IH',sig,seq)+rest
        s.s.sendto(p+bytes([csum(p)]),s.addr)
    def recv(s):
        d,_=s.s.recvfrom(4096); assert csum(d[:-1])==d[-1],'checksum'; assert d[0]==0x31 and d[1]==s.me
        r=parse(d,False)
        if r['type']!=0: assert r['sig']==s.mysig, 'signature'
        return r
    def call(s,msg,frag_at=None):
        parts=[msg[i:i+(frag_at or 900)] for i in range(0,len(msg),frag_at or 900)]
        for i,p in enumerate(parts):
            body=bytes([2])+zlib.compress(p) if len(p)>40 else b'\0'+p
            s.raw(2,6,s.srvsig,s.seq,bytes([0 if i==len(parts)-1 else i+1])+rc4(SK,body)); s.seq+=1
        acks=0; out=b''
        while True:
            r=s.recv()
            if r['type']!=2: continue
            if r['flags']&1: acks+=1; continue
            s.raw(2,1,s.srvsig,r['seq'],bytes([r['frag']]))
            d=rc4(SK,r['payload']); out+=zlib.decompress(d[1:]) if d[0] else d[1:]
            if r['frag']==0: break
        assert acks==len(parts),(acks,len(parts))
        return out
    def ping(s):
        s.raw(4,4,s.srvsig,7,b''); r=s.recv(); assert r['type']==4 and r['flags']&1 and r['seq']==7
    def close(s):
        s.raw(3,6,s.srvsig,s.seq,b''); r=s.recv(); assert r['type']==3
def open_ticket(buf,pid):
    k=md5n(b'JaDe!' if pid==105 else b'UbiDummyPwd',65000+pid%1024); body,mac=buf[:-16],buf[-16:]
    assert hmac.new(k,body,'md5').digest()==mac,'ticket mac'
    d=rc4(k,body); n=struct.unpack_from('<I',d,20)[0]; assert len(d)==24+n
    return d[:16],pid,d[24:]
def recorded(path):
    """(port, client stream byte, request message, recorded answer body or None)"""
    buf={}; reqs=[]; answers={}
    for t,pr,s,sp,d,dp,pay in packets(path):
        if pr!=17 or not({sp,dp}&{21126,21127}) or 13000 in (sp,dp): continue
        c2s=dp in(21126,21127); r=parse(pay,c2s)
        if r['type']!=2 or not r['payload']: continue
        port=dp if c2s else sp; me=pay[0] if c2s else pay[1]
        dec=rc4(SK,r['payload']); dec=zlib.decompress(dec[1:]) if dec[0] else dec[1:]
        k=(c2s,port,me); buf[k]=buf.get(k,b'')+dec
        if r['frag']==0:
            m=buf.pop(k); x=rmc.R(m); x.u('I'); p=x.u('B')&0x7f
            if p==0x7f: x.u('H')
            if c2s: reqs.append((port,me,m,x.u('I')))
            else: x.u('B'); cid=x.u('I'); x.u('I'); answers[cid]=m[x.o:]
    return [(a,b,c,answers.get(cid)) for a,b,c,cid in reqs]
SKIP={(53,1),(10,1),(10,2),(10,3),(11,1),(11,4),(107,1),(53,6),(106,5),(106,8),(5002,1)}  # differ on purpose (tickets, time, addresses, our own texts)
def main():
    path,ap,sp=sys.argv[1],int(sys.argv[2]),int(sys.argv[3])
    # two throw-away logins first, so the recorded player gets the recorded number (1003)
    for name in (b'a',b'b'):
        c=Client(ap,0x3d); body=struct.pack('<H',len(name)+1)+name+b'\0'
        any_=struct.pack('<H',len(name)+1)+name+b'\0'+b'\x02\x00k\x00'+b'\x02\x00p\x00'
        m=b'\x8a'+struct.pack('<II',1,2)+body+b'\x21\x00UbiAuthenticationLoginCustomData\x00'+struct.pack('<II',len(any_)+4,len(any_))+any_
        a=c.call(struct.pack('<I',len(m))+m); assert a[5]==1,a.hex(); c.close()
    conns={}; tickets={}; same=diff=err=0
    for port,me,m,want in recorded(path):
        x=rmc.R(m); x.u('I'); proto=x.u('B')&0x7f
        if proto==0x7f: proto=x.u('H')
        cid=x.u('I'); method=x.u('I')
        which=ap if port==21126 else sp
        if (port,me) not in conns or (port==21126 and method in(1,2)):
            if (port,me) in conns: conns[(port,me)].close()
            conns[(port,me)]=Client(which,me,tickets.get(me) if port==21127 else None)
        c=conns[(port,me)]
        a=c.call(m, 180 if len(m)>2000 else None)
        x=rmc.R(a); x.u('I'); p=x.u('B')
        if p==0x7f: x.u('H')
        if not x.u('B'): print(f'ERROR  {proto}.{method}: {x.u("I"):08x}'); err+=1; continue
        assert x.u('I')==cid and x.u('I')==method|0x8000
        got=a[x.o:]
        if proto==10:
            y=rmc.R(got); assert y.u('I')==0x10001
            pid=y.u('I') if method!=3 else tickets[me][1]
            tickets[me]=open_ticket(y.take(y.u('I')),pid)
        if (proto,method) in SKIP: print(f'ok     {proto}.{method} (not compared, {len(got)} bytes; recorded {len(want)})'); same+=1
        elif got==want: same+=1
        else:
            diff+=1; i=next((i for i in range(min(len(got),len(want))) if got[i]!=want[i]),min(len(got),len(want)))
            print(f'DIFFER {proto}.{method}: {len(got)} bytes, recorded {len(want)}; first difference at {i}\n  got  {got[max(0,i-16):i+48].hex()}\n  want {want[max(0,i-16):i+48].hex()}')
    for c in conns.values(): c.ping(); c.close()
    print(f'{same} the same, {diff} different, {err} errors'); sys.exit(1 if diff or err else 0)
main()
