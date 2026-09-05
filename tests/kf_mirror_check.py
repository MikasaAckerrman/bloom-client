#!/usr/bin/env python3
"""Diff the Python mirror against the REAL C implementation.

The preview/render scripts predict what the game will draw. That only holds if
kf_ratios.py computes exactly what killfeed_layout.h computes -- including
float32 rounding and integer truncation. This runs the C dump and checks every
row against the mirror.

    python3 tests/kf_mirror_check.py
"""
import csv
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "cl_dll", "killfeed_ref"))
import kf_ratios as R  # noqa: E402

DUMP_C = os.path.join(ROOT, "tests", "kf_dump.c")
BIN = "/tmp/kf_dump_mirror"


def build_and_run():
    cc = subprocess.run(
        ["gcc", "-O1", "-I", os.path.join(ROOT, "cl_dll", "include"),
         "-o", BIN, DUMP_C, "-lm"],
        capture_output=True, text=True)
    if cc.returncode != 0:
        print(cc.stdout + cc.stderr)
        sys.exit("kf_dump.c did not compile")
    out = subprocess.run([BIN], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("kf_dump crashed")
    return out.stdout


# Each kind maps to a lambda taking the four generic input columns and returning
# the mirror's answer. Keeping this table flat makes an unhandled kind loud.
def _metric(field):
    return lambda a, b, c, d, e: R.compute_metrics(int(a), float(b), int(c))[field]


def _fallback(field):
    return lambda a, b, c, d, e: R.compute_metrics_for_font(int(a), float(b))[field]


HANDLERS = {
    "scale": lambda a, b, c, d, e: R.scale(int(a), float(b)),
    "px":    lambda a, b, c, d, e: R.px(float(a), R.scale(int(b), 1.0)),

    "m_textH":     _metric("textH"),
    "m_padx":      _metric("padx"),
    "m_pady":      _metric("pady"),
    "m_gap":       _metric("gap"),
    "m_gapTight":  _metric("gapTight"),
    "m_vgap":      _metric("vgap"),
    "m_corner":    _metric("corner"),
    "m_outline":   _metric("outline"),
    "m_marginX":   _metric("marginX"),
    "m_marginY":   _metric("marginY"),
    "m_raise":     _metric("raise_"),
    "m_textScale": _metric("textScale"),

    "f_textH":     _fallback("textH"),
    "f_padx":      _fallback("padx"),
    "f_gap":       _fallback("gap"),
    "f_vgap":      _fallback("vgap"),
    "f_corner":    _fallback("corner"),
    "f_raise":     _fallback("raise_"),
    "f_scale":     _fallback("scale"),
    "f_textScale": _fallback("textScale"),

    "iconh": lambda a, b, c, d, e: R.icon_height(int(a),
                                                 R.scale(int(b), float(c)),
                                                 int(d)),
    "wingh": lambda a, b, c, d, e: R.wing_height(int(a),
                                                 R.scale(int(b), float(c))),
    "iconw": lambda a, b, c, d, e: R.icon_width(int(a), int(b), int(c)),
    "rowh":  lambda a, b, c, d, e: R.row_height(int(a), int(b)),
    "alpha": lambda a, b, c, d, e: R.row_alpha(float(a), float(b)),
    "elemy": lambda a, b, c, d, e: R.elem_y(int(c), int(b), int(d), int(e)),
    "tallest": lambda a, b, c, d, e: R.tallest([
        dict(w=40, h=27, raised=False),
        dict(w=20, h=19, raised=False),
        dict(w=25, h=int(a), raised=True),
    ]),
    "bover":  lambda a, b, c, d, e: R.border_overhang(int(a)),
    "bthick": lambda a, b, c, d, e: R.border_thickness(int(a), int(b)),
}


def main():
    rows = list(csv.DictReader(io.StringIO(build_and_run())))
    if not rows:
        sys.exit("empty dump")

    bad = 0
    counts = {}
    for r in rows:
        kind = r["kind"]
        counts[kind] = counts.get(kind, 0) + 1
        h = HANDLERS.get(kind)
        if h is None:
            print("UNHANDLED kind: %s -- the mirror has no counterpart" % kind)
            bad += 1
            continue
        exp = h(r["a"], r["b"], r["c"], r["d"], r["e"])
        got = float(r["value"])
        if isinstance(exp, float):
            ok = abs(exp - got) < 1e-6
        else:
            ok = int(exp) == int(got)
        if not ok:
            bad += 1
            if bad <= 25:
                print("MISMATCH %-12s a=%s b=%s c=%s d=%s e=%s  C=%s  py=%s"
                      % (kind, r["a"], r["b"], r["c"], r["d"], r["e"],
                         r["value"], exp))

    missing = set(HANDLERS) - set(counts)
    for kind in sorted(missing):
        print("NOT COVERED by the dump: %s" % kind)

    print("\ncases per kind:")
    for kind in sorted(counts):
        print("  %-12s %5d" % (kind, counts[kind]))

    if bad:
        print("\n%d MISMATCHES -- the previews no longer predict the game" % bad)
        return 1
    print("\nPYTHON MIRROR MATCHES C IMPLEMENTATION (%d cases)" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
