#!/usr/bin/env python3
"""Killfeed preview on a synthetic backdrop, using the geometry death.cpp computes.

NOT engine output. Layout comes from kf_ratios.py + kf_draw.py, verified against
the real C code by tests/kf_mirror_check.py. Icons are the real GoldClient set.

ONE SCALE: the feed size follows --height (the virtual ScreenHeight) times
--scale (cl_killfeed_scale). There is no separate font-size knob, because having
two independent bases is exactly the bug this rewrite removed.

    KF_SPR_DIR=/path/to/sprites/kf python3 render.py --height 1080
"""
import argparse
import sys

from PIL import Image, ImageDraw

import kf_draw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=float, default=1.0, help="cl_killfeed_scale")
    ap.add_argument("--cap-ratio", type=float, default=kf_draw.CAP_RATIO,
                    help="cap-height/text-cell of the font (measured 0.444)")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080,
                    help="canvas height == ScreenHeight (drives THE scale)")
    ap.add_argument("--font-raster-h", type=int, default=None,
                    help="engine font raster height (only affects textScale)")
    ap.add_argument("--margin-top", type=int, default=None,
                    help="top offset of the feed (default: from the metrics)")
    ap.add_argument("--margin-right", type=int, default=None,
                    help="right offset of the feed (default: from the metrics)")
    ap.add_argument("--out", default="/var/minis/attachments/kf_render_code.png")
    a = ap.parse_args()

    rows = kf_draw.DEMO_ROWS
    img = Image.new("RGBA", (a.width, a.height), (29, 26, 21, 255))
    d0 = ImageDraw.Draw(img)
    for i in range(0, a.width, 90):
        d0.line([(i, 0), (i + 40, a.height)], fill=(38, 34, 28, 120), width=30)

    info = kf_draw.render_feed(img, rows, a.scale,
                               font_raster_h=a.font_raster_h,
                               cap_ratio=a.cap_ratio,
                               margin_top=a.margin_top,
                               margin_right=a.margin_right)

    img.convert("RGB").save(a.out)
    print("screenH=%d scale=%.2f -> THE scale=%.4f"
          % (a.height, a.scale, info["scale"]))
    print("textH=%d padx=%d pady=%d gap=%d gapTight=%d vgap=%d corner=%d outline=%d"
          % (info["textH"], info["padx"], info["pady"], info["gap"],
             info["gapTight"], info["vgap"], info["corner"], info["outline"]))
    print("margins: x=%d y=%d" % (info["marginX"], info["marginY"]))
    print("row heights: %s" % [r["h"] for r in info["rows"]])
    print("cap target=%dpx (k=%.3f)  cap drawn=%dpx"
          % (info["cap_target"], a.cap_ratio, info["cap_drawn"]))
    print("saved", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
