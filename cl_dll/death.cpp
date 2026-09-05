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
// Names are GoldClient's own d_*.tga stems (see scripts/kf_tga2spr.py).
static const char *kf_mod_names[KFI_COUNT] =
{
	"blind_kill",   // KFI_BLIND
	"inair_kill",   // KFI_INAIR
	"noscope",      // KFI_NOSCOPE
	"smoke_kill",   // KFI_SMOKE
	"penetrate",    // KFI_PENETRATE
	"headshot",     // KFI_HEADSHOT
	"plus",         // KFI_PLUS
	"assist_flash", // KFI_FLASHASSIST
	"domination",   // KFI_DOMINATION
	"revenge"       // KFI_REVENGE
};

// Weapon icon table. VERIFIED against the ReGameDLL submodule vendored here
// (3rdparty/ReGameDLL_CS @ 5.20.0.492-367-g7be9d59), not guessed:
//
//   * the msg name is what CBasePlayer::GetKillerWeaponName() produces
//     (player.cpp:824): either m_pActiveItem->pszName() or the inflictor's
//     classname, with a leading "weapon_" / "monster_" / "func_" stripped.
//   * the real classnames are the LINK_ENTITY_TO_CLASS(weapon_*) list:
//     ak47 aug awp c4 deagle elite famas fiveseven flashbang g3sg1 galil
//     glock18 hegrenade knife m249 m3 m4a1 mac10 mp5navy p228 p90 scout sg550
//     sg552 shield smokegrenade tmp ump45 usp xm1014
//     Note it is "glock18", never "glock": the short form only appears in
//     weapontype.cpp's buy-alias table, which never reaches DeathMsg.
//   * "world" is GetKillerWeaponName's default when there is no client killer,
//     so a fall or a map hazard arrives under that name.
//   * a thrown grenade's projectile is LINK_ENTITY_TO_CLASS(grenade, ...) with
//     MAKE_STRING_CLASS("grenade", pev), so an HE kill can arrive as either
//     "hegrenade" (the weapon was the inflictor) or "grenade" (the projectile
//     was). Both are mapped.
//
// The sprites are GoldClient's own killfeed set (scripts/kf_tga2spr.py), whose
// stems are the CS 1.6 weapon names. A name that is not in this table yields
// sprite -1 and the row simply draws without a weapon icon (KF_BuildRow skips
// it), so an unknown weapon degrades instead of breaking the layout.
struct kf_weapon_map { const char *msg; const char *spr; };
static const kf_weapon_map kf_weapons[] =
{
	{ "ak47", "ak47" }, { "aug", "aug" }, { "awp", "awp" },
	{ "deagle", "deagle" }, { "elite", "elite" }, { "famas", "famas" },
	{ "fiveseven", "fiveseven" }, { "g3sg1", "g3sg1" }, { "galil", "galil" },
	{ "glock18", "glock18" },
	{ "m249", "m249" }, { "m4a1", "m4a1" }, { "mac10", "mac10" },
	{ "mp5navy", "mp5navy" }, { "p90", "p90" },
	{ "p228", "p228" }, { "scout", "scout" },
	{ "sg550", "sg550" }, { "sg552", "sg552" },
	{ "tmp", "tmp" }, { "ump45", "ump45" }, { "usp", "usp" },
	{ "xm1014", "xm1014" }, { "m3", "m3" },
	// grenades: HE uses the grenade sprite, smoke kills get their own icon
	{ "hegrenade", "grenade" }, { "grenade", "grenade" },
	{ "flashbang", "flashbang" }, { "smokegrenade", "smoke_kill" },
	// NOTE: weapontype.h also enumerates WEAPON_C4, but neither GoldClient's
	// asset set nor ours ships a c4 killfeed icon, so a C4 kill deliberately has
	// no weapon icon rather than a broken sprite handle.
	{ "knife", "knife" },
	// Suicide / environmental death. The engine sends "world" for a fall or a
	// map hazard; the legacy path draws d_skull for it, so ours must too or the
	// row degenerates to a bare victim name with no icon at all.
	{ "world", "skull" }, { "worldspawn", "skull" },
	// NOTE: weapon_c4 and weapon_shield exist as entities but do not appear
	// here. C4 kills come from the explosion, whose inflictor is the "grenade"
	// projectile, and the shield deals no damage. Neither GoldClient nor we ship
	// a c4/shield killfeed icon, so mapping them would point at a missing
	// sprite.
	//
	// NOTE: GoldClient's asset set also carries d_tripmine / d_sentrygun /
	// d_tracktrain / d_infection / d_snowball / d_inferno icons. Those are
	// Half-Life / mod entities that this game's ReGameDLL never links (checked
	// the LINK_ENTITY_TO_CLASS list), so they are not mapped either.
	//
	// d_inferno IS what the reference's row 2 shows (silhouette match: MAE
	// 0.185 vs 0.247 for the runner-up), so GoldClient does use it -- but it
	// belongs to a mod that has a fire damage type, which this game does not.
	{ NULL, NULL }
};

#define KF_MAX_WEAPONS  40

struct DeathNoticeItem {
	char szKiller[MAX_PLAYER_NAME_LENGTH*2];
	char szVictim[MAX_PLAYER_NAME_LENGTH*2];
	char szAssister[MAX_PLAYER_NAME_LENGTH*2];
	int iId;	// the index number of the associated sprite (legacy path)
	bool bUsed;	// slot occupancy -- see the note in MsgFunc_DeathMsg
	int iKfWeapon;  // index into m_kfWeapon[], or -1
	bool bSuicide;
	bool bTeamKill;
	bool bNonPlayerKill;
	bool bLocal;    // local player is killer or victim -> red border
	float flDisplayTime;
	float *KillerColor;
	float *VictimColor;
	float *AssisterColor;
	int iHeadShotId;
	kf_row_mods mods;
};

// MAX_DEATHNOTICES lives in hud.h; the array itself (rgDeathNoticeList) is here.
//
// A legacy `static int DEATHNOTICE_DISPLAY_TIME = 6` used to sit here and the
// draw loop re-clamped every live row to (flTime + 6) on EVERY frame. That
// silently truncated any cl_killfeed_time / hud_deathnotice_time above 6: the
// cvar took the value and the feed ignored it. It protected nothing --
// flDisplayTime is set once from gHUD.m_flTime when the message arrives, and
// expired rows are evicted by the branch right above -- so it is gone.
// Pinned by tests/test_killfeed_slots.c (test_long_lifetime_is_not_capped).

#define DEATHNOTICE_TOP		32

DeathNoticeItem rgDeathNoticeList[ MAX_DEATHNOTICES + 1 ];

// kf sprite handles (parallel to a private name list)
static HSPRITE  s_kfWeaponSpr[ KF_MAX_WEAPONS ];
static char     s_kfWeaponName[ KF_MAX_WEAPONS ][ 24 ];
static int      s_kfWeaponCount = 0;
static HSPRITE  s_kfModSpr[ KFI_COUNT ];
static bool     s_kfReady = false;
// Index into s_kfWeaponSpr[] of the skull, used when the server names a killer
// this table does not know. See KF_LoadIcons.
static int      s_kfFallbackWeapon = -1;

cvar_t *cl_killsound;
cvar_t *cl_killsound_path;

// ---- killfeed console customisation ----------------------------------------
// EVERY size cvar here is a MULTIPLIER on the one scale (see killfeed_layout.h),
// never an absolute pixel count: an absolute pixel cvar would be a second base
// and would break proportions on some device, which is the bug class this whole
// file was restructured to prevent. Colours are "R G B" strings, 0-255.
//
// File scope, not members of CHud: only the helpers in this file read them.
static cvar_t *cl_killfeed;             // 1 = GoldClient killfeed, 0 = classic
static cvar_t *cl_killfeed_time;        // seconds a row stays before fading
static cvar_t *cl_killfeed_scale;       // overall size multiplier
static cvar_t *cl_killfeed_x;           // right-edge inset multiplier
static cvar_t *cl_killfeed_y;           // top inset multiplier
static cvar_t *cl_killfeed_rows;        // max rows drawn at once
static cvar_t *cl_killfeed_plate;       // 0 = no backing plate
static cvar_t *cl_killfeed_plate_color; // "R G B"
static cvar_t *cl_killfeed_plate_alpha; // 0..255
static cvar_t *cl_killfeed_corner;      // corner radius multiplier (0 = square)
static cvar_t *cl_killfeed_outline;     // local border thickness multiplier
static cvar_t *cl_killfeed_ct_color;    // "R G B"
static cvar_t *cl_killfeed_t_color;     // "R G B"
static cvar_t *cl_killfeed_icon_color;  // "R G B"
static cvar_t *cl_killfeed_bold;        // 1 = faux-bold names

// ---- killfeed geometry ------------------------------------------------------
// The layout math lives in killfeed_layout.h (kf_scale / kf_row_height /
// kf_row_alpha). The GEOMETRY constants there are measured from the reference
// frame and cross-checked against GoldClient's client.dll; see
// shared/bloom-client/GOLDCLIENT_KILLFEED.md.
//
// Model in one line: the TEXT cell and the icons come from ONE scale, and the
// plate wraps the finished row (see killfeed_layout.h).
//
// KF_EXIT_MS is NOT measured. GoldClient has an hud_deathnotice_fade toggle
// (0x101F9E58) but the disassembly notes do not record its duration, and a
// still frame cannot show it. 220ms is a chosen value, short enough not to hold
// an expired row visibly. Calling it reverse-engineered would be a lie.
#define KF_EXIT_MS     220.0f  // fade-out duration on expiry (chosen, not measured)

// Killfeed name colours: CT = steel blue, T = amber/gold. MEASURED from the
// reference the user approved (screenshot 1000312966.png), glyph cores only, so
// antialiasing toward the plate does not drag the value.
// The engine-wide g_ColorBlue/g_ColorRed are shared with chat/statusbar/
// scoreboard, so the killfeed keeps its own copies here and leaves the rest of
// the HUD untinted.
//
// NOT const: KF_RowStyle() refreshes them from the colour cvars every frame.
// Rows hold POINTERS to these (assigned when the message arrives), so refreshing
// in place re-colours rows that are already on screen -- otherwise a colour
// change would only affect kills that happen afterwards.
static vec3_t s_kfColorCT   = { 129.0f/255.0f, 154.0f/255.0f, 202.0f/255.0f };
static vec3_t s_kfColorT    = { 221.0f/255.0f, 195.0f/255.0f, 135.0f/255.0f };
static vec3_t s_kfColorGrey = { 204.0f/255.0f, 204.0f/255.0f, 204.0f/255.0f };

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


int CHudDeathNotice :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_DeathNotice, DeathMsg );

	hud_deathnotice_time = CVAR_CREATE( "hud_deathnotice_time", "6", FCVAR_ARCHIVE );
	cl_killsound = CVAR_CREATE( "cl_killsound", "0", FCVAR_ARCHIVE );
	cl_killsound_path = CVAR_CREATE( "cl_killsound_path", "buttons/bell1.wav", FCVAR_ARCHIVE );

	// Killfeed. Defaults reproduce the measured GoldClient reference at 1080p.
	// Sizes are MULTIPLIERS on the one scale, never absolute pixels -- that way
	// a user tweak cannot re-introduce a second base and break the proportions.
	cl_killfeed             = CVAR_CREATE( "cl_killfeed",             "1", FCVAR_ARCHIVE );
	cl_killfeed_time        = CVAR_CREATE( "cl_killfeed_time",        "6", FCVAR_ARCHIVE );
	cl_killfeed_scale       = CVAR_CREATE( "cl_killfeed_scale",       "1", FCVAR_ARCHIVE );
	cl_killfeed_x           = CVAR_CREATE( "cl_killfeed_x",           "1", FCVAR_ARCHIVE );
	cl_killfeed_y           = CVAR_CREATE( "cl_killfeed_y",           "1", FCVAR_ARCHIVE );
	cl_killfeed_rows        = CVAR_CREATE( "cl_killfeed_rows",        "6", FCVAR_ARCHIVE );
	cl_killfeed_plate       = CVAR_CREATE( "cl_killfeed_plate",       "1", FCVAR_ARCHIVE );
	cl_killfeed_plate_color = CVAR_CREATE( "cl_killfeed_plate_color", "46 43 42", FCVAR_ARCHIVE );
	cl_killfeed_plate_alpha = CVAR_CREATE( "cl_killfeed_plate_alpha", "136", FCVAR_ARCHIVE );
	cl_killfeed_corner      = CVAR_CREATE( "cl_killfeed_corner",      "1", FCVAR_ARCHIVE );
	cl_killfeed_outline     = CVAR_CREATE( "cl_killfeed_outline",     "1", FCVAR_ARCHIVE );
	cl_killfeed_ct_color    = CVAR_CREATE( "cl_killfeed_ct_color",    "129 154 202", FCVAR_ARCHIVE );
	cl_killfeed_t_color     = CVAR_CREATE( "cl_killfeed_t_color",     "221 195 135", FCVAR_ARCHIVE );
	cl_killfeed_icon_color  = CVAR_CREATE( "cl_killfeed_icon_color",  "204 204 204", FCVAR_ARCHIVE );
	cl_killfeed_bold        = CVAR_CREATE( "cl_killfeed_bold",        "1", FCVAR_ARCHIVE );
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
		// Several msg names deliberately share one sprite file (hegrenade and
		// grenade both use grenade.spr; world and worldspawn both use skull.spr).
		// Each gets its own entry so the name lookup stays a flat scan; the
		// engine's SPR_Load caches by filename, so the duplicate call does not
		// load the file twice.
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

	// Fallback icon for a killer name this table does not carry.
	//
	// The server does NOT only send weapon names. GetKillerWeaponName()
	// (ReGameDLL player.cpp:824-880) returns the inflictor's classname with the
	// weapon_/monster_/func_ prefix STRIPPED, so a map entity that deals damage
	// arrives here under its bare name: func_door -> "door" (doors.cpp:765,
	// DMG_CRUSH on a blocked door), func_breakable -> "breakable"
	// (func_break.cpp:507), monster_mortar -> "mortar", plus the func_train /
	// func_rotating / func_guntarget family in bmodels.cpp and plats.cpp.
	//
	// None of those can sensibly be mapped to a weapon icon, and there are too
	// many to enumerate. The legacy path already handles this: it substitutes
	// d_skull whenever GetSpriteIndex() fails (see the `iId == -1` test in
	// Draw()). Without the same fallback the kf path drew such a row with NO
	// icon at all -- just two names with a gap where the icon belongs.
	s_kfFallbackWeapon = KF_WeaponSprite( "world" );
	if( s_kfFallbackWeapon >= 0 && s_kfWeaponSpr[s_kfFallbackWeapon] == 0 )
		s_kfFallbackWeapon = -1;   /* skull.spr missing -> no fallback */
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
	// blendsrc GL_ONE(1), blenddst GL_ONE(1) -> additive. The sprites are
	// GoldClient's own killfeed art converted by scripts/kf_tga2spr.py, which
	// bakes the TGA's alpha shape into white-on-black SPR32 texels, so additive
	// blending reproduces them exactly.
	gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, &rc, 1, 1, w, h );
}

// On-screen size of one kf sprite. ONE scale applies to every icon in the feed
// (see killfeed_layout.h): the texture is drawn at its own size times the shared
// row scale, so the relative sizes the artist drew survive. MEASURED: the '+'
// glue (12x12 texture) stays small next to combat icons (32x32), which rules out
// per-sprite normalisation to the font height.
static void KF_IconWH( HSPRITE spr, const kf_metrics *m, int raised,
					   int *w, int *h )
{
	int nw, nh;
	*w = 0; *h = 0;
	if( !spr )
		return;
	nw = SPR_Width( spr, 0 );
	nh = SPR_Height( spr, 0 );
	// The airborne wing has its own measured ratio and is not clamped to the
	// text cell -- it overhangs the plate on purpose. See killfeed_layout.h.
	*h = raised ? kf_wing_height( nh, m->scale )
				: kf_icon_height( nh, m->scale, m->textH );
	*w = kf_icon_width( nw, nh, *h );
}

// ---- text: the killfeed must be able to SIZE its text, not just place it ----
//
// The engine HUD font is a fixed raster (engine/client/cl_scrn.c: charHeight is
// the raster height times hud_fontscale, FCVAR_LATCH, default 1.0). It does NOT
// change with resolution. Drawing killfeed text through the plain console path
// therefore pins text to one size while icons scale -- the two bases drift and
// the plate, sized by max(text, icons), changes shape on every device. That was
// the root defect.
//
// A scaled path DOES exist: DrawUtils::DrawHudString(..., scale) ->
// TextMessageDrawChar -> gMobileAPI.pfnDrawScaledCharacter. The ONLY thing it
// needs is a mobile API (g_iMobileAPIVersion != 0, set in cdll_int.cpp's
// HUD_MobilityInterface).
//
// This deliberately does NOT test hud_textmode. An earlier version did, and that
// was a real bug: hud_textmode only picks which implementation the WRAPPER
// DrawUtils::DrawConsoleString forwards to (draw_util.h) -- and the killfeed
// never calls that wrapper, KF_DrawName calls DrawHudString directly. With the
// extra condition the feed fell into the unscaled branch on the DEFAULT config
// (hud_textmode is created as "0" in hud.cpp), so the text stayed at the raw
// font size and cl_killfeed_scale appeared to move only the icons.
//
// Verified against the engine binary rather than assumed: pfnDrawScaledCharacter
// (libxash.so vaddr 0x1757bc; R E segment is file = vaddr - 0x4000) contains
// exactly two comparisons -- an fcmp on the scale and one integer cmp -- and
// reads no cvar at all. And CL_GetScreenInfo (0x16e9d4) reloads the font through
// CL_FreeFont + SCR_LoadCreditsFont, i.e. iCharHeight describes the SAME
// creditsFont that the scaled draw renders, so measure and draw agree here.
//
// When the mobile API is absent (desktop build) the feed does not fake scaling:
// kf_row_metrics() keys the whole row to the font's own height instead, so
// everything still derives from one base.
static bool KF_TextScalable( void )
{
	return g_iMobileAPIVersion != 0;
}

// Width of a killfeed string at the row's text scale.
static int KF_TextWidth( const char *str, float textScale )
{
	if( !str || !str[0] )
		return 0;
	if( KF_TextScalable() )
		return DrawUtils::HudStringLen( str, textScale );
	return DrawUtils::ConsoleStringLen( str );
}

// Draw a killfeed name at the row's text scale. The engine console font cannot
// be made bold via a style flag, so we FAUX-BOLD it: draw twice with a 1px
// horizontal offset, which thickens every stroke and reproduces the heavy look
// of the reference without shipping a new font asset.
static void KF_DrawName( int x, int y, const char *name, float *rgb,
						 float alpha, float textScale, int bold )
{
	int r = 255, g = 255, b = 255;
	if( rgb )
	{
		r = (int)( rgb[0] * alpha * 255.0f );
		g = (int)( rgb[1] * alpha * 255.0f );
		b = (int)( rgb[2] * alpha * 255.0f );
	}
	if( KF_TextScalable() )
	{
		// iMaxX 0 = no clipping; the row was measured with the same scale.
		DrawUtils::DrawHudString( x, y, 0, name, r, g, b, textScale );
		if( bold )
			DrawUtils::DrawHudString( x + 1, y, 0, name, r, g, b, textScale );
		return;
	}
	if( rgb )
		DrawUtils::SetConsoleTextColor( rgb[0]*alpha, rgb[1]*alpha, rgb[2]*alpha );
	DrawUtils::DrawConsoleString( x, y, name );
	if( bold )
	{
		if( rgb )
			DrawUtils::SetConsoleTextColor( rgb[0]*alpha, rgb[1]*alpha, rgb[2]*alpha );
		DrawUtils::DrawConsoleString( x + 1, y, name );
	}
}

// Parse an "R G B" cvar string into clamped channels. A malformed or empty
// string leaves the fallback untouched, so a typo in config.cfg cannot make the
// feed invisible.
//
// WHY THREE CHANNELS AND NOT FOUR. GoldClient's equivalent cvar takes RGBA
// ("Override RGBA colors for icons into killfeed.", registered at client.dll
// 0x100016cc, object 0x101f9c00) because it paints through vgui. This build
// cannot: icons go out via pfnSPR_DrawGeneric after SPR_Set(spr, r, g, b), and
// the engine's SPR_Set has no alpha argument at all -- verified in the header,
// engine/APIProxy.h:225:
//     typedef void (*pfnEngSrc_pfnSPR_Set_t)( HSPRITE hPic, int r, int g, int b );
//
// Nothing is lost. The blend is additive (GL_ONE/GL_ONE, see KF_DrawIcon), where
// scaling RGB IS scaling opacity -- the contribution to the frame is linear in
// the colour. That is exactly how the row fade works:
//     tint = 255 * rowAlpha;  icon drawn at (iconR*tint/255, ...)
// A user-supplied constant alpha on top of that would multiply into the fade and
// make the two indistinguishable, so the fourth channel would be actively
// harmful here rather than merely absent.
//
// Mirrored by tests/test_killfeed_cvars.c (death.cpp cannot be compiled on the
// host -- it needs the engine). Change this and change that.
static void KF_ParseColor( const char *s, int *r, int *g, int *b )
{
	int v[3];
	if( !s || !s[0] )
		return;
	if( sscanf( s, "%d %d %d", &v[0], &v[1], &v[2] ) != 3 )
		return;
	*r = kf_clamp_i( v[0], 0, 255 );
	*g = kf_clamp_i( v[1], 0, 255 );
	*b = kf_clamp_i( v[2], 0, 255 );
}

// Height of the text CELL the killfeed must reserve, in the font that will
// actually draw the glyphs. Getting this from the wrong font is not cosmetic:
// the plate is sized from it, so the names would either overflow the plate or
// float inside it.
//
// The engine has TWO independent HUD fonts, and which one draws depends on
// hud_textmode:
//   hud_textmode 0 (DEFAULT) -> DrawConsoleString -> pfnDrawConsoleString ->
//       Con_GetFont(con_fontsize) = con.chars[N]. THREE different fonts, picked
//       by resolution (engine/client/console.c Con_LoadConchars: <=640 -> font0,
//       >=1280 -> font2, else font1).
//   hud_textmode != 0        -> DrawHudString -> pfnDrawCharacter ->
//       cls.creditsFont, which is what gHUD.GetCharHeight() reports.
//
// So GetCharHeight() is the RIGHT answer only when the scalable path is actually
// taken. The condition MUST be the same one KF_TextScalable() uses -- checking
// only hud_textmode would report the creditsFont's height on a non-mobile build,
// where the draw call still goes to DrawConsoleString. That is exactly the
// "measure one font, draw another" bug this function exists to prevent.
static int KF_TextCellHeight( void )
{
	int h;
	if( KF_TextScalable() )
	{
		h = gHUD.GetCharHeight();
	}
	else
	{
		int w = 0;
		h = 0;
		// "M" is only a probe; the height does not depend on the string.
		DrawUtils::ConsoleStringSize( "M", &w, &h );
	}
	// The engine returns 0 before the fonts are loaded (first VidInit) and could
	// in principle fail to load a font at all. A zero cell would collapse every
	// row, so fall back to the reference cell instead of drawing garbage.
	if( h < 1 )
		h = (int)KF_REF_TEXT_H;
	return h;
}


// Build the per-frame metric bundle: ONE scale for the whole feed.
//
// When the font cannot be scaled (see KF_TextScalable) we do not let icons run
// away from the text: the scale is re-derived so the text cell equals the font's
// natural height. The feed then tracks the font instead of the resolution, but
// it stays internally proportional -- which is the property that was broken.
//
// NOTE ON UNITS: ScreenHeight here is the engine's VIRTUAL height. With
// hud_scale set to a target width (any value >= 320, e.g. "1600") the engine
// reports a virtual viewport and upscales every draw call itself -- verified in
// CL_GetScreenInfo (libxash.so 0x16ea58 compares hud_scale to 320.0f, 0x16eaac
// divides the physical width by it, 0x16eb18 stores physicalHeight/factor as
// scrInfo.iHeight). Since the killfeed draws in that same virtual space, keying
// the scale to ScreenHeight is correct and needs no m_flScale correction. Mixing
// in TrueHeight here would double-count the engine's upscale.
static void KF_RowMetrics( float userScale, kf_metrics *m )
{
	int fontRasterH = KF_TextCellHeight();

	userScale = kf_clamp_f( userScale, 0.25f, 4.0f );

	if( KF_TextScalable() )
		kf_compute_metrics( ScreenHeight, userScale, fontRasterH, m );
	else
		kf_compute_metrics_for_font( fontRasterH, userScale, m );

	// User offsets for the feed position. Multipliers, so the inset keeps its
	// proportion to everything else instead of becoming a fixed pixel margin.
	m->marginX = kf_px( (float)m->marginX,
						kf_clamp_f( cl_killfeed_x->value, 0.0f, 20.0f ) );
	m->marginY = kf_px( (float)m->marginY,
						kf_clamp_f( cl_killfeed_y->value, 0.0f, 20.0f ) );
	if( m->marginX < 0 ) m->marginX = 0;
	if( m->marginY < 0 ) m->marginY = 0;
	// A cranked multiplier must not push the feed off-screen: at x 20 the inset
	// would be ~400px, which is most of a narrow viewport. Cap each inset at a
	// quarter of the screen so the feed stays visible whatever the user types.
	// (KF_DrawRow separately clamps a row that measures wider than the screen.)
	if( m->marginX > ScreenWidth / 4 )  m->marginX = ScreenWidth / 4;
	if( m->marginY > ScreenHeight / 4 ) m->marginY = ScreenHeight / 4;
}

// Read the console-tunable appearance once per frame.
static void KF_RowStyle( kf_style *st )
{
	st->plate      = cl_killfeed_plate->value != 0.0f;
	st->plateR = 46; st->plateG = 43; st->plateB = 42;
	KF_ParseColor( cl_killfeed_plate_color->string,
				   &st->plateR, &st->plateG, &st->plateB );
	st->plateAlpha = kf_clamp_i( (int)cl_killfeed_plate_alpha->value, 0, 255 );

	st->cornerScale100  = kf_clamp_i( (int)( cl_killfeed_corner->value * 100.0f ), 0, 400 );
	st->outlineScale100 = kf_clamp_i( (int)( cl_killfeed_outline->value * 100.0f ), 0, 400 );
	st->bold = cl_killfeed_bold->value != 0.0f;

	st->ctR = 129; st->ctG = 154; st->ctB = 202;
	KF_ParseColor( cl_killfeed_ct_color->string, &st->ctR, &st->ctG, &st->ctB );
	st->tR = 221; st->tG = 195; st->tB = 135;
	KF_ParseColor( cl_killfeed_t_color->string, &st->tR, &st->tG, &st->tB );
	st->iconR = 204; st->iconG = 204; st->iconB = 204;
	KF_ParseColor( cl_killfeed_icon_color->string,
				   &st->iconR, &st->iconG, &st->iconB );

	// Push the name colours into the shared vec3_t's the rows point at, so a
	// colour change takes effect on rows ALREADY on screen, not just future ones.
	s_kfColorCT[0] = st->ctR / 255.0f;
	s_kfColorCT[1] = st->ctG / 255.0f;
	s_kfColorCT[2] = st->ctB / 255.0f;
	s_kfColorT[0]  = st->tR / 255.0f;
	s_kfColorT[1]  = st->tG / 255.0f;
	s_kfColorT[2]  = st->tB / 255.0f;
	s_kfColorGrey[0] = st->iconR / 255.0f;
	s_kfColorGrey[1] = st->iconG / 255.0f;
	s_kfColorGrey[2] = st->iconB / 255.0f;

	st->maxRows = kf_clamp_i( (int)cl_killfeed_rows->value, 1, MAX_DEATHNOTICES );
}

// Engine-side draw payload, parallel to the kf_elem array: what to actually
// blit for each measured element.
typedef struct
{
	HSPRITE     spr;    // sprite handle, 0 for a text element
	const char *text;   // string, NULL for an icon element
	float      *color;  // text colour (NULL for icons)
} kf_draw;

// Measure one row into the shared element list, in draw order. This is the ONE
// place the element sequence is written down -- kf_layout_row() then assigns x
// for both measuring and drawing, so the two can no longer drift apart.
// Returns the element count.
static int KF_BuildRow( const DeathNoticeItem *item, const kf_metrics *m,
						const kf_style *st, kf_elem *el, kf_draw *dr )
{
	int n = 0;
	int j;

	// append an icon element
	#define KF_PUSH_ICON( sprite, tight, lift ) do { \
		if( n < KF_MAX_ELEMS ) { \
			HSPRITE _s = (sprite); \
			KF_IconWH( _s, m, (lift), &el[n].w, &el[n].h ); \
			el[n].tightGap = (tight); el[n].raised = (lift); \
			dr[n].spr = _s; dr[n].text = NULL; dr[n].color = NULL; \
			n++; \
		} \
	} while(0)

	// append a text element (+1px only when faux-bold widens the string)
	#define KF_PUSH_TEXT( str, rgb ) do { \
		if( n < KF_MAX_ELEMS ) { \
			el[n].w = KF_TextWidth( str, m->textScale ) + ( st->bold ? 1 : 0 ); \
			el[n].h = m->textH; \
			el[n].tightGap = 0; \
			el[n].raised = 0; \
			dr[n].spr = 0; dr[n].text = (str); dr[n].color = (rgb); \
			n++; \
		} \
	} while(0)

	for( j = 0; j < item->mods.nPre; j++ )
		KF_PUSH_ICON( s_kfModSpr[item->mods.pre[j]], 0, 0 );

	if( !item->bSuicide && item->szKiller[0] )
		KF_PUSH_TEXT( item->szKiller, item->KillerColor );

	// Assist. VERIFIED against ReGameDLL: the server sends the assister index
	// (PLAYERDEATH_ASSISTANT) INDEPENDENTLY of whether the help was a flash
	// (KILLRARITY_ASSISTEDFLASH). So the row must show an assist whenever there
	// is an assister; the flash icon is an EXTRA that only appears for a flash
	// assist. Gating the whole block on flashAssist hid every ordinary assist.
	//
	// MEASURED on the native reference (row 6): a '+' glyph sits between the
	// killer name and the assister, and for a flash assist the flash icon sits
	// between the '+' and the name -- "killer + [flash] assister". Silhouette
	// match by alpha channel: assist_flash 0.159 at x1659 (20x15). The '+' is
	// its own 12x12 sprite, drawn at the shared icon scale, so it stays visibly
	// smaller than combat icons.
	if( item->szAssister[0] )
	{
		KF_PUSH_ICON( s_kfModSpr[KFI_PLUS], 0, 0 );
		if( item->mods.flashAssist )
			KF_PUSH_ICON( s_kfModSpr[KFI_FLASHASSIST], 0, 0 );
		KF_PUSH_TEXT( item->szAssister, item->AssisterColor );
	}

	// the wing (airborne kill) is raised and hugs the weapon with a tight gap
	if( item->mods.wing >= 0 )
		KF_PUSH_ICON( s_kfModSpr[item->mods.wing], 0, 1 );

	if( item->iKfWeapon >= 0 )
		KF_PUSH_ICON( s_kfWeaponSpr[item->iKfWeapon], item->mods.wing >= 0, 0 );

	for( j = 0; j < item->mods.nMid; j++ )
		KF_PUSH_ICON( s_kfModSpr[item->mods.mid[j]], 0, 0 );

	if( !item->bNonPlayerKill && item->szVictim[0] )
		KF_PUSH_TEXT( item->szVictim, item->VictimColor );

	#undef KF_PUSH_ICON
	#undef KF_PUSH_TEXT
	return n;
}

// Filled plate with rounded corners. GoldClient's style 3 draws the plate with
// d_panel_corner sprites; the reference shows a ~5px quarter-round at each
// corner (MEASURED: the right edge ramps 643->648 over 4 rows at the top). We
// approximate the same curve by insetting the first `radius` scanlines top and
// bottom, which reproduces the measured ramp without shipping the corner sprite.
static void KF_FilledPlate( int x, int y, int w, int h,
							int r, int g, int b, int a, int radius )
{
	int i;
	if( radius < 1 || radius * 2 >= h || radius * 2 >= w )
	{
		FillRGBABlend( x, y, w, h, r, g, b, a );
		return;
	}
	// middle block (full width) between the rounded caps
	FillRGBABlend( x, y + radius, w, h - radius * 2, r, g, b, a );
	// top and bottom caps: each scanline inset by a quarter-circle amount
	for( i = 0; i < radius; i++ )
	{
		// horizontal inset for this scanline of the corner arc
		int dx = radius - (int)( sqrtf( (float)( radius * radius
					- ( radius - 1 - i ) * ( radius - 1 - i ) ) ) + 0.5f );
		int rowW = w - dx * 2;
		if( rowW <= 0 )
			continue;
		FillRGBABlend( x + dx, y + i,             rowW, 1, r, g, b, a ); // top
		FillRGBABlend( x + dx, y + h - 1 - i,     rowW, 1, r, g, b, a ); // bottom
	}
}

// Outline for the local player's row. Thickness t, colour rgba.
//
// The border STRADDLES the plate edge -- it is not inside it, nor outside it.
// See kf_border_overhang() in killfeed_layout.h for the measurement that settles
// this (centroids of the two red bands are 32.98px apart, and a plain plate is
// exactly 33px).
//
// The caller is responsible for reserving the overhang vertically (KF_DrawRow
// insets the plate by it), so this function only draws.
static void KF_Border( int x, int y, int w, int h, int t,
					   int r, int g, int b, int a )
{
	int out = kf_border_overhang( t );
	int ox = x - out, oy = y - out;
	int ow = w + out * 2, oh = h + out * 2;

	FillRGBABlend( ox, oy, ow, t, r, g, b, a );              // top
	FillRGBABlend( ox, oy + oh - t, ow, t, r, g, b, a );     // bottom
	FillRGBABlend( ox, oy, t, oh, r, g, b, a );              // left
	FillRGBABlend( ox + ow - t, oy, t, oh, r, g, b, a );     // right
}

// Draw one row, right-aligned at rightX with its top at topY.
// Returns the VERTICAL SPACE the row occupied so the caller can advance to the
// next one -- which is NOT always the plate height: an outlined row reserves
// rowH + 2*overhang because its border straddles the plate edge (see
// kf_border_overhang in killfeed_layout.h). The row width is reported through
// *outW for callers that need it.
//
// GEOMETRY: text drives everything (see killfeed_layout.h). The row is as tall
// as the taller of the font and the biggest icon, and the plate wraps that.
//
// ONE SCALE. Every length in a row comes from kf_metrics, which is derived from
// kf_scale(ScreenHeight, cl_killfeed_scale) -- see killfeed_layout.h for why the
// console font height is NOT the base (it does not change with resolution, so
// text and icons used to drift apart and the plate changed shape per device).
//
// The MODEL is GoldClient's: text drives the row, the plate wraps the finished
// row, row height = max(text cell, tallest icon), one shared icon scale.
static int KF_DrawRow( DeathNoticeItem *item, int rightX, int topY,
					   const kf_metrics *m, const kf_style *st,
					   float alpha, int *outW )
{
	kf_elem el[KF_MAX_ELEMS];
	kf_draw dr[KF_MAX_ELEMS];
	int n, i, w, x, rowH, contentH, border = 0, overhang = 0, plateY, advance;

	n = KF_BuildRow( item, m, st, el, dr );
	w = kf_layout_row( el, n, m->gap, m->gapTight, m->padx );

	// Nothing visible -> draw nothing, and tell the caller not to advance.
	//
	// kf_layout_row returns padx*2 for an empty element list, so without this
	// the row would paint a bare 26px plate with no content in it. That is
	// reachable: the server sends killer 0 for a non-player kill
	// (multiplay_gamerules.cpp:5391), which sets bSuicide and suppresses the
	// killer name; if the victim's userinfo has not arrived yet szVictim is
	// empty too, and if the fallback skull sprite failed to load there is no
	// weapon icon either. The legacy path cannot hit this because it always
	// blits d_skull.
	if( kf_visible_count( el, n ) == 0 )
	{
		if( outW ) *outW = 0;
		return 0;
	}

	// Row height: the taller of the text cell and the biggest icon, plus padding.
	contentH = kf_row_height( m->textH, kf_tallest( el, n ) );
	rowH = contentH + m->pady * 2;
	if( outW ) *outW = w;

	// An outlined row's border overhangs the plate, so the PLATE is inset by the
	// overhang and the row reserves that space on both sides. Without this the
	// gap above an outlined row shrank to vgap - overhang (1px instead of 3px at
	// the reference) while every other gap stayed vgap -- the outlined row looked
	// glued to the row above it.
	if( item->bLocal && st->outlineScale100 > 0 && st->plate )
	{
		border = kf_border_thickness( ( m->outline * st->outlineScale100 ) / 100,
									  m->vgap );
		overhang = kf_border_overhang( border );
	}
	plateY = topY + overhang;
	advance = rowH + overhang * 2;

	int cy = plateY + rowH / 2;
	int ty = cy - m->textH / 2;   // console strings draw from the top
	// Right-aligned, but never past the left screen edge. A long name pair on a
	// narrow viewport can measure wider than the screen; without this the plate
	// would start at a negative x and the killer's name would be cut off outside
	// the window instead of the row simply touching the left edge.
	x = rightX - w;
	if( x < 0 )
		x = 0;

	// Backing plate. GoldClient's BgColor is (46,43,42) at alpha 136 -- a faint
	// lift over the scene (measured +6..+9 luminance), NOT a heavy grey tile.
	//
	// The early-out is on the ROW's fade, not on the plate's opacity: keying it
	// to the plate meant `cl_killfeed_plate_alpha 0` (or plate 0) hid the names
	// and icons too, since the function returned before drawing them.
	int baseA = (int)( st->plateAlpha * alpha );
	if( alpha <= 0.0f )
		return advance;

	if( st->plate && baseA >= 4 )
	{
		int corner = ( m->corner * st->cornerScale100 ) / 100;
		KF_FilledPlate( x, plateY, w, rowH,
						st->plateR, st->plateG, st->plateB, baseA, corner );

		// Local player's row gets the outline. Scheme OutlineFgColor (238,23,23),
		// imm32 0xff1717ee at client.dll 0x10061484, thickness 3 at the reference.
		//
		// CONFIRMED IN PIXELS 2026-09-05: the outlined row on the native 1080p
		// frame (y201..y237), sampled inside the feed only (x1700..1909), has a
		// band core of (191,36,34) -- 50.0 from this colour, 223.8 from
		// GoldClient's blue HighlightBgColorKill2. Red is certain; which red
		// (OutlineFgColor vs Dead2 240,45,45, one point apart) cannot be resolved
		// from a 3px band in a JPEG. See the note in kf_ratios.py for the four
		// highlight colours this build deliberately does not draw.
		if( border > 0 )
		{
			int oa = (int)( 255 * alpha );
			KF_Border( x, plateY, w, rowH, border, 238, 23, 23, oa );
		}
	}

	int tint = (int)( 255 * alpha );
	for( i = 0; i < n; i++ )
	{
		if( el[i].w <= 0 )
			continue;
		if( dr[i].text )
		{
			KF_DrawName( x + el[i].x, ty, dr[i].text, dr[i].color, alpha,
						 m->textScale, st->bold );
		}
		else
		{
			// The airborne "wing" icon is lifted above the row centre and
			// overhangs the plate; everything else is centred. kf_elem_y() owns
			// that rule.
			int iy = kf_elem_y( &el[i], cy, m->raise );
			KF_DrawIcon( dr[i].spr, x + el[i].x, iy, el[i].w, el[i].h,
						 ( st->iconR * tint ) / 255,
						 ( st->iconG * tint ) / 255,
						 ( st->iconB * tint ) / 255 );
		}
	}

	return advance;
}

int CHudDeathNotice :: Draw( float flTime )
{
	int x, y, r, g, b, i;

	bool useKf = ( cl_killfeed->value != 0.0f ) && s_kfReady;

	// ONE SCALE, computed once per frame. Every length in every row comes from
	// this bundle; nothing downstream may invent its own base.
	kf_metrics kfM;
	kf_style   kfS;
	int kfY = 0;      // running top edge for the accumulating kf stack
	int kfDrawn = 0;  // rows drawn so far, against cl_killfeed_rows
	if( useKf )
	{
		KF_RowMetrics( cl_killfeed_scale->value, &kfM );
		KF_RowStyle( &kfS );
		kfY = kfM.marginY;

		// Spectator mode paints an opaque black bar across the whole top of the
		// screen, INT_YPOS(2) = 20% of ScreenHeight tall (hud/spectator_gui.cpp
		// "silly black bars"). At the normal top margin the feed would be drawn
		// underneath it and be invisible. The legacy notice path already dodged
		// this with ScreenHeight/5; do the same here instead of re-discovering
		// the bug in-game.
		if( g_iUser1 )
		{
			int specTop = ScreenHeight / 5 + kfM.marginY;
			if( kfY < specTop )
				kfY = specTop;
		}
	}

	for( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( !rgDeathNoticeList[i].bUsed )
			break;  // we've gone through them all

		if ( rgDeathNoticeList[i].flDisplayTime < flTime )
		{ // display time has expired -- but keep the row alive during fade-out
			if( useKf && ( flTime - rgDeathNoticeList[i].flDisplayTime ) < KF_EXIT_MS / 1000.0f )
			{
				// still fading; leave it in place
			}
			else
			{
				// Shift the rest down. The array has MAX_DEATHNOTICES + 1 slots
				// and the extra tail slot is zeroed in InitHUDData and never
				// filled, so it acts as a permanent "not used" terminator that
				// gets shifted in here.
				memmove( &rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES - i) );
				i--;
				continue;
			}
		}
		// No `else` clamp here: the row keeps the lifetime it was given when the
		// message arrived. See the note at DEATHNOTICE_TOP above.

		if( useKf )
		{
			// ONE SCALE for the whole feed, computed once per frame (see
			// KF_RowMetrics / killfeed_layout.h). Rows stack by ACCUMULATING the
			// advance each row reports, not on a fixed grid: kfY carries the
			// running total. It is a plain local because the loop already walks
			// rows in draw order.
			//
			// For a given scale every PLAIN row advances by the same amount --
			// kf_icon_height() clamps icon boxes to the text cell and kf_tallest()
			// skips the raised wing, so contentH can never exceed textH (verified
			// mechanically across 480..2160px and scale 0.45..4.0). The one row
			// that advances differently is the local player's outlined row, which
			// reserves the border overhang on both sides. Accumulating rather than
			// multiplying is what keeps the visible GAPS equal in that case.
			float deathMs = ( flTime - rgDeathNoticeList[i].flDisplayTime ) * 1000.0f;
			float alpha = kf_row_alpha( deathMs, KF_EXIT_MS );

			// cl_killfeed_rows caps how many are DRAWN. The entries stay in the
			// list and keep expiring on schedule, so lowering the cap does not
			// strand rows that would then pop in later.
			//
			// A row that is fading out still counts against the cap: it is still
			// occupying its place on screen, so releasing its slot early would
			// draw the next row ON TOP of it. With the default cap (5) and a
			// 5-entry buffer the cap never bites; at a lower cap a new row waits
			// out the 220ms fade, which is the correct behaviour.
			if( kfDrawn >= kfS.maxRows )
				continue;

			// A row with no visible content advances by 0 (see KF_DrawRow); it
			// must not consume a cap slot nor leave a vgap-sized hole in the
			// stack, so only count and advance when something was painted.
			int adv = KF_DrawRow( &rgDeathNoticeList[i],
								  ScreenWidth - kfM.marginX, kfY,
								  &kfM, &kfS, alpha, NULL );
			if( adv > 0 )
			{
				kfDrawn++;
				kfY += adv + kfM.vgap;
			}
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

	// Optional extended payload (ReGameDLL): flags long, [position], [assister],
	// [rarity long]. VERIFIED against the vendored submodule
	// (regamedll/dlls/multiplay_gamerules.cpp, SendDeathMessage): the server
	// writes the flags long ONLY when iDeathMessageFlags > 0, and each optional
	// block only when its bit is set. Flag values are the PLAYERDEATH_* enum in
	// regamedll/dlls/gamerules.h.
	//
	// Reading is safe even on a truncated packet: BufferReader::Read() bounds
	// checks and latches m_bBad, after which every read returns -1 and Valid()
	// is false. So a malformed payload yields rarity 0 (no modifier icons)
	// rather than garbage.
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

		// A truncated or malformed payload must not turn into modifier icons.
		if( reader.Bad() )
		{
			assister = 0;
			rarity = 0;
		}
	}

	gHUD.m_Scoreboard.DeathMsg( killer, victim );
	gHUD.m_Spectator.DeathMessage(victim);

	// Find a free slot.
	//
	// This used to test `iId == 0`, i.e. it treated "no legacy sprite index" as
	// "slot empty". But iId comes from gHUD.GetSpriteIndex(), whose return value
	// is a VALID index into the HUD sprite list -- and 0 is a perfectly valid
	// index (it means the weapon's d_* sprite happens to be the first entry in
	// hud.txt, which is a data file a mod can reorder). A row that landed on
	// index 0 would then be treated as an empty slot: the next kill would
	// overwrite it, and the DRAW loop would stop at it and hide every row after.
	// GetSpriteIndex returns -1 when the sprite is missing, not 0, so the old
	// test was not even guarding what it looked like it was guarding.
	//
	// An explicit occupancy flag has no such collision, and it also decouples the
	// list from the legacy sprite entirely -- the kf path never looks at iId.
	int i;
	for ( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( !rgDeathNoticeList[i].bUsed )
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
		kf_sanitise_name( killer_name, rgDeathNoticeList[i].szKiller, sizeof( rgDeathNoticeList[i].szKiller ) );
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
		kf_sanitise_name( victim_name, rgDeathNoticeList[i].szVictim, sizeof( rgDeathNoticeList[i].szVictim ) );
	}

	// Assister
	if( assister >= 1 && assister <= MAX_PLAYERS && g_PlayerInfoList[assister].name )
	{
		rgDeathNoticeList[i].AssisterColor = KF_TeamColor( assister );
		kf_sanitise_name( g_PlayerInfoList[assister].name, rgDeathNoticeList[i].szAssister, sizeof( rgDeathNoticeList[i].szAssister ) );
	}

	if( victim == 255 )
	{
		rgDeathNoticeList[i].bNonPlayerKill = true;
		kf_sanitise_name( killedwith+2, rgDeathNoticeList[i].szVictim, sizeof( rgDeathNoticeList[i].szVictim ) );
	}
	else
	{
		if ( killer == victim || killer == 0 )
			rgDeathNoticeList[i].bSuicide = true;
		// Team kill: derive it from the TEAM NUMBERS, not from the weapon string.
		//
		// The old test was `!strncmp( killedwith, "d_teammate", ... )`. It could
		// never fire in a real game: killedwith is "d_" + whatever the server
		// wrote, and the server writes GetKillerWeaponName() (ReGameDLL
		// player.cpp:824), which returns a weapon classname or "world". Grepping
		// the whole vendored ReGameDLL for a weapon named "teammate" finds
		// nothing -- "d_teammate" is a HUD SPRITE name from sprites/hud.txt.
		//
		// Teams come from g_PlayerExtraInfo, kept up to date by the TeamInfo
		// message. teamnumber 0 means "not assigned yet", so it must not count as
		// a match, otherwise two unassigned players would look like team mates.
		else if( killer >= 1 && killer <= MAX_PLAYERS &&
				 victim >= 1 && victim <= MAX_PLAYERS &&
				 g_PlayerExtraInfo[killer].teamnumber != 0 &&
				 g_PlayerExtraInfo[killer].teamnumber ==
				 g_PlayerExtraInfo[victim].teamnumber )
			rgDeathNoticeList[i].bTeamKill = true;
	}

	rgDeathNoticeList[i].iHeadShotId = headshot;
	rgDeathNoticeList[i].bLocal = ( killer_this_player || victim_this_player || g_iUser2 == killer || g_iUser2 == victim );

	// legacy sprite
	rgDeathNoticeList[i].iId = gHUD.GetSpriteIndex( killedwith );
	// kf weapon sprite. An unknown name falls back to the skull rather than
	// leaving the row iconless -- see the note in KF_LoadIcons.
	rgDeathNoticeList[i].iKfWeapon = KF_WeaponSprite( killedwith );
	if( rgDeathNoticeList[i].iKfWeapon < 0 )
		rgDeathNoticeList[i].iKfWeapon = s_kfFallbackWeapon;
	// decode modifiers (works with legacy headshot byte alone too)
	kf_decode_modifiers( rarity, headshot, &rgDeathNoticeList[i].mods );

	rgDeathNoticeList[i].flDisplayTime = gHUD.m_flTime +
		( cl_killfeed->value ? cl_killfeed_time->value : hud_deathnotice_time->value );

	// The row is fully populated -- claim the slot. Set LAST so a partially
	// built entry is never visible to the draw loop.
	rgDeathNoticeList[i].bUsed = true;

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
