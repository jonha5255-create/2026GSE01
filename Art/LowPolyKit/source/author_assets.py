"""Blender asset-authoring recipe. No game/runtime integration."""
import bpy, math, json, os, random
from mathutils import Vector
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
random.seed(20261020)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
for c in list(bpy.data.collections):
    if c.name != 'Collection': bpy.data.collections.remove(c)
lib = bpy.data.collections.get('Collection'); lib.name = '01_MESH_LIBRARY'
palette = {
 'skin': '#C2967D', 'skin_shadow': '#926E62', 'ink': '#202C35',
 'hair': '#30363E', 'hair_light': '#66716D', 'cloth': '#596C68',
 'cloth_light': '#929889', 'ochre': '#A17D50', 'rust': '#84544A',
 'steel': '#616F78', 'edge': '#A2AFAD', 'stone': '#767E77',
 'concrete': '#91948A', 'darkstone': '#474F50', 'wood': '#665A4B',
 'leaf': '#586B56', 'leaf_light': '#788568', 'dry': '#9A9170',
 'memory': '#64BCB5', 'paper': '#C5BE9F', 'eye': '#101B23'}
M={}
for name, hx in palette.items():
    rgb=[int(hx[i:i+2],16)/255 for i in (1,3,5)]
    mat=bpy.data.materials.new('MAT_'+name); mat.diffuse_color=(*rgb,1)
    mat.use_nodes=True
    bs=mat.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value=(*rgb,1); bs.inputs['Roughness'].default_value=.88
    M[name]=mat
parts=[]; catalog=[]
def mark(obj, mat):
    obj.data.materials.append(M[mat]); parts.append(obj)
    return obj
def box(loc, scale, mat, bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o=bpy.context.object; o.scale=scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod=o.modifiers.new('CutCorners','BEVEL'); mod.width=bevel; mod.segments=1
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=mod.name)
    return mark(o,mat)
def ico(loc, scale, mat, subdivisions=1):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1,location=loc)
    o=bpy.context.object; o.scale=scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return mark(o,mat)
def rod(a,b,r,mat,r2=None,n=6):
    a,b=Vector(a),Vector(b)
    bpy.ops.mesh.primitive_cone_add(vertices=n,radius1=r,radius2=r if r2 is None else r2,depth=(b-a).length,location=(a+b)/2)
    o=bpy.context.object; o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler()
    return mark(o,mat)
def custom(verts,faces,mat):
    mesh=bpy.data.meshes.new('geometry');mesh.from_pydata(verts,[],faces);mesh.update()
    o=bpy.data.objects.new('part',mesh);lib.objects.link(o);return mark(o,mat)
def finish(name,category,index,notes=''):
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    o=bpy.context.object; o.name=name;o.data.name=name+'_Mesh'
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # Explicit triangulation and flat corner normals, portable to ordinary mesh importers.
    mod=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in o.data.polygons:p.use_smooth=False
    o['slot']=category;o['part_index']=index;o['units']='meters';o['front']='-Y';o['notes']=notes
    bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,'meshes',name+'.glb'),export_format='GLB',use_selection=True,export_yup=True)
    catalog.append(dict(mesh=name,slot=category,index=index,file='meshes/'+name+'.glb',triangles=len(o.data.polygons),notes=notes))
    parts.clear();return o
# All NPC parts share foot origin, height 1.8m, head 0.6m, rest pose with relaxed arms.
for idx,sex in enumerate(['Male','Female']):
    ico((0,0,1.5),(.257 if idx==0 else .246,.235,.30),'skin',2)
    rod((0,0,1.12),(0,0,1.26),.09,'skin')
    for side in [-1,1]:
        ico((side*.249,0,1.48),(.046,.06,.078),'skin')
        ico((side*.39,-.015,.73),(.072,.07,.09),'skin')
        box((side*.085,-.221,1.51),(.036,.018,.042),'eye',.006)
    ico((0,-.235,1.45),(.028,.04,.033),'skin_shadow')
    box((0,-.223,1.385),(.07,.012,.012),'skin_shadow')
    finish('SM_NPC_Body_%02d_%s'%(idx,sex),'body',idx,'Head, neck and hands only. Torso and legs supplied by clothing slots. Shared fit envelope.')
for idx,style in enumerate(['Short','Long','Crop','Ponytail']):
    # Open-bottom cap follows head, keeping face exposed on -Y.
    verts=[(0,0,1.82)];N=10
    for j in range(1,4):
        theta=j*math.pi/6
        for i in range(N):
            a=2*math.pi*i/N
            rim_lift = .15*max(0,-math.sin(a)) if j==3 else 0
            verts.append((.268*math.sin(theta)*math.cos(a),.247*math.sin(theta)*math.sin(a),1.51+.312*math.cos(theta)+rim_lift))
    faces=[]
    for i in range(N):faces.append((0,1+i,1+(i+1)%N))
    for j in range(2):
        for i in range(N):
            a=1+j*N+i;b=1+j*N+(i+1)%N;faces.append((a,a+N,b+N,b))
    custom(verts,faces,'hair')
    if idx in (0,1,3):
        for x in [-.17,-.06,.07,.17]:
            ico((x,-.208,1.66),(.075,.055,.12 if x<0 else .08),'hair')
    if idx==1:
        for x in [-.23,.23]:rod((x,.05,1.58),(x,.08,1.00),.085,'hair',.055)
        box((0,.225,1.29),(.39,.09,.43),'hair',.04)
    if idx==3:
        rod((0,.23,1.62),(0,.31,1.18),.09,'hair',.045)
        box((0,.275,1.54),(.15,.08,.055),'rust',.01)
    finish('SM_NPC_Hair_%02d_%s'%(idx,style),'hair',idx)
for idx,style in enumerate(['Citizen','Armor','Archer','Recorder','Pilgrim']):
    color=['cloth','steel','ochre','cloth_light','rust'][idx]
    box((0,0,.99),(.43,.28,.46),color,.055)
    for s in [-1,1]:
        rod((s*.21,0,1.12),(s*.38,0,.79),.088,color,.07)
    box((0,-.153,.79),(.45,.055,.07),'wood',.014)
    box((0,-.19,.79),(.07,.025,.07),'edge',.009)
    if idx==1:
        for s in [-1,1]:ico((s*.25,0,1.15),(.15,.19,.10),'steel')
        box((0,-.162,1.02),(.31,.06,.27),'edge',.025)
    elif idx==2:
        o=box((.04,-.175,1.02),(.065,.045,.45),'wood',.01);o.rotation_euler[1]=-.43
        box((.17,-.18,.87),(.10,.065,.12),'rust',.015)
    elif idx==3:
        box((0,.05,.72),(.43,.28,.18),'cloth_light',.02)
        for s in [-1,1]:box((s*.11,-.16,1.12),(.06,.045,.19),'paper')
    elif idx==4:
        box((0,0,.68),(.48,.31,.25),'rust',.03)
        for s in [-1,1]:rod((s*.19,-.10,1.18),(s*.13,-.19,.94),.042,'cloth_light')
    finish('SM_NPC_Top_%02d_%s'%(idx,style),'top',idx)
for idx,style in enumerate(['Trousers','Guard','Wrapped']):
    box((0,0,.68),(.37,.25,.19),'ink',.025)
    for s in [-1,1]:
        rod((s*.108,0,.70),(s*.115,0,.17),.093,'ink' if idx!=2 else 'cloth',.075)
        if idx==1:box((s*.115,-.08,.37),(.13,.05,.23),'steel',.02)
        if idx==2:
            for z in [.23,.29,.35]:rod((s*.115,0,z),(s*.115,0,z+.035),.081,'cloth_light')
    finish('SM_NPC_Legs_%02d_%s'%(idx,style),'legs',idx)
for idx,style in enumerate(['Boots','Shoes']):
    for s in [-1,1]:box((s*.115,-.04,.105),(.18,.31,.21 if idx==0 else .15),'wood' if idx==0 else 'ink',.035)
    finish('SM_NPC_Feet_%02d_%s'%(idx,style),'feet',idx)
for idx,style in enumerate(['Scarf','Satchel','Quiver','BureauBadge','PilgrimBeads']):
    if idx==0:
        box((0,0,1.20),(.26,.29,.10),'rust',.025);box((-.14,.19,1.04),(.10,.045,.32),'rust',.015)
    elif idx==1:
        box((.29,.01,.78),(.17,.22,.23),'ochre',.025)
        o=box((0,-.18,1.02),(.045,.025,.42),'wood');o.rotation_euler[1]=.6
    elif idx==2:
        rod((.10,.25,.80),(.10,.25,1.14),.09,'wood')
        for x in [.05,.10,.15]:
            rod((x,.25,1.02),(x,.25,1.37),.009,'dry')
            ico((x,.25,1.33),(.025,.018,.06),'paper')
    elif idx==3:box((-.115,-.20,1.08),(.07,.025,.10),'memory',.012)
    else:
        for i in range(9):
            a=math.pi*i/8;ico((.18*math.cos(a),-.18,1.14-.18*math.sin(a)),(.026,.026,.026),'wood')
    finish('SM_NPC_Accessory_%02d_%s'%(idx,style),'accessory',idx)
for idx,style in enumerate(['Bow','Spear','Ledger']):
    if idx==0:
        chain=[(.45,-.02,.45),(.54,-.02,.63),(.57,-.02,.91),(.54,-.02,1.17),(.45,-.02,1.35)]
        for a,b in zip(chain,chain[1:]):rod(a,b,.024,'wood')
        rod(chain[0],chain[-1],.004,'paper')
    elif idx==1:
        rod((.46,0,.03),(.46,0,1.80),.018,'wood')
        ico((.46,0,1.85),(.055,.025,.15),'steel')
    else:
        box((.44,-.06,.78),(.17,.10,.23),'rust',.015)
        box((.44,-.118,.78),(.13,.015,.19),'paper')
    finish('SM_NPC_Prop_%02d_%s'%(idx,style),'prop',idx)
# Modern village architecture: blank signs, oxidized shutters, concrete and ward details.
for idx,style in enumerate(['House','Market','Clinic','Archive','Station','Bureau']):
    w=[3.6,4.8,4.0,3.6,6.0,5.0][idx];d=[3,3,3.2,3,3.5,4][idx];h=[2.8,2.7,3,3.7,4,5][idx]
    box((0,0,.12),(w+.22,d+.22,.24),'darkstone',.05)
    box((0,0,h/2),(w,d,h),'concrete',.08)
    box((0,0,h+.10),(w+.28,d+.28,.22),'darkstone',.04)
    box((-.65,-d/2-.04,.99),(.85,.10,1.92),'wood',.02)
    box((-.65,-d/2-.105,1.62),(.56,.035,.48),'ink')
    for x in [.65,1.28]:
        box((x,-d/2-.04,1.65),(.49,.08,.85),'ink',.02)
        box((x,-d/2-.09,1.65),(.04,.04,.85),'edge')
    box((0,-d/2-.10,h-.35),(w*.70,.15,.36),'cloth',.015)
    for x in [-w*.34,w*.34]:box((x,-d/2-.19,h-.35),(.05,.025,.29),'paper')
    if idx==1:
        o=box((0,-d/2-.59,2.13),(w,.95,.12),'ochre');o.rotation_euler[0]=.17
        for x in [-w*.46,w*.46]:rod((x,-d/2-.96,0),(x,-d/2-.96,2.1),.04,'wood')
        box((0,-d/2-.66,.8),(w*.85,.65,.8),'wood',.025)
    if idx==2:
        box((0,-d/2-.20,h-.35),(.21,.035,.27),'paper')
        box((0,-d/2-.22,h-.35),(.07,.02,.19),'memory')
    if idx>=4:
        for x in [-w*.40,w*.40]:box((x,-d/2-.17,h*.46),(.28,.32,h*.92),'darkstone',.025)
    if idx==3:
        box((-.65,-d/2-.12,1.05),(.25,.018,.4),'paper')
    finish('SM_ENV_Building_%02d_%s'%(idx,style),'building',idx,'Exterior background shell; no interior; openings are inset solid panels.')
for idx,style in enumerate(['Wall','Gate','BrokenWall','Rubble','MemoryShrine','StreetLamp']):
    if idx in (0,2):
        for row in range(4 if idx==0 else 3):
            for col in range(4):
                if idx==2 and col>1 and row>0:continue
                box((col*.75-1.1+(row%2)*.10,0,.25+row*.49),(.72,.40,.46),'stone',.025)
        if idx==0:
            for x in [-1.1,-.35,.4,1.15]:box((x,0,2.18),(.45,.45,.3),'darkstone',.02)
    elif idx==1:
        for x in [-1.7,1.7]:box((x,0,1.5),(.7,.75,3),'stone',.055)
        box((0,0,2.85),(4.1,.75,.4),'darkstone',.035)
        box((0,-.4,2.85),(1.2,.05,.24),'cloth')
    elif idx==3:
        for i in range(10):ico((random.uniform(-.8,.8),random.uniform(-.5,.5),random.uniform(.1,.3)),(.3,.25,.24),'stone')
    elif idx==4:
        box((0,0,.12),(1.25,1.15,.24),'darkstone',.06)
        box((0,0,.38),(.86,.78,.32),'stone',.05)
        ico((0,0,.93),(.25,.22,.53),'ink')
        box((0,-.19,1.03),(.08,.04,.43),'memory',.015)
        for x in [-.42,.42]:rod((x,0,.5),(x,0,1.45),.025,'wood')
        rod((-.42,0,1.4),(.42,0,1.4),.014,'dry')
        for x in [-.25,0,.25]:box((x,0,1.24),(.10,.02,.22),'paper')
    else:
        box((0,0,.12),(.34,.34,.24),'darkstone',.03)
        rod((0,0,.1),(0,0,3.0),.045,'steel')
        rod((0,0,3),(0,-.5,3.1),.035,'steel')
        box((0,-.5,3.05),(.28,.38,.15),'paper',.025)
    finish('SM_ENV_Structure_%02d_%s'%(idx,style),'structure',idx)
for idx,style in enumerate(['Broadleaf','Pine','Dead','Distorted']):
    rod((0,0,0),(.06,0,2.4),.18,'wood',.07,n=7)
    for s in [-1,1]:rod((0,0,1.4),(s*.65,.12,2.35),.07,'wood',.025)
    if idx==0:
        for x,y,z in [(-.6,0,2.5),(.55,.1,2.65),(0,0,3.15)]:ico((x,y,z),(.86,.72,.85),'leaf' if x<0 else 'leaf_light')
    elif idx==1:
        for z,r in [(1.5,.95),(2.1,.73),(2.7,.48)]:rod((0,0,z),(0,0,z+1.0),r,'leaf',0,n=7)
    elif idx==2:
        for s in [-1,1]:rod((s*.5,.1,2.1),(s*.85,.08,2.8),.035,'wood',.008)
    else:
        for x in [-.65,.15,.55]:ico((x,0,2.7+abs(x)),(.6,.55,.65),'ink')
        rod((.18,-.08,1.0),(.23,-.08,2.5),.028,'memory',.01)
    finish('SM_ENV_Tree_%02d_%s'%(idx,style),'tree',idx)
for idx,style in enumerate(['Grass','DryGrass','Reeds','Shrub']):
    if idx==3:
        for x,y in [(-.22,0),(.2,.1),(0,-.2)]:ico((x,y,.3),(.4,.32,.32),'leaf')
    else:
        for i in range(9):
            a=i*2.399;x=.22*math.cos(a);y=.22*math.sin(a);h=random.uniform(.22,.52)*(1.6 if idx==2 else 1)
            # Closed triangular blades give real volume and two-sided silhouettes.
            custom([(x-.035,y,0),(x+.035,y,0),(x,y+.035,0),(x+.08,y+.04,h)],[(0,1,3),(1,2,3),(2,0,3),(0,2,1)],'dry' if idx else 'leaf_light')
            if idx==2:rod((x+.08,y+.04,h*.75),(x+.08,y+.04,h),.025,'ochre')
    finish('SM_ENV_Foliage_%02d_%s'%(idx,style),'foliage',idx)
for idx,style in enumerate(['Small','Boulder','Slab','MemorySplit']):
    ico((0,0,[.24,.7,.22,.8][idx]),[(.45,.36,.3),(1.0,.8,.85),(.9,.65,.28),(.55,.45,.85)][idx],'stone' if idx<3 else 'ink',1 if idx!=1 else 2)
    if idx==3:box((0,-.38,.8),(.045,.04,.8),'memory',.01)
    finish('SM_ENV_Rock_%02d_%s'%(idx,style),'rock',idx)
# Data describes external implementation contracts; no runtime code is changed.
mapping={}
for item in catalog:mapping.setdefault(item['slot'],{})[item['index']]=item['mesh']
names=['도윤','유리','건우','갈치','붉은 눈','미령','덕수','이안','무명 순례자','초소장','백연']
roles=['hunter']*3+['raider']*2+['merchant']*2+['seeker']*2+['guard','recorder']
choices=[(0,0,2,0,0,2,0),(1,3,2,2,0,2,0),(0,2,2,0,1,0,0),(0,0,0,1,0,0,1),(1,1,1,2,0,3,1),(1,1,0,0,1,1,2),(0,2,0,2,1,1,2),(1,3,4,2,1,4,-1),(0,1,4,0,0,4,-1),(0,2,1,1,0,3,1),(1,0,3,0,1,3,2)]
npcs=[]
for i,(name,role,choice) in enumerate(zip(names,roles,choices)):
    npcs.append(dict(appearance_id='NPC_LOOK_%03d'%i,display_name=name,role=role,parts=dict(zip(['body','hair','top','legs','feet','accessory','prop'],choice)),palette_index=i%4,provisional=True))
data=dict(schema_version=1,units='meters',up_axis='Z (Blender); Y (glTF)',front='-Y (Blender); +Z (glTF)',height_m=1.8,head_height_m=.6,none_index=-1,slots=mapping,meshes=catalog)
for file,obj in [('mesh_catalog.json',data),('npc_appearance_presets.json',dict(schema_version=1,note='Authoring presets only; gender and appearance are art proposals, not story canon. No runtime binding.',npcs=npcs)),('palette.json',dict(schema_version=1,materials=palette,variants=[dict(name='village',cloth='cloth',accent='ochre'),dict(name='bureau',cloth='ink',accent='memory'),dict(name='pilgrim',cloth='rust',accent='paper'),dict(name='wilderness',cloth='leaf',accent='dry')],note='Variant indices are handoff suggestions. Exported meshes use default materials; runtime recoloring is not implemented.'))]:
    with open(os.path.join(ROOT,'data',file),'w',encoding='utf-8') as f:json.dump(obj,f,ensure_ascii=False,indent=2)
# Save library at shared origin, hidden in presentation; previews use linked mesh instances.
lib.hide_render=True;lib.hide_viewport=True
show=bpy.data.collections.new('02_NPC_COMBINATION_PREVIEW');bpy.context.scene.collection.children.link(show)
env=bpy.data.collections.new('03_ENVIRONMENT_CONTACT_SHEET');bpy.context.scene.collection.children.link(env)
def instance(source,collection,pos,name=None):
    o=bpy.data.objects.new(name or source.name+'_Display',source.data);collection.objects.link(o);o.location=pos;return o
for i,npc in enumerate(npcs):
    p=((i%6)*2.1,(i//6)*3.1,0)
    for slot,idx in npc['parts'].items():
        if idx>=0:instance(bpy.data.objects[mapping[slot][idx]],show,p,npc['appearance_id']+'__'+slot)
environment=[c for c in catalog if c['slot'] in ['building','structure','tree','foliage','rock']]
for i,item in enumerate(environment):instance(bpy.data.objects[item['mesh']],env,((i%6)*7,(i//6)*7+10,0))
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.resolution_x=1600;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.world.color=(.22,.22,.22)
bpy.ops.object.light_add(type='AREA',location=(4,-6,12));bpy.context.object.data.energy=1800;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=10
bpy.ops.object.light_add(type='SUN',location=(0,0,10));bpy.context.object.rotation_euler=(.4,-.5,-.5);bpy.context.object.data.energy=2
bpy.ops.object.camera_add();cam=bpy.context.object;scene.camera=cam
def aim(pos,target,scale):
    cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=scale
env.hide_render=True
aim((13,-18,12),(5,1.5,.8),15)
scene.render.filepath=os.path.join(ROOT,'previews','npc_lineup.png');bpy.ops.render.render(write_still=True)
show.hide_render=True;env.hide_render=False
aim((43,-35,43),(17,22,0),51)
scene.render.filepath=os.path.join(ROOT,'previews','environment_catalog.png');bpy.ops.render.render(write_still=True)
show.hide_render=False
aim((46,-42,48),(15,16,0),56)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
# Structural asset QA, independent of the game/build.
issues=[]
for c in catalog:
    o=bpy.data.objects[c['mesh']]
    if not len(o.data.polygons):issues.append(c['mesh']+': empty')
    if any(not math.isfinite(v) for vertex in o.data.vertices for v in vertex.co):issues.append(c['mesh']+': nonfinite')
    if any(len(p.vertices)!=3 for p in o.data.polygons):issues.append(c['mesh']+': not triangulated')
    if not os.path.exists(os.path.join(ROOT,c['file'])):issues.append(c['mesh']+': missing export')
for n in npcs:
    for slot,idx in n['parts'].items():
        if idx!=-1 and idx not in mapping[slot]:issues.append(n['appearance_id']+': invalid '+slot)
with open(os.path.join(ROOT,'data','asset_validation.json'),'w') as f:json.dump(dict(mesh_count=len(catalog),npc_presets=len(npcs),total_triangles=sum(c['triangles'] for c in catalog),issues=issues,rigged=False,game_integration=False),f,indent=2)
print('ASSET_AUTHORING_COMPLETE',len(catalog),issues)
