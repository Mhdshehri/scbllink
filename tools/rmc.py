# Decodes the game's RMC calls from an SCBLLink capture into names and fields.
# usage: rmc.py capture.pcap [-v]      By Mhdshehri.
import sys, json, struct, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dump import messages, inflate
_ddl=os.path.join(os.path.dirname(os.path.abspath(__file__)),'ddl.json')
D=json.load(open(_ddl)) if os.path.exists(_ddl) else {'protocols':{},'types':{}}  # names and value layouts, kept locally
P={int(k):v for k,v in D['protocols'].items()}; T=D['types']
class R:
    def __init__(s,b): s.b=b; s.o=0
    def take(s,n):
        if s.o+n>len(s.b): raise ValueError('short')
        v=s.b[s.o:s.o+n]; s.o+=n; return v
    def u(s,fmt): return struct.unpack('<'+fmt,s.take(struct.calcsize(fmt)))[0]
PRIM={'u8':'B','i8':'b','u16':'H','i16':'h','u32':'I','i32':'i','u64':'Q','i64':'q','f64':'d','bool':'?','DateTime':'Q','QResult':'I'}
def split_generic(t):
    m=re.match(r'([\w:]+)<(.*)>$',t)
    if not m: return t,[]
    args=[];depth=0;cur=''
    for ch in m.group(2):
        if ch=='<':depth+=1
        if ch=='>':depth-=1
        if ch==',' and depth==0: args.append(cur);cur=''
        else: cur+=ch
    args.append(cur); return m.group(1),args
def string(r):
    n=r.u('H'); return r.take(n)[:-1].decode('utf-8','replace') if n else ''
def read(r,t):
    if t in PRIM: return r.u(PRIM[t])
    if t in('String','StationURL'): return string(r)
    g,a=split_generic(t)
    if g=='QList': return [read(r,a[0]) for _ in range(min(r.u('I'),100000))]
    if g=='Vec': return r.take(r.u('I')).hex()
    if g=='QBuffer': return r.take(r.u('H')).hex()
    if g=='BufferTail': return r.take(len(r.b)-r.o).hex()
    if g.endswith('HashMap'): return [(read(r,a[0]),read(r,a[1])) for _ in range(r.u('I'))]
    if g=='Variant':
        k=r.u('B'); return {0:lambda:None,1:lambda:r.u('q'),2:lambda:r.u('d'),3:lambda:r.u('?'),4:lambda:string(r),5:lambda:r.u('Q'),6:lambda:r.u('Q')}[k]()
    if g=='Any':
        name=string(r); n1=r.u('I'); n2=r.u('I'); body=r.take(n2)
        try:
            rr=R(body); v=read(rr,name) if name in T else body.hex()
        except Exception: v=body.hex()
        return {'any':name,'value':v}
    if g in T: return {f:read(r,ft) for f,ft in T[g]}
    raise ValueError('type '+t)
def decode(body,tname):
    if tname not in T: return None,'no type '+tname
    r=R(body)
    try: v=read(r,tname)
    except Exception as e: return None,f'{e} at {r.o}/{len(body)}'
    return v,(None if r.o==len(body) else f'{len(body)-r.o} bytes left')
def calls(path):
    pend={}
    for t,c2s,port,seq,m in messages(path):
        b=m; r=R(b); r.u('I'); p=r.u('B'); req=bool(p&0x80); p&=0x7f
        if p==0x7f: p=r.u('H')
        pr=P.get(p,{'name':f'P{p}','methods':{}})
        if req:
            cid=r.u('I'); mid=r.u('I'); name=pr['methods'].get(mid) or pr['methods'].get(str(mid)) or f'M{mid}'
            body=b[r.o:]; pend[(port,c2s,cid)]=(pr['name'],name)
            yield dict(t=t,dir='C>S' if c2s else 'S>C',port=port,kind='req',proto=p,pname=pr['name'],mid=mid,method=name,cid=cid,body=body,type=name+'Request')
        else:
            ok=r.u('B')
            if ok:
                cid=r.u('I'); mid=r.u('I')&0x7fff; pn,name=pend.pop((port,not c2s,cid),(pr['name'],f'M{mid}'))
                yield dict(t=t,dir='C>S' if c2s else 'S>C',port=port,kind='rsp',proto=p,pname=pn,mid=mid,method=name,cid=cid,body=b[r.o:],type=name+'Response')
            else:
                err=r.u('I'); cid=r.u('I'); pn,name=pend.pop((port,not c2s,cid),(pr['name'],'?'))
                yield dict(t=t,dir='C>S' if c2s else 'S>C',port=port,kind='err',proto=p,pname=pn,mid=0,method=name,cid=cid,body=b'',err=err,type='')
if __name__=='__main__':
    v='-v' in sys.argv
    for c in calls(sys.argv[1]):
        head=f"{c['t']:7.2f} {c['dir']} {c['port']} {c['kind']} #{c['cid']} {c['proto']}.{c['mid']} {c['pname']}.{c['method']}"
        if c['kind']=='err': print(head,'ERROR %08x'%c['err']); continue
        val,note=decode(c['body'],c['type'])
        s=json.dumps(val,ensure_ascii=False) if val is not None else c['body'].hex()
        if not v and len(s)>230: s=s[:230]+'…'
        print(head,f"[{len(c['body'])}]",('!! '+note+' ') if note else '',s)
