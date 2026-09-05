#!/usr/bin/env python3
"""Keep tests/test_killfeed_bounds.c honest about KF_BuildRow.

The bounds test counts the worst-case element count ARITHMETICALLY, re-stating
KF_BuildRow's push sequence by hand. That is a copy of the logic, not the logic:
add one KF_PUSH_ICON to death.cpp and the test keeps reporting the old worst case
while the real array overflows.

This diffs the two: it extracts the actual push sites from KF_BuildRow (in order,
with the loop/condition each one sits under) and checks that the bounds test
accounts for exactly that many contributions.

    python3 scripts/kf_pushcount_check.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEATH = os.path.join(ROOT, "cl_dll", "death.cpp")
BOUNDS = os.path.join(ROOT, "tests", "test_killfeed_bounds.c")

# Each push site in KF_BuildRow, in draw order, with how many elements it can
# contribute at most and the token the bounds test must use to account for it.
# Written as (regex matching the push line, max contribution, accounting token).
EXPECTED = [
    (r"KF_PUSH_ICON\(\s*s_kfModSpr\[item->mods\.pre\[j\]\]", "nPre",  "m.nPre"),
    (r"KF_PUSH_TEXT\(\s*item->szKiller",                     1,      "killer"),
    (r"KF_PUSH_ICON\(\s*s_kfModSpr\[KFI_PLUS\]",             1,      "plus"),
    (r"KF_PUSH_ICON\(\s*s_kfModSpr\[KFI_FLASHASSIST\]",      1,      "flashAssist"),
    (r"KF_PUSH_TEXT\(\s*item->szAssister",                   1,      "assister"),
    (r"KF_PUSH_ICON\(\s*s_kfModSpr\[item->mods\.wing\]",     1,      "wing"),
    (r"KF_PUSH_ICON\(\s*s_kfWeaponSpr\[item->iKfWeapon\]",   1,      "weapon"),
    (r"KF_PUSH_ICON\(\s*s_kfModSpr\[item->mods\.mid\[j\]\]", "nMid", "m.nMid"),
    (r"KF_PUSH_TEXT\(\s*item->szVictim",                     1,      "victim"),
]


def build_row_body(text):
    """The KF_BuildRow body, from its signature to the #undef pair that ends it."""
    start = text.find("KF_BuildRow(")
    if start < 0:
        return None
    end = text.find("#undef KF_PUSH_TEXT", start)
    if end < 0:
        return None
    return text[start:end]


def main():
    try:
        with open(DEATH, "r", encoding="utf-8", errors="replace") as f:
            death = f.read()
        with open(BOUNDS, "r", encoding="utf-8", errors="replace") as f:
            bounds = f.read()
    except OSError as exc:
        print("cannot read a source file: %s" % exc)
        return 1

    body = build_row_body(death)
    if body is None:
        print("could not locate the KF_BuildRow body in death.cpp -- the markers "
              "(signature / #undef KF_PUSH_TEXT) moved")
        return 1

    # every actual push site, in source order
    actual = [m.group(0) for m in
              re.finditer(r"KF_PUSH_(?:ICON|TEXT)\([^\n]*", body)]
    # drop the two macro DEFINITIONS, which match the same shape
    actual = [a for a in actual if "do {" not in a]

    if len(actual) != len(EXPECTED):
        print("PUSH SITE COUNT CHANGED: death.cpp has %d, the bounds test models "
              "%d." % (len(actual), len(EXPECTED)))
        print("KF_BuildRow now pushes:")
        for a in actual:
            print("    %s" % a.strip()[:88])
        print("If this is intentional, update EXPECTED here AND the arithmetic in "
              "tests/test_killfeed_bounds.c, then re-run it: the worst case and "
              "the KF_MAX_ELEMS headroom both move.")
        return 1

    bad = 0
    for i, (pattern, _, token) in enumerate(EXPECTED):
        if not re.search(pattern, actual[i]):
            print("PUSH SITE %d IS NOT WHAT THE TEST MODELS" % i)
            print("  death.cpp: %s" % actual[i].strip()[:88])
            print("  expected pattern: %s" % pattern)
            bad += 1
        if token not in bounds:
            print("the bounds test does not account for %r (push site %d)"
                  % (token, i))
            bad += 1

    if bad:
        return 1

    print("push sequence matches: %d sites in KF_BuildRow, all accounted for in "
          "the bounds test" % len(actual))
    return 0


if __name__ == "__main__":
    sys.exit(main())
