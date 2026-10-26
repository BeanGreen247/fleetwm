# Which image format for the VNC picture? Size, encode time and quality of JPEG variants, WebP, PNG and zlib on four 1280x800
# test pictures (run from the repository root; needs Pillow, numpy, scipy). Quality is luma SSIM (7x7 window), target 0.985.
import io, time, zlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import uniform_filter
def luma(a): return (0.299*a[...,0]+0.587*a[...,1]+0.114*a[...,2]).astype(np.float64)
def ssim(a,b):
    x,y=luma(a),luma(b); C1,C2=6.5025,58.5225
    mx,my=uniform_filter(x,7),uniform_filter(y,7)
    vx=uniform_filter(x*x,7)-mx*mx; vy=uniform_filter(y*y,7)-my*my; cxy=uniform_filter(x*y,7)-mx*my
    return float(np.mean(((2*mx*my+C1)*(2*cxy+C2))/((mx*mx+my*my+C1)*(vx+vy+C2))))
rng=np.random.default_rng(1)
imgs={'ui':Image.open('docs/images/debug-overlay.png').convert('RGB'),'wallpaper':Image.open('perflog/2026-10-20/vm-scenes/wp7.png').convert('RGB')}
t=Image.new('RGB',(1280,800),(250,250,250)); d=ImageDraw.Draw(t); f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
for i in range(50): d.text((20,10+i*15),("The quick brown fox jumps over the lazy dog 0123456789 "*3)[:150]+str(i),fill=(20,20,20),font=f)
imgs['text']=t
yy,xx=np.mgrid[0:800,0:1280]
v=np.stack([128+90*np.sin(xx/170.0+yy/230.0),128+90*np.sin(xx/90.0-yy/310.0),128+90*np.cos(xx/250.0+yy/120.0)],-1)+rng.normal(0,1.5,(800,1280,3))
imgs['video']=Image.fromarray(np.clip(v,0,255).astype(np.uint8))
def enc(img,fmt,q):
    b=io.BytesIO()
    if fmt=='jpeg420': img.save(b,'JPEG',quality=q,subsampling=2)
    elif fmt=='jpeg444': img.save(b,'JPEG',quality=q,subsampling=0)
    elif fmt=='jpeg420opt': img.save(b,'JPEG',quality=q,subsampling=2,optimize=True)
    elif fmt=='jpeg420prog': img.save(b,'JPEG',quality=q,subsampling=2,progressive=True,optimize=True)
    elif fmt=='webp_m0': img.save(b,'WEBP',quality=q,method=0)
    elif fmt=='webp_m4': img.save(b,'WEBP',quality=q,method=4)
    elif fmt=='webp_ll': img.save(b,'WEBP',lossless=True,quality=q,method=0)
    elif fmt=='png1': img.save(b,'PNG',compress_level=1)
    elif fmt=='png6': img.save(b,'PNG',compress_level=6)
    return b.getvalue()
dec=lambda d: np.array(Image.open(io.BytesIO(d)).convert('RGB'))
def timeit(img,fmt,q,n=3):
    best=1e9
    for _ in range(n):
        t0=time.perf_counter(); d=enc(img,fmt,q); best=min(best,time.perf_counter()-t0)
    return d,best*1000
TARGET=0.985
lossy=['jpeg420','jpeg444','jpeg420opt','jpeg420prog','webp_m0','webp_m4']
print("SSIM target %.3f, 1280x800, best of 3 encode time"%TARGET)
for name,img in imgs.items():
    a=np.array(img); raw=a.size; rows=[]
    print(f"\n== {name}  (raw {raw/1024:.0f} KB)")
    for fmt in lossy:
        pick=None
        for q in range(30,100,5):
            d=enc(img,fmt,q); s=ssim(a,dec(d))
            if s>=TARGET: pick=(q,d,s); break
        if pick is None: print(f"  {fmt:12s} never reaches the target"); continue
        q,d,s=pick; _,ms=timeit(img,fmt,q); rows.append((len(d),fmt,q,s,ms))
    for fmt in ['webp_ll','png1','png6']:
        d,ms=timeit(img,fmt,80,2); rows.append((len(d),fmt,'-',1.0,ms))
    rows.append((len(zlib.compress(a.tobytes(),3)),'zlib3 raw','-',1.0,0))
    for size,fmt,q,s,ms in sorted(rows):
        print(f"  {fmt:12s} q={q!s:>3} size={size/1024:8.1f} KB  ratio={raw/size:6.1f}x  ssim={s:.4f}  enc={ms:7.1f} ms")
