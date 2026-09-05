/* Host test for the death-notice SLOT LIST invariants.
 *
 * The list is a fixed array with an extra tail slot that acts as a terminator.
 * Both the message handler (find a free slot / evict the oldest) and the draw
 * loop (stop at the first unused slot) depend on that. This harness reproduces
 * the real logic from death.cpp and pins it, because the bug it replaced was
 * silent: occupancy used to be "iId != 0", and iId is a HUD sprite index where 0
 * is a VALID value -- so a kill whose weapon sprite happened to be first in
 * hud.txt made its row invisible AND hid every row after it.
 *
 *   cc -o /tmp/t test_killfeed_slots.c && /tmp/t
 */
#include <stdio.h>
#include <string.h>

/* Must match cl_dll/hud.h. Kept as a private copy so this harness needs no
 * engine headers; scripts/kf_check_all.sh diffs the two values so they cannot
 * drift apart silently. */
#define MAX_DEATHNOTICES 6

typedef struct {
	int  iId;          /* legacy HUD sprite index; 0 is VALID */
	int  bUsed;        /* explicit occupancy */
	int  tag;          /* identifies the row in assertions */
	float flDisplayTime;
} item_t;

static item_t list[MAX_DEATHNOTICES + 1];
static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

static void init_hud_data(void)
{
	memset(list, 0, sizeof(list));
}

/* mirror of MsgFunc_DeathMsg's slot acquisition */
static int add_row(int tag, int spriteIndex, float displayTime)
{
	int i;
	for (i = 0; i < MAX_DEATHNOTICES; i++)
		if (!list[i].bUsed)
			break;
	if (i == MAX_DEATHNOTICES) {
		memmove(list, list + 1, sizeof(item_t) * MAX_DEATHNOTICES);
		i = MAX_DEATHNOTICES - 1;
	}
	memset(&list[i], 0, sizeof(item_t));
	list[i].iId = spriteIndex;
	list[i].tag = tag;
	list[i].flDisplayTime = displayTime;
	list[i].bUsed = 1;      /* claimed LAST, as in death.cpp */
	return i;
}

/* mirror of the Draw loop's expiry pass; returns how many rows it would draw.
 *
 * The `else` branch mirrors death.cpp's per-frame lifetime handling. It is here
 * so the CAP BUG below is pinned by a test rather than by a comment: death.cpp
 * used to re-clamp flDisplayTime to (now + DEATHNOTICE_DISPLAY_TIME) on EVERY
 * frame, which silently truncated any cl_killfeed_time above that constant. */
static int draw_pass_capped(float now, float cap)
{
	int i, drawn = 0;
	for (i = 0; i < MAX_DEATHNOTICES; i++) {
		if (!list[i].bUsed)
			break;
		if (list[i].flDisplayTime < now) {
			memmove(&list[i], &list[i+1],
					sizeof(item_t) * (MAX_DEATHNOTICES - i));
			i--;
			continue;
		} else if (cap > 0.0f) {
			/* the removed legacy line: min(flDisplayTime, now + cap) */
			if (list[i].flDisplayTime > now + cap)
				list[i].flDisplayTime = now + cap;
		}
		drawn++;
	}
	return drawn;
}

static int draw_pass(float now)
{
	return draw_pass_capped(now, 0.0f);
}

static void test_sprite_index_zero_is_a_real_row(void)
{
	init_hud_data();
	/* the weapon's d_* sprite is the FIRST entry in hud.txt -> index 0 */
	add_row(1, 0, 100.0f);
	add_row(2, 7, 100.0f);
	CHECK(draw_pass(50.0f) == 2, "a row with sprite index 0 is still drawn");
	CHECK(list[0].tag == 1, "and it keeps its place");
	CHECK(list[1].tag == 2, "and does not hide the row after it");
}

static void test_index_zero_slot_is_not_reused(void)
{
	init_hud_data();
	add_row(1, 0, 100.0f);      /* index 0 -- used to look "empty" */
	add_row(2, 3, 100.0f);
	CHECK(list[0].tag == 1, "the index-0 row was not overwritten");
	CHECK(list[1].tag == 2, "the second row took the next slot");
}

static void test_eviction_keeps_order(void)
{
	int k;
	init_hud_data();
	for (k = 1; k <= MAX_DEATHNOTICES; k++)
		add_row(k, k, 100.0f);
	CHECK(list[0].tag == 1, "oldest first before eviction");

	add_row(99, 9, 100.0f);     /* one too many -> evict the oldest */
	CHECK(list[0].tag == 2, "eviction drops the OLDEST row");
	CHECK(list[MAX_DEATHNOTICES-1].tag == 99, "new row lands last");
	CHECK(draw_pass(50.0f) == MAX_DEATHNOTICES, "list stays full");
}

static void test_expiry_compacts_without_holes(void)
{
	init_hud_data();
	add_row(1, 1, 10.0f);       /* expires first */
	add_row(2, 2, 100.0f);
	add_row(3, 3, 100.0f);

	CHECK(draw_pass(50.0f) == 2, "the expired row is dropped");
	CHECK(list[0].tag == 2, "survivors shift down");
	CHECK(list[1].tag == 3, "in order");
	CHECK(!list[2].bUsed, "and the vacated slot is free");
}

static void test_all_expired_empties_the_list(void)
{
	init_hud_data();
	add_row(1, 1, 10.0f);
	add_row(2, 2, 10.0f);
	CHECK(draw_pass(50.0f) == 0, "everything expired -> nothing drawn");
	CHECK(!list[0].bUsed, "list is empty afterwards");
	/* and it must still accept new rows */
	add_row(3, 3, 100.0f);
	CHECK(draw_pass(50.0f) == 1, "the list is reusable after emptying");
	CHECK(list[0].tag == 3, "new row went to slot 0");
}

static void test_tail_slot_stays_a_terminator(void)
{
	int k;
	init_hud_data();
	for (k = 1; k <= MAX_DEATHNOTICES; k++)
		add_row(k, k, 100.0f);
	/* the tail slot is never written by add_row */
	CHECK(!list[MAX_DEATHNOTICES].bUsed, "tail slot is never claimed");
	/* evict repeatedly; the tail must stay unused so the shifts terminate */
	for (k = 0; k < 20; k++)
		add_row(100 + k, k % 4, 100.0f);
	CHECK(!list[MAX_DEATHNOTICES].bUsed, "tail slot survives many evictions");
	CHECK(draw_pass(50.0f) == MAX_DEATHNOTICES, "list still bounded");
}

/* A user-chosen lifetime must survive the draw loop.
 *
 * death.cpp carried a legacy line that re-clamped every live row to
 * (now + DEATHNOTICE_DISPLAY_TIME = 6) on EVERY frame. Rows created with a
 * longer cl_killfeed_time were silently cut back to 6s: the cvar accepted the
 * value, the game ignored it. Values BELOW 6 were unaffected, which is why it
 * went unnoticed.
 *
 * The clamp added no protection -- flDisplayTime is set once from m_flTime on
 * arrival, and expired rows are already evicted by the branch above -- so it was
 * removed. These two cases are the regression guard. */
static void test_long_lifetime_is_not_capped(void)
{
	init_hud_data();
	/* cl_killfeed_time 10 at t=0 -> the row must live until t=10 */
	add_row(1, 1, 10.0f);

	/* WITH the legacy cap the row would be pulled back to 6.0 here */
	CHECK(draw_pass_capped(1.0f, 6.0f) == 1, "row alive at t=1 either way");
	CHECK(list[0].flDisplayTime <= 7.0f + 1e-6f,
		  "the legacy cap really did truncate (proves the test is not vacuous)");

	/* now the real, uncapped pass: the full 10s must be honoured */
	init_hud_data();
	add_row(1, 1, 10.0f);
	CHECK(draw_pass(1.0f) == 1, "row alive at t=1");
	CHECK(list[0].flDisplayTime == 10.0f, "lifetime kept intact at t=1");
	CHECK(draw_pass(7.0f) == 1, "STILL alive at t=7 -- past the old 6s cap");
	CHECK(list[0].flDisplayTime == 10.0f, "lifetime still intact at t=7");
	CHECK(draw_pass(9.9f) == 1, "alive just before expiry");
	CHECK(draw_pass(10.1f) == 0, "and gone once its own time is up");
}

/* Shorter-than-default lifetimes were never affected by the cap, but they must
 * keep working after its removal: nothing may extend a row's life either. */
static void test_short_lifetime_still_expires_on_time(void)
{
	init_hud_data();
	add_row(1, 1, 2.0f);        /* cl_killfeed_time 2 */
	CHECK(draw_pass(1.0f) == 1, "alive before its own expiry");
	CHECK(draw_pass(2.1f) == 0, "expires at its own time, not later");
}

int main(void)
{
	test_sprite_index_zero_is_a_real_row();
	test_index_zero_slot_is_not_reused();
	test_eviction_keeps_order();
	test_expiry_compacts_without_holes();
	test_all_expired_empties_the_list();
	test_tail_slot_stays_a_terminator();
	test_long_lifetime_is_not_capped();
	test_short_lifetime_still_expires_on_time();

	if (fails) {
		printf("%d KILLFEED SLOT TEST(S) FAILED\n", fails);
		return 1;
	}
	printf("ALL KILLFEED SLOT TESTS PASSED\n");
	return 0;
}
