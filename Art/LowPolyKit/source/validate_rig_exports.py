"""Read actual GLB payloads: skins, weights, named clips and finite samples."""
import json,struct,os,math
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
issues=[];files=[]
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:manifest=json.load(f)
expected_counts={os.path.basename(c['file']):len(c['clips']) for c in manifest['creatures']}
def inspect(path):
    b=open(path,'rb').read();magic,version,total=struct.unpack_from('<III',b,0)
    if magic!=0x46546c67 or version!=2 or total!=len(b):issues.append(path+': invalid header')
    n=struct.unpack_from('<I',b,12)[0];d=json.loads(b[20:20+n]);binary=b[28+n:]
    def accessor(idx):
        a=d['accessors'][idx];v=d['bufferViews'][a['bufferView']]
        types={5126:('f',4),5125:('I',4),5123:('H',2),5121:('B',1)}
        fmt,size=types[a['componentType']];width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        offset=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',size*width)
        return [struct.unpack_from('<'+fmt*width,binary,offset+i*stride) for i in range(a['count'])]
    skins=d.get('skins',[]);animations=d.get('animations',[])
    if len(skins)!=1:issues.append(path+': expected one skeleton')
    vertex_count=0
    for mesh in d.get('meshes',[]):
        for p in mesh['primitives']:
            attrs=p['attributes']
            if 'JOINTS_0' not in attrs or 'WEIGHTS_0' not in attrs:issues.append(path+': missing skin attributes');continue
            for weights in accessor(attrs['WEIGHTS_0']):
                if abs(sum(weights)-1)>.002:issues.append(path+': bad skin weights');break
            vertex_count+=d['accessors'][attrs['POSITION']]['count']
    for anim in animations:
        for sampler in anim['samplers']:
            ts=[x[0] for x in accessor(sampler['input'])]
            if any(y<=x for x,y in zip(ts,ts[1:])):issues.append(path+': nonmonotonic keys')
            if any(not math.isfinite(y) for x in accessor(sampler['output']) for y in x):issues.append(path+': nonfinite animation')
        if anim['name'].endswith(('_Idle','_Walk','_Run')):
            for channel in anim['channels']:
                sampler=anim['samplers'][channel['sampler']];values=accessor(sampler['output'])
                a,z=values[0],values[-1]
                delta=math.sqrt(sum((x-y)**2 for x,y in zip(a,z)))
                if channel['target']['path']=='rotation':delta=min(delta,math.sqrt(sum((x+y)**2 for x,y in zip(a,z))))
                if delta>.001:issues.append(path+': nonmatching loop '+anim['name'])
    return dict(file='rigged/'+os.path.basename(path),skins=len(skins),bones=len(skins[0]['joints']) if skins else 0,meshes=len(d.get('meshes',[])),vertices=vertex_count,clips=[a['name'] for a in animations])
for name in sorted(os.listdir(os.path.join(ROOT,'rigged'))):
    if name.endswith('.glb'):
        item=inspect(os.path.join(ROOT,'rigged',name));expected=expected_counts.get(name,20)
        if len(item['clips'])!=expected:issues.append(name+': wrong action count')
        files.append(item)
report=dict(files_checked=len(files),issues=issues,files=files)
with open(os.path.join(ROOT,'data','glb_animation_validation.json'),'w') as f:json.dump(report,f,indent=2)
print('GLB_CHECKS',len(files),'ISSUES',issues)
if issues:raise SystemExit(1)
