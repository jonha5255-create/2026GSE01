"""Refit existing weapon meshes and sockets to the character's hands."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
blend = ROOT / 'Yokai_Weapons.blend'
catalog_path = ROOT / 'data' / 'yokai_weapon_catalog.json'
catalog_data = json.loads(catalog_path.read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(blend))
scene = bpy.context.scene
M = {m.name.removeprefix('MAT_YK_'): m for m in bpy.data.materials if m.name.startswith('MAT_YK_')}
parts, catalog = [], []
recipe = (ROOT / 'source' / 'author_yokai_weapons.py').read_text(encoding='utf-8')
exec(recipe[recipe.index('def mat('):recipe.index('defs=[')])

for index, sockets in {
    0: {'grip': [0, 0, .128]},
    1: {'grip': [0, -.085, .026], 'support_hand': [0, -.127, .38]},
    2: {'grip': [0, 0, 0], 'cuff_back': [0, 0, -.16], 'knuckle': [0, .105, .14]},
    3: {'grip_L': [-.22, -.06, 0], 'grip_R': [.22, -.06, 0]},
    4: {'grip_L': [-.11, 0, -.12], 'grip_R': [.11, 0, -.12]},
}.items():
    family = catalog_data['families'][index]
    form = family['forms']['Weapon']
    form['sockets'].update(sockets)
    if index not in (1, 2):
        continue
    for stage, name in enumerate(form['parts']):
        old = bpy.data.objects[name]
        old_data = old.data
        weapon(family, stage)
        fresh = finish(family, 'Weapon', stage)
        for obj in bpy.data.objects:
            if obj.type == 'MESH' and obj.data == old_data:
                obj.data = fresh.data
        bpy.data.objects.remove(fresh, do_unlink=True)
        old.data.name = name + '_Mesh'
        entry = next(e for e in catalog_data['meshes'] if e['mesh'] == name)
        entry['triangles'] = len(old.data.polygons)
    for variant in form['variants']:
        bpy.ops.object.select_all(action='DESELECT')
        objects = [bpy.data.objects[name] for name in variant['nodes']]
        for obj in objects:
            obj.hide_set(False)
            obj.hide_render = False
            obj.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(ROOT / variant['file']), export_format='GLB',
                                  use_selection=True, export_yup=True, export_animations=False)
        for obj in objects:
            obj.hide_set(True)
            obj.hide_render = True
catalog_data['grip_revision'] = 2
catalog_path.write_text(json.dumps(catalog_data, ensure_ascii=False, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
print('YOKAI_GRIP_MODELS_COMPLETE', flush=True)
