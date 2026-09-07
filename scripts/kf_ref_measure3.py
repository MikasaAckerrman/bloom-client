"""Столбцовый профиль: колонка x=1250 (правый край, только плита без иконок)."""
from PIL import Image
import numpy as np
im = np.array(Image.open('/var/minis/attachments/uploads/1000312706_1.png').convert('RGB'))
g = im.mean(axis=2)
W = im.shape[1]
for x in (1150, 1200, 1250, 630):
    col = g[:, x]
    print(f'=== колонка x={x} (из {W}) ===')
    prev = None
    for y in range(0, 632, 2):
        v = col[y]
        # печатаем только смены состояния: фон(~20-28) / плита(30-60) / ярче
        state = 'BG' if v < 29 else ('PLATE' if v < 62 else 'BRIGHT')
        if state != prev:
            print(f'  y={y:3d}: {v:5.1f} -> {state}')
            prev = state
