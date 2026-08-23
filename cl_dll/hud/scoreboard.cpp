/***
*
*	Copyright (c) 1999, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// Scoreboard.cpp
//
// implementation of CHudScoreboard class
//
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "triangleapi.h"
#include "com_weapons.h"
#include "cdll_dll.h"
#include "draw_util.h"
#include "vgui_parser.h"
#include "eventscripts.h"
#include "cl_sb_scheme.h"

hud_player_info_t   g_PlayerInfoList[MAX_PLAYERS+1]; // player info from the engine
extra_player_info_t	g_PlayerExtraInfo[MAX_PLAYERS+1]; // additional player info sent directly to the client dll
team_info_t         g_TeamInfo[MAX_TEAMS+1];
hostage_info_t      g_HostageInfo[MAX_HOSTAGES+1];
int g_iUser1;
int g_iUser2;
int g_iUser3;
int g_iTeamNumber;


// X positions

int xstart, xend;
int ystart, yend;

enum
{
	COL_NAME = 0,
	COL_ATTRIB,
	COL_HP,
	COL_MONEY,
	COL_KILLS,
	COL_DEATHS,
	COL_PING,
	TOTAL_COLUMNS
};

static struct Column
{
	int start, end;
	const char *name;

	Column() :
	    start( 0 ), end( 0 ), name( nullptr ) { }

	Column( int s, const char *n = nullptr, bool reverse = true )
	{
		name = n;
		end = 0;
		start = 0;

		if ( reverse )
		{
			start = s;
			if ( n )
				end = start - DrawUtils::HudStringLen( n );
		}
		else
		{
			start = s;
			if ( n )
				end = start + DrawUtils::HudStringLen( n );
		}
	}
} g_Columns[TOTAL_COLUMNS];

//#include "vgui_TeamFortressViewport.h"

// ===========================================================================
// ClientScheme.res colours (CS 1.6 style)
//
// The board reads resource/ClientScheme.res once and uses it as the source of
// its colours, exactly like the original CS 1.6 scoreboard: team1/team2/team0
// for player-name colours, ListBG for the panel, SelectionBG for the local
// player's row, BrightBaseText for headers. Anything the file omits keeps the
// compiled-in default, so a missing or malformed file never blacks the board.
//
// bloom_scoreboard_scheme (default 1) lets a player turn the file off and get
// the built-in palette back; bloom_scoreboard_scheme_file overrides the path.
// ===========================================================================
static sb_scheme_t s_scheme;
static bool        s_scheme_tried = false;
static cvar_t     *s_pCvarScheme      = nullptr;   // bloom_scoreboard_scheme (0/1)
static cvar_t     *s_pCvarSchemeFile  = nullptr;   // path, default resource/ClientScheme.res

static void Scoreboard_LoadScheme( void )
{
	s_scheme_tried = true;
	memset( &s_scheme, 0, sizeof( s_scheme ));

	if( s_pCvarScheme && s_pCvarScheme->value == 0.0f )
		return; // explicitly disabled: keep built-in colours

	const char *path = ( s_pCvarSchemeFile && s_pCvarSchemeFile->string[0] )
		? s_pCvarSchemeFile->string : "resource/ClientScheme.res";

	int len = 0;
	char *buf = reinterpret_cast<char*>( gEngfuncs.COM_LoadFile( (char*)path, 5, &len ));
	if( !buf )
	{
		gEngfuncs.Con_DPrintf( "Scoreboard: scheme '%s' not found -- using built-in colours\n", path );
		return;
	}

	int got = SBScheme_Parse( buf, &s_scheme );
	gEngfuncs.COM_FreeFile( buf );
	gEngfuncs.Con_DPrintf( "Scoreboard: scheme '%s' -> %d key(s), have=0x%02x\n", path, got, s_scheme.have );
}

// Force a re-read next draw (e.g. after the player edits the file and reconnects).
void Scoreboard_InvalidateScheme( void )
{
	s_scheme_tried = false;
}

// Player-name colour for a team, from the scheme when present, else the client's
// built-in GetClientColor (which the reference board also falls back to). Values
// are 0..255. Returns via r/g/b.
static void Scoreboard_TeamColor( int r_in, int g_in, int b_in, int teamnumber, int &r, int &g, int &b )
{
	const unsigned char *c = nullptr;

	switch( teamnumber )
	{
	case TEAM_TERRORIST:
		// teamnumber 1 == Terrorists -> scheme key "team1"
		if( s_scheme.have & SB_SCHEME_HAS_TEAM1 ) c = s_scheme.team1;
		break;
	case TEAM_CT:
		// teamnumber 2 == Counter-Terrorists -> scheme key "team2"
		if( s_scheme.have & SB_SCHEME_HAS_TEAM2 ) c = s_scheme.team2;
		break;
	case TEAM_SPECTATOR:
	case TEAM_UNASSIGNED:
		if( s_scheme.have & SB_SCHEME_HAS_TEAM0 ) c = s_scheme.team0;
		break;
	}

	if( c )
	{
		r = c[0]; g = c[1]; b = c[2];
	}
	else
	{
		r = r_in; g = g_in; b = b_in; // built-in default already computed by caller
	}
}

// Header / server-name text colour: BrightBaseText from the scheme, else the
// board's historic amber (255 140 0).
static void Scoreboard_HeaderColor( int &r, int &g, int &b )
{
	if( s_scheme.have & SB_SCHEME_HAS_HEADER_TEXT )
	{
		r = s_scheme.header_text[0]; g = s_scheme.header_text[1]; b = s_scheme.header_text[2];
	}
	else { r = 255; g = 140; b = 0; }
}

// ===========================================================================
// Adaptive board size (ported from the engine reference cl_scoreboard_slayer.c)
//
// The board advances a fractional `list_slot` and draws each element at
// ystart + list_slot * pitch, breaking when ypos > yend. With the old fixed
// pitch (ROW_GAP 15) and fixed 75%-wide box the board was the same size for one
// player or a full server: a lone player floated in a huge empty panel, and a
// 32-player server ran past the bottom edge so the last rows were clipped.
//
// Scoreboard_ComputeGeometry sizes the panel to the roster:
//   * width grows from a compact fraction (few players) to a wider one (full),
//   * row pitch is the comfortable "natural" height for a sparse roster, and is
//     compressed only enough that a full server's LAST DRAWN ROW still sits
//     inside the panel — never clipped.
//
// s_rowPitch replaces the ROW_GAP constant in every ypos computation. The two
// slot models (drawn-bottom for the panel height, running-extent for the pitch
// clamp) are the ones proved against a loop simulation in test_sb_layout.cpp.

static float s_rowPitch = 15.0f;   // px per list_slot unit; set per-frame below

// Live-tunable so a player can widen/tighten without a rebuild. Defaults match
// the engine reference board.
static cvar_t *s_pCvarWidthMax     = nullptr;  // bloom_scoreboard_width        (full-roster width frac)
static cvar_t *s_pCvarWidthCompact = nullptr;  // bloom_scoreboard_width_compact(few-player width frac)
static cvar_t *s_pCvarShowKit      = nullptr;  // bloom_scoreboard_show_defusekit (0 = hide "Компл." column text)

// Count players and the teams that will actually get a header, so the geometry
// matches what the draw loops emit. Called after GetAllPlayersInfo(), before the
// draw loops NULL the names out.
static void Scoreboard_CountRoster( int &out_players, int &out_teams )
{
	int players = 0;
	// buckets keyed by teamnumber: CT, T, spectator/unassigned, other
	bool have_ct = false, have_t = false, have_spec = false, have_other = false;

	for( int i = 1; i < MAX_PLAYERS; i++ )
	{
		if( !g_PlayerInfoList[i].name || !g_PlayerInfoList[i].name[0] )
			continue;
		players++;

		switch( g_PlayerExtraInfo[i].teamnumber )
		{
		case TEAM_CT:         have_ct = true; break;
		case TEAM_TERRORIST:  have_t = true; break;
		case TEAM_SPECTATOR:
		case TEAM_UNASSIGNED: have_spec = true; break;
		default:              have_other = true; break;
		}
	}

	out_players = players;
	out_teams   = ( have_ct ? 1 : 0 ) + ( have_t ? 1 : 0 )
	            + ( have_spec ? 1 : 0 ) + ( have_other ? 1 : 0 );
}

// Sets xstart/xend/ystart/yend and s_rowPitch for the given roster. Centered.
static void Scoreboard_ComputeGeometry( int num_players, bool teamplay, int num_teams )
{
	const int scr_w = ScreenWidth;
	const int scr_h = ScreenHeight;

	// --- width: interpolate compact->max across 5..20 players (engine wfrac) ---
	float max_frac     = s_pCvarWidthMax     ? s_pCvarWidthMax->value     : 0.72f;
	float compact_frac = s_pCvarWidthCompact ? s_pCvarWidthCompact->value : 0.62f;

	if( max_frac < 0.55f ) max_frac = 0.55f;
	if( max_frac > 0.95f ) max_frac = 0.95f;
	if( compact_frac < 0.50f ) compact_frac = 0.50f;
	if( compact_frac > max_frac ) compact_frac = max_frac;

	float roster_t = (float)( num_players - 5 ) / 15.0f;
	if( roster_t < 0.0f ) roster_t = 0.0f;
	if( roster_t > 1.0f ) roster_t = 1.0f;

	float wfrac = compact_frac + ( max_frac - compact_frac ) * roster_t;
	int board_w = (int)( scr_w * wfrac );
	int min_w = (int)( scr_w * 0.50f );
	int max_w = (int)( scr_w * 0.95f );
	if( board_w < min_w ) board_w = min_w;
	if( board_w > max_w ) board_w = max_w;

	// --- slot extents (see test_sb_layout.cpp) ---
	// running_extent overshoots by the trailing DrawPlayers advances; using it as
	// the pitch denominator guarantees even those advances fit, so nothing clips.
	// drawn_bottom is where the LAST VISIBLE row sits; the panel is sized to it so
	// a small roster gets no empty strip under the last player.
	float running_extent, drawn_bottom;
	if( teamplay )
	{
		running_extent = 8.8f + 3.6f * (float)num_teams + (float)num_players;
		drawn_bottom   = 3.6f * (float)num_teams + (float)num_players + 0.8f;
	}
	else
	{
		running_extent = 4.8f + (float)num_players;
		drawn_bottom   = 2.8f + (float)num_players;
	}
	if( num_players <= 0 ) { running_extent = 4.8f; drawn_bottom = 2.8f; }

	// --- pitch: comfortable natural height, compressed only to avoid clipping ---
	int charH = gHUD.GetCharHeight();
	if( charH < 8 ) charH = 8;                 // sane floor if scrinfo not ready
	int pad = charH / 3;
	if( pad < 4 )  pad = 4;
	if( pad > 12 ) pad = 12;
	float natural = (float)( charH + pad );
	if( natural < 15.0f ) natural = 15.0f;     // never tighter than the old board

	float avail = (float)scr_h * 0.92f;        // usable vertical band
	float fit   = ( running_extent > 0.0f ) ? avail / running_extent : natural;
	float pitch = ( natural < fit ) ? natural : fit;
	if( pitch < 1.0f ) pitch = 1.0f;
	s_rowPitch = pitch;

	// --- panel rectangle, sized to the drawn content and centered ---
	int board_h = (int)( ( drawn_bottom + 0.5f ) * pitch + 0.5f );  // +0.5 slot bottom margin
	int avail_h = (int)( scr_h * 0.95f );
	if( board_h > avail_h ) board_h = avail_h;

	// small minimum so a near-empty server still reads as a board
	int min_h = (int)( pitch * 4.0f );
	if( board_h < min_h ) board_h = min_h;
	if( board_h > avail_h ) board_h = avail_h;

	xstart = ( scr_w - board_w ) / 2;
	xend   = xstart + board_w;
	ystart = ( scr_h - board_h ) / 2;
	if( ystart < 0 ) ystart = 0;
	yend   = ystart + board_h;
}



int CHudScoreboard :: Init( void )
{
	gHUD.AddHudElem( this );

	// Hook messages & commands here
	HOOK_COMMAND( gHUD.m_Scoreboard, "+showscores", ShowScores );
	HOOK_COMMAND( gHUD.m_Scoreboard, "-showscores", HideScores );
	HOOK_COMMAND( gHUD.m_Scoreboard, "showscoreboard2", ShowScoreboard2 );
	HOOK_COMMAND( gHUD.m_Scoreboard, "hidescoreboard2", HideScoreboard2 );

	HOOK_MESSAGE( gHUD.m_Scoreboard, ScoreInfo );
	HOOK_MESSAGE( gHUD.m_Scoreboard, TeamScore );
	HOOK_MESSAGE( gHUD.m_Scoreboard, TeamInfo );

	InitHUDData();

	cl_showpacketloss = CVAR_CREATE( "cl_showpacketloss", "0", FCVAR_ARCHIVE );
	cl_showplayerversion = CVAR_CREATE( "cl_showplayerversion", "0", 0 );
	cl_show_scoreboard_on_death = CVAR_CREATE( "cl_show_scoreboard_on_death", "0", FCVAR_ARCHIVE );

	// --- Bloom board: appearance from ClientScheme.res -------------------
	// 1 = read the file (default), 0 = keep the compiled-in palette.
	s_pCvarScheme     = CVAR_CREATE( "bloom_scoreboard_scheme", "1", FCVAR_ARCHIVE );
	s_pCvarSchemeFile = CVAR_CREATE( "bloom_scoreboard_scheme_file",
									 "resource/ClientScheme.res", FCVAR_ARCHIVE );

	// --- Bloom board: adaptive size -------------------------------------
	// Panel width as a fraction of the screen: _compact for a near-empty
	// server, _width for a full roster; interpolated across 5..20 players.
	s_pCvarWidthMax     = CVAR_CREATE( "bloom_scoreboard_width",         "0.72", FCVAR_ARCHIVE );
	s_pCvarWidthCompact = CVAR_CREATE( "bloom_scoreboard_width_compact", "0.62", FCVAR_ARCHIVE );
	// 0 = hide the defuse-kit tag in the attribute column (default).
	s_pCvarShowKit      = CVAR_CREATE( "bloom_scoreboard_show_defusekit", "0", FCVAR_ARCHIVE );

	return 1;
}


int CHudScoreboard :: VidInit( void )
{
	xstart = ScreenWidth * 0.125f;
	xend = ScreenWidth - xstart;
	ystart = 100;
	yend = ScreenHeight - ystart;
	m_bForceDraw = false;

	// Load sprites here
	return 1;
}

void CHudScoreboard :: InitHUDData( void )
{
	memset( g_PlayerExtraInfo, 0, sizeof g_PlayerExtraInfo );
	m_iLastKilledBy = 0;
	m_fLastKillTime = 0;
	m_iPlayerNum = 0;
	m_iNumTeams = 0;
	memset( g_TeamInfo, 0, sizeof g_TeamInfo );

	for ( int i = 1; i <= MAX_PLAYERS; i++ )
	{
		// a1ba: get the cl.playernum from the engine
		// it shouldn't ever change during normal gameplay
		if( !m_iPlayerNum && EV_IsLocal( i ))
			m_iPlayerNum = i;

		g_PlayerExtraInfo[i].sb_health = -1;
		g_PlayerExtraInfo[i].sb_account = -1;
	}

	m_iFlags &= ~HUD_DRAW;  // starts out inactive

	m_iFlags |= HUD_INTERMISSION; // is always drawn during an intermission

	// Re-read ClientScheme.res on (re)connect so switching servers or editing
	// the file takes effect without a restart.
	Scoreboard_InvalidateScheme();
}

bool CHudScoreboard :: ShouldDrawScoreboard() const
{
	if( m_bForceDraw )
		return true;

	if( m_bShowscoresHeld || gHUD.m_iIntermission )
		return true;

	if( cl_show_scoreboard_on_death && cl_show_scoreboard_on_death->value && gHUD.m_Health.m_iHealth <= 0 )
		return true;

	return false;
}

// Y positions. The row pitch is now the adaptive s_rowPitch (computed per
// roster in Scoreboard_ComputeGeometry); this constant is kept only as the
// documented baseline the pitch never drops below.
#define ROW_GAP  15

int CHudScoreboard :: Draw( float flTime )
{
	if( !ShouldDrawScoreboard( ))
		return 1;

	if( !m_bForceDraw )
	{
		xstart     = 0.125f * ScreenWidth;
		xend       = ScreenWidth - xstart;
		ystart     = 90;
		yend       = ScreenHeight - ystart;
		m_colors.r = 0;
		m_colors.g = 0;
		m_colors.b = 0;
		m_colors.a = 153;
		m_bDrawStroke = true;
	}

	return DrawScoreboard(flTime);
}

int CHudScoreboard :: DrawScoreboard( float fTime )
{
	GetAllPlayersInfo();
	char ServerName[90];

	// Colours come from ClientScheme.res; read once per connect (cheap no-op
	// afterwards). Must happen before anything is drawn.
	if( !s_scheme_tried )
		Scoreboard_LoadScheme();

	// Size the panel to the current roster (adaptive width + row pitch), unless
	// showscoreboard2 supplied its own geometry via m_bForceDraw.
	if( !m_bForceDraw )
	{
		int roster_players = 0, roster_teams = 0;
		Scoreboard_CountRoster( roster_players, roster_teams );
		Scoreboard_ComputeGeometry( roster_players, gHUD.m_Teamplay != 0, roster_teams );

		// Panel fill from the scheme's ListBG when the file provides one.
		if( s_scheme.have & SB_SCHEME_HAS_BG )
		{
			m_colors.r = s_scheme.bg[0];
			m_colors.g = s_scheme.bg[1];
			m_colors.b = s_scheme.bg[2];
			m_colors.a = s_scheme.bg[3] ? s_scheme.bg[3] : 153;
		}
	}

//	Packetloss removed on Kelly 'shipping nazi' Bailey's orders
//	if ( cl_showpacketloss && cl_showpacketloss->value && ( ScreenWidth >= 400 ) )
//	{
//		can_show_packetloss = 1;
//	}

	// just sort the list on the fly
	// list is sorted first by frags, then by deaths
	float list_slot = 0;

	// calculate columns sizes
	g_Columns[COL_PING] = Column( xend - 15, Localize( "#PlayerPing" ) );
	g_Columns[COL_PING].end = min( g_Columns[COL_PING].end, g_Columns[COL_PING].start - DrawUtils::HudStringLen( "9999" ) );

	g_Columns[COL_DEATHS] = Column( g_Columns[COL_PING].end - 10, Localize( "#PlayerDeath" ) );
	g_Columns[COL_DEATHS].end = min( g_Columns[COL_DEATHS].end, g_Columns[COL_DEATHS].start - DrawUtils::HudStringLen( "9999" ) );

	g_Columns[COL_KILLS] = Column( g_Columns[COL_DEATHS].end - 10, Localize( "#PlayerScore" ) );
	g_Columns[COL_KILLS].end = min( g_Columns[COL_KILLS].end, g_Columns[COL_KILLS].start - DrawUtils::HudStringLen( "9999" ) );

	g_Columns[COL_MONEY] = Column( g_Columns[COL_KILLS].end - 10, Localize( "#Cstrike_ACCOUNT" ) );
	g_Columns[COL_MONEY].end = min( g_Columns[COL_MONEY].end, g_Columns[COL_MONEY].start - DrawUtils::HudStringLen( "$999999" ) );

	g_Columns[COL_HP] = Column( g_Columns[COL_MONEY].end - 10, Localize( "#Cstrike_HEALTH" ) );
	g_Columns[COL_HP].end = min( g_Columns[COL_HP].end, g_Columns[COL_HP].start - DrawUtils::HudStringLen( "999999" ) );

	g_Columns[COL_ATTRIB] = Column( g_Columns[COL_HP].end - 10 );
	g_Columns[COL_ATTRIB].end = g_Columns[COL_ATTRIB].start - DrawUtils::HudStringLen( "#Cstrike_DEFUSE_KIT" );

	g_Columns[COL_NAME] = Column( xstart + 15, nullptr, false );
	g_Columns[COL_NAME].end = g_Columns[COL_ATTRIB].end - 10;

	// print the heading line

	DrawUtils::DrawRectangle(xstart, ystart, xend - xstart, yend - ystart,
		m_colors.r, m_colors.g, m_colors.b, m_colors.a, m_bDrawStroke);

	int ypos = ystart + (int)(list_slot * s_rowPitch) + 5;

	if( gHUD.m_szServerName[0] )
		// snprintf( ServerName, 80, "%s", (char*)(gHUD.m_Teamplay ? "TEAMS" : "PLAYERS"), gHUD.m_szServerName );
		strncpy( ServerName, gHUD.m_szServerName, 80 );
	else
		strncpy( ServerName, gHUD.m_Teamplay ? "TEAMS" : "PLAYERS", 80 );

	// Header / column-label colour from the scheme (BrightBaseText), else amber.
	int hr, hg, hb;
	Scoreboard_HeaderColor( hr, hg, hb );

	DrawUtils::DrawHudString( g_Columns[COL_NAME].start, ypos, g_Columns[COL_NAME].end, ServerName, hr, hg, hb );
	DrawUtils::DrawHudStringReverse( g_Columns[COL_HP].start, ypos, g_Columns[COL_HP].end, g_Columns[COL_HP].name, hr, hg, hb );
	DrawUtils::DrawHudStringReverse( g_Columns[COL_MONEY].start, ypos, g_Columns[COL_MONEY].end, g_Columns[COL_MONEY].name, hr, hg, hb );
	DrawUtils::DrawHudStringReverse( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, g_Columns[COL_KILLS].name, hr, hg, hb );
	DrawUtils::DrawHudStringReverse( g_Columns[COL_DEATHS].start, ypos, g_Columns[COL_DEATHS].end, g_Columns[COL_DEATHS].name, hr, hg, hb );
	DrawUtils::DrawHudStringReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, g_Columns[COL_PING].name, hr, hg, hb );

	list_slot += 2;
	ypos = ystart + (int)(list_slot * s_rowPitch);
	{
		// separator line: scheme DividerColor (BorderDark) when present, else amber
		int sr = 255, sg = 140, sb = 0, sa = 255;
		if( s_scheme.have & SB_SCHEME_HAS_DIVIDER )
		{
			sr = s_scheme.divider[0]; sg = s_scheme.divider[1];
			sb = s_scheme.divider[2]; sa = s_scheme.divider[3] ? s_scheme.divider[3] : 255;
		}
		FillRGBA( xstart, ypos, xend - xstart, 1, sr, sg, sb, sa );
	}

	list_slot += 0.8;

	if ( gHUD.m_Teamplay )
	{
		DrawTeams( list_slot );
	}
	else
	{
		// it's not teamplay,  so just draw a simple player list
		DrawPlayers( list_slot );
	}
	return 1;
}

int CHudScoreboard :: DrawTeams( float list_slot )
{
	int j;
	int ypos = ystart + (int)(list_slot * s_rowPitch) + 5;

	// clear out team scores
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		if ( !g_TeamInfo[i].scores_overriden )
			g_TeamInfo[i].frags = g_TeamInfo[i].deaths = 0;
		g_TeamInfo[i].sumping = 0;
		g_TeamInfo[i].players = 0;
		g_TeamInfo[i].already_drawn = FALSE;
	}

	// recalc the team scores, then draw them
	for ( int i = 1; i < MAX_PLAYERS; i++ )
	{
		if ( !g_PlayerInfoList[i].name || !g_PlayerInfoList[i].name[0] )
			continue; // empty player slot, skip

		if ( g_PlayerExtraInfo[i].teamname[0] == 0 )
			continue; // skip over players who are not in a team

		// find what team this player is in
		for ( j = 1; j <= m_iNumTeams; j++ )
		{
			if ( !stricmp( g_PlayerExtraInfo[i].teamname, g_TeamInfo[j].name ) )
				break;
		}

		if ( j > m_iNumTeams )  // player is not in a team, skip to the next guy
			continue;

		if ( !g_TeamInfo[j].scores_overriden )
		{
			g_TeamInfo[j].frags += g_PlayerExtraInfo[i].frags;
			g_TeamInfo[j].deaths += g_PlayerExtraInfo[i].deaths;
		}

		g_TeamInfo[j].sumping += g_PlayerInfoList[i].ping;

		if ( g_PlayerInfoList[i].thisplayer )
			g_TeamInfo[j].ownteam = TRUE;
		else
			g_TeamInfo[j].ownteam = FALSE;

		g_TeamInfo[j].players++;
	}

	// Draw the teams
	int iSpectatorPos = -1;

	while( true )
	{
		int highest_frags = -99999; int lowest_deaths = 99999;
		int best_team = 0;

		for ( int i = 1; i <= m_iNumTeams; i++ )
		{
			// don't draw team without players
			if ( g_TeamInfo[i].players <= 0 )
				continue;

			if (!strnicmp(g_TeamInfo[i].name, "SPECTATOR", MAX_TEAM_NAME))
			{
				iSpectatorPos = i;
				continue;
			}

			if ( !g_TeamInfo[i].already_drawn && g_TeamInfo[i].frags >= highest_frags )
			{
				if ( g_TeamInfo[i].frags > highest_frags || g_TeamInfo[i].deaths < lowest_deaths )
				{
					best_team = i;
					lowest_deaths = g_TeamInfo[i].deaths;
					highest_frags = g_TeamInfo[i].frags;
				}
			}
		}

		// draw the best team on the scoreboard
		if ( !best_team )
		{
			// if spectators is found and still not drawn
			if( iSpectatorPos != -1 && g_TeamInfo[iSpectatorPos].already_drawn == FALSE )
				best_team = iSpectatorPos;
			else break;
		}
		// draw out the best team
		team_info_t *team_info = &g_TeamInfo[best_team];

		// don't draw team without players
		if ( team_info->players <= 0 )
			continue;

		ypos = ystart + (int)(list_slot * s_rowPitch);

		// check we haven't drawn too far down
		if ( ypos > yend )  // don't draw to close to the lower border
			break;

		int r, g, b;
		char teamName[64];

		char numPlayers[16];
		sprintf( numPlayers, "%d", team_info->players );

		char fmtString[32];
		const char *fmtStringName = team_info->players == 1 ? "#Cstrike_ScoreBoard_Player" : "#Cstrike_ScoreBoard_Players";
		strncpy( fmtString, Localize( fmtStringName ), sizeof( fmtString ) );
		fmtString[sizeof( fmtString ) - 1] = 0;

		if ( !strcmp( fmtString, fmtStringName ) )
		{
			const char *fallback = team_info->players == 1 ? "%s1    -   %s2 player" : "%s1    -   %s2 players";
			strncpy( fmtString, fallback, sizeof( fmtString ) );
			fmtString[sizeof( fmtString ) - 1] = 0;
		}

		GetTeamColor( r, g, b, team_info->teamnumber );
		// scheme team colour (team1/team2/team0) overrides the built-in when set
		Scoreboard_TeamColor( r, g, b, team_info->teamnumber, r, g, b );
		switch ( team_info->teamnumber )
		{
		case TEAM_TERRORIST:
		{
			const char *args[2] = { Localize( "#Cstrike_ScoreBoard_Ter" ), numPlayers };
			Localize_Format( teamName, sizeof( teamName ), fmtString, args, 2 );
			DrawUtils::DrawHudNumberString( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, team_info->frags, r, g, b );
			break;
		}
		case TEAM_CT:
		{
			const char *args[2] = { Localize( "#Cstrike_ScoreBoard_CT" ), numPlayers };
			Localize_Format( teamName, sizeof( teamName ), fmtString, args, 2 );
			DrawUtils::DrawHudNumberString( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, team_info->frags, r, g, b );
			break;
		}
		case TEAM_SPECTATOR:
		case TEAM_UNASSIGNED:
			strncpy( teamName, Localize( "#Spectators" ), sizeof( teamName ) );
			break;
		}

		DrawUtils::DrawHudString( g_Columns[COL_NAME].start, ypos, g_Columns[COL_NAME].end, teamName, r, g, b );
		DrawUtils::DrawHudNumberString( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, team_info->sumping / team_info->players, r, g, b );

		team_info->already_drawn = TRUE;  // set the already_drawn to be TRUE, so this team won't get drawn again

		// draw underline
		list_slot += 1.2f;
		FillRGBA( xstart, ystart + (int)(list_slot * s_rowPitch), xend - xstart, 1, r, g, b, 255);

		list_slot += 0.4f;
		// draw all the players that belong to this team, indented slightly
		list_slot = DrawPlayers( list_slot, 10, team_info->name );
	}

	// draw all the players who are not in a team
	list_slot += 4.0f;
	DrawPlayers( list_slot, 0, "" );

	return 1;
}

// returns the ypos where it finishes drawing
int CHudScoreboard :: DrawPlayers( float list_slot, int nameoffset, const char *team )
{
	// draw the players, in order,  and restricted to team if set
	while ( 1 )
	{
		// Find the top ranking player
		int highest_frags = -99999;	int lowest_deaths = 99999;
		int best_player = 0;

		for ( int i = 1; i < MAX_PLAYERS; i++ )
		{
			if ( g_PlayerInfoList[i].name && g_PlayerExtraInfo[i].frags >= highest_frags )
			{
				if ( !(team && stricmp(g_PlayerExtraInfo[i].teamname, team)) )  // make sure it is the specified team
				{
					extra_player_info_t *pl_info = &g_PlayerExtraInfo[i];
					if ( pl_info->frags > highest_frags || pl_info->deaths < lowest_deaths )
					{
						best_player = i;
						lowest_deaths = pl_info->deaths;
						highest_frags = pl_info->frags;
					}
				}
			}
		}

		if ( !best_player )
			break;

		// draw out the best player
		hud_player_info_t *pl_info = &g_PlayerInfoList[best_player];

		int ypos = ystart + (int)(list_slot * s_rowPitch);

		// check we haven't drawn too far down
		if ( ypos > yend )  // don't draw to close to the lower border
			break;

		int r = 255, g = 255, b = 255;
		float *colors = GetClientColor( best_player );
		r *= colors[0];
		g *= colors[1];
		b *= colors[2];

		// Scheme team colour (team1/team2/team0) overrides the built-in when the
		// .res file sets one; otherwise the value computed above is kept.
		Scoreboard_TeamColor( r, g, b, g_PlayerExtraInfo[best_player].teamnumber, r, g, b );

		if(pl_info->thisplayer) // hey, it's me!
		{
			// Local player's row highlight: scheme SelectionBG when present, else
			// the historic faint white wash. Band height follows the adaptive row
			// pitch so it matches the row it marks.
			int band = (int)s_rowPitch;
			if( band < 1 ) band = 1;

			if( s_scheme.have & SB_SCHEME_HAS_SELECTED_BG )
				FillRGBABlend( xstart, ypos, xend - xstart, band,
					s_scheme.selected_bg[0], s_scheme.selected_bg[1],
					s_scheme.selected_bg[2],
					s_scheme.selected_bg[3] ? s_scheme.selected_bg[3] : 15 );
			else
				FillRGBABlend( xstart, ypos, xend - xstart, band, 255, 255, 255, 15 );
		}

		DrawUtils::DrawHudString( g_Columns[COL_NAME].start + nameoffset, ypos, g_Columns[COL_NAME].start + 350, pl_info->name, r, g, b );

		if( cl_showplayerversion->value == 0.0f )
		{
			if( team && stricmp( team, "SPECTATOR" ))
			{
				// draw bomb( if player have the bomb )
				if( g_PlayerExtraInfo[best_player].dead )
					DrawUtils::DrawHudStringReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, Localize( "#Cstrike_DEAD" ), r, g, b );
				else if( g_PlayerExtraInfo[best_player].has_c4 )
					DrawUtils::DrawHudStringReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, Localize( "#Cstrike_BOMB" ), r, g, b );
				else if( g_PlayerExtraInfo[best_player].vip )
					DrawUtils::DrawHudStringReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, Localize( "#Cstrike_VIP" ),  r, g, b );
				else if (g_PlayerExtraInfo[best_player].has_defuse_kit
						 && s_pCvarShowKit && s_pCvarShowKit->value != 0.0f )
					DrawUtils::DrawHudStringReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, Localize( "#Cstrike_DEFUSE_KIT" ),  r, g, b );
			}
		}
		else
		{
			DrawUtils::DrawHudStringReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, gEngfuncs.PlayerInfo_ValueForKey( best_player, "cscl_ver" ),  r, g, b );
		}

		if ( g_PlayerExtraInfo[best_player].sb_health >= 0 && !g_PlayerExtraInfo[best_player].dead )
		{
			if ( gHUD.m_pShowHealth->value )
			{
				static char buf[64];
				sprintf( buf, "%d", g_PlayerExtraInfo[best_player].sb_health );
				DrawUtils::DrawHudStringReverse( g_Columns[COL_HP].start, ypos, g_Columns[COL_HP].end, buf, r, g, b );
			}
		}

		if ( g_PlayerExtraInfo[best_player].sb_account >= 0 )
		{
			if ( gHUD.m_pShowMoney->value )
			{
				static char buf[64];
				sprintf( buf, "$%d", g_PlayerExtraInfo[best_player].sb_account );
				DrawUtils::DrawHudStringReverse( g_Columns[COL_MONEY].start, ypos, g_Columns[COL_MONEY].end, buf, r, g, b );
			}
		}

		// draw kills (right to left)
		if( team && stricmp( team, "SPECTATOR" ) )
		{
			DrawUtils::DrawHudNumberString( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, g_PlayerExtraInfo[best_player].frags, r, g, b );

			// draw deaths
			DrawUtils::DrawHudNumberString( g_Columns[COL_DEATHS].start, ypos, g_Columns[COL_DEATHS].end, g_PlayerExtraInfo[best_player].deaths, r, g, b );
		}

		// draw ping & packetloss
		const char *value;
		if( pl_info->ping <= 5  // must be 0, until Xash's bug not fixed
			&& ( value = gEngfuncs.PlayerInfo_ValueForKey( best_player, "*bot" ) )
			&& atoi( value ) > 0 )
		{
			DrawUtils::DrawHudStringReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, "BOT", r, g, b );
		}
		else
		{
			static char buf[64];
			sprintf( buf, "%d", pl_info->ping );
			DrawUtils::DrawHudStringReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, buf, r, g, b );
		}

		pl_info->name = NULL;  // set the name to be NULL, so this client won't get drawn again
		list_slot++;
	}

	list_slot += 2.0f;

	return list_slot;
}


void CHudScoreboard :: GetAllPlayersInfo( void )
{
	memset( &g_PlayerInfoList[0], 0, sizeof( g_PlayerInfoList[0] ));

	for( int i = 1; i < MAX_PLAYERS; i++ )
		GetPlayerInfo( i, &g_PlayerInfoList[i] );
}

int CHudScoreboard :: MsgFunc_ScoreInfo( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );
	short cl = reader.ReadByte();
	short frags = reader.ReadShort();
	short deaths = reader.ReadShort();
	short playerclass = reader.ReadShort();
	short teamnumber = reader.ReadShort();

	if ( cl > 0 && cl <= MAX_PLAYERS )
	{
		g_PlayerExtraInfo[cl].frags = frags;
		g_PlayerExtraInfo[cl].deaths = deaths;
		g_PlayerExtraInfo[cl].playerclass = playerclass;
		g_PlayerExtraInfo[cl].teamnumber = teamnumber;

		//gViewPort->UpdateOnPlayerInfo();
	}

	return 1;
}

// Message handler for TeamInfo message
// accepts two values:
//		byte: client number
//		string: client team name
int CHudScoreboard :: MsgFunc_TeamInfo( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	short cl = reader.ReadByte();
	int teamNumber = 0;

	if ( cl > 0 && cl <= MAX_PLAYERS )
	{
		// set the players team
		char teamName[MAX_TEAM_NAME];
		strncpy( teamName, reader.ReadString(), MAX_TEAM_NAME );
		teamName[MAX_TEAM_NAME-1] = 0;

		if( !strcmp( teamName, "TERRORIST") )
			teamNumber = TEAM_TERRORIST;
		else if( !strcmp( teamName, "CT") )
			teamNumber = TEAM_CT;
		else if( !strcmp( teamName, "SPECTATOR" ) )
		{
			teamNumber = TEAM_SPECTATOR;
		}
		else if( !strcmp( teamName, "UNASSIGNED" ) )
		{
			teamNumber = TEAM_UNASSIGNED;
			strncpy( teamName, "SPECTATOR", MAX_TEAM_NAME );
		}
		// just in case
		else teamNumber = TEAM_UNASSIGNED;

		strncpy( g_PlayerExtraInfo[cl].teamname, teamName, MAX_TEAM_NAME );
		g_PlayerExtraInfo[cl].teamnumber = teamNumber;
	}

	// rebuild the list of teams

	// clear out player counts from teams
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		g_TeamInfo[i].players = 0;
	}

	// rebuild the team list
	GetAllPlayersInfo();
	m_iNumTeams = 0;

	for ( int i = 1; i < MAX_PLAYERS; i++ )
	{
		int j;
		//if ( g_PlayerInfoList[i].name == NULL )
		//	continue;

		if ( g_PlayerExtraInfo[i].teamname[0] == 0 )
			continue; // skip over players who are not in a team

		// is this player in an existing team?
		for ( j = 1; j <= m_iNumTeams; j++ )
		{
			if ( g_TeamInfo[j].name[0] == '\0' )
				break;

			if ( !stricmp( g_PlayerExtraInfo[i].teamname, g_TeamInfo[j].name ) )
				break;
		}

		if ( j > m_iNumTeams )
		{
			// they aren't in a listed team, so make a new one
			for ( j = 1; j <= m_iNumTeams; j++ )
			{
				if ( g_TeamInfo[j].name[0] == '\0' )
					break;
			}


			m_iNumTeams = max( j, m_iNumTeams );

			strncpy( g_TeamInfo[j].name, g_PlayerExtraInfo[i].teamname, MAX_TEAM_NAME );
			g_TeamInfo[j].teamnumber = g_PlayerExtraInfo[i].teamnumber;
			g_TeamInfo[j].players = 0;
		}

		g_TeamInfo[j].players++;
	}

	// clear out any empty teams
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		if ( g_TeamInfo[i].players < 1 )
			memset( &g_TeamInfo[i], 0, sizeof(team_info_t) );
	}

	return 1;
}

// Message handler for TeamScore message
// accepts three values:
//		string: team name
//		short: teams kills
//		short: teams deaths
// if this message is never received, then scores will simply be the combined totals of the players.
int CHudScoreboard :: MsgFunc_TeamScore( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	char *TeamName = reader.ReadString();
	int i;

	// find the team matching the name
	for ( i = 0; i < m_iNumTeams; i++ )
	{
		if ( !stricmp( TeamName, g_TeamInfo[i].name ) )
			break;
	}
	if ( i > m_iNumTeams )
	{
		reader.Flush();
		return 1;
	}

	// use this new score data instead of combined player scores
	g_TeamInfo[i].scores_overriden = TRUE;
	g_TeamInfo[i].frags = reader.ReadShort();
	// g_TeamInfo[i].deaths = reader.ReadShort();

	return 1;
}

void CHudScoreboard :: DeathMsg( int killer, int victim )
{
	// if we were the one killed,  or the world killed us, set the scoreboard to indicate suicide
	if ( victim == m_iPlayerNum || killer == 0 )
	{
		m_iLastKilledBy = killer ? killer : m_iPlayerNum;
		m_fLastKillTime = gHUD.m_flTime + 10;	// display who we were killed by for 10 seconds

		if ( killer == m_iPlayerNum )
			m_iLastKilledBy = m_iPlayerNum;
	}
}



void CHudScoreboard :: UserCmd_ShowScores( void )
{
	m_bForceDraw = false;
	m_bShowscoresHeld = true;
}

void CHudScoreboard :: UserCmd_HideScores( void )
{
	m_bForceDraw = m_bShowscoresHeld = false;
}


void CHudScoreboard	:: UserCmd_ShowScoreboard2()
{
	if( gEngfuncs.Cmd_Argc() != 9 )
	{
		ConsolePrint("showscoreboard2 <xstart> <xend> <ystart> <yend> <r> <g> <b> <a>");
	}

	xstart     = atof(gEngfuncs.Cmd_Argv(1)) * ScreenWidth;
	xend       = atof(gEngfuncs.Cmd_Argv(2)) * ScreenWidth;
	ystart     = atof(gEngfuncs.Cmd_Argv(3)) * ScreenHeight;
	yend       = atof(gEngfuncs.Cmd_Argv(4)) * ScreenHeight;
	m_colors.r = atoi(gEngfuncs.Cmd_Argv(5));
	m_colors.b = atoi(gEngfuncs.Cmd_Argv(6));
	m_colors.b = atoi(gEngfuncs.Cmd_Argv(7));
	m_colors.a = atoi(gEngfuncs.Cmd_Argv(8));
	m_bDrawStroke = false;
	m_bForceDraw = true;
}

void CHudScoreboard :: UserCmd_HideScoreboard2()
{
	m_bForceDraw = m_bShowscoresHeld = false; // and disable it
}
