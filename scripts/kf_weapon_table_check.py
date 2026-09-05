#!/usr/bin/env python3
"""Verify the killfeed weapon table against the sprites that ship with it.

The table in death.cpp maps a DeathMsg weapon name to a sprite FILE NAME. Nothing
in the C code checks that the file exists: a typo produces SPR_Load() == 0, which
draws no icon and reports nothing. This parses the real table out of the source
and cross-checks it against the sprite directory, so a bad entry fails a check
instead of silently losing an icon in game.

It also flags:
  * duplicate msg keys  -- the lookup is a flat first-match scan, so a second
                           entry with the same key is dead code
  * table overflow      -- KF_LoadIcons stops at KF_MAX_WEAPONS entries
  * unused sprite files -- shipped but never referenced (dead weight in the APK)

    python3 scripts/kf_weapon_table_check.py [sprite_dir] [death.cpp]
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SPR_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    ROOT, "3rdparty/cs16client-extras/sprites/kf")
SRC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "cl_dll/death.cpp")


def parse_table(src_text):
    """Return [(msg, spr)] from the kf_weapons[] initialiser."""
    m = re.search(r"kf_weapons\[\]\s*=\s*\{(.*?)\n\};", src_text, re.S)
    if not m:
        raise SystemExit("kf_weapons[] not found in %s" % SRC)
    body = m.group(1)
    # strip // comments so a name mentioned in prose is not picked up
    body = re.sub(r"//[^\n]*", "", body)
    return re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', body)


def parse_mod_names(src_text):
    """Return the modifier sprite basenames from kf_mod_names[]."""
    # the array is declared with a size (kf_mod_names[KFI_COUNT]), not empty
    m = re.search(r"kf_mod_names\[[^\]]*\]\s*=\s*\{(.*?)\n\};", src_text, re.S)
    if not m:
        raise SystemExit("kf_mod_names[] not found in %s" % SRC)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    return re.findall(r'"([^"]+)"', body)


def parse_max(src_text):
    m = re.search(r"#define\s+KF_MAX_WEAPONS\s+(\d+)", src_text)
    return int(m.group(1)) if m else None


def main():
    src = open(SRC, encoding="utf-8", errors="replace").read()
    table = parse_table(src)
    mods = parse_mod_names(src)
    limit = parse_max(src)

    on_disk = {f[:-4] for f in os.listdir(SPR_DIR) if f.endswith(".spr")}
    problems = []

    # 1. every referenced sprite must exist
    referenced = set()
    for msg, spr in table:
        referenced.add(spr)
        if spr not in on_disk:
            problems.append("weapon %-14s -> missing sprite %s.spr" % (msg, spr))
    for spr in mods:
        referenced.add(spr)
        if spr not in on_disk:
            problems.append("modifier -> missing sprite %s.spr" % spr)

    # 2. duplicate msg keys are unreachable after the first
    seen = {}
    for idx, (msg, spr) in enumerate(table):
        key = msg.lower()
        if key in seen:
            problems.append("duplicate msg key %r (entry %d shadows entry %d)"
                            % (msg, idx, seen[key]))
        else:
            seen[key] = idx

    # 3. the loader silently truncates past KF_MAX_WEAPONS
    if limit is not None and len(table) > limit:
        problems.append("table has %d entries but KF_MAX_WEAPONS is %d -- "
                        "the tail is never loaded" % (len(table), limit))

    unused = sorted(on_disk - referenced)

    print("weapon entries : %d (limit %s)" % (len(table), limit))
    print("modifier icons : %d" % len(mods))
    print("sprites on disk: %d" % len(on_disk))
    if unused:
        print("unused sprites : %s" % ", ".join(unused))

    if problems:
        print()
        for p in problems:
            print("PROBLEM: %s" % p)
        print("\n%d problem(s)" % len(problems))
        return 1

    print("table matches the shipped sprites, no duplicates, within limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
