/*
cl_sb_scheme.cpp - CS 1.6 VGUI scheme (ClientScheme.res) reader for the Bloom
                   scoreboard. Pure C++ using only the C library so it can be
                   host-tested against the real resource files shipped with the
                   game, not a paraphrase of them.

Ported from the engine-side Slayer3D parser (cl_sb_scheme_slayer.c) and EXTENDED
with the CS 1.6 team-colour palette keys (team0/team1/team2), which the original
board takes from the scheme rather than from cvars.

Precedence, matching the reference board:
  * A key present in ClientScheme.res supplies the colour.
  * A key present in a widget block (SectionedListPanel.*) wins over the same
    field named by a general palette entry, because the widget key names THIS
    exact control.
  * Team colours: an explicit team1/team2/team0 in the Colors section wins; if
    absent, the board keeps its compiled-in default (no black text).
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "cl_sb_scheme.h"

#define SB_TOK_MAX     128
#define SB_COLORS_MAX  96
#define SB_DEPTH_MAX   8

typedef struct
{
	char name[SB_SCHEME_NAME_MAX];
	char value[SB_TOK_MAX];
} sb_named_color_t;

// ---------------------------------------------------------------------------
// Tokenizer: quoted string, bare word, or a single { / }. // comments to EOL.
// Braces are their own tokens even when glued to a word ("Colors{").
// ---------------------------------------------------------------------------
static int SB_Token( const char **pp, char *out, int size )
{
	const char *p = *pp;
	int len = 0;

	out[0] = '\0';

	for( ;; )
	{
		while( *p && (unsigned char)*p <= ' ' )
			p++;

		if( p[0] == '/' && p[1] == '/' )
		{
			while( *p && *p != '\n' )
				p++;
			continue;
		}
		break;
	}

	if( !*p )
	{
		*pp = p;
		return 0;
	}

	if( *p == '{' || *p == '}' )
	{
		out[0] = *p;
		out[1] = '\0';
		*pp = p + 1;
		return 1;
	}

	if( *p == '"' )
	{
		p++;
		while( *p && *p != '"' )
		{
			if( len + 1 < size )
				out[len++] = *p;
			p++;
		}
		if( *p == '"' )
			p++;
		out[len] = '\0';
		*pp = p;
		return 1;
	}

	while( *p && (unsigned char)*p > ' ' && *p != '{' && *p != '}'
		&& !( p[0] == '/' && p[1] == '/' ))
	{
		if( len + 1 < size )
			out[len++] = *p;
		p++;
	}
	out[len] = '\0';
	*pp = p;
	return 1;
}

static int SB_StrEqualNoCase( const char *a, const char *b )
{
	while( *a && *b )
	{
		int ca = (unsigned char)*a;
		int cb = (unsigned char)*b;

		if( ca >= 'A' && ca <= 'Z' ) ca += 32;
		if( cb >= 'A' && cb <= 'Z' ) cb += 32;
		if( ca != cb )
			return 0;
		a++;
		b++;
	}
	return ( *a == '\0' && *b == '\0' );
}

// "62 70 55 255" or "62 70 55" (alpha defaults to opaque). Clamped here.
static int SB_ParseRGBA( const char *s, unsigned char *out )
{
	int v[4] = { 0, 0, 0, 255 };
	int n, i;

	if( !s || !s[0] )
		return 0;

	n = sscanf( s, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3] );
	if( n < 3 )
		return 0;

	for( i = 0; i < 4; i++ )
	{
		if( v[i] < 0 )   v[i] = 0;
		if( v[i] > 255 ) v[i] = 255;
		out[i] = (unsigned char)v[i];
	}
	return 1;
}

// Collect the file's Colors section so BaseSettings keys that name a colour
// ("SelectedBgColor" "Orange") can be dereferenced.
static int SB_ReadColors( const char *text, sb_named_color_t *table, int max )
{
	const char *p = text;
	char tok[SB_TOK_MAX];
	char stack[SB_DEPTH_MAX][SB_SCHEME_NAME_MAX];
	int  depth = 0;
	int  count = 0;

	for( ;; )
	{
		const char *save;
		char key[SB_TOK_MAX];

		if( !SB_Token( &p, key, sizeof( key )))
			break;

		if( key[0] == '}' && key[1] == '\0' )
		{
			if( depth > 0 ) depth--;
			continue;
		}
		if( key[0] == '{' && key[1] == '\0' )
			continue;

		save = p;
		if( !SB_Token( &p, tok, sizeof( tok )))
			break;

		if( tok[0] == '{' && tok[1] == '\0' )
		{
			if( depth < SB_DEPTH_MAX )
			{
				strncpy( stack[depth], key, SB_SCHEME_NAME_MAX - 1 );
				stack[depth][SB_SCHEME_NAME_MAX - 1] = '\0';
			}
			depth++;
			continue;
		}
		if( tok[0] == '}' && tok[1] == '\0' )
		{
			p = save;
			continue;
		}

		if( depth > 0 && depth <= SB_DEPTH_MAX
		 && SB_StrEqualNoCase( stack[depth - 1], "Colors" )
		 && count < max )
		{
			strncpy( table[count].name, key, SB_SCHEME_NAME_MAX - 1 );
			table[count].name[SB_SCHEME_NAME_MAX - 1] = '\0';
			strncpy( table[count].value, tok, SB_TOK_MAX - 1 );
			table[count].value[SB_TOK_MAX - 1] = '\0';
			count++;
		}
	}

	return count;
}

static int SB_Resolve( const sb_named_color_t *table, int count, const char *value,
	unsigned char *out )
{
	int i;

	if( !value || !value[0] )
		return 0;

	if( SB_ParseRGBA( value, out ))   // a literal is not a name
		return 1;

	for( i = 0; i < count; i++ )
	{
		if( SB_StrEqualNoCase( table[i].name, value ))
			return SB_ParseRGBA( table[i].value, out );
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Wanted keys
// ---------------------------------------------------------------------------
typedef struct
{
	const char  *key;
	unsigned int flag;
	int          offset;   // byte offset of the rgba field inside sb_scheme_t
	int          prio;     // higher wins when two keys feed one field
} sb_wanted_t;

#define SB_FIELD( f ) ( (int)( (char *)&( (sb_scheme_t *)0 )->f - (char *)0 ) )
#define SB_PRIO_PALETTE  0
#define SB_PRIO_WIDGET   1

static const sb_wanted_t sb_wanted[] =
{
	// Widget block (TrackerScheme-style themed board): most specific.
	{ "SectionedListPanel.BgColor",            SB_SCHEME_HAS_BG,            SB_FIELD( bg ),            SB_PRIO_WIDGET  },
	{ "SectionedListPanel.SelectedBgColor",    SB_SCHEME_HAS_SELECTED_BG,   SB_FIELD( selected_bg ),   SB_PRIO_WIDGET  },
	{ "SectionedListPanel.HeaderTextColor",    SB_SCHEME_HAS_HEADER_TEXT,   SB_FIELD( header_text ),   SB_PRIO_WIDGET  },
	{ "SectionedListPanel.TextColor",          SB_SCHEME_HAS_TEXT,          SB_FIELD( text ),          SB_PRIO_WIDGET  },
	{ "SectionedListPanel.DividerColor",       SB_SCHEME_HAS_DIVIDER,       SB_FIELD( divider ),       SB_PRIO_WIDGET  },

	// ClientScheme.res palette (vanilla CS 1.6 board).
	{ "ListBG",         SB_SCHEME_HAS_BG,          SB_FIELD( bg ),          SB_PRIO_PALETTE },
	{ "SelectionBG",    SB_SCHEME_HAS_SELECTED_BG, SB_FIELD( selected_bg ), SB_PRIO_PALETTE },
	{ "BrightBaseText", SB_SCHEME_HAS_HEADER_TEXT, SB_FIELD( header_text ), SB_PRIO_PALETTE },
	{ "BaseText",       SB_SCHEME_HAS_TEXT,        SB_FIELD( text ),        SB_PRIO_PALETTE },
	{ "BorderDark",     SB_SCHEME_HAS_DIVIDER,     SB_FIELD( divider ),     SB_PRIO_PALETTE },

	// CS 1.6 team-name colours. team1 = T (teamnumber 1), team2 = CT (teamnumber 2), team0 = spec/unassigned.
	// These are the keys the reference scoreboard actually reads for names, and
	// the reason the user should not have to pick colours by hand.
	{ "team0", SB_SCHEME_HAS_TEAM0, SB_FIELD( team0 ), SB_PRIO_PALETTE },
	{ "team1", SB_SCHEME_HAS_TEAM1, SB_FIELD( team1 ), SB_PRIO_PALETTE },
	{ "team2", SB_SCHEME_HAS_TEAM2, SB_FIELD( team2 ), SB_PRIO_PALETTE },
};

#define SB_WANTED_COUNT ( (int)( sizeof( sb_wanted ) / sizeof( sb_wanted[0] )))

int SBScheme_ResolveValue( const char *text, const char *value, unsigned char *out_rgba )
{
	sb_named_color_t table[SB_COLORS_MAX];
	int count;

	if( !out_rgba )
		return 0;

	memset( out_rgba, 0, 4 );

	if( !text )
		return SB_ParseRGBA( value, out_rgba );

	count = SB_ReadColors( text, table, SB_COLORS_MAX );
	return SB_Resolve( table, count, value, out_rgba );
}

int SBScheme_Parse( const char *text, sb_scheme_t *out )
{
	sb_named_color_t table[SB_COLORS_MAX];
	char raw[SB_WANTED_COUNT][SB_TOK_MAX];
	char section[SB_DEPTH_MAX][SB_SCHEME_NAME_MAX];
	const char *p;
	int  colors;
	int  resolved = 0;
	int  depth;
	int  i;

	if( !out )
		return 0;

	memset( out, 0, sizeof( *out ));
	memset( raw, 0, sizeof( raw ));

	if( !text || !text[0] )
		return 0;

	colors = SB_ReadColors( text, table, SB_COLORS_MAX );

	// Collect first, resolve afterwards, so Colors may follow BaseSettings.
	// Section is tracked so a palette name only matches as a KEY inside Colors,
	// never as a VALUE ("ListBgColor" "ListBG") elsewhere.
	p = text;
	depth = 0;
	for( ;; )
	{
		char key[SB_TOK_MAX];
		char val[SB_TOK_MAX];
		const char *save;
		int  in_colors;

		if( !SB_Token( &p, key, sizeof( key )))
			break;

		if( key[0] == '}' && key[1] == '\0' )
		{
			if( depth > 0 ) depth--;
			continue;
		}
		if( key[0] == '{' && key[1] == '\0' )
			continue;

		save = p;
		if( !SB_Token( &p, val, sizeof( val )))
			break;

		if( val[0] == '{' && val[1] == '\0' )
		{
			if( depth < SB_DEPTH_MAX )
			{
				strncpy( section[depth], key, SB_SCHEME_NAME_MAX - 1 );
				section[depth][SB_SCHEME_NAME_MAX - 1] = '\0';
			}
			depth++;
			continue;
		}
		if( val[0] == '}' && val[1] == '\0' )
		{
			p = save;
			continue;
		}

		in_colors = ( depth > 0 && depth <= SB_DEPTH_MAX
			&& SB_StrEqualNoCase( section[depth - 1], "Colors" ));

		for( i = 0; i < SB_WANTED_COUNT; i++ )
		{
			if( sb_wanted[i].prio == SB_PRIO_PALETTE && !in_colors )
				continue;

			if( SB_StrEqualNoCase( key, sb_wanted[i].key ))
			{
				strncpy( raw[i], val, SB_TOK_MAX - 1 );
				raw[i][SB_TOK_MAX - 1] = '\0';
				break;
			}
		}
	}

	for( i = 0; i < SB_WANTED_COUNT; i++ )
	{
		unsigned char rgba[4];
		int j;
		int shadowed = 0;

		if( !raw[i][0] )
			continue;

		for( j = 0; j < SB_WANTED_COUNT; j++ )
		{
			if( j == i )
				continue;
			if( sb_wanted[j].offset != sb_wanted[i].offset )
				continue;
			if( raw[j][0] && sb_wanted[j].prio > sb_wanted[i].prio )
			{
				shadowed = 1;
				break;
			}
		}
		if( shadowed )
			continue;

		if( !SB_Resolve( table, colors, raw[i], rgba ))
			continue;

		memcpy( (char *)out + sb_wanted[i].offset, rgba, 4 );
		out->have |= sb_wanted[i].flag;
		resolved++;
	}

	return resolved;
}
