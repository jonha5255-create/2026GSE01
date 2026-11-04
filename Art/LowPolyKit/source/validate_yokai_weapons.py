import os,json,struct,math
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT,'data','yokai_weapon_catalog.json'),encoding='utf-8') as fp:cat=json.load(fp)
issues=[];files=[]
for family in cat['families']:
 for form,desc in family['forms'].items():
  for variant in desc['variants']:
   path=os.path.join(ROOT,variant['file']);raw=open(path,'rb').read();magic,version,size=struct.unpack_from('<III',raw)
   assert magic==0x46546c67 and version==2 and size==len(raw)
   n,kind=struct.unpack_from('<II',raw,12);g=json.loads(raw[20:20+n]);nodes={o.get('name') for o in g['nodes'] if 'mesh' in o}
   if nodes!=set(variant['nodes']):issues.append([variant['file'],'node mismatch'])
   vertices=triangles=0
   for m in g['meshes']:
    for p in m['primitives']:
     a=g['accessors'][p['attributes']['POSITION']];vertices+=a['count'];triangles+=g['accessors'][p['indices']]['count']//3
     if not all(math.isfinite(v) for v in a['min']+a['max']):issues.append([variant['file'],'nonfinite bounds'])
     if 'NORMAL' not in p['attributes']:issues.append([variant['file'],'missing normals'])
   files.append(dict(file=variant['file'],mesh_nodes=len(nodes),vertices=vertices,triangles=triangles))
report=dict(families=5,forms=10,evolution_variants=len(files),modular_groups=len(cat['meshes']),files=files,issues=issues)
with open(os.path.join(ROOT,'data','yokai_weapon_validation.json'),'w') as fp:json.dump(report,fp,indent=2)
print(json.dumps(dict(variants=len(files),issues=issues)))
assert not issues
