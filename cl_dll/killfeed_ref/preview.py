#!/usr/bin/env python3
"""Phone-accurate preview of the killfeed as death.cpp now lays it out.

Sizing is keyed to the engine console-font line height (TL), exactly like the
C++: iconH=1.18TL, modH=1.34TL, gap=0.30TL, padx=0.42TL, plate H=1.7*TL*scale.
We emulate a landscape phone (2340x1080) with a console font ~22px tall, draw
the feed hugging the TOP-RIGHT corner, and use the bright CS team colours
(g_ColorBlue {0.6,0.8,1.0}, g_ColorRed {1.0,0.25,0.25})."""
import os, struct, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(__file__))
import kf_ratios as R   # SAME constants the C++ uses

SPR = "/tmp/minis-killfeed-bloom-20260822/3rdparty/cs16client-extras/sprites/kf"
OUT = "/var/minis/attachments/killfeed-icons"

# phone landscape + engine-ish font metric
SCR_W, SCR_H = 2340, 1080
TL = 30                                       # console line height at this res
SCALE = 1.0                                   # cl_killfeed_scale
H = int(TL * R.PLATE_H_OVER_TL * SCALE)       # plate height
ICON_H = max(8, int(H*R.ICON_OVER_H)); MOD_H = max(8, int(H*R.MOD_OVER_H))
GAP = max(2,int(TL*R.GAP_OVER_TL)); GAPW = max(1,int(TL*R.GAPW_OVER_TL)); PADX = max(3,int(TL*R.PADX_OVER_TL))
RADIUS = max(3,int(H*R.RADIUS_OVER_H)); BORDER = max(2,int(H*R.BORDER_OVER_H)); WING_DY=int(H*R.WING_DY_OVER_H)
PITCH = int(H*R.PITCH_OVER_H)
PLATE_A = R.PLATE_ALPHA; PLATE_RGB = R.PLATE_RGB
MARGIN_TOP = int(SCR_H*8/480); MARGIN_RIGHT = int(SCR_W*6/640)

CT=(153,204,255); T=(255,63,63)   # g_ColorBlue/Red *255

def spr_size(name):
    with open(os.path.join(SPR,name+".spr"),"rb") as f: b=f.read()
    _,_,_,_,w,h,_,_,_=struct.unpack("<3i f 2i i f I", b[:36]); return w,h
def spr_img(name):
    with open(os.path.join(SPR,name+".spr"),"rb") as f: b=f.read()
    _,_,_,_,w,h,_,_,_=struct.unpack("<3i f 2i i f I", b[:36]); px=b[36+4+16:]
    im=Image.new("RGBA",(w,h)); p=im.load()
    for y in range(h):
        for x in range(w):
            o=(y*w+x)*4; r,g,bb,a=px[o],px[o+1],px[o+2],px[o+3]
            p[x,y]=(255,255,255,max(r,g,bb))
    return im
def font(sz):
    for p in ("/usr/share/fonts/ttf-dejavu/DejaVuSans-Bold.ttf",):
        if os.path.exists(p): return ImageFont.truetype(p,sz)
    return ImageFont.load_default()
FN=font(int(H*0.54))      # name cap ends ~0.39*H like the approved reference
def iconw(name,h): w,sh=spr_size(name); return round(w*h/sh)

def draw_row(img, rightX, topY, cfg, alpha=1.0, dx=0):
    d=ImageDraw.Draw(img,"RGBA")
    def txtw(s): return int(d.textlength(s,font=FN))
    def W():
        w=PADX; first=[True]
        def adv(px):
            nonlocal w
            if not first[0]: w+=GAP
            w+=px; first[0]=False
        for m in cfg.get("pre",[]): adv(iconw(m,MOD_H))
        if cfg.get("killer"): adv(txtw(cfg["killer"]))
        if cfg.get("assist"):
            adv(iconw("flashbang_assist",MOD_H)); adv(txtw(cfg["assist"]))
        if cfg.get("wing"):
            if not first[0]: w+=GAP
            w+=iconw(cfg["wing"],MOD_H); first[0]=False; w+=GAPW; w+=iconw(cfg["wpn"],ICON_H)
        else: adv(iconw(cfg["wpn"],ICON_H))
        for m in cfg.get("mid",[]): adv(iconw(m,MOD_H))
        if cfg.get("victim"): adv(txtw(cfg["victim"]))
        return w+PADX
    w=W(); x=rightX-w+dx; y=topY; cy=y+H//2; ty=cy
    a=int((170 if cfg.get("local") else PLATE_A)*alpha)
    pr,pg,pb=PLATE_RGB
    if cfg.get("mydeath"): pr,pg,pb=90,22,22; a=min(235,a+40)
    d.rounded_rectangle([x,y,x+w-1,y+H-1],radius=RADIUS,fill=(pr,pg,pb,a))
    if cfg.get("local"):
        for t in range(BORDER):
            d.rounded_rectangle([x+t,y+t,x+w-1-t,y+H-1-t],radius=max(1,RADIUS-t),outline=(255,36,36,255))
    tint=int(255*alpha); cx=x+PADX; first=True
    def blit(name,h,dy=0):
        nonlocal cx
        iw=iconw(name,h); im=spr_img(name).resize((iw,h),Image.LANCZOS)
        al=im.split()[3]; col=Image.new("RGBA",im.size,(tint,tint,tint,0)); col.putalpha(al)
        img.alpha_composite(col,(cx,cy-h//2+dy)); cx+=iw
    def text(s,col):
        nonlocal cx
        d.text((cx,ty),s,font=FN,fill=tuple(int(c*alpha) for c in col)+(255,),anchor="lm")
        cx+=int(d.textlength(s,font=FN))
    for m in cfg.get("pre",[]):
        if not first: cx+=GAP
        blit(m,MOD_H); first=False
    if cfg.get("killer"):
        if not first: cx+=GAP
        text(cfg["killer"],CT if cfg["kt"]=="ct" else T); first=False
    if cfg.get("assist"):
        if not first: cx+=GAP
        blit("flashbang_assist",MOD_H); cx+=GAP; text(cfg["assist"],CT if cfg["at"]=="ct" else T); first=False
    if cfg.get("wing"):
        if not first: cx+=GAP
        blit(cfg["wing"],MOD_H,WING_DY); first=False; cx+=GAPW; blit(cfg["wpn"],ICON_H)
    else:
        if not first: cx+=GAP
        blit(cfg["wpn"],ICON_H); first=False
    for m in cfg.get("mid",[]):
        cx+=GAP; blit(m,MOD_H)
    if cfg.get("victim"):
        cx+=GAP; text(cfg["victim"],CT if cfg["vt"]=="ct" else T)
    return w

ROWS=[
    dict(pre=["blind"],killer="Gunner",kt="ct",wing="inair",wpn="awp",mid=["noscope","smoke"],victim="KILLER",vt="t"),
    dict(killer="WASD",kt="t",wpn="hegrenade",victim="Gordon",vt="ct"),
    dict(killer="!defa",kt="t",wpn="ak47",mid=["penetrate"],victim="Spaceman",vt="ct"),
    dict(killer="Psycho",kt="ct",wpn="flashbang",victim="Boss",vt="t"),
    dict(killer="Crazyf",kt="ct",wpn="m4a1",mid=["headshot"],victim="theKing",vt="t"),
    dict(killer="GoldPlayer",kt="ct",assist="Albert",at="ct",wpn="deagle",mid=["headshot"],victim="GabeN",vt="t",local=True),
]

# dark, near-black striped backdrop (matches reference, NOT the green test bg)
img=Image.new("RGBA",(SCR_W,SCR_H),(29,26,21,255))
d=ImageDraw.Draw(img)
for i in range(0,SCR_W,90): d.line([(i,0),(i+40,SCR_H)],fill=(38,34,28,120),width=30)
d.rectangle([0,SCR_H-140,SCR_W,SCR_H],fill=(16,15,13,255))  # hud bar hint
d.text((40,40),"phone landscape — killfeed в правом верхнем углу (эталонные пропорции)",font=font(30),fill=(255,255,255,200))

y=MARGIN_TOP
for cfg in ROWS:
    draw_row(img,SCR_W-MARGIN_RIGHT,y,cfg); y+=PITCH
img.convert("RGB").save(os.path.join(OUT,"phone_preview.png"))

# also a tight crop of the corner at 100%
crop=img.crop((SCR_W-980,0,SCR_W,y+20)).convert("RGB")
crop.save(os.path.join(OUT,"phone_corner.png"))
print("saved phone_preview.png + phone_corner.png  H=%d TL=%d pitch=%d"%(H,TL,PITCH))
