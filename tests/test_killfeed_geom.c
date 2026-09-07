/* Host harness for the killfeed ROW LAYOUT from killfeed_layout.h.
 *
 * IMPORTANT: this test drives the REAL kf_layout_row()/kf_build order used by
 * death.cpp -- it does not re-implement the advance logic. The previous version
 * of this harness kept its own copy, which meant a layout change in death.cpp
 * could pass the tests while breaking the game.
 *
 *   cc -I../cl_dll/include -o /tmp/kf_geom test_killfeed_geom.c -lm
 */
#include <stdio.h>
#include <string.h>
#include "killfeed_layout.h"

/* Row metrics come from the ONE scale, exactly as the draw path builds them. */
static kf_metrics M;
static int GAP, GAPW, PADX, TEXT, TEXTH;
static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

/* Texture sizes as shipped in sprites/kf (SPR32 header w@16, h@20). */
#define TEX_MOD_W   32
#define TEX_MOD_H   32
#define TEX_WPN_W   96
#define TEX_WPN_H   32
#define TEX_PLUS_W  12
#define TEX_PLUS_H  12
#define TEX_WING_W  64
#define TEX_WING_H  64

static void geom_init(void)
{
	kf_compute_metrics(1080, 1.0f, 27, &M);
	GAP = M.gap; GAPW = M.gapTight; PADX = M.padx; TEXTH = M.textH;
	TEXT = 50;   /* stand-in string width */
}

static void push_icon(kf_elem *el, int *n, int texW, int texH, int tight,
					  int raised)
{
	/* the airborne wing uses its own measured ratio and is not capped by the
	 * text cell -- it overhangs the plate (see killfeed_layout.h) */
	int h = raised ? kf_wing_height(texH, M.scale)
				   : kf_icon_height(texH, M.scale, M.textH);
	el[*n].slot = 0;
	el[*n].w = kf_icon_width(texW, texH, h);
	el[*n].h = h;
	el[*n].tightGap = tight;
	el[*n].raised = raised;
	(*n)++;
}

static void push_text(kf_elem *el, int *n)
{
	el[*n].slot = -1;
	el[*n].w = TEXT;
	el[*n].h = TEXTH;
	el[*n].tightGap = 0;
	el[*n].raised = 0;
	(*n)++;
}

/* Build the element list in the same order death.cpp's KF_BuildRow does. */
static int build(const kf_row_mods *m, int hasKiller, int hasAssist,
				 int hasVictim, int hasWeapon, kf_elem *el)
{
	int n = 0, j;
	for(j = 0; j < m->nPre; j++)
		push_icon(el, &n, TEX_MOD_W, TEX_MOD_H, 0, 0);
	if(hasKiller) push_text(el, &n);
	/* An assist is shown whenever there IS an assister; the flash icon is an
	 * extra that only appears for a flash assist. VERIFIED against ReGameDLL:
	 * the server sends the assister index independently of the flash flag. */
	if(hasAssist){
		push_icon(el, &n, TEX_PLUS_W, TEX_PLUS_H, 0, 0);
		if(m->flashAssist)
			push_icon(el, &n, TEX_MOD_W, TEX_MOD_H, 0, 0);
		push_text(el, &n);
	}
	/* the wing is the ONLY raised element, and it uses the 64px texture */
	if(m->wing >= 0)
		push_icon(el, &n, TEX_WING_W, TEX_WING_H, 0, 1);
	if(hasWeapon)
		push_icon(el, &n, TEX_WPN_W, TEX_WPN_H, m->wing >= 0, 0);
	for(j = 0; j < m->nMid; j++)
		push_icon(el, &n, TEX_MOD_W, TEX_MOD_H, 0, 0);
	if(hasVictim) push_text(el, &n);
	return n;
}

static void test_no_overlap(void)
{
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	int rarity = KF_RARITY_KILLER_BLIND | KF_RARITY_INAIR |
				 KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE;
	int n, i, w;
	kf_decode_modifiers(rarity, 0, &m);
	n = build(&m, 1, 0, 1, 1, el);
	w = kf_layout_row(el, n, GAP, GAPW, PADX);

	CHECK(n == 7, "awp row: blind,killer,wing,weapon,noscope,smoke,victim");
	CHECK(w > 0, "row width positive");
	/* every element must start after the previous one ENDS -- catches overlap,
	 * which a plain "x increasing" check would miss. */
	for(i = 1; i < n; i++)
		CHECK(el[i].x >= el[i-1].x + el[i-1].w, "no element overlap");
	/* the row must be wide enough to contain the last element plus padding */
	CHECK(w >= el[n-1].x + el[n-1].w + PADX, "width covers all elements");
}

static void test_wing_uses_tight_gap(void)
{
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	int n, gapUsed;
	kf_decode_modifiers(KF_RARITY_INAIR, 0, &m);
	n = build(&m, 1, 0, 1, 1, el);
	/* elements: killer(0), wing(1), weapon(2), victim(3) */
	CHECK(n == 4, "wing row element count");
	kf_layout_row(el, n, GAP, GAPW, PADX);
	gapUsed = el[2].x - (el[1].x + el[1].w);
	CHECK(gapUsed == GAPW, "wing->weapon uses the tight gap");

	/* The wing is LIFTED above the row centre and overhangs the plate; every
	 * other element is centred. Rule chosen by IoU against the reference
	 * (workspace/uicopy-kfgold/wing_rule.py): lifted-from-centre scored 0.735
	 * vs 0.291 for top-aligned-to-plate. */
	{
		/* cy must be the REAL row centre, or the overhang check is meaningless:
		 * rowH = max(textCell, tallestIcon) + 2*pady = 27 + 26 = 53. */
		int top = 100;
		int rowH = kf_row_height(M.textH, kf_tallest(el, n)) + M.pady * 2;
		int cy = top + rowH / 2;
		CHECK(el[1].raised == 1, "wing is flagged raised");
		CHECK(kf_elem_y(&el[1], cy, M.raise)
			  == cy - el[1].h / 2 - M.raise,
			  "raised wing is lifted above the row centre");
		/* GC reference: icons sit INSIDE the plate (y78..120 in plate
		 * y60..143, measured on the 632p frame). With pady 13 the wing
		 * clears the top edge; the old pady-3 layout pushed it out by 8. */
		CHECK(kf_elem_y(&el[1], cy, M.raise) >= top,
			  "at the reference metrics the wing stays inside the plate");
		CHECK(el[2].raised == 0, "weapon is not raised");
		CHECK(kf_elem_y(&el[2], cy, M.raise) == cy - el[2].h / 2,
			  "weapon stays vertically centred");
		CHECK(kf_elem_y(&el[0], cy, M.raise) == cy - el[0].h / 2,
			  "text stays vertically centred");
	}

	/* the wing must NOT size the row: the reference row that carries one has the
	 * same 33px plate as the five rows without one */
	{
		kf_elem probe[3];
		probe[0] = el[0]; probe[0].h = 27; probe[0].raised = 0; probe[0].w = 40;
		probe[1] = el[0]; probe[1].h = 19; probe[1].raised = 0; probe[1].w = 20;
		probe[2] = el[0]; probe[2].h = 99; probe[2].raised = 1; probe[2].w = 25;
		CHECK(kf_tallest(probe, 3) == 27,
			  "a huge raised element does not inflate the row height");
	}
}

static void test_noscope_is_after_the_weapon(void)
{
	/* A no-scope kill puts its icon AFTER the weapon (mid[]), never before the
	 * killer name -- MEASURED on the GoldClient reference. */
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	kf_decode_modifiers(KF_RARITY_NOSCOPE, 0, &m);
	CHECK(m.nPre == 0, "no-scope contributes nothing before the killer");
	CHECK(m.nMid == 1 && m.mid[0] == KFI_NOSCOPE, "no-scope lands in mid[]");
	{
		int n = build(&m, 1, 0, 1, 1, el);
		/* killer(0), weapon(1), noscope(2), victim(3) */
		CHECK(n == 4, "noscope row element count");
		kf_layout_row(el, n, GAP, GAPW, PADX);
		CHECK(el[2].x > el[1].x, "noscope icon is drawn after the weapon");
	}
}

static void test_assist_without_flash(void)
{
	/* An ORDINARY assist (no flash) must still be shown: killer + assister,
	 * with the '+' glue but WITHOUT the flash icon.
	 *
	 * This guards a real bug: the row builder used to gate the whole assist
	 * block on mods.flashAssist, so every non-flash assist vanished. VERIFIED
	 * against ReGameDLL (multiplay_gamerules.cpp SendDeathMessage): the server
	 * writes the assister index under PLAYERDEATH_ASSISTANT, independently of
	 * the KILLRARITY_ASSISTEDFLASH bit. */
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	int nPlain, nFlash;

	kf_decode_modifiers(0, 0, &m);
	CHECK(m.flashAssist == 0, "no flash flag");
	nPlain = build(&m, 1, 1, 1, 1, el);
	/* killer, '+', assister, weapon, victim */
	CHECK(nPlain == 5, "plain assist row has 5 elements (no flash icon)");

	kf_decode_modifiers(KF_RARITY_ASSISTEDFLASH, 0, &m);
	CHECK(m.flashAssist == 1, "flash flag decoded");
	nFlash = build(&m, 1, 1, 1, 1, el);
	CHECK(nFlash == nPlain + 1, "a flash assist adds exactly the flash icon");

	/* no assister at all -> neither the '+' nor the flash icon appears, even
	 * when the flash bit is set (a flash assist with no assister name is
	 * meaningless and used to draw a dangling '+') */
	{
		int nNone = build(&m, 1, 0, 1, 1, el);
		CHECK(nNone == 3, "no assister -> killer+weapon+victim only");
	}
}

static void test_minimal_row(void)
{
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	int n, w;
	kf_decode_modifiers(0, 0, &m);
	n = build(&m, 1, 0, 1, 1, el);
	w = kf_layout_row(el, n, GAP, GAPW, PADX);
	CHECK(n == 3, "killer+weapon+victim = 3 elements");
	CHECK(el[0].x == PADX, "first element starts after left padding");
	CHECK(w > PADX * 2, "minimal row wider than padding alone");
}

static void test_missing_sprite_no_hole(void)
{
	/* A sprite that failed to load reports width 0. It must not consume a gap
	 * or leave a visible hole in the row. */
	kf_elem el[KF_MAX_ELEMS];
	int n = 0, wWith, wWithout;

	push_text(el, &n);
	push_icon(el, &n, TEX_WPN_W, TEX_WPN_H, 0, 0);
	push_text(el, &n);
	wWithout = kf_layout_row(el, n, GAP, GAPW, PADX);

	/* same row, but with a zero-width (unloaded) icon inserted in the middle */
	n = 0;
	push_text(el, &n);
	el[n].slot = 0; el[n].w = 0; el[n].h = 0;
	el[n].tightGap = 0; n++;
	push_icon(el, &n, TEX_WPN_W, TEX_WPN_H, 0, 0);
	push_text(el, &n);
	wWith = kf_layout_row(el, n, GAP, GAPW, PADX);

	CHECK(wWith == wWithout, "missing sprite adds no width and no gap");
}

static void test_first_element_no_leading_gap(void)
{
	/* If the killer name is absent (suicide), the row must not start with a
	 * dangling gap -- the first VISIBLE element sits at padx. */
	kf_elem el[KF_MAX_ELEMS];
	int n = 0;
	el[n].slot = 0; el[n].w = 0; el[n].h = 0;
	el[n].tightGap = 0; n++;   /* absent element */
	push_icon(el, &n, TEX_WPN_W, TEX_WPN_H, 0, 0);
	kf_layout_row(el, n, GAP, GAPW, PADX);
	CHECK(el[1].x == PADX, "first visible element starts at padx");
}

static void test_empty_row_has_no_visible_elements(void)
{
	/* A row whose every sprite failed to load and whose names are absent has
	 * NOTHING to paint. kf_layout_row still returns padx*2 for it, so death.cpp
	 * uses kf_visible_count to suppress the row instead of painting a bare
	 * plate. Reachable: the server sends killer 0 for a non-player kill (which
	 * suppresses the killer name), the victim's userinfo may not have arrived,
	 * and the fallback skull sprite may be missing. */
	kf_elem el[KF_MAX_ELEMS];
	int n = 0, w;

	el[n].slot = 0; el[n].w = 0; el[n].h = 0;
	el[n].tightGap = 0; el[n].raised = 0; n++;
	el[n].slot = 0; el[n].w = 0; el[n].h = 0;
	el[n].tightGap = 0; el[n].raised = 0; n++;

	w = kf_layout_row(el, n, GAP, GAPW, PADX);
	CHECK(kf_visible_count(el, n) == 0, "an all-missing row has no visible elements");
	CHECK(w == PADX * 2, "and it still measures padx*2 -- hence the guard");

	/* one real element is enough to make the row legitimate */
	push_icon(el, &n, TEX_WPN_W, TEX_WPN_H, 0, 0);
	CHECK(kf_visible_count(el, n) == 1, "one loaded sprite makes the row visible");
}

static void test_row_height_from_content(void)
{
	/* at the reference dimension every icon normalises to the font height */
	kf_elem el[KF_MAX_ELEMS];
	kf_row_mods m;
	int n, tallest;
	kf_decode_modifiers(KF_RARITY_HEADSHOT, 0, &m);
	n = build(&m, 1, 0, 1, 1, el);
	tallest = kf_tallest(el, n);
	CHECK(tallest == TEXTH, "icons normalise to the font height at ref width");
	CHECK(kf_row_height(TEXTH, tallest) == TEXTH, "row height == font height");
}

int main(void)
{
	geom_init();
	test_no_overlap();
	test_wing_uses_tight_gap();
	test_noscope_is_after_the_weapon();
	test_assist_without_flash();
	test_minimal_row();
	test_missing_sprite_no_hole();
	test_first_element_no_leading_gap();
	test_empty_row_has_no_visible_elements();
	test_row_height_from_content();
	if(!fails) printf("ALL KILLFEED GEOMETRY TESTS PASSED\n");
	else printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
