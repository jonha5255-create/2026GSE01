"""Visual inspection renders for the authored keyframes."""
import bpy, os, math, json
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
monsters_only=os.environ.get('ART_PREVIEW_MONSTERS_ONLY')=='1'
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
scene=bpy.context.scene
for c in scene.collection.children:c.hide_render=True
source=bpy.data.collections['05_RIGGED_CHARACTERS'];source.hide_render=False;source.hide_viewport=False
for c in source.children:c.hide_render=False;c.hide_viewport=False
bpy.context.view_layer.update()
print('PREVIEW_COLLECTIONS_READY',flush=True)
for o in list(source.all_objects):o.hide_render=True;o.hide_set(False)
print('PREVIEW_OBJECTS_READY',flush=True)
stage=bpy.data.collections.new('06_PREVIEW_STAGE');scene.collection.children.link(stage)
bpy.context.view_layer.active_layer_collection=bpy.context.view_layer.layer_collection.children[stage.name]
scene.render.engine='CYCLES';scene.cycles.samples=12;scene.cycles.use_denoising=True
scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.12,.16,.19,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.55
bpy.ops.object.light_add(type='AREA',location=(2,-4,7));bpy.context.object.data.energy=1000;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=6
bpy.ops.object.light_add(type='AREA',location=(-4,2,5));bpy.context.object.data.energy=750;bpy.context.object.data.size=5
bpy.ops.object.camera_add();cam=bpy.context.object;scene.camera=cam;cam.data.type='ORTHO'
bpy.ops.mesh.primitive_plane_add(size=200);plane=bpy.context.object
mat=bpy.data.materials.new('PreviewFloor');mat.diffuse_color=(.10,.14,.16,1);plane.data.materials.append(mat);plane.location.z=-.025
mat.use_nodes=True;mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.045,.065,.08,1)
mat.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=.94
def aim(pos,target,scale):cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=scale
def render(path,res=(640,640)):
    scene.render.resolution_x,scene.render.resolution_y=res;scene.render.resolution_percentage=100
    scene.render.filepath=os.path.join(ROOT,'previews',path);bpy.ops.render.render(write_still=True)
def play(rig,name):
    rig.animation_data.action=bpy.data.actions[name]
    for t in rig.animation_data.nla_tracks:t.mute=True
def label(body,p,size=.22):
    data=bpy.data.curves.new('Label','FONT');data.body=body;data.size=size;data.align_x='CENTER'
    o=bpy.data.objects.new('Label',data);stage.objects.link(o);o.location=p;o.rotation_euler=(math.radians(55),0,0)
    m=bpy.data.materials.get('MAT_paper');o.data.materials.append(m);return o
human=bpy.data.objects['RIG_NPC_Humanoid_v1']
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:manifest=json.load(f)
humanparts=[o for o in source.all_objects if o.type=='MESH' and o.name.startswith('SK_NPC_')]
def show_human(prop=-1,top=0):
    for o in list(source.all_objects):o.hide_render=True
    for slot,idx in [('Body',0),('Hair',0),('Top',top),('Legs',0),('Feet',0),('Prop',prop)]:
        for o in humanparts:
            if o.name.startswith('SK_NPC_%s_%02d_'%(slot,idx)):o.hide_render=False
    human.hide_render=False;human.location=(0,0,0)
# Seven creatures in a single measured lineup (original NPC provides a height reference).
labels=[]
for i,c in enumerate(manifest['creatures']):
    rig=bpy.data.objects[c['skeleton']];mesh=bpy.data.objects[c['id']]
    rig.hide_render=False;mesh.hide_render=False;rig.location=((i%4)*3.5,(i//4)*3.8,0)
    play(rig,'MOB_%02d_Idle'%c['index']);labels.append(label('%02d  %s'%(c['index'],c['id'].split('_',3)[-1]),(rig.location.x,rig.location.y-1.85,.02)))
scene.frame_set(1);aim((14,-17,14),(5,1,1),17)
render('creature_lineup.png',(1800,1150))
for o in labels:o.hide_render=True
for c in manifest['creatures']:bpy.data.objects[c['skeleton']].location=(0,0,0)
os.makedirs(os.path.join(ROOT,'previews','animation_frames'),exist_ok=True)
sequences=[]
def sequence(id,rig,action,mesh=None,prop=-1,top=0):
    if monsters_only and rig==human:
        last=round(bpy.data.actions[action].frame_range[1])
        sequences.append(dict(id=id,frames=['animation_frames/%s_%02d.png'%(id,i) for i in range(12)],duration_ms=round((last-1)/30/12*1000)))
        return
    if mesh:
        for o in list(source.all_objects):o.hide_render=True
        mesh.hide_render=False;rig.hide_render=False
        aim((3.6,-4.8,2.2),(0,-.10,.85),3.8)
    else:
        show_human(prop,top);aim((3.4,-5.4,2.4),(0,-.05,.95),2.75)
    play(rig,action);last=round(bpy.data.actions[action].frame_range[1]);frames=[]
    for i in range(12):
        frame=1+(last-1)*i/12;scene.frame_set(int(frame),subframe=frame-int(frame))
        file='animation_frames/%s_%02d.png'%(id,i);render(file,(480,480));frames.append(file)
    sequences.append(dict(id=id,frames=frames,duration_ms=round((last-1)/30/12*1000)))
sequence('npc_walk',human,'NPC_Walk')
sequence('npc_run',human,'NPC_Run')
beast=next(c for c in manifest['creatures'] if c['gait']=='quadruped');wrig=bpy.data.objects[beast['skeleton']];wmesh=bpy.data.objects[beast['id']]
sequence('quadruped_walk',wrig,'MOB_%02d_Walk'%beast['index'],wmesh)
sequence('quadruped_run',wrig,'MOB_%02d_Run'%beast['index'],wmesh)
sequence('npc_sword',human,'NPC_Attack_Sword',prop=3,top=1)
sequence('npc_bow',human,'NPC_Attack_Bow',prop=0,top=2)
sequence('npc_jump',human,'NPC_Jump')
sequence('quadruped_turn',wrig,'MOB_%02d_Turn_L90'%beast['index'],wmesh)
# Three key poses for every weapon attack, including dual-wield gear.
weapons=[('sword','NPC_Attack_Sword',3),('spear','NPC_Attack_Spear',1),('bow','NPC_Attack_Bow',0),('dual_pistols','NPC_Attack_DualPistol',4),('claws','NPC_Attack_Claw',5),('scissors','NPC_Attack_Scissors',6),('unarmed','NPC_Attack_Unarmed',-1)]
weapon_frames=[]
for name,clip,prop in weapons:
    show_human(prop,2 if name=='bow' else 1);play(human,clip);aim((3.4,-5.4,2.4),(0,-.05,.95),2.8)
    last=round(bpy.data.actions[clip].frame_range[1]);row=[]
    for i,t in enumerate([.22,.46,.82]):
        scene.frame_set(1+round((last-1)*t));file='animation_frames/weapon_%s_%d.png'%(name,i)
        if not monsters_only:render(file,(320,360))
        row.append(file)
    weapon_frames.append(dict(weapon=name,poses=row))
with open(os.path.join(ROOT,'data','preview_sequences.json'),'w') as f:json.dump(dict(sequences=sequences,weapons=weapon_frames),f,indent=2)
print('ANIMATION_PREVIEWS_COMPLETE',flush=True)
