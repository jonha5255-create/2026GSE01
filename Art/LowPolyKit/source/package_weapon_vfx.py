import os,json,math,struct
from PIL import Image,ImageDraw,ImageFont
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT,'data','weapon_vfx_catalog.json'),encoding='utf-8') as fp:cat=json.load(fp)
try:font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',16)
except OSError:font=ImageFont.load_default()
issues=[];checked=[];framesets=[]
for fx in cat['effects']:
 frames=[Image.open(os.path.join(ROOT,'vfx','frames','%s_%02d.png'%(fx['id'],i))).convert('RGBA') for i in range(1,fx['frames']+1)]
 atlas=Image.new('RGBA',(fx['columns']*256,fx['rows']*256))
 for i,im in enumerate(frames):atlas.paste(im,((i%4)*256,(i//4)*256))
 atlas.save(os.path.join(ROOT,fx['sprite_sheet']))
 if not any(im.getchannel('A').getextrema()[1]>0 for im in frames):issues.append([fx['id'],'empty frames'])
 if not all(im.getchannel('A').getextrema()[0]==0 for im in frames):issues.append([fx['id'],'no transparent background'])
 for i,im in enumerate(frames):
  bounds=im.getchannel('A').getbbox()
  if bounds and (bounds[0]==0 or bounds[1]==0 or bounds[2]==256 or bounds[3]==256):issues.append([fx['id'],'clipped frame',i+1])
 raw=open(os.path.join(ROOT,fx['glb']),'rb').read();magic,version,length=struct.unpack_from('<III',raw);n,typ=struct.unpack_from('<II',raw,12);g=json.loads(raw[20:20+n])
 if magic!=0x46546c67 or length!=len(raw):issues.append([fx['id'],'GLB header'])
 mesh_nodes=[o for o in g['nodes'] if 'mesh' in o]
 if len(mesh_nodes)!=len(fx['nodes']):issues.append([fx['id'],'node count'])
 animations=g.get('animations',[])
 if len(animations)!=1:issues.append([fx['id'],'animation count',len(animations)])
 for a in g.get('accessors',[]):
  if any(not math.isfinite(v) for key in ['min','max'] for v in a.get(key,[])):issues.append([fx['id'],'nonfinite bounds'])
 checked.append(dict(id=fx['id'],frames=len(frames),nodes=len(mesh_nodes),animations=[a.get('name') for a in animations],atlas_size=list(atlas.size)))
 preview=[]
 for im in frames:
  bg=Image.new('RGBA',im.size,(11,17,26,255));bg.alpha_composite(im);preview.append(bg.convert('RGB'))
 delays=[round((i+1)*100/30)*10-round(i*100/30)*10 for i in range(len(frames))]
 preview[0].save(os.path.join(ROOT,'vfx',fx['id']+'.gif'),save_all=True,append_images=preview[1:],duration=delays,loop=0,disposal=2)
 framesets.append(frames)
def board(frame):
 canvas=Image.new('RGB',(1024,888),(11,17,26));draw=ImageDraw.Draw(canvas)
 for i,(fx,seq) in enumerate(zip(cat['effects'],framesets)):
  x=(i%4)*256;y=(i//4)*296
  im=seq[min(frame,len(seq)-1)]
  bg=Image.new('RGBA',(256,256),(11,17,26,255));bg.alpha_composite(im);canvas.paste(bg.convert('RGB'),(x,y))
  draw.text((x+10,y+260),fx['id'],font=font,fill=(220,232,242));draw.text((x+10,y+279),'%02d  %s'%(fx['index'],fx['kind']),font=font,fill=(125,151,173))
 return canvas
board(5).save(os.path.join(ROOT,'previews','weapon_vfx_overview.png'))
boards=[board(i) for i in range(24)]
boards[0].save(os.path.join(ROOT,'previews','weapon_vfx_overview.gif'),save_all=True,append_images=boards[1:],duration=[30,40,30]*8,loop=0,disposal=2)
with open(os.path.join(ROOT,'data','weapon_vfx_validation.json'),'w') as fp:json.dump(dict(effects=len(checked),files=checked,issues=issues),fp,indent=2)
print(json.dumps(dict(effects=len(checked),issues=issues)))
assert not issues
