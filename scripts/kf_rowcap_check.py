#!/usr/bin/env python3
"""Check that the death-notice row cap is the SAME number everywhere.

MAX_DEATHNOTICES sizes the real array in death.cpp, but the host test harnesses
cannot include hud.h (it needs the engine headers), so each keeps a private copy.
A private copy that drifts is worse than no test: tests/test_killfeed_cvars.c
kept asserting a ceiling of 5 after the array had grown to 6, so it "passed"
while pinning a bound the game no longer had.

Also checks the shipped cl_killfeed_rows default, which must be reachable --
death.cpp clamps it to MAX_DEATHNOTICES, so a default above the cap is silently
reduced and the user never learns why.

    python3 scripts/kf_rowcap_check.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (path, regex capturing the number, human label)
SITES = [
    ("cl_dll/hud.h",
     r"^#define\s+MAX_DEATHNOTICES\s+(\d+)", "MAX_DEATHNOTICES (the real array)"),
    ("tests/test_killfeed_slots.c",
     r"^#define\s+MAX_DEATHNOTICES\s+(\d+)", "slots harness copy"),
    ("tests/test_killfeed_cvars.c",
     r"^#define\s+KF_TEST_MAX_ROWS\s+(\d+)", "cvars harness copy"),
]

DEFAULT_SITE = ("cl_dll/death.cpp",
                r'CVAR_CREATE\(\s*"cl_killfeed_rows"\s*,\s*"(\d+)"')


def grab(rel, pattern, label):
    path = os.path.join(ROOT, rel)
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as exc:
        print("cannot read %s: %s" % (rel, exc))
        return None
    m = re.search(pattern, text, re.M)
    if not m:
        print("NOT FOUND: %s in %s (pattern %s)" % (label, rel, pattern))
        return None
    return int(m.group(1))


def main():
    values = {}
    bad = 0
    for rel, pattern, label in SITES:
        v = grab(rel, pattern, label)
        if v is None:
            bad += 1
            continue
        values[label] = (rel, v)

    if bad:
        return 1

    distinct = {v for _, v in values.values()}
    if len(distinct) != 1:
        print("ROW CAP MISMATCH -- the harnesses no longer test the real bound:")
        for label, (rel, v) in values.items():
            print("  %-34s %s  (%s)" % (label, v, rel))
        return 1

    cap = distinct.pop()

    default = grab(DEFAULT_SITE[0], DEFAULT_SITE[1], "cl_killfeed_rows default")
    if default is None:
        return 1
    if default > cap:
        print("cl_killfeed_rows default %d exceeds the cap %d -- death.cpp clamps "
              "it, so the shipped default would silently become %d"
              % (default, cap, cap))
        return 1
    if default < 1:
        print("cl_killfeed_rows default %d is below 1" % default)
        return 1

    print("row cap consistent: MAX_DEATHNOTICES = %d in all %d sites, "
          "cl_killfeed_rows default %d fits" % (cap, len(values), default))
    return 0


if __name__ == "__main__":
    sys.exit(main())
