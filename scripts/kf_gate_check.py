#!/usr/bin/env python3
"""The border gate in death.cpp must match the one the cvar test pins.

tests/test_killfeed_cvars.c models the gate as a macro, because death.cpp cannot
be compiled on the host. A model is only worth something while it still matches
the source, and this particular gate is a DECISION (the outline is deliberately
tied to the plate) that somebody could plausibly "fix" by dropping a term.

    python3 scripts/kf_gate_check.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEATH = os.path.join(ROOT, "cl_dll", "death.cpp")
TEST = os.path.join(ROOT, "tests", "test_killfeed_cvars.c")

# the three terms the gate must AND together, in death.cpp's own spelling
TERMS = ["item->bLocal", "st->outlineScale100 > 0", "st->plate"]


def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def main():
    death, test = read(DEATH), read(TEST)
    bad = 0

    # the real gate: the if() that guards the border computation
    m = re.search(r"if\(\s*(item->bLocal[^)]*?)\)\s*\n\s*\{\s*\n\s*border\s*=",
                  death)
    if not m:
        print("could not find the border gate in death.cpp -- it was reworded or "
              "moved; re-read it and update this check")
        return 1
    gate = " ".join(m.group(1).split())
    print("death.cpp gate: if( %s )" % gate)

    for term in TERMS:
        if term not in gate:
            print("MISSING TERM in the real gate: %r" % term)
            print("  the outline is supposed to require ALL of: %s"
                  % ", ".join(TERMS))
            bad += 1
    extra = [t for t in re.split(r"&&", gate) if t.strip() and
             not any(term in t for term in TERMS)]
    if extra:
        print("UNMODELLED TERM(S) in the real gate: %s"
              % ", ".join(t.strip() for t in extra))
        print("  tests/test_killfeed_cvars.c models only: %s" % ", ".join(TERMS))
        bad += 1

    # the model in the test
    mm = re.search(r"#define KF_BORDER_ON\(bLocal, outlineScale100, plate\)\s*\\\s*"
                   r"\n\s*(.+)", test)
    if not mm:
        print("could not find the KF_BORDER_ON model in the cvar test")
        return 1
    model = " ".join(mm.group(1).split())
    print("test model:     %s" % model)

    for name in ("bLocal", "outlineScale100", "plate"):
        if name not in model:
            print("the test model dropped %r -- it no longer pins the gate" % name)
            bad += 1

    if bad:
        print("\nGATE MISMATCH -- the cvar test is modelling a gate the game does "
              "not have")
        return 1
    print("\nborder gate matches: all three terms present in death.cpp and modelled "
          "in the test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
