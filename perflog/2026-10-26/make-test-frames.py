import numpy as np
from PIL import Image, ImageDraw, ImageFont
R="/home/bean/git/fleetwm/"
def dump(name,img):
    a=np.array(img.convert('RGB')); x=np.zeros((a.shape[0],a.shape[1],4),np.uint8); x[...,0]=a[...,2]; x[...,1]=a[...,1]; x[...,2]=a[...,0]
    open(f'{name}.raw','wb').write(x.tobytes()); open(f'{name}.dim','w').write(f"{a.shape[1]} {a.shape[0]}")
dump('ui',Image.open(R+'docs/images/debug-overlay.png')); dump('wallpaper',Image.open(R+'perflog/2026-10-20/vm-scenes/wp7.png'))
t=Image.new('RGB',(1280,800),(250,250,250)); d=ImageDraw.Draw(t); f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
for i in range(50): d.text((20,10+i*15),("The quick brown fox jumps over the lazy dog 0123456789 "*3)[:150]+str(i),fill=(20,20,20),font=f)
dump('text',t)
rng=np.random.default_rng(1); yy,xx=np.mgrid[0:800,0:1280]
v=np.stack([128+90*np.sin(xx/170.0+yy/230.0),128+90*np.sin(xx/90.0-yy/310.0),128+90*np.cos(xx/250.0+yy/120.0)],-1)+rng.normal(0,1.5,(800,1280,3))
dump('video',Image.fromarray(np.clip(v,0,255).astype(np.uint8)))
