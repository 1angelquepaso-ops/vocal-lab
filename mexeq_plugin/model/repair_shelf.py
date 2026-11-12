import json,numpy as np
from build_shelf import *
from sb_model import sb
d=json.load(open('shelf_tables.json'))
def midcol(kind,T,ks,gs,M,ki):
    worst=0
    for j in range(len(gs)-1):
        if np.sign(gs[j])!=np.sign(gs[j+1]): continue
        p=(T[ki,j]+T[ki+1,j]+T[ki,j+1]+T[ki+1,j+1])/4
        Tg=M.target((ks[ki]+ks[ki+1])/2,(gs[j]+gs[j+1])/2)
        worst=max(worst,np.max(np.abs(sb(kind,p,GX[MASK],2)-Tg[MASK])))
    return worst
def fitw(kind,M,k,g,x0,w):
    T=M.target(k,g); ref=np.array(x0)
    lb=[np.log(5),-30,np.log(0.15),np.log(8),-6,np.log(0.2),np.log(8),-6,np.log(0.2)]
    ub=[np.log(60000),30,np.log(3),np.log(60000),6,np.log(3),np.log(60000),6,np.log(3)]
    x0=np.clip(x0,np.array(lb)+1e-6,np.array(ub)-1e-6)
    r=least_squares(lambda p: np.concatenate([sb(kind,p,GX,2)-T,w*(p-ref),0.03*np.array([p[4],p[7]])]),x0,bounds=(lb,ub))
    return r.x,float(np.max(np.abs(sb(kind,r.x,GX[MASK],2)-T[MASK])))
for kind in 'LH':
    D=d[kind]; ks=np.array(D['ks']); gs=np.array(D['gs']); T=np.array(D['table']); M=Model(kind)
    kd_i=int(np.argmin(abs(ks-M.kd)))
    for it in range(6):
        cols=[(ki,midcol(kind,T,ks,gs,M,ki)) for ki in range(len(ks)-1)]
        bad=[c for c in cols if c[1]>0.3]
        print(kind,'iter',it,'seams',[(ks[c[0]],round(c[1],2)) for c in bad])
        if not bad: break
        ki,_=bad[0]
        # direction: outward from default
        if ki>=kd_i: src,rng=ki,range(ki+1,len(ks))
        else: src,rng=ki+1,range(ki,-1,-1)
        for w in (0.3,1.0,3.0):
            Tn=T.copy(); prev=src; worst=0
            for kk in rng:
                for j in range(len(gs)):
                    x,e=fitw(kind,M,ks[kk],gs[j],Tn[prev,j],w); Tn[kk,j]=x; worst=max(worst,e)
                prev=kk
            m=midcol(kind,Tn,ks,gs,M,ki)
            print('   w',w,'node worst %.3f, seam now %.3f'%(worst,m))
            if m<0.3 and worst<0.4: T=Tn; break
        else:
            T=Tn
    D['table']=T.tolist()
json.dump(d,open('shelf_tables.json','w'))
