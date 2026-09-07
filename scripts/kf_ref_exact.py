#!/usr/bin/env python3
"""Точный реверс киллфида GoldClient по цвету плиты (38,34,31).

Плита узнаваема по цвету с допуском. Мерен каждый элемент:
- плиты: x0..x1, y0..y1, радиус углов
- иконки: позиции x, высоты
- текст: bbox заглавных
- обводка outline-строки
"""
from PIL import Image
import numpy as np
import sys

REF = '/var/minis/attachments/uploads/1000312706_1.png'
im = np.array(Image.open(REF).convert('RGB')).astype(int)
H, W = im.shape[:2]
print(f'кадр {W}x{H}')

def plate_mask(im):
    r, g, b = im[:,:,0], im[:,:,1], im[:,:,2]
    return (abs(r-38)<=3) & (abs(g-34)<=3) & (abs(b-31)<=3) & (b < r) & (r < g+8)

def runs(bools, minlen):
    out = []
    start = None
    for i, v in enumerate(bools):
        if v and start is None: start = i
        elif not v and start is not None:
            if i - start >= minlen: out.append((start, i-1))
            start = None
    if start is not None and len(bools)-start >= minlen:
        out.append((start, len(bools)-1))
    return out

m = plate_mask(im)
# строка: >=40% ширины - плита
rowfrac = m.sum(axis=1) / W
rows = runs(rowfrac > 0.4, 8)
print(f'\nплитные полосы (rowfrac>0.4, >=8px):')
for (a,b) in rows:
    print(f'  y={a}..{b}  h={b-a+1}')

# для каждой полосы — точный x-диапазон и углы
print('\nгеометрия каждой плиты:')
for (a,b) in rows:
    band = m[a:b+1]
    colfrac = band.sum(axis=0) / (b-a+1)
    cols = runs(colfrac > 0.5, 30)
    for (x0,x1) in cols:
        # углы: в верхних 3 строках плиты ширина меньше полной
        top_w = band[0, x0:x1+1].sum()
        full_w = x1-x0+1
        print(f'  y={a}..{b} h={b-a+1}  x={x0}..{x1} w={full_w}  top_row_w={top_w}')
