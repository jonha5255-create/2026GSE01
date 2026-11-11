"""Render the three normal attacks at the same playback speed for comparison."""
import bpy
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
catalog = json.loads((ROOT/'data'/'boss_arena_catalog.json').read_text(encoding='utf-8'))
effects = json.loads((ROOT/'data'/'boss_combat_catalog.json').read_text(encoding='utf-8'))['effects']
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Boss_Arenas.blend'))
for spec in catalog['scenes']:
    scene = bpy.data.scenes[spec['arena_id']]
    bpy.context.window.scene=scene
    index=spec['index']
    motion=next(e for e in effects if e['boss_id']==spec['boss_id'] and e['slot']=='normal')
    rig=bpy.data.objects[spec['boss_nodes'][0]]
    rig.animation_data.action=bpy.data.actions[motion['motion']]
    scene.render.resolution_x,scene.render.resolution_y=380,300
    scene.cycles.samples=5
    camera=scene.camera
    camera.location=[(-5,-9,5.7),(6,-8,5.8),(4,-8,6.0)][index]
    focus=Vector([(0,-.15,2.7),(0,-1.2,1.9),(0,0,3.7)][index])
    camera.rotation_euler=(focus-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=[10,12.2,12.4][index]
    for sample,frame in enumerate(range(1,50,4)):
        scene.frame_set(min(frame,motion['motion_last_frame']))
        scene.render.filepath=str(ROOT/'previews'/'animation_frames'/f'boss_normal_{index}_{sample:02d}.png')
        bpy.ops.render.render(write_still=True)
print('BOSS_ATTACK_MOTION_PREVIEW_COMPLETE',flush=True)
