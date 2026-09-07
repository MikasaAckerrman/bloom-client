#!/usr/bin/env python3
"""Раздельный замер: где плита как СРЕДА (много px цвета плиты), где контент.
Плита с текстом: plate_px высокий (>500), std высокий. Чистая плита: std низкий.
Фон: plate_px низкий.
"""
from PIL import Image
import numpy as np
import sys

im = np.array(Image.open(sys.argv[1] if len(sys.argv) > 1
                         else '/var/minis/attachments/uploads/1000312706_1.png').convert('RGB'))
g = im.mean(axis=2)
X0, X1 = 250, 1150

print('y: mean std plate_px  class')
classes = []
for y in range(0, 470):
    row = g[y, X0:X1]
    m, s = row.mean(), row.std()
    p = ((row > 25) & (row < 62)).sum()
    if p > 500:
        cls = 'ПЛИТА(с контентом)' if s >= 18 else 'ПЛИТА(чистая)'
    else:
        cls = 'фон'
    classes.append((y, m, s, p, cls))

prev = None; run = 0
for y, m, s, p, cls in classes:
    if prev is None:
        prev, run = cls, y; continue
    if cls != prev:
        print(f'  y=[{run:3d}..{y-1:3d}] {prev}')
        run, prev = y, cls
print(f'  y=[{run:3d}..469] {prev}')
