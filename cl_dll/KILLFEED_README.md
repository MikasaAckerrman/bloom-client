# CS2-style Killfeed (bloom)

Replaces the classic single-line Valve death notice with a CS2-style killfeed:
rounded semi-transparent plates, team-coloured names, authentic CS2 weapon
icons, and CS2 kill-modifier icons decoded from ReGameDLL's kill-rarity flags.
Rows slide in from the right, hold, then fade + drift out.

## Files
- `cl_dll/death.cpp` — the killfeed HUD element (draw + message decode).
- `cl_dll/include/killfeed_layout.h` — engine-independent logic (modifier
  decode, animation curve). Unit-tested on host.
- `cl_dll/hud.h` — `CHudDeathNotice` gained icon loaders + two cvars.
- `3rdparty/cs16client-extras/sprites/kf/*.spr` — SPR32 icon pack (packed into
  the APK assets by the existing CMake extras step; on the device they live at
  `<gamedir>/sprites/kf/`).
- `tests/test_killfeed.c`, `tests/test_killfeed_geom.c` — host unit tests.

## cvars
- `cl_killfeed` (default 1) — 1 = CS2 killfeed, 0 = classic Valve notice.
- `cl_killfeed_time` (default 6) — seconds a row stays before fading.

If the `sprites/kf` pack is missing at runtime the element automatically falls
back to the legacy notice, so the change is safe even without the assets.

## Modifiers
Decoded from the DeathMsg payload. ReGameDLL sends assister + rarity when
`mp_deathmsg_flags` includes them (default `"abc"` = position+assistant+rarity).

| icon        | source flag              | placement            |
|-------------|--------------------------|----------------------|
| blind (eye) | `KILLRARITY_KILLER_BLIND`| before killer name   |
| inair (wing)| `KILLRARITY_INAIR`       | raised, left of gun  |
| noscope     | `KILLRARITY_NOSCOPE`     | after gun            |
| smoke       | `KILLRARITY_THRUSMOKE`   | after gun            |
| penetrate   | `KILLRARITY_PENETRATED`  | after gun (wallbang) |
| headshot    | `KILLRARITY_HEADSHOT` / legacy byte | after gun |
| flash assist| `KILLRARITY_ASSISTEDFLASH` | between killer & assister |

On stock CS servers only the legacy headshot byte is present; the killfeed
still shows names + weapon + headshot.

## Icon art
Authentic CS2 killfeed vectors (Valve art) rasterized to white SPR32 at
per-category native heights (weapon 40px, modifier 46px, grenade 41px, wing
43px), aspect preserved — never stretched. `smokegrenade` is a hand-drawn
CS2-style canister (no SVG existed on the source). Baked via
`/var/minis/workspace/bloom-killfeed/png2spr32.py` (white silhouette,
premultiplied over black for additive blending).

## Metrics
Measured against the real in-game killfeed (plate H, text cap 0.39H, weapon
0.58H, modifier 0.66H, wing 0.62H, gaps 0.26H, wing→gun 0.05H, corner 0.13H).
All fractions of plate height, so the feed scales with `hud_scale`/DPI.

## Verification done
- Host unit tests: modifier decode order, no double-headshot, animation curve,
  row geometry (strictly increasing x, wing small-gap). All pass.
- `g++ -fsyntax-only` against the real client headers: clean.
- Faithful pixel preview rendered from the actual SPR32 + death.cpp constants.
- NOT yet built into an APK (needs the Android NDK toolchain / CI).
