/*
*
*    This program is free software; you can redistribute it and/or modify it
*    under the terms of the GNU General Public License as published by the
*    Free Software Foundation; either version 2 of the License, or (at
*    your option) any later version.
*
*    This program is distributed in the hope that it will be useful, but
*    WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*    General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*    In addition, as a special exception, the author gives permission to
*    link the code of this program with the Half-Life Game Engine ("HL
*    Engine") and Modified Game Libraries ("MODs") developed by Valve,
*    L.L.C ("Valve").  You must obey the GNU General Public License in all
*    respects for all of the code used other than the HL Engine and MODs
*    from Valve.  If you modify this file, you may extend this exception
*    to your version of the file, but you are not obligated to do so.  If
*    you do not wish to do so, delete this exception statement from your
*    version.
*
*/
#pragma once
#ifndef DRAW_UTIL_H
#define DRAW_UTIL_H
// Drawing primitives

#define DHN_DRAWZERO 1
#define DHN_2DIGITS  2
#define DHN_3DIGITS  4

extern int g_codepage;
extern qboolean g_accept_utf8;

extern cvar_t *con_charset;
extern cvar_t *cl_charset;


int Con_UtfProcessChar( int in );
int Con_UtfProcessCharForce( int in );

class DrawUtils
{
public:
	static int DrawHudNumber(int x, int y, int iFlags, int iNumber,
						 int r, int g, int b );

	static int DrawHudNumber2( int x, int y, bool DrawZero, int iDigits, int iNumber,
						   int r, int g, int b);

	static int DrawHudNumber2( int x, int y, int iNumber,
						   int r, int g, int b);

	static int DrawHudString(int x, int y, int iMaxX, const char *szString,
						 int r, int g, int b, float scale = 0.0f );

	static int DrawHudStringReverse( int xpos, int ypos, int iMinX, const char *szString,
								 int r, int g, int b, float scale = 0.0f );

	static inline int DrawHudNumberString( int xpos, int ypos, int iMinX, int iNumber,
								int r, int g, int b, float scale = 0.0f )
	{
		char szString[16];
		snprintf( szString, sizeof(szString), "%d", iNumber );
		return DrawHudStringReverse( xpos, ypos, iMinX, szString, r, g, b, scale );
	}

	static int HudStringLen( const char *szIt, float scale = 1 );

	// legacy shit came with Valve
	static inline int GetNumWidth(int iNumber, int iFlags)
	{
		if ( iFlags & ( DHN_3DIGITS ) )
			return 3;

		if ( iFlags & ( DHN_2DIGITS ) )
			return 2;

		if ( iNumber <= 0 )
			return iFlags & DHN_DRAWZERO ? 1 : 0;

		if ( iNumber < 10 )
			return 1;

		if ( iNumber < 100 )
			return 2;

		return 3;
	}

	static inline int DrawConsoleString(int x, int y, const char *string)
	{
		if ( gHUD.hud_textmode->value )
		{
			int ret  = DrawHudString( x, y, 9999, (char *)string, color[0] * 255, color[1] * 255, color[2] * 255 );
			color[0] = color[1] = color[2] = 1.0f;
			return ret;
		}
		else
			return gEngfuncs.pfnDrawConsoleString( x, y, (char *)string );
	}

	static inline void SetConsoleTextColor( float r, float g, float b )
	{
		if ( gHUD.hud_textmode->value )
			color[0] = r, color[1] = g, color[2] = b;
		else
			gEngfuncs.pfnDrawSetTextColor( r, g, b );
	}

	static inline void SetConsoleTextColor( unsigned char r, unsigned char g, unsigned char b )
	{
		if ( gHUD.hud_textmode->value )
			color[0] = r / 255.0f, color[1] = g / 255.0f, color[2] = b / 255.0f;
		else
			gEngfuncs.pfnDrawSetTextColor( r / 255.0f, g / 255.0f, b / 255.0f );
	}

	static inline int ConsoleStringLen(  const char *szIt )
	{
		if ( gHUD.hud_textmode->value )
		{
			return HudStringLen( (char *)szIt );
		}
		else
		{
			int _width;
			int _height;

			gEngfuncs.pfnDrawConsoleStringLen( szIt, &_width, &_height );
			return _width;
		}
	}

	static inline void ConsoleStringSize( const char *szIt, int *width, int *height )
	{
		if ( gHUD.hud_textmode->value )
			*height = 13, *width = HudStringLen( (char *)szIt );
		else
			gEngfuncs.pfnDrawConsoleStringLen( szIt, width, height );
	}

	// Both engine entry points return the cursor advance in the SAME units the
	// x/y arguments use, so neither may be rescaled here.
	//
	// Traced through the engine (DragonSlayer, engine/client/cl_font.c:196-251):
	//   CL_DrawCharacter() returns font->charWidths[number] -- untouched by
	//   SPR_AdjustSize(), which only converts the quad geometry (x,y,w,h) from
	//   HUD-logical to physical pixels. cl_mobile.c:63-77 builds g_scaled_font
	//   by multiplying charWidths[] AND scale by `scale`, then calls the very
	//   same CL_DrawCharacter. So the scaled path already returns
	//   base_charWidth * scale, i.e. a logical advance, exactly like the
	//   unscaled pfnDrawCharacter path right below.
	//
	// The old code divided the scaled advance by gHUD.m_flScale
	// (= TrueWidth / ScreenWidth, see hud.cpp). That is a second correction on
	// a value that never needed the first one, and m_flScale is >= 1 by
	// construction (CL_GetScreenInfo only ever shrinks scrInfo.iWidth below
	// refState.width), so it could only ever UNDER-advance the cursor:
	// glyphs crawled on top of each other while HudStringLen() -- which does
	// not divide -- kept reserving the full width, leaving a dead gap at the
	// end of every scaled string. Invisible on desktop where m_flScale == 1,
	// clearly visible on a phone with hud_scale set.
	//
	// Arrived here from upstream merge 63f346f (PR #399), not from killfeed
	// work. Only the killfeed passes a non-zero scale today (MOTD, scoreboard
	// and message.cpp all take the `else` branch), so this fix is confined to
	// the scaled path.
	static inline int TextMessageDrawChar( int x, int y, int number, int r, int g, int b, float scale = 0.0f )
	{
		int ret;
		if( scale && g_iMobileAPIVersion )
			ret = gMobileAPI.pfnDrawScaledCharacter( x, y, number, r, g, b, scale );
		else
			ret = gEngfuncs.pfnDrawCharacter( x, y, number, r, g, b );
		return ret;
	}

	static inline void UnpackRGB( int &r, int &g, int &b, const unsigned long ulRGB )
	{
		r = (ulRGB & 0xFF0000) >>16;
		g = (ulRGB & 0xFF00) >> 8;
		b = ulRGB & 0xFF;
	}

	static inline void ScaleColors( int &r, int &g, int &b, const int a )
	{
		r *= a / 255.0f;
		g *= a / 255.0f;
		b *= a / 255.0f;
	}

	static inline void DrawRectangle( int x, int y, int wide, int tall,
						   int r = 0, int g = 0, int b = 0, int a = 153,
						   bool drawStroke = true )
	{
		FillRGBABlend( x, y, wide, tall, r, g, b, a );
		if ( drawStroke )
		{
			// TODO: remove this hardcoded hardcore
			FillRGBA( x + 1,        y,            wide - 1, 1,        255, 140, 0, 255 );
			FillRGBA( x,            y,            1,        tall - 1, 255, 140, 0, 255 );
			FillRGBA( x + wide - 1, y + 1,        1,        tall - 1, 255, 140, 0, 255 );
			FillRGBA( x,            y + tall - 1, wide - 1, 1,        255, 140, 0, 255 );
		}
	}

	// Same panel, but the stroke colour is caller-supplied and the border is
	// inset by cornerRadius at both ends of every edge, which leaves the corners
	// open instead of drawing a hard 90-degree join. Used by the scoreboard so a
	// themed install can colour its frame from ClientScheme.res.
	static inline void DrawRectangleExt( int x, int y, int wide, int tall,
						   int r = 0, int g = 0, int b = 0, int a = 153,
						   int stroke_r = 255, int stroke_g = 140, int stroke_b = 0,
						   bool drawStroke = true, int cornerRadius = 2 )
	{
		FillRGBABlend( x, y, wide, tall, r, g, b, a );
		if ( drawStroke )
		{
			FillRGBA( x + cornerRadius, y,                wide - (cornerRadius * 2), 1,                        stroke_r, stroke_g, stroke_b, 255 );
			FillRGBA( x,                y + cornerRadius, 1,                         tall - (cornerRadius * 2), stroke_r, stroke_g, stroke_b, 255 );
			FillRGBA( x + wide - 1,     y + cornerRadius, 1,                         tall - (cornerRadius * 2), stroke_r, stroke_g, stroke_b, 255 );
			FillRGBA( x + cornerRadius, y + tall - 1,     wide - (cornerRadius * 2), 1,                        stroke_r, stroke_g, stroke_b, 255 );
		}
	}

	static void Draw2DQuad( float x1, float y1, float x2, float y2 );
	static void DrawStretchPic( float x, float y, float w, float h,
								float s1 = 0, float t1 = 0, float s2 = 1, float t2 = 1);


private:
	// console string color
	static float color[3];
};

#endif // DRAW_UTIL_H
