"""Softer low-poly characters, anatomically articulated quadrupeds and baked gaits.
This is an offline art-authoring recipe. It never changes game code or runs a build.
"""
import bpy,bmesh,os,json,math
from mathutils import Vector,Quaternion,Matrix
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)));OUT=os.path.join(ROOT,'rigged')
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:_revision=json.load(f).get('art_revision',5)
input_blend=os.path.join(ROOT,'revisions','Characters_before_natural_v6.blend') if _revision>=6 else os.path.join(ROOT,'Characters_Rigged.blend')
bpy.ops.wm.open_mainfile(filepath=input_blend)
scene=bpy.context.scene;scene.render.fps=30
collection=bpy.data.collections['05_RIGGED_CHARACTERS'];collection.hide_viewport=False
for col in collection.children:col.hide_viewport=False
bpy.context.view_layer.update()
for obj in list(collection.all_objects):obj.hide_set(False)
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:manifest=json.load(f)
with open(os.path.join(ROOT,'source','author_rigs_and_creatures.py'),encoding='utf-8') as f:recipe=f.read()
exec(recipe[recipe.index('def bone_rig('):recipe.index('clips=[];rigs=[];')])
exec(recipe[recipe.index('chunks=[]'):recipe.index("bow=human_parts[('prop',0)]")])
clips=[];human=bpy.data.objects[manifest['npc_rig']]
human_meshes=[o for o in collection.all_objects if o.type=='MESH' and o.name.startswith('SK_NPC_')]
M={m.name.removeprefix('MAT_'):m for m in bpy.data.materials}
def deactivate(rig):
    rig.animation_data.action=None
    for track in rig.animation_data.nla_tracks:track.mute=True
    reset(rig)
for rig in [human]+[bpy.data.objects[c['skeleton']] for c in manifest['creatures']]:deactivate(rig)
scene.frame_set(1);bpy.context.view_layer.update()
def components(mesh):
    adjacency=[[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a,b=edge.vertices;adjacency[a].append(b);adjacency[b].append(a)
    seen=set();out=[]
    for start in range(len(mesh.vertices)):
        if start in seen:continue
        seen.add(start);todo=[start];part=[]
        while todo:
            i=todo.pop();part.append(i)
            for j in adjacency[i]:
                if j not in seen:seen.add(j);todo.append(j)
        out.append(part)
    return out
def center(mesh,part):return sum((mesh.vertices[i].co for i in part),Vector())/len(part)
def remesh_legs(obj):
    if obj.get('knee_loops_v6'):return
    mesh=obj.data;legparts=[]
    for part in components(mesh):
        c=center(mesh,part);zs=[mesh.vertices[i].co.z for i in part]
        if abs(c.x)>.06 and max(zs)-min(zs)>.42:legparts.append((part,'L' if c.x<0 else 'R'))
    if not legparts:return
    removed={i for part,side in legparts for i in part};verts=[];weights=[];faces=[];mats=[];remap={}
    names=[g.name for g in obj.vertex_groups]
    for vertex in mesh.vertices:
        if vertex.index in removed:continue
        remap[vertex.index]=len(verts);verts.append(tuple(vertex.co));weights.append([(g.group,g.weight) for g in vertex.groups])
    for poly in mesh.polygons:
        if any(i in removed for i in poly.vertices):continue
        faces.append(tuple(remap[i] for i in poly.vertices));mats.append(poly.material_index)
    for part,side in legparts:
        selected=set(part);material=next(p.material_index for p in mesh.polygons if p.vertices[0] in selected)
        top=human.data.bones['thigh.'+side].head_local.copy();bottom=human.data.bones['foot.'+side].head_local.copy();rings=[]
        for t in [0,.20,.38,.46,.50,.56,.64,.82,1]:
            c=top.lerp(bottom,t);radius=.091+(.072-.091)*t;ring=[]
            for j in range(10):
                a=j*math.tau/10;ring.append(len(verts));verts.append(tuple(c+Vector((math.cos(a)*radius,math.sin(a)*radius,0))))
                if c.z>.59:
                    p=max(0,min(1,(c.z-.59)/.11));ww={'pelvis':p,'thigh.'+side:1-p}
                else:
                    p=max(0,min(1,(c.z-.38)/.11));ww={'thigh.'+side:p,'shin.'+side:1-p}
                weights.append([(names.index(n),w) for n,w in ww.items() if w>0])
            rings.append(ring)
        for a,b in zip(rings,rings[1:]):
            for j in range(10):
                k=(j+1)%10;faces.extend([(a[j],b[j],b[k]),(a[j],b[k],a[k])]);mats.extend([material,material])
        for ring in [rings[0],list(reversed(rings[-1]))]:
            for j in range(1,9):faces.append((ring[0],ring[j],ring[j+1]));mats.append(material)
    result=bpy.data.meshes.new(obj.name+'_KneeLoops_v6');result.from_pydata(verts,[],faces);result.update()
    for m in mesh.materials:result.materials.append(m)
    for p,mat in zip(result.polygons,mats):p.material_index=mat
    obj.data=result;obj.vertex_groups.clear()
    for name in names:obj.vertex_groups.new(name=name)
    for i,ww in enumerate(weights):
        for g,w in ww:obj.vertex_groups[g].add([i],w,'REPLACE')
    obj['knee_loops_v6']=True
for obj in human_meshes:
    if obj.name.startswith('SK_NPC_Legs_'):remesh_legs(obj)
    if obj.name.startswith('SK_NPC_Feet_01_') and not obj.get('sole_v6'):
        low=min(v.co.z for v in obj.data.vertices)
        for vertex in obj.data.vertices:vertex.co.z-=low
        obj['sole_v6']=True
# Preserve existing foot targets/attack clips while adding scapular, carpal and hock joints.
helpers=bpy.data.collections.new('AUTHORING_REFERENCE_ONLY');scene.collection.children.link(helpers)
old_quads={};scales={}
for creature in manifest['creatures']:
    rig=bpy.data.objects[creature['skeleton']]
    scales[rig.name]=rig.data.bones['pelvis'].head_local.z/(.88 if creature['gait']=='quadruped' else .70)
    if creature['gait']!='quadruped':continue
    mesh=bpy.data.objects[creature['id']];scale=scales[rig.name]
    if rig.get('natural_skeleton_v6'):raise RuntimeError('Use the saved v5 revision as input to regenerate v6')
    old=rig.copy();old.data=rig.data.copy();old.name=rig.name+'_RetargetSource';helpers.objects.link(old);old.animation_data_clear();old.animation_data_create()
    old_quads[rig.name]=old
    old_matrices={n:rig.data.bones[n].matrix_local.copy() for n in rig.data.bones.keys()}
    old_lengths={n:rig.data.bones[n].length for n in rig.data.bones.keys()}
    bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig;bpy.ops.object.mode_set(mode='EDIT')
    for limb in ['front','hind']:
        for side in ['L','R']:
            upper=rig.data.edit_bones[f'{limb}_upper.{side}'];lower=rig.data.edit_bones[f'{limb}_lower.{side}'];foot=rig.data.edit_bones[f'{limb}_foot.{side}']
            if limb=='front':
                scap=rig.data.edit_bones.new('scapula.'+side);scap.head=upper.head+Vector((0,.12,.10))*scale;scap.tail=upper.head.copy();scap.parent=rig.data.edit_bones['chest'];upper.parent=scap
            joint=rig.data.edit_bones.new(('carpus.' if limb=='front' else 'hock.')+side)
            joint.tail=foot.head.copy();joint.head=foot.head+Vector((0,.025 if limb=='front' else .14,.16 if limb=='front' else .18))*scale
            lower.tail=joint.head.copy();joint.parent=lower;joint.use_connect=True;foot.parent=joint;foot.use_connect=True
    bpy.ops.object.mode_set(mode='OBJECT')
    for p in rig.pose.bones:p.rotation_mode='QUATERNION'
    for vertex in mesh.data.vertices:
        for group in vertex.groups:
            name=mesh.vertex_groups[group.group].name
            if '_lower.' in name and group.weight>.5:
                local=old_matrices[name].inverted() @ vertex.co;local.y*=rig.data.bones[name].length/old_lengths[name]
                vertex.co=rig.data.bones[name].matrix_local @ local;break
    for mod in list(mesh.modifiers):
        if mod.type=='ARMATURE':mesh.modifiers.remove(mod)
    chunks.clear();chunks.append(mesh)
    for limb in ['front','hind']:
        for side in ['L','R']:
            name=('carpus.' if limb=='front' else 'hock.')+side;b=rig.data.bones[name]
            mat='gunmetal' if creature['index']==4 else 'flesh' if creature['index']==5 else 'void'
            segment(b.head_local,b.tail_local,.050*scale,mat,name,.040*scale,n=10)
            ellipsoid(b.head_local,(.070*scale,)*3,mat,name,2)
    join_mesh(mesh.name,rig);rig['natural_skeleton_v6']=True;creature['bones']=len(rig.data.bones)
# Softer silhouettes and normals, while keeping the same mesh IDs and material boundaries.
def soften(obj):
    if obj.get('soft_surface_v6'):return
    mesh=obj.data;organic=[]
    for part in components(mesh):
        if len(part)<10:continue
        points=[mesh.vertices[i].co for i in part];lo=Vector(tuple(min(p[k] for p in points) for k in range(3)));hi=Vector(tuple(max(p[k] for p in points) for k in range(3)))
        radius=(hi-lo)*.5;c=(hi+lo)*.5
        if min(radius)<.012:continue
        norms=[sum(((p[k]-c[k])/radius[k])**2 for k in range(3))**.5 for p in points]
        if min(norms)>.87 and max(norms)<1.16:organic.append((part,c,radius))
    bm=bmesh.new();bm.from_mesh(mesh);bm.verts.ensure_lookup_table();tag=bm.verts.layers.int.new('round_component')
    spec={}
    for idx,(part,c,r) in enumerate(organic,1):
        spec[idx]=(c,r)
        for i in part:bm.verts[i][tag]=idx
    edges=[edge for edge in bm.edges if edge.verts[0][tag]>0 and edge.verts[0][tag]==edge.verts[1][tag]]
    if edges:bmesh.ops.subdivide_edges(bm,edges=edges,cuts=1,use_grid_fill=True)
    for vertex in bm.verts:
        idx=vertex[tag]
        if idx in spec:
            c,r=spec[idx];u=Vector(tuple((vertex.co[k]-c[k])/r[k] for k in range(3)))
            if u.length>.001:
                u.normalize();vertex.co=c+Vector(tuple(u[k]*r[k] for k in range(3)))
    bm.verts.layers.int.remove(tag);bm.to_mesh(mesh);bm.free();mesh.update()
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bevel=obj.modifiers.new('Soft silhouette','BEVEL');bevel.width=.014 if obj.name.startswith('SK_NPC_') else .018
    if 'Feet' in obj.name:bevel.width=.022
    bevel.segments=3;bevel.limit_method='ANGLE';bevel.angle_limit=math.radians(48)
    obj.modifiers.move(len(obj.modifiers)-1,0);bpy.ops.object.modifier_apply(modifier=bevel.name)
    for poly in obj.data.polygons:poly.use_smooth=True
    if hasattr(obj.data,'set_sharp_from_angle'):obj.data.set_sharp_from_angle(angle=math.radians(68))
    normal=obj.modifiers.new('Balanced surface normals','WEIGHTED_NORMAL');normal.keep_sharp=True
    obj.modifiers.move(len(obj.modifiers)-1,0);bpy.ops.object.modifier_apply(modifier=normal.name)
    tri=obj.modifiers.new('Export triangles','TRIANGULATE');obj.modifiers.move(len(obj.modifiers)-1,0);bpy.ops.object.modifier_apply(modifier=tri.name)
    obj['soft_surface_v6']=True
for obj in human_meshes+[bpy.data.objects[c['id']] for c in manifest['creatures']]:soften(obj)
print('V6_GEOMETRY_AND_SKELETON_READY',flush=True)
def key(rig,frame):
    for p in rig.pose.bones:
        p.keyframe_insert('location',frame=frame,group=p.name);p.keyframe_insert('rotation_quaternion',frame=frame,group=p.name)
def displacement(rig,name,delta):rig.pose.bones[name].location=rig.data.bones[name].matrix_local.to_quaternion().inverted() @ Vector(delta)
def remove_clip(rig,name):
    if rig.animation_data.action and rig.animation_data.action.name==name:rig.animation_data.action=None
    for track in list(rig.animation_data.nla_tracks):
        if any(s.action and s.action.name==name for s in track.strips):rig.animation_data.nla_tracks.remove(track)
    a=bpy.data.actions.get(name)
    if a:bpy.data.actions.remove(a)
def footpath(p,d,speed,stance,lift):
    y,z,grounded=stride(p,d,speed,stance,lift)
    if not grounded:
        q=((p%1)-stance)/(1-stance);z=lift*math.sin(math.pi*q)**1.6
    return y,z,grounded
def gait_biped(rig,scale,d,speed,kind,rm):
    run=kind=='Run';stance=.36 if run else .62
    def pose(t,seconds):
        wave=math.cos(math.tau*(t-.18));bob=(-.065+.020*math.cos(2*math.tau*(t-.08))) if run else (-.048-.012*math.cos(2*math.tau*(t-.07)))
        translate_root(rig,(-(.007 if run else .012)*scale*wave,-speed*seconds if rm else 0,bob*scale))
        rotate(rig,'root',x=.040 if run else .012)
        rotate(rig,'pelvis',x=.008,y=.012*wave,z=(.032 if run else .042)*wave)
        rotate(rig,'spine',x=.018 if run else .005,z=-.023*wave)
        rotate(rig,'chest',z=-.027*wave,y=-.008*wave);rotate(rig,'neck',x=-.025 if run else -.01,z=.008*wave)
        bpy.context.view_layer.update();err=0
        for sign,side in [(-1,'L'),(1,'R')]:
            p=(t+(0 if side=='L' else .5))%1;y,z,grounded=footpath(p,d,speed,stance,(.20 if run else .075)*scale)
            # Heel-to-flat-to-toe-off, with the sole-height compensation baked into ankle keys.
            angle=0
            if grounded:
                angle=-.13*(1-smooth(p/.08))+.24*smooth((p-(stance-.12))/.12)
            else:angle=.12*math.sin(math.pi*(p-stance)/(1-stance))
            sole_comp=0
            if grounded:
                sole_values=[(Quaternion((1,0,0),angle) @ Vector((0,yy,-.17))).z for yy in [-.19,.11]]
                sole_comp=max(0,-min(sole_values)-.17)*scale
            target=joint_point(rig,'foot.'+side)+Vector((0,y-speed*seconds if rm else y,z+sole_comp))
            err=max(err,ik(rig,'thigh.'+side,'shin.'+side,'foot.'+side,target,(0,-1,0)))
            global_rotation(rig,'foot.'+side,Quaternion((1,0,0),angle) @ rig.data.bones['foot.'+side].matrix_local.to_quaternion())
            phase=math.tau*t+(0 if side=='L' else math.pi)
            alpha=-(.50 if run else .28)*math.cos(phase)
            beta=(1.08+.12*math.sin(phase-.4)) if run else (.30+.08*math.sin(phase))
            u=rig.data.bones['upper_arm.'+side].length;l=rig.data.bones['forearm.'+side].length
            aim(rig,'upper_arm.'+side,(sign*.018,-u*math.sin(alpha),-u*math.cos(alpha)));bpy.context.view_layer.update()
            aim(rig,'forearm.'+side,(sign*.008,-l*math.sin(alpha+beta),-l*math.cos(alpha+beta)));bpy.context.view_layer.update()
        return err
    return pose
def solve_animal_leg(rig,limb,side,target,scale,recovery=0,foot_q=None):
    name=('carpus.' if limb=='front' else 'hock.')+side
    offset=Vector((0,.025 if limb=='front' else .14,.16 if limb=='front' else .18))*scale
    offset.y+=recovery*(.025 if limb=='front' else .070)*scale;offset.z+=recovery*.025*scale
    if foot_q is not None:
        motion=foot_q @ rig.data.bones[f'{limb}_foot.{side}'].matrix_local.to_quaternion().inverted()
        offset=motion @ offset
    hinge=Vector(target)+offset
    err=ik(rig,f'{limb}_upper.{side}',f'{limb}_lower.{side}',name,hinge,(0,1,0) if limb=='front' else (0,-1,0))
    aim(rig,name,Vector(target)-rig.pose.bones[name].head);bpy.context.view_layer.update()
    foot=f'{limb}_foot.{side}';global_rotation(rig,foot,foot_q or rig.data.bones[foot].matrix_local.to_quaternion())
    return err
def gait_quad(rig,scale,d,speed,kind,rm):
    sprint=kind=='Sprint';run=kind=='Run'
    td={('front','L'):0,('front','R'):.12,('hind','R'):.50,('hind','L'):.62} if sprint else {('front','L'):0,('hind','R'):.02,('front','R'):.5,('hind','L'):.52} if run else {('hind','L'):0,('front','L'):.25,('hind','R'):.5,('front','R'):.75}
    def pose(t,seconds):
        curl=math.cos(math.tau*(t-.43)) if sprint else .12*math.sin(math.tau*t)
        bob=(-.070-.030*math.cos(2*math.tau*(t-.15))) if sprint else (-.070+.012*math.cos(2*math.tau*(t-.08))) if run else (-.065+.006*math.cos(2*math.tau*t))
        translate_root(rig,((.003 if run else .007)*scale*math.sin(math.tau*t),-speed*seconds if rm else 0,bob*scale))
        rotate(rig,'pelvis',x=-.075*curl if sprint else 0,y=.012*math.sin(math.tau*t),z=.008*math.cos(math.tau*t))
        rotate(rig,'spine',x=.16*curl if sprint else .014*math.sin(2*math.tau*t),y=-.012*math.sin(math.tau*t))
        displacement(rig,'spine',(0,.045*scale*curl if sprint else 0,0))
        rotate(rig,'chest',x=-.07*curl if sprint else 0,y=.008*math.sin(math.tau*t))
        rotate(rig,'neck',x=-.035*curl if sprint else .015*math.cos(2*math.tau*t),y=-.008*math.sin(math.tau*t))
        rotate(rig,'head',x=-.015*curl if sprint else -.008*math.cos(2*math.tau*t))
        rotate(rig,'tail.01',x=.06*math.sin(math.tau*t-.7),z=.06*math.sin(math.tau*t-.4));rotate(rig,'tail.02',z=.08*math.sin(math.tau*t-1))
        for side in ['L','R']:
            phase=(t-td[('front',side)])%1
            rotate(rig,'scapula.'+side,x=.10*math.sin(math.tau*phase))
            displacement(rig,'scapula.'+side,(0,.030*scale*math.sin(math.tau*phase),.008*scale*math.cos(math.tau*phase)))
        bpy.context.view_layer.update();err=0
        for limb in ['front','hind']:
            stance=(.24 if limb=='front' else .22) if sprint else .44 if run else .66
            for side in ['L','R']:
                p=(t-td[(limb,side)])%1;lift=(.25 if limb=='front' else .30) if sprint else (.105 if limb=='front' else .14) if run else (.065 if limb=='front' else .085)
                y,z,grounded=footpath(p,d,speed,stance,lift*scale);foot=f'{limb}_foot.{side}'
                target=joint_point(rig,foot)+Vector((0,y-speed*seconds if rm else y,z))
                recovery=0 if grounded else math.sin(math.pi*(p-stance)/(1-stance))
                q=Quaternion((1,0,0),.20*recovery) @ rig.data.bones[foot].matrix_local.to_quaternion()
                err=max(err,solve_animal_leg(rig,limb,side,target,scale,recovery,q))
        return err
    return pose,td
def replace_action(rig,old,fn,d=None,speed=None,events=None):
    remove_clip(rig,old['name'])
    a=action(rig,old['name'],d or old['duration_s'],fn,old['loop'],old['speed_mps'] if speed is None else speed,old['root_motion'],old['events'] if events is None else events)
    nla(rig,[a]);return clips[-1]
for owner,rig,scale in [(manifest,human,1)]+[(c,bpy.data.objects[c['skeleton']],scales[c['skeleton']]) for c in manifest['creatures'] if c['gait']=='biped']:
    keyname='npc_clips' if owner is manifest else 'clips';updated=[]
    for old in owner[keyname]:
        iswalk=old['name'].endswith(('_Walk','_Walk_RootMotion'));isrun=old['name'].endswith(('_Run','_Run_RootMotion'))
        if not iswalk and not isrun:updated.append(old);continue
        d=32/30 if iswalk else 22/30;speed=(.70 if iswalk else 1.40)*scale
        fn=gait_biped(rig,scale,d,speed,'Walk' if iswalk else 'Run',old['root_motion'])
        updated.append(replace_action(rig,old,fn,d,speed,[dict(time_s=round(t*d,4),event='foot_contact',foot=side) for t,side in [(0,'L'),(.5,'R')]]))
    owner[keyname]=updated
print('V6_BIPED_GAITS_READY',flush=True)
for creature in manifest['creatures']:
    if creature['gait']!='quadruped':continue
    rig=bpy.data.objects[creature['skeleton']];oldrig=old_quads[rig.name];scale=scales[rig.name];updated=[]
    for old in creature['clips']:
        kind=old['name'].split('_')[2];isgait=kind in ['Walk','Run','Sprint']
        if isgait:
            d=36/30 if kind=='Walk' else 26/30 if kind=='Run' else 24/30
            speed=(.55 if kind=='Walk' else 1.30 if kind=='Run' else 2.10)*scale
            fn,td=gait_quad(rig,scale,d,speed,kind,old['root_motion'])
            events=[dict(time_s=round(when*d,4),event='foot_contact',foot=limb+'.'+side) for (limb,side),when in td.items()]
            updated.append(replace_action(rig,old,fn,d,speed,events));continue
        # Read old FK targets first, then solve the new three-link limbs and bake.
        old_action=bpy.data.actions[old['name']];oldrig.animation_data.action=old_action
        samples=[];n=old['frames'][1]-1
        for frame in range(n+1):
            scene.frame_set(frame+1);bpy.context.view_layer.update()
            torso={p.name:(p.location.copy(),p.rotation_quaternion.copy()) for p in oldrig.pose.bones if not any(s in p.name for s in ['_upper.','_lower.','_foot.'])}
            feet={f'{limb}_foot.{side}':(oldrig.pose.bones[f'{limb}_foot.{side}'].head.copy(),oldrig.pose.bones[f'{limb}_foot.{side}'].matrix.to_quaternion()) for limb in ['front','hind'] for side in ['L','R']}
            samples.append((torso,feet))
        oldrig.animation_data.action=None
        def retarget(t,seconds,values=samples,count=n):
            torso,feet=values[min(count,round(t*count))]
            for name,(location,rotation) in torso.items():rig.pose.bones[name].location=location;rig.pose.bones[name].rotation_quaternion=rotation
            bpy.context.view_layer.update();err=0
            for limb in ['front','hind']:
                for side in ['L','R']:
                    target,q=feet[f'{limb}_foot.{side}'];err=max(err,solve_animal_leg(rig,limb,side,target,scale,0,q))
            return err
        updated.append(replace_action(rig,old,retarget))
    creature['clips']=updated
    print('V6_QUADRUPED_READY',rig.name,flush=True)
# A sparse skeleton control rig is not needed at runtime; all new joints are ordinary keyed bones.
for old in old_quads.values():bpy.data.objects.remove(old,do_unlink=True)
bpy.data.collections.remove(helpers)
checks=[]
for rig,items in [(human,manifest['npc_clips'])]+[(bpy.data.objects[c['skeleton']],c['clips']) for c in manifest['creatures']]:
    deactivate(rig)
    for item in items:
        if not item['root_motion'] or not any(item['name'].endswith(x) for x in ['_Walk_RootMotion','_Run_RootMotion','_Sprint_RootMotion']):continue
        rig.animation_data.action=bpy.data.actions[item['name']];positions=[]
        for frame in range(item['frames'][1]):
            scene.frame_set(frame+1);bpy.context.view_layer.update()
            positions.extend([p.head.copy() for p in rig.pose.bones])
        checks.append(dict(rig=rig.name,clip=item['name'],finite=all(math.isfinite(x) for p in positions for x in p),max_requested_reach_error_m=item['max_reach_error_m']))
    deactivate(rig)
parts={}
for obj in human_meshes:
    t=obj.name.split('_');parts[(t[2].lower(),int(t[3]))]=obj
for preset in manifest['npc_exports']:
    export(human,[parts[(slot,index)] for slot,index in preset['parts'].items() if index>=0],os.path.basename(preset['file']),[bpy.data.actions[c['name']] for c in manifest['npc_clips']])
export(human,human_meshes,'NPC_ModularParts_Library.glb',[bpy.data.actions[c['name']] for c in manifest['npc_clips']])
for creature in manifest['creatures']:
    rig=bpy.data.objects[creature['skeleton']];mesh=bpy.data.objects[creature['id']]
    export(rig,[mesh],os.path.basename(creature['file']),[bpy.data.actions[c['name']] for c in creature['clips']]);reset(rig)
    creature['triangles']=sum(len(p.vertices)-2 for p in mesh.data.polygons)
manifest['art_revision']=6;manifest['natural_revision']=dict(soft_shading=True,rounded_silhouettes=True,knee_loops=True,quadruped_scapula_and_hock=True,quadruped_bones=27,walk_and_run_rebaked=True)
with open(os.path.join(ROOT,'data','animation_manifest.json'),'w',encoding='utf-8') as f:json.dump(manifest,f,ensure_ascii=False,indent=2)
report=dict(art_revision=6,checks=checks,issues=[],weighted_vertices=0,unweighted_vertices=0,bad_weight_sums=0)
for obj in human_meshes+[bpy.data.objects[c['id']] for c in manifest['creatures']]:
    for v in obj.data.vertices:
        report['weighted_vertices']+=1
        if not v.groups:report['unweighted_vertices']+=1
        if abs(sum(g.weight for g in v.groups)-1)>.002:report['bad_weight_sums']+=1
if report['unweighted_vertices'] or report['bad_weight_sums']:report['issues'].append('Skin weights require correction')
if any(not c['finite'] for c in checks):report['issues'].append('Nonfinite bone sample')
with open(os.path.join(ROOT,'data','natural_v6_validation.json'),'w') as f:json.dump(report,f,indent=2)
for col in collection.children:col.hide_viewport=True;col.hide_render=True
for obj in human_meshes:obj.hide_set(True);obj.hide_render=True
for slot,index in manifest['npc_exports'][0]['parts'].items():
    if index>=0:parts[(slot,index)].hide_set(False);parts[(slot,index)].hide_render=False
human.hide_set(False);human.animation_data.action=bpy.data.actions['NPC_Walk'];scene.frame_start=1;scene.frame_end=33;scene.frame_set(1)
bpy.context.view_layer.objects.active=human
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
print('NATURAL_V6_COMPLETE',report['issues'],flush=True)
