"""Evaluate baked FK foot positions in the new 19/18/27-bone rigs."""
import bpy,os,json,math
from mathutils import Vector,Quaternion
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:m=json.load(f)
source=bpy.data.collections['05_RIGGED_CHARACTERS'];source.hide_viewport=False
for c in source.children:c.hide_viewport=False
bpy.context.view_layer.update()
for o in list(source.all_objects):o.hide_set(False)
rigs=[bpy.data.objects[m['npc_rig']]]+[bpy.data.objects[c['skeleton']] for c in m['creatures']]
for rig in rigs:
    rig.animation_data.action=None
    for track in rig.animation_data.nla_tracks:track.mute=True
def smooth(t):t=max(0,min(1,t));return t*t*(3-2*t)
checks=[]
for name,clips,quad in [(m['npc_rig'],m['npc_clips'],False)]+[(c['skeleton'],c['clips'],c['gait']=='quadruped') for c in m['creatures']]:
    rig=bpy.data.objects[name];scale=rig.data.bones['pelvis'].head_local.z/(.88 if quad else .7)
    for item in clips:
        kind=item['name'].split('_')[2] if item['name'].startswith('MOB_') else item['name'].split('_')[1]
        if not item['root_motion'] or kind not in ['Walk','Run','Sprint']:continue
        if quad:
            td={('front','L'):0,('front','R'):.12,('hind','R'):.50,('hind','L'):.62} if kind=='Sprint' else {('front','L'):0,('hind','R'):.02,('front','R'):.5,('hind','L'):.52} if kind=='Run' else {('hind','L'):0,('front','L'):.25,('hind','R'):.5,('front','R'):.75}
            feet=[(f'{limb}_foot.{side}',when,(.24 if limb=='front' else .22) if kind=='Sprint' else .44 if kind=='Run' else .66) for (limb,side),when in td.items()]
        else:feet=[('foot.L',0,.36 if kind=='Run' else .62),('foot.R',.5,.36 if kind=='Run' else .62)]
        rig.animation_data.action=bpy.data.actions[item['name']];n=item['frames'][1]-1;d=item['duration_s'];speed=item['speed_mps'];errors=[]
        for frame in range(n):
            t=frame/n;bpy.context.scene.frame_set(frame+1);bpy.context.view_layer.update()
            for bone,when,stance in feet:
                p=(t-when)%1
                if not .02<p<stance-.02:continue
                expected=rig.data.bones[bone].head_local.copy();expected.y+=d*speed*(p-stance/2)-speed*frame/30
                if not quad:
                    angle=-.13*(1-smooth(p/.08))+.24*smooth((p-(stance-.12))/.12)
                    sole=[(Quaternion((1,0,0),angle) @ Vector((0,y,-.17))).z for y in [-.19,.11]]
                    expected.z+=max(0,-min(sole)-.17)*scale
                errors.append((rig.pose.bones[bone].head-expected).length)
        checks.append(dict(rig=name,clip=item['name'],contact_samples=len(errors),max_contact_error_m=max(errors or [0])))
        rig.animation_data.action=None
report=dict(art_revision=6,checks=checks,max_contact_error_m=max(c['max_contact_error_m'] for c in checks),issues=[])
if report['max_contact_error_m']>.003:report['issues'].append('Contact target error exceeds 3 mm')
with open(os.path.join(ROOT,'data','natural_v6_contact_validation.json'),'w') as f:json.dump(report,f,indent=2)
print('V6_CONTACT_CHECK',report['max_contact_error_m'],report['issues'])
