#!/usr/bin/env python3
"""
kf_render.py — geometry render of the killfeed AS DEATH.CPP LAYS IT OUT.

IMPORTANT, read before trusting the picture
-------------------------------------------
This is NOT engine output. There is no NDK in this sandbox, so nothing here
comes from the real renderer. What this script does is reproduce the *geometry*
that death.cpp computes, using the same ratio constants the C++ uses
(cl_dll/killfeed_ref/kf_ratios.py), and draw it with real .spr icons.

The old preview (phone_preview.py) had a REAL BUG that caused the whole
"the text is too small" confusion: it picked the name font size arbitrarily
(font(int(H*0.58))), which has nothing to do with what the engine would draw.
Here the name size is derived the way the engine derives it:

    TL   = console-font line height   (gHUD.GetCharHeight())
    cap  = ENGINE_CAP_RATIO * TL      (k = cap-height / cell-box of the atlas)
    H    = 1.80 * TL                  (plate; death.cpp:480)

so cap/H = k/1.80 and the name can no longer be "whatever looked right".

ENGINE_CAP_RATIO (k) is the ONE number that is still an assumption: it lives in
the runtime font atlas (gfx/conchars or creditsfont.fnt), which is not in any
source tree. 0.72 is the typical GoldSrc value and is what makes cap/H land on
the reference's 0.40. Override it to see the effect:

    python3 kf_render.py --cap-ratio 0.55     # a "thin cap" atlas

Also unlike the old preview this draws NO debug caption on the image.
"""
import argparse, os, struct, sys

from PIL import Image, ImageDraw, ImageFont

RATIOS = "/tmp/minis-killfeed-bloom-20260822/cl_dll/killfeed_ref"
sys.path.insert(0, RATIOS)
import kf_ratios as R                      # same constants as the C++

SPR = "/tmp/minis-killfeed-bloom-20260822/3rdparty/cs16client-extras/sprites/kf"
FONT = "/usr/share/fonts/ttf-dejavu/DejaVuSans-Bold.ttf"

CT = (153, 204, 255)   # g_ColorBlue {0.6,0.8,1.0} * 255
T  = (255,  63,  63)   # g_ColorRed  {1.0,0.25,0.25} * 255

ROWS = [
    dict(pre=["blind"], killer="Gunner", kt="ct", wing="inair", wpn="awp",
         mid=["noscope", "smoke"], victim="KILLER", vt="t"),
    dict(killer="WASD", kt="t", wpn="hegrenade", victim="Gordon", vt="ct"),
    dict(killer="!defa", kt="t", wpn="ak47", mid=["penetrate"],
         victim="Spaceman", vt="ct"),
    dict(killer="Psycho", kt="ct", wpn="flashbang", victim="Boss", vt="t"),
    dict(killer="Crazyf", kt="ct", wpn="m4a1", mid=["headshot"],
         victim="theKing", vt="t"),
    dict(killer="GoldPlayer", kt="ct", assist="Albert", at="ct", wpn="deagle",
         mid=["headshot"], victim="GabeN", vt="t", local=True),
]


def spr_size(name):
    with open(os.path.join(SPR, name + ".spr"), "rb") as f:
        b = f.read(36)
    _, _, _, _, w, h, _, _, _ = struct.unpack("<3i f 2i i f I", b)
    return w, h


def spr_img(name):
    with open(os.path.join(SPR, name + ".spr"), "rb") as f:
        b = f.read()
    _, _, _, _, w, h, _, _, _ = struct.unpack("<3i f 2i i f I", b[:36])
    px = b[36 + 4 + 16:]
    im = Image.new("RGBA", (w, h))
    p = im.load()
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            p[x, y] = (255, 255, 255, max(px[o], px[o + 1], px[o + 2]))
    return im


def cap_px(font, probe="GKAMW"):
    """Measured cap-height of a font in pixels (capitals only, no descenders)."""
    img = Image.new("L", (400, 200), 0)
    ImageDraw.Draw(img).text((10, 40), probe, font=font, fill=255)
    bb = img.getbbox()
    return (bb[3] - bb[1]) if bb else 0


def font_for_cap(target_cap):
    """Pick the DejaVu size whose measured cap-height == target_cap.

    The engine cannot resize its font; we are matching OUR preview font to the
    cap-height the engine WOULD produce, so the picture reflects engine metrics
    instead of an arbitrary choice.
    """
    best, best_err = None, 1e9
    for size in range(6, 200):
        f = ImageFont.truetype(FONT, size)
        err = abs(cap_px(f) - target_cap)
        if err < best_err:
            best, best_err = f, err
        if err == 0:
            break
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tl", type=int, default=31,
                    help="console font line height (gHUD.GetCharHeight())")
    ap.add_argument("--scale", type=float, default=1.0, help="cl_killfeed_scale")
    ap.add_argument("--cap-ratio", type=float, default=0.72,
                    help="k = cap-height/cell-box of the font atlas (ASSUMPTION)")
    ap.add_argument("--width", type=int, default=1300)
    ap.add_argument("--out", default="/var/minis/attachments/kf_render_code.png")
    a = ap.parse_args()

    TL = a.tl
    H = int(TL * R.PLATE_H_OVER_TL * a.scale)          # death.cpp:480
    PITCH = int(H * R.PITCH_OVER_H)                    # death.cpp:482
    ICON_H = max(8, int(H * R.ICON_OVER_H))            # death.cpp:342
    MOD_H = max(8, int(H * R.MOD_OVER_H))              # death.cpp:343
    GAP = max(2, int(TL * R.GAP_OVER_TL))              # :344
    GAPW = max(1, int(TL * R.GAPW_OVER_TL))            # :345
    PADX = max(3, int(TL * R.PADX_OVER_TL))            # :346
    RADIUS = max(2, int(H * R.RADIUS_OVER_H))          # :347
    BORDER = max(2, int(H * R.BORDER_OVER_H))          # :348
    WING_DY = int(H * R.WING_DY_OVER_H)

    target_cap = max(4, int(round(a.cap_ratio * TL)))
    FN = font_for_cap(target_cap)
    real_cap = cap_px(FN)

    SCR_W = a.width
    SCR_H = PITCH * len(ROWS) + int(TL * 2.5)
    MARGIN_RIGHT = int(SCR_W * 6 / 640)
    MARGIN_TOP = max(4, int(TL * 0.3))

    img = Image.new("RGBA", (SCR_W, SCR_H), (29, 26, 21, 255))
    d0 = ImageDraw.Draw(img)
    for i in range(0, SCR_W, 90):
        d0.line([(i, 0), (i + 40, SCR_H)], fill=(38, 34, 28, 120), width=30)

    def iconw(name, h):
        w, sh = spr_size(name)
        return max(1, round(w * h / sh))               # KF_IconW, aspect kept

    def draw_row(cfg, topY):
        d = ImageDraw.Draw(img, "RGBA")
        def txtw(s):
            return int(d.textlength(s, font=FN))

        # ---- KF_RowWidth ----
        w = PADX
        first = [True]

        def adv(px):
            nonlocal w
            if not first[0]:
                w += GAP
            w += px
            first[0] = False

        for m in cfg.get("pre", []):
            adv(iconw(m, MOD_H))
        if cfg.get("killer"):
            adv(txtw(cfg["killer"]) + 1)               # +1 faux-bold
        if cfg.get("assist"):
            adv(iconw("flashbang_assist", MOD_H))
            adv(txtw(cfg["assist"]) + 1)
        if cfg.get("wing"):
            if not first[0]:
                w += GAP
            w += iconw(cfg["wing"], MOD_H)
            first[0] = False
            w += GAPW
            w += iconw(cfg["wpn"], ICON_H)
        else:
            adv(iconw(cfg["wpn"], ICON_H))
        for m in cfg.get("mid", []):
            adv(iconw(m, MOD_H))
        if cfg.get("victim"):
            adv(txtw(cfg["victim"]) + 1)
        w += PADX

        x = SCR_W - MARGIN_RIGHT - w                   # right anchor
        y = topY
        cy = y + H // 2

        pr, pg, pb = R.PLATE_RGB
        alpha = 120 if cfg.get("local") else R.PLATE_ALPHA
        # ImageDraw on an RGBA image REPLACES pixels (it does not blend), which
        # would show the plate as fully opaque and hide the transparency we are
        # verifying. Composite a separate layer so alpha behaves like FillRGBABlend.
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        dl = ImageDraw.Draw(layer, "RGBA")
        dl.rounded_rectangle([x, y, x + w - 1, y + H - 1], radius=RADIUS,
                             fill=(pr, pg, pb, alpha))
        if cfg.get("local"):
            for t in range(BORDER):
                dl.rounded_rectangle([x + t, y + t, x + w - 1 - t, y + H - 1 - t],
                                     radius=max(1, RADIUS - t),
                                     outline=(255, 36, 36, 255))
        img.alpha_composite(layer)

        cx = x + PADX
        first = True

        def blit(name, h, dy=0):
            nonlocal cx
            iw = iconw(name, h)
            im = spr_img(name).resize((iw, h), Image.LANCZOS)
            img.alpha_composite(im, (cx, cy - h // 2 + dy))
            cx += iw

        def text(s, col):
            # engine draws the console string from the TOP of its cell at
            # ty = cy - TL/2 (death.cpp:355); the capitals sit centred in that
            # cell, so the cap band is centred on cy.
            nonlocal cx
            top = cy - real_cap // 2
            bb = FN.getbbox("G")
            d.text((cx, top - bb[1]), s, font=FN, fill=col + (255,))
            d.text((cx + 1, top - bb[1]), s, font=FN, fill=col + (255,))  # faux-bold
            cx += int(d.textlength(s, font=FN)) + 1

        for m in cfg.get("pre", []):
            if not first:
                cx += GAP
            blit(m, MOD_H)
            first = False
        if cfg.get("killer"):
            if not first:
                cx += GAP
            text(cfg["killer"], CT if cfg["kt"] == "ct" else T)
            first = False
        if cfg.get("assist"):
            if not first:
                cx += GAP
            blit("flashbang_assist", MOD_H)
            cx += GAP
            text(cfg["assist"], CT if cfg["at"] == "ct" else T)
            first = False
        if cfg.get("wing"):
            if not first:
                cx += GAP
            blit(cfg["wing"], MOD_H, WING_DY)
            first = False
            cx += GAPW
            blit(cfg["wpn"], ICON_H)
        else:
            if not first:
                cx += GAP
            blit(cfg["wpn"], ICON_H)
            first = False
        for m in cfg.get("mid", []):
            cx += GAP
            blit(m, MOD_H)
        if cfg.get("victim"):
            cx += GAP
            text(cfg["victim"], CT if cfg["vt"] == "ct" else T)

    y = MARGIN_TOP
    for cfg in ROWS:
        draw_row(cfg, y)
        y += PITCH

    img.convert("RGB").save(a.out)
    print("TL=%d  plate H=%d  pitch=%d  icon=%d  mod=%d" % (TL, H, PITCH, ICON_H, MOD_H))
    print("cap target=%dpx (k=%.2f)  cap drawn=%dpx  cap/H=%.3f  (reference 0.40)"
          % (target_cap, a.cap_ratio, real_cap, real_cap / float(H)))
    print("saved", a.out)


if __name__ == "__main__":
    main()
