/* test_killfeed_fade.c -- the row fade must never DARKEN the text.
 *
 * WHY THIS EXISTS. No text entry point in the engine takes an alpha:
 *     pfnDrawSetTextColor( float r, float g, float b )
 *     pfnDrawCharacter( int x, int y, int ch, int r, int g, int b )
 * so the only lever on a glyph's opacity is its colour. Scaling the colour is a
 * correct fade ONLY under an additive blend, where the glyph's contribution to
 * the frame is linear in the colour. Under a trans blend the glyph replaces the
 * backdrop weighted by the font's own coverage, so a scaled colour is not a
 * transparent glyph -- it is a BLACK one, drawn at full opacity.
 *
 * That is the reported bug: text went black as the row expired, after the feed
 * was switched to the console font (a trans blend).
 *
 * death.cpp needs the engine and cannot be compiled on the host, so the decision
 * function is mirrored here. scripts/kf_fade_check.py proves the mirror still
 * matches the real source; if you change one, change both.
 */
#include <stdio.h>
#include <string.h>

/* ---- mirror of the two predicates in death.cpp ------------------------- */

static int g_mobileAPI;        /* g_iMobileAPIVersion */
static float g_kfFont;         /* cl_killfeed_font->value */
static float g_hudFontrender;  /* engine cvar hud_fontrender */

static int KF_TextScalable( void )
{
	if( g_kfFont != 0.0f )
		return 0;
	return g_mobileAPI != 0;
}

static int KF_TextFadeable( void )
{
	if( !KF_TextScalable() )
		return 0;
	if( g_hudFontrender != 0.0f )
		return 0;
	return 1;
}

/* Colour actually handed to the draw call, mirroring KF_DrawName. */
static void kf_text_rgb( float alpha, const float *rgb, int *r, int *g, int *b )
{
	float fade = KF_TextFadeable() ? alpha : 1.0f;
	*r = (int)( rgb[0] * fade * 255.0f );
	*g = (int)( rgb[1] * fade * 255.0f );
	*b = (int)( rgb[2] * fade * 255.0f );
}

/* ------------------------------------------------------------------------ */

static int fails;

static void check( int cond, const char *what )
{
	if( cond )
		printf( "  ok    %s\n", what );
	else
	{
		printf( "  FAIL  %s\n", what );
		fails++;
	}
}

int main( void )
{
	/* White is the killfeed's own colour; a team colour behaves the same way. */
	static const float white[3] = { 1.0f, 1.0f, 1.0f };
	static const float teamB[3] = { 0.36f, 0.58f, 0.91f };
	int r, g, b, i;

	printf( "=== console font (trans blend): colour must NOT be scaled ===\n" );
	g_mobileAPI = 1; g_kfFont = 1.0f; g_hudFontrender = 0.0f;
	check( !KF_TextFadeable(), "console path reports non-fadeable" );

	/* The whole point: at the very end of the fade the glyph is still white. */
	for( i = 0; i <= 10; i++ )
	{
		float a = (float)i / 10.0f;
		kf_text_rgb( a, white, &r, &g, &b );
		if( r != 255 || g != 255 || b != 255 )
		{
			printf( "  FAIL  alpha %.1f darkened white to (%d,%d,%d)\n", a, r, g, b );
			fails++;
		}
	}
	printf( "  ok    white stays (255,255,255) across the whole fade\n" );

	kf_text_rgb( 0.05f, teamB, &r, &g, &b );
	check( r == (int)( 0.36f * 255.0f ) && g == (int)( 0.58f * 255.0f ),
		   "team colour keeps its hue at the end of the fade" );

	printf( "\n=== scalable font, additive: colour IS the opacity ===\n" );
	g_mobileAPI = 1; g_kfFont = 0.0f; g_hudFontrender = 0.0f;
	check( KF_TextFadeable(), "additive path reports fadeable" );

	kf_text_rgb( 1.0f, white, &r, &g, &b );
	check( r == 255 && g == 255 && b == 255, "alpha 1.0 -> full white" );
	kf_text_rgb( 0.5f, white, &r, &g, &b );
	check( r == 127 && g == 127 && b == 127, "alpha 0.5 -> half (additive = dimmer)" );
	kf_text_rgb( 0.0f, white, &r, &g, &b );
	check( r == 0 && g == 0 && b == 0, "alpha 0.0 -> gone" );

	/* Monotonic: an older row is never brighter than a fresher one. */
	{
		int prev = 256, bad = 0;
		for( i = 10; i >= 0; i-- )
		{
			kf_text_rgb( (float)i / 10.0f, white, &r, &g, &b );
			if( r > prev ) bad++;
			prev = r;
		}
		check( !bad, "fade is monotonic on the additive path" );
	}

	printf( "\n=== user sets hud_fontrender 2 (trans) on the scalable path ===\n" );
	/* This is the trap the separate predicate exists for: the font is still the
	 * scalable one, so KF_TextScalable() is true, but the blend is no longer
	 * additive and scaling the colour would blacken the glyphs again. */
	g_mobileAPI = 1; g_kfFont = 0.0f; g_hudFontrender = 2.0f;
	check( KF_TextScalable(), "font is still the scalable one" );
	check( !KF_TextFadeable(), "but fading is refused (trans blend)" );
	kf_text_rgb( 0.1f, white, &r, &g, &b );
	check( r == 255 && g == 255 && b == 255, "text stays white, not dark grey" );

	printf( "\n=== desktop build (no mobile API) ===\n" );
	g_mobileAPI = 0; g_kfFont = 0.0f; g_hudFontrender = 0.0f;
	check( !KF_TextScalable(), "no mobile API -> console path" );
	check( !KF_TextFadeable(), "and therefore not fadeable" );

	printf( "\n%s\n", fails ? "SOME FADE CHECKS FAILED" : "ALL FADE CHECKS PASSED" );
	return fails ? 1 : 0;
}
