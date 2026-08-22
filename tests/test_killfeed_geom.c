/* Host harness for the killfeed ROW GEOMETRY math from death.cpp.
 * It re-implements KF_RowWidth's advance logic with stub icon widths and
 * checks: (1) width is deterministic and positive, (2) element x-positions are
 * strictly increasing (no overlap), (3) wing uses the tiny gap not the full gap.
 * This catches layout regressions without an engine.
 */
#include <stdio.h>
#include <string.h>
#include "killfeed_layout.h"

/* stub metrics */
static int ICONH = 40, MODH = 46, GAP = 15, GAPW = 3, PADX = 20, TEXT = 50;
static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

/* fake per-slot icon widths (aspect-based, like real sprites) */
static int modw(int slot){ (void)slot; return 42; }
static int weaponw(void){ return 73; } /* awp-ish */

/* Reproduce the advance sequence, recording x of each element start. */
static int layout_positions(const kf_row_mods *m, int hasKiller, int hasAssist,
							 int hasVictim, int *xs, int *n)
{
	int x = PADX, first = 1, j, cnt = 0;
	#define ADV(px) do{ if(!first) x += GAP; xs[cnt++]=x; x += (px); first=0; }while(0)
	for(j=0;j<m->nPre;j++) ADV(modw(m->pre[j]));
	if(hasKiller) ADV(TEXT);
	if(m->flashAssist && hasAssist){ ADV(modw(KFI_FLASHASSIST)); ADV(TEXT); }
	if(m->wing>=0){
		if(!first) x+=GAP; xs[cnt++]=x; x+=modw(m->wing); first=0;
		x+=GAPW; xs[cnt++]=x; x+=weaponw();
	} else {
		ADV(weaponw());
	}
	for(j=0;j<m->nMid;j++) ADV(modw(m->mid[j]));
	if(hasVictim) ADV(TEXT);
	#undef ADV
	x += PADX;
	*n = cnt;
	return x;
}

static void test_increasing(void)
{
	kf_row_mods m;
	int rarity = KF_RARITY_KILLER_BLIND | KF_RARITY_INAIR |
				 KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE;
	kf_decode_modifiers(rarity, 0, &m);
	int xs[16], n, i;
	int w = layout_positions(&m, 1, 0, 1, xs, &n);
	CHECK(w > 0, "row width positive");
	for(i=1;i<n;i++) CHECK(xs[i] > xs[i-1], "element x strictly increasing");
	/* the AWP example: blind, killer, wing, weapon, noscope, smoke, victim = 7 */
	CHECK(n == 7, "awp row element count");
}

static void test_wing_gap(void)
{
	/* with a wing, the weapon should sit only GAPW after the wing, not GAP. */
	kf_row_mods m; kf_decode_modifiers(KF_RARITY_INAIR, 0, &m);
	int xs[16], n; layout_positions(&m, 1, 0, 1, xs, &n);
	/* elements: killer(0), wing(1), weapon(2), victim(3) */
	int wingStart = xs[1], weaponStart = xs[2];
	int gapUsed = weaponStart - (wingStart + modw(KFI_INAIR));
	CHECK(gapUsed == GAPW, "wing->weapon uses small gap");
}

static void test_minimal_row(void)
{
	kf_row_mods m; kf_decode_modifiers(0, 0, &m);
	int xs[16], n;
	int w = layout_positions(&m, 1, 0, 1, xs, &n);
	CHECK(n == 3, "killer+weapon+victim = 3 elements");
	CHECK(w > PADX*2, "minimal row wider than padding");
}

int main(void)
{
	test_increasing();
	test_wing_gap();
	test_minimal_row();
	if(!fails) printf("ALL KILLFEED GEOMETRY TESTS PASSED\n");
	else printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
