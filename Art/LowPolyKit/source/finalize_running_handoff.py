"""Package current v5 contact reports and cache-safe GIF names."""
import os,json,shutil
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read(name):
    with open(os.path.join(ROOT,'data',name),encoding='utf-8') as f:return json.load(f)
def write(name,value):
    with open(os.path.join(ROOT,'data',name),'w',encoding='utf-8') as f:json.dump(value,f,ensure_ascii=False,indent=2)
m=read('animation_manifest.json');run=read('run_revision_validation.json');rig=read('rig_validation.json')
fresh={c['clip']:dict(rig=c['rig'],action=c['clip'],max_ankle_target_error_m=c['max_planted_ankle_error_m']) for c in run['checks']}
for old in rig['foot_checks']:
    if old['action'] not in fresh:fresh[old['action']]=old
rig.update(art_revision=5,foot_checks=list(fresh.values()),npc_actions=len(m['npc_clips']),creature_actions=sum(len(c['clips']) for c in m['creatures']),run_revision_checks='data/run_revision_validation.json')
write('rig_validation.json',rig)
monster=read('monster_rig_validation.json');monster.update(art_revision=5,foot_checks=[c for c in fresh.values() if c['rig']!=m['npc_rig']],creature_actions=rig['creature_actions'])
write('monster_rig_validation.json',monster)
preview=read('preview_sequences.json')
write('running_preview_sequences.json',dict(art_revision=5,sequences=[s for s in preview['sequences'] if s.get('art_revision')==5]))
for name in ['npc_run','npc_run_side','quadruped_run','quadruped_run_side','quadruped_sprint']:
    path=os.path.join(ROOT,'previews',name+'.gif')
    shutil.copyfile(path,os.path.join(ROOT,'previews',name+'_v5.gif'))
print('RUN_HANDOFF_FINALIZED',rig['npc_actions']+rig['creature_actions'])
