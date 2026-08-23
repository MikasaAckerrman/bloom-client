/*
 * Host test for the animated health readout (cl_dll/health.cpp DrawHealthBar).
 *
 * The real function needs the whole HUD/engine, so the easing maths is
 * replicated here EXACTLY as written in health.cpp and driven through the same
 * scenarios the HUD would see. If the formula in health.cpp changes, these
 * numbers must be updated together -- the point is to pin the behaviour we
 * claim (framerate independence, snap, clamping, extended-value handling),
 * not to re-test C arithmetic.
 *
 * Build: cc -O2 -o /tmp/hp_test tests/test_health_anim.c -lm && /tmp/hp_test
 */
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fails = 0;

#define CHECK(cond, msg, ...) do { \
	if (!(cond)) { printf("FAIL: " msg "\n", ##__VA_ARGS__); fails++; } \
} while (0)

/* --- mirror of health.cpp: the easing step ------------------------------- */
typedef struct {
	float display;      /* m_flDisplayHealth */
	int   health;       /* m_iHealth or extended sb_health */
	int   enabled;      /* cl_health_transition */
	float speed;        /* cl_health_transition_speed */
} hp_t;

static void hp_step(hp_t *s, double dt)
{
	if (s->enabled) {
		float target = (float)s->health;
		float speed = s->speed;
		if (speed < 1.0f)   speed = 1.0f;
		if (speed > 100.0f) speed = 100.0f;

		float delta = target - s->display;
		if (fabs(delta) < 0.5f)
			s->display = target;
		else
			s->display += delta * (float)dt * speed;

		if (s->display < 0.0f) s->display = 0.0f;
	} else {
		s->display = (float)s->health;
	}
}

static int hp_shown(const hp_t *s) { return (int)(s->display + 0.5f); }

/* run until settled or the cap is hit; returns elapsed seconds */
static double settle(hp_t *s, double dt, double max_seconds)
{
	double t = 0.0;
	while (t < max_seconds) {
		hp_step(s, dt);
		t += dt;
		if (s->display == (float)s->health)
			break;
	}
	return t;
}

int main(void)
{
	/* 1. disabled -> instant, no easing at all */
	{
		hp_t s = { 100.0f, 40, 0, 10.0f };
		hp_step(&s, 1.0 / 60.0);
		CHECK(hp_shown(&s) == 40, "disabled should snap: got %d", hp_shown(&s));
	}

	/* 2. enabled -> does NOT jump in one frame, but moves towards target */
	{
		hp_t s = { 100.0f, 40, 1, 10.0f };
		hp_step(&s, 1.0 / 60.0);
		int v = hp_shown(&s);
		CHECK(v < 100 && v > 40, "should ease, not snap: got %d", v);
	}

	/* 3. framerate independence: same wall time at 30 vs 300 fps (within 10%) */
	{
		hp_t a = { 100.0f, 0, 1, 10.0f };
		hp_t b = { 100.0f, 0, 1, 10.0f };
		double ta = settle(&a, 1.0 / 30.0,  30.0);
		double tb = settle(&b, 1.0 / 300.0, 30.0);
		double rel = fabs(ta - tb) / ((ta + tb) * 0.5);
		CHECK(rel < 0.10,
			  "frametime should not change duration: 30fps=%.3fs 300fps=%.3fs rel=%.3f",
			  ta, tb, rel);
	}

	/* 4. always reaches the target exactly (no infinite crawl) */
	{
		hp_t s = { 100.0f, 37, 1, 10.0f };
		settle(&s, 1.0 / 60.0, 30.0);
		CHECK(s.display == 37.0f, "must settle exactly: got %.4f", s.display);
	}

	/* 5. never negative even with a huge step */
	{
		hp_t s = { 100.0f, 0, 1, 100.0f };
		for (int i = 0; i < 500; i++) hp_step(&s, 1.0);   /* absurd dt */
		CHECK(s.display >= 0.0f, "must clamp at 0: got %.4f", s.display);
		CHECK(hp_shown(&s) == 0, "shown must be 0: got %d", hp_shown(&s));
	}

	/* 6. speed cvar is clamped: out-of-range values still behave */
	{
		hp_t lo = { 100.0f, 50, 1, -5.0f };     /* clamps to 1 */
		hp_t hi = { 100.0f, 50, 1, 9999.0f };   /* clamps to 100 */
		double tlo = settle(&lo, 1.0 / 60.0, 60.0);
		double thi = settle(&hi, 1.0 / 60.0, 60.0);
		CHECK(lo.display == 50.0f, "clamped-low must settle: %.3f", lo.display);
		CHECK(hi.display == 50.0f, "clamped-high must settle: %.3f", hi.display);
		CHECK(thi < tlo, "higher speed must settle sooner: hi=%.3f lo=%.3f", thi, tlo);
	}

	/* 7. healing upwards works the same way */
	{
		hp_t s = { 10.0f, 100, 1, 10.0f };
		hp_step(&s, 1.0 / 60.0);
		CHECK(hp_shown(&s) > 10, "should rise: got %d", hp_shown(&s));
		settle(&s, 1.0 / 60.0, 30.0);
		CHECK(s.display == 100.0f, "must reach 100: got %.3f", s.display);
	}

	/* 8. extended health (>255) animates too, and crosses 255 downwards.
	 *    This is why the drawer must be picked by the SHOWN value: while
	 *    easing from 500 to 100 the number passes through <256. */
	{
		hp_t s = { 500.0f, 100, 1, 10.0f };
		int saw_below_256 = 0;
		for (int i = 0; i < 2000; i++) {
			hp_step(&s, 1.0 / 60.0);
			if (hp_shown(&s) <= 255) { saw_below_256 = 1; break; }
		}
		CHECK(saw_below_256,
			  "animating down from 500 must pass under 256 (drawer choice must "
			  "follow the shown value, not the target)");
	}

	/* 9. retargeting mid-animation does not glitch (damage while animating) */
	{
		hp_t s = { 100.0f, 60, 1, 10.0f };
		for (int i = 0; i < 5; i++) hp_step(&s, 1.0 / 60.0);
		float mid = s.display;
		CHECK(mid < 100.0f && mid > 60.0f, "mid-animation value sane: %.3f", mid);
		s.health = 20;                        /* took another hit */
		settle(&s, 1.0 / 60.0, 30.0);
		CHECK(s.display == 20.0f, "must settle on the new target: %.3f", s.display);
	}

	/* 10. no movement when already at target */
	{
		hp_t s = { 75.0f, 75, 1, 10.0f };
		hp_step(&s, 1.0 / 60.0);
		CHECK(s.display == 75.0f, "must stay put: %.3f", s.display);
	}

	if (fails == 0) {
		printf("ALL HEALTH ANIMATION TESTS PASSED\n");
		return 0;
	}
	printf("%d CHECK(S) FAILED\n", fails);
	return 1;
}
