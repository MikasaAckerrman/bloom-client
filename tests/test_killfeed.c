/* Host unit test for killfeed_layout.h — build & run with plain gcc, no engine.
 *   cc -I../cl_dll/include -o /tmp/kf_test test_killfeed.c && /tmp/kf_test
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "killfeed_layout.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
	if(!(cond)){ printf("FAIL: %s\n", msg); fails++; } \
} while(0)

static void test_decode_basic(void)
{
	kf_row_mods m;
	kf_decode_modifiers(0, 0, &m);
	CHECK(m.nPre == 0 && m.nMid == 0 && m.wing == -1 && m.flashAssist == 0,
		  "empty rarity -> no icons");

	/* legacy headshot byte alone still adds headshot */
	kf_decode_modifiers(0, 1, &m);
	CHECK(m.nMid == 1 && m.mid[0] == KFI_HEADSHOT, "legacy headshot byte");

	/* blind is pre-killer */
	kf_decode_modifiers(KF_RARITY_KILLER_BLIND, 0, &m);
	CHECK(m.nPre == 1 && m.pre[0] == KFI_BLIND, "blind -> pre");

	/* inair is the wing */
	kf_decode_modifiers(KF_RARITY_INAIR, 0, &m);
	CHECK(m.wing == KFI_INAIR, "inair -> wing");

	/* flash assist flag */
	kf_decode_modifiers(KF_RARITY_ASSISTEDFLASH, 0, &m);
	CHECK(m.flashAssist == 1, "flash assist flag");
}

static void test_mid_order(void)
{
	kf_row_mods m;
	/* all mid flags set -> fixed order noscope, smoke, penetrate, headshot,
	 * then the social flags domination, revenge */
	int rarity = KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE |
				 KF_RARITY_PENETRATED | KF_RARITY_HEADSHOT;
	kf_decode_modifiers(rarity, 0, &m);
	CHECK(m.nMid == 4, "four mid icons");
	CHECK(m.mid[0] == KFI_NOSCOPE, "mid[0]=noscope");
	CHECK(m.mid[1] == KFI_SMOKE, "mid[1]=smoke");
	CHECK(m.mid[2] == KFI_PENETRATE, "mid[2]=penetrate");
	CHECK(m.mid[3] == KFI_HEADSHOT, "mid[3]=headshot");
}

static void test_domination_and_revenge(void)
{
	kf_row_mods m;

	kf_decode_modifiers(KF_RARITY_DOMINATION, 0, &m);
	CHECK(m.nMid == 1 && m.mid[0] == KFI_DOMINATION, "domination -> its icon");

	kf_decode_modifiers(KF_RARITY_REVENGE, 0, &m);
	CHECK(m.nMid == 1 && m.mid[0] == KFI_REVENGE, "revenge -> its icon");

	/* EVERY mid flag at once must fit mid[] -- that array is sized from this
	 * list (6), and an overflow would write past the struct. */
	kf_decode_modifiers(KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE |
						KF_RARITY_PENETRATED | KF_RARITY_HEADSHOT |
						KF_RARITY_DOMINATION | KF_RARITY_REVENGE, 1, &m);
	CHECK(m.nMid == 6, "all six mid flags decode");
	CHECK(m.mid[4] == KFI_DOMINATION, "domination comes after the method icons");
	CHECK(m.mid[5] == KFI_REVENGE, "revenge is last");

	/* The unidentified bit 0x040 (inside GoldClient's 0x3ff mask) must be
	 * ignored, not turned into some icon. */
	kf_decode_modifiers(0x040, 0, &m);
	CHECK(m.nMid == 0 && m.nPre == 0 && m.wing == -1 && m.flashAssist == 0,
		  "unknown rarity bit 0x040 produces no icon");
}

static void test_headshot_not_doubled(void)
{
	kf_row_mods m;
	/* headshot present in BOTH rarity and legacy byte -> only one icon */
	kf_decode_modifiers(KF_RARITY_HEADSHOT, 1, &m);
	CHECK(m.nMid == 1, "headshot not doubled");
}

static void test_full_awp_row(void)
{
	/* the marquee example: blind + inair + noscope + smoke, AWP */
	kf_row_mods m;
	int rarity = KF_RARITY_KILLER_BLIND | KF_RARITY_INAIR |
				 KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE;
	kf_decode_modifiers(rarity, 0, &m);
	CHECK(m.nPre == 1 && m.pre[0] == KFI_BLIND, "awp row pre=blind");
	CHECK(m.wing == KFI_INAIR, "awp row wing=inair");
	CHECK(m.nMid == 2 && m.mid[0] == KFI_NOSCOPE && m.mid[1] == KFI_SMOKE,
		  "awp row mid=noscope,smoke");
}

/* ---- GoldClient geometry model ------------------------------------------- */

/* Natural sprite sizes measured from the shipped SPR32 files in
 * 3rdparty/cs16client-extras/sprites/kf (header: maxwidth@16, maxheight@20).
 * Weapons are 40px tall, modifiers 46px -- so any correct model MUST normalise
 * per-sprite, otherwise the two groups render at different visual weights. */
#define NAT_WEAPON_H  40
#define NAT_MOD_H     46
#define NAT_AWP_W    156   /* ssg08/awp-class, the widest weapon */

static void test_scale_floor_and_ceiling(void)
{
	/* A tiny viewport must clamp at the floor, never below. */
	CHECK(fabsf(kf_scale(100, 0.5f) - KF_SCALE_MIN) < 1e-6f,
		  "tiny screen clamps at the floor");
	/* A fat cvar must clamp at the ceiling so the feed cannot fill the screen. */
	CHECK(fabsf(kf_scale(1080, 99.0f) - KF_SCALE_MAX) < 1e-6f,
		  "huge user scale clamps at the ceiling");
	/* Degenerate inputs fall back instead of dividing by zero / going negative. */
	CHECK(fabsf(kf_scale(0, 1.0f) - KF_SCALE_MIN) < 1e-6f, "zero height -> floor");
	CHECK(kf_scale(1080, 0.0f) == kf_scale(1080, 1.0f),
		  "zero user scale behaves as 1.0");
	CHECK(kf_scale(1080, -3.0f) == kf_scale(1080, 1.0f),
		  "negative user scale behaves as 1.0");
}

static void test_scale_is_one_at_reference(void)
{
	/* The reference frame is 1080 tall, so scale is exactly 1 there and every
	 * metric equals its measured reference value. */
	kf_metrics m;
	CHECK(fabsf(kf_scale(1080, 1.0f) - 1.0f) < 1e-6f, "scale == 1 at reference");
	kf_compute_metrics(1080, 1.0f, 27, &m);
	CHECK(m.textH == 27, "text cell 27px at reference");
	CHECK(m.padx == 13, "padx 13px at reference");
	CHECK(m.pady == 13, "pady 13px at reference (GC plate 53, text/plate 0.51)");
	CHECK(m.gap == 8,   "gap 8px at reference");
	CHECK(m.vgap == 3,  "vgap 3px at reference");
	CHECK(kf_icon_height(32, m.scale, 0) == 19,
		  "32px texture draws 19px at reference");
	CHECK(kf_icon_height(12, m.scale, 0) == 7,
		  "12px '+' texture draws 7px at reference");
}

static void test_everything_scales_together(void)
{
	/* THE fundamental property: doubling the screen doubles EVERY metric, so
	 * the feed looks identical, only bigger. This is what was broken when text
	 * was keyed to the font and icons to the resolution. */
	kf_metrics a, b;
	kf_compute_metrics(1080, 1.0f, 27, &a);
	kf_compute_metrics(2160, 1.0f, 27, &b);
	CHECK(fabsf(b.scale - a.scale * 2.0f) < 1e-6f, "scale doubles");
	CHECK(b.textH >= a.textH * 2 - 1 && b.textH <= a.textH * 2 + 1, "textH doubles");
	CHECK(b.padx  >= a.padx  * 2 - 1 && b.padx  <= a.padx  * 2 + 1, "padx doubles");
	CHECK(b.gap   >= a.gap   * 2 - 1 && b.gap   <= a.gap   * 2 + 1, "gap doubles");
	CHECK(b.vgap  >= a.vgap  * 2 - 1 && b.vgap  <= a.vgap  * 2 + 1, "vgap doubles");
	CHECK(kf_icon_height(32, b.scale, 0) >= kf_icon_height(32, a.scale, 0) * 2 - 1,
		  "icons double");

	/* The user cvar must move text and icons TOGETHER, not one of them. */
	{
		kf_metrics c;
		kf_compute_metrics(1080, 2.0f, 27, &c);
		CHECK(c.textH >= a.textH * 2 - 1 && c.textH <= a.textH * 2 + 1,
			  "user scale 2.0 doubles the text cell");
		CHECK(kf_icon_height(32, c.scale, 0) >= kf_icon_height(32, a.scale, 0) * 2 - 1,
			  "user scale 2.0 doubles the icons");
		CHECK(fabsf((float)c.textH / (float)kf_icon_height(32, c.scale, 0)
				  - (float)a.textH / (float)kf_icon_height(32, a.scale, 0)) < 0.05f,
			  "text/icon ratio is invariant under the user scale");
	}
}

static void test_ratios_are_resolution_invariant(void)
{
	/* Same shape on a phone, a tablet and a 4K monitor: the plate must occupy a
	 * constant fraction of the screen, and the metrics must keep their ratios.
	 *
	 * fontRasterH is 18 here, NOT 27, and that is deliberate. The model floors
	 * the scale so the raster font is never shrunk (see kf_compute_metrics), and
	 * with a 27px font that floor is active for every screen below 1080 -- which
	 * is a different regime, covered by test_font_floor_takes_over below. 18 is
	 * the largest font that leaves the whole 720..2160 range in the
	 * resolution-driven regime, so this test measures what it claims to.
	 *
	 * 480/540 are excluded from the tight band on purpose: at those sizes the
	 * text cell is 12-14px and integer rounding of a 3px pady is a whole ~7% of
	 * it. That is quantisation, not a second base -- the check below still
	 * requires monotonic growth there. */
	int heights[] = { 720, 1080, 1440, 2160 };
	int i;
	float ref = -1.0f;
	int prevPlate = 0;

	for( i = 0; i < (int)(sizeof(heights)/sizeof(heights[0])); i++ )
	{
		kf_metrics m;
		int plate;
		float frac;
		kf_compute_metrics(heights[i], 1.0f, 18, &m);
		plate = m.textH + m.pady * 2;
		frac = (float)plate / (float)heights[i];
		if( ref < 0.0f )
			ref = frac;
		CHECK(fabsf(frac - ref) < 0.002f,
			  "plate occupies a constant fraction of the screen");
		CHECK(plate > prevPlate, "plate grows monotonically with the screen");
		prevPlate = plate;
	}

	/* the small-screen end must still grow, even if quantisation shifts ratios */
	{
		kf_metrics a, b;
		kf_compute_metrics(480, 1.0f, 12, &a);
		kf_compute_metrics(540, 1.0f, 12, &b);
		CHECK(b.textH >= a.textH, "text cell does not shrink as the screen grows");
	}
}

static void test_font_floor_takes_over(void)
{
	/* The other regime: when the screen is small relative to the engine's font,
	 * the resolution-driven scale would demand a text cell SMALLER than the font's
	 * own raster, i.e. it would resample a bitmap font downwards and destroy the
	 * glyph strokes. The model refuses: the scale is floored so the cell equals
	 * the font, and the feed follows the FONT instead of the screen.
	 *
	 * This is exactly the user's device: 2800x1260 with hud_scale "1600" gives a
	 * VIRTUAL 720-tall screen (CL_GetScreenInfo, libxash.so 0x16eb18), while the
	 * HUD font raster is around 19-24px.
	 *
	 * GoldClient has the same floor: its row height is
	 * max(GetFontTall(DeathNoticeFont), tallest icon). */
	int rasters[] = { 19, 20, 22, 24, 32, 43 };
	int i;

	for( i = 0; i < (int)(sizeof(rasters)/sizeof(rasters[0])); i++ )
	{
		kf_metrics m;
		kf_compute_metrics(720, 1.0f, rasters[i], &m);

		CHECK(m.textScale >= 1.0f - 1e-6f,
			  "font floor: the raster font is never shrunk");
		CHECK(m.textH >= rasters[i],
			  "font floor: the text cell holds the font's own raster");
		/* and the plate still contains that cell */
		CHECK(m.pady >= 1, "font floor: padding survives");
	}

	/* Above the floor the resolution still drives everything: a bigger screen
	 * with the same font must grow. */
	{
		kf_metrics a, b;
		kf_compute_metrics(720, 1.0f, 19, &a);
		kf_compute_metrics(2160, 1.0f, 19, &b);
		CHECK(b.textH > a.textH, "above the floor, resolution drives the size");
	}

	/* The floor must not swallow the user's scale: asking for a bigger feed on a
	 * floored device still has to grow it. */
	{
		kf_metrics a, b;
		kf_compute_metrics(720, 1.0f, 19, &a);
		kf_compute_metrics(720, 1.5f, 19, &b);
		CHECK(b.textH > a.textH, "user scale still grows a floored feed");
	}
}

static void test_font_fallback_keeps_proportions(void)
{
	/* When the text cannot be scaled the feed follows the FONT instead of the
	 * resolution -- but it must still be one proportional family, and the text
	 * cell must equal the font's own height (otherwise glyphs get clipped or
	 * float inside the plate). */
	kf_metrics m;
	kf_compute_metrics_for_font(27, 1.0f, &m);
	CHECK(m.textH == 27, "fallback text cell equals the font height");
	CHECK(fabsf(m.textScale - 1.0f) < 1e-6f, "fallback draws glyphs unscaled");
	CHECK(m.padx == 13, "fallback padx matches the reference at font 27");

	kf_compute_metrics_for_font(54, 1.0f, &m);
	CHECK(m.textH == 54, "fallback tracks a bigger font");
	CHECK(m.padx >= 25 && m.padx <= 27, "fallback padx doubles with the font");
	CHECK(kf_icon_height(32, m.scale, 0) >= 37 && kf_icon_height(32, m.scale, 0) <= 39,
		  "fallback icons double with the font");
}

static void test_user_scale_works_on_the_fallback_path(void)
{
	/* hud_textmode defaults to 0, so the fallback IS the default path. Dropping
	 * userScale there made cl_killfeed_scale a dead cvar for most players. */
	kf_metrics a, b, c;
	kf_compute_metrics_for_font(27, 1.0f, &a);
	kf_compute_metrics_for_font(27, 2.0f, &b);
	kf_compute_metrics_for_font(27, 0.5f, &c);

	CHECK(fabsf(b.scale - a.scale * 2.0f) < 1e-4f, "user scale 2.0 doubles the scale");
	CHECK(fabsf(c.scale - a.scale * 0.5f) < 1e-4f, "user scale 0.5 halves the scale");
	CHECK(b.padx > a.padx, "padding grows with the user scale");
	CHECK(b.gap  > a.gap,  "gap grows with the user scale");
	CHECK(kf_icon_height(32, b.scale, 0) > kf_icon_height(32, a.scale, 0),
		  "icons grow with the user scale");

	/* The text cell CANNOT follow -- the engine draws this path at the font's
	 * natural size. Pin that so nobody "fixes" it into a lie. */
	CHECK(b.textH == 27, "the text cell stays at the font height");
	CHECK(fabsf(b.textScale - 1.0f) < 1e-6f, "glyphs stay unscaled");

	/* A nonsense value must not collapse the feed. */
	kf_compute_metrics_for_font(27, 0.0f, &b);
	CHECK(fabsf(b.scale - a.scale) < 1e-6f, "userScale 0 is treated as 1.0");
	kf_compute_metrics_for_font(27, -3.0f, &b);
	CHECK(fabsf(b.scale - a.scale) < 1e-6f, "a negative userScale is treated as 1.0");
}

static void test_icon_width_aspect(void)
{
	/* Aspect ratio is preserved and never stretched. */
	int w = kf_icon_width(NAT_AWP_W, NAT_WEAPON_H, 20);
	CHECK(w == (NAT_AWP_W * 20) / NAT_WEAPON_H, "icon width preserves aspect");
	CHECK(kf_icon_width(0, NAT_WEAPON_H, 20) == 0, "zero natural width -> 0");
	CHECK(kf_icon_width(NAT_AWP_W, 0, 20) == 0, "zero natural height -> 0");
}

static void test_row_height(void)
{
	/* Row height is the taller of font vs the biggest icon. */
	CHECK(kf_row_height(13, 20) == 20, "tall icon sets row height");
	CHECK(kf_row_height(30, 20) == 30, "tall font sets row height");
	CHECK(kf_row_height(20, 20) == 20, "equal -> same");
}

/* Every plain row must advance by the SAME amount, whatever sprites it holds.
 *
 * This is what lets death.cpp stack rows by accumulation and still show even
 * gaps. It holds because kf_icon_height() clamps an icon box to the text cell
 * and kf_tallest() skips the raised wing -- so contentH can never exceed textH.
 * If either clamp is ever removed, a row with the 64px wing texture or a future
 * oversized sprite would silently become taller than its neighbours. */
static void test_plain_rows_advance_equally(void)
{
	int screens[] = { 480, 720, 1080, 1440, 2160 };
	float uscales[] = { 0.45f, 1.0f, 2.0f, 4.0f };
	int texH[] = { 12, 32, 64, 128 };   /* plus, combat, wing texture, oversized */
	int si, ui, ti;

	for (si = 0; si < 5; si++)
	for (ui = 0; ui < 4; ui++) {
		kf_metrics m;
		kf_elem el[4];
		int n = 0;

		kf_compute_metrics(screens[si], uscales[ui], 20, &m);

		for (ti = 0; ti < 4; ti++) {
			el[n].w = 10;
			el[n].h = kf_icon_height(texH[ti], m.scale, m.textH);
			el[n].raised = 0;
			el[n].tightGap = 0;
			n++;
		}
		CHECK(kf_row_height(m.textH, kf_tallest(el, n)) == m.textH,
			  "no icon can make a plain row taller than the text cell");
	}

	/* The raised wing is the one sprite drawn larger than the cell; it must be
	 * excluded from the row height, or the wing row would be taller. */
	{
		kf_metrics m;
		kf_elem el[2];
		kf_compute_metrics(1080, 1.0f, 20, &m);

		el[0].w = 10; el[0].h = m.textH;
		el[0].raised = 0; el[0].tightGap = 0;
		el[1].w = 10; el[1].h = kf_wing_height(64, m.scale);
		el[1].raised = 1; el[1].tightGap = 0;

		CHECK(el[1].h > 0, "wing has a height");
		CHECK(kf_row_height(m.textH, kf_tallest(el, 2)) == m.textH,
			  "the raised wing does not grow the row it sits in");
	}
}

static void test_outline_keeps_gaps_uniform(void)
{
	/* The visible gap around the local player's row must equal vgap, the same as
	 * between any two plain rows.
	 *
	 * MEASURED on the reference: the gap is a constant 3px on ALL five steps,
	 * including the step into the outlined row. That works because the border is
	 * centred on the plate edge (centroids 32.98px apart vs a 33px plain plate),
	 * so its outward half lands exactly where a plain plate's edge would be.
	 *
	 * The bug this guards: drawing the plate at the row's top and letting the
	 * border overhang upward shrank the gap above it to vgap - overhang. */
	kf_metrics m;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	int plate = m.textH + m.pady * 2;
	int t = kf_border_thickness(m.outline, m.vgap);
	int over = kf_border_overhang(t);

	CHECK(t == 3, "outline thickness is 3 at the reference");
	CHECK(over == 2, "overhang is 2 at the reference");

	/* An outlined row reserves the overhang on BOTH sides, and its plate is
	 * inset by it, so the outer edges land on the plain grid. */
	int advance = plate + over * 2;
	int plainTop = 21 + 5 * (plate + m.vgap);   /* row 6's slot, marginY 21 */
	int plateTop = plainTop + over;

	/* Pinned to pady=13: plate 53, advance 57, row-6 grid line 21+5*56=301. */
	CHECK(plate == 53, "plate is 53 at the reference (GC text/plate 0.36)");
	CHECK(plateTop == 303, "outlined plate starts at y303 (pinned grid)");
	CHECK(plateTop + plate - 1 == 355, "outlined plate ends at y355 (pinned grid)");
	CHECK(plainTop == 301, "its border's outer edge is on the grid line y301");
	CHECK(plainTop + advance - 1 == 357, "outer extent ends at y357 (pinned grid)");

	/* The gap BELOW an outlined row is vgap as well: the next row starts at
	 * plainTop + advance + vgap, i.e. exactly vgap after the outer edge. */
	CHECK(advance == 57, "an outlined row occupies 57px (pinned outer extent)");

	/* The cap must keep the overhang inside the gap at every scale, or an
	 * outlined row would touch its neighbour. */
	int h;
	for( h = 240; h <= 2160; h += 60 )
	{
		kf_metrics k;
		kf_compute_metrics(h, 1.0f, 27, &k);
		int tt = kf_border_thickness(k.outline * 4, k.vgap);  /* user cranked it */
		CHECK(kf_border_overhang(tt) <= k.vgap,
			  "overhang never exceeds vgap, at any resolution");
	}
}

/* TWO outlined rows back to back.
 *
 * REACHABLE, not hypothetical: bLocal is set when the local player is the killer
 * OR the victim (death.cpp, MsgFunc_DeathMsg), so two frags in a row by the same
 * player put two outlined rows next to each other. The reference frame has only
 * ONE outlined row (the last), so this transition is NOT covered by any
 * measurement -- it has to be pinned by construction instead.
 *
 * GoldClient handles the same case with an explicit branch (client.dll
 * 0x10061e2b: `lea ecx, [ecx+eax*2]` -> gap + o*2 when the CURRENT and the NEXT
 * row are both outlined; 0x10061e41 `add ecx, [ebp-0x64]` -> gap + o when only
 * one of them is). This build takes the local route instead: every outlined row
 * reserves its own overhang on both sides, so a row never needs to know what its
 * neighbour looks like. The invariant below is what makes the two equivalent:
 * the visible gap between two outlined rows is vgap + 2*overhang, which is
 * exactly GoldClient's gap + o*2 when overhang == o.
 *
 * What must hold regardless: the two borders must NOT touch or overlap, and the
 * plain rows around them must stay on the same grid. */
static void test_two_outlined_rows_in_a_row(void)
{
	kf_metrics m;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	int plate   = m.textH + m.pady * 2;
	int t       = kf_border_thickness(m.outline, m.vgap);
	int over    = kf_border_overhang(t);
	int advPlain = plate;                 /* a plain row occupies its plate */
	int advOut   = plate + over * 2;      /* outlined: plate + overhang both sides */

	/* stack: plain, outlined, outlined, plain -- starting at marginY */
	int y0 = m.marginY;                            /* plain row top          */
	int y1 = y0 + advPlain + m.vgap;               /* first outlined  outer  */
	int y2 = y1 + advOut   + m.vgap;               /* second outlined outer  */
	int y3 = y2 + advOut   + m.vgap;               /* trailing plain         */

	int plate1Top = y1 + over, plate1Bot = plate1Top + plate - 1;
	int plate2Top = y2 + over, plate2Bot = plate2Top + plate - 1;

	/* the borders' outer edges */
	int b1Bot = plate1Bot + over;   /* lowest pixel of row 1's border */
	int b2Top = plate2Top - over;   /* highest pixel of row 2's border */

	CHECK(b2Top > b1Bot, "two outlined borders never touch");
	CHECK(b2Top - b1Bot - 1 == m.vgap,
		  "the visible gap between two outlined rows is exactly vgap");

	/* and that gap equals GoldClient's both-outlined branch, gap + o*2, measured
	 * plate-edge to plate-edge rather than border-edge to border-edge */
	CHECK(plate2Top - plate1Bot - 1 == m.vgap + over * 2,
		  "plate-to-plate spacing matches GoldClient's gap + o*2 form");

	/* a plain row after two outlined ones keeps the plain gap */
	CHECK(y3 - (plate2Bot + over) - 1 == m.vgap,
		  "the plain row below two outlined rows keeps a vgap gap");

	/* no outlined row may eat into its neighbour at ANY scale, including the
	 * back-to-back case where two overhangs share one gap */
	int h;
	for( h = 240; h <= 4320; h += 60 )
	{
		kf_metrics k;
		kf_compute_metrics(h, 1.0f, 27, &k);
		int kt = kf_border_thickness(k.outline, k.vgap);
		int ko = kf_border_overhang(kt);
		/* two overhangs plus the gap must still leave the gap positive */
		CHECK(k.vgap >= 1, "vgap never collapses to 0");
		CHECK(ko <= k.vgap, "one overhang fits in the gap");
		/* the shared-gap case: borders must not meet even when both rows have one */
		CHECK(k.vgap + ko * 2 > ko * 2 - 1,
			  "back-to-back outlined rows keep a positive visible gap");
	}
}

static void test_sanitise_name(void)
{
	/* Player names come from the server. DrawHudString interprets "\R" as
	 * "jump the cursor to iMaxX - 10 charwidths", and the killfeed draws with
	 * iMaxX 0, so an unsanitised "\R" would move the cursor to a negative x and
	 * throw the rest of the row off the left edge. HudStringLen SKIPS the same
	 * escapes when measuring, so the row would also be measured at a width it is
	 * not drawn at. */
	char out[32];

	kf_sanitise_name("Gunner", out, sizeof(out));
	CHECK(!strcmp(out, "Gunner"), "plain name passes through");

	kf_sanitise_name("a\\Rb", out, sizeof(out));
	CHECK(!strcmp(out, "ab"), "\\R is stripped");

	kf_sanitise_name("\\yhi\\w!\\d", out, sizeof(out));
	CHECK(!strcmp(out, "hi!"), "colour escapes are stripped");

	kf_sanitise_name("x^3y", out, sizeof(out));
	CHECK(!strcmp(out, "x^3y"), "^N colour code is KEPT (engine parses it)");

	kf_sanitise_name("one\ntwo", out, sizeof(out));
	CHECK(!strcmp(out, "one"), "name stops at a newline");

	/* a lone backslash is NOT an escape -- keep it, or names legitimately
	 * containing one would silently lose a character */
	kf_sanitise_name("a\\b", out, sizeof(out));
	CHECK(!strcmp(out, "a\\b"), "a non-escape backslash survives");

	/* '^' not followed by a digit is an ordinary character */
	kf_sanitise_name("up^", out, sizeof(out));
	CHECK(!strcmp(out, "up^"), "trailing ^ survives");

	/* ^N must survive intact: the ENGINE parses colour codes (disassembled:
	 * '^' is compared 12x in Con_DrawString's worker), and whether they recolour
	 * is the user's hud_colored setting, shared with chat and the scoreboard.
	 * Stripping them here would override that setting for the killfeed only. */
	kf_sanitise_name("^1red^7white", out, sizeof(out));
	CHECK(!strcmp(out, "^1red^7white"), "all ^N codes survive untouched");

	/* truncation must still terminate */
	{
		char small[4];
		kf_sanitise_name("abcdefgh", small, sizeof(small));
		CHECK(strlen(small) == 3, "output is truncated to fit");
		CHECK(small[3] == 0, "output stays terminated");
	}

	/* degenerate inputs must not write anywhere */
	kf_sanitise_name(NULL, out, sizeof(out));
	CHECK(out[0] == 0, "NULL source yields an empty string");
}

static void test_row_pitch_is_constant(void)
{
	/* The pitch for a PLAIN row is rowHeight + vgap.
	 * MEASURED on the reference: 36px on all five steps. An outlined row is
	 * handled by test_outline_keeps_gaps_uniform: it occupies more space
	 * (rowH + 2*overhang) but the visible GAP stays vgap, which is what the
	 * reference shows and what the eye reads as "even spacing".
	 * (An earlier kf_row_pitch() added (thickness-1) per outlined neighbour,
	 * which would have made that step 38px. It was removed.) */
	kf_metrics m;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	int plate = m.textH + m.pady * 2;
	CHECK(plate == 53, "plate is 53px at the reference (GC text/plate ratio)");
	CHECK(plate + m.vgap == 56, "pitch is 56px at the reference");

	/* Plate positions from the native 1920x1080 frame: first plate y21
	 * (gapfit1080.py). The MEASURED per-row grid 21/57/93/129/165 belonged to
	 * pady=3 and is gone -- with pady 13 the pitch is 56, so the pinned grid
	 * is 21/77/133/189/245. What stays pinned is the START (marginY 21) and
	 * the UNIFORM STEP, which is what the eye reads as "even spacing". */
	{
		const int refTops[5] = { 21, 77, 133, 189, 245 };
		int k;
		CHECK(m.marginY == refTops[0], "the feed starts at y21 (measured)");
		for( k = 0; k < 5; k++ )
			CHECK(m.marginY + k * (plate + m.vgap) == refTops[k],
				  "plain plate tops land on the pady-13 grid");
		/* the outlined row's OUTER edge continues the same grid */
		CHECK(m.marginY + 5 * (plate + m.vgap) == 301,
			  "the outlined row's outer edge is at y301 (pady-13 grid)");
		/* and its outer extent is 53 + 2*overhang(2) = 57 */
		CHECK(plate + kf_border_overhang(
				  kf_border_thickness(m.outline, m.vgap)) * 2 == 57,
			  "the outlined row occupies 57px (pady-13 plate height)");
	}

	/* An outlined row must not change the pitch: the border straddles the plate
	 * edge instead of growing it, so the caller advances by the same amount. */
	CHECK(m.outline == 3, "outline thickness is 3 at the reference");
}

static void test_row_alpha(void)
{
	/* Alive rows are fully opaque -- there is no fade-in in GoldClient. */
	CHECK(fabsf(kf_row_alpha(-1.f, 220.f) - 1.0f) < 1e-6f, "alive row opaque");
	CHECK(fabsf(kf_row_alpha(0.f, 220.f) - 1.0f) < 1e-6f, "just-expired opaque");

	/* Expiring rows fade linearly to zero and never go negative. */
	CHECK(fabsf(kf_row_alpha(110.f, 220.f) - 0.5f) < 1e-6f, "half fade = 0.5");
	CHECK(fabsf(kf_row_alpha(220.f, 220.f)) < 1e-6f, "full fade = 0");
	CHECK(fabsf(kf_row_alpha(9999.f, 220.f)) < 1e-6f, "over-fade clamps at 0");
	CHECK(fabsf(kf_row_alpha(5.f, 0.f)) < 1e-6f, "zero exit time -> 0, no div0");
}

static void test_icon_box_is_capped_by_the_text_cell(void)
{
	/* MEASURED: all six reference plates are 33px = textH 27 + 2*pady 3, and the
	 * row height is max(textH, tallestIcon) + 2*pady. A 64px texture at the
	 * shared scale is 38px uncapped, which would have made its row's plate 44px.
	 * It did not, so the icon box is capped at the text cell. */
	kf_metrics m;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	CHECK(kf_icon_height(64, m.scale, 0) == 38,
		  "uncapped, a 64px texture would be 38px");
	CHECK(kf_icon_height(64, m.scale, m.textH) == m.textH,
		  "capped, it stops at the text cell");
	CHECK(kf_row_height(m.textH, kf_icon_height(64, m.scale, m.textH))
		  + m.pady * 2 == 53,
		  "so the plate stays 53px (pady 13, GC proportion)");

	/* the cap must not touch icons that already fit */
	CHECK(kf_icon_height(32, m.scale, m.textH) == 19, "32px texture unaffected");
	CHECK(kf_icon_height(12, m.scale, m.textH) == 7,  "12px '+' unaffected");
}

int main(void)
{
	test_decode_basic();
	test_mid_order();
	test_domination_and_revenge();
	test_headshot_not_doubled();
	test_full_awp_row();
	test_scale_floor_and_ceiling();
	test_scale_is_one_at_reference();
	test_icon_box_is_capped_by_the_text_cell();
	test_everything_scales_together();
	test_ratios_are_resolution_invariant();
	test_font_floor_takes_over();
	test_font_fallback_keeps_proportions();
	test_user_scale_works_on_the_fallback_path();
	test_icon_width_aspect();
	test_row_height();
	test_plain_rows_advance_equally();
	test_sanitise_name();
	test_row_pitch_is_constant();
	test_outline_keeps_gaps_uniform();
	test_two_outlined_rows_in_a_row();
	test_row_alpha();
	if(fails == 0) printf("ALL KILLFEED LOGIC TESTS PASSED\n");
	else printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
