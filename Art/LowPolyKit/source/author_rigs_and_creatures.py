"""Offline Blender modeling, skinning and baked keyframe animation. No game code."""
import bpy, bmesh, math, os, json, random
from mathutils import Vector, Quaternion, Matrix
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
scene=bpy.context.scene;scene.render.fps=30;random.seed(20261020)
OUT=os.path.join(ROOT,'rigged');os.makedirs(OUT,exist_ok=True)
for c in scene.collection.children:c.hide_render=True;c.hide_viewport=True
collection=bpy.data.collections.new('05_RIGGED_CHARACTERS');scene.collection.children.link(collection)
layer=bpy.context.view_layer.layer_collection.children[collection.name];bpy.context.view_layer.active_layer_collection=layer
M={m.name.removeprefix('MAT_'):m for m in bpy.data.materials}
for name,h in {'fur':'#655D55','wolf':'#697782','boar':'#64534B','bear':'#59483B','deer':'#A38C69','bone':'#CCBE96','ghost':'#59516B','joint':'#3D4545','moss':'#586E59'}.items():
    m=bpy.data.materials.new('MAT_'+name);m.use_nodes=True
    rgb=tuple(int(h[i:i+2],16)/255 for i in (1,3,5));m.diffuse_color=(*rgb,1)
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(*rgb,1)
    m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.9;M[name]=m
def new_collection(name):
    c=bpy.data.collections.new(name);scene.collection.children.link(c);return c
def bone_rig(name,spec):
    data=bpy.data.armatures.new(name+'_Skeleton');o=bpy.data.objects.new(name,data);collection.objects.link(o)
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.mode_set(mode='EDIT')
    for n,a,b,parent in spec:
        e=data.edit_bones.new(n);e.head=a;e.tail=b
        if parent:e.parent=data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT');o.show_in_front=True
    for p in o.pose.bones:p.rotation_mode='QUATERNION'
    o['front']='-Y';o['units']='meters';o['animation_fps']=30
    return o
def humanoid_spec():
    s=[('root',(0,0,0),(0,0,.2),None),('pelvis',(0,0,.7),(0,0,.9),'root'),('spine',(0,0,.9),(0,0,1.10),'pelvis'),('chest',(0,0,1.10),(0,0,1.2),'spine'),('neck',(0,0,1.2),(0,0,1.27),'chest'),('head',(0,0,1.27),(0,0,1.75),'neck')]
    for side,x in [('L',-.115),('R',.115)]:
        s += [(f'thigh.{side}',(x,0,.70),(x,0,.435),'pelvis'),(f'shin.{side}',(x,0,.435),(x,0,.17),f'thigh.{side}'),(f'foot.{side}',(x,0,.17),(x,-.17,.10),f'shin.{side}')]
        sign=-1 if side=='L' else 1
        s += [(f'upper_arm.{side}',(sign*.21,0,1.12),(sign*.30,0,.93),'chest'),(f'forearm.{side}',(sign*.30,0,.93),(sign*.39,-.015,.73),f'upper_arm.{side}'),(f'hand.{side}',(sign*.39,-.015,.73),(sign*.40,-.025,.65),f'forearm.{side}')]
    s.append(('bow_string',(-.39,-.02,.73),(-.39,-.02,.83),'hand.L'))
    return s
def reset(rig):
    for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
def global_rotation(rig,name,q):
    p=rig.pose.bones[name];rest=p.bone.matrix_local.to_quaternion()
    if p.parent:
        relative=p.parent.bone.matrix_local.to_quaternion().inverted() @ rest
        p.rotation_quaternion=relative.inverted() @ p.parent.matrix.to_quaternion().inverted() @ q
    else:p.rotation_quaternion=rest.inverted() @ q
def rotate(rig,name,x=0,y=0,z=0):
    p=rig.pose.bones[name]
    q=Quaternion((0,0,1),z) @ Quaternion((0,1,0),y) @ Quaternion((1,0,0),x)
    rest=p.bone.matrix_local.to_quaternion()
    p.rotation_quaternion=rest.inverted() @ q @ rest
def translate_root(rig,delta):
    rig.pose.bones['root'].location=rig.data.bones['root'].matrix_local.to_quaternion().inverted() @ Vector(delta)
def aim(rig,name,vec):
    b=rig.data.bones[name];d=b.tail_local-b.head_local
    global_rotation(rig,name,d.rotation_difference(Vector(vec).normalized()) @ b.matrix_local.to_quaternion())
def ik(rig,upper,lower,end,target,bend):
    p=rig.pose.bones[upper];origin=p.head.copy();target=Vector(target)
    l1=rig.data.bones[upper].length;l2=rig.data.bones[lower].length
    diff=target-origin;dist=diff.length
    d=diff.normalized();limited=min(max(dist,.001),l1+l2-.0001)
    a=(l1*l1-l2*l2+limited*limited)/(2*limited)
    pole=Vector(bend);pole=(pole-d*pole.dot(d)).normalized()
    knee=origin+d*a+pole*math.sqrt(max(0,l1*l1-a*a))
    aim(rig,upper,knee-origin);bpy.context.view_layer.update()
    aim(rig,lower,target-knee);bpy.context.view_layer.update()
    return max(0,dist-(l1+l2))
def joint_point(rig,name):return rig.data.bones[name].head_local.copy()
def action_curves(a):
    return [f for layer in a.layers for strip in layer.strips for bag in strip.channelbags for f in bag.fcurves]
def key(rig,frame):
    for p in rig.pose.bones:
        p.keyframe_insert('rotation_quaternion',frame=frame,group=p.name)
        if p.name in ['root','bow_string']:p.keyframe_insert('location',frame=frame,group=p.name)
def action(rig,name,duration,pose,loop=False,speed=0,rootmotion=False,events=None):
    a=bpy.data.actions.new(name);rig.animation_data_create();rig.animation_data.action=a
    frames=round(duration*30);stats=[]
    for f in range(frames+1):
        reset(rig);scene.frame_set(f+1);err=pose(f/frames,f/30);stats.append(err or 0);key(rig,f+1)
    for curve in action_curves(a):
        for k in curve.keyframe_points:k.interpolation='LINEAR'
    a.use_fake_user=True;a['loop']=loop;a['speed_mps']=speed;a['root_motion']=rootmotion
    meta=dict(name=name,frames=[1,frames+1],duration_s=frames/30,loop=loop,speed_mps=speed,root_motion=rootmotion,events=events or [],max_reach_error_m=max(stats))
    clips.append(meta);return a
def nla(rig,actions):
    rig.animation_data.action=None
    for a in actions:
        t=rig.animation_data.nla_tracks.new();t.name=a.name
        strip=t.strips.new(a.name,1,a);strip.extrapolation='NOTHING';t.mute=True
def export(rig,meshes,file,actions):
    reset(rig);scene.frame_set(1);rig.animation_data.action=None
    bpy.ops.object.select_all(action='DESELECT');rig.hide_set(False);rig.select_set(True)
    for o in meshes:o.hide_set(False);o.select_set(True)
    bpy.context.view_layer.objects.active=rig
    # NLA tracks are isolated into distinct named glTF clips.
    for t in rig.animation_data.nla_tracks:t.mute=False
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,file),export_format='GLB',use_selection=True,
        export_animations=True,export_animation_mode='NLA_TRACKS',export_skins=True,
        export_force_sampling=True,export_frame_step=1,export_optimize_animation_size=False)
    for t in rig.animation_data.nla_tracks:t.mute=True
    print('EXPORTED',file,len(actions),flush=True)
def skin(o,rig,weight_fn):
    o.vertex_groups.clear()
    groups={n:o.vertex_groups.new(name=n) for n in rig.data.bones.keys()}
    for v in o.data.vertices:
        weights=weight_fn(v.co)
        for n,w in weights.items():
            if w>0:groups[n].add([v.index],w,'REPLACE')
    mod=o.modifiers.new('Skin','ARMATURE');mod.object=rig;o.parent=rig
    return o
def humanoid_weights(slot,index):
    def weight(v):
        x,y,z=v;side='L' if x<0 else 'R'
        if slot=='hair':return {'head':1}
        if slot=='prop':return {'hand.R':1}
        if slot=='body':
            if z<.85:return {f'hand.{side}':1}
            if z>1.25:return {'head':1}
            return {'neck':1}
        if slot=='accessory':
            return {('neck' if index==0 else 'pelvis' if index==1 else 'chest'):1}
        if slot=='feet':return {f'foot.{side}':1}
        if slot=='legs':
            if z>.66:return {'pelvis':1}
            t=max(0,min(1,(z-.36)/.14))
            return {f'thigh.{side}':t,f'shin.{side}':1-t}
        if abs(x)>.235 and z<1.18:
            t=max(0,min(1,(z-.86)/.15))
            return {f'upper_arm.{side}':t,f'forearm.{side}':1-t}
        t=max(0,min(1,(z-.85)/.25));return {'pelvis':1-t,'chest':t}
    return weight
def smooth(t):t=max(0,min(1,t));return t*t*(3-2*t)
def envelope(t,a,b,c):
    if t<a:return 0
    if t<b:return smooth((t-a)/(b-a))
    if t<c:return 1-smooth((t-b)/(c-b))
    return 0
def stride(p,duration,speed,stance,lift):
    p=p%1;span=duration*speed*stance
    if p<stance:return -span/2+duration*speed*p,0,True
    q=(p-stance)/(1-stance);h00=2*q**3-3*q*q+1;h10=q**3-2*q*q+q;h01=-2*q**3+3*q*q;h11=q**3-q*q
    # Match stance horizontal speed at lift-off/touchdown, zero vertical speed.
    tangent=speed*duration*(1-stance)
    return h00*span/2+h10*tangent-h01*span/2+h11*tangent,lift*math.sin(math.pi*q)**2,False
def point_rotate(p,yaw):return Quaternion((0,0,1),yaw) @ Vector(p)
clips=[];rigs=[];validation={'unweighted_vertices':0,'bad_weight_sums':0,'foot_checks':[]}
human=bone_rig('RIG_NPC_Humanoid_v1',humanoid_spec());rigs.append(human)
with open(os.path.join(ROOT,'data','mesh_catalog.json'),encoding='utf-8') as f:base=json.load(f)
human_parts={};human_meshes=[]
for item in base['meshes']:
    if item['slot'] not in ['body','hair','top','legs','feet','accessory','prop']:continue
    src=bpy.data.objects[item['mesh']];o=bpy.data.objects.new(item['mesh'].replace('SM_','SK_'),src.data.copy());collection.objects.link(o)
    skin(o,human,humanoid_weights(item['slot'],item['index']));o['slot']=item['slot'];o['part_index']=item['index']
    if item['slot']=='prop' and item['index']==0:
        for v in o.data.vertices:v.co.x-=.84;v.co.z-=.17
        bm=bmesh.new();bm.from_mesh(o.data)
        faces=[p for p in bm.faces if o.data.materials[p.material_index].name=='MAT_paper']
        bmesh.ops.delete(bm,geom=faces,context='FACES');bm.to_mesh(o.data);bm.free()
        o.vertex_groups.clear();o.vertex_groups.new(name='hand.L').add(list(range(len(o.data.vertices))),1,'REPLACE')
    human_parts[(item['slot'],item['index'])]=o;human_meshes.append(o)
# New rigid skinned weapon meshes use the hand bones and preserve existing prop IDs 0..2.
chunks=[]
def geom(o,mat,bone):
    o.data.materials.append(M[mat]);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    # Apply world transform to vertices before joining into origin-based skinned mesh.
    o.data.transform(o.matrix_world);o.matrix_world=Matrix.Identity(4)
    g=o.vertex_groups.new(name=bone);g.add(list(range(len(o.data.vertices))),1,'REPLACE');chunks.append(o);return o
def cube(p,s,mat,bone):
    bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.scale=s;return geom(o,mat,bone)
def ellipsoid(p,s,mat,bone,sub=1):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=p);o=bpy.context.object;o.scale=s;return geom(o,mat,bone)
def segment(a,b,r,mat,bone,r2=None,n=6):
    a,b=Vector(a),Vector(b);bpy.ops.mesh.primitive_cone_add(vertices=n,radius1=r,radius2=r if r2 is None else r2,depth=(b-a).length,location=(a+b)*.5)
    o=bpy.context.object;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return geom(o,mat,bone)
def join_mesh(name,rig):
    bpy.ops.object.select_all(action='DESELECT')
    for o in chunks:o.select_set(True)
    bpy.context.view_layer.objects.active=chunks[0];bpy.ops.object.join();o=bpy.context.object;o.name=name;o.data.name=name+'_Mesh'
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    for p in o.data.polygons:p.use_smooth=False
    m=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=m.name)
    m=o.modifiers.new('Skin','ARMATURE');m.object=rig;o.parent=rig;chunks.clear();return o
bow=human_parts[('prop',0)]
for a,b in [((-.39,-.02,.28),(-.39,-.02,.73)),((-.39,-.02,.73),(-.39,-.02,1.18))]:
    o=segment(a,b,.004,'paper','hand.L',n=4)
    center=o.vertex_groups.new(name='bow_string')
    for v in o.data.vertices:
        if abs(v.co.z-.73)<.04:
            o.vertex_groups['hand.L'].remove([v.index]);center.add([v.index],1,'REPLACE')
for mod in list(bow.modifiers):bow.modifiers.remove(mod)
chunks.insert(0,bow)
bow=join_mesh(bow.name,human);human_parts[('prop',0)]=bow
for idx,name in [(3,'Sword'),(4,'DualPistols'),(5,'Claws'),(6,'Scissors')]:
    if idx==3:
        segment((.39,-.015,.72),(.39,-.015,1.48),.045,'steel','hand.R',.005,n=4)
        cube((.39,-.015,.79),(.24,.06,.05),'edge','hand.R');segment((.39,-.015,.62),(.39,-.015,.78),.028,'wood','hand.R')
    elif idx==4:
        for s,side in [(-1,'L'),(1,'R')]:
            cube((s*.39,-.13,.75),(.075,.27,.075),'steel','hand.'+side)
            cube((s*.39,-.015,.69),(.075,.08,.15),'ink','hand.'+side)
    elif idx==5:
        for s,side in [(-1,'L'),(1,'R')]:
            for dx in [-.045,0,.045]:segment((s*.39+dx,-.05,.72),(s*.39+dx,-.29,.74),.017,'bone','hand.'+side,.002,n=4)
    else:
        for s,side in [(-1,'L'),(1,'R')]:
            segment((s*.39,-.015,.72),(s*.53,-.015,1.4),.055,'steel','hand.'+side,.005,n=4)
            ellipsoid((s*.39,-.015,.69),(.075,.035,.075),'red','hand.'+side)
    o=join_mesh('SK_NPC_Prop_%02d_%s'%(idx,name),human);human_parts[('prop',idx)]=o;human_meshes.append(o)
def human_pose(kind,duration=1,speed=0,rootmotion=False,turn=0):
    def pose(t,seconds):
        err=0;bob=0;delta=Vector((0,0,0));yaw=turn*smooth(t);attack='Attack_' in kind
        if kind in ['Walk','Run']:
            bob=-.055 if kind=='Walk' else -.12
            bob+= (.012 if kind=='Walk' else .025)*math.cos(4*math.pi*t)
            if rootmotion:delta.y=-speed*seconds
        if kind=='Idle':bob=.004*math.sin(2*math.pi*t)
        if turn:bob=-.09*math.sin(math.pi*t)
        if kind=='Jump':
            crouch=envelope(t,0,.14,.3);air=max(0,math.sin(math.pi*max(0,min(1,(t-.22)/.58))))
            bob=-.11*crouch+.55*air-.10*envelope(t,.78,.87,1)
        if kind=='Hit':bob=-.04*envelope(t,0,.25,1)
        if kind=='Death':
            delta.y=.1*smooth(t);bob=-.15*smooth(t)
        delta.z=bob;translate_root(human,delta);rotate(human,'root',z=yaw)
        rotate(human,'spine',x=.06*math.sin(2*math.pi*t) if kind=='Idle' else .10 if kind=='Run' else 0)
        if kind=='Hit':rotate(human,'chest',x=-.28*envelope(t,0,.25,1))
        if kind=='Death':rotate(human,'root',x=-1.45*smooth(t));return 0
        wind=envelope(t,0,.27,.5);hit=envelope(t,.25,.46,.78)
        if attack:rotate(human,'chest',z=.32*wind-.48*hit,x=.12*hit)
        bpy.context.view_layer.update()
        for s,side in [(-1,'L'),(1,'R')]:
            foot=joint_point(human,'foot.'+side);toe=0
            if kind in ['Walk','Run']:
                yy,zz,planted=stride(t+(0 if side=='L' else .5),duration,speed,.64 if kind=='Walk' else .42,.09 if kind=='Walk' else .18)
                foot.y+=yy;foot.z+=zz;foot+=delta-Vector((0,0,bob))
                toe=.25*math.sin(math.pi*((t+(0 if side=='L' else .5))%1)) if not planted else 0
            elif turn:
                # Stagger the two foot placements while torso rotates continuously.
                phase=smooth((t-(.05 if side=='L' else .43))/.45)
                foot=point_rotate(foot,turn*phase);foot.z+=.055*math.sin(math.pi*phase)**2
            elif kind=='Jump':foot.z+=max(0,bob)+.12*envelope(t,.25,.48,.75)
            err=max(err,ik(human,'thigh.'+side,'shin.'+side,'foot.'+side,foot,(0,-1,0)))
            global_rotation(human,'foot.'+side,Quaternion((0,0,1),yaw) @ Quaternion((1,0,0),toe) @ human.data.bones['foot.'+side].matrix_local.to_quaternion())
            hand=joint_point(human,'hand.'+side)+delta
            if kind in ['Walk','Run']:
                hand.y+=(.08 if kind=='Walk' else .17)*math.sin(2*math.pi*t+(0 if side=='L' else math.pi));hand.z+=.04 if kind=='Walk' else .15
            elif kind=='Jump':hand.z+=.20*envelope(t,.1,.4,.9);hand.y-=.10
            elif kind=='Attack_Sword':
                if side=='R':hand=Vector((.22+.15*wind,-.08-.24*hit,1.10+.15*wind-.26*hit))
                else:hand+=Vector((-.05,-.1,.08))*hit
            elif kind=='Attack_SwordHeavy':hand=Vector((s*.12,-.10-.16*hit,1.32-.36*hit))
            elif kind=='Attack_Spear':hand=Vector((s*.14,-.10-.22*hit,1.00-.06*hit))
            elif kind=='Attack_Bow':
                draw=smooth(t/.58)*(1-smooth((t-.68)/.12));hand=Vector((.09 if side=='R' else -.18,-.30+.29*draw if side=='R' else -.32,1.08))
            elif kind=='Attack_DualPistol':hand=Vector((s*.17,-.31+.025*math.sin(t*math.pi*8),1.04))
            elif kind in ['Attack_Claw','Attack_Scissors','Attack_Unarmed']:
                pulse=hit if side=='R' else envelope(t,.42,.64,.92)
                hand=Vector((s*(.33-.22*pulse),-.04-.32*pulse,.94+.10*wind))
            if turn:hand=point_rotate(hand-delta,yaw)+delta
            err=max(err,ik(human,'upper_arm.'+side,'forearm.'+side,'hand.'+side,hand,(0,-1,0)))
            # Weapon bones follow forearm; explicit aim for shooting and thrusting.
            if kind in ['Attack_Bow','Attack_DualPistol','Attack_Spear']:
                restq=human.data.bones['hand.'+side].matrix_local.to_quaternion()
                q=Quaternion((1,0,0),math.pi/2 if kind=='Attack_Spear' else 0)
                global_rotation(human,'hand.'+side,q @ restq)
            if kind in ['Attack_Sword','Attack_SwordHeavy','Attack_Scissors','Attack_Claw']:
                wrist=Quaternion((1,0,0),.20*wind+1.30*hit) @ Quaternion((0,1,0),s*(.35*wind-.40*hit))
                global_rotation(human,'hand.'+side,wrist @ human.data.bones['hand.'+side].matrix_local.to_quaternion())
        if kind=='Attack_Bow':
            draw=smooth(t/.58)*(1-smooth((t-.68)/.12))
            human.pose.bones['bow_string'].location=human.data.bones['bow_string'].matrix_local.to_quaternion().inverted() @ (Vector((.27,.315,0))*draw)
        bpy.context.view_layer.update();return err
    return pose
human_actions=[]
for name,duration,speed in [('Idle',2,0),('Walk',1,.70),('Run',.6,1.7)]:
    human_actions.append(action(human,'NPC_'+name,duration,human_pose(name,duration,speed),True,speed))
    if speed:human_actions.append(action(human,'NPC_'+name+'_RootMotion',duration,human_pose(name,duration,speed,True),False,speed,True))
for angle in [45,90]:
    for side,sign in [('L',1),('R',-1)]:
        human_actions.append(action(human,f'NPC_Turn_{side}{angle}',.8,human_pose('Turn',turn=math.radians(angle)*sign),False,events=[dict(time_s=.8,event='yaw_complete',degrees=angle*sign)]))
for name,duration in [('Jump',1.2),('Attack_Sword',.9),('Attack_SwordHeavy',1.3),('Attack_Spear',1),('Attack_Bow',1.4),('Attack_DualPistol',.8),('Attack_Claw',1),('Attack_Scissors',1.2),('Attack_Unarmed',.9),('Hit',.5),('Death',1.3)]:
    events=[dict(time_s=round(duration*.46,3),event='impact')] if name.startswith('Attack') else []
    if name=='Attack_Bow':events=[dict(time_s=round(duration*.68,3),event='arrow_release')]
    if name=='Attack_DualPistol':events=[dict(time_s=round(duration*t,3),event='fire') for t in [.0625,.3125,.5625,.8125]]
    if name in ['Attack_Claw','Attack_Scissors','Attack_Unarmed']:events=[dict(time_s=round(duration*t,3),event='impact') for t in [.46,.64]]
    if name=='Jump':events=[dict(time_s=round(duration*.22,3),event='takeoff'),dict(time_s=round(duration*.80,3),event='land')]
    human_actions.append(action(human,'NPC_'+name,duration,human_pose(name),False,events=events))
nla(human,human_actions)
# Retain modular parts in source; export assembled NPC presets and independent parts.
for o in human_meshes:o.hide_render=True
with open(os.path.join(ROOT,'data','npc_appearance_presets.json'),encoding='utf-8') as f:presets=json.load(f)
npc_exports=[]
for preset in presets['npcs']:
    chosen=[human_parts[(slot,idx)] for slot,idx in preset['parts'].items() if idx>=0]
    file=preset['appearance_id']+'.glb';export(human,chosen,file,human_actions)
    npc_exports.append(dict(id=preset['appearance_id'],name=preset['display_name'],file='rigged/'+file,parts=preset['parts']))
# A complete gear library allows other tasks to swap any skinned part without duplicating rigs.
export(human,human_meshes,'NPC_ModularParts_Library.glb',human_actions)
for o in human_meshes:o.hide_set(True)
human.hide_set(True)
creatures=[]
def quadruped_spec(scale=1,kind='wolf'):
    s=scale;v=lambda p:tuple(s*x for x in p)
    spec=[('root',v((0,0,0)),v((0,0,.2)),None),('pelvis',v((0,.43,.88)),v((0,.20,.90)),'root'),('spine',v((0,.20,.90)),v((0,-.25,.96)),'pelvis'),('chest',v((0,-.25,.96)),v((0,-.58,1.02)),'spine'),('neck',v((0,-.58,1.02)),v((0,-.88,1.12)),'chest'),('head',v((0,-.88,1.12)),v((0,-1.20,1.10)),'neck'),('jaw',v((0,-.96,1.02)),v((0,-1.24,1.0)),'head'),('tail.01',v((0,.62,.98)),v((0,1.0,.92)),'pelvis'),('tail.02',v((0,1,.92)),v((0,1.30,.8)),'tail.01')]
    for limb,y,z in [('front',-.50,.94),('hind',.48,.88)]:
        for side,x in [('L',-.27),('R',.27)]:
            knee_y=y+(.08 if limb=='front' else -.16)
            spec += [(f'{limb}_upper.{side}',v((x,y,z)),v((x,knee_y,.51)),'chest' if limb=='front' else 'pelvis'),(f'{limb}_lower.{side}',v((x,knee_y,.51)),v((x,y,.12)),f'{limb}_upper.{side}'),(f'{limb}_foot.{side}',v((x,y,.12)),v((x,y-.14,.09)),f'{limb}_lower.{side}')]
    return spec
def build_quad(idx,name,label,scale,kind):
    rig=bone_rig('RIG_MOB_%02d_%s'%(idx,name),quadruped_spec(scale,kind));rigs.append(rig)
    v=lambda p:tuple(x*scale for x in p);sz=lambda p:tuple(x*scale for x in p)
    mat={'hound':'ghost','wolf':'wolf','boar':'boar','bear':'bear','deer':'deer'}[kind]
    fat={'hound':.85,'wolf':1,'boar':1.35,'bear':1.6,'deer':.82}[kind]
    ellipsoid(v((0,.32,.88)),sz((.35*fat,.46,.33*fat)),mat,'pelvis',2)
    ellipsoid(v((0,-.23,.96)),sz((.37*fat,.53,.37*fat)),mat,'chest',2)
    ellipsoid(v((0,0,.91)),sz((.29*fat,.45,.27*fat)),mat,'spine',2)
    segment(v((0,-.52,.96)),v((0,-.87,1.12)),.25*scale,mat,'neck',.21*scale)
    ellipsoid(v((0,-.98,1.14)),sz((.24*fat,.30,.25)),mat,'head',2)
    snout=.35 if kind in ['wolf','hound'] else .28
    ellipsoid(v((0,-1.18,1.08)),sz((.14*fat,snout,.12)),mat,'head')
    ellipsoid(v((0,-1.22,1.0)),sz((.13*fat,snout*.85,.055)),mat,'jaw')
    ellipsoid(v((0,-1.18-snout*.80,1.09)),sz((.09,.055,.065)),'ink','head')
    for side,sgn in [('L',-1),('R',1)]:
        x=sgn*.18*fat
        ellipsoid(v((x,-1.11,1.22)),sz((.035,.048,.035)),'memory' if kind=='hound' else 'eye','head')
        if kind in ['wolf','hound','deer']:
            segment(v((sgn*.16,-.88,1.30)),v((sgn*.23,-.85,1.66 if kind=='deer' else 1.57)),.11*scale,mat,'head',.008*scale)
        else:ellipsoid(v((sgn*.20,-.91,1.35)),sz((.10,.07,.10)),mat,'head')
        if kind=='boar':segment(v((sgn*.16,-1.25,.99)),v((sgn*.25,-1.40,1.22)),.04*scale,'bone','jaw',.003*scale)
        if kind=='deer':
            a=v((sgn*.10,-.88,1.31));b=v((sgn*.34,-.65,2.0));segment(a,b,.035*scale,'bone','head',.012*scale)
            for z in [1.58,1.78]:segment(v((sgn*.24,-.72,z)),v((sgn*.48,-.82,z+.20)),.022*scale,'bone','head',.004*scale)
    for n,a,b,parent in quadruped_spec(scale,kind):
        if '_upper.' in n or '_lower.' in n:
            upper='_upper.' in n;r=(.12 if upper else .075)*scale*(1.3 if kind=='bear' else 1)
            segment(a,b,r,mat,n,r*.72)
            ellipsoid(a,(r*1.1,)*3,mat,n)
        if '_foot.' in n:
            p=Vector(a);p.y-=.055*scale;p.z=.10*scale
            ellipsoid(p,sz((.11 if kind!='deer' else .075,.17,.10)), 'ink' if kind in ['deer','boar'] else mat,n)
    if kind in ['wolf','hound']:
        segment(v((0,.62,.98)),v((0,1,.92)),.10*scale,mat,'tail.01',.10*scale)
        segment(v((0,1,.92)),v((0,1.3,.8)),.10*scale,mat,'tail.02',.025*scale)
    else:
        segment(v((0,.62,.98)),v((0,.91,.91)),.055*scale,mat,'tail.01',.02*scale)
    if kind=='hound':
        for y in [-.4,-.1,.2,.5]:
            segment(v((0,y,1.18)),v((0,y+.08,1.43)),.05*scale,'bone','chest' if y<0 else 'pelvis',.002*scale)
    mesh=join_mesh('SK_MOB_%02d_%s'%(idx,name),rig)
    return dict(index=idx,name=name,label=label,rig=rig,mesh=mesh,scale=scale,kind=kind,gait='quadruped')
def build_biped(idx,name,label,golem=False):
    # Same biped topology with proportion-specific rest skeleton, enabling separate timing.
    spec=[b for b in humanoid_spec() if b[0]!='bow_string'];scale=1.30 if golem else 1.08
    spec=[(n,tuple(x*scale for x in a),tuple(x*scale for x in b),p) for n,a,b,p in spec]
    rig=bone_rig('RIG_MOB_%02d_%s'%(idx,name),spec);rigs.append(rig)
    v=lambda p:tuple(x*scale for x in p);mat='stone' if golem else 'ghost'
    ellipsoid(v((0,0,.74)),v((.27,.19,.20)),mat,'pelvis')
    ellipsoid(v((0,0,1.02)),v((.42 if golem else .27,.25,.30)),mat,'chest',2)
    ellipsoid(v((0,0,.90)),v((.23,.19,.23)),mat,'spine')
    ellipsoid(v((0,0,1.46)),v((.27,.23,.28)),mat,'head',2)
    for s,side in [(-1,'L'),(1,'R')]:
        ellipsoid(v((s*.10,-.22,1.50)),v((.055,.025,.035)),'memory','head')
        if not golem:segment(v((s*.18,0,1.65)),v((s*.30,.02,2.06)),.09*scale,'bone','head',.005*scale)
        for n in ['upper_arm','forearm','thigh','shin']:
            bn=rig.data.bones[n+'.'+side];r=.12 if n in ['upper_arm','thigh'] else .10
            segment(bn.head_local,bn.tail_local,r*scale,mat,n+'.'+side,r*.8*scale)
            ellipsoid(bn.head_local,(r*scale,)*3,mat,n+'.'+side)
        ellipsoid(v((s*.39,-.015,.73)),v((.16 if golem else .085,.13,.14)),mat,'hand.'+side)
        ellipsoid(v((s*.115,-.05,.10)),v((.14,.23,.10)),mat,'foot.'+side)
        if not golem:
            for dx in [-.05,0,.05]:segment(v((s*.39+dx,-.08,.72)),v((s*.39+dx,-.29,.70)),.018*scale,'bone','hand.'+side,.001*scale,n=4)
        else:
            ellipsoid(v((s*.32,0,1.16)),v((.20,.23,.16)),'moss','upper_arm.'+side)
    mesh=join_mesh('SK_MOB_%02d_%s'%(idx,name),rig)
    return dict(index=idx,name=name,label=label,rig=rig,mesh=mesh,scale=scale,kind='golem' if golem else 'wraith',gait='biped')
creatures.append(build_biped(0,'HornWraith','뿔 잔향귀'))
creatures.append(build_quad(1,'CorpseHound','시체 사냥개',1,'hound'))
creatures.append(build_biped(2,'RubbleGolem','폐허 석괴',True))
creatures.append(build_quad(3,'Wolf','회색 늑대',.95,'wolf'))
creatures.append(build_quad(4,'Boar','야생 멧돼지',.85,'boar'))
creatures.append(build_quad(5,'Bear','불곰',1.30,'bear'))
creatures.append(build_quad(6,'Deer','산사슴',1.05,'deer'))
def creature_pose(c,kind,duration=1,speed=0,rm=False,turn=0):
    rig=c['rig'];sc=c['scale'];quad=c['gait']=='quadruped'
    def pose(t,sec):
        err=0;moving=kind in ['Walk','Run'];run=kind=='Run';yaw=turn*smooth(t);bob=0;delta=Vector((0,0,0))
        if moving:
            bob=(-.075 if not run else -.15)*sc + (.012 if not run else .035)*sc*math.cos(4*math.pi*t)
            if rm:delta.y=-speed*sec
        if kind=='Idle':bob=.008*sc*math.sin(2*math.pi*t)
        if turn:bob=-.15*sc*math.sin(math.pi*t)
        if kind=='Hit':bob=-.05*sc*envelope(t,0,.2,1)
        if kind=='Jump':bob=sc*(.55*max(0,math.sin(math.pi*max(0,min(1,(t-.2)/.6))))-.10*envelope(t,0,.12,.3)-.08*envelope(t,.79,.89,1))
        strike=envelope(t,.20,.46,.8);prep=envelope(t,0,.24,.45)
        if kind.startswith('Attack'):
            bob-=.06*sc*prep
            if kind=='AttackPrimary':delta.y=-.16*sc*strike
        delta.z=bob;translate_root(rig,delta);rotate(rig,'root',z=yaw)
        if kind=='Death':
            translate_root(rig,(0,0,-.4*sc*smooth(t)));rotate(rig,'root',y=1.45*smooth(t));return 0
        if quad:
            rotate(rig,'spine',x=(.07*math.sin(2*math.pi*t) if run else .018*math.sin(2*math.pi*t)))
            rotate(rig,'neck',x=(-.3*strike if kind=='AttackPrimary' else .22*prep if kind=='AttackSecondary' else -.03*math.sin(2*math.pi*t)))
            if c['kind']=='boar' and kind.startswith('Attack'):
                rotate(rig,'neck',x=.4*prep-.38*strike);rotate(rig,'head',x=.18*prep-.22*strike)
            if c['kind']=='deer' and kind=='AttackPrimary':
                rotate(rig,'neck',x=.50*prep-.25*strike);rotate(rig,'head',x=.22*prep)
            if c['kind'] in ['wolf','hound'] and kind=='AttackSecondary':rotate(rig,'head',z=.22*prep-.28*strike)
            if kind.startswith('Attack'):rotate(rig,'jaw',x=.45*prep-.16*strike)
            rotate(rig,'tail.01',z=.12*math.sin(2*math.pi*t),x=.06*math.cos(2*math.pi*t))
            rotate(rig,'tail.02',z=.15*math.sin(2*math.pi*t+.7))
        else:
            rotate(rig,'chest',x=.10*strike,z=.22*prep-.32*strike)
        if kind=='Hit':rotate(rig,'chest',x=-.2*envelope(t,0,.2,1))
        bpy.context.view_layer.update()
        limbs=['front','hind'] if quad else ['']
        for limb in limbs:
            for sign,side in [(-1,'L'),(1,'R')]:
                upper=f'{limb}_upper.{side}' if quad else 'thigh.'+side
                lower=f'{limb}_lower.{side}' if quad else 'shin.'+side
                footname=f'{limb}_foot.{side}' if quad else 'foot.'+side
                foot=joint_point(rig,footname)
                if moving:
                    if quad:
                        phases={('front','L'):0,('front','R'):.5,('hind','L'):.75,('hind','R'):.25} if not run else {('hind','L'):0,('hind','R'):.10,('front','L'):.46,('front','R'):.56}
                        phase=phases[(limb,side)]
                    else:phase=0 if side=='L' else .5
                    yy,zz,grounded=stride(t+phase,duration,speed,.70 if not run else .42,.11*sc if not run else .22*sc)
                    foot.y+=yy+delta.y;foot.z+=zz
                elif turn:
                    offset=(0 if side=='L' else .25)+(0 if limb!='hind' else .08)
                    p=smooth((t-offset)/.67);foot=point_rotate(foot,turn*p);foot.z+=.07*sc*math.sin(math.pi*p)**2
                elif kind=='Jump':foot.z+=max(0,bob)+.08*sc*envelope(t,.25,.5,.76)
                elif kind.startswith('Attack'):foot.y+=delta.y*.35
                if c['kind']=='bear' and limb=='front' and kind.startswith('Attack'):
                    if kind=='AttackSecondary' or side=='L':
                        foot.z+=.44*sc*prep;foot.y-=.16*sc*strike
                        if kind=='AttackPrimary':foot.x+=.14*sc*prep-.10*sc*strike
                if c['kind']=='deer' and limb=='hind' and kind=='AttackSecondary':
                    foot.y+=.32*sc*strike;foot.z+=.36*sc*strike
                bend=(0,1,0) if quad and limb=='front' else (0,-1,0)
                err=max(err,ik(rig,upper,lower,footname,foot,bend))
                global_rotation(rig,footname,Quaternion((0,0,1),yaw) @ rig.data.bones[footname].matrix_local.to_quaternion())
        if not quad:
            for sign,side in [(-1,'L'),(1,'R')]:
                target=joint_point(rig,'hand.'+side)+delta
                if moving:
                    target.y+=.10*sc*math.sin(2*math.pi*t+(0 if side=='L' else math.pi));target.z+=.06*sc
                if kind=='Jump':target.z+=.15*sc
                if kind=='AttackPrimary':target=Vector((sign*.19,-.23-.1*strike,1.06+.15*prep-.26*strike))*sc
                if kind=='AttackSecondary':target=Vector((sign*.18,-.10-.22*strike,1.34-.38*strike))*sc
                if turn:target=point_rotate(target-delta,yaw)+delta
                err=max(err,ik(rig,'upper_arm.'+side,'forearm.'+side,'hand.'+side,target,(0,-1,0)))
        bpy.context.view_layer.update();return err
    return pose
for c in creatures:
    rig=c['rig'];s=c['scale'];prefix='MOB_%02d_'%c['index'];actions=[];start=len(clips)
    walk_d=1.15 if c['kind']=='golem' else 1.0;run_d=.80 if c['kind'] in ['golem','bear'] else .65
    walk_speed=(.40 if c['gait']=='biped' else .53)*s;run_speed=(.85 if c['gait']=='biped' else 1.05)*s
    for k,d,speed in [('Idle',2,0),('Walk',walk_d,walk_speed),('Run',run_d,run_speed)]:
        actions.append(action(rig,prefix+k,d,creature_pose(c,k,d,speed),True,speed))
        if speed:actions.append(action(rig,prefix+k+'_RootMotion',d,creature_pose(c,k,d,speed,True),False,speed,True))
    for side,sgn in [('L',1),('R',-1)]:actions.append(action(rig,prefix+'Turn_'+side+'90',1.1,creature_pose(c,'Turn',turn=sgn*math.pi/2)))
    for k,d in [('Jump',1.2),('AttackPrimary',1.0 if c['kind']!='golem' else 1.4),('AttackSecondary',1.3),('Hit',.5),('Death',1.3)]:
        events=[dict(time_s=round(d*.46,3),event='impact')] if k.startswith('Attack') else []
        actions.append(action(rig,prefix+k,d,creature_pose(c,k),events=events))
    c['actions']=actions;c['clips']=clips[start:];nla(rig,actions)
    export(rig,[c['mesh']],c['mesh'].name+'.glb',actions)
    c['mesh'].hide_set(True);rig.hide_set(True)
# Validate weights and contact trajectories from baked FK, not only IK targets.
for rig,meshes in [(human,human_meshes)]+[(c['rig'],[c['mesh']]) for c in creatures]:
    for o in meshes:
        for v in o.data.vertices:
            weights=[g.weight for g in v.groups]
            if not weights:validation['unweighted_vertices']+=1
            if abs(sum(weights)-1)>.001:validation['bad_weight_sums']+=1
    reset(rig)
def contact_test(rig,act,feet,duration,speed,stance,phases):
    rig.animation_data.action=act;errors=[]
    for f in range(round(duration*30)):
        t=f/round(duration*30);scene.frame_set(f+1);bpy.context.view_layer.update()
        for foot,phase in zip(feet,phases):
            p=(t+phase)%1
            if .05<p<stance-.05:
                expected=joint_point(rig,foot);yy,_,_=stride(p,duration,speed,stance,.1)
                expected.y+=yy-speed*f/30
                errors.append((rig.pose.bones[foot].head-expected).length)
    validation['foot_checks'].append(dict(rig=rig.name,action=act.name,max_ankle_target_error_m=max(errors or [0])))
    rig.animation_data.action=None
contact_test(human,human_actions[2],['foot.L','foot.R'],1,.70,.64,[0,.5])
contact_test(human,human_actions[4],['foot.L','foot.R'],.6,1.7,.42,[0,.5])
for c in creatures:
    q=c['gait']=='quadruped';s=c['scale'];d=1.15 if c['kind']=='golem' else 1
    feet=['front_foot.L','front_foot.R','hind_foot.L','hind_foot.R'] if q else ['foot.L','foot.R']
    contact_test(c['rig'],c['actions'][2],feet,d,(.53 if q else .40)*s,.70,[0,.5,.75,.25] if q else [0,.5])
    rd=.80 if c['kind'] in ['golem','bear'] else .65
    contact_test(c['rig'],c['actions'][4],feet,rd,(1.05 if q else .85)*s,.42,[.46,.56,0,.10] if q else [0,.5])
for rig in rigs:reset(rig);rig.hide_set(True)
scene.frame_set(1)
manifest=dict(schema_version=1,art_revision=3,fps=30,units='meters',source_front='-Y',glTF_front='+Z',root_bone='root',npc_rig=human.name,npc_exports=npc_exports,
    modular_library='rigged/NPC_ModularParts_Library.glb',npc_clips=clips[:len(human_actions)],
    weapons=dict(sword=3,dual_pistols=4,claws=5,scissors=6,bow=0,spear=1,ledger=2),
    creatures=[dict(index=c['index'],id=c['mesh'].name,name=c['label'],gait=c['gait'],file='rigged/'+c['mesh'].name+'.glb',skeleton=c['rig'].name,bones=len(c['rig'].data.bones),triangles=sum(len(p.vertices)-2 for p in c['mesh'].data.polygons),clips=c['clips']) for c in creatures])
with open(os.path.join(ROOT,'data','animation_manifest.json'),'w',encoding='utf-8') as f:json.dump(manifest,f,ensure_ascii=False,indent=2)
validation['max_ik_reach_error_m']=max(i['max_reach_error_m'] for i in clips)
validation['npc_actions']=len(human_actions);validation['creature_actions']=sum(len(c['actions']) for c in creatures)
validation['rig_count']=len(rigs);validation['game_integration']=False
with open(os.path.join(ROOT,'data','rig_validation.json'),'w') as f:json.dump(validation,f,indent=2)
# Make an inspectable skinned NPC preview from the shared library, without showing every variant.
for slot,idx in presets['npcs'][0]['parts'].items():
    if idx>=0:human_parts[(slot,idx)].hide_render=False;human_parts[(slot,idx)].hide_set(False)
human.hide_set(False);human.animation_data.action=human_actions[0]
collection.hide_render=False
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
print('RIG_AUTHORING_COMPLETE',len(clips),validation,flush=True)
