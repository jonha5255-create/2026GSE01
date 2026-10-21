"""Character surface / walk / run visual QA for the v6 source."""
import bpy,os,json,math
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT,'source','render_rig_previews.py'),encoding='utf-8') as f:setup=f.read()
exec(setup[:setup.index('# Seven creatures')])
scene.cycles.samples=10
actors=[];labels=[]
for index,c in enumerate(manifest['creatures']):
    rig=bpy.data.objects[c['skeleton']];mesh=bpy.data.objects[c['id']]
    rig.location=((index%4)*3.7,(index//4)*3.6,0);mesh.hide_render=False;rig.hide_render=False
    play(rig,'MOB_%02d_Idle'%c['index']);actors.append(rig)
    labels.append(label('%02d  %s'%(c['index'],c['id'].split('_',3)[-1]),(rig.location.x,rig.location.y-1.8,.02)))
human.location=(11.1,3.6,0);human.hide_render=False
for slot,index in [('Body',0),('Hair',0),('Top',0),('Legs',0),('Feet',0)]:
    for obj in humanparts:
        if obj.name.startswith('SK_NPC_%s_%02d_'%(slot,index)):obj.hide_render=False
play(human,'NPC_Idle');labels.append(label('NPC',(11.1,1.8,.02)))
scene.frame_set(1);aim((15,-18,14),(5.5,1.0,1),18)
render('characters_natural_v6.png',(1800,1150))
for obj in labels:obj.hide_render=True
for rig in actors+[human]:rig.location=(0,0,0)
beast=next(c for c in manifest['creatures'] if c['gait']=='quadruped');animal=bpy.data.objects[beast['skeleton']];mesh=bpy.data.objects[beast['id']]
sequences=[]
def sequence(id,rig,clip,npc=False,side=False):
    if npc:
        show_human(-1,0);aim((4,-5.7,2.2) if not side else (6,-.15,1.45),(0,-.05,.95),2.8)
    else:
        for obj in list(source.all_objects):obj.hide_render=True
        mesh.hide_render=False;animal.hide_render=False
        aim((3.6,-4.8,2.2) if not side else (6,-.15,1.65),(0,-.10,.85),3.75)
    play(rig,clip);rig.location=(0,0,0)
    count=round(bpy.data.actions[clip].frame_range[1])-1;frames=[]
    for i in range(count):
        scene.frame_set(i+1);file='animation_frames/%s_%02d.png'%(id,i);render(file,(480,480));frames.append(file)
    sequences.append(dict(id=id,frames=frames,duration_s=count/30,duration_ms=1000/30,art_revision=6))
sequence('npc_walk_v6',human,'NPC_Walk',npc=True)
sequence('npc_run_v6',human,'NPC_Run',npc=True)
sequence('quadruped_walk_v6',animal,'MOB_%02d_Walk'%beast['index'])
sequence('quadruped_run_v6',animal,'MOB_%02d_Run'%beast['index'])
sequence('npc_run_side_v6',human,'NPC_Run',npc=True,side=True)
sequence('quadruped_run_side_v6',animal,'MOB_%02d_Run'%beast['index'],side=True)
with open(os.path.join(ROOT,'data','preview_sequences.json')) as f:prior=json.load(f)
ids={s['id'] for s in sequences};prior['sequences']=[s for s in prior['sequences'] if s['id'] not in ids]+sequences
with open(os.path.join(ROOT,'data','preview_sequences.json'),'w') as f:json.dump(prior,f,indent=2)
with open(os.path.join(ROOT,'data','natural_v6_previews.json'),'w') as f:json.dump(dict(art_revision=6,sequences=sequences),f,indent=2)
print('NATURAL_V6_PREVIEWS_COMPLETE',flush=True)
