"""Author portable mesh VFX, baked transforms and transparent flipbooks."""
import bpy,math,os,json,random
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,'vfx');FR=os.path.join(OUT,'frames');os.makedirs(FR,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene;scene.render.fps=30;random.seed(41)
palette={'blue':(.08,.55,1),'gold':(1,.52,.06),'violet':(.62,.12,1),'jade':(.08,1,.63),'white':(.72,.91,1)}
mats={}
for key,col in palette.items():
 m=bpy.data.materials.new('MAT_VFX_'+key);m.use_nodes=True;n=m.node_tree.nodes;n.clear();out=n.new('ShaderNodeOutputMaterial');e=n.new('ShaderNodeEmission');e.inputs[0].default_value=(*col,1);e.inputs[1].default_value=2;m.node_tree.links.new(e.outputs[0],out.inputs['Surface']);m.diffuse_color=(*col,1);mats[key]=m
objects=[]
def mesh(name,verts,faces,color):
 me=bpy.data.meshes.new(name+'_Mesh');me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new(name,me);scene.collection.objects.link(o);o.data.materials.append(mats[color]);objects.append(o);return o
def arc(name,r,w,a,b,z,color,tilt=0):
 vs=[];n=40
 for j in range(n+1):
  t=j/n;ang=a+(b-a)*t;ww=w*math.sin(math.pi*t)**.55
  for rr in [r-ww,r+ww]:vs.append((rr*math.cos(ang),rr*math.sin(ang)*math.cos(tilt),z+rr*math.sin(ang)*math.sin(tilt)))
 return mesh(name,vs,[(j*2,j*2+1,j*2+3,j*2+2) for j in range(n)],color)
def star(name,p,r,color,arms=6):
 vs=[p]
 for j in range(arms*2):
  a=j*math.pi/arms;rr=r if j%2==0 else r*.20;vs.append((p[0]+rr*math.cos(a),p[1]+rr*math.sin(a),p[2]))
 return mesh(name,vs,[(0,1+j,1+(j+1)%(arms*2)) for j in range(arms*2)],color)
def streak(name,a,b,w,color):
 a,b=Vector(a),Vector(b);d=b-a;side=d.cross(Vector((0,0,1))).normalized()*w
 return mesh(name,[a,b+side,b,b-side],[(0,1,2),(0,2,3)],color)
def animate(o,start,end,n,travel=(0,0,0),expand=1.25):
 base=o.location.copy()
 for frame,s,t in [(1,.001,0),(start,.001,0),(min(start+2,end-1),1,0),(end,expand,1),(min(end+1,n),.001,1),(n,.001,1)]:
  o.scale=(s,s,s);o.location=base+Vector(travel)*t;o.keyframe_insert('scale',frame=frame);o.keyframe_insert('location',frame=frame)
 if o.animation_data and o.animation_data.action:
  act=o.animation_data.action;act.name='KEY_'+o.name
  track=o.animation_data.nla_tracks.new();track.name=current_id;track.strips.new(current_id,1,act);o.animation_data.action=None
defs=[('Muk_Slash',0,'basic','blue',16),('Muk_MoonSweep',0,'skill','blue',24),('SoulRifle_Fire',1,'basic','gold',16),('SoulRifle_Scatter',1,'skill','gold',24),('Claw_Rake',2,'basic','violet',16),('Claw_TripleRake',2,'skill','violet',24),('Soryeong_DualShot',3,'basic','gold',16),('Soryeong_RailBurst',3,'skill','gold',24),('Mes_CrossCut',4,'basic','jade',16),('Mes_ScissorSnap',4,'skill','jade',24),('Shared_Hit',-1,'impact','white',16)]
catalog=[]
for fxindex,(current_id,family,kind,color,n) in enumerate(defs):
 objects=[];skill=kind=='skill'
 if family==0:
  arc(current_id+'_Blade',1,.09,-2.7,1.1,.05,color,.18)
  arc(current_id+'_Core',.99,.025,-2.3,1.05,.055,'white',.18)
  if skill:
   for j in range(3):arc(current_id+'_Moon%d'%j,1.1+j*.22,.035,-math.pi,math.pi,.02+j*.04,color)
 elif family in [1,3]:
  for k,x in enumerate([0] if family==1 else [-.23,.23]):
   star(current_id+'_Muzzle%d'%k,(x,-.67,.02),.25,color)
   star(current_id+'_HotCore%d'%k,(x,-.67,.03),.12,'white',4)
   count=5 if skill else 1
   for j in range(count):
    a=(j-(count-1)/2)*.20;start=(x,-.55,.04);end=(x+math.sin(a)*1.5,1.10*math.cos(a),.04)
    streak(current_id+'_Tracer%d_%d'%(k,j),start,end,.018,color)
   if skill:arc(current_id+'_Shock%d'%k,.42,.045,-math.pi,math.pi,.03,color)
 elif family==2:
  for hit in range(3 if skill else 1):
   for j in range(3):
    o=arc(current_id+'_Claw%d_%d'%(hit,j),.70+j*.14,.035,-2.7+hit*.28,.60+hit*.28,.04+hit*.05,color,.08)
    o['burst_index']=hit
 elif family==4:
  streak(current_id+'_CrossA',(-.72,-.72,.09),(.72,.72,.09),.06,color)
  streak(current_id+'_CrossB',(-.72,.72,.10),(.72,-.72,.10),.06,color)
  for sign in [-1,1]:
   o=arc(current_id+'_Blade%d'%sign,.95,.055,-2.5,.9,.05,color,sign*.5);o.rotation_euler.z=sign*.67
  if skill:
   star(current_id+'_Pivot',(0,0,.08),.37,'white',4)
   arc(current_id+'_SnapRing',.8,.035,-math.pi,math.pi,.02,color)
 else:
  star(current_id+'_Core',(0,0,.03),.32,'white',8)
 for j in range(8 if skill else 5):
  a=random.uniform(-math.pi,math.pi);r=random.uniform(.55,1.05)
  streak(current_id+'_Spark%02d'%j,(r*.7*math.cos(a),r*.7*math.sin(a),.07),(r*math.cos(a),r*math.sin(a),.07),.016,color)
 for j,o in enumerate(objects):
  o['effect_index']=fxindex;o['family_index']=family;o['component_index']=j
  start=2+int(o.get('burst_index',0))*4
  end=min(n-2,start+(4 if 'Muzzle' in o.name or 'HotCore' in o.name else 9))
  animate(o,start,end,n,travel=(0,.35,0) if 'Tracer' in o.name else (0,0,0),expand=1.4)
 scene.frame_start=1;scene.frame_end=n;scene.frame_set(5)
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 filename=current_id+'.glb'
 bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,filename),export_format='GLB',use_selection=True,export_yup=True,export_animation_mode='NLA_TRACKS',export_force_sampling=True)
 catalog.append(dict(index=fxindex,id=current_id,family_index=family,kind=kind,frames=n,fps=30,duration_s=n/30,glb='vfx/'+filename,nodes=[o.name for o in objects],color=color,anchor='hit_position' if family<0 else ('muzzle_socket' if family in [1,3] else 'attack_plane'),sprite_sheet='vfx/'+current_id+'_atlas.png',columns=4,rows=math.ceil(n/4),cell_px=256,frame_order='left-to-right then top-to-bottom',blend='additive recommended; straight alpha fallback',events=[dict(frame=2,event='visual_onset')]+([dict(frame=6,event='second_rake'),dict(frame=10,event='third_rake')] if current_id=='Claw_TripleRake' else [])))
 for o in objects:o.hide_render=True
# Orthographic transparent camera, looks at local XY plane.
bpy.ops.object.camera_add(location=(0,0,6));camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=4.8;scene.camera=camera
scene.render.engine='CYCLES';scene.cycles.samples=8;scene.render.film_transparent=True;scene.render.resolution_x=256;scene.render.resolution_y=256;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.view_settings.view_transform='Standard'
for fx in catalog:
 for o in bpy.data.objects:
  if o.type=='MESH':o.hide_render=o.name not in fx['nodes']
 for frame in range(1,fx['frames']+1):
  scene.frame_set(frame);scene.render.filepath=os.path.join(FR,'%s_%02d.png'%(fx['id'],frame));bpy.ops.render.render(write_still=True)
# Save a single effect selected and visible, other effects accessible by named collection.
for fx in catalog:
 collection=bpy.data.collections.new('FX_%02d_%s'%(fx['index'],fx['id']));scene.collection.children.link(collection)
 for name in fx['nodes']:
  o=bpy.data.objects[name]
  for c in list(o.users_collection):c.objects.unlink(o)
  collection.objects.link(o);o.hide_render=fx['index']!=0;o.hide_set(fx['index']!=0)
scene.frame_end=24;scene.frame_set(5)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Weapon_VFX.blend'))
with open(os.path.join(ROOT,'data','weapon_vfx_catalog.json'),'w',encoding='utf-8') as fp:json.dump(dict(version=1,units='meters',local_plane_blender='XY',plane_normal_blender='+Z',local_plane_glb='XZ',plane_normal_glb='+Y',effects=catalog,notes=['No game code, damage timing, collision or runtime spawning implemented.','GLB clips animate node transforms; shader emission is static.','RGBA flipbooks contain the rendered timing including disappearance.','Rotate the local effect plane to the swing/shot plane when spawning.','Basic and skill durations are visual lifetimes, not gameplay attack cooldowns.']),fp,indent=2,ensure_ascii=False)
print('WEAPON_VFX_COMPLETE',flush=True)
