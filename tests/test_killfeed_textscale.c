// tests/kf_textscale.c -- guard the property that actually decides whether the
// killfeed's text looks right in game: the engine's raster font must never be
// asked to SHRINK.
//
// Why this matters (measured, not assumed): the user's device runs hud_scale
// "1600" on a 2800x1260 screen, which CL_GetScreenInfo turns into a VIRTUAL
// 1600x720 space (libxash.so 0x16ea58 compares hud_scale to 320.0f, 0x16eaac
// divides physical width by it, 0x16eb18 stores physicalHeight/factor). So
// kf_scale() sees screenH=720, giving scale 0.667, and the text cell becomes
// 27*0.667 = 18px. With a raster font ~19-24px tall that is textScale < 1, i.e.
// the engine resamples a bitmap font DOWN and the glyph strokes break up. That is
// the "font is not the right one" the user reported -- not a size problem, a
// resampling problem.
//
// The rule: textScale may stretch (>= 1.0) but must never shrink below
// KF_TEXTSCALE_MIN. kf_metrics_from_scale() enforces it by growing the text cell
// to the font's own height when the scale would shrink it.
#include <stdio.h>
#include <math.h>
#include "killfeed_layout.h"

static int fails = 0;
static int checks = 0;

#define CHECK(cond, msg) do { \
	checks++; \
	if( !(cond) ) { fails++; printf("FAIL: %s\n", msg); } \
} while(0)

int main( void )
{
	int screenH[] = { 240, 480, 540, 600, 720, 768, 900, 1024, 1080, 1260,
					  1440, 1600, 2160 };
	float user[] = { 0.25f, 0.5f, 0.6f, 0.8f, 1.0f, 1.2f, 1.5f, 2.0f, 4.0f };
	int raster[] = { 8, 12, 16, 18, 19, 20, 22, 24, 32, 43 };
	unsigned si, ui, ri;

	for( si = 0; si < sizeof(screenH)/sizeof(screenH[0]); si++ )
	for( ui = 0; ui < sizeof(user)/sizeof(user[0]); ui++ )
	for( ri = 0; ri < sizeof(raster)/sizeof(raster[0]); ri++ )
	{
		kf_metrics m;
		char msg[192];

		kf_compute_metrics( screenH[si], user[ui], raster[ri], &m );

		snprintf( msg, sizeof(msg),
				  "screenH=%d user=%.2f raster=%d -> textScale=%.3f shrinks the font",
				  screenH[si], user[ui], raster[ri], m.textScale );
		CHECK( m.textScale >= KF_TEXTSCALE_MIN - 0.001f, msg );

		// The text cell must always be able to hold the glyphs it will draw.
		snprintf( msg, sizeof(msg),
				  "screenH=%d user=%.2f raster=%d -> cell %d < drawn glyph height %d",
				  screenH[si], user[ui], raster[ri], m.textH,
				  (int)( raster[ri] * m.textScale ) );
		CHECK( m.textH + 1 >= (int)( raster[ri] * m.textScale ), msg );

		// The plate must still contain the text cell.
		snprintf( msg, sizeof(msg), "screenH=%d user=%.2f raster=%d -> pady %d < 1",
				  screenH[si], user[ui], raster[ri], m.pady );
		CHECK( m.pady >= 1, msg );
	}

	// The fallback path draws glyphs at natural size, so it must report exactly
	// 1.0 -- never a resample.
	for( ri = 0; ri < sizeof(raster)/sizeof(raster[0]); ri++ )
	for( ui = 0; ui < sizeof(user)/sizeof(user[0]); ui++ )
	{
		kf_metrics m;
		char msg[160];
		kf_compute_metrics_for_font( raster[ri], user[ui], &m );
		snprintf( msg, sizeof(msg), "fallback raster=%d user=%.2f textScale=%.3f != 1",
				  raster[ri], user[ui], m.textScale );
		CHECK( fabsf( m.textScale - 1.0f ) < 0.0001f, msg );
		snprintf( msg, sizeof(msg), "fallback raster=%d user=%.2f cell %d != raster",
				  raster[ri], user[ui], m.textH );
		CHECK( m.textH == raster[ri], msg );
	}

	printf( "%d checks, %d failures\n", checks, fails );
	if( fails )
		return 1;
	printf( "ALL KILLFEED TEXTSCALE TESTS PASSED\n" );
	return 0;
}
