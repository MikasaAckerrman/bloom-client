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
	/* all mid flags set -> fixed order noscope, smoke, penetrate, headshot */
	int rarity = KF_RARITY_NOSCOPE | KF_RARITY_THRUSMOKE |
				 KF_RARITY_PENETRATED | KF_RARITY_HEADSHOT;
	kf_decode_modifiers(rarity, 0, &m);
	CHECK(m.nMid == 4, "four mid icons");
	CHECK(m.mid[0] == KFI_NOSCOPE, "mid[0]=noscope");
	CHECK(m.mid[1] == KFI_SMOKE, "mid[1]=smoke");
	CHECK(m.mid[2] == KFI_PENETRATE, "mid[2]=penetrate");
	CHECK(m.mid[3] == KFI_HEADSHOT, "mid[3]=headshot");
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

static void test_anim_enter(void)
{
	float a, dx;
	/* t=0: fully transparent, fully slid right */
	kf_anim_state(0.f, -1.f, 180.f, 220.f, 60.f, &a, &dx);
	CHECK(a < 0.01f, "enter t0 alpha ~0");
	CHECK(dx > 59.f, "enter t0 dx ~slidePx");

	/* mid enter */
	kf_anim_state(90.f, -1.f, 180.f, 220.f, 60.f, &a, &dx);
	CHECK(a > 0.4f && a < 1.0f, "enter mid alpha rising");
	CHECK(dx > 0.f && dx < 60.f, "enter mid dx shrinking");

	/* after enter: settled */
	kf_anim_state(200.f, -1.f, 180.f, 220.f, 60.f, &a, &dx);
	CHECK(fabsf(a - 1.0f) < 0.001f, "post-enter alpha 1");
	CHECK(fabsf(dx) < 0.001f, "post-enter dx 0");
}

static void test_anim_exit(void)
{
	float a, dx;
	/* just died: still ~opaque */
	kf_anim_state(5000.f, 1.f, 180.f, 220.f, 60.f, &a, &dx);
	CHECK(a > 0.98f, "exit start alpha ~1");
	/* fully expired: transparent */
	kf_anim_state(5000.f, 220.f, 180.f, 220.f, 60.f, &a, &dx);
	CHECK(a < 0.01f, "exit end alpha ~0");
	CHECK(dx > 0.f, "exit dx drifts right");
}

int main(void)
{
	test_decode_basic();
	test_mid_order();
	test_headshot_not_doubled();
	test_full_awp_row();
	test_anim_enter();
	test_anim_exit();
	if(fails == 0) printf("ALL KILLFEED LOGIC TESTS PASSED\n");
	else printf("%d FAILURES\n", fails);
	return fails ? 1 : 0;
}
