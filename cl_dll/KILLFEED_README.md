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
Measured **in pixels** from the approved v5 reference and kept in one source of
truth: `cl_dll/killfeed_ref/kf_ratios.py` (the preview) mirrors the C constants
in `death.cpp`. Reference: plate 56px, bold name cap 22px, weapon icon 34px,
modifier 27px, pitch ~58px. Everything is keyed to the engine console line
height (TL) — the one font we cannot resize — so the whole feed scales with the
player-name size. `cl_killfeed_scale` (0.5–3.0) enlarges the whole block.
Ordering that matters: weapon(0.61*H) > modifier(0.50*H) > name-cap(~0.39*H).

Known limitation: player names use the engine console font via
`pfnDrawConsoleString`, which is not bold and cannot be arbitrarily resized. In
the reference mock the names are bold; in-game they render at the console
font's native weight/size. Making them bold requires swapping the engine font
(out of scope for the killfeed).

## Verification done
- Host unit tests: modifier decode order, no double-headshot, animation curve,
  row geometry (strictly increasing x, wing small-gap). All pass.
- `g++ -fsyntax-only` against the real client headers: clean.
- Faithful pixel preview rendered from the actual SPR32 + death.cpp constants.
- NOT yet built into an APK (needs the Android NDK toolchain / CI).
