/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
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
// death notice
//
// bloom: replaced the classic single-line valve notice with a CS2-style
// killfeed -- rounded semi-transparent plates, coloured names, weapon icon,
// and CS2 kill-modifier icons (blind / airborne / noscope / smoke / wallbang /
// headshot / flash-assist) decoded from ReGameDLL's kill-rarity flags. Rows
// slide in from the right, hold, then fade+drift out. Falls back to the legacy
// notice when cl_killfeed is 0 or the kf sprites are missing.
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "draw_util.h"
#include "strl.h"
#include "killfeed_layout.h"

float color[3];

// One modifier-icon sprite per kf_icon_slot (see killfeed_layout.h).
static const char *kf_mod_names[KFI_COUNT] =
{
	"blind",        // KFI_BLIND
	"inair",        // KFI_INAIR
	"noscope",      // KFI_NOSCOPE
	"smoke",        // KFI_SMOKE
	"penetrate",    // KFI_PENETRATE
	"headshot",     // KFI_HEADSHOT
	"flashbang_assist" // KFI_FLASHASSIST
};

// Weapon icon table. The server sends the killer weapon name with weapon_/
// monster_/func_ already stripped (see ReGameDLL GetKillerWeaponName), so we
// match the bare name and map a few CS aliases onto our sprite files.
struct kf_weapon_map { const char *msg; const char *spr; };
static const kf_weapon_map kf_weapons[] =
{
	{ "ak47", "ak47" }, { "aug", "aug" }, { "awp", "awp" },
	{ "deagle", "deagle" }, { "elite", "elite" }, { "famas", "famas" },
	{ "fiveseven", "fiveseven" }, { "g3sg1", "g3sg1" }, { "galil", "galilar" },
	{ "galilar", "galilar" }, { "glock18", "glock" }, { "glock", "glock" },
	{ "m249", "m249" }, { "m4a1", "m4a1" }, { "mac10", "mac10" },
	{ "mp5navy", "mp5sd" }, { "mp5sd", "mp5sd" }, { "p90", "p90" },
	{ "p228", "deagle" }, { "scout", "ssg08" }, { "ssg08", "ssg08" },
	{ "sg550", "g3sg1" }, { "sg552", "sg556" }, { "sg556", "sg556" },
	{ "tmp", "mac10" }, { "ump45", "ump45" }, { "usp", "usp_silencer" },
	{ "usp_silencer", "usp_silencer" }, { "xm1014", "xm1014" },
	{ "m3", "xm1014" }, { "hegrenade", "hegrenade" },
	{ "flashbang", "flashbang" }, { "smokegrenade", "smokegrenade" },
	{ "c4", "c4" }, { "knife", "knife" }, { "grenade", "hegrenade" },
	{ NULL, NULL }
};

#define KF_MAX_WEAPONS  40

struct DeathNoticeItem {
	char szKiller[MAX_PLAYER_NAME_LENGTH*2];
	char szVictim[MAX_PLAYER_NAME_LENGTH*2];
	char szAssister[MAX_PLAYER_NAME_LENGTH*2];
	int iId;	// the index number of the associated sprite (legacy path)
	int iKfWeapon;  // index into m_kfWeapon[], or -1
	bool bSuicide;
	bool bTeamKill;
	bool bNonPlayerKill;
	bool bLocal;    // local player is killer or victim -> red border
	bool bVictimIsLocalDeath; // local player is the victim -> dark red plate
	float flDisplayTime;
	float flSpawnTime;
	float *KillerColor;
	float *VictimColor;
	float *AssisterColor;
	int iHeadShotId;
	kf_row_mods mods;
};

#define MAX_DEATHNOTICES	5
static int DEATHNOTICE_DISPLAY_TIME = 6;

#define DEATHNOTICE_TOP		32

DeathNoticeItem rgDeathNoticeList[ MAX_DEATHNOTICES + 1 ];

// kf sprite handles (parallel to a private name list)
static HSPRITE  s_kfWeaponSpr[ KF_MAX_WEAPONS ];
static char     s_kfWeaponName[ KF_MAX_WEAPONS ][ 24 ];
static int      s_kfWeaponCount = 0;
static HSPRITE  s_kfModSpr[ KFI_COUNT ];
static bool     s_kfReady = false;

cvar_t *cl_killsound;
cvar_t *cl_killsound_path;

// ---- killfeed visual metrics (fractions of plate height H) ----
// measured against the real killfeed (see project notes). Plate height is
// derived from the console font so the feed scales with hud_scale / DPI.
#define KF_F_PADX      0.34f
#define KF_F_GAP       0.26f
#define KF_F_GAP_WING  0.05f
#define KF_F_RADIUS    0.13f
#define KF_F_BORDER    0.07f
#define KF_F_WING_DY  -0.20f
#define KF_F_PITCH     1.14f   // row-to-row spacing (plate + gap)
#define KF_ENTER_MS    180.0f
#define KF_EXIT_MS     220.0f

// Killfeed-specific team name colours, MEASURED from the approved reference
// (screenshot 1000312966.png glyph cores). The engine-wide g_ColorBlue/g_ColorRed
// are shared with chat/statusbar/scoreboard, so we keep our own here to avoid
// tinting the rest of the HUD. CT = steel blue, T = amber/gold.
static vec3_t s_kfColorCT = { 129.0f/255.0f, 154.0f/255.0f, 202.0f/255.0f };
static vec3_t s_kfColorT  = { 221.0f/255.0f, 195.0f/255.0f, 135.0f/255.0f };
static vec3_t s_kfColorGrey = { 0.8f, 0.8f, 0.8f };

// Team colour for the killfeed only (mirrors GetClientColor's team mapping).
static float *KF_TeamColor( int clientIndex )
{
	if( clientIndex <= 0 || clientIndex > MAX_PLAYERS )
		return s_kfColorGrey;
	switch( g_PlayerExtraInfo[clientIndex].teamnumber )
	{
	case TEAM_CT:        return s_kfColorCT;
	case TEAM_TERRORIST: return s_kfColorT;
	default:             return s_kfColorGrey;
	}
}

static inline int kf_min( int a, int b ) { return a < b ? a : b; }
static inline int kf_max( int a, int b ) { return a > b ? a : b; }

int CHudDeathNotice :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_DeathNotice, DeathMsg );

	hud_deathnotice_time = CVAR_CREATE( "hud_deathnotice_time", "6", FCVAR_ARCHIVE );
	cl_killsound = CVAR_CREATE( "cl_killsound", "0", FCVAR_ARCHIVE );
	cl_killsound_path = CVAR_CREATE( "cl_killsound_path", "buttons/bell1.wav", FCVAR_ARCHIVE );
	cl_killfeed = CVAR_CREATE( "cl_killfeed", "1", FCVAR_ARCHIVE );
	cl_killfeed_time = CVAR_CREATE( "cl_killfeed_time", "6", FCVAR_ARCHIVE );
	cl_killfeed_scale = CVAR_CREATE( "cl_killfeed_scale", "0.8", FCVAR_ARCHIVE );
	m_iFlags = 0;

	return 1;
}


void CHudDeathNotice :: InitHUDData( void )
{
	memset( rgDeathNoticeList, 0, sizeof(rgDeathNoticeList) );
	for( int i = 0; i < MAX_DEATHNOTICES + 1; i++ )
		rgDeathNoticeList[i].iKfWeapon = -1;
}

void CHudDeathNotice :: KF_LoadIcons( void )
{
	char path[64];
	int i;

	s_kfWeaponCount = 0;
	s_kfReady = false;

	for( i = 0; kf_weapons[i].msg && s_kfWeaponCount < KF_MAX_WEAPONS; i++ )
	{
		// de-dup by sprite file: several msg names share one sprite
		snprintf( path, sizeof(path), "sprites/kf/%s.spr", kf_weapons[i].spr );
		HSPRITE h = SPR_Load( path );
		strlcpy( s_kfWeaponName[s_kfWeaponCount], kf_weapons[i].msg,
				 sizeof(s_kfWeaponName[0]) );
		s_kfWeaponSpr[s_kfWeaponCount] = h;
		s_kfWeaponCount++;
	}

	for( i = 0; i < KFI_COUNT; i++ )
	{
		snprintf( path, sizeof(path), "sprites/kf/%s.spr", kf_mod_names[i] );
		s_kfModSpr[i] = SPR_Load( path );
	}

	// consider ready if at least the AWP + headshot loaded (proxy for the pack)
	int awp = KF_WeaponSprite( "awp" );
	s_kfReady = ( awp >= 0 && s_kfWeaponSpr[awp] != 0 && s_kfModSpr[KFI_HEADSHOT] != 0 );
}

int CHudDeathNotice :: KF_WeaponSprite( const char *killedwith )
{
	// killedwith is "d_<name>"; skip the d_ if present.
	const char *name = killedwith;
	if( name[0] == 'd' && name[1] == '_' )
		name += 2;

	for( int i = 0; i < s_kfWeaponCount; i++ )
	{
		if( !stricmp( s_kfWeaponName[i], name ) )
			return i;
	}
	return -1;
}

int CHudDeathNotice :: VidInit( void )
{
	m_HUD_d_skull = gHUD.GetSpriteIndex( "d_skull" );
	m_HUD_d_headshot = gHUD.GetSpriteIndex("d_headshot");

	KF_LoadIcons();

	return 1;
}

// ---- CS2 killfeed row drawing ------------------------------------------------

// scaled draw of a kf sprite: uses pfnSPR_DrawGeneric so the native-size SPR
// can be tinted (via SPR_Set) and blended additively at any on-screen size.
static void KF_DrawIcon( HSPRITE spr, int x, int y, int w, int h,
						 int r, int g, int b )
{
	wrect_t rc;
	if( !spr )
		return;
	rc.left = 0; rc.top = 0;
	rc.right = SPR_Width( spr, 0 );
	rc.bottom = SPR_Height( spr, 0 );

	SPR_Set( spr, r, g, b );
	// blendsrc GL_ONE(1), blenddst GL_ONE(1) -> additive, matches the white
	// premultiplied-over-black sprites we bake (see png2spr32.py).
	gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, &rc, 1, 1, w, h );
}

static inline int KF_IconW( HSPRITE spr, int h )
{
	int sw, sh;
	if( !spr ) return 0;
	sw = SPR_Width( spr, 0 );
	sh = SPR_Height( spr, 0 );
	if( sh <= 0 ) return h;
	return ( sw * h ) / sh; // preserve aspect, never stretch
}

// Draw a killfeed name. The engine console font is a fixed-size bitmap that we
// cannot make bold via a style flag, so we FAUX-BOLD it: draw the string twice
// with a 1px horizontal offset, which thickens every stroke and reproduces the
// heavy look of the reference without shipping a new font asset. Returns the
// end x of a single (un-offset) pass so callers advance by the true text width.
static int KF_DrawName( int x, int y, const char *name, float *rgb, float alpha )
{
	int endx;
	if( rgb )
		DrawUtils::SetConsoleTextColor( rgb[0]*alpha, rgb[1]*alpha, rgb[2]*alpha );
	endx = DrawUtils::DrawConsoleString( x, y, name );   // main pass, defines width
	if( rgb )
		DrawUtils::SetConsoleTextColor( rgb[0]*alpha, rgb[1]*alpha, rgb[2]*alpha );
	DrawUtils::DrawConsoleString( x + 1, y, name );       // +1px pass -> bold weight
	return endx;
}

// Measure the pixel width of one row at plate height H.
static int KF_RowWidth( const DeathNoticeItem *item, int H, int iconH, int modH,
						int gap, int gapWing, int padx )
{
	int w = padx;
	int j;
	bool first = true;

	#define KF_ADV(px) do { if(!first) w += gap; w += (px); first = false; } while(0)

	for( j = 0; j < item->mods.nPre; j++ )
		KF_ADV( KF_IconW( s_kfModSpr[item->mods.pre[j]], modH ) );

	if( !item->bSuicide && item->szKiller[0] )
		KF_ADV( DrawUtils::ConsoleStringLen( item->szKiller ) + 1 ); // +1 faux-bold

	if( item->mods.flashAssist && item->szAssister[0] )
	{
		KF_ADV( KF_IconW( s_kfModSpr[KFI_FLASHASSIST], modH ) );
		KF_ADV( DrawUtils::ConsoleStringLen( item->szAssister ) + 1 );
	}

	if( item->mods.wing >= 0 )
	{
		// wing sits just left of the weapon with a tiny gap
		if(!first) w += gap;
		w += KF_IconW( s_kfModSpr[item->mods.wing], modH );
		first = false;
		w += gapWing; // replaces the normal gap before the weapon
		w += ( item->iKfWeapon >= 0 ) ? KF_IconW( s_kfWeaponSpr[item->iKfWeapon], iconH ) : 0;
	}
	else
	{
		KF_ADV( ( item->iKfWeapon >= 0 ) ? KF_IconW( s_kfWeaponSpr[item->iKfWeapon], iconH ) : 0 );
	}

	for( j = 0; j < item->mods.nMid; j++ )
		KF_ADV( KF_IconW( s_kfModSpr[item->mods.mid[j]], modH ) );

	if( !item->bNonPlayerKill && item->szVictim[0] )
		KF_ADV( DrawUtils::ConsoleStringLen( item->szVictim ) + 1 ); // +1 faux-bold

	#undef KF_ADV
	w += padx;
	return w;
}

// Draw a rounded rectangle by filling an inner rect and clipping the corners
// with small notches (engine has no native rounded fill from the client).
static void KF_RoundedPlate( int x, int y, int w, int h, int radius,
							 int r, int g, int b, int a )
{
	if( radius * 2 > h ) radius = h / 2;
	if( radius * 2 > w ) radius = w / 2;
	// center band (full height)
	FillRGBABlend( x + radius, y, w - radius * 2, h, r, g, b, a );
	// left/right bands (reduced height for the rounded ends)
	FillRGBABlend( x, y + radius, radius, h - radius * 2, r, g, b, a );
	FillRGBABlend( x + w - radius, y + radius, radius, h - radius * 2, r, g, b, a );
	// diagonal step-fill of the four corners (cheap anti-alias-ish)
	for( int i = 0; i < radius; i++ )
	{
		int inset = radius - (int)( sqrtf( (float)(radius*radius - (radius-i)*(radius-i)) ) );
		FillRGBABlend( x + inset,             y + i,             radius - inset, 1, r, g, b, a );
		FillRGBABlend( x + w - radius,        y + i,             radius - inset, 1, r, g, b, a );
		FillRGBABlend( x + inset,             y + h - 1 - i,     radius - inset, 1, r, g, b, a );
		FillRGBABlend( x + w - radius,        y + h - 1 - i,     radius - inset, 1, r, g, b, a );
	}
}

static void KF_Border( int x, int y, int w, int h, int t, int r, int g, int b )
{
	FillRGBA( x, y, w, t, r, g, b, 255 );
	FillRGBA( x, y + h - t, w, t, r, g, b, 255 );
	FillRGBA( x, y, t, h, r, g, b, 255 );
	FillRGBA( x + w - t, y, t, h, r, g, b, 255 );
}

static int KF_DrawRow( DeathNoticeItem *item, int rightX, int topY, int H,
					   float alpha, int slideDx )
{
	// PROPORTIONS MEASURED IN PIXELS FROM THE APPROVED v5 REFERENCE.
	// Reference: plate 56px, bold name cap 22px, weapon icon 34px, modifier
	// (crossed-eye/event) 27px, pitch ~58px (~4px gap). Everything is keyed to
	// the engine console line height (TL) -- the ONE font we have and cannot
	// resize -- so the whole feed scales with the player-name size.
	// Ordering that matters: weapon(0.61*H) > modifier(0.50*H) > name-cap
	// (~0.39*H). Icons are a bit taller than the text but never huge.
	int TL    = gHUD.GetCharHeight();
	int iconH = kf_max( 8, (int)( H * 0.61f ) );   // weapon silhouette (largest)
	int modH  = kf_max( 8, (int)( H * 0.50f ) );   // modifier / event icon
	int gap   = kf_max( 2, (int)( TL * 0.34f ) );  // small tidy element gap
	int gapW  = kf_max( 1, (int)( TL * 0.06f ) );  // wing -> weapon
	int padx  = kf_max( 3, (int)( TL * 0.50f ) );  // inner L/R padding
	int radius = kf_max( 2, (int)( H * 0.16f ) );  // ~9px @ ref
	int border = kf_max( 2, (int)( H * 0.05f ) );  // 2-3px @ ref
	int j;

	int w = KF_RowWidth( item, H, iconH, modH, gap, gapW, padx );
	int x = rightX - w + slideDx;
	int y = topY;
	int cy = y + H / 2;
	int ty = cy - TL / 2;  // text baseline row (console string draws from top)

	// Translucent plate. MEASURED from the approved reference: the plate interior
	// reads ~(49,47,44) over a ~(28,24,20) backdrop, i.e. it is LIGHTER than the
	// scene behind it, not near-black. A dark plate (the previous 18,18,18 @ 0.63)
	// made rows sink into the background instead of reading as separate tiles.
	// A light grey at low alpha lifts the row over any backdrop while staying
	// see-through, which is what the reference does.
	int baseA = (int)( ( item->bLocal ? 120 : 105 ) * alpha );
	if( baseA < 4 ) return w;

	// plate: light grey for normal, red tint when the local player died
	int pr = 70, pg = 68, pb = 64;
	if( item->bLocal && item->bVictimIsLocalDeath )
		{ pr = 150; pg = 40; pb = 40; baseA = kf_min( 190, baseA + 45 ); }

	KF_RoundedPlate( x, y, w, H, radius, pr, pg, pb, baseA );
	if( item->bLocal )
		KF_Border( x, y, w, H, border, 255, 36, 36 );  // bright red outline

	int tint = (int)( 255 * alpha );
	int cx = x + padx;
	bool first = true;

	// pre-killer modifiers
	for( j = 0; j < item->mods.nPre; j++ )
	{
		HSPRITE s = s_kfModSpr[item->mods.pre[j]];
		int iw = KF_IconW( s, modH );
		if(!first) cx += gap; first = false;
		KF_DrawIcon( s, cx, cy - modH/2, iw, modH, tint, tint, tint );
		cx += iw;
	}

	// killer name
	if( !item->bSuicide && item->szKiller[0] )
	{
		if(!first) cx += gap; first = false;
		cx = KF_DrawName( cx, ty, item->szKiller, item->KillerColor, alpha );
	}

	// flash-assist icon + assister name
	if( item->mods.flashAssist && item->szAssister[0] )
	{
		HSPRITE s = s_kfModSpr[KFI_FLASHASSIST];
		int iw = KF_IconW( s, modH );
		if(!first) cx += gap; first = false;
		KF_DrawIcon( s, cx, cy - modH/2, iw, modH, tint, tint, tint );
		cx += iw + gap;
		cx = KF_DrawName( cx, ty, item->szAssister, item->AssisterColor, alpha );
	}

	// wing (raised) then weapon, or just weapon
	if( item->mods.wing >= 0 )
	{
		HSPRITE s = s_kfModSpr[item->mods.wing];
		int iw = KF_IconW( s, modH );
		if(!first) cx += gap; first = false;
		KF_DrawIcon( s, cx, cy - modH/2 + (int)( H * KF_F_WING_DY ), iw, modH, tint, tint, tint );
		cx += iw + gapW;
	}
	if( item->iKfWeapon >= 0 )
	{
		HSPRITE s = s_kfWeaponSpr[item->iKfWeapon];
		int iw = KF_IconW( s, iconH );
		if( !first && item->mods.wing < 0 ) cx += gap;
		first = false;
		KF_DrawIcon( s, cx, cy - iconH/2, iw, iconH, tint, tint, tint );
		cx += iw;
	}

	// mid modifiers
	for( j = 0; j < item->mods.nMid; j++ )
	{
		HSPRITE s = s_kfModSpr[item->mods.mid[j]];
		int iw = KF_IconW( s, modH );
		cx += gap;
		KF_DrawIcon( s, cx, cy - modH/2, iw, modH, tint, tint, tint );
		cx += iw;
	}

	// victim name
	if( !item->bNonPlayerKill && item->szVictim[0] )
	{
		cx += gap;
		KF_DrawName( cx, ty, item->szVictim, item->VictimColor, alpha );
	}

	return w;
}

int CHudDeathNotice :: Draw( float flTime )
{
	int x, y, r, g, b, i;

	bool useKf = ( cl_killfeed->value != 0.0f ) && s_kfReady;

	for( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;  // we've gone through them all

		if ( rgDeathNoticeList[i].flDisplayTime < flTime )
		{ // display time has expired -- but keep the row alive during fade-out
			if( useKf && ( flTime - rgDeathNoticeList[i].flDisplayTime ) < KF_EXIT_MS / 1000.0f )
			{
				// still fading; leave it in place
			}
			else
			{
				memmove( &rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES - i) );
				i--;
				continue;
			}
		}
		else
		{
			rgDeathNoticeList[i].flDisplayTime = min( rgDeathNoticeList[i].flDisplayTime, flTime + DEATHNOTICE_DISPLAY_TIME );
		}

		if( useKf )
		{
			// Plate height keyed to the console font line height so text fits
			// snugly; user scale lets phones bump it up/down. On a phone the
			// feed sits in the top-right corner and never spans the screen.
			float sc = cl_killfeed_scale->value;
			if( sc < 0.5f ) sc = 0.5f;
			if( sc > 3.0f ) sc = 3.0f;
			int TL = gHUD.GetCharHeight();
			// Plate height keyed to the ACTUAL console font (which sets the
			// name text size we cannot enlarge past native). Ref ratio H~1.4*TL
			// keeps names and icons at comparable visual weight (icon ~1.8x cap
			// height, exactly like screenshots #1/#4). Enlarge the whole block
			// with cl_killfeed_scale, never by shrinking the text.
			int H = (int)( TL * 1.80f * sc );
			if( H < 18 ) H = 18;
			int pitch = (int)( H * 1.07f );  // ref vertical rhythm (~4px gap)
			// top margin: a little down from the very top, right-aligned
			int marginTop = YRES( 8 );
			int marginRight = XRES( 6 );
			int topY = marginTop + i * pitch;

			float ageMs = ( flTime - rgDeathNoticeList[i].flSpawnTime ) * 1000.0f;
			float deathMs = ( flTime - rgDeathNoticeList[i].flDisplayTime ) * 1000.0f;
			float alpha, dx;
			kf_anim_state( ageMs, deathMs, KF_ENTER_MS, KF_EXIT_MS, (float)( H * 1.4f ), &alpha, &dx );

			KF_DrawRow( &rgDeathNoticeList[i], ScreenWidth - marginRight, topY, H, alpha, (int)dx );
			continue;
		}

		// -------- legacy valve death notice (cl_killfeed 0 / no sprites) -----
		{
			if( !g_iUser1 )
				y = YRES(DEATHNOTICE_TOP) + 2 + (20 * i);
			else
				y = ScreenHeight / 5 + 2 + (20 * i);

			int id = (rgDeathNoticeList[i].iId == -1) ? m_HUD_d_skull : rgDeathNoticeList[i].iId;
			x = ScreenWidth - DrawUtils::ConsoleStringLen(rgDeathNoticeList[i].szVictim) - (gHUD.GetSpriteRect(id).right - gHUD.GetSpriteRect(id).left);
			if( rgDeathNoticeList[i].iHeadShotId )
				x -= (gHUD.GetSpriteRect(m_HUD_d_headshot).right - gHUD.GetSpriteRect(m_HUD_d_headshot).left);

			if ( !rgDeathNoticeList[i].bSuicide )
			{
				x -= (5 + DrawUtils::ConsoleStringLen( rgDeathNoticeList[i].szKiller ) );
				if ( rgDeathNoticeList[i].KillerColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].KillerColor[0], rgDeathNoticeList[i].KillerColor[1], rgDeathNoticeList[i].KillerColor[2] );
				x = 5 + DrawUtils::DrawConsoleString( x, y, rgDeathNoticeList[i].szKiller );
			}

			r = 255;  g = 80;	b = 0;
			if ( rgDeathNoticeList[i].bTeamKill )
			{ r = 10;	g = 240; b = 10; }

			SPR_Set( gHUD.GetSprite(id), r, g, b );
			SPR_DrawAdditive( 0, x, y, &gHUD.GetSpriteRect(id) );
			x += (gHUD.GetSpriteRect(id).right - gHUD.GetSpriteRect(id).left);

			if( rgDeathNoticeList[i].iHeadShotId)
			{
				SPR_Set( gHUD.GetSprite(m_HUD_d_headshot), r, g, b );
				SPR_DrawAdditive( 0, x, y, &gHUD.GetSpriteRect(m_HUD_d_headshot));
				x += (gHUD.GetSpriteRect(m_HUD_d_headshot).right - gHUD.GetSpriteRect(m_HUD_d_headshot).left);
			}

			if (!rgDeathNoticeList[i].bNonPlayerKill)
			{
				if ( rgDeathNoticeList[i].VictimColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].VictimColor[0], rgDeathNoticeList[i].VictimColor[1], rgDeathNoticeList[i].VictimColor[2] );
				x = DrawUtils::DrawConsoleString( x, y, rgDeathNoticeList[i].szVictim );
			}
		}
	}

	if( i == 0 )
		m_iFlags &= ~HUD_DRAW; // disable hud item

	return 1;
}

// This message handler may be better off elsewhere
int CHudDeathNotice :: MsgFunc_DeathMsg( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );

	int killer = reader.ReadByte();
	int victim = reader.ReadByte();
	int headshot = reader.ReadByte();

	char killedwith[32];
	strlcpy( killedwith, "d_", sizeof( killedwith ) );
	strlcat( killedwith, reader.ReadString(), sizeof( killedwith ) );

	// Optional extended payload (ReGameDLL): flags long, [position], [assister], [rarity long]
	int deathFlags = 0, assister = 0, rarity = 0;
	if( reader.Valid() )
	{
		deathFlags = reader.ReadLong();
		if( deathFlags & 0x001 ) // PLAYERDEATH_POSITION
		{
			reader.ReadCoord(); reader.ReadCoord(); reader.ReadCoord();
		}
		if( deathFlags & 0x002 ) // PLAYERDEATH_ASSISTANT
			assister = reader.ReadByte();
		if( deathFlags & 0x004 ) // PLAYERDEATH_KILLRARITY
			rarity = reader.ReadLong();
	}

	gHUD.m_Scoreboard.DeathMsg( killer, victim );
	gHUD.m_Spectator.DeathMessage(victim);

	int i;
	for ( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;
	}
	if ( i == MAX_DEATHNOTICES )
	{
		memmove( rgDeathNoticeList, rgDeathNoticeList+1, sizeof(DeathNoticeItem) * MAX_DEATHNOTICES );
		i = MAX_DEATHNOTICES - 1;
	}

	// clear slot
	memset( &rgDeathNoticeList[i], 0, sizeof(DeathNoticeItem) );
	rgDeathNoticeList[i].iKfWeapon = -1;

	gHUD.m_Scoreboard.GetAllPlayersInfo();

	// Killer
	const char *killer_name = NULL;
	bool killer_this_player = false;
	if ( killer >= 1 && killer <= MAX_PLAYERS )
	{
		killer_name = g_PlayerInfoList[killer].name;
		killer_this_player = g_PlayerInfoList[killer].thisplayer;
	}
	if ( !killer_name )
	{
		killer_name = "";
		rgDeathNoticeList[i].szKiller[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].KillerColor = KF_TeamColor( killer );
		strlcpy( rgDeathNoticeList[i].szKiller, killer_name, sizeof( rgDeathNoticeList[i].szKiller ) );
	}

	// Victim
	const char *victim_name = NULL;
	bool victim_this_player = false;
	if ( victim >= 1 && victim <= MAX_PLAYERS )
	{
		victim_name = g_PlayerInfoList[ victim ].name;
		victim_this_player = g_PlayerInfoList[ victim ].thisplayer;
	}
	if ( !victim_name )
	{
		victim_name = "";
		rgDeathNoticeList[i].szVictim[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].VictimColor = KF_TeamColor( victim );
		strlcpy( rgDeathNoticeList[i].szVictim, victim_name, sizeof( rgDeathNoticeList[i].szVictim ) );
	}

	// Assister
	if( assister >= 1 && assister <= MAX_PLAYERS && g_PlayerInfoList[assister].name )
	{
		rgDeathNoticeList[i].AssisterColor = KF_TeamColor( assister );
		strlcpy( rgDeathNoticeList[i].szAssister, g_PlayerInfoList[assister].name, sizeof( rgDeathNoticeList[i].szAssister ) );
	}

	if( victim == 255 )
	{
		rgDeathNoticeList[i].bNonPlayerKill = true;
		strlcpy( rgDeathNoticeList[i].szVictim, killedwith+2, sizeof( rgDeathNoticeList[i].szVictim ) );
	}
	else
	{
		if ( killer == victim || killer == 0 )
			rgDeathNoticeList[i].bSuicide = true;
		if ( !strncmp( killedwith, "d_teammate", sizeof(killedwith)  ) )
			rgDeathNoticeList[i].bTeamKill = true;
	}

	rgDeathNoticeList[i].iHeadShotId = headshot;
	rgDeathNoticeList[i].bLocal = ( killer_this_player || victim_this_player || g_iUser2 == killer || g_iUser2 == victim );
	rgDeathNoticeList[i].bVictimIsLocalDeath = victim_this_player;

	// legacy sprite
	rgDeathNoticeList[i].iId = gHUD.GetSpriteIndex( killedwith );
	// kf weapon sprite
	rgDeathNoticeList[i].iKfWeapon = KF_WeaponSprite( killedwith );
	// decode modifiers (works with legacy headshot byte alone too)
	kf_decode_modifiers( rarity, headshot, &rgDeathNoticeList[i].mods );

	rgDeathNoticeList[i].flSpawnTime = gHUD.m_flTime;
	rgDeathNoticeList[i].flDisplayTime = gHUD.m_flTime +
		( cl_killfeed->value ? cl_killfeed_time->value : hud_deathnotice_time->value );

	// Play kill sound
	if ((killer_this_player || g_iUser2 == killer) &&
		!rgDeathNoticeList[i].bNonPlayerKill &&
		!rgDeathNoticeList[i].bSuicide &&
		cl_killsound->value > 0.0f)
	{
		PlaySound(cl_killsound_path->string, cl_killsound->value);
	}

	// console log (unchanged)
	if (rgDeathNoticeList[i].bNonPlayerKill)
	{
		ConsolePrint( rgDeathNoticeList[i].szKiller );
		ConsolePrint( " killed a " );
		ConsolePrint( rgDeathNoticeList[i].szVictim );
		ConsolePrint( "\n" );
	}
	else
	{
		if ( rgDeathNoticeList[i].bSuicide )
		{
			ConsolePrint( rgDeathNoticeList[i].szVictim );
			if ( !strncmp( killedwith, "d_world", sizeof(killedwith)  ) )
				ConsolePrint( " died" );
			else
				ConsolePrint( " killed self" );
		}
		else if ( rgDeathNoticeList[i].bTeamKill )
		{
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed his teammate " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}
		else
		{
			if( headshot )
				ConsolePrint( "*** ");
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}

		if ( *killedwith && (*killedwith > 13 ) && strncmp( killedwith, "d_world", sizeof(killedwith) ) && !rgDeathNoticeList[i].bTeamKill )
		{
			if ( headshot )
				ConsolePrint(" with a headshot from ");
			else
				ConsolePrint(" with ");
			ConsolePrint( killedwith+2 );
		}

		if( headshot ) ConsolePrint( " ***");
		ConsolePrint( "\n" );
	}

	return 1;
}
