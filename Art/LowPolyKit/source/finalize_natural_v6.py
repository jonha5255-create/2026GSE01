"""Publish current v6 preview aliases and validation metadata inside the art kit."""
import os,json,shutil
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read(name):
    with open(os.path.join(ROOT,'data',name),encoding='utf-8') as f:return json.load(f)
def write(name,value):
    with open(os.path.join(ROOT,'data',name),'w',encoding='utf-8') as f:json.dump(value,f,ensure_ascii=False,indent=2)
manifest=read('animation_manifest.json');geometry=read('natural_v6_validation.json');contact=read('natural_v6_contact_validation.json')
checks=[dict(rig=c['rig'],action=c['clip'],max_ankle_target_error_m=c['max_contact_error_m']) for c in contact['checks']]
report=dict(art_revision=6,unweighted_vertices=geometry['unweighted_vertices'],bad_weight_sums=geometry['bad_weight_sums'],weighted_vertices=geometry['weighted_vertices'],foot_checks=checks,npc_actions=len(manifest['npc_clips']),creature_actions=sum(len(c['clips']) for c in manifest['creatures']),rig_count=8,quadruped_bones=27,game_integration=False,issues=geometry['issues']+contact['issues'])
write('rig_validation.json',report)
monster=read('monster_rig_validation.json');monster.update(art_revision=6,foot_checks=[c for c in checks if c['rig']!=manifest['npc_rig']],quadruped_bones=27,validation_source='data/natural_v6_validation.json',contact_validation='data/natural_v6_contact_validation.json')
write('monster_rig_validation.json',monster)
for base in ['npc_walk','npc_run','quadruped_walk','quadruped_run','npc_run_side','quadruped_run_side']:
    shutil.copyfile(os.path.join(ROOT,'previews',base+'_v6.gif'),os.path.join(ROOT,'previews',base+'.gif'))
shutil.copyfile(os.path.join(ROOT,'previews','characters_natural_v6.png'),os.path.join(ROOT,'previews','creature_lineup.png'))
print('NATURAL_V6_HANDOFF_COMPLETE',report['issues'])
