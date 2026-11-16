"""Acceptance-only two-layer grid routing. Generates copper through PCB IPC, never files."""
from collections import defaultdict
from heapq import heappush,heappop
from math import ceil,hypot
import json,time
from kipy_schematic.pcb import layer_id


class Grid:
    def __init__(self,pads,artifacts,step=254000,clearance=500000,width=250000):
        self.step=step;self.width=width;self.clearance=clearance;self.artifacts=artifacts
        self.x0=ceil(82000000/step);self.y0=ceil(82000000/step)
        self.w=int(178000000/step)-self.x0+1;self.h=int(138000000/step)-self.y0+1;self.n=self.w*self.h
        self.names=sorted({p['net'] for p in pads if p['net']});self.bit={name:1<<i for i,name in enumerate(self.names)}
        self.block=[ [0]*self.n for _ in range(2) ];self.viablock=[0]*self.n;self.holes=[False]*self.n
        self.vias=set();self.stats=[]
        allbits=1<<len(self.names)
        self.allmask=(1<<(len(self.names)+1))-1
        for p in pads:
            bit=self.bit.get(p['net'],allbits);x,y=p['position'];radius=p['radius']
            layers=[i for i,l in enumerate((layer_id('F.Cu'),layer_id('B.Cu'))) if l in p['layers']]
            for layer in layers:self.circle(self.block[layer],x,y,radius+clearance+width/2,bit)
            self.circle(self.viablock,x,y,radius+clearance+400000,bit)
            # Avoid via-in-pad and all drilled-hole collisions, even on the same net.
            self.circle(self.holes,x,y,radius+800000,True)

    def cell(self,xy):
        x=round(xy[0]/self.step)-self.x0;y=round(xy[1]/self.step)-self.y0
        if not(0<=x<self.w and 0<=y<self.h):raise ValueError(('Endpoint outside acceptance board',xy))
        return y*self.w+x

    def xy(self,cell):
        return ((cell%self.w+self.x0)*self.step,(cell//self.w+self.y0)*self.step)

    def circle(self,array,x,y,radius,bit):
        lo_x=max(0,int((x-radius)/self.step)-self.x0-1);hi_x=min(self.w-1,ceil((x+radius)/self.step)-self.x0+1)
        lo_y=max(0,int((y-radius)/self.step)-self.y0-1);hi_y=min(self.h-1,ceil((y+radius)/self.step)-self.y0+1)
        radius2=radius*radius
        for yy in range(lo_y,hi_y+1):
            dy=(yy+self.y0)*self.step-y
            for xx in range(lo_x,hi_x+1):
                dx=(xx+self.x0)*self.step-x
                if dx*dx+dy*dy<=radius2:
                    index=yy*self.w+xx;array[index]|=bit

    def segment(self,layer,start,end,net):
        length=hypot(end[0]-start[0],end[1]-start[1]);samples=max(1,ceil(length/(self.step/2)));bit=self.bit[net]
        # Padding includes half a grid cell to cover sampled continuous copper.
        for i in range(samples+1):
            x=start[0]+(end[0]-start[0])*i/samples;y=start[1]+(end[1]-start[1])*i/samples
            self.circle(self.block[layer],x,y,self.width+self.clearance+self.step/2,bit)
            self.circle(self.viablock,x,y,400000+self.width/2+self.clearance+self.step/2,bit)

    def search(self,start,end,net,start_layers=(0,1),end_layers=(0,1)):
        a=self.cell(start);z=self.cell(end);bit=self.bit[net];other=self.allmask^bit
        gx=z%self.w;gy=z//self.w
        heap=[];distance={};previous={};expanded=0
        for layer in start_layers:
            if self.block[layer][a]&other:continue
            node=layer*self.n+a;distance[node]=0
            heappush(heap,(abs(a%self.w-gx)+abs(a//self.w-gy),0,node))
        goals={layer*self.n+z for layer in end_layers if not self.block[layer][z]&other}
        if not heap or not goals:raise RuntimeError(('Pad blocked at grid resolution',net,start,end))
        while heap:
            _,cost,node=heappop(heap)
            if distance.get(node)!=cost:continue
            if node in goals:
                path=[node]
                while path[-1] in previous:path.append(previous[path[-1]])
                path.reverse();self.stats.append(dict(net=net,expanded=expanded,cost=cost));return path
            expanded+=1;layer,cell=divmod(node,self.n);x=cell%self.w;y=cell//self.w
            candidates=[]
            if x:candidates.append((node-1,1))
            if x+1<self.w:candidates.append((node+1,1))
            if y:candidates.append((node-self.w,1))
            if y+1<self.h:candidates.append((node+self.w,1))
            if not self.holes[cell] and not(self.viablock[cell]&other):candidates.append(((1-layer)*self.n+cell,22))
            for target,weight in candidates:
                tl,tc=divmod(target,self.n)
                if self.block[tl][tc]&other:continue
                new=cost+weight
                if new>=distance.get(target,10**20):continue
                distance[target]=new;previous[target]=node
                heuristic=abs(tc%self.w-gx)+abs(tc//self.w-gy)
                heappush(heap,(new+heuristic,new,target))
        raise RuntimeError(('No clearance-respecting route',net,start,end,expanded))

    def draw(self,pcb,doc,path,start,end,net):
        points=[]
        for node in path:
            layer,cell=divmod(node,self.n);points.append((layer,self.xy(cell)))
        if points[0][1]!=tuple(start):points.insert(0,(points[0][0],tuple(start)))
        if points[-1][1]!=tuple(end):points.append((points[-1][0],tuple(end)))
        segments=[];vias=[];anchor=points[0];last=anchor;direction=None
        for point in points[1:]:
            if point[0]!=last[0]:
                if anchor[1]!=last[1]:segments.append((anchor[0],anchor[1],last[1]))
                vias.append(last[1]);anchor=point;last=point;direction=None;continue
            delta=(point[1][0]-last[1][0],point[1][1]-last[1][1]);length=hypot(*delta)
            current=(round(delta[0]/length,8),round(delta[1]/length,8)) if length else direction
            if direction is not None and current!=direction:
                if anchor[1]!=last[1]:segments.append((anchor[0],anchor[1],last[1]))
                anchor=last
            direction=current;last=point
        if anchor[1]!=last[1]:segments.append((anchor[0],anchor[1],last[1]))
        for layer,a,z in segments:
            pcb.add_track(a,z,self.width,('F.Cu','B.Cu')[layer],doc,net=net);self.segment(layer,a,z,net)
        for xy in vias:
            key=(tuple(xy),net)
            if key in self.vias:continue
            pcb.add_via(xy,net,doc);self.vias.add(key)
            for layer in (0,1):self.circle(self.block[layer],*xy,400000+self.clearance+self.width/2,self.bit[net])
            self.circle(self.viablock,*xy,800000+self.clearance,self.bit[net]);self.circle(self.holes,*xy,900000,True)
        return len(segments),len(vias)


def collect_pads(pcb,doc):
    result=[]
    for fp in pcb.get_items([1],doc):
        shapes={}
        for any_item in fp.definition.items:
            if any_item.type_url.endswith('kiapi.board.types.Pad'):
                pad=pcb._unpack_item(any_item)
                sizes=[(abs(c.size.x_nm),abs(c.size.y_nm)) for c in pad.pad_stack.copper_layers]
                radius=max((hypot(x,y)/2 for x,y in sizes),default=1100000)
                shapes[pad.id.value]=max(radius,800000)
        for pad in pcb.get_footprint_pads(document=doc,footprint_id=fp.id.value):
            result.append(dict(id=pad.id.value,reference=pad.reference,number=pad.number,net=pad.net.name,
                 position=(pad.position.x_nm,pad.position.y_nm),layers=list(pad.copper_layers),radius=shapes[pad.id.value]))
    return result


def route(pcb,doc,artifacts):
    pads=collect_pads(pcb,doc);grid=Grid(pads,artifacts)
    for track in pcb.get_items([11],doc):
        layer=0 if track.layer==layer_id('F.Cu') else 1
        grid.segment(layer,(track.start.x_nm,track.start.y_nm),(track.end.x_nm,track.end.y_nm),track.net.name)
    bynet=defaultdict(list)
    for pad in pads:
        if pad['net']:bynet[pad['net']].append(pad)
    counts=dict(segments=0,vias=0,connections=0)
    for net,nodes in sorted(bynet.items(),key=lambda pair:(len(pair[1]),pair[0])):
        if not pcb.get_ratsnest(net,doc):continue
        linked=[nodes[0]];remaining=nodes[1:]
        while remaining:
            _,i,j=min((hypot(a['position'][0]-z['position'][0],a['position'][1]-z['position'][1]),i,j)
                       for i,a in enumerate(linked) for j,z in enumerate(remaining))
            a=linked[i];z=remaining.pop(j)
            layers_a=[i for i,l in enumerate((layer_id('F.Cu'),layer_id('B.Cu'))) if l in a['layers']]
            layers_z=[i for i,l in enumerate((layer_id('F.Cu'),layer_id('B.Cu'))) if l in z['layers']]
            path=grid.search(a['position'],z['position'],net,layers_a,layers_z)
            segments,vias=grid.draw(pcb,doc,path,a['position'],z['position'],net)
            counts['segments']+=segments;counts['vias']+=vias;counts['connections']+=1;linked.append(z)
            print('ROUTE',net,a['reference']+'.'+a['number'],'->',z['reference']+'.'+z['number'],segments,'segments',vias,'vias',flush=True)
    (artifacts/'routing-grid-stats.json').write_text(json.dumps(grid.stats,indent=2)+'\n')
    (artifacts/'routing-pads.json').write_text(json.dumps(pads,indent=2)+'\n')
    (artifacts/'routing-summary.json').write_text(json.dumps(counts,indent=2)+'\n')
    return counts
