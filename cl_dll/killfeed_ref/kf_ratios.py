# SINGLE SOURCE OF TRUTH for killfeed proportions.
#
# The C++ (cl_dll/include/killfeed_layout.h + death.cpp) and the preview/render
# scripts in this directory MUST agree. Change a number here and in the C++ in
# the same commit, or the previews stop predicting the game.
#
# ---- THE ONE SCALE ---------------------------------------------------------
# Everything derives from a SINGLE number: scale(). Nothing below may introduce
# its own base. This is the fix for rows that came out a different size on every
# screen and at every cl_killfeed_scale value.
#
# WHY not the console font height: the engine HUD font is a fixed raster
# (engine/client/cl_scrn.c SCR_LoadCreditsFont loads it as raster *
# hud_fontscale, FCVAR_LATCH, default 1.0). It does NOT change with the screen
# resolution. Text keyed to it stayed put while icons keyed to ScreenHeight
# grew, so the plate -- sized by max(text, icons) -- changed shape per device.
#
# WHY ScreenHeight and not ScreenWidth: width varies with aspect ratio (a 20:9
# phone is ~2.2x wider than tall), so width-keyed sizes explode in landscape.
#
# GoldClient keys to the HEIGHT too. PROVEN from client.dll, not assumed: the
# killfeed loads its base from the BSS word at 0x101f3684 (0x10061917
# `movd xmm0, [0x101f3684]`) and divides it by 960.0 (0x10061949
# `divss xmm0, [0x1017df00]`, and [0x1017df00] reads 960.0).
#
# That 0x101f3684 is the HEIGHT is established at 0x10016423..0x1001644a: the
# pair {0x101f3680, 0x101f3684} places a widget bottom-right, 0x101f3680 -> the
# X argument (`sub esi, 0xa`, a 10px right inset), 0x101f3684 -> the Y argument
# (`sub ecx, eax` with eax a text height). Cross-check: the aspect ratio at
# 0x10016307 divides the SAME 0x101f3684 by 480.0 -- GoldSrc's canonical HEIGHT.
#
# An earlier version of this comment cited 0x10061944 as the divide. That
# address holds `mov eax, [0x101b0764]` and takes no part in it. The conclusion
# was right, the evidence was not -- do not "fix" the axis by re-reading it.
#
# UNITS: screen_h is the engine's VIRTUAL height (m_scrinfo, already divided by
# hud_scale), the same space every killfeed draw call uses.

import struct as _struct

REF_H      = 1080.0   # reference frame height the numbers below were measured at
SCALE_MIN  = 0.45     # GoldClient's own floor: [0x1017d878] reads 0.449999988
                      # and the killfeed applies it as a LOWER bound at
                      # 0x10061a99 (`maxss xmm2, xmm0`). Verified, not inferred.
SCALE_MAX  = 4.0      # clamp ceiling so a fat cvar cannot fill the screen

# ---- row metrics, in REFERENCE pixels (at REF_H) ---------------------------
# MEASURED on the native GoldClient frame (1920x1080). Cross-checks that made
# these self-consistent:
#   plate height 33 = text cell 27 + 2*pady 3
#   row pitch    36 = plate 33 + vgap 3
#   icon box     19 = 32px texture * 0.607 shared scale
#   '+' box       7 = 12px texture * 0.607   (7/19 == 12/32 -> ONE scale)
REF_TEXT_H   = 27.0   # text cell height: plate 33 - 2*pady
REF_PADX     = 13.0   # plate edge -> first/last element box
REF_PADY     = 3.0    # plate edge -> content, top and bottom
REF_GAP      = 8.0    # between element boxes (median 8.4, n=20)
REF_GAP_TIGHT = 1.0   # wing -> weapon: boxes essentially touch
REF_VGAP     = 3.0    # between consecutive plates -- MEASURED, not GoldClient's
                      # hud_deathnotice_gap default of 4. Re-measured 2026-09-05
                      # on the native 1920x1080 frame: plates at y21/57/93/129/
                      # 165 are 33px with FIVE 3px gaps and a constant 36px
                      # pitch; the 37px sixth plate is the outlined local row
                      # (33 + 2*overhang). GoldClient's default is really "4"
                      # (pushed at client.dll 0x1000165b), but the frame shows 3
                      # and the frame is what we copy. Also: their gap does NOT
                      # scale (int, added directly at 0x10061df3), ours does.
REF_CORNER   = 3.5    # plate corner bevel radius
REF_OUTLINE  = 3.0    # local-player plate border thickness
REF_MARGIN_X = 22.0   # feed right edge -> screen right edge
REF_MARGIN_Y = 21.0   # feed top -> screen top
REF_RAISE    = 11.0   # airborne wing: lift above the row centre

# Icon calibration: a 32px GoldClient combat texture draws 19px tall on the
# reference, so any texture is drawn at texH * (19/32) * scale.
ICON_TEX_REF = 32.0
ICON_BOX_REF = 19.0

# WHY a shared ratio does not contradict GoldClient's own formula.
#
# GoldClient computes a PER-ICON scale (client.dll 0x10061a73..0x10061a9d):
#     scale[i] = max(0.45, fontTall / naturalIconH * base)
# stored one slot per icon. Read literally that normalises every icon to the
# font height. It comes out the same anyway: of the 39 shipped GoldClient
# killfeed sprites, 37 are exactly 32px tall (widths vary 24..117), so with
# naturalIconH constant a per-icon scale IS a shared scale.
#
# Exceptions: inair_kill 64x64 has its own ratio (WING_* below); plus 12x12 uses
# the shared ratio, which is what the frame shows -- '+' draws 7px next to 19px
# combat icons, while GoldClient's formula on a 12px texture would have drawn it
# the same height as them. So naturalIconH there is NOT the sprite's own height;
# it lives in a runtime texture field ([ecx+0xc]) and cannot be read statically.
# Do not "align" this with the disassembly: it would break the reference frame.

# The airborne wing has its OWN measured ratio: fitting the drawn box by IoU
# against the reference pixels gives 25px from a 64px texture (k = 0.39), while
# the same fit on the weapon in the same row gives 20px from 32px (k = 0.625).
# One shared ratio cannot produce both. This is not a second scale -- it is a
# per-asset constant, and both are multiplied by the same scale().
WING_TEX_REF = 64.0
WING_BOX_REF = 25.0

# Plate colour. GoldClient scheme BgColor (46,43,42) at alpha 136 -- a faint
# lift over the scene (+6..+9 luminance on the reference), not a heavy tile.
# imm32 0x882a2b2e, written at client.dll 0x1006123e right before the GetColor
# call for "DeathNotice/BgColor" (ApplySchemeSettings 0x100611B0).
PLATE_RGB   = (46, 43, 42)
PLATE_ALPHA = 136

# Local-player outline colour, scheme OutlineFgColor: imm32 0xff1717ee written at
# client.dll 0x10061484.
#
# CONFIRMED IN PIXELS 2026-09-05 (workspace/uicopy-kfgold/outlinecolor.py): the
# outlined row on the native 1080p frame occupies y201..y237, and sampling only
# inside the feed (x 1700..1909, so the game scene cannot swamp the median) gives
# a band core of (191,36,34). Distances: OutlineFgColor 50.0,
# HighlightBgColorDead2 (240,45,45) 51.0, HighlightBgColorKill2 (94,146,203)
# 223.8. Blue is ruled out by a wide margin; the two reds are one point apart and
# CANNOT be told apart from a 3px band in a JPEG -- the measured value is darker
# than both because of compression and the plate bleeding through. Stated as a
# limit, not resolved by guessing.
OUTLINE_RGB = (238, 23, 23)

# NOT IMPLEMENTED, deliberately: GoldClient's scheme also carries four highlight
# colours for "this kill involves me", verified from the same function --
#   HighlightBgColorKill  (36,45,211,62)   0x3ed32d24 @ 0x10061300  fill,   I killed
#   HighlightBgColorDead  (225,65,65,105)  0x694141e1 @ 0x10061361  fill,   I died
#   HighlightBgColorKill2 (94,146,203,255) 0xffcb925e @ 0x100613c2  outline,I killed
#   HighlightBgColorDead2 (240,45,45,255)  0xff2d2df0 @ 0x10061423  outline,I died
# The alphas tell the structure: 62/105 are translucent FILLS, 255 are opaque
# OUTLINES, and GoldClient distinguishes killing from dying (blue vs red).
#
# This build draws one red outline for any bLocal row and no fill. That matches
# the reference: the outlined row there is red (measured above), and neither
# reference frame shows a highlight fill at all. Implementing the four would mean
# drawing something the reference does not show, so it stays out until there is a
# frame that shows it.

# Name colours: CT = steel blue, T = amber/gold. MEASURED from the reference the
# user approved (screenshot 1000312966.png), glyph cores only, so antialiasing
# toward the plate does not drag the value. Must match death.cpp's s_kfColor*
# and the cl_killfeed_{ct,t,icon}_color cvar defaults.
CT_RGB   = (129, 154, 202)
T_RGB    = (221, 195, 135)
GREY_RGB = (204, 204, 204)   # icons and neutral text

EXIT_MS = 220.0  # fade-out duration on expiry; there is no fade-in


# --- float32 emulation -------------------------------------------------------
# The C code computes in `float` (32-bit). Python floats are doubles, and the
# difference can land on the int-truncation boundary. So the mirror rounds to
# float32 after EVERY operation, in the same order as the C, or the preview
# would predict a different pixel than the game.
def _f32(x):
    return _struct.unpack("f", _struct.pack("f", x))[0]


# --- the one scale -----------------------------------------------------------
def scale(screen_h, user_scale):
    """Mirror of kf_scale() in killfeed_layout.h."""
    if screen_h <= 0:
        return SCALE_MIN
    if user_scale <= 0.0:
        user_scale = 1.0
    s = _f32(_f32(_f32(screen_h) / _f32(REF_H)) * _f32(user_scale))
    if s < SCALE_MIN:
        return SCALE_MIN
    if s > SCALE_MAX:
        return SCALE_MAX
    return s


def px(ref_px, s):
    """Mirror of kf_px(): reference-space length -> screen pixels."""
    return int(_f32(_f32(ref_px) * _f32(s)) + 0.5)


def metrics_from_scale(s, font_raster_h):
    """Mirror of kf_metrics_from_scale(). Returns a dict of SCREEN pixels."""
    m = dict(
        scale=s,
        textH=px(REF_TEXT_H, s),
        padx=px(REF_PADX, s),
        pady=px(REF_PADY, s),
        gap=px(REF_GAP, s),
        gapTight=px(REF_GAP_TIGHT, s),
        vgap=px(REF_VGAP, s),
        corner=px(REF_CORNER, s),
        outline=px(REF_OUTLINE, s),
        marginX=px(REF_MARGIN_X, s),
        marginY=px(REF_MARGIN_Y, s),
        raise_=px(REF_RAISE, s),
    )
    # floors: a metric that rounds to 0 would visually merge elements.
    # gapTight is allowed to be 0 -- on the reference the wing touches the gun.
    for k in ("textH", "padx", "pady", "gap", "vgap", "corner", "outline"):
        if m[k] < 1:
            m[k] = 1
    m["textScale"] = (_f32(_f32(m["textH"]) / _f32(font_raster_h))
                      if font_raster_h > 0 else 1.0)
    return m


def compute_metrics(screen_h, user_scale, font_raster_h):
    """Mirror of kf_compute_metrics(): resolution-driven, needs scalable text.

    Includes the raster-font floor: the engine's HUD font must never be asked to
    shrink (a bitmap font loses strokes when resampled down), so the scale is
    floored at the point where the text cell equals the font's own height. Below
    that the feed follows the FONT rather than the resolution."""
    s = scale(screen_h, user_scale)
    if font_raster_h > 0:
        s_font = _f32(_f32(font_raster_h) / _f32(REF_TEXT_H))
        if s_font > SCALE_MAX:
            s_font = SCALE_MAX
        if s < s_font:
            s = s_font
    return metrics_from_scale(s, font_raster_h)


def compute_metrics_for_font(font_raster_h, user_scale=1.0):
    """Mirror of kf_compute_metrics_for_font(): fallback when the engine cannot
    scale text. Keys the family to the font's own height instead of the screen,
    and user_scale multiplies everything EXCEPT the text cell (the glyphs are
    drawn at their natural size on this path)."""
    if font_raster_h < 1:
        font_raster_h = 1
    if user_scale <= 0.0:
        user_scale = 1.0
    s = _f32(_f32(_f32(font_raster_h) / _f32(REF_TEXT_H)) * _f32(user_scale))
    if s < SCALE_MIN:
        s = SCALE_MIN
    if s > SCALE_MAX:
        s = SCALE_MAX
    m = metrics_from_scale(s, font_raster_h)
    m["textScale"] = 1.0
    m["textH"] = font_raster_h
    return m


def icon_height(tex_h, s, max_h=0):
    """Mirror of kf_icon_height(): ONE scale for the whole feed, so the relative
    sizes the artist drew survive. '+' (12x12) -> 7x7 while combat icons
    (32x32) -> 19x19 on the reference; 7/19 == 12/32.

    CLAMPED to max_h (the text cell) when max_h > 0: all six reference plates are
    33px = textH 27 + 2*pady 3, and a 64px texture at the shared scale would be
    38px, which would have made that row's plate 44px. It did not.
    """
    if tex_h <= 0:
        return 0
    h = int(_f32(_f32(_f32(tex_h) * _f32(ICON_BOX_REF / ICON_TEX_REF))
                 * _f32(s)) + 0.5)
    if max_h > 0 and h > max_h:
        h = max_h
    return h if h >= 1 else 1


def wing_height(tex_h, s):
    """Mirror of kf_wing_height(): the airborne wing's own measured ratio, and
    NOT clamped to the text cell -- it overhangs the plate on purpose."""
    if tex_h <= 0:
        return 0
    h = int(_f32(_f32(_f32(tex_h) * _f32(WING_BOX_REF / WING_TEX_REF))
                 * _f32(s)) + 0.5)
    return h if h >= 1 else 1


def icon_width(tex_w, tex_h, drawn_h):
    """Mirror of kf_icon_width(): keep the sprite's aspect ratio.
    Integer math, matching the C exactly (truncating division, no rounding)."""
    if tex_h <= 0 or tex_w <= 0:
        return 0
    return (tex_w * drawn_h) // tex_h


def row_height(text_h, tallest_icon_h):
    """Mirror of kf_row_height(): the taller of the text cell and the icons.
    Both inputs come from the SAME scale, so this no longer mixes two bases."""
    return tallest_icon_h if tallest_icon_h > text_h else text_h


def border_overhang(thickness):
    """Mirror of kf_border_overhang(). How far the local-player outline sticks
    out past the plate edge.

    MEASURED: the centroids of the two red bands are 32.98px apart while a plain
    plate is exactly 33px, so the border is CENTRED on the plate edge. The
    outward half lands where a plain plate's edge would be, which is why the
    reference keeps a constant 3px visible gap on every step -- including the
    step into the outlined row."""
    return (thickness * 2) // 3


def border_thickness(scaled, vgap):
    """Mirror of kf_border_thickness(). The overhang eats into the inter-row gap,
    so cap the thickness: the outward part must fit inside vgap."""
    max_t = max(1, (vgap * 3) // 2)
    return max(1, min(scaled, max_t))


def row_alpha(death_age_ms, exit_ms):
    """Mirror of kf_row_alpha(): full opacity while alive, linear fade on exit.
    There is no enter animation and no horizontal drift in GoldClient."""
    if death_age_ms <= 0.0:
        return 1.0
    if exit_ms <= 0.0:
        return 0.0
    p = _f32(_f32(death_age_ms) / _f32(exit_ms))
    if p > 1.0:
        p = 1.0
    return _f32(1.0 - p)


def elem_y(raised, row_center_y, h, raise_lift=0):
    """Mirror of kf_elem_y(): the airborne wing is lifted above the row centre
    (and overhangs the plate); everything else is vertically centred."""
    if not raised:
        return row_center_y - h // 2
    return row_center_y - h // 2 - raise_lift


def tallest(elems):
    """Mirror of kf_tallest(): tallest element with non-zero width, EXCLUDING
    raised ones -- the wing overhangs the plate, so it must not size the row."""
    t = 0
    for e in elems:
        if e["w"] > 0 and not e.get("raised") and e["h"] > t:
            t = e["h"]
    return t


def layout_row(elems, gap, padx=0, gap_tight=None):
    """Mirror of kf_layout_row(). elems is a list of dicts with 'w'; each gets
    an 'x'. Zero-width elements (a sprite that failed to load) consume neither
    space nor a gap. An element flagged 'tight' uses gap_tight before it (the
    wing hugging the weapon). Returns the row content width, no padding."""
    if gap_tight is None:
        gap_tight = gap
    x = padx
    first = True
    for e in elems:
        if e["w"] <= 0:
            e["x"] = x
            continue
        if not first:
            x += gap_tight if e.get("tight") else gap
        e["x"] = x
        x += e["w"]
        first = False
    return x - padx
