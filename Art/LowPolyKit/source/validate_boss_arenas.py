"""Check that exported arenas and bosses contain only their own scene's assets."""
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
catalog = json.loads((ROOT/'data'/'boss_arena_catalog.json').read_text(encoding='utf-8'))


def load_glb(relative_path):
    raw = (ROOT/relative_path).read_bytes()
    magic,version,length,size,kind = struct.unpack_from('<4sIIII',raw)
    assert (magic,version,length,kind)==(b'glTF',2,len(raw),0x4E4F534A), relative_path
    return json.loads(raw[20:20+size])


def mesh_nodes(glb):
    return {n['name'] for n in glb['nodes'] if 'mesh' in n}


def triangle_count(glb):
    count=0
    for mesh in glb['meshes']:
        for prim in mesh['primitives']:
            accessor=glb['accessors'][prim['attributes']['POSITION']]
            assert all(math.isfinite(x) for x in accessor['min']+accessor['max'])
            count+=glb['accessors'][prim['indices']]['count']//3
    return count


checks=[]
boss_files={}
for spec in catalog['scenes']:
    arena=load_glb(spec['arena_file'])
    assert mesh_nodes(arena)==set(spec['arena_nodes']), (spec['arena_file'],mesh_nodes(arena))
    assert not arena.get('animations') and not arena.get('skins'),spec['arena_file']
    markers={node['name'].split('.')[0] for node in arena['nodes'] if 'mesh' not in node}
    assert markers==set(spec['placement_blender']), markers
    boss=load_glb(spec['boss_file'])
    boss_files[spec['boss_id']]=boss
    assert mesh_nodes(boss)==set(spec['boss_nodes'][1:]), (spec['boss_file'],mesh_nodes(boss))
    assert all('skin' in node for node in boss['nodes'] if 'mesh' in node)
    clips={clip['name'] for clip in boss['animations']}
    assert clips==set(spec['animations']), (spec['boss_file'],clips)
    assert len(clips)==(14 if spec['index']==1 else 12)+int(bool(catalog.get('combat_revision')))
    arena_triangles=triangle_count(arena)
    boss_triangles=triangle_count(boss)
    assert arena_triangles==spec['arena_triangles'] and boss_triangles==spec['boss_triangles']
    checks.append(dict(arena=spec['arena_id'],boss=spec['boss_id'],arena_triangles=arena_triangles,
                       boss_triangles=boss_triangles,clips=len(clips),mesh_nodes=len(mesh_nodes(arena))))


def duration(glb,clip):
    inputs=[glb['accessors'][s['input']] for s in clip['samplers']]
    return max(a['max'][0] for a in inputs)-min(a['min'][0] for a in inputs)


combat_checks=[]
if catalog.get('combat_revision'):
    combat=json.loads((ROOT/'data'/'boss_combat_catalog.json').read_text(encoding='utf-8'))
    fps=combat['fps']
    for entry in combat['effects']:
        boss=boss_files[entry['boss_id']]
        action=next(a for a in boss['animations'] if a['name']==entry['motion'])
        assert abs(duration(boss,action)-(entry['motion_last_frame']-1)/fps)<.002, entry['motion']
        fx=load_glb(entry['file'])
        assert mesh_nodes(fx)=={f"FX_BOSS_{entry['id']}_{part}" for part in ('Telegraph','Release','Debris')}
        assert len(fx['animations'])==1
        assert abs(duration(fx,fx['animations'][0])-(entry['effect_last_frame']-1)/fps)<.002,entry['file']
        combat_checks.append(entry['file'])
    for entry in combat['devices']:
        device=load_glb(entry['file'])
        assert len(mesh_nodes(device))==1 and not device.get('animations') and not device.get('skins')
        combat_checks.append(entry['file'])
    hit=load_glb(combat['hit_confirm']['file'])
    assert len(hit['animations'])==1 and len(mesh_nodes(hit))==1
    combat_checks.append(combat['hit_confirm']['file'])
count=6+len(combat_checks)
(ROOT/'data'/'boss_arena_validation.json').write_text(
    json.dumps(dict(files=count,checks=checks,combat_files=combat_checks),indent=2))
print('BOSS_ARENA_GLB_CHECKS',count,checks)
