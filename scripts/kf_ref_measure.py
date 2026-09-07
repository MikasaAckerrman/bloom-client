#!/usr/bin/env python3
"""Чистый финальный замер эталона GoldClient (1000312706_1.png, 1260x632).

Метод не зависит от порогов-угадайок: строка плиты определяется ОДНОРОДНОСТЬЮ
(std по ширине) + заполненностью цветом плиты. Печатает полный профиль,
чтобы числа можно было перепроверить глазами.
"""
from PIL import Image
import numpy as np
import sys

im = np.array(Image.open(sys.argv[1] if len(sys.argv) > 1
                         else '/var/minis/attachments/uploads/1000312706_1.png').convert('RGB'))
g = im.mean(axis=2)
H, W = g.shape
print(f'кадр {W}x{H}')

# Полоса x, где живут плиты (правый верхний киллфид): замеряю по всей ширине,
# но в статистику берём x=250..1150 (внутри плит, без левого края).
X0, X1 = 250, 1150

def row_stats(y):
    row = g[y, X0:X1]
    mean = row.mean()
    std = row.std()
    inplate = ((row > 25) & (row < 62)).sum()
    return mean, std, inplate

print('\ny: mean std plate_px   verdict')
verdict = []
for y in range(0, 460):
    m, s, p = row_stats(y)
    is_plate = (p > 600 and s < 18)
    verdict.append((y, m, s, p, is_plate))

# печать со сжатием одинаковых строк
prev = None
run_start = 0
for y, m, s, p, isp in verdict:
    key = isp
    if prev is None:
        prev, run_start = key, y
        continue
    if key != prev:
        print(f'  y=[{run_start:3d}..{y-1:3d}] {"ПЛИТА" if prev else "фон/контент"}')
        run_start = y
        prev = key
print(f'  y=[{run_start:3d}..459] {"ПЛИТА" if prev else "фон/контент"}')
