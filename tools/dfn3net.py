# DeepFilterNet3 (48 kHz) read from its official ONNX export, and the network frame by frame in numpy:
# the reference for src/neural/DfnVoice.cpp. The ONNX reader needs no onnx package.
# minimal ONNX reader (protobuf wire format), enough for nodes, attributes and weights
import struct, numpy as np
def _vi(b,i):
    r=0;s=0
    while True:
        c=b[i]; i+=1; r|=(c&127)<<s; s+=7
        if c<128: return r,i
def fields(b):
    i=0; n=len(b)
    while i<n:
        k,i=_vi(b,i); f=k>>3; w=k&7
        if w==0: v,i=_vi(b,i)
        elif w==1: v=b[i:i+8]; i+=8
        elif w==2: l,i=_vi(b,i); v=b[i:i+l]; i+=l
        elif w==5: v=b[i:i+4]; i+=4
        else: raise ValueError(w)
        yield f,w,v
def _ints(v,w):
    if w==0: return [v]
    out=[];i=0
    while i<len(v): x,i=_vi(v,i); out.append(x)
    return out
def _s(x): return x-(1<<64) if x>=(1<<63) else x
DT={1:np.float32,7:np.int64,6:np.int32,11:np.float64,9:np.bool_}
def tensor(b):
    dims=[];dt=1;raw=None;name='';fd=[];i64=[]
    for f,w,v in fields(b):
        if f==1: dims+=_ints(v,w)
        elif f==2: dt=v
        elif f==8: name=bytes(v).decode()
        elif f==9: raw=bytes(v)
        elif f==4: fd+= list(np.frombuffer(bytes(v),'<f4')) if w==2 else [struct.unpack('<f',v)[0]]
        elif f==7: i64+=[_s(x) for x in _ints(v,w)]
    if raw is not None: a=np.frombuffer(raw,DT[dt])
    elif fd: a=np.array(fd,np.float32)
    else: a=np.array(i64,np.int64)
    return name,a.reshape(dims) if dims else a.reshape(())if a.size==1 else a
def attr(b):
    name='';val=None;ints=[];floats=[]
    for f,w,v in fields(b):
        if f==1: name=bytes(v).decode()
        elif f==2: val=struct.unpack('<f',v)[0]
        elif f==3: val=_s(v)
        elif f==4: val=bytes(v).decode(errors='replace')
        elif f==5: val=tensor(v)[1]
        elif f==8: ints+=[_s(x) for x in _ints(v,w)]
        elif f==7: floats+= list(np.frombuffer(bytes(v),'<f4')) if w==2 else [struct.unpack('<f',v)[0]]
    return name,(ints if ints else floats if floats else val)
def node(b):
    d=dict(inp=[],out=[],name='',op='',attr={})
    for f,w,v in fields(b):
        if f==1: d['inp'].append(bytes(v).decode())
        elif f==2: d['out'].append(bytes(v).decode())
        elif f==3: d['name']=bytes(v).decode()
        elif f==4: d['op']=bytes(v).decode()
        elif f==5: k,x=attr(v); d['attr'][k]=x
    return d
def load(path):
    b=memoryview(open(path,'rb').read()); g=None
    for f,w,v in fields(b):
        if f==7: g=v
    nodes=[];init={};ins=[];outs=[]
    for f,w,v in fields(g):
        if f==1: nodes.append(node(v))
        elif f==5: n,a=tensor(v); init[n]=a
        elif f==11: ins.append(bytes(next(x for ff,ww,x in fields(v) if ff==1)).decode())
        elif f==12: outs.append(bytes(next(x for ff,ww,x in fields(v) if ff==1)).decode())
    return nodes,init,ins,outs
if __name__=='__main__':
    import collections,sys
    for fn in ('enc','erb_dec','df_dec'):
        nodes,init,ins,outs=load(f'/home/claude/nn/m3/tmp/export/{fn}.onnx')
        print(fn,len(nodes),'nodes',dict(collections.Counter(n['op'] for n in nodes)))
        print('  inputs',[i for i in ins if i not in init],'outputs',outs,'weights',len(init),sum(a.size for a in init.values()))

# ---- network
import numpy as np
def _graph(fn):
    nodes,init,ins,outs=load(_M+fn)
    const={n['out'][0]:n['attr'].get('value') for n in nodes if n['op']=='Constant'}
    get=lambda x: np.asarray(init[x] if x in init else const[x],np.float32)
    return nodes,get
def _convs(nodes,get):   # conv-like nodes in order: (weight, bias or None)
    out=[]
    for n in nodes:
        if n['op'] in ('Conv','ConvTranspose'):
            out.append((get(n['inp'][1]), get(n['inp'][2]).reshape(-1) if len(n['inp'])>2 else None))
    return out
def _grus(nodes,get):
    return [(get(n['inp'][1])[0],get(n['inp'][2])[0],get(n['inp'][3])[0]) for n in nodes if n['op']=='GRU']
def _eins(nodes,get): return [get(n['inp'][1]) for n in nodes if n['op']=='Einsum']
def weights(folder):
    global _M
    _M=folder
    W={}
    nodes,get=_graph('enc.onnx'); c=_convs(nodes,get); g=_grus(nodes,get); e=_eins(nodes,get)
    names=['erb_conv0','erb_conv1_dw','erb_conv1_pw','erb_conv2_dw','erb_conv2_pw','erb_conv3_dw','erb_conv3_pw','df_conv0_dw','df_conv0_pw','df_conv1_dw','df_conv1_pw']
    for nm,(w,b) in zip(names,c):
        W['enc.'+nm+'.w']=w
        if b is not None: W['enc.'+nm+'.b']=b
    W['enc.df_fc_emb']=e[0]; W['enc.lin_in']=e[1]; W['enc.lin_out']=e[2]
    W['enc.gru.W'],W['enc.gru.R'],W['enc.gru.B']=g[0]
    nodes,get=_graph('erb_dec.onnx'); c=_convs(nodes,get); g=_grus(nodes,get); e=_eins(nodes,get)
    names=['conv3p','convt3_dw','convt3_pw','conv2p','convt2_dw','convt2_pw','conv1p','convt1_dw','convt1_pw','conv0p','conv0_out']
    for nm,(w,b) in zip(names,c):
        W['erb.'+nm+'.w']=w
        if b is not None: W['erb.'+nm+'.b']=b
    W['erb.lin_in']=e[0]; W['erb.lin_out']=e[1]
    for i in range(2): W[f'erb.gru{i}.W'],W[f'erb.gru{i}.R'],W[f'erb.gru{i}.B']=g[i]
    nodes,get=_graph('df_dec.onnx'); c=_convs(nodes,get); g=_grus(nodes,get); e=_eins(nodes,get)
    W['df.convp_dw.w']=c[0][0]; W['df.convp_pw.w']=c[1][0]; W['df.convp_pw.b']=c[1][1]
    W['df.lin_in']=e[0]; W['df.skip']=e[1]; W['df.out']=e[2]
    for i in range(2): W[f'df.gru{i}.W'],W[f'df.gru{i}.R'],W[f'df.gru{i}.B']=g[i]
    return W
sig=lambda x: 1/(1+np.exp(-x)); relu=lambda x: np.maximum(x,0)
def glin(x,w):          # grouped linear: x [g*i] , w [g,i,h] -> [g*h]
    g,i,h=w.shape; return np.einsum('gi,gih->gh',x.reshape(g,i),w).reshape(-1)
def gru(x,h,W,R,B):
    H=h.size; a=W@x+B[:3*H]; b=R@h+B[3*H:]
    z=sig(a[:H]+b[:H]); r=sig(a[H:2*H]+b[H:2*H]); n=np.tanh(a[2*H:]+r*b[2*H:]); return (1-z)*n+z*h
def dw3(x,w,stride=1):  # depthwise 1x3 over frequency, pad 1: x [C,F], w [C,1,1,3]
    C,F=x.shape; xp=np.pad(x,((0,0),(1,1))); Fo=(F+2-3)//stride+1; k=w[:,0,0,:]
    return sum(xp[:,j:j+stride*Fo:stride]*k[:,j:j+1] for j in range(3))
def pw(x,w,b): return np.einsum('oc,cf->of',w[:,:,0,0],x)+b[:,None]
def dwt3(x,w):          # depthwise transposed 1x3, stride 2, pad 1, output padding 1: [C,F] -> [C,2F]
    C,F=x.shape; y=np.zeros((C,2*F+2)); k=w[:,0,0,:]
    for j in range(3): y[:,j:j+2*F:2]+=x*k[:,j:j+1]
    return y[:,1:2*F+1]
class Net:
    def __init__(s,W=None):
        s.W=W; s.reset()
    def reset(s):
        s.pe=np.zeros((2,32)); s.ps=np.zeros((2,2,96)); s.pc=np.zeros((4,64,96))
        s.h=[np.zeros(256) for _ in range(5)]
    def step(s,fe,fs):
        """fe [32] erb feature, fs [2,96] complex feature (re, im) -> erb mask [32], DF coefficients [96,5] complex"""
        W=s.W
        # ---- encoder
        x=np.stack([s.pe[0],s.pe[1],fe]); s.pe=np.stack([s.pe[1],fe])            # [3 frames, 32]
        xp=np.pad(x,((0,0),(1,1))); w=W['enc.erb_conv0.w']                        # [64,1,3,3]
        e0=sum(w[:,0,t,j][:,None]*xp[t,j:j+32][None] for t in range(3) for j in range(3))+W['enc.erb_conv0.b'][:,None]; e0=relu(e0)
        e1=relu(pw(dw3(e0,W['enc.erb_conv1_dw.w'],2),W['enc.erb_conv1_pw.w'],W['enc.erb_conv1_pw.b']))
        e2=relu(pw(dw3(e1,W['enc.erb_conv2_dw.w'],2),W['enc.erb_conv2_pw.w'],W['enc.erb_conv2_pw.b']))
        e3=relu(pw(dw3(e2,W['enc.erb_conv3_dw.w'],1),W['enc.erb_conv3_pw.w'],W['enc.erb_conv3_pw.b']))
        x=np.stack([s.ps[0],s.ps[1],fs]); s.ps=np.stack([s.ps[1],fs])            # [3, 2, 96]
        xp=np.pad(x,((0,0),(0,0),(1,1))); w=W['enc.df_conv0_dw.w']                # [64,1,3,3], groups 2
        d=np.zeros((64,96))
        for o in range(64):
            ci=o//32
            for t in range(3):
                for j in range(3): d[o]+=w[o,0,t,j]*xp[t,ci,j:j+96]
        c0=relu(pw(d,W['enc.df_conv0_pw.w'],W['enc.df_conv0_pw.b']))
        c1=relu(pw(dw3(c0,W['enc.df_conv1_dw.w'],2),W['enc.df_conv1_pw.w'],W['enc.df_conv1_pw.b']))   # [64,48]
        cemb=relu(glin(c1.T.reshape(-1),W['enc.df_fc_emb']))
        emb=e3.T.reshape(-1)+cemb
        x=relu(glin(emb,W['enc.lin_in'])); s.h[0]=gru(x,s.h[0],W['enc.gru.W'],W['enc.gru.R'],W['enc.gru.B'])
        emb=relu(glin(s.h[0],W['enc.lin_out']))
        # ---- erb decoder
        x=relu(glin(emb,W['erb.lin_in']))
        s.h[1]=gru(x,s.h[1],W['erb.gru0.W'],W['erb.gru0.R'],W['erb.gru0.B']); s.h[2]=gru(s.h[1],s.h[2],W['erb.gru1.W'],W['erb.gru1.R'],W['erb.gru1.B'])
        de=relu(glin(s.h[2],W['erb.lin_out'])).reshape(8,64).T                    # [64,8]
        p=lambda x,n: relu(x*W[f'erb.{n}.w'][:,0,0,0][:,None]+W[f'erb.{n}.b'][:,None])
        y=relu(pw(dw3(p(e3,'conv3p')+de,W['erb.convt3_dw.w']),W['erb.convt3_pw.w'],W['erb.convt3_pw.b']))
        y=relu(pw(dwt3(p(e2,'conv2p')+y,W['erb.convt2_dw.w']),W['erb.convt2_pw.w'],W['erb.convt2_pw.b']))
        y=relu(pw(dwt3(p(e1,'conv1p')+y,W['erb.convt1_dw.w']),W['erb.convt1_pw.w'],W['erb.convt1_pw.b']))
        y=p(e0,'conv0p')+y; yp=np.pad(y,((0,0),(1,1))); w=W['erb.conv0_out.w'][0,:,0,:]      # [64,3]
        m=sig(sum((w[:,j][:,None]*yp[:,j:j+32]).sum(0) for j in range(3))+W['erb.conv0_out.b'][0])
        # ---- deep-filter decoder
        x=relu(glin(emb,W['df.lin_in']))
        s.h[3]=gru(x,s.h[3],W['df.gru0.W'],W['df.gru0.R'],W['df.gru0.B']); s.h[4]=gru(s.h[3],s.h[4],W['df.gru1.W'],W['df.gru1.R'],W['df.gru1.B'])
        c=s.h[4]+glin(emb,W['df.skip'])
        co=np.tanh(glin(c,W['df.out'])).reshape(96,10)
        x=np.concatenate([s.pc,c0[None]]); s.pc=x[1:]                              # [5,64,96]
        w=W['df.convp_dw.w']                                                       # [10,32,5,1], groups 2
        d=np.zeros((10,96))
        for o in range(10):
            ci=(o//5)*32; d[o]=np.einsum('ct,tcf->f',w[o,:,:,0],x[:,ci:ci+32,:])
        d=relu(pw(d,W['df.convp_pw.w'],W['df.convp_pw.b']))
        co=co+d.T
        return m, co[:,0::2]+1j*co[:,1::2]
