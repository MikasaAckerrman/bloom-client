#!/usr/bin/env python3
"""
sb_render.py — geometry render of the ported scoreboard.

NOT engine output (no NDK here). It re-implements what cl_dll/hud/scoreboard.cpp
computes and draws, line for line, so the port can be checked before a real
build exists. Everything below is read off the C++ or off the real game data:

  layout    Scoreboard_CountRoster / Scoreboard_ComputeGeometry -> panel + pitch
  frame     DrawUtils::DrawRectangleExt (corners inset by cornerRadius=2, so the
            border is open at the corners; stroke from the scheme's BorderDark)
  columns   PING = xend-15, each next = prev.end-10, NAME = xstart+15,
            ends clamped with min(..., start - HudStringLen("9999")) etc.
  slots     header at 0 (+5px), separator at 2, +0.8, then per team:
            row, +1.2 underline, +0.4, members (+1 each), +2 on exit
  labels    the REAL localized strings from cstrike_russian/valve_russian
  colours   the REAL resource/ClientScheme.res of the installed game

Deliberately NOT invented: there is no dead-row dimming in the code (a dead
player gets the "Убит" tag and no HP value instead), and the header is
left-aligned at the name column, not centred.

Usage:
  python3 sb_render.py --players 10
  python3 sb_render.py --players 2      # compact panel
  python3 sb_render.py --players 24     # pitch compresses, nothing clipped
  python3 sb_render.py --no-scheme      # built-in palette (cvar 0)
  python3 sb_render.py --show-kit       # bloom_scoreboard_show_defusekit 1
"""
import argparse
import os
import re

from PIL import Image, ImageDraw, ImageFont

FONTS = ("/usr/share/fonts/ttf-dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf")

RES = "/var/minis/mounts/xash/cstrike/resource/ClientScheme.res"

# Built-in fallbacks, i.e. exactly what the C++ uses when the scheme has no key.
AMBER = (255, 140, 0)
BUILTIN = dict(
    bg=(0, 0, 0, 153),
    selected_bg=(255, 255, 255, 15),
    header_text=AMBER,
    divider=AMBER + (255,),
    team1=None,   # falls back to GetTeamColor
    team2=None,
    team0=None,
)
# GetTeamColor equivalents (client built-in) for the fallback path
GETTEAMCOLOR = {"T": (255, 63, 63), "CT": (153, 204, 255), "SPEC": (200, 200, 200)}

# Real localized strings (cstrike_russian.txt / valve_russian.txt)
L = dict(
    ping="Пинг", deaths="Смертей", score="Счет",
    money="Деньги", health="Здоровье",
    kit="Набор сапера", dead="Убит", bomb="Бомба", vip="VIP",
    ter="Террористы", ct="Контр-Террористы", spectators="Наблюдатели",
    fmt_one="%s    -   %s игрок", fmt_many="%s    -   %s игроков",
)


def font(sz):
    for p in FONTS:
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def parse_scheme(path):
    """Minimal reader matching cl_sb_scheme.cpp's behaviour for our keys."""
    if not os.path.exists(path):
        return {}
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for key in ("ListBG", "SelectionBG", "BrightBaseText", "BaseText",
                "BorderDark", "team0", "team1", "team2"):
        # skip commented-out lines, take the first live definition
        for m in re.finditer(r'^\s*"%s"\s+"([0-9 ]+)"' % key, txt, re.M):
            nums = [int(v) for v in m.group(1).split()]
            while len(nums) < 4:
                nums.append(255)
            out[key] = tuple(nums[:4])
            break
    return out


def compute_geometry(scr_w, scr_h, charH, num_players, teamplay, num_teams,
                     max_frac=0.72, compact_frac=0.62):
    """1:1 port of Scoreboard_ComputeGeometry."""
    max_frac = min(max(max_frac, 0.55), 0.95)
    compact_frac = min(max(compact_frac, 0.50), max_frac)

    roster_t = min(max((num_players - 5) / 15.0, 0.0), 1.0)
    wfrac = compact_frac + (max_frac - compact_frac) * roster_t
    board_w = int(scr_w * wfrac)
    board_w = min(max(board_w, int(scr_w * 0.50)), int(scr_w * 0.95))

    if teamplay:
        running = 8.8 + 3.6 * num_teams + num_players
        bottom = 3.6 * num_teams + num_players + 0.8
    else:
        running = 4.8 + num_players
        bottom = 2.8 + num_players
    if num_players <= 0:
        running, bottom = 4.8, 2.8

    charH = max(charH, 8)
    pad = min(max(charH // 3, 4), 12)
    natural = max(charH + pad, 15.0)
    fit = (scr_h * 0.92) / running if running > 0 else natural
    pitch = max(min(natural, fit), 1.0)

    board_h = int((bottom + 0.5) * pitch + 0.5)
    avail_h = int(scr_h * 0.95)
    board_h = min(min(max(board_h, int(pitch * 4.0)), avail_h), avail_h)

    xstart = (scr_w - board_w) // 2
    ystart = max((scr_h - board_h) // 2, 0)
    return dict(xstart=xstart, xend=xstart + board_w,
                ystart=ystart, yend=ystart + board_h, pitch=pitch)


# roster: name, frags, deaths, ping, money, hp, tag, bot
ROSTER_T = [
    ("KILLER",     31, 4,  12, 4200, 100, "",     False),
    ("!defa",      24, 9,  15, 3150, 87,  "bomb", False),
    ("Boss",       18, 11, 21, 800,  45,  "",     False),
    ("GabeN",      15, 14, 18, 1600, 0,   "dead", False),
    ("WASD",       12, 16, 0,  2300, 62,  "",     True),
    ("Psycho",     9,  18, 24, 950,  33,  "",     False),
    ("Albert",     7,  21, 19, 400,  100, "kit",  False),
    ("theKing",    5,  23, 27, 6100, 71,  "",     False),
]
ROSTER_CT = [
    ("Gunner",     28, 6,  14, 5200, 100, "",     False),
    ("GoldPlayer", 22, 10, 16, 2750, 84,  "kit",  False),   # local player
    ("Spaceman",   17, 12, 23, 1900, 55,  "",     False),
    ("Gordon",     13, 15, 31, 3400, 0,   "dead", False),
    ("Crazyf",     10, 17, 0,  1250, 91,  "",     True),
    ("theBest",    8,  19, 26, 700,  40,  "vip",  False),
    ("NoScope",    6,  22, 20, 2100, 68,  "",     False),
    ("Ranger",     4,  24, 29, 500,  22,  "",     False),
]
LOCAL = "GoldPlayer"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--players", type=int, default=10)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--charh", type=int, default=13, help="GetCharHeight()")
    ap.add_argument("--no-scheme", action="store_true")
    ap.add_argument("--show-kit", action="store_true")
    ap.add_argument("--out", default="/var/minis/attachments/sb_render.png")
    a = ap.parse_args()

    sch = {} if a.no_scheme else parse_scheme(RES)

    def col_bg():
        return sch.get("ListBG", BUILTIN["bg"])

    def col_sel():
        return sch.get("SelectionBG", BUILTIN["selected_bg"])

    def col_hdr():
        c = sch.get("BrightBaseText")
        return c[:3] if c else AMBER

    def col_div():
        c = sch.get("BorderDark")
        return c if c else BUILTIN["divider"]

    def col_team(side):
        key = {"T": "team1", "CT": "team2", "SPEC": "team0"}[side]
        c = sch.get(key)
        return c[:3] if c else GETTEAMCOLOR[side]

    # split roster between the two teams like a server would
    n = max(0, min(a.players, len(ROSTER_T) + len(ROSTER_CT)))
    n_t = (n + 1) // 2
    n_ct = n - n_t
    teams = [("T", L["ter"], ROSTER_T[:n_t]), ("CT", L["ct"], ROSTER_CT[:n_ct])]
    teams = [t for t in teams if t[2]]

    W, H = a.width, a.height
    g = compute_geometry(W, H, a.charh, n, True, len(teams))
    xstart, xend, ystart, yend, pitch = (g["xstart"], g["xend"], g["ystart"],
                                         g["yend"], g["pitch"])

    FN = font(max(9, int(a.charh * 0.95)))
    img = Image.new("RGBA", (W, H), (48, 54, 42, 255))
    d0 = ImageDraw.Draw(img)
    for i in range(0, W, 70):                      # backdrop so alpha is visible
        d0.line([(i, 0), (i + 34, H)], fill=(58, 66, 50, 255), width=26)

    d = ImageDraw.Draw(img, "RGBA")

    def tw(s):
        return int(d.textlength(s, font=FN))

    # ---- DrawRectangleExt: fill (blended) + stroke inset by cornerRadius ----
    CR = 2
    lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    dl = ImageDraw.Draw(lay, "RGBA")
    dl.rectangle([xstart, ystart, xend - 1, yend - 1], fill=col_bg())
    img.alpha_composite(lay)

    wide, tall = xend - xstart, yend - ystart
    st = col_div()[:3] + (255,)
    d.rectangle([xstart + CR, ystart, xstart + wide - CR - 1, ystart], fill=st)
    d.rectangle([xstart, ystart + CR, xstart, ystart + tall - CR - 1], fill=st)
    d.rectangle([xstart + wide - 1, ystart + CR,
                 xstart + wide - 1, ystart + tall - CR - 1], fill=st)
    d.rectangle([xstart + CR, ystart + tall - 1,
                 xstart + wide - CR - 1, ystart + tall - 1], fill=st)

    # ---- columns, exactly as DrawScoreboard computes them ----
    class Col:
        def __init__(self, start, name=None, reverse=True):
            self.start, self.name = start, name
            if name:
                self.end = start - tw(name) if reverse else start + tw(name)
            else:
                self.end = start

    ping = Col(xend - 15, L["ping"])
    ping.end = min(ping.end, ping.start - tw("9999"))
    deaths = Col(ping.end - 10, L["deaths"])
    deaths.end = min(deaths.end, deaths.start - tw("9999"))
    kills = Col(deaths.end - 10, L["score"])
    kills.end = min(kills.end, kills.start - tw("9999"))
    money = Col(kills.end - 10, L["money"])
    money.end = min(money.end, money.start - tw("$999999"))
    hp = Col(money.end - 10, L["health"])
    hp.end = min(hp.end, hp.start - tw("999999"))
    attrib = Col(hp.end - 10)
    attrib.end = attrib.start - tw(L["kit"])
    name_col_start = xstart + 15

    def rev(s, right_x, y, col):
        """DrawHudStringReverse: text ends at right_x."""
        d.text((right_x - tw(s), y), s, font=FN, fill=tuple(col) + (255,))

    hdr = col_hdr()

    # ---- header row: slot 0, +5px ----
    slot = 0.0
    ypos = ystart + int(slot * pitch) + 5
    d.text((name_col_start, ypos), "Bloom Dedicated Server", font=FN,
           fill=hdr + (255,))
    for c in (hp, money, kills, deaths, ping):
        rev(c.name, c.start, ypos, hdr)

    # ---- separator at slot 2 ----
    slot += 2
    ypos = ystart + int(slot * pitch)
    d.rectangle([xstart, ypos, xend - 1, ypos], fill=col_div())
    slot += 0.8

    clipped = 0
    for side, tname, members in teams:
        tc = col_team(side)

        ypos = ystart + int(slot * pitch)
        if ypos > yend:
            clipped += 1
            break
        fmt = L["fmt_one"] if len(members) == 1 else L["fmt_many"]
        d.text((name_col_start, ypos), fmt % (tname, len(members)), font=FN,
               fill=tuple(tc) + (255,))
        rev(str(sum(m[1] for m in members)), kills.start, ypos, tc)
        rev(str(sum(m[3] for m in members) // max(len(members), 1)),
            ping.start, ypos, tc)

        slot += 1.2
        uy = ystart + int(slot * pitch)
        d.rectangle([xstart, uy, xend - 1, uy], fill=tuple(tc) + (255,))
        slot += 0.4

        for (nm, frags, dths, png, mny, hpv, tag, bot) in members:
            ypos = ystart + int(slot * pitch)
            if ypos > yend:
                clipped += 1
                break

            if nm == LOCAL:
                band = max(int(pitch), 1)
                lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
                ImageDraw.Draw(lay, "RGBA").rectangle(
                    [xstart, ypos, xend - 1, ypos + band - 1], fill=col_sel())
                img.alpha_composite(lay)

            # name: COL_NAME.start + nameoffset(10) for team members
            d.text((name_col_start + 10, ypos), nm, font=FN,
                   fill=tuple(tc) + (255,))

            # attrib column: dead > bomb > vip > kit (kit gated by cvar)
            if tag == "dead":
                rev(L["dead"], attrib.start, ypos, tc)
            elif tag == "bomb":
                rev(L["bomb"], attrib.start, ypos, tc)
            elif tag == "vip":
                rev(L["vip"], attrib.start, ypos, tc)
            elif tag == "kit" and a.show_kit:
                rev(L["kit"], attrib.start, ypos, tc)

            # HP only when known and alive
            if hpv >= 0 and tag != "dead":
                rev(str(hpv), hp.start, ypos, tc)
            rev("$%d" % mny, money.start, ypos, tc)
            rev(str(frags), kills.start, ypos, tc)
            rev(str(dths), deaths.start, ypos, tc)
            rev("BOT" if bot else str(png), ping.start, ypos, tc)

            slot += 1.0
        slot += 2.0

    img.convert("RGB").save(a.out)
    print("players=%d teams=%d  panel=%dx%d at (%d,%d)  pitch=%.1f  scheme=%s"
          % (n, len(teams), xend - xstart, yend - ystart, xstart, ystart, pitch,
             "off" if a.no_scheme else "ClientScheme.res"))
    if not a.no_scheme:
        print("  ListBG=%s SelectionBG=%s BorderDark=%s team1=%s team2=%s"
              % (sch.get("ListBG"), sch.get("SelectionBG"), sch.get("BorderDark"),
                 sch.get("team1"), sch.get("team2")))
    print("clipped rows: %d (must be 0)" % clipped)
    print("saved", a.out)


if __name__ == "__main__":
    main()
