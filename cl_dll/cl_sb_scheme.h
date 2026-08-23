/*
cl_sb_scheme.h - CS 1.6 VGUI scheme (ClientScheme.res) reader for the Bloom
                 scoreboard. See cl_sb_scheme.cpp for the rationale.

The board reads resource/ClientScheme.res and uses it as the source of colours,
exactly like the original CS 1.6 scoreboard: team1/team2/team0 for player-name
colours, ListBG for the panel fill, SelectionBG for the local player's row,
BaseText/BrightBaseText for text and headers. Anything the file omits keeps the
board's compiled-in default, so a missing or broken file never blacks the board
out.
*/
#ifndef CL_SB_SCHEME_H
#define CL_SB_SCHEME_H

#ifdef __cplusplus
extern "C" {
#endif

#define SB_SCHEME_NAME_MAX  64

// Which keys were present. A missing key keeps the consumer's own default
// rather than reading as 0 0 0.
#define SB_SCHEME_HAS_BG           ( 1 << 0 )
#define SB_SCHEME_HAS_SELECTED_BG  ( 1 << 1 )
#define SB_SCHEME_HAS_HEADER_TEXT  ( 1 << 2 )
#define SB_SCHEME_HAS_TEXT         ( 1 << 3 )
#define SB_SCHEME_HAS_DIVIDER      ( 1 << 4 )
#define SB_SCHEME_HAS_TEAM0        ( 1 << 5 )
#define SB_SCHEME_HAS_TEAM1        ( 1 << 6 )
#define SB_SCHEME_HAS_TEAM2        ( 1 << 7 )

typedef struct
{
	unsigned int  have;             // SB_SCHEME_HAS_* bitmask
	unsigned char bg[4];            // ListBG              -> panel fill
	unsigned char selected_bg[4];   // SelectionBG         -> local player row
	unsigned char header_text[4];   // BrightBaseText      -> column headers / server name
	unsigned char text[4];          // BaseText            -> generic text
	unsigned char divider[4];       // BorderDark          -> separator line
	unsigned char team0[4];         // team0               -> spectators / unassigned
	unsigned char team1[4];         // team1               -> Terrorists (teamnumber 1)
	unsigned char team2[4];         // team2               -> Counter-Terrorists (teamnumber 2)
} sb_scheme_t;

// Parse a VGUI scheme held in memory. `text` is NUL-terminated and not modified.
// Returns the number of keys resolved; `out` is zeroed first, so a failed parse
// leaves have == 0 and every consumer falls back to its own default.
int SBScheme_Parse( const char *text, sb_scheme_t *out );

// Resolve one "R G B A" / "R G B" / named-colour value against the file's own
// Colors section. Exposed for host testing.
int SBScheme_ResolveValue( const char *text, const char *value, unsigned char *out_rgba );

#ifdef __cplusplus
}
#endif

#endif // CL_SB_SCHEME_H
