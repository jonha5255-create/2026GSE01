"""Three arena scenes and three fitted, rigged bosses for the Three Regions quest."""
import bpy
import bmesh
import json
import math
import os
import random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'boss_arenas'
OUT.mkdir(exist_ok=True)
COLORS = {
    'soil': ('2F2930',0), 'basalt': ('3A3741',0), 'tile': ('55464B',0), 'tile_alt': ('645256',0),
    'brick': ('75403E',0), 'cinnabar': ('B74F3B',0), 'bone': ('BCAF94',0), 'iron': ('393944',0),
    'gold': ('CA9555',0), 'scarlet': ('FC492C',2.4), 'ember': ('FFC45F',2.2),
    'mud': ('303837',0), 'peat': ('4A5243',0), 'slate': ('4C5D5E',0), 'moss': ('61744F',0),
    'water': ('293D38',0), 'toxic': ('A9D460',.7), 'pipe': ('58685C',0), 'reed': ('888555',0),
    'concrete': ('576579',0), 'light_stone': ('8490A0',0), 'charcoal': ('343F50',0),
    'cyan': ('7FE8F3',1.6), 'blue': ('3F7398',0), 'black': ('22242B',0), 'red_hide': ('803C38',0),
}
MATERIALS = {}
for name, (hex_color, glow) in COLORS.items():
    srgb = [int(hex_color[i:i+2],16)/255 for i in (0,2,4)]
    color = tuple(x/12.92 if x<.04045 else ((x+.055)/1.055)**2.4 for x in srgb)
    material = bpy.data.materials.new('MAT_BOSS_'+name)
    material.use_nodes = True
    material.diffuse_color = (*color,1)
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color,1)
    shader.inputs['Roughness'].default_value = .32 if name=='water' else .76
    shader.inputs['Emission Color'].default_value = (*color,1)
    shader.inputs['Emission Strength'].default_value = glow
    MATERIALS[name] = material


class MeshBatch:
    """Accumulate modeled pieces into a named mesh with optional rigid bone weights."""
    def __init__(self):
        self.vertices = []
        self.faces = []
        self.materials = []
        self.weights = {}

    def add(self, vertices, faces, material, bone=None):
        start = len(self.vertices)
        self.vertices.extend(vertices)
        self.faces.extend(tuple(start+i for i in f) for f in faces)
        self.materials.extend([material]*len(faces))
        if bone:
            self.weights.setdefault(bone,[]).extend(range(start,len(self.vertices)))

    def box(self, center, size, material, yaw=0, bone=None, taper=1):
        x,y,z = [v/2 for v in size]
        c,s = math.cos(yaw), math.sin(yaw)
        points = []
        for dz,factor in [(-z,1),(z,taper)]:
            for dx,dy in [(-x,-y),(x,-y),(x,y),(-x,y)]:
                points.append((center[0]+factor*(c*dx-s*dy),center[1]+factor*(s*dx+c*dy),center[2]+dz))
        self.add(points, [(3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material,bone)

    def beam(self, a, b, radius, material, end=None, sides=8, bone=None):
        a,b = Vector(a),Vector(b)
        q = (b-a).to_track_quat('Z','Y')
        vertices = [p + q @ Vector((r*math.cos(i*math.tau/sides),r*math.sin(i*math.tau/sides),0))
                    for p,r in [(a,radius),(b,radius if end is None else end)] for i in range(sides)]
        faces = [tuple(reversed(range(sides))),tuple(range(sides,2*sides))]
        faces.extend((i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides))
        self.add(vertices,faces,material,bone)

    def band(self, center, radius, width, material, start=0, length=math.tau, segments=64):
        vertices = [(center[0]+r*math.cos(start+length*i/segments),
                     center[1]+r*math.sin(start+length*i/segments),center[2])
                    for i in range(segments+1) for r in [radius-width/2,radius+width/2]]
        self.add(vertices,[(2*i,2*i+1,2*i+3,2*i+2) for i in range(segments)],material)

    def roof(self, x,y,z,w,d,h,material):
        self.add([(x-w/2,y-d/2,z),(x+w/2,y-d/2,z),(x+w/2,y+d/2,z),
                  (x-w/2,y+d/2,z),(x-w*.39,y,z+h),(x+w*.39,y,z+h)],
                 [(0,1,5,4),(2,3,4,5),(3,0,4),(1,2,5),(3,2,1,0)],material)

    def object(self, name, collection, armature=None):
        mesh = bpy.data.meshes.new(name+'_Mesh')
        mesh.from_pydata(self.vertices,[],self.faces)
        mesh.update()
        materials = list(dict.fromkeys(self.materials))
        indices = {m:i for i,m in enumerate(materials)}
        for material in materials:
            mesh.materials.append(MATERIALS[material])
        for polygon, material in zip(mesh.polygons,self.materials):
            polygon.material_index = indices[material]
        obj = bpy.data.objects.new(name,mesh)
        collection.objects.link(obj)
        if armature:
            obj.parent = armature
            mod = obj.modifiers.new('Skin','ARMATURE')
            mod.object = armature
            for bone, ids in self.weights.items():
                obj.vertex_groups.new(name=bone).add(ids,1,'REPLACE')
        return obj


def pavement(batch, rng, palette):
    batch.box((0,0,-.26),(44,44,.52),palette[0])
    for row in range(-8,9):
        for col in range(-8,9):
            x,y = col*2.55,row*2.55
            if math.hypot(x,y)>21 and not (abs(x)<4 and y<0):
                continue
            batch.box((x,y,-.028),(2.50,2.50,.057),rng.choice(palette),taper=.98)


def debris(batch, rng, palette, count, minimum=13.5):
    for _ in range(count):
        a = rng.uniform(0,math.tau)
        r = rng.uniform(minimum,21.4)
        x,y = r*math.cos(a),r*math.sin(a)
        if abs(x)<3.6 and y<0:
            continue
        width = rng.uniform(.20,1.0)
        height = rng.uniform(.12,.62)
        batch.box((x,y,height*.48),(width,width*rng.uniform(.66,1.65),height),
                  rng.choice(palette),rng.uniform(0,math.tau),taper=rng.uniform(.48,.88))


def blood_arena(rng):
    floor, seal, buildings, lights, rubble = [MeshBatch() for _ in range(5)]
    pavement(floor,rng,['basalt','tile','tile_alt','tile'])
    for r,width in [(5.2,.10),(8.2,.11),(11.7,.17),(12.15,.06)]:
        seal.band((0,0,.014),r,width,'scarlet' if r in (8.2,11.7) else 'brick')
    for i in range(12):
        angle = i*math.tau/12
        x,y = 10.75*math.cos(angle),10.75*math.sin(angle)
        seal.box((x,y,.022),(1.18,.26,.03),'brick',angle+math.pi/4)
        seal.box((x,y,.029),(.56,.085,.025),'ember',angle-math.pi/4)
    for y in [-20,17.8]:
        for x in [-3.25,3.25]:
            buildings.box((x,y,.4),(1.4,1.4,.8),'basalt')
            buildings.box((x,y,2.33),(.63,.63,4.30),'brick',taper=.84)
            lights.box((x,y-.38,2.7),(.32,.065,.73),'ember')
        buildings.box((0,y,4.25),(8.25,.74,.44),'cinnabar')
        buildings.roof(0,y,4.52,9.3,2.8,1.16,'iron')
        buildings.box((0,y,5.74),(7.8,.30,.21),'basalt')
    for x in [-17.8,17.8]:
        for y in [-13,-5,4,13]:
            buildings.box((x,y,.60),(2.2,2.25,1.2),'basalt')
            buildings.box((x,y,1.65),(1.1,1.12,1.30),'brick',taper=.76)
            buildings.roof(x,y,2.34,2.4,2.12,.54,'iron')
            lights.box((x,y,2.0),(1.02,1.02,.39),'ember')
    for x in [-11.5,11.5]:
        buildings.box((x,17,1.1),(5.2,4.1,2.2),'brick')
        buildings.roof(x,17,2.2,6.5,4.8,1.40,'iron')
        lights.box((x,14.91,1.2),(1.30,.08,.60),'scarlet')
    for z,w,d in [(.13,5.5,3.45),(.34,4.5,2.75),(.61,3.7,2.1)]:
        buildings.box((0,14.65,z),(w,d,.29),'basalt',taper=.94)
    buildings.box((0,14.65,1.29),(2.76,1.27,1.17),'brick',taper=.83)
    buildings.box((0,14.65,2.02),(3.18,1.5,.23),'basalt')
    lights.box((0,13.99,1.47),(.73,.09,.62),'scarlet',math.pi/4,taper=.67)
    for x in [-1.19,1.19]:
        lights.box((x,14.59,2.23),(.21,.21,.38),'ember',taper=.53)
    for side in [-1,1]:
        for j in range(8):
            y = -17+j*4.5
            buildings.box((side*20.4,y,.62),(.9,4.25,1.24),'basalt')
            buildings.box((side*20.4,y,1.26),(1.17,4.38,.18),'brick')
    debris(rubble,rng,['basalt','brick','tile_alt'],155)
    return [('Ground',floor),('BloodSeal',seal),('ShrinesAndWalls',buildings),('EmberLanterns',lights),('Rubble',rubble)]


def poison_arena(rng):
    floor, sumps, works, grass, rubble = [MeshBatch() for _ in range(5)]
    floor.box((0,0,-.30),(44,44,.6),'mud')
    floor.beam((0,0,-.09),(0,0,.012),12.1,'peat',sides=36)
    for row in range(-5,6):
        for col in range(-5,6):
            x,y = col*2.14,row*2.14
            if math.hypot(x,y)<11.55:
                floor.box((x,y,.016),(2.04,2.03,.044),rng.choice(['peat','slate','peat','mud']),rng.uniform(-.018,.018))
    for x in [-15.6,15.6]:
        sumps.box((x,.3,.030),(8.0,29.5,.063),'water',taper=.97)
        for s in [-1,1]:
            works.box((x+s*4.0,.3,.34),(.68,30.4,.68),'slate')
        for y in [-12,-6,0,6,12]:
            sumps.band((x+rng.uniform(-2,2),y,.073),rng.uniform(.5,1.3),.05,'toxic',segments=24)
            for side in [-1,1]:
                works.box((x+side*3.9,y,.85),(.42,.44,1.4),'pipe')
        for y in [-11,9]:
            works.beam((x-4.8,y,1.7),(x+4.8,y,1.7),.34,'pipe',sides=12)
            for s in [-1,1]:
                works.beam((x+s*4.8,y,.19),(x+s*4.8,y,1.7),.38,'pipe',sides=12)
                works.beam((x+s*3.7,y,1.7),(x+s*3.5,y,1.7),.39,'toxic')
    for x in [-16,-10,10,16]:
        works.beam((x,18.3,0),(x,18.3,3.9),1.55,'pipe',end=1.47,sides=12)
        works.beam((x,18.3,3.9),(x,18.3,4.37),1.47,'slate',end=1.13,sides=12)
        works.box((x,16.80,1.87),(1.12,.11,.43),'toxic')
    works.box((0,18.55,.96),(8.15,3.7,1.92),'slate')
    works.box((0,18.55,2.13),(9.02,3.98,.45),'pipe')
    for x in [-2.4,0,2.4]:
        works.box((x,16.67,.93),(1.73,.14,1.18),'mud')
        for dx in [-.54,-.27,0,.27,.54]:
            works.box((x+dx,16.57,.93),(.080,.095,1.08),'pipe')
        sumps.box((x,16.47,.18),(1.63,.23,.13),'toxic')
    for y in [-18.8,-16.8,-14.8,-12.8]:
        floor.box((0,y,.03),(7.4,1.93,.075),'slate')
        for x in [-2.9,2.9]:
            floor.box((x,y,.077),(.14,1.84,.025),'reed')
    for _ in range(225):
        x,y = rng.uniform(-21,21),rng.uniform(-21,21)
        if math.hypot(x,y)<12.8 or (abs(x)<4 and y<0) or (12<abs(x)<19 and abs(y)<14):
            continue
        for __ in range(3):
            dx,dy = rng.uniform(-.32,.32),rng.uniform(-.32,.32)
            grass.beam((x+dx,y+dy,.01),(x+dx*2,y+dy*2,rng.uniform(.22,.90)),.05,
                       rng.choice(['moss','reed']),.004,3)
    debris(rubble,rng,['mud','peat','slate'],95,18)
    return [('Ground',floor),('PoisonChannels',sumps),('DrainageWorks',works),('Reeds',grass),('Rubble',rubble)]


def stone_arena(rng):
    floor, inlay, columns, ruins, rubble = [MeshBatch() for _ in range(5)]
    pavement(floor,rng,['charcoal','concrete','slate','concrete'])
    for r,w in [(4.4,.19),(7.85,.13),(11.4,.14)]:
        inlay.band((0,0,.014),r,w,'blue' if r==4.4 else 'cyan',start=math.pi/4,segments=8)
    for i in range(8):
        angle = i*math.tau/8
        x,y = 10.15*math.cos(angle),10.15*math.sin(angle)
        inlay.box((x,y,.022),(1.48,.38,.031),'blue',angle)
        inlay.box((x,y,.040),(1.12,.075,.031),'cyan',angle)
    for i in range(10):
        a = (i+.15)*math.tau/10
        x,y = math.cos(a)*16.0,math.sin(a)*16.0
        if abs(x)<4.6 and y<0:
            continue
        height = [3.6,5.8,2.7,5.8,6.3][i%5]
        columns.box((x,y,.46),(2.48,2.48,.92),'charcoal',a)
        columns.box((x,y,.92+height/2),(1.46,1.46,height),'light_stone',a,taper=.74)
        columns.box((x,y,height+1.02),(1.86,1.83,.45),'concrete',a,taper=.83)
        columns.box((x,y-1.1,2.0),(.22,.14,1.34),'cyan',0)
        if i%2==0:
            columns.beam((x+.52,y,1.6),(x+2.9,y-.45,.47),.54,'charcoal',end=.38,sides=4)
    for x in [-13,-6.5,6.5,13]:
        height = 4.4 if abs(x)>8 else 6.1
        ruins.box((x,19.1,height/2),(4.75,3.45,height),'charcoal',taper=.91)
        for level in range(2):
            for dx in [-1.30,0,1.30]:
                ruins.box((x+dx,17.28,1.9+level*1.36),(.76,.18,.92),'concrete')
        ruins.beam((x,18.9,height),(x+.8,19.4,height+2.5),.21,'iron',sides=4)
        ruins.box((x-.33,19.45,height+.50),(2.45,1.32,.39),'light_stone',rng.uniform(-.5,.5))
    for x in [-4.3,4.3]:
        ruins.box((x,-19.2,1.65),(1.47,2.2,3.3),'concrete',taper=.83)
        ruins.box((x,-20.22,2.23),(.88,.14,.77),'cyan')
    for x in [-3.5,3.5]:
        ruins.box((x,15.9,2.75),(1.55,1.38,5.5),'charcoal',taper=.81)
        ruins.box((x,15.9,5.37),(2.04,1.84,.66),'light_stone',taper=.83)
    ruins.beam((-3.9,15.9,5.61),(-.77,15.8,5.20),.73,'concrete',end=.56,sides=4)
    ruins.beam((3.88,15.9,5.70),(1.18,15.82,5.90),.71,'light_stone',end=.44,sides=4)
    ruins.box((.2,15.4,.65),(1.84,1.47,1.12),'concrete',.52,taper=.63)
    debris(rubble,rng,['concrete','light_stone','charcoal'],195)
    return [('Ground',floor),('CoreInlay',inlay),('BrokenColumns',columns),('CollapsedFacades',ruins),('Rubble',rubble)]


def ring_horn(batch, points, radii, material, bone):
    vertices = []
    n = 9
    for i,(p,r) in enumerate(zip(points,radii)):
        prev = Vector(points[max(0,i-1)])
        nxt = Vector(points[min(len(points)-1,i+1)])
        q = (nxt-prev).to_track_quat('Z','Y')
        vertices.extend(Vector(p)+q @ Vector((r*math.cos(j*math.tau/n),r*math.sin(j*math.tau/n),0))
                        for j in range(n))
    faces = [tuple(reversed(range(n)))]
    for k in range(len(points)-1):
        faces.extend((k*n+j,k*n+(j+1)%n,(k+1)*n+(j+1)%n,(k+1)*n+j) for j in range(n))
    faces.append(tuple(range((len(points)-1)*n,len(points)*n)))
    batch.add(vertices,faces,material,bone)


def crystal(batch, center, size, material, bone):
    x,y,z = center
    a,b,c = [n/2 for n in size]
    vertices = [(x-a,y,z),(x,y-b,z),(x+a,y,z),(x,y+b,z),(x,y,z+c),(x,y,z-c)]
    faces = [(i,(i+1)%4,4) for i in range(4)] + [((i+1)%4,i,5) for i in range(4)]
    batch.add(vertices,faces,material,bone)


def boss_armor(index, rig, batch):
    bone = rig.data.bones
    if index==0:
        for side,s in [('L',-1),('R',1)]:
            ring_horn(batch,[(s*.19,0,1.66),(s*.43,.065,1.80),(s*.59,.048,2.04),
                             (s*.61,-.02,2.27),(s*.52,-.13,2.48)],
                      [.141,.116,.093,.061,.006],'bone','head')
            ring_horn(batch,[(s*.18,.025,1.64),(s*.28,.055,1.70),(s*.34,.049,1.756)],
                      [.156,.142,.122],'gold','head')
            upper = bone['upper_arm.'+side].head_local
            arm = 'upper_arm.'+side
            batch.box(upper+Vector((s*.121,0,.057)),(.42,.46,.25),'brick',s*.22,arm,taper=.69)
            batch.box(upper+Vector((s*.134,-.019,.148)),(.42,.36,.12),'cinnabar',s*.23,arm,taper=.70)
            batch.box(upper+Vector((s*.138,-.209,.05)),(.28,.078,.099),'gold',s*.14,arm,taper=.77)
            ring_horn(batch,[upper+Vector((s*.21,.075,.14)),upper+Vector((s*.29,.10,.24)),
                             upper+Vector((s*.36,.098,.30))],[.076,.050,.004],'bone',arm)
            forearm = bone['forearm.'+side].head_local.lerp(bone['hand.'+side].head_local,.48)
            batch.box(forearm,(.25,.27,.225),'iron',s*.22,'forearm.'+side,taper=.80)
            batch.box(forearm+Vector((s*.025,-.137,.015)),(.17,.034,.12),
                      'gold',s*.18,'forearm.'+side,taper=.76)
            batch.box((s*.132,-.27,1.547),(.16,.12,.073),'black',s*.12,'head',taper=.69)
            batch.box((s*.133,-.337,1.551),(.111,.021,.026),'scarlet',s*.12,'head')
        batch.box((0,-.192,1.049),(.54,.20,.38),'iron',bone='chest',taper=.72)
        crystal(batch,(0,-.304,1.062),(.251,.098,.278),'gold','chest')
        crystal(batch,(0,-.354,1.062),(.123,.052,.157),'scarlet','chest')
        crystal(batch,(0,-.265,1.677),(.078,.08,.141),'ember','head')
        for x in [-.146,0,.146]:
            batch.box((x,-.151,.633),(.124,.071,.287),'brick',x*.4,'pelvis',taper=.82)
            batch.box((x,-.193,.657),(.076,.018,.177),'gold',x*.4,'pelvis',taper=.70)
        batch.box((0,-.223,1.39),(.32,.13,.11),'red_hide',bone='head',taper=.65)
        for x in [-.111,-.055,.055,.111]:
            batch.beam((x,-.303,1.43),(x,-.316,1.34),.023,'bone',.005,5,'head')
    elif index==1:
        for side,s in [('L',-1),('R',1)]:
            batch.box((s*.182,-1.12,1.005),(.215,.48,.18),'pipe',s*.16,'head',taper=.68)
            batch.box((s*.219,-1.096,1.116),(.095,.19,.056),'black',s*.17,'head',taper=.80)
            batch.box((s*.232,-1.10,1.138),(.047,.124,.023),'toxic',s*.17,'head')
            ring_horn(batch,[(s*.19,-.76,1.10),(s*.31,-.68,1.37),(s*.36,-.58,1.48)],
                      [.094,.073,.004],'pipe','head')
            for y,joint in [(-.37,'chest'),(.04,'spine'),(.42,'pelvis')]:
                batch.box((s*.31,y,.998),(.31,.36,.238),'pipe',s*.21,joint,taper=.69)
                batch.box((s*.454,y-.032,1.015),(.041,.169,.062),'black',0,joint)
                batch.box((s*.478,y-.032,1.015),(.020,.127,.038),'toxic',0,joint)
            for leg in ['front','hind']:
                b = bone[f'{leg}_upper.{side}']
                center = b.head_local.lerp(b.tail_local,.25)
                batch.box(center+Vector((s*.071,-.015,.037)),(.295,.35,.226),'peat',s*.19,b.name,taper=.68)
            # Low slung reservoirs echo the basin's horizontal pipe runs.
            batch.beam((s*.376,-.56,1.105),(s*.405,-.115,1.229),.126,'pipe',.122,10,'chest')
            batch.beam((s*.393,-.43,1.146),(s*.40,-.245,1.196),.132,'toxic',.129,10,'chest')
            batch.beam((s*.399,-.44,1.145),(s*.420,-.39,1.290),.086,'pipe',.060,8,'chest')
        for y,joint in [(-.47,'chest'),(-.08,'spine'),(.35,'pelvis')]:
            batch.box((0,y,1.176),(.52,.34,.17),'pipe',bone=joint,taper=.66)
        batch.box((0,-1.308,1.005),(.282,.164,.103),'black',bone='head',taper=.69)
        for x in [-.135,-.067,.067,.135]:
            batch.beam((x,-1.340,.969),(x*.96,-1.365,.827),.031,'bone',.004,6,'head')
    else:
        batch.box((0,0,1.75),(.58,.46,.61),'light_stone',bone='head',taper=.76)
        batch.box((0,-.246,1.805),(.42,.074,.135),'charcoal',bone='head',taper=.83)
        batch.box((0,-.29,1.807),(.36,.028,.048),'cyan',bone='head')
        batch.box((0,-.265,1.18),(.75,.43,.56),'concrete',bone='chest',taper=.83)
        crystal(batch,(0,-.491,1.214),(.40,.14,.421),'charcoal','chest')
        crystal(batch,(0,-.57,1.217),(.226,.098,.247),'cyan','chest')
        for s in [-1,1]:
            a=(s*.080,-.479,1.164)
            b=(s*.198,-.480,1.123)
            c=(s*.284,-.481,1.026)
            batch.beam(a,b,.016,'cyan',.012,4,'chest')
            batch.beam(b,c,.012,'cyan',.003,4,'chest')
        for side,s in [('L',-1),('R',1)]:
            shoulder = bone['upper_arm.'+side].head_local
            arm = 'upper_arm.'+side
            batch.box(shoulder+Vector((s*.172,.018,.085)),(.49,.57,.42),'light_stone',s*.18,arm,taper=.77)
            batch.beam(shoulder+Vector((s*.21,.10,.195)),shoulder+Vector((s*.30,.14,.65 if s<0 else .46)),
                       .184,'concrete',.122,5,arm)
            batch.box(shoulder+Vector((s*.187,-.275,.104)),(.207,.040,.122),'charcoal',s*.18,arm,taper=.70)
            batch.box(shoulder+Vector((s*.187,-.302,.104)),(.146,.022,.031),'cyan',s*.18,arm)
            hand = bone['hand.'+side].head_local
            batch.box(hand+Vector((s*.032,-.014,-.081)),(.34,.332,.331),'concrete',s*.12,'hand.'+side,taper=.80)
            crystal(batch,hand+Vector((s*.032,-.183,-.087)),(.134,.049,.16),'cyan','hand.'+side)
            shin = bone['shin.'+side]
            batch.box(shin.head_local.lerp(shin.tail_local,.35)+Vector((0,-.075,0)),(.24,.20,.26),
                      'light_stone',0,shin.name,taper=.82)
        for x,z in [(-.53,2.10),(-.36,2.47),(0,2.63),(.38,2.47),(.55,2.14)]:
            crystal(batch,(x,.14,z),(.228,.245,.319),'light_stone','head')
            crystal(batch,(x,.012,z),(.074,.041,.109),'cyan','head')


SPECS = [
    dict(id='BloodAltar', name='핏빛 제단', boss='HornKing', boss_name='뿔왕', source=2,
         chunk=[2,0], scale=2.36, builder=blood_arena, accent=(1,.20,.12)),
    dict(id='PoisonBasin', name='독안개 분지', boss='MireHound', boss_name='독안개 포식견', source=4,
         chunk=[0,2], scale=3.10, builder=poison_arena, accent=(.54,.88,.33)),
    dict(id='CollapsedCourt', name='붕괴 석정', boss='StoneColossus', boss_name='거신', source=3,
         chunk=[-2,0], scale=2.82, builder=stone_arena, accent=(.28,.64,1)),
]


def remove_old_head_details(body, index):
    if index not in (0,2):
        return
    group = body.vertex_groups['head'].index
    mesh = bmesh.new()
    mesh.from_mesh(body.data)
    deform = mesh.verts.layers.deform.active
    pending = {v for v in mesh.verts if v[deform].get(group,0)>.999}
    islands = []
    while pending:
        start = pending.pop()
        connected = [start]
        for vertex in connected:
            for edge in vertex.link_edges:
                neighbor = edge.other_vert(vertex)
                if neighbor in pending:
                    pending.remove(neighbor)
                    connected.append(neighbor)
        islands.append(connected)
    solid_head = max(islands,key=len) if index==0 else None
    bmesh.ops.delete(mesh,geom=[v for island in islands if island is not solid_head for v in island],
                     context='VERTS')
    mesh.to_mesh(body.data)
    mesh.free()


def export(objects, filepath, animated=False):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(filepath=str(filepath),export_format='GLB',use_selection=True,
                              use_active_scene=True,
                              export_animations=animated,export_animation_mode='NLA_TRACKS',
                              export_force_sampling=True,export_yup=True)


def marker(name, position, collection, radius=.5):
    obj = bpy.data.objects.new(name,None)
    obj.empty_display_type='CIRCLE'
    obj.empty_display_size=radius
    obj.location=position
    collection.objects.link(obj)
    obj.hide_render=True
    return obj


def aim(obj, position, target):
    obj.location = position
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()


def lighting(scene, spec):
    scene.world = bpy.data.worlds.new(spec['id']+'_Sky')
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes.get('Background')
    bg.inputs[0].default_value = (.15,.19,.26,1)
    bg.inputs[1].default_value = .52
    for name,p,energy,color,size in [
        ('SoftKey',(7,-13,22),8500,(1,.88,.79),13),
        ('SkyFill',(-15,8,19),6200,(.48,.66,1),17),
        ('ThemeRim',(4,15,13),5900,spec['accent'],12),
        ('BossKey',(1,-5,7),980,(1,.85,.73),5),
    ]:
        data=bpy.data.lights.new(name,'AREA')
        data.energy,data.color,data.size=energy,color,size
        obj=bpy.data.objects.new(name,data)
        scene.collection.objects.link(obj)
        aim(obj,p,(0,0,1))
    data=bpy.data.cameras.new(spec['id']+'_Camera')
    camera=bpy.data.objects.new(data.name,data)
    scene.collection.objects.link(camera)
    scene.camera=camera
    data.type='ORTHO'
    aim(camera,(29,-42,42),(0,0,0))
    data.ortho_scale=65
    scene.render.engine='CYCLES'
    scene.cycles.samples=16
    scene.cycles.use_denoising=True
    scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX'
    return camera


def render(scene, name, resolution):
    scene.render.resolution_x,scene.render.resolution_y=resolution
    scene.render.filepath=str(ROOT/'previews'/name)
    bpy.ops.render.render(write_still=True)


source_manifest=json.loads((ROOT/'data'/'animation_manifest.json').read_text(encoding='utf-8'))
initial_scene=bpy.context.scene
reports=[]
scenes=[]
for index,spec in enumerate(SPECS):
    scene=bpy.data.scenes.new('ARENA_%02d_%s'%(index,spec['id']))
    bpy.context.window.scene=scene
    scene.unit_settings.system='METRIC'
    scene.render.fps=30
    scene.frame_start,scene.frame_end=1,61
    stage=bpy.data.collections.new('01_Arena_Modules')
    character=bpy.data.collections.new('02_Boss')
    guides=bpy.data.collections.new('03_Placement_Guides')
    for collection in (stage,character,guides):
        scene.collection.children.link(collection)
    arena_id='ARENA_%02d_%s'%(index,spec['id'])
    boss_id='BOSS_%02d_%s'%(index,spec['boss'])
    objects=[batch.object('SM_'+arena_id+'_'+part,stage)
             for part,batch in spec['builder'](random.Random(128+index))]
    placement = {'BOSS_SPAWN':[0,0,0], 'PLAYER_ENTRY':[0,-19,0], 'ROAD_EXIT':[0,-22,0]}
    markers=[marker(name,pos,guides) for name,pos in placement.items()]
    marker('CLEAR_COMBAT_RADIUS', (0,0,0), guides, 11)
    arena_file=arena_id+'.glb'
    if os.environ.get('ART_BOSS_REVISE_ONLY')!='1':
        export([*objects,*markers],OUT/arena_file)

    base=source_manifest['creatures'][spec['source']]
    with bpy.data.libraries.load(str(ROOT/'Characters_Rigged.blend')) as (src,dst):
        dst.objects=[base['skeleton'],base['id']]
    rig,body=dst.objects
    for obj in (rig,body):
        character.objects.link(obj)
        obj.hide_viewport=False
        obj.hide_render=False
        obj.hide_set(False)
    rig.name='RIG_'+boss_id
    rig.location=(0,0,0)
    rig.rotation_euler=(0,0,0)
    rig.scale=(spec['scale'],)*3
    body.name='SK_'+boss_id+'_Body'
    body.data.name=body.name+'_Mesh'
    remove_old_head_details(body,index)
    if index==1:
        remap={'MAT_gunmetal':'mud','MAT_armor':'pipe','MAT_mask':'bone',
               'MAT_core_red':'toxic','MAT_red_dark':'peat'}
        for slot in body.material_slots:
            if slot.material:
                key=slot.material.name.split('.')[0]
                if key in remap:
                    slot.material=MATERIALS[remap[key]]
    armor_batch=MeshBatch()
    boss_armor(index,rig,armor_batch)
    armor=armor_batch.object('SK_'+boss_id+'_Armor',character,rig)
    clip_ids=[]
    idle=None
    rig.animation_data.action=None
    for track in rig.animation_data.nla_tracks:
        suffix=track.name.split('_',2)[-1]
        track.name=boss_id+'_'+suffix
        if suffix=='Idle':
            idle=track.strips[0].action
        clip_ids.append(track.name)
        track.mute=False
    scene.frame_set(1)
    boss_file=boss_id+'.glb'
    export([rig,body,armor],OUT/boss_file,True)
    for track in rig.animation_data.nla_tracks:
        track.mute=True
    for pose_bone in rig.pose.bones:
        pose_bone.matrix_basis.identity()
    bpy.context.view_layer.update()
    points = [obj.matrix_world @ v.co for obj in (body,armor) for v in obj.data.vertices]
    dimensions = [round(max(v[i] for v in points)-min(v[i] for v in points),2) for i in range(3)]
    rig.animation_data.action=idle
    scene.frame_set(1)
    bpy.context.view_layer.update()
    camera=lighting(scene,spec)
    # Match scale across the three arena shots, then show a tighter boss view.
    render(scene,arena_id+'_overview.png',(1400,1040))
    detail_positions=[(8.5,-13,6.0),(8.5,-13,5.6),(5.5,-8.4,6.5)]
    aim(camera,detail_positions[index],(0,-.15,[2.89,2.22,3.95][index]))
    camera.data.ortho_scale=[8.1,8.5,10.1][index]
    render(scene,boss_id+'_detail.png',(950,900))
    aim(camera,(29,-42,42),(0,0,0))
    camera.data.ortho_scale=65
    scene.render.resolution_x,scene.render.resolution_y=1400,1040
    arena_triangles=sum(len(p.vertices)-2 for o in objects for p in o.data.polygons)
    boss_triangles=sum(len(p.vertices)-2 for o in (body,armor) for p in o.data.polygons)
    reports.append(dict(index=index,arena_id=arena_id,name=spec['name'],boss_id=boss_id,
                        boss_name=spec['boss_name'],reference_chunk=spec['chunk'],footprint_m=[44,44],
                        clear_combat_radius_m=11,placement_blender=placement,arena_file='boss_arenas/'+arena_file,
                        boss_file='boss_arenas/'+boss_file,arena_nodes=[o.name for o in objects],
                        boss_bind_dimensions_m=dimensions,
                        boss_nodes=[rig.name,body.name,armor.name],animations=clip_ids,
                        arena_triangles=arena_triangles,boss_triangles=boss_triangles))
    scenes.append(scene)
    print('BOSS_REGION_READY',arena_id,len(clip_ids),flush=True)

bpy.context.window.scene=scenes[0]
bpy.data.scenes.remove(initial_scene)
bpy.ops.outliner.orphans_purge(do_local_ids=True,do_linked_ids=True,do_recursive=True)
manifest=dict(version=2,units='meters',source_up='+Z',source_front='-Y',glb_up='+Y',glb_front='+Z',
              scenes=reports,notes=['Origin at boss spawn. South entrance is source -Y.',
              'The 44 m art layout is independent of current runtime chunk dimensions.',
              'Boss rigs reuse the compatible biped and quadruped gait clips under separate boss names.'])
(ROOT/'data'/'boss_arena_catalog.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Boss_Arenas.blend'))
print('BOSS_ARENAS_COMPLETE',flush=True)
