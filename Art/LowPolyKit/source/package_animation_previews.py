"""Assemble rendered PNG keyframes as GIFs/contact sheets using Pillow."""
from PIL import Image, ImageDraw
import os,json
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT,'data','preview_sequences.json')) as f:data=json.load(f)
for s in data['sequences']:
    frames=[Image.open(os.path.join(ROOT,'previews',p)).convert('RGB') for p in s['frames']]
    if 'duration_s' in s:
        total=s['duration_s']*1000
        durations=[max(10,round((i+1)*total/len(frames)/10)*10-round(i*total/len(frames)/10)*10) for i in range(len(frames))]
    else:durations=max(35,s['duration_ms'])
    frames[0].save(os.path.join(ROOT,'previews',s['id']+'.gif'),save_all=True,append_images=frames[1:],duration=durations,loop=0)
sheet=Image.new('RGB',(1060,7*400),(24,32,38));draw=ImageDraw.Draw(sheet)
for row,item in enumerate(data['weapons']):
    draw.text((15,row*400+12),item['weapon'],fill=(235,228,207))
    for col,path in enumerate(item['poses']):
        im=Image.open(os.path.join(ROOT,'previews',path)).convert('RGB');sheet.paste(im,(70+col*325,row*400+35))
sheet.save(os.path.join(ROOT,'previews','weapon_attack_keyposes.png'))
print('GIFS_AND_CONTACT_SHEET_PACKAGED')
