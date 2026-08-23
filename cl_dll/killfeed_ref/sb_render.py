#!/usr/bin/env python3
"""
sb_render.py — geometry render of the ported scoreboard.

NOT engine output (no NDK in this sandbox). It re-implements the layout that
cl_dll/hud/scoreboard.cpp computes, using the same formulas, so the port can be
eyeballed for breakage before a real build exists. Everything positional here is
read off the C++: column anchors, slot advances, the adaptive width/pitch, the
scheme colours and the local-player highlight.

Mirrors, in order:
  Scoreboard_CountRoster / Scoreboard_ComputeGeometry   -> panel + s_rowPitch
  DrawScoreboard   header at slot 0 (+5px), separator at slot 2, then +0.8
  DrawTeams        team row, +1.2 underline, +0.4, members, +2 on exit
  DrawPlayers      +1 per player
  columns          PING=xend-15, then each = prev.end-10, NAME=xstart+15

Usage:
  python3 sb_render.py --players 10 --teams 2 --out shot.png
  python3 sb_render.py --players 2            # sparse roster (compact panel)
  python3 sb_render.py --players 32           # full server (compressed pitch)
  python3 sb_render.py --no-scheme            # built-in palette instead of .res
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = (
    "/usr/share/fonts/ttf-dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf",
)

# ClientScheme.res-ish palette (what the .res reader supplies when present)
SCHEME = dict(
    bg=(0, 0, 0, 153),            # ListBG
    selected_bg=(255, 255, 255, 25),  # SelectionBG
    header_text=(255, 255, 255),  # BrightBaseText
    divider=(120, 120, 120, 255), # BorderDark
    team1=(255, 64, 64),          # Terrorists
    team2=(112, 176, 255),        # Counter-Terrorists
    team0=(200, 200, 200),        # spectators
)
AMBER = (255, 140, 0)


def font(sz):
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


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
        running_extent = 8.8 + 3.6 * num_teams + num_players
        drawn_bottom = 3.6 * num_teams + num_players + 0.8
    else:
        running_extent = 4.8 + num_players
        drawn_bottom = 2.8 + num_players
    if num_players <= 0:
        running_extent, drawn_bottom = 4.8, 2.8

    charH = max(charH, 8)
    pad = min(max(charH // 3, 4), 12)
    natural = max(charH + pad, 15.0)
    avail = scr_h * 0.92
    fit = avail / running_extent if running_extent > 0 else natural
    pitch = max(min(natural, fit), 1.0)

    board_h = int((drawn_bottom + 0.5) * pitch + 0.5)
    avail_h = int(scr_h * 0.95)
    board_h = min(board_h, avail_h)
    board_h = min(max(board_h, int(pitch * 4.0)), avail_h)

    xstart = (scr_w - board_w) // 2
    ystart = max((scr_h - board_h) // 2, 0)
    return dict(xstart=xstart, xend=xstart + board_w,
                ystart=ystart, yend=ystart + board_h, pitch=pitch)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--players", type=int, default=10)
    ap.add_argument("--teams", type=int, default=2)
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--charh", type=int, default=13, help="GetCharHeight()")
    ap.add_argument("--no-scheme", action="store_true",
                    help="built-in palette (bloom_scoreboard_scheme 0)")
    ap.add_argument("--show-kit", action="store_true",
                    help="bloom_scoreboard_show_defusekit 1")
    ap.add_argument("--out", default="/var/minis/attachments/sb_render.png")
    a = ap.parse_args()

    use_scheme = not a.no_scheme
    W, H = a.width, a.height
    g = compute_geometry(W, H, a.charh, a.players, True, a.teams)
    xstart, xend, ystart, yend = g["xstart"], g["xend"], g["ystart"], g["yend"]
    pitch = g["pitch"]

    FS = max(9, int(a.charh * 0.95))
    FN = font(FS)
    img = Image.new("RGBA", (W, H), (44, 58, 44, 255))
    d0 = ImageDraw.Draw(img)
    for i in range(0, W, 64):                      # faint map-ish backdrop
        d0.line([(i, 0), (i + 30, H)], fill=(52, 68, 52, 255), width=22)

    # panel (translucent, composited so alpha is real)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    dl = ImageDraw.Draw(layer, "RGBA")
    bg = SCHEME["bg"] if use_scheme else (0, 0, 0, 153)
    dl.rectangle([xstart, ystart, xend - 1, yend - 1], fill=bg)
    dl.rectangle([xstart, ystart, xend - 1, yend - 1],
                 outline=(255, 255, 255, 40))       # m_bDrawStroke
    img.alpha_composite(layer)
    d = ImageDraw.Draw(img, "RGBA")

    def tw(s):
        return int(d.textlength(s, font=FN))

    # ---- columns, exactly as DrawScoreboard computes them ----
    class Col:
        def __init__(self, start, name=None):
            self.start = start
            self.name = name
            self.end = start - (tw(name) if name else 0)

    ping = Col(xend - 15, "Ping");            ping.end = min(ping.end, ping.start - tw("9999"))
    deaths = Col(ping.end - 10, "Deaths");    deaths.end = min(deaths.end, deaths.start - tw("9999"))
    kills = Col(deaths.end - 10, "Score");    kills.end = min(kills.end, kills.start - tw("9999"))
    money = Col(kills.end - 10, "Money");     money.end = min(money.end, money.start - tw("$999999"))
    hp = Col(money.end - 10, "Health");       hp.end = min(hp.end, hp.start - tw("999999"))
    attrib = Col(hp.end - 10);                attrib.end = attrib.start - tw("Has Defuse Kit")
    name_start = xstart + 15
    name_end = attrib.end - 10

    hdr = SCHEME["header_text"] if use_scheme else AMBER
    div = SCHEME["divider"] if use_scheme else AMBER + (255,)

    def right_text(s, right_x, y, col):
        d.text((right_x - tw(s), y), s, font=FN, fill=col + (255,) if len(col) == 3 else col)

    # ---- header row (slot 0, +5px) ----
    slot = 0.0
    ypos = ystart + int(slot * pitch) + 5
    d.text((name_start, ypos), "de_dust2 — Bloom Server", font=FN, fill=hdr + (255,))
    for c in (hp, money, kills, deaths, ping):
        right_text(c.name, c.start, ypos, hdr)

    # ---- separator (slot 2) ----
    slot += 2
    ypos = ystart + int(slot * pitch)
    d.rectangle([xstart, ypos, xend - 1, ypos], fill=div)
    slot += 0.8

    ROSTER = [
        ("KILLER",     31, 4, 12, 4200, 100, ""),
        ("Gunner",     24, 9, 15, 3150, 87,  ""),
        ("theKing",    18, 11, 21, 800,  45,  "Bomb"),
        ("GabeN",      15, 14, 18, 1600, 0,   "Dead"),
        ("Spaceman",   12, 16, 22, 2300, 62,  ""),
        ("Boss",       9,  18, 24, 950,  33,  ""),
        ("WASD",       7,  21, 19, 400,  100, "VIP"),
        ("Gordon",     5,  23, 27, 6100, 71,  ""),
        ("!defa",      3,  25, 30, 1200, 12,  ""),
        ("GoldPlayer", 1,  28, 33, 750,  100, ""),
        ("Albert",     0,  30, 31, 300,  55,  ""),
        ("Crazyf",     0,  31, 29, 150,  8,   ""),
    ]
    per_team = a.players // max(a.teams, 1)
    leftover = a.players % max(a.teams, 1)
    team_defs = [("Terrorists", SCHEME["team1"] if use_scheme else (255, 64, 64)),
                 ("Counter-Terrorists", SCHEME["team2"] if use_scheme else (112, 176, 255))]

    idx = 0
    clipped = 0
    for t in range(min(a.teams, len(team_defs))):
        tname, tcol = team_defs[t]
        members = per_team + (1 if t < leftover else 0)

        ypos = ystart + int(slot * pitch)
        if ypos > yend:
            clipped += 1
            break
        label = "%s    -   %d player%s" % (tname, members, "" if members == 1 else "s")
        d.text((name_start, ypos + 2), label, font=FN, fill=tcol + (255,))
        right_text(str(sum(r[1] for r in ROSTER[idx:idx + members])), kills.start, ypos + 2, tcol)
        right_text(str(30 + t * 7), ping.start, ypos + 2, tcol)

        slot += 1.2
        uy = ystart + int(slot * pitch)
        d.rectangle([xstart, uy, xend - 1, uy], fill=tcol + (255,))
        slot += 0.4

        for m in range(members):
            r = ROSTER[idx % len(ROSTER)]
            idx += 1
            py = ystart + int(slot * pitch)
            if py > yend:
                clipped += 1
                break

            nm, frags, deaths_v, ping_v, money_v, hpv, attr = r
            col = tcol
            if attr == "Dead":                      # dead rows dim to 0.42
                col = tuple(max(int(c * 0.42), 35) for c in tcol)

            is_local = (nm == "GoldPlayer")
            if is_local:
                band = max(int(pitch), 1)
                sel = SCHEME["selected_bg"] if use_scheme else (255, 255, 255, 15)
                lay = Image.new("RGBA", img.size, (0, 0, 0, 0))
                ImageDraw.Draw(lay, "RGBA").rectangle(
                    [xstart, py, xend - 1, py + band - 1], fill=sel)
                img.alpha_composite(lay)

            d.text((name_start + 10, py + 2), nm, font=FN, fill=col + (255,))
            if attr:
                if attr == "Has Defuse Kit" and not a.show_kit:
                    pass
                else:
                    right_text(attr, attrib.start, py + 2, col)
            if hpv > 0 and attr != "Dead":
                right_text(str(hpv), hp.start, py + 2, col)
            right_text("$%d" % money_v, money.start, py + 2, col)
            right_text(str(frags), kills.start, py + 2, col)
            right_text(str(deaths_v), deaths.start, py + 2, col)
            right_text(str(ping_v), ping.start, py + 2, col)

            slot += 1.0
        slot += 2.0

    img.convert("RGB").save(a.out)
    print("players=%d teams=%d  panel=%dx%d at (%d,%d)  pitch=%.1f  scheme=%s"
          % (a.players, a.teams, xend - xstart, yend - ystart, xstart, ystart,
             pitch, "on" if use_scheme else "off"))
    print("clipped rows: %d  (must be 0)" % clipped)
    print("saved", a.out)


if __name__ == "__main__":
    main()
