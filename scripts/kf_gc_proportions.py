"""Пропорции GoldClient (эталон 632p, замер скриптами kf_ref_measure*) vs наши.

Правило: абсолютный scale эталона неизвестен (кадр мог быть снят с не-дефолтным
cvar), поэтому сравниваем ТОЛЬКО безразмерные отношения внутри строки.
"""
GC = dict(plate=84, text_glyph=30, icon=43, vgap=8, pitch=92)
OUR = dict(plate=33, text_cell=27, icon=19, vgap=3)

def ratios(plate, text, icon, vgap):
    return dict(text_per_plate=text/plate, icon_per_plate=icon/plate,
                vgap_per_plate=vgap/plate)

g = ratios(GC['plate'], GC['text_glyph'], GC['icon'], GC['vgap'])
# наш глиф ≈ 0.7 клетки (шрифтовой кегль): измеряемо 19px глиф при клетке 27
o = ratios(OUR['plate'], 19, OUR['icon'], OUR['vgap'])

print(f"{'отношение':<20}{'GoldClient':>12}{'наши':>10}{'GC/наши':>10}")
for k in g:
    print(f"{k:<20}{g[k]:>12.2f}{o[k]:>10.2f}{(g[k]/o[k]) if o[k] else 0:>10.2f}")

print("""
Целевые отношения GC (за х2.7-x4.5 от наших):
  text_per_plate 0.36 -> у нас 0.58: ТЕКСТ относительно плиты КРУПНЕЕ у нас?? нет:
     text_per_plate = глиф/плита. GC 0.36: глиф занимает 36% плиты. Наш 0.58.
     Значит у нас текст относительно плиты КРУПНЕЕ, а плита теснее. Общий масштаб
     меньше, но текст жмётся. Итог: надо РАСШИРЯТЬ плиту (pady), текст держать.
  icon_per_plate 0.51 vs 0.58: почти норм (иконку чуть меньше относительно).
  vgap_per_plate 0.10 vs 0.09: норм.
""")
