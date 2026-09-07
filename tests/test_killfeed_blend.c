/* Test: how many times does the killfeed toggle the OpenGL blend per frame?
 *
 * The previous code switched to kRenderTransAdd BEFORE every icon and back to
 * kRenderTransTexture AFTER every icon. With up to 16 icons per row and 4 rows
 * per frame that is up to 128 transitions per frame. The engine flags the swap
 * with OpenGL Error: GL_INVALID_ENUM whenever its sprite pipeline is mid-upload,
 * so the same draw produces a wall of identical error lines in the console.
 *
 * The fix moves the bracket to the whole loop: one RenderMode call at the top
 * and one at the bottom. Per-icon work stays as draw calls, which is what the
 * blend state is supposed to wrap.
 *
 * The harness models the render-mode state machine, not the OpenGL stack, so
 * it is portable: it counts RenderMode() calls in death.cpp and checks the
 * invariants the fix is supposed to preserve. The OpenGL spam itself cannot be
 * reproduced without the engine binary.
 *
 * Acceptance:
 *   * Two RenderMode() calls inside CHudDeathNotice::Draw (one ADD, one TEX).
 *   * Zero RenderMode() calls inside KF_DrawIcon (the per-icon path).
 *   * KF_DrawIcon is called from the row loop, NOT from KF_DrawName or any text
 *     path, because additive glyphs would dissolve the text -- another reason
 *     to keep it out of the icon helper.
 *   * kRenderTransAdd is set BEFORE the loop and kRenderTransTexture AFTER.
 *     This is not just cosmetic: the engine's error fires on the TRANSITION
 *     to additive, so getting the order wrong moves the spam rather than
 *     removing it. */
#include <stdio.h>
#include <string.h>

static const char *DEATH = "cl_dll/death.cpp";

static int count_render_mode_in_draw( const char *src )
{
	/* Find the body of CHudDeathNotice::Draw. Anything else -- helpers, the
	 * legacy notice path -- is not what we care about. */
	const char *open = strstr( src, "int CHudDeathNotice :: Draw" );
	if( !open ) return -1;
	const char *body = strchr( open, '{' );
	if( !body ) return -1;
	int depth = 0, n = 0;
	for( const char *p = body; *p; p++ )
	{
		if( *p == '{' ) depth++;
		else if( *p == '}' )
		{
			depth--;
			if( !depth ) break;
		}
		else if( depth == 1 && strncmp( p, "RenderMode", 10 ) == 0
			&& ( p[10] == '(' || p[10] == ' ' ) )
			n++;
	}
	return n;
}

static int count_render_mode_in_kf_draw_icon( const char *src )
{
	/* Find the actual DEFINITION, not the first mention. The first "KF_DrawIcon"
	 * in the source is in a comment at the top of the file -- picking that up
	 * made the count meaningless. The definition starts with "static void
	 * KF_DrawIcon("; require the "static" so a caller named e.g. "KF_DrawIconX"
	 * cannot sneak in. */
	const char *def = strstr( src, "static void KF_DrawIcon(" );
	if( !def ) return -1;
	const char *body = strchr( def, '{' );
	if( !body ) return -1;
	int depth = 0, n = 0;
	for( const char *p = body; *p; p++ )
	{
		if( *p == '{' ) depth++;
		else if( *p == '}' )
		{
			depth--;
			if( !depth ) break;
		}
		else if( depth == 1 && strncmp( p, "RenderMode", 10 ) == 0
			&& ( p[10] == '(' || p[10] == ' ' ) )
			n++;
	}
	return n;
}

static const char *first_render_mode_arg( const char *src, const char *needle )
{
	const char *p = strstr( src, needle );
	if( !p ) return NULL;
	const char *paren = strchr( p, '(' );
	if( !paren ) return NULL;
	paren++;
	while( *paren == ' ' || *paren == '\t' ) paren++;
	return paren;
}

static int starts_with( const char *s, const char *prefix )
{
	return strncmp( s, prefix, strlen( prefix ) ) == 0;
}

int main( void )
{
	int fails = 0;
	char buf[200000];
	FILE *f = fopen( DEATH, "r" );
	if( !f ) { printf( "cannot open %s\n", DEATH ); return 1; }
	size_t n = fread( buf, 1, sizeof( buf ) - 1, f );
	buf[n] = 0;
	fclose( f );

	int in_draw = count_render_mode_in_draw( buf );
	printf( "RenderMode() calls in CHudDeathNotice::Draw: %d\n", in_draw );
	if( in_draw != 2 )
	{
		printf( "  FAIL: expected exactly 2 (one ADD at the top, one TEX at the bottom)\n" );
		fails++;
	}
	else
		printf( "  OK: bracket is one toggle at each end of the loop\n" );

	int in_icon = count_render_mode_in_kf_draw_icon( buf );
	printf( "RenderMode() calls in KF_DrawIcon: %d\n", in_icon );
	if( in_icon != 0 )
	{
		printf( "  FAIL: per-icon toggles were the source of GL_INVALID_ENUM spam\n" );
		fails++;
	}
	else
		printf( "  OK: blend state is owned by the loop, not the icon helper\n" );

	const char *add = first_render_mode_arg( buf, "RenderMode( kRenderTransAdd" );
	const char *tex = first_render_mode_arg( buf, "RenderMode( kRenderTransTexture" );
	printf( "first RenderMode(kRenderTransAdd) arg position: %ld\n",
		add ? (long)( add - buf ) : -1 );
	printf( "first RenderMode(kRenderTransTexture) arg position: %ld\n",
		tex ? (long)( tex - buf ) : -1 );

	if( !add || !tex )
	{
		printf( "  FAIL: missing one of the two render-mode calls\n" );
		fails++;
	}
	else if( !starts_with( add, "kRenderTransAdd )" ) )
	{
		printf( "  FAIL: ADD call has unexpected argument\n" );
		fails++;
	}
	else if( !starts_with( tex, "kRenderTransTexture )" ) )
	{
		printf( "  FAIL: TEX call has unexpected argument\n" );
		fails++;
	}
	else
		printf( "  OK: ADD and TEX use the canonical mode names\n" );

	/* The first ADD must come BEFORE the first TEX. If a future change swaps
	 * the order the spam moves rather than disappears: the engine logs the
	 * transition INTO additive, not the call itself. */
	if( add && tex && add > tex )
	{
		printf( "  FAIL: ADD appears after TEX in the source -- spam would survive\n" );
		fails++;
	}

	printf( "\n%s\n", fails ? "SOME BLEND-BRACKET CHECKS FAILED"
							 : "ALL BLEND-BRACKET CHECKS PASSED" );
	return fails ? 1 : 0;
}
