"""Offline revision: stable humanoid hips, longer arms, true phased animal locomotion.
Edits Blender/GLB art only. All animations are baked keyframes, without runtime IK.
"""
import bpy,math,os,json
from mathutils import Vector,Quaternion,Matrix
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,'rigged')
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
scene=bpy.context.scene;scene.render.fps=30
source=bpy.data.collections['05_RIGGED_CHARACTERS'];source.hide_viewport=False
for col in source.children:col.hide_viewport=False
bpy.context.view_layer.update()
for obj in list(source.all_objects):obj.hide_set(False)
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:manifest=json.load(f)
with open(os.path.join(ROOT,'source','author_rigs_and_creatures.py'),encoding='utf-8') as f:recipe=f.read()
exec(recipe[recipe.index('def bone_rig('):recipe.index('clips=[];rigs=[];')])
exec(recipe[recipe.index('def human_pose('):recipe.index('human_actions=[]')])
clips=[];human=bpy.data.objects[manifest['npc_rig']]
human_meshes=[o for o in source.all_objects if o.type=='MESH' and o.name.startswith('SK_NPC_')]
def deactivate(rig):
    rig.animation_data.action=None
    for t in rig.animation_data.nla_tracks:t.mute=True
    reset(rig)
deactivate(human)
def components(mesh):
    adjacency=[[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a,b=edge.vertices;adjacency[a].append(b);adjacency[b].append(a)
    seen=set();result=[]
    for start in range(len(mesh.vertices)):
        if start in seen:continue
        seen.add(start);pending=[start];component=[]
        while pending:
            i=pending.pop();component.append(i)
            for j in adjacency[i]:
                if j not in seen:seen.add(j);pending.append(j)
        result.append(component)
    return result
def centroid(mesh,indices):return sum((mesh.vertices[i].co for i in indices),Vector())/len(indices)
def arm_map(p):
    s=-1 if p.x<0 else 1
    return Vector((s*(.21+(abs(p.x)-.21)*1.15),p.y,1.12+(p.z-1.12)*1.28))
arm_delta={side:arm_map(human.data.bones['hand.'+side].head_local)-human.data.bones['hand.'+side].head_local for side in ['L','R']}
if not human.get('arm_revision_v5',False):
    # Change rest geometry and edit bones together, preserving clothing and hand attachments.
    for obj in human_meshes:
        if obj.name.startswith('SK_NPC_Body_'):
            for comp in components(obj.data):
                center=centroid(obj.data,comp)
                if center.z<.90 and abs(center.x)>.28:
                    d=arm_delta['L' if center.x<0 else 'R']
                    for idx in comp:obj.data.vertices[idx].co+=d
        elif obj.name.startswith('SK_NPC_Top_'):
            for comp in components(obj.data):
                center=centroid(obj.data,comp)
                if abs(center.x)>.23 and .85<center.z<1.09:
                    for idx in comp:obj.data.vertices[idx].co=arm_map(obj.data.vertices[idx].co)
        elif obj.name.startswith('SK_NPC_Prop_'):
            names={g.index:g.name for g in obj.vertex_groups}
            for vertex in obj.data.vertices:
                side='L' if any(names[g.group] in ['hand.L','bow_string'] and g.weight>.5 for g in vertex.groups) else 'R'
                vertex.co+=arm_delta[side]
    bpy.ops.object.select_all(action='DESELECT');human.select_set(True);bpy.context.view_layer.objects.active=human
    bpy.ops.object.mode_set(mode='EDIT')
    for side in ['L','R']:
        for name in ['upper_arm','forearm']:
            b=human.data.edit_bones[name+'.'+side];b.head=arm_map(b.head.copy());b.tail=arm_map(b.tail.copy())
        b=human.data.edit_bones['hand.'+side];b.head+=arm_delta[side];b.tail+=arm_delta[side]
    b=human.data.edit_bones['bow_string'];b.head+=arm_delta['L'];b.tail+=arm_delta['L']
    bpy.ops.object.mode_set(mode='OBJECT');human['arm_revision_v5']=True
    human['arm_length_scale']=1.26
# Component-aware weights keep the shorts shell intact instead of splitting it across thighs.
def assign(obj,indices,weights):
    for group in obj.vertex_groups:
        group.remove(indices)
    for name,weight in weights.items():
        if weight>0:
            group=obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name);group.add(indices,weight,'REPLACE')
def elbow_topology(obj):
    if obj.get('elbow_rings_v5',False):return
    mesh=obj.data;parts=[]
    for comp in components(mesh):
        center=centroid(mesh,comp)
        if abs(center.x)>.23 and .80<center.z<1.09:parts.append((comp,'L' if center.x<0 else 'R'))
    if not parts:return
    removed={i for comp,side in parts for i in comp};vertices=[];weights=[];remap={};faces=[];materials=[]
    for vertex in mesh.vertices:
        if vertex.index in removed:continue
        remap[vertex.index]=len(vertices);vertices.append(tuple(vertex.co));weights.append([(g.group,g.weight) for g in vertex.groups])
    for poly in mesh.polygons:
        if any(i in removed for i in poly.vertices):continue
        faces.append(tuple(remap[i] for i in poly.vertices));materials.append(poly.material_index)
    for comp,side in parts:
        selected=set(comp);mat=next(p.material_index for p in mesh.polygons if p.vertices[0] in selected)
        sign=-1 if side=='L' else 1
        a=human.data.bones['upper_arm.'+side].head_local.copy();b=arm_map(Vector((sign*.38,0,.79)))
        direction=(b-a).normalized();u=Vector((0,1,0));v=direction.cross(u).normalized();rings=[]
        for t in [0,.27,.44,.575,.70,.85,1]:
            center=a.lerp(b,t);radius=.088+(.070-.088)*t;ring=[]
            z=center.z;p=max(0,min(1,(z-(human.data.bones['forearm.'+side].head_local.z-.06))/.12))
            for j in range(6):
                angle=2*math.pi*j/6;position=center+radius*(u*math.cos(angle)+v*math.sin(angle))
                ring.append(len(vertices));vertices.append(tuple(position));weights.append([(obj.vertex_groups['upper_arm.'+side].index,p),(obj.vertex_groups['forearm.'+side].index,1-p)])
            rings.append(ring)
        for first,last in zip(rings,rings[1:]):
            for j in range(6):
                k=(j+1)%6;faces.extend([(first[j],last[j],last[k]),(first[j],last[k],first[k])]);materials.extend([mat,mat])
        for ring in [rings[0],list(reversed(rings[-1]))]:
            for j in range(1,5):faces.append((ring[0],ring[j],ring[j+1]));materials.append(mat)
    new=bpy.data.meshes.new(obj.name+'_ElbowLoops');new.from_pydata(vertices,[],faces);new.update()
    for m in mesh.materials:new.materials.append(m)
    for p,m in zip(new.polygons,materials):p.material_index=m;p.use_smooth=False
    group_names=[g.name for g in obj.vertex_groups]
    obj.data=new;obj.vertex_groups.clear()
    for name in group_names:obj.vertex_groups.new(name=name)
    for i,group_weights in enumerate(weights):
        for group,weight in group_weights:
            if weight>0:obj.vertex_groups[group].add([i],weight,'REPLACE')
    obj['elbow_rings_v5']=True
for obj in human_meshes:
    if obj.name.startswith('SK_NPC_Top_'):elbow_topology(obj)
for obj in human_meshes:
    if obj.name.startswith('SK_NPC_Legs_'):
        for comp in components(obj.data):
            c=centroid(obj.data,comp);side='L' if c.x<0 else 'R'
            if abs(c.x)<.055 and c.z>.55:assign(obj,comp,{'pelvis':1});continue
            for idx in comp:
                z=obj.data.vertices[idx].co.z
                if z>.59:
                    p=max(0,min(1,(z-.59)/.11));weights={'pelvis':p,'thigh.'+side:1-p}
                else:
                    p=max(0,min(1,(z-.37)/.13));weights={'thigh.'+side:p,'shin.'+side:1-p}
                assign(obj,[idx],weights)
    if obj.name.startswith('SK_NPC_Top_'):
        for comp in components(obj.data):
            c=centroid(obj.data,comp)
            if abs(c.x)>.23 and .80<c.z<1.09:
                side='L' if c.x<0 else 'R';elbow=human.data.bones['forearm.'+side].head_local.z
                for idx in comp:
                    z=obj.data.vertices[idx].co.z;p=max(0,min(1,(z-(elbow-.06))/.12))
                    assign(obj,[idx],{'upper_arm.'+side:p,'forearm.'+side:1-p})
# Key translations on pelvis/spine as well as rotations, then bake to standard glTF channels.
def key(rig,frame):
    for p in rig.pose.bones:
        p.keyframe_insert('rotation_quaternion',frame=frame,group=p.name)
        p.keyframe_insert('location',frame=frame,group=p.name)
def delete_clip(rig,name):
    if rig.animation_data.action and rig.animation_data.action.name==name:rig.animation_data.action=None
    for t in list(rig.animation_data.nla_tracks):
        if any(s.action and s.action.name==name for s in t.strips):rig.animation_data.nla_tracks.remove(t)
    a=bpy.data.actions.get(name)
    if a:bpy.data.actions.remove(a)
def local_displacement(rig,bone,delta):
    # Convert desired rig-space offset into the bone's rest local axes.
    rig.pose.bones[bone].location=rig.data.bones[bone].matrix_local.to_quaternion().inverted() @ Vector(delta)
def phase_foot(p,duration,speed,stance,lift):
    y,z,grounded=stride(p,duration,speed,stance,lift)
    if not grounded:
        q=((p%1)-stance)/(1-stance);z=lift*math.sin(math.pi*q)**1.5
    return y,z,grounded
records=[];contact_tests=[]
def run_biped(rig,scale,duration,speed,rm=False):
    def pose(t,seconds):
        wave=math.sin(2*math.pi*t);delta=Vector((.006*scale*wave,-speed*seconds if rm else 0,scale*(-.055+.018*math.cos(4*math.pi*t))))
        translate_root(rig,delta);rotate(rig,'root',x=.065)
        rotate(rig,'pelvis',x=.01,y=.012*wave,z=.035*wave)
        rotate(rig,'spine',x=.025,z=-.035*wave)
        rotate(rig,'chest',z=-.025*wave,y=-.012*wave)
        rotate(rig,'neck',x=-.04);bpy.context.view_layer.update();err=0
        for sign,side in [(-1,'L'),(1,'R')]:
            p=(t+(0 if side=='L' else .5))%1
            y,z,grounded=phase_foot(p,duration,speed,.34,.225*scale)
            target=joint_point(rig,'foot.'+side)+Vector((0,y,z))
            if rm:target.y-=speed*seconds
            err=max(err,ik(rig,'thigh.'+side,'shin.'+side,'foot.'+side,target,(0,-1,0)))
            toe=0 if grounded else .16*math.sin(math.pi*(p-.34)/.66)
            global_rotation(rig,'foot.'+side,Quaternion((1,0,0),toe) @ rig.data.bones['foot.'+side].matrix_local.to_quaternion())
            # Opposite arm/leg phase; elbow stays flexed instead of tiny wrist oscillations.
            alpha=(-1 if side=='L' else 1)*.55*math.cos(2*math.pi*t)
            beta=1.43+.12*math.sin(2*math.pi*t+(0 if side=='L' else math.pi))
            upper=rig.data.bones['upper_arm.'+side].length
            lower=rig.data.bones['forearm.'+side].length
            aim(rig,'upper_arm.'+side,(sign*.025,-upper*math.sin(alpha),-upper*math.cos(alpha)))
            bpy.context.view_layer.update()
            aim(rig,'forearm.'+side,(sign*.012,-lower*math.sin(alpha+beta),-lower*math.cos(alpha+beta)))
            bpy.context.view_layer.update()
            # Relaxed wrist follows forearm; no arm scaling at pose time.
        return err
    return pose
def quadruped_pose(rig,scale,duration,speed,kind='Run',rm=False):
    running=kind in ['Run','Sprint'];sprinting=kind=='Sprint'
    # Explicit touchdown times, rather than four similar sinusoidal leg swings.
    td=({('front','L'):0,('front','R'):.12,('hind','R'):.50,('hind','L'):.62} if sprinting else
        {('front','L'):0,('hind','R'):0,('front','R'):.5,('hind','L'):.5} if running else
        {('hind','L'):0,('front','L'):.25,('hind','R'):.5,('front','R'):.75})
    def pose(t,seconds):
        curl=math.cos(2*math.pi*(t-.43)) if sprinting else .12*math.sin(2*math.pi*t)
        bob=scale*(-.045-.05*math.cos(4*math.pi*(t-.16))) if sprinting else scale*(-.06+.024*math.cos(4*math.pi*(t-.08))) if running else scale*(-.055+.008*math.cos(4*math.pi*t))
        translate_root(rig,(0,-speed*seconds if rm else 0,bob))
        rotate(rig,'pelvis',x=-.10*curl if running else 0,y=.012*math.sin(2*math.pi*t))
        rotate(rig,'spine',x=.22*curl if running else 0)
        local_displacement(rig,'spine',(0,.065*scale*curl if running else 0,0))
        rotate(rig,'chest',x=-.10*curl if running else 0)
        rotate(rig,'neck',x=-.035*curl);rotate(rig,'head',x=-.015*curl)
        rotate(rig,'tail.01',x=.10*math.sin(2*math.pi*t-.7),z=.06*math.sin(2*math.pi*t))
        rotate(rig,'tail.02',x=.12*math.sin(2*math.pi*t-1.3))
        bpy.context.view_layer.update();err=0
        for limb in ['front','hind']:
            stance=(.24 if limb=='front' else .22) if sprinting else .40 if running else .67
            for side in ['L','R']:
                p=(t-td[(limb,side)])%1;lift=(.29 if limb=='front' else .34)*scale if sprinting else (.16 if limb=='front' else .18)*scale if running else .12*scale
                y,z,grounded=phase_foot(p,duration,speed,stance,lift)
                footname=f'{limb}_foot.{side}';target=joint_point(rig,footname)+Vector((0,y,z))
                if rm:target.y-=speed*seconds
                bend=(0,1,0) if limb=='front' else (0,-1,0)
                err=max(err,ik(rig,f'{limb}_upper.{side}',f'{limb}_lower.{side}',footname,target,bend))
                # Distal paw folds during recovery, but remains flat throughout load bearing.
                angle=0 if grounded else .25*math.sin(math.pi*(p-stance)/(1-stance))
                global_rotation(rig,footname,Quaternion((1,0,0),angle) @ rig.data.bones[footname].matrix_local.to_quaternion())
        return err
    return pose,td
def replace(rig,name,duration,fn,loop,speed,rm,events):
    delete_clip(rig,name);a=action(rig,name,duration,fn,loop,speed,rm,events)
    nla(rig,[a]);return a
# Rebuild every NPC clip against the longer bind-pose arms, so attacks and gear stay attached.
for old in manifest['npc_clips']:
    name=old['name'];delete_clip(human,name)
human_actions=[]
for old in manifest['npc_clips']:
    name=old['name'];short=name.removeprefix('NPC_');d=old['duration_s'];speed=old['speed_mps'];rm=old['root_motion']
    if short in ['Run','Run_RootMotion']:
        d=22/30;speed=1.45;fn=run_biped(human,1,d,speed,rm)
        events=[dict(time_s=round(d*t,4),event='foot_contact',foot=side) for t,side in [(0,'L'),(.5,'R')]]
    else:
        if short.startswith('Turn_'):
            sign=1 if '_L' in short else -1;angle=int(short.split('_')[1][1:]);fn=human_pose('Turn',turn=sign*math.radians(angle))
        else:fn=human_pose(short.removesuffix('_RootMotion'),d,speed,rm)
        events=old['events']
    a=action(human,name,d,fn,old['loop'],speed,rm,events);human_actions.append(a)
manifest['npc_clips']=clips.copy();nla(human,human_actions)
new_metas={}
for creature in manifest['creatures']:
    rig=bpy.data.objects[creature['skeleton']];deactivate(rig);scale=rig.data.bones['pelvis'].head_local.z/(.70 if creature['gait']=='biped' else .88)
    for old in creature['clips']:
        run=old['name'].endswith(('_Run','_Run_RootMotion'));walk=old['name'].endswith(('_Walk','_Walk_RootMotion'))
        if not run and not (walk and creature['gait']=='quadruped'):continue
        d=24/30 if creature['gait']=='quadruped' and run else 22/30 if run else 36/30
        speed=(1.50 if creature['gait']=='quadruped' and run else 1.30 if run else .58)*scale
        rm=old['root_motion'];events=[]
        if creature['gait']=='quadruped':
            fn,td=quadruped_pose(rig,scale,d,speed,'Run' if run else 'Walk',rm)
            for (limb,side),when in td.items():events.append(dict(time_s=round(when*d,4),event='foot_contact',foot=limb+'.'+side))
        else:fn=run_biped(rig,scale,d,speed,rm)
        a=replace(rig,old['name'],d,fn,old['loop'],speed,rm,events)
        new_metas[a.name]=clips[-1]
    creature['clips']=[new_metas.get(c['name'],c) for c in creature['clips']]
    if creature['gait']=='quadruped':
        creature['clips']=[c for c in creature['clips'] if not c['name'].endswith(('_Sprint','_Sprint_RootMotion'))]
        for rm in [False,True]:
            name='MOB_%02d_Sprint'%creature['index']+('_RootMotion' if rm else '')
            fn,td=quadruped_pose(rig,scale,.8,2.25*scale,'Sprint',rm)
            events=[dict(time_s=round(when*.8,4),event='foot_contact',foot=limb+'.'+side) for (limb,side),when in td.items()]
            replace(rig,name,.8,fn,not rm,2.25*scale,rm,events);creature['clips'].append(clips[-1])
# Actual FK contact/loop samples, including spine translations and pelvis counter rotation.
def check(rig,clip,phase_info,scale,run=True):
    deactivate(rig);rig.animation_data.action=bpy.data.actions[clip['name']]
    n=clip['frames'][1]-1;d=clip['duration_s'];speed=clip['speed_mps'];errs=[];air=0;maxreach=clip['max_reach_error_m']
    samples=[]
    for f in range(n+1):
        t=f/n;scene.frame_set(f+1);bpy.context.view_layer.update();contact=[]
        for bone,when,stance in phase_info:
            p=(t-when)%1
            if .015<p<stance-.015:
                expected=joint_point(rig,bone);y,_,_=phase_foot(p,d,speed,stance,.1)
                expected.y+=y-speed*f/30;errs.append((rig.pose.bones[bone].head-expected).length);contact.append(bone)
        if not contact:air+=1
        samples.append(dict(frame=f+1,contacts=contact))
    contact_tests.append(dict(rig=rig.name,clip=clip['name'],max_planted_ankle_error_m=max(errs or [0]),max_requested_reach_error_m=maxreach,all_feet_air_samples=air,contacts=samples))
    rig.animation_data.action=None
check(human,next(c for c in manifest['npc_clips'] if c['name']=='NPC_Run_RootMotion'),[('foot.L',0,.34),('foot.R',.5,.34)],1)
for creature in manifest['creatures']:
    rig=bpy.data.objects[creature['skeleton']];q=creature['gait']=='quadruped'
    meta=next(c for c in creature['clips'] if c['name'].endswith('_Run_RootMotion'))
    info=[('front_foot.L',0,.40),('hind_foot.R',0,.40),('front_foot.R',.5,.40),('hind_foot.L',.5,.40)] if q else [('foot.L',0,.34),('foot.R',.5,.34)]
    check(rig,meta,info,1)
    if q:
        check(rig,next(c for c in creature['clips'] if c['name'].endswith('_Sprint_RootMotion')),[('front_foot.L',0,.24),('front_foot.R',.12,.24),('hind_foot.R',.50,.22),('hind_foot.L',.62,.22)],1)
        check(rig,next(c for c in creature['clips'] if c['name'].endswith('_Walk_RootMotion')),[('hind_foot.L',0,.67),('front_foot.L',.25,.67),('hind_foot.R',.5,.67),('front_foot.R',.75,.67)],1)
# Re-export complete named clip sets, keeping all established part and monster identifiers.
parts={}
for o in human_meshes:
    tokens=o.name.split('_');parts[(tokens[2].lower(),int(tokens[3]))]=o
with open(os.path.join(ROOT,'data','npc_appearance_presets.json'),encoding='utf-8') as f:presets=json.load(f)
for p in presets['npcs']:
    export(human,[parts[(slot,index)] for slot,index in p['parts'].items() if index>=0],p['appearance_id']+'.glb',human_actions)
export(human,human_meshes,'NPC_ModularParts_Library.glb',human_actions)
for c in manifest['creatures']:
    rig=bpy.data.objects[c['skeleton']];export(rig,[bpy.data.objects[c['id']]],os.path.basename(c['file']),[bpy.data.actions[x['name']] for x in c['clips']])
    reset(rig)
manifest['art_revision']=5
manifest['run_revision']=dict(npc_arm_extension='about 26 percent',npc_stance=.34,quadruped_run='diagonal trot',quadruped_touchdowns={'front.L':0,'hind.R':0,'front.R':.5,'hind.L':.5},quadruped_sprint='rotary gallop',quadruped_sprint_touchdowns={'front.L':0,'front.R':.12,'hind.R':.50,'hind.L':.62},quadruped_walk='lateral four-beat',spine_translation_baked=True)
with open(os.path.join(ROOT,'data','animation_manifest.json'),'w',encoding='utf-8') as f:json.dump(manifest,f,ensure_ascii=False,indent=2)
report=dict(art_revision=5,checks=contact_tests,issues=[],max_planted_ankle_error_m=max(c['max_planted_ankle_error_m'] for c in contact_tests))
if report['max_planted_ankle_error_m']>.015:report['issues'].append('Contact error exceeds 15 mm')
with open(os.path.join(ROOT,'data','run_revision_validation.json'),'w') as f:json.dump(report,f,indent=2)
for col in source.children:col.hide_viewport=True;col.hide_render=True
for o in human_meshes:o.hide_render=True;o.hide_set(True)
for slot,index in presets['npcs'][0]['parts'].items():
    if index>=0:parts[(slot,index)].hide_render=False;parts[(slot,index)].hide_set(False)
deactivate(human);human.hide_set(False);human.animation_data.action=bpy.data.actions['NPC_Run'];scene.frame_start=1;scene.frame_end=23;scene.frame_set(1)
bpy.context.view_layer.objects.active=human
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
print('RUN_REVISION_COMPLETE',report['max_planted_ankle_error_m'],report['issues'],flush=True)
