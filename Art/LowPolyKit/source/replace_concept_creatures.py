"""Replace the first monster draft with 4 humanoids and 3 beasts from the supplied board.
NPC rigs, modular meshes, weapon animations and all environment assets are retained.
"""
import bpy,math,os,json,random
from mathutils import Vector,Quaternion,Matrix
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,'rigged');random.seed(20261020)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
scene=bpy.context.scene;scene.render.fps=30
collection=bpy.data.collections['05_RIGGED_CHARACTERS'];collection.hide_viewport=False
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:manifest=json.load(f)
old_files=[c['file'] for c in manifest['creatures']]
for obj in list(bpy.data.objects):
    if obj.name.startswith(('SK_MOB_','RIG_MOB_')):bpy.data.objects.remove(obj,do_unlink=True)
for c in list(collection.children):
    if c.name.startswith('SK_MOB_'):bpy.data.collections.remove(c)
for a in list(bpy.data.actions):
    if a.name.startswith('MOB_'):bpy.data.actions.remove(a)
layer=bpy.context.view_layer.layer_collection.children[collection.name];bpy.context.view_layer.active_layer_collection=layer
M={m.name.removeprefix('MAT_'):m for m in bpy.data.materials}
colors={'cardboard':'#AA8253','card_side':'#826443','navy':'#263E50','uniform':'#3D5964',
        'oni_red':'#A72D36','red_dark':'#5D252E','core_red':'#FF3048','gunmetal':'#303E4D',
        'armor':'#A9ABB0','mask':'#D0C7AD','void':'#29253F','violet_plate':'#575175',
        'flesh':'#82534F','tendon':'#B44E55','gold':'#B5965C'}
for name,h in colors.items():
    m=bpy.data.materials.new('MAT_'+name);m.use_nodes=True
    # Convert board-like sRGB colors to linear shader inputs.
    rgb=tuple(int(h[i:i+2],16)/255 for i in (1,3,5))
    linear=tuple(x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb)
    m.diffuse_color=(*rgb,1);bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*linear,1);bs.inputs['Roughness'].default_value=.82
    if name=='core_red':bs.inputs['Emission Color'].default_value=(*linear,1);bs.inputs['Emission Strength'].default_value=2
    M[name]=m
with open(os.path.join(ROOT,'source','author_rigs_and_creatures.py'),encoding='utf-8') as f:recipe=f.read()
# Reuse the tested offline rig/skin/keyframe authoring helpers, without rerunning NPC generation.
exec(recipe[recipe.index('def bone_rig('):recipe.index('clips=[];rigs=[];')])
exec(recipe[recipe.index('chunks=[]'):recipe.index("bow=human_parts[('prop',0)]")])
exec(recipe[recipe.index('def quadruped_spec('):recipe.index('def build_quad(')])
exec(recipe[recipe.index('def creature_pose('):recipe.index('for c in creatures:\n    rig=')])
clips=[];rigs=[];creatures=[]
def poly(verts,faces,mat,bone):
    mesh=bpy.data.meshes.new('ConceptDetail');mesh.from_pydata(verts,[],faces);mesh.update()
    obj=bpy.data.objects.new('Detail',mesh);collection.objects.link(obj);return geom(obj,mat,bone)
def head_eyes(v,x,y,z,size,bone='head'):
    for s in [-1,1]:ellipsoid(v((s*x,y,z)),v((size,.025,size*.70)),'core_red',bone)
def teeth(v,y,z,width,count,bone='head',up=False):
    for i in range(count):
        x=-width/2+width*i/max(1,count-1)
        segment(v((x,y,z)),v((x,y-.018,z+(.09 if up else -.09))),.032*v((1,1,1))[0],'mask',bone,.001,n=4)
def new_biped(idx,name,label,scale,kind):
    spec=[b for b in humanoid_spec() if b[0]!='bow_string']
    spec=[(n,tuple(x*scale for x in a),tuple(x*scale for x in b),p) for n,a,b,p in spec]
    rig=bone_rig('RIG_MOB_%02d_%s'%(idx,name),spec);rigs.append(rig)
    return rig,lambda p:tuple(x*scale for x in p)
def biped_limbs(rig,v,kind):
    for side,s in [('L',-1),('R',1)]:
        for limb in ['upper_arm','forearm','thigh','shin']:
            bone=limb+'.'+side;b=rig.data.bones[bone]
            mat='gunmetal' if kind=='security' else 'oni_red' if kind=='oni' else 'navy'
            r=.105 if limb in ['upper_arm','thigh'] else .085
            segment(b.head_local,b.tail_local,r*v((1,1,1))[0],mat,bone,r*.78*v((1,1,1))[0])
            ellipsoid(b.head_local,v((r*1.1,r*1.1,r*1.1)),'red_dark' if kind in ['oni','security'] else mat,bone)
            if kind=='security':
                center=(b.head_local+b.tail_local)*.5
                ellipsoid(center,v((r*1.30,r*1.30,b.length/v((1,1,1))[0]*.42)),'armor',bone)
        ellipsoid(v((s*.39,-.015,.73)),v((.105,.09,.11)),'mask' if kind=='paper' else 'flesh' if kind=='station' else 'oni_red' if kind=='oni' else 'armor','hand.'+side)
        cube(v((s*.115,-.055,.10)),v((.20,.30,.20)),'gunmetal' if kind!='oni' else 'red_dark','foot.'+side)
        if kind=='oni':
            for dx in [-.05,0,.05]:segment(v((s*.39+dx,-.07,.73)),v((s*.39+dx,-.27,.70)),.023*v((1,1,1))[0],'mask','hand.'+side,.001,n=4)
def finish_biped(idx,name,label,rig,scale,kind):
    mesh=join_mesh('SK_MOB_%02d_%s'%(idx,name),rig)
    creatures.append(dict(index=idx,name=name,label=label,rig=rig,mesh=mesh,scale=scale,kind='golem' if kind=='security' else 'wraith' if kind=='oni' else kind,gait='biped'))
# 00: cardboard face, folded flaps, work shorts and a compact human-like body.
rig,v=new_biped(0,'PaperBoxYokai','종이상자 요괴',.84,'paper')
cube(v((0,0,.99)),v((.42,.29,.43)),'card_side','chest')
cube(v((0,0,.72)),v((.37,.27,.22)),'navy','pelvis')
biped_limbs(rig,v,'paper')
cube(v((0,0,1.49)),v((.69,.56,.61)),'cardboard','head')
cube(v((.348,0,1.49)),v((.012,.55,.58)),'card_side','head')
for s in [-1,1]:
    cube(v((s*.14,-.289,1.54)),v((.050,.023,.075)),'ink','head')
    cube(v((s*.23,-.291,1.43)),v((.075,.022,.038)),'oni_red','head')
    poly([v((s*.35,-.17,1.70)),v((s*.45,-.17,1.57)),v((s*.35,.15,1.57))],[(0,1,2)],'card_side','head')
segment(v((-.08,-.294,1.43)),v((0,-.302,1.39)),.012*v((1,1,1))[0],'ink','head')
segment(v((0,-.302,1.39)),v((.08,-.294,1.43)),.012*v((1,1,1))[0],'ink','head')
cube(v((0,0,1.804)),v((.075,.55,.014)),'mask','head')
finish_biped(0,'PaperBoxYokai','종이상자 요괴',rig,.84,'paper')
# 01: railway uniform, cap with a modeled badge, pale spirit face and signal baton.
rig,v=new_biped(1,'StationWarden','역무원 요괴',.94,'station')
cube(v((0,0,1.0)),v((.46,.31,.47)),'uniform','chest')
cube(v((0,0,.74)),v((.39,.30,.24)),'navy','pelvis');biped_limbs(rig,v,'station')
ellipsoid(v((0,0,1.49)),v((.27,.23,.29)),'mask','head',2)
cube(v((0,-.22,1.51)),v((.42,.045,.13)),'ink','head');head_eyes(v,.10,-.25,1.52,.035)
segment(v((0,0,1.71)),v((0,0,1.84)),.29*.94,'navy','head',.27*.94,n=8)
cube(v((0,-.17,1.72)),v((.62,.48,.055)),'gunmetal','head')
ellipsoid(v((0,-.29,1.78)),v((.06,.025,.055)),'gold','head')
cube(v((0,-.175,1.03)),v((.055,.024,.29)),'navy','chest')
for z in [.94,1.04,1.14]:ellipsoid(v((0,-.195,z)),v((.018,.015,.018)),'gold','chest')
for s in [-1,1]:cube(v((s*.14,-.17,1.10)),v((.11,.035,.10)),'navy','chest')
segment(v((.39,-.015,.64)),v((.39,-.015,1.25)),.030*.94,'gunmetal','hand.R')
segment(v((.39,-.015,1.0)),v((.39,-.015,1.27)),.042*.94,'core_red','hand.R')
finish_biped(1,'StationWarden','역무원 요괴',rig,.94,'station')
# 02: red horned oni, faceted mask and large articulated claws.
rig,v=new_biped(2,'RedHornOni','붉은 뿔 요괴',.98,'oni')
ellipsoid(v((0,0,.99)),v((.30,.21,.29)),'red_dark','chest',2)
cube(v((0,0,.72)),v((.39,.28,.24)),'gunmetal','pelvis');biped_limbs(rig,v,'oni')
ellipsoid(v((0,0,1.49)),v((.34,.28,.31)),'oni_red','head',2)
for s in [-1,1]:
    ellipsoid(v((s*.14,-.25,1.56)),v((.12,.055,.08)),'ink','head')
    ellipsoid(v((s*.14,-.294,1.56)),v((.045,.016,.035)),'core_red','head')
    segment(v((s*.20,0,1.70)),v((s*.29,-.015,1.96)),.095*.98,'mask','head',.025*.98)
    segment(v((s*.29,-.015,1.96)),v((s*.25,-.08,2.09)),.025*.98,'red_dark','head',.001,n=5)
    ellipsoid(v((s*.27,0,1.16)),v((.15,.18,.13)),'gunmetal','upper_arm.'+('L' if s<0 else 'R'))
cube(v((0,-.267,1.37)),v((.38,.06,.12)),'ink','head');teeth(v,-.307,1.42,.28,5)
for z in [.86,.99,1.12]:cube(v((0,-.216,z)),v((.17,.025,.035)),'oni_red','chest')
finish_biped(2,'RedHornOni','붉은 뿔 요괴',rig,.98,'oni')
# 03: ivory security armor on a dark jointed frame with red memory reactor.
rig,v=new_biped(3,'SecurityConstruct','保安 병기',1.18,'security')
ellipsoid(v((0,0,.99)),v((.34,.24,.30)),'gunmetal','chest',2)
cube(v((0,-.14,1.03)),v((.47,.19,.38)),'armor','chest')
cube(v((0,0,.73)),v((.42,.31,.23)),'gunmetal','pelvis');biped_limbs(rig,v,'security')
ellipsoid(v((0,0,1.48)),v((.29,.24,.29)),'mask','head',2)
cube(v((0,-.232,1.52)),v((.43,.06,.13)),'gunmetal','head');head_eyes(v,.11,-.27,1.52,.044)
for x in [-.12,-.06,0,.06,.12]:cube(v((x,-.24,1.33)),v((.028,.04,.12)),'gunmetal','head')
ellipsoid(v((0,-.253,1.01)),v((.095,.035,.095)),'core_red','chest')
for s in [-1,1]:
    ellipsoid(v((s*.27,0,1.15)),v((.21,.25,.17)),'armor','upper_arm.'+('L' if s<0 else 'R'))
    ellipsoid(v((s*.32,-.12,1.17)),v((.08,.08,.08)),'core_red','upper_arm.'+('L' if s<0 else 'R'))
finish_biped(3,'SecurityConstruct','보안 병기',rig,1.18,'security')
def beast(idx,name,label,scale,kind):
    rig=bone_rig('RIG_MOB_%02d_%s'%(idx,name),quadruped_spec(scale,kind));rigs.append(rig)
    v=lambda p:tuple(x*scale for x in p)
    base='gunmetal' if kind=='iron' else 'flesh' if kind=='fusion' else 'void'
    plate='armor' if kind=='iron' else 'mask' if kind=='fusion' else 'violet_plate'
    ellipsoid(v((0,.33,.9)),v((.35,.45,.32)),base,'pelvis',2)
    ellipsoid(v((0,-.25,.96)),v((.39,.51,.37)),base,'chest',2)
    ellipsoid(v((0,0,.92)),v((.29,.43,.25)),base,'spine',2)
    segment(v((0,-.54,1)),v((0,-.88,1.12)),.24*scale,base,'neck',.18*scale)
    ellipsoid(v((0,-.98,1.14)),v((.28,.31,.25)),plate if kind!='void' else base,'head',2)
    ellipsoid(v((0,-1.22,1.08)),v((.17,.30,.12)),plate,'head')
    ellipsoid(v((0,-1.21,.98)),v((.15,.29,.07)),base,'jaw')
    teeth(v,-1.37,1.03,.26,5);teeth(v,-1.38,.98,.22,4,'jaw',True)
    for s in [-1,1]:
        ellipsoid(v((s*.235,-1.12,1.20)),v((.065,.05,.05)),'core_red','head')
        segment(v((s*.18,-.86,1.3)),v((s*.29,-.71,1.61)),.095*scale,plate,'head',.003)
        for limb,y in [('front',-.5),('hind',.48)]:
            side='L' if s<0 else 'R'
            for part in ['upper','lower']:
                bone=f'{limb}_{part}.{side}';b=rig.data.bones[bone]
                segment(b.head_local,b.tail_local,(.12 if part=='upper' else .075)*scale,base,bone,.06*scale)
                ellipsoid(b.head_local,v((.14,.14,.14)),plate if part=='upper' else 'red_dark',bone)
                center=(b.head_local+b.tail_local)*.5
                if kind=='iron':ellipsoid(center,v((.14,.15,.18)),plate,bone)
            foot=f'{limb}_foot.{side}';p=rig.data.bones[foot].head_local.copy();p.y-=.07*scale;p.z=.09*scale
            ellipsoid(p,v((.13,.20,.09)),base,foot)
            for dx in [-.065,0,.065]:
                a=p+Vector((dx*scale,-.13*scale,0));b=a+Vector((0,-.12*scale,-.025*scale))
                segment(a,b,.025*scale,plate,foot,.002,n=4)
    # Overlapping armor panels, exposed red tissue and dorsal memory spikes.
    for y,bone in [(-.42,'chest'),(-.15,'chest'),(.15,'spine'),(.43,'pelvis')]:
        ellipsoid(v((0,y,1.18)),v((.35,.18,.14)),plate,bone)
        segment(v((0,y,1.27)),v((0,y+.10,1.53 if kind!='void' else 1.70)),.065*scale,plate,bone,.001)
        for s in [-1,1]:cube(v((s*.31,y,1.01)),v((.045,.15,.12)),'tendon' if kind=='fusion' else 'red_dark',bone)
    segment(v((0,.63,.99)),v((0,1.0,.94)),.10*scale,base,'tail.01',.07*scale)
    segment(v((0,1,.94)),v((0,1.4,.82)),.07*scale,plate,'tail.02',.01)
    if kind=='fusion':
        for s in [-1,1]:
            segment(v((s*.16,.35,1.16)),v((s*.53,.64,1.38)),.045*scale,'tendon','pelvis',.024*scale)
            segment(v((s*.53,.64,1.38)),v((s*.62,.88,1.13)),.024*scale,'tendon','tail.01',.003)
        cube(v((0,-.30,1.28)),v((.18,.36,.11)),'core_red','chest')
    if kind=='void':
        ellipsoid(v((0,-1.02,1.36)),v((.105,.09,.07)),'core_red','head')
        for s in [-1,1]:
            segment(v((s*.30,-.1,1.1)),v((s*.62,.1,1.57)),.10*scale,'violet_plate','chest',.003)
            segment(v((s*.25,.30,1.13)),v((s*.53,.55,1.51)),.08*scale,'violet_plate','pelvis',.002)
    mesh=join_mesh('SK_MOB_%02d_%s'%(idx,name),rig)
    # Preserve the tested quadruped gait and adapt attacks by creature anatomy.
    animkind='hound' if kind=='iron' else 'boar' if kind=='fusion' else 'wolf'
    creatures.append(dict(index=idx,name=name,label=label,rig=rig,mesh=mesh,scale=scale,kind=animkind,gait='quadruped'))
beast(4,'IronHound','철문 견마',.92,'iron')
beast(5,'FusionBeast','혼종체',1.05,'fusion')
beast(6,'VoidStalker','공허 추적자',1.00,'void')
exec(recipe[recipe.index('for c in creatures:\n    rig='):recipe.index('# Validate weights')])
# Validate all newly authored weights and FK foot placement.
validation={'unweighted_vertices':0,'bad_weight_sums':0,'foot_checks':[]}
exec(recipe[recipe.index('def contact_test('):recipe.index('contact_test(human,')])
for c in creatures:
    rig=c['rig'];mesh=c['mesh'];s=c['scale'];q=c['gait']=='quadruped'
    for vertex in mesh.data.vertices:
        if not vertex.groups:validation['unweighted_vertices']+=1
        if abs(sum(g.weight for g in vertex.groups)-1)>.001:validation['bad_weight_sums']+=1
    feet=['front_foot.L','front_foot.R','hind_foot.L','hind_foot.R'] if q else ['foot.L','foot.R']
    d=1.15 if c['kind']=='golem' else 1
    contact_test(rig,c['actions'][2],feet,d,(.53 if q else .40)*s,.70,[0,.5,.75,.25] if q else [0,.5])
    rd=.80 if c['kind']=='golem' else .65
    contact_test(rig,c['actions'][4],feet,rd,(1.05 if q else .85)*s,.42,[.46,.56,0,.10] if q else [0,.5])
    reset(rig)
    child=bpy.data.collections.new(mesh.name);collection.children.link(child)
    for o in [rig,mesh]:
        for col in list(o.users_collection):col.objects.unlink(o)
        child.objects.link(o);o.hide_set(False)
    child.hide_viewport=True;child.hide_render=True
manifest['art_revision']=4
manifest['monster_reference']='../../doc/몬스터 컨셉 아트_ 2.5D 오픈월드 RPG.png'
manifest['creatures']=[dict(index=c['index'],id=c['mesh'].name,name=c['label'],gait=c['gait'],file='rigged/'+c['mesh'].name+'.glb',skeleton=c['rig'].name,bones=len(c['rig'].data.bones),triangles=len(c['mesh'].data.polygons),clips=c['clips']) for c in creatures]
with open(os.path.join(ROOT,'data','animation_manifest.json'),'w',encoding='utf-8') as f:json.dump(manifest,f,ensure_ascii=False,indent=2)
validation.update(art_revision=4,creature_count=7,biped_count=4,quadruped_count=3,creature_actions=84,max_ik_reach_error_m=max(i['max_reach_error_m'] for i in clips),game_integration=False)
with open(os.path.join(ROOT,'data','monster_rig_validation.json'),'w') as f:json.dump(validation,f,indent=2)
human=bpy.data.objects[manifest['npc_rig']];human.animation_data.action=bpy.data.actions['NPC_Idle'];scene.frame_set(1)
bpy.context.view_layer.objects.active=human
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
# User requested removing the first monster draft; delete only its now-obsolete active exports.
new_paths={os.path.normcase(os.path.abspath(os.path.join(ROOT,c['file']))) for c in manifest['creatures']}
out_root=os.path.normcase(os.path.abspath(OUT))+os.sep
removed=[]
for relative in old_files:
    path=os.path.abspath(os.path.join(ROOT,relative));normalized=os.path.normcase(path)
    if normalized in new_paths:continue
    if not normalized.startswith(out_root) or not os.path.basename(path).startswith('SK_MOB_'):raise RuntimeError('Unexpected deletion target')
    if os.path.isfile(path):os.remove(path);removed.append(relative)
with open(os.path.join(ROOT,'data','monster_replacement.json'),'w',encoding='utf-8') as f:json.dump(dict(art_revision=4,removed_exports=removed,reference=manifest['monster_reference'],bipeds=4,quadrupeds=3),f,ensure_ascii=False,indent=2)
print('MONSTER_CONCEPT_REPLACEMENT_COMPLETE',validation,flush=True)
