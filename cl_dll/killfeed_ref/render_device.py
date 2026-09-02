#!/usr/bin/env python3
"""Killfeed rendered at DEVICE resolution, composited over a real screenshot.

Same geometry as render.py (both call kf_draw.render_feed); the difference is the
backdrop. The size comes from the screenshot's own height, exactly as the game
derives it from ScreenHeight -- there is no separate font-size knob, because two
independent bases is the bug this design removed.

    python3 render_device.py --bg /path/to/screenshot.jpg
"""
import argparse
import sys

from PIL import Image

import kf_draw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bg", required=True,
                    help="device screenshot to composite the feed over")
    ap.add_argument("--scale", type=float, default=1.0, help="cl_killfeed_scale")
    ap.add_argument("--cap-ratio", type=float, default=kf_draw.CAP_RATIO,
                    help="cap-height/text-cell of the font (measured 0.444)")
    ap.add_argument("--font-raster-h", type=int, default=None,
                    help="engine font raster height (only affects textScale)")
    ap.add_argument("--margin-top", type=int, default=None)
    ap.add_argument("--margin-right", type=int, default=None)
    ap.add_argument("--out", default="/var/minis/attachments/kf_device.png")
    a = ap.parse_args()

    bg = Image.open(a.bg).convert("RGBA")
    info = kf_draw.render_feed(bg, kf_draw.DEMO_ROWS, a.scale,
                               font_raster_h=a.font_raster_h,
                               cap_ratio=a.cap_ratio,
                               margin_top=a.margin_top,
                               margin_right=a.margin_right)

    bg.convert("RGB").save(a.out)
    print("screen=%dx%d  cl_killfeed_scale=%.2f -> THE scale=%.4f"
          % (bg.size[0], bg.size[1], a.scale, info["scale"]))
    print("textH=%d padx=%d pady=%d gap=%d gapTight=%d vgap=%d"
          % (info["textH"], info["padx"], info["pady"], info["gap"],
             info["gapTight"], info["vgap"]))
    print("row heights: %s  total=%dpx" % ([r["h"] for r in info["rows"]],
                                           info["total_h"]))
    print("saved", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
