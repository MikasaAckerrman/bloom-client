#!/usr/bin/env python3
"""
kf_spr_check.py - verify the baked killfeed sprites against their source TGAs.

Checks the properties death.cpp actually depends on, not the conversion formula
(re-implementing the formula here would only prove the code equals itself):

  1. header is a well-formed SPR32 and payload == fw*fh*4
  2. frame size == source size (no silent rescale)
  3. alpha preserved byte-for-byte (the shape lives in alpha)
  4. RGB <= A everywhere -- required for the additive GL_ONE/GL_ONE draw, an
     over-bright texel would glow through the plate
  5. greyscale (R==G==B) so SPR_Set() tinting is the only colour source
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

HDR_SIZE = 56


def check(spr_dir, tga_dir):
    fails = []
    n = 0
    for f in sorted(os.listdir(spr_dir)):
        if not f.endswith(".spr"):
            continue
        n += 1
        stem = f[:-4]
        d = open(os.path.join(spr_dir, f), "rb").read()

        if d[:4] != b"IDSP":
            fails.append("%s: not IDSP" % stem)
            continue
        version, = struct.unpack_from("<i", d, 4)
        w, h, nf = struct.unpack_from("<iii", d, 16)
        grp, ox, oy, fw, fh = struct.unpack_from("<5i", d, 36)
        body = d[HDR_SIZE:]

        if version != 32:
            fails.append("%s: version %d != 32" % (stem, version))
        if len(body) != fw * fh * 4:
            fails.append("%s: payload %d != %d" % (stem, len(body), fw * fh * 4))
            continue

        got = np.frombuffer(body, dtype=np.uint8).reshape(fh, fw, 4).astype(int)
        src = np.asarray(
            Image.open(os.path.join(tga_dir, "d_%s.tga" % stem)).convert("RGBA")
        ).astype(int)

        if (fw, fh) != (src.shape[1], src.shape[0]):
            fails.append("%s: size %dx%d != source %dx%d"
                         % (stem, fw, fh, src.shape[1], src.shape[0]))
            continue
        if not np.array_equal(got[:, :, 3], src[:, :, 3]):
            fails.append("%s: alpha altered" % stem)
        over = int((got[:, :, 0] > got[:, :, 3]).sum())
        if over:
            fails.append("%s: %d texels with RGB > A (additive overbright)"
                         % (stem, over))
        if not (np.array_equal(got[:, :, 0], got[:, :, 1])
                and np.array_equal(got[:, :, 1], got[:, :, 2])):
            fails.append("%s: not greyscale" % stem)
    return n, fails


def main():
    if len(sys.argv) != 3:
        print("usage: kf_spr_check.py <spr_dir> <tga_dir>", file=sys.stderr)
        return 2
    n, fails = check(sys.argv[1], sys.argv[2])
    for f in fails:
        print("FAIL " + f)
    print("%d sprites checked, %d problems" % (n, len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
