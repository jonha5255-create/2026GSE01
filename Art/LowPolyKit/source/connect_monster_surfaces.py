"""Fuse monster body shells and blend skin weights while preserving existing rigs."""
import bpy
import bmesh
import json
import math
import os
from pathlib import Path
from mathutils import Matrix, Vector, geometry
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
OUT = str(ROOT / 'rigged')
manifest_path = ROOT / 'data' / 'animation_manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
source = ROOT / 'Characters_Rigged.blend'
backup = ROOT / 'revisions' / 'Characters_before_connected_surfaces.blend'
bpy.ops.wm.open_mainfile(filepath=str(backup if backup.exists() else source))
scene = bpy.context.scene
collection = bpy.data.collections['05_RIGGED_CHARACTERS']
visibility = [(c, c.hide_viewport) for c in [collection, *collection.children]]
for col, _ in visibility:
    col.hide_viewport = False
bpy.context.view_layer.update()
recipe = (ROOT / 'source' / 'author_rigs_and_creatures.py').read_text(encoding='utf-8')
exec(recipe[recipe.index('def bone_rig('):recipe.index('clips=[];rigs=[];')])
recipe = (ROOT / 'source' / 'natural_character_pass_v6.py').read_text(encoding='utf-8')
exec(recipe[recipe.index('def components('):recipe.index('def remesh_legs(')])


def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def apply(obj, kind, **settings):
    activate(obj)
    mod = obj.modifiers.new(kind, kind)
    for key, value in settings.items():
        setattr(mod, key, value)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def normalized(weights):
    top = sorted(weights.items(), key=lambda pair: -pair[1])[:4]
    top = [(bone, w) for bone, w in top if w > .001]
    total = sum(w for _, w in top)
    return {bone: w / total for bone, w in top}


def add_joint_ball(target, point, radii):
    matrix = Matrix.Diagonal((*radii, 1))
    matrix.translation = point
    bmesh.ops.create_uvsphere(target, u_segments=14, v_segments=10, radius=1, matrix=matrix)


def build(creature):
    obj = bpy.data.objects[creature['id']]
    rig = bpy.data.objects[creature['skeleton']]
    old = obj.data
    old.calc_loop_triangles()
    names = [group.name for group in obj.vertex_groups]
    if 'neck' not in names:
        names.append('neck')
    weights = [{g.group: g.weight for g in v.groups} for v in old.vertices]
    points = [v.co.copy() for v in old.vertices]
    mats = list(old.materials)
    mi = {m.name.removeprefix('MAT_'): i for i, m in enumerate(mats)}
    by_vertex = {i: p.material_index for p in old.polygons for i in p.vertices}
    idx = creature['index']
    quad = idx >= 4
    base = ['navy', 'navy', 'oni_red', 'gunmetal', 'gunmetal', 'flesh', 'void'][idx]
    core = set()
    for part in components(old):
        first = part[0]
        bone = names[max(weights[first], key=weights[first].get)]
        material = mats[by_vertex[first]].name.removeprefix('MAT_')
        center = sum((points[i] for i in part), Vector()) / len(part)
        if quad:
            body = bone in {'pelvis', 'spine', 'chest', 'neck'} and material == base
            leg = any(bone.startswith(x) for x in ('front_', 'hind_', 'hock.', 'carpus.')) and (
                '_foot.' not in bone or material == base)
            head = bone == 'head' and len(part) in (42, 162) and material in {'armor', 'mask', 'void'}
            include = body or leg or head or bone.startswith('tail.') and len(part) >= 120
        else:
            limb = bone.startswith(('upper_arm.', 'forearm.', 'thigh.', 'shin.'))
            shoulder = bone.startswith('upper_arm.') and center.z > rig.data.bones[bone].head_local.z
            include = (limb and material != 'core_red' and not (shoulder and material in {'armor', 'gunmetal'} and len(part) == 42))
            include |= bone.startswith('foot.') or bone.startswith('hand.') and len(part) == 42
            include |= bone == 'pelvis' or bone == 'chest' and material in {'card_side', 'uniform', 'red_dark', 'gunmetal'}
        if include:
            core.update(part)

    original_faces = [tuple(p.vertices) for p in old.polygons]
    reference = [tuple(t.vertices) for t in old.loop_triangles if t.vertices[0] in core]
    reference_mats = [t.material_index for t in old.loop_triangles if t.vertices[0] in core]
    tree = BVHTree.FromPolygons(points, reference, all_triangles=True)
    ids = sorted(core)
    remap = {i: j for j, i in enumerate(ids)}
    core_data = bpy.data.meshes.new('Connected volume')
    core_data.from_pydata([points[i] for i in ids], [],
                         [[remap[i] for i in p] for p in original_faces if p[0] in core])
    bm = bmesh.new()
    bm.from_mesh(core_data)
    factor = rig.data.bones['head'].head_local.z / (1.12 if quad else 1.48)
    if quad:
        for side in ['L', 'R']:
            for leg, hinge in [('front', 'carpus'), ('hind', 'hock')]:
                for name, radius in [(f'{leg}_lower.{side}', .079), (f'{hinge}.{side}', .062),
                                     (f'{leg}_foot.{side}', .069)]:
                    joint = rig.data.bones[name].head_local
                    add_joint_ball(bm, joint, (radius*factor,)*3)
    else:
        neck = rig.data.bones['neck']
        add_joint_ball(bm, neck.head_local.lerp(neck.tail_local, .25),
                       tuple(x * factor for x in (.080, .078, .142)))
    bm.to_mesh(core_data)
    bm.free()
    volume = bpy.data.objects.new('Connected volume', core_data)
    scene.collection.objects.link(volume)
    activate(volume)
    core_data.remesh_voxel_size = .019 if quad else .016
    core_data.use_remesh_preserve_volume = True
    bpy.ops.object.voxel_remesh()
    apply(volume, 'SMOOTH', factor=.82, iterations=8)
    # Keep the outer connected body and remove small voxel remnants.
    islands = components(volume.data)
    largest = max(islands, key=len)
    if len(largest) < .98 * len(volume.data.vertices):
        raise RuntimeError(f'{obj.name}: separated body region {[len(p) for p in islands]}')
    bm = bmesh.new()
    bm.from_mesh(volume.data)
    bm.verts.ensure_lookup_table()
    keep = set(largest)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in keep], context='VERTS')
    bm.to_mesh(volume.data)
    bm.free()
    goal = 5600 if quad else 4200
    apply(volume, 'DECIMATE', ratio=min(1, goal / sum(len(p.vertices)-2 for p in volume.data.polygons)))
    apply(volume, 'TRIANGULATE')
    skin = []
    for vertex in volume.data.vertices:
        point, _, tri, _ = tree.find_nearest(vertex.co)
        a, b, c = reference[tri]
        # Barycentric interpolation of the original rig weights on the nearest surface.
        blend = geometry.barycentric_transform(point, points[a], points[b], points[c],
                                                Vector((1,0,0)), Vector((0,1,0)), Vector((0,0,1)))
        result = {}
        for index, fraction in zip((a,b,c), blend):
            for group, value in weights[index].items():
                result[group] = result.get(group, 0) + max(0, fraction)*value
        if not quad and vertex.co.z > rig.data.bones['neck'].head_local.z:
            z = rig.data.bones['neck'].head_local.z
            fraction = min(1, max(0, (vertex.co.z-z)/(.10*factor)))
            result[names.index('neck')] = fraction
        skin.append(normalized(result))
    adjacency = [[] for _ in skin]
    for edge in volume.data.edges:
        a,b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    for _ in range(6):
        updated = []
        for i, near in enumerate(adjacency):
            result = {bone: value*.45 for bone, value in skin[i].items()}
            for j in near:
                for bone, value in skin[j].items():
                    result[bone] = result.get(bone, 0) + .55*value/len(near)
            updated.append(normalized(result))
        skin = updated
    new_points = [v.co.copy() for v in volume.data.vertices]
    faces = [tuple(p.vertices) for p in volume.data.polygons]
    material_indices = []
    for poly in volume.data.polygons:
        _, _, tri, _ = tree.find_nearest(poly.center)
        material = reference_mats[tri]
        if quad:
            # A continuous base surface avoids random ivory flecks at fused armor
            # seams. The dorsal plates, teeth and spikes keep their own materials.
            material = mi[base]
            if idx in (4, 5) and poly.center.y < -.91 * factor:
                material = mi['armor' if idx == 4 else 'mask']
        elif idx == 3:
            material = mi[base]
        elif idx == 2 and mats[material].name == 'MAT_red_dark':
            if poly.center.z > .20 * factor and poly.center.z < 1.19 * factor:
                material = mi[base]
        material_indices.append(material)
    core_vertices = len(new_points)
    core_triangles = len(faces)
    remaining = [i for i in range(len(points)) if i not in core]
    rest_map = {i: core_vertices+j for j, i in enumerate(remaining)}
    new_points.extend(points[i] for i in remaining)
    skin.extend(weights[i] for i in remaining)
    preserved = []
    for polygon in old.polygons:
        if polygon.vertices[0] not in core:
            faces.append(tuple(rest_map[i] for i in polygon.vertices))
            material_indices.append(polygon.material_index)
            preserved.append(polygon.use_smooth)
    mesh = bpy.data.meshes.new(obj.name+'_Connected')
    mesh.from_pydata(new_points, [], faces)
    for material in mats:
        mesh.materials.append(material)
    mesh.update()
    for p in mesh.polygons:
        p.material_index = material_indices[p.index]
        p.use_smooth = p.index < core_triangles or preserved[p.index-core_triangles]
    obj.data = mesh
    obj.vertex_groups.clear()
    groups = [obj.vertex_groups.new(name=name) for name in names]
    for i, mapping in enumerate(skin):
        for group, value in mapping.items():
            groups[group].add([i], value, 'REPLACE')
    obj['connected_body_v7'] = True
    bpy.data.objects.remove(volume, do_unlink=True)
    body_islands = sum(part[0] < core_vertices for part in components(mesh))
    assert body_islands == 1
    assert all(v.groups and abs(sum(g.weight for g in v.groups)-1) < .002 for v in mesh.vertices)
    report = dict(id=obj.name, core_vertices=core_vertices, core_triangles=core_triangles,
                  body_islands=body_islands, total_triangles=len(mesh.polygons), clips=len(creature['clips']))
    # Sample all exported clips at the extreme and intermediate poses.
    max_edge_ratio = 0
    edges = [(e.vertices[0], e.vertices[1]) for e in mesh.edges if max(e.vertices) < core_vertices]
    lengths = [(new_points[a]-new_points[b]).length for a,b in edges]
    for clip in creature['clips']:
        rig.animation_data.action = bpy.data.actions[clip['name']]
        for frame in sorted({1, *range(1, clip['frames'][1]+1, 3), clip['frames'][1]}):
            scene.frame_set(frame)
            evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices
            assert len(evaluated) == len(new_points)
            positions = [v.co.copy() for v in evaluated[:core_vertices]]
            assert all(math.isfinite(x) for point in positions for x in point)
            max_edge_ratio = max(max_edge_ratio, max((positions[a]-positions[b]).length / length
                                 for (a,b),length in zip(edges,lengths) if length > .002))
    report['max_sampled_body_edge_stretch'] = round(max_edge_ratio, 3)
    rig.animation_data.action = None
    reset(rig)
    print('BODY_READY', report, flush=True)
    return obj, rig, report


results = []
for creature in manifest['creatures']:
    obj = bpy.data.objects[creature['id']]
    rig = bpy.data.objects[creature['skeleton']]
    obj.hide_set(False)
    rig.hide_set(False)
    rig.animation_data.action = None
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    reset(rig)
    results.append(build(creature))
for creature, (obj, rig, report) in zip(manifest['creatures'], results):
    export(rig, [obj], Path(creature['file']).name, [bpy.data.actions[c['name']] for c in creature['clips']])
    creature['triangles'] = report['total_triangles']
    creature['connected_body'] = True
for col, hidden in visibility:
    col.hide_viewport = hidden
scene.frame_set(1)
manifest['art_revision'] = 7
manifest['monster_surface_revision'] = 'continuous weighted body with separately modeled accessories'
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
(ROOT/'data'/'monster_connected_validation.json').write_text(json.dumps([r for _,_,r in results], indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(source))
print('CONNECTED_MONSTERS_COMPLETE', flush=True)
