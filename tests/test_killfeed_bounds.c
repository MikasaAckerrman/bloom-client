/* Worst-case element count against KF_MAX_ELEMS.
 *
 * KF_BuildRow writes into kf_elem el[KF_MAX_ELEMS] with a macro that has NO
 * bounds check in the hot path. Count the maximum an all-flags-set kill can
 * produce and prove it fits, so a future extra icon cannot silently overflow
 * the stack array in death.cpp.
 *
 * Element order in KF_BuildRow:
 *   nPre icons, killer text, [plus, flash icon, assister text],
 *   wing icon, weapon icon, nMid icons, victim text
 */
#include <stdio.h>
#include "../cl_dll/include/killfeed_layout.h"

int main( void )
{
	kf_row_mods m;
	int rarity, worst = 0, worstRarity = 0;
	int fails = 0;

	/* every bit combination the server can send in the low 10 bits */
	for( rarity = 0; rarity < 1024; rarity++ )
	{
		int n;
		kf_decode_modifiers( rarity, 1, &m );

		n  = m.nPre;              /* pre icons                */
		n += 1;                   /* killer name              */
		n += 1;                   /* the '+' glue             */
		n += m.flashAssist ? 1 : 0;
		n += 1;                   /* assister name            */
		n += ( m.wing >= 0 ) ? 1 : 0;
		n += 1;                   /* weapon icon              */
		n += m.nMid;              /* mid icons                */
		n += 1;                   /* victim name              */

		if( n > worst ) { worst = n; worstRarity = rarity; }

		if( m.nPre > (int)( sizeof(m.pre)/sizeof(m.pre[0]) ) ) {
			printf( "FAIL: nPre %d overflows pre[] at rarity 0x%03x\n",
					m.nPre, rarity );
			fails++;
		}
		if( m.nMid > (int)( sizeof(m.mid)/sizeof(m.mid[0]) ) ) {
			printf( "FAIL: nMid %d overflows mid[] at rarity 0x%03x\n",
					m.nMid, rarity );
			fails++;
		}
	}

	printf( "worst-case elements: %d (rarity 0x%03x), KF_MAX_ELEMS = %d\n",
			worst, worstRarity, KF_MAX_ELEMS );

	if( worst > KF_MAX_ELEMS ) {
		printf( "FAIL: a row can need %d elements but the array holds %d\n",
				worst, KF_MAX_ELEMS );
		fails++;
	}

	/* headroom report: how many icons could be added before overflow */
	printf( "headroom: %d more elements\n", KF_MAX_ELEMS - worst );

	if( fails ) {
		printf( "%d FAILURE(S)\n", fails );
		return 1;
	}
	printf( "ALL KILLFEED BOUNDS TESTS PASSED\n" );
	return 0;
}
