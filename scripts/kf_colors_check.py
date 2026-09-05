#!/usr/bin/env python3
"""Every killfeed colour must be ONE number, not two that look alike.

The C draw path and the Python mirror each carry the palette. They are supposed
to agree, but nothing enforced it: the outline colour in particular lives as a
bare literal in death.cpp (KF_Border(..., 238, 23, 23, ...)) while the mirror has
OUTLINE_RGB, and the two could drift with no test noticing -- the mirror check
only compares GEOMETRY, never colour.

Checked here, in both directions:
  * plate / CT / T / icon colours: the cvar DEFAULT string in death.cpp must equal
    the mirror constant (that is what ships, and what the reference was measured
    to), and the C fallback initialiser must equal it too;
  * outline colour: the literal passed to KF_Border must equal OUTLINE_RGB.

    python3 scripts/kf_colors_check.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEATH = os.path.join(ROOT, "cl_dll", "death.cpp")
RATIOS = os.path.join(ROOT, "cl_dll", "killfeed_ref", "kf_ratios.py")


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def py_tuple(text, name):
    # \b on the left matters: T_RGB is a SUBSTRING of CT_RGB, so an unanchored
    # search for "T_RGB" happily matched the CT line and reported a mismatch on
    # perfectly correct code. Anchor both ends.
    m = re.search(r"^" + name + r"\s*=\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)",
                  text, re.M)
    return tuple(int(g) for g in m.groups()) if m else None


def py_int(text, name):
    m = re.search(r"^" + name + r"\s*=\s*(\d+)", text, re.M)
    return int(m.group(1)) if m else None


def cvar_rgb(text, cvar):
    m = re.search(r'CVAR_CREATE\(\s*"' + cvar + r'"\s*,\s*"(\d+)\s+(\d+)\s+(\d+)"',
                  text)
    return tuple(int(g) for g in m.groups()) if m else None


def cvar_int(text, cvar):
    m = re.search(r'CVAR_CREATE\(\s*"' + cvar + r'"\s*,\s*"(\d+)"', text)
    return int(m.group(1)) if m else None


def c_vec3_255(text, name):
    """static vec3_t NAME = { 129.0f/255.0f, ... } -> (129,154,202).

    Anchored with \\b for the same reason as py_tuple: s_kfColorT is a substring
    of nothing here, but the vec3 names are close enough that a loose match would
    be a trap waiting for the next colour to be added."""
    m = re.search(r"\b" + name + r"\s*=\s*\{\s*([\d.]+)f?/255\.0f\s*,"
                  r"\s*([\d.]+)f?/255\.0f\s*,\s*([\d.]+)f?/255\.0f\s*\}", text)
    return tuple(int(float(g)) for g in m.groups()) if m else None


def kf_border_rgb(text):
    m = re.search(r"KF_Border\([^;]*?,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,",
                  text, re.S)
    return tuple(int(g) for g in m.groups()) if m else None


def main():
    death, ratios = read(DEATH), read(RATIOS)
    bad = 0

    def cmp(label, a, a_src, b, b_src):
        nonlocal bad
        if a is None:
            print("NOT FOUND: %s in %s" % (label, a_src))
            bad += 1
            return
        if b is None:
            print("NOT FOUND: %s in %s" % (label, b_src))
            bad += 1
            return
        if a != b:
            print("MISMATCH %-16s %s=%s  %s=%s" % (label, a_src, a, b_src, b))
            bad += 1
        else:
            print("  %-16s %s" % (label, a))

    print("cvar default (death.cpp) vs mirror constant (kf_ratios.py):")
    cmp("plate",   cvar_rgb(death, "cl_killfeed_plate_color"), "cvar",
        py_tuple(ratios, "PLATE_RGB"), "mirror")
    cmp("plate alpha", cvar_int(death, "cl_killfeed_plate_alpha"), "cvar",
        py_int(ratios, "PLATE_ALPHA"), "mirror")
    cmp("CT",      cvar_rgb(death, "cl_killfeed_ct_color"), "cvar",
        py_tuple(ratios, "CT_RGB"), "mirror")
    cmp("T",       cvar_rgb(death, "cl_killfeed_t_color"), "cvar",
        py_tuple(ratios, "T_RGB"), "mirror")
    cmp("icon",    cvar_rgb(death, "cl_killfeed_icon_color"), "cvar",
        py_tuple(ratios, "GREY_RGB"), "mirror")

    print("C fallback initialiser vs mirror constant:")
    cmp("CT init",   c_vec3_255(death, "s_kfColorCT"), "s_kfColorCT",
        py_tuple(ratios, "CT_RGB"), "mirror")
    cmp("T init",    c_vec3_255(death, "s_kfColorT"), "s_kfColorT",
        py_tuple(ratios, "T_RGB"), "mirror")
    cmp("grey init", c_vec3_255(death, "s_kfColorGrey"), "s_kfColorGrey",
        py_tuple(ratios, "GREY_RGB"), "mirror")

    print("outline literal vs mirror constant:")
    cmp("outline", kf_border_rgb(death), "KF_Border literal",
        py_tuple(ratios, "OUTLINE_RGB"), "mirror")

    if bad:
        print("\n%d COLOUR MISMATCH(ES) -- the previews no longer predict the "
              "game's palette" % bad)
        return 1
    print("\nall killfeed colours agree between death.cpp and the mirror")
    return 0


if __name__ == "__main__":
    sys.exit(main())
