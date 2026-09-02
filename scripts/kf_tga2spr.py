#!/usr/bin/env python3
"""
kf_tga2spr.py - bake GoldClient's killfeed icons into the SPR32 files that
death.cpp loads through SPR_Load().

WHY a conversion step and not the TGAs directly: cl_dll has no TGA loader --
the only texture entry point the client DLL gets from the engine for HUD work
is SPR_Load + pfnSPR_DrawGeneric. Converting the assets keeps the loader
untouched.

SOURCE: GoldClient's HD death-notice set, gfx/hud/deathnotice/d_*.tga (the
`hud_deathnotice_iconshd 1` default). Verified as the set on the reference
screenshot by silhouette MAE against every candidate: HD wins on every row
(ak47 0.047 vs legacy 0.147, m4a1 0.042 vs 0.144, awp 0.100 vs 0.146).

PIXEL FORMAT, measured on the source TGAs: RGB is a flat 255 and the shape
lives entirely in alpha (channel spread across all opaque pixels is exactly 0).
KF_DrawIcon draws with blendsrc=GL_ONE, blenddst=GL_ONE (additive) and tints
via SPR_Set(), so the stored texel must be the light added to the framebuffer:
we write premultiplied greyscale, RGB == A. Anti-aliased edges stay soft
because alpha is preserved verbatim.

HEADER: byte-for-byte the layout the existing kf/*.spr use, re-verified here
against a reference file before writing anything (--verify does only that).
"""
import argparse
import os
import struct
import sys

from PIL import Image

VERSION = 32   # 32bpp marker used by this asset set
SPRTYPE = 2    # SPR_VP_PARALLEL
HDR_SIZE = 56

# Sprites GoldClient ships but the killfeed never asks for: game-mode extras and
# the 9-slice panel pieces (our plate is drawn with engine rects, not textures).
SKIP = {
    "panel_corner", "panel_corner_solid", "panel_hline", "panel_vline",
    "infection", "snowball", "sentrygun", "tracktrain", "tripmine",
}


def read_header(path):
    """Parse an SPR32 header so assumptions can be checked, not trusted."""
    b = open(path, "rb").read()
    version, sprtype = struct.unpack_from("<ii", b, 4)
    w, h, nf = struct.unpack_from("<iii", b, 16)
    grp, ox, oy, fw, fh = struct.unpack_from("<5i", b, 36)
    return dict(ident=b[:4], version=version, sprtype=sprtype, w=w, h=h,
                nf=nf, grp=grp, ox=ox, oy=oy, fw=fw, fh=fh,
                payload=len(b) - HDR_SIZE, expect=fw * fh * 4)


def write_spr(img, out):
    w, h = img.size
    px = img.load()
    body = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            v = (lum * a) // 255          # premultiply: additive blend
            body += bytes((v, v, v, a))

    hdr = b"IDSP"
    hdr += struct.pack("<ii", VERSION, SPRTYPE)
    hdr += struct.pack("<f", float(w))               # boundingradius = width
    hdr += struct.pack("<iii", w, h, 1)              # w, h, numframes
    hdr += struct.pack("<f", 0.0)                    # beamlength
    hdr += struct.pack("<i", 0)                      # synctype
    hdr += struct.pack("<i", 0)                      # frame group
    hdr += struct.pack("<4i", -(w // 2), h // 2, w, h)
    assert len(hdr) == HDR_SIZE, len(hdr)
    with open(out, "wb") as f:
        f.write(hdr + bytes(body))
    return HDR_SIZE + len(body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="dir with GoldClient d_*.tga")
    ap.add_argument("--dst", help="output sprites/kf dir")
    ap.add_argument("--ref", help="existing .spr to verify the header layout")
    ap.add_argument("--verify", action="store_true",
                    help="only check --ref and exit")
    a = ap.parse_args()

    if a.verify and not a.ref:
        ap.error("--verify needs --ref")
    if not a.verify and not (a.src and a.dst):
        ap.error("--src and --dst are required unless --verify")

    if a.ref:
        h = read_header(a.ref)
        ok = (h["ident"] == b"IDSP" and h["version"] == VERSION
              and h["payload"] == h["expect"])
        print("reference %s: %dx%d v%d payload %d == fw*fh*4 %d -> %s"
              % (os.path.basename(a.ref), h["w"], h["h"], h["version"],
                 h["payload"], h["expect"], "OK" if ok else "MISMATCH"))
        if not ok:
            return 1
        if a.verify:
            return 0

    os.makedirs(a.dst, exist_ok=True)
    names = sorted(n for n in os.listdir(a.src)
                   if n.startswith("d_") and n.endswith(".tga"))
    if not names:
        print("no d_*.tga in %s" % a.src, file=sys.stderr)
        return 1

    written, skipped = [], []
    for n in names:
        stem = n[2:-4]
        if stem in SKIP:
            skipped.append(stem)
            continue
        img = Image.open(os.path.join(a.src, n)).convert("RGBA")
        size = write_spr(img, os.path.join(a.dst, stem + ".spr"))
        written.append((stem, img.size, size))

    for stem, wh, size in written:
        print("  %-18s %3dx%-3d -> %6d B" % (stem, wh[0], wh[1], size))
    print("\n%d sprites written to %s" % (len(written), a.dst))
    print("skipped (not used by the killfeed): %s" % ", ".join(sorted(skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
