"""Render updated monsters in rest and locomotion poses."""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT, 'source', 'render_rig_previews.py'), encoding='utf-8') as file:
    recipe = file.read()
exec(recipe[:recipe.index('# Seven creatures')])
scene.cycles.samples = 12
for mode, fraction in [('Idle', .05), ('Walk', .32), ('Run', .61)]:
    labels = []
    for i, creature in enumerate(manifest['creatures']):
        rig = bpy.data.objects[creature['skeleton']]
        mesh = bpy.data.objects[creature['id']]
        rig.hide_render = mesh.hide_render = False
        rig.location = ((i % 4) * 2.85, (i // 4) * 3.15, 0)
        name = f"MOB_{creature['index']:02d}_{mode}"
        play(rig, name)
        clip = bpy.data.actions[name]
        frame = 1 + round((clip.frame_range[1] - 1)*fraction)
        # Freeze each rig at the same phase despite different cycle durations.
        scene.frame_set(frame)
        poses = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}
        rig.animation_data.action = None
        for bone in rig.pose.bones:
            bone.matrix_basis = poses[bone.name]
        labels.append(label(creature['id'].split('_', 3)[-1],
                            (rig.location.x, rig.location.y-1.35, .025), .16))
    aim((12.5,-15,9.0), (4.0,1.2,.80), 12.3)
    render('monsters_connected_'+mode.lower()+'.png', (1740,1040))
    for obj in labels:
        bpy.data.objects.remove(obj, do_unlink=True)
print('CONNECTED_PREVIEWS_COMPLETE', flush=True)
