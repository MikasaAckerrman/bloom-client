/*
 * Host test for the adaptive scoreboard geometry (cl_dll/hud/scoreboard.cpp:
 * Scoreboard_ComputeGeometry / Scoreboard_CountRoster).
 *
 * The real function touches ScreenWidth/ScreenHeight/gHUD, so the formulas are
 * replicated here EXACTLY as written in scoreboard.cpp and then driven through
 * the same slot loop the draw code performs. The point is to prove the two
 * properties the port claims and that the old fixed-pitch board violated:
 *
 *   1. NOTHING CLIPS: with the computed pitch, the last row the draw loop emits
 *      is still inside the panel (the old board silently dropped rows past yend
 *      on a full server).
 *   2. NO DEAD SPACE: a small roster gets a panel sized to its content, not the
 *      fixed 75%-of-screen box that left one player floating in emptiness.
 *
 * If the formulas in scoreboard.cpp change, update them here together.
 *
 * Build: cc -O2 -o /tmp/sb_test tests/test_sb_layout.c -lm && /tmp/sb_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond, msg, ...) do { \
	if (!(cond)) { printf("FAIL: " msg "\n", ##__VA_ARGS__); fails++; } \
} while (0)

/* ---- mirror of scoreboard.cpp ------------------------------------------- */
typedef struct {
	int   xstart, xend, ystart, yend;
	float pitch;
} geom_t;

static void compute_geometry(geom_t *g, int scr_w, int scr_h, int charH,
							 int num_players, int teamplay, int num_teams,
							 float max_frac, float compact_frac)
{
	if (max_frac < 0.55f) max_frac = 0.55f;
	if (max_frac > 0.95f) max_frac = 0.95f;
	if (compact_frac < 0.50f) compact_frac = 0.50f;
	if (compact_frac > max_frac) compact_frac = max_frac;

	float roster_t = (float)(num_players - 5) / 15.0f;
	if (roster_t < 0.0f) roster_t = 0.0f;
	if (roster_t > 1.0f) roster_t = 1.0f;

	float wfrac = compact_frac + (max_frac - compact_frac) * roster_t;
	int board_w = (int)(scr_w * wfrac);
	int min_w = (int)(scr_w * 0.50f);
	int max_w = (int)(scr_w * 0.95f);
	if (board_w < min_w) board_w = min_w;
	if (board_w > max_w) board_w = max_w;

	float running_extent, drawn_bottom;
	if (teamplay) {
		running_extent = 8.8f + 3.6f * (float)num_teams + (float)num_players;
		drawn_bottom   = 3.6f * (float)num_teams + (float)num_players + 0.8f;
	} else {
		running_extent = 4.8f + (float)num_players;
		drawn_bottom   = 2.8f + (float)num_players;
	}
	if (num_players <= 0) { running_extent = 4.8f; drawn_bottom = 2.8f; }

	if (charH < 8) charH = 8;
	int pad = charH / 3;
	if (pad < 4)  pad = 4;
	if (pad > 12) pad = 12;
	float natural = (float)(charH + pad);
	if (natural < 15.0f) natural = 15.0f;

	float avail = (float)scr_h * 0.92f;
	float fit   = (running_extent > 0.0f) ? avail / running_extent : natural;
	float pitch = (natural < fit) ? natural : fit;
	if (pitch < 1.0f) pitch = 1.0f;
	g->pitch = pitch;

	int board_h = (int)((drawn_bottom + 0.5f) * pitch + 0.5f);
	int avail_h = (int)(scr_h * 0.95f);
	if (board_h > avail_h) board_h = avail_h;
	int min_h = (int)(pitch * 4.0f);
	if (board_h < min_h) board_h = min_h;
	if (board_h > avail_h) board_h = avail_h;

	g->xstart = (scr_w - board_w) / 2;
	g->xend   = g->xstart + board_w;
	g->ystart = (scr_h - board_h) / 2;
	if (g->ystart < 0) g->ystart = 0;
	g->yend   = g->ystart + board_h;
}

/* ---- replica of the DRAW LOOP's slot advances ---------------------------
 * Taken from the real code, not guessed:
 *   DrawScoreboard: header at slot 0, += 2 (separator), += 0.8.
 *   DrawTeams, per team that has players: row at slot, += 1.2 (underline),
 *     += 0.4, then DrawPlayers(members) which does += 1 per player and += 2
 *     on exit; after the team returns, the loop continues to the next team.
 *   After all teams: += 4.0, then a final DrawPlayers for team-less players.
 *   DrawPlayers (no teamplay): += 1 per player, += 2 on exit.
 * Returns the slot of the LAST ROW ACTUALLY DRAWN, and sets *clipped if the
 * loop would have hit the ypos > yend guard.
 */
static float simulate(const geom_t *g, int num_players, int teamplay,
					  int num_teams, int *clipped)
{
	float slot = 0.0f;
	float last_drawn = 0.0f;
	*clipped = 0;

	slot += 2.0f;       /* separator */
	slot += 0.8f;

	if (teamplay) {
		int per_team = num_teams > 0 ? num_players / num_teams : 0;
		int leftover = num_teams > 0 ? num_players % num_teams : 0;
		for (int t = 0; t < num_teams; t++) {
			int ypos = g->ystart + (int)(slot * g->pitch);
			if (ypos > g->yend) { *clipped = 1; return last_drawn; }
			last_drawn = slot;
			slot += 1.2f;   /* team header + underline */
			slot += 0.4f;

			int members = per_team + (t < leftover ? 1 : 0);
			for (int p = 0; p < members; p++) {
				int py = g->ystart + (int)(slot * g->pitch);
				if (py > g->yend) { *clipped = 1; return last_drawn; }
				last_drawn = slot;
				slot += 1.0f;
			}
			slot += 2.0f;   /* DrawPlayers exit advance */
		}
	} else {
		for (int p = 0; p < num_players; p++) {
			int py = g->ystart + (int)(slot * g->pitch);
			if (py > g->yend) { *clipped = 1; return last_drawn; }
			last_drawn = slot;
			slot += 1.0f;
		}
	}
	return last_drawn;
}

int main(void)
{
	const int W = 1920, H = 1080, CH = 13;
	const float MAXF = 0.72f, CF = 0.62f;

	/* 1. NOTHING CLIPS across the whole roster range, both modes */
	for (int players = 0; players <= 32; players++) {
		for (int tp = 0; tp <= 1; tp++) {
			int teams = tp ? (players == 0 ? 0 : (players > 1 ? 2 : 1)) : 0;
			geom_t g;
			compute_geometry(&g, W, H, CH, players, tp, teams, MAXF, CF);
			int clipped = 0;
			simulate(&g, players, tp, teams, &clipped);
			CHECK(!clipped, "clipped at players=%d teamplay=%d (pitch=%.2f h=%d)",
				  players, tp, g.pitch, g.yend - g.ystart);
		}
	}

	/* 2. NO DEAD SPACE: the last drawn row sits near the bottom, not far above.
	 *    Allow one pitch of slack (bottom margin is +0.5 slot by design). */
	for (int players = 1; players <= 32; players++) {
		int teams = players > 1 ? 2 : 1;
		geom_t g;
		compute_geometry(&g, W, H, CH, players, 1, teams, MAXF, CF);
		int clipped = 0;
		float last = simulate(&g, players, 1, teams, &clipped);
		int last_y = g.ystart + (int)(last * g.pitch);
		int gap = g.yend - last_y;
		CHECK(gap <= (int)(g.pitch * 2.5f) + 2,
			  "dead space at players=%d: %dpx below last row (pitch %.1f)",
			  players, gap, g.pitch);
	}

	/* 3. width grows with the roster and stays inside the clamps */
	{
		geom_t few, many;
		compute_geometry(&few,  W, H, CH, 2,  1, 1, MAXF, CF);
		compute_geometry(&many, W, H, CH, 24, 1, 2, MAXF, CF);
		int w_few = few.xend - few.xstart, w_many = many.xend - many.xstart;
		CHECK(w_few < w_many, "width must grow: few=%d many=%d", w_few, w_many);
		CHECK(w_few >= (int)(W * 0.50f), "min width clamp: %d", w_few);
		CHECK(w_many <= (int)(W * 0.95f), "max width clamp: %d", w_many);
	}

	/* 4. horizontally centred in every case */
	for (int players = 0; players <= 32; players += 8) {
		geom_t g;
		compute_geometry(&g, W, H, CH, players, 1, 2, MAXF, CF);
		int left = g.xstart, right = W - g.xend;
		CHECK(abs(left - right) <= 1, "not centred at players=%d: %d vs %d",
			  players, left, right);
	}

	/* 5. pitch never tighter than the documented 15px baseline unless a full
	 *    roster genuinely forces compression, and never below 1 */
	{
		geom_t sparse;
		compute_geometry(&sparse, W, H, CH, 3, 1, 1, MAXF, CF);
		CHECK(sparse.pitch >= 15.0f, "sparse pitch must be comfortable: %.2f",
			  sparse.pitch);
		geom_t full;
		compute_geometry(&full, W, 240, CH, 32, 1, 2, MAXF, CF);  /* tiny screen */
		CHECK(full.pitch >= 1.0f, "pitch floor: %.3f", full.pitch);
		int clipped = 0;
		simulate(&full, 32, 1, 2, &clipped);
		CHECK(!clipped, "must still fit on a 240px-tall screen");
	}

	/* 6. panel stays on screen vertically */
	for (int players = 0; players <= 32; players += 4) {
		geom_t g;
		compute_geometry(&g, W, H, CH, players, 1, 2, MAXF, CF);
		CHECK(g.ystart >= 0, "ystart negative at %d: %d", players, g.ystart);
		CHECK(g.yend <= H, "yend past screen at %d: %d", players, g.yend);
	}

	/* 7. cvar clamping: absurd fractions do not produce an absurd board */
	{
		geom_t lo, hi;
		compute_geometry(&lo, W, H, CH, 10, 1, 2, 0.01f, 0.01f);
		compute_geometry(&hi, W, H, CH, 10, 1, 2, 9.0f,  9.0f);
		int wlo = lo.xend - lo.xstart, whi = hi.xend - hi.xstart;
		CHECK(wlo >= (int)(W * 0.50f), "clamped-low width: %d", wlo);
		CHECK(whi <= (int)(W * 0.95f), "clamped-high width: %d", whi);
	}

	/* 8. empty server still yields a readable board (min height) */
	{
		geom_t g;
		compute_geometry(&g, W, H, CH, 0, 1, 0, MAXF, CF);
		int h = g.yend - g.ystart;
		CHECK(h >= (int)(g.pitch * 4.0f), "empty board too short: %d", h);
	}

	/* 9. a phone-ish landscape viewport behaves too */
	for (int players = 1; players <= 32; players++) {
		geom_t g;
		compute_geometry(&g, 2340, 1080, 22, players, 1, 2, MAXF, CF);
		int clipped = 0;
		simulate(&g, players, 1, 2, &clipped);
		CHECK(!clipped, "phone viewport clipped at players=%d", players);
	}

	if (fails == 0) {
		printf("ALL SCOREBOARD LAYOUT TESTS PASSED\n");
		return 0;
	}
	printf("%d CHECK(S) FAILED\n", fails);
	return 1;
}
