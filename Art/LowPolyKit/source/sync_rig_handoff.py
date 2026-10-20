"""Synchronize current monster validation and remove obsolete monster previews."""
import os,json,re
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read(name):
    with open(os.path.join(ROOT,'data',name),encoding='utf-8') as f:return json.load(f)
def write(name,data):
    with open(os.path.join(ROOT,'data',name),'w',encoding='utf-8') as f:json.dump(data,f,ensure_ascii=False,indent=2)
manifest=read('animation_manifest.json');new=read('monster_rig_validation.json');old=read('rig_validation.json')
old['foot_checks']=[c for c in old['foot_checks'] if c['rig']==manifest['npc_rig']]+new['foot_checks']
old['art_revision']=4;old['biped_monsters']=4;old['quadruped_monsters']=3
old['max_ik_reach_error_m']=max(new['max_ik_reach_error_m'],max(c['max_reach_error_m'] for c in manifest['npc_clips']))
write('rig_validation.json',old)
new_previews=['quadruped_walk.gif','quadruped_run.gif','quadruped_turn.gif']
if all(os.path.isfile(os.path.join(ROOT,'previews',p)) for p in new_previews):
    preview_root=os.path.abspath(os.path.join(ROOT,'previews'))
    for folder in [preview_root,os.path.join(preview_root,'animation_frames')]:
        for name in os.listdir(folder):
            if re.fullmatch(r'wolf_(walk|run|turn)(\.gif|_\d{2}\.png)',name):
                path=os.path.abspath(os.path.join(folder,name))
                if not os.path.normcase(path).startswith(os.path.normcase(preview_root)+os.sep):raise RuntimeError('Unexpected target')
                os.remove(path)
print('HANDOFF_SYNC_COMPLETE')
