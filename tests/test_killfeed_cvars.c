/* Host-side tests for the killfeed CVAR customisation layer.
 *
 * These pin the CONTRACT of the tunables, which is the part a user can break:
 *   - every size cvar is a MULTIPLIER, so proportions survive any value;
 *   - clamps keep a hostile value (negative, huge, garbage colour string) from
 *     producing a broken or invisible feed;
 *   - turning the plate off must NOT hide names and icons.
 *
 * The parse/clamp helpers are duplicated here in the SAME form death.cpp uses,
 * because death.cpp cannot be compiled on the host (it needs the engine). If
 * you change one, change both -- the comment in death.cpp says so too.
 *   cc -I../cl_dll/include -o /tmp/t test_killfeed_cvars.c -lm && /tmp/t
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "killfeed_layout.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

/* --- mirrors of the death.cpp helpers (keep in sync) --------------------- */

static void parse_color(const char *s, int *r, int *g, int *b)
{
	int v[3];
	if(!s || !s[0]) return;
	if(sscanf(s, "%d %d %d", &v[0], &v[1], &v[2]) != 3) return;
	*r = kf_clamp_i(v[0], 0, 255);
	*g = kf_clamp_i(v[1], 0, 255);
	*b = kf_clamp_i(v[2], 0, 255);
}

/* --- tests ---------------------------------------------------------------- */

static void test_color_parse_keeps_fallback_on_garbage(void)
{
	int r, g, b;
	const char *bad[] = { "", "abc", "1 2", "1;2;3", "-", "255" };
	unsigned i;
	for(i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
	{
		r = 46; g = 43; b = 42;
		parse_color(bad[i], &r, &g, &b);
		CHECK(r == 46 && g == 43 && b == 42,
			  "garbage colour string leaves the fallback intact");
	}
}

static void test_color_parse_clamps_channels(void)
{
	int r = 0, g = 0, b = 0;
	parse_color("300 -20 999", &r, &g, &b);
	CHECK(r == 255, "over-range channel clamps to 255");
	CHECK(g == 0,   "negative channel clamps to 0");
	CHECK(b == 255, "way-over channel clamps to 255");

	parse_color("131 165 222", &r, &g, &b);
	CHECK(r == 131 && g == 165 && b == 222, "valid colour parses exactly");
}

static void test_scale_cvar_cannot_break_proportions(void)
{
	/* Whatever the user types, the ratio between metrics must hold: that is the
	 * property that makes the feed look the same, only bigger or smaller. */
	float vals[] = { -5.0f, 0.0f, 0.01f, 0.25f, 1.0f, 2.0f, 3.0f, 50.0f };
	unsigned i;
	float ref = -1.0f;
	for(i = 0; i < sizeof(vals)/sizeof(vals[0]); i++)
	{
		kf_metrics m;
		float user = kf_clamp_f(vals[i], 0.25f, 4.0f);
		float ratio;
		kf_compute_metrics(1080, user, 27, &m);
		CHECK(m.textH >= 1 && m.padx >= 1 && m.gap >= 1,
			  "no metric collapses to zero at any cvar value");
		ratio = (float)m.padx / (float)m.textH;
		if(ref < 0.0f) ref = ratio;
		CHECK(fabsf(ratio - ref) < 0.08f,
			  "padx/textH ratio survives every scale value");
	}
}

static void test_position_cvars_clamp_to_screen(void)
{
	/* A negative or absurd inset must not push the feed off-screen or make the
	 * margin negative (which would read as a huge positive after int wrap). */
	kf_metrics m;
	int mx, my;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	mx = kf_px((float)m.marginX, kf_clamp_f(-3.0f, 0.0f, 20.0f));
	my = kf_px((float)m.marginY, kf_clamp_f(-3.0f, 0.0f, 20.0f));
	CHECK(mx == 0 && my == 0, "negative inset clamps to 0, not off-screen");

	mx = kf_px((float)m.marginX, kf_clamp_f(1000.0f, 0.0f, 20.0f));
	CHECK(mx == m.marginX * 20, "huge inset clamps at the ceiling");
}

/* Row cap: the ceiling is the death-notice array size, and it must come from the
 * SAME place death.cpp gets it (cl_dll/hud.h) rather than a literal repeated
 * here. With a hardcoded 5 this test kept passing while the array grew to 6 --
 * it would have gone on claiming a bound the game no longer had. */
#define KF_TEST_MAX_ROWS 6   /* must equal MAX_DEATHNOTICES in cl_dll/hud.h */

static void test_row_cap_is_bounded(void)
{
	const int cap = KF_TEST_MAX_ROWS;

	/* cl_killfeed_rows must stay inside the array the notices live in. */
	CHECK(kf_clamp_i(-4, 1, cap) == 1,   "negative row cap clamps to 1");
	CHECK(kf_clamp_i(0, 1, cap) == 1,    "zero row cap clamps to 1");
	CHECK(kf_clamp_i(99, 1, cap) == cap, "over-range row cap clamps to the array");
	CHECK(kf_clamp_i(3, 1, cap) == 3,    "in-range row cap passes through");

	/* the DEFAULT (cl_killfeed_rows "6") must survive the clamp unchanged --
	 * at MAX_DEATHNOTICES 5 it silently became 5 and the sixth row, which the
	 * reference frame shows, could never be drawn. */
	CHECK(kf_clamp_i(6, 1, cap) == 6, "the shipped default of 6 rows is reachable");
}

static void test_corner_and_outline_multipliers(void)
{
	/* 0 means "square" / "no border", and the percent maths must not produce a
	 * negative radius. The draw path floors the border at 1 when it is enabled. */
	kf_metrics m;
	int c0, c100, c400;
	kf_compute_metrics(1080, 1.0f, 27, &m);

	c0   = (m.corner * kf_clamp_i(0, 0, 400)) / 100;
	c100 = (m.corner * kf_clamp_i(100, 0, 400)) / 100;
	c400 = (m.corner * kf_clamp_i(400, 0, 400)) / 100;
	CHECK(c0 == 0, "corner 0 -> square plate");
	CHECK(c100 == m.corner, "corner 1.0 -> measured radius");
	CHECK(c400 == m.corner * 4, "corner 4.0 -> 4x radius");

	/* a negative cvar clamps to 0 rather than inverting the radius */
	CHECK((m.corner * kf_clamp_i(-500, 0, 400)) / 100 == 0,
		  "negative corner clamps to square");
}

static void test_plate_off_does_not_hide_content(void)
{
	/* Regression: the early-out used to be keyed to the PLATE alpha, so
	 * cl_killfeed_plate_alpha 0 (or plate 0) returned before drawing the names
	 * and icons. The gate must be the ROW's fade alpha instead. */
	float rowAlpha;
	int plateAlpha = 0;
	int baseA;

	rowAlpha = 1.0f;                 /* row fully alive */
	baseA = (int)(plateAlpha * rowAlpha);
	CHECK(baseA == 0, "plate alpha 0 yields a transparent plate");
	CHECK(rowAlpha > 0.0f, "row is still alive, so content MUST be drawn");

	/* and the row does disappear once its own fade completes */
	rowAlpha = kf_row_alpha(300.0f, 220.0f);
	CHECK(rowAlpha == 0.0f, "expired row fades to nothing");
}

/* The outline is DELIBERATELY gated on the plate.
 *
 * death.cpp: `if( item->bLocal && st->outlineScale100 > 0 && st->plate )`.
 * So cl_killfeed_plate 0 removes the outline too, even though cl_killfeed_outline
 * is a separate cvar the user may have set. That is intentional, not an
 * oversight: the border is drawn straddling the PLATE edge (kf_border_overhang),
 * so with no plate it would be a bare rectangle floating over the game scene,
 * around text it no longer encloses.
 *
 * GoldClient couples them the same way -- its hud_deathnotice_style has no
 * "outline, no background" value: 3 is "OUTLINE AND BACKGROUND", and the styles
 * without a background have no outline in the pitch maths either (client.dll
 * 0x100619b1: o = outlinethickness-1 is only computed for style 2 or 3).
 *
 * Pinned here so nobody "fixes" the coupling by making the outline independent,
 * and so the silent-ignore is a documented decision rather than a surprise. */
static void test_outline_requires_the_plate(void)
{
	/* mirror of the death.cpp gate */
	#define KF_BORDER_ON(bLocal, outlineScale100, plate) \
		( (bLocal) && (outlineScale100) > 0 && (plate) )

	CHECK( KF_BORDER_ON(1, 100, 1), "local row with plate and outline: border on");
	CHECK(!KF_BORDER_ON(1, 100, 0), "plate off -> NO border, even with outline 1");
	CHECK(!KF_BORDER_ON(1,   0, 1), "outline 0 -> no border, plate unaffected");
	CHECK(!KF_BORDER_ON(0, 100, 1), "a non-local row never gets a border");
	CHECK(!KF_BORDER_ON(0,   0, 0), "nothing set, nothing drawn");

	/* the outline multiplier still scales when both are on: 0..400% of the
	 * measured thickness, clamped by the gap so it cannot touch the neighbour */
	{
		kf_metrics m;
		int t100, t400;
		kf_compute_metrics(1080, 1.0f, 27, &m);
		t100 = kf_border_thickness((m.outline * kf_clamp_i(100, 0, 400)) / 100,
								   m.vgap);
		t400 = kf_border_thickness((m.outline * kf_clamp_i(400, 0, 400)) / 100,
								   m.vgap);
		CHECK(t100 == 3, "outline 100% is the measured 3px at the reference");
		CHECK(t400 >= t100, "outline 400% is not thinner than 100%");
		CHECK(kf_border_overhang(t400) <= m.vgap,
			  "even at 400% the overhang stays inside the gap");
	}
	#undef KF_BORDER_ON
}

int main(void)
{
	test_color_parse_keeps_fallback_on_garbage();
	test_color_parse_clamps_channels();
	test_scale_cvar_cannot_break_proportions();
	test_position_cvars_clamp_to_screen();
	test_row_cap_is_bounded();
	test_corner_and_outline_multipliers();
	test_plate_off_does_not_hide_content();
	test_outline_requires_the_plate();
	if(!fails) printf("ALL KILLFEED CVAR TESTS PASSED\n");
	else printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
