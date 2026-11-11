"""Fit the existing NPC attack clips and render their weapon contact poses."""
import bpy
import json
import math
import os
from pathlib import Path
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = str(ROOT / 'rigged')
manifest_path = ROOT / 'data' / 'animation_manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
catalog = json.loads((ROOT / 'data' / 'yokai_weapon_catalog.json').read_text(encoding='utf-8'))
recipe = (ROOT / 'source' / 'author_rigs_and_creatures.py').read_text(encoding='utf-8')
exec(recipe[recipe.index('def reset('):recipe.index('def skin(')])
blend = ROOT / 'Characters_Rigged.blend'


def orient_hand(rig, side, delta):
    name = 'hand.' + side
    global_rotation(rig, name, delta @ rig.data.bones[name].matrix_local.to_quaternion())
    bpy.context.view_layer.update()


def forward_frame(direction):
    """Rigid rotation from -Y forward and +Z up to the requested aim."""
    forward = Vector(direction).normalized()
    right = Vector((0, 0, 1)).cross(forward).normalized()
    up = forward.cross(right)
    return Matrix((right, -forward, up)).transposed().to_quaternion()


def place_arm(rig, side, target, delta=None):
    sign = -1 if side == 'L' else 1
    error = ik(rig, 'upper_arm.'+side, 'forearm.'+side, 'hand.'+side,
               target, (sign*.42, .34, -1))
    if delta is None:
        wrist = rig.pose.bones['hand.'+side].head
        elbow = rig.pose.bones['forearm.'+side].head
        delta = forward_frame(wrist - elbow)
    orient_hand(rig, side, delta)
    return error


def interpolate(keys, t):
    for (a, p, v), (b, q, w) in zip(keys, keys[1:]):
        if t <= b:
            u = (t-a)/(b-a)
            u = u*u*(3-2*u)
            return Vector(p).lerp(Vector(q),u), Vector(v).lerp(Vector(w),u).normalized()
    return Vector(keys[-1][1]), Vector(keys[-1][2])


READY = ((.25,-.24,.91), (.47,-.79,.48))
SWORD = [(0,*READY), (.30,(.37,.04,1.16),(.64,.49,.59)),
         (.46,(.12,-.39,1.02),(-.43,-.89,.13)), (.66,(-.15,-.24,.84),(-.89,-.28,-.36)),
         (1,*READY)]
HEAVY = [(0,*READY), (.32,(.19,.04,1.40),(.30,.65,.70)),
         (.48,(.16,-.40,1.05),(.10,-.98,.15)), (.66,(.06,-.32,.75),(.08,-.76,-.65)),
         (1,*READY)]


def fit(rig, name, phase, seconds):
    if name in ('NPC_Attack_Sword', 'NPC_Attack_SwordHeavy'):
        target, blade = interpolate(HEAVY if name.endswith('Heavy') else SWORD, phase)
        # The sword's authored +Z axis runs through the middle of its hilt.
        wrist = Vector((0,0,1)).rotation_difference(blade)
        return max(place_arm(rig, 'R', target, wrist),
                   place_arm(rig, 'L', (-.30,-.11,.84), Quaternion()))
    if name == 'NPC_Attack_DualPistol':
        # Both pistols aim downrange. The currently playable rifle uses the
        # right-hand aim from this clip and solves its support hand at runtime.
        recoil = math.exp(-((seconds-.05)/.038)**2)
        for shot in (.25, .45, .65):
            recoil += math.exp(-((seconds-shot)/.038)**2)
        errors = []
        for side, sign in [('L',-1), ('R',1)]:
            wrist = forward_frame((sign*.055,-1,.016+.12*recoil))
            errors.append(place_arm(rig, side, (sign*.205,-.359+.025*recoil,1.074), wrist))
        return max(errors)
    errors = []
    for side, sign in [('L',-1), ('R',1)]:
        if name == 'NPC_Attack_Claw':
            peak = .46 if side == 'R' else .64
            stroke = max(0, 1-abs(phase-peak)/.22)
            stroke = stroke*stroke*(3-2*stroke)
            target = (sign*(.285-.19*stroke),-.235-.178*stroke,.94+.08*stroke)
            errors.append(place_arm(rig, side, target))
        else:
            closure = math.sin(math.pi*phase)**2
            target = (sign*(.26-.15*closure),-.27-.08*closure,1.01+.03*closure)
            blade = Vector((sign*(.54-.76*closure),-.80,.34)).normalized()
            delta = forward_frame(blade)
            errors.append(place_arm(rig, side, target, delta))
    return max(errors)


NAMES = ['NPC_Attack_Sword','NPC_Attack_SwordHeavy','NPC_Attack_DualPistol',
         'NPC_Attack_Claw','NPC_Attack_Scissors']


def bake():
    global scene
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    scene = bpy.context.scene
    collection = bpy.data.collections['05_RIGGED_CHARACTERS']
    collection.hide_viewport = False
    visibility = [(c,c.hide_viewport) for c in collection.children]
    for c,_ in visibility:
        c.hide_viewport = False
    bpy.context.view_layer.update()
    rig = bpy.data.objects[manifest['npc_rig']]
    mesh_parts = [o for o in collection.all_objects if o.name.startswith('SK_NPC_') and o.type=='MESH']
    hidden = [(o,o.hide_get()) for o in mesh_parts]
    rig.hide_set(False)
    for track in rig.animation_data.nla_tracks:
        track.mute = True
    results = []
    for meta in manifest['npc_clips']:
        name = meta['name']
        if name not in NAMES:
            continue
        rig.animation_data.action = bpy.data.actions[name]
        last = meta['frames'][1]
        max_error = 0
        previous = {}
        for frame in range(1, last+1):
            scene.frame_set(frame)
            max_error = max(max_error,fit(rig,name,(frame-1)/(last-1),(frame-1)/30))
            for side in ['L','R']:
                for joint in ['upper_arm','forearm','hand']:
                    bone = rig.pose.bones[joint+'.'+side]
                    rotation = bone.rotation_quaternion.copy()
                    if bone.name in previous and rotation.dot(previous[bone.name]) < 0:
                        rotation.negate()
                    bone.rotation_quaternion = rotation
                    previous[bone.name] = rotation
                    bone.keyframe_insert('rotation_quaternion',frame=frame,group=bone.name)
        assert max_error < .035, (name,max_error)
        results.append(dict(clip=name,frames=last,max_arm_reach_error_m=round(max_error,6)))
    clips = [bpy.data.actions[c['name']] for c in manifest['npc_clips']]
    indexed = {(o.name.split('_')[2].lower(),int(o.name.split('_')[3])):o for o in mesh_parts}
    for preset in manifest['npc_exports']:
        objects = [indexed[(slot,number)] for slot,number in preset['parts'].items() if number>=0]
        export(rig,objects,Path(preset['file']).name,clips)
    export(rig,mesh_parts,'NPC_ModularParts_Library.glb',clips)
    for o,value in hidden:
        o.hide_set(value)
    for c,value in visibility:
        c.hide_viewport = value
    rig.animation_data.action = bpy.data.actions['NPC_Walk']
    scene.frame_set(1)
    manifest['weapon_grip_revision'] = 2
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'data'/'weapon_grip_validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    print('GRIP_CLIPS_COMPLETE',results,flush=True)


def attachment(rig, side, grip, family):
    hand = rig.pose.bones['hand.'+side]
    rotation = hand.matrix.to_quaternion() @ hand.bone.matrix_local.to_quaternion().inverted()
    align = Matrix.Rotation(0 if family==0 else math.pi/2,4,'X')
    scale = [.8,.7,.72,.72,.72][family]
    mirror = -scale if family==2 and side=='L' else scale
    return Matrix.Translation(hand.head) @ rotation.to_matrix().to_4x4() @ align @ Matrix.Diagonal(
        (mirror,scale,scale,1)) @ Matrix.Translation(-Vector(grip))


def preview():
    # The stage helper loads the updated character file in a separate process.
    stage_code = (ROOT/'source'/'render_rig_previews.py').read_text(encoding='utf-8')
    stage_ns = {'__file__': __file__}
    exec(stage_code[:stage_code.index('# Seven creatures')],stage_ns)
    scene,rig,stage = [stage_ns[key] for key in ['scene','human','stage']]
    chosen = {k:v for k,v in manifest['npc_exports'][0]['parts'].items() if v>=0 and k!='prop'}
    body = [o for o in stage_ns['humanparts']
            if chosen.get(o.name.split('_')[2].lower())==int(o.name.split('_')[3])]
    setup = [(0,'NPC_Attack_Sword','Muk - Sword'),(1,'NPC_Attack_DualPistol','Soul Rifle'),
             (2,'NPC_Attack_Claw','Claw Cuffs'),(3,'NPC_Attack_DualPistol','Soryeong - Dual Pistols'),
             (4,'NPC_Attack_Scissors','Mes - Split Blades')]
    wanted = {n for i,_,_ in setup for n in catalog['families'][i]['forms']['Weapon']['variants'][3]['nodes']}
    with bpy.data.libraries.load(str(ROOT/'Yokai_Weapons.blend')) as (src,dst):
        dst.objects = [n for n in src.objects if n in wanted]
    weapon_objects = {o.name:o for o in dst.objects}
    rig.location = (0,0,0)
    for row,phase in enumerate([0,.46]):
        for col,(family,clip,label) in enumerate(setup):
            offset = Vector(((col-2)*2.05,(1-row)*2.8,0))
            form = catalog['families'][family]['forms']['Weapon']
            sockets = form['sockets']
            stage_ns['play'](rig,clip)
            action = bpy.data.actions[clip]
            frame = 1+round((action.frame_range[1]-1)*phase)
            if family==1 and row:frame=3
            scene.frame_set(frame)
            if family==1:
                # Reproduce the current game's wrist target, then solve the
                # support wrist at the newly authored foregrip contact.
                chest = rig.pose.bones['chest'].head.copy()
                right = rig.pose.bones['hand.R']
                delta = right.matrix.to_quaternion() @ right.bone.matrix_local.to_quaternion().inverted()
                place_arm(rig,'R',chest+Vector((.03,-.10,-.13)),delta)
                mount = attachment(rig,'R',sockets['grip'],family)
                support = mount @ Vector(sockets['support_hand'])
                left = rig.pose.bones['hand.L']
                delta = left.matrix.to_quaternion() @ left.bone.matrix_local.to_quaternion().inverted()
                miss = place_arm(rig,'L',support,delta)
                assert miss < .006, ('rifle support',miss)
            depsgraph = bpy.context.evaluated_depsgraph_get()
            for obj in body:
                data = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),depsgraph=depsgraph)
                copy = bpy.data.objects.new('Preview '+obj.name,data)
                stage.objects.link(copy)
                copy.matrix_world = Matrix.Translation(offset) @ obj.matrix_world
            for side in (['R','L'] if family>=2 else ['R']):
                grip = sockets['grip_'+side] if family>=3 else sockets['grip']
                mount = attachment(rig,side,grip,family)
                assert ((mount @ Vector(grip))-rig.pose.bones['hand.'+side].head).length < .0001
                for name in form['variants'][3]['nodes']:
                    if family>=3 and not name.endswith('_'+side):
                        continue
                    copy=bpy.data.objects.new('Mounted '+name,weapon_objects[name].data)
                    stage.objects.link(copy)
                    copy.matrix_world = Matrix.Translation(offset) @ mount
            stage_ns['label'](label if not row else 'Attack pose',offset+Vector((0,-.73,.025)),.118)
    scene.cycles.samples=16
    stage_ns['aim']((6.3,-11.8,7.9),(0,1.0,.73),11.7)
    stage_ns['render']('yokai_weapon_grips.png',(2000,1100))
    print('GRIP_PREVIEW_COMPLETE',flush=True)


if os.environ.get('ART_GRIP_PREVIEW')=='1':
    preview()
else:
    bake()
