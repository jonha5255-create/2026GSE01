"""Split paired weapons into independently attachable GLB nodes."""
import bpy,bmesh,os,json
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Yokai_Weapons.blend'))
path=os.path.join(ROOT,'data','yokai_weapon_catalog.json')
with open(path,encoding='utf-8') as fp:cat=json.load(fp)
for f in cat['families']:
 for form,desc in f['forms'].items():
  desc['export_nodes']={}
  for name in desc['parts']:
   src=bpy.data.objects[name]
   if form=='Weapon' and f['index'] in [3,4]:
    nodes=[]
    for sign,side in [(-1,'L'),(1,'R')]:
     o=src.copy();o.data=src.data.copy();bpy.context.scene.collection.objects.link(o);o.name=name+'_'+side;o.data.name=o.name+'_Mesh'
     bm=bmesh.new();bm.from_mesh(o.data)
     bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=.000001,plane_co=(0,0,0),plane_no=(1,0,0),clear_inner=sign>0,clear_outer=sign<0)
     edges=[e for e in bm.edges if e.is_boundary]
     if edges:bmesh.ops.holes_fill(bm,edges=edges,sides=0)
     bmesh.ops.triangulate(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free();o.data.update();o['side_index']=0 if side=='L' else 1;o.hide_render=True;o.hide_set(True);nodes.append(o.name)
    desc['export_nodes'][name]=nodes
   else:desc['export_nodes'][name]=[name]
  for variant in desc['variants']:
   bpy.ops.object.select_all(action='DESELECT');objects=[]
   for name in desc['parts'][:variant['evolution']+1]:
    for n in desc['export_nodes'][name]:
     o=bpy.data.objects[n];o.hide_set(False);o.hide_render=False;o.select_set(True);objects.append(o)
   bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,variant['file']),export_format='GLB',use_selection=True,export_yup=True,export_animations=False)
   variant['nodes']=[o.name for o in objects]
   for o in objects:o.hide_set(True);o.hide_render=True
  if form=='Companion':
   cores=[(0,-.19,.69),(0,-.33,.74),(0,-.438,.70),(0,-.195,.84),(0,-.155,1.00)]
   desc['sockets']['soul_core']=cores[f['index']]
cat['notes'][-1]='Soryeong paired guns and Mes scissor blades export as separate L/R nodes. Mes pivot stays at (0,0,0.25) in Blender space; no animation is baked.'
with open(path,'w',encoding='utf-8') as fp:json.dump(cat,fp,ensure_ascii=False,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Yokai_Weapons.blend'))
print('YOKAI_FINALIZE_COMPLETE',flush=True)
