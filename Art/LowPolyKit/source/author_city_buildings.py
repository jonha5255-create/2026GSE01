"""Blender-only modeling pass using doc/로우 폴리 도시 건물 컨셉 보드.png.
Run after author_assets.py. Updates building assets, never game source.
"""
import bpy, os, math, json, random
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
random.seed(20261020)
scene=bpy.context.scene
lib=bpy.data.collections['01_MESH_LIBRARY'];lib.hide_viewport=False;lib.hide_render=False
for c in list(bpy.data.collections):
    if c.name.startswith('03_') or c.name.startswith('04_'):
        for o in list(c.objects):bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.collections.remove(c)
for o in list(lib.objects):
    if o.name.startswith('SM_ENV_Building_'):bpy.data.objects.remove(o,do_unlink=True)
with open(os.path.join(ROOT,'data','mesh_catalog.json'),encoding='utf-8') as f:catalog=json.load(f)
catalog['meshes']=[x for x in catalog['meshes'] if x['slot']!='building']
catalog['slots']['building']={}
colors={'plaster':'#AFA99A','cream':'#CEC2A0','blueglass':'#42616A','windowlit':'#DBB770',
        'tile':'#3C4A50','terracotta':'#8C6555','red':'#A4453D','signblue':'#32667E',
        'signgold':'#B58D43','white':'#D0CDC0','purple':'#705C83','conduit':'#727A77'}
M={m.name.removeprefix('MAT_'):m for m in bpy.data.materials}
for n,h in colors.items():
    m=bpy.data.materials.get('MAT_'+n) or bpy.data.materials.new('MAT_'+n)
    rgb=tuple(int(h[i:i+2],16)/255 for i in (1,3,5));m.diffuse_color=(*rgb,1);m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*rgb,1)
    bs.inputs['Roughness'].default_value=.83
    if n in ['windowlit','memory','red','signblue','purple']:
        bs.inputs['Emission Color'].default_value=(*rgb,1);bs.inputs['Emission Strength'].default_value=.18
    M[n]=m
parts=[]
def material(o,m):o.data.materials.append(M[m]);parts.append(o);return o
def box(p,s,m='concrete',b=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.scale=s
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if b:
        mod=o.modifiers.new('Edge cuts','BEVEL');mod.width=b;mod.segments=1
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return material(o,m)
def rod(a,b,r=.035,m='steel',r2=None,n=6):
    a,b=Vector(a),Vector(b)
    bpy.ops.mesh.primitive_cone_add(vertices=n,radius1=r,radius2=r if r2 is None else r2,depth=(b-a).length,location=(a+b)*.5)
    o=bpy.context.object;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return material(o,m)
def geo(v,f,m):
    mesh=bpy.data.meshes.new('Geometry');mesh.from_pydata(v,[],f);mesh.update()
    o=bpy.data.objects.new('Part',mesh);lib.objects.link(o);return material(o,m)
def text(label,p,size=.35,m='paper',rotation=(math.pi/2,0,0)):
    curve=bpy.data.curves.new('Sign lettering','FONT');curve.body=label;curve.size=size;curve.align_x='CENTER';curve.extrude=.006
    o=bpy.data.objects.new('Lettering',curve);lib.objects.link(o);o.location=p;o.rotation_euler=rotation
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH')
    return material(bpy.context.object,m)
def window(x,y,z,w=1.0,h=1.5,side=False,lit=False):
    if not side:
        box((x,y,z),(w+.15,.13,h+.15),'darkstone')
        box((x,y-.075,z),(w,.03,h),'windowlit' if lit else 'blueglass')
        box((x,y-.105,z),(.055,.045,h),'edge')
        box((x,y-.105,z),(w,.045,.055),'edge')
        box((x,y-.09,z-h/2-.08),(w+.24,.24,.09),'stone')
    else:
        box((x,y,z),(.13,w+.15,h+.15),'darkstone')
        box((x+.075,y,z),(.03,w,h),'blueglass')
        box((x+.105,y,z),(.04,.055,h),'edge')
        box((x+.105,y,z),(.04,w,.055),'edge')
def ac(x,y,z):
    box((x,y,z),(.78,.38,.54),'white',.025)
    for dx in [-.25,-.15,-.05,.05,.15,.25]:box((x+dx,y-.2,z),(.025,.02,.36),'conduit')
    rod((x+.38,y,z),(x+.5,y,z-.75),.018,'conduit')
def railing(a,b,z,h=.7):
    a,b=Vector(a),Vector(b)
    for i in range(max(2,int((b-a).length/.48))+1):
        p=a.lerp(b,i/max(2,int((b-a).length/.48)));rod((p.x,p.y,z),(p.x,p.y,z+h),.022)
    rod((a.x,a.y,z+h),(b.x,b.y,z+h),.028)
def stairs(x,y,z,steps=5,w=1.5):
    for i in range(steps):box((x,y-i*.32,z+(i+1)*.085),(w,.34,(i+1)*.17),'stone')
def sign(x,y,z,w,label,color='signblue'):
    box((x,y,z),(w,.25,.63),'ink',.03)
    box((x,y-.14,z),(w-.10,.04,.50),color)
    text(label,(x,y-.17,z-.12),.29,'paper')
def vertical_sign(x,y,z,label,color='red'):
    box((x,y,z),(.55,.40,2.4),'ink',.025)
    box((x,y-.22,z),(.43,.035,2.25),color)
    for i,c in enumerate(label[:5]):text(c,(x,y-.25,z+.76-i*.39),.29)
def roof(w,d,h,antenna=True):
    box((0,0,h), (w+.22,d+.22,.22),'darkstone')
    for x in [-w/2,w/2]:box((x,0,h+.3),(.14,d,.6),'stone')
    for y in [-d/2,d/2]:box((0,y,h+.3),(w,.14,.6),'stone')
    box((-.8,.5,h+.48),(1.35,1.05,.8),'concrete',.035)
    ac(.9,.6,h+.39)
    rod((w*.26,d*.25,h+.12),(w*.26,d*.25,h+1.3),.38,'steel',n=10)
    if antenna:
        rod((-w*.32,.3,h),(-w*.32,.3,h+2),.03)
        for z in [.9,1.3,1.6]:rod((-w*.32-.35,.3,h+z),(-w*.32+.35,.3,h+z),.018)
def shell(w=5,d=4,floors=3,color='plaster',shop=False,roofed=True):
    h=floors*2.8
    box((0,0,.12),(w+.8,d+.8,.24),'darkstone',.035)
    box((0,0,h/2),(w,d,h),color,.04)
    for row in range(floors):
        z=row*2.8
        box((0,-d/2-.06,z+.18),(w+.08,.15,.22),'stone')
        if row==0:
            box((-w*.22,-d/2-.06,1.12),(1.05,.14,2.16),'ink')
            box((-w*.22,-d/2-.15,1.27),(.85,.035,1.65),'blueglass')
            rod((-w*.22+.31,-d/2-.2,.95),(-w*.22+.31,-d/2-.2,1.3),.025,'edge')
            window(w*.22,-d/2-.03,1.4,w*.32,1.65,lit=shop)
        else:
            for i in range(3):window((i-1)*w*.29,-d/2-.03,z+1.5,w*.22,1.55,lit=(row+i)%4==1)
        for j in [-.9,.9]:window(w/2+.03,j,z+1.5,1.05,1.5,side=True)
    for x in [-w*.47,w*.47]:rod((x,-d/2-.13,.25),(x,-d/2-.13,h-.1),.035,'conduit')
    for row in range(1,floors):ac(w*.38,-d/2-.22,row*2.8+.5)
    if roofed:roof(w,d,h)
    return h
def tile_roof(cx,cy,z,w,d):
    # Thick swept gable with raised eaves; segmented ribs create modeled tiles.
    xs=[-w/2,-w*.34,0,w*.34,w/2];zs=[.18,0,.88,0,.18]
    verts=[]
    for y in [-d/2,d/2]:
        verts.extend([(cx+x,cy+y,z+zz) for x,zz in zip(xs,zs)])
        verts.extend([(cx+x,cy+y,z+zz-.14) for x,zz in zip(xs,zs)])
    faces=[]
    for i in range(4):faces.extend([(i,i+1,11+i,10+i),(5+i,15+i,16+i,6+i),(i,5+i,6+i,i+1),(10+i,11+i,16+i,15+i)])
    faces.extend([(0,10,15,5),(4,9,19,14)])
    geo(verts,faces,'tile')
    for j in range(int(d/.22)+1):
        y=cy-d/2+j*d/int(d/.22)
        for i in range(4):rod((cx+xs[i],y,z+zs[i]+.025),(cx+xs[i+1],y,z+zs[i+1]+.025),.035,'tile')
    rod((cx,cy-d/2-.1,z+.92),(cx,cy+d/2+.1,z+.92),.065,'stone')
def temple(w=5,d=3,z=0):
    box((0,0,z+.23),(w+1,d+1,.46),'stone',.04)
    for x in [-w*.43,0,w*.43]:
        for y in [-d*.40,d*.40]:rod((x,y,z+.45),(x,y,z+2.65),.10,'red',n=8)
    box((0,.25,z+1.45),(w*.75,d*.6,2),'wood')
    for x in [-w*.24,0,w*.24]:
        box((x,-d*.065-.72,z+1.5),(.7,.12,1.55),'red')
        for dx in [-.22,0,.22]:box((x+dx,-d*.065-.80,z+1.5),(.035,.035,1.45),'signgold')
    box((0,0,z+2.6),(w,.24,.24),'red')
    tile_roof(0,0,z+2.75,w+1.2,d+1.2)
    stairs(0,-d/2-.4,z,4,w*.44)
def export(idx,name,region,title):
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object
    o.name='SM_ENV_Building_%02d_%s'%(idx,name);o.data.name=o.name+'_Mesh'
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    mod=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in o.data.polygons:p.use_smooth=False
    for c in list(o.users_collection):c.objects.unlink(o)
    lib.objects.link(o)
    o['part_index']=idx;o['slot']='building';o['region']=region;o['title']=title
    bpy.ops.export_scene.gltf(filepath=os.path.join(ROOT,'meshes',o.name+'.glb'),export_format='GLB',use_selection=True,export_yup=True)
    item=dict(mesh=o.name,slot='building',index=idx,file='meshes/'+o.name+'.glb',triangles=len(o.data.polygons),region=region,title=title,notes='Concept-board v2. Exterior static model; no collision, interiors or runtime integration.')
    catalog['meshes'].append(item);catalog['slots']['building'][str(idx)]=o.name
    parts.clear();return o
buildings=[]
def done(*args):buildings.append(export(*args))
# Preserve existing six asset identities and indices, replacing their geometry.
h=shell(5,4,3,'plaster',True)
sign(0,-2.32,2.43,4.4,'STORES','signgold');vertical_sign(-2.68,-1.5,5.3,'ROOM')
for x in [-1.4,0,1.4]:
    box((x,-2.25,5.3),(1.25,.55,.12),'darkstone');railing((x-.6,-2.5),(x+.6,-2.5),5.35,.55)
done(0,'House','01_Commercial','Shop-house / apartments')
h=shell(5.6,4,2,'cream',True)
sign(0,-2.29,2.42,5.3,'24  MART','signblue')
for dx in [-1.6,0,1.6]:box((dx,-2.48,2.03),(1.58,.75,.10),'ochre')
vertical_sign(2.98,-1.3,4.6,'24H','red')
box((2,-2.8,.55),(.62,.55,1.1),'signblue',.035)
done(1,'Market','01_Commercial','Corner convenience store')
h=shell(7,5,4,'white')
sign(0,-2.8,2.45,6.3,'HOSPITAL','red')
box((0,-3.05,2.2),(3.4,1.1,.18),'white')
box((0,-3.51,2.2),(3.3,.05,.15),'red')
box((-2.3,-2.65,9),(.62,.17,1.65),'red');box((-2.3,-2.65,9),(1.4,.17,.55),'red')
for x in [-3.5,3.5]:box((x,-2.57,5.4),(.25,.22,10.8),'stone')
done(2,'Clinic','04_Hospital','Abandoned hospital main wing')
h=shell(4.5,3.6,2,'terracotta',True)
tile_roof(0,0,h+.15,5.8,4.8)
sign(0,-2.05,2.43,3.7,'TEA  /  RECORDS','signgold')
for x in [-1.8,1.8]:
    rod((x,-2.3,2.4),(x,-2.3,3.15),.13,'red',n=8)
    rod((x,-2.3,3.15),(x,-2.3,3.3),.04,'signgold')
done(3,'Archive','01_Commercial','Traditional tea and records shop')
# Rail entry: open tunnel arch around a dark passage (unlike solid painted door).
box((0,0,.18),(8,7,.36),'stone')
for x in [-2.75,2.75]:box((x,0,1.8),(1.5,4,3.6),'concrete')
for i in range(9):
    a=i*math.pi/8
    o=box((2.08*math.cos(a),0,3+2.08*math.sin(a)),(.83,4,.75),'darkstone');o.rotation_euler[1]=math.pi/2-a
for x in [-.67,.67]:rod((x,-3.3,.38),(x,3.3,.38),.055,'steel')
for i in range(12):box((0,-3+i*.55,.32),(2.1,.17,.11),'wood')
sign(0,-2.18,4.7,3.9,'LOST  STATION','cloth')
done(4,'Station','02_Rail','Rail tunnel entrance')
h=shell(6.5,5.5,4,'concrete')
for x in [-2.8,2.8]:box((x,-2.9,h*.5),(.38,.48,h),'darkstone')
sign(0,-3.05,2.5,5.4,'MEMORY  BUREAU','cloth')
box((0,-3.25,1.05),(1.8,.6,2.1),'ink');box((0,-3.57,1.1),(.07,.025,1.5),'memory')
for z in [4.1,6.9,9.7]:box((0,-2.9,z),(1.2,.13,2.1),'blueglass')
done(5,'Bureau','03_Bureau','Memory bureau research block')
# Additional board representatives, append-only asset indices.
for x in [-2.4,2.4]:box((x,0,1.5),(.6,3,3),'concrete')
box((0,0,3.15),(7.5,4,.55),'stone')
for x in [-.6,.6]:rod((x,-1.9,3.52),(x,1.9,3.52),.055,'steel')
for y in [-1.8,-1.3,-.8,-.3,.2,.7,1.2,1.7]:box((0,y,3.45),(2,.14,.1),'wood')
for x in [-3.55,3.55]:railing((x,-2),(x,2),3.43)
for x in [-3.4,3.4]:rod((x,0,3.5),(x,0,5.8),.055,'steel')
box((0,0,5.85),(7.6,.9,.18),'cream')
done(6,'ElevatedPlatform','02_Rail','Elevated railway platform')
h=shell(2.8,2.8,4,'stone')
box((0,0,h+1.2),(3.5,3.5,2),'darkstone')
for x in [-1.15,0,1.15]:window(x,-1.77,h+1.2,.8,1.15)
roof(3.5,3.5,h+2.3,False)
rod((0,0,h+2.5),(0,0,h+4),.045,'steel');box((0,0,h+4),(.14,.14,.18),'red')
done(7,'SignalTower','02_Rail','Rail control tower')
h=shell(6,5,3,'concrete')
for x in [-2.2,-1.1,0,1.1,2.2]:
    box((x,-2.64,4.7),(.65,.16,5.3),'blueglass')
    for z in [2.5,3.2,4,4.8,5.6,6.4,7.1]:box((x,-2.75,z),(.65,.04,.045),'edge')
sign(0,-2.86,1.65,3.8,'DATA  ARCHIVE','cloth')
done(8,'DataCenter','03_Bureau','Secure data center')
for x in [-3,3]:
    box((x,0,2),(.55,1,4),'darkstone');box((x,0,4.05),(.8,1.3,.16),'stone')
box((0,0,3.65),(6.5,1,.55),'concrete');sign(0,-.57,3.68,4.4,'CONTROL  ZONE','cloth')
box((3.4,1.3,1.25),(2.1,2,2.5),'stone');window(3.4,.25,1.7,1.4,.8)
for x in [-2,0,2]:box((x,-1,.4),(.5,1,.8),'stone',.07)
rod((-2,-.5,1.1),(2.5,-.5,1.1),.055,'red')
done(9,'Checkpoint','03_Bureau','Bureau checkpoint gate')
h=shell(4.2,4,4,'plaster')
for x in [-1.7,1.7]:box((x,-2.15,5.5),(.18,.27,11),'white')
for z in [1,3.8,6.6,9.4]:
    box((0,-2.3,z+.7),(3.5,.65,.12),'darkstone');railing((-1.7,-2.62),(1.7,-2.62),z+.76)
sign(0,-2.72,2.5,3.6,'WARD  04','red')
done(10,'HospitalWard','04_Hospital','Abandoned isolation ward')
h=shell(5.5,4,2,'plaster')
box((0,-2.17,1.25),(3.8,.2,2.3),'steel')
for z in [.35,.6,.85,1.1,1.35,1.6,1.85,2.1]:box((0,-2.29,z),(3.65,.03,.04),'darkstone')
sign(0,-2.4,2.6,4.5,'MEDICAL  STORAGE','red')
for x in [-1.5,1.3]:
    box((x,0,h+.8),(1.5,1.5,1.4),'steel',.04)
    rod((x,.4,h+1.3),(x,.4,h+2.2),.18,'darkstone')
done(11,'MedicalStorage','04_Hospital','Medical service and storage annex')
temple(6,4)
temple(4.1,2.8,3.65)
sign(0,-2.17,2.23,2.4,'UNION','red')
done(12,'FusionTemple','05_Cult','Two-tier fusion temple')
for x in [-2.35,2.35]:
    box((x,0,.2),(.9,1,.4),'stone');rod((x,0,.4),(x,0,3.5),.18,'red',n=8)
box((0,0,3.15),(5.4,.45,.4),'red');tile_roof(0,0,3.55,6.5,2.5)
sign(0,-.34,3.05,1.8,'UNION','ink')
for x in [-1.6,1.6]:rod((x,0,2.3),(x,0,2.9),.18,'red',n=8)
done(13,'CultGate','05_Cult','Traditional cult entrance gate')
for z,w in [(.15,5),(.45,4.3),(.75,3.6)]:box((0,0,z),(w,w*.8,.3),'darkstone')
for i in range(4):
    z=.95+i*1.65;box((0,0,z+.62),(1.3,1.3,1.3),'rust')
    tile_roof(0,0,z+1.25,2.6-i*.24,2.6-i*.24)
rod((0,0,7.8),(0,0,9.0),.07,'signgold')
done(14,'RitualPagoda','05_Cult','Ritual pagoda')
h=shell(4.4,3.8,3,'plaster')
# Remove a jagged wedge from the facade using a true mesh boolean.
before=list(parts)
cutter=box((1.8,-1.7,6.5),(2.4,2.2,7.0),'ink');cutter.rotation_euler[1]=-.25
parts.remove(cutter)
for obj in before:
    mod=obj.modifiers.new('Broken edge','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
    bpy.context.view_layer.objects.active=obj
    try:bpy.ops.object.modifier_apply(modifier=mod.name)
    except RuntimeError:obj.modifiers.remove(mod)
bpy.data.objects.remove(cutter,do_unlink=True)
for i in range(9):
    x=random.uniform(-2.5,2.5);y=random.uniform(-3,-2)
    o=box((x,y,.2),(.55,.5,.35),'stone',.06);o.rotation_euler[2]=random.uniform(-1,1)
done(15,'BrokenBuilding','06_Distortion','Fractured city apartment')
h=shell(4,3.8,2,'plaster',roofed=False)
box((0,0,h),(4.2,4,.2),'darkstone')
lower_parts=list(parts)
shell(4,3.8,1,'plaster')
for o in parts:
    if o not in lower_parts:o.location+=Vector((.9,0,7.25))
for x in [-1.7,-.5,1.2]:
    o=box((x,0,6.4),(.6,1,.4),'stone');o.rotation_euler[1]=.3
done(16,'FloatingBuilding','06_Distortion','Apartment with separated upper floors')
h=shell(4.5,4,3,'terracotta',True)
sign(0,-2.3,2.45,3.8,'LOST  STREET','purple')
vertical_sign(-2.5,-1.5,5.2,'LOST','purple')
for o in parts:
    if o.location.z>3:
        t=(o.location.z-3)/6
        o.location.x+=.7*t;o.rotation_euler[1]-=.12*t
done(17,'TwistedShop','06_Distortion','Leaning distorted shop-house')
for z,w in [(.2,5),(.6,4), (1,3)]:box((0,0,z),(w,w,.4),'darkstone')
for x,y,h in [(-1,-1,12),(1,-1,15),(-1,1,10),(1,1,13),(0,0,17)]:
    box((x,y,h/2+1),(1.0,1.0,h),'stone')
    box((x,y-.54,h/2+2),(.10,.04,h*.70),'memory' if x<0 else 'purple')
    for z in range(3,int(h),3):box((x,y,z),(1.2,1.2,.16),'darkstone')
done(18,'MemoryTower','07_Origin','Fragmented memory tower')
for z,w in [(0.3,6),(1.2,5),(3.2,3.8)]:
    box((0,0,z),(w,w,.45),'darkstone')
    for x,y in [(-w/2,-w/2),(w/2,-w/2),(-w/2,w/2),(w/2,w/2)]:rod((x,y,z+.3),(x,y,z+1.6),.09,'stone',.04)
for x in [-1.2,0,1.2]:rod((x,-1.3,3.5),(x,-1.3,6),.10,'memory',.03)
done(19,'FloatingPlatform','07_Origin','Suspended ritual platform')
box((0,0,.2),(7,4,.4),'darkstone')
for x in [-2.2,2.2]:
    for z in [1.2,3.7,6.2]:box((x,0,z),(.9,1.3,2.1),'stone')
    box((x,-.68,3.8),(.08,.04,6.8),'purple')
box((0,0,7.8),(5.4,1.5,.7),'darkstone')
for x in [-1.3,0,1.3]:rod((x,0,8.2),(x,0,10+abs(x)),.12,'memory',.025)
done(20,'OriginGate','07_Origin','Monumental origin gate')
# Catalog and portable previews with linked mesh data.
lib.hide_render=True;lib.hide_viewport=True
show=bpy.data.collections.new('03_BUILDING_CONCEPT_BOARD_V2');scene.collection.children.link(show)
natural=bpy.data.collections.new('04_NATURE_PREVIEW');scene.collection.children.link(natural)
def link(source,col,p):
    o=bpy.data.objects.new(source.name+'_Preview',source.data);col.objects.link(o);o.location=p;return o
regionlist=['01_Commercial','02_Rail','03_Bureau','04_Hospital','05_Cult','06_Distortion','07_Origin']
ordered=[]
for ri,region in enumerate(regionlist):
    row=[i for i in catalog['meshes'] if i.get('region')==region]
    row.sort(key=lambda v:v['index'])
    for ci,item in enumerate(row):
        o=link(bpy.data.objects[item['mesh']],show,(ci*12,ri*15,0));ordered.append(o)
for i,item in enumerate([x for x in catalog['meshes'] if x['slot'] in ['tree','foliage','rock','structure']]):
    link(bpy.data.objects[item['mesh']],natural,((i%6)*7,(i//6)*7,0))
bpy.data.collections['02_NPC_COMBINATION_PREVIEW'].hide_render=True
natural.hide_render=True
scene.render.engine='CYCLES';scene.cycles.samples=32
scene.render.resolution_percentage=100
scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.12,.16,.19,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.6
for o in scene.objects:
    if o.type=='LIGHT':
        if o.data.type=='SUN':o.data.energy=2.3
        else:o.data.energy=3200;o.location=(8,-10,22);o.data.size=16
cam=scene.camera
def aim(pos,target,scale):
    cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=scale
def render(name,pos,target,scale,res):
    aim(pos,target,scale);scene.render.resolution_x,scene.render.resolution_y=res
    scene.render.filepath=os.path.join(ROOT,'previews',name);bpy.ops.render.render(write_still=True)
# Render one row at a time for readable silhouettes and no tall-building occlusion.
for ri,region in enumerate(regionlist):
    for o in show.objects:o.hide_render=(o.location.y!=ri*15)
    row=[o for o in show.objects if not o.hide_render]
    height=max(max(v.co.z for v in o.data.vertices) for o in row)
    render('buildings_'+region+'.png',(37,ri*15-36,26),(12,ri*15,height*.42),39,(1800,1050))
for o in show.objects:o.hide_render=False
# Broad overview, plus commercial hero used as default handoff preview.
render('building_overview.png',(80,-105,120),(12,42,3),118,(1800,2400))
for o in show.objects:o.hide_render=o.location.y!=0
render('environment_catalog.png',(37,-36,26),(12,0,3.8),39,(1800,1050))
show.hide_render=True;natural.hide_render=False
render('nature_catalog.png',(37,-35,32),(17,10,1),46,(1600,1000))
show.hide_render=False;natural.hide_render=True
for o in show.objects:o.hide_render=False
aim((80,-105,120),(12,42,3),118)
catalog['art_revision']=2
catalog['building_reference']='../../doc/로우 폴리 도시 건물 컨셉 보드.png'
with open(os.path.join(ROOT,'data','mesh_catalog.json'),'w',encoding='utf-8') as f:json.dump(catalog,f,ensure_ascii=False,indent=2)
with open(os.path.join(ROOT,'data','building_regions.json'),'w',encoding='utf-8') as f:json.dump(dict(art_revision=2,reference=catalog['building_reference'],regions=regionlist,buildings=[x for x in catalog['meshes'] if x['slot']=='building']),f,ensure_ascii=False,indent=2)
issues=[]
for c in catalog['meshes']:
    o=bpy.data.objects[c['mesh']]
    if not len(o.data.polygons):issues.append(c['mesh']+': empty')
    if any(not math.isfinite(v) for vertex in o.data.vertices for v in vertex.co):issues.append(c['mesh']+': nonfinite')
    if any(len(p.vertices)!=3 for p in o.data.polygons):issues.append(c['mesh']+': not triangulated')
    if not os.path.exists(os.path.join(ROOT,c['file'])):issues.append(c['mesh']+': missing export')
with open(os.path.join(ROOT,'data','asset_validation.json'),'w') as f:json.dump(dict(art_revision=2,mesh_count=len(catalog['meshes']),building_count=len(buildings),total_triangles=sum(c['triangles'] for c in catalog['meshes']),issues=issues,rigged=False,game_integration=False),f,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'LowPolyKit.blend'))
print('CITY_BUILDINGS_COMPLETE',len(buildings),issues)
