#!/usr/bin/env python3
"""Shared killfeed renderer for the preview scripts in this directory.

NOT engine output -- there is no NDK in the sandbox. This reproduces the
GEOMETRY that death.cpp computes, using kf_ratios.py (verified against the real
GoldClient client.dll by tests/kf_mirror_check.py) and the real GoldClient
d_*.spr icons.

render.py (synthetic backdrop) and render_device.py (composited over a device
screenshot) both call render_feed() here, so the two previews cannot diverge
from each other or from the C++.

The one remaining assumption is CAP_RATIO: the cap-height/cell-box ratio of the
engine's runtime font atlas (gfx/conchars), which is not in any source tree.
0.56 matches the reference glyphs (measured); it affects only which DejaVu size
stands in for the engine font -- never the layout.
"""
import os
import struct

from PIL import Image, ImageDraw, ImageFont

import kf_ratios as R

# GoldClient killfeed icons, converted to SPR32 by scripts/kf_tga2spr.py.
SPR_DIR = os.environ.get(
    "KF_SPR_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "..", "3rdparty", "cs16client-extras", "sprites", "kf"))
FONT_PATH = "/usr/share/fonts/ttf-dejavu/DejaVuSans-Bold.ttf"
CAP_RATIO = 0.444       # cap-height / textH, MEASURED on the native
                        # reference: cap 12px at text cell 27 (six rows agreed)
# Name colours live in kf_ratios (R.CT_RGB / R.T_RGB / R.GREY_RGB) so the C and
# the preview cannot disagree -- they used to be duplicated here and drifted.

# Killfeed team colours measured from the reference glyph cores.

# Modifier sprite files, per kf_mod_names[] in death.cpp.
FLASH_ASSIST_SPR = "assist_flash"
PLUS_SPR = "plus"

# Demo rows reproducing the reference screenshot, one per layout branch.
DEMO_ROWS = [
    dict(pre=["blind_kill"], killer="Gunner", kt="ct", wing="inair_kill",
         wpn="awp", mid=["noscope", "smoke_kill"], victim="KILLER", vt="t"),
    # Row 2 uses d_inferno, which is what the reference shows there (silhouette
    # MAE 0.185). The shipped weapon table does not map any CS 1.6 weapon name
    # to it -- this row exists to reproduce the reference frame, not to claim the
    # game can produce it.
    dict(killer="WASD", kt="t", wpn="inferno", victim="Gordon", vt="ct"),
    dict(killer="!defa", kt="t", wpn="ak47", mid=["penetrate"],
         victim="Spaceman", vt="ct"),
    dict(killer="Psycho", kt="ct", wpn="grenade", victim="Boss", vt="t"),
    dict(killer="Crazyf", kt="ct", wpn="m4a1", mid=["headshot"],
         victim="theKing", vt="t"),
    dict(killer="GoldPlayer", kt="ct", assist="Albert", at="ct", wpn="deagle",
         mid=["headshot"], victim="GabeN", vt="t", local=True),
]

_spr_cache = {}


def spr_size(name):
    """Natural (width, height) from the SPR32 header (w@16, h@20)."""
    if name not in _spr_cache:
        with open(os.path.join(SPR_DIR, name + ".spr"), "rb") as f:
            b = f.read(36)
        w, h = struct.unpack_from("<ii", b, 16)
        _spr_cache[name] = (w, h)
    return _spr_cache[name]


def spr_img(name):
    """Decode an SPR32 frame to RGBA. GoldClient icons are white premultiplied
    over black with the coverage in RGB, so luminance is the alpha (matching the
    additive blend death.cpp uses via pfnSPR_DrawGeneric)."""
    with open(os.path.join(SPR_DIR, name + ".spr"), "rb") as f:
        b = f.read()
    w, h = struct.unpack_from("<ii", b, 16)
    px = b[36 + 4 + 16:]
    im = Image.new("RGBA", (w, h))
    p = im.load()
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            a = max(px[o], px[o + 1], px[o + 2])
            p[x, y] = (255, 255, 255, a)
    return im


def cap_px(font, probe="GKAMW"):
    """Measured cap-height of a font in pixels."""
    img = Image.new("L", (400, 200), 0)
    ImageDraw.Draw(img).text((10, 40), probe, font=font, fill=255)
    bb = img.getbbox()
    return (bb[3] - bb[1]) if bb else 0


def font_for_cap(target_cap):
    """DejaVu size whose measured cap-height == target_cap."""
    best, best_err = None, 1e9
    for size in range(6, 200):
        f = ImageFont.truetype(FONT_PATH, size)
        err = abs(cap_px(f) - target_cap)
        if err < best_err:
            best, best_err = f, err
        if err == 0:
            break
    return best


def _team_color(tag):
    return {"ct": R.CT_RGB, "t": R.T_RGB}.get(tag, R.GREY_RGB)


def build_row(cfg, m, text_width):
    """Mirror of KF_BuildRow(): measure the row's elements in draw order.
    m is the metric bundle from R.compute_metrics -- ONE scale for everything."""
    el = []

    def push_icon(name, tight=False, raised=False):
        tw, th = spr_size(name)
        h = (R.wing_height(th, m["scale"]) if raised
             else R.icon_height(th, m["scale"], m["textH"]))
        el.append(dict(kind="icon", spr=name, w=R.icon_width(tw, th, h), h=h,
                       tight=tight, raised=raised))

    def push_text(s, col):
        el.append(dict(kind="text", text=s, color=col,
                       w=text_width(s) + 1, h=m["textH"], tight=False,
                       raised=False))

    for mod in cfg.get("pre", []):
        push_icon(mod)
    if cfg.get("killer"):
        push_text(cfg["killer"], _team_color(cfg.get("kt")))
    if cfg.get("assist"):
        # An assist is shown whenever there IS an assister; the flash icon is an
        # EXTRA that only appears for a flash assist. Mirror of KF_BuildRow().
        # MEASURED on the native reference: '+' glue, then (for a flash assist)
        # the flash icon, then the assister name.
        push_icon(PLUS_SPR)
        if cfg.get("flash_assist", True):
            push_icon(FLASH_ASSIST_SPR)
        push_text(cfg["assist"], _team_color(cfg.get("at")))
    if cfg.get("wing"):
        push_icon(cfg["wing"], raised=True)
    if cfg.get("wpn"):
        push_icon(cfg["wpn"], tight=bool(cfg.get("wing")))
    for mod in cfg.get("mid", []):
        push_icon(mod)
    if cfg.get("victim"):
        push_text(cfg["victim"], _team_color(cfg.get("vt")))
    return el


def render_feed(img, rows, user_scale=1.0, font_raster_h=None,
                cap_ratio=CAP_RATIO, margin_top=None, margin_right=None):
    """Draw the feed top-right onto an RGBA image exactly as death.cpp does.

    ONE scale drives everything: it comes from the image height, mirroring
    kf_scale(ScreenHeight, cl_killfeed_scale). Returns the metrics used.
    """
    screen_h = img.size[1]
    screen_w = img.size[0]

    m = R.compute_metrics(screen_h, user_scale,
                          font_raster_h if font_raster_h else m_default_font(screen_h))

    # The preview draws with a TrueType face, so it can hit any cell height: pick
    # the size whose cap height matches the measured cap/textH ratio.
    target_cap = max(4, int(round(cap_ratio * m["textH"])))
    fn = font_for_cap(target_cap)
    real_cap = cap_px(fn)

    margin_right = m["marginX"] if margin_right is None else margin_right
    margin_top = m["marginY"] if margin_top is None else margin_top

    measure = ImageDraw.Draw(img, "RGBA")

    def text_width(s):
        return int(measure.textlength(s, font=fn))

    y = margin_top
    row_info = []

    for cfg in rows:
        el = build_row(cfg, m, text_width)

        # single source of truth for x-assignment: the same mirror function the
        # C code is diffed against (tests/kf_mirror_check.py)
        w = R.layout_row(el, m["gap"], 0, m["gapTight"]) + m["padx"] * 2

        content_h = R.row_height(m["textH"], R.tallest(el))
        row_h = content_h + m["pady"] * 2
        px0 = screen_w - margin_right - w

        # An outlined row's border straddles the plate edge, so the PLATE is
        # inset by the overhang and the row reserves it on both sides. Without
        # this the visible gap above an outlined row would be vgap - overhang
        # (1px instead of 3px at the reference) while every other gap is vgap.
        # Mirror of KF_DrawRow / kf_border_overhang in the C code.
        border = R.border_thickness(m["outline"], m["vgap"]) \
            if cfg.get("local") else 0
        overhang = R.border_overhang(border)
        plate_y = y + overhang
        advance = row_h + overhang * 2

        cy = plate_y + row_h // 2

        pr, pg, pb = R.PLATE_RGB
        pa = R.PLATE_ALPHA

        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        dl = ImageDraw.Draw(layer, "RGBA")
        dl.rounded_rectangle([px0, plate_y, px0 + w - 1, plate_y + row_h - 1],
                             radius=m["corner"], fill=(pr, pg, pb, pa))
        if border:
            for k in range(border):
                dl.rounded_rectangle(
                    [px0 - overhang + k, plate_y - overhang + k,
                     px0 + w - 1 + overhang - k,
                     plate_y + row_h - 1 + overhang - k],
                    radius=max(1, m["corner"] + overhang - k),
                    outline=R.OUTLINE_RGB + (255,))
        img.alpha_composite(layer)

        for e in el:
            if e["w"] <= 0:
                continue
            ex = px0 + m["padx"] + e["x"]
            if e["kind"] == "text":
                top = cy - real_cap // 2
                bb = fn.getbbox("G")
                measure.text((ex, top - bb[1]), e["text"], font=fn,
                             fill=e["color"] + (255,))
                measure.text((ex + 1, top - bb[1]), e["text"], font=fn,
                             fill=e["color"] + (255,))  # faux-bold
            else:
                im = spr_img(e["spr"]).resize((e["w"], e["h"]), Image.LANCZOS)
                # the airborne wing is lifted above the row centre and overhangs
                # the plate; all other icons are vertically centred (MEASURED).
                iy = R.elem_y(e.get("raised"), cy, e["h"], m["raise_"])
                img.alpha_composite(im, (ex, iy))

        row_info.append(dict(w=w, h=row_h, y=plate_y, advance=advance))
        y += advance + m["vgap"]

    out = dict(m)
    out.update(rows=row_info, cap_target=target_cap, cap_drawn=real_cap,
               total_h=y - margin_top)
    return out


def m_default_font(screen_h):
    """Stand-in for the engine's font raster height when the caller does not
    supply one. Only affects textScale, which the preview does not use (it picks
    a TrueType size instead), so the reference cell height is a safe default."""
    return int(R.REF_TEXT_H)


def total_height(rows, user_scale=1.0, screen_h=1080, margin_top=0):
    """Height the feed will occupy, for sizing a synthetic canvas up front."""
    m = R.compute_metrics(screen_h, user_scale, int(R.REF_TEXT_H))
    h = margin_top
    for cfg in rows:
        el = build_row(cfg, m, lambda s: 0)
        h += R.row_height(m["textH"], R.tallest(el)) + m["pady"] * 2 + m["vgap"]
    return h
