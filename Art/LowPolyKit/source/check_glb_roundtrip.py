"""Import exported skins back into a fresh Blender scene and evaluate animation."""
import bpy,os,json,math
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
results=[]
for file,clip,frame in [('NPC_LOOK_000.glb','NPC_Attack_Bow',23),('SK_MOB_04_IronHound.glb','MOB_04_Run',9)]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT,'rigged',file))
    rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
    tracks=rig.animation_data.nla_tracks if rig.animation_data else []
    for track in tracks:track.mute=True
    matches=[a for a in bpy.data.actions if a.name.startswith(clip)]
    if not matches:raise RuntimeError('Missing imported clip '+clip)
    rig.animation_data.action=matches[0];bpy.context.scene.frame_set(frame)
    deps=bpy.context.evaluated_depsgraph_get();positions=[]
    for o in bpy.data.objects:
        if o.type!='MESH' or not any(m.type=='ARMATURE' for m in o.modifiers):continue
        evaluated=o.evaluated_get(deps);mesh=evaluated.to_mesh()
        positions.extend([evaluated.matrix_world @ v.co for v in mesh.vertices]);evaluated.to_mesh_clear()
    bounds=[[min(v[i] for v in positions),max(v[i] for v in positions)] for i in range(3)]
    finite=all(math.isfinite(x) for p in positions for x in p)
    sane=max(hi-lo for lo,hi in bounds)<5 and finite
    results.append(dict(file=file,clip=matches[0].name,armature_bones=len(rig.data.bones),evaluated_bounds=bounds,finite=finite,sane_bounds=sane))
    if not sane:raise RuntimeError('Invalid roundtrip deformation')
with open(os.path.join(ROOT,'data','glb_roundtrip_validation.json'),'w') as f:json.dump(results,f,indent=2)
print('GLB_ROUNDTRIP_OK',results)
