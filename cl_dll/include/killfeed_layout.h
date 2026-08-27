/*
 * killfeed_layout.h - engine-independent killfeed logic (CS2-style death notice)
 *
 * This header holds the PURE logic of the killfeed: decoding ReGameDLL kill
 * rarity flags into an ordered list of modifier icons, and computing row
 * width / element x-offsets from measured pixel metrics. It deliberately has
 * NO engine dependency (only ints), so it can be unit-tested on the host with
 * a plain gcc harness (see tests/test_killfeed.c) and reused verbatim by
 * death.cpp on the device.
 *
 * Icon slots are indices; death.cpp maps each slot to a loaded SPR32 handle.
 *
 * Ordering follows the real CS killfeed the user referenced:
 *     [blind] Killer [+flashassist Assister] (wing:inair) WEAPON
 *     [noscope][smoke][penetrate][headshot] Victim
 */
#ifndef KILLFEED_LAYOUT_H
#define KILLFEED_LAYOUT_H

/* Mirror of ReGameDLL KILLRARITY_* (dlls/gamerules.h). Kept as literals so the
 * client does not need to include server headers. Verified against
 * 3rdparty/ReGameDLL_CS/regamedll/dlls/gamerules.h. */
#define KF_RARITY_HEADSHOT      0x001
#define KF_RARITY_KILLER_BLIND  0x002
#define KF_RARITY_NOSCOPE       0x004
#define KF_RARITY_PENETRATED    0x008
#define KF_RARITY_THRUSMOKE     0x010
#define KF_RARITY_ASSISTEDFLASH 0x020
#define KF_RARITY_DOMINATION    0x080
#define KF_RARITY_REVENGE       0x100
#define KF_RARITY_INAIR         0x200

/* Modifier icon slots (index into CHudDeathNotice::m_ModIcons[]). */
enum kf_icon_slot
{
	KFI_BLIND = 0,   /* eye, pre-killer            */
	KFI_INAIR,       /* wing, raised above weapon  */
	KFI_NOSCOPE,     /* mid                        */
	KFI_SMOKE,       /* mid                        */
	KFI_PENETRATE,   /* mid                        */
	KFI_HEADSHOT,    /* mid                        */
	KFI_FLASHASSIST, /* between killer and assister*/
	KFI_COUNT
};

/* Decoded, ordered modifier layout for one row. */
typedef struct
{
	int pre[4];   int nPre;   /* icons before the killer name        */
	int wing;                 /* raised icon slot, or -1             */
	int mid[4];   int nMid;   /* icons after the weapon, before victim */
	int flashAssist;          /* 1 if a flash-assist icon is shown   */
} kf_row_mods;

/* Decode server rarity bitfield (+ legacy headshot byte) into ordered icons.
 * headshotByte is the 3rd DeathMsg byte (works even when KILLRARITY not sent). */
static inline void kf_decode_modifiers( int rarity, int headshotByte,
										kf_row_mods *out )
{
	out->nPre = 0;
	out->nMid = 0;
	out->wing = -1;
	out->flashAssist = ( rarity & KF_RARITY_ASSISTEDFLASH ) ? 1 : 0;

	/* pre-killer: killer state */
	if( rarity & KF_RARITY_KILLER_BLIND )
		out->pre[out->nPre++] = KFI_BLIND;

	/* raised wing: airborne kill */
	if( rarity & KF_RARITY_INAIR )
		out->wing = KFI_INAIR;

	/* mid (method), fixed order noscope -> smoke -> penetrate -> headshot */
	if( rarity & KF_RARITY_NOSCOPE )
		out->mid[out->nMid++] = KFI_NOSCOPE;
	if( rarity & KF_RARITY_THRUSMOKE )
		out->mid[out->nMid++] = KFI_SMOKE;
	if( rarity & KF_RARITY_PENETRATED )
		out->mid[out->nMid++] = KFI_PENETRATE;
	if( ( rarity & KF_RARITY_HEADSHOT ) || headshotByte )
		out->mid[out->nMid++] = KFI_HEADSHOT;
}

/* Ease-out cubic for slide/fade, t in [0,1]. */
static inline float kf_ease_out( float t )
{
	float u;
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;
	u = 1.0f - t;
	return 1.0f - u * u * u;
}

/* Per-row animation state at a given age (ms since spawn) and death age
 * (ms since expiry, <=0 while alive). Fills alpha [0..1] and slide dx (px, +=right).
 * enterMs/exitMs are durations; slidePx is the max horizontal offset. */
static inline void kf_anim_state( float ageMs, float deathAgeMs,
								   float enterMs, float exitMs, float slidePx,
								   float *alphaOut, float *dxOut )
{
	float alpha = 1.0f;
	float dx = 0.0f;

	/* ENTER: CS2 does not slide rows in — a new row simply appears at full
	 * opacity (older rows shove up instantly). The user asked for exactly this,
	 * so there is no enter animation: alpha starts at 1 and dx stays 0. */
	(void)ageMs; (void)enterMs; (void)slidePx;

	/* EXIT: rows fade out as they expire (also how the top row disappears when
	 * the feed overflows). Kept, no horizontal drift. */
	if( deathAgeMs > 0.0f )
	{
		float p = deathAgeMs / exitMs;
		if( p > 1.0f ) p = 1.0f;
		alpha = 1.0f - p;
	}
	if( alpha < 0.0f ) alpha = 0.0f;
	*alphaOut = alpha;
	*dxOut = dx;
}

#endif /* KILLFEED_LAYOUT_H */
