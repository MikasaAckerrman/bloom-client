#!/usr/bin/env python3
"""Плиты как единые блоки: полосы сливаются, если разрыв < 24px.
После слияния: x-диапазон = объединение, углы через полноту краевых строк."""
from PIL import Image
import numpy as np

REF = '/var/minis/attachments/uploads/1000312706_1.png'
im = np.array(Image.open(REF).convert('RGB')).astype(int)
H, W = im.shape[:2]
r, g, b = im[:,:,0], im[:,:,1], im[:,:,2]
m = (abs(r-38)<=3) & (abs(g-34)<=3) & (abs(b-31)<=3)

def runs(bools, minlen):
    out, start = [], None
    for i, v in enumerate(bools):
        if v and start is None: start = i
        elif not v and start is not None:
            if i-start >= minlen: out.append((start, i-1))
            start = None
    if start is not None and len(bools)-start >= minlen:
        out.append((start, len(bools)-1))
    return out

rowfrac = m.sum(axis=1) / W
bands = runs(rowfrac > 0.3, 8)

# сливаем: разрыв <= 40px (контент внутри плиты до ~35px высотой)
merged = []
for s,e in bands:
    if merged and s - merged[-1][1] <= 40:
        merged[-1] = (merged[-1][0], e)
    else:
        merged.append((s,e))

print(f'плит (слиянных): {len(merged)}')
plates = []
for (a,b) in merged:
    band = m[a:b+1]
    cols = band.any(axis=0)
    xs = runs(cols, 30)
    for (x0,x1) in xs:
        w = x1-x0+1
        if w < 80: continue  # мусор
        plates.append((a,b,x0,x1,w))
        print(f'  y={a}..{b} h={b-a+1}  x={x0}..{x1} w={w}')

# Радиус углов: в верхней строке плиты y=a ширина заполнения меньше w.
print('\nуглы (верхняя строка vs полная ширина):')
for (a,b,x0,x1,w) in plates[:6]:
    band = m[a:b+1, x0:x1+1]
    top = band[0].sum(); bot = band[-1].sum()
    print(f'  y={a}: top_row={top}/{w} ({100*top//w}%)  bot_row={bot}/{w}')

# vgap между плитами
print('\nvgap между соседними плитами по y:')
for i in range(1, len(plates)):
    prev, cur = plates[i-1], plates[i]
    if cur[0] > prev[1]:
        print(f'  {prev[1]} -> {cur[0]}: gap={cur[0]-prev[1]-1}')
