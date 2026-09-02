/*
 * killfeed_layout.h - engine-independent killfeed logic (CS2-style death notice)
 *
 * This header holds the PURE logic of the killfeed: decoding ReGameDLL kill
 * rarity flags into an ordered list of modifier icons, and computing row
 * width / element x-offsets from measured pixel metrics. It deliberately has
 * NO engine dependency (only ints), so it can be unit-tested on the host with
 * a plain gcc harness (see tests/test_killfeed.c) and reused verbatim by
 * death.cpp on the device.
 *
 * Icon slots are indices; death.cpp maps each slot to a loaded SPR32 handle.
 *
 * Ordering follows the real CS killfeed the user referenced, VERIFIED against
 * the reference frame and ReGameDLL's SendDeathMessage:
 *     [blind] Killer [+ [flashassist] Assister] (wing:inair) WEAPON
 *     [noscope][smoke][penetrate][headshot][domination][revenge] Victim
 *
 * The '+' and the assister appear whenever there IS an assister; the flash icon
 * between them is an extra that only appears for a flash assist.
 */
#ifndef KILLFEED_LAYOUT_H
#define KILLFEED_LAYOUT_H

/* Mirror of ReGameDLL KILLRARITY_* (dlls/gamerules.h). Kept as literals so the
 * client does not need to include server headers. Verified against
 * 3rdparty/ReGameDLL_CS/regamedll/dlls/gamerules.h. */
/* Kill "rarity" bit flags. VERIFIED against the ReGameDLL submodule vendored in
 * this repo: 3rdparty/ReGameDLL_CS/regamedll/dlls/gamerules.h, `enum KillRarity`
 * (submodule at 5.20.0.492-367-g7be9d59). Not guessed, not taken from a
 * disassembly.
 *
 * The bit 0x040 that GoldClient's 0x3ff mask includes and that I could not name
 * from the disassembly is KILLRARITY_DOMINATION_BEGAN: it fires ONCE, on the kill
 * that starts a domination, and the server sets KILLRARITY_DOMINATION together
 * with it (multiplay_gamerules.cpp: `if (iKillsUnanswered == ...) iRarity |=
 * DOMINATION_BEGAN;` then unconditionally `iRarity |= DOMINATION;`). So it needs
 * no icon of its own -- the domination icon already covers that kill. */
#define KF_RARITY_HEADSHOT          0x001
#define KF_RARITY_KILLER_BLIND      0x002
#define KF_RARITY_NOSCOPE           0x004
#define KF_RARITY_PENETRATED        0x008
#define KF_RARITY_THRUSMOKE         0x010
#define KF_RARITY_ASSISTEDFLASH     0x020
#define KF_RARITY_DOMINATION_BEGAN  0x040
#define KF_RARITY_DOMINATION        0x080
#define KF_RARITY_REVENGE           0x100
#define KF_RARITY_INAIR             0x200

/* Sanitise a player name for a single-line, left-to-right draw.
 *
 * WHAT IS STRIPPED, and why exactly this set. Established by disassembling the
 * engine's own string workers in libxash.so (Con_DrawString tail-calls 0x13bb3c,
 * Con_DrawStringLen tail-calls 0x13bfa8; note the R E segment is mapped at
 * vaddr = file + 0x4000, so read the file at vaddr - 0x4000):
 *
 *   '^' compared 12 times in the draw worker, 5 in the length worker
 *   '\n' compared 11 / 7 times
 *   '\\' (backslash), 'R', 'y', 'w', 'd' -- NOT compared even once
 *
 * So the backslash escapes are purely a CLIENT invention, living only in
 * DrawUtils::DrawHudString. There "\R" JUMPS the cursor to `iMaxX - 10
 * charwidths`; the killfeed draws with iMaxX 0, which puts the cursor at a
 * NEGATIVE x and throws the rest of the row off the left edge. HudStringLen
 * skips the same escapes when measuring, so the row would also be measured at a
 * width it is not drawn at. Player names come from the server, so this is
 * attacker-controlled. Strip them.
 *
 * '^N' colour codes are deliberately NOT stripped. The ENGINE parses them (proof
 * above), the client path parses them too, and both the draw and the measure
 * agree -- and whether they actually recolour is the user's own hud_colored
 * setting, shared with chat and the scoreboard. Stripping them here would
 * silently override that setting for the killfeed only. An earlier version of
 * this function did strip them; that was wrong.
 *
 * A newline is truncated: the engine treats it as a line break and would draw a
 * second line straight into the row below.
 *
 * dstSize includes the terminator; dst is always terminated (unless dstSize < 1).
 */
static inline void kf_sanitise_name( const char *src, char *dst, int dstSize )
{
	int o = 0;

	if( dstSize < 1 )
		return;
	if( !src )
	{
		dst[0] = 0;
		return;
	}

	for( ; *src && o + 1 < dstSize; src++ )
	{
		if( *src == '\n' || *src == '\r' )
			break;
		/* "\<one of ywdR>" -> drop both characters (client-only escapes) */
		if( src[0] == '\\' && ( src[1] == 'y' || src[1] == 'w' ||
								src[1] == 'd' || src[1] == 'R' ) )
		{
			src++;
			continue;
		}
		dst[o++] = *src;
	}
	dst[o] = 0;
}

/* Modifier icon slots (index into CHudDeathNotice::m_ModIcons[]). */
enum kf_icon_slot
{
	KFI_BLIND = 0,   /* eye, pre-killer            */
	KFI_INAIR,       /* wing, raised above weapon  */
	KFI_NOSCOPE,     /* mid                        */
	KFI_SMOKE,       /* mid                        */
	KFI_PENETRATE,   /* mid                        */
	KFI_HEADSHOT,    /* mid                        */
	KFI_PLUS,        /* '+' glue, before the assist icon */
	KFI_FLASHASSIST, /* between killer and assister*/
	KFI_DOMINATION,  /* mid: killer now dominates the victim */
	KFI_REVENGE,     /* mid: killer took revenge            */
	KFI_COUNT
};

/* Decoded, ordered modifier layout for one row. */
typedef struct
{
	int pre[4];   int nPre;   /* icons before the killer name        */
	int wing;                 /* raised icon slot, or -1             */
	/* icons after the weapon, before the victim. Capacity 6 = the maximum the
	 * flags can produce at once: noscope, smoke, penetrate, headshot,
	 * domination, revenge. Sized from the flag list, not guessed -- an overflow
	 * here would write past the struct. */
	int mid[6];   int nMid;
	int flashAssist;          /* 1 if a flash-assist icon is shown   */
} kf_row_mods;

/* Decode server rarity bitfield (+ legacy headshot byte) into ordered icons.
 * headshotByte is the 3rd DeathMsg byte (works even when KILLRARITY not sent). */
static inline void kf_decode_modifiers( int rarity, int headshotByte,
										kf_row_mods *out )
{
	out->nPre = 0;
	out->nMid = 0;
	out->wing = -1;
	out->flashAssist = ( rarity & KF_RARITY_ASSISTEDFLASH ) ? 1 : 0;

	/* pre-killer: killer state */
	if( rarity & KF_RARITY_KILLER_BLIND )
		out->pre[out->nPre++] = KFI_BLIND;

	/* raised wing: airborne kill */
	if( rarity & KF_RARITY_INAIR )
		out->wing = KFI_INAIR;

	/* mid (method), fixed order noscope -> smoke -> penetrate -> headshot,
	 * then the social flags domination/revenge last. Those two are mutually
	 * exclusive on the server (you either started dominating or took revenge),
	 * but nothing here depends on that. */
	if( rarity & KF_RARITY_NOSCOPE )
		out->mid[out->nMid++] = KFI_NOSCOPE;
	if( rarity & KF_RARITY_THRUSMOKE )
		out->mid[out->nMid++] = KFI_SMOKE;
	if( rarity & KF_RARITY_PENETRATED )
		out->mid[out->nMid++] = KFI_PENETRATE;
	if( ( rarity & KF_RARITY_HEADSHOT ) || headshotByte )
		out->mid[out->nMid++] = KFI_HEADSHOT;
	if( rarity & KF_RARITY_DOMINATION )
		out->mid[out->nMid++] = KFI_DOMINATION;
	if( rarity & KF_RARITY_REVENGE )
		out->mid[out->nMid++] = KFI_REVENGE;
}

/* ---- THE ONE SCALE ---------------------------------------------------------
 * Everything in a killfeed row derives from a SINGLE number: kf_scale().
 * Nothing below may introduce its own base. This is the fix for rows that came
 * out a different size on every screen and at every cl_killfeed_scale value.
 *
 * WHY a dedicated base instead of the console font height:
 *   gHUD.GetCharHeight() is the raster height of the engine's HUD font. It is
 *   loaded once (engine/client/cl_scrn.c SCR_LoadCreditsFont) as
 *   raster * hud_fontscale, where hud_fontscale is FCVAR_LATCH and defaults to
 *   1.0. It does NOT change with the screen resolution. So text keyed to it
 *   stayed put while icons keyed to ScreenHeight grew -- the two bases drifted
 *   apart and the plate, sized by max(text, icons), changed shape per device.
 *
 * WHY ScreenHeight and not ScreenWidth:
 *   ScreenWidth varies with aspect ratio; on a 20:9 phone it is ~2.2x the
 *   height, so width-keyed sizes explode in landscape. GoldClient divides by
 *   ScreenHeight too (client.dll 0x10061944; the same register is divided by 480
 *   to derive aspect at 0x10016307). Keying to height makes the feed occupy the
 *   same fraction of the screen on any aspect.
 *
 * UNITS: ScreenHeight is the engine's VIRTUAL height (m_scrinfo, already
 *   divided by hud_scale), so kf_scale() is in the same space as every draw
 *   call the killfeed makes. Do not mix in TrueHeight/m_flScale here.
 *
 * KF_REF_H is the reference height the measured ratios below were taken at. */
#define KF_REF_H      1080.0f
#define KF_SCALE_MIN  0.45f   /* clamp, GoldClient's floor at 0x1017d878 */
#define KF_SCALE_MAX  4.0f    /* sanity clamp so a fat cvar cannot fill the screen */

static inline float kf_scale( int screenH, float userScale )
{
	float s;
	if( screenH <= 0 )
		return KF_SCALE_MIN;
	if( userScale <= 0.0f )
		userScale = 1.0f;
	s = ( (float)screenH / KF_REF_H ) * userScale;
	if( s < KF_SCALE_MIN ) return KF_SCALE_MIN;
	if( s > KF_SCALE_MAX ) return KF_SCALE_MAX;
	return s;
}

/* Round a reference-space length into screen space. All row metrics go through
 * this one helper so a metric can never be scaled by a private formula. */
static inline int kf_px( float refPx, float scale )
{
	int v = (int)( refPx * scale + 0.5f );
	return v;
}

/* ---- row metrics, in REFERENCE pixels (at KF_REF_H) ------------------------
 * MEASURED on the native GoldClient frame (1920x1080), then divided by nothing
 * -- these ARE the reference-space numbers. Multiply by kf_scale() to draw.
 *
 * Cross-checks that made these self-consistent on the reference frame:
 *   plate height 33 = text cell 27 + 2*pady 3
 *   row pitch    36 = plate 33 + vgap 3
 *   icon box     19 = 32px texture * 0.607 shared scale
 *   '+' box       7 = 12px texture * 0.607   (7/19 == 12/32 -> ONE scale)      */
#define KF_REF_TEXT_H   27.0f  /* text cell height: plate 33 - 2*pady          */
#define KF_REF_CAP_H    12.0f  /* cap height of the glyphs (measured, 6 rows)  */
#define KF_REF_PADX     13.0f  /* plate edge -> first/last element box         */
#define KF_REF_PADY      3.0f  /* plate edge -> content, top and bottom        */
#define KF_REF_GAP       8.0f  /* between element boxes (median 8.4, n=20)     */
#define KF_REF_GAP_TIGHT 1.0f  /* wing -> weapon: boxes essentially touch      */
#define KF_REF_VGAP      3.0f  /* between consecutive plates                   */
#define KF_REF_CORNER    3.5f  /* plate corner bevel radius                    */
#define KF_REF_OUTLINE   3.0f  /* local-player plate border thickness          */
#define KF_REF_MARGIN_X 22.0f  /* feed right edge -> screen right edge         */
#define KF_REF_MARGIN_Y 21.0f  /* feed top -> screen top                       */
#define KF_REF_RAISE    11.0f  /* airborne wing: lift above the row centre     */

/* Texture height the icon scale is calibrated against: a 32px GoldClient combat
 * icon draws 19px tall on the reference, so icons are drawn at
 * texH * (19/32) * scale. Expressed as a ratio so any texture size works. */
#define KF_ICON_TEX_REF 32.0f
#define KF_ICON_BOX_REF 19.0f

/* The airborne wing is drawn at its OWN ratio, and that is a measurement, not a
 * shortcut: fitting the drawn box by IoU against the reference pixels gives
 * 25px from a 64px texture (k = 0.39), while the same fit on the weapon in the
 * same row gives 60x20 from a 32px texture (k = 0.625). One shared ratio cannot
 * produce both. Script: workspace/uicopy-kfgold/allicon_fit.py
 *
 * This does NOT reintroduce a second scale: the ratio is a per-asset constant,
 * and both still get multiplied by the ONE kf_scale(). The wing keeps its
 * proportion to the rest of the row at every resolution. */
#define KF_WING_TEX_REF 64.0f
#define KF_WING_BOX_REF 25.0f

/* One row metric bundle, all in SCREEN pixels. Computed once per frame from the
 * single scale, then passed down. Both the C draw path and the Python mirror
 * build this the same way, so they cannot drift. */
typedef struct
{
	float scale;     /* the one scale                     */
	float textScale; /* multiplier to pass to the scaled text draw call */
	int textH;       /* text cell height                  */
	int capH;        /* glyph cap height (for centring)    */
	int padx, pady;  /* plate padding                     */
	int gap;         /* between elements                  */
	int gapTight;    /* wing -> weapon                    */
	int vgap;        /* between plates                    */
	int corner;      /* plate bevel radius                */
	int outline;     /* local-player border thickness     */
	int marginX;     /* feed inset from the right edge     */
	int marginY;     /* feed inset from the top edge       */
	int raise;       /* airborne wing lift above the centre */
} kf_metrics;

/* Fill every metric from one scale. fontRasterH is the engine font's own raster
 * height, used only to derive textScale (how much the glyphs must be stretched
 * to fill the reference-proportioned text cell). */
static inline void kf_metrics_from_scale( float s, int fontRasterH,
										  kf_metrics *m )
{
	m->scale    = s;
	m->textH    = kf_px( KF_REF_TEXT_H,   s );
	m->capH     = kf_px( KF_REF_CAP_H,    s );
	m->padx     = kf_px( KF_REF_PADX,     s );
	m->pady     = kf_px( KF_REF_PADY,     s );
	m->gap      = kf_px( KF_REF_GAP,      s );
	m->gapTight = kf_px( KF_REF_GAP_TIGHT, s );
	m->vgap     = kf_px( KF_REF_VGAP,     s );
	m->corner   = kf_px( KF_REF_CORNER,   s );
	m->outline  = kf_px( KF_REF_OUTLINE,  s );
	m->marginX  = kf_px( KF_REF_MARGIN_X, s );
	m->marginY  = kf_px( KF_REF_MARGIN_Y, s );
	m->raise    = kf_px( KF_REF_RAISE,    s );
	/* floors: a metric that rounds to 0 would visually merge elements */
	if( m->textH    < 1 ) m->textH    = 1;
	if( m->capH     < 1 ) m->capH     = 1;
	if( m->padx     < 1 ) m->padx     = 1;
	if( m->pady     < 1 ) m->pady     = 1;
	if( m->gap      < 1 ) m->gap      = 1;
	if( m->vgap     < 1 ) m->vgap     = 1;
	if( m->corner   < 1 ) m->corner   = 1;
	if( m->outline  < 1 ) m->outline  = 1;
	/* gapTight is allowed to be 0: on the reference the wing touches the gun */

	m->textScale = ( fontRasterH > 0 )
				 ? ( (float)m->textH / (float)fontRasterH )
				 : 1.0f;
}

/* Resolution-driven metrics: the feed keeps the same share of the screen on any
 * device, and text is stretched to match. Requires a scalable text path. */
static inline void kf_compute_metrics( int screenH, float userScale,
									   int fontRasterH, kf_metrics *m )
{
	kf_metrics_from_scale( kf_scale( screenH, userScale ), fontRasterH, m );
}

/* Fallback when the text cannot be scaled (no mobile API, or hud_textmode 0).
 *
 * The base scale is derived so the text cell equals the font's OWN height: the
 * feed follows the font rather than the resolution. userScale then multiplies
 * EVERYTHING EXCEPT the text cell, because the engine cannot scale the glyphs on
 * this path.
 *
 * Passing userScale here is not optional: this is the DEFAULT path
 * (hud_textmode's default is 0), so dropping it silently made cl_killfeed_scale
 * a dead cvar for most players.
 *
 * This does NOT reintroduce a second base -- there is still one scale, it is just
 * anchored to the font instead of the screen. GoldClient does the same thing with
 * hud_deathnotice_iconscale, a multiplier layered on its own base scale.
 * The text/icon ratio does change with userScale on this path, which is
 * unavoidable: a fixed-size bitmap font cannot follow. */
static inline void kf_compute_metrics_for_font( int fontRasterH, float userScale,
												kf_metrics *m )
{
	float s;
	if( fontRasterH < 1 )
		fontRasterH = 1;
	if( userScale <= 0.0f )
		userScale = 1.0f;
	s = (float)fontRasterH / KF_REF_TEXT_H * userScale;
	if( s < KF_SCALE_MIN ) s = KF_SCALE_MIN;
	if( s > KF_SCALE_MAX ) s = KF_SCALE_MAX;
	kf_metrics_from_scale( s, fontRasterH, m );
	/* the glyphs are drawn at their natural size in this mode */
	m->textScale = 1.0f;
	m->textH = fontRasterH;
}

/* On-screen height of one icon: ONE scale for the whole feed, so the relative
 * sizes the artist drew survive. MEASURED: '+' (12x12 texture) lands at 7x7
 * while combat icons (32x32) land at 19x19 -- 7/19 == 12/32, which rules out
 * per-sprite normalisation to the font height.
 *
 * CLAMPED to maxH (the text cell). This is not a guess: all six plates on the
 * reference are exactly 33px = textH 27 + 2*pady 3, and the row-height formula
 * is max(textH, tallestIcon) + 2*pady. A 64px texture at the shared scale would
 * be 38px and would have produced a 44px plate on the row that carries it. It
 * did not, so the icon box is capped at the text cell before the row is sized.
 *
 * Pass maxH = 0 to skip the cap (used by the tests to probe the raw scale). */
static inline int kf_icon_height( int texH, float scale, int maxH )
{
	int h;
	if( texH <= 0 )
		return 0;
	h = (int)( (float)texH * ( KF_ICON_BOX_REF / KF_ICON_TEX_REF ) * scale
			 + 0.5f );
	if( maxH > 0 && h > maxH )
		h = maxH;
	return ( h < 1 ) ? 1 : h;
}

/* Drawn height of the airborne wing icon. Its ratio differs from the combat
 * icons' (0.39 vs 0.625, both FITTED against the reference -- see the note on
 * KF_WING_BOX_REF), and it is NOT clamped to the text cell because the icon
 * deliberately overhangs the plate rather than living inside it. */
static inline int kf_wing_height( int texH, float scale )
{
	int h;
	if( texH <= 0 )
		return 0;
	h = (int)( (float)texH * ( KF_WING_BOX_REF / KF_WING_TEX_REF ) * scale
			 + 0.5f );
	return ( h < 1 ) ? 1 : h;
}

/* Width of an icon at a given height, preserving aspect ratio. */
static inline int kf_icon_width( int naturalW, int naturalH, int h )
{
	if( naturalH <= 0 || naturalW <= 0 )
		return 0;
	return ( naturalW * h ) / naturalH;
}

/* Row height = max(text cell, tallest icon). GoldClient formula at
 * 0x10061AC1..0x10061ACF. Both inputs now come from the SAME scale (see
 * kf_metrics), so this no longer mixes two independent bases. */
static inline int kf_row_height( int textH, int tallestIconH )
{
	return ( tallestIconH > textH ) ? tallestIconH : textH;
}

/* ---- LOCAL PLAYER'S OUTLINE ------------------------------------------------
 * The border does not sit inside the plate, nor outside it: it STRADDLES the
 * plate edge.
 *
 * MEASURED on the reference (row 6 is the local player's). Centroids of the two
 * red bands, taken from the redness profile so JPEG blur cannot shift them:
 *   top band    centroid y = 203.01
 *   bottom band centroid y = 235.99
 *   distance             = 32.98 px
 * A plain plate is exactly 33px. So the plate under the outline is NOT bigger --
 * the border is centred on its edge, overhanging by (t*2)/3 on each side.
 *
 * The consequence that matters for layout: the outward half lands where a plain
 * plate's edge would be, so an outlined row occupies rowH + 2*overhang and its
 * plate is INSET by overhang. That keeps the visible gap the same above and
 * below it as everywhere else -- the reference has a constant 3px gap on all
 * five steps, including the step into the outlined row.
 *
 * Horizontally the reference does NOT inset: rows 1-5 end at x1897 while the
 * outlined row's border reaches x1899. Rows are right-aligned, so the border
 * simply overhangs toward the screen edge. Only the vertical is inset. */
static inline int kf_border_overhang( int thickness )
{
	return ( thickness * 2 ) / 3;   /* t=3 -> 2, t=1 -> 0, t=6 -> 4 */
}

/* Effective border thickness. The overhang eats into the inter-row gap, so a
 * user-cranked cl_killfeed_outline is capped: the outward part must fit inside
 * vgap, or an outlined row would touch its neighbour. */
static inline int kf_border_thickness( int scaled, int vgap )
{
	int maxT = ( vgap * 3 ) / 2;   /* inverse of kf_border_overhang() */
	if( maxT < 1 ) maxT = 1;
	if( scaled < 1 ) scaled = 1;
	if( scaled > maxT ) scaled = maxT;
	return scaled;
}

/* NOTE on row pitch. GoldClient's disassembly suggested the pitch grows by
 * (outlineThickness - 1) for each outlined neighbour, and an earlier version of
 * this header implemented that as kf_row_pitch(). The reference frame refutes it:
 * the pitch is a constant 36px on all five steps, INCLUDING the step into row 6,
 * which is the local player's outlined row. The plate stays 33px there and the
 * border straddles its edge (see KF_Border in death.cpp).
 *
 * So the pitch is simply rowHeight + vgap, which is what death.cpp accumulates.
 * kf_row_pitch() was never called by the draw path; it was removed rather than
 * left as a plausible-looking trap. */

/* Per-row fade. GoldClient's rows appear at full opacity (older rows are
 * shoved up instantly) and fade out on expiry -- there is no enter animation
 * and no horizontal drift, so age/slide are deliberately not parameters.
 * deathAgeMs <= 0 while the row is alive. Returns alpha in [0..1]. */
static inline float kf_row_alpha( float deathAgeMs, float exitMs )
{
	float p;
	if( deathAgeMs <= 0.0f )
		return 1.0f;
	if( exitMs <= 0.0f )
		return 0.0f;
	p = deathAgeMs / exitMs;
	if( p > 1.0f )
		p = 1.0f;
	return 1.0f - p;
}

/* ---- user customisation ----------------------------------------------------
 * Every console-tunable size is a MULTIPLIER on the one scale (or a plain
 * colour/flag), never an absolute pixel count. That is deliberate: an absolute
 * pixel cvar would be a second base and would break proportions on some device,
 * which is the exact class of bug this header exists to prevent.
 *
 * kf_style is filled from the cvars once per frame and passed down alongside
 * kf_metrics. Defaults reproduce the measured reference. */
typedef struct
{
	int  plate;          /* draw the backing plate at all              */
	int  plateR, plateG, plateB;
	int  plateAlpha;     /* 0..255                                     */
	int  cornerScale100; /* percent: 100 = measured radius, 0 = square */
	int  outlineScale100;/* percent: 100 = measured thickness          */
	int  bold;           /* faux-bold names (two-pass draw)            */
	int  ctR, ctG, ctB;  /* CT name colour                             */
	int  tR, tG, tB;     /* T name colour                              */
	int  iconR, iconG, iconB; /* icon tint                             */
	int  maxRows;        /* rows drawn at once                         */
} kf_style;

/* Clamp helpers: a user cvar must never be able to produce a negative size, an
 * out-of-range colour channel, or a row count the row array cannot hold. */
static inline int kf_clamp_i( int v, int lo, int hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

static inline float kf_clamp_f( float v, float lo, float hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}
/* ---- horizontal layout -----------------------------------------------------
 * ONE implementation of the element advance, shared by measuring, drawing and
 * the host tests. Previously the sequence was written out three times (row
 * width, row draw, test harness), which is exactly how those three drift apart.
 *
 * The caller measures each element (sprite or text) and appends it in draw
 * order; this assigns x positions and returns the total row width. */

#define KF_MAX_ELEMS 16

/* One measured row element, in draw order. */
typedef struct
{
	int slot;      /* icon slot (kf_icon_slot), or -1 for a text element */
	int w, h;      /* measured on-screen size, px                        */
	int tightGap;  /* 1 = use the small gap before this element (wing)    */
	int raised;    /* 1 = top-align to the row top (airborne wing icon)   */
	int x;         /* OUT: assigned left edge, px from row left          */
} kf_elem;

// Assign x to every element and return the total row width (incl. padding).
// gap is the normal element spacing, gapTight the reduced wing->weapon gap.
static inline int kf_layout_row( kf_elem *el, int n, int gap, int gapTight,
								 int padx )
{
	int x = padx, i, first = 1;
	for( i = 0; i < n; i++ )
	{
		if( el[i].w <= 0 )      /* missing sprite: skip, do not leave a hole */
		{
			el[i].x = x;
			continue;
		}
		if( !first )
			x += el[i].tightGap ? gapTight : gap;
		el[i].x = x;
		x += el[i].w;
		first = 0;
	}
	return x + padx;
}

/* Tallest element in a row, used for the row height.
 *
 * RAISED elements are EXCLUDED. Measured justification: the wing overhangs the
 * plate (box y14..38 vs plate y21..53), so it is not inside the row box at all;
 * including it would inflate the plate of any row that has one. The reference
 * confirms it -- row 1 carries the wing and its plate is 33px, identical to the
 * five rows without one. */
static inline int kf_tallest( const kf_elem *el, int n )
{
	int i, t = 0;
	for( i = 0; i < n; i++ )
		if( el[i].w > 0 && !el[i].raised && el[i].h > t )
			t = el[i].h;
	return t;
}

/* Vertical placement of one element inside its row.
 *
 * MEASURED on the reference (row 1 is the only row with an airborne kill):
 * the wing icon's box is y14..38 while its plate is y21..53. The icon starts
 * SEVEN PIXELS ABOVE the plate top -- it deliberately overhangs the plate.
 *
 * Three placement rules were scored by IoU against the reference pixels
 * (script: workspace/uicopy-kfgold/wing_rule.py), for both plausible box sizes:
 *
 *              box 25   box 27
 *   top-aligned to plate   0.291    0.000   <- what this function used to do
 *   bottom on row centre   0.575    0.523
 *   centred, then lifted   0.735    0.691   <- winner, LIFT = 11px both times
 *
 * The winning lift is the SAME 11px for both box sizes, so it is a real constant
 * of the layout and not a per-size fudge. 11px is at the reference scale, hence
 * KF_REF_RAISE below.
 *
 * Earlier I claimed "the wing's ink top is flush with the plate top". That was an
 * artefact: the scan only looked INSIDE the plate, so it could not see the part
 * sticking out above, and reported the crop's edge as the icon's edge. */
static inline int kf_elem_y( const kf_elem *el, int plateTop, int rowCenterY,
							 int raiseLift )
{
	(void)plateTop;
	if( !el->raised )
		return rowCenterY - el->h / 2;
	return rowCenterY - el->h / 2 - raiseLift;
}

#endif /* KILLFEED_LAYOUT_H */
