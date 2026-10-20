"""Organize the rig source and write the modular skin handoff catalog."""
import bpy,os,json
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
source=bpy.data.collections['05_RIGGED_CHARACTERS']
with open(os.path.join(ROOT,'data','animation_manifest.json'),encoding='utf-8') as f:data=json.load(f)
for c in data['creatures']:
    child=bpy.data.collections.get(c['id']) or bpy.data.collections.new(c['id'])
    if child.name not in source.children:source.children.link(child)
    for name in [c['id'],c['skeleton']]:
        obj=bpy.data.objects[name]
        for col in list(obj.users_collection):col.objects.unlink(obj)
        child.objects.link(obj);obj.hide_set(False)
    child.hide_viewport=True;child.hide_render=True
catalog=[]
for o in source.all_objects:
    if o.type=='MESH' and o.name.startswith('SK_NPC_'):
        segments=o.name.split('_');catalog.append(dict(slot=segments[2].lower(),index=int(segments[3]),mesh_node=o.name,library='rigged/NPC_ModularParts_Library.glb',skin='RIG_NPC_Humanoid_v1'))
with open(os.path.join(ROOT,'data','rigged_part_catalog.json'),'w') as f:json.dump(dict(schema_version=1,parts=catalog,none_index=-1),f,indent=2)
rig=bpy.data.objects['RIG_NPC_Humanoid_v1'];rig.hide_set(False)
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);bpy.context.view_layer.objects.active=rig
rig.animation_data.action=bpy.data.actions['NPC_Idle']
bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=61;bpy.context.scene.frame_set(1)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_location=(0,0,.95)
            area.spaces.active.region_3d.view_distance=3.5
            area.spaces.active.region_3d.view_rotation=(Vector((3,-5,2.5))-Vector((0,0,.95))).to_track_quat('Z','Y')
            area.spaces.active.shading.color_type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'Characters_Rigged.blend'))
print('RIG_SCENE_ORGANIZED',len(catalog))
