# SINGLE SOURCE OF TRUTH for killfeed proportions.
# death.cpp reads these as C ratios; the preview script reads the same file.
#
# MEASURED IN PIXELS from the v5 reference the user approved (1000313081_2):
#   plate height H = 56px   (pitch 58-60px -> ~4px gap between plates)
#   bold name cap  = 22px   (vertically centred in the plate)
#   weapon icon    = 34px   (sniper; the LARGEST icon class)
#   modifier icon  = 27px   (crossed-eye; event/modifier class)
#   -> engine console line height TL ~= cap/0.70 ~= 31px
#
# The user's rule: EVERYTHING is proportional to OUR name text size (the engine
# console font, which we cannot resize). So the anchor is TL. Plate height and
# pitch are keyed to TL; icon/pad/radius are keyed to plate H (H itself = f(TL),
# so the whole feed scales with the one font we have).
#
# CRITICAL ORDERING (this is what the compressed builds got wrong):
#   weapon icon (0.61*H) > modifier icon (0.48*H) > name cap (0.39*H)
#   i.e. icons are a bit taller than the text but NOT huge; text still reads as
#   a first-class element, exactly like the reference.
PLATE_H_OVER_TL = 1.80   # plate height  = 1.80 * text line height
PITCH_OVER_H    = 1.07   # row-to-row    = 1.07 * plate height (~4px gap @ ref)
ICON_OVER_H     = 0.61   # weapon icon   = 0.61 * plate height  (34px @ ref)
MOD_OVER_H      = 0.50   # modifier icon = 0.50 * plate height  (27px @ ref)
GAP_OVER_TL     = 0.34   # element gap   = 0.34 * text line height (small/tidy)
GAPW_OVER_TL    = 0.06   # wing->weapon  = 0.06 * text line height
PADX_OVER_TL    = 0.50   # inner L/R pad = 0.50 * text line height
RADIUS_OVER_H   = 0.16   # corner radius = 0.16 * plate height (9px @ ref)
BORDER_OVER_H   = 0.05   # local border  = 0.05 * plate height (2-3px @ ref)
WING_DY_OVER_H  = -0.20  # wing raised   = -0.20 * plate height
PLATE_ALPHA     = 165    # dark translucent plate (of 255) ~0.65
PLATE_RGB       = (20, 20, 20)   # near-black
