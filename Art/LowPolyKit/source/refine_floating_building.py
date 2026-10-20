"""Incremental Blender model correction; main recipe already contains this fix."""
import bpy, os, json, math, random
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
scene=bpy.context.scene;lib=bpy.data.collections['01_MESH_LIBRARY']
lib.hide_viewport=False;lib.hide_render=False
with open(os.path.join(ROOT,'data','mesh_catalog.json'),encoding='utf-8') as f:catalog=json.load(f)
name='SM_ENV_Building_16_FloatingBuilding'
old=bpy.data.objects[name];olddata=old.data
preview=[o for o in bpy.data.objects if o!=old and o.type=='MESH' and o.data==olddata]
old.name=name+'_old'
catalog['meshes']=[i for i in catalog['meshes'] if i['mesh']!=name]
with open(os.path.join(ROOT,'source','author_city_buildings.py'),encoding='utf-8') as f:recipe=f.read()
exec(recipe[recipe.index('colors='):recipe.index('# Preserve existing')])
start=recipe.index("h=shell(4,3.8,2,'plaster',roofed=False)")
end=recipe.index("done(16,'FloatingBuilding'",start)
end=recipe.index('\n',end)
exec(recipe[start:end])
new=bpy.data.objects[name]
for o in preview:o.data=new.data
bpy.data.objects.remove(old,do_unlink=True)
lib.hide_viewport=True;lib.hide_render=True
with open(os.path.join(ROOT,'data','mesh_catalog.json'),'w',encoding='utf-8') as f:json.dump(catalog,f,ensure_ascii=False,indent=2)
with open(os.path.join(ROOT,'data','building_regions.json'),encoding='utf-8') as f:regions=json.load(f)
regions['buildings']=[i for i in catalog['meshes'] if i['slot']=='building']
with open(os.path.join(ROOT,'data','building_regions.json'),'w',encoding='utf-8') as f:json.dump(regions,f,ensure_ascii=False,indent=2)
with open(os.path.join(ROOT,'data','asset_validation.json')) as f:validation=json.load(f)
validation['total_triangles']=sum(i['triangles'] for i in catalog['meshes'])
with open(os.path.join(ROOT,'data','asset_validation.json'),'w') as f:json.dump(validation,f,indent=2)
show=bpy.data.collections['03_BUILDING_CONCEPT_BOARD_V2']
for o in show.objects:o.hide_render=o.location.y!=75
cam=scene.camera;cam.location=(37,39,26);cam.rotation_euler=(Vector((12,75,4.8))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=39
scene.render.resolution_x=1800;scene.render.resolution_y=1050
scene.render.filepath=os.path.join(ROOT,'previews','buildings_06_Distortion.png');bpy.ops.render.render(write_still=True)
for o in show.objects:o.hide_render=False
cam.location=(80,-105,120);cam.rotation_euler=(Vector((12,42,3))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=118
scene.render.resolution_x=1800;scene.render.resolution_y=2400
scene.render.filepath=os.path.join(ROOT,'previews','building_overview.png');bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
print('FLOATING_MODEL_REFINED')
