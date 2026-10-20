"""Full-cycle run previews from two angles; no frame skipping of foot contacts."""
import bpy,os,json,math
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
npc_only=os.environ.get('ART_RUN_PREVIEW_NPC_ONLY')=='1'
with open(os.path.join(ROOT,'source','render_rig_previews.py'),encoding='utf-8') as f:setup=f.read()
exec(setup[:setup.index('# Seven creatures')])
scene.cycles.samples=10
with open(os.path.join(ROOT,'data','preview_sequences.json')) as f:prior=json.load(f)
sequences=[]
beast=next(c for c in manifest['creatures'] if c['gait']=='quadruped')
animal=bpy.data.objects[beast['skeleton']];animal_mesh=bpy.data.objects[beast['id']]
def sequence(id,rig,clip,side=False,npc=False):
    if npc_only and not npc:return
    if npc:
        show_human(-1,0)
        aim((4.0,-5.7,2.2) if not side else (6,-.15,1.45),(0,-.05,.95),2.8)
    else:
        for o in list(source.all_objects):o.hide_render=True
        animal_mesh.hide_render=False;animal.hide_render=False
        aim((3.6,-4.8,2.2) if not side else (6,-.15,1.65),(0,-.1,.85),3.75)
    rig.location=(0,0,0);play(rig,clip)
    action=bpy.data.actions[clip];n=round(action.frame_range[1])-1;frames=[]
    count=n if clip.endswith(('_Run','_Sprint')) else 12
    for i in range(count):
        frame=1+i*n/count;scene.frame_set(int(frame),subframe=frame-int(frame));path='animation_frames/%s_%02d.png'%(id,i)
        render(path,(480,480));frames.append(path)
    sequences.append(dict(id=id,frames=frames,duration_ms=n/30/count*1000,duration_s=n/30,art_revision=5))
sequence('npc_run',human,'NPC_Run',npc=True)
sequence('npc_run_side',human,'NPC_Run',side=True,npc=True)
sequence('quadruped_run',animal,'MOB_%02d_Run'%beast['index'])
sequence('quadruped_run_side',animal,'MOB_%02d_Run'%beast['index'],side=True)
sequence('quadruped_sprint',animal,'MOB_%02d_Sprint'%beast['index'])
# Walking uses the revised bind pose / corrected four-beat sequence too.
sequence('npc_walk',human,'NPC_Walk',npc=True)
sequence('quadruped_walk',animal,'MOB_%02d_Walk'%beast['index'])
ids={s['id'] for s in sequences}
prior['sequences']=[s for s in prior['sequences'] if s['id'] not in ids]+sequences
with open(os.path.join(ROOT,'data','preview_sequences.json'),'w') as f:json.dump(prior,f,indent=2)
with open(os.path.join(ROOT,'data','running_preview_sequences.json'),'w') as f:json.dump(dict(art_revision=5,sequences=sequences),f,indent=2)
print('RUNNING_V5_PREVIEWS_COMPLETE',flush=True)
