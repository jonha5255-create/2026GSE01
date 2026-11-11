"""Bake arena boss motions and export effect and device assets."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Quaternion, Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'boss_arenas'
FX = OUT / 'combat'
FX.mkdir(exist_ok=True)
CATALOG = ROOT / 'data' / 'boss_arena_catalog.json'
catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Boss_Arenas.blend'))
geometry = (ROOT/'source'/'author_boss_arenas.py').read_text(encoding='utf-8')
MATERIALS = {m.name.removeprefix('MAT_BOSS_'):m for m in bpy.data.materials if m.name.startswith('MAT_BOSS_')}
exec(geometry[geometry.index('class MeshBatch:'):geometry.index('def pavement(')])
exec(geometry[geometry.index('def export('):geometry.index('def marker(')])
exec(geometry[geometry.index('def aim('):geometry.index('def lighting(')])
aim_camera = aim
rig_code = (ROOT/'source'/'author_rigs_and_creatures.py').read_text(encoding='utf-8')
exec(rig_code[rig_code.index('def reset('):rig_code.index('def action_curves(')])
exec(rig_code[rig_code.index('def action_curves('):rig_code.index('def key(')])

FPS = 30
UNIT = 44/640
PATTERNS = [
    [('AltarSlam','제단 강타','circle',.85,64,0),
     ('HornCharge','뿔 돌진','line',1.10,26,300),
     ('BloodSeal','핏빛 파동','outside_circle',2.60,78,0)],
    [('Bite','포식견 물어뜯기','circle',.65,53,0),
     ('VenomJet','독액 분사','line',.95,33,280),
     ('PoisonPool','독웅덩이','circle',1.15,64,0)],
    [('StoneSlam','거신 내려찍기','circle',1.05,72,0),
     ('Fissure','지면 균열','line',1.30,35,300),
     ('CoreOverload','핵 폭주','arena',8.0,0,0)],
]
THEMES = [
    ('scarlet','ember','brick','basalt','SealStone'),
    ('toxic','toxic','pipe','slate','DrainValve'),
    ('cyan','cyan','concrete','charcoal','CoreDevice'),
]
DEVICES = [[(320,432),(222,301),(414,273)],
           [(246,396),(395,357),(315,215)],
           [(227,360),(408,381),(342,214)]]


def ease(t):
    t = max(0,min(t,1))
    return t*t*(3-2*t)


def envelope(frame, release, last, drive_frames):
    drive_start = release-drive_frames
    wind = ease((frame-1)/max(1,drive_start-1))
    strike = ease((frame-drive_start)/drive_frames)
    recovery = ease((frame-release)/max(1,last-release))
    return wind*(1-strike), strike*(1-recovery)


def set_world_rotation(rig, name, rotation):
    global_rotation(rig,name,rotation @ rig.data.bones[name].matrix_local.to_quaternion())


def pose_hound(rig, pattern, wind, hit, frame):
    bite = pattern=='Bite'
    jet = pattern=='VenomJet'
    shift = .075*wind-.15*hit if bite else .075*wind+.025*hit
    drop = -.078*wind+.034*hit if bite else -.095*wind-.080*hit
    translate_root(rig,(0,shift,drop))
    rotate(rig,'pelvis',x=-.12*wind+.14*hit if bite else -.035*wind)
    rotate(rig,'spine',x=.10*wind-.23*hit if bite else .10*wind-.05*hit)
    rotate(rig,'chest',x=.17*wind+.10*hit if bite else -.05*wind+.08*hit)
    rotate(rig,'neck',x=-.27*wind+.34*hit if bite else -.28*wind+.10*hit)
    rotate(rig,'head',x=-.09*wind+.19*hit if bite else .20*wind-.13*hit)
    rotate(rig,'jaw',x=.61*wind+.035*hit if bite else .18*wind+(.42 if jet else .56)*hit)
    rotate(rig,'tail.01',x=-.32*wind+.32*hit,z=.13*wind-.20*hit)
    rotate(rig,'tail.02',z=.24*wind-.30*hit)
    bpy.context.view_layer.update()
    for side,sign in [('L',-1),('R',1)]:
        for limb,hinge_base in [('front','carpus'),('hind','hock')]:
            foot=f'{limb}_foot.{side}'
            hinge=f'{hinge_base}.{side}'
            target=rig.data.bones[foot].head_local.copy()
            target.x += sign*((.029 if bite else .075)*wind+(.062 if bite else .083)*hit)
            if bite and limb=='front':
                target.y -= (.22 if side=='R' else .13)*hit
                target.z += (.10 if side=='R' else .056)*wind
            offset=rig.data.bones[hinge].head_local-rig.data.bones[foot].head_local
            ik(rig,f'{limb}_upper.{side}',f'{limb}_lower.{side}',hinge,target+offset,
               (0,1,0) if limb=='front' else (0,-1,0))
            aim(rig,hinge,target-rig.pose.bones[hinge].head)
            bpy.context.view_layer.update()
            set_world_rotation(rig,foot,Quaternion())
    if not bite:
        rotate(rig,'head',x=.20*wind-.13*hit,z=.045*hit*math.sin(frame*.47))


def pose_biped(rig, pattern, wind, hit):
    sweep = pattern=='AltarSlam'
    charge = pattern=='HornCharge'
    slam = pattern=='StoneSlam'
    fissure = pattern=='Fissure'
    yaw = (.55*wind-.76*hit if sweep else -.28*wind+.28*hit if fissure else 0)
    pitch = ((-.07*wind+.22*hit) if sweep else (.29*wind+.58*hit) if charge
             else (-.28*wind+.54*hit) if slam else (-.14*wind+.42*hit) if fissure
             else (-.17*wind+.13*hit))
    root_x = .075*wind-.086*hit if sweep else .040*wind-.025*hit if fissure else 0
    root_y = .065*wind-(.22 if charge else .095 if slam else .069)*hit
    root_z = (-.15*wind-.13*hit if charge else -.034*wind-.18*hit if slam
              else -.068*wind-.093*hit)
    translate_root(rig,(root_x,root_y,root_z))
    rotate(rig,'pelvis',x=pitch*.18,z=yaw*.35)
    rotate(rig,'spine',x=pitch*.69,z=yaw*.63)
    rotate(rig,'chest',x=pitch*.22,z=yaw*.22)
    rotate(rig,'head',x=.28*hit+.16*wind if charge else -pitch*.34,z=-yaw*.24)
    bpy.context.view_layer.update()
    for side,sign in [('L',-1),('R',1)]:
        foot=rig.data.bones['foot.'+side].head_local.copy()
        foot.x += sign*((.075 if slam else .045)*wind+(.085 if slam else .059)*hit)
        foot.y -= (.22 if charge and side=='R' else .075 if side=='R' else -.035)*hit
        ik(rig,'thigh.'+side,'shin.'+side,'foot.'+side,foot,(0,-1,0))
        set_world_rotation(rig,'foot.'+side,Quaternion((0,0,1),sign*.18*(wind+hit)))
        shoulder=rig.pose.bones['upper_arm.'+side].head.copy()
        if sweep:
            # The right fist crosses the torso while the left arm guards the ribs.
            offset=((.14+.22*wind-.44*hit,-.11+.12*wind-.24*hit,-.30+.33*wind+.13*hit)
                    if side=='R' else (-.15-.025*wind,-.10-.10*hit,-.29+.14*wind+.09*hit))
        elif charge:
            offset=(sign*(.14+.08*wind-.04*hit),-.13+.22*wind+.42*hit,-.30+.06*wind+.09*hit)
        elif slam:
            offset=(sign*(.16-.045*wind),-.13-.12*wind-.21*hit,-.30+.66*wind-.07*hit)
        elif fissure:
            offset=((.15+.13*wind-.04*hit,-.13+.22*wind-.31*hit,-.30+.44*wind-.015*hit)
                    if side=='R' else (-.16-.21*wind,-.13-.06*hit,-.30+.09*wind+.03*hit))
        elif pattern=='BloodSeal':
            offset=(sign*(.15-.13*wind+.26*hit),-.13-.22*wind-.04*hit,-.30+.27*wind+.32*hit)
        else:
            offset=(sign*(.15+.19*wind+.21*hit),-.13-.10*wind+.13*hit,-.30+.54*wind+.36*hit)
        ik(rig,'upper_arm.'+side,'forearm.'+side,'hand.'+side,shoulder+Vector(offset),
           (sign,.33,-.4))
        set_world_rotation(rig,'hand.'+side,Quaternion((1,0,0),-.26*wind+.54*hit))
    bpy.context.view_layer.update()


DRIVE_FRAMES = {'AltarSlam':6,'HornCharge':4,'Bite':3,'VenomJet':5,
                'StoneSlam':7,'Fissure':6,'BloodSeal':7,'PoisonPool':5,'CoreOverload':9}


def pose_attack(rig, index, pattern, frame, release, last):
    reset(rig)
    wind,hit=envelope(frame,release,last,DRIVE_FRAMES[pattern])
    if index==1:
        pose_hound(rig,pattern,wind,hit,frame)
    else:
        pose_biped(rig,pattern,wind,hit)


def bake_attack(scene, rig, name, index, pattern, release, last):
    for track in list(rig.animation_data.nla_tracks):
        if track.name==name:
            rig.animation_data.nla_tracks.remove(track)
    prior = bpy.data.actions.get(name)
    if prior:
        bpy.data.actions.remove(prior)
    action = bpy.data.actions.new(name)
    action.use_fake_user=True
    rig.animation_data.action=action
    for frame in range(1,last+1):
        scene.frame_set(frame)
        pose_attack(rig,index,pattern,frame,release,last)
        for bone in rig.pose.bones:
            bone.keyframe_insert('rotation_quaternion',frame=frame,group=bone.name)
            if bone.name=='root':
                bone.keyframe_insert('location',frame=frame,group=bone.name)
    for curve in action_curves(action):
        for point in curve.keyframe_points:
            point.interpolation='LINEAR'
    action.pose_markers.new('Release').frame=release
    rig.animation_data.action=None
    track=rig.animation_data.nla_tracks.new()
    track.name=name
    track.strips.new(name,1,action)
    track.mute=True
    return action


def customize_gait(scene,rig,index,track):
    action=track.strips[0].action
    rig.animation_data.action=action
    first,last=(round(x) for x in action.frame_range)
    run='_Run' in track.name
    for frame in range(first,last+1):
        scene.frame_set(frame)
        t=(frame-first)/(last-first)
        wave=math.sin(math.tau*t)
        targets = [('head',.043 if run else .025),('neck',.055 if run else .031)]
        if index==1:
            targets.append(('tail.02',.14 if run else .09))
        for name,strength in targets:
            p=rig.pose.bones[name]
            axis=(0,0,1) if name.startswith('tail') else (1,0,0)
            p.rotation_quaternion @= Quaternion(axis,strength*wave)
            p.keyframe_insert('rotation_quaternion',frame=frame,group=name)
    rig.animation_data.action=None


def beam_strip(batch,a,b,width,material,z=.06):
    d=Vector((b[0]-a[0],b[1]-a[1]))
    d=Vector((-d.y,d.x)).normalized()*width/2
    batch.add([(a[0]+d.x,a[1]+d.y,z),(b[0]+d.x,b[1]+d.y,z),
               (b[0]-d.x,b[1]-d.y,z),(a[0]-d.x,a[1]-d.y,z)],[(0,1,2,3)],material)


def effect_geometry(pattern,shape,radius,reach,index):
    bright,accent,dark,_,_=THEMES[index]
    warning,impact,particles=MeshBatch(),MeshBatch(),MeshBatch()
    if shape=='line':
        for side in (-1,1):
            beam_strip(warning,(side*radius,0),(side*radius,-reach),.11,bright)
        for y in (-reach*.20,-reach*.45,-reach*.70,-reach*.94):
            for side in (-1,1):
                beam_strip(warning,(side*radius*.56,y+.65),(0,y),.10,accent)
        last=(0,0)
        for step in range(1,19):
            target=(radius*.36*math.sin(step*2.2),-reach*step/18)
            beam_strip(impact,last,target,.43 if index==2 else .27,bright,.11)
            if index==2:
                branch=(target[0]+radius*.62*math.sin(step*1.7),target[1]-.43)
                beam_strip(impact,target,branch,.11,accent,.10)
            last=target
        for step in range(1,15):
            y=-reach*step/15
            x=radius*.68*math.sin(step*2.1)
            particles.beam((x,y,.07),(x*.9,y-.70,.32+.1*(step%3)),.13,accent,.015,5)
        if index==1:
            for side in (-1,0,1):
                impact.beam((side*.14,-3.75,2.72),(side*radius*.52,-reach,.095),.19,bright,.11,6)
    else:
        outer=radius if radius else 19
        safe=shape=='outside_circle'
        warning.band((0,0,.055),outer,.14,bright)
        if safe or shape=='arena':
            for ring in (outer+2.3,outer+5.0,outer+8.4) if safe else (5,11,16):
                warning.band((0,0,.057),ring,.115,bright)
        if shape=='arena':
            corners=[(-21.5,-21.5),(21.5,-21.5),(21.5,21.5),(-21.5,21.5)]
            for a,b in zip(corners,corners[1:]+corners[:1]):
                beam_strip(warning,a,b,.16,bright)
        for i in range(24):
            a=i*math.tau/24
            r=outer+(.31 if safe else -.23)
            x,y=r*math.cos(a),r*math.sin(a)
            warning.box((x,y,.059),(.28,.081,.02),accent,a)
        for scale in (1,.72,.39):
            r=outer*scale if not safe else outer+(1-scale)*12
            impact.band((0,0,.085),r,.24 if scale==1 else .105,bright)
        for i in range(20):
            a=i*math.tau/20
            r=outer*(.65+.28*math.sin(i*2.7)) if not safe else outer+1+i%3
            x,y=r*math.cos(a),r*math.sin(a)
            particles.beam((x,y,.07),(x*1.05,y*1.05,.44+.13*(i%4)),.15,dark,.018,5)
        if pattern=='Bite':
            for s in (-1,1):
                impact.band((0,-radius*.79,1.6),radius*.58,.27,bright,
                            start=(0 if s==1 else math.pi),length=1.0,segments=16)
        if pattern=='PoisonPool':
            impact.beam((0,0,.015),(0,0,.026),outer,'water',sides=42)
            for i in range(8):
                a=i*2.4
                impact.band((outer*.62*math.cos(a),outer*.62*math.sin(a),.105),.38+(i%3)*.21,.055,bright,segments=22)
        if pattern=='CoreOverload':
            for i in range(8):
                a=i*math.tau/8
                beam_strip(impact,(0,0),(outer*math.cos(a),outer*math.sin(a)),.21,bright,.14)
    return warning,impact,particles


def fx_keys(obj, name, frames):
    for frame,scale,z in frames:
        obj.scale=scale if isinstance(scale,tuple) else (scale,)*3
        obj.location.z=z
        obj.keyframe_insert('scale',frame=frame)
        obj.keyframe_insert('location',frame=frame)
    action=obj.animation_data.action
    for curve in action_curves(action):
        for key in curve.keyframe_points:
            key.interpolation='LINEAR'
    track=obj.animation_data.nla_tracks.new()
    track.name=name
    track.strips.new(name,1,action)
    obj.animation_data.action=None


def make_device(scene,index,group):
    glow,accent,stone,base,name=THEMES[index]
    batch=MeshBatch()
    batch.beam((0,0,0),(0,0,.18),.70,base,end=.58,sides=8)
    batch.box((0,0,.50),(.66,.63,.70),stone,taper=.78)
    if index==0:
        batch.beam((0,0,.72),(0,0,1.24),.29,glow,end=.10,sides=6)
        batch.box((0,-.292,.53),(.23,.047,.28),accent,math.pi/4)
    elif index==1:
        batch.beam((0,0,.49),(0,0,1.0),.18,base,sides=8)
        batch.band((0,0,1.04),.43,.10,accent,segments=16)
        for side in [-1,1]:
            batch.box((0,0,1.04),(.82,.071,.09),stone,(side+1)*math.pi/4)
    else:
        for side in [-1,1]:
            batch.box((side*.29,0,1.06),(.21,.50,.86),stone,taper=.77)
        batch.beam((0,0,.75),(0,0,1.44),.24,glow,end=.075,sides=5)
    obj=batch.object('SM_BOSS_'+name,group)
    file=FX/(name+'.glb')
    export([obj],file)
    for slot,(x,y) in enumerate(DEVICES[index]):
        device=obj if slot==0 else bpy.data.objects.new(obj.name.split('_Placement')[0],obj.data)
        if slot:
            group.objects.link(device)
        device.name='SM_BOSS_'+name+'_Placement_'+str(slot+1)
        device.location=((x-320)*UNIT,-(y-320)*UNIT,0)
        device.hide_render=True
        device.hide_set(True)
    return file.relative_to(ROOT).as_posix()


def preview(scene,rig,action,objects,release,last,pattern,slot):
    camera=scene.camera
    pos,rot,scale=camera.location.copy(),camera.rotation_euler.copy(),camera.data.ortho_scale
    resolution=(scene.render.resolution_x,scene.render.resolution_y)
    old_samples=scene.cycles.samples
    scene.cycles.samples=10
    wide = pattern in ('BloodSeal','CoreOverload')
    if slot==0:
        aim_camera(camera,(4,-8,6),(0,-1.2,2.6))
        camera.data.ortho_scale=13.6
    else:
        aim_camera(camera,(14,-23,20),(0,0 if wide else -5,1))
        camera.data.ortho_scale=55 if wide else 34
    scene.render.resolution_x,scene.render.resolution_y=850,620
    rig.animation_data.action=action
    frames=[('release',min(last,release+2))]
    if slot<2:
        frames.insert(0,('warning',max(1,release-DRIVE_FRAMES[pattern])))
    for label,frame in frames:
        scene.frame_set(frame)
        scene.render.filepath=str(ROOT/'previews'/f"boss_{pattern}_{label}.png")
        bpy.ops.render.render(write_still=True)
    camera.location,camera.rotation_euler,camera.data.ortho_scale=pos,rot,scale
    scene.render.resolution_x,scene.render.resolution_y=resolution
    scene.cycles.samples=old_samples
    for obj in objects:
        obj.hide_render=True
    rig.animation_data.action=None


effects=[]
devices=[]
for spec in catalog['scenes']:
    index=spec['index']
    boss=spec['boss_id']
    scene=bpy.data.scenes[spec['arena_id']]
    bpy.context.window.scene=scene
    rig=bpy.data.objects[spec['boss_nodes'][0]]
    idle=next(t.strips[0].action for t in rig.animation_data.nla_tracks if t.name==boss+'_Idle')
    for track in rig.animation_data.nla_tracks:
        track.mute=True
    if not rig.get('boss_combat_gaits'):
        for track in rig.animation_data.nla_tracks:
            if track.name.removeprefix(boss+'_') in ('Walk','Run','Walk_RootMotion','Run_RootMotion'):
                customize_gait(scene,rig,index,track)
        rig['boss_combat_gaits']=True
    group=bpy.data.collections.get('Combat_'+boss)
    if group:
        for obj in list(group.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
    else:
        group=bpy.data.collections.new('Combat_'+boss)
        scene.collection.children.link(group)
    for slot,(pattern,label,shape,windup,radius,reach) in enumerate(PATTERNS[index]):
        release=1+round(windup*FPS)
        last=release+11
        motion=boss+'_'+['AttackPrimary','AttackSecondary','TrialCast'][slot]
        action=bake_attack(scene,rig,motion,index,pattern,release,last)
        fx_id='FX_BOSS_'+pattern
        fx_last=release+(108 if pattern=='PoisonPool' else 18)
        batches=effect_geometry(pattern,shape,radius*UNIT,reach*UNIT,index)
        warning,burst,debris=[batch.object(fx_id+'_'+suffix,group) for batch,suffix in
                              zip(batches,('Telegraph','Release','Debris'))]
        fx_keys(warning,fx_id,[(1,1,0),(release-1,1,0),(release,.001,0),(fx_last,.001,0)])
        linger=fx_last-4 if pattern=='PoisonPool' else release+8
        start_scale=1 if pattern in ('PoisonPool','VenomJet') else (1,.58,1) if shape=='line' else .76
        fx_keys(burst,fx_id,[(1,.001,0),(release-1,.001,0),(release,start_scale,0),
                            (release+4,1,0),(linger,1,0),(fx_last,.001,0)])
        fx_keys(debris,fx_id,[(1,.001,0),(release-1,.001,0),(release,.67,0),
                             (release+5,1,.20),(release+10,1.08,.44),(release+14,.001,0),
                             (fx_last,.001,0)])
        scene.frame_end=fx_last
        scene.frame_set(1)
        file=FX/(fx_id+'.glb')
        export([warning,burst,debris],file,True)
        effects.append(dict(id=pattern,name=label,boss_id=boss,slot=['normal','special','gimmick'][slot],
                            motion=motion,shape=shape,release_frame=release,motion_last_frame=last,
                            effect_last_frame=fx_last,authored_windup_s=(release-1)/FPS,
                            game_windup_s=windup,game_radius=radius,game_reach=reach,
                            anchor={'BloodSeal':'arena_center','PoisonPool':'target_ground',
                                    'CoreOverload':'arena_center'}.get(pattern,'attack_origin'),
                            file=file.relative_to(ROOT).as_posix()))
        preview(scene,rig,action,[warning,burst,debris],release,last,pattern,slot)
        for obj in (warning,burst,debris):
            obj.hide_render=True
            obj.hide_set(True)
    device_file=make_device(scene,index,group)
    devices.append(dict(boss_id=boss,file=device_file,
                        positions_game_chunk=DEVICES[index],
                        positions_blender=[[(x-320)*UNIT,-(y-320)*UNIT,0] for x,y in DEVICES[index]]))
    scene.frame_end=61
    rig.animation_data.action=None
    tracks=list(rig.animation_data.nla_tracks)
    for track in tracks:
        track.mute=False
    spec['animations']=[track.name for track in tracks]
    scene.frame_set(1)
    export([bpy.data.objects[n] for n in spec['boss_nodes']],ROOT/spec['boss_file'],True)
    for track in tracks:
        track.mute=True
    rig.animation_data.action=idle
    scene.frame_set(1)
    print('BOSS_COMBAT_READY',boss,flush=True)

# A separate contact cue for a successful hit, at the target's impact point.
contact_scene=bpy.context.scene
old_contact=bpy.data.objects.get('FX_BOSS_HitConfirm_Sparks')
if old_contact:
    bpy.data.objects.remove(old_contact,do_unlink=True)
contact=MeshBatch()
for i in range(12):
    a=i*math.tau/12
    x,y=math.cos(a),math.sin(a)
    contact.beam((x*.18,y*.18,.06),(x*.99,y*.99,.37+(i%3)*.16),.085,'ember',.008,5)
contact.band((0,0,.04),.62,.11,'scarlet',segments=32)
obj=contact.object('FX_BOSS_HitConfirm_Sparks',contact_scene.collection)
fx_keys(obj,'FX_BOSS_HitConfirm',[(1,.56,0),(4,1,0),(7,1.15,.18),(12,.001,0)])
contact_scene.frame_set(1)
export([obj],FX/'FX_BOSS_HitConfirm.glb',True)
obj.hide_set(True)
obj.hide_render=True
catalog['combat_revision']=2
CATALOG.write_text(json.dumps(catalog,ensure_ascii=False,indent=2),encoding='utf-8')
combat=dict(fps=FPS,arena_m_per_game_unit=UNIT,local_forward_blender='-Y',local_forward_glb='+Z',
            effects=effects,devices=devices,hit_confirm=dict(file='boss_arenas/combat/FX_BOSS_HitConfirm.glb',
            anchor='confirmed_damage_contact',frames=12))
(ROOT/'data'/'boss_combat_catalog.json').write_text(json.dumps(combat,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.context.window.scene=bpy.data.scenes[catalog['scenes'][0]['arena_id']]
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Boss_Arenas.blend'))
print('BOSS_COMBAT_COMPLETE',flush=True)
