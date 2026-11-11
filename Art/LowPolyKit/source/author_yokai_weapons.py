"""Standalone contract-yokai modeling library; no game integration."""
import bpy, math, os, json
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,'yokai');os.makedirs(OUT,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
scene.unit_settings.system='METRIC'
colors={'shell':(.055,.075,.095),'steel':(.23,.31,.35),'bone':(.73,.72,.59),'edge':(.53,.64,.65),'black':(.012,.02,.028),'cloth':(.15,.24,.25),'red':(.43,.12,.10),'blue':(.08,.58,.82),'gold':(.95,.52,.10),'violet':(.56,.16,.85),'jade':(.10,.74,.61)}
M={}
for n,c in colors.items():
 m=bpy.data.materials.new('MAT_YK_'+n);m.diffuse_color=(*c,1);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=.42 if n in ['shell','steel','edge'] else .65
 if n in ['blue','gold','violet','jade']:p.inputs['Emission Color'].default_value=(*c,1);p.inputs['Emission Strength'].default_value=.55
 if n in ['steel','edge']:p.inputs['Metallic'].default_value=.65
 M[n]=m
parts=[];catalog=[];families=[]
def mat(o,n):
 o.data.materials.append(M[n]);parts.append(o);return o
def sphere(p,s,n):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=10,location=p);o=bpy.context.object;o.scale=s;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 for f in o.data.polygons:f.use_smooth=True
 return mat(o,n)
def box(p,s,n,r=.025):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.scale=s;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if r:
  b=o.modifiers.new('Soft edge','BEVEL');b.width=r;b.segments=3;bpy.ops.object.modifier_apply(modifier=b.name)
 return mat(o,n)
def rod(a,b,r,n,r2=None):
 a,b=Vector(a),Vector(b);bpy.ops.mesh.primitive_cone_add(vertices=12,radius1=r,radius2=r if r2 is None else r2,depth=(b-a).length,location=(a+b)/2);o=bpy.context.object;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
 for f in o.data.polygons:f.use_smooth=True
 return mat(o,n)
def curve(points,r,n):
 for i in range(len(points)-1):rod(points[i],points[i+1],r*(1-i/len(points)),n,r*(1-(i+1)/len(points)) if i<len(points)-2 else .005)
def ring(p,r,n,rotation=(math.pi/2,0,0),minor=.025):
 bpy.ops.mesh.primitive_torus_add(major_segments=20,minor_segments=6,location=p,major_radius=r,minor_radius=minor,rotation=rotation);return mat(bpy.context.object,n)
def cuff():
 # Open through both ends. Local +Y is the back of the hand after mounting.
 vertices=[]
 for z,rx,ry in [(-.16,.125,.109),(.14,.115,.097),(.14,.096,.078),(-.16,.106,.090)]:
  for i in range(16):
   angle=math.tau*i/16
   vertices.append((rx*math.cos(angle),ry*math.sin(angle),z))
 faces=[(j*16+i,j*16+(i+1)%16,((j+1)%4)*16+(i+1)%16,((j+1)%4)*16+i)
        for j in range(4) for i in range(16)]
 mesh=bpy.data.meshes.new('Open cuff');mesh.from_pydata(vertices,[],faces);mesh.update()
 obj=bpy.data.objects.new('Open cuff',mesh);scene.collection.objects.link(obj)
 for face in mesh.polygons:face.use_smooth=face.index<16 or 32<=face.index<48
 return mat(obj,'shell')
def blade(x,y,z,length,width,n='bone',sweep=.12):
 # Double-edged, convex diamond section, tapered tip.
 vs=[]
 for t,w in [(0,.52),(.18,1),(.7,.72),(1,0)]:
  cx=x+sweep*t*t;zz=z+length*t
  vs.extend([(cx-width*w,y,zz),(cx,y-.035,zz),(cx+width*w,y,zz),(cx,y+.035,zz)])
 fs=[]
 for j in range(3):
  for k in range(4):fs.append((j*4+k,j*4+(k+1)%4,(j+1)*4+(k+1)%4,(j+1)*4+k))
 fs.extend([(3,2,1,0),(12,13,14,15)])
 me=bpy.data.meshes.new('BladeGeometry');me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new('Blade',me);scene.collection.objects.link(o);return mat(o,n)
def eye(x,y,z,c,r=.09):
 sphere((x,y,z),(r,r*.4,r),'black');sphere((x,y-r*.34,z),(r*.53,r*.15,r*.65),c)
def finish(family,form,stage):
 bpy.ops.object.select_all(action='DESELECT')
 for o in parts:o.select_set(True)
 bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object
 name='SM_YK_%02d_%s_%s_P%02d'%(family['index'],family['id'],form,stage);o.name=name;o.data.name=name+'_Mesh'
 scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 t=o.modifiers.new('PortableTriangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=t.name)
 o['family_index']=family['index'];o['form_index']=0 if form=='Companion' else 1;o['part_index']=stage;o['unlock_evolution']=stage;o['front']='-Y';o['units']='meters'
 entry=dict(mesh=name,index=len(catalog),family_index=family['index'],form=form,part_index=stage,unlock_evolution=stage,triangles=len(o.data.polygons))
 catalog.append(entry);parts.clear();return o
def companion(f,stage):
 i=f['index'];c=f['color']
 if stage==0:
  if i==0: # Muk: masked memory spirit, shoulders echo sword crossguard.
   sphere((0,0,.70),(.26,.19,.33),'shell');sphere((0,-.13,.98),(.27,.12,.27),'bone')
   for s in [-1,1]:
    eye(s*.095,-.245,1.00,c,.065);curve([(s*.18,0,1.14),(s*.27,.01,1.36),(s*.21,-.02,1.48)],.075,'bone')
    sphere((s*.32,0,.68),(.12,.13,.18),'shell');curve([(s*.17,.05,.49),(s*.14,.12,.21),(s*.30,.12,.12)],.08,'cloth')
   blade(0,-.258,.87,.18,.035,'shell',0);sphere((0,-.19,.69),(.09,.04,.11),c)
  elif i==1: # Ranged: cyclopean shell with stock-like tail and vented carapace.
   sphere((0,0,.64),(.36,.28,.32),'shell');sphere((0,-.24,.72),(.25,.08,.23),'bone');eye(0,-.33,.74,c,.14)
   for s in [-1,1]:
    sphere((s*.31,0,.50),(.13,.19,.13),'steel');rod((s*.17,-.20,.41),(s*.18,-.30,.22),.06,'bone',.035)
   curve([(0,.20,.65),(0,.49,.64),(0,.60,.85)],.10,'steel');ring((0,-.34,.74),.17,'edge')
  elif i==2: # Claw: small feline predator, separate toes and articulated tail silhouette.
   sphere((0,.06,.48),(.23,.36,.21),'shell');sphere((0,-.28,.66),(.24,.18,.22),'shell');sphere((0,-.41,.58),(.14,.08,.09),'bone')
   for s in [-1,1]:
    curve([(s*.16,-.27,.79),(s*.22,-.23,1.00),(s*.09,-.29,.87)],.075,'bone');eye(s*.10,-.438,.70,c,.05)
    for yy in [-.22,.29]:
     rod((s*.18,yy,.45),(s*.21,yy-.07,.18),.075,'steel');sphere((s*.21,yy-.13,.13),(.11,.14,.07),'shell')
     for dx in [-.045,0,.045]:curve([(s*.21+dx,yy-.20,.14),(s*.21+dx,yy-.30,.11)],.021,'bone')
   curve([(0,.32,.53),(.13,.60,.62),(.30,.71,.85),(.35,.55,1.02)],.07,'shell')
  elif i==3: # Soryeong: station lantern, twin rail pods and ticket strips.
   box((0,0,.75),(.40,.30,.51),'shell',.085);box((0,-.165,.79),(.30,.045,.28),'bone',.05)
   for s in [-1,1]:
    eye(s*.075,-.195,.84,c,.052);box((s*.31,0,.73),(.13,.23,.39),'steel',.045);ring((s*.31,-.125,.79),.055,c,minor=.012)
    curve([(s*.12,.02,.53),(s*.16,-.01,.31),(s*.26,.07,.20)],.045,'cloth')
   box((0,0,1.055),(.52,.37,.08),'cloth');sphere((0,-.18,1.08),(.065,.025,.035),'gold');ring((0,0,1.18),.11,'edge',rotation=(math.pi/2,0,0),minor=.018)
  else: # Mes: medical mantis, paired split mask and scissor forearms.
   sphere((0,0,.63),(.19,.14,.27),'cloth')
   for s in [-1,1]:
    sphere((s*.12,-.02,.97),(.14,.14,.23),'bone');eye(s*.11,-.155,1.00,c,.06)
    rod((s*.16,0,.72),(s*.35,-.04,.56),.045,'steel');blade(s*.35,-.04,.55,.37,.06,'bone',-s*.18)
    rod((s*.10,0,.45),(s*.15,.03,.20),.045,'steel');sphere((s*.15,-.04,.17),(.07,.13,.05),'shell')
   box((0,-.165,.88),(.055,.025,.07),'red',.008);box((0,-.17,.88),(.09,.024,.025),'red',.006)
 elif stage==1:
  if i==2:
   for s in [-1,1]:sphere((s*.21,-.22,.24),(.13,.17,.08),'bone')
  else:
   for s in [-1,1]:sphere((s*(.28 if i!=4 else .22),.04,.77),(.13,.19,.18),'steel')
  sphere((0,.13,.83),(.26,.16,.19),'steel')
 elif stage==2:
  for s in [-1,1]:
   for j in range(3):
    z=.52+j*.15;blade(s*(.22+j*.025),.08,z,.21,.065,'bone',s*.17)
 elif stage==3:
  sphere((0,-.19 if i!=2 else -.34,1.14 if i!=2 else .84),(.075,.065,.12),c)
  for s in [-1,1]:
   curve([(s*.20,.10,.92),(s*.34,.16,1.19),(s*.27,.14,1.39)],.06,'bone')
   sphere((s*.29,.02,.73),(.043,.035,.10),c)
def weapon(f,stage):
 i=f['index'];c=f['color']
 if stage==0:
  if i==0:
   rod((0,0,0),(0,0,.28),.045,'shell');sphere((0,0,-.035),(.065,.055,.065),'steel');blade(0,0,.37,.94,.085,sweep=.09)
   box((0,0,.33),(.40,.13,.10),'steel');sphere((0,-.08,.34),(.065,.035,.065),c)
   for z in [.055,.11,.165,.22]:ring((0,0,z),.046,'cloth',rotation=(0,0,0),minor=.008)
  elif i==1:
   # Local Z barrel axis. Rear grip origin, support and muzzle sockets in manifest.
   box((0,.07,.40),(.20,.22,.47),'shell')
   rod((.16,.095,-.095),(0,.09,.23),.045,'steel',.058)
   box((.16,.095,-.12),(.13,.20,.09),'steel',.024)
   grip=box((0,-.085,.026),(.077,.17,.10),'cloth',.023);grip.rotation_euler.x=-.20
   box((0,-.085,.235),(.108,.19,.105),'steel')
   rod((0,.08,.61),(0,.08,1.04),.055,'steel');ring((0,.08,1.05),.06,'bone',rotation=(0,0,0),minor=.015)
   box((0,-.074,.38),(.145,.074,.22),'cloth',.019)
   sphere((0,.178,.51),(.068,.032,.09),c);box((0,.155,.93),(.025,.075,.06),'bone')
   ring((0,-.045,.111),.052,'steel',rotation=(0,math.pi/2,0),minor=.009)
  elif i==2:
   cuff();box((0,.105,.065),(.235,.063,.235),'steel',.028)
   for x in [-.096,0,.096]:blade(x,.104,.15,.41,.028,'bone',x*.45)
   sphere((0,.141,.055),(.047,.021,.061),c)
  elif i==3:
   for s in [-1,1]:
    x=s*.22;box((x,.03,.23),(.12,.18,.44),'shell');box((x,-.06,0),(.095,.15,.20),'cloth');rod((x,.03,.43),(x,.03,.58),.044,'steel');ring((x,.03,.59),.045,'bone',rotation=(0,0,0),minor=.012)
    box((x,-.075,.31),(.09,.045,.14),'bone');sphere((x,-.105,.31),(.035,.02,.045),c);ring((x,-.07,.1),.065,'steel',rotation=(0,math.pi/2,0),minor=.011)
  else:
   for s in [-1,1]:
    ring((s*.11,0,-.12),.095,'shell',minor=.029);rod((s*.11,0,-.04),(s*.045,0,.24),.035,'steel');blade(s*.045,s*.02,.22,.79,.07,'bone',s*.25)
   rod((0,-.075,.25),(0,.075,.25),.055,'steel');sphere((0,-.08,.25),(.04,.02,.04),c)
 elif stage==1:
  if i==0:
   blade(0,.02,.39,.77,.14,'shell',.07)
   for s in [-1,1]:curve([(s*.12,0,.35),(s*.27,0,.44),(s*.30,0,.59)],.055,'bone')
  elif i in [1,3]:
   for s in ([-1,1] if i==3 else [0]):
    x=s*.22
    for j in range(3):box((x,.07,.30+j*.09),(.17 if i==3 else .29,.25,.065),'steel')
  elif i==2:box((0,.124,.028),(.244,.049,.195),'bone',.022)
  else:
   for s in [-1,1]:blade(s*.05,s*.025,.25,.67,.10,'steel',s*.25)
 elif stage==2:
  for s in [-1,1]:
   if i in [1,3]:curve([(s*.12,.10,.28),(s*.25,.12,.10),(s*.22,.12,-.06)],.055,'bone')
   elif i==2:
    for j in range(3):blade(s*.105,.100,-.13+j*.064,.145,.031,'bone',s*.05)
   else:
    for j in range(3):blade(s*(.09+j*.025),.02,.42+j*.16,.19,.04,'bone',s*.14)
 elif stage==3:
  if i in [1,3]:
   for s in [-1,1]:box((s*(.08 if i==1 else .22),-.11,.43),(.025,.025,.37 if i==1 else .26),c,.008)
   sphere((0 if i==1 else -.22,-.09,.23),(.07,.035,.09),c)
  elif i==2:
   sphere((0,.158,.026),(.061,.025,.079),c)
   for x in [-.096,0,.096]:blade(x,.14,.187,.30,.012,c,x*.38)
  else:
   blade(0,-.045,.49,.53,.026,c,.06 if i==0 else 0);sphere((0,-.09,.33),(.07,.04,.09),c)
defs=[('Muk','묵','Sword','blue'),('SoulRifle','영혼총 요괴','SoulRifle','gold'),('Claw','클로 요괴','Claw','violet'),('Soryeong','소령','DualGun','gold'),('Mes','메스','ScissorDualBlade','jade')]
for idx,(id,ko,typ,c) in enumerate(defs):
 f=dict(index=idx,id=id,name=ko,weapon_type=typ,color=c,forms={});families.append(f)
 for form,fn in [('Companion',companion),('Weapon',weapon)]:
  objs=[]
  for stage in range(4):fn(f,stage);objs.append(finish(f,form,stage))
  f['forms'][form]=dict(parts=[o.name for o in objs],variants=[])
  for ev in range(4):
   bpy.ops.object.select_all(action='DESELECT')
   for o in objs[:ev+1]:o.select_set(True)
   filename='YK_%02d_%s_%s_E%02d.glb'%(idx,id,form,ev)
   bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,filename),export_format='GLB',use_selection=True,export_yup=True,export_animations=False)
   f['forms'][form]['variants'].append(dict(evolution=ev,level=[1,3,6,9][ev],file='yokai/'+filename,part_indices=list(range(ev+1))))
  # Source library on a separate collection, instance layout uses linked mesh data.
  for o in objs:o.hide_render=True;o.hide_set(True)
  f['forms'][form]['sockets']=({'root':[0,0,0],'soul_core':[0,-.19,.69]} if form=='Companion' else {'grip':[0,0,0],'forward':[0,0,1]})
  if form=='Weapon' and idx==0:f['forms'][form]['sockets'].update(grip=[0,0,.128])
  if form=='Weapon' and idx==1:f['forms'][form]['sockets'].update(grip=[0,-.085,.026],support_hand=[0,-.127,.38],muzzle=[0,.08,1.06])
  if form=='Weapon' and idx==2:f['forms'][form]['sockets'].update(cuff_back=[0,0,-.16],knuckle=[0,.105,.14])
  if form=='Weapon' and idx==3:f['forms'][form]['sockets'].update(grip_L=[-.22,-.06,0],grip_R=[.22,-.06,0],muzzle_L=[-.22,.03,.59],muzzle_R=[.22,.03,.59])
  if form=='Weapon' and idx==4:f['forms'][form]['sockets'].update(grip_L=[-.11,0,-.12],grip_R=[.11,0,-.12],pivot=[0,0,.25],split_blades=True)
# Additional limb/grip group indices preserved on vertices for future rigging / splitting.
for f in families:
 for form,desc in f['forms'].items():
  for name in desc['parts']:
   o=bpy.data.objects[name]
   if form=='Weapon' and f['index'] in [3,4]:
    for s,side in [(-1,'L'),(1,'R')]:
     g=o.vertex_groups.new(name='PART_'+side);ids=[v.index for v in o.data.vertices if (v.co.x<0)==(s<0)];
     if ids:g.add(ids,1,'REPLACE')
# Re-export with semantic vertex groups recorded in blend, glTF keeps separate evolution nodes.
def label(text,p,size=.13):
 cu=bpy.data.curves.new('Label','FONT');cu.body=text;cu.size=size;cu.align_x='CENTER';o=bpy.data.objects.new('LABEL_'+text,cu);scene.collection.objects.link(o);o.location=p;o.rotation_euler=(math.radians(68),0,0);o.data.materials.append(M['bone']);return o
display=[]
for f in families:
 for col,(form,ev) in enumerate([('Companion',0),('Weapon',0),('Companion',3),('Weapon',3)]):
  offset=Vector((col*2.1,-f['index']*2.0,0));group=[]
  for name in f['forms'][form]['parts'][:ev+1]:
   src=bpy.data.objects[name];o=src.copy();o.data=src.data;scene.collection.objects.link(o);o.name='DISPLAY_'+name+'_E%d'%ev;o.location=offset;o.hide_render=False;o.hide_set(False);group.append(o)
  boxparts=parts[:];parts.clear()
  label('%s / %s E%d'%(f['id'],form,ev),(offset.x,offset.y-.67,.02),.105)
  display.extend(group)
# Studio, deliberately orthographic for side-by-side silhouette assessment.
ground=box((3.1,-4,-.16),(14,14,.12),'black',.05);parts.clear()
bpy.ops.object.camera_add(location=(8,-17,14));cam=bpy.context.object;cam.rotation_euler=(Vector((3.1,-3.8,.55))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=12.4;scene.camera=cam
for p,power,size in [((1,-6,10),1800,8),((8,-1,6),1300,6),((-4,2,5),1600,5)]:
 bpy.ops.object.light_add(type='AREA',location=p);o=bpy.context.object;o.data.energy=power;o.data.shape='DISK';o.data.size=size;o.rotation_euler=(Vector((3,-4,.5))-o.location).to_track_quat('-Z','Y').to_euler()
scene.world.color=(.17,.17,.17);scene.render.engine='CYCLES';scene.cycles.samples=24;scene.render.resolution_x=1800;scene.render.resolution_y=2000;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG';scene.render.filepath=os.path.join(ROOT,'previews','yokai_weapon_lineup.png')
report=dict(version=1,units='meters',blender_front='-Y',glb_up='Y',companion_front_glb='+Z',weapon_forward_blender='+Z',weapon_forward_glb='+Y',evolution_levels=[1,3,6,9],selection_schema=dict(family_index='0..4',form_index='0 companion / 1 weapon',evolution='0..3; include parts 0 through evolution'),families=families,meshes=catalog,notes=['Modeling assets only; no game integration or new animation clips.','Companion and weapon share material colors and visual motifs; not topology morph targets.','Evolution GLBs contain separate base/additive mesh nodes; shared source mesh data.','Mes scissor/dual-blade and Soryeong paired weapons have L/R vertex groups in blend for splitting/rigging.'])
with open(os.path.join(ROOT,'data','yokai_weapon_catalog.json'),'w',encoding='utf-8') as fp:json.dump(report,fp,ensure_ascii=False,indent=2)
issues=[]
for e in catalog:
 o=bpy.data.objects[e['mesh']]
 if not o.data.vertices or any(not math.isfinite(v) for p in o.data.vertices for v in p.co):issues.append(e['mesh'])
with open(os.path.join(ROOT,'data','yokai_weapon_validation.json'),'w') as fp:json.dump(dict(meshes=len(catalog),variants=40,issues=issues,triangles=sum(e['triangles'] for e in catalog)),fp,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Yokai_Weapons.blend'))
bpy.ops.render.render(write_still=True)
print('YOKAI_WEAPONS_COMPLETE',flush=True)
